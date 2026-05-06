/**
 * @file bin_dirs.c
 * @brief `dirs` builtin -- display directory stack
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "dirstack.h"
#include "executor.h"
#include "shell_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Display directory stack
 *
 * Usage:
 *   dirs         - Display stack on one line
 *   dirs -p      - Print one entry per line
 *   dirs -v      - Print with stack index numbers
 *   dirs -c      - Clear the stack
 *   dirs -l      - Use full paths (no ~ substitution)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_dirs(int argc, char **argv) {
    bool one_per_line = false;
    bool show_index = false;
    bool clear_stack = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            one_per_line = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            show_index = true;
            one_per_line = true;
        } else if (strcmp(argv[i], "-c") == 0) {
            clear_stack = true;
        } else if (strcmp(argv[i], "-l") == 0) {
            // Full paths (currently default, ~ substitution not implemented)
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
                        err, "supported options: -c (clear), -l (full), "
                             "-p (per-line), -v (numbered)");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr,
                            "lush: %s: "
                            "%s: invalid option"
                            "\n",
                            "dirs", argv[i]);
                }
            }
            return 1;
        }
    }

    if (clear_stack) {
        dirstack_clear();
        return 0;
    }

    dirstack_print(one_per_line, show_index);
    return 0;
}
