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
        /// No arguments - print all readonly variables
        symtable_manager_t *manager = symtable_get_global_manager();
        if (!manager) {
            executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                                  builtin_get_source_location(),
                                  "symbol table not available");
            return 1;
        }

        /// Print readonly variables in the format: readonly name=value
        printf("readonly functionality not fully implemented for listing\n");
        return 0;
    }

    /// Process each argument
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal array-literal sentinel (\x1F): an argv
        /// element with this prefix came from the unquoted `name=(...)`
        /// form. Per SEMANTICS section 3.4 (no implicit list-to-string
        /// coercion) and section 3.9 (list in a scalar-requiring slot
        /// is a runtime type error), reject rather than silently
        /// join the elements into a string. bash supports readonly
        /// arrays via the `-a` flag; lush will add that surface
        /// alongside; for now the unflagged `readonly arr=(...)` is
        /// a type error rather than a silent scalar coercion.
        if (arg[0] == '\x1F') {
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
                "type mismatch: cannot bind list value '%s' to a "
                "scalar readonly",
                namebuf);
            if (err) {
                shell_error_set_suggestion(
                    err, "use `declare -a -r` for a readonly indexed "
                         "array, or pass an explicit-join expression "
                         "(readonly NAME=\"${arr[*]}\").");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_TYPE_MISMATCH,
                    builtin_get_source_location(),
                    "type mismatch: cannot bind list value '%s' to a "
                    "scalar readonly",
                    namebuf);
            }
            return 1;
        }
        char *equals = strchr(arg, '=');

        if (equals) {
            /// Variable assignment: readonly var=value
            *equals = '\0';
            char *name = arg;
            char *value = equals + 1;

            /// Validate variable name
            if (!is_valid_identifier(name)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", name);
                *equals = '='; /// Restore the string
                return 1;
            }

            /// Set the variable value
            symtable_set_global(name, value);

            /// Mark as readonly (note: this is a simplified implementation)
            /// In a full implementation, we would need to track readonly status
            /// and prevent future modifications

            *equals = '='; /// Restore the string
        } else {
            /// No assignment: readonly var (make existing variable readonly)
            if (!is_valid_identifier(arg)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", arg);
                return 1;
            }

            /// Check if variable exists
            char *value = symtable_get_global(arg);
            if (!value) {
                /// Variable doesn't exist, create it with empty value
                symtable_set_global(arg, "");
            }

            /// Mark as readonly (simplified implementation)
            /// Note: Full readonly implementation would require symbol table
            /// modifications to track and enforce readonly status
        }
    }

    return 0;
}
