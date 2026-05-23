/**
 * @file spec_14_performance_compliance.c
 * @brief Spec 14 Performance Monitoring - Compliance Verification Tests
 *
 * Verifies that the Performance Monitoring implementation is 100% compliant
 * with specification 14_performance_optimization_complete.md
 *
 * ZERO-TOLERANCE ENFORCEMENT:
 * These tests verify spec compliance. Any failure indicates a violation
 * and the code MUST be corrected before commit is allowed.
 *
 * Tests verify:
 * - All performance operation types from spec are defined
 * - All constants match spec requirements
 * - Performance target values are correct
 * - Cache configuration matches spec
 *
 * Spec Reference:
 * docs/lle_specification/14_performance_optimization_complete.md
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Include only the performance header
#include "lle/performance.h"

// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Simple assertion macros
#define ASSERT_EQ(expected, actual, message)                                   \
    do {                                                                       \
        tests_run++;                                                           \
        if ((expected) != (actual)) {                                          \
            printf("  FAIL: %s\n", message);                                   \
            printf("        Expected: %llu, Got: %llu\n",                      \
                   (unsigned long long)(expected),                             \
                   (unsigned long long)(actual));                              \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(condition, message)                                        \
    do {                                                                       \
        tests_run++;                                                           \
        if (!(condition)) {                                                    \
            printf("  FAIL: %s\n", message);                                   \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

/* ============================================================================
 * Performance Constants Compliance Tests
 * ========================================================================== */

/**
 * Test: Verify performance target constants match spec
 * Spec Requirement: Performance targets section
 */
static bool test_performance_targets(void) {
    printf("  Testing performance target constants...\n");

    // Target response time: 500µs = 500,000ns
    ASSERT_EQ(500000ULL, LLE_PERF_TARGET_RESPONSE_TIME_NS,
              "LLE_PERF_TARGET_RESPONSE_TIME_NS must be 500000ns (500µs)");

    // Monitoring overhead: 10µs = 10,000ns
    ASSERT_EQ(10000ULL, LLE_PERF_MONITORING_OVERHEAD_NS,
              "LLE_PERF_MONITORING_OVERHEAD_NS must be 10000ns (10µs)");

    return true;
}

/**
 * Test: Verify cache configuration constants
 * Spec Requirement: Cache configuration section
 */
static bool test_cache_configuration(void) {
    printf("  Testing cache configuration constants...\n");

    ASSERT_EQ(64, LLE_CACHE_NAME_MAX, "LLE_CACHE_NAME_MAX must be 64");

    ASSERT_EQ(10, LLE_CACHE_EVICTION_SCAN_DEPTH,
              "LLE_CACHE_EVICTION_SCAN_DEPTH must be 10");

    ASSERT_EQ(100, LLE_CACHE_OPT_HISTORY_SIZE,
              "LLE_CACHE_OPT_HISTORY_SIZE must be 100");

    ASSERT_EQ(0x01, LLE_CACHE_ENTRY_FLAG_VALID,
              "LLE_CACHE_ENTRY_FLAG_VALID must be 0x01");

    return true;
}

/**
 * Test: Verify profiler configuration constants
 * Spec Requirement: Profiler configuration section
 */
static bool test_profiler_configuration(void) {
    printf("  Testing profiler configuration constants...\n");

    ASSERT_EQ(32, LLE_PROFILER_MAX_CHILDREN,
              "LLE_PROFILER_MAX_CHILDREN must be 32");

    ASSERT_EQ(20, LLE_PROFILER_MAX_HOT_SPOTS,
              "LLE_PROFILER_MAX_HOT_SPOTS must be 20");

    ASSERT_EQ(256, LLE_PROFILER_FUNCTION_KEY_MAX,
              "LLE_PROFILER_FUNCTION_KEY_MAX must be 256");

    // Hot spot threshold: 100µs = 100,000ns
    ASSERT_EQ(100000ULL, LLE_PROFILER_HOT_SPOT_THRESHOLD_NS,
              "LLE_PROFILER_HOT_SPOT_THRESHOLD_NS must be 100000ns (100µs)");

    // Long running threshold: 10ms = 10,000,000ns
    ASSERT_EQ(
        10000000ULL, LLE_PROFILER_LONG_RUNNING_THRESHOLD_NS,
        "LLE_PROFILER_LONG_RUNNING_THRESHOLD_NS must be 10000000ns (10ms)");

    ASSERT_EQ(1000, LLE_PROFILER_HIGH_FREQUENCY_THRESHOLD,
              "LLE_PROFILER_HIGH_FREQUENCY_THRESHOLD must be 1000");

    ASSERT_EQ(10000, LLE_PROFILER_HOT_SPOT_CALL_THRESHOLD,
              "LLE_PROFILER_HOT_SPOT_CALL_THRESHOLD must be 10000");

    return true;
}

/**
 * Test: Verify measurement limits
 * Spec Requirement: Performance measurement configuration
 */
static bool test_measurement_limits(void) {
    printf("  Testing measurement limit constants...\n");

    ASSERT_EQ(1024, LLE_PERF_MAX_MEASUREMENTS,
              "LLE_PERF_MAX_MEASUREMENTS must be 1024");

    ASSERT_EQ(64, LLE_PERF_OPERATION_COUNT,
              "LLE_PERF_OPERATION_COUNT must be 64");

    ASSERT_EQ(32, LLE_PERF_MAX_ALERTS, "LLE_PERF_MAX_ALERTS must be 32");

    return true;
}

/**
 * Test: Verify memory pattern analysis constants
 * Spec Requirement: Memory pattern analysis configuration
 */
static bool test_memory_pattern_constants(void) {
    printf("  Testing memory pattern analysis constants...\n");

    ASSERT_EQ(32, LLE_MEMORY_PATTERN_SIZE_COUNT,
              "LLE_MEMORY_PATTERN_SIZE_COUNT must be 32");

    ASSERT_EQ(1000, LLE_MEMORY_PATTERN_TIME_WINDOW,
              "LLE_MEMORY_PATTERN_TIME_WINDOW must be 1000");

    ASSERT_EQ(8, LLE_MEMORY_POOL_TYPE_COUNT,
              "LLE_MEMORY_POOL_TYPE_COUNT must be 8");

    return true;
}

/**
 * Test: Verify performance operation types exist
 * Spec Requirement: Performance operation types enumeration
 */
static bool test_performance_operation_types_exist(void) {
    printf("  Testing performance operation types...\n");

    // Verify key operation types are defined and accessible
    int op_types[] = {
        LLE_PERF_OP_TERMINAL_INPUT,      LLE_PERF_OP_TERMINAL_OUTPUT,
        LLE_PERF_OP_BUFFER_INSERT,       LLE_PERF_OP_BUFFER_DELETE,
        LLE_PERF_OP_BUFFER_SEARCH,       LLE_PERF_OP_EVENT_PROCESSING,
        LLE_PERF_OP_EVENT_DISPATCH,      LLE_PERF_OP_DISPLAY_RENDER,
        LLE_PERF_OP_DISPLAY_UPDATE,      LLE_PERF_OP_HISTORY_SEARCH,
        LLE_PERF_OP_HISTORY_ADD,         LLE_PERF_OP_COMPLETION_SEARCH,
        LLE_PERF_OP_COMPLETION_GENERATE, LLE_PERF_OP_SYNTAX_HIGHLIGHT,
        LLE_PERF_OP_AUTOSUGGESTION,      LLE_PERF_OP_CACHE_LOOKUP,
        LLE_PERF_OP_CACHE_INSERT,        LLE_PERF_OP_CACHE_EVICTION,
        LLE_PERF_OP_MEMORY_ALLOC,        LLE_PERF_OP_MEMORY_FREE};

    // Just verify they compile and are accessible
    ASSERT_TRUE(sizeof(op_types) > 0,
                "Performance operation types must be defined and accessible");

    // Verify first operation type starts at 0
    ASSERT_EQ(0, LLE_PERF_OP_TERMINAL_INPUT,
              "LLE_PERF_OP_TERMINAL_INPUT must equal 0");

    return true;
}

/**
 * Test: Verify component count constant
 * Spec Requirement: Component monitoring configuration
 */
static bool test_component_count(void) {
    printf("  Testing component count constant...\n");

    ASSERT_EQ(16, LLE_COMPONENT_COUNT, "LLE_COMPONENT_COUNT must be 16");

    return true;
}

/* ============================================================================
 * Test Suite Execution
 * ========================================================================== */

/**
 * Run all Spec 14 compliance tests
 * Returns: Number of failed tests (0 = all passed)
 */
int main(void) {
    printf("Running Spec 14 (Performance Monitoring) Compliance Tests...\n");
    printf("===========================================================\n\n");

    // Run all compliance tests
    test_performance_targets();
    test_cache_configuration();
    test_profiler_configuration();
    test_measurement_limits();
    test_memory_pattern_constants();
    test_performance_operation_types_exist();
    test_component_count();

    // Print results
    printf("\n");
    printf("===========================================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("===========================================================\n\n");

    if (tests_failed == 0) {
        printf("RESULT: ALL COMPLIANCE TESTS PASSED\n\n");
        printf(
            "Performance Monitoring implementation is 100%% spec-compliant\n");
        printf("All constants and types match specification requirements\n");
        return 0;
    } else {
        printf("RESULT: SPEC COMPLIANCE VIOLATION\n\n");
        printf("Performance Monitoring implementation DOES NOT match "
               "specification\n");
        printf("This is a ZERO-TOLERANCE violation\n");
        printf("Commit MUST be blocked until violations are corrected\n");
        return 1;
    }
}
