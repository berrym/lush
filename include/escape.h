/**
 * @file escape.h
 * @brief Canonical backslash-escape expansion for the shell
 *
 * One place for every "turn `\n` into a newline" transformation. Two
 * dialects share a single engine:
 *
 *   LUSH_ESC_SIMPLE  - the C-escape subset used by `echo -e`, `print`,
 *                      and the `printf` format string: \a \b \f \n \r
 *                      \t \v \\ \" \'. An unrecognized `\x` is left as
 *                      the two literal characters.
 *
 *   LUSH_ESC_ANSI_C  - the full ANSI-C ($'...') set, also used by the
 *                      ${var@E} parameter transform: the simple set plus
 *                      \e/\E, \?, \xHH, \uHHHH, \UHHHHHHHH, \cX control
 *                      characters, and \NNN octal.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_ESCAPE_H
#define LUSH_ESCAPE_H

#include <stddef.h>

typedef enum {
    LUSH_ESC_SIMPLE, ///< echo / print / printf C-escape subset
    LUSH_ESC_ANSI_C, ///< full $'...' / ${var@E} ANSI-C set
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
 * @brief Decode one simple C-escape letter (the char after `\`)
 *
 * @param c The escape letter (e.g. 'n')
 * @return The decoded control byte, or -1 if `c` is not a simple escape.
 */
int lush_escape_basic_char(char c);

#endif /* LUSH_ESCAPE_H */
