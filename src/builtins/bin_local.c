/**
 * @file bin_local.c
 * @brief `local` builtin -- declare function-local variables
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

#include <ctype.h>

/**
 * @brief Declare local variables within function scope
 *
 * Creates variables that are local to the current function.
 * Can only be used inside a function. Supports assignment syntax
 * (local var=value) or declaration only (local var).
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, 1 on error or if not in a function
 */
int bin_local(int argc, char **argv) {
    if (argc == 1) {
        // No arguments - just return success (bash behavior)
        return 0;
    }

    // Get the current symbol table manager
    symtable_manager_t *manager = symtable_get_global_manager();
    if (!manager) {
        executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                              builtin_get_source_location(),
                              "symbol table not available");
        return 1;
    }

    // Check if we're in a function scope
    size_t current_level = symtable_current_level(manager);
    if (current_level == 0) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_FUNCTION_ERROR, SHELL_SEVERITY_ERROR,
                               loc, "can only be used in a function");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "local is only valid inside a function body");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: local: can only be used in a function\n");
        }
        return 1;
    }

    // Parse options
    bool opt_nameref = false;
    int opt_idx = 1;

    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        const char *opt = argv[opt_idx];

        // Handle -- to stop option processing
        if (strcmp(opt, "--") == 0) {
            opt_idx++;
            break;
        }

        // Process each character in the option string
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'n':
                opt_nameref = true;
                break;
            default:
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_OPTION,
                                      builtin_get_source_location(),
                                      "-%c: invalid option", opt[i]);
                return 2;
            }
        }
        opt_idx++;
    }

    // Process each argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        char *eq = strchr(arg, '=');

        if (eq) {
            // Assignment: local var=value or local -n ref=target
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

            // Validate variable name
            if (!name[0] || (!isalpha(name[0]) && name[0] != '_')) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid variable name");
                free(name);
                return 1;
            }

            for (size_t j = 1; j < name_len; j++) {
                if (!isalnum(name[j]) && name[j] != '_') {
                    executor_error_report(
                        current_executor, SHELL_ERR_INVALID_ARGUMENT,
                        builtin_get_source_location(), "invalid variable name");
                    free(name);
                    return 1;
                }
            }

            char *value = eq + 1;

            if (opt_nameref) {
                // Create local nameref: local -n ref=target
                symvar_flags_t flags = SYMVAR_LOCAL | SYMVAR_NAMEREF_FLAG;
                if (symtable_set_nameref(manager, name, value, flags) != 0) {
                    executor_error_report(current_executor,
                                          SHELL_ERR_SCOPE_ERROR,
                                          builtin_get_source_location(),
                                          "failed to create nameref");
                    free(name);
                    return 1;
                }
            } else {
                // Set the local variable
                if (symtable_set_local_var(manager, name, value) != 0) {
                    executor_error_report(current_executor,
                                          SHELL_ERR_SCOPE_ERROR,
                                          builtin_get_source_location(),
                                          "failed to set variable");
                    free(name);
                    return 1;
                }
            }

            free(name);
        } else {
            // Declaration only: local var
            // Validate variable name
            if (!arg[0] || (!isalpha(arg[0]) && arg[0] != '_')) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(), "invalid variable name");
                return 1;
            }

            for (size_t j = 1; arg[j]; j++) {
                if (!isalnum(arg[j]) && arg[j] != '_') {
                    executor_error_report(
                        current_executor, SHELL_ERR_INVALID_ARGUMENT,
                        builtin_get_source_location(), "invalid variable name");
                    return 1;
                }
            }

            if (opt_nameref) {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                    "-n requires assignment");
                if (err) {
                    if (current_executor && SOURCE_LOC_VALID(loc)) {
                        char *src_line = executor_get_source_line(
                            current_executor, loc.line);
                        if (src_line) {
                            shell_error_set_source_line(
                                err, src_line, loc.column,
                                loc.column + loc.length);
                            free(src_line);
                        }
                    }
                    if (current_executor) {
                        for (size_t i = 0;
                             i < current_executor->context_depth &&
                             i < SHELL_ERROR_CONTEXT_MAX;
                             i++) {
                            if (current_executor->context_stack[i]) {
                                shell_error_push_context(
                                    err, "%s",
                                    current_executor->context_stack[i]);
                            }
                        }
                    }
                    shell_error_set_suggestion(
                        err, "use 'local -n ref=target' to bind a nameref");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr, "lush: local: -n requires assignment "
                                    "(local -n ref=target)\n");
                }
                return 1;
            }

            // Declare the local variable with empty value
            if (symtable_set_local_var(manager, arg, "") != 0) {
                executor_error_report(current_executor, SHELL_ERR_SCOPE_ERROR,
                                      builtin_get_source_location(),
                                      "failed to declare variable");
                return 1;
            }
        }
    }

    return 0;
}
