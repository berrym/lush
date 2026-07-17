/**
 * @file brace_match.h
 * @brief Canonical brace/paren matcher for the shell
 *
 * Replaces the legacy `find_closing_brace` in the retired
 * `src/strings.c`. The legacy implementation had four real defects:
 *
 *   1. Took a mutable `char *` where the only mutation was at call
 *      sites that needed to cast away `const`.
 *   2. Required NUL-terminated input.
 *   3. One-byte lookback for backslash escapes -- `\\"` (backslash +
 *      escaped quote) misclassified the quote as escaped because it
 *      saw `\` at `s[i-1]` instead of recognizing that the previous
 *      `\\` was a literal pair.
 *   4. Treated POSIX `'...'` as if it had escape processing (it does
 *      not) and did not recognize `$'...'` (ANSI-C quoting) at all.
 *
 * `lush_find_matching_brace` fixes all four:
 *
 *   - takes `const char *`
 *   - takes explicit length (or 0 for strlen-fallback for back-compat)
 *   - consumes `\X` as a two-byte unit so `\\` does NOT shadow the
 *     next byte
 *   - distinguishes POSIX `'...'` (literal, no escapes), `$'...'`
 *     (ANSI-C, `\` is an escape), `"..."`, and `` `...` ``
 *   - returns success/fail explicitly via bool + out-pointer so a
 *     "no match" cannot be confused with a valid offset of 0
 *
 * Per the project Unicode-mandatory policy, the implementation walks
 * the input by codepoint through `lle_utf8_decode_codepoint` so multi-
 * byte sequences advance atomically and the ASCII control bytes
 * `{`/`}`/`(`/`)`/`\`/`'`/`"`/`` ` ``/`$` are never falsely matched
 * against a continuation byte inside a multi-byte sequence.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_BRACE_MATCH_H
#define LUSH_BRACE_MATCH_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Find the closing brace/paren that matches `s[0]`.
 *
 * Walks `s` (which must start with `{` or `(`), tracking nested groups
 * and four shell-quote dialects, and writes the index of the matching
 * closing brace into `*out_offset` on success.
 *
 * Quote handling:
 *   - `'...'`     POSIX single-quoted -- literal; no escape processing.
 *                 A backslash inside is just a backslash; the idiom
 *                 `'\''` correctly parses as close, literal `\`, open.
 *   - `"..."`     double-quoted -- `\X` consumes both bytes as an
 *                 atomic unit so `\\` does not shadow the next quote.
 *   - `` `...` `` backtick command-sub -- same `\X`-consume behavior
 *                 as double quotes.
 *   - `$'...'`    ANSI-C quoted -- `\X` is an escape sequence; in
 *                 particular `\'` is a literal quote, NOT a close.
 *
 * Outside any quote, `\X` consumes both bytes.
 *
 * For a `(` group the scan is additionally shell-structure-aware so a
 * `case` pattern's closing `)` -- which has no matching `(` -- is
 * recognized as case syntax rather than a group close (`$(case x in x)
 * echo M;; esac)` matches the final `)`, not the pattern one; #494).
 * `case`/`esac` are recognized only at command position and `in` only
 * after a `case` subject, so those words used as ordinary arguments do
 * not affect the scan. A `#` comment at a word boundary runs to end of
 * line (so a `)` inside it is not a close). Without a `case`, a `(`
 * group scans byte-identically to a plain nested-paren match. The `{`
 * path is a plain nested match with no structure awareness.
 *
 * Multi-byte UTF-8 sequences are advanced atomically via
 * `lle_utf8_decode_codepoint`. Invalid UTF-8 falls through as a one-
 * byte advance so the scan never gets stuck.
 *
 * If `len == 0` the function falls back to `strlen(s)` so existing
 * NUL-terminated callers can be migrated without immediately threading
 * a length through every call site.
 *
 * @param s            Source pointer; must start with `{` or `(`.
 * @param len          Bytes of `s` to scan, or 0 to use strlen(s).
 * @param out_offset   On success, receives the closing-brace index
 *                     relative to `s[0]` (always >= 1). Unchanged on
 *                     failure.
 * @return true on a balanced match; false on bad opener, unterminated
 *         quote, or unbalanced braces.
 */
bool lush_find_matching_brace(const char *s, size_t len, size_t *out_offset);

#endif /* LUSH_BRACE_MATCH_H */
