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
#include "lle/char_width.h"
#include "test_framework.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

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
    /// Note: empty string causes strcmp with empty, which is valid but returns
    /// false
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
 * COMPLETION MATCH MODE VALIDATION TESTS
 * ============================================================================
 */

TEST(validate_completion_match_mode_valid) {
    ASSERT_TRUE(config_validate_completion_match_mode("prefix"),
                "prefix should be valid");
    ASSERT_TRUE(config_validate_completion_match_mode("substring"),
                "substring should be valid");
    ASSERT_TRUE(config_validate_completion_match_mode("fuzzy"),
                "fuzzy should be valid");
}

TEST(validate_completion_match_mode_invalid) {
    ASSERT_FALSE(config_validate_completion_match_mode("invalid"),
                 "invalid mode should be rejected");
    ASSERT_FALSE(config_validate_completion_match_mode(""),
                 "empty string should be rejected");
    ASSERT_FALSE(config_validate_completion_match_mode("PREFIX"),
                 "case-mismatched mode should be rejected");
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
    /// Shell options accept boolean values
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
    /// Verify some defaults were set
    ASSERT_TRUE(config.history_enabled, "history should be enabled by default");
    ASSERT_TRUE(config.completion_enabled,
                "completion should be enabled by default");
}

TEST(config_set_defaults_basic) {
    /// First init, then set defaults to reset
    config_init();
    config_set_defaults();

    /// Check default values
    ASSERT_TRUE(config.history_enabled,
                "history_enabled should default to true");
    ASSERT_EQ(config.history_size, 1000, "history_size should default to 1000");
    ASSERT_TRUE(config.completion_enabled,
                "completion_enabled should default to true");
}

/* ============================================================================
 * CONFIGURATION GETTER/SETTER TESTS
 * ============================================================================
 */

TEST(config_set_get_bool) {
    config_init();

    /// Set and get a boolean value
    int result = config_set_bool("history.enabled", false);
    ASSERT_EQ(result, 0, "config_set_bool should succeed");

    bool value = config_get_bool("history.enabled", true);
    ASSERT_FALSE(value, "config_get_bool should return set value");

    /// Restore
    config_set_bool("history.enabled", true);
}

TEST(config_set_get_int) {
    config_init();

    /// Set and get an integer value
    int result = config_set_int("history.size", 5000);
    ASSERT_EQ(result, 0, "config_set_int should succeed");

    int value = config_get_int("history.size", 1000);
    ASSERT_EQ(value, 5000, "config_get_int should return set value");
}

TEST(config_set_get_string) {
    config_init();

    /// Set and get a string value
    int result = config_set_string("lle.history_file", "/tmp/test_history");
    ASSERT_EQ(result, 0, "config_set_string should succeed");

    const char *value = config_get_string("lle.history_file", "default");
    ASSERT_STR_EQ(value, "/tmp/test_history",
                  "config_get_string should return set value");
}

TEST(config_display_lle_theme_persists) {
    config_init();

    /// display.lle.theme is a registry-backed string-pointer key: setting it
    /// through the config API write-throughs the owned char* cell and the value
    /// round-trips, which is what lets a theme choice persist to TOML and be
    /// restored at startup. (The runtime application to the prompt composer is
    /// covered by the PTY theme-persistence test.)
    int result = config_set_string("display.lle.theme", "two-line");
    ASSERT_EQ(result, 0, "config_set_string should reach the registry key");

    ASSERT_STR_EQ(config_get_string("display.lle.theme", ""), "two-line",
                  "config_get_string should return the persisted theme");
    ASSERT_TRUE(config.display_lle_theme != NULL &&
                    strcmp(config.display_lle_theme, "two-line") == 0,
                "the binding should write through to the owned char* cell");
}

TEST(config_display_ambiguous_width_applies) {
    config_init();

    /// display.ambiguous_width is a registry-backed string key whose value
    /// config_apply_settings pushes to the LLE codepoint-width policy. Setting
    /// it widens (or narrows) East Asian Ambiguous-class characters; U+2026
    /// HORIZONTAL ELLIPSIS is Ambiguous, so its width tracks the policy.
    const uint32_t ambiguous = 0x2026;

    ASSERT_EQ(config_set_string("display.ambiguous_width", "wide"), 0,
              "config_set_string should reach the registry key");
    config_apply_settings();
    ASSERT_EQ(lle_codepoint_width(ambiguous), 2,
              "wide policy widens an ambiguous character to 2 columns");

    config_set_string("display.ambiguous_width", "narrow");
    config_apply_settings();
    ASSERT_EQ(lle_codepoint_width(ambiguous), 1,
              "narrow policy renders an ambiguous character as 1 column");
}

TEST(config_display_newline_before_prompt_bound) {
    config_init();

    /// display.newline_before_prompt is a registered, bound display key: a set
    /// through the config API resolves to the key and write-throughs to the
    /// global config struct field that the prompt composer reads. (Regression
    /// guard: before registration the key was struct-only and config get/set
    /// could not reach it.)
    int result = config_set_bool("display.newline_before_prompt", false);
    ASSERT_EQ(result, 0, "config_set_bool should succeed for a registered key");
    ASSERT_FALSE(config_get_bool("display.newline_before_prompt", true),
                 "config_get_bool should return the set value");
    ASSERT_FALSE(config.display_newline_before_prompt,
                 "the binding should write through to the config struct field");

    config_set_bool("display.newline_before_prompt", true);
    ASSERT_TRUE(config.display_newline_before_prompt,
                "re-enabling should write through too");
}

TEST(config_show_lists_migrated_lle_section) {
    config_init();

    /// The lle.* keys migrated off the legacy config_options[] table into the
    /// registry "lle" section. `config show` must still enumerate them through
    /// the registry, or the migration silently drops them from discovery.
    /// Capture the section output and assert the registry-backed keys appear.
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0, "pipe should succeed");
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    config_show_section(CONFIG_SECTION_LLE);
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    ASSERT_TRUE(n > 0, "config show should produce output for the lle section");
    buf[n > 0 ? n : 0] = '\0';

    ASSERT_TRUE(strstr(buf, "dedup_scope") != NULL,
                "config show lle should list the dedup_scope key");
    ASSERT_TRUE(strstr(buf, "arrow_key_mode") != NULL,
                "config show lle should list the arrow_key_mode key");
    ASSERT_TRUE(strstr(buf, "cache_size") != NULL,
                "config show lle should list the cache_size key");
    ASSERT_TRUE(strstr(buf, "history_file") != NULL,
                "config show lle should list the history_file key");
}

TEST(config_get_bool_default) {
    config_init();

    /// Get a non-existent key should return default
    bool value = config_get_bool("nonexistent.key", true);
    ASSERT_TRUE(value, "should return default for non-existent key");

    value = config_get_bool("nonexistent.key", false);
    ASSERT_FALSE(value, "should return default for non-existent key");
}

TEST(config_get_int_default) {
    config_init();

    /// Get a non-existent key should return default
    int value = config_get_int("nonexistent.key", 42);
    ASSERT_EQ(value, 42, "should return default for non-existent key");
}

TEST(config_get_string_default) {
    config_init();

    /// Get a non-existent key should return default
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

    /// Set errexit option - API expects "shell." prefix
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
    /// With XDG_CONFIG_HOME set, the directory is exactly
    /// "$XDG_CONFIG_HOME/lush" (CONFIG_XDG_DIR). Pin it to a known value so the
    /// full path is assertable; restore the prior environment before asserting
    /// to avoid leaking state to later tests.
    char *saved = getenv("XDG_CONFIG_HOME");
    char saved_copy[CONFIG_PATH_MAX];
    bool had = saved != NULL;
    if (had) {
        snprintf(saved_copy, sizeof(saved_copy), "%s", saved);
    }
    setenv("XDG_CONFIG_HOME", "/tmp/lush_xdg_probe", 1);

    char buffer[CONFIG_PATH_MAX];
    int result = config_get_xdg_dir(buffer, sizeof(buffer));

    if (had) {
        setenv("XDG_CONFIG_HOME", saved_copy, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }

    ASSERT_EQ(result, 0, "config_get_xdg_dir should succeed");
    ASSERT_STR_EQ(buffer, "/tmp/lush_xdg_probe/lush",
                  "XDG dir is $XDG_CONFIG_HOME/lush");
}

TEST(config_get_xdg_config_path) {
    /// Full TOML config path is "$XDG_CONFIG_HOME/lush/lushrc.toml".
    char *saved = getenv("XDG_CONFIG_HOME");
    char saved_copy[CONFIG_PATH_MAX];
    bool had = saved != NULL;
    if (had) {
        snprintf(saved_copy, sizeof(saved_copy), "%s", saved);
    }
    setenv("XDG_CONFIG_HOME", "/tmp/lush_xdg_probe", 1);

    char buffer[CONFIG_PATH_MAX];
    int result = config_get_xdg_config_path(buffer, sizeof(buffer));

    if (had) {
        setenv("XDG_CONFIG_HOME", saved_copy, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }

    ASSERT_EQ(result, 0, "config_get_xdg_config_path should succeed");
    ASSERT_STR_EQ(buffer, "/tmp/lush_xdg_probe/lush/lushrc.toml",
                  "XDG config path is <xdg-dir>/lushrc.toml");
}

TEST(config_get_legacy_config_path) {
    /// Legacy path is "$HOME/.lushrc.toml" (USER_CONFIG_FILE), independent of
    /// XDG_CONFIG_HOME.
    char buffer[CONFIG_PATH_MAX];
    int result = config_get_legacy_config_path(buffer, sizeof(buffer));
    ASSERT_EQ(result, 0, "config_get_legacy_config_path should succeed");

    const char *home = getenv("HOME");
    if (home) {
        char expected[CONFIG_PATH_MAX];
        snprintf(expected, sizeof(expected), "%s/.lushrc.toml", home);
        ASSERT_STR_EQ(buffer, expected, "legacy path is $HOME/.lushrc.toml");
    } else {
        ASSERT(strlen(buffer) > 0, "legacy path is non-empty");
    }
}

TEST(config_get_script_config_path) {
    /// Script (non-TOML) path is "$XDG_CONFIG_HOME/lush/lushrc"
    /// (CONFIG_XDG_SCRIPT); it must not carry the .toml suffix of the TOML
    /// variant.
    char *saved = getenv("XDG_CONFIG_HOME");
    char saved_copy[CONFIG_PATH_MAX];
    bool had = saved != NULL;
    if (had) {
        snprintf(saved_copy, sizeof(saved_copy), "%s", saved);
    }
    setenv("XDG_CONFIG_HOME", "/tmp/lush_xdg_probe", 1);

    char buffer[CONFIG_PATH_MAX];
    int result = config_get_script_config_path(buffer, sizeof(buffer));

    if (had) {
        setenv("XDG_CONFIG_HOME", saved_copy, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }

    ASSERT_EQ(result, 0, "config_get_script_config_path should succeed");
    ASSERT_STR_EQ(buffer, "/tmp/lush_xdg_probe/lush/lushrc",
                  "script path is <xdg-dir>/lushrc (no .toml)");
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
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_HISTORY,
              "the history section should be selected");
}

TEST(config_parse_section_completion) {
    int result = config_parse_section("completion");
    ASSERT_EQ(result, 0, "parsing 'completion' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_COMPLETION,
              "the completion section should be selected");
}

TEST(config_parse_section_behavior) {
    int result = config_parse_section("behavior");
    ASSERT_EQ(result, 0, "parsing 'behavior' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_BEHAVIOR,
              "the behavior section should be selected");
}

TEST(config_parse_section_aliases) {
    int result = config_parse_section("aliases");
    ASSERT_EQ(result, 0, "parsing 'aliases' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_ALIASES,
              "the aliases section should be selected");
}

TEST(config_parse_section_network) {
    int result = config_parse_section("network");
    ASSERT_EQ(result, 0, "parsing 'network' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_NETWORK,
              "the network section should be selected");
}

TEST(config_parse_section_scripts) {
    int result = config_parse_section("scripts");
    ASSERT_EQ(result, 0, "parsing 'scripts' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_SCRIPTS,
              "the scripts section should be selected");
}

TEST(config_parse_section_keys) {
    int result = config_parse_section("keys");
    ASSERT_EQ(result, 0, "parsing 'keys' section should succeed");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_KEYS,
              "the keys section should be selected");
}

TEST(config_parse_section_invalid) {
    int result = config_parse_section("invalid_section");
    /// Should return non-zero for invalid section
    ASSERT(result != 0, "parsing invalid section should fail");
}

/* ============================================================================
 * LINE PARSING TESTS
 * ============================================================================
 */

TEST(config_parse_line_comment) {
    config_init();
    /// Comment lines should be skipped
    int result = config_parse_line("# This is a comment", 1, "test");
    ASSERT_EQ(result, 0, "comment line should be parsed successfully");
}

TEST(config_parse_line_empty) {
    config_init();
    /// Empty lines should be skipped
    int result = config_parse_line("", 1, "test");
    ASSERT_EQ(result, 0, "empty line should be parsed successfully");
}

TEST(config_parse_line_whitespace) {
    config_init();
    /// Whitespace-only lines should be skipped
    int result = config_parse_line("   \t  ", 1, "test");
    ASSERT_EQ(result, 0, "whitespace line should be parsed successfully");
}

TEST(config_parse_line_section_header) {
    config_init();
    int result = config_parse_line("[history]", 1, "test");
    ASSERT_EQ(result, 0, "section header should be parsed successfully");
    ASSERT_EQ(config_get_current_section(), CONFIG_SECTION_HISTORY,
              "the [history] header should select the history section");
}

/* ============================================================================
 * SCRIPT EXECUTION CONTROL TESTS
 * ============================================================================
 */

TEST(config_script_execution_control) {
    config_init();

    /// Enable script execution
    config_set_script_execution(true);
    ASSERT_TRUE(config_should_execute_scripts(),
                "scripts should be executable when enabled");

    /// Disable script execution
    config_set_script_execution(false);
    ASSERT_FALSE(config_should_execute_scripts(),
                 "scripts should not be executable when disabled");

    /// Re-enable
    config_set_script_execution(true);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

/* config_error_message was removed in the structured-error migration
 * (#71). The legacy config_get_last_error() / last_error globals it
 * exercised are gone; config-parsing errors now surface via the
 * shell_error_display() pipeline directly to stderr, so there is no
 * "last error" value to query from a follow-up test. The structured
 * error pipeline itself is exercised by tests/unit/test_shell_error.c. */

/* ============================================================================
 * SCRIPT PATH DETECTION TESTS
 * ============================================================================
 */

TEST(config_script_exists_nonexistent) {
    bool exists = config_script_exists("/nonexistent/path/to/script");
    ASSERT_FALSE(exists, "nonexistent script should not exist");
}

TEST(config_script_exists_etc_passwd) {
    /// /etc/passwd should exist on any Unix system
    bool exists = config_script_exists("/etc/passwd");
    ASSERT_TRUE(exists, "/etc/passwd should exist");
}

/* ============================================================================
 * CONFIGURATION CLEANUP TEST
 * ============================================================================
 */

TEST(config_cleanup_basic) {
    config_init();
    /// Should not crash
    config_cleanup();
    /// Re-init for subsequent tests
    config_init();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Configuration System tests...\n");

    /// Boolean validation
    printf("\n=== Boolean Validation Tests ===\n");
    RUN_TEST(validate_bool_true_values);
    RUN_TEST(validate_bool_false_values);
    RUN_TEST(validate_bool_invalid);

    /// Integer validation
    printf("\n=== Integer Validation Tests ===\n");
    RUN_TEST(validate_int_valid);
    RUN_TEST(validate_int_invalid);

    /// String validation
    printf("\n=== String Validation Tests ===\n");
    RUN_TEST(validate_string_valid);

    /// Float validation
    printf("\n=== Float Validation Tests ===\n");
    RUN_TEST(validate_float_valid);
    RUN_TEST(validate_float_invalid);

    /// Path validation
    printf("\n=== Path Validation Tests ===\n");
    RUN_TEST(validate_path_valid);

    /// Optimization level validation
    printf("\n=== Optimization Level Validation Tests ===\n");
    RUN_TEST(validate_optimization_level_valid);
    RUN_TEST(validate_optimization_level_invalid);

    /// LLE arrow mode validation
    printf("\n=== LLE Arrow Mode Validation Tests ===\n");
    RUN_TEST(validate_lle_arrow_mode_valid);
    RUN_TEST(validate_lle_arrow_mode_invalid);
    RUN_TEST(validate_completion_match_mode_valid);
    RUN_TEST(validate_completion_match_mode_invalid);

    /// LLE dedup scope validation
    printf("\n=== LLE Dedup Scope Validation Tests ===\n");
    RUN_TEST(validate_lle_dedup_scope_valid);
    RUN_TEST(validate_lle_dedup_scope_invalid);

    /// LLE dedup strategy validation
    printf("\n=== LLE Dedup Strategy Validation Tests ===\n");
    RUN_TEST(validate_lle_dedup_strategy_valid);
    RUN_TEST(validate_lle_dedup_strategy_invalid);

    /// Shell mode validation
    printf("\n=== Shell Mode Validation Tests ===\n");
    RUN_TEST(validate_shell_mode_valid);
    RUN_TEST(validate_shell_mode_invalid);

    /// Shell option validation
    printf("\n=== Shell Option Validation Tests ===\n");
    RUN_TEST(validate_shell_option_valid);
    RUN_TEST(validate_shell_option_invalid);

    /// Color validation
    printf("\n=== Color Validation Tests ===\n");
    RUN_TEST(validate_color_valid);

    /// Configuration initialization
    printf("\n=== Configuration Initialization Tests ===\n");
    RUN_TEST(config_init_basic);
    RUN_TEST(config_set_defaults_basic);

    /// Configuration getters/setters
    printf("\n=== Configuration Getter/Setter Tests ===\n");
    RUN_TEST(config_set_get_bool);
    RUN_TEST(config_set_get_int);
    RUN_TEST(config_set_get_string);
    RUN_TEST(config_display_lle_theme_persists);
    RUN_TEST(config_display_ambiguous_width_applies);
    RUN_TEST(config_display_newline_before_prompt_bound);
    RUN_TEST(config_show_lists_migrated_lle_section);
    RUN_TEST(config_get_bool_default);
    RUN_TEST(config_get_int_default);
    RUN_TEST(config_get_string_default);

    /// Shell option getters/setters
    printf("\n=== Shell Option Getter/Setter Tests ===\n");
    RUN_TEST(config_set_get_shell_option);
    RUN_TEST(config_shell_option_nounset);
    RUN_TEST(config_shell_option_xtrace);

    /// Path resolution
    printf("\n=== Path Resolution Tests ===\n");
    RUN_TEST(config_get_xdg_dir);
    RUN_TEST(config_get_xdg_config_path);
    RUN_TEST(config_get_legacy_config_path);
    RUN_TEST(config_get_script_config_path);
    RUN_TEST(config_get_system_config_path);

    /// Section parsing
    printf("\n=== Section Parsing Tests ===\n");
    RUN_TEST(config_parse_section_history);
    RUN_TEST(config_parse_section_completion);
    RUN_TEST(config_parse_section_behavior);
    RUN_TEST(config_parse_section_aliases);
    RUN_TEST(config_parse_section_network);
    RUN_TEST(config_parse_section_scripts);
    RUN_TEST(config_parse_section_keys);
    RUN_TEST(config_parse_section_invalid);

    /// Line parsing
    printf("\n=== Line Parsing Tests ===\n");
    RUN_TEST(config_parse_line_comment);
    RUN_TEST(config_parse_line_empty);
    RUN_TEST(config_parse_line_whitespace);
    RUN_TEST(config_parse_line_section_header);

    /// Script execution control
    printf("\n=== Script Execution Control Tests ===\n");
    RUN_TEST(config_script_execution_control);

    /// Error handling
    printf("\n=== Error Handling Tests ===\n");
    /// RUN_TEST(config_error_message) removed -- see #71 migration

    /// Script path detection
    printf("\n=== Script Path Detection Tests ===\n");
    RUN_TEST(config_script_exists_nonexistent);
    RUN_TEST(config_script_exists_etc_passwd);

    /// Cleanup
    printf("\n=== Cleanup Tests ===\n");
    RUN_TEST(config_cleanup_basic);

    return TEST_RESULT();
}
