/**
 * @file bin_type.c
 * @brief `type` builtin -- describe how a command would be interpreted
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "builtins.h"
#include "symtable.h"

#include <sys/stat.h>

/**
 * @brief Display how a command would be interpreted
 *
 * Shows whether a command is a builtin, alias, function, or external file.
 * Supports -t (type only), -p (path only), and -a (show all) options.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and command names
 * @return 0 if all commands found, 1 if any not found
 */
int bin_type(int argc, char **argv) {
    bool type_only = false; // -t flag: output only the type
    bool path_only = false; // -p flag: output only the path
    bool show_all = false;  // -a flag: show all locations
    int name_start = 1;

    // Parse options
    for (int i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            type_only = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            path_only = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "--") == 0) {
            name_start = i + 1;
            break;
        } else {
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "invalid option: %s", argv[i]);
            return 1;
        }
    }

    if (name_start >= argc) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing name argument");
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
                for (size_t k = 0; k < current_executor->context_depth &&
                                   k < SHELL_ERROR_CONTEXT_MAX;
                     k++) {
                    if (current_executor->context_stack[k]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[k]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "usage: type [-t] [-p] [-a] name [name ...]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: usage: type [-t] [-p] [-a] name [name ...]\n");
        }
        return 1;
    }

    int result = 0;
    for (int i = name_start; i < argc; i++) {
        const char *name = argv[i];

        bool found_any = false;

        // Check if it's a builtin command
        if (is_builtin(name)) {
            found_any = true;
            if (type_only) {
                printf("builtin\n");
            } else if (path_only) {
                // -p flag: builtins have no path, so output nothing
            } else {
                printf("%s is a shell builtin\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's an alias (simplified - would need full alias parsing)
        char *alias_value = symtable_get_global("alias");
        if (alias_value) {
            found_any = true;
            if (type_only) {
                printf("alias\n");
            } else if (path_only) {
                // -p flag: aliases have no path, so output nothing
            } else {
                printf("%s is aliased\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's a function (stored in symbol table)
        char *func_value = symtable_get_global(name);
        if (func_value && strstr(func_value, "function")) {
            found_any = true;
            if (type_only) {
                printf("function\n");
            } else if (path_only) {
                // -p flag: functions have no path, so output nothing
            } else {
                printf("%s is a function\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's an executable file in PATH
        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");
            bool found_in_path = false;

            while (dir != NULL) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

                if (access(full_path, X_OK) == 0) {
                    found_any = true;
                    found_in_path = true;

                    if (type_only) {
                        printf("file\n");
                    } else if (path_only) {
                        printf("%s\n", full_path);
                    } else {
                        printf("%s is %s\n", name, full_path);
                    }

                    if (!show_all)
                        break;
                }
                dir = strtok(NULL, ":");
            }

            free(path_copy);
            if (found_in_path && !show_all) {
                continue;
            }
        }

        // Not found anywhere
        if (!found_any) {
            if (type_only || path_only) {
                // For -t and -p, output nothing for not found commands
            } else {
                printf("%s: not found\n", name);
            }
            result = 1;
        }
    }

    return result;
}
