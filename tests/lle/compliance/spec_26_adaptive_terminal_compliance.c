/**
 * @file spec_26_adaptive_terminal_compliance.c
 * @brief Spec 26 adaptive terminal compliance test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * spec_26_adaptive_terminal_compliance.c - Spec 26 Compliance Verification
 *
 * Validates that Spec 26 (Adaptive Terminal Integration) implementation
 * complies with specification requirements. Tests API completeness and
 * basic functionality across all phases.
 *
 * COMPLIANCE METHODOLOGY:
 * This test verifies the actual header API by including it directly,
 * ensuring all declared functions exist and work correctly. Tests are
 * based on reading the actual header file, not assumptions.
 *
 * Specification:
 * docs/lle_specification/critical_gaps/26_adaptive_terminal_integration_complete.md
 * Header: include/lle/adaptive_terminal_integration.h
 * Date: 2025-11-02
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * HEADER VERIFICATION:
 * This compliance test is based on the actual header file at:
 * include/lle/adaptive_terminal_integration.h
 *
 * Key API functions verified:
 * - lle_detect_terminal_capabilities_comprehensive()
 * - lle_detect_terminal_capabilities_optimized()
 * - lle_terminal_detection_result_destroy()
 * - lle_get_terminal_signature_database()
 * - lle_initialize_adaptive_context()
 * - lle_adaptive_context_destroy()
 * - lle_create_adaptive_interface()
 * - lle_adaptive_interface_destroy()
 * - lle_adaptive_should_shell_be_interactive()
 * - lle_adaptive_get_recommended_config()
 * - lle_adaptive_perform_health_check()
 * - lle_adaptive_mode_to_string()
 * - lle_capability_level_to_string()
 */

/// @brief Test Phase 1: Detection System API
static void test_phase1_detection_api(void) {
    printf("Testing Phase 1: Detection System API...\n");

    /// Test 1: Core detection function exists and works
    lle_terminal_detection_result_t *result = NULL;
    lle_result_t res = lle_detect_terminal_capabilities_comprehensive(&result);
    assert(res == LLE_SUCCESS);
    assert(result != NULL);

    /// Test 2: Detection result has all required fields
    assert(result->recommended_mode >= LLE_ADAPTIVE_MODE_NONE);
    assert(result->recommended_mode <= LLE_ADAPTIVE_MODE_MULTIPLEXED);
    assert(result->capability_level >= LLE_CAPABILITY_NONE);
    assert(result->capability_level <= LLE_CAPABILITY_PREMIUM);

    /// Test 3: Terminal signature database accessible
    size_t count = 0;
    const lle_terminal_signature_t *signatures =
        lle_get_terminal_signature_database(&count);
    assert(signatures != NULL);
    assert(count > 0);

    /// Test 4: Optimized detection with caching
    lle_terminal_detection_result_t *cached = NULL;
    res = lle_detect_terminal_capabilities_optimized(&cached);
    assert(res == LLE_SUCCESS);

    /// Test 5: Detection result cleanup
    lle_terminal_detection_result_destroy(result);

    printf("  Phase 1 Detection API: PASS\n");
}

/// @brief Test Phase 2: Controller System API
static void test_phase2_controller_api(void) {
    printf("Testing Phase 2: Controller System API...\n");

    /// Test 1: Context initialization
    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t res =
        lle_detect_terminal_capabilities_comprehensive(&detection);
    assert(res == LLE_SUCCESS);

    lle_adaptive_context_t *context = NULL;
    res = lle_initialize_adaptive_context(&context, detection, NULL);

    /// NONE mode may fail initialization (non-interactive)
    if (detection->recommended_mode == LLE_ADAPTIVE_MODE_NONE) {
        assert(res == LLE_ERROR_FEATURE_NOT_AVAILABLE);
        printf("  Phase 2 Controller API: PASS (non-interactive mode)\n");
        return;
    }

    assert(res == LLE_SUCCESS);
    assert(context != NULL);

    /// Test 2: Interface creation
    lle_adaptive_interface_t *interface = NULL;
    res = lle_create_adaptive_interface(&interface, NULL);
    assert(res == LLE_SUCCESS);
    assert(interface != NULL);
    assert(interface->adaptive_context != NULL);
    assert(interface->read_line != NULL);
    assert(interface->process_input != NULL);
    assert(interface->update_display != NULL);
    assert(interface->handle_resize != NULL);

    /// Test 3: Configuration recommendations
    lle_adaptive_config_recommendation_t config;
    lle_adaptive_get_recommended_config(&config);
    assert(config.recommended_mode >= LLE_ADAPTIVE_MODE_NONE);
    assert(config.color_support_level >= 0 && config.color_support_level <= 3);

    /// Test 4: Shell integration
    bool interactive =
        lle_adaptive_should_shell_be_interactive(false, true, false);
    assert(interactive == false); /// Script file is never interactive

    interactive = lle_adaptive_should_shell_be_interactive(true, false, false);
    assert(interactive == true); /// Forced interactive always works

    /// Test 5: Health monitoring
    bool healthy = lle_adaptive_perform_health_check(context);
    assert(healthy == true);

    /// Test 6: Utility functions
    const char *mode_str =
        lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_ENHANCED);
    assert(mode_str != NULL);
    assert(strcmp(mode_str, "enhanced") == 0);

    const char *cap_str = lle_capability_level_to_string(LLE_CAPABILITY_FULL);
    assert(cap_str != NULL);
    assert(strcmp(cap_str, "full") == 0);

    /// Cleanup
    lle_adaptive_interface_destroy(interface);
    lle_adaptive_context_destroy(context);

    printf("  Phase 2 Controller API: PASS\n");
}

/**
 * Main compliance test runner
 */
int main(void) {
    printf("\n");
    printf("==================================================================="
           "=============\n");
    printf("LLE Spec 26: Adaptive Terminal Integration - Compliance "
           "Verification\n");
    printf("==================================================================="
           "=============\n");
    printf("\n");

    test_phase1_detection_api();
    test_phase2_controller_api();

    printf("\n");
    printf("==================================================================="
           "=============\n");
    printf("Spec 26 Compliance: PASS\n");
    printf("All API requirements verified for Phases 1-2\n");
    printf("==================================================================="
           "=============\n");
    printf("\n");

    return 0;
}
