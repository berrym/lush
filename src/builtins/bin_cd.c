/**
 * @file bin_cd.c
 * @brief `cd` builtin -- change the current working directory
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "dirstack.h"
#include "executor.h"
#include "lle/lle_shell_event_hub.h"
#include "lush.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Canonicalize a path by resolving . and .. components logically
 *
 * Resolves relative path components without following symlinks,
 * maintaining the logical path as entered by the user.
 *
 * @param path The path to canonicalize
 * @return Newly allocated canonicalized path, or NULL on error (caller must
 * free)
 */
static char *canonicalize_logical_path(const char *path) {
    if (!path)
        return NULL;

    size_t path_len = strlen(path);
    char *result = malloc(path_len + 1);
    if (!result)
        return NULL;

    strcpy(result, path);

    // Simple canonicalization: remove /./  and resolve /../
    char *src = result;
    char *dst = result;

    while (*src) {
        if (*src == '/') {
            // Skip multiple slashes
            while (*src == '/')
                src++;
            if (dst > result || dst == result)
                *dst++ = '/';

            // Check for . and ..
            if (*src == '.') {
                if (src[1] == '/' || src[1] == '\0') {
                    // Skip ./
                    src++;
                    continue;
                } else if (src[1] == '.' && (src[2] == '/' || src[2] == '\0')) {
                    // Handle ../
                    src += 2;
                    // Remove last component from dst
                    if (dst > result + 1) {
                        dst--; // Back up from the /
                        while (dst > result && dst[-1] != '/')
                            dst--;
                    }
                    continue;
                }
            }
        }
        *dst++ = *src++;
    }

    // Remove trailing slash unless it's root
    if (dst > result + 1 && dst[-1] == '/') {
        dst--;
    }

    *dst = '\0';

    // Handle empty path
    if (dst == result) {
        strcpy(result, "/");
    }

    return result;
}

/**
 * @brief Change the current working directory
 *
 * Implements the cd builtin with support for:
 * - cd (no args): go to HOME
 * - cd -: go to previous directory (OLDPWD)
 * - cd path: change to specified path
 *
 * Respects physical_mode setting for symlink resolution.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_cd(int argc __attribute__((unused)),
           char **argv __attribute__((unused))) {
    static char *previous_dir = NULL;
    char *current_dir = NULL;
    char *target_dir = NULL;

    // Privileged mode security check
    if (shell_opts.privileged_mode) {
        {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_PERMISSION_DENIED, SHELL_SEVERITY_ERROR, loc,
                "restricted command in privileged mode");
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
                    err, "drop privileges or run from an unrestricted shell");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr,
                        "lush: %s: "
                        "restricted command in privileged mode"
                        "\n",
                        "cd");
            }
        }
        return 1;
    }

    // Get current directory before changing
    current_dir = getcwd(NULL, 0);
    if (!current_dir && errno != ENOENT) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(), "getcwd: %s",
                              strerror(saved_errno));
        return 1;
    }

    // Parse arguments - handle -- as option terminator
    int arg_index = 1;
    if (argc > 1 && strcmp(argv[1], "--") == 0) {
        arg_index = 2; // Skip past --
    }

    if (arg_index >= argc) {
        // cd with no arguments (or just --) - go to HOME
        target_dir = getenv("HOME");
        if (!target_dir) {
            executor_error_report(current_executor, SHELL_ERR_UNBOUND_VARIABLE,
                                  builtin_get_source_location(),
                                  "HOME not set");
            free(current_dir);
            return 1;
        }
    } else if (arg_index == argc - 1) {
        if (strcmp(argv[arg_index], "-") == 0) {
            // cd - : go to previous directory
            if (!previous_dir) {
                executor_error_report(
                    current_executor, SHELL_ERR_UNBOUND_VARIABLE,
                    builtin_get_source_location(), "OLDPWD not set");
                free(current_dir);
                return 1;
            }
            target_dir = previous_dir;
            // Print the directory we're changing to (standard behavior)
            printf("%s\n", target_dir);
        } else {
            target_dir = argv[arg_index];
        }
    } else {
        executor_error_report(current_executor, SHELL_ERR_TOO_MANY_ARGUMENTS,
                              builtin_get_source_location(),
                              "usage: cd [pathname | -]");
        free(current_dir);
        return 1;
    }

    // Attempt to change directory
    if (chdir(target_dir) < 0) {
        // If chdir failed and cdable_vars is enabled, try treating target as
        // variable name
        if (shell_mode_allows(FEATURE_CDABLE_VARS) && target_dir[0] != '/' &&
            target_dir[0] != '.' && target_dir[0] != '~') {
            const char *var_value = symtable_get_global(target_dir);
            if (var_value && var_value[0] == '/') {
                // Try cd to the variable's value
                if (chdir(var_value) == 0) {
                    // Success - update target_dir for PWD setting
                    target_dir = (char *)var_value;
                    goto cd_success;
                }
            }
        }
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s",
                              target_dir, strerror(saved_errno));
        free(current_dir);
        return 1;
    }
cd_success:

    // Auto-push old directory to stack if enabled
    if (shell_mode_allows(FEATURE_AUTO_PUSHD) && current_dir) {
        dirstack_push(current_dir);
    }

    // Update previous directory
    if (previous_dir) {
        free(previous_dir);
    }
    previous_dir = current_dir;

    // Set OLDPWD variable according to current mode
    if (previous_dir) {
        if (shell_opts.physical_mode) {
            // In physical mode, resolve OLDPWD to physical path
            char *resolved_prev = realpath(previous_dir, NULL);
            if (resolved_prev) {
                symtable_set_global("OLDPWD", resolved_prev);
                free(resolved_prev);
            } else {
                symtable_set_global("OLDPWD", previous_dir);
            }
        } else {
            // In logical mode, use logical path
            symtable_set_global("OLDPWD", previous_dir);
        }
    }

    // Set PWD variable according to current mode
    if (shell_opts.physical_mode) {
        // In physical mode, resolve PWD to physical path
        char *resolved_dir = realpath(".", NULL);
        if (resolved_dir) {
            symtable_set_global("PWD", resolved_dir);
            free(resolved_dir);
        }
    } else {
        // In logical mode, preserve the logical path taken
        if (argc == 2 && strcmp(argv[1], "-") == 0) {
            // cd - case: PWD becomes old OLDPWD (already handled above in cd -
            // logic)
            char *new_dir = getcwd(NULL, 0);
            if (new_dir) {
                symtable_set_global("PWD", new_dir);
                free(new_dir);
            }
        } else if (target_dir && target_dir[0] == '/') {
            // Absolute path - canonicalize it in logical mode
            char *canonical_path = canonicalize_logical_path(target_dir);
            if (canonical_path) {
                symtable_set_global("PWD", canonical_path);
                free(canonical_path);
            } else {
                symtable_set_global("PWD", target_dir);
            }
        } else if (target_dir) {
            // Relative path - build logical path from current PWD
            char *current_pwd = symtable_get_global("PWD");
            if (current_pwd && strlen(current_pwd) > 0) {
                size_t pwd_len = strlen(current_pwd);
                size_t target_len = strlen(target_dir);
                char *logical_path = malloc(pwd_len + target_len + 2);
                if (logical_path) {
                    strcpy(logical_path, current_pwd);
                    if (logical_path[pwd_len - 1] != '/') {
                        strcat(logical_path, "/");
                    }
                    strcat(logical_path, target_dir);

                    // Canonicalize the logical path to handle . and ..
                    char *canonical_path =
                        canonicalize_logical_path(logical_path);
                    if (canonical_path) {
                        symtable_set_global("PWD", canonical_path);
                        free(canonical_path);
                    } else {
                        symtable_set_global("PWD", logical_path);
                    }
                    free(logical_path);
                } else {
                    // Fallback to getcwd if malloc fails
                    char *new_dir = getcwd(NULL, 0);
                    if (new_dir) {
                        symtable_set_global("PWD", new_dir);
                        free(new_dir);
                    }
                }
            } else {
                // No current PWD - fallback to getcwd
                char *new_dir = getcwd(NULL, 0);
                if (new_dir) {
                    symtable_set_global("PWD", new_dir);
                    free(new_dir);
                }
            }
        } else {
            // cd with no arguments - go to HOME
            char *home = getenv("HOME");
            if (home) {
                symtable_set_global("PWD", home);
            } else {
                char *new_dir = getcwd(NULL, 0);
                if (new_dir) {
                    symtable_set_global("PWD", new_dir);
                    free(new_dir);
                }
            }
        }
    }

    /* Fire LLE shell event for directory change (Spec 26)
     * This notifies registered handlers (prompt composer, autosuggestions,
     * etc.) that the working directory has changed. previous_dir holds the old
     * dir.
     */
    lle_fire_directory_changed(previous_dir, NULL);

    return 0;
}
