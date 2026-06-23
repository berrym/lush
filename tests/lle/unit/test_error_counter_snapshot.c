/**
 * @file test_error_counter_snapshot.c
 * @brief Unit tests for the LLE error counter snapshot
 *
 * The fault router accounts every fault in process-wide atomic counters;
 * lle_error_get_counter_snapshot exposes a read-only copy. These tests assert
 * the snapshot reads cleanly from a pristine process and tolerates a NULL
 * destination.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "test_framework.h"

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

int main(void) {
    printf("=== LLE Error Counter Snapshot Tests ===\n\n");
    RUN_TEST(snapshot_starts_zeroed);
    RUN_TEST(snapshot_tolerates_null);
    return TEST_RESULT();
}
