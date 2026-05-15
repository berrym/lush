/**
 * @file bin_pwd.c
 * @brief `pwd` builtin -- print the current working directory
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lush.h"
#include "symtable.h"

#include <errno.h>

/**
 * @brief Print the current working directory
 *
 * In physical mode, resolves symlinks and shows the physical path.
 * In logical mode, uses PWD from the symbol table if available.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success, 1 on error
 */
int bin_pwd(int argc, char **argv) {
    // Default to shell's physical_mode setting, but allow -P/-L to override
    bool physical = shell_opts.physical_mode;

    // Parse options: -P (physical), -L (logical)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-P") == 0) {
            physical = true;
        } else if (strcmp(argv[i], "-L") == 0) {
            physical = false;
        } else if (argv[i][0] == '-') {
            {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR, loc,
                    "%s: invalid option", argv[i]);
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
                        err, "supported options: -L (logical), -P (physical)");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "%s: invalid option"
                            "\n",
                            "pwd", argv[i]);
                }
            }
            return 1;
        }
    }

    if (physical) {
        // In physical mode, resolve symlinks and show physical path
        char *physical_path = realpath(".", NULL);
        if (physical_path) {
            printf("%s\n", physical_path);
            free(physical_path);
            return 0;
        }
        // Fall through to error handling if realpath fails
    } else {
        // In logical mode, use PWD from symbol table if available
        char *pwd_value = symtable_get_global("PWD");
        if (pwd_value) {
            printf("%s\n", pwd_value);
            free(pwd_value);
            return 0;
        }
        // Fall through to getcwd if PWD not available
    }

    // Fallback - use getcwd
    char cwd[MAXLINE] = {'\0'};
    if (getcwd(cwd, MAXLINE) == NULL) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(), "getcwd: %s",
                              strerror(saved_errno));
        return 1;
    }

    printf("%s\n", cwd);

    return 0;
}
