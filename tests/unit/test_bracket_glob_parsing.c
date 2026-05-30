/**
 * @file test_bracket_glob_parsing.c
 * @brief Regression tests for #154 -- leading-`[` bracket globs.
 *
 * Before the fix, any word that started with `[` was mis-parsed as an
 * invocation of the `[` test builtin, regardless of whether it
 * appeared as a command name, an argument, an assignment RHS, or a
 * for/select word-list item. The parser raised
 * `error[E1127]: '[' command missing closing ']'` instead of letting
 * the glob expand.
 *
 * Each test below pins one user-facing position where a leading-`[`
 * word must behave like a glob. The control tests confirm the
 * legitimate `[ ... ]` test command still works.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "symtable.h"
#include "test_framework.h"
#include "test_shell_harness.h"

#include <stdio.h>

/// Common script preamble: create an isolated temp dir with files
/// 5.txt and 6.txt, cd into it, and arrange cleanup on script exit.
#define BRACKET_TEST_PREAMBLE                                                  \
    "DIR=/tmp/lush_test_bracket_$$ && mkdir -p \"$DIR\" && "                   \
    "touch \"$DIR/5.txt\" \"$DIR/6.txt\" && "                                  \
    "trap 'rm -rf \"$DIR\"' EXIT && "                                          \
    "cd \"$DIR\" && "

/* ============================================================================
 * #154 repro cases: every position where a leading-`[` word must glob
 * ============================================================================
 */

TEST(echo_leading_bracket_glob_expands) {
    /// echo [5].txt -> "5.txt\n". This is the headline #154 case.
    run_result_t r = run_shell_subprocess(BRACKET_TEST_PREAMBLE "echo [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n");
}

TEST(echo_leading_bracket_range_glob_expands) {
    /// echo [0-9].txt -> "5.txt 6.txt\n" (alphabetic order).
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "echo [0-9].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt 6.txt\n");
}

TEST(true_leading_bracket_arg_does_not_terminate) {
    /// true [5].txt: before #154 fix this emitted E1127 (the parser
    /// routed `[5].txt` to the `[` test builtin). After: argv[1] is
    /// the glob-expanded "5.txt" and true returns 0.
    run_result_t r = run_shell_subprocess(BRACKET_TEST_PREAMBLE "true [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
}

TEST(assignment_rhs_leading_bracket_is_literal) {
    /// x=[5].txt; echo "$x" -- assignment RHS does NOT do glob
    /// expansion (POSIX: pathname expansion happens at word-expansion
    /// time on commands, not on assignment values). So `$x` is the
    /// literal "[5].txt".
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "x=[5].txt; echo \"$x\"");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "[5].txt\n");
}

TEST(for_loop_word_list_leading_bracket_globs) {
    /// for f in [5].txt; do echo "F=$f"; done -> "F=5.txt\n".
    /// Before fix: word list was empty (TOK_LBRACKET rejected) so
    /// the loop body never ran.
    run_result_t r = run_shell_subprocess(
        BRACKET_TEST_PREAMBLE "for f in [5].txt; do echo \"F=$f\"; done");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "F=5.txt\n");
}

TEST(for_loop_word_list_leading_bracket_range_globs) {
    /// for f in [0-9].txt -> two iterations.
    run_result_t r = run_shell_subprocess(
        BRACKET_TEST_PREAMBLE "for f in [0-9].txt; do echo \"F=$f\"; done");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "F=5.txt\nF=6.txt\n");
}

/* ============================================================================
 * Controls: legitimate `[ ... ]` test builtin still works
 * ============================================================================
 */

TEST(test_command_true_branch_still_works) {
    run_result_t r = run_shell("[ 1 = 1 ]; echo $?");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "0\n");
}

TEST(test_command_false_branch_still_works) {
    run_result_t r = run_shell("[ 1 = 2 ]; echo $?");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "1\n");
}

TEST(bracket_command_missing_close_still_errors) {
    /// Bare `[` with no closing `]` is still a malformed test
    /// invocation -- the fix for #154 didn't touch this path.
    run_result_t r = run_shell("[ 1 = 1");
    ASSERT_STDERR_CONTAINS(r, "missing closing ']'");
}

/* ============================================================================
 * Behavior matches across all modes (issue calls out posix/bash/zsh)
 * ============================================================================
 */

TEST(leading_bracket_glob_works_in_posix_mode) {
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "mode posix; echo [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n");
}

TEST(leading_bracket_glob_works_in_bash_mode) {
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "mode bash; echo [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n");
}

TEST(leading_bracket_glob_works_in_zsh_mode) {
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "mode zsh; echo [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n");
}

TEST(leading_bracket_glob_works_in_lush_mode) {
    run_result_t r =
        run_shell_subprocess(BRACKET_TEST_PREAMBLE "mode lush; echo [5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5.txt\n");
}

/* ============================================================================
 * Adjacency: a [ that follows a non-space char must keep working too
 * ============================================================================
 */

TEST(non_leading_bracket_glob_still_works) {
    /// `echo a[5].txt` -- the tokenizer already handles this case
    /// (`[` is a mid-word char in is_word_char), so a single TOK_WORD
    /// goes to the executor. This test guards against the parser fix
    /// breaking the adjacent-bracket case.
    run_result_t r = run_shell_subprocess(BRACKET_TEST_PREAMBLE
                                          "touch a5.txt; echo a[5].txt");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a5.txt\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    init_symtable();

    printf("=== #154 bracket-glob parsing regression tests ===\n");

    printf("\nRepro cases (every position where a leading-[ must glob):\n");
    RUN_TEST(echo_leading_bracket_glob_expands);
    RUN_TEST(echo_leading_bracket_range_glob_expands);
    RUN_TEST(true_leading_bracket_arg_does_not_terminate);
    RUN_TEST(assignment_rhs_leading_bracket_is_literal);
    RUN_TEST(for_loop_word_list_leading_bracket_globs);
    RUN_TEST(for_loop_word_list_leading_bracket_range_globs);

    printf("\nControls (legitimate `[ ... ]` test command):\n");
    RUN_TEST(test_command_true_branch_still_works);
    RUN_TEST(test_command_false_branch_still_works);
    RUN_TEST(bracket_command_missing_close_still_errors);

    printf("\nMode coverage (issue calls out posix/bash/zsh/lush):\n");
    RUN_TEST(leading_bracket_glob_works_in_posix_mode);
    RUN_TEST(leading_bracket_glob_works_in_bash_mode);
    RUN_TEST(leading_bracket_glob_works_in_zsh_mode);
    RUN_TEST(leading_bracket_glob_works_in_lush_mode);

    printf("\nAdjacency regression guard:\n");
    RUN_TEST(non_leading_bracket_glob_still_works);

    return TEST_RESULT();
}
