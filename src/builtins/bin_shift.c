/**
 * @file bin_shift.c
 * @brief `shift` builtin -- shift positional parameters
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"
#include "symtable.h"

#include <limits.h>

/**
 * @brief Emit a structured "shift count exceeds positional parameter count"
 *        error for bin_shift.
 *
 * Both the function-scope and global-scope paths in bin_shift detect
 * overshift the same way and need the same diagnostic. Centralised here
 * so the reporting block is one place, not two near-duplicates.
 *
 * Modeled on the bin_let error pattern: source_location_t from the
 * builtin call site, executor context stack, builtin-specific while
 * line, help suggestion. Returns 1 so callers can `return` the result
 * directly.
 *
 * @param shift_count Requested shift amount
 * @param available   Actual count of positional parameters
 * @return Always 1 (so callers can `return report_shift_overflow(...)`)
 */
static int report_shift_overflow(int shift_count, int available) {
    source_location_t loc = builtin_get_source_location();
    shell_error_t *error = shell_error_create(
        SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
        "shift count %d exceeds %d positional parameter%s", shift_count,
        available, available == 1 ? "" : "s");
    if (error) {
        if (current_executor) {
            for (size_t i = 0; i < current_executor->context_depth; i++) {
                if (current_executor->context_stack[i]) {
                    shell_error_push_context(
                        error, "%s", current_executor->context_stack[i]);
                }
            }
        }
        shell_error_push_context(error, "in builtin 'shift'");
        shell_error_set_suggestion(error, "shift count must be <= $#");
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
    }
    return 1;
}

/**
 * @brief Shift positional parameters left
 *
 * Shifts positional parameters ($1, $2, etc.) left by n positions.
 * Default shift count is 1. Updates $# and individual parameters.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional shift count)
 * @return 0 on success, 1 on invalid argument or overshift
 */
int bin_shift(int argc, char **argv) {
    int shift_count = 1; // Default shift by 1

    // Parse optional shift count argument
    if (argc > 1) {
        char *endptr;
        shift_count = strtol(argv[1], &endptr, 10);

        // Validate that the argument is a valid number
        if (*endptr != '\0' || shift_count < 0) {
            fprintf(stderr, "shift: %s: numeric argument required\n", argv[1]);
            return 1;
        }
    }

    // Check if we're in a function scope
    symtable_manager_t *mgr = symtable_get_global_manager();
    if (mgr && symtable_in_function_scope(mgr)) {
        // In function scope - shift local positional parameters
        char *argc_str = symtable_get_var(mgr, "#");
        int func_argc = argc_str ? atoi(argc_str) : 0;
        free(argc_str);

        // POSIX leaves overshift unspecified; dash, bash, and zsh all
        // diagnose and return non-zero. Match the reference shells.
        if (shift_count > func_argc) {
            return report_shift_overflow(shift_count, func_argc);
        }

        if (shift_count > 0 && func_argc > 0) {
            int new_argc = func_argc - shift_count;

            // Collect values of parameters that will remain after shift
            char **new_values = malloc((new_argc + 1) * sizeof(char *));
            if (!new_values) {
                return 1;
            }

            for (int i = 0; i < new_argc; i++) {
                char param_name[16];
                snprintf(param_name, sizeof(param_name), "%d",
                         i + 1 + shift_count);
                new_values[i] = symtable_get_var(mgr, param_name);
            }
            new_values[new_argc] = NULL;

            // Update positional parameters with shifted values
            for (int i = 1; i <= func_argc; i++) {
                char param_name[16];
                snprintf(param_name, sizeof(param_name), "%d", i);
                if (i <= new_argc && new_values[i - 1]) {
                    symtable_set_local_var(mgr, param_name, new_values[i - 1]);
                } else {
                    // Clear parameters beyond new count
                    symtable_set_local_var(mgr, param_name, "");
                }
            }

            // Free collected values
            for (int i = 0; i < new_argc; i++) {
                free(new_values[i]);
            }
            free(new_values);

            // Update $#
            char new_argc_str[16];
            snprintf(new_argc_str, sizeof(new_argc_str), "%d", new_argc);
            symtable_set_local_var(mgr, "#", new_argc_str);
        }

        return 0;
    }

    // Not in function scope - shift global shell_argv
    // Calculate available parameters to shift (excluding script name at
    // argv[0])
    int available_params = shell_argc > 1 ? shell_argc - 1 : 0;

    // POSIX leaves overshift unspecified; dash, bash, and zsh all
    // diagnose and return non-zero. Match the reference shells.
    if (shift_count > available_params) {
        return report_shift_overflow(shift_count, available_params);
    }

    // Perform the shift by adjusting shell_argc and shell_argv
    if (shift_count > 0 && shell_argc > 1) {
        // Shift the argv array
        for (int i = 1; i < shell_argc - shift_count; i++) {
            shell_argv[i] = shell_argv[i + shift_count];
        }

        // Update argc to reflect the new parameter count
        shell_argc -= shift_count;

        // Update shell variables to reflect the new parameter count
        // Update $# (parameter count)
        char param_count_str[16];
        snprintf(param_count_str, sizeof(param_count_str), "%d",
                 shell_argc > 1 ? shell_argc - 1 : 0);
        symtable_set_global("#", param_count_str);

        // Update individual positional parameters ($1, $2, etc.)
        // Clear old parameters first
        for (int i = 1; i <= 9; i++) {
            char param_name[3];
            snprintf(param_name, sizeof(param_name), "%d", i);
            if (i < shell_argc && shell_argv[i]) {
                symtable_set_global(param_name, shell_argv[i]);
            } else {
                symtable_set_global(param_name, "");
            }
        }
    }

    return 0;
}
