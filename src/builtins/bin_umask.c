/**
 * @file bin_umask.c
 * @brief `umask` builtin -- get/set the file mode creation mask
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * @brief Set or display the file creation mask
 *
 * With no arguments, displays the current umask in octal format.
 * With an octal argument, sets the new file creation mask.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional octal mask)
 * @return 0 on success, 1 on invalid mode
 */
int bin_umask(int argc, char **argv) {
    // If no arguments, display current umask
    if (argc == 1) {
        mode_t current_mask = umask(0); // Get current mask
        umask(current_mask);            // Restore it
        printf("%04o\n", current_mask);
        return 0;
    }

    // If one argument, set new umask
    if (argc == 2) {
        // Check for empty argument
        if (argv[1][0] == '\0') {
            {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                    "invalid mode");
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
                        err, "expected an octal mode (e.g. 022, 077)");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "invalid mode"
                            "\n",
                            "umask");
                }
            }
            return 1;
        }

        char *endptr;
        long mask_val = strtol(argv[1], &endptr, 8); // Parse as octal

        // Validate argument
        if (*endptr != '\0' || mask_val < 0 || mask_val > 0777) {
            {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                    "%s: invalid mode", argv[1]);
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
                    shell_error_set_suggestion(err,
                                               "octal mode must be in 0..0777");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "%s: invalid mode"
                            "\n",
                            "umask", argv[1]);
                }
            }
            return 1;
        }

        umask((mode_t)mask_val);
        return 0;
    }

    // Too many arguments
    {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_TOO_MANY_ARGUMENTS,
                               SHELL_SEVERITY_ERROR, loc, "too many arguments");
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
            shell_error_set_suggestion(err, "usage: umask [octal-mode]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: %s: "
                    "too many arguments"
                    "\n",
                    "umask");
        }
    }
    return 1;
}
