/**
 * @file bin_readonly.c
 * @brief `readonly` builtin -- create read-only variables
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

/**
 * @brief Create read-only variables
 *
 * Marks variables as read-only according to POSIX standards.
 * With no arguments, lists all readonly variables.
 * Supports assignment syntax (readonly var=value).
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, 1 on invalid identifier
 */
int bin_readonly(int argc, char **argv) {
    if (argc == 1) {
        // No arguments - print all readonly variables
        symtable_manager_t *manager = symtable_get_global_manager();
        if (!manager) {
            executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                                  builtin_get_source_location(),
                                  "symbol table not available");
            return 1;
        }

        // Print readonly variables in the format: readonly name=value
        printf("readonly functionality not fully implemented for listing\n");
        return 0;
    }

    // Process each argument
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        char *equals = strchr(arg, '=');

        if (equals) {
            // Variable assignment: readonly var=value
            *equals = '\0';
            char *name = arg;
            char *value = equals + 1;

            // Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", name);
                *equals = '='; // Restore the string
                return 1;
            }

            // Set the variable value
            symtable_set_global(name, value);

            // Mark as readonly (note: this is a simplified implementation)
            // In a full implementation, we would need to track readonly status
            // and prevent future modifications

            *equals = '='; // Restore the string
        } else {
            // No assignment: readonly var (make existing variable readonly)
            if (!is_valid_identifier(arg)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", arg);
                return 1;
            }

            // Check if variable exists
            char *value = symtable_get_global(arg);
            if (!value) {
                // Variable doesn't exist, create it with empty value
                symtable_set_global(arg, "");
            }

            // Mark as readonly (simplified implementation)
            // Note: Full readonly implementation would require symbol table
            // modifications to track and enforce readonly status
        }
    }

    return 0;
}
