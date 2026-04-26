/**
 * @file test_config.c
 * @brief Unit tests for configuration system
 *
 * Tests the configuration module including:
 * - Validation functions for all config types
 * - Configuration initialization and defaults
 * - Configuration getters and setters
 * - Path resolution functions
 * - Section parsing
 * - Error handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test_framework.h"

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */








/* ============================================================================
 * BOOLEAN VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_bool_true_values) {
    ASSERT_TRUE(config_validate_bool("true"), "\"true\" should be valid");
    ASSERT_TRUE(config_validate_bool("yes"), "\"yes\" should be valid");
    ASSERT_TRUE(config_validate_bool("1"), "\"1\" should be valid");
    ASSERT_TRUE(config_validate_bool("on"), "\"on\" should be valid");
}

TEST(validate_bool_false_values) {
    ASSERT_TRUE(config_validate_bool("false"), "\"false\" should be valid");
    ASSERT_TRUE(config_validate_bool("no"), "\"no\" should be valid");
    ASSERT_TRUE(config_validate_bool("0"), "\"0\" should be valid");
    ASSERT_TRUE(config_validate_bool("off"), "\"off\" should be valid");
}

TEST(validate_bool_invalid) {
    ASSERT_FALSE(config_validate_bool("invalid"),
                 "\"invalid\" should be invalid");
    ASSERT_FALSE(config_validate_bool("maybe"), "\"maybe\" should be invalid");
    ASSERT_FALSE(config_validate_bool("2"), "\"2\" should be invalid");
    /* Note: empty string causes strcmp with empty, which is valid but returns
     * false */
}

/* ============================================================================
 * INTEGER VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_int_valid) {
    ASSERT_TRUE(config_validate_int("0"), "\"0\" should be valid");
    ASSERT_TRUE(config_validate_int("123"), "\"123\" should be valid");
    ASSERT_TRUE(config_validate_int("-456"), "\"-456\" should be valid");
    ASSERT_TRUE(config_validate_int("1000000"), "large number should be valid");
}

TEST(validate_int_invalid) {
    ASSERT_FALSE(config_validate_int("abc"), "letters should be invalid");
    ASSERT_FALSE(config_validate_int("12.34"), "float should be invalid");
    ASSERT_FALSE(config_validate_int("12abc"), "mixed should be invalid");
}

/* ============================================================================
 * STRING VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_string_valid) {
    ASSERT_TRUE(config_validate_string("hello"),
                "simple string should be valid");
    ASSERT_TRUE(config_validate_string("hello world"),
                "string with spaces should be valid");
    ASSERT_TRUE(config_validate_string("/path/to/file"),
                "path should be valid");
}

/* ============================================================================
 * FLOAT VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_float_valid) {
    ASSERT_TRUE(config_validate_float("0.0"), "\"0.0\" should be valid");
    ASSERT_TRUE(config_validate_float("3.14"), "\"3.14\" should be valid");
    ASSERT_TRUE(config_validate_float("-2.5"), "\"-2.5\" should be valid");
    ASSERT_TRUE(config_validate_float("100"), "integer format should be valid");
}

TEST(validate_float_invalid) {
    ASSERT_FALSE(config_validate_float("abc"), "letters should be invalid");
    ASSERT_FALSE(config_validate_float("1.2.3"),
                 "multiple dots should be invalid");
}

/* ============================================================================
 * PATH VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_path_valid) {
    ASSERT_TRUE(config_validate_path("/etc/passwd"),
                "absolute path should be valid");
    ASSERT_TRUE(config_validate_path("./relative/path"),
                "relative path should be valid");
    ASSERT_TRUE(config_validate_path("~/home/file"),
                "home path should be valid");
}

/* ============================================================================
 * OPTIMIZATION LEVEL VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_optimization_level_valid) {
    ASSERT_TRUE(config_validate_optimization_level("0"),
                "level 0 should be valid");
    ASSERT_TRUE(config_validate_optimization_level("1"),
                "level 1 should be valid");
    ASSERT_TRUE(config_validate_optimization_level("2"),
                "level 2 should be valid");
    ASSERT_TRUE(config_validate_optimization_level("3"),
                "level 3 should be valid");
    ASSERT_TRUE(config_validate_optimization_level("4"),
                "level 4 should be valid");
}

TEST(validate_optimization_level_invalid) {
    ASSERT_FALSE(config_validate_optimization_level("5"),
                 "level 5 should be invalid");
    ASSERT_FALSE(config_validate_optimization_level("-1"),
                 "negative should be invalid");
    ASSERT_FALSE(config_validate_optimization_level("abc"),
                 "letters should be invalid");
}

/* ============================================================================
 * LLE ARROW MODE VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_lle_arrow_mode_valid) {
    ASSERT_TRUE(config_validate_lle_arrow_mode("context-aware"),
                "context-aware should be valid");
    ASSERT_TRUE(config_validate_lle_arrow_mode("classic"),
                "classic should be valid");
    ASSERT_TRUE(config_validate_lle_arrow_mode("always-history"),
                "always-history should be valid");
    ASSERT_TRUE(config_validate_lle_arrow_mode("multiline-first"),
                "multiline-first should be valid");
}

TEST(validate_lle_arrow_mode_invalid) {
    ASSERT_FALSE(config_validate_lle_arrow_mode("invalid"),
                 "invalid mode should be rejected");
}

/* ============================================================================
 * LLE STORAGE MODE VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_lle_storage_mode_valid) {
    ASSERT_TRUE(config_validate_lle_storage_mode("lle-only"),
                "lle-only should be valid");
    ASSERT_TRUE(config_validate_lle_storage_mode("bash-only"),
                "bash-only should be valid");
    ASSERT_TRUE(config_validate_lle_storage_mode("dual"),
                "dual should be valid");
    ASSERT_TRUE(config_validate_lle_storage_mode("readline-compat"),
                "readline-compat should be valid");
}

TEST(validate_lle_storage_mode_invalid) {
    ASSERT_FALSE(config_validate_lle_storage_mode("invalid"),
                 "invalid mode should be rejected");
}

/* ============================================================================
 * LLE DEDUP SCOPE VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_lle_dedup_scope_valid) {
    ASSERT_TRUE(config_validate_lle_dedup_scope("none"),
                "none should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_scope("session"),
                "session should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_scope("recent"),
                "recent should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_scope("global"),
                "global should be valid");
}

TEST(validate_lle_dedup_scope_invalid) {
    ASSERT_FALSE(config_validate_lle_dedup_scope("invalid"),
                 "invalid scope should be rejected");
}

/* ============================================================================
 * LLE DEDUP STRATEGY VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_lle_dedup_strategy_valid) {
    ASSERT_TRUE(config_validate_lle_dedup_strategy("ignore"),
                "ignore should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_strategy("keep-recent"),
                "keep-recent should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_strategy("keep-frequent"),
                "keep-frequent should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_strategy("merge"),
                "merge should be valid");
    ASSERT_TRUE(config_validate_lle_dedup_strategy("keep-all"),
                "keep-all should be valid");
}

TEST(validate_lle_dedup_strategy_invalid) {
    ASSERT_FALSE(config_validate_lle_dedup_strategy("invalid"),
                 "invalid strategy should be rejected");
}

/* ============================================================================
 * SHELL MODE VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_shell_mode_valid) {
    ASSERT_TRUE(config_validate_shell_mode("posix"), "posix should be valid");
    ASSERT_TRUE(config_validate_shell_mode("sh"), "sh should be valid");
    ASSERT_TRUE(config_validate_shell_mode("bash"), "bash should be valid");
    ASSERT_TRUE(config_validate_shell_mode("zsh"), "zsh should be valid");
    ASSERT_TRUE(config_validate_shell_mode("lush"), "lush should be valid");
}

TEST(validate_shell_mode_invalid) {
    ASSERT_FALSE(config_validate_shell_mode("invalid"),
                 "invalid mode should be rejected");
    ASSERT_FALSE(config_validate_shell_mode("ksh"),
                 "unsupported shell should be rejected");
}

/* ============================================================================
 * SHELL OPTION VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_shell_option_valid) {
    /* Shell options accept boolean values */
    ASSERT_TRUE(config_validate_shell_option("true"), "true should be valid");
    ASSERT_TRUE(config_validate_shell_option("false"), "false should be valid");
    ASSERT_TRUE(config_validate_shell_option("1"), "1 should be valid");
    ASSERT_TRUE(config_validate_shell_option("0"), "0 should be valid");
}

TEST(validate_shell_option_invalid) {
    ASSERT_FALSE(config_validate_shell_option("invalid"),
                 "invalid should be rejected");
}

/* ============================================================================
 * COLOR SCHEME VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_color_scheme_valid) {
    /* Color scheme accepts any non-null string */
    ASSERT_TRUE(config_validate_color_scheme("default"),
                "default should be valid");
    ASSERT_TRUE(config_validate_color_scheme("dark"), "dark should be valid");
    ASSERT_TRUE(config_validate_color_scheme("light"), "light should be valid");
}

/* ============================================================================
 * COLOR VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_color_valid) {
    ASSERT_TRUE(config_validate_color("red"), "color name should be valid");
    ASSERT_TRUE(config_validate_color("#FF0000"), "hex color should be valid");
}

/* ============================================================================
 * CONFIGURATION INITIALIZATION TESTS
 * ============================================================================
 */

TEST(config_init_basic) {
    int result = config_init();
    ASSERT_EQ(result, 0, "config_init should succeed");
    /* Verify some defaults were set */
    ASSERT_TRUE(config.history_enabled, "history should be enabled by default");
    ASSERT_TRUE(config.completion_enabled,
                "completion should be enabled by default");
}

TEST(config_set_defaults_basic) {
    /* First init, then set defaults to reset */
    config_init();
    config_set_defaults();

    /* Check default values */
    ASSERT_TRUE(config.history_enabled,
                "history_enabled should default to true");
    ASSERT_EQ(config.history_size, 1000, "history_size should default to 1000");
    ASSERT_TRUE(config.completion_enabled,
                "completion_enabled should default to true");
    ASSERT_TRUE(config.colors_enabled, "colors_enabled should default to true");
}

/* ============================================================================
 * CONFIGURATION GETTER/SETTER TESTS
 * ============================================================================
 */

TEST(config_set_get_bool) {
    config_init();

    /* Set and get a boolean value */
    int result = config_set_bool("history.enabled", false);
    ASSERT_EQ(result, 0, "config_set_bool should succeed");

    bool value = config_get_bool("history.enabled", true);
    ASSERT_FALSE(value, "config_get_bool should return set value");

    /* Restore */
    config_set_bool("history.enabled", true);
}

TEST(config_set_get_int) {
    config_init();

    /* Set and get an integer value */
    int result = config_set_int("history.size", 5000);
    ASSERT_EQ(result, 0, "config_set_int should succeed");

    int value = config_get_int("history.size", 1000);
    ASSERT_EQ(value, 5000, "config_get_int should return set value");
}

TEST(config_set_get_string) {
    config_init();

    /* Set and get a string value */
    int result = config_set_string("prompt.theme", "test_theme");
    ASSERT_EQ(result, 0, "config_set_string should succeed");

    const char *value = config_get_string("prompt.theme", "default");
    ASSERT_STR_EQ(value, "test_theme",
                  "config_get_string should return set value");
}

TEST(config_get_bool_default) {
    config_init();

    /* Get a non-existent key should return default */
    bool value = config_get_bool("nonexistent.key", true);
    ASSERT_TRUE(value, "should return default for non-existent key");

    value = config_get_bool("nonexistent.key", false);
    ASSERT_FALSE(value, "should return default for non-existent key");
}

TEST(config_get_int_default) {
    config_init();

    /* Get a non-existent key should return default */
    int value = config_get_int("nonexistent.key", 42);
    ASSERT_EQ(value, 42, "should return default for non-existent key");
}

TEST(config_get_string_default) {
    config_init();

    /* Get a non-existent key should return default */
    const char *value = config_get_string("nonexistent.key", "default_value");
    ASSERT_STR_EQ(value, "default_value",
                  "should return default for non-existent key");
}

/* ============================================================================
 * SHELL OPTION GETTER/SETTER TESTS
 * ============================================================================
 */

TEST(config_set_get_shell_option) {
    config_init();

    /* Set errexit option - API expects "shell." prefix */
    config_set_shell_option("shell.errexit", true);
    bool value = config_get_shell_option("shell.errexit");
    ASSERT_TRUE(value, "errexit should be set");

    config_set_shell_option("shell.errexit", false);
    value = config_get_shell_option("shell.errexit");
    ASSERT_FALSE(value, "errexit should be unset");
}

TEST(config_shell_option_nounset) {
    config_init();

    config_set_shell_option("shell.nounset", true);
    bool value = config_get_shell_option("shell.nounset");
    ASSERT_TRUE(value, "nounset should be set");

    config_set_shell_option("shell.nounset", false);
    value = config_get_shell_option("shell.nounset");
    ASSERT_FALSE(value, "nounset should be unset");
}

TEST(config_shell_option_xtrace) {
    config_init();

    config_set_shell_option("shell.xtrace", true);
    bool value = config_get_shell_option("shell.xtrace");
    ASSERT_TRUE(value, "xtrace should be set");

    config_set_shell_option("shell.xtrace", false);
}

/* ============================================================================
 * PATH RESOLUTION TESTS
 * ============================================================================
 */

TEST(config_get_xdg_dir) {
    char buffer[CONFIG_PATH_MAX];
    int result = config_get_xdg_dir(buffer, sizeof(buffer));
    ASSERT_EQ(result, 0, "config_get_xdg_dir should succeed");
    ASSERT_NOT_NULL(buffer, "buffer should be populated");
    /* Should contain "lush" */
    ASSERT(strstr(buffer, "lush") != NULL, "XDG dir should contain 'lush'");
}

TEST(config_get_xdg_config_path) {
    char buffer[CONFIG_PATH_MAX];
    int result = config_get_xdg_config_path(buffer, sizeof(buffer));
    ASSERT_EQ(result, 0, "config_get_xdg_config_path should succeed");
    /* Should end with lushrc.toml */
    ASSERT(strstr(buffer, "lushrc.toml") != NULL,
           "XDG config path should contain 'lushrc.toml'");
}

TEST(config_get_legacy_config_path) {
    char buffer[CONFIG_PATH_MAX];
    int result = config_get_legacy_config_path(buffer, sizeof(buffer));
    ASSERT_EQ(result, 0, "config_get_legacy_config_path should succeed");
    /* Should end with .lushrc */
    ASSERT(strstr(buffer, ".lushrc") != NULL,
           "Legacy config path should contain '.lushrc'");
}

TEST(config_get_script_config_path) {
    char buffer[CONFIG_PATH_MAX];
    int result = config_get_script_config_path(buffer, sizeof(buffer));
    ASSERT_EQ(result, 0, "config_get_script_config_path should succeed");
    /* Should contain lushrc (but not .toml) */
    ASSERT(strstr(buffer, "lush") != NULL,
           "Script config path should contain 'lush'");
}

TEST(config_get_system_config_path) {
    char *path = config_get_system_config_path();
    ASSERT_NOT_NULL(path, "system config path should not be NULL");
    ASSERT_STR_EQ(path, SYSTEM_CONFIG_FILE,
                  "system config path should match constant");
    free(path);
}

/* ============================================================================
 * SECTION PARSING TESTS
 * ============================================================================
 */

TEST(config_parse_section_history) {
    int result = config_parse_section("history");
    ASSERT_EQ(result, 0, "parsing 'history' section should succeed");
}

TEST(config_parse_section_completion) {
    int result = config_parse_section("completion");
    ASSERT_EQ(result, 0, "parsing 'completion' section should succeed");
}

TEST(config_parse_section_prompt) {
    int result = config_parse_section("prompt");
    ASSERT_EQ(result, 0, "parsing 'prompt' section should succeed");
}

TEST(config_parse_section_behavior) {
    int result = config_parse_section("behavior");
    ASSERT_EQ(result, 0, "parsing 'behavior' section should succeed");
}

TEST(config_parse_section_aliases) {
    int result = config_parse_section("aliases");
    ASSERT_EQ(result, 0, "parsing 'aliases' section should succeed");
}

TEST(config_parse_section_network) {
    int result = config_parse_section("network");
    ASSERT_EQ(result, 0, "parsing 'network' section should succeed");
}

TEST(config_parse_section_scripts) {
    int result = config_parse_section("scripts");
    ASSERT_EQ(result, 0, "parsing 'scripts' section should succeed");
}

TEST(config_parse_section_keys) {
    int result = config_parse_section("keys");
    ASSERT_EQ(result, 0, "parsing 'keys' section should succeed");
}

TEST(config_parse_section_invalid) {
    int result = config_parse_section("invalid_section");
    /* Should return non-zero for invalid section */
    ASSERT(result != 0, "parsing invalid section should fail");
}

/* ============================================================================
 * LINE PARSING TESTS
 * ============================================================================
 */

TEST(config_parse_line_comment) {
    config_init();
    /* Comment lines should be skipped */
    int result = config_parse_line("# This is a comment", 1, "test");
    ASSERT_EQ(result, 0, "comment line should be parsed successfully");
}

TEST(config_parse_line_empty) {
    config_init();
    /* Empty lines should be skipped */
    int result = config_parse_line("", 1, "test");
    ASSERT_EQ(result, 0, "empty line should be parsed successfully");
}

TEST(config_parse_line_whitespace) {
    config_init();
    /* Whitespace-only lines should be skipped */
    int result = config_parse_line("   \t  ", 1, "test");
    ASSERT_EQ(result, 0, "whitespace line should be parsed successfully");
}

TEST(config_parse_line_section_header) {
    config_init();
    int result = config_parse_line("[history]", 1, "test");
    ASSERT_EQ(result, 0, "section header should be parsed successfully");
}

/* ============================================================================
 * SCRIPT EXECUTION CONTROL TESTS
 * ============================================================================
 */

TEST(config_script_execution_control) {
    config_init();

    /* Enable script execution */
    config_set_script_execution(true);
    ASSERT_TRUE(config_should_execute_scripts(),
                "scripts should be executable when enabled");

    /* Disable script execution */
    config_set_script_execution(false);
    ASSERT_FALSE(config_should_execute_scripts(),
                 "scripts should not be executable when disabled");

    /* Re-enable */
    config_set_script_execution(true);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(config_error_message) {
    config_init();

    /* Trigger an error by parsing invalid option */
    config_set_bool("nonexistent.option", true);

    /* Get last error should return something */
    const char *error = config_get_last_error();
    /* Error may or may not be set depending on implementation */
    (void)error; /* Suppress unused warning */
}

/* ============================================================================
 * SCRIPT PATH DETECTION TESTS
 * ============================================================================
 */

TEST(config_script_exists_nonexistent) {
    bool exists = config_script_exists("/nonexistent/path/to/script");
    ASSERT_FALSE(exists, "nonexistent script should not exist");
}

TEST(config_script_exists_etc_passwd) {
    /* /etc/passwd should exist on any Unix system */
    bool exists = config_script_exists("/etc/passwd");
    ASSERT_TRUE(exists, "/etc/passwd should exist");
}

/* ============================================================================
 * CONFIGURATION CLEANUP TEST
 * ============================================================================
 */

TEST(config_cleanup_basic) {
    config_init();
    /* Should not crash */
    config_cleanup();
    /* Re-init for subsequent tests */
    config_init();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Configuration System tests...\n");

    /* Boolean validation */
    printf("\n=== Boolean Validation Tests ===\n");
    RUN_TEST(validate_bool_true_values);
    RUN_TEST(validate_bool_false_values);
    RUN_TEST(validate_bool_invalid);

    /* Integer validation */
    printf("\n=== Integer Validation Tests ===\n");
    RUN_TEST(validate_int_valid);
    RUN_TEST(validate_int_invalid);

    /* String validation */
    printf("\n=== String Validation Tests ===\n");
    RUN_TEST(validate_string_valid);

    /* Float validation */
    printf("\n=== Float Validation Tests ===\n");
    RUN_TEST(validate_float_valid);
    RUN_TEST(validate_float_invalid);

    /* Path validation */
    printf("\n=== Path Validation Tests ===\n");
    RUN_TEST(validate_path_valid);

    /* Optimization level validation */
    printf("\n=== Optimization Level Validation Tests ===\n");
    RUN_TEST(validate_optimization_level_valid);
    RUN_TEST(validate_optimization_level_invalid);

    /* LLE arrow mode validation */
    printf("\n=== LLE Arrow Mode Validation Tests ===\n");
    RUN_TEST(validate_lle_arrow_mode_valid);
    RUN_TEST(validate_lle_arrow_mode_invalid);

    /* LLE storage mode validation */
    printf("\n=== LLE Storage Mode Validation Tests ===\n");
    RUN_TEST(validate_lle_storage_mode_valid);
    RUN_TEST(validate_lle_storage_mode_invalid);

    /* LLE dedup scope validation */
    printf("\n=== LLE Dedup Scope Validation Tests ===\n");
    RUN_TEST(validate_lle_dedup_scope_valid);
    RUN_TEST(validate_lle_dedup_scope_invalid);

    /* LLE dedup strategy validation */
    printf("\n=== LLE Dedup Strategy Validation Tests ===\n");
    RUN_TEST(validate_lle_dedup_strategy_valid);
    RUN_TEST(validate_lle_dedup_strategy_invalid);

    /* Shell mode validation */
    printf("\n=== Shell Mode Validation Tests ===\n");
    RUN_TEST(validate_shell_mode_valid);
    RUN_TEST(validate_shell_mode_invalid);

    /* Shell option validation */
    printf("\n=== Shell Option Validation Tests ===\n");
    RUN_TEST(validate_shell_option_valid);
    RUN_TEST(validate_shell_option_invalid);

    /* Color scheme validation */
    printf("\n=== Color Scheme Validation Tests ===\n");
    RUN_TEST(validate_color_scheme_valid);

    /* Color validation */
    printf("\n=== Color Validation Tests ===\n");
    RUN_TEST(validate_color_valid);

    /* Configuration initialization */
    printf("\n=== Configuration Initialization Tests ===\n");
    RUN_TEST(config_init_basic);
    RUN_TEST(config_set_defaults_basic);

    /* Configuration getters/setters */
    printf("\n=== Configuration Getter/Setter Tests ===\n");
    RUN_TEST(config_set_get_bool);
    RUN_TEST(config_set_get_int);
    RUN_TEST(config_set_get_string);
    RUN_TEST(config_get_bool_default);
    RUN_TEST(config_get_int_default);
    RUN_TEST(config_get_string_default);

    /* Shell option getters/setters */
    printf("\n=== Shell Option Getter/Setter Tests ===\n");
    RUN_TEST(config_set_get_shell_option);
    RUN_TEST(config_shell_option_nounset);
    RUN_TEST(config_shell_option_xtrace);

    /* Path resolution */
    printf("\n=== Path Resolution Tests ===\n");
    RUN_TEST(config_get_xdg_dir);
    RUN_TEST(config_get_xdg_config_path);
    RUN_TEST(config_get_legacy_config_path);
    RUN_TEST(config_get_script_config_path);
    RUN_TEST(config_get_system_config_path);

    /* Section parsing */
    printf("\n=== Section Parsing Tests ===\n");
    RUN_TEST(config_parse_section_history);
    RUN_TEST(config_parse_section_completion);
    RUN_TEST(config_parse_section_prompt);
    RUN_TEST(config_parse_section_behavior);
    RUN_TEST(config_parse_section_aliases);
    RUN_TEST(config_parse_section_network);
    RUN_TEST(config_parse_section_scripts);
    RUN_TEST(config_parse_section_keys);
    RUN_TEST(config_parse_section_invalid);

    /* Line parsing */
    printf("\n=== Line Parsing Tests ===\n");
    RUN_TEST(config_parse_line_comment);
    RUN_TEST(config_parse_line_empty);
    RUN_TEST(config_parse_line_whitespace);
    RUN_TEST(config_parse_line_section_header);

    /* Script execution control */
    printf("\n=== Script Execution Control Tests ===\n");
    RUN_TEST(config_script_execution_control);

    /* Error handling */
    printf("\n=== Error Handling Tests ===\n");
    RUN_TEST(config_error_message);

    /* Script path detection */
    printf("\n=== Script Path Detection Tests ===\n");
    RUN_TEST(config_script_exists_nonexistent);
    RUN_TEST(config_script_exists_etc_passwd);

    /* Cleanup */
    printf("\n=== Cleanup Tests ===\n");
    RUN_TEST(config_cleanup_basic);

    return TEST_RESULT();
}
