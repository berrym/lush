/**
 * @file bin_echo.c
 * @brief `echo` builtin -- print arguments separated by spaces
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "escape.h"
#include "shell_mode.h"

#include <errno.h>
#include <string.h>

/**
 * @brief Echo arguments to stdout
 *
 * Implements the echo builtin with XSI escape sequence processing.
 * Per POSIX XSI extension, escape sequences like \n, \t are interpreted.
 * Supports -n (no newline), -e (interpret escapes), -E (no escapes) options.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and strings to echo
 * @return Always returns 0
 */
int bin_echo(int argc, char **argv) {
    /// Default escape-interpretation policy is the FEATURE_XPG_ECHO flag,
    /// curated zsh-style in lush mode (escapes interpreted unless `-E` is
    /// given). Bash mode and `unsetopt xpg_echo` (or the inverted-alias
    /// `setopt bsd_echo`) flip the default to literal. `-e` / `-E` always
    /// override per-call.
    bool interpret_escapes = shell_mode_allows(FEATURE_XPG_ECHO);
    bool no_newline = false;
    int arg_start = 1;

    /// Parse options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0) {
            interpret_escapes = true;
            arg_start = i + 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            no_newline = true;
            arg_start = i + 1;
        } else if (strcmp(argv[i], "-E") == 0) {
            interpret_escapes = false;
            arg_start = i + 1;
        } else if (argv[i][0] == '-') {
            /// Unknown option, treat as argument
            break;
        } else {
            break;
        }
    }

    /// Clear any previous error state on stdout
    clearerr(stdout);

    /// Print arguments. A `\c` in any interpreted argument terminates the
    /// whole echo: no further arguments, no trailing newline (XSI). The
    /// decoded text can hold embedded NULs (\0, \xHH), so it is written by
    /// its true byte length, not as a C string.
    bool terminated = false;
    for (int i = arg_start; i < argc && !terminated; i++) {
        if (i > arg_start) {
            printf(" ");
        }

        if (interpret_escapes) {
            size_t out_len = 0;
            char *processed = lush_expand_escapes_ex(
                argv[i], strlen(argv[i]), LUSH_ESC_XSI, &terminated, &out_len);
            if (processed) {
                fwrite(processed, 1, out_len, stdout);
                free(processed);
            } else {
                printf("%s", argv[i]);
            }
        } else {
            printf("%s", argv[i]);
        }
    }

    if (!no_newline && !terminated) {
        printf("\n");
    }

    /// Flush and check for write errors (e.g., writing to closed/invalid fd)
    if (fflush(stdout) == EOF || ferror(stdout)) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "echo: write error: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    return 0;
}
