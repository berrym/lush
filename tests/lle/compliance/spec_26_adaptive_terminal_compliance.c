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
 * complies with specification requirements, exercising the detection and
 * controller APIs against the live terminal environment.
 *
 * COMPLIANCE METHODOLOGY:
 * Drives the actual header API and asserts on the returned values, not on
 * symbol existence. Assertions use COMPLIANCE_ASSERT so they remain active
 * under NDEBUG (a bare assert() would compile away in a release build,
 * leaving the suite green without checking anything).
 *
 * Specification:
 * docs/lle_specification/critical_gaps/26_adaptive_terminal_integration_complete.md
 * Header: include/lle/adaptive_terminal_integration.h
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// NDEBUG-proof assertion: reports the failure and exits non-zero so the
/// compliance gate holds in every build configuration.
#define COMPLIANCE_ASSERT(cond, msg)                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s\n    at %s:%d\n", (msg), __FILE__, __LINE__);   \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/// @brief Test: Detection System API
static void test_detection_api(void) {
    printf("Testing Detection System API...\n");

    /// Core detection returns a populated result.
    lle_terminal_detection_result_t *result = NULL;
    lle_result_t res = lle_detect_terminal_capabilities_comprehensive(&result);
    COMPLIANCE_ASSERT(res == LLE_SUCCESS, "comprehensive detection succeeds");
    COMPLIANCE_ASSERT(result != NULL, "comprehensive detection returns result");

    /// The detected mode and capability level are recognized enum values:
    /// within range and resolvable to a name (>= the zero enum is vacuous).
    COMPLIANCE_ASSERT(result->recommended_mode <= LLE_ADAPTIVE_MODE_MULTIPLEXED,
                      "recommended_mode within range");
    COMPLIANCE_ASSERT(lle_adaptive_mode_to_string(result->recommended_mode) !=
                          NULL,
                      "recommended_mode resolves to a known name");
    COMPLIANCE_ASSERT(result->capability_level <= LLE_CAPABILITY_PREMIUM,
                      "capability_level within range");
    COMPLIANCE_ASSERT(
        lle_capability_level_to_string(result->capability_level) != NULL,
        "capability_level resolves to a known name");

    /// The signature database is non-empty.
    size_t count = 0;
    const lle_terminal_signature_t *signatures =
        lle_get_terminal_signature_database(&count);
    COMPLIANCE_ASSERT(signatures != NULL, "signature database accessible");
    COMPLIANCE_ASSERT(count > 0, "signature database is non-empty");

    /// Optimized detection caches: repeated calls return the same instance.
    lle_terminal_detection_result_t *cached1 = NULL, *cached2 = NULL;
    COMPLIANCE_ASSERT(lle_detect_terminal_capabilities_optimized(&cached1) ==
                          LLE_SUCCESS,
                      "optimized detection succeeds");
    COMPLIANCE_ASSERT(cached1 != NULL, "optimized detection returns a result");
    COMPLIANCE_ASSERT(lle_detect_terminal_capabilities_optimized(&cached2) ==
                          LLE_SUCCESS,
                      "second optimized detection succeeds");
    COMPLIANCE_ASSERT(cached1 == cached2,
                      "optimized detection returns the cached instance");

    /// Only the comprehensive result is owned here; the optimized result is
    /// managed by the detection cache and must not be destroyed.
    lle_terminal_detection_result_destroy(result);

    printf("  Detection API: PASS\n");
}

/// @brief Test: Controller System API
static void test_controller_api(void) {
    printf("Testing Controller System API...\n");

    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t res =
        lle_detect_terminal_capabilities_comprehensive(&detection);
    COMPLIANCE_ASSERT(res == LLE_SUCCESS, "comprehensive detection succeeds");

    lle_adaptive_context_t *context = NULL;
    res = lle_initialize_adaptive_context(&context, detection, NULL);

    /// NONE mode is non-interactive and cannot initialize a context.
    if (detection->recommended_mode == LLE_ADAPTIVE_MODE_NONE) {
        COMPLIANCE_ASSERT(res == LLE_ERROR_FEATURE_NOT_AVAILABLE,
                          "NONE mode rejects context init");
        /// Context init failed (context is NULL); only the detection result was
        /// allocated here and must be released before returning.
        lle_terminal_detection_result_destroy(detection);
        printf("  Controller API: PASS (non-interactive mode)\n");
        return;
    }

    COMPLIANCE_ASSERT(res == LLE_SUCCESS, "context initializes");
    COMPLIANCE_ASSERT(context != NULL, "context allocated");

    /// An adaptive interface wires up its context and operation callbacks.
    lle_adaptive_interface_t *interface = NULL;
    res = lle_create_adaptive_interface(&interface, NULL);
    COMPLIANCE_ASSERT(res == LLE_SUCCESS, "interface creates");
    COMPLIANCE_ASSERT(interface != NULL, "interface allocated");
    COMPLIANCE_ASSERT(interface->adaptive_context != NULL,
                      "interface has a context");
    COMPLIANCE_ASSERT(interface->read_line != NULL, "read_line wired");
    COMPLIANCE_ASSERT(interface->process_input != NULL, "process_input wired");
    COMPLIANCE_ASSERT(interface->update_display != NULL,
                      "update_display wired");
    COMPLIANCE_ASSERT(interface->handle_resize != NULL, "handle_resize wired");

    /// Recommended configuration is a recognized mode with a sane color level.
    lle_adaptive_config_recommendation_t config;
    lle_adaptive_get_recommended_config(&config);
    COMPLIANCE_ASSERT(config.recommended_mode <= LLE_ADAPTIVE_MODE_MULTIPLEXED,
                      "config recommended_mode within range");
    COMPLIANCE_ASSERT(lle_adaptive_mode_to_string(config.recommended_mode) !=
                          NULL,
                      "config recommended_mode resolves to a known name");
    COMPLIANCE_ASSERT(config.color_support_level >= 0 &&
                          config.color_support_level <= 3,
                      "config color level in 0..3");

    /// Shell interactivity precedence: a script file is never interactive;
    /// a forced-interactive shell always is.
    COMPLIANCE_ASSERT(
        lle_adaptive_should_shell_be_interactive(false, true, false) == false,
        "script file is non-interactive");
    COMPLIANCE_ASSERT(
        lle_adaptive_should_shell_be_interactive(true, false, false) == true,
        "forced interactive wins");

    /// Health check on a live context passes.
    COMPLIANCE_ASSERT(lle_adaptive_perform_health_check(context) == true,
                      "health check passes");

    /// Name lookups return the expected spellings.
    COMPLIANCE_ASSERT(
        strcmp(lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_ENHANCED),
               "enhanced") == 0,
        "ENHANCED mode name");
    COMPLIANCE_ASSERT(
        strcmp(lle_capability_level_to_string(LLE_CAPABILITY_FULL), "full") ==
            0,
        "FULL capability name");

    /// Teardown in reverse creation order: interface (owns its own internal
    /// context), then the context built from detection, then the detection
    /// result itself. lle_initialize_adaptive_context copies the detection
    /// result, so context destruction does not free the caller-owned original.
    lle_adaptive_interface_destroy(interface);
    lle_adaptive_context_destroy(context);
    lle_terminal_detection_result_destroy(detection);

    printf("  Controller API: PASS\n");
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

    test_detection_api();
    test_controller_api();

    printf("\n");
    printf("==================================================================="
           "=============\n");
    printf("Spec 26 Compliance: PASS\n");
    printf("Detection and controller APIs verified\n");
    printf("==================================================================="
           "=============\n");
    printf("\n");

    return 0;
}
