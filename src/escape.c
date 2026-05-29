/**
 * @file escape.c
 * @brief Canonical backslash-escape expansion (see escape.h)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "escape.h"

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

/// Encode `cp` as UTF-8 into `out` (must hold >= 4 bytes); return byte count.
static size_t encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

char *lush_expand_escapes(const char *str, size_t len,
                          lush_escape_dialect_t dialect) {
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
    while (i < len) {
        if (str[i] != '\\' || i + 1 >= len) {
            out[pos++] = str[i++];
        } else {
            char next = str[i + 1];
            int basic = lush_escape_basic_char(next);
            if (basic >= 0) {
                out[pos++] = (char)basic;
                i += 2;
            } else if (dialect == LUSH_ESC_SIMPLE) {
                /// Unknown escape: keep the backslash; the following
                /// character copies through on the next iteration.
                out[pos++] = str[i++];
            } else {
                /// ANSI-C extended forms.
                switch (next) {
                case 'e':
                case 'E':
                    out[pos++] = '\033';
                    i += 2;
                    break;
                case '?':
                    out[pos++] = '?';
                    i += 2;
                    break;
                case 'x': {
                    char hex[3] = {0};
                    int n = 0;
                    for (int j = 0; j < 2 && i + 2 + (size_t)j < len; j++) {
                        if (isxdigit((unsigned char)str[i + 2 + j])) {
                            hex[n++] = str[i + 2 + j];
                        } else {
                            break;
                        }
                    }
                    if (n > 0) {
                        out[pos++] = (char)strtoul(hex, NULL, 16);
                        i += 2 + (size_t)n;
                    } else {
                        out[pos++] = str[i++];
                    }
                    break;
                }
                case 'u':
                case 'U': {
                    int want = (next == 'u') ? 4 : 8;
                    char hex[9] = {0};
                    int n = 0;
                    for (int j = 0; j < want && i + 2 + (size_t)j < len; j++) {
                        if (isxdigit((unsigned char)str[i + 2 + j])) {
                            hex[n++] = str[i + 2 + j];
                        } else {
                            break;
                        }
                    }
                    if (n == want) {
                        uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                        pos += encode_utf8(cp, out + pos);
                        i += 2 + (size_t)n;
                    } else {
                        out[pos++] = str[i++];
                    }
                    break;
                }
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7': {
                    char oct[4] = {0};
                    int n = 0;
                    for (int j = 0; j < 3 && i + 1 + (size_t)j < len; j++) {
                        char c = str[i + 1 + j];
                        if (c >= '0' && c <= '7') {
                            oct[n++] = c;
                        } else {
                            break;
                        }
                    }
                    if (n > 0) {
                        unsigned int v = (unsigned int)strtoul(oct, NULL, 8);
                        out[pos++] = (char)(v & 0xFF);
                        i += 1 + (size_t)n;
                    } else {
                        out[pos++] = str[i++];
                    }
                    break;
                }
                case 'c':
                    if (i + 2 < len) {
                        char ctrl = str[i + 2];
                        if (ctrl >= '@' && ctrl <= '_') {
                            out[pos++] = (char)(ctrl - '@');
                            i += 3;
                        } else if (ctrl >= 'a' && ctrl <= 'z') {
                            out[pos++] = (char)(ctrl - 'a' + 1);
                            i += 3;
                        } else if (ctrl == '?') {
                            out[pos++] = 127; /// DEL
                            i += 3;
                        } else {
                            out[pos++] = str[i++];
                        }
                    } else {
                        out[pos++] = str[i++];
                    }
                    break;
                default:
                    /// Unknown escape: keep the backslash; the char
                    /// copies through next iteration.
                    out[pos++] = str[i++];
                    break;
                }
            }
        }

        if (pos + 4 >= cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                return strdup("");
            }
            out = grown;
        }
    }

    out[pos] = '\0';
    return out;
}
