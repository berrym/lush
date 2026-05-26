/**
 * @file unicode_class.h
 * @brief Unicode general-category classification for LLE
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Unicode-aware predicates for POSIX-style character classes used by
 * shell features such as `[[:alpha:]]` / `[[:digit:]]` / `[[:upper:]]`
 * in case-pattern and `[[ ]]`-test pattern matching. Replaces the
 * single-byte `ctype.h` predicates that ignore non-ASCII codepoints.
 *
 * Coverage matches lush's existing Unicode scope (see unicode_case.h):
 * Latin-1 Supplement (U+0080-U+00FF), Latin Extended-A (U+0100-U+017F),
 * Latin Extended-B (U+0180-U+024F), Greek (U+0370-U+03FF), and the
 * common Unicode whitespace / control codepoints. ASCII (cp < 0x80)
 * uses the standard C predicates for performance and absolute
 * correctness on the universal subset.
 *
 * The letter predicates leverage the case-mapping tables in
 * unicode_case.c via lle_unicode_is_upper / lle_unicode_is_lower:
 * any codepoint with a case mapping is, by definition, a letter.
 * This avoids duplicating Unicode data across modules.
 *
 * Digit and decimal-digit predicates include the common Unicode
 * digit ranges (Arabic-Indic U+0660-U+0669, Extended Arabic-Indic
 * U+06F0-U+06F9, Devanagari U+0966-U+096F, etc.) so the digit class
 * matches what real users write in non-Latin scripts.
 *
 * Predicates that POSIX defines as ASCII-only (`[[:xdigit:]]`,
 * `[[:blank:]]` -- space and tab) keep their ASCII semantics
 * regardless of locale, matching bash and zsh.
 */

#ifndef LLE_UNICODE_CLASS_H
#define LLE_UNICODE_CLASS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief POSIX `[[:alpha:]]` -- Unicode letter (categories Lu/Ll within scope)
 * @param cp Codepoint to test
 * @return true if cp is a letter
 */
bool lle_unicode_is_alpha(uint32_t cp);

/**
 * @brief POSIX `[[:digit:]]` -- Unicode decimal digit (category Nd within
 * scope)
 *
 * ASCII '0'-'9' plus the common non-Latin decimal-digit blocks
 * (Arabic-Indic, Devanagari, etc.). Matches Unicode general
 * category Nd within the module's coverage scope.
 *
 * @param cp Codepoint to test
 * @return true if cp is a decimal digit
 */
bool lle_unicode_is_digit(uint32_t cp);

/**
 * @brief POSIX `[[:alnum:]]` -- letter or decimal digit
 * @param cp Codepoint to test
 * @return true if cp is alpha or digit
 */
bool lle_unicode_is_alnum(uint32_t cp);

/**
 * @brief POSIX `[[:space:]]` -- Unicode whitespace
 *
 * ASCII whitespace (space, tab, newline, vertical tab, form feed,
 * carriage return) plus the common Unicode whitespace codepoints
 * (NBSP, line/paragraph separators, zero-width space, ideographic
 * space, etc.).
 *
 * @param cp Codepoint to test
 * @return true if cp is whitespace
 */
bool lle_unicode_is_space(uint32_t cp);

/**
 * @brief POSIX `[[:blank:]]` -- space or tab (ASCII-defined by POSIX)
 *
 * Strictly ASCII space (0x20) and tab (0x09). POSIX defines `blank`
 * as exactly these two characters regardless of locale, and bash /
 * zsh match that; lush follows.
 *
 * @param cp Codepoint to test
 * @return true if cp is space or tab
 */
bool lle_unicode_is_blank(uint32_t cp);

/**
 * @brief POSIX `[[:cntrl:]]` -- control character
 *
 * ASCII 0x00-0x1F and 0x7F (C0 controls + DEL), plus the C1 control
 * range U+0080-U+009F.
 *
 * @param cp Codepoint to test
 * @return true if cp is a control character
 */
bool lle_unicode_is_cntrl(uint32_t cp);

/**
 * @brief POSIX `[[:print:]]` -- printable (non-control)
 *
 * Approximated as the complement of is_cntrl, plus a positive
 * filter to exclude codepoints that are by definition zero-width
 * or formatting-only.
 *
 * @param cp Codepoint to test
 * @return true if cp is printable
 */
bool lle_unicode_is_print(uint32_t cp);

/**
 * @brief POSIX `[[:graph:]]` -- printable but not whitespace
 * @param cp Codepoint to test
 * @return true if cp is graphical
 */
bool lle_unicode_is_graph(uint32_t cp);

/**
 * @brief POSIX `[[:punct:]]` -- punctuation
 *
 * Currently covers ASCII punctuation plus the common Latin-1
 * Supplement and General Punctuation block (U+2000-U+206F)
 * codepoints. Broader category-P coverage would require explicit
 * range tables for each P subcategory (Pc, Pd, Pe, Pf, Pi, Po, Ps).
 *
 * @param cp Codepoint to test
 * @return true if cp is punctuation
 */
bool lle_unicode_is_punct(uint32_t cp);

/**
 * @brief POSIX `[[:xdigit:]]` -- hexadecimal digit (ASCII-defined)
 *
 * Strictly ASCII 0-9, a-f, A-F. POSIX defines `xdigit` over the
 * ASCII hex alphabet; lush follows.
 *
 * @param cp Codepoint to test
 * @return true if cp is an ASCII hex digit
 */
bool lle_unicode_is_xdigit(uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif /// LLE_UNICODE_CLASS_H
