/**
 * @file arithmetic.c
 * @brief Arithmetic expansion entry point and error state for lush.
 *
 * This module is the front door for arithmetic evaluation. It owns the
 * public entry points (arithm_expand / arithm_expand_with_executor) and the
 * structured error state the executor reads to render diagnostics. The actual
 * evaluation is performed by the AST engine:
 *
 *     expand_variables_in_string  ->  arith_lex  ->  arith_parse  ->
 * arith_ast_eval (Layer-0 $-form expansion)      (lexer)        (Pratt parser)
 * (tree walk)
 *
 * The lexer, parser, and evaluator live in arithmetic_lex.c,
 * arithmetic_parse.c, and arithmetic_eval.c. This file strips a $(( )) wrapper,
 * runs the Layer-0 expansion pre-pass, drives the engine pipeline, and forwards
 * a lexer/parser/ evaluator diagnostic (arith_diag_t) into the module error
 * state.
 *
 * The legacy shunting-yard evaluator that previously lived here was replaced
 * by the AST engine (issue #607).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"

#include "arithmetic_ast.h"
#include "brace_match.h"
#include "executor.h"
#include "symtable.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/// Cap on nested `$((...))` recursion depth. A nested $((...)) inside a
/// Layer-0 expansion recurses back into arithm_expand_internal; without a
/// bound a pathologically deep expression would overflow the stack. Real
/// expressions never nest more than a handful deep, so 128 is generous.
#define MAX_ARITH_NEST_DEPTH 128

/// Live nesting depth of the `$((...))` recursion in arithm_expand_internal;
/// incremented only around the recursive call (see MAX_ARITH_NEST_DEPTH).
static int s_arith_nest_depth = 0;

/* Typed error state for the structured shell error system. The pre-2026-04
 * bare globals (arithm_error_flag / arithm_error_message) were deleted as
 * part of the error-system migration; callers now read this state through
 * the accessor functions declared in arithmetic.h.
 *
 * File-scope, rather than returned to each caller, because the executor reads
 * it out of band (arithm_error_is_flagged / arithm_error_code / ...) after a
 * NULL return from arithm_expand to render the diagnostic. The AST engine
 * itself is pure -- it reports failures through a local arith_diag_t --
 * and arithm_raise_diag bridges that diagnostic into this state: the
 * arithmetic module owns evaluation and error capture; the executor owns
 * reporting.
 */
typedef struct {
    bool flagged;
    shell_error_code_t code;
    char message[512];
    char while_context[128];
    char help[256];
} arithm_error_state_t;

static arithm_error_state_t s_arithm_error;

/// Error handling functions
void arithm_init(void) { arithm_clear_error(); }

void arithm_cleanup(void) { arithm_clear_error(); }

void arithm_set_error(shell_error_code_t code, const char *while_context,
                      const char *help, const char *fmt, ...) {
    /// Preserve the first reported error of an evaluation: shunting-yard
    /// cleanup paths often try to set further errors after the original
    /// trip; the original is the actionable one.
    if (s_arithm_error.flagged) {
        return;
    }
    s_arithm_error.flagged = true;
    s_arithm_error.code = code;

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_arithm_error.message, sizeof(s_arithm_error.message), fmt,
              args);
    va_end(args);

    if (while_context) {
        snprintf(s_arithm_error.while_context,
                 sizeof(s_arithm_error.while_context), "%s", while_context);
    } else {
        s_arithm_error.while_context[0] = '\0';
    }
    if (help) {
        snprintf(s_arithm_error.help, sizeof(s_arithm_error.help), "%s", help);
    } else {
        s_arithm_error.help[0] = '\0';
    }
}

void arithm_clear_error(void) {
    s_arithm_error.flagged = false;
    s_arithm_error.code = SHELL_ERR_ARITHMETIC_SYNTAX;
    s_arithm_error.message[0] = '\0';
    s_arithm_error.while_context[0] = '\0';
    s_arithm_error.help[0] = '\0';
}

bool arithm_error_is_flagged(void) { return s_arithm_error.flagged; }

bool arithm_expansion_in_progress(void) { return s_arith_nest_depth > 0; }

shell_error_code_t arithm_error_code(void) { return s_arithm_error.code; }

const char *arithm_error_message(void) {
    return s_arithm_error.message[0] ? s_arithm_error.message : NULL;
}

const char *arithm_error_while(void) {
    return s_arithm_error.while_context[0] ? s_arithm_error.while_context
                                           : NULL;
}

const char *arithm_error_help(void) {
    return s_arithm_error.help[0] ? s_arithm_error.help : NULL;
}

/**
 * @brief Forward a lexer/parser/evaluator diagnostic into the module's
 *        structured error state so the executor renders it like any other
 *        arithmetic error.
 */
static void arithm_raise_diag(const arith_diag_t *diag) {
    if (!diag || !diag->flagged) {
        arithm_set_error(
            SHELL_ERR_ARITHMETIC_SYNTAX, "evaluating an arithmetic expression",
            "check operator and operand placement", "arithmetic error");
        return;
    }
    arithm_set_error(diag->code, diag->while_ctx, diag->help, "%s",
                     diag->message);
}

/// Main arithmetic expansion entry point (executor-aware).
static char *arithm_expand_internal(void *executor, const char *orig_expr);

char *arithm_expand(const char *orig_expr) {
    return arithm_expand_internal(NULL, orig_expr);
}

char *arithm_expand_with_executor(executor_t *executor, const char *orig_expr) {
    return arithm_expand_internal(executor, orig_expr);
}

/**
 * @brief Expand an arithmetic expression to its integer value as a string.
 *
 * The pipeline is: strip a $(( )) wrapper, run the Layer-0 expansion pre-pass
 * (expand_variables_in_string resolves $var / ${...} / $(...) / $((...)) to a
 * pure arithmetic string, without word-splitting or globbing), then lex, parse
 * (Pratt), and evaluate (tree walk) via the AST engine. A lexer, parser, or
 * evaluator diagnostic is forwarded to the module error state for the executor
 * to render. Returns a freshly allocated result string, or NULL on error.
 */
static char *arithm_expand_internal(void *executor, const char *orig_expr) {
    if (!orig_expr) {
        return strdup("0");
    }

    /// Clear the error state only at the outermost arithmetic expansion. A
    /// nested $((...)) expanded during Layer 0 re-enters this function; if it
    /// cleared the state, an error a sibling nested expansion already flagged
    /// (e.g. the `$((1/0))` in `$(( $((1/0)) + $((5)) ))`) would be wiped, and
    /// the outer evaluation would silently produce a wrong value.
    if (s_arith_nest_depth == 0) {
        arithm_clear_error();
    }

    /// Strip a whole-expression $(( )) wrapper if present.
    ///
    /// "Starts with $(( and ends with ))" is NOT sufficient: an expression may
    /// merely BEGIN with a nested expansion. The `((` after the `$` has to be
    /// the pair that closes at the very end, which is a balance question, so
    /// ask the canonical matcher instead of looking at the last two bytes.
    ///
    ///   $((a)) + $((b))  both tests pass, but on DIFFERENT parens -- the naive
    ///                    strip produced `a)) + $((b` (#613)
    ///   $((1))+1         starts right, ends wrong -- the naive test called a
    ///                    valid expression malformed and hard-errored, which
    ///                    the ${arr[...]} read path swallowed into an empty
    ///                    result (#646)
    ///
    /// Both are the same mistake. A leading `$((` that closes early is simply a
    /// nested expansion: leave it for Layer 0, which expands it correctly.
    const char *expr = orig_expr;
    char *unwrapped = NULL;
    if (strncmp(orig_expr, "$((", 3) == 0) {
        size_t len = strlen(orig_expr);
        size_t close = 0;
        /// orig_expr + 1 starts at the `((`, whose OUTER paren is the one a
        /// true wrapper's trailing `))` closes.
        bool balanced =
            lush_find_matching_brace(orig_expr + 1, len - 1, &close);
        /// The canonical matcher reads SHELL structure -- it honors `#`
        /// comments and case-pattern `)`. Arithmetic text has neither, so a
        /// `#` or an odd quote makes it report "unbalanced" for input whose
        /// `))` is plainly present. Its NEGATIVE answer therefore is not
        /// evidence of a missing `))`; only its positive answer is decisive.
        /// When it declines but the text does end in `))`, treat that as the
        /// wrapper and let the arithmetic lexer -- which reads arithmetic --
        /// name the real defect. Nothing is lost: a truly unterminated `$((`
        /// does not end in `))` and still gets the precise diagnostic below.
        bool ends_with_close =
            len >= 5 && orig_expr[len - 2] == ')' && orig_expr[len - 1] == ')';
        if ((balanced && 1 + close == len - 1) ||
            (!balanced && ends_with_close)) {
            unwrapped = malloc(len - 5 + 1);
            if (!unwrapped) {
                arithm_set_error(SHELL_ERR_OUT_OF_MEMORY,
                                 "preparing an arithmetic expression",
                                 "out of memory", "memory allocation failed");
                return NULL;
            }
            memcpy(unwrapped, orig_expr + 3, len - 5);
            unwrapped[len - 5] = '\0';
            expr = unwrapped;
        } else if (!balanced) {
            /// Unbalanced AND not ending in `))`: genuinely unterminated.
            arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                             "scanning a $(( )) wrapper",
                             "every '$((' needs a matching '))'",
                             "malformed $(( )) arithmetic expansion");
            return NULL;
        }
        /// Balanced but closing early: a leading nested expansion, not a
        /// wrapper. Fall through with expr == orig_expr.
    }

    /// Layer 0: expand $-forms to a pure arithmetic string. Without an executor
    /// there is no scope to expand against, so the expression is taken as-is
    /// (the evaluator resolves bare names against the global scope). The nest
    /// counter bounds recursion through a nested $((...)) that expands back
    /// into this function.
    char *expanded = NULL;
    if (executor) {
        if (s_arith_nest_depth >= MAX_ARITH_NEST_DEPTH) {
            arithm_set_error(SHELL_ERR_ARITHMETIC_SYNTAX,
                             "expanding an arithmetic expression",
                             "reduce the nesting depth",
                             "arithmetic expansion nested too deeply");
            free(unwrapped);
            return NULL;
        }
        s_arith_nest_depth++;
        expanded = expand_variables_in_string((executor_t *)executor, expr);
        s_arith_nest_depth--;
        /// A nested $((...)) expansion that failed leaves the error flagged.
        if (arithm_error_is_flagged()) {
            free(expanded);
            free(unwrapped);
            return NULL;
        }
    }
    const char *pure = expanded ? expanded : expr;

    /// Lex -> parse -> evaluate.
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};

    if (!arith_lex(pure, &tokens, &count, &diag)) {
        arithm_raise_diag(&diag);
        free(expanded);
        free(unwrapped);
        return NULL;
    }

    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    if (!ast) {
        if (diag.flagged) {
            arithm_raise_diag(&diag);
            free(expanded);
            free(unwrapped);
            return NULL;
        }
        /// An empty expression evaluates to 0 (e.g. `$(( ))`).
        free(expanded);
        free(unwrapped);
        return strdup("0");
    }

    ssize_t result = 0;
    bool ok = arith_ast_eval(ast, executor, &result, &diag);
    arith_ast_free(ast);
    free(expanded);
    free(unwrapped);
    if (!ok) {
        arithm_raise_diag(&diag);
        return NULL;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%zd", result);
    return strdup(buf);
}
