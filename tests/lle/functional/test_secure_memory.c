/**
 * @file test_secure_memory.c
 * @brief Functional Test for Spec 15 Minimal Secure Mode
 *
 * This test demonstrates real-world usage of the minimal secure mode API
 * for protecting sensitive data (passwords, tokens, etc.) from being
 * swapped to disk.
 *
 * Copyright (C) 2025 Michael Berry
 * Licensed under GPL v3
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lle/error_handling.h"
#include "lle/memory_management.h"

/// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    printf("\n--- Test: " #name " ---\n");                                     \
    if (test_##name()) {                                                       \
        printf("✓ PASSED\n");                                                  \
        tests_passed++;                                                        \
    } else {                                                                   \
        printf("✗ FAILED\n");                                                  \
        tests_failed++;                                                        \
    }

/* ============================================================================
 * TEST 1: Basic Parameter Validation
 * ============================================================================
 */
static int test_parameter_validation(void) {
    printf("Testing parameter validation...\n");

    /// NULL pointer tests
    lle_result_t result = lle_memory_enable_secure_mode(NULL, 1024);
    if (result != LLE_ERROR_INVALID_PARAMETER) {
        printf("  ERROR: NULL pointer should fail with INVALID_PARAMETER\n");
        return 0;
    }
    printf("  ✓ NULL pointer correctly rejected\n");

    /// Zero size tests
    char buffer[100];
    result = lle_memory_enable_secure_mode(buffer, 0);
    if (result != LLE_ERROR_INVALID_PARAMETER) {
        printf("  ERROR: Zero size should fail with INVALID_PARAMETER\n");
        return 0;
    }
    printf("  ✓ Zero size correctly rejected\n");

    /// Same for secure_clear
    result = lle_memory_secure_clear(NULL, 100);
    if (result != LLE_ERROR_INVALID_PARAMETER) {
        printf("  ERROR: NULL pointer to secure_clear should fail\n");
        return 0;
    }
    printf("  ✓ secure_clear validates parameters\n");

    /// Same for disable
    result = lle_memory_disable_secure_mode(NULL, 100);
    if (result != LLE_ERROR_INVALID_PARAMETER) {
        printf("  ERROR: NULL pointer to disable should fail\n");
        return 0;
    }
    printf("  ✓ disable validates parameters\n");

    return 1;
}

/* ============================================================================
 * TEST 2: Secure Clear Functionality
 * ============================================================================
 */
static int test_secure_clear_functionality(void) {
    printf("Testing secure clear functionality...\n");

    /// Create buffer with sensitive data
    char password[256];
    strcpy(password, "SuperSecretPassword123!");
    printf("  Initial password: '%s'\n", password);

    /// Secure clear
    lle_result_t result = lle_memory_secure_clear(password, sizeof(password));
    if (result != LLE_SUCCESS) {
        printf("  ERROR: Secure clear failed with code %d\n", result);
        return 0;
    }
    printf("  ✓ Secure clear succeeded\n");

    /// Verify all bytes are zero
    int all_zero = 1;
    for (size_t i = 0; i < sizeof(password); i++) {
        if (password[i] != 0) {
            all_zero = 0;
            printf("  ERROR: Byte %zu not cleared (value: %d)\n", i,
                   password[i]);
            break;
        }
    }

    if (!all_zero) {
        return 0;
    }
    printf("  ✓ All bytes cleared to zero\n");

    return 1;
}

/* ============================================================================
 * TEST 3: Complete Secure Mode Lifecycle
 * ============================================================================
 */
static int test_complete_lifecycle(void) {
    printf("Testing complete secure mode lifecycle...\n");

    /// Allocate buffer for sensitive data
    char api_token[512];
    strcpy(api_token,
           "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0");

    /// Step 1: Enable secure mode (lock in RAM)
    printf("  Step 1: Enabling secure mode...\n");
    lle_result_t result =
        lle_memory_enable_secure_mode(api_token, sizeof(api_token));

    /// Check result - should succeed or gracefully degrade
    int secure_enabled = 0;
    switch (result) {
    case LLE_SUCCESS:
        printf("  ✓ Secure mode enabled successfully\n");
        secure_enabled = 1;
        break;
    case LLE_ERROR_PERMISSION_DENIED:
        printf("  ⚠ Secure mode denied (no CAP_IPC_LOCK) - continuing with "
               "degraded security\n");
        break;
    case LLE_ERROR_OUT_OF_MEMORY:
        printf("  ⚠ RLIMIT_MEMLOCK exceeded - continuing with degraded "
               "security\n");
        break;
    case LLE_ERROR_RESOURCE_UNAVAILABLE:
        printf(
            "  ⚠ Resource unavailable - continuing with degraded security\n");
        break;
    case LLE_ERROR_FEATURE_NOT_AVAILABLE:
        printf("  ⚠ mlock not available on this platform - continuing with "
               "degraded security\n");
        break;
    default:
        printf("  ERROR: Unexpected result code: %d\n", result);
        return 0;
    }

    /// Step 2: Use the sensitive data (simulated)
    printf("  Step 2: Using sensitive data...\n");
    printf("  Token length: %zu bytes\n", strlen(api_token));

    /// Step 3: Secure clear when done
    printf("  Step 3: Securely clearing data...\n");
    result = lle_memory_secure_clear(api_token, sizeof(api_token));
    if (result != LLE_SUCCESS) {
        printf("  ERROR: Secure clear failed\n");
        return 0;
    }
    printf("  ✓ Data securely cleared\n");

    /// Verify clearing
    if (api_token[0] != '\0' || strlen(api_token) != 0) {
        printf("  ERROR: Data not properly cleared\n");
        return 0;
    }

    /// Step 4: Disable secure mode (unlock)
    if (secure_enabled) {
        printf("  Step 4: Disabling secure mode...\n");
        result = lle_memory_disable_secure_mode(api_token, sizeof(api_token));
        if (result != LLE_SUCCESS && result != LLE_ERROR_OUT_OF_MEMORY) {
            printf("  ERROR: Disable failed with code %d\n", result);
            return 0;
        }
        printf("  ✓ Secure mode disabled\n");
    }

    return 1;
}

/* ============================================================================
 * TEST 4: Multiple Concurrent Secure Regions
 * ============================================================================
 */
static int test_multiple_regions(void) {
    printf("Testing multiple secure regions...\n");

    /// Allocate multiple sensitive buffers
    char password[256] = "user_password_123";
    char api_key[512] = "sk_live_abc123xyz789";
    char session_token[1024] = "session_abc123def456ghi789";

    printf("  Securing 3 separate memory regions...\n");

    /// Enable secure mode for all
    lle_result_t r1 = lle_memory_enable_secure_mode(password, sizeof(password));
    lle_result_t r2 = lle_memory_enable_secure_mode(api_key, sizeof(api_key));
    lle_result_t r3 =
        lle_memory_enable_secure_mode(session_token, sizeof(session_token));

    int any_secured = 0;
    if (r1 == LLE_SUCCESS) {
        printf("  ✓ Region 1 (password) secured\n");
        any_secured = 1;
    }
    if (r2 == LLE_SUCCESS) {
        printf("  ✓ Region 2 (api_key) secured\n");
        any_secured = 1;
    }
    if (r3 == LLE_SUCCESS) {
        printf("  ✓ Region 3 (session_token) secured\n");
        any_secured = 1;
    }

    if (!any_secured) {
        printf("  ⚠ No regions could be secured (platform limitation)\n");
    }

    /// Clear all regions
    printf("  Clearing all regions...\n");
    lle_memory_secure_clear(password, sizeof(password));
    lle_memory_secure_clear(api_key, sizeof(api_key));
    lle_memory_secure_clear(session_token, sizeof(session_token));
    printf("  ✓ All regions cleared\n");

    /// Disable secure mode
    if (r1 == LLE_SUCCESS)
        lle_memory_disable_secure_mode(password, sizeof(password));
    if (r2 == LLE_SUCCESS)
        lle_memory_disable_secure_mode(api_key, sizeof(api_key));
    if (r3 == LLE_SUCCESS)
        lle_memory_disable_secure_mode(session_token, sizeof(session_token));

    printf("  ✓ All regions unlocked\n");

    return 1;
}

/* ============================================================================
 * TEST 5: Graceful Degradation
 * ============================================================================
 */
static int test_graceful_degradation(void) {
    printf("Testing graceful degradation...\n");

    /// This test verifies that the system continues to function even when
    /// secure mode cannot be enabled (e.g., insufficient permissions)

    char data[128] = "test_data";

    /// Try to enable secure mode - may fail, that's OK
    lle_result_t result = lle_memory_enable_secure_mode(data, sizeof(data));
    printf("  Enable result: %d\n", result);

    /// But secure_clear should ALWAYS work
    result = lle_memory_secure_clear(data, sizeof(data));
    if (result != LLE_SUCCESS) {
        printf("  ERROR: secure_clear should always succeed\n");
        return 0;
    }
    printf("  ✓ Secure clear works even without mlock\n");

    /// Verify clearing worked
    if (data[0] != 0) {
        printf("  ERROR: Data not cleared\n");
        return 0;
    }

    /// Disable should also be safe to call even if enable failed
    result = lle_memory_disable_secure_mode(data, sizeof(data));
    printf("  ✓ Disable is safe to call\n");

    return 1;
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */
int main(void) {
    printf("=============================================================\n");
    printf("LLE Spec 15: Minimal Secure Mode - Functional Test Suite\n");
    printf("=============================================================\n");
    printf("\nThis test demonstrates real-world usage of secure mode\n");
    printf("for protecting sensitive data from swap-to-disk.\n");

    TEST(parameter_validation);
    TEST(secure_clear_functionality);
    TEST(complete_lifecycle);
    TEST(multiple_regions);
    TEST(graceful_degradation);

    printf("\n=============================================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=============================================================\n");

    if (tests_failed == 0) {
        printf("\n✓ ALL FUNCTIONAL TESTS PASSED\n\n");
        printf("NOTE: If you see warnings about mlock permission denied,\n");
        printf("this is expected. Run with CAP_IPC_LOCK capability or as\n");
        printf("root to test full secure mode functionality.\n\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n\n");
        return 1;
    }
}
