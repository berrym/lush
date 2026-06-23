/**
 * @file test_error_reporting_lifecycle.c
 * @brief Unit tests for the LLE error reporting system lifecycle
 *
 * Exercises the process-wide reporting singleton and the read-only counter
 * snapshot:
 *   init, shutdown, is_initialized, get_counter_snapshot
 *
 * Tests cover:
 *   - The system starts uninitialized in a fresh process
 *   - init marks the system initialized and is idempotent
 *   - The counter snapshot is readable, starts zeroed, and tolerates NULL
 *   - init does not perturb the counters
 *   - shutdown clears the initialized state and is idempotent
 *   - A full init -> shutdown -> init -> shutdown cycle is clean
 *
 * The tests run in process order; the first asserts the pristine state before
 * any init occurs, and the last leaves the system torn down so the singleton
 * is not leaked at exit.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "test_framework.h"

/* ============================================================================
 * Lifecycle
 * ============================================================================
 */

TEST(starts_uninitialized) {
    /// Must run first: no init has happened in this process yet.
    ASSERT_FALSE(lle_error_reporting_system_is_initialized(),
                 "system should be uninitialized before any init call");
}

TEST(init_marks_initialized) {
    ASSERT_EQ(lle_error_reporting_system_init(), LLE_SUCCESS,
              "init should succeed");
    ASSERT_TRUE(lle_error_reporting_system_is_initialized(),
                "system should report initialized after init");
}

TEST(init_is_idempotent) {
    /// A second init while already initialized is a no-op success and must
    /// not replace (and thereby leak) the existing singleton.
    ASSERT_EQ(lle_error_reporting_system_init(), LLE_SUCCESS,
              "redundant init should succeed");
    ASSERT_TRUE(lle_error_reporting_system_is_initialized(),
                "system should remain initialized");
}

/* ============================================================================
 * Counter snapshot
 * ============================================================================
 */

TEST(snapshot_starts_zeroed) {
    /// No fault has been reported in this binary, so every counter is zero.
    lle_error_counter_snapshot_t snap;
    lle_error_get_counter_snapshot(&snap);
    ASSERT_EQ(snap.total_errors_handled, 0, "no faults reported yet");
    ASSERT_EQ(snap.critical_errors_count, 0, "no critical faults yet");
    ASSERT_EQ(snap.warnings_count, 0, "no warnings yet");
    ASSERT_EQ(snap.active_error_contexts, 0, "no active contexts");
    ASSERT_EQ(snap.concurrent_errors, 0, "no concurrent faults");
}

TEST(snapshot_tolerates_null) {
    /// Must not dereference a NULL destination.
    lle_error_get_counter_snapshot(NULL);
}

TEST(init_does_not_touch_counters) {
    /// Constructing the reporting system reports no faults of its own.
    lle_error_counter_snapshot_t snap;
    lle_error_get_counter_snapshot(&snap);
    ASSERT_EQ(snap.total_errors_handled, 0,
              "init must not bump the fault counter");
}

/* ============================================================================
 * Teardown
 * ============================================================================
 */

TEST(shutdown_clears_initialized) {
    lle_error_reporting_system_shutdown();
    ASSERT_FALSE(lle_error_reporting_system_is_initialized(),
                 "system should be uninitialized after shutdown");
}

TEST(shutdown_is_idempotent) {
    /// A second shutdown when already torn down is a safe no-op.
    lle_error_reporting_system_shutdown();
    ASSERT_FALSE(lle_error_reporting_system_is_initialized(),
                 "system should remain uninitialized");
}

TEST(reinit_after_shutdown) {
    /// The full cycle works again; leave the system torn down at the end so
    /// the singleton is not leaked at process exit.
    ASSERT_EQ(lle_error_reporting_system_init(), LLE_SUCCESS,
              "re-init after shutdown should succeed");
    ASSERT_TRUE(lle_error_reporting_system_is_initialized(),
                "system should be initialized again");
    lle_error_reporting_system_shutdown();
    ASSERT_FALSE(lle_error_reporting_system_is_initialized(),
                 "system should be torn down at end of test");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Error Reporting Lifecycle Tests ===\n\n");

    printf("--- Lifecycle ---\n");
    RUN_TEST(starts_uninitialized);
    RUN_TEST(init_marks_initialized);
    RUN_TEST(init_is_idempotent);

    printf("\n--- Counter snapshot ---\n");
    RUN_TEST(snapshot_starts_zeroed);
    RUN_TEST(snapshot_tolerates_null);
    RUN_TEST(init_does_not_touch_counters);

    printf("\n--- Teardown ---\n");
    RUN_TEST(shutdown_clears_initialized);
    RUN_TEST(shutdown_is_idempotent);
    RUN_TEST(reinit_after_shutdown);

    return TEST_RESULT();
}
