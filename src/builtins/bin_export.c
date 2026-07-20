/**
 * @file bin_export.c
 * @brief `export` builtin -- export variables to the environment
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/lle_pager.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>

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
        /// Print all exported variables. The full environment is
        /// commonly several hundred entries deep on containerised
        /// or development setups, so the listing is built into an
        /// open_memstream heap buffer and handed to
        /// lle_pager_present; non-tty, disabled master switch, and
        /// fits-in-one-screen cases stream directly. memstream
        /// allocation failure falls back to the prior streaming
        /// path so output still surfaces.
        extern char **environ;
        char *buf = NULL;
        size_t buf_len = 0;
        FILE *out = open_memstream(&buf, &buf_len);
        FILE *sink = out ? out : stdout;
        for (char **env = environ; *env; env++) {
            fprintf(sink, "export %s\n", *env);
        }
        if (out) {
            fclose(out);
            lle_pager_present(NULL, buf);
            free(buf);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal array-literal sentinel (\x1F): an argv
        /// element with this prefix came from the unquoted `name=(...)`
        /// form. The process environment is a key=string map -- list
        /// values cannot be exported. Under strict value typing (lush
        /// mode), per SEMANTICS section 3.4 (no implicit list-to-string
        /// coercion) and section 3.9 (list in a scalar-requiring slot is
        /// a runtime type error), reject with the structured-error
        /// type-mismatch rather than silently joining the elements into
        /// a string. In the relaxed compat modes the section 3.9
        /// boundary policy follows the oracle instead (see below).
        if (arg[0] == '\x1F') {
            /// Recover the name for the diagnostic / for the relaxed bind.
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
            if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
                /// Relaxed (bash/zsh/posix): bash creates the indexed array
                /// and marks it exported. An array is not an environment
                /// string, so the export attribute is a no-op for the env --
                /// exactly as in bash; the value simply lives in the shell.
                /// A later bare `$name` read follows the mode's section 3.9
                /// flatten (element 0 for bash/posix, whole-join for zsh).
                const char *literal = name_end ? name_end + 1 : NULL;
                if (builtin_bind_array_literal(namebuf, literal,
                                               /*assoc=*/false,
                                               SYMVAR_EXPORTED) != 0) {
                    return 1;
                }
                continue;
            }
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

            /// Check whether the variable exists. symtable_get_global returns
            /// an owned copy used only for this presence test, so free it.
            char *current_value = symtable_get_global(argv[i]);
            if (current_value) {
                /// Variable exists - just export it
                symtable_export_global(argv[i]);
            } else {
                /// Variable doesn't exist - create with empty value and export
                symtable_set_global(argv[i], "");
                symtable_export_global(argv[i]);
            }
            free(current_value);
        }
    }

    return 0;
}
