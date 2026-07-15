/**
 * @file bin_pushd.c
 * @brief `pushd` builtin -- push directory onto the directory stack
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "dirstack.h"
#include "lush.h"
#include "shell_mode.h"
#include "symtable.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Change to `target`, pushing the old working directory onto the stack
 *
 * Shared by `pushd <dir>` and by bare `pushd` under pushd_to_home. Honors
 * pushd_ignore_dups (an existing copy of `cwd` is dropped before the push so
 * the stack never keeps duplicates) and pushd_silent (via
 * builtin_report_dirstack). Takes ownership of `cwd` and frees it.
 *
 * @param cwd Owned copy of the current working directory (freed here)
 * @param target Directory to change into
 * @return 0 on success, 1 on error (already reported)
 */
static int pushd_to(char *cwd, const char *target) {
    if (chdir(target) < 0) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s", target,
                              strerror(saved_errno));
        free(cwd);
        return 1;
    }

    dirstack_push(cwd);

    symtable_set_global("OLDPWD", cwd);
    char *new_cwd = getcwd(NULL, 0);
    if (new_cwd) {
        symtable_set_global("PWD", new_cwd);
        /// pushd_ignore_dups: drop any older stack copy of the directory we
        /// just moved to, so the current directory is not duplicated in the
        /// stack. zsh deduplicates the target (the new cwd), not the old
        /// directory being pushed; the just-pushed old cwd differs from the
        /// new cwd (except for `pushd .`, where the redundant push is exactly
        /// what should be dropped). Match on the resolved absolute path so a
        /// relative target still deduplicates.
        if (shell_mode_allows(FEATURE_PUSHD_IGNORE_DUPS)) {
            dirstack_remove_matching(new_cwd);
        }
        free(new_cwd);
    }

    free(cwd);
    builtin_report_dirstack();
    return 0;
}

/**
 * @brief Push directory onto stack and change to it
 *
 * Usage:
 *   pushd [dir]  - Push current dir, cd to dir
 *   pushd +N     - Rotate stack so Nth entry is at top, cd there
 *   pushd -N     - Rotate stack so Nth from bottom is at top, cd there
 *   pushd        - Exchange top two stack entries (or cd to $HOME under
 *                  pushd_to_home)
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
        /// zsh pushd_to_home: bare `pushd` behaves like `pushd $HOME` instead
        /// of exchanging the top two entries.
        if (shell_mode_allows(FEATURE_PUSHD_TO_HOME)) {
            const char *home = getenv("HOME");
            if (!home || !*home) {
                executor_error_report(
                    current_executor, SHELL_ERR_UNBOUND_VARIABLE,
                    builtin_get_source_location(), "HOME not set");
                free(cwd);
                return 1;
            }
            return pushd_to(cwd, home);
        }

        /// pushd with no args: exchange top two entries
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

        /// Save current directory, pop top, push current, cd to old top
        char *old_top = dirstack_pop();
        dirstack_push(cwd);

        if (chdir(old_top) < 0) {
            int saved_errno = errno;
            executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                                  builtin_get_source_location(), "%s: %s",
                                  old_top, strerror(saved_errno));
            /// Restore stack state
            dirstack_pop();
            dirstack_push(old_top);
            free(old_top);
            free(cwd);
            return 1;
        }

        /// Update PWD
        symtable_set_global("OLDPWD", cwd);
        char *new_cwd = getcwd(NULL, 0);
        if (new_cwd) {
            symtable_set_global("PWD", new_cwd);
            free(new_cwd);
        }

        free(old_top);
        free(cwd);
        builtin_report_dirstack();
        return 0;
    }

    char *arg = argv[1];

    /// Check for +N or -N rotation
    if (arg[0] == '+' || arg[0] == '-') {
        char *endptr;
        long n = strtol(arg + 1, &endptr, 10);
        if (*endptr == '\0' && n >= 0) {
            /// zsh pushdminus inverts the +N / -N sign convention.
            char sign = arg[0];
            if (shell_mode_allows(FEATURE_PUSHD_MINUS)) {
                sign = (sign == '+') ? '-' : '+';
            }
            /// pushd +N / -N circularly rotates the full directory list
            /// shown by `dirs`: D0 = cwd, D1.. = stack top-to-bottom. +N
            /// counts from the left (0-based), -N from the right; the target
            /// entry becomes the new cwd and the remaining entries keep their
            /// circular order. pushdminus (above) already swapped which sign
            /// counts from which end.
            int size = dirstack_size() + 1; /// full list including cwd
            /// Validate against the long n before the (int) cast below, so a
            /// huge argument cannot truncate into a small in-range index or
            /// overflow the signed subtraction. n >= 0 is already checked;
            /// n < size makes t land in [0, size-1] for both signs.
            if (n >= (long)size) {
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

            int t = (sign == '+') ? (int)n : (size - 1 - (int)n);
            if (t == 0) {
                /// Rotating to the current directory is a no-op.
                free(cwd);
                builtin_report_dirstack();
                return 0;
            }

            /// Snapshot the full list D0..D(size-1): D0 = cwd, Di =
            /// peek(i-1). Copy the strings before mutating the stack.
            char **list = malloc((size_t)size * sizeof(char *));
            if (!list) {
                free(cwd);
                return 1;
            }
            bool snap_ok = true;
            for (int i = 0; i < size; i++) {
                const char *d = (i == 0) ? cwd : dirstack_peek(i - 1);
                list[i] = d ? strdup(d) : NULL;
                if (!list[i]) {
                    snap_ok = false;
                }
            }
            if (!snap_ok) {
                for (int i = 0; i < size; i++) {
                    free(list[i]);
                }
                free(list);
                free(cwd);
                return 1;
            }

            /// cd to the rotation target (the new cwd, D0').
            if (chdir(list[t]) < 0) {
                int saved_errno = errno;
                executor_error_report(current_executor,
                                      SHELL_ERR_FILE_NOT_FOUND,
                                      builtin_get_source_location(), "%s: %s",
                                      list[t], strerror(saved_errno));
                for (int i = 0; i < size; i++) {
                    free(list[i]);
                }
                free(list);
                free(cwd);
                return 1;
            }

            /// Rebuild the stack in rotated order. The new dirs order is
            /// list[(t + j) % size] for j = 0..size-1: j == 0 is the new
            /// cwd, and j = 1..size-1 are the stack entries D1'..Dk'. Push
            /// bottom-up (Dk' first) so the internal top ends as D1'.
            dirstack_clear();
            for (int j = size - 1; j >= 1; j--) {
                dirstack_push(list[(t + j) % size]);
            }

            symtable_set_global("OLDPWD", cwd);
            char *new_cwd = getcwd(NULL, 0);
            if (new_cwd) {
                symtable_set_global("PWD", new_cwd);
                free(new_cwd);
            }

            for (int i = 0; i < size; i++) {
                free(list[i]);
            }
            free(list);
            free(cwd);
            builtin_report_dirstack();
            return 0;
        }
    }

    /// Push current directory and cd to new one.
    return pushd_to(cwd, arg);
}
