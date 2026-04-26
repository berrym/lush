/**
 * @file test_config_registry.c
 * @brief Unit tests for the config registry
 *
 * Tests the config_registry.c implementation covering:
 * - Registry lifecycle (init, cleanup)
 * - Section registration
 * - Value get/set operations
 * - Typed value access
 * - Change notifications
 * - Persistence (load/save)
 * - Utility functions (reset, defaults)
 */

#include "config_registry.h"

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* This file's pre-existing helpers used 2-arg variants of ASSERT_EQ
 * and ASSERT_STR_EQ (no message). Bridge them to the framework's
 * 3-arg versions by synthesizing a message. The bare 1-arg ASSERT
 * already matches the framework's signature exactly. */
#undef ASSERT_EQ
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")

/* These tests assume a fresh registry per test. The pre-existing local
 * RUN_TEST wrapped every call with cleanup+init; preserve that here by
 * overriding RUN_TEST and deferring to the framework's internal state
 * for accounting and failure isolation. */
#undef RUN_TEST
#define RUN_TEST(name)                                                         \
    do {                                                                       \
        test_framework_run++;                                                  \
        test_framework_current_name = #name;                                   \
        printf("  %s ... ", #name);                                            \
        fflush(stdout);                                                        \
        config_registry_cleanup();                                             \
        config_registry_init();                                                \
        if (setjmp(test_framework_jmpbuf) == 0) {                              \
            test_##name();                                                     \
            test_framework_passed++;                                           \
            printf("PASS\n");                                                  \
        } else {                                                               \
            test_framework_failed++;                                           \
        }                                                                      \
        config_registry_cleanup();                                             \
    } while (0)

/* ============================================================================
 * Test Framework
 * ============================================================================
 */

/* ============================================================================
 * Test Section Definitions
 * ============================================================================
 */

static const creg_option_t shell_options[] = {
    {"mode",
     CREG_VALUE_STRING,
     {.type = CREG_VALUE_STRING, .data.string = "lush"},
     "Shell mode",
     true},
    {"errexit",
     CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Exit on error",
     true},
    {"nounset",
     CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Error on unset",
     true},
    {"xtrace",
     CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Trace execution",
     true},
};

static const creg_section_t shell_section = {
    .name = "shell",
    .options = shell_options,
    .option_count = sizeof(shell_options) / sizeof(shell_options[0]),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

static const creg_option_t history_options[] = {
    {"enabled",
     CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable history",
     true},
    {"size",
     CREG_VALUE_INTEGER,
     {.type = CREG_VALUE_INTEGER, .data.integer = 10000},
     "History size",
     true},
    {"file",
     CREG_VALUE_STRING,
     {.type = CREG_VALUE_STRING, .data.string = "~/.lush_history"},
     "History file",
     true},
};

static const creg_section_t history_section = {
    .name = "history",
    .options = history_options,
    .option_count = sizeof(history_options) / sizeof(history_options[0]),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ============================================================================
 * Lifecycle Tests
 * ============================================================================
 */

TEST(init_cleanup) {
    /* Registry should already be initialized by RUN_TEST */
    ASSERT(config_registry_is_initialized());

    config_registry_cleanup();
    ASSERT(!config_registry_is_initialized());

    /* Re-init for cleanup in RUN_TEST */
    config_registry_init();
}

TEST(double_init) {
    /* Double init should be safe */
    creg_result_t result = config_registry_init();
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT(config_registry_is_initialized());
}

/* ============================================================================
 * Section Registration Tests
 * ============================================================================
 */

TEST(register_section) {
    creg_result_t result = config_registry_register_section(&shell_section);
    ASSERT_EQ(result, CREG_SUCCESS);

    const creg_section_t *sec = config_registry_get_section("shell");
    ASSERT(sec != NULL);
    ASSERT_STR_EQ(sec->name, "shell");
}

TEST(register_multiple_sections) {
    creg_result_t result = config_registry_register_section(&shell_section);
    ASSERT_EQ(result, CREG_SUCCESS);

    result = config_registry_register_section(&history_section);
    ASSERT_EQ(result, CREG_SUCCESS);

    ASSERT(config_registry_get_section("shell") != NULL);
    ASSERT(config_registry_get_section("history") != NULL);
}

TEST(register_duplicate_section) {
    creg_result_t result = config_registry_register_section(&shell_section);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Duplicate registration should be a no-op */
    result = config_registry_register_section(&shell_section);
    ASSERT_EQ(result, CREG_SUCCESS);
}

TEST(register_null_section) {
    creg_result_t result = config_registry_register_section(NULL);
    ASSERT_EQ(result, CREG_ERROR_INVALID_PARAM);
}

TEST(get_nonexistent_section) {
    const creg_section_t *sec = config_registry_get_section("nonexistent");
    ASSERT(sec == NULL);
}

/* ============================================================================
 * Value Access Tests
 * ============================================================================
 */

TEST(get_default_value) {
    config_registry_register_section(&shell_section);

    creg_value_t value;
    creg_result_t result = config_registry_get("shell.mode", &value);
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT_EQ(value.type, CREG_VALUE_STRING);
    ASSERT_STR_EQ(value.data.string, "lush");
}

TEST(set_and_get_string) {
    config_registry_register_section(&shell_section);

    creg_result_t result = config_registry_set_string("shell.mode", "posix");
    ASSERT_EQ(result, CREG_SUCCESS);

    char buf[64];
    result = config_registry_get_string("shell.mode", buf, sizeof(buf));
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "posix");
}

TEST(set_and_get_boolean) {
    config_registry_register_section(&shell_section);

    creg_result_t result = config_registry_set_boolean("shell.errexit", true);
    ASSERT_EQ(result, CREG_SUCCESS);

    bool val;
    result = config_registry_get_boolean("shell.errexit", &val);
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT(val == true);
}

TEST(set_and_get_integer) {
    config_registry_register_section(&history_section);

    creg_result_t result = config_registry_set_integer("history.size", 50000);
    ASSERT_EQ(result, CREG_SUCCESS);

    int64_t val;
    result = config_registry_get_integer("history.size", &val);
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT_EQ(val, 50000);
}

TEST(get_nonexistent_key) {
    config_registry_register_section(&shell_section);

    creg_value_t value;
    creg_result_t result = config_registry_get("shell.nonexistent", &value);
    ASSERT_EQ(result, CREG_ERROR_NOT_FOUND);
}

TEST(set_nonexistent_key) {
    config_registry_register_section(&shell_section);

    creg_result_t result =
        config_registry_set_boolean("shell.nonexistent", true);
    ASSERT_EQ(result, CREG_ERROR_NOT_FOUND);
}

TEST(type_mismatch) {
    config_registry_register_section(&shell_section);

    /* shell.mode is a string, try to set as boolean */
    creg_result_t result = config_registry_set_boolean("shell.mode", true);
    ASSERT_EQ(result, CREG_ERROR_TYPE_MISMATCH);
}

TEST(exists_check) {
    config_registry_register_section(&shell_section);

    ASSERT(config_registry_exists("shell.mode"));
    ASSERT(config_registry_exists("shell.errexit"));
    ASSERT(!config_registry_exists("shell.nonexistent"));
    ASSERT(!config_registry_exists("other.key"));
}

/* ============================================================================
 * Change Notification Tests
 * ============================================================================
 */

static int notification_count = 0;
static char last_notified_key[128] = {0};
static creg_value_t last_old_value = {0};
static creg_value_t last_new_value = {0};

static void test_change_callback(const char *key, const creg_value_t *old_value,
                                 const creg_value_t *new_value,
                                 void *user_data) {
    notification_count++;
    snprintf(last_notified_key, sizeof(last_notified_key), "%s", key);
    if (old_value)
        last_old_value = *old_value;
    if (new_value)
        last_new_value = *new_value;
    (void)user_data;
}

TEST(subscribe_exact_key) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    creg_result_t result =
        config_registry_subscribe("shell.errexit", test_change_callback, NULL);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Change the value */
    result = config_registry_set_boolean("shell.errexit", true);
    ASSERT_EQ(result, CREG_SUCCESS);

    ASSERT_EQ(notification_count, 1);
    ASSERT_STR_EQ(last_notified_key, "shell.errexit");
    ASSERT(last_new_value.data.boolean == true);
}

TEST(subscribe_section_wildcard) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    creg_result_t result =
        config_registry_subscribe("shell.*", test_change_callback, NULL);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Change multiple values in shell section */
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_boolean("shell.nounset", true);

    ASSERT_EQ(notification_count, 2);
}

TEST(subscribe_global_wildcard) {
    config_registry_register_section(&shell_section);
    config_registry_register_section(&history_section);

    notification_count = 0;
    creg_result_t result =
        config_registry_subscribe("*", test_change_callback, NULL);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Change values in different sections */
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_integer("history.size", 5000);

    ASSERT_EQ(notification_count, 2);
}

TEST(no_notification_on_same_value) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    config_registry_subscribe("shell.errexit", test_change_callback, NULL);

    /* Set to same value (default is false) */
    config_registry_set_boolean("shell.errexit", false);

    ASSERT_EQ(notification_count, 0);
}

TEST(unsubscribe) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    config_registry_subscribe("shell.errexit", test_change_callback, NULL);

    /* Unsubscribe */
    creg_result_t result = config_registry_unsubscribe(test_change_callback);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Change should not notify */
    config_registry_set_boolean("shell.errexit", true);
    ASSERT_EQ(notification_count, 0);
}

/* ============================================================================
 * Reset and Default Tests
 * ============================================================================
 */

TEST(reset_key) {
    config_registry_register_section(&shell_section);

    /* Change value */
    config_registry_set_boolean("shell.errexit", true);

    bool val;
    config_registry_get_boolean("shell.errexit", &val);
    ASSERT(val == true);

    /* Reset to default */
    creg_result_t result = config_registry_reset("shell.errexit");
    ASSERT_EQ(result, CREG_SUCCESS);

    config_registry_get_boolean("shell.errexit", &val);
    ASSERT(val == false);
}

TEST(reset_section) {
    config_registry_register_section(&shell_section);

    /* Change multiple values */
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_boolean("shell.nounset", true);
    config_registry_set_boolean("shell.xtrace", true);

    /* Reset section */
    creg_result_t result = config_registry_reset_section("shell");
    ASSERT_EQ(result, CREG_SUCCESS);

    /* All should be back to defaults */
    bool val;
    config_registry_get_boolean("shell.errexit", &val);
    ASSERT(val == false);
    config_registry_get_boolean("shell.nounset", &val);
    ASSERT(val == false);
    config_registry_get_boolean("shell.xtrace", &val);
    ASSERT(val == false);
}

TEST(is_default) {
    config_registry_register_section(&shell_section);

    /* Initially should be default */
    ASSERT(config_registry_is_default("shell.errexit"));

    /* Change it */
    config_registry_set_boolean("shell.errexit", true);
    ASSERT(!config_registry_is_default("shell.errexit"));

    /* Reset */
    config_registry_reset("shell.errexit");
    ASSERT(config_registry_is_default("shell.errexit"));
}

TEST(get_default_value_explicit) {
    config_registry_register_section(&history_section);

    /* Change the current value */
    config_registry_set_integer("history.size", 99999);

    /* Get default should return original */
    creg_value_t def;
    creg_result_t result = config_registry_get_default("history.size", &def);
    ASSERT_EQ(result, CREG_SUCCESS);
    ASSERT_EQ(def.data.integer, 10000);
}

/* ============================================================================
 * Persistence Tests
 * ============================================================================
 */

TEST(save_and_load) {
    config_registry_register_section(&shell_section);
    config_registry_register_section(&history_section);

    /* Set non-default values */
    config_registry_set_string("shell.mode", "bash");
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_integer("history.size", 50000);

    /* Save to temp file */
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_save(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Reset to defaults */
    config_registry_reset_all();

    /* Verify reset */
    char buf[64];
    config_registry_get_string("shell.mode", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "lush");

    /* Load from file */
    result = config_registry_load(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Verify loaded values */
    config_registry_get_string("shell.mode", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "bash");

    bool errexit;
    config_registry_get_boolean("shell.errexit", &errexit);
    ASSERT(errexit == true);

    int64_t size;
    config_registry_get_integer("history.size", &size);
    ASSERT_EQ(size, 50000);

    /* Cleanup */
    unlink(tmpfile);
}

TEST(save_sparse_format) {
    config_registry_register_section(&shell_section);

    /* Only change one value */
    config_registry_set_boolean("shell.errexit", true);

    /* Save to temp file */
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_save(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /* Read file and verify sparse format (only non-default values) */
    FILE *f = fopen(tmpfile, "r");
    ASSERT(f != NULL);

    char content[1024];
    size_t len = fread(content, 1, sizeof(content) - 1, f);
    content[len] = '\0';
    fclose(f);

    /* Should contain errexit = true */
    ASSERT(strstr(content, "errexit = true") != NULL);

    /* Should NOT contain mode, nounset, xtrace (they're at defaults) */
    ASSERT(strstr(content, "mode =") == NULL);
    ASSERT(strstr(content, "nounset =") == NULL);
    ASSERT(strstr(content, "xtrace =") == NULL);

    unlink(tmpfile);
}

TEST(load_nonexistent_file) {
    creg_result_t result =
        config_registry_load("/nonexistent/path/config.toml");
    ASSERT_EQ(result, CREG_ERROR_IO_FAILED);
}

TEST(load_empty_file) {
    config_registry_register_section(&shell_section);

    /* Create empty temp file */
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_load(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    unlink(tmpfile);
}

/* ============================================================================
 * Value Helper Tests
 * ============================================================================
 */

TEST(value_equal_strings) {
    creg_value_t a = creg_value_string("hello");
    creg_value_t b = creg_value_string("hello");
    creg_value_t c = creg_value_string("world");

    ASSERT(creg_value_equal(&a, &b));
    ASSERT(!creg_value_equal(&a, &c));
}

TEST(value_equal_integers) {
    creg_value_t a = creg_value_integer(42);
    creg_value_t b = creg_value_integer(42);
    creg_value_t c = creg_value_integer(43);

    ASSERT(creg_value_equal(&a, &b));
    ASSERT(!creg_value_equal(&a, &c));
}

TEST(value_equal_booleans) {
    creg_value_t a = creg_value_boolean(true);
    creg_value_t b = creg_value_boolean(true);
    creg_value_t c = creg_value_boolean(false);

    ASSERT(creg_value_equal(&a, &b));
    ASSERT(!creg_value_equal(&a, &c));
}

TEST(value_equal_different_types) {
    creg_value_t a = creg_value_string("42");
    creg_value_t b = creg_value_integer(42);

    ASSERT(!creg_value_equal(&a, &b));
}

TEST(value_equal_null) {
    creg_value_t a = creg_value_integer(42);

    ASSERT(!creg_value_equal(&a, NULL));
    ASSERT(!creg_value_equal(NULL, &a));
    ASSERT(creg_value_equal(NULL, NULL));
}

/* ============================================================================
 * Lifecycle Hook Tests
 * ============================================================================
 */

static int on_load_called = 0;
static int sync_to_runtime_called = 0;
static int sync_from_runtime_called = 0;

static void test_on_load(void) { on_load_called++; }
static void test_sync_to_runtime(void) { sync_to_runtime_called++; }
static void test_sync_from_runtime(void) { sync_from_runtime_called++; }

TEST(on_load_hook) {
    creg_option_t opts[] = {
        {"test",
         CREG_VALUE_BOOLEAN,
         {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
         NULL,
         true}};
    creg_section_t sec = {
        .name = "test",
        .options = opts,
        .option_count = 1,
        .on_load = test_on_load,
    };

    config_registry_register_section(&sec);

    /* Create temp file with content */
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    write(fd, "[test]\ntest = true\n", 19);
    close(fd);

    on_load_called = 0;
    config_registry_load(tmpfile);
    ASSERT_EQ(on_load_called, 1);

    unlink(tmpfile);
}

TEST(sync_hooks) {
    creg_option_t opts[] = {
        {"test",
         CREG_VALUE_BOOLEAN,
         {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
         NULL,
         true}};
    creg_section_t sec = {
        .name = "test",
        .options = opts,
        .option_count = 1,
        .sync_to_runtime = test_sync_to_runtime,
        .sync_from_runtime = test_sync_from_runtime,
    };

    config_registry_register_section(&sec);

    sync_to_runtime_called = 0;
    sync_from_runtime_called = 0;

    config_registry_sync_to_runtime();
    ASSERT_EQ(sync_to_runtime_called, 1);

    config_registry_sync_from_runtime();
    ASSERT_EQ(sync_from_runtime_called, 1);
}

/* ============================================================================
 * Main
 * ============================================================================
 */

int main(void) {
    printf("=== Config Registry Unit Tests ===\n\n");

    printf("Lifecycle Tests:\n");
    RUN_TEST(init_cleanup);
    RUN_TEST(double_init);

    printf("\nSection Registration Tests:\n");
    RUN_TEST(register_section);
    RUN_TEST(register_multiple_sections);
    RUN_TEST(register_duplicate_section);
    RUN_TEST(register_null_section);
    RUN_TEST(get_nonexistent_section);

    printf("\nValue Access Tests:\n");
    RUN_TEST(get_default_value);
    RUN_TEST(set_and_get_string);
    RUN_TEST(set_and_get_boolean);
    RUN_TEST(set_and_get_integer);
    RUN_TEST(get_nonexistent_key);
    RUN_TEST(set_nonexistent_key);
    RUN_TEST(type_mismatch);
    RUN_TEST(exists_check);

    printf("\nChange Notification Tests:\n");
    RUN_TEST(subscribe_exact_key);
    RUN_TEST(subscribe_section_wildcard);
    RUN_TEST(subscribe_global_wildcard);
    RUN_TEST(no_notification_on_same_value);
    RUN_TEST(unsubscribe);

    printf("\nReset and Default Tests:\n");
    RUN_TEST(reset_key);
    RUN_TEST(reset_section);
    RUN_TEST(is_default);
    RUN_TEST(get_default_value_explicit);

    printf("\nPersistence Tests:\n");
    RUN_TEST(save_and_load);
    RUN_TEST(save_sparse_format);
    RUN_TEST(load_nonexistent_file);
    RUN_TEST(load_empty_file);

    printf("\nValue Helper Tests:\n");
    RUN_TEST(value_equal_strings);
    RUN_TEST(value_equal_integers);
    RUN_TEST(value_equal_booleans);
    RUN_TEST(value_equal_different_types);
    RUN_TEST(value_equal_null);

    printf("\nLifecycle Hook Tests:\n");
    RUN_TEST(on_load_hook);
    RUN_TEST(sync_hooks);

    return TEST_RESULT();
}
