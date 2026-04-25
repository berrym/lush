/**
 * @file test_shell_mode.c
 * @brief Unit tests for shell mode system (Phase 0: Extended Language Support)
 *
 * Tests the multi-mode architecture including:
 * - Mode switching (POSIX, Bash, Zsh, Lush)
 * - Feature matrix queries
 * - Per-feature overrides
 * - Shebang detection
 * - Strict mode enforcement
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

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
        if (strcmp((actual), (expected)) != 0) {                               \
            printf("    FAILED: %s\n", message);                               \
            printf("      Expected: \"%s\", Got: \"%s\"\n", (expected),        \
                   (actual));                                                  \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(init_default_mode) {
    shell_mode_init();

    /* Default mode should be LUSH */
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH, "Default mode should be LUSH");

    /* Should not be in strict mode by default */
    ASSERT(!shell_mode_is_strict(),
           "Strict mode should be disabled by default");
}

TEST(mode_names) {
    ASSERT_STR_EQ(shell_mode_name(SHELL_MODE_POSIX), "posix",
                  "POSIX mode name incorrect");
    ASSERT_STR_EQ(shell_mode_name(SHELL_MODE_BASH), "bash",
                  "Bash mode name incorrect");
    ASSERT_STR_EQ(shell_mode_name(SHELL_MODE_ZSH), "zsh",
                  "Zsh mode name incorrect");
    ASSERT_STR_EQ(shell_mode_name(SHELL_MODE_LUSH), "lush",
                  "Lush mode name incorrect");
}

/* ============================================================================
 * MODE SWITCHING TESTS
 * ============================================================================
 */

TEST(mode_switching) {
    shell_mode_init();

    /* Switch to POSIX mode */
    ASSERT(shell_mode_set(SHELL_MODE_POSIX), "Failed to set POSIX mode");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_POSIX, "Mode should be POSIX");
    ASSERT(shell_mode_is(SHELL_MODE_POSIX),
           "shell_mode_is() should return true");

    /* Switch to Bash mode */
    ASSERT(shell_mode_set(SHELL_MODE_BASH), "Failed to set Bash mode");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_BASH, "Mode should be Bash");

    /* Switch to Zsh mode */
    ASSERT(shell_mode_set(SHELL_MODE_ZSH), "Failed to set Zsh mode");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_ZSH, "Mode should be Zsh");

    /* Switch back to Lush mode */
    ASSERT(shell_mode_set(SHELL_MODE_LUSH), "Failed to set Lush mode");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH, "Mode should be Lush");
}

TEST(strict_mode) {
    shell_mode_init();

    /* Enable strict mode */
    shell_mode_set_strict(true);
    ASSERT(shell_mode_is_strict(), "Strict mode should be enabled");

    /* Attempting to change mode should fail */
    ASSERT(!shell_mode_set(SHELL_MODE_BASH),
           "Mode change should fail in strict mode");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "Mode should remain LUSH after failed change");

    /* Disable strict mode */
    shell_mode_set_strict(false);
    ASSERT(!shell_mode_is_strict(), "Strict mode should be disabled");

    /* Mode change should now succeed */
    ASSERT(shell_mode_set(SHELL_MODE_BASH),
           "Mode change should succeed after disabling strict mode");

    /* Reset for other tests */
    shell_mode_init();
}

/* ============================================================================
 * FEATURE MATRIX TESTS
 * ============================================================================
 */

TEST(posix_mode_features) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_POSIX);

    /* POSIX mode should disable extended features */
    ASSERT(!shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "POSIX should not allow indexed arrays");
    ASSERT(!shell_mode_allows(FEATURE_ASSOCIATIVE_ARRAYS),
           "POSIX should not allow associative arrays");
    ASSERT(!shell_mode_allows(FEATURE_EXTENDED_TEST),
           "POSIX should not allow [[ ]]");
    ASSERT(!shell_mode_allows(FEATURE_REGEX_MATCH),
           "POSIX should not allow =~");
    ASSERT(!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION),
           "POSIX should not allow process substitution");
    ASSERT(!shell_mode_allows(FEATURE_EXTENDED_GLOB),
           "POSIX should not allow extended glob");
}

TEST(bash_mode_features) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_BASH);

    /* Bash mode should enable common features */
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Bash should allow indexed arrays");
    ASSERT(shell_mode_allows(FEATURE_ASSOCIATIVE_ARRAYS),
           "Bash should allow associative arrays");
    ASSERT(shell_mode_allows(FEATURE_EXTENDED_TEST), "Bash should allow [[ ]]");
    ASSERT(shell_mode_allows(FEATURE_REGEX_MATCH), "Bash should allow =~");
    ASSERT(shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION),
           "Bash should allow process substitution");

    /* Bash uses 0-indexed arrays */
    ASSERT(shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED),
           "Bash should use 0-indexed arrays");

    /* Bash has word splitting on by default */
    ASSERT(shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT),
           "Bash should have word splitting on by default");
}

TEST(zsh_mode_features) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_ZSH);

    /* Zsh mode should enable common features */
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Zsh should allow indexed arrays");
    ASSERT(shell_mode_allows(FEATURE_EXTENDED_TEST), "Zsh should allow [[ ]]");

    /* Zsh uses 1-indexed arrays (0-indexed is false) */
    ASSERT(!shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED),
           "Zsh should use 1-indexed arrays");

    /* Zsh has word splitting off by default */
    ASSERT(!shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT),
           "Zsh should have word splitting off by default");

    /* Zsh has anonymous functions */
    ASSERT(shell_mode_allows(FEATURE_ANONYMOUS_FUNCTIONS),
           "Zsh should allow anonymous functions");
}

TEST(lush_mode_features) {
    shell_mode_init();
    /* Lush is the default mode */

    /* Lush cherry-picks best features */
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Lush should allow indexed arrays");
    ASSERT(shell_mode_allows(FEATURE_EXTENDED_TEST), "Lush should allow [[ ]]");

    /* 0-indexed like Bash */
    ASSERT(shell_mode_allows(FEATURE_ARRAY_ZERO_INDEXED),
           "Lush should use 0-indexed arrays (like Bash)");

    /* Word splitting off like Zsh (safer) */
    ASSERT(!shell_mode_allows(FEATURE_WORD_SPLIT_DEFAULT),
           "Lush should have word splitting off (like Zsh)");

    /* Anonymous functions like Zsh */
    ASSERT(shell_mode_allows(FEATURE_ANONYMOUS_FUNCTIONS),
           "Lush should allow anonymous functions (like Zsh)");
}

/* ============================================================================
 * FEATURE OVERRIDE TESTS
 * ============================================================================
 */

TEST(feature_enable_override) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_POSIX);

    /* Verify feature is off in POSIX mode */
    ASSERT(!shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be off in POSIX mode");

    /* Enable the feature */
    shell_feature_enable(FEATURE_INDEXED_ARRAYS);

    /* Now it should be on */
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be on after override");
    ASSERT(shell_feature_is_overridden(FEATURE_INDEXED_ARRAYS),
           "Feature should be marked as overridden");

    /* Reset for other tests */
    shell_mode_init();
}

TEST(feature_disable_override) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_BASH);

    /* Verify feature is on in Bash mode */
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be on in Bash mode");

    /* Disable the feature */
    shell_feature_disable(FEATURE_INDEXED_ARRAYS);

    /* Now it should be off */
    ASSERT(!shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be off after override");

    /* Reset for other tests */
    shell_mode_init();
}

TEST(feature_reset) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_POSIX);

    /* Enable a feature */
    shell_feature_enable(FEATURE_INDEXED_ARRAYS);
    ASSERT(shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be on after override");

    /* Reset the feature */
    shell_feature_reset(FEATURE_INDEXED_ARRAYS);

    /* Should be back to mode default (off for POSIX) */
    ASSERT(!shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be off after reset to POSIX default");
    ASSERT(!shell_feature_is_overridden(FEATURE_INDEXED_ARRAYS),
           "Feature should no longer be overridden");

    /* Reset for other tests */
    shell_mode_init();
}

TEST(feature_reset_all) {
    shell_mode_init();
    shell_mode_set(SHELL_MODE_POSIX);

    /* Enable multiple features */
    shell_feature_enable(FEATURE_INDEXED_ARRAYS);
    shell_feature_enable(FEATURE_EXTENDED_TEST);
    shell_feature_enable(FEATURE_PROCESS_SUBSTITUTION);

    /* Reset all */
    shell_feature_reset_all();

    /* All should be back to POSIX defaults */
    ASSERT(!shell_mode_allows(FEATURE_INDEXED_ARRAYS),
           "Arrays should be off after reset_all");
    ASSERT(!shell_mode_allows(FEATURE_EXTENDED_TEST),
           "[[ ]] should be off after reset_all");
    ASSERT(!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION),
           "Process substitution should be off after reset_all");

    /* Reset for other tests */
    shell_mode_init();
}

/* ============================================================================
 * FEATURE NAME TESTS
 * ============================================================================
 */

TEST(feature_names) {
    /* Check some feature names are defined */
    const char *name = shell_feature_name(FEATURE_INDEXED_ARRAYS);
    ASSERT(name != NULL, "Feature name should not be NULL");
    ASSERT(strlen(name) > 0, "Feature name should not be empty");

    name = shell_feature_name(FEATURE_EXTENDED_TEST);
    ASSERT(name != NULL, "EXTENDED_TEST name should not be NULL");

    name = shell_feature_name(FEATURE_PROCESS_SUBSTITUTION);
    ASSERT(name != NULL, "PROCESS_SUBSTITUTION name should not be NULL");
}

TEST(feature_parse) {
    shell_feature_t feature;

    /* Parse valid feature names */
    ASSERT(shell_feature_parse("indexed_arrays", &feature),
           "Should parse 'indexed_arrays'");
    ASSERT_EQ(feature, FEATURE_INDEXED_ARRAYS, "Parsed feature should match");

    ASSERT(shell_feature_parse("extended_test", &feature),
           "Should parse 'extended_test'");
    ASSERT_EQ(feature, FEATURE_EXTENDED_TEST, "Parsed feature should match");

    /* Invalid feature name should fail */
    ASSERT(!shell_feature_parse("not_a_real_feature", &feature),
           "Should fail to parse invalid feature name");
}

/* ============================================================================
 * SHEBANG DETECTION TESTS
 * ============================================================================
 */

TEST(shebang_bash) {
    shell_mode_t mode;

    ASSERT(shell_mode_detect_from_shebang("#!/bin/bash", &mode),
           "Should detect bash shebang");
    ASSERT_EQ(mode, SHELL_MODE_BASH, "Mode should be BASH");

    ASSERT(shell_mode_detect_from_shebang("#!/usr/bin/env bash", &mode),
           "Should detect env bash shebang");
    ASSERT_EQ(mode, SHELL_MODE_BASH, "Mode should be BASH");

    ASSERT(shell_mode_detect_from_shebang("#!/usr/local/bin/bash", &mode),
           "Should detect alternate bash path");
    ASSERT_EQ(mode, SHELL_MODE_BASH, "Mode should be BASH");
}

TEST(shebang_zsh) {
    shell_mode_t mode;

    ASSERT(shell_mode_detect_from_shebang("#!/bin/zsh", &mode),
           "Should detect zsh shebang");
    ASSERT_EQ(mode, SHELL_MODE_ZSH, "Mode should be ZSH");

    ASSERT(shell_mode_detect_from_shebang("#!/usr/bin/env zsh", &mode),
           "Should detect env zsh shebang");
    ASSERT_EQ(mode, SHELL_MODE_ZSH, "Mode should be ZSH");
}

TEST(shebang_sh_posix) {
    shell_mode_t mode;

    ASSERT(shell_mode_detect_from_shebang("#!/bin/sh", &mode),
           "Should detect sh shebang");
    ASSERT_EQ(mode, SHELL_MODE_POSIX, "Mode should be POSIX");

    ASSERT(shell_mode_detect_from_shebang("#!/usr/bin/env sh", &mode),
           "Should detect env sh shebang");
    ASSERT_EQ(mode, SHELL_MODE_POSIX, "Mode should be POSIX");
}

TEST(shebang_lush) {
    shell_mode_t mode;

    ASSERT(shell_mode_detect_from_shebang("#!/usr/bin/lush", &mode),
           "Should detect lush shebang");
    ASSERT_EQ(mode, SHELL_MODE_LUSH, "Mode should be LUSH");

    ASSERT(shell_mode_detect_from_shebang("#!/usr/bin/env lush", &mode),
           "Should detect env lush shebang");
    ASSERT_EQ(mode, SHELL_MODE_LUSH, "Mode should be LUSH");
}

TEST(shebang_invalid) {
    shell_mode_t mode;

    /* Not a shebang */
    ASSERT(!shell_mode_detect_from_shebang("echo hello", &mode),
           "Should not detect non-shebang line");

    /* Unknown shell */
    ASSERT(!shell_mode_detect_from_shebang("#!/bin/fish", &mode),
           "Should not detect unknown shell");
}

/* ============================================================================
 * MODE BOUNDS TESTS
 * ============================================================================
 */

TEST(mode_bounds) {
    /* Verify enum bounds are correct */
    ASSERT(SHELL_MODE_POSIX >= 0, "POSIX mode should be non-negative");
    ASSERT(SHELL_MODE_LUSH < SHELL_MODE_COUNT, "LUSH should be within count");
    ASSERT_EQ(SHELL_MODE_COUNT, 4, "Should have 4 modes");

    /* Verify mode names work for all modes */
    for (int i = 0; i < SHELL_MODE_COUNT; i++) {
        const char *name = shell_mode_name((shell_mode_t)i);
        ASSERT(name != NULL, "Mode name should not be NULL");
        ASSERT(strlen(name) > 0, "Mode name should not be empty");
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("\n=== Shell Mode System Tests ===\n\n");

    printf("Initialization Tests:\n");
    RUN_TEST(init_default_mode);
    RUN_TEST(mode_names);

    printf("\nMode Switching Tests:\n");
    RUN_TEST(mode_switching);
    RUN_TEST(strict_mode);

    printf("\nFeature Matrix Tests:\n");
    RUN_TEST(posix_mode_features);
    RUN_TEST(bash_mode_features);
    RUN_TEST(zsh_mode_features);
    RUN_TEST(lush_mode_features);

    printf("\nFeature Override Tests:\n");
    RUN_TEST(feature_enable_override);
    RUN_TEST(feature_disable_override);
    RUN_TEST(feature_reset);
    RUN_TEST(feature_reset_all);

    printf("\nFeature Name Tests:\n");
    RUN_TEST(feature_names);
    RUN_TEST(feature_parse);

    printf("\nShebang Detection Tests:\n");
    RUN_TEST(shebang_bash);
    RUN_TEST(shebang_zsh);
    RUN_TEST(shebang_sh_posix);
    RUN_TEST(shebang_lush);
    RUN_TEST(shebang_invalid);

    printf("\nMode Bounds Tests:\n");
    RUN_TEST(mode_bounds);

    printf("\n=== All Shell Mode Tests PASSED ===\n\n");
    return 0;
}
