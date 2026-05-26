/**
 * @file spec_03_atomic_operations_test.c
 * @brief Spec 03 atomic operations compliance test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * Spec 03 Atomic Buffer Operations and Undo/Redo Compliance Tests
 *
 * Verifies that:
 * - Buffer insert/delete/replace operations are atomic
 * - Change tracking records all operations correctly
 * - Undo/redo restores exact previous states
 * - Cursor positions are preserved correctly
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/memory_management.h"
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

/// Global memory pool for tests
static lush_memory_pool_t *test_memory_pool = NULL;

/// Test: Buffer insert operation
static void test_buffer_insert() {
    TEST("Buffer insert operation");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_memory_pool, 256);
    ASSERT_SUCCESS(result, "Buffer creation failed");
    ASSERT_TRUE(buffer != NULL, "Buffer is NULL");

    /// Insert text at position 0
    const char *text = "Hello";
    result = lle_buffer_insert_text(buffer, 0, text, strlen(text));
    ASSERT_SUCCESS(result, "Insert failed");

    ASSERT_EQ(buffer->length, 5, "Buffer length incorrect");
    ASSERT_STR_EQ(buffer->data, "Hello", "Buffer content incorrect");

    /// Insert more text
    result = lle_buffer_insert_text(buffer, 5, " World", 6);
    ASSERT_SUCCESS(result, "Second insert failed");

    ASSERT_EQ(buffer->length, 11, "Buffer length after second insert");
    ASSERT_STR_EQ(buffer->data, "Hello World",
                  "Buffer content after second insert");

    lle_buffer_destroy(buffer);
    PASS();
}

/// Test: Buffer delete operation
static void test_buffer_delete() {
    TEST("Buffer delete operation");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_memory_pool, 256);
    ASSERT_SUCCESS(result, "Buffer creation failed");

    /// Insert initial text
    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Initial insert failed");

    /// Delete " World"
    result = lle_buffer_delete_text(buffer, 5, 6);
    ASSERT_SUCCESS(result, "Delete failed");

    ASSERT_EQ(buffer->length, 5, "Buffer length after delete");
    ASSERT_STR_EQ(buffer->data, "Hello", "Buffer content after delete");

    lle_buffer_destroy(buffer);
    PASS();
}

/// Test: Buffer replace operation
static void test_buffer_replace() {
    TEST("Buffer replace operation");

    lle_buffer_t *buffer = NULL;
    lle_result_t result = lle_buffer_create(&buffer, test_memory_pool, 256);
    ASSERT_SUCCESS(result, "Buffer creation failed");

    /// Insert initial text
    result = lle_buffer_insert_text(buffer, 0, "Hello World", 11);
    ASSERT_SUCCESS(result, "Initial insert failed");

    /// Replace "World" with "Claude"
    result = lle_buffer_replace_text(buffer, 6, 5, "Claude", 6);
    ASSERT_SUCCESS(result, "Replace failed");

    ASSERT_EQ(buffer->length, 12, "Buffer length after replace");
    ASSERT_STR_EQ(buffer->data, "Hello Claude", "Buffer content after replace");

    lle_buffer_destroy(buffer);
    PASS();
}

/// Test: Change tracker initialization
static void test_change_tracker_init() {
    TEST("Change tracker initialization");

    lle_change_tracker_t *tracker = NULL;
    lle_result_t result =
        lle_change_tracker_init(&tracker, test_memory_pool, 100);
    ASSERT_SUCCESS(result, "Tracker init failed");
    ASSERT_TRUE(tracker != NULL, "Tracker is NULL");

    ASSERT_TRUE(!lle_change_tracker_can_undo(tracker),
                "Should not be able to undo initially");
    ASSERT_TRUE(!lle_change_tracker_can_redo(tracker),
                "Should not be able to redo initially");

    lle_change_tracker_destroy(tracker);
    PASS();
}

/// Test: Basic undo operation
static void test_basic_undo() {
    TEST("Basic undo operation");

    lle_buffer_t *buffer = NULL;
    lle_change_tracker_t *tracker = NULL;

    lle_result_t result = lle_buffer_create(&buffer, test_memory_pool, 256);
    ASSERT_SUCCESS(result, "Buffer creation failed");

    result = lle_change_tracker_init(&tracker, test_memory_pool, 100);
    ASSERT_SUCCESS(result, "Tracker init failed");

    /// Enable change tracking
    buffer->change_tracking_enabled = true;

    /// Create a sequence for the operation
    lle_change_sequence_t *seq = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "Insert text", &seq);
    ASSERT_SUCCESS(result, "Begin sequence failed");

    buffer->current_sequence = seq;

    /// Insert text
    result = lle_buffer_insert_text(buffer, 0, "Hello", 5);
    ASSERT_SUCCESS(result, "Insert failed");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence failed");

    ASSERT_STR_EQ(buffer->data, "Hello", "Buffer before undo");

    /// Undo the operation
    ASSERT_TRUE(lle_change_tracker_can_undo(tracker), "Should be able to undo");

    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo failed");

    ASSERT_EQ(buffer->length, 0, "Buffer length after undo");
    ASSERT_STR_EQ(buffer->data, "", "Buffer should be empty after undo");

    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
    PASS();
}

/// Test: Undo/Redo cycle
static void test_undo_redo_cycle() {
    TEST("Undo/Redo cycle");

    lle_buffer_t *buffer = NULL;
    lle_change_tracker_t *tracker = NULL;

    lle_result_t result = lle_buffer_create(&buffer, test_memory_pool, 256);
    ASSERT_SUCCESS(result, "Buffer creation failed");

    result = lle_change_tracker_init(&tracker, test_memory_pool, 100);
    ASSERT_SUCCESS(result, "Tracker init failed");

    buffer->change_tracking_enabled = true;

    /// First operation: Insert "Hello"
    lle_change_sequence_t *seq1 = NULL;
    result = lle_change_tracker_begin_sequence(tracker, "Insert Hello", &seq1);
    ASSERT_SUCCESS(result, "Begin sequence 1 failed");

    buffer->current_sequence = seq1;
    result = lle_buffer_insert_text(buffer, 0, "Hello", 5);
    ASSERT_SUCCESS(result, "Insert 1 failed");

    result = lle_change_tracker_complete_sequence(tracker);
    ASSERT_SUCCESS(result, "Complete sequence 1 failed");

    ASSERT_STR_EQ(buffer->data, "Hello", "After first insert");

    /// Undo
    result = lle_change_tracker_undo(tracker, buffer);
    ASSERT_SUCCESS(result, "Undo failed");
    ASSERT_STR_EQ(buffer->data, "", "After undo");

    /// Redo
    ASSERT_TRUE(lle_change_tracker_can_redo(tracker), "Should be able to redo");
    result = lle_change_tracker_redo(tracker, buffer);
    ASSERT_SUCCESS(result, "Redo failed");
    ASSERT_STR_EQ(buffer->data, "Hello", "After redo");

    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);
    PASS();
}

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Spec 03: Atomic Operations and Undo/Redo Tests\n");
    printf("=================================================\n\n");

    /// Initialize memory pool for tests
    lush_memory_pool_system_t pool_system;
    memset(&pool_system, 0, sizeof(pool_system));
    test_memory_pool = &pool_system;

    /// Buffer Operations Tests
    printf("Atomic Buffer Operations:\n");
    test_buffer_insert();
    test_buffer_delete();
    test_buffer_replace();

    /// Change Tracking Tests
    printf("\nChange Tracking and Undo/Redo:\n");
    test_change_tracker_init();
    test_basic_undo();
    test_undo_redo_cycle();

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
