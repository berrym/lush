/**
 * @file bin_source.c
 * @brief `source` (and `.`) builtin -- execute commands from a file in current
 * shell
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "input.h"
#include "lush.h"
#include "shell_error.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Source (execute) a script file in the current shell
 *
 * Reads and executes commands from the specified file in the current
 * shell environment. Variables set in the sourced file persist.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is the filename)
 * @return 0 on success, 1 on error, or last command's exit status
 */
int bin_source(int argc, char **argv) {
    if (argc < 2) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing filename");
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
            shell_error_set_suggestion(err, "usage: source filename");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: source: missing filename\n");
        }
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        int saved_errno = errno;
        executor_error_report(current_executor, SHELL_ERR_FILE_NOT_FOUND,
                              builtin_get_source_location(),
                              "cannot open '%s': %s", argv[1],
                              strerror(saved_errno));
        return 1;
    }

    // Get global executor for script context tracking
    executor_t *executor = get_global_executor();
    if (!executor) {
        fclose(file);
        executor_error_report(current_executor, SHELL_ERR_ASSERTION,
                              builtin_get_source_location(),
                              "no execution context available");
        return 1;
    }

    // Track source depth for return support
    // Save and reset source_return flag for this source level
    bool saved_source_return = executor->source_return;
    executor->source_depth++;
    executor->source_return = false;

    // Set script execution context for debugging
    executor_set_script_context(executor, argv[1], 1);

    char *complete_input;
    int result = 0;
    int construct_number = 1;

    // Read complete multi-line constructs instead of line by line
    while ((complete_input = get_input_complete(file)) != NULL) {
        // Check if return was called in sourced script
        if (executor->source_return) {
            free(complete_input);
            break;
        }

        // Skip empty constructs
        char *trimmed = complete_input;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;
        if (*trimmed == '\0') {
            free(complete_input);
            construct_number++;
            continue;
        }

        // Update script context for debugging
        executor_set_script_context(executor, argv[1], construct_number);

        /* Parse and execute the complete construct. Sourced-script
         * line tracking through bin_source: construct_number is the
         * 1-based construct ordinal, not a file line; passing 1 here
         * preserves pre-2026-04 behaviour. Threading the actual source
         * line of each construct through bin_source is future work. */
        int construct_result = parse_and_execute(complete_input, 1);

        // Check for return from sourced script (exit code 200+)
        if (construct_result >= 200 && construct_result <= 455) {
            result = construct_result - 200;
            executor->source_return = true;
            free(complete_input);
            break;
        }

        if (construct_result != 0) {
            result = construct_result;
        }

        free(complete_input);
        construct_number++;
    }

    // Clear source tracking and restore parent's source_return state
    executor->source_depth--;
    executor->source_return = saved_source_return;

    // Clear script execution context
    executor_clear_script_context(executor);

    fclose(file);
    return result;
}
