/**
 * Spec 03 UTF-8 and Unicode TR#29 Compliance Tests
 *
 * Verifies foundation modules for buffer management comply with:
 * - Unicode UTF-8 encoding standard
 * - Unicode TR#29 (UAX #29) grapheme cluster boundary specification
 */

#include "../../../include/lle/unicode_grapheme.h"
#include "../../../include/lle/utf8_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    do {                                                                       \
        printf("  Testing: %s ... ", name);                                    \
        fflush(stdout);                                                        \
        tests_run++;                                                           \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        printf("PASS\n");                                                      \
        tests_passed++;                                                        \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("FAIL: %s\n", msg);                                             \
        tests_failed++;                                                        \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

// UTF-8 Encoding Tests
static void test_utf8_ascii() {
    TEST("UTF-8 ASCII encoding");
    ASSERT_EQ(lle_utf8_sequence_length('A'), 1, "ASCII should be 1 byte");
    ASSERT_EQ(lle_utf8_sequence_length(0x7F), 1, "Max ASCII should be 1 byte");
    PASS();
}

static void test_utf8_multibyte() {
    TEST("UTF-8 multibyte sequence lengths");
    ASSERT_EQ(lle_utf8_sequence_length(0xC2), 2, "2-byte sequence");
    ASSERT_EQ(lle_utf8_sequence_length(0xE0), 3, "3-byte sequence");
    ASSERT_EQ(lle_utf8_sequence_length(0xF0), 4, "4-byte sequence");
    PASS();
}

static void test_utf8_validation() {
    TEST("UTF-8 validation");
    const char *valid = "Hello";
    const char *invalid = "\xFF\xFE";

    ASSERT_TRUE(lle_utf8_is_valid(valid, strlen(valid)), "Valid ASCII");
    ASSERT_TRUE(!lle_utf8_is_valid(invalid, 2), "Invalid sequence rejected");
    PASS();
}

static void test_utf8_codepoint_counting() {
    TEST("UTF-8 codepoint counting");
    const char *text = "AB"; // 2 ASCII chars = 2 codepoints
    ASSERT_EQ(lle_utf8_count_codepoints(text, strlen(text)), 2,
              "Count ASCII codepoints");
    PASS();
}

// Grapheme Cluster Tests
static void test_grapheme_ascii() {
    TEST("Grapheme cluster - ASCII");
    const char *text = "AB";
    size_t count = lle_utf8_count_graphemes(text, strlen(text));
    ASSERT_EQ(count, 2, "Two ASCII chars = 2 graphemes");
    PASS();
}

static void test_grapheme_boundary_start() {
    TEST("Grapheme boundary at start (GB1)");
    const char *text = "A";
    ASSERT_TRUE(lle_is_grapheme_boundary(text, text, text + 1),
                "Start is always boundary");
    PASS();
}

static void test_grapheme_cr_lf() {
    TEST("Grapheme CR+LF sequence (GB3)");
    const char *text = "\r\n";
    // CR and LF together should be one grapheme
    size_t count = lle_utf8_count_graphemes(text, strlen(text));
    ASSERT_EQ(count, 1, "CR+LF is one grapheme");
    PASS();
}

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Spec 03: UTF-8 and Unicode TR#29 Compliance Tests\n");
    printf("=================================================\n\n");

    // UTF-8 Tests
    printf("UTF-8 Support Module Tests:\n");
    test_utf8_ascii();
    test_utf8_multibyte();
    test_utf8_validation();
    test_utf8_codepoint_counting();

    // Grapheme Cluster Tests
    printf("\nUnicode TR#29 Grapheme Detection Tests:\n");
    test_grapheme_ascii();
    test_grapheme_boundary_start();
    test_grapheme_cr_lf();

    // Summary
    printf("\n");
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    return (tests_failed == 0) ? 0 : 1;
}
