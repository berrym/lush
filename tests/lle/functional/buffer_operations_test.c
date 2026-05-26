/**
 * LLE Buffer Operations Functional Tests
 *
 * Comprehensive test suite for buffer operations that actually USE the
 * buffer management system, not just verify structure definitions.
 *
 * Tests:
 * - Buffer lifecycle (create, destroy, clear)
 * - Basic operations (insert, delete, replace)
 * - UTF-8 handling
 * - Cursor tracking
 * - Change tracking integration
 * - Complex operation sequences
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Global memory pool for tests
static lush_memory_pool_t *test_pool = NULL;

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

#define ASSERT_STR_EQ(a, b, msg)                                               \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("\n    Expected: '%s', Got: '%s' - %s\n", (b), (a), msg);   \
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

/* ============================================================================
 * BUFFER LIFECYCLE TESTS
 * ============================================================================
 */

static void test_buffer_create_destroy() {
    TEST("Buffer create and destroy");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);

    ASSERT_SUCCESS(result, "Buffer creation succeeds");
    ASSERT_TRUE(buffer != NULL, "Buffer is allocated");
    ASSERT_TRUE(buffer->data != NULL, "Buffer data is allocated");
    ASSERT_TRUE(buffer->capacity >= LLE_BUFFER_MIN_CAPACITY,
                "Buffer has minimum capacity");
    ASSERT_EQ(buffer->length, 0, "Buffer length is 0");
    ASSERT_TRUE(buffer->data[0] == '\0', "Buffer is null-terminated");

    result = lle_buffer_destroy(buffer);
    ASSERT_SUCCESS(result, "Buffer destruction succeeds");

    PASS();
}

static void test_buffer_create_with_capacity() {
    TEST("Buffer create with custom capacity");

    lle_buffer_t *buffer = NULL;
    size_t requested_capacity = 2048;
    lle_result_t result =
        lle_buffer_create(&buffer, test_pool, requested_capacity);

    ASSERT_SUCCESS(result, "Buffer creation succeeds");
    ASSERT_TRUE(buffer->capacity >= requested_capacity,
                "Buffer has requested capacity");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_buffer_clear() {
    TEST("Buffer clear operation");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Insert some text
    const char *text = "Hello, World!";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Text insertion succeeds");
    ASSERT_EQ(buffer->length, strlen(text), "Buffer has text");

    /// Clear buffer
    result = lle_buffer_clear(buffer);
    ASSERT_SUCCESS(result, "Buffer clear succeeds");
    ASSERT_EQ(buffer->length, 0, "Buffer length is 0");
    ASSERT_TRUE(buffer->data[0] == '\0', "Buffer is null-terminated");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * BASIC OPERATION TESTS
 * ============================================================================
 */

static void test_insert_text_at_start() {
    TEST("Insert text at buffer start");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    const char *text = "Hello";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    ASSERT_EQ(buffer->length, strlen(text), "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "Hello", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_insert_text_at_end() {
    TEST("Insert text at buffer end");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello", 5);
    ASSERT_SUCCESS(result, "First insertion succeeds");

    result = lle_buffer_insert_text(buffer, 5, " World", 6);
    ASSERT_SUCCESS(result, "Second insertion succeeds");

    ASSERT_EQ(buffer->length, 11, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "Hello World", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_insert_text_in_middle() {
    TEST("Insert text in buffer middle");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "HelloWorld", 10);
    ASSERT_SUCCESS(result, "First insertion succeeds");

    result = lle_buffer_insert_text(buffer, 5, " ", 1);
    ASSERT_SUCCESS(result, "Middle insertion succeeds");

    ASSERT_EQ(buffer->length, 11, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "Hello World", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_delete_text_from_start() {
    TEST("Delete text from buffer start");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_delete_text(buffer, 0, 6);
    ASSERT_SUCCESS(result, "Text deletion succeeds");

    ASSERT_EQ(buffer->length, 5, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "World", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_delete_text_from_end() {
    TEST("Delete text from buffer end");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "Text deletion succeeds");

    ASSERT_EQ(buffer->length, 5, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "Hello", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_delete_text_from_middle() {
    TEST("Delete text from buffer middle");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_delete_text(buffer, 5, 1);
    ASSERT_SUCCESS(result, "Text deletion succeeds");

    ASSERT_EQ(buffer->length, 10, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "HelloWorld", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_replace_text() {
    TEST("Replace text in buffer");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_replace_text(buffer, 6, 5, "Earth", 5);
    ASSERT_SUCCESS(result, "Text replacement succeeds");

    ASSERT_EQ(buffer->length, 11, "Buffer length correct");
    ASSERT_STR_EQ(buffer->data, "Hello Earth", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * UTF-8 HANDLING TESTS
 * ============================================================================
 */

static void test_insert_utf8_text() {
    TEST("Insert UTF-8 text (multibyte characters)");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Insert text with Chinese characters
    const char *text = "Hello 世界";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "UTF-8 text insertion succeeds");

    ASSERT_EQ(buffer->length, 12,
              "Buffer length correct (6 ASCII + 6 UTF-8 bytes)");
    ASSERT_STR_EQ(buffer->data, "Hello 世界", "Buffer content correct");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_reject_invalid_utf8() {
    TEST("Reject invalid UTF-8 sequences");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    /// Try to insert invalid UTF-8
    char invalid_utf8[3] = {(char)0xFF, (char)0xFF, '\0'};
    result = lle_buffer_insert_text(buffer, 0, invalid_utf8, 2);

    ASSERT_TRUE(result == LLE_ERROR_INVALID_ENCODING, "Invalid UTF-8 rejected");
    ASSERT_EQ(buffer->length, 0, "Buffer remains empty");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * COMPLEX SEQUENCE TESTS
 * ============================================================================
 */

static void test_multiple_insertions() {
    TEST("Multiple consecutive insertions");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "a", 1);
    ASSERT_SUCCESS(result, "Insert 'a'");

    result = lle_buffer_insert_text(buffer, 1, "b", 1);
    ASSERT_SUCCESS(result, "Insert 'b'");

    result = lle_buffer_insert_text(buffer, 2, "c", 1);
    ASSERT_SUCCESS(result, "Insert 'c'");

    ASSERT_STR_EQ(buffer->data, "abc",
                  "Content correct after multiple insertions");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_insert_delete_sequence() {
    TEST("Insert-delete-insert sequence");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Initial insert");

    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "Delete middle");

    result = lle_buffer_insert_text(buffer, 5, " Earth", 6);
    ASSERT_SUCCESS(result, "Insert replacement");

    ASSERT_STR_EQ(buffer->data, "Hello Earth",
                  "Content correct after sequence");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_buffer_growth() {
    TEST("Buffer automatic growth");

    lle_buffer_t *buffer = NULL;
    lle_result_t result =
        lle_buffer_create(&buffer, test_pool, LLE_BUFFER_MIN_CAPACITY);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    size_t initial_capacity = buffer->capacity;

    /// Insert text larger than initial capacity
    char large_text[1024];
    memset(large_text, 'A', 1023);
    large_text[1023] = '\0';

    result = lle_buffer_insert_text(buffer, 0, large_text, 1023);
    ASSERT_SUCCESS(result, "Large text insertion succeeds");

    ASSERT_TRUE(buffer->capacity > initial_capacity, "Buffer capacity grew");
    ASSERT_EQ(buffer->length, 1023, "Buffer length correct");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

static void test_insert_out_of_bounds() {
    TEST("Insert at invalid position");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 100, "test", 4);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_RANGE,
                "Out of bounds insert rejected");

    lle_buffer_destroy(buffer);
    PASS();
}

static void test_delete_out_of_bounds() {
    TEST("Delete at invalid position");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_pool, 0);
    ASSERT_SUCCESS(result, "Buffer creation succeeds");

    result = lle_buffer_insert_text(buffer, 0, "Hello", 5);
    ASSERT_SUCCESS(result, "Text insertion succeeds");

    result = lle_buffer_delete_text(buffer, 10, 5);
    ASSERT_TRUE(result == LLE_ERROR_INVALID_RANGE,
                "Out of bounds delete rejected");

    lle_buffer_destroy(buffer);
    PASS();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("LLE Buffer Operations Functional Tests\n");
    printf("=================================================\n\n");

    /// Initialize test memory pool (mock implementation uses malloc/free)
    test_pool = global_memory_pool;

    /// Buffer Lifecycle Tests
    printf("Buffer Lifecycle Tests:\n");
    test_buffer_create_destroy();
    test_buffer_create_with_capacity();
    test_buffer_clear();

    /// Basic Operation Tests
    printf("\nBasic Operation Tests:\n");
    test_insert_text_at_start();
    test_insert_text_at_end();
    test_insert_text_in_middle();
    test_delete_text_from_start();
    test_delete_text_from_end();
    test_delete_text_from_middle();
    test_replace_text();

    /// UTF-8 Handling Tests
    printf("\nUTF-8 Handling Tests:\n");
    test_insert_utf8_text();
    test_reject_invalid_utf8();

    /// Complex Sequence Tests
    printf("\nComplex Sequence Tests:\n");
    test_multiple_insertions();
    test_insert_delete_sequence();
    test_buffer_growth();

    /// Error Handling Tests
    printf("\nError Handling Tests:\n");
    test_insert_out_of_bounds();
    test_delete_out_of_bounds();

    /// Summary
    printf("\n");
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
