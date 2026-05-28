/**
 * @file test_shell_quoting.c
 * @brief Unit tests for shell quoting
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * Lush Shell - Quoting and Escape Functional Tests
 * Copyright (C) 2021-2026 Michael Berry
 *
 * Licensed under the MIT License. See LICENSE file for details.
 *
 * End-to-end coverage for shell quoting and escape behavior. Each test
 * runs a script string through the in-process executor (or via a
 * forked lush -c subprocess for cases that exercise process state)
 * and asserts on the captured stdout. The tokenizer's per-character
 * unit tests in test_tokenizer.c verify the lexical layer in
 * isolation; the cases here verify the user-visible shell behavior
 * the way someone running an interactive lush would experience it.
 *
 * Coverage targets:
 *   - Backslash escapes outside quotes (issue #90)
 *   - Double-quoted strings with embedded spaces and metacharacters
 *   - Single-quoted strings (no expansion, no escape)
 *   - Backslash + newline line continuation
 *   - End-to-end filename-with-space: create via touch, remove via rm
 */

#include "symtable.h"
#include "test_framework.h"
#include "test_shell_harness.h"

#include <stdio.h>

/* ============================================================================
 * Backslash escapes outside any quote (issue #90)
 * ============================================================================
 */

TEST(echo_backslash_space_produces_literal_space) {
    /// `echo a\ b` -> "a b\n". Before the fix the backslash leaked into
    /// the argument and echo printed "a\ b".
    run_result_t r = run_shell("echo a\\ b");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a b\n");
}

TEST(echo_backslash_dollar_suppresses_variable_expansion) {
    /// `echo \$foo` -> "$foo\n" -- the escape stops $foo from being
    /// expanded by the parser/executor.
    run_result_t r = run_shell("echo \\$foo");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "$foo\n");
}

TEST(printf_backslash_backslash_produces_single_backslash) {
    /// `printf '%s\n' a\\b` -- the first backslash escapes the second, so
    /// the argument is a literal "a\b". printf is used (not echo) because
    /// echo's XPG_ECHO default would interpret the `\b` as backspace and
    /// mask the tokenizer behavior under test here.
    run_result_t r = run_shell("printf '%s\\n' a\\\\b");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a\\b\n");
}

TEST(echo_escaped_keyword_treated_as_word) {
    /// `\if` is a literal "if" word, NOT the IF reserved word. The
    /// parser should treat it as a command name; lush has no command
    /// called "if" so this either runs as external-not-found or
    /// (since the parser may interpret it differently) at minimum
    /// does NOT enter an if-block.
    run_result_t r = run_shell("echo \\if");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "if\n");
}

/* ============================================================================
 * Double-quoted strings
 * ============================================================================
 */

TEST(echo_double_quoted_string_with_space) {
    /// The canonical "filename with spaces" idiom must not split.
    run_result_t r = run_shell("echo \"a test file.txt\"");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a test file.txt\n");
}

TEST(echo_double_quoted_string_expands_variables) {
    run_result_t r = run_shell("X=foo; echo \"value=$X\"");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "value=foo\n");
}

TEST(echo_double_quoted_backslash_escapes_dollar) {
    /// Inside "..." the backslash escapes only $, `, \, ".
    run_result_t r = run_shell("X=foo; echo \"\\$X is $X\"");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "$X is foo\n");
}

/* ============================================================================
 * Single-quoted strings
 * ============================================================================
 */

TEST(echo_single_quoted_string_with_space) {
    run_result_t r = run_shell("echo 'a test file.txt'");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a test file.txt\n");
}

TEST(echo_single_quoted_string_does_not_expand) {
    run_result_t r = run_shell("X=foo; echo '$X stays literal'");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "$X stays literal\n");
}

TEST(single_quoted_string_treats_backslash_as_literal) {
    /// Inside '...' a backslash is just a backslash. printf is used (not
    /// echo) so XPG_ECHO escape interpretation doesn't mask the tokenizer
    /// behavior under test.
    run_result_t r = run_shell("printf '%s\\n' 'a\\b'");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a\\b\n");
}

/* ============================================================================
 * Filename-with-space end-to-end
 *
 * Each of these creates a file with a space in its name in /tmp,
 * removes it via the form being tested, and verifies the file no
 * longer exists. Uses run_shell_subprocess so tests stay isolated
 * from each other and the harness's in-process executor doesn't have
 * to worry about filesystem state cleanup.
 * ============================================================================
 */

TEST(rm_filename_with_space_via_backslash_escape) {
    /// The chain: touch creates the file; rm with backslash-space
    /// removes it; ls confirms it's gone. The whole chain runs in one
    /// subprocess so the file lifecycle is deterministic.
    const char *script = "DIR=/tmp/lush_test_quoting_$$ && "
                         "mkdir -p \"$DIR\" && "
                         "touch \"$DIR/a test file.txt\" && "
                         "rm $DIR/a\\ test\\ file.txt && "
                         "ls \"$DIR\" 2>/dev/null && "
                         "rmdir \"$DIR\"";
    run_result_t r = run_shell_subprocess(script);
    ASSERT_EXIT_STATUS(r, 0);
    /// ls of an empty directory produces no stdout.
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rm_filename_with_space_via_double_quotes) {
    const char *script = "DIR=/tmp/lush_test_quoting_$$ && "
                         "mkdir -p \"$DIR\" && "
                         "touch \"$DIR/a test file.txt\" && "
                         "rm \"$DIR/a test file.txt\" && "
                         "ls \"$DIR\" 2>/dev/null && "
                         "rmdir \"$DIR\"";
    run_result_t r = run_shell_subprocess(script);
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rm_filename_with_space_via_single_quotes) {
    const char *script = "DIR=/tmp/lush_test_quoting_$$ && "
                         "mkdir -p \"$DIR\" && "
                         "touch \"$DIR/a test file.txt\" && "
                         "rm \"$DIR\"/'a test file.txt' && "
                         "ls \"$DIR\" 2>/dev/null && "
                         "rmdir \"$DIR\"";
    run_result_t r = run_shell_subprocess(script);
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "");
}

/* ============================================================================
 * Test runner
 * ============================================================================
 */

int main(void) {
    /// The in-process executor allocates from the global symbol table,
    /// which the shell normally initializes during startup. Standalone
    /// tests have to do it themselves.
    init_symtable();

    printf("=== Shell Quoting and Escape Functional Tests ===\n");

    printf("\nBackslash escapes outside quotes (issue #90):\n");
    RUN_TEST(echo_backslash_space_produces_literal_space);
    RUN_TEST(echo_backslash_dollar_suppresses_variable_expansion);
    RUN_TEST(printf_backslash_backslash_produces_single_backslash);
    RUN_TEST(echo_escaped_keyword_treated_as_word);

    printf("\nDouble-quoted strings:\n");
    RUN_TEST(echo_double_quoted_string_with_space);
    RUN_TEST(echo_double_quoted_string_expands_variables);
    RUN_TEST(echo_double_quoted_backslash_escapes_dollar);

    printf("\nSingle-quoted strings:\n");
    RUN_TEST(echo_single_quoted_string_with_space);
    RUN_TEST(echo_single_quoted_string_does_not_expand);
    RUN_TEST(single_quoted_string_treats_backslash_as_literal);

    printf("\nFilename-with-space end-to-end:\n");
    RUN_TEST(rm_filename_with_space_via_backslash_escape);
    RUN_TEST(rm_filename_with_space_via_double_quotes);
    RUN_TEST(rm_filename_with_space_via_single_quotes);

    return TEST_RESULT();
}
