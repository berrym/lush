/**
 * @file bin_printf.c
 * @brief `printf` builtin -- formatted output
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Process escape sequences in a string
 *
 * Converts escape sequences like \n, \t, \r, etc. to their
 * corresponding characters.
 *
 * @param str The string to process
 * @return Newly allocated string with escapes processed (caller must free),
 *         or NULL on error
 */
/* Non-static -- also called from src/builtins/bin_echo.c. The helper
 * is a per-file colocator rather than a public API; bin_echo.c
 * forward-declares it locally rather than promoting it through
 * include/builtins.h. */
char *process_escape_sequences(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    const char *src = str;
    char *dst = result;

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++; /// Skip the backslash
            switch (*src) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            case 'b':
                *dst++ = '\b';
                break;
            case 'a':
                *dst++ = '\a';
                break;
            case 'v':
                *dst++ = '\v';
                break;
            case 'f':
                *dst++ = '\f';
                break;
            case '\\':
                *dst++ = '\\';
                break;
            case '"':
                *dst++ = '"';
                break;
            case '\'':
                *dst++ = '\'';
                break;
            default:
                *dst++ = '\\';
                *dst++ = *src;
                break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
    }

    *dst = '\0';
    return result;
}

/**
 * @brief Formatted output to stdout
 *
 * Implements the printf builtin with POSIX format specifier support.
 * Handles width specifiers, precision, and all standard format conversions
 * including %s, %d, %i, %c, %x, %X, %o, %u, %f, %g, %e.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is format, rest are arguments)
 * @return 0 on success, 1 on usage error
 */
int bin_printf(int argc, char **argv) {
    /// `printf -v VAR FMT ARGS` assigns the formatted output to VAR
    /// instead of writing it to stdout.
    const char *assign_var = NULL;
    int base = 1;
    if (argc >= 2 && strcmp(argv[1], "-v") == 0) {
        if (argc < 3) {
            fprintf(stderr, "lush: printf: -v: option requires a variable "
                            "name\n");
            return 1;
        }
        assign_var = argv[2];
        base = 3;
    }

    if (argc < base + 1) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "missing format argument");
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
                for (size_t k = 0; k < current_executor->context_depth &&
                                   k < SHELL_ERROR_CONTEXT_MAX;
                     k++) {
                    if (current_executor->context_stack[k]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[k]);
                    }
                }
            }
            shell_error_set_suggestion(err,
                                       "usage: printf format [arguments ...]");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr,
                    "lush: printf: usage: printf format [arguments ...]\n");
        }
        return 1;
    }

    const char *format = argv[base];
    int arg_index = base + 1;

    /// For `-v`, redirect stdout to a temp file for the duration so the
    /// existing per-conversion output code is unchanged; the captured
    /// bytes are read back and assigned after the format loop.
    int printf_saved_fd = -1;
    FILE *printf_capture = NULL;
    if (assign_var) {
        fflush(stdout);
        printf_capture = tmpfile();
        if (printf_capture) {
            printf_saved_fd = dup(STDOUT_FILENO);
            if (printf_saved_fd < 0 ||
                dup2(fileno(printf_capture), STDOUT_FILENO) < 0) {
                if (printf_saved_fd >= 0) {
                    close(printf_saved_fd);
                }
                fclose(printf_capture);
                printf_capture = NULL;
                printf_saved_fd = -1;
            }
        }
    }

    /// POSIX: The format string is reused as often as necessary to satisfy
    /// the remaining arguments. If the format string contains no conversion
    /// specifications, and there are arguments, the format is used once and
    /// subsequent arguments are ignored.
    do {
        int args_used_this_pass = 0;

        for (int i = 0; format[i] != '\0'; i++) {
            if (format[i] == '%' && format[i + 1] != '\0') {
                int spec_start = i; /// points at '%' for forwarding to libc
                i++;                /// Skip the %

                /// Handle literal %
                if (format[i] == '%') {
                    putchar('%');
                    continue;
                }

                /// Parse flags, width, precision
                int width = 0;
                int precision = -1;
                bool zero_pad = false;
                bool left_align = false;

                /// Parse flags
                while (format[i] == '-' || format[i] == '+' ||
                       format[i] == ' ' || format[i] == '#' ||
                       format[i] == '0') {
                    if (format[i] == '0') {
                        zero_pad = true;
                    }
                    if (format[i] == '-') {
                        left_align = true;
                    }
                    i++;
                }

                /// Parse width
                if (format[i] == '*') {
                    /// Dynamic width from argument
                    if (arg_index < argc) {
                        width = atoi(argv[arg_index]);
                        if (width < 0) {
                            left_align = true;
                            width = -width;
                        }
                        arg_index++;
                    }
                    i++;
                } else if (isdigit(format[i])) {
                    while (isdigit(format[i])) {
                        width = width * 10 + (format[i] - '0');
                        i++;
                    }
                }

                /// Parse precision
                if (format[i] == '.') {
                    i++;
                    if (format[i] == '*') {
                        /// Dynamic precision from argument
                        if (arg_index < argc) {
                            precision = atoi(argv[arg_index]);
                            arg_index++;
                        }
                        i++;
                    } else {
                        precision = 0;
                        while (isdigit(format[i])) {
                            precision = precision * 10 + (format[i] - '0');
                            i++;
                        }
                    }
                }

                /// Handle conversion specifier
                char specifier = format[i];
                /// Get the argument for the format specifier
                const char *format_arg =
                    (arg_index < argc) ? argv[arg_index] : "";

                /// Reconstruct the user's original conversion spec
                /// (e.g. "%05d", "%+8.2f") so libc printf handles all
                /// flags — `0`, `+`, ` `, `#`, `-` — uniformly across
                /// every numeric specifier. Without this, lush would
                /// silently drop every flag except `-`.
                char fwd_fmt[64];
                {
                    size_t fwd_len = (size_t)(i - spec_start + 1);
                    if (fwd_len >= sizeof(fwd_fmt)) {
                        fwd_len = sizeof(fwd_fmt) - 1;
                    }
                    memcpy(fwd_fmt, &format[spec_start], fwd_len);
                    fwd_fmt[fwd_len] = '\0';
                }
                (void)width;
                (void)left_align;
                (void)zero_pad;

                switch (specifier) {
                case 's': {
                    /// String format
                    int len = strlen(format_arg);
                    int padding = (width > len) ? width - len : 0;

                    if (!left_align && padding > 0) {
                        /// Right-align with padding
                        char pad_char = zero_pad ? '0' : ' ';
                        for (int p = 0; p < padding; p++) {
                            putchar(pad_char);
                        }
                    }

                    /// Print the string (truncated if precision specified)
                    if (precision >= 0 && precision < len) {
                        for (int j = 0; j < precision; j++) {
                            putchar(format_arg[j]);
                        }
                    } else {
                        fputs(format_arg, stdout);
                    }

                    if (left_align && padding > 0) {
                        /// Left-align with padding
                        for (int p = 0; p < padding; p++) {
                            putchar(' ');
                        }
                    }

                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    int value = (arg_index < argc) ? atoi(format_arg) : 0;
                    printf(fwd_fmt, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'c': {
                    /// Character format
                    int value = (arg_index < argc) ? atoi(format_arg) : 0;

                    if (!left_align && width > 1) {
                        /// Right-align with padding
                        for (int p = 1; p < width; p++) {
                            putchar(' ');
                        }
                    }

                    putchar(value);

                    if (left_align && width > 1) {
                        /// Left-align with padding
                        for (int p = 1; p < width; p++) {
                            putchar(' ');
                        }
                    }

                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'x':
                case 'X':
                case 'o':
                case 'u': {
                    unsigned int value =
                        (arg_index < argc)
                            ? (unsigned int)strtoul(format_arg, NULL, 10)
                            : 0;
                    printf(fwd_fmt, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                case 'e':
                case 'E': {
                    double value =
                        (arg_index < argc) ? strtod(format_arg, NULL) : 0.0;
                    printf(fwd_fmt, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                default:
                    /// Unknown specifier - just print as is
                    putchar('%');
                    putchar(specifier);
                    break;
                }
            } else if (format[i] == '\\' && format[i + 1] != '\0') {
                /// Handle escaped characters
                i++;
                switch (format[i]) {
                case 'n':
                    putchar('\n');
                    break;
                case 't':
                    putchar('\t');
                    break;
                case 'r':
                    putchar('\r');
                    break;
                case 'b':
                    putchar('\b');
                    break;
                case 'f':
                    putchar('\f');
                    break;
                case 'a':
                    putchar('\a');
                    break;
                case 'v':
                    putchar('\v');
                    break;
                case '\\':
                    putchar('\\');
                    break;
                case '"':
                    putchar('"');
                    break;
                case '\'':
                    putchar('\'');
                    break;
                default:
                    /// Unknown escape - print as literal
                    putchar('\\');
                    putchar(format[i]);
                    break;
                }
            } else {
                /// Regular character
                putchar(format[i]);
            }
        }

        /// If no format specifiers consumed arguments, stop to avoid infinite
        /// loop
        if (args_used_this_pass == 0) {
            break;
        }

    } while (arg_index < argc);

    if (assign_var) {
        fflush(stdout);
        if (printf_capture) {
            if (printf_saved_fd >= 0) {
                dup2(printf_saved_fd, STDOUT_FILENO);
                close(printf_saved_fd);
            }
            long n = ftell(printf_capture);
            if (n < 0) {
                n = 0;
            }
            rewind(printf_capture);
            char *buf = malloc((size_t)n + 1);
            if (buf) {
                size_t got = fread(buf, 1, (size_t)n, printf_capture);
                buf[got] = '\0';
                symtable_set_global(assign_var, buf);
                free(buf);
            }
            fclose(printf_capture);
        } else {
            /// Capture setup failed; clear the target rather than leave a
            /// stale value.
            symtable_set_global(assign_var, "");
        }
    }

    return 0;
}
