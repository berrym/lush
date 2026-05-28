/**
 * @file test_char_width.c
 * @brief Unit tests for LLE Unicode character width
 *
 * Exercises lle_codepoint_width() and lle_is_wide_character() across the
 * Unicode ranges the implementation distinguishes: control characters,
 * combining marks, zero-width, Hangul Jamo, CJK ideographs, Hangul
 * syllables, kana, fullwidth forms, emoji, variation selectors, regional
 * indicators, and box/block/geometric drawing.
 *
 * Each TEST function focuses on one categorical range and exercises both
 * interior values and the immediate boundary codepoints (the values just
 * inside and just outside the range), since boundary errors are the
 * dominant bug class for table-driven width logic.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/char_width.h"
#include "test_framework.h"

/* ============================================================================
 * Control characters: width 0
 * ============================================================================
 */

TEST(c0_control_characters_zero_width) {
    ASSERT_EQ(lle_codepoint_width(0x00), 0, "NUL"); /// C0: 0x00-0x1F
    ASSERT_EQ(lle_codepoint_width(0x07), 0, "BEL");
    ASSERT_EQ(lle_codepoint_width(0x09), 0, "TAB");
    ASSERT_EQ(lle_codepoint_width(0x0A), 0, "LF");
    ASSERT_EQ(lle_codepoint_width(0x1B), 0, "ESC");
    ASSERT_EQ(lle_codepoint_width(0x1F), 0, "US (last C0)");
}

TEST(del_is_zero_width) { ASSERT_EQ(lle_codepoint_width(0x7F), 0, "DEL"); }

TEST(c1_control_characters_zero_width) {
    ASSERT_EQ(lle_codepoint_width(0x80), 0, "first C1");
    ASSERT_EQ(lle_codepoint_width(0x90), 0, "DCS");
    ASSERT_EQ(lle_codepoint_width(0x9B), 0, "CSI");
    ASSERT_EQ(lle_codepoint_width(0x9F), 0, "last C1");
}

TEST(boundary_just_after_c0_is_one) {
    ASSERT_EQ(lle_codepoint_width(0x20), 1, "space (just past C0)");
}

TEST(boundary_just_after_c1_is_one) {
    ASSERT_EQ(lle_codepoint_width(0xA0), 1, "NBSP (just past C1)");
}

/* ============================================================================
 * ASCII printable: width 1
 * ============================================================================
 */

TEST(ascii_printable_width_one) {
    ASSERT_EQ(lle_codepoint_width('A'), 1, "uppercase letter");
    ASSERT_EQ(lle_codepoint_width('z'), 1, "lowercase letter");
    ASSERT_EQ(lle_codepoint_width('0'), 1, "digit");
    ASSERT_EQ(lle_codepoint_width('!'), 1, "punctuation");
    ASSERT_EQ(lle_codepoint_width('~'), 1, "tilde (last printable ASCII)");
    ASSERT_EQ(lle_codepoint_width(' '), 1, "space");
}

/* ============================================================================
 * Combining marks: width 0
 * ============================================================================
 */

TEST(combining_diacritical_marks_zero_width) {
    /// U+0300..U+036F
    ASSERT_EQ(lle_codepoint_width(0x0300), 0, "first combining mark");
    ASSERT_EQ(lle_codepoint_width(0x0301), 0, "combining acute");
    ASSERT_EQ(lle_codepoint_width(0x036F), 0, "last in block");
}

TEST(combining_diacritical_marks_extended_zero_width) {
    /// U+1AB0..U+1AFF
    ASSERT_EQ(lle_codepoint_width(0x1AB0), 0, "first ext combining");
    ASSERT_EQ(lle_codepoint_width(0x1AFF), 0, "last ext combining");
}

TEST(combining_diacritical_marks_supplement_zero_width) {
    /// U+1DC0..U+1DFF
    ASSERT_EQ(lle_codepoint_width(0x1DC0), 0, "first supp combining");
    ASSERT_EQ(lle_codepoint_width(0x1DFF), 0, "last supp combining");
}

TEST(combining_diacritical_marks_for_symbols_zero_width) {
    /// U+20D0..U+20FF
    ASSERT_EQ(lle_codepoint_width(0x20D0), 0, "first symbol-comb");
    ASSERT_EQ(lle_codepoint_width(0x20FF), 0, "last symbol-comb");
}

TEST(combining_half_marks_zero_width) {
    /// U+FE20..U+FE2F
    ASSERT_EQ(lle_codepoint_width(0xFE20), 0, "first combining half");
    ASSERT_EQ(lle_codepoint_width(0xFE2F), 0, "last combining half");
}

/* ============================================================================
 * Explicit zero-width characters
 * ============================================================================
 */

TEST(explicit_zero_width_characters) {
    ASSERT_EQ(lle_codepoint_width(0x200B), 0, "ZERO WIDTH SPACE");
    ASSERT_EQ(lle_codepoint_width(0x200C), 0, "ZERO WIDTH NON-JOINER");
    ASSERT_EQ(lle_codepoint_width(0x200D), 0, "ZERO WIDTH JOINER");
    ASSERT_EQ(lle_codepoint_width(0xFEFF), 0, "ZERO WIDTH NO-BREAK SPACE");
}

/* ============================================================================
 * Hangul Jamo: choseong is wide, jungseong/jongseong are combining
 * ============================================================================
 */

TEST(hangul_jamo_choseong_wide) {
    /// U+1100..U+115F : initial consonants render as wide
    ASSERT_EQ(lle_codepoint_width(0x1100), 2, "first choseong");
    ASSERT_EQ(lle_codepoint_width(0x1112), 2, "middle of choseong block");
    ASSERT_EQ(lle_codepoint_width(0x115F), 2, "last choseong");
}

TEST(hangul_jamo_jungseong_jongseong_zero_width) {
    /// U+1160..U+11FF : medial vowels and final consonants combine
    ASSERT_EQ(lle_codepoint_width(0x1160), 0, "first jungseong");
    ASSERT_EQ(lle_codepoint_width(0x11A7), 0, "boundary jungseong/jongseong");
    ASSERT_EQ(lle_codepoint_width(0x11FF), 0, "last jongseong");
}

/* ============================================================================
 * CJK Unified Ideographs and extensions: width 2
 * ============================================================================
 */

TEST(cjk_unified_ideographs_wide) {
    /// U+4E00..U+9FFF : the main CJK block
    ASSERT_EQ(lle_codepoint_width(0x4E00), 2, "U+4E00 (one)");
    ASSERT_EQ(lle_codepoint_width(0x4E2D), 2, "U+4E2D (middle / China)");
    ASSERT_EQ(lle_codepoint_width(0x6F22), 2, "U+6F22 (Han)");
    ASSERT_EQ(lle_codepoint_width(0x9FFF), 2, "last in block");
}

TEST(cjk_extension_a_wide) {
    ASSERT_EQ(lle_codepoint_width(0x3400), 2, "first Extension A");
    ASSERT_EQ(lle_codepoint_width(0x4DBF), 2, "last Extension A");
}

TEST(cjk_extension_b_wide) {
    ASSERT_EQ(lle_codepoint_width(0x20000), 2, "first Extension B");
    ASSERT_EQ(lle_codepoint_width(0x2A6DF), 2, "last Extension B");
}

TEST(cjk_extension_c_through_g_wide) {
    ASSERT_EQ(lle_codepoint_width(0x2A700), 2, "Extension C");
    ASSERT_EQ(lle_codepoint_width(0x2B740), 2, "Extension D");
    ASSERT_EQ(lle_codepoint_width(0x2B820), 2, "Extension E");
    ASSERT_EQ(lle_codepoint_width(0x2CEB0), 2, "Extension F");
    ASSERT_EQ(lle_codepoint_width(0x30000), 2, "Extension G");
    ASSERT_EQ(lle_codepoint_width(0x3134F), 2, "last Extension G");
}

/* ============================================================================
 * Hangul Syllables: width 2
 * ============================================================================
 */

TEST(hangul_syllables_wide) {
    /// U+AC00..U+D7A3
    ASSERT_EQ(lle_codepoint_width(0xAC00), 2, "first syllable (가)");
    ASSERT_EQ(lle_codepoint_width(0xC548), 2, "안");
    ASSERT_EQ(lle_codepoint_width(0xB155), 2, "녕");
    ASSERT_EQ(lle_codepoint_width(0xD7A3), 2, "last syllable (힣)");
}

/* ============================================================================
 * Hiragana and Katakana: width 2
 * ============================================================================
 */

TEST(hiragana_wide) {
    /// U+3040..U+309F
    ASSERT_EQ(lle_codepoint_width(0x3040), 2, "first hiragana block");
    ASSERT_EQ(lle_codepoint_width(0x3042), 2, "あ");
    ASSERT_EQ(lle_codepoint_width(0x309F), 2, "last hiragana block");
}

TEST(katakana_wide) {
    /// U+30A0..U+30FF
    ASSERT_EQ(lle_codepoint_width(0x30A0), 2, "first katakana block");
    ASSERT_EQ(lle_codepoint_width(0x30AB), 2, "カ");
    ASSERT_EQ(lle_codepoint_width(0x30FF), 2, "last katakana block");
}

TEST(katakana_phonetic_extensions_wide) {
    /// U+31F0..U+31FF
    ASSERT_EQ(lle_codepoint_width(0x31F0), 2, "first kata phonetic ext");
    ASSERT_EQ(lle_codepoint_width(0x31FF), 2, "last kata phonetic ext");
}

/* ============================================================================
 * Halfwidth and Fullwidth Forms: width 2 in the implementation's ranges
 * ============================================================================
 */

TEST(fullwidth_forms_wide) {
    /// U+FF00..U+FF60
    ASSERT_EQ(lle_codepoint_width(0xFF00), 2, "first fullwidth");
    ASSERT_EQ(lle_codepoint_width(0xFF21), 2, "FULLWIDTH A");
    ASSERT_EQ(lle_codepoint_width(0xFF60), 2, "last in first fullwidth range");
}

TEST(fullwidth_forms_secondary_range_wide) {
    /// U+FFE0..U+FFE6
    ASSERT_EQ(lle_codepoint_width(0xFFE0), 2, "FULLWIDTH CENT SIGN");
    ASSERT_EQ(lle_codepoint_width(0xFFE6), 2, "FULLWIDTH WON SIGN");
}

/* ============================================================================
 * Emoji and pictographs: width 2
 * ============================================================================
 */

TEST(emoji_main_block_wide) {
    /// U+1F300..U+1F9FF — the dominant emoji blocks
    ASSERT_EQ(lle_codepoint_width(0x1F300), 2, "first emoji block");
    ASSERT_EQ(lle_codepoint_width(0x1F600), 2, "smiley");
    ASSERT_EQ(lle_codepoint_width(0x1F9FF), 2, "last in emoji block");
}

TEST(emoji_symbols_pictographs_extended_a_wide) {
    /// U+1FA00..U+1FAFF
    ASSERT_EQ(lle_codepoint_width(0x1FA00), 2, "first SP-A");
    ASSERT_EQ(lle_codepoint_width(0x1FAFF), 2, "last SP-A");
}

TEST(misc_symbols_wide) {
    /// U+2600..U+27BF
    ASSERT_EQ(lle_codepoint_width(0x2600), 2, "BLACK SUN WITH RAYS");
    ASSERT_EQ(lle_codepoint_width(0x2705), 2, "WHITE HEAVY CHECK MARK");
    ASSERT_EQ(lle_codepoint_width(0x27BF), 2, "last in block");
}

TEST(misc_technical_wide) {
    /// U+2300..U+23FF
    ASSERT_EQ(lle_codepoint_width(0x2300), 2, "first technical");
    ASSERT_EQ(lle_codepoint_width(0x23FF), 2, "last technical");
}

TEST(stars_wide) {
    /// U+2B50..U+2B55
    ASSERT_EQ(lle_codepoint_width(0x2B50), 2, "WHITE MEDIUM STAR");
    ASSERT_EQ(lle_codepoint_width(0x2B55), 2, "HEAVY LARGE CIRCLE");
}

/* ============================================================================
 * Variation selectors: zero-width
 * ============================================================================
 */

TEST(variation_selectors_zero_width) {
    /// U+FE00..U+FE0F
    ASSERT_EQ(lle_codepoint_width(0xFE00), 0, "VS-1");
    ASSERT_EQ(lle_codepoint_width(0xFE0E), 0, "VS-15 (text)");
    ASSERT_EQ(lle_codepoint_width(0xFE0F), 0, "VS-16 (emoji)");
}

/* ============================================================================
 * Skin tone modifiers (U+1F3FB..U+1F3FF): standalone width 2 per TR11
 *
 * Per Unicode TR11 these codepoints have East Asian Width = W. Their
 * "combine with preceding emoji" behavior is a grapheme-cluster property
 * handled by the grapheme detector, not by lle_codepoint_width(). They
 * also fall inside the broader emoji block (U+1F300..U+1F9FF) so even
 * without a special branch they are width 2.
 * ============================================================================
 */

TEST(emoji_skin_tone_modifiers_wide) {
    ASSERT_EQ(lle_codepoint_width(0x1F3FB), 2, "skin tone 1-2 (light)");
    ASSERT_EQ(lle_codepoint_width(0x1F3FC), 2, "skin tone 3");
    ASSERT_EQ(lle_codepoint_width(0x1F3FD), 2, "skin tone 4");
    ASSERT_EQ(lle_codepoint_width(0x1F3FE), 2, "skin tone 5");
    ASSERT_EQ(lle_codepoint_width(0x1F3FF), 2, "skin tone 6 (dark)");
}

/* ============================================================================
 * Regional Indicators (flags): width 2
 * ============================================================================
 */

TEST(regional_indicators_wide) {
    /// U+1F1E6..U+1F1FF — flag letters
    ASSERT_EQ(lle_codepoint_width(0x1F1E6), 2, "REGIONAL INDICATOR A");
    ASSERT_EQ(lle_codepoint_width(0x1F1FA), 2, "REGIONAL INDICATOR U");
    ASSERT_EQ(lle_codepoint_width(0x1F1FF), 2, "REGIONAL INDICATOR Z");
}

/* ============================================================================
 * Drawing characters: explicitly width 1 (overrides "wide" defaults
 * because terminals render them as single-width)
 * ============================================================================
 */

TEST(box_drawing_width_one) {
    /// U+2500..U+257F
    ASSERT_EQ(lle_codepoint_width(0x2500), 1, "BOX DRAWINGS LIGHT HORIZONTAL");
    ASSERT_EQ(lle_codepoint_width(0x2550), 1, "BOX DRAWINGS DOUBLE HORIZONTAL");
    ASSERT_EQ(lle_codepoint_width(0x257F), 1, "last box drawing");
}

TEST(block_elements_width_one) {
    /// U+2580..U+259F
    ASSERT_EQ(lle_codepoint_width(0x2580), 1, "UPPER HALF BLOCK");
    ASSERT_EQ(lle_codepoint_width(0x2588), 1, "FULL BLOCK");
    ASSERT_EQ(lle_codepoint_width(0x259F), 1, "last block element");
}

TEST(geometric_shapes_width_one) {
    /// U+25A0..U+25FF
    ASSERT_EQ(lle_codepoint_width(0x25A0), 1, "BLACK SQUARE");
    ASSERT_EQ(lle_codepoint_width(0x25CF), 1, "BLACK CIRCLE");
    ASSERT_EQ(lle_codepoint_width(0x25FF), 1, "last geometric shape");
}

/* ============================================================================
 * Default width-1 fallback for unmapped codepoints
 * ============================================================================
 */

TEST(unmapped_codepoints_default_to_one) {
    ASSERT_EQ(lle_codepoint_width(0x00A0), 1, "NBSP");
    ASSERT_EQ(lle_codepoint_width(0x00FF), 1, "Latin-1 supplement");
    ASSERT_EQ(lle_codepoint_width(0x0250), 1, "IPA extension");
    ASSERT_EQ(lle_codepoint_width(0x07FF), 1, "high BMP page");
    ASSERT_EQ(lle_codepoint_width(0xE000), 1, "Private Use Area start");
}

/* ============================================================================
 * Boundary tests: codepoints just outside named ranges should fall to
 * the default (1) or to the next category's value, never to a stale prior
 * range. These catch off-by-one errors in the table-driven branches.
 * ============================================================================
 */

TEST(boundary_just_before_cjk_unified) {
    /// 0x4DFF is just before the CJK block (0x4E00..) and is the end of
    /// Yijing Hexagram Symbols, which the implementation does not treat
    /// specially, so it falls through to the default width of 1.
    ASSERT_EQ(lle_codepoint_width(0x4DFF), 1, "just before CJK");
    ASSERT_EQ(lle_codepoint_width(0x4E00), 2, "first CJK");
}

TEST(boundary_just_after_cjk_unified) {
    /// 0xA000 is just after CJK (0x9FFF). Yi Syllables block; falls
    /// through to default.
    ASSERT_EQ(lle_codepoint_width(0x9FFF), 2, "last CJK");
    ASSERT_EQ(lle_codepoint_width(0xA000), 1, "just after CJK (default)");
}

TEST(boundary_just_before_hangul_jamo_choseong) {
    ASSERT_EQ(lle_codepoint_width(0x10FF), 1, "just before choseong");
    ASSERT_EQ(lle_codepoint_width(0x1100), 2, "first choseong");
}

TEST(boundary_choseong_to_jungseong) {
    ASSERT_EQ(lle_codepoint_width(0x115F), 2, "last choseong");
    ASSERT_EQ(lle_codepoint_width(0x1160), 0, "first jungseong");
}

TEST(boundary_just_after_block_elements) {
    ASSERT_EQ(lle_codepoint_width(0x259F), 1, "last block element");
    ASSERT_EQ(lle_codepoint_width(0x25A0), 1, "first geometric shape");
}

/* ============================================================================
 * lle_is_wide_character() — should agree with width == 2
 * ============================================================================
 */

TEST(is_wide_character_true_for_width_2) {
    ASSERT_TRUE(lle_is_wide_character(0x4E00), "CJK is wide");
    ASSERT_TRUE(lle_is_wide_character(0xAC00), "Hangul syllable is wide");
    ASSERT_TRUE(lle_is_wide_character(0x3042), "hiragana is wide");
    ASSERT_TRUE(lle_is_wide_character(0x1F300), "emoji is wide");
    ASSERT_TRUE(lle_is_wide_character(0x1F1E6), "regional indicator is wide");
}

TEST(is_wide_character_false_for_width_0_or_1) {
    ASSERT_FALSE(lle_is_wide_character(0x00), "NUL is not wide");
    ASSERT_FALSE(lle_is_wide_character(0x20), "space is not wide");
    ASSERT_FALSE(lle_is_wide_character('A'), "A is not wide");
    ASSERT_FALSE(lle_is_wide_character(0x0301), "combining mark is not wide");
    ASSERT_FALSE(lle_is_wide_character(0x200B), "ZWS is not wide");
    ASSERT_FALSE(lle_is_wide_character(0x2500), "box drawing is not wide");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Char Width Tests ===\n\n");

    printf("--- Control characters ---\n");
    RUN_TEST(c0_control_characters_zero_width);
    RUN_TEST(del_is_zero_width);
    RUN_TEST(c1_control_characters_zero_width);
    RUN_TEST(boundary_just_after_c0_is_one);
    RUN_TEST(boundary_just_after_c1_is_one);

    printf("\n--- ASCII printable ---\n");
    RUN_TEST(ascii_printable_width_one);

    printf("\n--- Combining marks ---\n");
    RUN_TEST(combining_diacritical_marks_zero_width);
    RUN_TEST(combining_diacritical_marks_extended_zero_width);
    RUN_TEST(combining_diacritical_marks_supplement_zero_width);
    RUN_TEST(combining_diacritical_marks_for_symbols_zero_width);
    RUN_TEST(combining_half_marks_zero_width);

    printf("\n--- Explicit zero-width ---\n");
    RUN_TEST(explicit_zero_width_characters);

    printf("\n--- Hangul Jamo ---\n");
    RUN_TEST(hangul_jamo_choseong_wide);
    RUN_TEST(hangul_jamo_jungseong_jongseong_zero_width);

    printf("\n--- CJK ideographs ---\n");
    RUN_TEST(cjk_unified_ideographs_wide);
    RUN_TEST(cjk_extension_a_wide);
    RUN_TEST(cjk_extension_b_wide);
    RUN_TEST(cjk_extension_c_through_g_wide);

    printf("\n--- Hangul syllables ---\n");
    RUN_TEST(hangul_syllables_wide);

    printf("\n--- Hiragana and Katakana ---\n");
    RUN_TEST(hiragana_wide);
    RUN_TEST(katakana_wide);
    RUN_TEST(katakana_phonetic_extensions_wide);

    printf("\n--- Fullwidth forms ---\n");
    RUN_TEST(fullwidth_forms_wide);
    RUN_TEST(fullwidth_forms_secondary_range_wide);

    printf("\n--- Emoji and symbols ---\n");
    RUN_TEST(emoji_main_block_wide);
    RUN_TEST(emoji_symbols_pictographs_extended_a_wide);
    RUN_TEST(misc_symbols_wide);
    RUN_TEST(misc_technical_wide);
    RUN_TEST(stars_wide);

    printf("\n--- Variation selectors and modifiers ---\n");
    RUN_TEST(variation_selectors_zero_width);
    RUN_TEST(emoji_skin_tone_modifiers_wide);

    printf("\n--- Regional indicators (flags) ---\n");
    RUN_TEST(regional_indicators_wide);

    printf("\n--- Drawing characters (explicit width 1) ---\n");
    RUN_TEST(box_drawing_width_one);
    RUN_TEST(block_elements_width_one);
    RUN_TEST(geometric_shapes_width_one);

    printf("\n--- Default width-1 fallback ---\n");
    RUN_TEST(unmapped_codepoints_default_to_one);

    printf("\n--- Boundary tests ---\n");
    RUN_TEST(boundary_just_before_cjk_unified);
    RUN_TEST(boundary_just_after_cjk_unified);
    RUN_TEST(boundary_just_before_hangul_jamo_choseong);
    RUN_TEST(boundary_choseong_to_jungseong);
    RUN_TEST(boundary_just_after_block_elements);

    printf("\n--- lle_is_wide_character ---\n");
    RUN_TEST(is_wide_character_true_for_width_2);
    RUN_TEST(is_wide_character_false_for_width_0_or_1);

    return TEST_RESULT();
}
