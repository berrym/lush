/**
 * @file bin_popd.c
 * @brief `popd` builtin -- pop directory from the directory stack
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

/**
 * @brief Pop the top of the directory stack and cd to it.
 *
 * Shared by bare `popd` and by `popd +0` / `popd -N` when the target is the
 * current directory: bash and zsh both remove the current directory and
 * change to the new top. The caller guarantees the stack is non-empty
 * (checked at entry to bin_popd), so the empty branch is a defensive
 * fallback.
 *
 * @return 0 on success, 1 on error.
 */
static int popd_pop_and_cd(void) {
    char *dir = dirstack_pop();
    if (!dir) {
        executor_error_report(current_executor, SHELL_ERR_DIRECTORY_STACK,
                              builtin_get_source_location(),
                              "directory stack empty");
        return 1;
    }

    char *cwd = getcwd(NULL, 0);
    if (chdir(dir) < 0) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                              builtin_get_source_location(), "%s: %s", dir,
                              strerror(saved_errno));
        dirstack_push(dir); /// put it back
        free(dir);
        free(cwd);
        return 1;
    }

    if (cwd) {
        symtable_set_global("OLDPWD", cwd);
        free(cwd);
    }

    char *new_cwd = getcwd(NULL, 0);
    if (new_cwd) {
        symtable_set_global("PWD", new_cwd);
        free(new_cwd);
    }

    free(dir);
    builtin_report_dirstack();
    return 0;
}

/**
 * @brief Pop directory from stack and change to it
 *
 * Usage:
 *   popd         - Pop top entry and cd there
 *   popd +N      - Remove Nth entry from top (0 = current dir)
 *   popd -N      - Remove Nth entry from bottom
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_popd(int argc, char **argv) {
    if (dirstack_size() < 1) {
        {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(SHELL_ERR_DIRECTORY_STACK,
                                                    SHELL_SEVERITY_ERROR, loc,
                                                    "directory stack empty");
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
                    err, "push a directory first: pushd <dir>");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr,
                        "lush: %s: "
                        "directory stack empty"
                        "\n",
                        "popd");
            }
        }
        return 1;
    }

    if (argc == 1) {
        /// popd with no args: pop top and cd there.
        return popd_pop_and_cd();
    }

    char *arg = argv[1];

    /// Check for +N or -N removal
    if (arg[0] == '+' || arg[0] == '-') {
        char *endptr;
        long n = strtol(arg + 1, &endptr, 10);
        if (*endptr == '\0' && n >= 0) {
            /// zsh pushdminus inverts the +N / -N sign convention.
            char sign = arg[0];
            if (shell_mode_allows(FEATURE_PUSHD_MINUS)) {
                sign = (sign == '+') ? '-' : '+';
            }
            int idx;
            if (sign == '+') {
                idx = (int)n;
            } else {
                idx = dirstack_size() - (int)n;
            }

            /// idx 0 targets the current directory. bash and zsh both remove
            /// it and cd to the new top -- the same as no-arg popd.
            if (idx == 0) {
                return popd_pop_and_cd();
            }

            /// Adjust for cwd being position 0
            int stack_idx = idx - 1;

            if (dirstack_remove(stack_idx) < 0) {
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
                                "popd", arg);
                    }
                }
                return 1;
            }

            builtin_report_dirstack();
            return 0;
        }
    }

    {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "%s: invalid argument", arg);
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
            shell_error_set_suggestion(err, "usage: popd [+N | -N]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: %s: "
                    "%s: invalid argument"
                    "\n",
                    "popd", arg);
        }
    }
    return 1;
}
