/**
 * @file bin_source.c
 * @brief `source` (and `.`) builtin -- execute commands from a file in current
 * shell
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "input.h"
#include "lush.h"
#include "restricted_mode.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

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
    /// Restricted-shell mode forbids `.` / `source` with a filename
    /// containing `/`. Restricted users must source by basename
    /// against the locked-down PATH; absolute or relative paths
    /// would bypass the PATH lockdown. Matches bash rbash.
    if (restricted_mode_is_engaged() && argc >= 2 && argv[1] &&
        strchr(argv[1], '/')) {
        return restricted_mode_reject(builtin_get_source_location(),
                                      argv[0] ? argv[0] : "source");
    }

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

    /// Get global executor for script context tracking
    executor_t *executor = get_global_executor();
    if (!executor) {
        fclose(file);
        executor_error_report(current_executor, SHELL_ERR_ASSERTION,
                              builtin_get_source_location(),
                              "no execution context available");
        return 1;
    }

    /// Track source depth for return support
    /// Save and reset source_return flag for this source level
    bool saved_source_return = executor->source_return;
    executor->source_depth++;
    executor->source_return = false;

    /// Set script execution context so breakpoints can match this file.
    executor_set_script_context(executor, argv[1]);

    char *complete_input;
    int result = 0;
    /// 1-based file line of the first character of the next construct.
    /// Threaded into parse_and_execute as the parser's starting line so
    /// node->loc.line carries the absolute source line -- what both
    /// breakpoints and structured-error snippets match against.
    size_t source_line = 1;

    /// Read complete multi-line constructs instead of line by line
    while ((complete_input = get_input_complete(file)) != NULL) {
        /// Check if return was called in sourced script
        if (executor->source_return) {
            free(complete_input);
            break;
        }

        /// Physical lines this construct spans, for advancing source_line.
        size_t construct_lines = 0;
        for (const char *p = complete_input; *p; p++) {
            if (*p == '\n') {
                construct_lines++;
            }
        }

        /// Skip empty constructs
        char *trimmed = complete_input;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;
        if (*trimmed == '\0') {
            free(complete_input);
            source_line += construct_lines;
            continue;
        }

        /// Parse and execute the complete construct at its true file line.
        int construct_result = parse_and_execute(complete_input, source_line);
        source_line += construct_lines;

        /// Check for return from sourced script (exit code 200+)
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
    }

    /// Clear source tracking and restore parent's source_return state
    executor->source_depth--;
    executor->source_return = saved_source_return;

    /// Clear script execution context
    executor_clear_script_context(executor);

    fclose(file);
    return result;
}
