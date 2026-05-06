/**
 * @file bin_unset.c
 * @brief `unset` builtin -- remove variables and functions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "shell_error.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    // No arguments - silently succeed (POSIX behavior)
    if (argc < 2) {
        return 0;
    }

    // Unset each variable specified
    for (int i = 1; i < argc; i++) {
        const char *var_name = argv[i];

        // Resolve nameref if applicable - unset the target, not the nameref
        // itself
        symtable_manager_t *mgr = symtable_get_global_manager();
        if (mgr && symtable_is_nameref(mgr, var_name)) {
            const char *target = symtable_resolve_nameref(mgr, var_name, 10);
            if (target && target != var_name) {
                var_name = target;
            }
        }

        // Use legacy API function for unsetting variables
        symtable_unset_global(var_name);
    }
    return 0;
}
