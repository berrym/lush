/**
 * @file test_adaptive_fallback.c
 * @brief Unit tests for adaptive fallback
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * test_adaptive_fallback.c - Fallback Mode Testing
 *
 * Tests graceful degradation when controllers fail.
 * Verifies fallback hierarchy and error recovery.
 *
 * Specification: Spec 26 - Graceful Degradation
 * Date: 2025-11-02
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Route the suite's TEST_ASSERT through the framework's ASSERT_TRUE so a
/// failed assertion actually fails the test (longjmp back to RUN_TEST and a
/// non-zero process exit) instead of only printing a line the harness ignores.
#define TEST_ASSERT(condition, message) ASSERT_TRUE(condition, message)

/**
 * Test fallback hierarchy logic
 */
TEST(fallback_hierarchy) {
    printf("\nFallback Hierarchy Tests:\n");

    /// Get a valid detection result
    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t res =
        lle_detect_terminal_capabilities_comprehensive(&detection);
    TEST_ASSERT(res == LLE_SUCCESS, "Detection succeeds");

    if (!detection || detection->recommended_mode == LLE_ADAPTIVE_MODE_NONE) {
        printf("  Skipping fallback tests (non-interactive mode)\n");
        if (detection)
            lle_terminal_detection_result_destroy(detection);
        return;
    }

    /// Create context with detected mode
    lle_adaptive_context_t *context = NULL;
    res = lle_initialize_adaptive_context(&context, detection, NULL);
    TEST_ASSERT(res == LLE_SUCCESS, "Context initialization succeeds");

    if (!context) {
        lle_terminal_detection_result_destroy(detection);
        return;
    }

    lle_adaptive_mode_t original_mode = context->mode;
    printf("  Original mode: %s\n", lle_adaptive_mode_to_string(original_mode));

    /// Test fallback from current mode
    res = lle_adaptive_try_fallback_mode(context);

    /// Verify fallback worked based on original mode
    switch (original_mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        TEST_ASSERT(res == LLE_SUCCESS, "Native mode can fallback");
        TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_ENHANCED,
                    "Native falls back to enhanced");
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        TEST_ASSERT(res == LLE_SUCCESS, "Enhanced mode can fallback");
        TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_MINIMAL,
                    "Enhanced falls back to minimal");
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        TEST_ASSERT(res == LLE_SUCCESS, "Multiplexed mode can fallback");
        TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_NATIVE,
                    "Multiplexed falls back to native");
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        TEST_ASSERT(res == LLE_ERROR_FEATURE_NOT_AVAILABLE,
                    "Minimal mode has no fallback");
        TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_MINIMAL,
                    "Minimal mode unchanged");
        break;

    default:
        break;
    }

    /// Test health status after fallback
    if (res == LLE_SUCCESS) {
        TEST_ASSERT(context->healthy == true, "Context healthy after fallback");
        TEST_ASSERT(context->error_count == 0,
                    "Error count reset after fallback");

        printf("  Fallback mode: %s\n",
               lle_adaptive_mode_to_string(context->mode));
    }

    lle_adaptive_context_destroy(context);
}

/**
 * Test multiple fallback levels
 */
TEST(multiple_fallbacks) {
    printf("\nMultiple Fallback Tests:\n");

    /// Create a detection result for testing
    lle_terminal_detection_result_t *detection = NULL;
    (void)lle_detect_terminal_capabilities_comprehensive(&detection);

    if (!detection || detection->recommended_mode == LLE_ADAPTIVE_MODE_NONE) {
        printf("  Skipping multiple fallback tests (non-interactive mode)\n");
        if (detection)
            lle_terminal_detection_result_destroy(detection);
        return;
    }

    /// Try to create context with native mode for maximum fallback levels
    /// Note: We can only test modes that actually initialize
    lle_adaptive_context_t *context = NULL;
    (void)lle_initialize_adaptive_context(&context, detection, NULL);

    if (!context) {
        lle_terminal_detection_result_destroy(detection);
        return;
    }

    lle_adaptive_mode_t start_mode = context->mode;
    int fallback_count = 0;

    /// Keep falling back until we can't
    while (lle_adaptive_try_fallback_mode(context) == LLE_SUCCESS) {
        fallback_count++;
        printf("  Fallback %d: %s\n", fallback_count,
               lle_adaptive_mode_to_string(context->mode));

        /// Sanity check - should never need more than 3 fallbacks
        if (fallback_count > 3) {
            TEST_ASSERT(false, "Too many fallbacks (infinite loop?)");
            break;
        }
    }

    /// The hierarchy is at most NATIVE -> ENHANCED -> MINIMAL, so the chain
    /// from any start mode terminates within three fallbacks.
    TEST_ASSERT(fallback_count <= 3, "at most three fallback levels");
    TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_MINIMAL ||
                    context->mode == start_mode,
                "Final mode is minimal or original (if no fallback needed)");

    printf("  Total fallbacks from %s: %d\n",
           lle_adaptive_mode_to_string(start_mode), fallback_count);

    lle_adaptive_context_destroy(context);
}

/**
 * Test fallback error handling
 */
TEST(fallback_errors) {
    printf("\nFallback Error Handling Tests:\n");

    /// Test NULL context
    lle_result_t res = lle_adaptive_try_fallback_mode(NULL);
    TEST_ASSERT(res == LLE_ERROR_INVALID_PARAMETER, "NULL context rejected");

    /// Test NONE mode fallback
    lle_adaptive_context_t *context = calloc(1, sizeof(lle_adaptive_context_t));
    context->mode = LLE_ADAPTIVE_MODE_NONE;
    context->healthy = true;

    res = lle_adaptive_try_fallback_mode(context);
    TEST_ASSERT(res == LLE_ERROR_FEATURE_NOT_AVAILABLE,
                "NONE mode cannot fallback");
    TEST_ASSERT(context->mode == LLE_ADAPTIVE_MODE_NONE, "NONE mode unchanged");

    free(context);
}

/**
 * Test graceful degradation chain
 */
TEST(degradation_chain) {
    printf("\nGraceful Degradation Chain Tests:\n");

    /// The documented degradation paths are encoded by the pure
    /// lle_adaptive_fallback_mode_for mapping, so verify each step directly
    /// (no controller or TTY required):
    ///   NATIVE -> ENHANCED -> MINIMAL
    ///   MULTIPLEXED -> NATIVE -> ENHANCED -> MINIMAL
    ///   MINIMAL / NONE -> themselves (terminal, no further fallback)
    TEST_ASSERT(lle_adaptive_fallback_mode_for(LLE_ADAPTIVE_MODE_NATIVE) ==
                    LLE_ADAPTIVE_MODE_ENHANCED,
                "NATIVE degrades to ENHANCED");
    TEST_ASSERT(lle_adaptive_fallback_mode_for(LLE_ADAPTIVE_MODE_ENHANCED) ==
                    LLE_ADAPTIVE_MODE_MINIMAL,
                "ENHANCED degrades to MINIMAL");
    TEST_ASSERT(lle_adaptive_fallback_mode_for(LLE_ADAPTIVE_MODE_MULTIPLEXED) ==
                    LLE_ADAPTIVE_MODE_NATIVE,
                "MULTIPLEXED degrades to NATIVE");

    /// Terminal modes map to themselves, which is how the chain signals "no
    /// further fallback".
    TEST_ASSERT(lle_adaptive_fallback_mode_for(LLE_ADAPTIVE_MODE_MINIMAL) ==
                    LLE_ADAPTIVE_MODE_MINIMAL,
                "MINIMAL has no further fallback");
    TEST_ASSERT(lle_adaptive_fallback_mode_for(LLE_ADAPTIVE_MODE_NONE) ==
                    LLE_ADAPTIVE_MODE_NONE,
                "NONE has no fallback");

    /// Following the chain from NATIVE reaches MINIMAL in two steps and then
    /// terminates.
    lle_adaptive_mode_t m = LLE_ADAPTIVE_MODE_NATIVE;
    m = lle_adaptive_fallback_mode_for(m); /// ENHANCED
    m = lle_adaptive_fallback_mode_for(m); /// MINIMAL
    TEST_ASSERT(m == LLE_ADAPTIVE_MODE_MINIMAL,
                "NATIVE reaches MINIMAL after two fallbacks");
    TEST_ASSERT(lle_adaptive_fallback_mode_for(m) == m,
                "the chain terminates at MINIMAL");
}

/**
 * Main test runner
 */
int main(void) {
    printf("=== Adaptive Terminal Fallback Tests (Spec 26) ===\n");
    RUN_TEST(fallback_hierarchy);
    RUN_TEST(multiple_fallbacks);
    RUN_TEST(fallback_errors);
    RUN_TEST(degradation_chain);
    return TEST_RESULT();
}
