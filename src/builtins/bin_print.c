/**
 * @file bin_print.c
 * @brief `print` builtin -- zsh-style output (issue #94)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Zsh-native output command. Differs from `echo` in:
 *   - escape interpretation is the default (no -e flag required)
 *   - -l prints one argument per line
 *   - -P expands zsh prompt-style escapes (%n, %~, %F{...}, etc.)
 *   - -u N writes to fd N instead of stdout
 *   - -f FMT routes through bin_printf for formatted output
 *
 * Gated on FEATURE_ZSH_PRINT_BUILTIN (zsh+lush mode on; bash+posix off
 * for parity -- bash has no `print` builtin and emits "command not
 * found" through PATH lookup; surface the same here so bash-mode
 * scripts don't silently absorb a zsh idiom).
 */

#include "builtins.h"
#include "lle/prompt/prompt_expansion.h"
#include "lush.h"
#include "shell_mode.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* From src/builtins/bin_printf.c -- shared escape-sequence interpreter.
 * Same forward-decl pattern as bin_echo.c. */
char *process_escape_sequences(const char *str);

/* From src/builtins/bin_printf.c -- printf-style format processor.
 * Used for `print -f FMT [args...]`. */
int bin_printf(int argc, char **argv);

#define PRINT_PROMPT_BUF_SIZE 8192

static int print_emit(int fd, const char *text, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, text, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        text += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

int bin_print(int argc, char **argv) {
    /// Mode gate. The builtin is registered universally in the
    /// builtins[] table; honor the feature flag so bash and POSIX
    /// modes surface "command not found" matching their actual
    /// behavior (neither has a `print` builtin).
    if (!shell_mode_allows(FEATURE_ZSH_PRINT_BUILTIN)) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR,
            builtin_get_source_location(), "print: command not found");
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 127;
    }

    bool one_per_line = false;
    bool no_newline = false;
    bool raw = false;
    bool prompt_expand_flag = false;
    const char *fmt_str = NULL;
    int target_fd = STDOUT_FILENO;

    /// Zsh-style short-option parsing: combinable flags (-lr, -nr, etc.),
    /// -u and -f take an argument that may be glued (-u2) or separate
    /// (-u 2). `--` terminates option processing. The first non-option
    /// argument also terminates.
    int i = 1;
    while (i < argc) {
        const char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            break; /// positional, or bare "-"
        }
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        bool stop_outer = false;
        bool consumed_glued_value = false;
        size_t off = 1;
        char last_letter = '\0';
        while (arg[off] && !stop_outer && !consumed_glued_value) {
            char c = arg[off];
            last_letter = c;
            switch (c) {
            case 'l':
                one_per_line = true;
                off++;
                break;
            case 'n':
                no_newline = true;
                off++;
                break;
            case 'r':
                raw = true;
                off++;
                break;
            case 'P':
                prompt_expand_flag = true;
                off++;
                break;
            case 'u':
                if (arg[off + 1] != '\0') {
                    char *endp = NULL;
                    long fd_val = strtol(arg + off + 1, &endp, 10);
                    if (!endp || *endp != '\0' || fd_val < 0) {
                        shell_error_t *error = shell_error_create(
                            SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                            builtin_get_source_location(),
                            "print: -u: invalid file descriptor: %s",
                            arg + off + 1);
                        shell_error_display(error, stderr,
                                            isatty(STDERR_FILENO));
                        shell_error_free(error);
                        return 1;
                    }
                    target_fd = (int)fd_val;
                    consumed_glued_value = true;
                } else {
                    /// -u with next-arg form: handled after the inner
                    /// loop because we must advance `i` first.
                    off++;
                }
                break;
            case 'f':
                if (arg[off + 1] != '\0') {
                    fmt_str = arg + off + 1;
                    consumed_glued_value = true;
                } else {
                    off++;
                }
                break;
            default:
                /// Unknown short flag. zsh prints "bad option" and exits
                /// with status 1; mirror that.
                {
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                        builtin_get_source_location(), "print: bad option: -%c",
                        c);
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    return 1;
                }
            }
        }

        i++;

        /// Handle -u/-f when the value comes from the next argv. The
        /// inner loop stops at the trailing 'u' or 'f' (no glued value
        /// after it); pull the value from argv[i].
        if (!consumed_glued_value &&
            (last_letter == 'u' || last_letter == 'f')) {
            if (i >= argc) {
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                    builtin_get_source_location(),
                    "print: -%c: argument expected", last_letter);
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                return 1;
            }
            if (last_letter == 'u') {
                char *endp = NULL;
                long fd_val = strtol(argv[i], &endp, 10);
                if (!endp || *endp != '\0' || fd_val < 0) {
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                        builtin_get_source_location(),
                        "print: -u: invalid file descriptor: %s", argv[i]);
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    return 1;
                }
                target_fd = (int)fd_val;
            } else {
                fmt_str = argv[i];
            }
            i++;
        }
    }

    int arg_start = i;

    /// -f delegates to bin_printf with FMT as argv[1] and remaining
    /// positional args following. Inherits the full printf format-spec
    /// surface (%s, %d, %x, %.Ns, etc.) without duplicating any of it
    /// here. -n is the only print-side flag that interacts with -f:
    /// zsh's `print -nf FMT a b c` produces no trailing newline. -u
    /// also composes with -f.
    if (fmt_str) {
        /// Rebuild argv as if it were ["printf", FMT, args...]. The
        /// extra slot at index 0 is the synthetic argv[0] bin_printf
        /// expects.
        int new_argc = 2 + (argc - arg_start);
        char **new_argv = malloc((size_t)(new_argc + 1) * sizeof(char *));
        if (!new_argv) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_OUT_OF_MEMORY, SHELL_SEVERITY_ERROR,
                builtin_get_source_location(), "print: out of memory");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            return 1;
        }
        new_argv[0] = (char *)"printf";
        new_argv[1] = (char *)fmt_str;
        for (int k = 0; k < argc - arg_start; k++) {
            new_argv[2 + k] = argv[arg_start + k];
        }
        new_argv[new_argc] = NULL;

        /// bin_printf writes to stdout. For target_fd != stdout we'd
        /// need to dup-restore; document the constraint by erroring
        /// on the combination rather than silently dropping output.
        /// Most zsh callers using -f use stdout; -u is an edge case
        /// that can be added later if a real script needs it.
        if (target_fd != STDOUT_FILENO) {
            int saved = dup(STDOUT_FILENO);
            if (saved < 0 || dup2(target_fd, STDOUT_FILENO) < 0) {
                if (saved >= 0) {
                    close(saved);
                }
                free(new_argv);
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                    builtin_get_source_location(),
                    "print: -u: cannot redirect to fd %d: %s", target_fd,
                    strerror(errno));
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                return 1;
            }
            int rc = bin_printf(new_argc, new_argv);
            fflush(stdout);
            dup2(saved, STDOUT_FILENO);
            close(saved);
            free(new_argv);
            return rc;
        }
        int rc = bin_printf(new_argc, new_argv);
        free(new_argv);
        return rc;
    }

    /// Default / -l / -n / -r / -P path. Build the joined output,
    /// then emit in one write() to the target fd so partial writes
    /// across split args are avoided.
    const char *sep = one_per_line ? "\n" : " ";
    size_t sep_len = strlen(sep);

    /// Compute total length needed. For -P we don't know post-expansion
    /// length, so use a per-arg buffer of PRINT_PROMPT_BUF_SIZE.
    size_t total_cap = 64;
    size_t total_len = 0;
    char *buf = malloc(total_cap);
    if (!buf) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_OUT_OF_MEMORY, SHELL_SEVERITY_ERROR,
            builtin_get_source_location(), "print: out of memory");
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    for (int k = arg_start; k < argc; k++) {
        const char *src = argv[k];
        char *processed = NULL;
        char prompt_buf[PRINT_PROMPT_BUF_SIZE];

        if (prompt_expand_flag) {
            /// Route through the unified prompt expansion engine. The
            /// minimal context is fine for `print -P`: no template
            /// segments (template_ctx=NULL), runtime values pulled
            /// from the live shell state.
            lle_prompt_expand_ctx_t pctx = {
                .template_ctx = NULL,
                .last_exit_status = last_exit_status,
                .job_count = 0,
                .history_number = 0,
                .command_number = 0,
                .color_depth = isatty(target_fd) ? 3 : 0,
            };
            lle_result_t r =
                lle_prompt_expand(src, prompt_buf, sizeof(prompt_buf), &pctx);
            if (r == LLE_SUCCESS) {
                src = prompt_buf;
            }
            /// On failure fall through and emit the raw argument so
            /// the user at least sees something.
        } else if (!raw) {
            /// Default: process \n, \t, \xNN, etc. like bin_echo's
            /// XPG-mode path.
            processed = process_escape_sequences(argv[k]);
            if (processed) {
                src = processed;
            }
        }

        size_t src_len = strlen(src);
        size_t sep_chunk = (k > arg_start) ? sep_len : 0;
        size_t need = total_len + sep_chunk + src_len + 1 /* trailing NL */ + 1;
        while (need > total_cap) {
            total_cap *= 2;
            char *nb = realloc(buf, total_cap);
            if (!nb) {
                free(processed);
                free(buf);
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_OUT_OF_MEMORY, SHELL_SEVERITY_ERROR,
                    builtin_get_source_location(), "print: out of memory");
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                return 1;
            }
            buf = nb;
        }
        if (sep_chunk) {
            memcpy(buf + total_len, sep, sep_chunk);
            total_len += sep_chunk;
        }
        memcpy(buf + total_len, src, src_len);
        total_len += src_len;
        free(processed);
    }

    if (!no_newline) {
        buf[total_len++] = '\n';
    }

    int rc = print_emit(target_fd, buf, total_len);
    free(buf);

    if (rc != 0) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                               builtin_get_source_location(),
                               "print: write error: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    return 0;
}
