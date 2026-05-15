/**
 * @file bin_export.c
 * @brief `export` builtin -- export variables to the environment
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

/**
 * @brief Export shell variables to the environment
 *
 * With no arguments, prints all exported variables.
 * With arguments, exports the specified variables to child processes.
 * Supports VAR=value syntax for simultaneous assignment and export.
 *
 * @param argc Argument count
 * @param argv Argument vector with variable names/assignments
 * @return 0 on success, 1 on error
 */
int bin_export(int argc, char **argv) {
    if (argc == 1) {
        // Print all exported variables
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("export %s\n", *env);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            // Variable assignment: VAR=value
            size_t name_len = eq - argv[i];
            char *name = malloc(name_len + 1);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }
            strncpy(name, argv[i], name_len);
            name[name_len] = '\0';

            const char *value = eq + 1;

            // Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", name);
                free(name);
                return 1;
            }

            // Set variable value using modern API
            symtable_set_global(name, value);

            // Export the variable using modern API
            symtable_export_global(name);

            free(name);
        } else if (i + 2 < argc && strcmp(argv[i + 1], "=") == 0) {
            // Handle tokenized assignment: VAR = value
            const char *name = argv[i];
            const char *value = argv[i + 2];

            // Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", name);
                return 1;
            }

            // Set variable value using modern API
            symtable_set_global(name, value);

            // Export the variable using modern API
            symtable_export_global(name);

            // Skip the = and value tokens
            i += 2;
        } else {
            // Just export existing variable
            if (!is_valid_identifier(argv[i])) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", argv[i]);
                return 1;
            }

            // Check if variable exists and get its value
            char *current_value = symtable_get_global(argv[i]);
            if (current_value) {
                // Variable exists - just export it
                symtable_export_global(argv[i]);
            } else {
                // Variable doesn't exist - create with empty value and export
                symtable_set_global(argv[i], "");
                symtable_export_global(argv[i]);
            }
        }
    }

    return 0;
}
