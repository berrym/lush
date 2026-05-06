/**
 * @file bin_terminal.c
 * @brief `terminal` builtin -- display terminal capability information
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "lle/adaptive_terminal_integration.h"
#include "shell_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Display terminal capability information
 *
 * Shows detailed terminal detection results from LLE including:
 * TTY status, terminal type, dimensions, color support,
 * unicode support, mouse support, and multiplexer detection.
 *
 * @param argc Argument count
 * @param argv Argument vector (supports "help" subcommand)
 * @return 0 on success, 1 on error
 */
int bin_terminal(int argc, char **argv) {
    if (argc > 2) {
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
            shell_error_set_suggestion(
                err, "run 'terminal help' for usage information");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: terminal: too many arguments\n");
        }
        return 1;
    }

    if (argc == 2 &&
        (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("terminal - Display terminal capability information\n\n");
        printf("Usage: terminal [option]\n\n");
        printf("Options:\n");
        printf("  (none)  Show terminal capabilities (default)\n");
        printf("  help    Show this help message\n\n");
        printf("The terminal command displays information about the current\n");
        printf("terminal environment detected by LLE.\n");
        return 0;
    }

    lle_terminal_detection_result_t *detection = NULL;
    if (lle_detect_terminal_capabilities_optimized(&detection) != LLE_SUCCESS ||
        !detection) {
        executor_error_report(current_executor, SHELL_ERR_IO_ERROR,
                              builtin_get_source_location(),
                              "failed to detect terminal capabilities");
        return 1;
    }

    printf("Terminal Information (LLE Detection)\n");
    printf("=====================================\n\n");

    printf("TTY Status:\n");
    printf("  stdin:  %s\n", detection->stdin_is_tty ? "yes" : "no");
    printf("  stdout: %s\n", detection->stdout_is_tty ? "yes" : "no");
    printf("  stderr: %s\n", detection->stderr_is_tty ? "yes" : "no");

    printf("\nTerminal Type:\n");
    printf("  TERM:         %s\n",
           detection->term_name[0] ? detection->term_name : "(not set)");
    printf("  TERM_PROGRAM: %s\n",
           detection->term_program[0] ? detection->term_program : "(not set)");
    printf("  COLORTERM:    %s\n",
           detection->colorterm[0] ? detection->colorterm : "(not set)");

    printf("\nDimensions:\n");
    printf("  Columns: %d\n", detection->terminal_cols);
    printf("  Rows:    %d\n", detection->terminal_rows);

    printf("\nCapabilities:\n");
    printf("  Colors:        %s\n", detection->supports_colors ? "yes" : "no");
    printf("  256 colors:    %s\n",
           detection->supports_256_colors ? "yes" : "no");
    printf("  True color:    %s\n",
           detection->supports_truecolor ? "yes" : "no");
    printf("  Unicode:       %s\n", detection->supports_unicode ? "yes" : "no");
    printf("  Mouse:         %s\n", detection->supports_mouse ? "yes" : "no");
    printf("  Bracketed paste: %s\n",
           detection->supports_bracketed_paste ? "yes" : "no");

    printf("\nMultiplexer:\n");
    if (lle_is_tmux(detection)) {
        printf("  Running inside: tmux\n");
    } else if (lle_is_screen(detection)) {
        printf("  Running inside: GNU screen\n");
    } else {
        printf("  Running inside: (none detected)\n");
    }

    if (lle_is_iterm2(detection)) {
        printf("  Terminal app:   iTerm2\n");
    }

    printf("\nLLE Mode:\n");
    printf("  Recommended: %s\n",
           lle_adaptive_mode_to_string(detection->recommended_mode));
    printf("  Capability:  %s\n",
           lle_capability_level_to_string(detection->capability_level));

    return 0;
}
