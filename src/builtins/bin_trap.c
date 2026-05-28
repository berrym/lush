/**
 * @file bin_trap.c
 * @brief `trap` builtin -- set or list signal handlers
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "signals.h"

#include <signal.h>

/**
 * @brief Set or display signal traps
 *
 * Manages signal handlers for the shell. Can set, display, or reset
 * traps for signals like EXIT, INT, TERM, HUP, etc.
 *
 * @param argc Argument count
 * @param argv Argument vector with trap action and signal names
 * @return 0 on success, 1 on error
 */
int bin_trap(int argc, char **argv) {
    /// If no arguments, list all traps
    if (argc == 1) {
        list_traps();
        return 0;
    }

    /// Handle special options
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        /// List available signals
        printf("EXIT  0) exit from shell\n");
        printf("HUP   1) hangup\n");
        printf("INT   2) interrupt\n");
        printf("QUIT  3) quit\n");
        printf("TERM  15) software termination signal\n");
        printf("USR1  10) user defined signal 1\n");
        printf("USR2  12) user defined signal 2\n");
        return 0;
    }

    /// Parse arguments: trap [-l] [action] [signal ...]
    int arg_index = 1;

    /// Skip -l option if present
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        arg_index = 2;
    }

    /// Need at least action argument
    if (arg_index >= argc) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "usage: trap [-l] [action] [signal ...]");
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
            shell_error_set_suggestion(err, "trap [-l] [action] [signal ...] — "
                                            "use -l to list supported signals");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: trap: usage: trap [-l] [action] [signal ...]\n");
        }
        return 1;
    }

    const char *action = argv[arg_index++];

    /// If no signals specified, this is an error
    if (arg_index >= argc) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "usage: trap [-l] [action] [signal ...]");
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
            shell_error_set_suggestion(err, "trap [-l] [action] [signal ...] — "
                                            "use -l to list supported signals");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: trap: usage: trap [-l] [action] [signal ...]\n");
        }
        return 1;
    }

    /// Process each signal
    for (int i = arg_index; i < argc; i++) {
        int signal = get_signal_number(argv[i]);

        /// Negative pseudo-signal sentinels (TRAP_PSEUDO_ERR etc.) are
        /// legitimate; only -1 (the "unknown name" return) is rejected.
        if (signal == -1) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%s: invalid signal specification", argv[i]);
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
                    for (size_t i2 = 0; i2 < current_executor->context_depth &&
                                        i2 < SHELL_ERROR_CONTEXT_MAX;
                         i2++) {
                        if (current_executor->context_stack[i2]) {
                            shell_error_push_context(
                                err, "%s", current_executor->context_stack[i2]);
                        }
                    }
                }
                shell_error_set_suggestion(
                    err, "use 'trap -l' to list supported signal names and "
                         "numbers");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr,
                        "lush: trap: %s: invalid signal specification\n",
                        argv[i]);
            }
            return 1;
        }

        /// Handle special cases
        if (strcmp(action, "-") == 0) {
            /// Reset to default
            remove_trap(signal);
        } else if (strcmp(action, "") == 0 || strcmp(action, "\"\"") == 0) {
            /// Ignore signal
            if (signal == 0) {
                /// EXIT trap - remove it
                remove_trap(signal);
            } else {
                set_trap(signal, "");
            }
        } else {
            /// Set trap command
            if (set_trap(signal, action) != 0) {
                executor_error_report(current_executor, SHELL_ERR_TRAP_ERROR,
                                      builtin_get_source_location(),
                                      "failed to set trap for signal %s",
                                      argv[i]);
                return 1;
            }
        }
    }

    return 0;
}
