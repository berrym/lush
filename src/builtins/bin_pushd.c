/**
 * @file bin_pushd.c
 * @brief `pushd` builtin -- push directory onto the directory stack
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "dirstack.h"
#include "executor.h"
#include "lush.h"
#include "shell_error.h"
#include "symtable.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Push directory onto stack and change to it
 *
 * Usage:
 *   pushd [dir]  - Push current dir, cd to dir
 *   pushd +N     - Rotate stack so Nth entry is at top, cd there
 *   pushd -N     - Rotate stack so Nth from bottom is at top, cd there
 *   pushd        - Exchange top two stack entries
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_pushd(int argc, char **argv) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(), "getcwd: %s",
                              strerror(saved_errno));
        return 1;
    }

    if (argc == 1) {
        // pushd with no args: exchange top two entries
        if (dirstack_size() < 1) {
            {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_DIRECTORY_STACK, SHELL_SEVERITY_ERROR, loc,
                    "no other directory");
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
                        err, "push a directory first: pushd <dir>");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "no other directory"
                            "\n",
                            "pushd");
                }
            }
            free(cwd);
            return 1;
        }

        const char *top = dirstack_peek(0);
        if (!top) {
            {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_DIRECTORY_STACK, SHELL_SEVERITY_ERROR, loc,
                    "directory stack empty");
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
                        err, "push a directory first: pushd <dir>");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "directory stack empty"
                            "\n",
                            "pushd");
                }
            }
            free(cwd);
            return 1;
        }

        // Save current directory, pop top, push current, cd to old top
        char *old_top = dirstack_pop();
        dirstack_push(cwd);

        if (chdir(old_top) < 0) {
            int saved_errno = errno;
            executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                                  builtin_get_source_location(), "%s: %s",
                                  old_top, strerror(saved_errno));
            // Restore stack state
            dirstack_pop();
            dirstack_push(old_top);
            free(old_top);
            free(cwd);
            return 1;
        }

        // Update PWD
        symtable_set_global("OLDPWD", cwd);
        char *new_cwd = getcwd(NULL, 0);
        if (new_cwd) {
            symtable_set_global("PWD", new_cwd);
            free(new_cwd);
        }

        free(old_top);
        free(cwd);
        dirstack_print(false, false);
        return 0;
    }

    char *arg = argv[1];

    // Check for +N or -N rotation
    if (arg[0] == '+' || arg[0] == '-') {
        char *endptr;
        long n = strtol(arg + 1, &endptr, 10);
        if (*endptr == '\0' && n >= 0) {
            int idx = (arg[0] == '+') ? (int)n : -(int)n - 1;

            // Need to account for cwd being index 0
            if (idx == 0) {
                // +0 means current dir, nothing to do
                free(cwd);
                dirstack_print(false, false);
                return 0;
            }

            // Adjust for the fact that cwd is position 0
            int stack_idx = idx - 1;

            const char *target = dirstack_peek(stack_idx);
            if (!target) {
                {
                    source_location_t loc = builtin_get_source_location();
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_DIRECTORY_STACK, SHELL_SEVERITY_ERROR, loc,
                        "%s: directory stack index out of range", arg);
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
                            err, "use 'dirs' to list valid stack indices");
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    } else {
                        fprintf(stderr,
                                "lush: %s: "
                                "%s: directory stack index out of range"
                                "\n",
                                "pushd", arg);
                    }
                }
                free(cwd);
                return 1;
            }

            // Rotate stack and cd
            if (dirstack_rotate(stack_idx) < 0) {
                {
                    source_location_t loc = builtin_get_source_location();
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_DIRECTORY_STACK, SHELL_SEVERITY_ERROR, loc,
                        "%s: rotation failed", arg);
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
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    } else {
                        fprintf(stderr,
                                "lush: %s: "
                                "%s: rotation failed"
                                "\n",
                                "pushd", arg);
                    }
                }
                free(cwd);
                return 1;
            }

            target = dirstack_peek(0);
            if (chdir(target) < 0) {
                int saved_errno = errno;
                executor_error_report(current_executor,
                                      SHELL_ERR_FILE_NOT_FOUND,
                                      builtin_get_source_location(), "%s: %s",
                                      target, strerror(saved_errno));
                free(cwd);
                return 1;
            }

            symtable_set_global("OLDPWD", cwd);
            char *new_cwd = getcwd(NULL, 0);
            if (new_cwd) {
                symtable_set_global("PWD", new_cwd);
                free(new_cwd);
            }

            free(cwd);
            dirstack_print(false, false);
            return 0;
        }
    }

    // Push current directory and cd to new one
    if (chdir(arg) < 0) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s", arg,
                              strerror(saved_errno));
        free(cwd);
        return 1;
    }

    dirstack_push(cwd);

    symtable_set_global("OLDPWD", cwd);
    char *new_cwd = getcwd(NULL, 0);
    if (new_cwd) {
        symtable_set_global("PWD", new_cwd);
        free(new_cwd);
    }

    free(cwd);
    dirstack_print(false, false);
    return 0;
}
