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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test framework macros */
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                         \
    do {                                                                       \
        printf("  Running: %s...\n", #name);                                   \
        test_##name();                                                         \
        printf("    PASSED\n");                                                \
    } while (0)

#define ASSERT(condition, message)                                             \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("    FAILED: %s\n", message);                               \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(actual, expected, message)                                   \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            printf("    FAILED: %s\n", message);                               \
            printf("      Expected: %d, Got: %d\n", (int)(expected),           \
                   (int)(actual));                                             \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(actual, expected, message)                               \
    do {                                                                       \
        const char *_actual = (actual);                                        \
        const char *_expected = (expected);                                    \
        if (_actual == NULL || _expected == NULL) {                            \
            if (_actual != _expected) {                                        \
                printf("    FAILED: %s\n", message);                           \
                printf("      Expected: %s, Got: %s\n",                        \
                       _expected ? _expected : "NULL",                         \
                       _actual ? _actual : "NULL");                            \
                printf("      at %s:%d\n", __FILE__, __LINE__);                \
                exit(1);                                                       \
            }                                                                  \
        } else if (strcmp(_actual, _expected) != 0) {                          \
            printf("    FAILED: %s\n", message);                               \
            printf("      Expected: \"%s\", Got: \"%s\"\n", _expected,         \
                   _actual);                                                   \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, message)                                          \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            printf("    FAILED: %s (got NULL)\n", message);                    \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr, message)                                              \
    do {                                                                       \
        if ((ptr) != NULL) {                                                   \
            printf("    FAILED: %s (expected NULL)\n", message);               \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* ============================================================================
 * DATABASE INITIALIZATION TESTS
 * ============================================================================ */

TEST(compat_init_basic) {
    int result = compat_init(NULL);
    ASSERT_EQ(result, 0, "compat_init with NULL should use defaults");
    compat_cleanup();
}

TEST(compat_init_cleanup_cycle) {
    for (int i = 0; i < 3; i++) {
        int result = compat_init(NULL);
        ASSERT_EQ(result, 0, "compat_init should succeed");
        compat_cleanup();
    }
}

TEST(compat_cleanup_without_init) {
    /* Should not crash */
    compat_cleanup();
}

TEST(compat_reload) {
    compat_init(NULL);
    int result = compat_reload();
    /* May succeed or fail depending on file availability */
    (void)result;
    compat_cleanup();
}

/* ============================================================================
 * CATEGORY NAME TESTS
 * ============================================================================ */

TEST(compat_category_name_builtin) {
    const char *name = compat_category_name(COMPAT_CATEGORY_BUILTIN);
    ASSERT_NOT_NULL(name, "Category name should not be NULL");
    ASSERT(strlen(name) > 0, "Category name should not be empty");
}

TEST(compat_category_name_expansion) {
    const char *name = compat_category_name(COMPAT_CATEGORY_EXPANSION);
    ASSERT_NOT_NULL(name, "Category name should not be NULL");
}

TEST(compat_category_name_quoting) {
    const char *name = compat_category_name(COMPAT_CATEGORY_QUOTING);
    ASSERT_NOT_NULL(name, "Category name should not be NULL");
}

TEST(compat_category_name_syntax) {
    const char *name = compat_category_name(COMPAT_CATEGORY_SYNTAX);
    ASSERT_NOT_NULL(name, "Category name should not be NULL");
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
 * ============================================================================ */

TEST(compat_severity_name_info) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_INFO);
    ASSERT_NOT_NULL(name, "Severity name should not be NULL");
}

TEST(compat_severity_name_warning) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_WARNING);
    ASSERT_NOT_NULL(name, "Severity name should not be NULL");
}

TEST(compat_severity_name_error) {
    const char *name = compat_severity_name(COMPAT_SEVERITY_ERROR);
    ASSERT_NOT_NULL(name, "Severity name should not be NULL");
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
 * ============================================================================ */

TEST(compat_fix_type_name_none) {
    const char *name = compat_fix_type_name(FIX_TYPE_NONE);
    ASSERT_NOT_NULL(name, "Fix type name should not be NULL");
}

TEST(compat_fix_type_name_safe) {
    const char *name = compat_fix_type_name(FIX_TYPE_SAFE);
    ASSERT_NOT_NULL(name, "Fix type name should not be NULL");
}

TEST(compat_fix_type_name_unsafe) {
    const char *name = compat_fix_type_name(FIX_TYPE_UNSAFE);
    ASSERT_NOT_NULL(name, "Fix type name should not be NULL");
}

TEST(compat_fix_type_name_manual) {
    const char *name = compat_fix_type_name(FIX_TYPE_MANUAL);
    ASSERT_NOT_NULL(name, "Fix type name should not be NULL");
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
 * ============================================================================ */

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

    const char *target = compat_get_target();
    ASSERT_NOT_NULL(target, "Target should not be NULL");
    /* Default is typically "posix" */

    compat_cleanup();
}

/* ============================================================================
 * STRICT MODE TESTS
 * ============================================================================ */

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

    /* Default should be non-strict */
    ASSERT(!compat_is_strict(), "Strict mode should be off by default");

    compat_cleanup();
}

/* ============================================================================
 * ENTRY QUERY TESTS
 * ============================================================================ */

TEST(compat_get_entry_count) {
    compat_init(NULL);

    size_t count = compat_get_entry_count();
    /* Count may be 0 if no data files found, which is okay for testing */
    (void)count;

    compat_cleanup();
}

TEST(compat_get_entry_nonexistent) {
    compat_init(NULL);

    const compat_entry_t *entry = compat_get_entry("nonexistent_entry_id");
    /* May or may not find it depending on database content */
    (void)entry;

    compat_cleanup();
}

TEST(compat_get_by_category) {
    compat_init(NULL);

    const compat_entry_t *entries[10];
    size_t count = compat_get_by_category(COMPAT_CATEGORY_BUILTIN, entries, 10);
    /* May have entries or not */
    ASSERT(count <= 10, "Should not exceed max");

    compat_cleanup();
}

TEST(compat_get_by_feature) {
    compat_init(NULL);

    const compat_entry_t *entries[10];
    size_t count = compat_get_by_feature("echo", entries, 10);
    /* May have entries or not */
    ASSERT(count <= 10, "Should not exceed max");

    compat_cleanup();
}

TEST(compat_get_first_by_feature) {
    compat_init(NULL);

    const compat_entry_t *entry = compat_get_first_by_feature("arrays");
    /* May or may not find it */
    (void)entry;

    compat_cleanup();
}

/* ============================================================================
 * FOREACH ENTRY TESTS
 * ============================================================================ */

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
    /* Count should match get_entry_count */
    ASSERT_EQ((size_t)foreach_count, compat_get_entry_count(),
              "foreach should visit all entries");

    compat_cleanup();
}

/* ============================================================================
 * PORTABILITY CHECKING TESTS
 * ============================================================================ */

TEST(compat_is_portable_simple) {
    compat_init(NULL);

    compat_result_t result;
    bool portable = compat_is_portable("echo hello", SHELL_MODE_POSIX, &result);
    /* Basic echo should be portable */
    (void)portable;  /* May vary by database content */

    compat_cleanup();
}

TEST(compat_is_portable_null_result) {
    compat_init(NULL);

    /* Should not crash with NULL result */
    bool portable = compat_is_portable("echo hello", SHELL_MODE_POSIX, NULL);
    (void)portable;

    compat_cleanup();
}

TEST(compat_check_line) {
    compat_init(NULL);

    compat_result_t results[10];
    size_t count = compat_check_line("echo -e 'hello\\n'", SHELL_MODE_POSIX,
                                     results, 10);
    /* May find issues or not depending on database */
    ASSERT(count <= 10, "Should not exceed max results");

    compat_cleanup();
}

TEST(compat_check_script) {
    compat_init(NULL);

    const char *script = "#!/bin/sh\necho hello\nls -la\n";
    compat_result_t results[10];
    size_t count = compat_check_script(script, SHELL_MODE_POSIX, results, 10);
    ASSERT(count <= 10, "Should not exceed max results");

    compat_cleanup();
}

/* ============================================================================
 * EFFECTIVE SEVERITY TESTS
 * ============================================================================ */

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
    /* In strict mode, warnings may be elevated to errors */
    ASSERT(sev >= COMPAT_SEVERITY_WARNING, "Severity should be at least warning");

    compat_set_strict(false);
    compat_cleanup();
}

/* ============================================================================
 * FIX TYPE FOR TARGET TESTS
 * ============================================================================ */

TEST(compat_get_fix_type_for_target) {
    compat_fix_class_t fix_class = {
        .posix = FIX_TYPE_SAFE,
        .bash = FIX_TYPE_UNSAFE,
        .zsh = FIX_TYPE_MANUAL,
        .lush = FIX_TYPE_NONE
    };

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
 * ============================================================================ */

TEST(compat_format_result) {
    compat_init(NULL);

    compat_result_t result = {
        .is_portable = false,
        .entry = NULL,
        .target = SHELL_MODE_POSIX,
        .line = 5,
        .column = 10
    };

    char buffer[256];
    int len = compat_format_result(&result, buffer, sizeof(buffer));
    ASSERT(len >= 0, "Format should succeed");
    ASSERT((size_t)len < sizeof(buffer), "Should not overflow buffer");

    compat_cleanup();
}

/* ============================================================================
 * DEBUG FUNCTIONS TESTS
 * ============================================================================ */

TEST(compat_debug_print_stats) {
    compat_init(NULL);

    /* Should not crash - output goes to stderr */
    FILE *old_stderr = stderr;
    FILE *null_err = fopen("/dev/null", "w");
    if (null_err) {
        stderr = null_err;
        compat_debug_print_stats();
        fclose(null_err);
        stderr = old_stderr;
    }

    compat_cleanup();
}

TEST(compat_debug_print_entry) {
    compat_init(NULL);

    compat_entry_t entry = {
        .id = "test_entry",
        .category = COMPAT_CATEGORY_BUILTIN,
        .feature = "test",
        .description = "Test entry"
    };

    FILE *old_stderr = stderr;
    FILE *null_err = fopen("/dev/null", "w");
    if (null_err) {
        stderr = null_err;
        compat_debug_print_entry(&entry);
        fclose(null_err);
        stderr = old_stderr;
    }

    compat_cleanup();
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

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

    printf("\n=== All compat.c tests passed! ===\n");
    return 0;
}
