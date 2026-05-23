/**
 * test_adaptive_detection.c - Adaptive Terminal Detection Tests
 *
 * Tests for Spec 26 Phase 1: Core Detection System
 *
 * Date: 2025-11-02
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * SIGNATURE DATABASE TESTS
 * ============================================================================
 */

TEST(signature_database) {
    printf("\nSignature Database Tests:\n");

    size_t count = 0;
    const lle_terminal_signature_t *sigs =
        lle_get_terminal_signature_database(&count);

    TEST_ASSERT(sigs != NULL, "Signature database exists");
    TEST_ASSERT(count > 0, "Database has entries");
    TEST_ASSERT(count >= 10, "Database has at least 10 known terminals");

    // Check for key terminals
    bool found_zed = false;
    bool found_vscode = false;
    bool found_iterm2 = false;
    bool found_tmux = false;

    for (size_t i = 0; i < count; i++) {
        if (strcmp(sigs[i].name, "zed") == 0)
            found_zed = true;
        if (strcmp(sigs[i].name, "vscode") == 0)
            found_vscode = true;
        if (strcmp(sigs[i].name, "iterm2") == 0)
            found_iterm2 = true;
        if (strcmp(sigs[i].name, "tmux") == 0)
            found_tmux = true;
    }

    TEST_ASSERT(found_zed, "Database includes Zed editor");
    TEST_ASSERT(found_vscode, "Database includes VS Code");
    TEST_ASSERT(found_iterm2, "Database includes iTerm2");
    TEST_ASSERT(found_tmux, "Database includes tmux");
}

/* ============================================================================
 * DETECTION TESTS
 * ============================================================================
 */

TEST(basic_detection) {
    printf("\nBasic Detection Tests:\n");

    lle_terminal_detection_result_t *result = NULL;
    lle_result_t res = lle_detect_terminal_capabilities_comprehensive(&result);

    TEST_ASSERT(res == LLE_SUCCESS, "Detection completes successfully");
    TEST_ASSERT(result != NULL, "Detection returns result");

    if (result) {
        TEST_ASSERT(result->detection_time_us > 0,
                    "Detection time is recorded");
        TEST_ASSERT(result->detection_time_us < 10000, "Detection time < 10ms");

        // TTY status should be set
        printf("    stdin_is_tty: %d, stdout_is_tty: %d\n",
               result->stdin_is_tty, result->stdout_is_tty);

        // Mode should be valid
        TEST_ASSERT(result->recommended_mode >= LLE_ADAPTIVE_MODE_NONE &&
                        result->recommended_mode <=
                            LLE_ADAPTIVE_MODE_MULTIPLEXED,
                    "Recommended mode is valid");

        // Capability level should be valid
        TEST_ASSERT(result->capability_level >= LLE_CAPABILITY_NONE &&
                        result->capability_level <= LLE_CAPABILITY_PREMIUM,
                    "Capability level is valid");

        printf("    Mode: %s, Capability: %s\n",
               lle_adaptive_mode_to_string(result->recommended_mode),
               lle_capability_level_to_string(result->capability_level));

        lle_terminal_detection_result_destroy(result);
    }
}

TEST(optimized_detection) {
    printf("\nOptimized Detection Tests:\n");

    // First call - should be cache miss
    lle_terminal_detection_result_t *result1 = NULL;
    lle_result_t res1 = lle_detect_terminal_capabilities_optimized(&result1);

    TEST_ASSERT(res1 == LLE_SUCCESS, "Optimized detection succeeds");
    TEST_ASSERT(result1 != NULL, "First call returns result");

    // Second call - should be cache hit
    lle_terminal_detection_result_t *result2 = NULL;
    lle_result_t res2 = lle_detect_terminal_capabilities_optimized(&result2);

    TEST_ASSERT(res2 == LLE_SUCCESS, "Second call succeeds");
    TEST_ASSERT(result2 != NULL, "Second call returns result");
    TEST_ASSERT(result1 == result2, "Second call returns cached result");

    // Get stats
    lle_detection_performance_stats_t stats = {0};
    lle_adaptive_get_detection_stats(&stats);

    TEST_ASSERT(stats.cache_hits >= 1, "Cache hit recorded");
    TEST_ASSERT(stats.total_detections >= 2, "Multiple detections recorded");

    printf("    Cache hits: %lu, misses: %lu\n",
           (unsigned long)stats.cache_hits, (unsigned long)stats.cache_misses);
}

/* ============================================================================
 * UTILITY TESTS
 * ============================================================================
 */

TEST(utility_functions) {
    printf("\nUtility Function Tests:\n");

    // Test mode to string
    const char *mode_str =
        lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_ENHANCED);
    TEST_ASSERT(mode_str != NULL, "Mode to string returns value");
    TEST_ASSERT(strcmp(mode_str, "enhanced") == 0, "Mode string is correct");

    // Test capability to string
    const char *cap_str = lle_capability_level_to_string(LLE_CAPABILITY_FULL);
    TEST_ASSERT(cap_str != NULL, "Capability to string returns value");
    TEST_ASSERT(strcmp(cap_str, "full") == 0, "Capability string is correct");

    // Test all modes
    TEST_ASSERT(lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_NONE) != NULL,
                "NONE mode has string");
    TEST_ASSERT(lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_MINIMAL) != NULL,
                "MINIMAL mode has string");
    TEST_ASSERT(lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_NATIVE) != NULL,
                "NATIVE mode has string");
    TEST_ASSERT(lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_MULTIPLEXED) !=
                    NULL,
                "MULTIPLEXED mode has string");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("=== Adaptive Terminal Detection Tests (Spec 26 Phase 1) ===\n");
    RUN_TEST(signature_database);
    RUN_TEST(basic_detection);
    RUN_TEST(optimized_detection);
    RUN_TEST(utility_functions);
    return TEST_RESULT();
}
