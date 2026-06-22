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
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This file uses an unusual test pattern: each test function manually
 * declares itself static void test_X(void) and calls TEST("string"),
 * PASS(), FAIL(...) inside its body. The framework's TEST(name)
 * macro defines a function — incompatible with the in-body usage —
 * so it is undef'd and the in-body calls are made no-ops. The
 * framework's RUN_TEST(name) still calls test_##name(), which
 * matches the function naming convention here, so the runner works.
 *
 * ASSERT_EQ/TRUE/STR_EQ match the framework's 3-arg signatures
 * exactly and the framework's versions take over via #undef. */
#undef TEST
#define TEST(name_str) ((void)0)
#define PASS() ((void)0)
#define FAIL(msg) TEST_FAIL_MSG(msg)

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
    /// cafe with accented e (U+00E9) -> CAFE with E-acute (U+00C9)
    const char *input = "caf\xC3\xA9"; /// cafe in UTF-8
    size_t len = lle_utf8_toupper(input, strlen(input), out, sizeof(out));
    ASSERT_TRUE(len != (size_t)-1, "Conversion should succeed");
    out[len] = '\0';
    ASSERT_STR_EQ(out, "CAF\xC3\x89", "Latin-1 accented to uppercase");
    PASS();
}

static void test_case_latin1_lower(void) {
    TEST("Latin-1 lowercase conversion (accented)");
    char out[32];
    /// CAFE with E-acute (U+00C9) -> cafe with e-acute (U+00E9)
    const char *input = "CAF\xC3\x89"; /// CAFE in UTF-8
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
    char out[3]; /// Too small for "HELLO"
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
    /// cafe with e-acute should equal itself
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
 * UNICODE SUBSTRING (CONTAINS) MATCHING TESTS
 * ============================================================================
 */

static void test_contains_ascii_middle(void) {
    TEST("ASCII contains - substring in the middle");
    ASSERT_TRUE(lle_unicode_contains("ell", 3, "hello", 5, NULL),
                "ell is contained in hello");
    ASSERT_TRUE(!lle_unicode_contains("xyz", 3, "hello", 5, NULL),
                "xyz is not contained in hello");
    PASS();
}

static void test_contains_ascii_endpoints(void) {
    TEST("ASCII contains - prefix and suffix substrings");
    ASSERT_TRUE(lle_unicode_contains("hel", 3, "hello", 5, NULL),
                "hel matches as prefix substring");
    ASSERT_TRUE(lle_unicode_contains("llo", 3, "hello", 5, NULL),
                "llo matches as suffix substring");
    ASSERT_TRUE(lle_unicode_contains("hello", 5, "hello", 5, NULL),
                "exact-length match counts as contained");
    PASS();
}

static void test_contains_empty_needle(void) {
    TEST("Contains - empty needle matches everything");
    ASSERT_TRUE(lle_unicode_contains("", 0, "hello", 5, NULL),
                "empty needle matches non-empty haystack");
    ASSERT_TRUE(lle_unicode_contains_z("", "hello", NULL),
                "empty needle (z) matches non-empty haystack");
    PASS();
}

static void test_contains_empty_haystack(void) {
    TEST("Contains - empty haystack rejects non-empty needle");
    ASSERT_TRUE(!lle_unicode_contains("a", 1, "", 0, NULL),
                "non-empty needle cannot be in empty haystack");
    PASS();
}

static void test_contains_needle_longer(void) {
    TEST("Contains - needle longer than haystack");
    ASSERT_TRUE(!lle_unicode_contains("hello world", 11, "hello", 5, NULL),
                "longer needle returns false");
    PASS();
}

static void test_contains_z_basic(void) {
    TEST("Null-terminated contains");
    ASSERT_TRUE(lle_unicode_contains_z("lo wo", "hello world", NULL),
                "lo wo is contained in hello world (z)");
    ASSERT_TRUE(!lle_unicode_contains_z("zzz", "hello world", NULL),
                "zzz is not contained in hello world (z)");
    PASS();
}

static void test_contains_case_insensitive(void) {
    TEST("Case-insensitive contains");
    lle_unicode_compare_options_t opts = LLE_UNICODE_COMPARE_DEFAULT;
    opts.case_insensitive = true;
    ASSERT_TRUE(lle_unicode_contains("ELL", 3, "hello", 5, &opts),
                "ELL matches hello case-insensitive");
    ASSERT_TRUE(lle_unicode_contains("ell", 3, "HELLO", 5, &opts),
                "ell matches HELLO case-insensitive");
    PASS();
}

static void test_contains_case_sensitive_strict(void) {
    TEST("Case-sensitive contains rejects cross-case match");
    lle_unicode_compare_options_t opts = LLE_UNICODE_COMPARE_DEFAULT;
    opts.case_insensitive = false;
    ASSERT_TRUE(!lle_unicode_contains("ELL", 3, "hello", 5, &opts),
                "ELL does not match hello case-sensitive");
    PASS();
}

static void test_contains_nfc_equivalence(void) {
    TEST("NFC-equivalent forms match under contains");
    /// Decomposed cafe (cafe + COMBINING ACUTE U+0301) versus
    /// precomposed cafe (U+00E9). The needle "afe-acute" spans the codepoint
    /// that differs between the two forms, so a match only succeeds once NFC
    /// normalization runs -- an ASCII-only needle would match trivially.
    const char *precomposed = "caf\xC3\xA9";
    const char *decomposed = "cafe\xCC\x81";
    ASSERT_TRUE(lle_unicode_contains_z("af\xC3\xA9", precomposed, NULL),
                "afe-acute contained in precomposed cafe");
    ASSERT_TRUE(lle_unicode_contains_z("af\xC3\xA9", decomposed, NULL),
                "afe-acute contained in decomposed cafe after NFC");
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
    /// e-acute (U+00E9) is already NFC
    const char *input = "caf\xC3\xA9";
    int result = lle_unicode_normalize_nfc(input, strlen(input), out,
                                           sizeof(out), &out_len);
    ASSERT_EQ(result, 0, "Normalization should succeed");
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, input, "Precomposed unchanged");
    PASS();
}

/* ============================================================================
 * NFC HEAP-ALLOC WRAPPER
 * ============================================================================
 */

static void test_nfc_alloc_null_input(void) {
    TEST("NFC alloc - NULL input returns NULL");
    char *got = lle_unicode_normalize_nfc_alloc(NULL);
    ASSERT_TRUE(got == NULL, "NULL in -> NULL out");
    PASS();
}

static void test_nfc_alloc_empty_string(void) {
    TEST("NFC alloc - empty string returns heap empty");
    char *got = lle_unicode_normalize_nfc_alloc("");
    ASSERT_TRUE(got != NULL, "empty in -> heap empty");
    ASSERT_STR_EQ(got, "", "value is empty");
    free(got);
    PASS();
}

static void test_nfc_alloc_ascii_fast_path(void) {
    TEST("NFC alloc - ASCII input is identity");
    char *got = lle_unicode_normalize_nfc_alloc("hello-world");
    ASSERT_TRUE(got != NULL, "alloc succeeded");
    ASSERT_STR_EQ(got, "hello-world", "ASCII unchanged");
    free(got);
    PASS();
}

static void test_nfc_alloc_precomposed_unchanged(void) {
    TEST("NFC alloc - precomposed e-acute round-trips");
    /// "café" with precomposed e-acute (U+00E9 = 0xC3 0xA9).
    const char *input = "caf\xC3\xA9";
    char *got = lle_unicode_normalize_nfc_alloc(input);
    ASSERT_TRUE(got != NULL, "alloc succeeded");
    ASSERT_STR_EQ(got, input, "precomposed NFC is identity");
    free(got);
    PASS();
}

static void test_nfc_alloc_decomposed_to_precomposed(void) {
    TEST("NFC alloc - decomposed e+acute folds to precomposed");
    /// NFD form: 'e' (0x65) + combining acute (U+0301 = 0xCC 0x81).
    const char *nfd_input = "cafe\xCC\x81";
    const char *nfc_expected = "caf\xC3\xA9";
    char *got = lle_unicode_normalize_nfc_alloc(nfd_input);
    ASSERT_TRUE(got != NULL, "alloc succeeded");
    ASSERT_STR_EQ(got, nfc_expected, "NFD folded to NFC");
    free(got);
    PASS();
}

static void test_nfc_alloc_invalid_utf8_fallback(void) {
    TEST("NFC alloc - invalid UTF-8 falls back to strdup");
    /// 0xC3 alone is the start of a 2-byte UTF-8 sequence but
    /// has no continuation byte: malformed. The wrapper must
    /// strdup the input so callers still get a usable string for
    /// byte-level comparison rather than NULL.
    const char *bad_input = "ab\xC3"
                            "xy";
    char *got = lle_unicode_normalize_nfc_alloc(bad_input);
    ASSERT_TRUE(got != NULL, "fallback strdup succeeded");
    ASSERT_TRUE(strcmp(got, bad_input) == 0, "invalid input passes through");
    free(got);
    PASS();
}

static void test_nfc_alloc_long_unicode_input(void) {
    TEST("NFC alloc - long mixed input survives sizing");
    /// Mix of ASCII + NFD repeats to exercise the heap size path
    /// (the inline ASCII fast path takes a different branch).
    char input[512];
    size_t len = 0;
    for (int i = 0; i < 40; i++) {
        memcpy(input + len, "cafe\xCC\x81 ", 7);
        len += 7;
    }
    input[len] = '\0';
    char *got = lle_unicode_normalize_nfc_alloc(input);
    ASSERT_TRUE(got != NULL, "alloc succeeded for long input");
    /// Each "cafe<combining-acute> " (7 bytes) becomes the precomposed
    /// "cafe-acute " (6 bytes) after NFC, so the result is the precomposed
    /// run repeated forty times -- not merely shorter.
    char expected[512];
    size_t elen = 0;
    for (int i = 0; i < 40; i++) {
        memcpy(expected + elen, "caf\xC3\xA9 ", 6);
        elen += 6;
    }
    expected[elen] = '\0';
    ASSERT_TRUE(strcmp(got, expected) == 0, "NFC output matches expected NFC");
    free(got);
    PASS();
}

/* ============================================================================
 * COMBINING CHARACTER TESTS
 * ============================================================================
 */

static void test_combining_class(void) {
    TEST("Combining class detection");
    /// Regular characters have class 0
    ASSERT_EQ(lle_unicode_combining_class('A'), 0, "A has class 0");
    ASSERT_EQ(lle_unicode_combining_class('a'), 0, "a has class 0");
    /// Combining acute accent (U+0301) has class 230
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
    printf("=== LLE Unicode Case Conversion and Comparison Tests ===\n");

    printf("Unicode Case Conversion:\n");
    RUN_TEST(case_ascii_upper);
    RUN_TEST(case_ascii_lower);
    RUN_TEST(case_mixed_ascii);
    RUN_TEST(case_latin1_upper);
    RUN_TEST(case_latin1_lower);
    RUN_TEST(case_codepoint_upper);
    RUN_TEST(case_codepoint_lower);
    RUN_TEST(case_is_upper_lower);
    RUN_TEST(case_first_upper);
    RUN_TEST(case_first_lower);
    RUN_TEST(case_empty_string);
    RUN_TEST(case_buffer_too_small);

    printf("Unicode String Comparison:\n");
    RUN_TEST(compare_equal_ascii);
    RUN_TEST(compare_case_sensitive);
    RUN_TEST(compare_case_insensitive);
    RUN_TEST(compare_with_length);
    RUN_TEST(compare_unicode_strings);

    printf("Unicode Prefix Matching:\n");
    RUN_TEST(prefix_ascii);
    RUN_TEST(prefix_empty);
    RUN_TEST(prefix_longer_than_string);
    RUN_TEST(prefix_null_terminated);
    RUN_TEST(prefix_case_insensitive);
    RUN_TEST(contains_ascii_middle);
    RUN_TEST(contains_ascii_endpoints);
    RUN_TEST(contains_empty_needle);
    RUN_TEST(contains_empty_haystack);
    RUN_TEST(contains_needle_longer);
    RUN_TEST(contains_z_basic);
    RUN_TEST(contains_case_insensitive);
    RUN_TEST(contains_case_sensitive_strict);
    RUN_TEST(contains_nfc_equivalence);

    printf("NFC Normalization:\n");
    RUN_TEST(nfc_ascii_passthrough);
    RUN_TEST(nfc_precomposed);

    printf("NFC Heap-Alloc Wrapper:\n");
    RUN_TEST(nfc_alloc_null_input);
    RUN_TEST(nfc_alloc_empty_string);
    RUN_TEST(nfc_alloc_ascii_fast_path);
    RUN_TEST(nfc_alloc_precomposed_unchanged);
    RUN_TEST(nfc_alloc_decomposed_to_precomposed);
    RUN_TEST(nfc_alloc_invalid_utf8_fallback);
    RUN_TEST(nfc_alloc_long_unicode_input);

    printf("Combining Characters:\n");
    RUN_TEST(combining_class);
    RUN_TEST(is_combining);

    return TEST_RESULT();
}
