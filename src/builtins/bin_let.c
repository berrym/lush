/**
 * @file bin_let.c
 * @brief `let` builtin -- evaluate arithmetic expressions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "arithmetic.h"
#include "builtins.h"
#include "symtable.h"

/**
 * @brief Evaluate arithmetic expressions (let builtin)
 *
 * Evaluates each argument as an arithmetic expression.
 * Returns 0 if the last expression evaluates to non-zero,
 * returns 1 if the last expression evaluates to zero.
 *
 * Usage: let expr [expr ...]
 *
 * Examples:
 *   let x=5+3        # Sets x to 8
 *   let "x += 1"     # Increments x
 *   let a=1 b=2 c=a+b  # Multiple expressions
 *
 * @param argc Argument count
 * @param argv Argument vector containing arithmetic expressions
 * @return 0 if last expression is non-zero, 1 if zero or on error
 */
int bin_let(int argc, char **argv) {
    if (argc < 2) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *error =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing arithmetic expression");
        if (error) {
            /* Source-line snippet block from executor's batch text. */
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(error, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            /* Walk executor's context stack — already includes
             * "in builtin 'let'" pushed by the dispatcher. */
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth; i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            error, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_detail(
                error, "let requires at least one expression to evaluate");
            shell_error_set_suggestion(
                error,
                "usage: let expr [expr ...]\n"
                "   examples: let x=5+3    let \"x++\"    let a=1 b=2 c=a+b");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
        }
        return 1;
    }

    ssize_t last_result = 0;

    /* Evaluate each argument as an arithmetic expression */
    for (int i = 1; i < argc; i++) {
        arithm_clear_error();
        char *result = arithm_expand(argv[i]);

        if (arithm_error_is_flagged() || !result) {
            const char *err_msg = arithm_error_message();
            const char *err_while = arithm_error_while();
            const char *err_help = arithm_error_help();
            shell_error_code_t err_code = arithm_error_is_flagged()
                                              ? arithm_error_code()
                                              : SHELL_ERR_ARITHMETIC_SYNTAX;
            source_location_t loc = builtin_get_source_location();
            shell_error_t *error =
                shell_error_create(err_code, SHELL_SEVERITY_ERROR, loc,
                                   "invalid expression '%s'", argv[i]);
            if (error) {
                /* Source-line snippet block from executor's batch text. */
                if (current_executor && SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(error, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                /* Walk executor's context stack — already includes
                 * "in builtin 'let'" pushed by the dispatcher. */
                if (current_executor) {
                    for (size_t i = 0; i < current_executor->context_depth;
                         i++) {
                        if (current_executor->context_stack[i]) {
                            shell_error_push_context(
                                error, "%s",
                                current_executor->context_stack[i]);
                        }
                    }
                }
                shell_error_push_context(error, "evaluating argument %d of %d",
                                         i, argc - 1);
                if (err_while) {
                    shell_error_push_context(error, "%s", err_while);
                }
                if (err_msg) {
                    shell_error_set_detail(error, err_msg);
                }
                /* Prefer the per-site help from arithmetic.c when present;
                 * fall back to the generic let-builtin operator cheat sheet. */
                if (err_help) {
                    shell_error_set_suggestion(error, err_help);
                } else {
                    shell_error_set_suggestion(
                        error,
                        "supported operators: + - * / %% ** ++ -- = += -= "
                        "*= /= %%=\n"
                        "   comparisons: == != < > <= >= && || !\n"
                        "   note: variables don't need $: let x=5 y=x+1");
                }
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
            }
            if (result) {
                free(result);
            }
            return 1;
        }

        last_result = strtol(result, NULL, 10);
        free(result);
    }

    /* Return 0 if last result is non-zero, 1 if zero (bash behavior) */
    return (last_result == 0) ? 1 : 0;
}
