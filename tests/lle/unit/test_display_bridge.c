/**
 * @file test_display_bridge.c
 * @brief Unit Tests for LLE Display Bridge
 *
 * Tests the display bridge component that connects LLE's internal buffer/cursor
 * system with Lush's layered display architecture.
 *
 * SPECIFICATION: docs/lle_specification/08_display_integration_complete.md
 * IMPLEMENTATION: src/lle/display_bridge.c
 *
 * TEST COVERAGE:
 * - Bridge initialization and cleanup
 * - Parameter validation
 * - Memory allocation error handling
 * - Component initialization sequence
 * - Error context management
 * - State initialization
 *
 * NOTE: Uses mock memory pool (test_memory_mock.c) for standalone testing
 */

#include "display/display_controller.h"
#include "lle/display_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "test_framework.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Mock memory pool instance - just a non-NULL pointer for validation
/* The test_memory_mock.c provides lle_pool_alloc/free that don't actually use
 * this */
/// We cast a dummy value to satisfy the pointer requirement
static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ========================================================================== */
/// MOCK OBJECTS
/* ========================================================================== */

/// Note: display_controller_get_event_system is provided by libdisplay.a

/// @brief Mock display controller for testing
/// Uses real display_controller structure but minimal initialization
static display_controller_t *create_mock_display_controller(void) {
    display_controller_t *display = calloc(1, sizeof(display_controller_t));
    if (!display)
        return NULL;

    /// Create mock composition engine
    display->compositor = calloc(1, sizeof(composition_engine_t));
    if (!display->compositor) {
        free(display);
        return NULL;
    }

    /// Create mock command_layer - required by display_bridge
    display->compositor->command_layer = calloc(1, sizeof(command_layer_t));
    if (!display->compositor->command_layer) {
        free(display->compositor);
        free(display);
        return NULL;
    }

    /// Initialize minimal compositor state
    display->compositor->initialized = false;

    return display;
}

static void destroy_mock_display_controller(display_controller_t *display) {
    if (display) {
        if (display->compositor) {
            if (display->compositor->command_layer) {
                free(display->compositor->command_layer);
            }
            free(display->compositor);
        }
        free(display);
    }
}

/// @brief Mock editor context (opaque pointer for testing)
static void *create_mock_editor(void) {
    /// Just return a non-NULL pointer for validation tests
    static int dummy = 42;
    return &dummy;
}

/* ========================================================================== */
/// INITIALIZATION TESTS
/* ========================================================================== */

TEST(bridge_init_success) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Test successful initialization
    lle_result_t result =
        lle_display_bridge_init(&bridge, editor, display, mock_pool);

    ASSERT_EQ(result, LLE_SUCCESS, "Bridge init should succeed");
    ASSERT_NOT_NULL(bridge, "Bridge should be allocated");

    /// Verify bridge state
    ASSERT_NOT_NULL(bridge->composition_engine,
                    "Composition engine should be set");
    ASSERT_EQ(bridge->sync_state, LLE_DISPLAY_SYNC_IDLE,
              "Initial sync state should be IDLE");
    ASSERT_TRUE(bridge->force_full_render, "Initial render should be forced");
    ASSERT_EQ(bridge->render_skip_count, 0, "Render skip count should be 0");
    ASSERT_EQ(bridge->consecutive_errors, 0, "Consecutive errors should be 0");
    ASSERT_NOT_NULL(bridge->render_queue, "Render queue should be initialized");
    ASSERT_NOT_NULL(bridge->diff_tracker, "Diff tracker should be initialized");
    ASSERT_NOT_NULL(bridge->error_context,
                    "Error context should be initialized");

    /// Cleanup
    lle_display_bridge_cleanup(bridge);
    lle_pool_free(bridge);
    destroy_mock_display_controller(display);
}

TEST(bridge_init_null_bridge_pointer) {
    void *editor = create_mock_editor();
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Test NULL bridge pointer
    lle_result_t result =
        lle_display_bridge_init(NULL, editor, display, mock_pool);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should fail with NULL bridge pointer");

    /// Cleanup
    destroy_mock_display_controller(display);
}

TEST(bridge_init_null_editor) {
    lle_display_bridge_t *bridge = NULL;
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Test NULL editor - now allowed since editor is set per readline call
    lle_result_t result =
        lle_display_bridge_init(&bridge, NULL, display, mock_pool);

    ASSERT_EQ(result, LLE_SUCCESS, "Should succeed with NULL editor");
    ASSERT_NOT_NULL(bridge, "Bridge should be allocated");

    /// Cleanup
    lle_display_bridge_cleanup(bridge);
    destroy_mock_display_controller(display);
}

TEST(bridge_init_null_display) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();

    /// Test NULL display
    lle_result_t result =
        lle_display_bridge_init(&bridge, editor, NULL, mock_pool);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should fail with NULL display");
    ASSERT_NULL(bridge, "Bridge should not be allocated on failure");
}

TEST(bridge_init_null_memory_pool) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Test NULL memory pool
    lle_result_t result =
        lle_display_bridge_init(&bridge, editor, display, NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should fail with NULL memory pool");
    ASSERT_NULL(bridge, "Bridge should not be allocated on failure");

    /// Cleanup
    destroy_mock_display_controller(display);
}

TEST(bridge_init_invalid_display_no_compositor) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();
    display_controller_t *display = calloc(1, sizeof(display_controller_t));

    ASSERT_NOT_NULL(display, "Display allocation failed");

    /// Display has no compositor - should fail
    display->compositor = NULL;

    lle_result_t result =
        lle_display_bridge_init(&bridge, editor, display, mock_pool);

    ASSERT_EQ(result, LLE_ERROR_INVALID_STATE,
              "Should fail with invalid display state");
    ASSERT_NULL(bridge, "Bridge should not be allocated on failure");

    /// Cleanup
    free(display);
}

/* ========================================================================== */
/// CLEANUP TESTS
/* ========================================================================== */

TEST(bridge_cleanup_success) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Initialize bridge
    lle_result_t result =
        lle_display_bridge_init(&bridge, editor, display, mock_pool);
    ASSERT_EQ(result, LLE_SUCCESS, "Bridge init should succeed");
    ASSERT_NOT_NULL(bridge, "Bridge should be allocated");

    /// Test cleanup
    result = lle_display_bridge_cleanup(bridge);
    ASSERT_EQ(result, LLE_SUCCESS, "Bridge cleanup should succeed");

    /// Verify all resources are cleared
    ASSERT_NULL(bridge->error_context, "Error context should be cleared");
    ASSERT_NULL(bridge->diff_tracker, "Diff tracker should be cleared");
    ASSERT_NULL(bridge->render_queue, "Render queue should be cleared");
    ASSERT_NULL(bridge->composition_engine,
                "Composition engine ref should be cleared");

    /// Cleanup
    lle_pool_free(bridge);
    destroy_mock_display_controller(display);
}

TEST(bridge_cleanup_null_bridge) {
    /// Test cleanup with NULL pointer - should handle gracefully
    lle_result_t result = lle_display_bridge_cleanup(NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should handle NULL gracefully");
}

/* ========================================================================== */
/// CONVENIENCE WRAPPER TEST
/* ========================================================================== */

TEST(bridge_create_wrapper) {
    lle_display_bridge_t *bridge = NULL;
    void *editor = create_mock_editor();
    display_controller_t *display = create_mock_display_controller();

    ASSERT_NOT_NULL(display, "Mock display creation failed");

    /// Test convenience wrapper function
    lle_result_t result =
        lle_display_create_bridge(&bridge, editor, display, mock_pool);

    ASSERT_EQ(result, LLE_SUCCESS, "Bridge create wrapper should succeed");
    ASSERT_NOT_NULL(bridge, "Bridge should be allocated");

    /// Cleanup
    lle_display_bridge_cleanup(bridge);
    lle_pool_free(bridge);
    destroy_mock_display_controller(display);
}

/* ========================================================================== */
/// TEST RUNNER
/* ========================================================================== */

int main(void) {
    printf(
        "=================================================================\n");
    printf("LLE Display Bridge Unit Tests\n");
    printf("================================================================="
           "\n\n");

    /// Initialization tests
    printf("Initialization Tests:\n");
    printf(
        "-----------------------------------------------------------------\n");
    RUN_TEST(bridge_init_success);
    RUN_TEST(bridge_init_null_bridge_pointer);
    RUN_TEST(bridge_init_null_editor);
    RUN_TEST(bridge_init_null_display);
    RUN_TEST(bridge_init_null_memory_pool);
    RUN_TEST(bridge_init_invalid_display_no_compositor);

    /// Cleanup tests
    printf("\nCleanup Tests:\n");
    printf(
        "-----------------------------------------------------------------\n");
    RUN_TEST(bridge_cleanup_success);
    RUN_TEST(bridge_cleanup_null_bridge);

    /// Convenience wrapper tests
    printf("\nConvenience Wrapper Tests:\n");
    printf(
        "-----------------------------------------------------------------\n");
    RUN_TEST(bridge_create_wrapper);

    return TEST_RESULT();
}
