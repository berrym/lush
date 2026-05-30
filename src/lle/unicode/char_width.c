/**
 * @file char_width.c
 * @brief Character Width Calculation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements Unicode East Asian Width property (UAX #11) for terminal
 * display. Includes a configurable policy knob for the TR11 'Ambiguous'
 * class -- codepoints whose width depends on the host terminal /
 * font (e.g. U+2300-U+23FF Miscellaneous Technical, U+2600-U+27BF
 * Miscellaneous Symbols). The shell sets the policy via
 * `lle_codepoint_width_set_ambiguous_policy(int width)`; the default
 * is 1 (narrow), matching traditional wcwidth behavior.
 *
 * Reference: Unicode Standard Annex #11 (East Asian Width)
 * https://www.unicode.org/reports/tr11/
 */

#include "lle/char_width.h"

/// Ambiguous-width policy. 1 (narrow) is the traditional wcwidth
/// default; 2 (wide) is what some Asian-language terminals use.
/// Touched only by lle_codepoint_width_set_ambiguous_policy().
static int g_ambiguous_width = 1;

void lle_codepoint_width_set_ambiguous_policy(int width) {
    g_ambiguous_width = (width == 2) ? 2 : 1;
}

bool lle_codepoint_is_east_asian_ambiguous(uint32_t cp) {
    /// Latin-1 Supplement ambiguous range.
    if (cp == 0x00A1 || cp == 0x00A4 || cp == 0x00A7 || cp == 0x00A8 ||
        cp == 0x00AA || cp == 0x00AD || cp == 0x00AE ||
        (cp >= 0x00B0 && cp <= 0x00B4) || (cp >= 0x00B6 && cp <= 0x00BA) ||
        (cp >= 0x00BC && cp <= 0x00BF)) {
        return true;
    }
    if (cp == 0x00C6 || cp == 0x00D0 || cp == 0x00D7 || cp == 0x00D8 ||
        (cp >= 0x00DE && cp <= 0x00E1) || cp == 0x00E6 ||
        (cp >= 0x00E8 && cp <= 0x00EA) || cp == 0x00EC || cp == 0x00ED ||
        cp == 0x00F0 || cp == 0x00F2 || cp == 0x00F3 ||
        (cp >= 0x00F7 && cp <= 0x00FA) || cp == 0x00FC || cp == 0x00FE) {
        return true;
    }

    /// Greek uppercase letters (alpha-omega, no final-sigma equivalent).
    if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) {
        return true;
    }
    /// Greek lowercase letters.
    if (cp >= 0x03B1 && cp <= 0x03C9 && cp != 0x03C2) {
        return true;
    }

    /// Cyrillic core.
    if (cp == 0x0401 || (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0451) {
        return true;
    }

    /// General Punctuation that varies by terminal.
    if (cp == 0x2010 || (cp >= 0x2013 && cp <= 0x2016) || cp == 0x2018 ||
        cp == 0x2019 || cp == 0x201C || cp == 0x201D ||
        (cp >= 0x2020 && cp <= 0x2022) || (cp >= 0x2024 && cp <= 0x2027) ||
        cp == 0x2030 || cp == 0x2032 || cp == 0x2033 || cp == 0x2035 ||
        cp == 0x203B || cp == 0x203E) {
        return true;
    }

    /// Euro sign.
    if (cp == 0x20AC) {
        return true;
    }

    /// Letterlike Symbols and Number Forms that are commonly ambiguous.
    if (cp == 0x2103 || cp == 0x2105 || cp == 0x2109 || cp == 0x2113 ||
        cp == 0x2116 || cp == 0x2121 || cp == 0x2122 || cp == 0x2126 ||
        cp == 0x212B || cp == 0x2153 || cp == 0x2154 ||
        (cp >= 0x215B && cp <= 0x215E) || (cp >= 0x2160 && cp <= 0x216B) ||
        (cp >= 0x2170 && cp <= 0x2179) || cp == 0x2189) {
        return true;
    }

    /// Arrows.
    if (cp >= 0x2190 && cp <= 0x2199) {
        return true;
    }
    if (cp == 0x21B8 || cp == 0x21B9 || cp == 0x21D2 || cp == 0x21D4 ||
        cp == 0x21E7) {
        return true;
    }

    /// Mathematical Operators (large subset is 'A').
    if (cp == 0x2200 || cp == 0x2202 || cp == 0x2203 || cp == 0x2207 ||
        cp == 0x2208 || cp == 0x220B || cp == 0x220F || cp == 0x2211 ||
        cp == 0x2215 || cp == 0x221A || (cp >= 0x221D && cp <= 0x2220) ||
        cp == 0x2223 || cp == 0x2225 || (cp >= 0x2227 && cp <= 0x222C) ||
        cp == 0x222E || (cp >= 0x2234 && cp <= 0x2237) || cp == 0x223C ||
        cp == 0x223D || cp == 0x2248 || cp == 0x224C || cp == 0x2252 ||
        cp == 0x2260 || cp == 0x2261 || (cp >= 0x2264 && cp <= 0x2267) ||
        cp == 0x226A || cp == 0x226B || cp == 0x226E || cp == 0x226F ||
        cp == 0x2282 || cp == 0x2283 || cp == 0x2286 || cp == 0x2287 ||
        cp == 0x2295 || cp == 0x2299 || cp == 0x22A5 || cp == 0x22BF ||
        cp == 0x2312) {
        return true;
    }

    /// Miscellaneous Technical (U+2300-U+23FF). Per TR11 this block is
    /// MIXED (mostly 'N' Neutral but with 'A' codepoints); rendering
    /// in common terminal+font pairs is inconsistent enough that lush
    /// treats the whole block as ambiguous for policy purposes.
    if (cp >= 0x2300 && cp <= 0x23FF) {
        return true;
    }

    /// Enclosed Alphanumerics, Box Drawing, Block Elements, Geometric
    /// Shapes -- mostly 'A' per TR11.
    if (cp >= 0x2460 && cp <= 0x24E9) {
        return true;
    }
    if (cp >= 0x24EB && cp <= 0x254B) {
        return true;
    }
    if (cp >= 0x2550 && cp <= 0x2573) {
        return true;
    }
    if (cp >= 0x2580 && cp <= 0x258F) {
        return true;
    }
    if (cp >= 0x2592 && cp <= 0x2595) {
        return true;
    }
    if (cp == 0x25A0 || cp == 0x25A1 || (cp >= 0x25A3 && cp <= 0x25A9) ||
        cp == 0x25B2 || cp == 0x25B3 || cp == 0x25B6 || cp == 0x25B7 ||
        cp == 0x25BC || cp == 0x25BD || cp == 0x25C0 || cp == 0x25C1 ||
        (cp >= 0x25C6 && cp <= 0x25C8) || cp == 0x25CB ||
        (cp >= 0x25CE && cp <= 0x25D1) || (cp >= 0x25E2 && cp <= 0x25E5) ||
        cp == 0x25EF) {
        return true;
    }

    /// Miscellaneous Symbols (U+2600-U+27BF). Like Misc Technical, this
    /// block is MIXED in TR11; lush treats it as ambiguous as a whole
    /// so the same policy knob covers it.
    if (cp >= 0x2600 && cp <= 0x27BF) {
        return true;
    }

    /// Private Use Area on the BMP.
    if (cp >= 0xE000 && cp <= 0xF8FF) {
        return true;
    }

    /// Replacement character.
    if (cp == 0xFFFD) {
        return true;
    }

    /// Plane 15 / Plane 16 supplementary private use areas.
    if ((cp >= 0xF0000 && cp <= 0xFFFFD) ||
        (cp >= 0x100000 && cp <= 0x10FFFD)) {
        return true;
    }

    return false;
}

/**
 * @brief Get the display width of a Unicode codepoint.
 * @param cp The Unicode codepoint to measure.
 * @return Display width: 0 (zero-width), 1 (normal), or 2 (wide/fullwidth).
 *
 * Implements East Asian Width property for terminal display. For
 * codepoints in the TR11 'Ambiguous' class, the returned width follows
 * the global policy set via lle_codepoint_width_set_ambiguous_policy()
 * (default 1 = narrow).
 */
int lle_codepoint_width(uint32_t cp) {
    /// C0 control characters (0x00-0x1F).
    if (cp < 0x20) {
        return 0;
    }

    /// DEL (0x7F).
    if (cp == 0x7F) {
        return 0;
    }

    /// C1 control characters (0x80-0x9F).
    if (cp >= 0x80 && cp <= 0x9F) {
        return 0;
    }

    /// Combining marks.
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

    /// Zero-width characters.
    if (cp == 0x200B)
        return 0; /// Zero Width Space
    if (cp == 0x200C)
        return 0; /// Zero Width Non-Joiner
    if (cp == 0x200D)
        return 0; /// Zero Width Joiner
    if (cp >= 0x200E && cp <= 0x200F)
        return 0; /// LRM, RLM
    if (cp == 0xFEFF)
        return 0; /// Zero Width No-Break Space

    /// Variation Selectors (zero-width).
    if (cp >= 0xFE00 && cp <= 0xFE0F)
        return 0;

    /// Hangul Jamo: Choseong (initial) is wide; Jungseong / Jongseong
    /// combine onto the preceding syllable as zero-width.
    if (cp >= 0x1100 && cp <= 0x115F)
        return 2;
    if (cp >= 0x1160 && cp <= 0x11FF)
        return 0;

    /// East Asian Wide (W) and Fullwidth (F) characters.

    /// CJK Symbols and Punctuation including the Ideographic Space.
    /// U+3000 is 'F' per TR11; U+3001-U+303E is 'W'.
    if (cp >= 0x3000 && cp <= 0x303E)
        return 2;

    /// CJK Unified Ideographs.
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

    /// CJK Compatibility Ideographs.
    if (cp >= 0xF900 && cp <= 0xFAFF)
        return 2;

    /// Hangul Syllables and Hangul Compatibility Jamo.
    if (cp >= 0xAC00 && cp <= 0xD7A3)
        return 2;
    if (cp >= 0x3131 && cp <= 0x318E)
        return 2;

    /// Hiragana, Katakana, Katakana Phonetic Extensions, Bopomofo.
    if (cp >= 0x3041 && cp <= 0x3096)
        return 2; /// Hiragana
    if (cp >= 0x3099 && cp <= 0x30FF)
        return 2; /// covers Katakana
    if (cp >= 0x31F0 && cp <= 0x31FF)
        return 2; /// Katakana Phonetic Extensions
    if (cp >= 0x3105 && cp <= 0x312F)
        return 2; /// Bopomofo

    /// CJK radicals supplement, Kangxi radicals, Ideographic description.
    if ((cp >= 0x2E80 && cp <= 0x2E99) || (cp >= 0x2E9B && cp <= 0x2EF3)) {
        return 2;
    }
    if (cp >= 0x2F00 && cp <= 0x2FD5)
        return 2;
    if (cp >= 0x2FF0 && cp <= 0x2FFB)
        return 2;

    /// CJK strokes, Enclosed CJK, vertical forms, compatibility forms.
    if (cp >= 0x3190 && cp <= 0x31E3)
        return 2;
    if (cp >= 0x3220 && cp <= 0x3247)
        return 2;
    if (cp >= 0xFE10 && cp <= 0xFE19)
        return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F)
        return 2;

    /// Left/right-pointing angle brackets (TR11 'W').
    if (cp == 0x2329 || cp == 0x232A)
        return 2;

    /// Halfwidth and Fullwidth Forms.
    if (cp >= 0xFF01 && cp <= 0xFF60)
        return 2; /// Fullwidth
    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 2; /// Fullwidth signs

    /// Yi syllables / radicals.
    if (cp >= 0xA000 && cp <= 0xA4CF)
        return 2;

    /// Stars and a handful of other dedicated 'W' codepoints in the
    /// Miscellaneous Symbols and Arrows block (U+2B00-U+2BFF). Most
    /// of the block is 'A' (handled by the Ambiguous classifier);
    /// these are unconditionally wide per TR11.
    if (cp >= 0x2B50 && cp <= 0x2B55)
        return 2;

    /// Emoji and Pictographs (unambiguously wide per TR11).
    if (cp >= 0x1F300 && cp <= 0x1F64F)
        return 2;
    if (cp >= 0x1F680 && cp <= 0x1F6FF)
        return 2;
    if (cp >= 0x1F900 && cp <= 0x1F9FF)
        return 2;
    if (cp >= 0x1FA00 && cp <= 0x1FAFF)
        return 2;

    /// Regional Indicators (flags).
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF)
        return 2;

    /// NOTE: Skin tone modifiers (U+1F3FB..U+1F3FF) are not handled
    /// specially here -- Unicode TR11 assigns them East Asian Width = W
    /// (the emoji block check above covers them as width 2). Their
    /// "no extra columns when combined" behavior is a grapheme-cluster
    /// property, handled by the grapheme detector / unicode_grapheme
    /// modules, not by per-codepoint width.

    /// East Asian Ambiguous class -- width depends on policy.
    if (lle_codepoint_is_east_asian_ambiguous(cp)) {
        return g_ambiguous_width;
    }

    /// Default: Normal width (1 column).
    return 1;
}

bool lle_is_wide_character(uint32_t codepoint) {
    return lle_codepoint_width(codepoint) == 2;
}
