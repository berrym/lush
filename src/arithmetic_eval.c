/**
 * @file arithmetic_eval.c
 * @brief Tree-walking evaluator for the AST arithmetic engine.
 *
 * The third and final layer of the engine (lexer -> parser -> evaluator) and
 * the only one coupled to the symbol table. It walks an AST produced by
 * arith_parse, resolving variable reads and performing assignments through the
 * executor's symbol table.
 *
 * Two behaviors are deliberate curations toward the bash/zsh/dash consensus,
 * fixing shunting-yard artifacts the legacy evaluator carried:
 *
 *   - Variable reads resolve RECURSIVELY: a variable whose value is itself an
 *     arithmetic expression (`x="y+1"; y=5`) evaluates to that expression's
 *     value (6), cycle-protected by a depth cap. The legacy engine truncated
 *     the value with strtol.
 *   - `&&`/`||` short-circuit and only the taken ternary branch is evaluated,
 *     so a side effect (an assignment) in an unreached subexpression does not
 *     occur. The legacy shunting-yard pre-evaluated every operand, so
 *     `0 && (y=5)` wrongly assigned y and `1 ? (a=1) : (b=2)` wrongly assigned
 *     b. A tree walk short-circuits naturally.
 *
 * An evaluation failure fills a caller-provided arith_diag_t with a specific
 * shell_error_code_t so the integration layer raises a structured error.
 */

#include "arithmetic_ast.h"
#include "executor.h"
#include "symtable.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/// The LONG_MIN / LONG_MAX guards below assume the arithmetic integer type
/// (ssize_t) is the same width as long, matching the legacy engine.
_Static_assert(
    sizeof(ssize_t) == sizeof(long),
    "arithmetic evaluator assumes ssize_t and long are the same size");

/// Width of the arithmetic integer type in bits (for shift masking).
#define ARITH_BITS ((size_t)(sizeof(ssize_t) * CHAR_BIT))

/// Maximum recursive variable-resolution depth. Bounds cyclic references
/// (`x="y"; y="x"`) and pathological nesting; overflow is a clean error.
#define ARITH_MAX_VAR_DEPTH 16

/**
 * @brief Evaluation context threaded through the tree walk.
 */
typedef struct {
    executor_t *executor; ///< Symbol-table owner (may be NULL -> global scope)
    arith_diag_t *diag;   ///< Diagnostic filled on failure
    bool failed;          ///< True once an evaluation error has occurred
    int var_depth;        ///< Current recursive variable-resolution depth
} eval_ctx_t;

/**
 * @brief Resolved lvalue handle, produced once by eval_lvalue and consumed by
 *        assignment / increment / decrement. `name` is borrowed from the AST.
 */
typedef struct {
    lvalue_kind_t kind; ///< LVAL_SCALAR or LVAL_ARRAY_INDEX
    const char *name;   ///< Variable name (borrowed)
    ssize_t index;      ///< Resolved integer index (LVAL_ARRAY_INDEX)
} lvalue_t;

static ssize_t eval_node(eval_ctx_t *ctx, const arith_ast_t *n);

/* ==========================================================================
 * Well-defined integer arithmetic (two's-complement wraparound), matching the
 * legacy engine's arithm_wrap_* helpers so results are bit-identical.
 * ========================================================================== */

static ssize_t wrap_neg(ssize_t a) { return (ssize_t)(0 - (size_t)a); }
static ssize_t wrap_add(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a + (size_t)b);
}
static ssize_t wrap_sub(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a - (size_t)b);
}
static ssize_t wrap_mul(ssize_t a, ssize_t b) {
    return (ssize_t)((size_t)a * (size_t)b);
}
static ssize_t safe_div(ssize_t a, ssize_t b) {
    if (a == LONG_MIN && b == -1) {
        return LONG_MIN;
    }
    return a / b;
}
static ssize_t safe_mod(ssize_t a, ssize_t b) {
    if (a == LONG_MIN && b == -1) {
        return 0;
    }
    return a % b;
}
static ssize_t wrap_lsh(ssize_t a, ssize_t count) {
    size_t c = (size_t)count & (ARITH_BITS - 1);
    return (ssize_t)((size_t)a << c);
}
static ssize_t wrap_rsh(ssize_t a, ssize_t count) {
    size_t c = (size_t)count & (ARITH_BITS - 1);
    return a >> c;
}

/**
 * @brief Record an evaluation error and return 0.
 *
 * The first error wins: once failed, later sites do not overwrite it (they
 * short-circuit off ctx->failed anyway).
 */
static ssize_t eval_fail(eval_ctx_t *ctx, shell_error_code_t code,
                         const char *while_ctx, const char *help,
                         const char *message, size_t pos) {
    if (!ctx->failed) {
        ctx->failed = true;
        if (ctx->diag) {
            ctx->diag->flagged = true;
            ctx->diag->code = code;
            ctx->diag->while_ctx = while_ctx;
            ctx->diag->help = help;
            snprintf(ctx->diag->message, sizeof(ctx->diag->message), "%s",
                     message);
            ctx->diag->pos = pos;
            ctx->diag->len = 1;
        }
    }
    return 0;
}

/**
 * @brief Read a scalar variable's string value from the active scope.
 * @return Owned string (caller frees), or NULL if unset.
 */
static char *read_var_string(eval_ctx_t *ctx, const char *name) {
    if (ctx->executor && ctx->executor->symtable) {
        return symtable_get_var(ctx->executor->symtable, name);
    }
    return symtable_get_global(name);
}

/**
 * @brief True if @p s is a pure integer literal (optionally surrounded by
 *        whitespace); if so, its base-0 value is returned via @p out.
 */
static bool parse_pure_integer(const char *s, ssize_t *out) {
    char *end = NULL;
    long value = strtol(s, &end, 0);
    if (end == s) {
        return false; /// no digits consumed
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return false; /// trailing non-numeric content
    }
    *out = (ssize_t)value;
    return true;
}

/**
 * @brief Resolve an owned value string to its integer value (recursively).
 *
 * Takes ownership of @p value (always non-NULL) and frees it. A pure integer
 * literal reads by base 0 (0x hex, 0 octal, decimal -- issue #578); an empty /
 * all-whitespace value is 0; any other value is itself evaluated as an
 * arithmetic expression, depth-capped to bound cycles. Shared by scalar reads
 * (read_scalar) and array-element reads (lvalue_read), so `a[0]="b+1"; b=5`
 * resolves a[0] to 6 exactly as a scalar would.
 */
static ssize_t resolve_value_string(eval_ctx_t *ctx, char *value) {
    ssize_t literal = 0;
    if (parse_pure_integer(value, &literal)) {
        free(value);
        return literal;
    }

    /// An empty / all-whitespace value is 0. The whitespace set matches the
    /// lexer's (arith_lex) exactly, so a value that survives this check has at
    /// least one byte the lexer will not skip -- which lets the parse-failure
    /// path below treat a NULL parse as an unambiguous syntax error.
    const char *p = value;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' ||
           *p == '\f') {
        p++;
    }
    if (*p == '\0') {
        free(value);
        return 0;
    }

    if (ctx->var_depth >= ARITH_MAX_VAR_DEPTH) {
        eval_fail(ctx, SHELL_ERR_ARITH_STACK_OVERFLOW,
                  "resolving a variable reference",
                  "variable values reference each other too deeply (a cycle?)",
                  "arithmetic variable resolution exceeded the depth limit", 0);
        free(value);
        return 0;
    }

    /// Evaluate the value as an arithmetic expression.
    arith_token_t *tokens = NULL;
    size_t count = 0;
    if (!arith_lex(value, &tokens, &count, ctx->diag)) {
        ctx->failed = true;
        free(value);
        return 0;
    }
    arith_ast_t *ast = arith_parse(tokens, count, ctx->diag);
    arith_tokens_free(tokens, count);
    free(value);
    if (!ast) {
        /// The value is non-empty (the whitespace check above matches the
        /// lexer), so it cannot lex to an EOF-only stream: a NULL parse is a
        /// genuine syntax error. Fail even when no diagnostic was provided to
        /// record it into.
        ctx->failed = true;
        return 0;
    }

    ctx->var_depth++;
    ssize_t result = eval_node(ctx, ast);
    ctx->var_depth--;
    arith_ast_free(ast);
    return result;
}

/**
 * @brief Resolve a scalar variable to its integer value (recursively).
 *
 * An unset variable is 0 and is NOT created (issue #601).
 */
static ssize_t read_scalar(eval_ctx_t *ctx, const char *name) {
    char *value = read_var_string(ctx, name);
    if (!value) {
        return 0;
    }
    return resolve_value_string(ctx, value);
}

/**
 * @brief Resolve an lvalue node to a handle, evaluating a subscript once.
 *
 * The parser guarantees @p n is AST_VAR or AST_INDEX.
 */
static lvalue_t eval_lvalue(eval_ctx_t *ctx, const arith_ast_t *n) {
    lvalue_t lv = {.kind = LVAL_SCALAR, .name = n->name, .index = 0};
    if (n->kind == AST_INDEX) {
        /// Associative-array subscripts are literal string keys, not arithmetic
        /// (bash and zsh agree: `m[foo]` keys on "foo", never on the value of
        /// foo). Supporting that needs the raw subscript text, which the engine
        /// does not yet thread through -- issue #603 scopes indexed arrays
        /// first. Decline associative targets with a specific error rather than
        /// arithmetic-evaluating the key and writing under the wrong subscript.
        /// The check precedes subscript evaluation so a key side effect
        /// (`m[i++]`) does not fire on the unsupported path.
        array_value_t *arr = symtable_get_array(n->name);
        if (arr && arr->is_associative) {
            eval_fail(ctx, SHELL_ERR_ARITHMETIC_SYNTAX,
                      "resolving an array subscript",
                      "associative arrays in arithmetic are not yet supported",
                      "associative array subscripts are not supported in "
                      "arithmetic yet",
                      n->pos);
            return lv;
        }
        lv.kind = LVAL_ARRAY_INDEX;
        lv.index = eval_node(ctx, n->a); /// subscript evaluated exactly once
    }
    return lv;
}

/**
 * @brief Read the current value of a resolved lvalue.
 */
static ssize_t lvalue_read(eval_ctx_t *ctx, const lvalue_t *lv) {
    if (lv->kind == LVAL_SCALAR) {
        return read_scalar(ctx, lv->name);
    }
    /// Array element (issue #603). The subscript was evaluated once by
    /// eval_lvalue; render it as a decimal string for the symtable API, which
    /// applies the mode's index base (zsh 1-indexed vs 0-indexed). An unset or
    /// out-of-range element is 0 and is NOT created, matching an unset scalar;
    /// a set value resolves recursively exactly as a scalar value does.
    char subscript[32];
    snprintf(subscript, sizeof(subscript), "%zd", lv->index);
    char *value = symtable_get_array_element(lv->name, subscript);
    if (!value) {
        return 0;
    }
    return resolve_value_string(ctx, value);
}

/**
 * @brief Write a value to a resolved lvalue, honoring the readonly attribute.
 */
static void lvalue_write(eval_ctx_t *ctx, const lvalue_t *lv, ssize_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%zd", value);

    int rc;
    if (lv->kind == LVAL_ARRAY_INDEX) {
        char subscript[32];
        snprintf(subscript, sizeof(subscript), "%zd", lv->index);

        /// Readonly precedence (mirror the executor array-assignment paths,
        /// #614/#620): a readonly array, or a readonly scalar being re-kinded,
        /// is refused with the readonly diagnostic before the kind policy.
        symvar_flags_t ro = symtable_array_get_flags(lv->name);
        if (ctx->executor && ctx->executor->symtable) {
            ro |= symtable_get_flags(ctx->executor->symtable, lv->name);
        }
        if (ro & SYMVAR_READONLY) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "cannot assign to readonly variable '%s'", lv->name);
            eval_fail(ctx, SHELL_ERR_READONLY_VAR,
                      "assigning in an arithmetic expression",
                      "the variable is read-only and cannot be reassigned", msg,
                      0);
            return;
        }

        /// Scalar->array kind transition (#621) for the user `(( s[i]=v ))`
        /// surface. Applied HERE, at the user surface -- not in the low-level
        /// symtable_set_array_element, which shell-internal writers also use.
        /// If the name is not yet an array, strict value typing (lush mode)
        /// refuses the implicit re-kind (the §3.9 mirror); a relaxed mode
        /// preserve-promotes, seeding the former scalar as the base element. An
        /// unbound name is a fresh array with no kind change.
        if (!symtable_get_array(lv->name)) {
            scalar_promo_t promo = symtable_scalar_promotion(lv->name);
            if (promo == SCALAR_PROMO_REFUSE) {
                char msg[96];
                snprintf(msg, sizeof(msg),
                         "cannot apply an array subscript to scalar '%s'",
                         lv->name);
                eval_fail(
                    ctx, SHELL_ERR_TYPE_MISMATCH, "assigning an array element",
                    "declare it a list first (declare -a NAME) or unset it",
                    msg, 0);
                return;
            }
            if (promo == SCALAR_PROMO_PRESERVE) {
                array_value_t *arr = symtable_array_create(false);
                if (arr) {
                    symtable_seed_promoted_scalar(lv->name, arr);
                    symtable_assign_array(lv->name, arr);
                }
            }
        }

        /// Array element (issue #603). The subscript was evaluated once by
        /// eval_lvalue; symtable_set_array_element applies the mode's index
        /// base. A subscript out of the mode's valid range (e.g. negative in
        /// 0-indexed mode) returns -1; report it rather than dropping the
        /// write.
        rc = symtable_set_array_element(lv->name, subscript, buf);
        if (rc == -1) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "array subscript %zd out of range for '%s'", lv->index,
                     lv->name);
            eval_fail(ctx, SHELL_ERR_ARITHMETIC_SYNTAX,
                      "assigning an array element",
                      "the subscript is out of range for this array", msg, 0);
            return;
        }
    } else if (ctx->executor && ctx->executor->symtable) {
        rc = symtable_assign_var(ctx->executor->symtable, lv->name, buf);
    } else {
        rc = symtable_set_global(lv->name, buf);
    }

    if (rc == SYMTABLE_ERR_READONLY) {
        char msg[96];
        snprintf(msg, sizeof(msg), "cannot assign to readonly variable '%s'",
                 lv->name);
        eval_fail(ctx, SHELL_ERR_READONLY_VAR,
                  "assigning in an arithmetic expression",
                  "the variable is read-only and cannot be reassigned", msg, 0);
    }
}

/**
 * @brief Apply a binary arithmetic operator (never a short-circuit / assign).
 */
static ssize_t apply_binop(eval_ctx_t *ctx, arith_token_kind_t op, ssize_t a,
                           ssize_t b, size_t pos) {
    switch (op) {
    case ATK_PLUS:
        return wrap_add(a, b);
    case ATK_MINUS:
        return wrap_sub(a, b);
    case ATK_STAR:
        return wrap_mul(a, b);
    case ATK_SLASH:
        if (b == 0) {
            return eval_fail(
                ctx, SHELL_ERR_DIVISION_BY_ZERO, "evaluating / operator",
                "divisor must be non-zero", "division by zero", pos);
        }
        return safe_div(a, b);
    case ATK_PERCENT:
        if (b == 0) {
            return eval_fail(ctx, SHELL_ERR_MODULO_BY_ZERO,
                             "evaluating % operator",
                             "divisor must be non-zero",
                             "division by zero in modulo operation", pos);
        }
        return safe_mod(a, b);
    case ATK_POW:
        if (b < 0) {
            return eval_fail(ctx, SHELL_ERR_ARITH_NEGATIVE_EXPONENT,
                             "evaluating ** operator",
                             "exponent must be non-negative",
                             "negative exponent not supported", pos);
        }
        {
            ssize_t result = 1;
            for (ssize_t i = 0; i < b; i++) {
                result = wrap_mul(result, a);
            }
            return result;
        }
    case ATK_SHL:
        return wrap_lsh(a, b);
    case ATK_SHR:
        return wrap_rsh(a, b);
    case ATK_LT:
        return a < b;
    case ATK_LE:
        return a <= b;
    case ATK_GT:
        return a > b;
    case ATK_GE:
        return a >= b;
    case ATK_EQ:
        return a == b;
    case ATK_NE:
        return a != b;
    case ATK_AMP:
        return a & b;
    case ATK_CARET:
        return a ^ b;
    case ATK_PIPE:
        return a | b;
    default:
        return eval_fail(ctx, SHELL_ERR_ARITHMETIC_SYNTAX,
                         "evaluating a binary operator", "unsupported operator",
                         "internal: unsupported binary operator", pos);
    }
}

/**
 * @brief Map a compound-assignment token to its underlying binary operator.
 */
static arith_token_kind_t compound_binop(arith_token_kind_t assign_op) {
    switch (assign_op) {
    case ATK_PLUSEQ:
        return ATK_PLUS;
    case ATK_MINUSEQ:
        return ATK_MINUS;
    case ATK_STAREQ:
        return ATK_STAR;
    case ATK_SLASHEQ:
        return ATK_SLASH;
    case ATK_PERCENTEQ:
        return ATK_PERCENT;
    case ATK_SHLEQ:
        return ATK_SHL;
    case ATK_SHREQ:
        return ATK_SHR;
    case ATK_AMPEQ:
        return ATK_AMP;
    case ATK_PIPEEQ:
        return ATK_PIPE;
    case ATK_CARETEQ:
        return ATK_CARET;
    default:
        return ATK_ASSIGN; /// plain '='
    }
}

/**
 * @brief Evaluate an AST node to an integer value.
 */
static ssize_t eval_node(eval_ctx_t *ctx, const arith_ast_t *n) {
    if (!n || ctx->failed) {
        return 0;
    }

    switch (n->kind) {
    case AST_NUM:
        return (ssize_t)n->num;

    case AST_VAR:
        return read_scalar(ctx, n->name);

    case AST_INDEX: {
        lvalue_t lv = eval_lvalue(ctx, n);
        if (ctx->failed) {
            return 0;
        }
        return lvalue_read(ctx, &lv);
    }

    case AST_UNOP: {
        ssize_t v = eval_node(ctx, n->a);
        switch (n->op) {
        case ATK_MINUS:
            return wrap_neg(v);
        case ATK_PLUS:
            return v;
        case ATK_NOT:
            return !v;
        case ATK_TILDE:
            return ~v;
        default:
            return eval_fail(ctx, SHELL_ERR_ARITHMETIC_SYNTAX,
                             "evaluating a unary operator",
                             "unsupported operator",
                             "internal: unsupported unary operator", n->pos);
        }
    }

    case AST_BINOP:
        /// Short-circuit the logical operators: the right operand (and any of
        /// its side effects) is evaluated only when it can affect the result.
        if (n->op == ATK_AND) {
            ssize_t a = eval_node(ctx, n->a);
            if (ctx->failed || a == 0) {
                return 0;
            }
            return eval_node(ctx, n->b) != 0;
        }
        if (n->op == ATK_OR) {
            ssize_t a = eval_node(ctx, n->a);
            if (ctx->failed) {
                return 0;
            }
            if (a != 0) {
                return 1;
            }
            return eval_node(ctx, n->b) != 0;
        }
        {
            ssize_t a = eval_node(ctx, n->a);
            ssize_t b = eval_node(ctx, n->b);
            if (ctx->failed) {
                return 0;
            }
            return apply_binop(ctx, n->op, a, b, n->pos);
        }

    case AST_COMMA: {
        /// Evaluate the left for its side effects, discard it, yield the right.
        eval_node(ctx, n->a);
        return eval_node(ctx, n->b);
    }

    case AST_ASSIGN: {
        lvalue_t lv = eval_lvalue(ctx, n->a);
        if (ctx->failed) {
            return 0;
        }
        ssize_t value;
        if (n->op == ATK_ASSIGN) {
            value = eval_node(ctx, n->b);
        } else {
            ssize_t current = lvalue_read(ctx, &lv);
            ssize_t rhs = eval_node(ctx, n->b);
            if (ctx->failed) {
                return 0;
            }
            value =
                apply_binop(ctx, compound_binop(n->op), current, rhs, n->pos);
        }
        if (ctx->failed) {
            return 0;
        }
        lvalue_write(ctx, &lv, value);
        return value;
    }

    case AST_PREINC:
    case AST_PREDEC: {
        lvalue_t lv = eval_lvalue(ctx, n->a);
        if (ctx->failed) {
            return 0;
        }
        ssize_t cur = lvalue_read(ctx, &lv);
        if (ctx->failed) {
            return 0;
        }
        ssize_t updated =
            (n->kind == AST_PREINC) ? wrap_add(cur, 1) : wrap_sub(cur, 1);
        lvalue_write(ctx, &lv, updated);
        return updated;
    }

    case AST_POSTINC:
    case AST_POSTDEC: {
        lvalue_t lv = eval_lvalue(ctx, n->a);
        if (ctx->failed) {
            return 0;
        }
        ssize_t cur = lvalue_read(ctx, &lv);
        if (ctx->failed) {
            return 0;
        }
        ssize_t updated =
            (n->kind == AST_POSTINC) ? wrap_add(cur, 1) : wrap_sub(cur, 1);
        lvalue_write(ctx, &lv, updated);
        return cur;
    }

    case AST_TERNARY: {
        /// Evaluate only the taken branch, so a side effect in the other one
        /// does not occur.
        ssize_t cond = eval_node(ctx, n->a);
        if (ctx->failed) {
            return 0;
        }
        return (cond != 0) ? eval_node(ctx, n->b) : eval_node(ctx, n->c);
    }
    }

    return eval_fail(ctx, SHELL_ERR_ARITHMETIC_SYNTAX,
                     "evaluating an arithmetic expression", "unsupported node",
                     "internal: unsupported AST node", n->pos);
}

bool arith_ast_eval(const arith_ast_t *ast, void *executor, ssize_t *out,
                    arith_diag_t *diag) {
    eval_ctx_t ctx = {
        .executor = (executor_t *)executor,
        .diag = diag,
        .failed = false,
        .var_depth = 0,
    };
    ssize_t result = eval_node(&ctx, ast);
    if (ctx.failed) {
        return false;
    }
    if (out) {
        *out = result;
    }
    return true;
}
