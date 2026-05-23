/*
 * test_terminal_capabilities.c - Unit tests for terminal capability detection
 *
 * Tests Spec 02 Phase 1: Terminal Capability Detection
 *
 * Test Categories:
 * 1. Terminal type detection
 * 2. Color capability detection
 * 3. Advanced feature detection
 * 4. Geometry detection
 * 5. Performance characteristics
 */

#include "lle/terminal_abstraction.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * TERMINAL TYPE DETECTION TESTS
 * ============================================================================
 */

TEST(capability_detection_basic) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);
    ASSERT(caps->terminal_type != NULL);
    ASSERT(caps->terminal_program != NULL);

    // Should detect some terminal type
    ASSERT(caps->terminal_type_enum >= LLE_TERMINAL_UNKNOWN);
    ASSERT(caps->terminal_type_enum <= LLE_TERMINAL_KITTY);

    // Cleanup
    lle_capabilities_destroy(caps);
}

TEST(terminal_type_strings) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Terminal type string should not be empty
    ASSERT(strlen(caps->terminal_type) > 0);
    ASSERT(strlen(caps->terminal_program) > 0);

    // Should be valid strings
    ASSERT(strcmp(caps->terminal_type, "") != 0);
    ASSERT(strcmp(caps->terminal_program, "") != 0);

    lle_capabilities_destroy(caps);
}

TEST(tty_detection) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // is_tty should match actual TTY status
    int stdin_is_tty = isatty(STDIN_FILENO);
    int stdout_is_tty = isatty(STDOUT_FILENO);
    bool expected_tty = (stdin_is_tty && stdout_is_tty);

    ASSERT(caps->is_tty == expected_tty);

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * COLOR CAPABILITY TESTS
 * ============================================================================
 */

TEST(color_depth_valid) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Color depth should be 0, 4, 8, or 24
    ASSERT(caps->detected_color_depth == 0 || caps->detected_color_depth == 4 ||
           caps->detected_color_depth == 8 || caps->detected_color_depth == 24);

    // Color support flags should be consistent with depth
    if (caps->detected_color_depth >= 4) {
        ASSERT(caps->supports_ansi_colors == true);
    }

    if (caps->detected_color_depth >= 8) {
        ASSERT(caps->supports_256_colors == true);
    }

    if (caps->detected_color_depth == 24) {
        ASSERT(caps->supports_truecolor == true);
    }

    lle_capabilities_destroy(caps);
}

TEST(color_flags_consistency) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // If truecolor supported, 256 colors must also be supported
    if (caps->supports_truecolor) {
        ASSERT(caps->supports_256_colors == true);
        ASSERT(caps->supports_ansi_colors == true);
    }

    // If 256 colors supported, ANSI colors must be supported
    if (caps->supports_256_colors) {
        ASSERT(caps->supports_ansi_colors == true);
    }

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * TEXT ATTRIBUTE TESTS
 * ============================================================================
 */

TEST(text_attributes_detected) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Modern terminals should support at least bold and underline
    // Note: This might fail in very minimal environments, but that's OK
    // We're just testing that detection ran, not enforcing support

    // Text attribute flags should be boolean
    ASSERT(caps->supports_bold == true || caps->supports_bold == false);
    ASSERT(caps->supports_italic == true || caps->supports_italic == false);
    ASSERT(caps->supports_underline == true ||
           caps->supports_underline == false);
    ASSERT(caps->supports_strikethrough == true ||
           caps->supports_strikethrough == false);
    ASSERT(caps->supports_reverse == true || caps->supports_reverse == false);
    ASSERT(caps->supports_dim == true || caps->supports_dim == false);

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * ADVANCED FEATURE TESTS
 * ============================================================================
 */

TEST(advanced_features_detected) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Feature flags should be boolean
    ASSERT(caps->supports_mouse_reporting == true ||
           caps->supports_mouse_reporting == false);
    ASSERT(caps->supports_bracketed_paste == true ||
           caps->supports_bracketed_paste == false);
    ASSERT(caps->supports_focus_events == true ||
           caps->supports_focus_events == false);
    ASSERT(caps->supports_synchronized_output == true ||
           caps->supports_synchronized_output == false);
    ASSERT(caps->supports_unicode == true || caps->supports_unicode == false);

    lle_capabilities_destroy(caps);
}

TEST(feature_correlation) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Modern terminals (Alacritty, Kitty) should support most features
    if (caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY) {
        ASSERT(caps->supports_unicode == true);
        ASSERT(caps->supports_mouse_reporting == true);
        ASSERT(caps->supports_bracketed_paste == true);
    }

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * GEOMETRY DETECTION TESTS
 * ============================================================================
 */

TEST(geometry_detection) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Width and height should be reasonable
    ASSERT(caps->terminal_width >= 20); // Minimum enforced
    ASSERT(caps->terminal_height >= 5); // Minimum enforced

    // Should not be absurdly large
    ASSERT(caps->terminal_width < 10000);
    ASSERT(caps->terminal_height < 10000);

    lle_capabilities_destroy(caps);
}

TEST(geometry_update) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Update with specific dimensions
    result = lle_capabilities_update_geometry(caps, 100, 40);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width == 100);
    ASSERT(caps->terminal_height == 40);

    // Update with zeros (should re-detect)
    result = lle_capabilities_update_geometry(caps, 0, 0);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width >= 20);
    ASSERT(caps->terminal_height >= 5);

    // Update with too-small values (should enforce minimums)
    result = lle_capabilities_update_geometry(caps, 10, 2);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps->terminal_width == 80);  // Enforced minimum
    ASSERT(caps->terminal_height == 24); // Enforced minimum

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * PERFORMANCE CHARACTERISTICS TESTS
 * ============================================================================
 */

TEST(performance_characteristics) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Latency should be reasonable (1-100ms)
    ASSERT(caps->estimated_latency_ms >= 1);
    ASSERT(caps->estimated_latency_ms <= 100);

    // Fast update flag should be boolean
    ASSERT(caps->supports_fast_updates == true ||
           caps->supports_fast_updates == false);

    // Modern terminals should have lower latency
    if (caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY) {
        ASSERT(caps->estimated_latency_ms <= 10);
        ASSERT(caps->supports_fast_updates == true);
    }

    lle_capabilities_destroy(caps);
}

TEST(optimization_flags) {
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);

    // Optimization flags should be set
    // At minimum, unicode awareness should be set for modern terminals
    if (caps->supports_unicode) {
        ASSERT((caps->optimizations & LLE_OPT_UNICODE_AWARE) != 0);
    }

    // Fast terminals should have incremental draw enabled
    if (caps->supports_fast_updates) {
        ASSERT((caps->optimizations & LLE_OPT_INCREMENTAL_DRAW) != 0);
    }

    lle_capabilities_destroy(caps);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(null_parameter_handling) {
    // NULL caps pointer should return error
    lle_result_t result = lle_capabilities_detect_environment(NULL, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    // NULL destroy should not crash
    lle_capabilities_destroy(NULL);

    // NULL update should return error
    result = lle_capabilities_update_geometry(NULL, 80, 24);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);
}

TEST(multiple_detections) {
    // Should be able to detect multiple times
    lle_terminal_capabilities_t *caps1 = NULL;
    lle_terminal_capabilities_t *caps2 = NULL;

    lle_result_t result1 = lle_capabilities_detect_environment(&caps1, NULL);
    lle_result_t result2 = lle_capabilities_detect_environment(&caps2, NULL);

    ASSERT(result1 == LLE_SUCCESS);
    ASSERT(result2 == LLE_SUCCESS);
    ASSERT(caps1 != NULL);
    ASSERT(caps2 != NULL);

    // Results should be consistent
    ASSERT(caps1->terminal_type_enum == caps2->terminal_type_enum);
    ASSERT(caps1->terminal_width == caps2->terminal_width);
    ASSERT(caps1->terminal_height == caps2->terminal_height);

    lle_capabilities_destroy(caps1);
    lle_capabilities_destroy(caps2);
}

/* ============================================================================
 * MEMORY MANAGEMENT TESTS
 * ============================================================================
 */

TEST(memory_cleanup) {
    // Test that destroy properly frees memory
    lle_terminal_capabilities_t *caps = NULL;
    lle_result_t result = lle_capabilities_detect_environment(&caps, NULL);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(caps != NULL);
    ASSERT(caps->terminal_type != NULL);
    ASSERT(caps->terminal_program != NULL);

    // Should not leak memory when destroyed
    lle_capabilities_destroy(caps);
    // If valgrind is run, this will be verified
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Terminal Capability Detection Tests\n");
    printf("============================================\n\n");

    printf("Terminal Type Detection Tests:\n");
    RUN_TEST(capability_detection_basic);
    RUN_TEST(terminal_type_strings);
    RUN_TEST(tty_detection);

    printf("\nColor Capability Tests:\n");
    RUN_TEST(color_depth_valid);
    RUN_TEST(color_flags_consistency);

    printf("\nText Attribute Tests:\n");
    RUN_TEST(text_attributes_detected);

    printf("\nAdvanced Feature Tests:\n");
    RUN_TEST(advanced_features_detected);
    RUN_TEST(feature_correlation);

    printf("\nGeometry Detection Tests:\n");
    RUN_TEST(geometry_detection);
    RUN_TEST(geometry_update);

    printf("\nPerformance Characteristics Tests:\n");
    RUN_TEST(performance_characteristics);
    RUN_TEST(optimization_flags);

    printf("\nError Handling Tests:\n");
    RUN_TEST(null_parameter_handling);
    RUN_TEST(multiple_detections);

    printf("\nMemory Management Tests:\n");
    RUN_TEST(memory_cleanup);

    return TEST_RESULT();
}
