/**
 * @file grapheme_detector.h
 * @brief Grapheme cluster boundary detection implementing Unicode UAX #29
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements Unicode UAX #29 Grapheme Cluster Boundary rules.
 * Reference: https://unicode.org/reports/tr29/
 */

#ifndef LLE_GRAPHEME_DETECTOR_H
#define LLE_GRAPHEME_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Grapheme cluster break property types (from UAX #29)
 */
typedef enum {
    GB_OTHER = 0,            ///< Any character not listed below
    GB_CR,                   ///< Carriage Return
    GB_LF,                   ///< Line Feed
    GB_CONTROL,              ///< Control characters
    GB_EXTEND,               ///< Extend (combining marks, etc.)
    GB_ZWJ,                  ///< Zero Width Joiner
    GB_REGIONAL_INDICATOR,   ///< Regional Indicator
    GB_PREPEND,              ///< Prepend
    GB_SPACING_MARK,         ///< SpacingMark
    GB_L,                    ///< Hangul L
    GB_V,                    ///< Hangul V
    GB_T,                    ///< Hangul T
    GB_LV,                   ///< Hangul LV
    GB_LVT,                  ///< Hangul LVT
    GB_EXTENDED_PICTOGRAPHIC ///< Emoji and pictographs
} grapheme_break_property_t;

/**
 * @brief Get grapheme break property for a Unicode codepoint
 * @param codepoint Unicode codepoint
 * @return Grapheme break property
 */
grapheme_break_property_t get_grapheme_break_property(uint32_t codepoint);

/**
 * @brief Check if there is a grapheme cluster boundary between two codepoints
 *
 * Implements UAX #29 boundary rules.
 *
 * @param cp1 First codepoint
 * @param cp2 Second codepoint
 * @param prev_was_zwj True if previous codepoint was ZWJ
 * @param ri_sequence_count Count of previous Regional Indicators in sequence
 * @return true if boundary exists, false otherwise
 */
bool is_grapheme_cluster_boundary(uint32_t cp1, uint32_t cp2, bool prev_was_zwj,
                                  int ri_sequence_count);

/**
 * @brief Check if position in text is at a grapheme cluster boundary
 *
 * This is the main function used by utf8_index.c
 *
 * @param pos Current position in text
 * @param text_start Start of text buffer
 * @param text_end End of text buffer
 * @return true if pos is at a grapheme boundary
 */
bool is_grapheme_boundary_at_position(const char *pos, const char *text_start,
                                      const char *text_end);

#endif /// LLE_GRAPHEME_DETECTOR_H
