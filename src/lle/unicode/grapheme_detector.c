/**
 * @file grapheme_detector.c
 * @brief Grapheme Cluster Boundary Detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements UAX #29 grapheme break property detection for
 * determining grapheme cluster boundaries in UTF-8 text.
 */

#include "lle/grapheme_detector.h"
#include "lle/utf8_support.h"

/**
 * @brief Get grapheme break property for a codepoint
 * @param cp The Unicode codepoint to classify
 * @return The grapheme break property value
 *
 * Implements UAX #29 grapheme break property detection.
 */
grapheme_break_property_t get_grapheme_break_property(uint32_t cp) {
    /// CR/LF
    if (cp == 0x000D)
        return GB_CR;
    if (cp == 0x000A)
        return GB_LF;

    /// Control characters (C0 and C1)
    if (cp < 0x20)
        return GB_CONTROL;
    if (cp >= 0x7F && cp <= 0x9F)
        return GB_CONTROL;

    /// Zero Width Joiner
    if (cp == 0x200D)
        return GB_ZWJ;

    /// Combining marks (Extend)
    if (cp >= 0x0300 && cp <= 0x036F)
        return GB_EXTEND; /// Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1AFF)
        return GB_EXTEND; /// Combining Diacritical Marks Extended
    if (cp >= 0x1DC0 && cp <= 0x1DFF)
        return GB_EXTEND; /// Combining Diacritical Marks Supplement
    if (cp >= 0x20D0 && cp <= 0x20FF)
        return GB_EXTEND; /// Combining Diacritical Marks for Symbols
    if (cp >= 0xFE20 && cp <= 0xFE2F)
        return GB_EXTEND; /// Combining Half Marks

    /// Regional Indicators (flags)
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF)
        return GB_REGIONAL_INDICATOR;

    /// Hangul Jamo
    if (cp >= 0x1100 && cp <= 0x115F)
        return GB_L; /// Hangul Choseong
    if (cp >= 0x1160 && cp <= 0x11A7)
        return GB_V; /// Hangul Jungseong
    if (cp >= 0x11A8 && cp <= 0x11FF)
        return GB_T; /// Hangul Jongseong

    /// Hangul Syllables
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        int s_index = cp - 0xAC00;
        int t_index = s_index % 28;
        if (t_index == 0)
            return GB_LV;
        else
            return GB_LVT;
    }

    /// Extended Pictographic (Emoji)
    if (cp >= 0x1F300 && cp <= 0x1F9FF)
        return GB_EXTENDED_PICTOGRAPHIC; /// Emoji blocks
    if (cp >= 0x2600 && cp <= 0x27BF)
        return GB_EXTENDED_PICTOGRAPHIC; /// Miscellaneous Symbols

    /// SpacingMark
    if (cp >= 0x0903 && cp <= 0x0903)
        return GB_SPACING_MARK; /// Devanagari

    return GB_OTHER;
}

/**
 * @brief Check if there's a boundary between two codepoints per UAX #29
 * @param cp1 Previous codepoint
 * @param cp2 Current codepoint
 * @param prev_was_zwj Whether the character before cp1 was ZWJ
 * @param ri_sequence_count Number of preceding Regional Indicator codepoints
 * @return true if there is a boundary, false if no break
 */
bool is_grapheme_cluster_boundary(uint32_t cp1, uint32_t cp2, bool prev_was_zwj,
                                  int ri_sequence_count) {
    grapheme_break_property_t prop1 = get_grapheme_break_property(cp1);
    grapheme_break_property_t prop2 = get_grapheme_break_property(cp2);

    /// GB3: CR × LF
    if (prop1 == GB_CR && prop2 == GB_LF) {
        return false; /// No break
    }

    /// GB4: (Control | CR | LF) ÷
    if (prop1 == GB_CONTROL || prop1 == GB_CR || prop1 == GB_LF) {
        return true; /// Break
    }

    /// GB5: ÷ (Control | CR | LF)
    if (prop2 == GB_CONTROL || prop2 == GB_CR || prop2 == GB_LF) {
        return true; /// Break
    }

    /// GB6: L × (L | V | LV | LVT)
    if (prop1 == GB_L &&
        (prop2 == GB_L || prop2 == GB_V || prop2 == GB_LV || prop2 == GB_LVT)) {
        return false; /// No break
    }

    /// GB7: (LV | V) × (V | T)
    if ((prop1 == GB_LV || prop1 == GB_V) && (prop2 == GB_V || prop2 == GB_T)) {
        return false; /// No break
    }

    /// GB8: (LVT | T) × T
    if ((prop1 == GB_LVT || prop1 == GB_T) && prop2 == GB_T) {
        return false; /// No break
    }

    /// GB9: × (Extend | ZWJ)
    if (prop2 == GB_EXTEND || prop2 == GB_ZWJ) {
        return false; /// No break
    }

    /// GB9a: × SpacingMark
    if (prop2 == GB_SPACING_MARK) {
        return false; /// No break
    }

    /// GB9b: Prepend ×
    if (prop1 == GB_PREPEND) {
        return false; /// No break
    }

    /// GB11: \p{Extended_Pictographic} Extend* ZWJ × \p{Extended_Pictographic}
    if (prev_was_zwj && prop1 == GB_EXTENDED_PICTOGRAPHIC &&
        prop2 == GB_EXTENDED_PICTOGRAPHIC) {
        return false; /// No break (emoji sequence)
    }

    /// GB12/GB13: Regional Indicator × Regional Indicator (pairs)
    if (prop1 == GB_REGIONAL_INDICATOR && prop2 == GB_REGIONAL_INDICATOR) {
        /// Regional indicators pair up (for flags)
        /// Odd position = no break, even position = break
        return (ri_sequence_count % 2) == 0;
    }

    /// GB999: Any ÷ Any
    return true; /// Default: break
}

/**
 * @brief Check if position is at a grapheme cluster boundary
 * @param pos Current position in UTF-8 text
 * @param text_start Start of the text buffer
 * @param text_end End of the text buffer
 * @return true if this position is a grapheme boundary, false otherwise
 *
 * Analyzes the context around the position to determine if it marks
 * the start of a new grapheme cluster.
 */
bool is_grapheme_boundary_at_position(const char *pos, const char *text_start,
                                      const char *text_end) {
    /// Start of text is always a boundary
    if (pos == text_start) {
        return true;
    }

    /// End of text is always a boundary
    if (pos >= text_end) {
        return true;
    }

    /// Decode current codepoint
    uint32_t cp_current;
    int len_current = lle_utf8_sequence_length(*pos);
    if (len_current == 0 || pos + len_current > text_end) {
        return true; /// Invalid UTF-8, treat as boundary
    }
    lle_utf8_decode_codepoint(pos, len_current, &cp_current);

    /// Find previous codepoint
    const char *prev_pos = pos - 1;

    /// Scan backward to find start of previous UTF-8 sequence
    while (prev_pos >= text_start && ((*prev_pos & 0xC0) == 0x80)) {
        prev_pos--; /// Skip continuation bytes
    }

    if (prev_pos < text_start) {
        return true; /// Start of text
    }

    /// Decode previous codepoint
    uint32_t cp_prev;
    int len_prev = lle_utf8_sequence_length(*prev_pos);
    if (len_prev == 0) {
        return true; /// Invalid UTF-8
    }
    lle_utf8_decode_codepoint(prev_pos, len_prev, &cp_prev);

    /// Check if previous was ZWJ (for emoji sequences)
    bool prev_was_zwj = false;
    if (prev_pos > text_start) {
        const char *before_prev = prev_pos - 1;
        while (before_prev >= text_start && ((*before_prev & 0xC0) == 0x80)) {
            before_prev--;
        }
        if (before_prev >= text_start) {
            uint32_t cp_before;
            int len_before = lle_utf8_sequence_length(*before_prev);
            lle_utf8_decode_codepoint(before_prev, len_before, &cp_before);
            prev_was_zwj = (get_grapheme_break_property(cp_before) == GB_ZWJ);
        }
    }

    /// Count regional indicator sequence length
    int ri_count = 0;
    if (get_grapheme_break_property(cp_prev) == GB_REGIONAL_INDICATOR) {
        const char *check_pos = prev_pos;
        while (check_pos >= text_start) {
            const char *cp_start = check_pos - 1;
            while (cp_start >= text_start && ((*cp_start & 0xC0) == 0x80)) {
                cp_start--;
            }
            if (cp_start < text_start)
                break;

            uint32_t cp;
            int len = lle_utf8_sequence_length(*cp_start);
            lle_utf8_decode_codepoint(cp_start, len, &cp);

            if (get_grapheme_break_property(cp) != GB_REGIONAL_INDICATOR) {
                break;
            }

            ri_count++;
            check_pos = cp_start;
        }
    }

    /// Apply boundary rules
    return is_grapheme_cluster_boundary(cp_prev, cp_current, prev_was_zwj,
                                        ri_count);
}
