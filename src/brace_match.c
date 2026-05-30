/**
 * @file brace_match.c
 * @brief Implementation of lush_find_matching_brace (see brace_match.h).
 *
 * Codepoint-aware scan via lle_utf8_decode_codepoint: ASCII control
 * bytes drive the state machine; multi-byte sequences advance
 * atomically and never falsely trigger on a continuation byte that
 * happens to alias an ASCII quote/brace byte (UTF-8's self-synchronizing
 * property already makes this safe at the byte level, but going through
 * the canonical decoder is the principled choice and keeps this file
 * consistent with the rest of lush's text-handling code).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "brace_match.h"

#include "lle/utf8_support.h"

#include <stdint.h>
#include <string.h>

/// Advance `*i` past one input unit at `s[*i]`. Multi-byte codepoints
/// advance atomically; malformed UTF-8 falls back to a single byte so
/// the caller never gets stuck. Returns the byte length advanced
/// (always >= 1).
static size_t step_one(const char *s, size_t len, size_t i) {
    uint32_t cp = 0;
    int step = lle_utf8_decode_codepoint(s + i, len - i, &cp);
    return (step > 1) ? (size_t)step : 1;
}

bool lush_find_matching_brace(const char *s, size_t len, size_t *out_offset) {
    if (!s || !out_offset) {
        return false;
    }
    if (len == 0) {
        len = strlen(s);
    }
    if (len < 2) {
        return false;
    }

    char open = s[0];
    char close;
    if (open == '{') {
        close = '}';
    } else if (open == '(') {
        close = ')';
    } else {
        return false;
    }

    int depth = 1; ///< we are inside one open brace already
    size_t i = 1;

    while (i < len) {
        uint32_t cp = 0;
        int step = lle_utf8_decode_codepoint(s + i, len - i, &cp);
        if (step <= 0) {
            /// Malformed UTF-8: skip one byte defensively.
            i += 1;
            continue;
        }
        if (step > 1) {
            /// Multi-byte codepoint: advance atomically; no ASCII
            /// control byte can be embedded in it.
            i += (size_t)step;
            continue;
        }

        unsigned char c = (unsigned char)s[i];

        /// Outside any quote: backslash consumes the NEXT byte as one
        /// atomic unit. This is the key fix for the legacy one-byte
        /// lookback bug -- `\\` advances i by 2, so the byte after the
        /// pair is processed cleanly as itself.
        if (c == '\\') {
            i += 2;
            continue;
        }

        /// `$'...'` -- ANSI-C quoting. `\X` IS an escape inside, so
        /// `\'` is a literal quote and does NOT close the quote.
        if (c == '$' && i + 1 < len && s[i + 1] == '\'') {
            i += 2; ///< past the `$'`
            while (i < len && s[i] != '\'') {
                if (s[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    i += step_one(s, len, i);
                }
            }
            if (i >= len) {
                return false; ///< unterminated $'...'
            }
            i += 1; ///< past the closing `'`
            continue;
        }

        /// `'...'` -- POSIX single-quoted: literal, no escape processing.
        /// The legacy code treated `\'` as escaped here; that breaks the
        /// `'\''` idiom. We treat backslash as a literal byte.
        if (c == '\'') {
            i += 1;
            while (i < len && s[i] != '\'') {
                i += step_one(s, len, i);
            }
            if (i >= len) {
                return false; ///< unterminated '...'
            }
            i += 1;
            continue;
        }

        /// `"..."` and `` `...` `` -- backslash escapes the next byte
        /// as a unit. Same consume-forward invariant as the outer
        /// scanner; in particular `\\` does NOT shadow the next quote.
        if (c == '"' || c == '`') {
            char q = (char)c;
            i += 1;
            while (i < len && s[i] != q) {
                if (s[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    i += step_one(s, len, i);
                }
            }
            if (i >= len) {
                return false; ///< unterminated "..." / `...`
            }
            i += 1;
            continue;
        }

        /// Plain depth tracking on the active brace type.
        if (c == (unsigned char)open) {
            depth++;
        } else if (c == (unsigned char)close) {
            depth--;
            if (depth == 0) {
                *out_offset = i;
                return true;
            }
        }
        i += 1;
    }

    return false; ///< ran off the end -- unbalanced
}
