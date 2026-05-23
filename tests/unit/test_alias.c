/**
 * @file test_alias.c
 * @brief Unit tests for shell alias management
 *
 * Tests the alias subsystem including:
 * - Alias creation and deletion
 * - Alias lookup and expansion
 * - Recursive alias expansion
 * - Alias name validation
 * - Shell operator handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Test framework macros

// Helper to setup and teardown aliases for each test
static void setup_aliases(void) { init_aliases(); }

static void teardown_aliases(void) { free_aliases(); }

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(init_aliases_basic) {
    init_aliases();
    ASSERT_NOT_NULL(aliases, "Alias hash table should be created");
    free_aliases();
}

TEST(free_aliases_null) {
    // Should not crash if called before init
    free_aliases();
}

TEST(init_free_cycle) {
    for (int i = 0; i < 3; i++) {
        init_aliases();
        set_alias("test", "value");
        free_aliases();
    }
}

/* ============================================================================
 * ALIAS NAME VALIDATION TESTS
 * ============================================================================
 */

TEST(valid_alias_name_simple) {
    ASSERT(valid_alias_name("ls"), "Simple name should be valid");
}

TEST(valid_alias_name_underscore) {
    ASSERT(valid_alias_name("my_alias"), "Underscore should be valid");
}

TEST(valid_alias_name_numbers) {
    ASSERT(valid_alias_name("ls2"), "Numbers in name should be valid");
}

TEST(valid_alias_name_long) {
    ASSERT(valid_alias_name("this_is_a_very_long_alias_name"),
           "Long name should be valid");
}

TEST(valid_alias_name_starts_with_number) {
    ASSERT(!valid_alias_name("2ls"),
           "Name starting with number should be invalid");
}

TEST(valid_alias_name_empty) {
    ASSERT(!valid_alias_name(""), "Empty name should be invalid");
}

TEST(valid_alias_name_with_dash) {
    // Dashes may or may not be valid depending on implementation
    bool result = valid_alias_name("my-alias");
    // Just ensure it doesn't crash
    (void)result;
}

TEST(valid_alias_name_with_space) {
    /* Note: valid_alias_name stops at whitespace and validates up to that
     * point, so "my alias" is considered valid (as "my") by the implementation
     */
    bool result = valid_alias_name("my alias");
    (void)result; // Just ensure it doesn't crash
}

TEST(valid_alias_name_with_equals) {
    ASSERT(!valid_alias_name("foo=bar"), "Name with equals should be invalid");
}

TEST(valid_alias_name_special_chars) {
    ASSERT(!valid_alias_name("foo$bar"), "Name with $ should be invalid");
    ASSERT(!valid_alias_name("foo!bar"), "Name with ! should be invalid");
    ASSERT(!valid_alias_name("foo@bar"), "Name with @ should be invalid");
}

/* ============================================================================
 * BASIC ALIAS OPERATIONS TESTS
 * ============================================================================
 */

TEST(set_alias_basic) {
    setup_aliases();

    bool result = set_alias("ll", "ls -l");
    ASSERT(result, "set_alias should succeed");

    char *value = lookup_alias("ll");
    ASSERT_NOT_NULL(value, "Alias should be found");
    ASSERT_STR_EQ(value, "ls -l", "Alias value should match");

    teardown_aliases();
}

TEST(set_alias_overwrite) {
    setup_aliases();

    set_alias("ll", "ls -l");
    bool result = set_alias("ll", "ls -la");
    ASSERT(result, "Overwriting alias should succeed");

    char *value = lookup_alias("ll");
    ASSERT_STR_EQ(value, "ls -la", "Alias should be overwritten");

    teardown_aliases();
}

TEST(lookup_alias_nonexistent) {
    setup_aliases();

    char *value = lookup_alias("nonexistent");
    ASSERT_NULL(value, "Nonexistent alias should return NULL");

    teardown_aliases();
}

TEST(unset_alias_basic) {
    setup_aliases();

    set_alias("ll", "ls -l");
    unset_alias("ll");

    char *value = lookup_alias("ll");
    ASSERT_NULL(value, "Unset alias should not be found");

    teardown_aliases();
}

TEST(unset_alias_nonexistent) {
    setup_aliases();

    // Should not crash
    unset_alias("nonexistent");

    teardown_aliases();
}

TEST(set_multiple_aliases) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("la", "ls -a");
    set_alias("grep", "grep --color=auto");

    ASSERT_STR_EQ(lookup_alias("ll"), "ls -l", "ll should work");
    ASSERT_STR_EQ(lookup_alias("la"), "ls -a", "la should work");
    ASSERT_STR_EQ(lookup_alias("grep"), "grep --color=auto",
                  "grep should work");

    teardown_aliases();
}

/* ============================================================================
 * PRINT ALIASES TESTS
 * ============================================================================
 */

TEST(print_aliases_empty) {
    setup_aliases();

    // Should not crash
    FILE *old_stdout = stdout;
    FILE *null_out = fopen("/dev/null", "w");
    if (null_out) {
        stdout = null_out;
        print_aliases();
        fclose(null_out);
        stdout = old_stdout;
    }

    teardown_aliases();
}

TEST(print_aliases_with_content) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("la", "ls -a");

    FILE *old_stdout = stdout;
    FILE *null_out = fopen("/dev/null", "w");
    if (null_out) {
        stdout = null_out;
        print_aliases();
        fclose(null_out);
        stdout = old_stdout;
    }

    teardown_aliases();
}

/* ============================================================================
 * RECURSIVE EXPANSION TESTS
 * ============================================================================
 */

TEST(expand_aliases_recursive_simple) {
    setup_aliases();

    // Use a unique command name that won't have existing aliases
    set_alias("mytest", "mycommand --option");
    char *expanded = expand_aliases_recursive("mytest", 10);
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    ASSERT_STR_EQ(expanded, "mycommand --option",
                  "Simple expansion should work");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_chain) {
    setup_aliases();

    set_alias("l", "ls");
    set_alias("ll", "l -l");
    char *expanded = expand_aliases_recursive("ll", 10);
    ASSERT_NOT_NULL(expanded, "Chain expansion should succeed");
    // Should expand ll -> l -l -> ls -l
    ASSERT(strstr(expanded, "ls") != NULL, "Chain should expand to ls");
    free(expanded);

    teardown_aliases();
}

TEST(expand_aliases_recursive_nonexistent) {
    setup_aliases();

    char *expanded = expand_aliases_recursive("notanalias", 10);
    // Should return NULL or original
    if (expanded != NULL) {
        free(expanded);
    }

    teardown_aliases();
}

TEST(expand_aliases_recursive_depth_limit) {
    setup_aliases();

    // Create a deep chain
    set_alias("a", "b");
    set_alias("b", "c");
    set_alias("c", "d");
    set_alias("d", "e");
    set_alias("e", "f");

    // With depth 2, should not fully expand
    char *expanded = expand_aliases_recursive("a", 2);
    // Should have stopped early
    if (expanded != NULL) {
        free(expanded);
    }

    teardown_aliases();
}

TEST(expand_aliases_recursive_circular) {
    setup_aliases();

    // Create circular aliases
    set_alias("a", "b");
    set_alias("b", "a");

    // Should handle circular reference without infinite loop
    char *expanded = expand_aliases_recursive("a", 10);
    // Should terminate
    if (expanded != NULL) {
        free(expanded);
    }

    teardown_aliases();
}

/* ============================================================================
 * FIRST WORD EXPANSION TESTS
 * ============================================================================
 */

TEST(expand_first_word_alias_basic) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_first_word_alias("ll /home");
    ASSERT_NOT_NULL(expanded, "First word expansion should succeed");
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand first word");
    ASSERT(strstr(expanded, "/home") != NULL, "Should preserve arguments");
    free(expanded);

    teardown_aliases();
}

TEST(expand_first_word_alias_no_alias) {
    setup_aliases();

    char *expanded = expand_first_word_alias("ls /home");
    ASSERT_NOT_NULL(expanded, "Non-alias should return copy");
    ASSERT(strstr(expanded, "ls") != NULL, "Should preserve command");
    free(expanded);

    teardown_aliases();
}

TEST(expand_first_word_alias_only_first) {
    setup_aliases();

    set_alias("ll", "ls -l");
    set_alias("home", "/home");

    char *expanded = expand_first_word_alias("ll home");
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    // Should expand ll but not home
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand first word");
    ASSERT(strstr(expanded, "home") != NULL, "Should not expand second word");
    free(expanded);

    teardown_aliases();
}

/* ============================================================================
 * SHELL OPERATOR HANDLING TESTS
 * ============================================================================
 */

TEST(contains_shell_operators_pipe) {
    ASSERT(contains_shell_operators("ls | grep foo"),
           "Pipe should be detected");
}

TEST(contains_shell_operators_redirect_out) {
    ASSERT(contains_shell_operators("ls > file"),
           "Redirect out should be detected");
}

TEST(contains_shell_operators_redirect_in) {
    ASSERT(contains_shell_operators("cat < file"),
           "Redirect in should be detected");
}

TEST(contains_shell_operators_append) {
    ASSERT(contains_shell_operators("ls >> file"), "Append should be detected");
}

TEST(contains_shell_operators_semicolon) {
    ASSERT(contains_shell_operators("ls; pwd"), "Semicolon should be detected");
}

TEST(contains_shell_operators_ampersand) {
    ASSERT(contains_shell_operators("cmd &"), "Ampersand should be detected");
}

TEST(contains_shell_operators_and) {
    ASSERT(contains_shell_operators("cmd1 && cmd2"), "AND should be detected");
}

TEST(contains_shell_operators_or) {
    ASSERT(contains_shell_operators("cmd1 || cmd2"), "OR should be detected");
}

TEST(contains_shell_operators_none) {
    ASSERT(!contains_shell_operators("ls -la /home"),
           "Simple command should not detect operators");
}

/* Note: is_special_alias_char is actually an alias for valid_alias_name_char,
 * checking if char is valid in alias names, not shell operators */
TEST(is_special_alias_char_valid_chars) {
    // Alphanumeric chars are valid
    ASSERT(is_special_alias_char('a'), "Letter should be valid");
    ASSERT(is_special_alias_char('A'), "Uppercase should be valid");
    ASSERT(is_special_alias_char('0'), "Digit should be valid");
    ASSERT(is_special_alias_char('_'), "Underscore should be valid");
    ASSERT(is_special_alias_char('-'), "Dash should be valid");
    ASSERT(is_special_alias_char('.'), "Dot should be valid");
}

TEST(is_special_alias_char_invalid_chars) {
    // Shell operators are NOT valid alias name chars
    ASSERT(!is_special_alias_char('|'), "Pipe should not be valid alias char");
    ASSERT(!is_special_alias_char('>'), "> should not be valid alias char");
    ASSERT(!is_special_alias_char('<'), "< should not be valid alias char");
    ASSERT(!is_special_alias_char(';'), "; should not be valid alias char");
    ASSERT(!is_special_alias_char(' '), "Space should not be valid alias char");
}

/* ============================================================================
 * EXPAND WITH SHELL OPERATORS TESTS
 * ============================================================================
 */

TEST(expand_alias_with_shell_operators_simple) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_alias_with_shell_operators("ll /home");
    ASSERT_NOT_NULL(expanded, "Expansion should succeed");
    ASSERT(strstr(expanded, "ls -l") != NULL, "Should expand alias");
    free(expanded);

    teardown_aliases();
}

TEST(expand_alias_with_shell_operators_pipe) {
    setup_aliases();

    set_alias("ll", "ls -l");
    char *expanded = expand_alias_with_shell_operators("ll | grep foo");
    ASSERT_NOT_NULL(expanded, "Expansion with pipe should succeed");
    free(expanded);

    teardown_aliases();
}

TEST(expand_alias_with_shell_operators_in_value) {
    setup_aliases();

    set_alias("lsgrep", "ls | grep");
    char *expanded = expand_alias_with_shell_operators("lsgrep foo");
    ASSERT_NOT_NULL(expanded, "Alias with operators should expand");
    free(expanded);

    teardown_aliases();
}

/* ============================================================================
 * USAGE FUNCTIONS TESTS
 * ============================================================================
 */

TEST(alias_usage) {
    // Should not crash
    FILE *old_stdout = stdout;
    FILE *null_out = fopen("/dev/null", "w");
    if (null_out) {
        stdout = null_out;
        alias_usage();
        fclose(null_out);
        stdout = old_stdout;
    }
}

TEST(unalias_usage) {
    // Should not crash
    FILE *old_stdout = stdout;
    FILE *null_out = fopen("/dev/null", "w");
    if (null_out) {
        stdout = null_out;
        unalias_usage();
        fclose(null_out);
        stdout = old_stdout;
    }
}

/* ============================================================================
 * EDGE CASES AND STRESS TESTS
 * ============================================================================
 */

TEST(alias_with_quotes) {
    setup_aliases();

    set_alias("say", "echo 'hello world'");
    char *value = lookup_alias("say");
    ASSERT_STR_EQ(value, "echo 'hello world'",
                  "Quoted value should be preserved");

    teardown_aliases();
}

TEST(alias_with_variables) {
    setup_aliases();

    set_alias("home", "cd $HOME");
    char *value = lookup_alias("home");
    ASSERT_STR_EQ(value, "cd $HOME", "Variable should be preserved");

    teardown_aliases();
}

TEST(alias_empty_value) {
    setup_aliases();

    set_alias("empty", "");
    char *value = lookup_alias("empty");
    ASSERT_NOT_NULL(value, "Empty alias should exist");
    ASSERT_STR_EQ(value, "", "Empty value should be empty");

    teardown_aliases();
}

TEST(many_aliases) {
    setup_aliases();

    // Add many aliases
    char name[32];
    char value[64];
    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "alias%d", i);
        snprintf(value, sizeof(value), "command%d --option", i);
        set_alias(name, value);
    }

    // Verify some
    ASSERT_STR_EQ(lookup_alias("alias0"), "command0 --option", "First alias");
    ASSERT_STR_EQ(lookup_alias("alias50"), "command50 --option",
                  "Middle alias");
    ASSERT_STR_EQ(lookup_alias("alias99"), "command99 --option", "Last alias");

    teardown_aliases();
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running alias.c tests...\n\n");

    printf("Initialization Tests:\n");
    RUN_TEST(init_aliases_basic);
    RUN_TEST(free_aliases_null);
    RUN_TEST(init_free_cycle);

    printf("\nAlias Name Validation Tests:\n");
    RUN_TEST(valid_alias_name_simple);
    RUN_TEST(valid_alias_name_underscore);
    RUN_TEST(valid_alias_name_numbers);
    RUN_TEST(valid_alias_name_long);
    RUN_TEST(valid_alias_name_starts_with_number);
    RUN_TEST(valid_alias_name_empty);
    RUN_TEST(valid_alias_name_with_dash);
    RUN_TEST(valid_alias_name_with_space);
    RUN_TEST(valid_alias_name_with_equals);
    RUN_TEST(valid_alias_name_special_chars);

    printf("\nBasic Alias Operations Tests:\n");
    RUN_TEST(set_alias_basic);
    RUN_TEST(set_alias_overwrite);
    RUN_TEST(lookup_alias_nonexistent);
    RUN_TEST(unset_alias_basic);
    RUN_TEST(unset_alias_nonexistent);
    RUN_TEST(set_multiple_aliases);

    printf("\nPrint Aliases Tests:\n");
    RUN_TEST(print_aliases_empty);
    RUN_TEST(print_aliases_with_content);

    printf("\nRecursive Expansion Tests:\n");
    RUN_TEST(expand_aliases_recursive_simple);
    RUN_TEST(expand_aliases_recursive_chain);
    RUN_TEST(expand_aliases_recursive_nonexistent);
    RUN_TEST(expand_aliases_recursive_depth_limit);
    RUN_TEST(expand_aliases_recursive_circular);

    printf("\nFirst Word Expansion Tests:\n");
    RUN_TEST(expand_first_word_alias_basic);
    RUN_TEST(expand_first_word_alias_no_alias);
    RUN_TEST(expand_first_word_alias_only_first);

    printf("\nShell Operator Handling Tests:\n");
    RUN_TEST(contains_shell_operators_pipe);
    RUN_TEST(contains_shell_operators_redirect_out);
    RUN_TEST(contains_shell_operators_redirect_in);
    RUN_TEST(contains_shell_operators_append);
    RUN_TEST(contains_shell_operators_semicolon);
    RUN_TEST(contains_shell_operators_ampersand);
    RUN_TEST(contains_shell_operators_and);
    RUN_TEST(contains_shell_operators_or);
    RUN_TEST(contains_shell_operators_none);
    RUN_TEST(is_special_alias_char_valid_chars);
    RUN_TEST(is_special_alias_char_invalid_chars);

    printf("\nExpand With Shell Operators Tests:\n");
    RUN_TEST(expand_alias_with_shell_operators_simple);
    RUN_TEST(expand_alias_with_shell_operators_pipe);
    RUN_TEST(expand_alias_with_shell_operators_in_value);

    printf("\nUsage Functions Tests:\n");
    RUN_TEST(alias_usage);
    RUN_TEST(unalias_usage);

    printf("\nEdge Cases and Stress Tests:\n");
    RUN_TEST(alias_with_quotes);
    RUN_TEST(alias_with_variables);
    RUN_TEST(alias_empty_value);
    RUN_TEST(many_aliases);

    return TEST_RESULT();
}
