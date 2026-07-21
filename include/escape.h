/**
 * @file escape.h
 * @brief Canonical backslash-escape expansion for the shell
 *
 * One place for every "turn `\n` into a newline" transformation. Two
 * dialects share a single engine:
 *
 *   LUSH_ESC_SIMPLE  - the bare C-escape subset: \a \b \f \n \r \t \v
 *                      \\ \" \'. An unrecognized `\x` is left as the two
 *                      literal characters. (Legacy; the shell-facing
 *                      builtins use the richer dialects below.)
 *
 *   LUSH_ESC_ANSI_C  - the full ANSI-C ($'...') set, also used by the
 *                      ${var@E} parameter transform: the simple set plus
 *                      \e/\E, \?, \xHH, \uHHHH, \UHHHHHHHH, \cX control
 *                      characters, and \NNN / \0NNN octal.
 *
 *   LUSH_ESC_XSI     - the XSI echo set used by `echo -e`, `print`, and
 *                      the printf `%b` argument: the simple set plus
 *                      \e/\E, \xHH, \uHHHH, \UHHHHHHHH, octal only in the
 *                      `\0NNN` form (a bare `\NNN` stays literal, matching
 *                      the bash+zsh echo consensus and POSIX XSI), and
 *                      `\c` which *terminates* output (see the `terminated`
 *                      out-param).
 *
 *   LUSH_ESC_PRINTF_FMT - the printf *format-string* set: like XSI but
 *                      octal is the POSIX printf `\NNN` form (no leading
 *                      `0` required); `\c` also terminates.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_ESCAPE_H
#define LUSH_ESCAPE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LUSH_ESC_SIMPLE,     ///< bare C-escape subset (legacy)
    LUSH_ESC_ANSI_C,     ///< full $'...' / ${var@E} ANSI-C set (\cX = ctrl)
    LUSH_ESC_XSI,        ///< echo / print / printf %b: \0NNN octal, \c ends
    LUSH_ESC_PRINTF_FMT, ///< printf format: \NNN octal, \c ends
} lush_escape_dialect_t;

/**
 * @brief Expand backslash escapes in the first `len` bytes of `str`
 *
 * Returns a newly allocated, NUL-terminated string (caller frees). On
 * NULL input or allocation failure, returns an empty allocated string.
 *
 * @param str     Source bytes (need not be NUL-terminated within `len`)
 * @param len     Number of bytes of `str` to process
 * @param dialect Which escape set to honor
 * @return Newly allocated expanded string
 */
char *lush_expand_escapes(const char *str, size_t len,
                          lush_escape_dialect_t dialect);

/**
 * @brief Like lush_expand_escapes, reporting a `\c` output-terminator
 *
 * In the LUSH_ESC_XSI and LUSH_ESC_PRINTF_FMT dialects, a `\c` escape
 * stops interpretation and signals that no further output (including any
 * trailing newline the caller would add) should be produced. When
 * @p terminated is non-NULL it is set to true if such a `\c` was reached.
 * The returned string contains everything decoded up to the `\c`.
 *
 * The result may contain embedded NUL bytes (from `\0`, `\xHH`, `\uHHHH`),
 * so callers that must emit them faithfully should use @p out_len rather
 * than strlen. The returned buffer is still NUL-terminated for convenience.
 *
 * @param str        Source bytes
 * @param len        Number of bytes to process
 * @param dialect    Which escape set to honor
 * @param terminated Out: set to true if a `\c` cut the output short (may be
 *                   NULL). Always initialized to false first.
 * @param out_len    Out: number of decoded bytes, excluding the terminator
 *                   (may be NULL).
 * @return Newly allocated expanded string
 */
char *lush_expand_escapes_ex(const char *str, size_t len,
                             lush_escape_dialect_t dialect, bool *terminated,
                             size_t *out_len);

/**
 * @brief Decode the single escape sequence beginning at `s`
 *
 * `s[0]` must be a backslash. Writes the decoded byte(s) into @p out (the
 * caller must provide at least 8 bytes to hold a 4-byte UTF-8 sequence),
 * sets @p out_n to the number of bytes written, and sets @p terminated (if
 * non-NULL) when the escape is an XSI / PRINTF_FMT `\c` output-terminator
 * (in which case out_n is 0). Returns the number of INPUT bytes consumed
 * (always >= 1). An unrecognized escape writes a literal backslash
 * (out[0]='\\', out_n=1) and consumes 1, leaving the following byte for the
 * caller. Lets a character-at-a-time consumer (printf's format parser) share
 * the one decoder with lush_expand_escapes_ex.
 *
 * @param s          Source bytes, s[0] == '\\'
 * @param len        Bytes available from s
 * @param dialect    Which escape set to honor
 * @param out        Output buffer (>= 8 bytes)
 * @param out_n      Out: decoded byte count
 * @param terminated Out: set true for a `\c` terminator (may be NULL)
 * @return Input bytes consumed
 */
size_t lush_decode_one_escape(const char *s, size_t len,
                              lush_escape_dialect_t dialect, char *out,
                              size_t *out_n, bool *terminated);

/**
 * @brief Decode one simple C-escape letter (the char after `\`)
 *
 * @param c The escape letter (e.g. 'n')
 * @return The decoded control byte, or -1 if `c` is not a simple escape.
 */
int lush_escape_basic_char(char c);

#endif /* LUSH_ESCAPE_H */
