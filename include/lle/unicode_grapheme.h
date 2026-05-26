/**
 * @file unicode_grapheme.h
 * @brief Unicode TR#29 grapheme cluster boundary detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Complete implementation of Unicode Technical Report #29 (UAX #29)
 * grapheme cluster boundary algorithm.
 *
 * Reference: https://www.unicode.org/reports/tr29/
 */

#ifndef LLE_UNICODE_GRAPHEME_H
#define LLE_UNICODE_GRAPHEME_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Determine if a given UTF-8 position is a grapheme cluster boundary
 *
 * This function implements the complete Unicode TR#29 grapheme cluster
 * boundary algorithm, handling all GB1-GB999 rules including:
 * - CR/LF sequences
 * - Control characters
 * - Hangul syllable composition
 * - Combining marks and extenders
 * - Emoji modifier sequences
 * - ZWJ emoji sequences
 * - Regional indicator pairs (flags)
 * - Prepended marks
 * - Spacing marks
 *
 * @param ptr Pointer to current position in UTF-8 text
 * @param start Pointer to start of text (for context)
 * @param end Pointer to end of text (for bounds checking)
 * @return true if this position starts a new grapheme cluster, false otherwise
 */
bool lle_is_grapheme_boundary(const char *ptr, const char *start,
                              const char *end);

/**
 * @brief Count grapheme clusters in a UTF-8 string
 *
 * This function iterates through the text and counts the number of
 * grapheme clusters (user-perceived characters) using the complete
 * TR#29 boundary detection algorithm.
 *
 * @param text UTF-8 text to analyze
 * @param length Length of text in bytes
 * @return Number of grapheme clusters in the text
 */
size_t lle_utf8_count_graphemes(const char *text, size_t length);

#endif /// LLE_UNICODE_GRAPHEME_H
