/**
 * @file bin_return.c
 * @brief `return` builtin -- return from a function or sourced script
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

/**
 * @brief Return from a function with optional exit code
 *
 * Exits from a shell function with the specified exit status.
 * If no argument is given, returns with status 0.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional exit code)
 * @return Special return code (200 + exit_code) for executor recognition
 */
int bin_return(int argc, char **argv) {
    /// Default to last exit status (POSIX behavior)
    int return_code = last_exit_status;

    /// Get executor context to check if we're in a valid return context
    executor_t *executor = get_global_executor();

    /// Check if we're in a function or sourced script
    bool in_function = executor && executor->symtable &&
                       symtable_in_function_scope(executor->symtable);
    bool in_source = executor && executor->source_depth > 0;

    if (!in_function && !in_source) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err = shell_error_create(
            SHELL_ERR_RETURN_OUTSIDE_FUNC, SHELL_SEVERITY_ERROR, loc,
            "can only `return' from a function or sourced script");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err,
                "return is only valid inside a function or sourced script");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: return: can only `return' from a function or "
                    "sourced script\n");
        }
        return 1;
    }

    /// Parse optional return code argument
    if (argc > 1) {
        char *endptr;
        return_code = strtol(argv[1], &endptr, 10);

        /// Validate that the argument is a valid number
        if (*endptr != '\0') {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%s: numeric argument required", argv[1]);
            if (err) {
                if (current_executor && SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                if (current_executor) {
                    for (size_t i = 0; i < current_executor->context_depth &&
                                       i < SHELL_ERROR_CONTEXT_MAX;
                         i++) {
                        if (current_executor->context_stack[i]) {
                            shell_error_push_context(
                                err, "%s", current_executor->context_stack[i]);
                        }
                    }
                }
                shell_error_set_suggestion(
                    err, "exit status must be an integer (0-255)");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr, "lush: return: %s: numeric argument required\n",
                        argv[1]);
            }
            return 1;
        }
    }

    /// Set the last exit status to the return code
    last_exit_status = return_code;

    /// Return a special exit code that the executor can recognize as "function
    /// return" We'll use a specific value that doesn't conflict with normal
    /// exit codes
    return 200 +
           (return_code & 0xFF); /// 200-455 range for function/source returns
}
