/**
 * @file test_unicode_class.c
 * @brief Unit tests for Unicode general-category classifiers
 *
 * Verifies the predicates declared in lle/unicode_class.h. Every test
 * targets a behavior whose regression would be user-visible: pattern
 * char-class matching across non-ASCII letters, decimal-digit blocks
 * beyond ASCII, Unicode whitespace, control / printable boundaries.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/unicode_class.h"
#include "test_framework.h"
#include <stdio.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Alpha / letter
 * ============================================================================
 */

TEST(alpha_ascii_letters) {
    ASSERT(lle_unicode_is_alpha('a'), "lowercase ascii");
    ASSERT(lle_unicode_is_alpha('Z'), "uppercase ascii");
    ASSERT(!lle_unicode_is_alpha('5'), "ascii digit is not alpha");
    ASSERT(!lle_unicode_is_alpha(' '), "ascii space is not alpha");
}

TEST(alpha_latin_supplement) {
    ASSERT(lle_unicode_is_alpha(0x00E9), "é (U+00E9)");
    ASSERT(lle_unicode_is_alpha(0x00C4), "Ä (U+00C4)");
    ASSERT(lle_unicode_is_alpha(0x00F1), "ñ (U+00F1)");
    ASSERT(lle_unicode_is_alpha(0x00FF), "ÿ (U+00FF)");
    ASSERT(!lle_unicode_is_alpha(0x00A0), "NBSP is not alpha");
    ASSERT(!lle_unicode_is_alpha(0x00B1), "± is not alpha");
}

TEST(alpha_latin_extended) {
    ASSERT(lle_unicode_is_alpha(0x0100), "Ā (Latin Ext-A)");
    ASSERT(lle_unicode_is_alpha(0x017F), "ſ (long s, Latin Ext-A)");
    ASSERT(lle_unicode_is_alpha(0x0180), "ƀ (Latin Ext-B)");
}

TEST(alpha_greek) {
    ASSERT(lle_unicode_is_alpha(0x03B1), "α (Greek alpha)");
    ASSERT(lle_unicode_is_alpha(0x03A9), "Ω (Greek capital omega)");
}

TEST(alpha_cjk_out_of_scope) {
    /// CJK ideographs are category Lo, which the case-mapping-based
    /// approach does not cover. Documented limitation.
    ASSERT(!lle_unicode_is_alpha(0x4E2D), "中 (CJK) out of scope");
}

/* ============================================================================
 * Digit
 * ============================================================================
 */

TEST(digit_ascii) {
    ASSERT(lle_unicode_is_digit('0'), "0");
    ASSERT(lle_unicode_is_digit('9'), "9");
    ASSERT(!lle_unicode_is_digit('a'), "a is not digit");
    ASSERT(!lle_unicode_is_digit('/'), "/ is not digit");
    ASSERT(!lle_unicode_is_digit(':'), ": is not digit");
}

TEST(digit_arabic_indic) {
    ASSERT(lle_unicode_is_digit(0x0660), "Arabic-Indic 0");
    ASSERT(lle_unicode_is_digit(0x0663), "Arabic-Indic 3 (٣)");
    ASSERT(lle_unicode_is_digit(0x0669), "Arabic-Indic 9");
    ASSERT(!lle_unicode_is_digit(0x066A), "U+066A (percent) not a digit");
}

TEST(digit_devanagari_and_others) {
    ASSERT(lle_unicode_is_digit(0x0966), "Devanagari 0");
    ASSERT(lle_unicode_is_digit(0x096F), "Devanagari 9");
    ASSERT(lle_unicode_is_digit(0x0E50), "Thai 0");
    ASSERT(lle_unicode_is_digit(0xFF10), "Fullwidth 0");
    ASSERT(lle_unicode_is_digit(0xFF19), "Fullwidth 9");
}

TEST(digit_letters_are_not_digits) {
    ASSERT(!lle_unicode_is_digit(0x00E9), "é is not digit");
    ASSERT(!lle_unicode_is_digit(0x03B1), "α is not digit");
}

/* ============================================================================
 * Alnum
 * ============================================================================
 */

TEST(alnum_ascii) {
    ASSERT(lle_unicode_is_alnum('a'), "letter");
    ASSERT(lle_unicode_is_alnum('5'), "digit");
    ASSERT(!lle_unicode_is_alnum(' '), "space is not alnum");
}

TEST(alnum_mixed_scripts) {
    ASSERT(lle_unicode_is_alnum(0x00E9), "é");
    ASSERT(lle_unicode_is_alnum(0x0663), "Arabic-Indic 3");
    ASSERT(lle_unicode_is_alnum(0x03B1), "α");
}

/* ============================================================================
 * Whitespace
 * ============================================================================
 */

TEST(space_ascii) {
    ASSERT(lle_unicode_is_space(' '), "ascii space");
    ASSERT(lle_unicode_is_space('\t'), "tab");
    ASSERT(lle_unicode_is_space('\n'), "newline");
    ASSERT(lle_unicode_is_space('\v'), "vertical tab");
    ASSERT(lle_unicode_is_space('\f'), "form feed");
    ASSERT(lle_unicode_is_space('\r'), "carriage return");
    ASSERT(!lle_unicode_is_space('a'), "letter is not space");
}

TEST(space_unicode) {
    ASSERT(lle_unicode_is_space(0x00A0), "NBSP");
    ASSERT(lle_unicode_is_space(0x2028), "LINE SEPARATOR");
    ASSERT(lle_unicode_is_space(0x2029), "PARAGRAPH SEPARATOR");
    ASSERT(lle_unicode_is_space(0x2003), "EM SPACE");
    ASSERT(lle_unicode_is_space(0x3000), "IDEOGRAPHIC SPACE");
    ASSERT(!lle_unicode_is_space(0x00E9), "é is not space");
}

/* ============================================================================
 * Blank (ASCII-only by POSIX)
 * ============================================================================
 */

TEST(blank_is_strictly_ascii_space_or_tab) {
    ASSERT(lle_unicode_is_blank(' '), "ascii space");
    ASSERT(lle_unicode_is_blank('\t'), "tab");
    ASSERT(!lle_unicode_is_blank('\n'), "newline is not blank");
    ASSERT(!lle_unicode_is_blank(0x00A0), "NBSP is not blank (POSIX strict)");
    ASSERT(!lle_unicode_is_blank(0x3000), "ideographic space is not blank");
}

/* ============================================================================
 * Control characters
 * ============================================================================
 */

TEST(cntrl_ascii) {
    ASSERT(lle_unicode_is_cntrl(0x00), "NUL");
    ASSERT(lle_unicode_is_cntrl(0x1F), "US (last C0)");
    ASSERT(lle_unicode_is_cntrl(0x7F), "DEL");
    ASSERT(!lle_unicode_is_cntrl(' '), "space is not cntrl");
    ASSERT(!lle_unicode_is_cntrl('a'), "letter is not cntrl");
}

TEST(cntrl_c1) {
    ASSERT(lle_unicode_is_cntrl(0x80), "C1 first");
    ASSERT(lle_unicode_is_cntrl(0x9F), "C1 last");
    ASSERT(!lle_unicode_is_cntrl(0xA0), "NBSP is not cntrl");
    ASSERT(!lle_unicode_is_cntrl(0x00E9), "é is not cntrl");
}

/* ============================================================================
 * Print / graph
 * ============================================================================
 */

TEST(print_excludes_controls) {
    ASSERT(!lle_unicode_is_print(0x00), "NUL not printable");
    ASSERT(!lle_unicode_is_print(0x1B), "ESC not printable");
    ASSERT(lle_unicode_is_print(' '), "space printable");
    ASSERT(lle_unicode_is_print('a'), "letter printable");
    ASSERT(lle_unicode_is_print(0x00E9), "é printable");
}

TEST(print_excludes_zero_width_formatters) {
    ASSERT(!lle_unicode_is_print(0x200B), "ZWSP not printable");
    ASSERT(!lle_unicode_is_print(0x200E), "LRM not printable");
    ASSERT(!lle_unicode_is_print(0xFEFF), "BOM not printable");
}

TEST(graph_excludes_spaces) {
    ASSERT(!lle_unicode_is_graph(' '), "space is not graph");
    ASSERT(!lle_unicode_is_graph('\t'), "tab is not graph");
    ASSERT(!lle_unicode_is_graph(0x00A0), "NBSP is not graph");
    ASSERT(lle_unicode_is_graph('a'), "letter is graph");
    ASSERT(lle_unicode_is_graph(0x00E9), "é is graph");
}

/* ============================================================================
 * Punctuation
 * ============================================================================
 */

TEST(punct_ascii) {
    ASSERT(lle_unicode_is_punct('!'), "exclamation");
    ASSERT(lle_unicode_is_punct(','), "comma");
    ASSERT(lle_unicode_is_punct('.'), "period");
    ASSERT(!lle_unicode_is_punct('a'), "letter not punct");
    ASSERT(!lle_unicode_is_punct(' '), "space not punct");
    ASSERT(!lle_unicode_is_punct('5'), "digit not punct");
}

TEST(punct_unicode) {
    ASSERT(lle_unicode_is_punct(0x00A1), "inverted !");
    ASSERT(lle_unicode_is_punct(0x00BF), "inverted ?");
    ASSERT(lle_unicode_is_punct(0x2014), "em dash (U+2014)");
    ASSERT(lle_unicode_is_punct(0x2026), "horizontal ellipsis (U+2026)");
}

/* ============================================================================
 * Xdigit (POSIX ASCII)
 * ============================================================================
 */

TEST(xdigit_ascii_only) {
    for (uint32_t c = '0'; c <= '9'; c++) {
        ASSERT(lle_unicode_is_xdigit(c), "decimal digit");
    }
    for (uint32_t c = 'a'; c <= 'f'; c++) {
        ASSERT(lle_unicode_is_xdigit(c), "lower hex");
    }
    for (uint32_t c = 'A'; c <= 'F'; c++) {
        ASSERT(lle_unicode_is_xdigit(c), "upper hex");
    }
    ASSERT(!lle_unicode_is_xdigit('g'), "g is not hex");
    ASSERT(!lle_unicode_is_xdigit('G'), "G is not hex");
    ASSERT(!lle_unicode_is_xdigit(0x0663),
           "Arabic-Indic 3 is not xdigit (POSIX)");
    ASSERT(!lle_unicode_is_xdigit(0xFF10), "Fullwidth 0 is not xdigit (POSIX)");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running unicode_class tests...\n\n");

    printf("Alpha:\n");
    RUN_TEST(alpha_ascii_letters);
    RUN_TEST(alpha_latin_supplement);
    RUN_TEST(alpha_latin_extended);
    RUN_TEST(alpha_greek);
    RUN_TEST(alpha_cjk_out_of_scope);

    printf("\nDigit:\n");
    RUN_TEST(digit_ascii);
    RUN_TEST(digit_arabic_indic);
    RUN_TEST(digit_devanagari_and_others);
    RUN_TEST(digit_letters_are_not_digits);

    printf("\nAlnum:\n");
    RUN_TEST(alnum_ascii);
    RUN_TEST(alnum_mixed_scripts);

    printf("\nSpace / blank:\n");
    RUN_TEST(space_ascii);
    RUN_TEST(space_unicode);
    RUN_TEST(blank_is_strictly_ascii_space_or_tab);

    printf("\nControl:\n");
    RUN_TEST(cntrl_ascii);
    RUN_TEST(cntrl_c1);

    printf("\nPrint / graph:\n");
    RUN_TEST(print_excludes_controls);
    RUN_TEST(print_excludes_zero_width_formatters);
    RUN_TEST(graph_excludes_spaces);

    printf("\nPunctuation:\n");
    RUN_TEST(punct_ascii);
    RUN_TEST(punct_unicode);

    printf("\nXdigit:\n");
    RUN_TEST(xdigit_ascii_only);

    return TEST_RESULT();
}
