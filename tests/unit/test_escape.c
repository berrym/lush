/**
 * @file test_escape.c
 * @brief Unit tests for the canonical escape engine (src/escape.c)
 *
 * Covers both dialects: LUSH_ESC_SIMPLE (echo/print/printf) and
 * LUSH_ESC_ANSI_C ($'...' / ${var@E}). Each test pins a concrete
 * behavioral promise; the SIMPLE-vs-ANSI_C pairs guard the dialect
 * boundary (e.g. \x and octal are interpreted only in ANSI-C).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "escape.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/// Convenience: expand a NUL-terminated literal.
static char *esc_expand(const char *s, lush_escape_dialect_t d) {
    return lush_expand_escapes(s, strlen(s), d);
}

/* ============================================================================
 * Simple dialect (echo / print / printf format)
 * ============================================================================
 */

TEST(simple_basic_escapes) {
    char *r = esc_expand("a\\nb\\tc\\\\d", LUSH_ESC_SIMPLE);
    ASSERT_STR_EQ(r, "a\nb\tc\\d", "n/t/backslash decoded");
    free(r);
}

TEST(simple_quotes) {
    char *r = esc_expand("\\\"q\\'s", LUSH_ESC_SIMPLE);
    ASSERT_STR_EQ(r, "\"q's", "escaped quotes decoded");
    free(r);
}

TEST(simple_unknown_kept_literal) {
    char *r = esc_expand("a\\zb", LUSH_ESC_SIMPLE);
    ASSERT_STR_EQ(r, "a\\zb", "unknown escape preserved as backslash+char");
    free(r);
}

TEST(simple_does_not_interpret_hex_or_octal) {
    char *r = esc_expand("\\x41", LUSH_ESC_SIMPLE);
    ASSERT_STR_EQ(r, "\\x41", "\\x is literal in simple dialect");
    free(r);
    char *o = esc_expand("\\101", LUSH_ESC_SIMPLE);
    ASSERT_STR_EQ(o, "\\101", "octal is literal in simple dialect");
    free(o);
}

/* ============================================================================
 * ANSI-C dialect ($'...' / ${var@E})
 * ============================================================================
 */

TEST(ansi_basic_escapes) {
    char *r = esc_expand("a\\nb\\tc", LUSH_ESC_ANSI_C);
    ASSERT_STR_EQ(r, "a\nb\tc", "basic escapes shared with simple");
    free(r);
}

TEST(ansi_hex) {
    char *r = esc_expand("\\x41\\x42", LUSH_ESC_ANSI_C);
    ASSERT_STR_EQ(r, "AB", "\\xHH decoded");
    free(r);
}

TEST(ansi_octal) {
    char *r = esc_expand("\\101\\102", LUSH_ESC_ANSI_C);
    ASSERT_STR_EQ(r, "AB", "\\NNN octal decoded");
    free(r);
}

TEST(ansi_unicode_bmp) {
    /// é is U+00E9 (e-acute) = UTF-8 0xC3 0xA9.
    char *r = esc_expand("\\u00e9", LUSH_ESC_ANSI_C);
    ASSERT_TRUE((unsigned char)r[0] == 0xC3 && (unsigned char)r[1] == 0xA9 &&
                    r[2] == '\0',
                "\\uNNNN encodes UTF-8");
    free(r);
}

TEST(ansi_unicode_astral) {
    /// \U0001F600 is U+1F600 = UTF-8 F0 9F 98 80 (4 bytes, no truncation).
    char *r = esc_expand("\\U0001F600", LUSH_ESC_ANSI_C);
    ASSERT_TRUE((unsigned char)r[0] == 0xF0 && (unsigned char)r[1] == 0x9F &&
                    (unsigned char)r[2] == 0x98 &&
                    (unsigned char)r[3] == 0x80 && r[4] == '\0',
                "\\UNNNNNNNN encodes 4-byte UTF-8");
    free(r);
}

TEST(ansi_control_and_escape) {
    char *r = esc_expand("\\cA", LUSH_ESC_ANSI_C);
    ASSERT_TRUE(r[0] == 0x01 && r[1] == '\0', "\\cA is control-A (0x01)");
    free(r);
    char *e = esc_expand("\\e", LUSH_ESC_ANSI_C);
    ASSERT_TRUE(e[0] == 0x1b && e[1] == '\0', "\\e is ESC (0x1b)");
    free(e);
    char *q = esc_expand("\\?", LUSH_ESC_ANSI_C);
    ASSERT_STR_EQ(q, "?", "\\? is literal question mark");
    free(q);
}

TEST(empty_and_null) {
    char *r = esc_expand("", LUSH_ESC_ANSI_C);
    ASSERT_STR_EQ(r, "", "empty input yields empty string");
    free(r);
    char *n = lush_expand_escapes(NULL, 0, LUSH_ESC_SIMPLE);
    ASSERT_NOT_NULL(n, "NULL input yields a non-NULL empty string");
    ASSERT_STR_EQ(n, "", "NULL input yields empty");
    free(n);
}

TEST(basic_char_decoder) {
    ASSERT_TRUE(lush_escape_basic_char('n') == '\n', "n -> newline");
    ASSERT_TRUE(lush_escape_basic_char('t') == '\t', "t -> tab");
    ASSERT_TRUE(lush_escape_basic_char('\\') == '\\', "backslash");
    ASSERT_TRUE(lush_escape_basic_char('z') == -1, "unknown -> -1");
    ASSERT_TRUE(lush_escape_basic_char('x') == -1, "x is not a basic escape");
}

/* ============================================================================
 * XSI dialect (echo / print / printf %b): \0NNN octal, \c terminates
 * ============================================================================
 */

TEST(xsi_hex_and_leading_zero_octal) {
    char *h = esc_expand("\\x41", LUSH_ESC_XSI);
    ASSERT_STR_EQ(h, "A", "\\xHH hex in XSI");
    free(h);
    char *o = esc_expand("\\0101", LUSH_ESC_XSI);
    ASSERT_STR_EQ(o, "A", "\\0NNN octal in XSI");
    free(o);
}

TEST(xsi_bare_octal_is_literal) {
    /// A bare \1..\7 without the leading 0 stays literal (bash+zsh echo).
    char *r = esc_expand("\\101", LUSH_ESC_XSI);
    ASSERT_STR_EQ(r, "\\101", "\\NNN without leading 0 is literal in XSI");
    free(r);
}

TEST(xsi_e_escape) {
    char *r = esc_expand("\\e", LUSH_ESC_XSI);
    ASSERT_STR_EQ(r, "\033", "\\e -> ESC in XSI");
    free(r);
}

TEST(xsi_c_terminates) {
    bool term = false;
    size_t n = 0;
    char *r = lush_expand_escapes_ex("a\\cbXY", strlen("a\\cbXY"), LUSH_ESC_XSI,
                                     &term, &n);
    ASSERT_TRUE(term, "\\c set terminated");
    ASSERT_EQ((int)n, 1, "output is just 'a'");
    ASSERT_EQ(r[0], 'a', "byte before \\c kept");
    free(r);
}

TEST(xsi_embedded_nul_length) {
    bool term = false;
    size_t n = 0;
    char *r = lush_expand_escapes_ex("A\\x00B", strlen("A\\x00B"), LUSH_ESC_XSI,
                                     &term, &n);
    ASSERT_TRUE(!term, "no terminate");
    ASSERT_EQ((int)n, 3, "A NUL B is 3 bytes");
    ASSERT_EQ(r[0], 'A', "A");
    ASSERT_EQ(r[1], '\0', "NUL");
    ASSERT_EQ(r[2], 'B', "B");
    free(r);
}

/* ============================================================================
 * PRINTF_FMT dialect: \NNN octal (no leading 0), \c terminates
 * ============================================================================
 */

TEST(printf_fmt_octal_no_leading_zero) {
    char *a = esc_expand("\\101", LUSH_ESC_PRINTF_FMT);
    ASSERT_STR_EQ(a, "A", "\\NNN octal in printf format");
    free(a);
    /// \0101 -> \010 (backspace) then a literal '1'
    char *b = esc_expand("\\0101", LUSH_ESC_PRINTF_FMT);
    ASSERT_EQ((int)strlen(b), 2, "\\0101 = BS + '1'");
    ASSERT_EQ(b[0], '\b', "octal 010 = backspace");
    ASSERT_EQ(b[1], '1', "trailing 1");
    free(b);
}

TEST(printf_fmt_c_terminates) {
    bool term = false;
    size_t n = 0;
    char *r = lush_expand_escapes_ex("a\\cb", strlen("a\\cb"),
                                     LUSH_ESC_PRINTF_FMT, &term, &n);
    ASSERT_TRUE(term, "\\c terminates in printf format");
    ASSERT_EQ((int)n, 1, "only 'a'");
    free(r);
}

TEST(ansi_c_control_still_distinct) {
    /// Regression guard: ANSI-C \cX stays a control char, never a terminator.
    char *r = esc_expand("\\cA", LUSH_ESC_ANSI_C);
    ASSERT_EQ((int)strlen(r), 1, "one byte");
    ASSERT_EQ(r[0], 1, "\\cA -> control-A (0x01)");
    free(r);
}

TEST(decode_one_escape_helper) {
    char out[8];
    size_t n = 0;
    bool term = false;
    size_t c = lush_decode_one_escape("\\n", 2, LUSH_ESC_XSI, out, &n, &term);
    ASSERT_EQ((int)c, 2, "consumed \\n");
    ASSERT_EQ((int)n, 1, "one byte");
    ASSERT_EQ(out[0], '\n', "newline");

    c = lush_decode_one_escape("\\c", 2, LUSH_ESC_XSI, out, &n, &term);
    ASSERT_TRUE(term, "\\c terminated");
    ASSERT_EQ((int)n, 0, "no bytes for \\c");
    ASSERT_EQ((int)c, 2, "consumed \\c");

    c = lush_decode_one_escape("\\z", 2, LUSH_ESC_XSI, out, &n, &term);
    ASSERT_EQ((int)c, 1, "unknown escape consumes just the backslash");
    ASSERT_EQ(out[0], '\\', "kept backslash");
}

int main(void) {
    printf("Running escape tests...\n\n");

    printf("Simple dialect:\n");
    RUN_TEST(simple_basic_escapes);
    RUN_TEST(simple_quotes);
    RUN_TEST(simple_unknown_kept_literal);
    RUN_TEST(simple_does_not_interpret_hex_or_octal);

    printf("\nANSI-C dialect:\n");
    RUN_TEST(ansi_basic_escapes);
    RUN_TEST(ansi_hex);
    RUN_TEST(ansi_octal);
    RUN_TEST(ansi_unicode_bmp);
    RUN_TEST(ansi_unicode_astral);
    RUN_TEST(ansi_control_and_escape);

    printf("\nEdge + decoder:\n");
    RUN_TEST(xsi_hex_and_leading_zero_octal);
    RUN_TEST(xsi_bare_octal_is_literal);
    RUN_TEST(xsi_e_escape);
    RUN_TEST(xsi_c_terminates);
    RUN_TEST(xsi_embedded_nul_length);
    RUN_TEST(printf_fmt_octal_no_leading_zero);
    RUN_TEST(printf_fmt_c_terminates);
    RUN_TEST(ansi_c_control_still_distinct);
    RUN_TEST(decode_one_escape_helper);

    RUN_TEST(empty_and_null);
    RUN_TEST(basic_char_decoder);

    return TEST_RESULT();
}
