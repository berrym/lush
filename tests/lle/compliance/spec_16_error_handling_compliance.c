/**
 * @file spec_16_error_handling_compliance.c
 * @brief Spec 16 Error Handling - Compliance Verification Tests
 *
 * Verifies that the Error Handling implementation is 100% compliant with
 * specification 16_error_handling_complete.md
 *
 * ZERO-TOLERANCE ENFORCEMENT:
 * These tests verify spec compliance. Any failure indicates a violation
 * and the code MUST be corrected before commit is allowed.
 *
 * Tests verify:
 * - All error codes from spec are defined
 * - Error code values match spec requirements exactly
 * - All error codes compile and can be used
 *
 * Spec Reference: docs/lle_specification/16_error_handling_complete.md
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/// Include only the error handling header - minimal dependencies
#include "lle/error_handling.h"

/// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Simple assertion macro
#define ASSERT_EQ(expected, actual, message)                                   \
    do {                                                                       \
        tests_run++;                                                           \
        if ((expected) != (actual)) {                                          \
            printf("  FAIL: %s\n", message);                                   \
            printf("        Expected: %d, Got: %d\n", (int)(expected),         \
                   (int)(actual));                                             \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

#define ASSERT_IN_RANGE(value, min, max, message)                              \
    do {                                                                       \
        tests_run++;                                                           \
        if ((value) < (min) || (value) > (max)) {                              \
            printf("  FAIL: %s\n", message);                                   \
            printf("        Value %d not in range [%d, %d]\n", (int)(value),   \
                   (int)(min), (int)(max));                                    \
            tests_failed++;                                                    \
            return false;                                                      \
        } else {                                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

/* ============================================================================
 * Error Code Compliance Tests
 * ========================================================================== */

/**
 * Test: Verify LLE_SUCCESS is defined and equals 0
 * Spec Requirement: Line 78 - "LLE_SUCCESS = 0"
 */
static bool test_success_code_compliance(void) {
    printf("  Testing LLE_SUCCESS code...\n");
    ASSERT_EQ(0, LLE_SUCCESS, "LLE_SUCCESS must equal 0 per spec");
    return true;
}

/**
 * Test: Verify all Input Validation error codes exist (1000-1099)
 * Spec Requirement: Lines 81-88
 */
static bool test_input_validation_error_codes_exist(void) {
    printf("  Testing Input Validation error codes (1000-1099)...\n");

    /// Verify first code is exactly 1000 per spec
    ASSERT_EQ(1000, LLE_ERROR_INVALID_PARAMETER,
              "LLE_ERROR_INVALID_PARAMETER must equal 1000");

    /// Verify all codes exist and are in correct range
    lle_result_t codes[] = {
        LLE_ERROR_INVALID_PARAMETER, LLE_ERROR_NULL_POINTER,
        LLE_ERROR_BUFFER_OVERFLOW,   LLE_ERROR_BUFFER_UNDERFLOW,
        LLE_ERROR_INVALID_STATE,     LLE_ERROR_INVALID_RANGE,
        LLE_ERROR_INVALID_FORMAT,    LLE_ERROR_INVALID_ENCODING};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(
            codes[i], 1000, 1099,
            "Input validation error code must be in range 1000-1099");
    }

    return true;
}

/**
 * Test: Verify all Memory Management error codes exist (1100-1199)
 * Spec Requirement: Lines 90-97
 */
static bool test_memory_error_codes_exist(void) {
    printf("  Testing Memory Management error codes (1100-1199)...\n");

    ASSERT_EQ(1100, LLE_ERROR_OUT_OF_MEMORY,
              "LLE_ERROR_OUT_OF_MEMORY must equal 1100");

    lle_result_t codes[] = {
        LLE_ERROR_OUT_OF_MEMORY,         LLE_ERROR_MEMORY_CORRUPTION,
        LLE_ERROR_MEMORY_POOL_EXHAUSTED, LLE_ERROR_MEMORY_LEAK_DETECTED,
        LLE_ERROR_DOUBLE_FREE_DETECTED,  LLE_ERROR_USE_AFTER_FREE,
        LLE_ERROR_MEMORY_ALIGNMENT,      LLE_ERROR_MEMORY_PROTECTION};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(codes[i], 1100, 1199,
                        "Memory error code must be in range 1100-1199");
    }

    return true;
}

/**
 * Test: Verify all System Integration error codes exist (1200-1299)
 * Spec Requirement: Lines 99-106
 */
static bool test_system_integration_error_codes_exist(void) {
    printf("  Testing System Integration error codes (1200-1299)...\n");

    ASSERT_EQ(1200, LLE_ERROR_SYSTEM_CALL,
              "LLE_ERROR_SYSTEM_CALL must equal 1200");

    lle_result_t codes[] = {
        LLE_ERROR_SYSTEM_CALL,       LLE_ERROR_IO_ERROR,
        LLE_ERROR_TIMEOUT,           LLE_ERROR_INTERRUPT,
        LLE_ERROR_PERMISSION_DENIED, LLE_ERROR_RESOURCE_UNAVAILABLE,
        LLE_ERROR_DEVICE_ERROR,      LLE_ERROR_NETWORK_ERROR};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(
            codes[i], 1200, 1299,
            "System integration error code must be in range 1200-1299");
    }

    return true;
}

/**
 * Test: Verify all Component-Specific error codes exist (1300-1399)
 * Spec Requirement: Lines 108-117
 */
static bool test_component_error_codes_exist(void) {
    printf("  Testing Component-Specific error codes (1300-1399)...\n");

    ASSERT_EQ(1300, LLE_ERROR_BUFFER_COMPONENT,
              "LLE_ERROR_BUFFER_COMPONENT must equal 1300");

    lle_result_t codes[] = {
        LLE_ERROR_BUFFER_COMPONENT,     LLE_ERROR_EVENT_SYSTEM,
        LLE_ERROR_TERMINAL_ABSTRACTION, LLE_ERROR_INPUT_PARSING,
        LLE_ERROR_HISTORY_SYSTEM,       LLE_ERROR_AUTOSUGGESTIONS,
        LLE_ERROR_SYNTAX_HIGHLIGHTING,  LLE_ERROR_COMPLETION_SYSTEM,
        LLE_ERROR_DISPLAY_INTEGRATION,  LLE_ERROR_PERFORMANCE_MONITORING};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(codes[i], 1300, 1399,
                        "Component error code must be in range 1300-1399");
    }

    return true;
}

/**
 * Test: Verify all Feature/Extensibility error codes exist (1400-1499)
 * Spec Requirement: Lines 119-128
 */
static bool test_feature_error_codes_exist(void) {
    printf("  Testing Feature/Extensibility error codes (1400-1499)...\n");

    ASSERT_EQ(1400, LLE_ERROR_FEATURE_DISABLED,
              "LLE_ERROR_FEATURE_DISABLED must equal 1400");

    lle_result_t codes[] = {
        LLE_ERROR_FEATURE_DISABLED,         LLE_ERROR_FEATURE_NOT_AVAILABLE,
        LLE_ERROR_PLUGIN_LOAD_FAILED,       LLE_ERROR_PLUGIN_INIT_FAILED,
        LLE_ERROR_PLUGIN_VALIDATION_FAILED, LLE_ERROR_DEPENDENCY_MISSING,
        LLE_ERROR_VERSION_MISMATCH,         LLE_ERROR_API_MISMATCH,
        LLE_ERROR_CONFIGURATION_INVALID,    LLE_ERROR_CONFIGURATION_MISSING};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(codes[i], 1400, 1499,
                        "Feature error code must be in range 1400-1499");
    }

    return true;
}

/**
 * Test: Verify all Performance/Resource error codes exist (1500-1599)
 * Spec Requirement: Lines 130-137
 */
static bool test_performance_error_codes_exist(void) {
    printf("  Testing Performance/Resource error codes (1500-1599)...\n");

    ASSERT_EQ(1500, LLE_ERROR_PERFORMANCE_DEGRADED,
              "LLE_ERROR_PERFORMANCE_DEGRADED must equal 1500");

    lle_result_t codes[] = {
        LLE_ERROR_PERFORMANCE_DEGRADED, LLE_ERROR_RESOURCE_EXHAUSTED,
        LLE_ERROR_QUEUE_FULL,           LLE_ERROR_CACHE_MISS,
        LLE_ERROR_CACHE_CORRUPTED,      LLE_ERROR_THROTTLING_ACTIVE,
        LLE_ERROR_MONITORING_FAILURE,   LLE_ERROR_OPTIMIZATION_FAILED};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(codes[i], 1500, 1599,
                        "Performance error code must be in range 1500-1599");
    }

    return true;
}

/**
 * Test: Verify all Critical System error codes exist (1600-1699)
 * Spec Requirement: Lines 139-146
 */
static bool test_critical_error_codes_exist(void) {
    printf("  Testing Critical System error codes (1600-1699)...\n");

    ASSERT_EQ(1600, LLE_ERROR_INITIALIZATION_FAILED,
              "LLE_ERROR_INITIALIZATION_FAILED must equal 1600");

    lle_result_t codes[] = {
        LLE_ERROR_INITIALIZATION_FAILED, LLE_ERROR_SHUTDOWN_FAILED,
        LLE_ERROR_STATE_CORRUPTION,      LLE_ERROR_INVARIANT_VIOLATION,
        LLE_ERROR_ASSERTION_FAILED,      LLE_ERROR_FATAL_INTERNAL,
        LLE_ERROR_RECOVERY_FAILED,       LLE_ERROR_DEGRADATION_LIMIT_REACHED};

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        ASSERT_IN_RANGE(codes[i], 1600, 1699,
                        "Critical error code must be in range 1600-1699");
    }

    return true;
}

/**
 * Test: Verify LLE_SUCCESS_WITH_WARNINGS exists
 * Spec Requirement: Line 79
 */
static bool test_success_with_warnings_exists(void) {
    printf("  Testing LLE_SUCCESS_WITH_WARNINGS...\n");

    /// Must exist and be different from LLE_SUCCESS
    ASSERT_EQ(true, LLE_SUCCESS_WITH_WARNINGS != LLE_SUCCESS,
              "LLE_SUCCESS_WITH_WARNINGS must be distinct from LLE_SUCCESS");

    return true;
}

/* ============================================================================
 * Test Suite Execution
 * ========================================================================== */

/**
 * Run all Spec 16 compliance tests
 * Returns: Number of failed tests (0 = all passed)
 */
int main(void) {
    printf("Running Spec 16 (Error Handling) Compliance Tests...\n");
    printf("======================================================\n\n");

    /// Run all compliance tests
    test_success_code_compliance();
    test_input_validation_error_codes_exist();
    test_memory_error_codes_exist();
    test_system_integration_error_codes_exist();
    test_component_error_codes_exist();
    test_feature_error_codes_exist();
    test_performance_error_codes_exist();
    test_critical_error_codes_exist();
    test_success_with_warnings_exists();

    /// Print results
    printf("\n");
    printf("======================================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("======================================================\n\n");

    if (tests_failed == 0) {
        printf("RESULT: ALL COMPLIANCE TESTS PASSED\n\n");
        printf("Error Handling implementation is 100%% spec-compliant\n");
        printf("All error codes match specification requirements\n");
        return 0;
    } else {
        printf("RESULT: SPEC COMPLIANCE VIOLATION\n\n");
        printf("Error Handling implementation DOES NOT match specification\n");
        printf("This is a ZERO-TOLERANCE violation\n");
        printf("Commit MUST be blocked until violations are corrected\n");
        return 1;
    }
}
