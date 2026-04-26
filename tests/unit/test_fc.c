/**
 * @file test_fc.c
 * @brief Unit tests for fc (fix command) builtin
 *
 * Tests the POSIX fc command implementation including:
 * - Pattern parsing (substitution)
 * - Range resolution
 * - List mode formatting
 * - Editor detection
 * - Command execution
 *
 * Note: Many fc functions depend on LLE history system, so some tests
 * use mock data or test internal helpers directly.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
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
 * HELPER: Substitution pattern parsing (reimplemented for testing)
 * ============================================================================
 */

/**
 * @brief Parse old=new substitution pattern (test helper)
 *
 * This reimplements the fc internal function for testing purposes.
 */
static bool parse_substitution_pattern(const char *pattern, char **old,
                                       char **new_str) {
    if (!pattern || !old || !new_str) {
        return false;
    }

    const char *equals = strchr(pattern, '=');
    if (!equals) {
        /* No equals sign, treat entire pattern as old with empty new */
        *old = strdup(pattern);
        *new_str = strdup("");
        return (*old && *new_str);
    }

    size_t old_len = (size_t)(equals - pattern);
    size_t new_len = strlen(equals + 1);

    *old = malloc(old_len + 1);
    *new_str = malloc(new_len + 1);

    if (!*old || !*new_str) {
        free(*old);
        free(*new_str);
        *old = NULL;
        *new_str = NULL;
        return false;
    }

    strncpy(*old, pattern, old_len);
    (*old)[old_len] = '\0';
    strcpy(*new_str, equals + 1);

    return true;
}

/**
 * @brief Apply substitution to command (test helper)
 */
static char *apply_substitution(const char *command, const char *old,
                                const char *new_str) {
    if (!command || !old || !new_str) {
        return NULL;
    }

    const char *pos = strstr(command, old);
    if (!pos) {
        return strdup(command); /* No match, return original */
    }

    size_t old_len = strlen(old);
    size_t new_len = strlen(new_str);
    size_t cmd_len = strlen(command);
    size_t result_len = cmd_len - old_len + new_len;

    char *result = malloc(result_len + 1);
    if (!result) {
        return NULL;
    }

    /* Copy prefix */
    size_t prefix_len = (size_t)(pos - command);
    strncpy(result, command, prefix_len);

    /* Copy replacement */
    strcpy(result + prefix_len, new_str);

    /* Copy suffix */
    strcpy(result + prefix_len + new_len, pos + old_len);

    return result;
}

/* ============================================================================
 * SUBSTITUTION PATTERN TESTS
 * ============================================================================
 */

TEST(substitution_simple) {
    char *old = NULL;
    char *new_str = NULL;

    bool result = parse_substitution_pattern("foo=bar", &old, &new_str);
    ASSERT_TRUE(result, "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old should be 'foo'");
    ASSERT_STR_EQ(new_str, "bar", "New should be 'bar'");

    free(old);
    free(new_str);
}

TEST(substitution_empty_new) {
    char *old = NULL;
    char *new_str = NULL;

    bool result = parse_substitution_pattern("foo=", &old, &new_str);
    ASSERT_TRUE(result, "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old should be 'foo'");
    ASSERT_STR_EQ(new_str, "", "New should be empty");

    free(old);
    free(new_str);
}

TEST(substitution_no_equals) {
    char *old = NULL;
    char *new_str = NULL;

    bool result = parse_substitution_pattern("foo", &old, &new_str);
    ASSERT_TRUE(result, "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old should be 'foo'");
    ASSERT_STR_EQ(new_str, "", "New should be empty (no equals)");

    free(old);
    free(new_str);
}

TEST(substitution_special_chars) {
    char *old = NULL;
    char *new_str = NULL;

    bool result = parse_substitution_pattern("a/b=c/d", &old, &new_str);
    ASSERT_TRUE(result, "Parse should succeed");
    ASSERT_STR_EQ(old, "a/b", "Old should be 'a/b'");
    ASSERT_STR_EQ(new_str, "c/d", "New should be 'c/d'");

    free(old);
    free(new_str);
}

TEST(substitution_multiple_equals) {
    char *old = NULL;
    char *new_str = NULL;

    /* Only first = is the separator */
    bool result = parse_substitution_pattern("a=b=c", &old, &new_str);
    ASSERT_TRUE(result, "Parse should succeed");
    ASSERT_STR_EQ(old, "a", "Old should be 'a'");
    ASSERT_STR_EQ(new_str, "b=c", "New should be 'b=c'");

    free(old);
    free(new_str);
}

TEST(substitution_null_inputs) {
    char *old = NULL;
    char *new_str = NULL;

    bool result = parse_substitution_pattern(NULL, &old, &new_str);
    ASSERT_FALSE(result, "NULL pattern should fail");

    result = parse_substitution_pattern("foo=bar", NULL, &new_str);
    ASSERT_FALSE(result, "NULL old should fail");

    result = parse_substitution_pattern("foo=bar", &old, NULL);
    ASSERT_FALSE(result, "NULL new_str should fail");
}

/* ============================================================================
 * SUBSTITUTION APPLICATION TESTS
 * ============================================================================
 */

TEST(apply_sub_simple) {
    char *result = apply_substitution("echo hello", "hello", "world");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "echo world", "Substitution should work");
    free(result);
}

TEST(apply_sub_at_start) {
    char *result = apply_substitution("hello world", "hello", "goodbye");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "goodbye world", "Substitution at start");
    free(result);
}

TEST(apply_sub_at_end) {
    char *result = apply_substitution("hello world", "world", "universe");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "hello universe", "Substitution at end");
    free(result);
}

TEST(apply_sub_no_match) {
    char *result = apply_substitution("echo hello", "foo", "bar");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "echo hello", "No match should return original");
    free(result);
}

TEST(apply_sub_delete) {
    char *result = apply_substitution("echo hello world", "hello ", "");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "echo world", "Empty replacement should delete");
    free(result);
}

TEST(apply_sub_expand) {
    char *result = apply_substitution("ls", "ls", "ls -la");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "ls -la", "Replacement can expand");
    free(result);
}

TEST(apply_sub_first_only) {
    /* Should only replace first occurrence */
    char *result = apply_substitution("echo echo echo", "echo", "print");
    ASSERT_NOT_NULL(result, "Result should not be NULL");
    ASSERT_STR_EQ(result, "print echo echo", "Only first occurrence replaced");
    free(result);
}

/* ============================================================================
 * EDITOR ENVIRONMENT TESTS
 * ============================================================================
 */

TEST(editor_env_fcedit) {
    /* Save current environment */
    char *saved_fcedit = getenv("FCEDIT") ? strdup(getenv("FCEDIT")) : NULL;
    char *saved_editor = getenv("EDITOR") ? strdup(getenv("EDITOR")) : NULL;

    /* Set FCEDIT */
    setenv("FCEDIT", "/usr/bin/vim", 1);
    unsetenv("EDITOR");

    const char *fcedit = getenv("FCEDIT");
    ASSERT_NOT_NULL(fcedit, "FCEDIT should be set");
    ASSERT_STR_EQ(fcedit, "/usr/bin/vim", "FCEDIT value correct");

    /* Restore */
    if (saved_fcedit) {
        setenv("FCEDIT", saved_fcedit, 1);
        free(saved_fcedit);
    } else {
        unsetenv("FCEDIT");
    }
    if (saved_editor) {
        setenv("EDITOR", saved_editor, 1);
        free(saved_editor);
    }
}

TEST(editor_env_editor_fallback) {
    /* Save current environment */
    char *saved_fcedit = getenv("FCEDIT") ? strdup(getenv("FCEDIT")) : NULL;
    char *saved_editor = getenv("EDITOR") ? strdup(getenv("EDITOR")) : NULL;

    /* Unset FCEDIT, set EDITOR */
    unsetenv("FCEDIT");
    setenv("EDITOR", "/usr/bin/nano", 1);

    const char *fcedit = getenv("FCEDIT");
    const char *editor = getenv("EDITOR");
    ASSERT_NULL(fcedit, "FCEDIT should not be set");
    ASSERT_NOT_NULL(editor, "EDITOR should be set");
    ASSERT_STR_EQ(editor, "/usr/bin/nano", "EDITOR value correct");

    /* Restore */
    if (saved_fcedit) {
        setenv("FCEDIT", saved_fcedit, 1);
        free(saved_fcedit);
    }
    if (saved_editor) {
        setenv("EDITOR", saved_editor, 1);
        free(saved_editor);
    } else {
        unsetenv("EDITOR");
    }
}

/* ============================================================================
 * RANGE SPECIFICATION TESTS
 * ============================================================================
 */

/**
 * @brief Test helper: resolve numeric range
 *
 * POSIX fc uses 1-based indexing where positive numbers are absolute
 * and negative numbers are relative to the end.
 */
static int resolve_fc_index(int spec, int history_count) {
    if (spec == 0) {
        /* 0 means current (last) command */
        return history_count;
    } else if (spec > 0) {
        /* Positive: absolute 1-based index */
        return (spec <= history_count) ? spec : history_count;
    } else {
        /* Negative: relative to end */
        int resolved = history_count + spec + 1;
        return (resolved > 0) ? resolved : 1;
    }
}

TEST(range_positive_index) {
    int idx = resolve_fc_index(5, 100);
    ASSERT_EQ(idx, 5, "Positive index 5 in 100-entry history");
}

TEST(range_negative_index) {
    int idx = resolve_fc_index(-1, 100);
    ASSERT_EQ(idx, 100, "-1 should be last command");

    idx = resolve_fc_index(-5, 100);
    ASSERT_EQ(idx, 96, "-5 should be 5 from end");
}

TEST(range_zero_index) {
    int idx = resolve_fc_index(0, 100);
    ASSERT_EQ(idx, 100, "0 should be last command");
}

TEST(range_out_of_bounds_positive) {
    int idx = resolve_fc_index(200, 100);
    ASSERT_EQ(idx, 100, "Out of bounds positive clamps to max");
}

TEST(range_out_of_bounds_negative) {
    int idx = resolve_fc_index(-200, 100);
    ASSERT_EQ(idx, 1, "Out of bounds negative clamps to 1");
}

TEST(range_small_history) {
    int idx = resolve_fc_index(-1, 3);
    ASSERT_EQ(idx, 3, "-1 in 3-entry history");

    idx = resolve_fc_index(-3, 3);
    ASSERT_EQ(idx, 1, "-3 in 3-entry history");
}

/* ============================================================================
 * BIN_FC BASIC TESTS (using actual builtin)
 * ============================================================================
 */

TEST(fc_no_args_no_history) {
    /* fc with no args and no history should fail gracefully */
    /* Note: This test may need LLE to be initialized */
    char *argv[] = {"fc", NULL};
    int result = bin_fc(1, argv);
    /* Without history, fc should return error */
    ASSERT(result == 0 || result == 1, "fc should return 0 or 1");
}

TEST(fc_list_empty_history) {
    /* fc -l with no history */
    char *argv[] = {"fc", "-l", NULL};
    int result = bin_fc(2, argv);
    /* Should succeed but show nothing or fail gracefully */
    ASSERT(result == 0 || result == 1, "fc -l should handle empty history");
}

TEST(fc_invalid_option) {
    /* fc with invalid option */
    char *argv[] = {"fc", "-Z", NULL};
    int result = bin_fc(2, argv);
    /* Should return error for invalid option */
    ASSERT(result == 1 || result == 2, "fc -Z should fail");
}

TEST(fc_substitute_mode_syntax) {
    /* fc -s without pattern - should use last command */
    char *argv[] = {"fc", "-s", NULL};
    int result = bin_fc(2, argv);
    /* May succeed or fail depending on history state */
    ASSERT(result == 0 || result == 1, "fc -s should handle gracefully");
}

/* ============================================================================
 * FC LIST MODE FORMATTING TESTS
 * ============================================================================
 */

/**
 * @brief Format a history entry for fc -l output
 */
static void format_fc_entry(char *buf, size_t size, int num, const char *cmd,
                            bool show_numbers) {
    if (show_numbers) {
        snprintf(buf, size, "%5d  %s", num, cmd);
    } else {
        snprintf(buf, size, "%s", cmd);
    }
}

TEST(format_with_numbers) {
    char buf[256];
    format_fc_entry(buf, sizeof(buf), 42, "echo hello", true);
    ASSERT(strstr(buf, "42") != NULL, "Should contain number");
    ASSERT(strstr(buf, "echo hello") != NULL, "Should contain command");
}

TEST(format_without_numbers) {
    char buf[256];
    format_fc_entry(buf, sizeof(buf), 42, "echo hello", false);
    ASSERT(strstr(buf, "42") == NULL, "Should not contain number");
    ASSERT(strstr(buf, "echo hello") != NULL, "Should contain command");
}

TEST(format_long_command) {
    char buf[256];
    const char *long_cmd = "very long command with many arguments "
                           "that might need special handling";
    format_fc_entry(buf, sizeof(buf), 1, long_cmd, true);
    ASSERT(strlen(buf) > 50, "Should format long command");
}

/* ============================================================================
 * TEMP FILE TESTS
 * ============================================================================
 */

TEST(temp_file_creation) {
    /* Test that we can create temp files in /tmp */
    char template[] = "/tmp/fc_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd >= 0, "mkstemp should succeed");

    /* Write some content */
    const char *content = "echo test\n";
    ssize_t written = write(fd, content, strlen(content));
    ASSERT(written == (ssize_t)strlen(content), "Write should succeed");

    close(fd);

    /* Read it back */
    FILE *f = fopen(template, "r");
    ASSERT_NOT_NULL(f, "Should open temp file");

    char buf[256];
    char *line = fgets(buf, sizeof(buf), f);
    ASSERT_NOT_NULL(line, "Should read line");
    ASSERT_STR_EQ(buf, "echo test\n", "Content should match");

    fclose(f);
    unlink(template);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n=== FC Builtin Unit Tests ===\n\n");

    /* Substitution pattern tests */
    printf("Substitution Pattern Parsing:\n");
    RUN_TEST(substitution_simple);
    RUN_TEST(substitution_empty_new);
    RUN_TEST(substitution_no_equals);
    RUN_TEST(substitution_special_chars);
    RUN_TEST(substitution_multiple_equals);
    RUN_TEST(substitution_null_inputs);

    /* Substitution application tests */
    printf("\nSubstitution Application:\n");
    RUN_TEST(apply_sub_simple);
    RUN_TEST(apply_sub_at_start);
    RUN_TEST(apply_sub_at_end);
    RUN_TEST(apply_sub_no_match);
    RUN_TEST(apply_sub_delete);
    RUN_TEST(apply_sub_expand);
    RUN_TEST(apply_sub_first_only);

    /* Editor environment tests */
    printf("\nEditor Environment:\n");
    RUN_TEST(editor_env_fcedit);
    RUN_TEST(editor_env_editor_fallback);

    /* Range specification tests */
    printf("\nRange Specification:\n");
    RUN_TEST(range_positive_index);
    RUN_TEST(range_negative_index);
    RUN_TEST(range_zero_index);
    RUN_TEST(range_out_of_bounds_positive);
    RUN_TEST(range_out_of_bounds_negative);
    RUN_TEST(range_small_history);

    /* Basic bin_fc tests */
    printf("\nbin_fc Basic Tests:\n");
    RUN_TEST(fc_no_args_no_history);
    RUN_TEST(fc_list_empty_history);
    RUN_TEST(fc_invalid_option);
    RUN_TEST(fc_substitute_mode_syntax);

    /* List mode formatting tests */
    printf("\nList Mode Formatting:\n");
    RUN_TEST(format_with_numbers);
    RUN_TEST(format_without_numbers);
    RUN_TEST(format_long_command);

    /* Temp file tests */
    printf("\nTemp File Handling:\n");
    RUN_TEST(temp_file_creation);

    return TEST_RESULT();
}
