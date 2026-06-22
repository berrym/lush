/**
 * @file test_compat.c
 * @brief Unit tests for shell compatibility database system
 *
 * Tests the compatibility module including:
 * - Database initialization and cleanup
 * - Entry queries
 * - Portability checking
 * - Strict mode
 * - Utility functions
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compat.h"
#include "shell_mode.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Capture what a compat debug-print routine writes to stderr. Redirects the
/// stderr stream to a temp file, runs body, then restores and reads it back
/// into out (NUL-terminated). Uses the same stderr-reassignment the discard
/// path already relies on.
#define CAPTURE_STDERR(out, bufsize, body)                                     \
    do {                                                                       \
        FILE *_old = stderr;                                                   \
        FILE *_tmp = tmpfile();                                                \
        (out)[0] = '\0';                                                       \
        if (_tmp) {                                                            \
            stderr = _tmp;                                                     \
            body;                                                              \
            fflush(_tmp);                                                      \
            rewind(_tmp);                                                      \
            size_t _r = fread((out), 1, (bufsize) - 1, _tmp);                  \
            (out)[_r] = '\0';                                                  \
            stderr = _old;                                                     \
            fclose(_tmp);                                                      \
        }                                                                      \
    } while (0)

/// Test framework macros

/* ============================================================================
 * DATABASE INITIALIZATION TESTS
 * ============================================================================
 */

TEST(compat_init_basic) {
    int result = compat_init(NULL);
    ASSERT_EQ(result, 0, "compat_init with NULL should use defaults");
    compat_cleanup();
}

TEST(compat_init_cleanup_cycle) {
    for (int i = 0; i < 3; i++) {
        int result = compat_init(LUSH_COMPAT_DATA_DIR);
        ASSERT_EQ(result, 0, "compat_init should succeed");
        ASSERT(compat_get_entry_count() > 0, "init loads the database");
        compat_cleanup();
        ASSERT_EQ(compat_get_entry_count(), 0u,
                  "cleanup releases all loaded entries");
    }
}

TEST(compat_cleanup_without_init) {
    /// Cleanup before any init is safe and leaves an empty database.
    compat_cleanup();
    ASSERT_EQ(compat_get_entry_count(), 0u,
              "uninitialized database has no entries");
}

TEST(compat_reload) {
    compat_init(NULL);
    /// Reloading re-reads the same sources, so the loaded set is preserved:
    /// the entry count is identical before and after, whatever it is.
    size_t before = compat_get_entry_count();
    compat_reload();
    ASSERT_EQ(compat_get_entry_count(), before,
              "reload should preserve the entry count");
    compat_cleanup();
}

/* ============================================================================
 * CATEGORY NAME TESTS
 * ============================================================================
 */

TEST(compat_category_name_builtin) {
    const char *name = compat_category_name(COMPAT_CATEGORY_BUILTIN);
    ASSERT_STR_EQ(name, "builtin", "category name mismatch");
}

TEST(compat_category_name_expansion) {
    const char *name = compat_category_name(COMPAT_CATEGORY_EXPANSION);
    ASSERT_STR_EQ(name, "expansion", "category name mismatch");
}

TEST(compat_category_name_quoting) {
    const char *name = compat_category_name(COMPAT_CATEGORY_QUOTING);
    ASSERT_STR_EQ(name, "quoting", "category name mismatch");
}

TEST(compat_category_name_syntax) {
    const char *name = compat_category_name(COMPAT_CATEGORY_SYNTAX);
    ASSERT_STR_EQ(name, "syntax", "category name mismatch");
}

TEST(compat_category_parse_valid) {
    compat_category_t cat;

    bool result = compat_category_parse("builtin", &cat);
    ASSERT(result, "Should parse 'builtin'");
    ASSERT_EQ(cat, COMPAT_CATEGORY_BUILTIN, "Should be BUILTIN category");
}

TEST(compat_category_parse_invalid) {
    compat_category_t cat;

    bool result = compat_category_parse("notacategory", &cat);
    ASSERT(!result, "Should fail for invalid category");
}

/* ============================================================================
 * SEVERITY NAME TESTS
 * ============================================================================
 */

TEST(compat_severity_name_info) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_INFO);
    ASSERT_STR_EQ(name, "info", "severity name mismatch");
}

TEST(compat_severity_name_warning) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_WARNING);
    ASSERT_STR_EQ(name, "warning", "severity name mismatch");
}

TEST(compat_severity_name_error) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_ERROR);
    ASSERT_STR_EQ(name, "error", "severity name mismatch");
}

TEST(compat_severity_parse_valid) {
    compat_severity_t sev;

    bool result = compat_severity_parse("warning", &sev);
    ASSERT(result, "Should parse 'warning'");
    ASSERT_EQ(sev, COMPAT_SEVERITY_WARNING, "Should be WARNING severity");
}

TEST(compat_severity_parse_error) {
    compat_severity_t sev;

    bool result = compat_severity_parse("error", &sev);
    ASSERT(result, "Should parse 'error'");
    ASSERT_EQ(sev, COMPAT_SEVERITY_ERROR, "Should be ERROR severity");
}

TEST(compat_severity_parse_invalid) {
    compat_severity_t sev;

    bool result = compat_severity_parse("notaseverity", &sev);
    ASSERT(!result, "Should fail for invalid severity");
}

/* ============================================================================
 * FIX TYPE TESTS
 * ============================================================================
 */

TEST(compat_fix_type_name_none) {
    const char *name = compat_fix_type_name(FIX_TYPE_NONE);
    ASSERT_STR_EQ(name, "none", "fix type name mismatch");
}

TEST(compat_fix_type_name_safe) {
    const char *name = compat_fix_type_name(FIX_TYPE_SAFE);
    ASSERT_STR_EQ(name, "safe", "fix type name mismatch");
}

TEST(compat_fix_type_name_unsafe) {
    const char *name = compat_fix_type_name(FIX_TYPE_UNSAFE);
    ASSERT_STR_EQ(name, "unsafe", "fix type name mismatch");
}

TEST(compat_fix_type_name_manual) {
    const char *name = compat_fix_type_name(FIX_TYPE_MANUAL);
    ASSERT_STR_EQ(name, "manual", "fix type name mismatch");
}

TEST(compat_fix_type_parse_valid) {
    fix_type_t type;

    bool result = compat_fix_type_parse("safe", &type);
    ASSERT(result, "Should parse 'safe'");
    ASSERT_EQ(type, FIX_TYPE_SAFE, "Should be SAFE fix type");
}

TEST(compat_fix_type_parse_unsafe) {
    fix_type_t type;

    bool result = compat_fix_type_parse("unsafe", &type);
    ASSERT(result, "Should parse 'unsafe'");
    ASSERT_EQ(type, FIX_TYPE_UNSAFE, "Should be UNSAFE fix type");
}

TEST(compat_fix_type_parse_manual) {
    fix_type_t type;

    bool result = compat_fix_type_parse("manual", &type);
    ASSERT(result, "Should parse 'manual'");
    ASSERT_EQ(type, FIX_TYPE_MANUAL, "Should be MANUAL fix type");
}

TEST(compat_fix_type_parse_invalid) {
    fix_type_t type;

    bool result = compat_fix_type_parse("notafixtype", &type);
    ASSERT(!result, "Should fail for invalid fix type");
}

/* ============================================================================
 * TARGET SHELL TESTS
 * ============================================================================
 */

TEST(compat_set_get_target) {
    compat_init(NULL);

    compat_set_target("bash");
    ASSERT_STR_EQ(compat_get_target(), "bash", "Target should be bash");

    compat_set_target("zsh");
    ASSERT_STR_EQ(compat_get_target(), "zsh", "Target should be zsh");

    compat_set_target(NULL);
    ASSERT_STR_EQ(compat_get_target(), "posix", "NULL should reset to posix");

    compat_cleanup();
}

TEST(compat_get_target_default) {
    compat_init(NULL);

    /// The default target is posix until compat_set_target changes it.
    ASSERT_STR_EQ(compat_get_target(), "posix", "default target is posix");

    compat_cleanup();
}

/* ============================================================================
 * STRICT MODE TESTS
 * ============================================================================
 */

TEST(compat_set_strict) {
    compat_init(NULL);

    compat_set_strict(true);
    ASSERT(compat_is_strict(), "Strict mode should be enabled");

    compat_set_strict(false);
    ASSERT(!compat_is_strict(), "Strict mode should be disabled");

    compat_cleanup();
}

TEST(compat_is_strict_default) {
    compat_init(NULL);

    /// Default should be non-strict
    ASSERT(!compat_is_strict(), "Strict mode should be off by default");

    compat_cleanup();
}

/* ============================================================================
 * ENTRY QUERY TESTS
 * ============================================================================
 */

TEST(compat_get_entry_count) {
    /// Load the shipped database from its source location. The default
    /// search path is relative to cwd/exe and does not resolve from the
    /// build directory the test runner uses, so point init at the data
    /// directory directly to make the count deterministic.
    compat_init(LUSH_COMPAT_DATA_DIR);

    size_t count = compat_get_entry_count();
    ASSERT(count > 0, "compatibility database should load entries");

    compat_cleanup();
}

TEST(compat_get_entry_nonexistent) {
    compat_init(NULL);

    /// An id that is in no database returns NULL regardless of what is
    /// loaded -- the lookup must not return a stale or garbage entry.
    const compat_entry_t *entry = compat_get_entry("nonexistent_entry_id");
    ASSERT_NULL(entry, "unknown entry id should return NULL");

    compat_cleanup();
}

TEST(compat_get_by_category) {
    compat_init(LUSH_COMPAT_DATA_DIR);

    const compat_entry_t *entries[512];
    size_t count =
        compat_get_by_category(COMPAT_CATEGORY_QUOTING, entries, 512);
    ASSERT(count > 0, "the quoting category has entries");
    ASSERT(count <= compat_get_entry_count(), "subset cannot exceed the whole");
    /// Every returned entry is actually in the requested category.
    for (size_t i = 0; i < count; i++) {
        ASSERT_EQ(entries[i]->category, COMPAT_CATEGORY_QUOTING,
                  "category filter returned a foreign entry");
    }

    compat_cleanup();
}

TEST(compat_get_by_feature) {
    compat_init(LUSH_COMPAT_DATA_DIR);

    const compat_entry_t *entries[64];
    size_t count = compat_get_by_feature("arrays", entries, 64);
    ASSERT(count > 0, "the arrays feature is catalogued");
    /// Every returned entry advertises the requested feature.
    for (size_t i = 0; i < count; i++) {
        ASSERT_STR_EQ(entries[i]->feature, "arrays",
                      "feature filter returned a foreign entry");
    }

    compat_cleanup();
}

TEST(compat_get_first_by_feature) {
    compat_init(NULL);

    /// The single-result accessor must agree with the bulk query: a hit one
    /// way is a hit the other, whatever the database happens to catalogue.
    const compat_entry_t *first = compat_get_first_by_feature("arrays");
    const compat_entry_t *bulk[10];
    size_t n = compat_get_by_feature("arrays", bulk, 10);
    ASSERT_EQ(first != NULL, n > 0,
              "get_first_by_feature should agree with get_by_feature");

    compat_cleanup();
}

/* ============================================================================
 * FOREACH ENTRY TESTS
 * ============================================================================
 */

static int foreach_count;

static void count_callback(const compat_entry_t *entry, void *user_data) {
    (void)entry;
    (void)user_data;
    foreach_count++;
}

TEST(compat_foreach_entry) {
    compat_init(NULL);

    foreach_count = 0;
    compat_foreach_entry(count_callback, NULL);
    /// Count should match get_entry_count
    ASSERT_EQ((size_t)foreach_count, compat_get_entry_count(),
              "foreach should visit all entries");

    compat_cleanup();
}

/* ============================================================================
 * PORTABILITY CHECKING TESTS
 * ============================================================================
 */

TEST(compat_is_portable_simple) {
    compat_init(NULL);

    /// 'echo hello' is plain POSIX with no compatibility caveats, so it is
    /// reported portable.
    compat_result_t result;
    bool portable = compat_is_portable("echo hello", SHELL_MODE_POSIX, &result);
    ASSERT(portable, "'echo hello' should be reported portable in POSIX mode");

    compat_cleanup();
}

TEST(compat_is_portable_null_result) {
    compat_init(NULL);

    /// Passing a NULL result struct must not crash and must yield the same
    /// verdict as passing a real one -- the out-param is optional detail,
    /// not part of the decision.
    compat_result_t result;
    bool with_result =
        compat_is_portable("echo hello", SHELL_MODE_POSIX, &result);
    bool null_result = compat_is_portable("echo hello", SHELL_MODE_POSIX, NULL);
    ASSERT_EQ(null_result, with_result,
              "NULL result pointer should not change the verdict");

    compat_cleanup();
}

TEST(compat_check_line) {
    compat_init(LUSH_COMPAT_DATA_DIR);

    /// A bash array expansion is not portable to POSIX sh, so it is flagged;
    /// a plain command is clean.
    compat_result_t results[16];
    size_t flagged =
        compat_check_line("echo ${arr[@]}", SHELL_MODE_POSIX, results, 16);
    ASSERT(flagged > 0, "array expansion is flagged under POSIX");

    size_t clean =
        compat_check_line("echo hello", SHELL_MODE_POSIX, results, 16);
    ASSERT_EQ(clean, 0u, "a portable line raises no issues");

    compat_cleanup();
}

TEST(compat_check_script) {
    compat_init(LUSH_COMPAT_DATA_DIR);

    /// A script using bash arrays is flagged under POSIX; a portable script
    /// raises nothing.
    compat_result_t results[16];
    size_t flagged =
        compat_check_script("#!/bin/bash\narr=(a b c)\necho ${arr[@]}\n",
                            SHELL_MODE_POSIX, results, 16);
    ASSERT(flagged > 0, "a bash script is flagged under POSIX");

    size_t clean = compat_check_script("#!/bin/sh\necho hello\n",
                                       SHELL_MODE_POSIX, results, 16);
    ASSERT_EQ(clean, 0u, "a portable script raises no issues");

    compat_cleanup();
}

/* ============================================================================
 * EFFECTIVE SEVERITY TESTS
 * ============================================================================
 */

TEST(compat_effective_severity_normal) {
    compat_init(NULL);

    compat_entry_t entry = {0};
    entry.lint.severity = COMPAT_SEVERITY_WARNING;

    compat_set_strict(false);
    compat_severity_t sev = compat_effective_severity(&entry);
    ASSERT_EQ(sev, COMPAT_SEVERITY_WARNING, "Should be warning in normal mode");

    compat_cleanup();
}

TEST(compat_effective_severity_strict) {
    compat_init(NULL);

    compat_entry_t entry = {0};
    entry.lint.severity = COMPAT_SEVERITY_WARNING;

    compat_set_strict(true);
    compat_severity_t sev = compat_effective_severity(&entry);
    /// Strict mode elevates a warning to an error.
    ASSERT_EQ(sev, COMPAT_SEVERITY_ERROR,
              "strict mode elevates warnings to errors");

    compat_set_strict(false);
    compat_cleanup();
}

/* ============================================================================
 * FIX TYPE FOR TARGET TESTS
 * ============================================================================
 */

TEST(compat_get_fix_type_for_target) {
    compat_fix_class_t fix_class = {.posix = FIX_TYPE_SAFE,
                                    .bash = FIX_TYPE_UNSAFE,
                                    .zsh = FIX_TYPE_MANUAL,
                                    .lush = FIX_TYPE_NONE};

    fix_type_t type;

    type = compat_get_fix_type_for_target(&fix_class, "posix");
    ASSERT_EQ(type, FIX_TYPE_SAFE, "POSIX fix type should be SAFE");

    type = compat_get_fix_type_for_target(&fix_class, "bash");
    ASSERT_EQ(type, FIX_TYPE_UNSAFE, "Bash fix type should be UNSAFE");

    type = compat_get_fix_type_for_target(&fix_class, "zsh");
    ASSERT_EQ(type, FIX_TYPE_MANUAL, "Zsh fix type should be MANUAL");

    type = compat_get_fix_type_for_target(&fix_class, "lush");
    ASSERT_EQ(type, FIX_TYPE_NONE, "Lush fix type should be NONE");
}

/* ============================================================================
 * FORMAT RESULT TESTS
 * ============================================================================
 */

TEST(compat_format_result) {
    compat_init(NULL);

    compat_result_t result = {.is_portable = false,
                              .entry = NULL,
                              .target = SHELL_MODE_POSIX,
                              .line = 5,
                              .column = 10};

    char buffer[256];
    int len = compat_format_result(&result, buffer, sizeof(buffer));
    ASSERT(len >= 0, "Format should succeed");
    ASSERT((size_t)len < sizeof(buffer), "Should not overflow buffer");
    /// With no offending entry the result formats as the portable message.
    ASSERT_STR_EQ(buffer, "No compatibility issues found",
                  "portable result formats as the no-issues message");
    ASSERT_EQ((size_t)len, strlen(buffer), "length matches written bytes");

    compat_cleanup();
}

/* ============================================================================
 * DEBUG FUNCTIONS TESTS
 * ============================================================================
 */

TEST(compat_debug_print_stats) {
    compat_init(LUSH_COMPAT_DATA_DIR);

    char out[2048];
    CAPTURE_STDERR(out, sizeof(out), compat_debug_print_stats());

    /// Stats report the live entry count and the per-category breakdown.
    char expected_total[64];
    snprintf(expected_total, sizeof(expected_total), "Total entries: %zu",
             compat_get_entry_count());
    ASSERT(strstr(out, expected_total) != NULL, "stats report the entry count");
    ASSERT(strstr(out, "builtin:") != NULL, "stats list the builtin category");

    compat_cleanup();
}

TEST(compat_debug_print_entry) {
    compat_init(NULL);

    compat_entry_t entry = {.id = "test_entry",
                            .category = COMPAT_CATEGORY_BUILTIN,
                            .feature = "test",
                            .description = "Test entry"};

    char out[2048];
    CAPTURE_STDERR(out, sizeof(out), compat_debug_print_entry(&entry));

    /// The dump reflects the entry's own fields.
    ASSERT(strstr(out, "Entry: test_entry") != NULL, "dump shows the id");
    ASSERT(strstr(out, "Feature: test") != NULL, "dump shows the feature");
    ASSERT(strstr(out, "Description: Test entry") != NULL,
           "dump shows the description");

    compat_cleanup();
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running compat.c tests...\n\n");

    printf("Database Initialization Tests:\n");
    RUN_TEST(compat_init_basic);
    RUN_TEST(compat_init_cleanup_cycle);
    RUN_TEST(compat_cleanup_without_init);
    RUN_TEST(compat_reload);

    printf("\nCategory Name Tests:\n");
    RUN_TEST(compat_category_name_builtin);
    RUN_TEST(compat_category_name_expansion);
    RUN_TEST(compat_category_name_quoting);
    RUN_TEST(compat_category_name_syntax);
    RUN_TEST(compat_category_parse_valid);
    RUN_TEST(compat_category_parse_invalid);

    printf("\nSeverity Name Tests:\n");
    RUN_TEST(compat_severity_name_info);
    RUN_TEST(compat_severity_name_warning);
    RUN_TEST(compat_severity_name_error);
    RUN_TEST(compat_severity_parse_valid);
    RUN_TEST(compat_severity_parse_error);
    RUN_TEST(compat_severity_parse_invalid);

    printf("\nFix Type Tests:\n");
    RUN_TEST(compat_fix_type_name_none);
    RUN_TEST(compat_fix_type_name_safe);
    RUN_TEST(compat_fix_type_name_unsafe);
    RUN_TEST(compat_fix_type_name_manual);
    RUN_TEST(compat_fix_type_parse_valid);
    RUN_TEST(compat_fix_type_parse_unsafe);
    RUN_TEST(compat_fix_type_parse_manual);
    RUN_TEST(compat_fix_type_parse_invalid);

    printf("\nTarget Shell Tests:\n");
    RUN_TEST(compat_set_get_target);
    RUN_TEST(compat_get_target_default);

    printf("\nStrict Mode Tests:\n");
    RUN_TEST(compat_set_strict);
    RUN_TEST(compat_is_strict_default);

    printf("\nEntry Query Tests:\n");
    RUN_TEST(compat_get_entry_count);
    RUN_TEST(compat_get_entry_nonexistent);
    RUN_TEST(compat_get_by_category);
    RUN_TEST(compat_get_by_feature);
    RUN_TEST(compat_get_first_by_feature);

    printf("\nForeach Entry Tests:\n");
    RUN_TEST(compat_foreach_entry);

    printf("\nPortability Checking Tests:\n");
    RUN_TEST(compat_is_portable_simple);
    RUN_TEST(compat_is_portable_null_result);
    RUN_TEST(compat_check_line);
    RUN_TEST(compat_check_script);

    printf("\nEffective Severity Tests:\n");
    RUN_TEST(compat_effective_severity_normal);
    RUN_TEST(compat_effective_severity_strict);

    printf("\nFix Type For Target Tests:\n");
    RUN_TEST(compat_get_fix_type_for_target);

    printf("\nFormat Result Tests:\n");
    RUN_TEST(compat_format_result);

    printf("\nDebug Functions Tests:\n");
    RUN_TEST(compat_debug_print_stats);
    RUN_TEST(compat_debug_print_entry);

    return TEST_RESULT();
}
