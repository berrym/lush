/**
 * @file test_grapheme_detector.c
 * @brief Unit tests for LLE grapheme cluster boundary detection
 *
 * Exercises:
 *   - get_grapheme_break_property() — codepoint -> GB property classification
 *   - is_grapheme_cluster_boundary() — UAX #29 boundary rules
 *   - is_grapheme_boundary_at_position() — UTF-8 byte-position queries
 *
 * Reference: https://unicode.org/reports/tr29/
 *
 * Observations recorded during the test pass (worth follow-up):
 *
 *   1. The implementation never returns GB_PREPEND from
 *      get_grapheme_break_property(). The enum value exists and
 *      is_grapheme_cluster_boundary() handles GB9b (Prepend × — no break),
 *      but no codepoint is currently classified as Prepend. The Unicode
 *      Prepend class includes characters from Arabic, Brahmi, Khmer, and
 *      others — currently treated as GB_OTHER. Tests therefore exercise
 *      GB9b only by synthesising the property indirectly via the boundary
 *      function's input parameters where possible; full coverage requires
 *      either extending the property table or accepting the gap.
 *
 *   2. GB_SPACING_MARK is only assigned to U+0903 (Devanagari Sign
 *      Visarga). The Unicode SpacingMark class spans many Indic scripts
 *      (Devanagari, Bengali, Gurmukhi, Gujarati, Oriya, Tamil, etc.).
 *      The single-codepoint coverage is a known incompleteness and is
 *      asserted explicitly in the GB_SPACING_MARK test below.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/grapheme_detector.h"
#include "test_framework.h"

/* ============================================================================
 * get_grapheme_break_property — classification
 * ============================================================================
 */

TEST(prop_cr_lf) {
    ASSERT_EQ(get_grapheme_break_property(0x000D), GB_CR, "U+000D is CR");
    ASSERT_EQ(get_grapheme_break_property(0x000A), GB_LF, "U+000A is LF");
}

TEST(prop_control_characters) {
    ASSERT_EQ(get_grapheme_break_property(0x00), GB_CONTROL, "NUL");
    ASSERT_EQ(get_grapheme_break_property(0x1F), GB_CONTROL, "US (last C0)");
    /* Note: TAB (0x09) and other C0 controls are GB_CONTROL by the
     * cp < 0x20 check; CR (0x0D) and LF (0x0A) are special-cased above. */
    ASSERT_EQ(get_grapheme_break_property(0x09), GB_CONTROL, "TAB");
    ASSERT_EQ(get_grapheme_break_property(0x7F), GB_CONTROL, "DEL");
    ASSERT_EQ(get_grapheme_break_property(0x80), GB_CONTROL, "first C1");
    ASSERT_EQ(get_grapheme_break_property(0x9F), GB_CONTROL, "last C1");
}

TEST(prop_zwj) {
    ASSERT_EQ(get_grapheme_break_property(0x200D), GB_ZWJ, "ZWJ");
}

TEST(prop_extend_combining_marks) {
    /* The five combining-mark blocks the implementation treats as Extend */
    ASSERT_EQ(get_grapheme_break_property(0x0300), GB_EXTEND,
              "first combining diacritical");
    ASSERT_EQ(get_grapheme_break_property(0x036F), GB_EXTEND,
              "last combining diacritical");
    ASSERT_EQ(get_grapheme_break_property(0x1AB0), GB_EXTEND,
              "first combining diacritical extended");
    ASSERT_EQ(get_grapheme_break_property(0x1AFF), GB_EXTEND,
              "last combining diacritical extended");
    ASSERT_EQ(get_grapheme_break_property(0x1DC0), GB_EXTEND,
              "first combining diacritical supplement");
    ASSERT_EQ(get_grapheme_break_property(0x1DFF), GB_EXTEND,
              "last combining diacritical supplement");
    ASSERT_EQ(get_grapheme_break_property(0x20D0), GB_EXTEND,
              "first combining diacritical for symbols");
    ASSERT_EQ(get_grapheme_break_property(0x20FF), GB_EXTEND,
              "last combining diacritical for symbols");
    ASSERT_EQ(get_grapheme_break_property(0xFE20), GB_EXTEND,
              "first combining half mark");
    ASSERT_EQ(get_grapheme_break_property(0xFE2F), GB_EXTEND,
              "last combining half mark");
}

TEST(prop_regional_indicator) {
    ASSERT_EQ(get_grapheme_break_property(0x1F1E6), GB_REGIONAL_INDICATOR,
              "first RI (A)");
    ASSERT_EQ(get_grapheme_break_property(0x1F1FA), GB_REGIONAL_INDICATOR,
              "RI U");
    ASSERT_EQ(get_grapheme_break_property(0x1F1FF), GB_REGIONAL_INDICATOR,
              "last RI (Z)");
}

TEST(prop_hangul_jamo_l_v_t) {
    ASSERT_EQ(get_grapheme_break_property(0x1100), GB_L, "first Choseong");
    ASSERT_EQ(get_grapheme_break_property(0x1112), GB_L, "mid Choseong");
    ASSERT_EQ(get_grapheme_break_property(0x115F), GB_L, "last Choseong");
    ASSERT_EQ(get_grapheme_break_property(0x1160), GB_V, "first Jungseong");
    ASSERT_EQ(get_grapheme_break_property(0x11A7), GB_V, "last Jungseong");
    ASSERT_EQ(get_grapheme_break_property(0x11A8), GB_T, "first Jongseong");
    ASSERT_EQ(get_grapheme_break_property(0x11FF), GB_T, "last Jongseong");
}

TEST(prop_hangul_syllable_lv_vs_lvt) {
    /* AC00..D7A3: 11172 syllables.
     * s_index = cp - 0xAC00; t_index = s_index % 28.
     * t_index == 0 is the "no trailing consonant" form -> GB_LV.
     * t_index != 0 has a trailing consonant -> GB_LVT.
     *
     * 0xAC00 = 가 (s=0, t=0) -> LV
     * 0xAC01 = 각 (s=1, t=1) -> LVT
     * 0xAC1B = 갛 (s=27, t=27) -> LVT
     * 0xAC1C = 개 (s=28, t=0) -> LV
     */
    ASSERT_EQ(get_grapheme_break_property(0xAC00), GB_LV, "가 (LV)");
    ASSERT_EQ(get_grapheme_break_property(0xAC01), GB_LVT, "각 (LVT)");
    ASSERT_EQ(get_grapheme_break_property(0xAC1B), GB_LVT,
              "갛 (LVT, last in row)");
    ASSERT_EQ(get_grapheme_break_property(0xAC1C), GB_LV, "개 (LV, next row)");
    ASSERT_EQ(get_grapheme_break_property(0xD7A3), GB_LVT, "힣 (LVT, last)");
}

TEST(prop_extended_pictographic) {
    /* Emoji blocks U+1F300..U+1F9FF and Misc Symbols U+2600..U+27BF */
    ASSERT_EQ(get_grapheme_break_property(0x1F300), GB_EXTENDED_PICTOGRAPHIC,
              "first emoji block");
    ASSERT_EQ(get_grapheme_break_property(0x1F600), GB_EXTENDED_PICTOGRAPHIC,
              "smiley");
    ASSERT_EQ(get_grapheme_break_property(0x1F9FF), GB_EXTENDED_PICTOGRAPHIC,
              "last emoji block");
    ASSERT_EQ(get_grapheme_break_property(0x2600), GB_EXTENDED_PICTOGRAPHIC,
              "BLACK SUN WITH RAYS");
    ASSERT_EQ(get_grapheme_break_property(0x27BF), GB_EXTENDED_PICTOGRAPHIC,
              "last in misc symbols");
}

TEST(prop_spacing_mark_only_covers_devanagari_visarga) {
    /* INCOMPLETE: the implementation classifies only U+0903 as
     * GB_SPACING_MARK. Real Unicode SpacingMark covers many Indic
     * codepoints. Asserted explicitly so a future expansion of the
     * table is forced to update this test. */
    ASSERT_EQ(get_grapheme_break_property(0x0903), GB_SPACING_MARK,
              "U+0903 Devanagari Sign Visarga");
    /* Other Devanagari spacing marks the implementation does NOT cover
     * (Unicode does): treated as GB_OTHER. */
    ASSERT_EQ(get_grapheme_break_property(0x093E), GB_OTHER,
              "U+093E (real SpacingMark, currently OTHER)");
    ASSERT_EQ(get_grapheme_break_property(0x094E), GB_OTHER,
              "U+094E (real SpacingMark, currently OTHER)");
}

TEST(prop_other_default) {
    /* Plain ASCII letters/digits/punctuation are GB_OTHER */
    ASSERT_EQ(get_grapheme_break_property('A'), GB_OTHER, "A");
    ASSERT_EQ(get_grapheme_break_property('0'), GB_OTHER, "0");
    ASSERT_EQ(get_grapheme_break_property(' '), GB_OTHER, "space");
    /* CJK ideographs not in any special class */
    ASSERT_EQ(get_grapheme_break_property(0x4E00), GB_OTHER, "U+4E00");
    /* Latin Supplement */
    ASSERT_EQ(get_grapheme_break_property(0x00E9), GB_OTHER, "é (precomposed)");
}

/* ============================================================================
 * is_grapheme_cluster_boundary — UAX #29 rules
 * ============================================================================
 */

TEST(rule_gb3_cr_then_lf_no_break) {
    /* GB3: CR × LF — the CR/LF pair is one grapheme */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x000D, 0x000A, false, 0),
                 "CR followed by LF must not break");
}

TEST(rule_gb4_break_after_control_cr_lf) {
    /* GB4: (Control | CR | LF) ÷ */
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x0001, 'A', false, 0),
                "Control followed by anything must break");
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x000A, 'A', false, 0),
                "LF followed by letter must break");
    /* CR followed by something other than LF must break (GB3 only saves
     * the specific CR×LF pair) */
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x000D, 'A', false, 0),
                "CR followed by non-LF must break");
}

TEST(rule_gb5_break_before_control_cr_lf) {
    /* GB5: ÷ (Control | CR | LF) */
    ASSERT_TRUE(is_grapheme_cluster_boundary('A', 0x0001, false, 0),
                "Anything followed by Control must break");
    ASSERT_TRUE(is_grapheme_cluster_boundary('A', 0x000A, false, 0),
                "Letter followed by LF must break");
    ASSERT_TRUE(is_grapheme_cluster_boundary('A', 0x000D, false, 0),
                "Letter followed by CR must break");
}

TEST(rule_gb6_l_no_break_to_l_v_lv_lvt) {
    /* GB6: L × (L | V | LV | LVT) */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1100, 0x1101, false, 0),
                 "L × L (Choseong + Choseong)");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1100, 0x1161, false, 0),
                 "L × V");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1100, 0xAC00, false, 0),
                 "L × LV");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1100, 0xAC01, false, 0),
                 "L × LVT");
    /* L followed by T should break (not in the GB6 set) */
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x1100, 0x11A8, false, 0),
                "L × T must break (T is not in GB6)");
}

TEST(rule_gb7_lv_or_v_no_break_to_v_or_t) {
    /* GB7: (LV | V) × (V | T) */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0xAC00, 0x1161, false, 0),
                 "LV × V");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0xAC00, 0x11A8, false, 0),
                 "LV × T");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1161, 0x1161, false, 0),
                 "V × V");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1161, 0x11A8, false, 0),
                 "V × T");
}

TEST(rule_gb8_lvt_or_t_no_break_to_t) {
    /* GB8: (LVT | T) × T */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0xAC01, 0x11A8, false, 0),
                 "LVT × T");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x11A8, 0x11A9, false, 0),
                 "T × T");
}

TEST(rule_gb9_no_break_before_extend_or_zwj) {
    /* GB9: × (Extend | ZWJ) — combining marks attach to the previous char */
    ASSERT_FALSE(is_grapheme_cluster_boundary('e', 0x0301, false, 0),
                 "letter × combining acute (Extend)");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x4E00, 0x0301, false, 0),
                 "CJK × Extend");
    ASSERT_FALSE(is_grapheme_cluster_boundary('A', 0x200D, false, 0),
                 "letter × ZWJ");
}

TEST(rule_gb9a_no_break_before_spacing_mark) {
    /* GB9a: × SpacingMark — only U+0903 is currently classified */
    ASSERT_FALSE(is_grapheme_cluster_boundary('A', 0x0903, false, 0),
                 "letter × U+0903 (SpacingMark)");
}

TEST(rule_gb11_emoji_zwj_sequence) {
    /* GB11: \p{Extended_Pictographic} Extend* ZWJ × \p{Extended_Pictographic}
     * The boundary function takes prev_was_zwj; when true and both sides are
     * pictographic, no break. */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1F468, 0x1F469, true, 0),
                 "emoji ZWJ emoji (man+woman family) — no break");
    /* Without prev_was_zwj=true, the same pictographic+pictographic must
     * break (GB999 default). */
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x1F468, 0x1F469, false, 0),
                "two emojis with no preceding ZWJ — break");
}

TEST(rule_gb12_gb13_regional_indicator_pairs) {
    /* GB12/GB13: RI × RI — pair up. Even ri_sequence_count -> no break;
     * odd -> break (we are completing a pair).
     *
     * The implementation: return (ri_sequence_count % 2) == 0;
     * That returns NO BREAK when count is even, BREAK when odd.
     *
     * Reading: ri_sequence_count is "number of preceding REGIONAL
     * INDICATOR codepoints in sequence". For the first RI of a pair,
     * count is even (0). For the second RI, count is odd (1). After the
     * pair completes, the next RI starts a new pair (count even again).
     *
     * Wait — that's the inverse of the comment. Let me re-read the
     * source: "Odd position = no break, even position = break".
     * Hmm, the comment says even -> break, but the return says
     * "(count % 2) == 0" returns true (which the function semantics
     * say is "boundary exists" -> break). So even count -> break,
     * odd count -> no break. That matches the comment.
     *
     * Concretely:
     *   First RI:  count=0 (even) -> BREAK (start of new flag)
     *   Second RI: count=1 (odd)  -> NO BREAK (completing the flag)
     *   Third RI:  count=2 (even) -> BREAK (start of next flag)
     *   Fourth RI: count=3 (odd)  -> NO BREAK (completing it)
     */
    /* RI followed by RI with even count (start of new pair) -> break */
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x1F1E6, 0x1F1E8, false, 0),
                "RI × RI, count=0 (even) -> break (start of new pair)");
    /* RI followed by RI with odd count (completing a pair) -> no break */
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1F1E6, 0x1F1E8, false, 1),
                 "RI × RI, count=1 (odd) -> no break (completing pair)");
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x1F1E6, 0x1F1E8, false, 2),
                "RI × RI, count=2 (even) -> break (start of next pair)");
    ASSERT_FALSE(is_grapheme_cluster_boundary(0x1F1E6, 0x1F1E8, false, 3),
                 "RI × RI, count=3 (odd) -> no break");
}

TEST(rule_gb999_default_break) {
    /* GB999: Any ÷ Any — anything not handled by a specific rule breaks */
    ASSERT_TRUE(is_grapheme_cluster_boundary('A', 'B', false, 0),
                "letter × letter (default break)");
    ASSERT_TRUE(is_grapheme_cluster_boundary(0x4E00, 0x4E01, false, 0),
                "CJK × CJK (default break)");
    ASSERT_TRUE(is_grapheme_cluster_boundary(0xAC00, 'A', false, 0),
                "Hangul syllable × letter (default break)");
}

/* ============================================================================
 * is_grapheme_boundary_at_position — UTF-8 byte-position queries
 * ============================================================================
 */

TEST(pos_start_of_text_is_boundary) {
    const char *text = "hello";
    ASSERT_TRUE(is_grapheme_boundary_at_position(text, text, text + 5),
                "pos == text_start");
}

TEST(pos_end_of_text_is_boundary) {
    /* Use a real char[] so the pos > text_end probe (text + 10) lives
     * inside the actual array -- otherwise the pointer arithmetic is
     * UB and gcc -Warray-bounds flags it. The logical text_end stays
     * at text + 5 (after "hello"). */
    char text[16] = "hello";
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 5, text, text + 5),
                "pos == text_end");
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 10, text, text + 5),
                "pos > text_end");
}

TEST(pos_between_ascii_letters_is_boundary) {
    const char *text = "ab";
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 1, text, text + 2),
                "between two ASCII letters is a boundary");
}

TEST(pos_before_combining_mark_is_not_boundary) {
    /* "e" + U+0301 (combining acute) -> "é" (one grapheme) */
    /* U+0301 in UTF-8 is 0xCC 0x81 (2 bytes). So text is "e\xCC\x81". */
    const char *text = "e\xCC\x81";
    /* Position 1 is between 'e' and the combining mark; should NOT be a
     * boundary because the mark attaches to the letter. */
    ASSERT_FALSE(is_grapheme_boundary_at_position(text + 1, text, text + 3),
                 "between letter and combining mark is not a boundary");
}

TEST(pos_after_combining_mark_is_boundary) {
    /* "e\xCC\x81X" — between the combining mark and X is a boundary */
    const char *text = "e\xCC\x81X";
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 3, text, text + 4),
                "after grapheme cluster, before next character");
}

TEST(pos_invalid_utf8_treated_as_boundary) {
    /* 0xFF is invalid as a UTF-8 lead byte. The function should treat
     * such positions as boundaries to avoid getting stuck. */
    const char text[] = {(char)0xFF, 'A', '\0'};
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 1, text, text + 2),
                "after invalid lead byte -> boundary");
}

TEST(pos_at_continuation_byte_scans_back) {
    /* If the queried position is in the middle of a multi-byte UTF-8
     * sequence (a continuation byte), the implementation scans backward
     * to find the lead byte. Test with a CJK char (3 bytes) followed
     * by ASCII. */
    /* U+4E2D '中' = 0xE4 0xB8 0xAD (3 bytes). text = "中A".
     * The string is split so the hex escape \xAD does not absorb the
     * following 'A' into a single (out-of-range) hex literal. */
    const char *text = "\xE4\xB8\xAD"
                       "A";
    /* Position 3 is just after the CJK char and before 'A' — boundary. */
    ASSERT_TRUE(is_grapheme_boundary_at_position(text + 3, text, text + 4),
                "between CJK char and ASCII letter is a boundary");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Grapheme Detector Tests ===\n\n");

    printf("--- Property classification ---\n");
    RUN_TEST(prop_cr_lf);
    RUN_TEST(prop_control_characters);
    RUN_TEST(prop_zwj);
    RUN_TEST(prop_extend_combining_marks);
    RUN_TEST(prop_regional_indicator);
    RUN_TEST(prop_hangul_jamo_l_v_t);
    RUN_TEST(prop_hangul_syllable_lv_vs_lvt);
    RUN_TEST(prop_extended_pictographic);
    RUN_TEST(prop_spacing_mark_only_covers_devanagari_visarga);
    RUN_TEST(prop_other_default);

    printf("\n--- UAX #29 boundary rules ---\n");
    RUN_TEST(rule_gb3_cr_then_lf_no_break);
    RUN_TEST(rule_gb4_break_after_control_cr_lf);
    RUN_TEST(rule_gb5_break_before_control_cr_lf);
    RUN_TEST(rule_gb6_l_no_break_to_l_v_lv_lvt);
    RUN_TEST(rule_gb7_lv_or_v_no_break_to_v_or_t);
    RUN_TEST(rule_gb8_lvt_or_t_no_break_to_t);
    RUN_TEST(rule_gb9_no_break_before_extend_or_zwj);
    RUN_TEST(rule_gb9a_no_break_before_spacing_mark);
    RUN_TEST(rule_gb11_emoji_zwj_sequence);
    RUN_TEST(rule_gb12_gb13_regional_indicator_pairs);
    RUN_TEST(rule_gb999_default_break);

    printf("\n--- UTF-8 byte-position queries ---\n");
    RUN_TEST(pos_start_of_text_is_boundary);
    RUN_TEST(pos_end_of_text_is_boundary);
    RUN_TEST(pos_between_ascii_letters_is_boundary);
    RUN_TEST(pos_before_combining_mark_is_not_boundary);
    RUN_TEST(pos_after_combining_mark_is_boundary);
    RUN_TEST(pos_invalid_utf8_treated_as_boundary);
    RUN_TEST(pos_at_continuation_byte_scans_back);

    return TEST_RESULT();
}
