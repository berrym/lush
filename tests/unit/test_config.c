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
#include "config_registry.h"
#include "lle/char_width.h"
#include "lush.h"
#include "shell_mode.h"
#include "test_framework.h"
#include <fcntl.h>
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

/// Capture the stdout of config_get_value(@p key) into @p out (NUL-terminated).
/// Used to assert what `config get` actually prints, including its trailing
/// newline (e.g. "true\n").
static void capture_config_get(const char *key, char *out, size_t n) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (n)
            out[0] = '\0';
        return;
    }
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    config_get_value(key);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    ssize_t r = read(pipefd[0], out, n - 1);
    close(pipefd[0]);
    out[r > 0 ? (size_t)r : 0] = '\0';
}

/// Capture the STDERR of config_set_value(@p key, @p value) into @p out, so a
/// test can assert the structured error a rejected set renders (the typed-error
/// text and its describe-based suggestion go to stderr via
/// shell_error_display).
static void capture_config_set_stderr(const char *key, const char *value,
                                      char *out, size_t n) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (n)
            out[0] = '\0';
        return;
    }
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    config_set_value(key, value);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    ssize_t r = read(pipefd[0], out, n - 1);
    close(pipefd[0]);
    out[r > 0 ? (size_t)r : 0] = '\0';
}

/// Set a config key while swallowing its output -- the "Set <key> = <value>"
/// stdout line and any structured-error stderr -- so a test driving several
/// sets (valid or deliberately invalid) does not interleave noise into the
/// suite output.
static void config_set_quiet(const char *key, const char *value) {
    fflush(stdout);
    fflush(stderr);
    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
    config_set_value(key, value);
    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
    dup2(saved_err, STDERR_FILENO);
    close(saved_err);
}

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
    ASSERT_EQ(config.history_size, 10000,
              "history_size should default to 10000");
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

TEST(config_save_sync_preserves_shell_options) {
    config_init();

    /// Enable errexit the way `set -o errexit` does: through the registry, so
    /// the SESSION layer holds true and the shell.* subscriber sets shell_opts.
    config_registry_set_boolean("shell.errexit", true);
    ASSERT_TRUE(config_get_shell_option("shell.errexit"),
                "precondition: errexit enabled via the registry");

    /// `config save` runs config_registry_sync_from_runtime before writing the
    /// TOML. It must NOT flip the option off. The prior shell_sync_from_runtime
    /// wrote shell.errexit=config_get_shell_option("errexit") -- a bare name
    /// the +6 prefix-strip mangled to false -- so saving silently disabled set
    /// -e.
    config_registry_sync_from_runtime();
    ASSERT_TRUE(config_get_shell_option("shell.errexit"),
                "config save (sync_from_runtime) must not disable errexit");
}

TEST(shell_editor_mutual_exclusion_reads_executor) {
    config_init();

    /// Editor mode is single-valued; config get/show read the executor's own
    /// shell_opts field (not the registry), so the field clear in
    /// config_set_shell_option is the whole mutual-exclusion story.
    config_set_value("shell.emacs", "true"); /// known editor: emacs

    /// config set switches the editor and config get reflects it.
    config_set_value("shell.vi", "true");
    ASSERT_TRUE(config_get_shell_option("shell.vi"),
                "vi on after config set shell.vi true");
    ASSERT_FALSE(config_get_shell_option("shell.emacs"),
                 "emacs off after switching to vi");

    /// Second-order: switching BACK must take effect. config set drives
    /// shell_opts directly; relying on the change-gated registry write alone
    /// would no-op here (shell.emacs still reads true at the default) and leave
    /// the editor on vi -- the silent-no-op the projection split removes.
    config_set_value("shell.emacs", "true");
    ASSERT_TRUE(config_get_shell_option("shell.emacs"),
                "switching back to emacs must take effect");
    ASSERT_FALSE(config_get_shell_option("shell.vi"),
                 "vi off after switching back to emacs");

    /// A config-file editor choice takes effect: loading vi=true switches the
    /// live editor field that the executor and config get read. (Clear the
    /// SESSION pins left above so the USER-layer file load is not outranked.)
    config_registry_reset("shell.emacs");
    config_registry_reset("shell.vi");
    char path[] = "/tmp/lush_ed_vi_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0, "temp config file created");
    FILE *a = fdopen(fd, "w");
    fputs("[shell]\nvi = true\n", a);
    fclose(a);
    config_registry_load(path);
    unlink(path); /// remove before any assert can longjmp past the unlink
    ASSERT_TRUE(config_get_shell_option("shell.vi"),
                "a config-file vi=true must switch the editor to vi");
    ASSERT_FALSE(config_get_shell_option("shell.emacs"),
                 "emacs off after a config-file vi=true");

    /// Restore the default trace-free. The load left shell.vi=true in the USER
    /// layer; a SESSION reset alone would only mask it (a stale USER slot a
    /// later reset_all could surface). Overwrite the USER layer back to
    /// default- equal through the load path, drop SESSION pins, then drive the
    /// editor field back to emacs for subsequent tests.
    char rpath[] = "/tmp/lush_ed_rst_XXXXXX";
    int rfd = mkstemp(rpath);
    ASSERT_TRUE(rfd >= 0, "temp restore file created");
    FILE *r = fdopen(rfd, "w");
    fputs("[shell]\nvi = false\nemacs = true\n", r);
    fclose(r);
    config_registry_load(rpath);
    unlink(rpath);
    config_registry_reset("shell.emacs");
    config_registry_reset("shell.vi");
    config_set_value("shell.emacs", "true");
}

TEST(shell_posix_is_a_mode_projection) {
    config_init();
    apply_mode_preset(SHELL_MODE_LUSH);

    /// config set shell.posix true routes to a MODE switch (as set -o posix
    /// does), not a SESSION pin: posix-strictness is purely (mode == POSIX).
    config_set_value("shell.posix", "true");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_POSIX,
              "config set shell.posix true must switch to POSIX mode");

    /// In POSIX mode config get must report true. This gates the get-derive
    /// (reading the executor's posix_mode field): with it removed config get
    /// shell.posix returns false even in POSIX mode, diverging from the
    /// executor and from config show.
    char buf[64];
    capture_config_get("shell.posix", buf, sizeof(buf));
    ASSERT_TRUE(strcmp(buf, "true\n") == 0,
                "config get shell.posix must report true in POSIX mode");

    /// Executor-sourced, not registry-effective: in lush mode posix_mode is
    /// false, and a divergent registry shell.posix=true must NOT leak into
    /// config get. Pinning the registry then asserting config get still reads
    /// false proves the read comes from the executor field; a
    /// registry-effective read would surface the pinned true and fail.
    apply_mode_preset(SHELL_MODE_LUSH);
    config_registry_set_boolean("shell.posix", true); /// divergent registry pin
    capture_config_get("shell.posix", buf, sizeof(buf));
    ASSERT_TRUE(
        strcmp(buf, "false\n") == 0,
        "config get shell.posix must read the live mode (false in lush), "
        "ignoring a divergent registry value");
    config_registry_reset("shell.posix"); /// drop the pin (SESSION layer)

    /// config set shell.posix false while in POSIX returns to lush.
    apply_mode_preset(SHELL_MODE_POSIX);
    config_set_value("shell.posix", "false");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "config set shell.posix false must leave POSIX for lush");

    apply_mode_preset(SHELL_MODE_LUSH);
}

TEST(persisted_false_key_not_loaded_from_file) {
    config_init();

    /// A persisted=false option is runtime/environmental (shell.monitor = job
    /// control), never a saved preference. persisted gates SAVE; it must gate
    /// LOAD too, so a stale or hand-edited file cannot pin a runtime-only
    /// value. shell.onecmd (persisted=true, untouched by other tests) is the
    /// control: a real preference still loads, proving the skip is specific.
    char path[] = "/tmp/lush_cfg_load_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0, "mkstemp should create a temp config file");
    FILE *f = fdopen(fd, "w");
    ASSERT_TRUE(f != NULL, "fdopen should succeed");
    fputs("[shell]\nmonitor = true\nonecmd = true\n", f);
    fclose(f);

    creg_result_t load_rc = config_registry_load(path);
    unlink(path); /// remove before any assert can longjmp past the unlink
    ASSERT_EQ(load_rc, CREG_SUCCESS, "config_registry_load should succeed");

    bool mon = true, one = false;
    config_registry_get_boolean("shell.monitor", &mon);
    config_registry_get_boolean("shell.onecmd", &one);
    ASSERT_FALSE(mon, "shell.monitor (persisted=false) must NOT load from a "
                      "config file -- job control is runtime-determined");
    ASSERT_TRUE(one, "shell.onecmd (persisted=true) must still load -- the "
                     "load-skip is specific to persisted=false keys");

    /// Restore the control trace-free: the load put onecmd=true in the USER
    /// layer, which a SESSION write would only mask (leaving a stale USER slot
    /// a later reset_all could surface). Overwrite the same USER layer with
    /// false through the load path so onecmd returns to its default with no
    /// residue.
    char rpath[] = "/tmp/lush_cfg_rst_XXXXXX";
    int rfd = mkstemp(rpath);
    ASSERT_TRUE(rfd >= 0, "mkstemp should create the restore file");
    FILE *rf = fdopen(rfd, "w");
    fputs("[shell]\nonecmd = false\n", rf);
    fclose(rf);
    config_registry_load(rpath);
    unlink(rpath);
}

TEST(shell_mode_registry_subscriber_applies) {
    config_init();

    /// A registry shell.mode write must reconcile the *runtime* mode, not just
    /// the label -- this subscriber is what makes a lushrc mode= take effect.
    config_registry_set_string("shell.mode", "bash");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_BASH,
              "a shell.mode registry write must switch the runtime mode");
    /// extended_glob is curated on in lush, off in bash: proves the feature
    /// matrix (not just the label) followed the registry value.
    ASSERT_FALSE(shell_mode_allows(FEATURE_EXTENDED_GLOB),
                 "the feature matrix must follow the registry mode");

    /// Restore lush for subsequent tests.
    config_registry_set_string("shell.mode", "lush");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "restoring the registry mode must switch back");
}

TEST(reapplying_current_mode_drops_overrides) {
    config_init();

    /// Land in a known mode with no overrides, then pin a feature override the
    /// way setopt/unsetopt would.
    apply_mode_preset(SHELL_MODE_LUSH);
    shell_feature_disable(FEATURE_EXTENDED_GLOB);
    ASSERT_FALSE(shell_mode_allows(FEATURE_EXTENDED_GLOB),
                 "precondition: extended_glob overridden off");

    /// Re-selecting the SAME mode (mode --reset, set -o posix while posix) must
    /// drop the override and re-seed the preset, even though the registry
    /// effective shell.mode does not change -- the change-gated subscriber
    /// alone would skip it, so apply_mode_preset must apply the runtime
    /// directly.
    apply_mode_preset(SHELL_MODE_LUSH);
    ASSERT_TRUE(shell_mode_allows(FEATURE_EXTENDED_GLOB),
                "re-applying the current mode must drop per-feature overrides");
}

TEST(set_o_only_options_gained_config_surface) {
    config_init();

    /// errtrace (set -E) / functrace (set -T) / pipeline-diagnostic were set -o
    /// options with no config key. Registering them means a registry write now
    /// reaches shell_opts via the shell.* subscriber + config_set_shell_option.
    config_registry_set_boolean("shell.errtrace", true);
    ASSERT_TRUE(
        config_get_shell_option("shell.errtrace"),
        "a shell.errtrace registry write must reach shell_opts.errtrace");

    config_registry_set_boolean("shell.pipeline-diagnostic", true);
    ASSERT_TRUE(
        config_get_shell_option("shell.pipeline-diagnostic"),
        "a shell.pipeline-diagnostic registry write must reach shell_opts");

    /// Leave the registry clean for subsequent tests.
    config_registry_set_boolean("shell.errtrace", false);
    config_registry_set_boolean("shell.pipeline-diagnostic", false);
}

TEST(argv_shell_options_hydrate_to_session) {
    config_init();

    /// Reproduce the init sequence: a known baseline, then a "command line"
    /// flag flips exit_on_error (as parse_opts -e does), captured before the
    /// mode preset, then replayed into the registry once it exists.
    shell_opts.exit_on_error = false;
    shell_opts_record_baseline();
    shell_opts.exit_on_error = true;
    shell_opts_capture_argv_overrides();
    shell_opts_hydrate_argv_to_registry();

    creg_inspect_t info;
    ASSERT_EQ(config_registry_inspect("shell.errexit", &info), CREG_SUCCESS,
              "shell.errexit must be registered");
    ASSERT_TRUE(info.layers[CREG_LAYER_SESSION].present,
                "an argv flag must hydrate the option into the SESSION layer");
    ASSERT_TRUE(info.layers[CREG_LAYER_SESSION].value.data.boolean,
                "the hydrated SESSION value must match the flag");

    /// An option the command line did not set must not gain a SESSION override.
    creg_inspect_t untouched;
    ASSERT_EQ(config_registry_inspect("shell.xtrace", &untouched), CREG_SUCCESS,
              "shell.xtrace must be registered");
    ASSERT_FALSE(untouched.layers[CREG_LAYER_SESSION].present,
                 "an unset option must not gain a SESSION override");

    /// Leave the registry clean for subsequent tests.
    config_registry_reset("shell.errexit");
    shell_opts.exit_on_error = false;
}

TEST(config_get_resolves_toggles_from_registry) {
    config_init();

    /// The flip: config get/show resolve genuine toggles from the registry (the
    /// single source of truth), not the shell_opts cache. Force the divergence
    /// the shell.* subscriber normally prevents -- registry true, cache false
    /// -- and assert config get follows the registry. Reverting config get to a
    /// shell_opts read (the pre-flip behavior) makes this read "false".
    config_registry_set_boolean("shell.errexit", true);
    shell_opts.exit_on_error = false; /// desync the cache behind the registry
    char buf[64];
    capture_config_get("shell.errexit", buf, sizeof(buf));
    ASSERT_TRUE(strcmp(buf, "true\n") == 0,
                "config get must resolve a genuine toggle from registry-"
                "effective, not the shell_opts cache");

    /// Restore consistency for later tests.
    config_registry_reset("shell.errexit");
    shell_opts.exit_on_error = false;
}

TEST(config_set_validates_typed_keys) {
    config_init();

    /// Drop any SESSION residue up front so the test starts clean even if a
    /// prior run of it longjmp'd out before its trailing restore.
    config_registry_reset("completion.match_mode");
    config_registry_reset("display.optimization_level");

    /// config_register_types attaches an enum descriptor to
    /// completion.match_mode and an int-range [0,4] to display.optimization_-
    /// level, so config set rejects invalid values at the registry chokepoint
    /// instead of silently storing them, reporting the valid set. A valid value
    /// still applies. Valid sets go through config_set_quiet to keep the "Set
    /// X = Y" lines out of the suite output.
    char msg[160], after[64];

    /// Enum: a non-member is rejected, the message lists the valid values, and
    /// the prior value is unchanged.
    config_set_quiet("completion.match_mode", "substring");
    capture_config_set_stderr("completion.match_mode", "bogus", msg,
                              sizeof(msg));
    ASSERT_TRUE(
        strstr(msg, "one of: prefix substring fuzzy") != NULL,
        "a rejected enum set must report the valid values via describe");
    capture_config_get("completion.match_mode", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "substring\n") == 0,
                "an invalid enum value must be rejected, leaving the prior "
                "value (was substring)");
    /// A valid member applies.
    config_set_quiet("completion.match_mode", "fuzzy");
    capture_config_get("completion.match_mode", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "fuzzy\n") == 0, "a valid enum value must apply");

    /// Int range: out of [0,4] is rejected with the range in the message; in
    /// range applies; the inclusive upper bound is accepted.
    config_set_quiet("display.optimization_level", "2");
    capture_config_set_stderr("display.optimization_level", "99", msg,
                              sizeof(msg));
    ASSERT_TRUE(strstr(msg, "0..4") != NULL,
                "a rejected int set must report the valid range via describe");
    capture_config_get("display.optimization_level", after, sizeof(after));
    ASSERT_TRUE(
        strcmp(after, "2\n") == 0,
        "an out-of-range int must be rejected, leaving the prior value");
    config_set_quiet("display.optimization_level", "4");
    capture_config_get("display.optimization_level", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "4\n") == 0,
                "the inclusive upper bound must be accepted");

    /// Restore by dropping the SESSION layer (not pinning a new SESSION value),
    /// so the keys fall back to their mode/default and leave no residue.
    config_registry_reset("completion.match_mode");
    config_registry_reset("display.optimization_level");
}

/// Run config_registry_validate_schema and print any violations to stderr.
static size_t report_schema_violations(const char *label) {
    creg_schema_violation_t v[64];
    size_t n = config_registry_validate_schema(v, 64);
    for (size_t i = 0; i < n && i < 64; i++) {
        fprintf(stderr, "  [%s] schema violation: %s -- %s\n", label, v[i].key,
                v[i].problem);
    }
    return n;
}

TEST(schema_invariant_holds_on_real_config) {
    /// Hermetic w.r.t. the user config: point HOME/XDG at an empty dir so
    /// config_init loads no developer or CI lushrc. (The system path
    /// /etc/lush/lushrc.toml is not isolated; it is assumed absent in the build
    /// environment.) strdup the saved env -- a real HOME path can exceed any
    /// fixed buffer, and a truncated restore would corrupt later tests.
    char tmpl[] = "/tmp/lush_schema_home_XXXXXX";
    char *home = mkdtemp(tmpl);
    ASSERT_TRUE(home != NULL, "temp HOME created");
    char *saved_home = getenv("HOME");
    char *saved_xdg = getenv("XDG_CONFIG_HOME");
    char *keep_home = saved_home ? strdup(saved_home) : NULL;
    char *keep_xdg = saved_xdg ? strdup(saved_xdg) : NULL;
    setenv("HOME", home, 1);
    setenv("XDG_CONFIG_HOME", home, 1);

    /// Start from a clean registry: the suite's earlier config_init calls
    /// accumulate bindings (config_init does not reset the registry), which
    /// would double-count. A fresh cleanup+init mirrors a production startup.
    config_registry_cleanup();
    config_init();

    /// The real configuration must be schema-consistent: every typed key's
    /// default and mode-defaults satisfy their constraints (mode-independent),
    /// and every bound runtime cell equals its key's effective value. The
    /// bound-cell check sees only the ACTIVE mode's effective value, so switch
    /// through every mode and re-check: this exercises each mode's binding
    /// round-trip (e.g. an enum mode-default the binding maps through its pair
    /// table into the cell).
    size_t total = report_schema_violations("init");
    shell_mode_t modes[] = {SHELL_MODE_LUSH, SHELL_MODE_BASH, SHELL_MODE_ZSH,
                            SHELL_MODE_POSIX};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        apply_mode_preset(modes[i]);
        total += report_schema_violations(shell_mode_name(modes[i]));
    }

    /// Restore the environment and re-init from it before asserting, so a
    /// failure's longjmp does not leave HOME pointing at the temp dir for later
    /// tests.
    if (keep_home) {
        setenv("HOME", keep_home, 1);
    } else {
        unsetenv("HOME");
    }
    if (keep_xdg) {
        setenv("XDG_CONFIG_HOME", keep_xdg, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }
    free(keep_home);
    free(keep_xdg);
    config_registry_cleanup();
    config_init();
    (void)rmdir(home); /// empty dir; config_init only reads under HOME

    ASSERT_EQ(total, (size_t)0,
              "the registry schema must be internally consistent across all "
              "modes (see stderr)");
}

TEST(all_typed_keys_attach) {
    config_init();

    /// config_register_types ignores each config_registry_set_type return, so a
    /// misspelled / unregistered / wrong-storage table key silently fails to
    /// attach and that key then validates nothing. The failure counter catches
    /// any such entry across the whole table, so a mis-registered key cannot
    /// ship with a green suite even though only a few keys are behavior-tested.
    ASSERT_EQ(config_type_attach_failure_count(), 0,
              "every type descriptor in the registration tables must attach");
}

TEST(beginner_tier_is_registered) {
    config_init();

    /// Every key in the curated beginner table must exist; a misspelled or
    /// renamed key fails to attach and the wizard would silently skip it.
    ASSERT_EQ(config_tier_attach_failure_count(), 0,
              "every beginner-tier key must attach");

    /// The wizard walks exactly the curated set. Spot-check that the set is
    /// non-empty, that a representative key is tagged BEGINNER, and that an
    /// everyday-but-not-first-run key is left untagged.
    char keys[64][CREG_KEY_MAX];
    size_t n = config_registry_collect_by_tier(CREG_TIER_BEGINNER, keys, 64);
    ASSERT_TRUE(n >= 8, "the beginner tier is populated");

    creg_tier_t tier = CREG_TIER_UNSET;
    ASSERT_EQ(config_registry_get_tier("completion.match_mode", &tier),
              CREG_SUCCESS, "match_mode is a registered key");
    ASSERT_EQ(tier, CREG_TIER_BEGINNER, "match_mode is in the beginner tier");

    ASSERT_EQ(config_registry_get_tier("behavior.tab_width", &tier),
              CREG_SUCCESS, "tab_width is a registered key");
    ASSERT_TRUE(tier != CREG_TIER_BEGINNER,
                "an advanced knob is not in the beginner tier");

    /// The wizard prefers each key's plain-language description over its terse
    /// help, so every beginner-tier key must declare a non-empty description.
    /// A future addition that forgets one would silently fall back to the
    /// one-line help and read as an afterthought in the guided setup. Checked
    /// here, against the already-collected set, to avoid a second config_init.
    for (size_t i = 0; i < n; i++) {
        const char *desc = config_registry_get_description(keys[i]);
        ASSERT_TRUE(desc != NULL && desc[0] != '\0', keys[i]);
    }
}

TEST(config_set_validates_increment_two_keys) {
    config_init();

    /// The second batch of typed keys: a representative enum, a bounded range,
    /// and a non-negative range. Each must reject an invalid value (proving the
    /// descriptor attached -- a mis-registered table entry would silently fail
    /// to attach and accept anything) and apply a valid one.
    char after[64];

    /// Enum: history.search_mode accepts plain/prefix, rejects others.
    config_set_quiet("history.search_mode", "bogus");
    capture_config_get("history.search_mode", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "bogus\n") != 0,
                "history.search_mode must reject an invalid enum value");
    config_set_quiet("history.search_mode", "plain");
    capture_config_get("history.search_mode", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "plain\n") == 0,
                "history.search_mode must accept a valid enum value");

    /// Bounded range [1,5]: reject 9, accept 3.
    config_set_quiet("behavior.autocorrect_max_suggestions", "2");
    config_set_quiet("behavior.autocorrect_max_suggestions", "9");
    capture_config_get("behavior.autocorrect_max_suggestions", after,
                       sizeof(after));
    ASSERT_TRUE(strcmp(after, "2\n") == 0,
                "autocorrect_max_suggestions must reject 9 (range 1..5)");

    /// Non-negative range: reject a negative, accept 0 (the documented
    /// disable sentinel).
    config_set_quiet("behavior.loop_failure_streak", "0");
    config_set_quiet("behavior.loop_failure_streak", "-3");
    capture_config_get("behavior.loop_failure_streak", after, sizeof(after));
    ASSERT_TRUE(strcmp(after, "0\n") == 0,
                "loop_failure_streak must reject a negative value, keeping 0");

    /// history.size is a wizard-exposed int bound to a 32-bit cell: it must
    /// reject a negative and an over-INT_MAX value (either would truncate
    /// through the binding and diverge from what is stored), accepting a normal
    /// value.
    config_set_quiet("history.size", "8000");
    config_set_quiet("history.size", "-5");
    config_set_quiet("history.size", "5000000000");
    capture_config_get("history.size", after, sizeof(after));
    ASSERT_TRUE(
        strcmp(after, "8000\n") == 0,
        "history.size must reject negative / over-INT_MAX, keeping 8000");

    config_registry_reset("history.search_mode");
    config_registry_reset("behavior.autocorrect_max_suggestions");
    config_registry_reset("behavior.loop_failure_streak");
    config_registry_reset("history.size");
}

/// Capture the STDERR of config_load_file(@p path) into @p out.
static void capture_load_file_stderr(const char *path, char *out, size_t n) {
    fflush(stderr);
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (n)
            out[0] = '\0';
        return;
    }
    int saved = dup(STDERR_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    config_load_file(path);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    ssize_t r = read(pipefd[0], out, n - 1);
    close(pipefd[0]);
    out[r > 0 ? (size_t)r : 0] = '\0';
}

static void write_temp_toml(char *path_template, const char *contents) {
    int fd = mkstemp(path_template);
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        if (f) {
            fputs(contents, f);
            fclose(f);
        }
    }
}

TEST(config_load_file_warns_on_dropped_keys) {
    config_init();

    /// Two distinct rejections: an out-of-range typed value (INVALID_VALUE) and
    /// an unknown key (NOT_FOUND). config_load_file warns naming both and tags
    /// each reason (the interactive surface errors on the same values, so the
    /// load surface must not be silent).
    char bad[] = "/tmp/lush_loadwarn_XXXXXX";
    write_temp_toml(bad,
                    "[display]\noptimization_level = 99\nno_such_key = 1\n");
    char out[512];
    capture_load_file_stderr(bad, out, sizeof(out));
    unlink(bad);
    ASSERT_TRUE(strstr(out, "ignored") != NULL &&
                    strstr(out, "optimization_level") != NULL,
                "config_load_file warns and names the out-of-range key");
    ASSERT_TRUE(strstr(out, "no_such_key") != NULL &&
                    strstr(out, "unknown key") != NULL,
                "config_load_file names the unknown key and its reason");

    /// A clean config produces no drop warning.
    char clean[] = "/tmp/lush_loadclean_XXXXXX";
    write_temp_toml(clean, "[display]\noptimization_level = 2\n");
    char out2[256];
    capture_load_file_stderr(clean, out2, sizeof(out2));
    unlink(clean);
    ASSERT_TRUE(strstr(out2, "ignored") == NULL,
                "a clean config emits no drop warning");
}

TEST(config_load_file_truncates_long_drop_list) {
    config_init();

    /// Enough dropped keys that the rendered list overflows the message buffer
    /// before the array cap (the byte budget, not CREG_LOAD_SKIP_MAX, is what
    /// truncates). The warning must still terminate with the "..." marker so
    /// the user knows the list was cut, and must stay within its fixed buffer.
    char body[2048];
    size_t off = (size_t)snprintf(body, sizeof(body), "[display]\n");
    for (int i = 0; i < 24; i++) {
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                                "removed_phantom_setting_number_%02d = %d\n", i,
                                i);
    }
    char bad[] = "/tmp/lush_loadtrunc_XXXXXX";
    write_temp_toml(bad, body);
    char out[1024];
    capture_load_file_stderr(bad, out, sizeof(out));
    unlink(bad);

    ASSERT_TRUE(strstr(out, "ignored 24") != NULL,
                "the warning reports the full dropped-key count");
    ASSERT_TRUE(strstr(out, "...") != NULL,
                "a list cut by the byte budget is marked truncated");
}

TEST(config_show_shell_lists_options_not_features) {
    config_init();

    /// config show shell renders the shell.* OPTIONS from the static schema
    /// (mode + mode_strict + the booleans), full shell.<name> names and live
    /// values. It deliberately EXCLUDES the 56 shell.feature.* keys (their
    /// discovery surface is debug features / setopt) so the listing is not
    /// flooded. The pipe capture also exercises the buffered pager render path
    /// (non-tty stdout streams verbatim).
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0, "pipe should succeed");
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    config_show_section(CONFIG_SECTION_SHELL);
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    char buf[8192];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    ASSERT_TRUE(n > 0,
                "config show should produce output for the shell section");
    buf[n > 0 ? n : 0] = '\0';

    /// The shell options appear with full shell.<name> names, including the
    /// mode keys and the newly-registered set -o options.
    ASSERT_TRUE(strstr(buf, "shell.errexit") != NULL,
                "config show shell should list shell.errexit");
    ASSERT_TRUE(strstr(buf, "shell.mode =") != NULL,
                "config show shell should list shell.mode");
    ASSERT_TRUE(strstr(buf, "shell.mode_strict") != NULL,
                "config show shell should list shell.mode_strict");
    ASSERT_TRUE(strstr(buf, "shell.errtrace") != NULL,
                "config show shell should list the registered set -o options");

    /// The 56 shell.feature.* keys are deliberately NOT flooded into the
    /// listing.
    ASSERT_TRUE(strstr(buf, "feature.") == NULL,
                "config show shell must not flood the shell.feature.* keys");
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
 * LINE PARSING TESTS
 * ============================================================================
 */

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
    RUN_TEST(config_save_sync_preserves_shell_options);
    RUN_TEST(shell_editor_mutual_exclusion_reads_executor);
    RUN_TEST(shell_posix_is_a_mode_projection);
    RUN_TEST(persisted_false_key_not_loaded_from_file);
    RUN_TEST(shell_mode_registry_subscriber_applies);
    RUN_TEST(reapplying_current_mode_drops_overrides);
    RUN_TEST(set_o_only_options_gained_config_surface);
    RUN_TEST(argv_shell_options_hydrate_to_session);
    RUN_TEST(config_get_resolves_toggles_from_registry);
    RUN_TEST(config_set_validates_typed_keys);
    RUN_TEST(schema_invariant_holds_on_real_config);
    RUN_TEST(all_typed_keys_attach);
    RUN_TEST(beginner_tier_is_registered);
    RUN_TEST(config_set_validates_increment_two_keys);
    RUN_TEST(config_load_file_warns_on_dropped_keys);
    RUN_TEST(config_load_file_truncates_long_drop_list);
    RUN_TEST(config_show_shell_lists_options_not_features);
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

    /// Script execution control
    printf("\n=== Script Execution Control Tests ===\n");

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
