/**
 * @file test_widget_hooks.c
 * @brief Unit tests for LLE Widget Hooks Manager
 * @date 2025-01-17
 *
 * Tests for widget hooks manager functionality including hook registration,
 * triggering, and lifecycle management.
 */

#include "../functional/test_memory_mock.h"
#include "lle/lle_editor.h"
#include "lle/widget_hooks.h"
#include "lle/widget_system.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test state tracking
static int g_hook_callback_called = 0;
static int g_hook_callback_count = 0;
static lle_editor_t *g_hook_editor_arg = NULL;
static lle_widget_hook_type_t g_last_hook_type = 0;

// Test widget callbacks
static lle_result_t test_hook_widget_callback(lle_editor_t *editor,
                                              void *user_data) {
    (void)user_data;
    g_hook_callback_called = 1;
    g_hook_callback_count++;
    g_hook_editor_arg = editor;
    return LLE_SUCCESS;
}

static lle_result_t test_hook_widget_error(lle_editor_t *editor,
                                           void *user_data) {
    (void)editor;
    (void)user_data;
    g_hook_callback_called = 1;
    g_hook_callback_count++;
    return LLE_ERROR_INVALID_STATE;
}

// Helper to reset test state
static void reset_test_state(void) {
    g_hook_callback_called = 0;
    g_hook_callback_count = 0;
    g_hook_editor_arg = NULL;
    g_last_hook_type = 0;
}

// Test: Initialize hooks manager
TEST(hooks_manager_init) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;

    // Initialize registry first
    lle_result_t result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Initialize hooks manager
    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(manager != NULL);

    // Verify all hook lists are empty
    for (int i = 0; i < LLE_HOOK_COUNT; i++) {
        int count =
            lle_widget_hook_get_count(manager, (lle_widget_hook_type_t)i);
        ASSERT(count == 0);
    }

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Invalid parameter checks for init
TEST(hooks_manager_init_invalid_params) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_result_t result;

    // Initialize registry for valid tests
    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // NULL manager pointer
    result = lle_widget_hooks_manager_init(NULL, registry, pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    // NULL registry
    result = lle_widget_hooks_manager_init(&manager, NULL, pool);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    // NULL pool
    result = lle_widget_hooks_manager_init(&manager, registry, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Register hook
TEST(hook_register) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;

    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Register a widget
    result = lle_widget_register(registry, "test-hook-widget",
                                 test_hook_widget_callback, LLE_WIDGET_BUILTIN,
                                 NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register widget to hook
    result = lle_widget_hook_register(manager, LLE_HOOK_LINE_INIT,
                                      "test-hook-widget");
    ASSERT(result == LLE_SUCCESS);

    // Verify hook count
    int count = lle_widget_hook_get_count(manager, LLE_HOOK_LINE_INIT);
    ASSERT(count == 1);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Register multiple hooks
TEST(hook_register_multiple) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Register widgets
    result = lle_widget_register(registry, "widget1", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "widget2", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "widget3", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register all to same hook
    result =
        lle_widget_hook_register(manager, LLE_HOOK_BUFFER_MODIFIED, "widget1");
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_widget_hook_register(manager, LLE_HOOK_BUFFER_MODIFIED, "widget2");
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_widget_hook_register(manager, LLE_HOOK_BUFFER_MODIFIED, "widget3");
    ASSERT(result == LLE_SUCCESS);

    // Verify count
    int count = lle_widget_hook_get_count(manager, LLE_HOOK_BUFFER_MODIFIED);
    ASSERT(count == 3);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Register duplicate hook
TEST(hook_register_duplicate) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Register widget
    result =
        lle_widget_register(registry, "dup-widget", test_hook_widget_callback,
                            LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register to hook twice
    result =
        lle_widget_hook_register(manager, LLE_HOOK_PRE_COMMAND, "dup-widget");
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_widget_hook_register(manager, LLE_HOOK_PRE_COMMAND, "dup-widget");
    ASSERT(result == LLE_ERROR_ALREADY_EXISTS);

    // Should still only have one
    int count = lle_widget_hook_get_count(manager, LLE_HOOK_PRE_COMMAND);
    ASSERT(count == 1);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Trigger hook
TEST(hook_trigger) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_editor_t editor = {0};
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Set up editor with registry
    editor.widget_registry = registry;
    editor.widget_hooks_manager = manager;

    // Register widget
    result =
        lle_widget_register(registry, "trigger-test", test_hook_widget_callback,
                            LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register to hook
    result =
        lle_widget_hook_register(manager, LLE_HOOK_LINE_INIT, "trigger-test");
    ASSERT(result == LLE_SUCCESS);

    // Trigger hook
    result = lle_widget_hook_trigger(manager, LLE_HOOK_LINE_INIT, &editor);
    ASSERT(result == LLE_SUCCESS);

    // Verify callback was called
    ASSERT(g_hook_callback_called == 1);
    ASSERT(g_hook_callback_count == 1);
    ASSERT(g_hook_editor_arg == &editor);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Trigger multiple hooks
TEST(hook_trigger_multiple) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_editor_t editor = {0};
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    editor.widget_registry = registry;
    editor.widget_hooks_manager = manager;

    // Register three widgets
    result = lle_widget_register(registry, "hook1", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "hook2", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "hook3", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register all to same hook
    result = lle_widget_hook_register(manager, LLE_HOOK_POST_COMMAND, "hook1");
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hook_register(manager, LLE_HOOK_POST_COMMAND, "hook2");
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hook_register(manager, LLE_HOOK_POST_COMMAND, "hook3");
    ASSERT(result == LLE_SUCCESS);

    // Trigger hook
    result = lle_widget_hook_trigger(manager, LLE_HOOK_POST_COMMAND, &editor);
    ASSERT(result == LLE_SUCCESS);

    // Verify all three callbacks were called
    ASSERT(g_hook_callback_count == 3);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Trigger hook with error widget (should continue)
TEST(hook_trigger_with_error) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_editor_t editor = {0};
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    editor.widget_registry = registry;
    editor.widget_hooks_manager = manager;

    // Register normal widget, error widget, then normal widget
    result = lle_widget_register(registry, "normal1", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "error", test_hook_widget_error,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_register(registry, "normal2", test_hook_widget_callback,
                                 LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register to hook
    result =
        lle_widget_hook_register(manager, LLE_HOOK_COMPLETION_START, "normal1");
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_widget_hook_register(manager, LLE_HOOK_COMPLETION_START, "error");
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_widget_hook_register(manager, LLE_HOOK_COMPLETION_START, "normal2");
    ASSERT(result == LLE_SUCCESS);

    // Trigger hook - should continue despite error
    result =
        lle_widget_hook_trigger(manager, LLE_HOOK_COMPLETION_START, &editor);
    ASSERT(result == LLE_SUCCESS); // Hook manager continues on error

    // All three should have been called
    ASSERT(g_hook_callback_count == 3);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Unregister hook
TEST(hook_unregister) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Register widget
    result =
        lle_widget_register(registry, "unreg-test", test_hook_widget_callback,
                            LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    // Register to hook
    result = lle_widget_hook_register(manager, LLE_HOOK_HISTORY_SEARCH,
                                      "unreg-test");
    ASSERT(result == LLE_SUCCESS);

    ASSERT(lle_widget_hook_get_count(manager, LLE_HOOK_HISTORY_SEARCH) == 1);

    // Unregister
    result = lle_widget_hook_unregister(manager, LLE_HOOK_HISTORY_SEARCH,
                                        "unreg-test");
    ASSERT(result == LLE_SUCCESS);

    ASSERT(lle_widget_hook_get_count(manager, LLE_HOOK_HISTORY_SEARCH) == 0);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Enable/disable hooks globally
TEST(hook_enable_disable) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_editor_t editor = {0};
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    editor.widget_registry = registry;
    editor.widget_hooks_manager = manager;

    // Register widget
    result =
        lle_widget_register(registry, "enable-test", test_hook_widget_callback,
                            LLE_WIDGET_BUILTIN, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hook_register(manager, LLE_HOOK_TERMINAL_RESIZE,
                                      "enable-test");
    ASSERT(result == LLE_SUCCESS);

    // Verify hooks are enabled by default
    ASSERT(lle_widget_hooks_enabled(manager) == true);

    // Trigger - should execute
    result =
        lle_widget_hook_trigger(manager, LLE_HOOK_TERMINAL_RESIZE, &editor);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(g_hook_callback_count == 1);

    // Disable hooks globally
    result = lle_widget_hooks_disable(manager);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(lle_widget_hooks_enabled(manager) == false);

    // Trigger - should NOT execute
    reset_test_state();
    result =
        lle_widget_hook_trigger(manager, LLE_HOOK_TERMINAL_RESIZE, &editor);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(g_hook_callback_count == 0); // Not called

    // Re-enable hooks globally
    result = lle_widget_hooks_enable(manager);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(lle_widget_hooks_enabled(manager) == true);

    // Trigger - should execute again
    result =
        lle_widget_hook_trigger(manager, LLE_HOOK_TERMINAL_RESIZE, &editor);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(g_hook_callback_count == 1);

    // Cleanup
    lle_pool_destroy(pool);
}

// Test: Hook count
TEST(hook_count) {
    reset_test_state();

    lle_memory_pool_t *pool = lle_pool_create();
    lle_widget_registry_t *registry = NULL;
    lle_widget_hooks_manager_t *manager = NULL;
    lle_result_t result;

    result = lle_widget_registry_init(&registry, pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_widget_hooks_manager_init(&manager, registry, pool);
    ASSERT(result == LLE_SUCCESS);

    // Initially zero
    ASSERT(lle_widget_hook_get_count(manager, LLE_HOOK_LINE_FINISH) == 0);

    // Register widgets
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "count-widget-%d", i);
        result = lle_widget_register(registry, name, test_hook_widget_callback,
                                     LLE_WIDGET_BUILTIN, NULL);
        ASSERT(result == LLE_SUCCESS);

        result = lle_widget_hook_register(manager, LLE_HOOK_LINE_FINISH, name);
        ASSERT(result == LLE_SUCCESS);

        ASSERT(lle_widget_hook_get_count(manager, LLE_HOOK_LINE_FINISH) ==
               (size_t)(i + 1));
    }

    // Cleanup
    lle_pool_destroy(pool);
}

// Main test runner
int main(void) {
    printf("=== Widget Hooks Manager Tests ===\n\n");

    RUN_TEST(hooks_manager_init);
    RUN_TEST(hooks_manager_init_invalid_params);
    RUN_TEST(hook_register);
    RUN_TEST(hook_register_multiple);
    RUN_TEST(hook_register_duplicate);
    RUN_TEST(hook_trigger);
    RUN_TEST(hook_trigger_multiple);
    RUN_TEST(hook_trigger_with_error);
    RUN_TEST(hook_unregister);
    RUN_TEST(hook_enable_disable);
    RUN_TEST(hook_count);
    return TEST_RESULT();
}
