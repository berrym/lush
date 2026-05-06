/**
 * @file bin_continue.c
 * @brief `continue` builtin -- skip to next loop iteration
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"

/**
 * @brief Continue to the next iteration of enclosing loop
 *
 * Skips the remaining commands in the current loop iteration and
 * continues with the next iteration. An optional numeric argument
 * specifies which enclosing loop to continue.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or invalid argument
 */
int bin_continue(int argc, char **argv) {
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
                err, "continue is only valid inside while/until/for loops");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: continue: not currently in a loop\n");
        }
        return 1;
    }

    // Parse optional level argument (continue n)
    int continue_level = 1;
    if (argc > 1) {
        char *endptr;
        continue_level = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || continue_level <= 0) {
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
                fprintf(stderr,
                        "lush: continue: %s: numeric argument required\n",
                        argv[1]);
            }
            return 1;
        }

        if (continue_level > current_executor->loop_depth) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%d: cannot continue %d levels (only %d nested)",
                continue_level, continue_level, current_executor->loop_depth);
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
                        "lush: continue: %d: cannot continue %d levels "
                        "(only %d nested)\n",
                        continue_level, continue_level,
                        current_executor->loop_depth);
            }
            return 1;
        }
    }

    // Set loop control state to continue
    current_executor->loop_control = LOOP_CONTINUE;

    return 0;
}
