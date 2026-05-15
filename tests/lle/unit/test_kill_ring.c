/**
 * test_kill_ring.c - Unit tests for kill ring system
 *
 * Tests GNU Readline compatible kill/yank operations.
 *
 * Date: 2025-11-02
 */

#include "lle/kill_ring.h"
#include "test_framework.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * BASIC LIFECYCLE TESTS
 * ============================================================================
 */

TEST(create_destroy) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    /* Create with default size */
    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create with default size failed");
    ASSERT(ring != NULL, "Ring is NULL after creation");

    /* Verify capacity is default */
    size_t capacity;
    result = lle_kill_ring_get_capacity(ring, &capacity);
    ASSERT(result == LLE_SUCCESS, "Get capacity failed");
    ASSERT_EQ(capacity, LLE_KILL_RING_DEFAULT_SIZE,
              "Capacity not default size");

    /* Verify empty */
    bool empty;
    result = lle_kill_ring_is_empty(ring, &empty);
    ASSERT(result == LLE_SUCCESS, "Is empty check failed");
    ASSERT(empty, "New ring not empty");

    /* Destroy */
    result = lle_kill_ring_destroy(ring);
    ASSERT(result == LLE_SUCCESS, "Destroy failed");
}

TEST(create_custom_size) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    /* Create with custom size */
    result = lle_kill_ring_create(&ring, 64, NULL);
    ASSERT(result == LLE_SUCCESS, "Create with custom size failed");

    size_t capacity;
    result = lle_kill_ring_get_capacity(ring, &capacity);
    ASSERT(result == LLE_SUCCESS, "Get capacity failed");
    ASSERT_EQ(capacity, 64, "Capacity not custom size");

    lle_kill_ring_destroy(ring);
}

TEST(create_max_size_clamping) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    /* Create with size exceeding max */
    result = lle_kill_ring_create(&ring, 1000, NULL);
    ASSERT(result == LLE_SUCCESS, "Create with excessive size failed");

    size_t capacity;
    result = lle_kill_ring_get_capacity(ring, &capacity);
    ASSERT(result == LLE_SUCCESS, "Get capacity failed");
    ASSERT_EQ(capacity, LLE_KILL_RING_MAX_SIZE, "Capacity not clamped to max");

    lle_kill_ring_destroy(ring);
}

TEST(null_pointer_checks) {
    lle_result_t result;

    /* Create with NULL ring pointer */
    result = lle_kill_ring_create(NULL, 0, NULL);
    ASSERT(result == LLE_ERROR_NULL_POINTER,
           "Create accepted NULL ring pointer");

    /* Destroy with NULL */
    result = lle_kill_ring_destroy(NULL);
    ASSERT(result == LLE_ERROR_NULL_POINTER, "Destroy accepted NULL");
}

/* ============================================================================
 * KILL OPERATION TESTS
 * ============================================================================
 */

TEST(add_single_kill) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add text */
    result = lle_kill_ring_add(ring, "hello", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    /* Verify count */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 1, "Count not 1 after add");

    /* Verify not empty */
    bool empty;
    result = lle_kill_ring_is_empty(ring, &empty);
    ASSERT(result == LLE_SUCCESS, "Is empty check failed");
    ASSERT(!empty, "Ring empty after add");

    lle_kill_ring_destroy(ring);
}

TEST(add_multiple_kills) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add three separate kills */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add first failed");

    result = lle_kill_ring_add(ring, "second", false);
    ASSERT(result == LLE_SUCCESS, "Add second failed");

    result = lle_kill_ring_add(ring, "third", false);
    ASSERT(result == LLE_SUCCESS, "Add third failed");

    /* Verify count */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 3, "Count not 3 after three adds");

    lle_kill_ring_destroy(ring);
}

TEST(add_with_append) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add initial text */
    result = lle_kill_ring_add(ring, "hello", false);
    ASSERT(result == LLE_SUCCESS, "Add initial failed");

    /* Append to same entry */
    result = lle_kill_ring_add(ring, " world", true);
    ASSERT(result == LLE_SUCCESS, "Add append failed");

    /* Verify still one entry */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 1, "Count not 1 after append");

    /* Verify text is concatenated */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Get current failed");
    ASSERT_STR_EQ(text, "hello world", "Appended text incorrect");

    lle_kill_ring_destroy(ring);
}

TEST(add_successive_kills_with_append) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Simulate C-k, C-k, C-k (successive kills) */
    result = lle_kill_ring_add(ring, "line1", false);
    ASSERT(result == LLE_SUCCESS, "Add line1 failed");

    result = lle_kill_ring_add(ring, "\n", true);
    ASSERT(result == LLE_SUCCESS, "Append newline failed");

    result = lle_kill_ring_add(ring, "line2", true);
    ASSERT(result == LLE_SUCCESS, "Append line2 failed");

    /* Verify single entry */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 1, "Count not 1 after successive kills");

    /* Verify accumulated text */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Get current failed");
    ASSERT_STR_EQ(text, "line1\nline2", "Accumulated kill text incorrect");

    lle_kill_ring_destroy(ring);
}

TEST(prepend_operation) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add initial text */
    result = lle_kill_ring_add(ring, "world", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    /* Prepend */
    result = lle_kill_ring_prepend(ring, "hello ");
    ASSERT(result == LLE_SUCCESS, "Prepend failed");

    /* Verify text order */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Get current failed");
    ASSERT_STR_EQ(text, "hello world", "Prepended text incorrect");

    lle_kill_ring_destroy(ring);
}

TEST(circular_buffer_overflow) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    /* Create small ring */
    result = lle_kill_ring_create(&ring, 3, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add more entries than capacity */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add 1 failed");

    result = lle_kill_ring_add(ring, "second", false);
    ASSERT(result == LLE_SUCCESS, "Add 2 failed");

    result = lle_kill_ring_add(ring, "third", false);
    ASSERT(result == LLE_SUCCESS, "Add 3 failed");

    result = lle_kill_ring_add(ring, "fourth", false);
    ASSERT(result == LLE_SUCCESS, "Add 4 failed");

    /* Count should be capped at capacity */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 3, "Count exceeded capacity");

    /* Most recent should be accessible */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Get current failed");
    ASSERT_STR_EQ(text, "fourth", "Most recent kill incorrect after overflow");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * YANK OPERATION TESTS
 * ============================================================================
 */

TEST(yank_from_empty_ring) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Try to yank from empty ring */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_ERROR_QUEUE_EMPTY,
           "Yank from empty didn't return QUEUE_EMPTY error");

    lle_kill_ring_destroy(ring);
}

TEST(yank_single_entry) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add and yank */
    result = lle_kill_ring_add(ring, "test text", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Yank failed");
    ASSERT_STR_EQ(text, "test text", "Yanked text incorrect");

    lle_kill_ring_destroy(ring);
}

TEST(yank_returns_most_recent) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add multiple entries */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add first failed");

    result = lle_kill_ring_add(ring, "second", false);
    ASSERT(result == LLE_SUCCESS, "Add second failed");

    result = lle_kill_ring_add(ring, "third", false);
    ASSERT(result == LLE_SUCCESS, "Add third failed");

    /* Yank should return most recent */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Yank failed");
    ASSERT_STR_EQ(text, "third", "Yank didn't return most recent");

    lle_kill_ring_destroy(ring);
}

TEST(yank_pop_without_yank_fails) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    result = lle_kill_ring_add(ring, "test", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    /* Try yank-pop without yank first */
    const char *text;
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_ERROR_INVALID_STATE,
           "Yank-pop without yank didn't fail");

    lle_kill_ring_destroy(ring);
}

TEST(yank_pop_cycles_through_ring) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add three entries */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add first failed");

    result = lle_kill_ring_add(ring, "second", false);
    ASSERT(result == LLE_SUCCESS, "Add second failed");

    result = lle_kill_ring_add(ring, "third", false);
    ASSERT(result == LLE_SUCCESS, "Add third failed");

    /* Initial yank */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Initial yank failed");
    ASSERT_STR_EQ(text, "third", "Initial yank incorrect");

    /* Yank-pop should give second */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "First yank-pop failed");
    ASSERT_STR_EQ(text, "second", "First yank-pop incorrect");

    /* Yank-pop should give first */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Second yank-pop failed");
    ASSERT_STR_EQ(text, "first", "Second yank-pop incorrect");

    /* Yank-pop should wrap to third */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Third yank-pop (wrap) failed");
    ASSERT_STR_EQ(text, "third", "Wrap-around yank-pop incorrect");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * STATE MANAGEMENT TESTS
 * ============================================================================
 */

TEST(reset_yank_state) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    result = lle_kill_ring_add(ring, "test", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    /* Yank to set yank state */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Yank failed");

    /* Verify yank state set */
    bool was_yank;
    result = lle_kill_ring_was_last_yank(ring, &was_yank);
    ASSERT(result == LLE_SUCCESS, "Check yank state failed");
    ASSERT(was_yank, "Yank state not set after yank");

    /* Reset yank state */
    result = lle_kill_ring_reset_yank_state(ring);
    ASSERT(result == LLE_SUCCESS, "Reset yank state failed");

    /* Verify yank state cleared */
    result = lle_kill_ring_was_last_yank(ring, &was_yank);
    ASSERT(result == LLE_SUCCESS, "Check yank state failed");
    ASSERT(!was_yank, "Yank state not cleared after reset");

    /* Yank-pop should now fail */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_ERROR_INVALID_STATE, "Yank-pop succeeded after reset");

    lle_kill_ring_destroy(ring);
}

TEST(clear_ring) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Add entries */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    result = lle_kill_ring_add(ring, "second", false);
    ASSERT(result == LLE_SUCCESS, "Add failed");

    /* Clear */
    result = lle_kill_ring_clear(ring);
    ASSERT(result == LLE_SUCCESS, "Clear failed");

    /* Verify empty */
    bool empty;
    result = lle_kill_ring_is_empty(ring, &empty);
    ASSERT(result == LLE_SUCCESS, "Is empty check failed");
    ASSERT(empty, "Ring not empty after clear");

    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 0, "Count not 0 after clear");

    lle_kill_ring_destroy(ring);
}

TEST(last_was_kill_tracking) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Set last was kill */
    result = lle_kill_ring_set_last_was_kill(ring, true);
    ASSERT(result == LLE_SUCCESS, "Set last was kill failed");

    /* Add with append - should append since last was kill */
    result = lle_kill_ring_add(ring, "first", false);
    ASSERT(result == LLE_SUCCESS, "Add first failed");

    result = lle_kill_ring_add(ring, "second", true);
    ASSERT(result == LLE_SUCCESS, "Add with append failed");

    /* Verify appended */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 1, "Append didn't work with last_was_kill");

    /* Clear last was kill */
    result = lle_kill_ring_set_last_was_kill(ring, false);
    ASSERT(result == LLE_SUCCESS, "Clear last was kill failed");

    /* Add with append - should create new entry */
    result = lle_kill_ring_add(ring, "third", true);
    ASSERT(result == LLE_SUCCESS, "Add after clear failed");

    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT_EQ(count, 2, "New entry not created after clearing last_was_kill");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * GNU READLINE COMPATIBILITY TESTS
 * ============================================================================
 */

TEST(readline_compat_successive_kill_line) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Simulate successive C-k (kill-line) operations */
    result = lle_kill_ring_add(ring, "first line", false);
    ASSERT(result == LLE_SUCCESS, "First C-k failed");

    result = lle_kill_ring_add(ring, "\n", true);
    ASSERT(result == LLE_SUCCESS, "Second C-k (newline) failed");

    result = lle_kill_ring_add(ring, "second line", true);
    ASSERT(result == LLE_SUCCESS, "Third C-k failed");

    /* Should have single entry with all text */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT_EQ(count, 1, "Successive C-k created multiple entries");

    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT_STR_EQ(text, "first line\nsecond line",
                  "Successive C-k text incorrect");

    lle_kill_ring_destroy(ring);
}

TEST(readline_compat_yank_and_yank_pop) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Build kill ring */
    result = lle_kill_ring_add(ring, "oldest", false);
    result = lle_kill_ring_add(ring, "middle", false);
    result = lle_kill_ring_add(ring, "newest", false);

    /* C-y (yank) */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "C-y failed");
    ASSERT_STR_EQ(text, "newest", "C-y didn't return newest");

    /* M-y (yank-pop) */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "First M-y failed");
    ASSERT_STR_EQ(text, "middle", "First M-y incorrect");

    /* M-y again */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Second M-y failed");
    ASSERT_STR_EQ(text, "oldest", "Second M-y incorrect");

    /* M-y wraps around */
    result = lle_kill_ring_yank_pop(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Third M-y (wrap) failed");
    ASSERT_STR_EQ(text, "newest", "M-y wrap incorrect");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * EDGE CASES AND ERROR HANDLING
 * ============================================================================
 */

TEST(empty_string_rejected) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Try to add empty string */
    result = lle_kill_ring_add(ring, "", false);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER, "Empty string not rejected");

    lle_kill_ring_destroy(ring);
}

TEST(large_text_handling) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 0, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Create large text (10KB) */
    char large_text[10240];
    memset(large_text, 'A', sizeof(large_text) - 1);
    large_text[sizeof(large_text) - 1] = '\0';

    /* Add large text */
    result = lle_kill_ring_add(ring, large_text, false);
    ASSERT(result == LLE_SUCCESS, "Add large text failed");

    /* Retrieve and verify */
    const char *text;
    result = lle_kill_ring_get_current(ring, &text);
    ASSERT(result == LLE_SUCCESS, "Retrieve large text failed");
    ASSERT(strlen(text) == sizeof(large_text) - 1,
           "Large text length incorrect");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * CONCURRENCY TESTS
 * ============================================================================
 */

typedef struct {
    lle_kill_ring_t *ring;
    int thread_id;
    int iterations;
} thread_test_data_t;

static void *concurrent_add_thread(void *arg) {
    thread_test_data_t *data = (thread_test_data_t *)arg;
    char text[64];

    for (int i = 0; i < data->iterations; i++) {
        snprintf(text, sizeof(text), "thread%d_iter%d", data->thread_id, i);
        lle_kill_ring_add(data->ring, text, false);
    }

    return NULL;
}

TEST(concurrent_adds) {
    lle_kill_ring_t *ring = NULL;
    lle_result_t result;

    result = lle_kill_ring_create(&ring, 128, NULL);
    ASSERT(result == LLE_SUCCESS, "Create failed");

    /* Launch multiple threads adding concurrently */
    const int num_threads = 4;
    const int iterations = 10;
    pthread_t threads[num_threads];
    thread_test_data_t data[num_threads];

    for (int i = 0; i < num_threads; i++) {
        data[i].ring = ring;
        data[i].thread_id = i;
        data[i].iterations = iterations;
        pthread_create(&threads[i], NULL, concurrent_add_thread, &data[i]);
    }

    /* Wait for threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Verify ring has entries (may be less than total due to overflow) */
    size_t count;
    result = lle_kill_ring_get_count(ring, &count);
    ASSERT(result == LLE_SUCCESS, "Get count failed");
    ASSERT(count > 0, "No entries after concurrent adds");

    lle_kill_ring_destroy(ring);
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Kill Ring Unit Tests\n");
    printf("====================\n\n");

    printf("Basic Lifecycle Tests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(create_custom_size);
    RUN_TEST(create_max_size_clamping);
    RUN_TEST(null_pointer_checks);

    printf("\nKill Operation Tests:\n");
    RUN_TEST(add_single_kill);
    RUN_TEST(add_multiple_kills);
    RUN_TEST(add_with_append);
    RUN_TEST(add_successive_kills_with_append);
    RUN_TEST(prepend_operation);
    RUN_TEST(circular_buffer_overflow);

    printf("\nYank Operation Tests:\n");
    RUN_TEST(yank_from_empty_ring);
    RUN_TEST(yank_single_entry);
    RUN_TEST(yank_returns_most_recent);
    RUN_TEST(yank_pop_without_yank_fails);
    RUN_TEST(yank_pop_cycles_through_ring);

    printf("\nState Management Tests:\n");
    RUN_TEST(reset_yank_state);
    RUN_TEST(clear_ring);
    RUN_TEST(last_was_kill_tracking);

    printf("\nGNU Readline Compatibility Tests:\n");
    RUN_TEST(readline_compat_successive_kill_line);
    RUN_TEST(readline_compat_yank_and_yank_pop);

    printf("\nEdge Cases and Error Handling:\n");
    RUN_TEST(empty_string_rejected);
    RUN_TEST(large_text_handling);

    printf("\nConcurrency Tests:\n");
    RUN_TEST(concurrent_adds);

    return TEST_RESULT();
}
