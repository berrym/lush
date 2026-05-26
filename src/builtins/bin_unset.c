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
                        if (array->is_associative) {
                            symtable_array_unset_assoc(array, sub_buf);
                        } else {
                            int idx = (int)strtol(sub_buf, NULL, 10);
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
        if (mgr && symtable_is_nameref(mgr, var_name)) {
            const char *target = symtable_resolve_nameref(mgr, var_name, 10);
            if (target && target != var_name) {
                var_name = target;
            }
        }

        /// Use legacy API function for unsetting variables
        symtable_unset_global(var_name);
    }
    return 0;
}
