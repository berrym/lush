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
    {.name = "mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "lush"},
     .help = "Shell mode",
     .persisted = true,
     .description = "Pick the syntax dialect: bash, zsh, or lush"},
    {.name = "errexit",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Exit on error",
     .persisted = true},
    {.name = "nounset",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Error on unset",
     .persisted = true},
    {.name = "xtrace",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Trace execution",
     .persisted = true},
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
    {.name = "enabled",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable history",
     .persisted = true},
    {   .name = "size",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 10000},
     .help = "History size",
     .persisted = true},
    {   .name = "file",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING,
     .data.string = "~/.lush_history"},
     .help = "History file",
     .persisted = true},
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
    /// Registry should already be initialized by RUN_TEST
    ASSERT(config_registry_is_initialized());

    config_registry_cleanup();
    ASSERT(!config_registry_is_initialized());

    /// Re-init for cleanup in RUN_TEST
    config_registry_init();
}

TEST(double_init) {
    /// Double init should be safe
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

    char before[64];
    ASSERT_EQ(config_registry_get_string("shell.mode", before, sizeof(before)),
              CREG_SUCCESS);

    /// Duplicate registration is a no-op: it succeeds and leaves the existing
    /// section's value untouched.
    result = config_registry_register_section(&shell_section);
    ASSERT_EQ(result, CREG_SUCCESS);

    char after[64];
    ASSERT_EQ(config_registry_get_string("shell.mode", after, sizeof(after)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(before, after);
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

    /// shell.mode is a string, try to set as boolean
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

    /// Change the value
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

    /// Change multiple values in shell section
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

    /// Change values in different sections
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_integer("history.size", 5000);

    ASSERT_EQ(notification_count, 2);
}

TEST(no_notification_on_same_value) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    config_registry_subscribe("shell.errexit", test_change_callback, NULL);

    /// Set to same value (default is false)
    config_registry_set_boolean("shell.errexit", false);

    ASSERT_EQ(notification_count, 0);
}

TEST(unsubscribe) {
    config_registry_register_section(&shell_section);

    notification_count = 0;
    config_registry_subscribe("shell.errexit", test_change_callback, NULL);

    /// Unsubscribe
    creg_result_t result = config_registry_unsubscribe(test_change_callback);
    ASSERT_EQ(result, CREG_SUCCESS);

    /// Change should not notify
    config_registry_set_boolean("shell.errexit", true);
    ASSERT_EQ(notification_count, 0);
}

/* ============================================================================
 * Reset and Default Tests
 * ============================================================================
 */

TEST(reset_key) {
    config_registry_register_section(&shell_section);

    /// Change value
    config_registry_set_boolean("shell.errexit", true);

    bool val;
    config_registry_get_boolean("shell.errexit", &val);
    ASSERT(val == true);

    /// Reset to default
    creg_result_t result = config_registry_reset("shell.errexit");
    ASSERT_EQ(result, CREG_SUCCESS);

    config_registry_get_boolean("shell.errexit", &val);
    ASSERT(val == false);
}

TEST(reset_section) {
    config_registry_register_section(&shell_section);

    /// Change multiple values
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_boolean("shell.nounset", true);
    config_registry_set_boolean("shell.xtrace", true);

    /// Reset section
    creg_result_t result = config_registry_reset_section("shell");
    ASSERT_EQ(result, CREG_SUCCESS);

    /// All should be back to defaults
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

    /// Initially should be default
    ASSERT(config_registry_is_default("shell.errexit"));

    /// Change it
    config_registry_set_boolean("shell.errexit", true);
    ASSERT(!config_registry_is_default("shell.errexit"));

    /// Reset
    config_registry_reset("shell.errexit");
    ASSERT(config_registry_is_default("shell.errexit"));
}

TEST(get_default_value_explicit) {
    config_registry_register_section(&history_section);

    /// Change the current value
    config_registry_set_integer("history.size", 99999);

    /// Get default should return original
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

    /// Set non-default values
    config_registry_set_string("shell.mode", "bash");
    config_registry_set_boolean("shell.errexit", true);
    config_registry_set_integer("history.size", 50000);

    /// Save to temp file
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_save(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /// Reset to defaults
    config_registry_reset_all();

    /// Verify reset
    char buf[64];
    config_registry_get_string("shell.mode", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "lush");

    /// Load from file
    result = config_registry_load(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /// Verify loaded values
    config_registry_get_string("shell.mode", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "bash");

    bool errexit;
    config_registry_get_boolean("shell.errexit", &errexit);
    ASSERT(errexit == true);

    int64_t size;
    config_registry_get_integer("history.size", &size);
    ASSERT_EQ(size, 50000);

    /// Cleanup
    unlink(tmpfile);
}

TEST(save_sparse_format) {
    config_registry_register_section(&shell_section);

    /// Only change one value
    config_registry_set_boolean("shell.errexit", true);

    /// Save to temp file
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_save(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /// Read file and verify sparse format (only non-default values)
    FILE *f = fopen(tmpfile, "r");
    ASSERT(f != NULL);

    char content[1024];
    size_t len = fread(content, 1, sizeof(content) - 1, f);
    content[len] = '\0';
    fclose(f);

    /// Should contain errexit = true
    ASSERT(strstr(content, "errexit = true") != NULL);

    /// Should NOT contain mode, nounset, xtrace (they're at defaults)
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

    char before[64];
    ASSERT_EQ(config_registry_get_string("shell.mode", before, sizeof(before)),
              CREG_SUCCESS);

    /// Create empty temp file
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    creg_result_t result = config_registry_load(tmpfile);
    ASSERT_EQ(result, CREG_SUCCESS);

    /// Loading an empty file changes nothing.
    char after[64];
    ASSERT_EQ(config_registry_get_string("shell.mode", after, sizeof(after)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(before, after);

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
        {.name = "test",
         .type = CREG_VALUE_BOOLEAN,
         .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
         .help = NULL,
         .persisted = true}
    };
    creg_section_t sec = {
        .name = "test",
        .options = opts,
        .option_count = 1,
        .on_load = test_on_load,
    };

    config_registry_register_section(&sec);

    /// Create temp file with content
    char tmpfile[] = "/tmp/lush_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    ASSERT_EQ((int)write(fd, "[test]\ntest = true\n", 19), 19);
    close(fd);

    on_load_called = 0;
    config_registry_load(tmpfile);
    ASSERT_EQ(on_load_called, 1);

    unlink(tmpfile);
}

TEST(sync_hooks) {
    creg_option_t opts[] = {
        {.name = "test",
         .type = CREG_VALUE_BOOLEAN,
         .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
         .help = NULL,
         .persisted = true}
    };
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
 * Per-Mode Default Tests
 * ============================================================================
 */

TEST(mode_default_unregistered_option_fails) {
    creg_value_t v = creg_value_boolean(true);
    creg_result_t r = config_registry_set_mode_default("shell.does_not_exist",
                                                       SHELL_MODE_LUSH, &v);
    ASSERT(r != CREG_SUCCESS);
}

TEST(mode_default_type_mismatch_fails) {
    config_registry_register_section(&shell_section);
    /// errexit is BOOLEAN; passing INTEGER should fail
    creg_value_t v = creg_value_integer(42);
    creg_result_t r =
        config_registry_set_mode_default("shell.errexit", SHELL_MODE_LUSH, &v);
    ASSERT(r == CREG_ERROR_TYPE_MISMATCH);
}

TEST(mode_default_applies_for_registered_mode) {
    config_registry_register_section(&shell_section);

    /// Register a per-mode default: errexit=true under POSIX, false elsewhere
    /// (default).
    creg_value_t posix_default = creg_value_boolean(true);
    ASSERT_EQ(config_registry_set_mode_default(
                  "shell.errexit", SHELL_MODE_POSIX, &posix_default),
              CREG_SUCCESS);

    /// Apply POSIX defaults: errexit should now be true.
    ASSERT_EQ(config_registry_apply_mode_defaults(SHELL_MODE_POSIX),
              CREG_SUCCESS);
    bool got = false;
    ASSERT_EQ(config_registry_get_boolean("shell.errexit", &got), CREG_SUCCESS);
    ASSERT(got == true);

    /// Apply LUSH defaults: a mode switch re-seeds the MODE layer wholesale, so
    /// errexit -- which POSIX set but LUSH does not override -- drops its POSIX
    /// MODE value and falls back to the schema default (false). This clean
    /// re-seed is exactly what lets a SESSION tweak survive a mode switch while
    /// a stale mode preset does not linger.
    ASSERT_EQ(config_registry_apply_mode_defaults(SHELL_MODE_LUSH),
              CREG_SUCCESS);
    got = true;
    ASSERT_EQ(config_registry_get_boolean("shell.errexit", &got), CREG_SUCCESS);
    ASSERT(got == false); /// MODE layer cleared, falls back to default
}

TEST(mode_default_replaces_prior_value_for_same_mode) {
    config_registry_register_section(&shell_section);

    creg_value_t v1 = creg_value_boolean(true);
    creg_value_t v2 = creg_value_boolean(false);

    ASSERT_EQ(
        config_registry_set_mode_default("shell.errexit", SHELL_MODE_BASH, &v1),
        CREG_SUCCESS);
    ASSERT_EQ(
        config_registry_set_mode_default("shell.errexit", SHELL_MODE_BASH, &v2),
        CREG_SUCCESS);

    ASSERT_EQ(config_registry_apply_mode_defaults(SHELL_MODE_BASH),
              CREG_SUCCESS);
    bool got = true;
    ASSERT_EQ(config_registry_get_boolean("shell.errexit", &got), CREG_SUCCESS);
    ASSERT(got == false); /// second registration won
}

TEST(mode_default_independent_per_mode) {
    config_registry_register_section(&shell_section);

    creg_value_t bash_v = creg_value_boolean(true);
    creg_value_t posix_v = creg_value_boolean(false);

    ASSERT_EQ(config_registry_set_mode_default("shell.errexit", SHELL_MODE_BASH,
                                               &bash_v),
              CREG_SUCCESS);
    ASSERT_EQ(config_registry_set_mode_default("shell.errexit",
                                               SHELL_MODE_POSIX, &posix_v),
              CREG_SUCCESS);

    /// Apply BASH defaults: errexit -> true
    config_registry_apply_mode_defaults(SHELL_MODE_BASH);
    bool got;
    config_registry_get_boolean("shell.errexit", &got);
    ASSERT(got == true);

    /// Apply POSIX defaults: errexit -> false
    config_registry_apply_mode_defaults(SHELL_MODE_POSIX);
    config_registry_get_boolean("shell.errexit", &got);
    ASSERT(got == false);

    /// Back to BASH: errexit -> true again (re-seed-every-time).
    config_registry_apply_mode_defaults(SHELL_MODE_BASH);
    config_registry_get_boolean("shell.errexit", &got);
    ASSERT(got == true);
}

TEST(mode_default_invalid_mode_fails) {
    config_registry_register_section(&shell_section);
    creg_value_t v = creg_value_boolean(true);
    creg_result_t r =
        config_registry_set_mode_default("shell.errexit", SHELL_MODE_COUNT, &v);
    ASSERT(r == CREG_ERROR_INVALID_PARAM);
}

TEST(mode_default_apply_invalid_mode_fails) {
    config_registry_register_section(&shell_section);
    ASSERT_EQ(config_registry_apply_mode_defaults(SHELL_MODE_COUNT),
              CREG_ERROR_INVALID_PARAM);
}

/* ============================================================================
 * Type descriptor (vtable) validation
 * ============================================================================
 */

static const creg_enum_pair_t mode_pairs[] = {
    {"posix", 0},
    { "bash", 1},
    {  "zsh", 2},
    { "lush", 3},
    {   NULL, 0}
};

TEST(type_enum_rejects_invalid_accepts_valid) {
    config_registry_register_section(&shell_section);
    creg_type_t mode_type;
    creg_type_init_enum(&mode_type, mode_pairs);
    ASSERT_EQ(config_registry_set_type("shell.mode", &mode_type), CREG_SUCCESS);

    /// A member of the enum is accepted.
    creg_value_t good = creg_value_string("bash");
    ASSERT_EQ(config_registry_set("shell.mode", &good), CREG_SUCCESS);
    creg_value_t got;
    config_registry_get("shell.mode", &got);
    ASSERT_STR_EQ(got.data.string, "bash");

    /// A non-member is rejected with INVALID_VALUE, and the prior value stands.
    creg_value_t bad = creg_value_string("klingon");
    ASSERT_EQ(config_registry_set("shell.mode", &bad),
              CREG_ERROR_INVALID_VALUE);
    config_registry_get("shell.mode", &got);
    ASSERT_STR_EQ(got.data.string, "bash");
}

TEST(type_int_range_rejects_out_of_bounds) {
    config_registry_register_section(&history_section);
    creg_type_t size_type;
    creg_type_init_int_range(&size_type, 0, 100);
    ASSERT_EQ(config_registry_set_type("history.size", &size_type),
              CREG_SUCCESS);

    /// In-range and both inclusive boundaries are accepted.
    creg_value_t mid = creg_value_integer(50);
    ASSERT_EQ(config_registry_set("history.size", &mid), CREG_SUCCESS);
    creg_value_t lo = creg_value_integer(0);
    ASSERT_EQ(config_registry_set("history.size", &lo), CREG_SUCCESS);
    creg_value_t hi = creg_value_integer(100);
    ASSERT_EQ(config_registry_set("history.size", &hi), CREG_SUCCESS);

    /// Above and below the range are rejected; the last valid value stands.
    creg_value_t over = creg_value_integer(101);
    ASSERT_EQ(config_registry_set("history.size", &over),
              CREG_ERROR_INVALID_VALUE);
    creg_value_t under = creg_value_integer(-1);
    ASSERT_EQ(config_registry_set("history.size", &under),
              CREG_ERROR_INVALID_VALUE);
    creg_value_t got;
    config_registry_get("history.size", &got);
    ASSERT_EQ(got.data.integer, 100);
}

TEST(type_describe_reports_valid_values) {
    config_registry_register_section(&shell_section);
    config_registry_register_section(&history_section);
    creg_type_t mode_type;
    creg_type_init_enum(&mode_type, mode_pairs);
    config_registry_set_type("shell.mode", &mode_type);
    creg_type_t size_type;
    creg_type_init_int_range(&size_type, 0, 100);
    config_registry_set_type("history.size", &size_type);

    char buf[128];
    ASSERT_EQ(config_registry_describe_type("shell.mode", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "one of: posix bash zsh lush");
    ASSERT_EQ(config_registry_describe_type("history.size", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "an integer in 0..100");

    /// A REGISTERED but untyped key reports NOT_FOUND for describe: assert
    /// history.enabled exists first, so the NOT_FOUND is about the missing
    /// descriptor, not a missing key.
    creg_value_t v;
    ASSERT_EQ(config_registry_get("history.enabled", &v), CREG_SUCCESS);
    ASSERT_EQ(
        config_registry_describe_type("history.enabled", buf, sizeof(buf)),
        CREG_ERROR_NOT_FOUND);
}

TEST(type_int_range_open_ended_describe) {
    config_registry_register_section(&history_section);
    char buf[64];

    /// An INT64_MAX upper bound is the open-ended / non-negative case: describe
    /// renders it in plain language, not the huge numeric limit. The check
    /// still rejects a negative and accepts a large value.
    creg_type_t nn;
    creg_type_init_int_range(&nn, 0, INT64_MAX);
    config_registry_set_type("history.size", &nn);
    ASSERT_EQ(config_registry_describe_type("history.size", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "a non-negative integer");
    creg_value_t neg = creg_value_integer(-1);
    ASSERT_EQ(config_registry_set("history.size", &neg),
              CREG_ERROR_INVALID_VALUE);
    creg_value_t big = creg_value_integer(1000000);
    ASSERT_EQ(config_registry_set("history.size", &big), CREG_SUCCESS);

    /// A positive lower bound with an open top renders ">= min".
    creg_type_t lo;
    creg_type_init_int_range(&lo, 5, INT64_MAX);
    config_registry_set_type("history.size", &lo);
    ASSERT_EQ(config_registry_describe_type("history.size", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "an integer >= 5");

    /// An open bottom with a finite top renders "<= max".
    creg_type_t hi;
    creg_type_init_int_range(&hi, INT64_MIN, 100);
    config_registry_set_type("history.size", &hi);
    ASSERT_EQ(config_registry_describe_type("history.size", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "an integer <= 100");

    /// Both ends open renders the bare "an integer".
    creg_type_t both;
    creg_type_init_int_range(&both, INT64_MIN, INT64_MAX);
    config_registry_set_type("history.size", &both);
    ASSERT_EQ(config_registry_describe_type("history.size", buf, sizeof(buf)),
              CREG_SUCCESS);
    ASSERT_STR_EQ(buf, "an integer");
}

TEST(type_set_type_rejects_storage_mismatch) {
    config_registry_register_section(&history_section);
    /// history.size is an INTEGER key; attaching an enum (string storage) is a
    /// programming error and must be rejected at attach time, so a later string
    /// set cannot reach an int-reading check op with the wrong union member.
    creg_type_t enum_type;
    creg_type_init_enum(&enum_type, mode_pairs);
    ASSERT_EQ(config_registry_set_type("history.size", &enum_type),
              CREG_ERROR_TYPE_MISMATCH);

    /// The matching storage kind attaches cleanly.
    creg_type_t range_type;
    creg_type_init_int_range(&range_type, 0, 100);
    ASSERT_EQ(config_registry_set_type("history.size", &range_type),
              CREG_SUCCESS);
}

TEST(type_untyped_key_accepts_any_well_typed_value) {
    config_registry_register_section(&history_section);
    /// history.size has no descriptor here: any integer is accepted (the
    /// validation is opt-in per key, so unmigrated keys keep prior behavior).
    creg_value_t huge = creg_value_integer(999999);
    ASSERT_EQ(config_registry_set("history.size", &huge), CREG_SUCCESS);
}

/* ============================================================================
 * Schema invariant validator
 * ============================================================================
 */

static bool violation_names(const creg_schema_violation_t *v, size_t n,
                            const char *key) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(v[i].key, key) == 0) {
            return true;
        }
    }
    return false;
}

TEST(validate_schema_clean_when_consistent) {
    config_registry_register_section(&history_section);
    /// No type descriptors, no bindings, no mode defaults: nothing to violate.
    creg_schema_violation_t v[8];
    ASSERT_EQ(config_registry_validate_schema(v, 8), (size_t)0);
}

TEST(validate_schema_catches_out_of_spec_default) {
    config_registry_register_section(&history_section);
    /// history.size DEFAULT is 10000; constrain it to [0,100] so its own
    /// default is now out of spec -- the default seed bypasses the set check,
    /// so only the validator can catch this.
    creg_type_t t;
    creg_type_init_int_range(&t, 0, 100);
    config_registry_set_type("history.size", &t);
    creg_schema_violation_t v[8];
    size_t n = config_registry_validate_schema(v, 8);
    ASSERT_TRUE(n >= 1, "an out-of-spec default must be reported");
    ASSERT_TRUE(violation_names(v, n < 8 ? n : 8, "history.size"),
                "the violation names history.size");
}

TEST(validate_schema_catches_out_of_spec_mode_default) {
    config_registry_register_section(&history_section);
    /// Range [0,20000] so the DEFAULT (10000) is fine and only the bad mode
    /// default violates -- isolates the mode-default check.
    creg_type_t t;
    creg_type_init_int_range(&t, 0, 20000);
    config_registry_set_type("history.size", &t);
    creg_value_t bad = creg_value_integer(99999);
    config_registry_set_mode_default("history.size", SHELL_MODE_BASH, &bad);
    creg_schema_violation_t v[8];
    size_t n = config_registry_validate_schema(v, 8);
    ASSERT_TRUE(n >= 1, "an out-of-spec mode default must be reported");
    ASSERT_TRUE(violation_names(v, n < 8 ? n : 8, "history.size"),
                "the violation names history.size");
}

TEST(validate_schema_string_truncation_not_flagged) {
    config_registry_register_section(&history_section);
    /// A fixed-buffer string binding: apply_binding truncates the effective
    /// value to fit, so the cell legitimately holds the truncated copy. The
    /// validator must compare against the SAME truncation, not the full string,
    /// or it reports a phantom desync.
    char buf[8] = {0};
    config_registry_bind_string("history.file", buf, sizeof(buf));
    creg_value_t longv = creg_value_string("a_very_long_history_path");
    ASSERT_EQ(config_registry_set("history.file", &longv), CREG_SUCCESS);
    /// buf now holds the 7-char truncation; effective is the full string.
    creg_schema_violation_t v[8];
    ASSERT_EQ(config_registry_validate_schema(v, 8), (size_t)0);
}

TEST(validate_schema_catches_bound_cell_desync) {
    config_registry_register_section(&history_section);
    int cell = 10000; /// matches the DEFAULT effective value
    config_registry_bind_integer("history.size", &cell);
    creg_schema_violation_t v[8];
    ASSERT_EQ(config_registry_validate_schema(v, 8), (size_t)0);

    /// Change the cell out of band (as a stray writer would): now the bound
    /// cell no longer equals the effective value.
    cell = 42;
    size_t n = config_registry_validate_schema(v, 8);
    ASSERT_TRUE(n >= 1, "a desynced bound cell must be reported");
    ASSERT_TRUE(violation_names(v, n < 8 ? n : 8, "history.size"),
                "the violation names history.size");
}

/* ============================================================================
 * Load report (dropped keys)
 * ============================================================================
 */

TEST(load_reported_records_dropped_keys) {
    config_registry_register_section(&history_section);
    creg_type_t small;
    creg_type_init_int_range(&small, 0, 100);
    config_registry_set_type("history.size", &small);

    char tmpfile[] = "/tmp/lush_load_report_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_TRUE(fd >= 0, "temp file created");
    FILE *f = fdopen(fd, "w");
    /// enabled=false is valid; size=999 fails [0,100]; bogus is unknown.
    fputs("[history]\nenabled = false\nsize = 999\nbogus = 5\n", f);
    fclose(f);

    creg_load_report_t report;
    ASSERT_EQ(config_registry_load_reported(tmpfile, &report), CREG_SUCCESS);
    unlink(tmpfile);

    ASSERT_EQ(report.skip_count, (size_t)2);
    bool saw_invalid = false, saw_unknown = false;
    for (size_t i = 0; i < report.skip_count && i < CREG_LOAD_SKIP_MAX; i++) {
        if (strcmp(report.skipped[i].key, "history.size") == 0 &&
            report.skipped[i].reason == CREG_ERROR_INVALID_VALUE) {
            saw_invalid = true;
        }
        if (strcmp(report.skipped[i].key, "history.bogus") == 0 &&
            report.skipped[i].reason == CREG_ERROR_NOT_FOUND) {
            saw_unknown = true;
        }
    }
    ASSERT_TRUE(saw_invalid,
                "the out-of-range value is recorded as INVALID_VALUE");
    ASSERT_TRUE(saw_unknown, "the unknown key is recorded as NOT_FOUND");

    /// The valid key still loaded despite the two bad ones.
    bool enabled = true;
    config_registry_get_boolean("history.enabled", &enabled);
    ASSERT_TRUE(enabled == false, "a valid key still loads past the bad keys");

    /// The plain config_registry_load (NULL report) skips silently with no
    /// crash -- the report is opt-in.
    char tmp2[] = "/tmp/lush_load_silent_XXXXXX";
    int fd2 = mkstemp(tmp2);
    ASSERT_TRUE(fd2 >= 0, "second temp file created");
    FILE *f2 = fdopen(fd2, "w");
    fputs("[history]\nbogus = 7\n", f2);
    fclose(f2);
    ASSERT_EQ(config_registry_load(tmp2), CREG_SUCCESS);
    unlink(tmp2);
}

/* ============================================================================
 * Discoverability tiers
 * ============================================================================
 */

TEST(tier_set_get_roundtrip) {
    config_registry_register_section(&history_section);

    /// A freshly registered key has no tier.
    creg_tier_t tier = CREG_TIER_BEGINNER;
    ASSERT_EQ(config_registry_get_tier("history.size", &tier), CREG_SUCCESS);
    ASSERT_EQ(tier, CREG_TIER_UNSET);

    /// Attach and read it back.
    ASSERT_EQ(config_registry_set_tier("history.size", CREG_TIER_BEGINNER),
              CREG_SUCCESS);
    ASSERT_EQ(config_registry_get_tier("history.size", &tier), CREG_SUCCESS);
    ASSERT_EQ(tier, CREG_TIER_BEGINNER);

    /// An unregistered key reports NOT_FOUND for both directions.
    ASSERT_EQ(config_registry_set_tier("history.bogus", CREG_TIER_BEGINNER),
              CREG_ERROR_NOT_FOUND);
    ASSERT_EQ(config_registry_get_tier("history.bogus", &tier),
              CREG_ERROR_NOT_FOUND);
}

TEST(collect_by_tier_returns_only_tagged_keys) {
    config_registry_register_section(&history_section);
    config_registry_set_tier("history.enabled", CREG_TIER_BEGINNER);
    config_registry_set_tier("history.size", CREG_TIER_BEGINNER);
    /// history.file is left UNSET.

    char keys[8][CREG_KEY_MAX];
    size_t n = config_registry_collect_by_tier(CREG_TIER_BEGINNER, keys, 8);
    ASSERT_EQ(n, (size_t)2);

    bool saw_enabled = false, saw_size = false, saw_file = false;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(keys[i], "history.enabled") == 0)
            saw_enabled = true;
        if (strcmp(keys[i], "history.size") == 0)
            saw_size = true;
        if (strcmp(keys[i], "history.file") == 0)
            saw_file = true;
    }
    ASSERT_TRUE(saw_enabled && saw_size, "both tagged keys are collected");
    ASSERT_TRUE(!saw_file, "an untagged key is not collected");

    /// The count is the true total even when the output buffer is smaller; only
    /// `max` rows are written.
    char one[1][CREG_KEY_MAX];
    ASSERT_EQ(config_registry_collect_by_tier(CREG_TIER_BEGINNER, one, 1),
              (size_t)2);
    ASSERT_TRUE(strcmp(one[0], "history.enabled") == 0 ||
                    strcmp(one[0], "history.size") == 0,
                "the one written row is a tagged key");
}

/* ============================================================================
 * Plain-language description
 * ============================================================================
 */

TEST(get_description_returns_registered_text_else_null) {
    config_registry_register_section(&shell_section);

    /// A key that declared a description returns it verbatim (no copy).
    const char *d = config_registry_get_description("shell.mode");
    ASSERT(d != NULL);
    ASSERT_STR_EQ(d, "Pick the syntax dialect: bash, zsh, or lush");

    /// A key with no description returns NULL so callers fall back to help --
    /// which this same key does have.
    ASSERT(config_registry_get_description("shell.errexit") == NULL);
    ASSERT(config_registry_get_help("shell.errexit") != NULL);

    /// An unregistered key returns NULL, never a dangling pointer.
    ASSERT(config_registry_get_description("shell.bogus") == NULL);
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

    printf("\nPer-Mode Default Tests:\n");
    RUN_TEST(mode_default_unregistered_option_fails);
    RUN_TEST(mode_default_type_mismatch_fails);
    RUN_TEST(mode_default_applies_for_registered_mode);
    RUN_TEST(mode_default_replaces_prior_value_for_same_mode);
    RUN_TEST(mode_default_independent_per_mode);
    RUN_TEST(mode_default_invalid_mode_fails);
    RUN_TEST(mode_default_apply_invalid_mode_fails);

    RUN_TEST(type_enum_rejects_invalid_accepts_valid);
    RUN_TEST(type_int_range_rejects_out_of_bounds);
    RUN_TEST(type_describe_reports_valid_values);
    RUN_TEST(type_int_range_open_ended_describe);
    RUN_TEST(type_set_type_rejects_storage_mismatch);
    RUN_TEST(type_untyped_key_accepts_any_well_typed_value);

    RUN_TEST(validate_schema_clean_when_consistent);
    RUN_TEST(validate_schema_catches_out_of_spec_default);
    RUN_TEST(validate_schema_catches_out_of_spec_mode_default);
    RUN_TEST(validate_schema_string_truncation_not_flagged);
    RUN_TEST(validate_schema_catches_bound_cell_desync);
    RUN_TEST(load_reported_records_dropped_keys);
    RUN_TEST(tier_set_get_roundtrip);
    RUN_TEST(collect_by_tier_returns_only_tagged_keys);
    RUN_TEST(get_description_returns_registered_text_else_null);

    return TEST_RESULT();
}
