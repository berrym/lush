/**
 * @file test_lush_plugin.c
 * @brief Unit tests for lush plugin system
 *
 * Tests the plugin system including:
 * - Result and state string conversions
 * - Plugin manager creation and destruction
 * - Plugin loading and unloading
 * - Permission checking
 * - Registration APIs (builtins, hooks, completions, events)
 * - Variable access APIs
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lush_plugin.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Test framework macros

/* ============================================================================
 * RESULT STRING TESTS
 * ============================================================================
 */

TEST(result_string_ok) {
    const char *str = lush_plugin_result_string(LUSH_PLUGIN_OK);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Success", "OK should return 'Success'");
}

TEST(result_string_error) {
    const char *str = lush_plugin_result_string(LUSH_PLUGIN_ERROR);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Generic error", "ERROR should return 'Generic error'");
}

TEST(result_string_not_found) {
    const char *str = lush_plugin_result_string(LUSH_PLUGIN_ERROR_NOT_FOUND);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Plugin not found",
                  "NOT_FOUND should return 'Plugin not found'");
}

TEST(result_string_load_failed) {
    const char *str = lush_plugin_result_string(LUSH_PLUGIN_ERROR_LOAD_FAILED);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Failed to load plugin", "LOAD_FAILED string");
}

TEST(result_string_symbol_not_found) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_SYMBOL_NOT_FOUND);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Required symbol not found", "SYMBOL_NOT_FOUND string");
}

TEST(result_string_version_mismatch) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_VERSION_MISMATCH);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "API version mismatch", "VERSION_MISMATCH string");
}

TEST(result_string_init_failed) {
    const char *str = lush_plugin_result_string(LUSH_PLUGIN_ERROR_INIT_FAILED);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Plugin initialization failed", "INIT_FAILED string");
}

TEST(result_string_permission_denied) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_PERMISSION_DENIED);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Permission denied", "PERMISSION_DENIED string");
}

TEST(result_string_already_loaded) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_ALREADY_LOADED);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Plugin already loaded", "ALREADY_LOADED string");
}

TEST(result_string_invalid_plugin) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_INVALID_PLUGIN);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Invalid plugin definition", "INVALID_PLUGIN string");
}

TEST(result_string_out_of_memory) {
    const char *str =
        lush_plugin_result_string(LUSH_PLUGIN_ERROR_OUT_OF_MEMORY);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Out of memory", "OUT_OF_MEMORY string");
}

TEST(result_string_unknown) {
    const char *str = lush_plugin_result_string((lush_plugin_result_t)9999);
    ASSERT_NOT_NULL(str, "Result string should not be NULL");
    ASSERT_STR_EQ(str, "Unknown error",
                  "Unknown value should return 'Unknown error'");
}

/* ============================================================================
 * STATE STRING TESTS
 * ============================================================================
 */

TEST(state_string_unloaded) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_UNLOADED);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "unloaded", "UNLOADED state string");
}

TEST(state_string_loading) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_LOADING);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "loading", "LOADING state string");
}

TEST(state_string_loaded) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_LOADED);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "loaded", "LOADED state string");
}

TEST(state_string_initializing) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_INITIALIZING);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "initializing", "INITIALIZING state string");
}

TEST(state_string_active) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_ACTIVE);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "active", "ACTIVE state string");
}

TEST(state_string_suspended) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_SUSPENDED);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "suspended", "SUSPENDED state string");
}

TEST(state_string_error) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_ERROR);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "error", "ERROR state string");
}

TEST(state_string_unloading) {
    const char *str = lush_plugin_state_string(LUSH_PLUGIN_STATE_UNLOADING);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "unloading", "UNLOADING state string");
}

TEST(state_string_unknown) {
    const char *str = lush_plugin_state_string((lush_plugin_state_t)9999);
    ASSERT_NOT_NULL(str, "State string should not be NULL");
    ASSERT_STR_EQ(str, "unknown", "Unknown state should return 'unknown'");
}

/* ============================================================================
 * PLUGIN MANAGER CREATION TESTS
 * ============================================================================
 */

TEST(manager_create_null_ptr) {
    lush_plugin_result_t result = lush_plugin_manager_create(NULL, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_create_default_config) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_result_t result = lush_plugin_manager_create(&manager, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "Create with NULL config should succeed");
    ASSERT_NOT_NULL(manager, "Manager should be created");
    ASSERT_TRUE(manager->active, "Manager should be active");
    ASSERT_EQ(manager->plugin_count, 0, "Plugin count should be 0");
    ASSERT_NULL(manager->plugins, "Plugins list should be NULL");
    lush_plugin_manager_destroy(manager);
}

TEST(manager_create_custom_config) {
    lush_plugin_manager_config_t config = {.auto_load = true,
                                           .default_permissions =
                                               LUSH_PLUGIN_PERM_ALL,
                                           .enable_sandbox = false,
                                           .max_plugins = 10};
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_result_t result = lush_plugin_manager_create(&manager, &config);
    ASSERT_EQ(result, LUSH_PLUGIN_OK,
              "Create with custom config should succeed");
    ASSERT_NOT_NULL(manager, "Manager should be created");
    ASSERT_TRUE(manager->config.auto_load, "auto_load should be set");
    ASSERT_EQ(manager->config.max_plugins, 10, "max_plugins should be 10");
    ASSERT_FALSE(manager->config.enable_sandbox, "sandbox should be disabled");
    lush_plugin_manager_destroy(manager);
}

TEST(manager_destroy_null) {
    /// Should not crash
    lush_plugin_manager_destroy(NULL);
}

/* ============================================================================
 * PLUGIN MANAGER SET FUNCTIONS
 * ============================================================================
 */

TEST(manager_set_executor) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    /// Use a fake executor pointer - we're just testing the setter
    struct executor *fake_executor = (struct executor *)0x12345678;
    lush_plugin_manager_set_executor(manager, fake_executor);
    ASSERT_EQ(manager->executor, fake_executor, "Executor should be set");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_set_executor_null_manager) {
    /// Should not crash
    lush_plugin_manager_set_executor(NULL, NULL);
}

TEST(manager_set_symtable) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    /// Use a fake symtable pointer
    struct symtable *fake_symtable = (struct symtable *)0xABCDEF00;
    lush_plugin_manager_set_symtable(manager, fake_symtable);
    ASSERT_EQ(manager->symtable, fake_symtable, "Symtable should be set");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_set_symtable_null_manager) {
    /// Should not crash
    lush_plugin_manager_set_symtable(NULL, NULL);
}

/* ============================================================================
 * PLUGIN LOAD TESTS
 * ============================================================================
 */

TEST(manager_load_null_manager) {
    lush_plugin_result_t result =
        lush_plugin_manager_load(NULL, "/test.so", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_load_null_path) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result = lush_plugin_manager_load(manager, NULL, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL path should return error");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_load_nonexistent) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result = lush_plugin_manager_load(
        manager, "/nonexistent/path/to/plugin.so", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_LOAD_FAILED,
              "Nonexistent plugin should fail to load");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_load_inactive_manager) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);
    manager->active = false;

    lush_plugin_result_t result =
        lush_plugin_manager_load(manager, "/test.so", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR,
              "Inactive manager should return error");

    /// Restore for cleanup
    manager->active = true;
    lush_plugin_manager_destroy(manager);
}

TEST(manager_load_max_plugins_reached) {
    lush_plugin_manager_config_t config = {
        .max_plugins = 0 /// Will set to 0 to simulate already at max
    };
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, &config);

    /// Manually set plugin count to 1 and max to 1 to simulate limit
    manager->config.max_plugins = 1;
    manager->plugin_count = 1;

    lush_plugin_result_t result =
        lush_plugin_manager_load(manager, "/test.so", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR,
              "Should fail when max plugins reached");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PLUGIN LOAD BY NAME TESTS
 * ============================================================================
 */

TEST(manager_load_by_name_null_manager) {
    lush_plugin_result_t result =
        lush_plugin_manager_load_by_name(NULL, "test", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_load_by_name_null_name) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result =
        lush_plugin_manager_load_by_name(manager, NULL, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_load_by_name_not_found) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result =
        lush_plugin_manager_load_by_name(manager, "nonexistent_plugin", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_NOT_FOUND,
              "Nonexistent plugin name should return NOT_FOUND");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PLUGIN UNLOAD TESTS
 * ============================================================================
 */

TEST(manager_unload_null_manager) {
    lush_plugin_result_t result = lush_plugin_manager_unload(NULL, "test");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_unload_null_name) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result = lush_plugin_manager_unload(manager, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_unload_not_found) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result =
        lush_plugin_manager_unload(manager, "nonexistent");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_NOT_FOUND,
              "Unloading nonexistent plugin should return NOT_FOUND");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PLUGIN FIND TESTS
 * ============================================================================
 */

TEST(manager_find_null_manager) {
    lush_plugin_t *plugin = lush_plugin_manager_find(NULL, "test");
    ASSERT_NULL(plugin, "NULL manager should return NULL");
}

TEST(manager_find_null_name) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_t *plugin = lush_plugin_manager_find(manager, NULL);
    ASSERT_NULL(plugin, "NULL name should return NULL");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_find_not_loaded) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_t *plugin = lush_plugin_manager_find(manager, "nonexistent");
    ASSERT_NULL(plugin, "Nonexistent plugin should return NULL");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PLUGIN LIST TESTS
 * ============================================================================
 */

TEST(manager_list_null_manager) {
    size_t count = 0;
    lush_plugin_result_t result = lush_plugin_manager_list(NULL, NULL, &count);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_list_null_count) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result = lush_plugin_manager_list(manager, NULL, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL count should return error");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_list_empty) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    size_t count = 10; /// Start with non-zero to verify it gets updated
    lush_plugin_result_t result =
        lush_plugin_manager_list(manager, NULL, &count);
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "List should succeed");
    ASSERT_EQ(count, 0, "Count should be 0 for empty manager");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PLUGIN RELOAD TESTS
 * ============================================================================
 */

TEST(manager_reload_null_manager) {
    lush_plugin_result_t result = lush_plugin_manager_reload(NULL, "test");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL manager should return error");
}

TEST(manager_reload_null_name) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result = lush_plugin_manager_reload(manager, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");

    lush_plugin_manager_destroy(manager);
}

TEST(manager_reload_not_found) {
    lush_plugin_manager_t *manager = NULL;
    lush_plugin_manager_create(&manager, NULL);

    lush_plugin_result_t result =
        lush_plugin_manager_reload(manager, "nonexistent");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_NOT_FOUND,
              "Reloading nonexistent plugin should return NOT_FOUND");

    lush_plugin_manager_destroy(manager);
}

/* ============================================================================
 * PERMISSION TESTS
 * ============================================================================
 */

TEST(has_permission_null_context) {
    bool result = lush_plugin_has_permission(NULL, LUSH_PLUGIN_PERM_READ_VARS);
    ASSERT_FALSE(result, "NULL context should return false");
}

TEST(has_permission_granted) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_READ_VARS |
                                     LUSH_PLUGIN_PERM_WRITE_VARS};

    ASSERT_TRUE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_READ_VARS),
                "Should have READ_VARS permission");
    ASSERT_TRUE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_WRITE_VARS),
                "Should have WRITE_VARS permission");
}

TEST(has_permission_not_granted) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_READ_VARS};

    ASSERT_FALSE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_WRITE_VARS),
                 "Should not have WRITE_VARS permission");
    ASSERT_FALSE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_EXEC),
                 "Should not have EXEC permission");
}

TEST(has_permission_multiple) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_READ_VARS};

    /* Test that asking for multiple permissions when only one is granted fails
     */
    lush_plugin_permission_t multi =
        LUSH_PLUGIN_PERM_READ_VARS | LUSH_PLUGIN_PERM_WRITE_VARS;
    ASSERT_FALSE(lush_plugin_has_permission(&ctx, multi),
                 "Should not have both permissions");
}

TEST(has_permission_all) {
    lush_plugin_context_t ctx = {.granted_permissions = LUSH_PLUGIN_PERM_ALL};

    ASSERT_TRUE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_READ_VARS),
                "ALL should include READ_VARS");
    ASSERT_TRUE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_WRITE_VARS),
                "ALL should include WRITE_VARS");
    ASSERT_TRUE(lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_EXEC),
                "ALL should include EXEC");
    ASSERT_TRUE(
        lush_plugin_has_permission(&ctx, LUSH_PLUGIN_PERM_REGISTER_BUILTIN),
        "ALL should include REGISTER_BUILTIN");
}

/* ============================================================================
 * REGISTRATION API TESTS
 * ============================================================================
 */

/// Dummy builtin function for testing
static int dummy_builtin(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

TEST(register_builtin_null_context) {
    lush_plugin_result_t result =
        lush_plugin_register_builtin(NULL, "test", dummy_builtin);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(register_builtin_null_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_BUILTIN};
    lush_plugin_result_t result =
        lush_plugin_register_builtin(&ctx, NULL, dummy_builtin);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");
}

TEST(register_builtin_null_fn) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_BUILTIN};
    lush_plugin_result_t result =
        lush_plugin_register_builtin(&ctx, "test", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL function should return error");
}

TEST(register_builtin_no_permission) {
    lush_plugin_t plugin = {0};
    lush_plugin_context_t ctx = {
        .plugin = &plugin, .granted_permissions = 0 /// No permissions
    };
    lush_plugin_result_t result =
        lush_plugin_register_builtin(&ctx, "test", dummy_builtin);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_PERMISSION_DENIED,
              "No permission should return PERMISSION_DENIED");
}

TEST(register_builtin_no_plugin) {
    lush_plugin_context_t ctx = {.plugin = NULL,
                                 .granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_BUILTIN};
    lush_plugin_result_t result =
        lush_plugin_register_builtin(&ctx, "test", dummy_builtin);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL plugin should return error");
}

TEST(unregister_builtin_null_context) {
    lush_plugin_result_t result = lush_plugin_unregister_builtin(NULL, "test");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(unregister_builtin_null_name) {
    lush_plugin_context_t ctx = {0};
    lush_plugin_result_t result = lush_plugin_unregister_builtin(&ctx, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");
}

/* ============================================================================
 * HOOK REGISTRATION TESTS
 * ============================================================================
 */

/// Dummy hook function for testing
static void dummy_hook(lush_plugin_context_t *ctx, const char *event_data) {
    (void)ctx;
    (void)event_data;
}

TEST(register_hook_null_context) {
    lush_plugin_result_t result =
        lush_plugin_register_hook(NULL, "precmd", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(register_hook_null_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, NULL, dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");
}

TEST(register_hook_null_fn) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "precmd", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL function should return error");
}

TEST(register_hook_no_permission) {
    lush_plugin_context_t ctx = {
        .granted_permissions = 0 /// No permissions
    };
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "precmd", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_PERMISSION_DENIED,
              "No permission should return PERMISSION_DENIED");
}

TEST(register_hook_valid_precmd) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "precmd", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "precmd hook should succeed");
}

TEST(register_hook_valid_preexec) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "preexec", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "preexec hook should succeed");
}

TEST(register_hook_valid_chpwd) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "chpwd", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "chpwd hook should succeed");
}

TEST(register_hook_invalid_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_REGISTER_HOOK};
    lush_plugin_result_t result =
        lush_plugin_register_hook(&ctx, "invalid_hook", dummy_hook);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR,
              "Invalid hook name should return error");
}

/* ============================================================================
 * COMPLETION REGISTRATION TESTS
 * ============================================================================
 */

/// Dummy completion function for testing
static int dummy_completion(lush_plugin_context_t *ctx, const char *line,
                            size_t cursor, char ***completions, size_t *count) {
    (void)ctx;
    (void)line;
    (void)cursor;
    (void)completions;
    (void)count;
    return 0;
}

TEST(register_completion_null_context) {
    lush_plugin_result_t result =
        lush_plugin_register_completion(NULL, "test", dummy_completion);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(register_completion_null_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_COMPLETIONS};
    lush_plugin_result_t result =
        lush_plugin_register_completion(&ctx, NULL, dummy_completion);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");
}

TEST(register_completion_null_fn) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_COMPLETIONS};
    lush_plugin_result_t result =
        lush_plugin_register_completion(&ctx, "test", NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL function should return error");
}

TEST(register_completion_no_permission) {
    lush_plugin_context_t ctx = {
        .granted_permissions = 0 /// No permissions
    };
    lush_plugin_result_t result =
        lush_plugin_register_completion(&ctx, "test", dummy_completion);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_PERMISSION_DENIED,
              "No permission should return PERMISSION_DENIED");
}

TEST(register_completion_with_permission) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_COMPLETIONS};
    lush_plugin_result_t result =
        lush_plugin_register_completion(&ctx, "test", dummy_completion);
    ASSERT_EQ(result, LUSH_PLUGIN_OK,
              "Registration with permission should succeed");
}

/* ============================================================================
 * EVENT SUBSCRIPTION TESTS
 * ============================================================================
 */

/// Dummy event function for testing
static void dummy_event(lush_plugin_context_t *ctx, int event_type,
                        void *event_data) {
    (void)ctx;
    (void)event_type;
    (void)event_data;
}

TEST(subscribe_event_null_context) {
    lush_plugin_result_t result =
        lush_plugin_subscribe_event(NULL, 0, dummy_event);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(subscribe_event_null_fn) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_EVENTS};
    lush_plugin_result_t result = lush_plugin_subscribe_event(&ctx, 0, NULL);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL function should return error");
}

TEST(subscribe_event_no_permission) {
    lush_plugin_context_t ctx = {
        .granted_permissions = 0 /// No permissions
    };
    lush_plugin_result_t result =
        lush_plugin_subscribe_event(&ctx, 0, dummy_event);
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_PERMISSION_DENIED,
              "No permission should return PERMISSION_DENIED");
}

TEST(subscribe_event_with_permission) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_EVENTS};
    lush_plugin_result_t result =
        lush_plugin_subscribe_event(&ctx, 0, dummy_event);
    ASSERT_EQ(result, LUSH_PLUGIN_OK,
              "Subscription with permission should succeed");
}

/* ============================================================================
 * VARIABLE ACCESS TESTS
 * ============================================================================
 */

TEST(get_var_null_context) {
    const char *result = lush_plugin_get_var(NULL, "HOME");
    ASSERT_NULL(result, "NULL context should return NULL");
}

TEST(get_var_null_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_READ_VARS};
    const char *result = lush_plugin_get_var(&ctx, NULL);
    ASSERT_NULL(result, "NULL name should return NULL");
}

TEST(get_var_no_permission) {
    lush_plugin_context_t ctx = {
        .granted_permissions = 0 /// No permissions
    };
    const char *result = lush_plugin_get_var(&ctx, "HOME");
    ASSERT_NULL(result, "No permission should return NULL");
}

TEST(set_var_null_context) {
    lush_plugin_result_t result = lush_plugin_set_var(NULL, "TEST", "value");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL context should return error");
}

TEST(set_var_null_name) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_WRITE_VARS};
    lush_plugin_result_t result = lush_plugin_set_var(&ctx, NULL, "value");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR, "NULL name should return error");
}

TEST(set_var_no_permission) {
    lush_plugin_context_t ctx = {
        .granted_permissions = 0 /// No permissions
    };
    lush_plugin_result_t result = lush_plugin_set_var(&ctx, "TEST", "value");
    ASSERT_EQ(result, LUSH_PLUGIN_ERROR_PERMISSION_DENIED,
              "No permission should return PERMISSION_DENIED");
}

TEST(set_var_with_permission) {
    lush_plugin_context_t ctx = {.granted_permissions =
                                     LUSH_PLUGIN_PERM_WRITE_VARS};
    lush_plugin_result_t result = lush_plugin_set_var(&ctx, "TEST", "value");
    ASSERT_EQ(result, LUSH_PLUGIN_OK, "Set with permission should succeed");
}

/* ============================================================================
 * LOG TESTS
 * ============================================================================
 */

TEST(log_null_context) {
    /// Should not crash
    lush_plugin_log(NULL, 1, "Test message");
}

TEST(log_null_format) {
    lush_plugin_context_t ctx = {0};
    /// Should not crash
    lush_plugin_log(&ctx, 1, NULL);
}

TEST(log_with_valid_context) {
    lush_plugin_def_t def = {.name = "test_plugin"};
    lush_plugin_t plugin = {.def = &def};
    lush_plugin_context_t ctx = {.plugin = &plugin};

    /// Should not crash - output goes to stderr
    lush_plugin_log(&ctx, 0, "Debug message %d", 42);
    lush_plugin_log(&ctx, 1, "Info message");
    lush_plugin_log(&ctx, 2, "Warning message");
    lush_plugin_log(&ctx, 3, "Error message");
    lush_plugin_log(&ctx, 99, "Unknown level message");
}

/* ============================================================================
 * PERMISSION FLAGS TESTS
 * ============================================================================
 */

TEST(permission_flags_defined) {
    /// Verify permission flags are properly defined and unique
    ASSERT_TRUE(LUSH_PLUGIN_PERM_NONE == 0, "PERM_NONE should be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_READ_VARS != 0,
                "PERM_READ_VARS should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_WRITE_VARS != 0,
                "PERM_WRITE_VARS should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_EXEC != 0, "PERM_EXEC should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_REGISTER_BUILTIN != 0,
                "PERM_REGISTER_BUILTIN should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_REGISTER_HOOK != 0,
                "PERM_REGISTER_HOOK should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_COMPLETIONS != 0,
                "PERM_COMPLETIONS should not be 0");
    ASSERT_TRUE(LUSH_PLUGIN_PERM_EVENTS != 0, "PERM_EVENTS should not be 0");

    /// Verify PERM_ALL includes all permissions
    lush_plugin_permission_t all =
        LUSH_PLUGIN_PERM_READ_VARS | LUSH_PLUGIN_PERM_WRITE_VARS |
        LUSH_PLUGIN_PERM_EXEC | LUSH_PLUGIN_PERM_REGISTER_BUILTIN |
        LUSH_PLUGIN_PERM_REGISTER_HOOK | LUSH_PLUGIN_PERM_COMPLETIONS |
        LUSH_PLUGIN_PERM_EVENTS;
    ASSERT_TRUE((LUSH_PLUGIN_PERM_ALL & all) == all,
                "PERM_ALL should include all permissions");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("\n=== Result String Tests ===\n");
    RUN_TEST(result_string_ok);
    RUN_TEST(result_string_error);
    RUN_TEST(result_string_not_found);
    RUN_TEST(result_string_load_failed);
    RUN_TEST(result_string_symbol_not_found);
    RUN_TEST(result_string_version_mismatch);
    RUN_TEST(result_string_init_failed);
    RUN_TEST(result_string_permission_denied);
    RUN_TEST(result_string_already_loaded);
    RUN_TEST(result_string_invalid_plugin);
    RUN_TEST(result_string_out_of_memory);
    RUN_TEST(result_string_unknown);

    printf("\n=== State String Tests ===\n");
    RUN_TEST(state_string_unloaded);
    RUN_TEST(state_string_loading);
    RUN_TEST(state_string_loaded);
    RUN_TEST(state_string_initializing);
    RUN_TEST(state_string_active);
    RUN_TEST(state_string_suspended);
    RUN_TEST(state_string_error);
    RUN_TEST(state_string_unloading);
    RUN_TEST(state_string_unknown);

    printf("\n=== Manager Creation Tests ===\n");
    RUN_TEST(manager_create_null_ptr);
    RUN_TEST(manager_create_default_config);
    RUN_TEST(manager_create_custom_config);
    RUN_TEST(manager_destroy_null);

    printf("\n=== Manager Set Functions Tests ===\n");
    RUN_TEST(manager_set_executor);
    RUN_TEST(manager_set_executor_null_manager);
    RUN_TEST(manager_set_symtable);
    RUN_TEST(manager_set_symtable_null_manager);

    printf("\n=== Load Tests ===\n");
    RUN_TEST(manager_load_null_manager);
    RUN_TEST(manager_load_null_path);
    RUN_TEST(manager_load_nonexistent);
    RUN_TEST(manager_load_inactive_manager);
    RUN_TEST(manager_load_max_plugins_reached);

    printf("\n=== Load by Name Tests ===\n");
    RUN_TEST(manager_load_by_name_null_manager);
    RUN_TEST(manager_load_by_name_null_name);
    RUN_TEST(manager_load_by_name_not_found);

    printf("\n=== Unload Tests ===\n");
    RUN_TEST(manager_unload_null_manager);
    RUN_TEST(manager_unload_null_name);
    RUN_TEST(manager_unload_not_found);

    printf("\n=== Find Tests ===\n");
    RUN_TEST(manager_find_null_manager);
    RUN_TEST(manager_find_null_name);
    RUN_TEST(manager_find_not_loaded);

    printf("\n=== List Tests ===\n");
    RUN_TEST(manager_list_null_manager);
    RUN_TEST(manager_list_null_count);
    RUN_TEST(manager_list_empty);

    printf("\n=== Reload Tests ===\n");
    RUN_TEST(manager_reload_null_manager);
    RUN_TEST(manager_reload_null_name);
    RUN_TEST(manager_reload_not_found);

    printf("\n=== Permission Tests ===\n");
    RUN_TEST(has_permission_null_context);
    RUN_TEST(has_permission_granted);
    RUN_TEST(has_permission_not_granted);
    RUN_TEST(has_permission_multiple);
    RUN_TEST(has_permission_all);

    printf("\n=== Builtin Registration Tests ===\n");
    RUN_TEST(register_builtin_null_context);
    RUN_TEST(register_builtin_null_name);
    RUN_TEST(register_builtin_null_fn);
    RUN_TEST(register_builtin_no_permission);
    RUN_TEST(register_builtin_no_plugin);
    RUN_TEST(unregister_builtin_null_context);
    RUN_TEST(unregister_builtin_null_name);

    printf("\n=== Hook Registration Tests ===\n");
    RUN_TEST(register_hook_null_context);
    RUN_TEST(register_hook_null_name);
    RUN_TEST(register_hook_null_fn);
    RUN_TEST(register_hook_no_permission);
    RUN_TEST(register_hook_valid_precmd);
    RUN_TEST(register_hook_valid_preexec);
    RUN_TEST(register_hook_valid_chpwd);
    RUN_TEST(register_hook_invalid_name);

    printf("\n=== Completion Registration Tests ===\n");
    RUN_TEST(register_completion_null_context);
    RUN_TEST(register_completion_null_name);
    RUN_TEST(register_completion_null_fn);
    RUN_TEST(register_completion_no_permission);
    RUN_TEST(register_completion_with_permission);

    printf("\n=== Event Subscription Tests ===\n");
    RUN_TEST(subscribe_event_null_context);
    RUN_TEST(subscribe_event_null_fn);
    RUN_TEST(subscribe_event_no_permission);
    RUN_TEST(subscribe_event_with_permission);

    printf("\n=== Variable Access Tests ===\n");
    RUN_TEST(get_var_null_context);
    RUN_TEST(get_var_null_name);
    RUN_TEST(get_var_no_permission);
    RUN_TEST(set_var_null_context);
    RUN_TEST(set_var_null_name);
    RUN_TEST(set_var_no_permission);
    RUN_TEST(set_var_with_permission);

    printf("\n=== Log Tests ===\n");
    RUN_TEST(log_null_context);
    RUN_TEST(log_null_format);
    RUN_TEST(log_with_valid_context);

    printf("\n=== Permission Flags Tests ===\n");
    RUN_TEST(permission_flags_defined);

    return TEST_RESULT();
}
