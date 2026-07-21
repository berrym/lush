/**
 * @file bin_readonly.c
 * @brief `readonly` builtin -- create read-only variables
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
 * @brief Callback for enumerating readonly variables into a FILE*
 *
 * Filters symtable_enumerate_global_vars output to entries whose
 * flags carry SYMVAR_READONLY. Skips internal `__`-prefixed names
 * (these are shell-private metadata that the user never set).
 * Output format matches POSIX: `readonly name='value'` with single
 * quotes around the value.
 */
static void readonly_print_callback(const char *key, const char *value,
                                    void *userdata) {
    FILE *out = (FILE *)userdata;
    if (!out || !key) {
        return;
    }
    if (key[0] == '_' && key[1] == '_') {
        return;
    }
    symtable_manager_t *mgr = symtable_get_global_manager();
    if (!mgr) {
        return;
    }
    symvar_flags_t flags = symtable_get_flags(mgr, key);
    if (!(flags & SYMVAR_READONLY)) {
        return;
    }
    fprintf(out, "readonly %s='%s'\n", key, value ? value : "");
}

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
        /// No arguments - print all readonly variables. Enumerate
        /// every global scalar and filter by the SYMVAR_READONLY flag;
        /// the listing is buffered through open_memstream and handed
        /// to lle_pager_present so long readonly sets paginate. On
        /// memstream allocation failure the callback writes directly
        /// to stdout. The symtable manager existence check is kept
        /// because the callback itself depends on it for flag lookup.
        symtable_manager_t *manager = symtable_get_global_manager();
        if (!manager) {
            executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                                  builtin_get_source_location(),
                                  "symbol table not available");
            return 1;
        }
        char *buf = NULL;
        size_t buf_len = 0;
        FILE *out = open_memstream(&buf, &buf_len);
        FILE *sink = out ? out : stdout;
        symtable_enumerate_global_vars(readonly_print_callback, sink);
        if (out) {
            fclose(out);
            lle_pager_present(NULL, buf);
            free(buf);
        }
        return 0;
    }

    /// Process each argument
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        /// Parser-internal array-literal sentinel (\x1F): an argv
        /// element with this prefix came from the unquoted `name=(...)`
        /// form. Under strict value typing (lush mode), per SEMANTICS
        /// section 3.4 (no implicit list-to-string coercion) and section
        /// 3.9 (list in a scalar-requiring slot is a runtime type error),
        /// reject rather than silently join the elements into a string.
        /// In the relaxed compat modes the section 3.9 boundary policy
        /// follows the oracle: bash binds a readonly indexed array.
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
            /// `name+=(...)` append leaves a trailing `+` on the name; strip
            /// it (and remember to append) so both the relaxed bind and the
            /// strict diagnostic use the clean name.
            bool array_append = builtin_array_name_is_append(namebuf);
            if (!shell_mode_allows(FEATURE_STRICT_VALUE_TYPING)) {
                /// Relaxed (bash/zsh/posix): bind the indexed array with the
                /// readonly attribute, matching bash's `readonly arr=(...)`.
                /// A later bare `$name` read follows the mode's section 3.9
                /// flatten (element 0 for bash/posix, whole-join for zsh).
                const char *literal = name_end ? name_end + 1 : NULL;
                if (builtin_bind_array_literal(namebuf, literal,
                                               /*assoc=*/false, SYMVAR_READONLY,
                                               array_append) != 0) {
                    return 1;
                }
                continue;
            }
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
            /// Variable assignment: readonly var=value. Mark the
            /// variable with SYMVAR_READONLY via the dedicated
            /// symtable_set_readonly_global() entry so the flag
            /// lookup in readonly_print_callback (and any future
            /// readonly-enforcement check) sees the attribute.
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

            symtable_set_readonly_global(name, value);

            *equals = '='; /// Restore the string
        } else {
            /// No assignment: readonly var (make existing variable
            /// readonly). For a nonexistent variable we create it
            /// with the readonly flag and an empty value; for an
            /// existing one we preserve the current value and add
            /// SYMVAR_READONLY through symtable_set_flags so the
            /// flag stack carries the attribute forward.
            if (!is_valid_identifier(arg)) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "'%s' not a valid identifier", arg);
                return 1;
            }

            /// Arrays carry their own flags field on array_value_t,
            /// so promoting an array to readonly is a flag merge on
            /// the array record -- routing through the scalar
            /// symtable_set_flags would clobber the array with an
            /// empty scalar. The element-write enforcement in
            /// symtable_set_array_element reads this flag.
            if (symtable_is_array(arg)) {
                symtable_array_add_flags(arg, SYMVAR_READONLY);
            } else {
                symtable_manager_t *manager = symtable_get_global_manager();
                /// symtable_get_global returns an owned copy; free it.
                char *current_value = symtable_get_global(arg);
                if (!current_value) {
                    symtable_set_readonly_global(arg, "");
                } else if (manager) {
                    symvar_flags_t cur = symtable_get_flags(manager, arg);
                    symtable_set_flags(manager, arg, cur | SYMVAR_READONLY);
                }
                free(current_value);
            }
        }
    }

    return 0;
}
