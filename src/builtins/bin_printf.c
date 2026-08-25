/**
 * @file bin_printf.c
 * @brief `printf` builtin -- formatted output
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "escape.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    ///
    /// A `\c` in the format string or in a `%b` argument terminates all
    /// output immediately (no further characters, no further reuse passes).
    bool printf_terminated = false;
    do {
        int args_used_this_pass = 0;

        for (int i = 0; format[i] != '\0' && !printf_terminated; i++) {
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
                /// flags -- `0`, `+`, ` `, `#`, `-` -- uniformly across
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
                case 'b': {
                    /// %b: interpret backslash escapes in the argument with the
                    /// XSI echo set (\n \t \xHH \0NNN \e ...); a \c terminates
                    /// all printf output. Decoded bytes may include NULs, so
                    /// write by length rather than as a C string.
                    ///
                    /// Octal here is the POSIX form `\0NNN`; a bare `\NNN`
                    /// stays literal. This is POSIX-conformant and matches zsh;
                    /// bash and dash additionally accept a bare `\NNN` in %b,
                    /// so lush is stricter than those two by curation (one
                    /// coherent XSI octal rule shared with echo), not by
                    /// consensus.
                    size_t out_len = 0;
                    bool arg_terminated = false;
                    char *decoded = lush_expand_escapes_ex(
                        format_arg, strlen(format_arg), LUSH_ESC_XSI,
                        &arg_terminated, &out_len);
                    if (decoded) {
                        fwrite(decoded, 1, out_len, stdout);
                        free(decoded);
                    }
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    if (arg_terminated) {
                        printf_terminated = true;
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
                /// Decode one escape via the shared engine in the printf
                /// format dialect (\NNN octal, \xHH, \e, \uHHHH, and \c which
                /// terminates output). Advance past exactly what it consumed.
                char dec[8];
                size_t dn = 0;
                bool esc_terminated = false;
                size_t consumed = lush_decode_one_escape(
                    &format[i], strlen(&format[i]), LUSH_ESC_PRINTF_FMT, dec,
                    &dn, &esc_terminated);
                fwrite(dec, 1, dn, stdout);
                if (esc_terminated) {
                    printf_terminated = true;
                }
                /// The loop's own i++ advances one; consume the rest here.
                i += (int)consumed - 1;
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

    } while (arg_index < argc && !printf_terminated);

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
                /// `-v` takes an assignable target, and an array element
                /// address is one. This wrote the whole string as a scalar
                /// NAME, so `printf -v 'a[2]'` bound nothing and reported
                /// success (#643).
                int elem_rc = 0;
                if (!builtin_assign_element_target(assign_var, buf, &elem_rc)) {
                    symtable_set_global(assign_var, buf);
                } else if (elem_rc != 0) {
                    free(buf);
                    fclose(printf_capture);
                    return elem_rc;
                }
                free(buf);
            }
            fclose(printf_capture);
        } else {
            /// Capture setup failed; clear the target rather than leave a
            /// stale value. An element target is cleared in place, not
            /// shadowed by a scalar of the same spelling.
            int elem_rc = 0;
            if (!builtin_assign_element_target(assign_var, "", &elem_rc)) {
                symtable_set_global(assign_var, "");
            }
        }
    }

    return 0;
}
