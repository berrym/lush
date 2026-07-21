/**
 * @file escape.c
 * @brief Canonical backslash-escape expansion (see escape.h)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "escape.h"

#include "lle/utf8_support.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int lush_escape_basic_char(char c) {
    switch (c) {
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    case '\\':
        return '\\';
    case '"':
        return '"';
    case '\'':
        return '\'';
    default:
        return -1;
    }
}

size_t lush_decode_one_escape(const char *s, size_t len,
                              lush_escape_dialect_t dialect, char *out,
                              size_t *out_n, bool *terminated) {
    *out_n = 0;
    if (terminated) {
        *terminated = false;
    }
    /// A lone backslash (or non-backslash) copies through literally.
    if (len < 2 || s[0] != '\\') {
        out[(*out_n)++] = '\\';
        return 1;
    }

    char next = s[1];
    int basic = lush_escape_basic_char(next);
    if (basic >= 0) {
        out[(*out_n)++] = (char)basic;
        return 2;
    }
    if (dialect == LUSH_ESC_SIMPLE) {
        /// Unknown escape: keep the backslash; the next byte is the
        /// caller's problem.
        out[(*out_n)++] = '\\';
        return 1;
    }

    switch (next) {
    case 'e':
    case 'E':
        out[(*out_n)++] = '\033';
        return 2;
    case '?':
        /// `\?` -> `?` is an ANSI-C nicety; the echo/printf dialects leave
        /// it a literal `\?`.
        if (dialect == LUSH_ESC_ANSI_C) {
            out[(*out_n)++] = '?';
            return 2;
        }
        out[(*out_n)++] = '\\';
        return 1;
    case 'x': {
        char hex[3] = {0};
        int n = 0;
        for (int j = 0; j < 2 && 2 + (size_t)j < len; j++) {
            if (isxdigit((unsigned char)s[2 + j])) {
                hex[n++] = s[2 + j];
            } else {
                break;
            }
        }
        if (n > 0) {
            out[(*out_n)++] = (char)strtoul(hex, NULL, 16);
            return 2 + (size_t)n;
        }
        out[(*out_n)++] = '\\';
        return 1;
    }
    case 'u':
    case 'U': {
        int want = (next == 'u') ? 4 : 8;
        char hex[9] = {0};
        int n = 0;
        for (int j = 0; j < want && 2 + (size_t)j < len; j++) {
            if (isxdigit((unsigned char)s[2 + j])) {
                hex[n++] = s[2 + j];
            } else {
                break;
            }
        }
        if (n == want) {
            uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
            int enc = lle_utf8_encode_codepoint(cp, out);
            if (enc > 0) {
                *out_n = (size_t)enc;
            }
            return 2 + (size_t)n;
        }
        out[(*out_n)++] = '\\';
        return 1;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
        /// Octal, but the accepted form differs by dialect:
        ///   XSI (echo / print / %b): only `\0NNN` -- the `\0` is a required
        ///     prefix, then up to three octal digits; a bare `\1`..`\7`
        ///     stays literal (the bash+zsh echo consensus, POSIX XSI).
        ///   ANSI-C / PRINTF_FMT: `\NNN` -- up to three octal digits from
        ///     the first digit (`\101` -> A, `\0101` -> `\010` + `1`).
        if (dialect == LUSH_ESC_XSI && next != '0') {
            out[(*out_n)++] = '\\';
            return 1;
        }
        size_t digits_at = (dialect == LUSH_ESC_XSI) ? 2 : 1;
        size_t prefix = (dialect == LUSH_ESC_XSI) ? 2 : 1;
        char oct[4] = {0};
        int n = 0;
        for (int j = 0; j < 3 && digits_at + (size_t)j < len; j++) {
            char c = s[digits_at + j];
            if (c >= '0' && c <= '7') {
                oct[n++] = c;
            } else {
                break;
            }
        }
        unsigned int v = (n > 0) ? (unsigned int)strtoul(oct, NULL, 8) : 0;
        out[(*out_n)++] = (char)(v & 0xFF);
        return prefix + (size_t)n;
    }
    case 'c':
        if (dialect == LUSH_ESC_ANSI_C) {
            /// ANSI-C `\cX` -> control character.
            if (len >= 3) {
                char ctrl = s[2];
                if (ctrl >= '@' && ctrl <= '_') {
                    out[(*out_n)++] = (char)(ctrl - '@');
                    return 3;
                }
                if (ctrl >= 'a' && ctrl <= 'z') {
                    out[(*out_n)++] = (char)(ctrl - 'a' + 1);
                    return 3;
                }
                if (ctrl == '?') {
                    out[(*out_n)++] = 127; /// DEL
                    return 3;
                }
            }
            out[(*out_n)++] = '\\';
            return 1;
        }
        /// XSI / PRINTF_FMT `\c` -> terminate output. Consume the `\c`,
        /// emit nothing, and signal the caller to stop.
        if (terminated) {
            *terminated = true;
        }
        return 2;
    default:
        /// Unknown escape: keep the backslash; the following byte copies
        /// through on the caller's next step.
        out[(*out_n)++] = '\\';
        return 1;
    }
}

char *lush_expand_escapes_ex(const char *str, size_t len,
                             lush_escape_dialect_t dialect, bool *terminated,
                             size_t *out_len) {
    if (terminated) {
        *terminated = false;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!str || len == 0) {
        return strdup("");
    }

    /// Worst case is ~4x for \u/\U Unicode expansion; grow on demand.
    size_t cap = len * 4 + 1;
    char *out = malloc(cap);
    if (!out) {
        return strdup("");
    }

    size_t pos = 0;
    size_t i = 0;
    bool done = false;
    while (i < len && !done) {
        /// Ensure room for the worst single step (a 4-byte \u sequence).
        if (pos + 8 >= cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                return strdup("");
            }
            out = grown;
        }

        if (str[i] != '\\') {
            out[pos++] = str[i++];
            continue;
        }

        char dec[8];
        size_t dn = 0;
        bool term = false;
        size_t consumed =
            lush_decode_one_escape(&str[i], len - i, dialect, dec, &dn, &term);
        for (size_t k = 0; k < dn; k++) {
            out[pos++] = dec[k];
        }
        i += consumed;
        if (term) {
            if (terminated) {
                *terminated = true;
            }
            done = true;
        }
    }

    out[pos] = '\0';
    if (out_len) {
        *out_len = pos;
    }
    return out;
}

char *lush_expand_escapes(const char *str, size_t len,
                          lush_escape_dialect_t dialect) {
    return lush_expand_escapes_ex(str, len, dialect, NULL, NULL);
}
