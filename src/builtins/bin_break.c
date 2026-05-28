/**
 * @file bin_break.c
 * @brief `break` builtin -- exit from enclosing loop
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

/**
 * @brief Break out of enclosing loop
 *
 * Exits from a for, while, or until loop. An optional numeric argument
 * specifies how many levels of loops to break out of.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or invalid argument
 */
int bin_break(int argc, char **argv) {
    if (!current_executor || current_executor->loop_depth <= 0) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_LOOP_CONTROL, SHELL_SEVERITY_ERROR,
                               loc, "not currently in a loop");
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
                err, "break is only valid inside while/until/for loops");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: break: not currently in a loop\n");
        }
        return 1;
    }

    /// Parse optional level argument (break n)
    int break_level = 1;
    if (argc > 1) {
        char *endptr;
        break_level = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || break_level <= 0) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%s: numeric argument required", argv[1]);
            if (err) {
                if (SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
                shell_error_set_suggestion(err,
                                           "level must be a positive integer");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr, "lush: break: %s: numeric argument required\n",
                        argv[1]);
            }
            return 1;
        }

        if (break_level > current_executor->loop_depth) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%d: cannot break %d levels (only %d nested)", break_level,
                break_level, current_executor->loop_depth);
            if (err) {
                if (SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
                shell_error_set_suggestion(
                    err, "level cannot exceed the current loop nesting depth");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr,
                        "lush: break: %d: cannot break %d levels "
                        "(only %d nested)\n",
                        break_level, break_level, current_executor->loop_depth);
            }
            return 1;
        }
    }

    /// Set loop control state to break
    current_executor->loop_control = LOOP_BREAK;

    return 0;
}
