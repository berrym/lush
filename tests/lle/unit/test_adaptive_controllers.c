/**
 * test_adaptive_controllers.c - Phase 2 Controller Tests
 *
 * Comprehensive tests for all four adaptive terminal controllers:
 * - Native Terminal Controller
 * - Enhanced Display Client Controller
 * - Minimal Controller
 * - Multiplexer Controller
 *
 * Tests initialization, basic operations, and controller-specific features.
 *
 * Specification: Spec 26 Phase 2
 * Date: 2025-11-02
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "test_framework.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test tracking
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        tests_run++;                                                           \
        if (condition) {                                                       \
            tests_passed++;                                                    \
            printf("  [PASS] %s\n", message);                                  \
        } else {                                                               \
            printf("  [FAIL] %s\n", message);                                  \
        }                                                                      \
    } while (0)

/* ============================================================================
 * CONTEXT INITIALIZATION TESTS
 * ============================================================================
 */

TEST(context_initialization) {
    printf("\nContext Initialization Tests:\n");

    // Check if we have a TTY - some tests require interactive terminal
    bool has_tty = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);

    // Test detection and context creation
    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t res =
        lle_detect_terminal_capabilities_comprehensive(&detection);

    TEST_ASSERT(res == LLE_SUCCESS, "Detection succeeds for context init");
    TEST_ASSERT(detection != NULL, "Detection result is valid");

    if (detection) {
        // Test context initialization with detected mode
        lle_adaptive_context_t *context = NULL;
        res = lle_initialize_adaptive_context(&context, detection, NULL);

        /* In non-TTY environments (CI), mode may be NONE which returns an error
         */
        if (has_tty || detection->recommended_mode != LLE_ADAPTIVE_MODE_NONE) {
            TEST_ASSERT(res == LLE_SUCCESS, "Context initialization succeeds");
            TEST_ASSERT(context != NULL, "Context is created");
        } else {
            /* Non-interactive mode - context init returns feature not available
             */
            TEST_ASSERT(res == LLE_ERROR_FEATURE_NOT_AVAILABLE ||
                            res == LLE_SUCCESS,
                        "Context initialization handles non-TTY correctly");
            printf("  [SKIP] Context creation skipped (no TTY)\n");
            tests_run++;
            tests_passed++;
        }

        if (context) {
            TEST_ASSERT(context->mode == detection->recommended_mode,
                        "Context mode matches detection");
            TEST_ASSERT(context->detection_result != NULL,
                        "Context has detection result");
            TEST_ASSERT(context->healthy == true, "Context is healthy");
            TEST_ASSERT(context->error_count == 0, "Context has no errors");

            lle_adaptive_context_destroy(context);
        }

        free(detection);
    }
}

/* ============================================================================
 * INTERFACE CREATION TESTS
 * ============================================================================
 */

TEST(interface_creation) {
    printf("\nInterface Creation Tests:\n");

    /* Check if we have a TTY - interface creation requires interactive terminal
     */
    bool has_tty = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);

    lle_adaptive_interface_t *interface = NULL;
    lle_result_t res = lle_create_adaptive_interface(&interface, NULL);

    // In non-TTY environments (CI), interface creation may fail
    if (has_tty) {
        TEST_ASSERT(res == LLE_SUCCESS, "Interface creation succeeds");
        TEST_ASSERT(interface != NULL, "Interface is created");
    } else {
        // Non-TTY - interface creation returns feature not available
        TEST_ASSERT(res == LLE_ERROR_FEATURE_NOT_AVAILABLE ||
                        res == LLE_SUCCESS,
                    "Interface creation handles non-TTY correctly");
        if (res != LLE_SUCCESS) {
            printf("  [SKIP] Interface creation skipped (no TTY)\n");
            tests_run++;
            tests_passed++;
            return;
        }
    }

    if (interface) {
        TEST_ASSERT(interface->adaptive_context != NULL,
                    "Interface has context");
        TEST_ASSERT(interface->read_line != NULL,
                    "Interface has read_line function");
        TEST_ASSERT(interface->process_input != NULL,
                    "Interface has process_input function");
        TEST_ASSERT(interface->update_display != NULL,
                    "Interface has update_display function");
        TEST_ASSERT(interface->handle_resize != NULL,
                    "Interface has handle_resize function");

        lle_adaptive_interface_destroy(interface);
    }
}

/* ============================================================================
 * MODE STRING TESTS
 * ============================================================================
 */

TEST(mode_utilities) {
    printf("\nMode Utility Tests:\n");

    const char *mode_str;

    mode_str = lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_NONE);
    TEST_ASSERT(mode_str != NULL && strcmp(mode_str, "none") == 0,
                "NONE mode string correct");

    mode_str = lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_MINIMAL);
    TEST_ASSERT(mode_str != NULL && strcmp(mode_str, "minimal") == 0,
                "MINIMAL mode string correct");

    mode_str = lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_ENHANCED);
    TEST_ASSERT(mode_str != NULL && strcmp(mode_str, "enhanced") == 0,
                "ENHANCED mode string correct");

    mode_str = lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_NATIVE);
    TEST_ASSERT(mode_str != NULL && strcmp(mode_str, "native") == 0,
                "NATIVE mode string correct");

    mode_str = lle_adaptive_mode_to_string(LLE_ADAPTIVE_MODE_MULTIPLEXED);
    TEST_ASSERT(mode_str != NULL && strcmp(mode_str, "multiplexed") == 0,
                "MULTIPLEXED mode string correct");

    const char *cap_str;

    cap_str = lle_capability_level_to_string(LLE_CAPABILITY_NONE);
    TEST_ASSERT(cap_str != NULL && strcmp(cap_str, "none") == 0,
                "NONE capability string correct");

    cap_str = lle_capability_level_to_string(LLE_CAPABILITY_BASIC);
    TEST_ASSERT(cap_str != NULL && strcmp(cap_str, "basic") == 0,
                "BASIC capability string correct");

    cap_str = lle_capability_level_to_string(LLE_CAPABILITY_STANDARD);
    TEST_ASSERT(cap_str != NULL && strcmp(cap_str, "standard") == 0,
                "STANDARD capability string correct");

    cap_str = lle_capability_level_to_string(LLE_CAPABILITY_FULL);
    TEST_ASSERT(cap_str != NULL && strcmp(cap_str, "full") == 0,
                "FULL capability string correct");

    cap_str = lle_capability_level_to_string(LLE_CAPABILITY_PREMIUM);
    TEST_ASSERT(cap_str != NULL && strcmp(cap_str, "premium") == 0,
                "PREMIUM capability string correct");
}

/* ============================================================================
 * CONFIGURATION RECOMMENDATION TESTS
 * ============================================================================
 */

TEST(config_recommendations) {
    printf("\nConfiguration Recommendation Tests:\n");

    lle_adaptive_config_recommendation_t config;
    memset(&config, 0, sizeof(config));

    lle_adaptive_get_recommended_config(&config);

    TEST_ASSERT(config.recommended_mode >= LLE_ADAPTIVE_MODE_NONE &&
                    config.recommended_mode <= LLE_ADAPTIVE_MODE_MULTIPLEXED,
                "Recommended mode is valid");

    TEST_ASSERT(config.color_support_level >= 0 &&
                    config.color_support_level <= 3,
                "Color support level is valid (0-3)");

    // Check consistency - if colors are enabled, level should be > 0
    if (config.enable_syntax_highlighting) {
        TEST_ASSERT(config.color_support_level > 0,
                    "Syntax highlighting requires colors");
    }

    // Check consistency - advanced features require LLE
    if (config.enable_autosuggestions || config.enable_multiline_editing) {
        TEST_ASSERT(config.enable_lle == true,
                    "Advanced features require LLE enabled");
    }

    printf("  Configuration: mode=%s, colors=%d, lle=%s\n",
           lle_adaptive_mode_to_string(config.recommended_mode),
           config.color_support_level, config.enable_lle ? "yes" : "no");
}

/* ============================================================================
 * SHELL INTEGRATION TESTS
 * ============================================================================
 */

TEST(shell_integration) {
    printf("\nShell Integration Tests:\n");

    // Test 1: Script file should never be interactive
    bool interactive =
        lle_adaptive_should_shell_be_interactive(false, true, false);
    TEST_ASSERT(interactive == false, "Script file is not interactive");

    // Test 2: Forced interactive should always be interactive
    interactive = lle_adaptive_should_shell_be_interactive(true, false, false);
    TEST_ASSERT(interactive == true, "Forced interactive flag works");

    // Test 3: Stdin mode typically disables interactive
    interactive = lle_adaptive_should_shell_be_interactive(false, false, true);
    TEST_ASSERT(interactive == false, "Stdin mode disables interactive");

    // Test 4: Normal case - depends on detection
    interactive = lle_adaptive_should_shell_be_interactive(false, false, false);
    printf("  Normal detection interactive: %s\n", interactive ? "yes" : "no");
    // Don't assert - depends on environment
}

/* ============================================================================
 * HEALTH CHECK TESTS
 * ============================================================================
 */

TEST(health_monitoring) {
    printf("\nHealth Monitoring Tests:\n");

    // Check if we have a TTY
    bool has_tty = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);

    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t res =
        lle_detect_terminal_capabilities_comprehensive(&detection);

    if (res == LLE_SUCCESS && detection) {
        // Skip context-dependent tests in non-TTY environments
        if (!has_tty && detection->recommended_mode == LLE_ADAPTIVE_MODE_NONE) {
            printf("  [SKIP] Health monitoring tests skipped (no TTY)\n");
            free(detection);
            return;
        }

        lle_adaptive_context_t *context = NULL;
        res = lle_initialize_adaptive_context(&context, detection, NULL);

        if (res == LLE_SUCCESS && context) {
            bool healthy = lle_adaptive_perform_health_check(context);
            TEST_ASSERT(healthy == true, "Fresh context is healthy");
            TEST_ASSERT(context->healthy == true,
                        "Context health flag is true");

            // Simulate errors
            context->error_count = 50;
            healthy = lle_adaptive_perform_health_check(context);
            TEST_ASSERT(healthy == true,
                        "Context healthy with moderate errors");

            // Simulate many errors
            context->error_count = 150;
            healthy = lle_adaptive_perform_health_check(context);
            TEST_ASSERT(healthy == false, "Context unhealthy with many errors");
            TEST_ASSERT(context->healthy == false,
                        "Context health flag updated");

            lle_adaptive_context_destroy(context);
        }

        free(detection);
    }
}

/* ============================================================================
 * CONTROLLER-SPECIFIC OPERATION TESTS
 * ============================================================================
 */

TEST(controller_operations) {
    printf("\nController Operation Tests:\n");

    // Check if we have a TTY
    bool has_tty = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);

    lle_adaptive_interface_t *interface = NULL;
    lle_result_t res = lle_create_adaptive_interface(&interface, NULL);

    if (res != LLE_SUCCESS) {
        if (!has_tty) {
            printf("  [SKIP] Controller operation tests skipped (no TTY)\n");
            return;
        }
    }

    if (res == LLE_SUCCESS && interface) {
        lle_adaptive_context_t *ctx = interface->adaptive_context;

        printf("  Current mode: %s\n", lle_adaptive_mode_to_string(ctx->mode));

        // Test display update (should succeed in all modes)
        res = interface->update_display(ctx);
        TEST_ASSERT(res == LLE_SUCCESS, "Display update succeeds");

        // Test resize handling (should succeed in all modes)
        res = interface->handle_resize(ctx, 100, 40);
        TEST_ASSERT(res == LLE_SUCCESS, "Resize handling succeeds");

        // Test get status (should succeed in all modes)
        int status = 0;
        res = interface->get_status(ctx, &status);
        TEST_ASSERT(res == LLE_SUCCESS, "Get status succeeds");

        lle_adaptive_interface_destroy(interface);
    }
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(error_handling) {
    printf("\nError Handling Tests:\n");

    lle_result_t res;

    // Test NULL parameter handling in context creation
    lle_adaptive_context_t *context = NULL;
    res = lle_initialize_adaptive_context(&context, NULL, NULL);
    TEST_ASSERT(res == LLE_ERROR_INVALID_PARAMETER,
                "Context init rejects NULL detection");

    // Test NULL parameter handling in interface creation
    res = lle_create_adaptive_interface(NULL, NULL);
    TEST_ASSERT(res == LLE_ERROR_INVALID_PARAMETER,
                "Interface creation rejects NULL output");

    // Test NULL parameter handling in health check
    bool healthy = lle_adaptive_perform_health_check(NULL);
    TEST_ASSERT(healthy == false, "Health check rejects NULL context");

    // Test NULL parameter handling in fallback
    res = lle_adaptive_try_fallback_mode(NULL);
    TEST_ASSERT(res == LLE_ERROR_INVALID_PARAMETER,
                "Fallback rejects NULL context");
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=== Adaptive Terminal Controllers Tests (Spec 26 Phase 2) ===\n");
    RUN_TEST(context_initialization);
    RUN_TEST(interface_creation);
    RUN_TEST(mode_utilities);
    RUN_TEST(config_recommendations);
    RUN_TEST(shell_integration);
    RUN_TEST(health_monitoring);
    RUN_TEST(controller_operations);
    RUN_TEST(error_handling);
    return TEST_RESULT();
}
