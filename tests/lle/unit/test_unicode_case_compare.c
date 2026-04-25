/**
 * @file test_unicode_case_compare.c
 * @brief Unit tests for LLE Unicode case conversion and comparison functions
 *
 * Tests the unicode_case.h and unicode_compare.h APIs:
 * - Case conversion: lle_utf8_toupper, lle_utf8_tolower, codepoint functions
 * - String comparison: lle_unicode_strings_equal, lle_unicode_is_prefix
 * - NFC normalization: lle_unicode_normalize_nfc
 */

#include "lle/unicode_case.h"
#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"
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

#define ASSERT_STR_EQ(a, b, msg)                                               \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("expected '%s', got '%s' - ", (b), (a));                    \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

/* ============================================================================
 * UNICODE CASE CONVERSION TESTS
 * ============================================================================
 */

static void test_case_ascii_upper(void) {
    TEST("ASCII uppercase conversion");
    char out[32];
    size_t len = lle_utf8_toupper("hello", 5, out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "HELLO", "ASCII lowercase to uppercase");
    PASS();
}

static void test_case_ascii_lower(void) {
    TEST("ASCII lowercase conversion");
    char out[32];
    size_t len = lle_utf8_tolower("WORLD", 5, out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "world", "ASCII uppercase to lowercase");
    PASS();
}

static void test_case_mixed_ascii(void) {
    TEST("Mixed ASCII case conversion");
    char out[32];
    size_t len = lle_utf8_toupper("HeLLo WoRLD", 11, out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "HELLO WORLD", "Mixed case to uppercase");
    PASS();
}

static void test_case_latin1_upper(void) {
    TEST("Latin-1 uppercase conversion (accented)");
    char out[32];
    /* cafe with accented e (U+00E9) -> CAFE with E-acute (U+00C9) */
    const char *input = "caf\xC3\xA9"; /* cafe in UTF-8 */
    size_t len = lle_utf8_toupper(input, strlen(input), out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "CAF\xC3\x89", "Latin-1 accented to uppercase");
    PASS();
}

static void test_case_latin1_lower(void) {
    TEST("Latin-1 lowercase conversion (accented)");
    char out[32];
    /* CAFE with E-acute (U+00C9) -> cafe with e-acute (U+00E9) */
    const char *input = "CAF\xC3\x89"; /* CAFE in UTF-8 */
    size_t len = lle_utf8_tolower(input, strlen(input), out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "caf\xC3\xA9", "Latin-1 accented to lowercase");
    PASS();
}

static void test_case_codepoint_upper(void) {
    TEST("Codepoint uppercase conversion");
    ASSERT_EQ(lle_unicode_toupper_codepoint('a'), 'A', "a -> A");
    ASSERT_EQ(lle_unicode_toupper_codepoint('z'), 'Z', "z -> Z");
    ASSERT_EQ(lle_unicode_toupper_codepoint('A'), 'A', "A unchanged");
    ASSERT_EQ(lle_unicode_toupper_codepoint('5'), '5', "digit unchanged");
    ASSERT_EQ(lle_unicode_toupper_codepoint(0x00E9), 0x00C9,
              "e-acute -> E-acute");
    PASS();
}

static void test_case_codepoint_lower(void) {
    TEST("Codepoint lowercase conversion");
    ASSERT_EQ(lle_unicode_tolower_codepoint('A'), 'a', "A -> a");
    ASSERT_EQ(lle_unicode_tolower_codepoint('Z'), 'z', "Z -> z");
    ASSERT_EQ(lle_unicode_tolower_codepoint('a'), 'a', "a unchanged");
    ASSERT_EQ(lle_unicode_tolower_codepoint('5'), '5', "digit unchanged");
    ASSERT_EQ(lle_unicode_tolower_codepoint(0x00C9), 0x00E9,
              "E-acute -> e-acute");
    PASS();
}

static void test_case_is_upper_lower(void) {
    TEST("Case classification functions");
    ASSERT_TRUE(lle_unicode_is_upper('A'), "A is uppercase");
    ASSERT_TRUE(lle_unicode_is_upper('Z'), "Z is uppercase");
    ASSERT_TRUE(!lle_unicode_is_upper('a'), "a is not uppercase");
    ASSERT_TRUE(lle_unicode_is_lower('a'), "a is lowercase");
    ASSERT_TRUE(lle_unicode_is_lower('z'), "z is lowercase");
    ASSERT_TRUE(!lle_unicode_is_lower('A'), "A is not lowercase");
    ASSERT_TRUE(!lle_unicode_is_upper('5'), "5 is not uppercase");
    ASSERT_TRUE(!lle_unicode_is_lower('5'), "5 is not lowercase");
    PASS();
}

static void test_case_first_upper(void) {
    TEST("First character uppercase");
    char out[32];
    size_t len = lle_utf8_toupper_first("hello", 5, out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "Hello", "First char uppercase only");
    PASS();
}

static void test_case_first_lower(void) {
    TEST("First character lowercase");
    char out[32];
    size_t len = lle_utf8_tolower_first("HELLO", 5, out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "hELLO", "First char lowercase only");
    PASS();
}

static void test_case_empty_string(void) {
    TEST("Empty string case conversion");
    char out[32];
    size_t len = lle_utf8_toupper("", 0, out, sizeof(out));
    ASSERT_EQ(len, 0, "Empty string returns 0 length");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "", "Empty output");
    PASS();
}

static void test_case_buffer_too_small(void) {
    TEST("Buffer too small returns error");
    char out[3]; /* Too small for "HELLO" */
    size_t len = lle_utf8_toupper("hello", 5, out, sizeof(out));
    ASSERT_EQ(len, (size_t)-1, "Should return error for small buffer");
    PASS();
}

/* ============================================================================
 * UNICODE STRING COMPARISON TESTS
 * ============================================================================
 */

static void test_compare_equal_ascii(void) {
    TEST("ASCII string equality");
    ASSERT_TRUE(lle_unicode_strings_equal("hello", "hello", NULL),
                "Identical strings equal");
    ASSERT_TRUE(!lle_unicode_strings_equal("hello", "world", NULL),
                "Different strings not equal");
    PASS();
}

static void test_compare_case_sensitive(void) {
    TEST("Case-sensitive comparison (default)");
    ASSERT_TRUE(!lle_unicode_strings_equal("Hello", "hello", NULL),
                "Different case not equal by default");
    ASSERT_TRUE(lle_unicode_strings_equal("Hello", "Hello", NULL),
                "Same case equal");
    PASS();
}

static void test_compare_case_insensitive(void) {
    TEST("Case-insensitive comparison");
    lle_unicode_compare_options_t opts = LLE_UNICODE_COMPARE_DEFAULT;
    opts.case_insensitive = true;
    ASSERT_TRUE(lle_unicode_strings_equal("Hello", "hello", &opts),
                "Case insensitive: Hello == hello");
    ASSERT_TRUE(lle_unicode_strings_equal("WORLD", "world", &opts),
                "Case insensitive: WORLD == world");
    ASSERT_TRUE(!lle_unicode_strings_equal("hello", "world", &opts),
                "Different words still not equal");
    PASS();
}

static void test_compare_with_length(void) {
    TEST("String comparison with length");
    ASSERT_TRUE(lle_unicode_strings_equal_n("hello world", 5, "hello", 5, NULL),
                "First 5 chars match");
    ASSERT_TRUE(!lle_unicode_strings_equal_n("hello", 5, "help", 4, NULL),
                "Different lengths");
    PASS();
}

static void test_compare_unicode_strings(void) {
    TEST("Unicode string comparison");
    /* cafe with e-acute should equal itself */
    const char *cafe = "caf\xC3\xA9";
    ASSERT_TRUE(lle_unicode_strings_equal(cafe, cafe, NULL),
                "Unicode string equals itself");
    PASS();
}

/* ============================================================================
 * UNICODE PREFIX MATCHING TESTS
 * ============================================================================
 */

static void test_prefix_ascii(void) {
    TEST("ASCII prefix matching");
    ASSERT_TRUE(lle_unicode_is_prefix("hel", 3, "hello", 5, NULL),
                "hel is prefix of hello");
    ASSERT_TRUE(lle_unicode_is_prefix("hello", 5, "hello", 5, NULL),
                "hello is prefix of hello (exact match)");
    ASSERT_TRUE(!lle_unicode_is_prefix("help", 4, "hello", 5, NULL),
                "help is not prefix of hello");
    PASS();
}

static void test_prefix_empty(void) {
    TEST("Empty prefix matching");
    ASSERT_TRUE(lle_unicode_is_prefix("", 0, "hello", 5, NULL),
                "Empty string is prefix of anything");
    PASS();
}

static void test_prefix_longer_than_string(void) {
    TEST("Prefix longer than string");
    ASSERT_TRUE(!lle_unicode_is_prefix("hello world", 11, "hello", 5, NULL),
                "Longer prefix returns false");
    PASS();
}

static void test_prefix_null_terminated(void) {
    TEST("Null-terminated prefix matching");
    ASSERT_TRUE(lle_unicode_is_prefix_z("hel", "hello", NULL),
                "hel is prefix of hello (z version)");
    ASSERT_TRUE(!lle_unicode_is_prefix_z("world", "hello", NULL),
                "world is not prefix of hello");
    PASS();
}

static void test_prefix_case_insensitive(void) {
    TEST("Case-insensitive prefix matching");
    lle_unicode_compare_options_t opts = LLE_UNICODE_COMPARE_DEFAULT;
    opts.case_insensitive = true;
    ASSERT_TRUE(lle_unicode_is_prefix("HEL", 3, "hello", 5, &opts),
                "HEL matches hello case-insensitive");
    ASSERT_TRUE(lle_unicode_is_prefix("hel", 3, "HELLO", 5, &opts),
                "hel matches HELLO case-insensitive");
    PASS();
}

/* ============================================================================
 * NFC NORMALIZATION TESTS
 * ============================================================================
 */

static void test_nfc_ascii_passthrough(void) {
    TEST("NFC normalization - ASCII passthrough");
    char out[32];
    size_t out_len;
    int result =
        lle_unicode_normalize_nfc("hello", 5, out, sizeof(out), &out_len);
    ASSERT_EQ(result, 0, "Normalization should succeed");
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello", "ASCII unchanged after NFC");
    PASS();
}

static void test_nfc_precomposed(void) {
    TEST("NFC normalization - precomposed characters");
    char out[32];
    size_t out_len;
    /* e-acute (U+00E9) is already NFC */
    const char *input = "caf\xC3\xA9";
    int result = lle_unicode_normalize_nfc(input, strlen(input), out,
                                           sizeof(out), &out_len);
    ASSERT_EQ(result, 0, "Normalization should succeed");
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, input, "Precomposed unchanged");
    PASS();
}

/* ============================================================================
 * COMBINING CHARACTER TESTS
 * ============================================================================
 */

static void test_combining_class(void) {
    TEST("Combining class detection");
    /* Regular characters have class 0 */
    ASSERT_EQ(lle_unicode_combining_class('A'), 0, "A has class 0");
    ASSERT_EQ(lle_unicode_combining_class('a'), 0, "a has class 0");
    /* Combining acute accent (U+0301) has class 230 */
    ASSERT_EQ(lle_unicode_combining_class(0x0301), 230,
              "Combining acute has class 230");
    PASS();
}

static void test_is_combining(void) {
    TEST("Combining character detection");
    ASSERT_TRUE(!lle_unicode_is_combining('A'), "A is not combining");
    ASSERT_TRUE(!lle_unicode_is_combining('a'), "a is not combining");
    ASSERT_TRUE(lle_unicode_is_combining(0x0301), "U+0301 is combining");
    ASSERT_TRUE(lle_unicode_is_combining(0x0300), "U+0300 is combining");
    PASS();
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("\n");
    printf("=====================================================\n");
    printf("LLE Unicode Case Conversion and Comparison Tests\n");
    printf("=====================================================\n\n");

    /* Case Conversion Tests */
    printf("Unicode Case Conversion Tests:\n");
    test_case_ascii_upper();
    test_case_ascii_lower();
    test_case_mixed_ascii();
    test_case_latin1_upper();
    test_case_latin1_lower();
    test_case_codepoint_upper();
    test_case_codepoint_lower();
    test_case_is_upper_lower();
    test_case_first_upper();
    test_case_first_lower();
    test_case_empty_string();
    test_case_buffer_too_small();

    /* String Comparison Tests */
    printf("\nUnicode String Comparison Tests:\n");
    test_compare_equal_ascii();
    test_compare_case_sensitive();
    test_compare_case_insensitive();
    test_compare_with_length();
    test_compare_unicode_strings();

    /* Prefix Matching Tests */
    printf("\nUnicode Prefix Matching Tests:\n");
    test_prefix_ascii();
    test_prefix_empty();
    test_prefix_longer_than_string();
    test_prefix_null_terminated();
    test_prefix_case_insensitive();

    /* NFC Normalization Tests */
    printf("\nNFC Normalization Tests:\n");
    test_nfc_ascii_passthrough();
    test_nfc_precomposed();

    /* Combining Character Tests */
    printf("\nCombining Character Tests:\n");
    test_combining_class();
    test_is_combining();

    /* Summary */
    printf("\n");
    printf("=====================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=====================================================\n");

    return (tests_failed == 0) ? 0 : 1;
}
