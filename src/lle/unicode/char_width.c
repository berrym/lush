/**
 * @file char_width.c
 * @brief Character Width Calculation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements Unicode East Asian Width property for terminal display.
 *
 * Reference: Unicode Standard Annex #11 (East Asian Width)
 * https://www.unicode.org/reports/tr11/
 */

#include "lle/char_width.h"

/**
 * @brief Get the display width of a Unicode codepoint
 * @param cp The Unicode codepoint to measure
 * @return Display width: 0 (zero-width), 1 (normal), or 2 (wide/fullwidth)
 *
 * Implements East Asian Width property for terminal display.
 * Handles control characters, combining marks, CJK, emoji, and more.
 */
int lle_codepoint_width(uint32_t cp) {
    /// C0 control characters (0x00-0x1F)
    if (cp < 0x20) {
        return 0;
    }

    /// DEL (0x7F)
    if (cp == 0x7F) {
        return 0;
    }

    /// C1 control characters (0x80-0x9F)
    if (cp >= 0x80 && cp <= 0x9F) {
        return 0;
    }

    /// Combining marks
    if (cp >= 0x0300 && cp <= 0x036F)
        return 0; /// Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1AFF)
        return 0; /// Combining Diacritical Marks Extended
    if (cp >= 0x1DC0 && cp <= 0x1DFF)
        return 0; /// Combining Diacritical Marks Supplement
    if (cp >= 0x20D0 && cp <= 0x20FF)
        return 0; /// Combining Diacritical Marks for Symbols
    if (cp >= 0xFE20 && cp <= 0xFE2F)
        return 0; /// Combining Half Marks

    /// Zero-width characters
    if (cp == 0x200B)
        return 0; /// Zero Width Space
    if (cp == 0x200C)
        return 0; /// Zero Width Non-Joiner
    if (cp == 0x200D)
        return 0; /// Zero Width Joiner
    if (cp == 0xFEFF)
        return 0; /// Zero Width No-Break Space

    /// Hangul Jamo (medial/final are combining, initial is wide in some fonts)
    if (cp >= 0x1100 && cp <= 0x115F)
        return 2; /// Choseong (initial)
    if (cp >= 0x1160 && cp <= 0x11FF)
        return 0; /// Jungseong and Jongseong (combining)

    /// East Asian Wide (W) and Fullwidth (F) characters

    /// CJK Unified Ideographs
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return 2; /// Extension A
    if (cp >= 0x20000 && cp <= 0x2A6DF)
        return 2; /// Extension B
    if (cp >= 0x2A700 && cp <= 0x2B73F)
        return 2; /// Extension C
    if (cp >= 0x2B740 && cp <= 0x2B81F)
        return 2; /// Extension D
    if (cp >= 0x2B820 && cp <= 0x2CEAF)
        return 2; /// Extension E
    if (cp >= 0x2CEB0 && cp <= 0x2EBEF)
        return 2; /// Extension F
    if (cp >= 0x30000 && cp <= 0x3134F)
        return 2; /// Extension G

    /// Hangul Syllables
    if (cp >= 0xAC00 && cp <= 0xD7A3)
        return 2;

    /// Hiragana and Katakana
    if (cp >= 0x3040 && cp <= 0x309F)
        return 2; /// Hiragana
    if (cp >= 0x30A0 && cp <= 0x30FF)
        return 2; /// Katakana

    /// Katakana Phonetic Extensions
    if (cp >= 0x31F0 && cp <= 0x31FF)
        return 2;

    /// Halfwidth and Fullwidth Forms
    if (cp >= 0xFF00 && cp <= 0xFF60)
        return 2; /// Fullwidth
    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 2; /// Fullwidth

    /// Emoji and Pictographs (most are wide)
    if (cp >= 0x1F300 && cp <= 0x1F9FF)
        return 2; /// Emoji blocks
    if (cp >= 0x1FA00 && cp <= 0x1FAFF)
        return 2; /// Symbols and Pictographs Extended-A
    if (cp >= 0x2600 && cp <= 0x27BF)
        return 2; /// Miscellaneous Symbols
    if (cp >= 0x2300 && cp <= 0x23FF)
        return 2; /// Miscellaneous Technical
    if (cp >= 0x2B50 && cp <= 0x2B55)
        return 2; /// Stars

    /// Emoji Variation Selectors
    if (cp >= 0xFE00 && cp <= 0xFE0F)
        return 0; /// Variation Selectors (zero-width)

    /// NOTE: Skin tone modifiers (U+1F3FB..U+1F3FF) are not handled
    /// specially here — Unicode TR11 assigns them East Asian Width = W,
    /// so they are width 2 as standalone codepoints (and the broader
    /// emoji-block check above already returns 2 for them). Their
    /// "no extra columns when combined" behavior is a grapheme-cluster
    /// property, handled by the grapheme detector / unicode_grapheme
    /// modules, not by per-codepoint width.

    /// Regional Indicators (flags)
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF)
        return 2;

    /// Box Drawing
    if (cp >= 0x2500 && cp <= 0x257F)
        return 1; /// Usually single-width in terminals

    /// Block Elements
    if (cp >= 0x2580 && cp <= 0x259F)
        return 1;

    /// Geometric Shapes
    if (cp >= 0x25A0 && cp <= 0x25FF)
        return 1;

    /// Default: Normal width (1 column)
    return 1;
}

/**
 * @brief Check if a codepoint is a wide (double-width) character
 * @param codepoint The Unicode codepoint to check
 * @return true if the character occupies 2 terminal columns, false otherwise
 */
bool lle_is_wide_character(uint32_t codepoint) {
    return lle_codepoint_width(codepoint) == 2;
}
