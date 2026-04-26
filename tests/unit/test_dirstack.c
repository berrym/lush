/**
 * @file test_dirstack.c
 * @brief Unit tests for directory stack (pushd/popd) functionality
 *
 * Tests the directory stack system including:
 * - Initialization and cleanup
 * - Push/pop operations
 * - Stack rotation
 * - Size and peek operations
 * - Edge cases
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirstack.h"
#include "test_framework.h"

/* Test framework macros */






/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(dirstack_init_cleanup) {
    dirstack_init();
    ASSERT_EQ(dirstack_size(), 0, "Initial stack should be empty");
    dirstack_cleanup();
}

TEST(dirstack_double_init) {
    dirstack_init();
    dirstack_init(); /* Should not crash */
    ASSERT_EQ(dirstack_size(), 0, "Stack should still be empty");
    dirstack_cleanup();
}

TEST(dirstack_cleanup_without_init) {
    /* Should not crash */
    dirstack_cleanup();
}

/* ============================================================================
 * PUSH TESTS
 * ============================================================================
 */

TEST(dirstack_push_single) {
    dirstack_init();

    int result = dirstack_push("/tmp");
    ASSERT_EQ(result, 0, "Push should succeed");
    ASSERT_EQ(dirstack_size(), 1, "Stack should have 1 entry");

    dirstack_cleanup();
}

TEST(dirstack_push_multiple) {
    dirstack_init();

    dirstack_push("/tmp");
    dirstack_push("/var");
    dirstack_push("/home");

    ASSERT_EQ(dirstack_size(), 3, "Stack should have 3 entries");

    dirstack_cleanup();
}

TEST(dirstack_push_null) {
    dirstack_init();

    int result = dirstack_push(NULL);
    ASSERT_EQ(result, -1, "Push NULL should fail");
    ASSERT_EQ(dirstack_size(), 0, "Stack should still be empty");

    dirstack_cleanup();
}

/* ============================================================================
 * POP TESTS
 * ============================================================================
 */

TEST(dirstack_pop_single) {
    dirstack_init();
    dirstack_push("/tmp");

    char *popped = dirstack_pop();
    ASSERT_NOT_NULL(popped, "Pop should return non-NULL");
    ASSERT_STR_EQ(popped, "/tmp", "Popped value should match");
    ASSERT_EQ(dirstack_size(), 0, "Stack should be empty");

    free(popped);
    dirstack_cleanup();
}

TEST(dirstack_pop_order) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");
    dirstack_push("/third");

    char *popped;

    popped = dirstack_pop();
    ASSERT_STR_EQ(popped, "/third", "First pop should be last pushed");
    free(popped);

    popped = dirstack_pop();
    ASSERT_STR_EQ(popped, "/second", "Second pop correct");
    free(popped);

    popped = dirstack_pop();
    ASSERT_STR_EQ(popped, "/first", "Third pop correct");
    free(popped);

    ASSERT_EQ(dirstack_size(), 0, "Stack should be empty");

    dirstack_cleanup();
}

TEST(dirstack_pop_empty) {
    dirstack_init();

    char *popped = dirstack_pop();
    ASSERT_NULL(popped, "Pop from empty stack should return NULL");

    dirstack_cleanup();
}

/* ============================================================================
 * PEEK TESTS
 * ============================================================================
 */

TEST(dirstack_peek_top) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");

    const char *peeked = dirstack_peek(0);
    ASSERT_NOT_NULL(peeked, "Peek should return non-NULL");
    ASSERT_STR_EQ(peeked, "/second", "Peek(0) should be top");
    ASSERT_EQ(dirstack_size(), 2, "Peek should not remove entry");

    dirstack_cleanup();
}

TEST(dirstack_peek_middle) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");
    dirstack_push("/third");

    const char *peeked = dirstack_peek(1);
    ASSERT_STR_EQ(peeked, "/second", "Peek(1) should be second from top");

    peeked = dirstack_peek(2);
    ASSERT_STR_EQ(peeked, "/first", "Peek(2) should be bottom");

    dirstack_cleanup();
}

TEST(dirstack_peek_out_of_range) {
    dirstack_init();
    dirstack_push("/only");

    const char *peeked = dirstack_peek(1);
    ASSERT_NULL(peeked, "Peek beyond stack should return NULL");

    peeked = dirstack_peek(-1);
    ASSERT_NULL(peeked, "Peek with negative index should return NULL");

    dirstack_cleanup();
}

TEST(dirstack_peek_empty) {
    dirstack_init();

    const char *peeked = dirstack_peek(0);
    ASSERT_NULL(peeked, "Peek on empty stack should return NULL");

    dirstack_cleanup();
}

/* ============================================================================
 * REMOVE TESTS
 * ============================================================================
 */

TEST(dirstack_remove_top) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");
    dirstack_push("/third");

    int result = dirstack_remove(0);
    ASSERT_EQ(result, 0, "Remove should succeed");
    ASSERT_EQ(dirstack_size(), 2, "Stack should have 2 entries");

    const char *top = dirstack_peek(0);
    ASSERT_STR_EQ(top, "/second", "New top should be /second");

    dirstack_cleanup();
}

TEST(dirstack_remove_middle) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");
    dirstack_push("/third");

    int result = dirstack_remove(1);
    ASSERT_EQ(result, 0, "Remove middle should succeed");
    ASSERT_EQ(dirstack_size(), 2, "Stack should have 2 entries");

    const char *top = dirstack_peek(0);
    ASSERT_STR_EQ(top, "/third", "Top should still be /third");

    const char *bottom = dirstack_peek(1);
    ASSERT_STR_EQ(bottom, "/first", "Bottom should be /first");

    dirstack_cleanup();
}

TEST(dirstack_remove_out_of_range) {
    dirstack_init();
    dirstack_push("/only");

    int result = dirstack_remove(5);
    ASSERT_EQ(result, -1, "Remove out of range should fail");
    ASSERT_EQ(dirstack_size(), 1, "Stack should be unchanged");

    dirstack_cleanup();
}

/* ============================================================================
 * CLEAR TESTS
 * ============================================================================
 */

TEST(dirstack_clear_populated) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");
    dirstack_push("/third");

    dirstack_clear();
    ASSERT_EQ(dirstack_size(), 0, "Stack should be empty after clear");

    dirstack_cleanup();
}

TEST(dirstack_clear_empty) {
    dirstack_init();

    dirstack_clear();
    ASSERT_EQ(dirstack_size(), 0, "Clear on empty stack should work");

    dirstack_cleanup();
}

/* ============================================================================
 * ROTATE TESTS
 * ============================================================================
 */

TEST(dirstack_rotate_positive) {
    dirstack_init();
    dirstack_push("/first"); /* bottom */
    dirstack_push("/second");
    dirstack_push("/third"); /* top */

    int result = dirstack_rotate(2);
    ASSERT_EQ(result, 0, "Rotate should succeed");

    const char *top = dirstack_peek(0);
    ASSERT_STR_EQ(top, "/first", "Bottom should now be top after rotate(2)");

    dirstack_cleanup();
}

TEST(dirstack_rotate_zero) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");

    int result = dirstack_rotate(0);
    ASSERT_EQ(result, 0, "Rotate(0) should succeed");

    const char *top = dirstack_peek(0);
    ASSERT_STR_EQ(top, "/second", "Stack should be unchanged");

    dirstack_cleanup();
}

TEST(dirstack_rotate_out_of_range) {
    dirstack_init();
    dirstack_push("/only");

    int result = dirstack_rotate(5);
    ASSERT_EQ(result, -1, "Rotate beyond stack should fail");

    dirstack_cleanup();
}

TEST(dirstack_rotate_empty) {
    dirstack_init();

    int result = dirstack_rotate(0);
    ASSERT_EQ(result, -1, "Rotate on empty stack should fail");

    dirstack_cleanup();
}

/* ============================================================================
 * SIZE TESTS
 * ============================================================================
 */

TEST(dirstack_size_empty) {
    dirstack_init();
    ASSERT_EQ(dirstack_size(), 0, "Empty stack size is 0");
    dirstack_cleanup();
}

TEST(dirstack_size_after_operations) {
    dirstack_init();

    dirstack_push("/a");
    ASSERT_EQ(dirstack_size(), 1, "Size after 1 push");

    dirstack_push("/b");
    ASSERT_EQ(dirstack_size(), 2, "Size after 2 pushes");

    char *p = dirstack_pop();
    free(p);
    ASSERT_EQ(dirstack_size(), 1, "Size after pop");

    dirstack_clear();
    ASSERT_EQ(dirstack_size(), 0, "Size after clear");

    dirstack_cleanup();
}

/* ============================================================================
 * PRINT TESTS (just verify they don't crash)
 * ============================================================================
 */

TEST(dirstack_print_empty) {
    dirstack_init();
    dirstack_print(false, false);
    dirstack_print(true, true);
    dirstack_cleanup();
}

TEST(dirstack_print_with_entries) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");

    /* Just verify these don't crash */
    dirstack_print(false, false);
    dirstack_print(true, false);
    dirstack_print(false, true);
    dirstack_print(true, true);

    dirstack_cleanup();
}

/* ============================================================================
 * SYNC VARIABLE TEST
 * ============================================================================
 */

TEST(dirstack_sync_variable) {
    dirstack_init();
    dirstack_push("/first");
    dirstack_push("/second");

    /* Should not crash */
    dirstack_sync_variable();

    dirstack_cleanup();
}

/* ============================================================================
 * STRESS TESTS
 * ============================================================================
 */

TEST(dirstack_many_entries) {
    dirstack_init();

    /* Push many entries */
    for (int i = 0; i < 50; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dir%d", i);
        int result = dirstack_push(path);
        ASSERT_EQ(result, 0, "Push should succeed");
    }

    ASSERT_EQ(dirstack_size(), 50, "Should have 50 entries");

    /* Pop all */
    for (int i = 49; i >= 0; i--) {
        char *popped = dirstack_pop();
        ASSERT_NOT_NULL(popped, "Pop should succeed");

        char expected[32];
        snprintf(expected, sizeof(expected), "/dir%d", i);
        ASSERT_STR_EQ(popped, expected, "Pop order should be LIFO");

        free(popped);
    }

    ASSERT_EQ(dirstack_size(), 0, "Stack should be empty");

    dirstack_cleanup();
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n=== Directory Stack Unit Tests ===\n\n");

    /* Initialization tests */
    printf("Initialization Tests:\n");
    RUN_TEST(dirstack_init_cleanup);
    RUN_TEST(dirstack_double_init);
    RUN_TEST(dirstack_cleanup_without_init);

    /* Push tests */
    printf("\nPush Tests:\n");
    RUN_TEST(dirstack_push_single);
    RUN_TEST(dirstack_push_multiple);
    RUN_TEST(dirstack_push_null);

    /* Pop tests */
    printf("\nPop Tests:\n");
    RUN_TEST(dirstack_pop_single);
    RUN_TEST(dirstack_pop_order);
    RUN_TEST(dirstack_pop_empty);

    /* Peek tests */
    printf("\nPeek Tests:\n");
    RUN_TEST(dirstack_peek_top);
    RUN_TEST(dirstack_peek_middle);
    RUN_TEST(dirstack_peek_out_of_range);
    RUN_TEST(dirstack_peek_empty);

    /* Remove tests */
    printf("\nRemove Tests:\n");
    RUN_TEST(dirstack_remove_top);
    RUN_TEST(dirstack_remove_middle);
    RUN_TEST(dirstack_remove_out_of_range);

    /* Clear tests */
    printf("\nClear Tests:\n");
    RUN_TEST(dirstack_clear_populated);
    RUN_TEST(dirstack_clear_empty);

    /* Rotate tests */
    printf("\nRotate Tests:\n");
    RUN_TEST(dirstack_rotate_positive);
    RUN_TEST(dirstack_rotate_zero);
    RUN_TEST(dirstack_rotate_out_of_range);
    RUN_TEST(dirstack_rotate_empty);

    /* Size tests */
    printf("\nSize Tests:\n");
    RUN_TEST(dirstack_size_empty);
    RUN_TEST(dirstack_size_after_operations);

    /* Print tests */
    printf("\nPrint Tests:\n");
    RUN_TEST(dirstack_print_empty);
    RUN_TEST(dirstack_print_with_entries);

    /* Sync tests */
    printf("\nSync Tests:\n");
    RUN_TEST(dirstack_sync_variable);

    /* Stress tests */
    printf("\nStress Tests:\n");
    RUN_TEST(dirstack_many_entries);

    return TEST_RESULT();
}
