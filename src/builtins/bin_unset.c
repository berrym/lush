/**
 * @file bin_unset.c
 * @brief `unset` builtin -- remove variables and functions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

/**
 * @brief Remove a variable from the global symbol table
 *
 * Unsets the specified shell variable, removing it from the environment.
 *
 * @param argc Argument count (must be 2)
 * @param argv Argument vector (argv[1] is variable name)
 * @return 0 on success, 1 on invalid usage
 */
int bin_unset(int argc, char **argv) {
    /// No arguments - silently succeed (POSIX behavior)
    if (argc < 2) {
        return 0;
    }

    /// Accumulated status: a readonly target is refused (non-zero) while the
    /// remaining names are still processed, matching builtin argument-loop
    /// discipline.
    int rc = 0;

    /// Unset each variable specified
    for (int i = 1; i < argc; i++) {
        const char *var_name = argv[i];

        /// Array-element form: `unset arr[N]` or `unset assoc[key]`
        /// removes just the matching element, not the whole array.
        /// Bash semantics; without this lush ignored the subscript
        /// and either no-op'd or tried to unset a scalar named
        /// "arr[N]" (issue #101). Detect a `[` in the name; if
        /// followed by a matching `]`, route through the array
        /// element-unset API.
        const char *bracket = strchr(var_name, '[');
        if (bracket && bracket > var_name) {
            const char *close = strrchr(bracket, ']');
            if (close && close > bracket + 1) {
                size_t name_len = (size_t)(bracket - var_name);
                size_t sub_len = (size_t)(close - bracket - 1);
                char name_buf[256];
                char sub_buf[256];
                if (name_len < sizeof(name_buf) && sub_len < sizeof(sub_buf)) {
                    memcpy(name_buf, var_name, name_len);
                    name_buf[name_len] = '\0';
                    memcpy(sub_buf, bracket + 1, sub_len);
                    sub_buf[sub_len] = '\0';
                    array_value_t *array = symtable_get_array(name_buf);
                    if (array) {
                        /// A readonly binding is immutable: it may be neither
                        /// reassigned nor destroyed. Removing an element
                        /// mutates the array, so refuse it the same way the
                        /// write surfaces refuse element writes (E1117),
                        /// leaving the array intact. Uniform readonly
                        /// diagnostic across all mutation surfaces.
                        if (array->flags & SYMVAR_READONLY) {
                            executor_error_report(
                                current_executor, SHELL_ERR_READONLY_VAR,
                                builtin_get_source_location(),
                                "%s: readonly variable", name_buf);
                            rc = 1;
                            continue;
                        }
                        if (array->is_associative) {
                            symtable_array_unset_assoc(array, sub_buf);
                        } else {
                            /// Full-width subscript so a 64-bit index is unset,
                            /// not truncated to a wrong element (issue #618).
                            long long idx = strtoll(sub_buf, NULL, 10);
                            symtable_array_unset_index(array, idx);
                        }
                        continue;
                    }
                }
            }
        }

        /// Resolve nameref if applicable - unset the target, not the nameref
        /// itself
        symtable_manager_t *mgr = symtable_get_global_manager();
        char *resolved_owned = NULL;
        if (mgr && symtable_is_nameref(mgr, var_name)) {
            resolved_owned =
                (char *)symtable_resolve_nameref(mgr, var_name, 10);
            if (resolved_owned) {
                var_name = resolved_owned;
            }
        }

        /// A readonly binding is immutable across every mutation surface, so
        /// `unset` must refuse it the way assignment/append/element/arithmetic
        /// already do (E1117), leaving the binding intact -- otherwise a
        /// readonly array or associative would be silently destroyed and a
        /// readonly scalar protected only incidentally and without diagnostic.
        /// Both queries are scope-chain aware, so a readonly outer/global
        /// binding is caught from within a function scope. Checked on the
        /// nameref-resolved target, matching where the unset itself lands.
        if ((symtable_array_get_flags(var_name) & SYMVAR_READONLY) ||
            (mgr && (symtable_get_flags(mgr, var_name) & SYMVAR_READONLY))) {
            executor_error_report(current_executor, SHELL_ERR_READONLY_VAR,
                                  builtin_get_source_location(),
                                  "%s: readonly variable", var_name);
            rc = 1;
            free(resolved_owned);
            continue;
        }

        /// Use legacy API function for unsetting variables
        symtable_unset_global(var_name);
        free(resolved_owned);
    }
    return rc;
}
