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
        /// Print all exported variables
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("export %s\n", *env);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal array-literal sentinel (\x1F): an argv
        /// element with this prefix came from the unquoted `name=(...)`
        /// form. The process environment is a key=string map -- list
        /// values cannot be exported. Per SEMANTICS section 3.4 (no
        /// implicit list-to-string coercion) and section 3.9 (list in
        /// a scalar-requiring slot is a runtime type error), reject
        /// with the structured-error type-mismatch rather than
        /// silently joining the elements into a string. bash silently
        /// flattens; lush does not.
        if (arg[0] == '\x1F') {
            /// Recover the name for the diagnostic.
            const char *name_start = arg + 1;
            const char *name_end = strchr(name_start, '=');
            size_t nlen =
                name_end ? (size_t)(name_end - name_start) : strlen(name_start);
            char namebuf[256];
            if (nlen >= sizeof(namebuf)) {
                nlen = sizeof(namebuf) - 1;
            }
            memcpy(namebuf, name_start, nlen);
            namebuf[nlen] = '\0';
            shell_error_t *err = shell_error_create(
                SHELL_ERR_TYPE_MISMATCH, SHELL_SEVERITY_ERROR,
                builtin_get_source_location(),
                "type mismatch: cannot export list value '%s' as an "
                "environment variable",
                namebuf);
            if (err) {
                shell_error_set_suggestion(
                    err,
                    "the process environment is a key=string map; "
                    "export individual elements (export FOO=\"${arr[0]}\") "
                    "or an explicit join (export FOO=\"${arr[*]}\"), or "
                    "use `declare -a -x` for an exported indexed array.");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_TYPE_MISMATCH,
                    builtin_get_source_location(),
                    "type mismatch: cannot export list value '%s' as an "
                    "environment variable",
                    namebuf);
            }
            return 1;
        }
        char *eq = strchr(arg, '=');
        if (eq) {
            /// Variable assignment: VAR=value
            size_t name_len = eq - arg;
            char *name = malloc(name_len + 1);
            if (!name) {
                executor_error_report(current_executor, SHELL_ERR_OUT_OF_MEMORY,
                                      builtin_get_source_location(),
                                      "memory allocation failed");
                return 1;
            }
            strncpy(name, arg, name_len);
            name[name_len] = '\0';

            const char *value = eq + 1;

            /// Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", name);
                free(name);
                return 1;
            }

            /// Set variable value using modern API
            symtable_set_global(name, value);

            /// Export the variable using modern API
            symtable_export_global(name);

            free(name);
        } else if (i + 2 < argc && strcmp(argv[i + 1], "=") == 0) {
            /// Handle tokenized assignment: VAR = value
            const char *name = argv[i];
            const char *value = argv[i + 2];

            /// Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "invalid variable name: %s", name);
                return 1;
            }

            /// Set variable value using modern API
            symtable_set_global(name, value);

            /// Export the variable using modern API
            symtable_export_global(name);

            /// Skip the = and value tokens
            i += 2;
        } else {
            /// Just export existing variable
            if (!is_valid_identifier(argv[i])) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", argv[i]);
                return 1;
            }

            /// Check if variable exists and get its value
            char *current_value = symtable_get_global(argv[i]);
            if (current_value) {
                /// Variable exists - just export it
                symtable_export_global(argv[i]);
            } else {
                /// Variable doesn't exist - create with empty value and export
                symtable_set_global(argv[i], "");
                symtable_export_global(argv[i]);
            }
        }
    }

    return 0;
}
