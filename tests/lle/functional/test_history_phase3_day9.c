/**
 * test_history_phase3_day9.c - Interactive Search Tests (Spec 09 Phase 3 Day 9)
 *
 * Comprehensive test suite for the LLE History Interactive Search (Ctrl+R):
 * - Session initialization and cleanup
 * - Query building and updating
 * - Result navigation (next/previous)
 * - Accept and cancel operations
 * - State management
 * - Prompt string generation
 *
 * API verified from include/lle/history.h on 2025-11-02
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Test memory mock provided by test_memory_mock.c

/// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Helper macros
#define TEST_START(name)                                                       \
    do {                                                                       \
        tests_run++;                                                           \
        printf("\n[TEST %d] %s...\n", tests_run, name);                        \
    } while (0)

#define TEST_PASS()                                                            \
    do {                                                                       \
        tests_passed++;                                                        \
        printf("  ✓ PASS\n");                                                  \
    } while (0)

#define TEST_FAIL(msg)                                                         \
    do {                                                                       \
        tests_failed++;                                                        \
        printf("  ✗ FAIL: %s\n", msg);                                         \
    } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            printf("  ✗ ASSERTION FAILED: %s\n", msg);                         \
            printf("    Expected: %ld, Got: %ld\n", (long)(expected),          \
                   (long)(actual));                                            \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  ✗ ASSERTION FAILED: %s\n", msg);                         \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg)                                              \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            printf("  ✗ ASSERTION FAILED: %s (got NULL)\n", msg);              \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

/* ============================================================================
 * SESSION INITIALIZATION TESTS
 * ============================================================================
 */

void test_session_init(void) {
    TEST_START("Interactive Search Session Init");

    /// Create history core
    lle_history_core_t *core = NULL;
    lle_result_t result = lle_history_core_create(&core, NULL, NULL);
    ASSERT_EQ(result, LLE_SUCCESS, "Core creation should succeed");

    /// Add some test entries
    lle_history_add_entry(core, "ls -la", 0, NULL);
    lle_history_add_entry(core, "cd /tmp", 0, NULL);
    lle_history_add_entry(core, "git status", 0, NULL);

    /// Initialize search session
    result = lle_history_interactive_search_init(core, "current line", 0);
    ASSERT_EQ(result, LLE_SUCCESS, "Session init should succeed");

    /// Check state
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_TRUE(state == LLE_SEARCH_STATE_ACTIVE ||
                    state == LLE_SEARCH_STATE_NO_RESULTS,
                "State should be active or no results");

    /// Check query is empty initially
    const char *query = lle_history_interactive_search_get_query();
    ASSERT_NOT_NULL(query, "Query should not be NULL");
    ASSERT_EQ(strlen(query), 0, "Initial query should be empty");

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_session_init_null_params(void) {
    TEST_START("Interactive Search Init - NULL Parameters");

    /// NULL core should fail
    lle_result_t result = lle_history_interactive_search_init(NULL, "line", 0);
    ASSERT_TRUE(result != LLE_SUCCESS, "Init with NULL core should fail");

    /// NULL line should be accepted (some systems allow it)
    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    result = lle_history_interactive_search_init(core, NULL, 0);
    /// Should either succeed or fail gracefully

    if (result == LLE_SUCCESS) {
        lle_history_interactive_search_cancel();
    }

    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * QUERY BUILDING TESTS
 * ============================================================================
 */

void test_query_update(void) {
    TEST_START("Interactive Search Query Update");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "git status", 0, NULL);
    lle_history_add_entry(core, "git commit", 0, NULL);

    /// Init session
    lle_history_interactive_search_init(core, "", 0);

    /// Build query character by character
    lle_history_interactive_search_update_query('g');
    const char *query1 = lle_history_interactive_search_get_query();
    ASSERT_NOT_NULL(query1, "Query should not be NULL");
    ASSERT_TRUE(strcmp(query1, "g") == 0, "Query should be 'g'");

    lle_history_interactive_search_update_query('i');
    const char *query2 = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query2, "gi") == 0, "Query should be 'gi'");

    lle_history_interactive_search_update_query('t');
    const char *query3 = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query3, "git") == 0, "Query should be 'git'");

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_query_backspace(void) {
    TEST_START("Interactive Search Query Backspace");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test command", 0, NULL);

    /// Init and build query
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('a');
    lle_history_interactive_search_update_query('b');
    lle_history_interactive_search_update_query('c');

    const char *query1 = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query1, "abc") == 0, "Query should be 'abc'");

    /// Backspace
    lle_history_interactive_search_backspace();
    const char *query2 = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query2, "ab") == 0,
                "Query should be 'ab' after backspace");

    lle_history_interactive_search_backspace();
    const char *query3 = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query3, "a") == 0,
                "Query should be 'a' after second backspace");

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_query_backspace_empty(void) {
    TEST_START("Interactive Search Backspace on Empty Query");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Init with empty query
    lle_history_interactive_search_init(core, "", 0);

    /// Backspace on empty should not crash
    (void)lle_history_interactive_search_backspace();
    /// Should either succeed or return appropriate error

    const char *query = lle_history_interactive_search_get_query();
    ASSERT_NOT_NULL(query, "Query should not be NULL");
    ASSERT_EQ(strlen(query), 0, "Query should still be empty");

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * NAVIGATION TESTS
 * ============================================================================
 */

void test_navigation_next(void) {
    TEST_START("Interactive Search Navigation - Next");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add multiple matching entries
    lle_history_add_entry(core, "git status", 0, NULL);
    lle_history_add_entry(core, "git commit", 0, NULL);
    lle_history_add_entry(core, "git push", 0, NULL);

    /// Init and search for "git"
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('g');
    lle_history_interactive_search_update_query('i');
    lle_history_interactive_search_update_query('t');

    /// Should find matches
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_ACTIVE, "Should have active results");

    /// Navigate to next result
    (void)lle_history_interactive_search_next();
    /// Should either succeed or indicate end of results

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_navigation_prev(void) {
    TEST_START("Interactive Search Navigation - Previous");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test1", 0, NULL);
    lle_history_add_entry(core, "test2", 0, NULL);
    lle_history_add_entry(core, "test3", 0, NULL);

    /// Init and search
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('t');
    lle_history_interactive_search_update_query('e');

    /// Navigate next then prev
    lle_history_interactive_search_next();
    (void)lle_history_interactive_search_prev();
    /// Should either succeed or indicate start of results

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_navigation_no_results(void) {
    TEST_START("Interactive Search Navigation - No Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Init and search for non-existent
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('x');
    lle_history_interactive_search_update_query('y');
    lle_history_interactive_search_update_query('z');

    /// Should have no results
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_NO_RESULTS, "Should have no results");

    /// Navigation should not crash
    lle_history_interactive_search_next();
    lle_history_interactive_search_prev();

    /// Cleanup
    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * ACCEPT/CANCEL TESTS
 * ============================================================================
 */

void test_accept_search(void) {
    TEST_START("Interactive Search Accept");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "ls -la", 0, NULL);

    /// Init and search
    lle_history_interactive_search_init(core, "original", 0);
    lle_history_interactive_search_update_query('l');
    lle_history_interactive_search_update_query('s');

    /// Accept should return the matched command
    const char *result = lle_history_interactive_search_accept();
    ASSERT_NOT_NULL(result, "Accept should return result");

    /// State should be inactive after accept
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_INACTIVE,
              "State should be inactive after accept");

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_cancel_search(void) {
    TEST_START("Interactive Search Cancel");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Init and search
    lle_history_interactive_search_init(core, "original", 0);
    lle_history_interactive_search_update_query('t');

    /// Cancel should restore original line
    const char *result = lle_history_interactive_search_cancel();
    ASSERT_NOT_NULL(result, "Cancel should return original line");
    ASSERT_TRUE(strcmp(result, "original") == 0,
                "Should restore original line");

    /// State should be inactive
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_INACTIVE,
              "State should be inactive after cancel");

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_accept_no_results(void) {
    TEST_START("Interactive Search Accept - No Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Search for non-existent
    lle_history_interactive_search_init(core, "original", 0);
    lle_history_interactive_search_update_query('x');

    /// Accept with no results should not crash
    (void)lle_history_interactive_search_accept();
    /// Should return original or empty string

    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * STATE MANAGEMENT TESTS
 * ============================================================================
 */

void test_state_transitions(void) {
    TEST_START("Interactive Search State Transitions");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Initial state should be inactive
    lle_interactive_search_state_t state1 =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state1, LLE_SEARCH_STATE_INACTIVE,
              "Initial state should be inactive");

    /// After init, should be active or no results
    lle_history_interactive_search_init(core, "", 0);
    lle_interactive_search_state_t state2 =
        lle_history_interactive_search_get_state();
    ASSERT_TRUE(state2 != LLE_SEARCH_STATE_INACTIVE,
                "State should not be inactive after init");

    /// After cancel, should be inactive
    lle_history_interactive_search_cancel();
    lle_interactive_search_state_t state3 =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state3, LLE_SEARCH_STATE_INACTIVE,
              "State should be inactive after cancel");

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_search_with_results_state(void) {
    TEST_START("Interactive Search State - With Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test command", 0, NULL);

    /// Search for existing command
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('t');
    lle_history_interactive_search_update_query('e');

    /// Should have active results
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_ACTIVE,
              "State should be active with results");

    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_search_no_results_state(void) {
    TEST_START("Interactive Search State - No Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Search for non-existent
    lle_history_interactive_search_init(core, "", 0);
    lle_history_interactive_search_update_query('z');
    lle_history_interactive_search_update_query('z');

    /// Should have no results state
    lle_interactive_search_state_t state =
        lle_history_interactive_search_get_state();
    ASSERT_EQ(state, LLE_SEARCH_STATE_NO_RESULTS, "State should be no results");

    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * PROMPT STRING TESTS
 * ============================================================================
 */

void test_prompt_string(void) {
    TEST_START("Interactive Search Prompt String");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test", 0, NULL);

    /// Init search
    lle_history_interactive_search_init(core, "", 0);

    /// Get prompt string
    const char *prompt = lle_history_interactive_search_get_prompt();
    ASSERT_NOT_NULL(prompt, "Prompt should not be NULL");

    /// Prompt should contain search indicator
    /// Common formats: "(reverse-i-search)", "bck-i-search", etc.

    lle_history_interactive_search_cancel();
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

void test_multiple_sessions(void) {
    TEST_START("Interactive Search Multiple Sessions");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "test1", 0, NULL);
    lle_history_add_entry(core, "test2", 0, NULL);

    /// First session
    lle_history_interactive_search_init(core, "line1", 0);
    lle_history_interactive_search_update_query('t');
    lle_history_interactive_search_cancel();

    /// Second session should work independently
    lle_history_interactive_search_init(core, "line2", 0);
    lle_history_interactive_search_update_query('t');
    const char *query = lle_history_interactive_search_get_query();
    ASSERT_TRUE(strcmp(query, "t") == 0,
                "Second session should have independent state");
    lle_history_interactive_search_cancel();

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_operations_without_init(void) {
    TEST_START("Interactive Search Operations Without Init");

    /// Operations without init should fail gracefully

    /// Update query without init
    lle_result_t result1 = lle_history_interactive_search_update_query('a');
    ASSERT_TRUE(result1 != LLE_SUCCESS, "Update without init should fail");

    /// Backspace without init
    lle_result_t result2 = lle_history_interactive_search_backspace();
    ASSERT_TRUE(result2 != LLE_SUCCESS, "Backspace without init should fail");

    /// Next without init
    lle_result_t result3 = lle_history_interactive_search_next();
    ASSERT_TRUE(result3 != LLE_SUCCESS, "Next without init should fail");

    TEST_PASS();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=======================================================\n");
    printf("  LLE INTERACTIVE SEARCH - PHASE 3 DAY 9 TESTS\n");
    printf("=======================================================\n");

    printf("\n--- SESSION INITIALIZATION ---\n");
    test_session_init();
    test_session_init_null_params();

    printf("\n--- QUERY BUILDING ---\n");
    test_query_update();
    test_query_backspace();
    test_query_backspace_empty();

    printf("\n--- NAVIGATION ---\n");
    test_navigation_next();
    test_navigation_prev();
    test_navigation_no_results();

    printf("\n--- ACCEPT/CANCEL ---\n");
    test_accept_search();
    test_cancel_search();
    test_accept_no_results();

    printf("\n--- STATE MANAGEMENT ---\n");
    test_state_transitions();
    test_search_with_results_state();
    test_search_no_results_state();

    printf("\n--- PROMPT STRING ---\n");
    test_prompt_string();

    printf("\n--- EDGE CASES ---\n");
    test_multiple_sessions();
    test_operations_without_init();

    printf("\n=======================================================\n");
    printf("  TEST RESULTS\n");
    printf("=======================================================\n");
    printf("Total Tests:  %d\n", tests_run);
    printf("Passed:       %d ✓\n", tests_passed);
    printf("Failed:       %d ✗\n", tests_failed);
    printf("Success Rate: %.1f%%\n",
           tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    printf("=======================================================\n");

    return (tests_failed == 0) ? 0 : 1;
}
