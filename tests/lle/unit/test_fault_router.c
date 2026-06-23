/**
 * @file test_fault_router.c
 * @brief Unit tests for the LLE fault router and lifecycle seam
 *
 * Exercises the in-liblle fault routing introduced for Spec 16:
 *   LLE_FAULT / lle_fault_report, lle_handle_fault_lifecycle, the two
 *   registration sinks, and the atomic counter accounting.
 *
 * The router is decoupled from the shell renderer: it drives output through
 * sinks the shell installs at startup. These tests install recording sinks to
 * observe routing directly, with no shell dependency.
 *
 * Tests cover:
 *   - lle_fault_report returns the original code unchanged (single-expression
 *     call sites)
 *   - the developer sink sees every fault; the user sink sees only faults at
 *     or above major severity
 *   - the lifecycle seam fires exactly once per report
 *   - counters increment by severity (total, critical, warnings)
 *   - the fault carries the code, component, detail, and source location
 *   - detached (NULL) sinks and a NULL fault are handled safely
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "test_framework.h"

#include <string.h>

/* ============================================================================
 * Recording sinks
 * ============================================================================
 */

static int g_dev_calls;
static int g_user_calls;
static lle_fault_t g_last_dev_fault;

static void recording_dev_sink(const lle_fault_t *fault) {
    g_dev_calls++;
    g_last_dev_fault = *fault;
}

static void recording_user_sink(const lle_fault_t *fault) {
    (void)fault;
    g_user_calls++;
}

/// Reset recorders and attach both sinks. Each test that observes routing
/// calls this first so it is independent of test order.
static void install_recording_sinks(void) {
    g_dev_calls = 0;
    g_user_calls = 0;
    memset(&g_last_dev_fault, 0, sizeof(g_last_dev_fault));
    lle_fault_set_dev_sink(recording_dev_sink);
    lle_fault_set_user_sink(recording_user_sink);
}

static void detach_sinks(void) {
    lle_fault_set_dev_sink(NULL);
    lle_fault_set_user_sink(NULL);
}

/* ============================================================================
 * Return value and routing
 * ============================================================================
 */

TEST(report_returns_code_unchanged) {
    install_recording_sinks();
    lle_result_t rc =
        LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "index alloc");
    ASSERT_EQ(rc, LLE_ERROR_OUT_OF_MEMORY,
              "report must return the original code for propagation");
    detach_sinks();
}

TEST(dev_sink_sees_every_fault) {
    install_recording_sinks();
    /// A warning-severity fault and a critical-severity fault.
    LLE_FAULT(LLE_ERROR_NOT_FOUND, "history", "no match");
    LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "alloc");
    ASSERT_EQ(g_dev_calls, 2, "developer sink should see both faults");
    detach_sinks();
}

TEST(user_sink_only_high_severity) {
    install_recording_sinks();
    /// Warning severity: developer-only, not user-visible.
    LLE_FAULT(LLE_ERROR_NOT_FOUND, "history", "no match");
    ASSERT_EQ(g_user_calls, 0, "warning must not reach the user channel");
    /// Critical severity: user-visible.
    LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "alloc");
    ASSERT_EQ(g_user_calls, 1, "critical fault must reach the user channel");
    ASSERT_EQ(g_dev_calls, 2, "developer sink still sees both");
    detach_sinks();
}

TEST(seam_fires_once_per_report) {
    install_recording_sinks();
    int before = g_dev_calls;
    LLE_FAULT(LLE_ERROR_SYSTEM_CALL, "terminal", "tcsetattr");
    ASSERT_EQ(g_dev_calls - before, 1, "exactly one dispatch per report");
    detach_sinks();
}

/* ============================================================================
 * Counter accounting
 * ============================================================================
 */

TEST(counters_increment_by_severity) {
    install_recording_sinks();
    lle_error_counter_snapshot_t before;
    lle_error_get_counter_snapshot(&before);

    LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "alloc"); /// critical
    LLE_FAULT(LLE_ERROR_NOT_FOUND, "history", "no match");  /// warning

    lle_error_counter_snapshot_t after;
    lle_error_get_counter_snapshot(&after);

    ASSERT_EQ(after.total_errors_handled - before.total_errors_handled, 2,
              "both faults counted in the total");
    ASSERT_EQ(after.critical_errors_count - before.critical_errors_count, 1,
              "one critical fault counted");
    ASSERT_EQ(after.warnings_count - before.warnings_count, 1,
              "one warning fault counted");
    detach_sinks();
}

/* ============================================================================
 * Fault contents
 * ============================================================================
 */

TEST(fault_carries_code_component_detail_and_location) {
    install_recording_sinks();
    LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "index alloc");
    ASSERT_EQ(g_last_dev_fault.code, LLE_ERROR_OUT_OF_MEMORY,
              "fault carries the raised code");
    ASSERT_STR_EQ(g_last_dev_fault.component, "history",
                  "fault carries the component");
    ASSERT_STR_EQ(g_last_dev_fault.detail, "index alloc",
                  "fault carries the detail phrase");
    ASSERT_GT(g_last_dev_fault.line, 0, "fault carries a source line");
    ASSERT_NOT_NULL(g_last_dev_fault.file, "fault carries a source file");
    ASSERT_NOT_NULL(g_last_dev_fault.function, "fault carries a function name");
    detach_sinks();
}

/* ============================================================================
 * Safety
 * ============================================================================
 */

TEST(detached_sinks_are_safe) {
    detach_sinks();
    lle_error_counter_snapshot_t before;
    lle_error_get_counter_snapshot(&before);
    lle_result_t rc = LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history", "alloc");
    lle_error_counter_snapshot_t after;
    lle_error_get_counter_snapshot(&after);
    ASSERT_EQ(rc, LLE_ERROR_OUT_OF_MEMORY,
              "report still returns the code with no sinks");
    ASSERT_EQ(after.total_errors_handled - before.total_errors_handled, 1,
              "counters still accrue with no sinks installed");
}

TEST(lifecycle_tolerates_null_fault) {
    ASSERT_EQ(lle_handle_fault_lifecycle(NULL), LLE_FAULT_SURFACED,
              "NULL fault dispatch is a safe no-op");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Fault Router Tests ===\n\n");

    printf("--- Return value and routing ---\n");
    RUN_TEST(report_returns_code_unchanged);
    RUN_TEST(dev_sink_sees_every_fault);
    RUN_TEST(user_sink_only_high_severity);
    RUN_TEST(seam_fires_once_per_report);

    printf("\n--- Counter accounting ---\n");
    RUN_TEST(counters_increment_by_severity);

    printf("\n--- Fault contents ---\n");
    RUN_TEST(fault_carries_code_component_detail_and_location);

    printf("\n--- Safety ---\n");
    RUN_TEST(detached_sinks_are_safe);
    RUN_TEST(lifecycle_tolerates_null_fault);

    return TEST_RESULT();
}
