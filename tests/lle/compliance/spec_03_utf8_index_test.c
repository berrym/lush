/**
 * Spec 03 UTF-8 Index Compliance Tests
 *
 * Verifies that UTF-8 index implementation complies with:
 * - Spec 03 Section 4: UTF-8 Unicode Support
 * - Fast O(1) position mapping
 * - Byte/codepoint/grapheme index conversions
 * - Index rebuild and invalidation
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include <stdio.h>
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
            printf("\n    Expected: %zu, Got: %zu - %s\n", (size_t)(b),        \
                   (size_t)(a), msg);                                          \
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

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("\n    Error code: %d - %s\n", (result), msg);              \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

// Test: UTF-8 index structure fields
static void test_utf8_index_structure() {
    TEST("UTF-8 index structure has all required fields");

    lle_utf8_index_t idx;
    memset(&idx, 0, sizeof(idx));

    // Verify all fields exist and can be set
    idx.byte_to_codepoint = NULL;
    idx.codepoint_to_byte = NULL;
    idx.grapheme_to_codepoint = NULL;
    idx.codepoint_to_grapheme = NULL;
    idx.byte_count = 0;
    idx.codepoint_count = 0;
    idx.grapheme_count = 0;
    idx.index_valid = false;
    idx.buffer_version = 0;
    idx.last_update_time = 0;
    idx.cache_hit_count = 0;
    idx.cache_miss_count = 0;

    ASSERT_TRUE(sizeof(idx.byte_to_codepoint) > 0,
                "byte_to_codepoint field exists");
    ASSERT_TRUE(sizeof(idx.codepoint_to_byte) > 0,
                "codepoint_to_byte field exists");
    ASSERT_TRUE(sizeof(idx.grapheme_to_codepoint) > 0,
                "grapheme_to_codepoint field exists");
    ASSERT_TRUE(sizeof(idx.codepoint_to_grapheme) > 0,
                "codepoint_to_grapheme field exists");
    ASSERT_TRUE(sizeof(idx.index_valid) > 0, "index_valid field exists");

    PASS();
}

// Test: UTF-8 index initialization
static void test_utf8_index_init() {
    TEST("UTF-8 index initialization");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);

    ASSERT_SUCCESS(result, "Index initialization succeeds");
    ASSERT_TRUE(index != NULL, "Index is allocated");
    ASSERT_TRUE(index->index_valid == false, "Index initially invalid");
    ASSERT_TRUE(index->byte_count == 0, "Byte count initially zero");
    ASSERT_TRUE(index->codepoint_count == 0, "Codepoint count initially zero");
    ASSERT_TRUE(index->grapheme_count == 0, "Grapheme count initially zero");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: UTF-8 index rebuild with ASCII text
static void test_utf8_index_rebuild_ascii() {
    TEST("UTF-8 index rebuild with ASCII text");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);
    ASSERT_SUCCESS(result, "Index initialization succeeds");

    const char *text = "Hello, World!";
    size_t text_length = strlen(text);

    result = lle_utf8_index_rebuild(index, text, text_length);
    ASSERT_SUCCESS(result, "Index rebuild succeeds");
    ASSERT_TRUE(index->index_valid, "Index is valid after rebuild");
    ASSERT_EQ(index->byte_count, text_length, "Byte count matches");
    ASSERT_EQ(index->codepoint_count, text_length,
              "Codepoint count equals byte count for ASCII");
    ASSERT_EQ(index->grapheme_count, text_length,
              "Grapheme count equals byte count for ASCII");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: UTF-8 index rebuild with multibyte UTF-8
static void test_utf8_index_rebuild_multibyte() {
    TEST("UTF-8 index rebuild with multibyte UTF-8");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);
    ASSERT_SUCCESS(result, "Index initialization succeeds");

    // "Hello 世界" - 2 Chinese characters (3 bytes each)
    const char *text = "Hello \xe4\xb8\x96\xe7\x95\x8c";
    size_t text_length = strlen(text);

    result = lle_utf8_index_rebuild(index, text, text_length);
    ASSERT_SUCCESS(result, "Index rebuild succeeds");
    ASSERT_TRUE(index->index_valid, "Index is valid after rebuild");
    ASSERT_EQ(index->byte_count, 12, "Byte count is 12 (6 ASCII + 6 UTF-8)");
    ASSERT_EQ(index->codepoint_count, 8,
              "Codepoint count is 8 (6 ASCII + 2 Chinese)");
    ASSERT_EQ(index->grapheme_count, 8, "Grapheme count is 8");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: Byte to codepoint lookup
static void test_byte_to_codepoint_lookup() {
    TEST("Byte to codepoint index lookup");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);
    ASSERT_SUCCESS(result, "Index initialization succeeds");

    const char *text = "Hello \xe4\xb8\x96\xe7\x95\x8c"; // "Hello 世界"
    result = lle_utf8_index_rebuild(index, text, strlen(text));
    ASSERT_SUCCESS(result, "Index rebuild succeeds");

    size_t codepoint_idx;

    // Byte 0 -> codepoint 0 (H)
    result = lle_utf8_index_byte_to_codepoint(index, 0, &codepoint_idx);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(codepoint_idx, 0, "Byte 0 maps to codepoint 0");

    // Byte 6 -> codepoint 6 (first byte of 世)
    result = lle_utf8_index_byte_to_codepoint(index, 6, &codepoint_idx);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(codepoint_idx, 6, "Byte 6 maps to codepoint 6");

    // Byte 7 -> codepoint 6 (second byte of 世, same codepoint)
    result = lle_utf8_index_byte_to_codepoint(index, 7, &codepoint_idx);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(codepoint_idx, 6,
              "Byte 7 maps to codepoint 6 (continuation byte)");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: Codepoint to byte lookup
static void test_codepoint_to_byte_lookup() {
    TEST("Codepoint to byte offset lookup");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);
    ASSERT_SUCCESS(result, "Index initialization succeeds");

    const char *text = "Hello \xe4\xb8\x96\xe7\x95\x8c"; // "Hello 世界"
    result = lle_utf8_index_rebuild(index, text, strlen(text));
    ASSERT_SUCCESS(result, "Index rebuild succeeds");

    size_t byte_offset;

    // Codepoint 0 -> byte 0
    result = lle_utf8_index_codepoint_to_byte(index, 0, &byte_offset);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(byte_offset, 0, "Codepoint 0 maps to byte 0");

    // Codepoint 6 -> byte 6 (first Chinese character)
    result = lle_utf8_index_codepoint_to_byte(index, 6, &byte_offset);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(byte_offset, 6, "Codepoint 6 maps to byte 6");

    // Codepoint 7 -> byte 9 (second Chinese character)
    result = lle_utf8_index_codepoint_to_byte(index, 7, &byte_offset);
    ASSERT_SUCCESS(result, "Lookup succeeds");
    ASSERT_EQ(byte_offset, 9, "Codepoint 7 maps to byte 9");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: Index invalidation
static void test_utf8_index_invalidate() {
    TEST("UTF-8 index invalidation");

    lle_utf8_index_t *index = NULL;
    lle_result_t result = lle_utf8_index_init(&index);
    ASSERT_SUCCESS(result, "Index initialization succeeds");

    const char *text = "Hello";
    result = lle_utf8_index_rebuild(index, text, strlen(text));
    ASSERT_SUCCESS(result, "Index rebuild succeeds");
    ASSERT_TRUE(index->index_valid, "Index is valid");

    result = lle_utf8_index_invalidate(index);
    ASSERT_SUCCESS(result, "Invalidation succeeds");
    ASSERT_TRUE(!index->index_valid, "Index is invalid after invalidation");

    // Lookups should fail on invalid index
    size_t codepoint_idx;
    result = lle_utf8_index_byte_to_codepoint(index, 0, &codepoint_idx);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_STATE,
                "Lookup fails on invalid index");

    lle_utf8_index_destroy(index);
    PASS();
}

// Test: Error handling - invalid parameters
static void test_error_handling() {
    TEST("UTF-8 index error handling");

    lle_result_t result;

    // NULL pointer to init
    result = lle_utf8_index_init(NULL);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Init rejects NULL pointer");

    // NULL pointer to destroy
    result = lle_utf8_index_destroy(NULL);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Destroy rejects NULL pointer");

    // NULL text to rebuild
    lle_utf8_index_t *index = NULL;
    lle_utf8_index_init(&index);
    result = lle_utf8_index_rebuild(index, NULL, 10);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_PARAMETER,
                "Rebuild rejects NULL text");

    lle_utf8_index_destroy(index);
    PASS();
}

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Spec 03: UTF-8 Index System Compliance Tests\n");
    printf("=================================================\n\n");

    // Structure Tests
    printf("UTF-8 Index Structure Tests:\n");
    test_utf8_index_structure();
    test_utf8_index_init();

    // Functional Tests
    printf("\nUTF-8 Index Rebuild Tests:\n");
    test_utf8_index_rebuild_ascii();
    test_utf8_index_rebuild_multibyte();

    // Lookup Tests
    printf("\nUTF-8 Index Lookup Tests:\n");
    test_byte_to_codepoint_lookup();
    test_codepoint_to_byte_lookup();

    // Management Tests
    printf("\nUTF-8 Index Management Tests:\n");
    test_utf8_index_invalidate();
    test_error_handling();

    // Summary
    printf("\n");
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
