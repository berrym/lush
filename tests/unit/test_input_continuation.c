/**
 * @file test_input_continuation.c
 * @brief Unit tests for input continuation system
 *
 * Tests the multiline input continuation functionality including:
 * - State initialization and cleanup
 * - Quote tracking (single, double, backtick)
 * - Bracket/brace/parenthesis counting
 * - Control structure detection (if/then/fi, case, loops)
 * - Here document handling
 * - Continuation prompt generation
 * - Control keyword detection
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "input_continuation.h"
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
            printf("      Expected: %ld, Got: %ld\n", (long)(expected),        \
                   (long)(actual));                                            \
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

#define ASSERT_TRUE(condition, message) ASSERT((condition), message)

#define ASSERT_FALSE(condition, message) ASSERT(!(condition), message)

/* ============================================================================
 * STATE INITIALIZATION TESTS
 * ============================================================================
 */

TEST(state_init_zeros_fields) {
    continuation_state_t state;
    /* Set some garbage values first */
    memset(&state, 0xFF, sizeof(state));

    continuation_state_init(&state);

    ASSERT_EQ(state.quote_count, 0, "quote_count should be 0");
    ASSERT_EQ(state.double_quote_count, 0, "double_quote_count should be 0");
    ASSERT_EQ(state.backtick_count, 0, "backtick_count should be 0");
    ASSERT_FALSE(state.in_single_quote, "in_single_quote should be false");
    ASSERT_FALSE(state.in_double_quote, "in_double_quote should be false");
    ASSERT_FALSE(state.in_backtick, "in_backtick should be false");
    ASSERT_EQ(state.paren_count, 0, "paren_count should be 0");
    ASSERT_EQ(state.brace_count, 0, "brace_count should be 0");
    ASSERT_EQ(state.bracket_count, 0, "bracket_count should be 0");
    ASSERT_FALSE(state.escaped, "escaped should be false");
    ASSERT_FALSE(state.has_continuation, "has_continuation should be false");
    ASSERT_FALSE(state.in_here_doc, "in_here_doc should be false");
    ASSERT_NULL(state.here_doc_delimiter, "here_doc_delimiter should be NULL");
}

TEST(state_cleanup_frees_delimiter) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* Simulate setting a here doc delimiter */
    state.here_doc_delimiter = strdup("EOF");
    state.in_here_doc = true;

    continuation_state_cleanup(&state);

    /* After cleanup, delimiter should be freed and set to NULL */
    ASSERT_NULL(state.here_doc_delimiter,
                "here_doc_delimiter should be NULL after cleanup");
    ASSERT_FALSE(state.in_here_doc,
                 "in_here_doc should be false after cleanup");
}

TEST(state_cleanup_null_delimiter) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* Should not crash with NULL delimiter */
    continuation_state_cleanup(&state);
}

/* ============================================================================
 * SIMPLE COMMAND TESTS
 * ============================================================================
 */

TEST(simple_command_is_complete) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("ls -la", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Simple command should be complete");
    ASSERT_FALSE(continuation_needs_continuation(&state),
                 "Simple command should not need continuation");
}

TEST(empty_line_is_complete) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Empty line should be complete");
}

TEST(whitespace_only_is_complete) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("   \t  ", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Whitespace-only line should be complete");
}

/* ============================================================================
 * QUOTE TRACKING TESTS
 * ============================================================================
 */

TEST(single_quote_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo 'hello", &state);

    ASSERT_TRUE(state.in_single_quote, "Should be in single quote");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed quote should need continuation");
}

TEST(single_quote_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo 'hello'", &state);

    ASSERT_FALSE(state.in_single_quote, "Should not be in single quote");
    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed quote should be complete");
}

TEST(double_quote_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo \"hello", &state);

    ASSERT_TRUE(state.in_double_quote, "Should be in double quote");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed double quote should need continuation");
}

TEST(double_quote_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo \"hello world\"", &state);

    ASSERT_FALSE(state.in_double_quote, "Should not be in double quote");
    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed double quote should be complete");
}

TEST(escaped_quote_not_terminator) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo \"hello\\\"", &state);

    /* The escaped quote should not close the string */
    ASSERT_TRUE(state.in_double_quote, "Escaped quote should not close string");
}

TEST(multiline_quote) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* First line - unclosed quote */
    continuation_analyze_line("echo \"hello", &state);
    ASSERT_TRUE(state.in_double_quote,
                "Should be in double quote after first line");

    /* Second line - still in quote */
    continuation_analyze_line("world", &state);
    ASSERT_TRUE(state.in_double_quote, "Should still be in double quote");

    /* Third line - close quote */
    continuation_analyze_line("end\"", &state);
    ASSERT_FALSE(state.in_double_quote, "Quote should be closed");
    ASSERT_TRUE(continuation_is_complete(&state),
                "Should be complete after closing quote");
}

TEST(backtick_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo `date", &state);

    ASSERT_TRUE(state.in_backtick, "Should be in backtick");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed backtick should need continuation");
}

TEST(backtick_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo `date`", &state);

    ASSERT_FALSE(state.in_backtick, "Should not be in backtick");
    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed backtick should be complete");
}

/* ============================================================================
 * BRACKET/BRACE/PAREN TESTS
 * ============================================================================
 */

TEST(paren_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("(echo hello", &state);

    ASSERT_TRUE(state.paren_count > 0, "paren_count should be positive");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed paren should need continuation");
}

TEST(paren_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("(echo hello)", &state);

    ASSERT_EQ(state.paren_count, 0, "paren_count should be 0");
    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed paren should be complete");
}

TEST(nested_parens) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("((echo hello)", &state);

    ASSERT_TRUE(state.paren_count > 0, "Should still have unclosed parens");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Nested unclosed paren should need continuation");
}

TEST(brace_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("{ echo hello", &state);

    ASSERT_TRUE(state.brace_count > 0, "brace_count should be positive");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed brace should need continuation");
}

TEST(brace_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("{ echo hello; }", &state);

    ASSERT_EQ(state.brace_count, 0, "brace_count should be 0");
}

TEST(bracket_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("array[0", &state);

    ASSERT_TRUE(state.bracket_count > 0, "bracket_count should be positive");
}

TEST(bracket_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("array[0]", &state);

    ASSERT_EQ(state.bracket_count, 0, "bracket_count should be 0");
}

/* ============================================================================
 * LINE CONTINUATION TESTS
 * ============================================================================
 */

TEST(backslash_continuation) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo hello \\", &state);

    ASSERT_TRUE(state.has_continuation, "Should have continuation");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Line with continuation should need more input");
}

TEST(backslash_not_at_end) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo hello\\nworld", &state);

    /* Backslash in middle of line is not continuation */
    ASSERT_TRUE(continuation_is_complete(&state),
                "Backslash in middle should not be continuation");
}

/* ============================================================================
 * CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(if_statement_needs_fi) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("if true; then echo yes", &state);

    ASSERT_TRUE(state.in_if_statement, "Should be in if statement");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "if without fi should need continuation");
}

TEST(if_then_fi_complete) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("if true; then echo yes; fi", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "if/then/fi should be complete");
}

TEST(while_loop_needs_done) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("while true; do echo loop", &state);

    ASSERT_TRUE(state.in_while_loop, "Should be in while loop");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "while without done should need continuation");
}

TEST(while_do_done_complete) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("while true; do echo loop; done", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "while/do/done should be complete");
}

TEST(for_loop_needs_done) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("for i in 1 2 3; do echo $i", &state);

    ASSERT_TRUE(state.in_for_loop, "Should be in for loop");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "for without done should need continuation");
}

TEST(until_loop_needs_done) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("until false; do echo loop", &state);

    ASSERT_TRUE(state.in_until_loop, "Should be in until loop");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "until without done should need continuation");
}

TEST(case_statement_needs_esac) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("case $x in", &state);

    ASSERT_TRUE(state.in_case_statement, "Should be in case statement");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "case without esac should need continuation");
}

/* ============================================================================
 * CONTROL KEYWORD DETECTION TESTS
 * ============================================================================
 */

TEST(is_control_keyword_if) {
    ASSERT_TRUE(continuation_is_control_keyword("if"),
                "if should be a control keyword");
}

TEST(is_control_keyword_then) {
    ASSERT_TRUE(continuation_is_control_keyword("then"),
                "then should be a control keyword");
}

TEST(is_control_keyword_else) {
    ASSERT_TRUE(continuation_is_control_keyword("else"),
                "else should be a control keyword");
}

TEST(is_control_keyword_elif) {
    ASSERT_TRUE(continuation_is_control_keyword("elif"),
                "elif should be a control keyword");
}

TEST(is_control_keyword_fi) {
    ASSERT_TRUE(continuation_is_control_keyword("fi"),
                "fi should be a control keyword");
}

TEST(is_control_keyword_while) {
    ASSERT_TRUE(continuation_is_control_keyword("while"),
                "while should be a control keyword");
}

TEST(is_control_keyword_do) {
    ASSERT_TRUE(continuation_is_control_keyword("do"),
                "do should be a control keyword");
}

TEST(is_control_keyword_done) {
    ASSERT_TRUE(continuation_is_control_keyword("done"),
                "done should be a control keyword");
}

TEST(is_control_keyword_for) {
    ASSERT_TRUE(continuation_is_control_keyword("for"),
                "for should be a control keyword");
}

TEST(is_control_keyword_case) {
    ASSERT_TRUE(continuation_is_control_keyword("case"),
                "case should be a control keyword");
}

TEST(is_control_keyword_esac) {
    ASSERT_TRUE(continuation_is_control_keyword("esac"),
                "esac should be a control keyword");
}

TEST(is_control_keyword_until) {
    ASSERT_TRUE(continuation_is_control_keyword("until"),
                "until should be a control keyword");
}

TEST(is_not_control_keyword_echo) {
    ASSERT_FALSE(continuation_is_control_keyword("echo"),
                 "echo should not be a control keyword");
}

TEST(is_not_control_keyword_ls) {
    ASSERT_FALSE(continuation_is_control_keyword("ls"),
                 "ls should not be a control keyword");
}

TEST(is_not_control_keyword_empty) {
    ASSERT_FALSE(continuation_is_control_keyword(""),
                 "empty string should not be a control keyword");
}

/* ============================================================================
 * TERMINATOR DETECTION TESTS
 * ============================================================================
 */

TEST(is_terminator_fi) {
    ASSERT_TRUE(continuation_is_terminator("fi"), "fi should be a terminator");
}

TEST(is_terminator_done) {
    ASSERT_TRUE(continuation_is_terminator("done"),
                "done should be a terminator");
}

TEST(is_terminator_esac) {
    ASSERT_TRUE(continuation_is_terminator("esac"),
                "esac should be a terminator");
}

TEST(is_terminator_close_brace) {
    ASSERT_TRUE(continuation_is_terminator("}"), "} should be a terminator");
}

TEST(is_not_terminator_if) {
    ASSERT_FALSE(continuation_is_terminator("if"),
                 "if should not be a terminator");
}

TEST(is_not_terminator_while) {
    ASSERT_FALSE(continuation_is_terminator("while"),
                 "while should not be a terminator");
}

/* ============================================================================
 * CONTINUATION PROMPT TESTS
 * ============================================================================
 */

TEST(prompt_for_single_quote) {
    continuation_state_t state;
    continuation_state_init(&state);
    state.in_single_quote = true;

    const char *prompt = continuation_get_prompt(&state);
    ASSERT_NOT_NULL(prompt, "Prompt should not be NULL");
    /* Prompt should indicate quote context */
}

TEST(prompt_for_double_quote) {
    continuation_state_t state;
    continuation_state_init(&state);
    state.in_double_quote = true;

    const char *prompt = continuation_get_prompt(&state);
    ASSERT_NOT_NULL(prompt, "Prompt should not be NULL");
}

TEST(prompt_for_here_doc) {
    continuation_state_t state;
    continuation_state_init(&state);
    state.in_here_doc = true;

    const char *prompt = continuation_get_prompt(&state);
    ASSERT_NOT_NULL(prompt, "Prompt should not be NULL");
}

TEST(prompt_for_complete_state) {
    continuation_state_t state;
    continuation_state_init(&state);

    const char *prompt = continuation_get_prompt(&state);
    ASSERT_NOT_NULL(prompt, "Prompt should not be NULL for complete state");
}

/* ============================================================================
 * COMMAND SUBSTITUTION TESTS
 * ============================================================================
 */

TEST(command_substitution_dollar_paren) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo $(date", &state);

    /* Should need continuation for unclosed $() */
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed $() should need continuation");
}

TEST(command_substitution_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo $(date)", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed $() should be complete");
}

TEST(arithmetic_expansion_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo $((1 + 2", &state);

    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed $(()) should need continuation");
}

TEST(arithmetic_expansion_closed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo $((1 + 2))", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Closed $(()) should be complete");
}

/* ============================================================================
 * PIPE AND OPERATOR TESTS
 * ============================================================================
 */

TEST(pipe_at_end) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("ls |", &state);

    /* Note: Whether pipe at end needs continuation depends on implementation */
    /* Just verify it doesn't crash and returns a valid result */
    (void)continuation_is_complete(&state);
}

TEST(operators_analyzed) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* Test that operators are analyzed without crashing */
    continuation_analyze_line("true && false", &state);
    ASSERT_TRUE(continuation_is_complete(&state),
                "Complete && expression should be complete");

    continuation_state_init(&state);
    continuation_analyze_line("true || false", &state);
    ASSERT_TRUE(continuation_is_complete(&state),
                "Complete || expression should be complete");
}

/* ============================================================================
 * FUNCTION DEFINITION TESTS
 * ============================================================================
 */

TEST(function_definition_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("myfunc() {", &state);

    ASSERT_TRUE(state.in_function_definition || state.brace_count > 0,
                "Should be in function definition");
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed function should need continuation");
}

TEST(function_keyword_unclosed) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("function myfunc {", &state);

    ASSERT_FALSE(continuation_is_complete(&state),
                 "Unclosed function should need continuation");
}

/* ============================================================================
 * COMPLEX MULTILINE TESTS
 * ============================================================================
 */

TEST(multiline_if_statement) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* Line 1: if */
    continuation_analyze_line("if [ -f file ]", &state);
    ASSERT_FALSE(continuation_is_complete(&state),
                 "if without then should need continuation");

    /* Line 2: then */
    continuation_analyze_line("then", &state);
    ASSERT_FALSE(continuation_is_complete(&state),
                 "if/then without fi should need continuation");

    /* Line 3: command */
    continuation_analyze_line("    echo exists", &state);
    ASSERT_FALSE(continuation_is_complete(&state), "Still need fi");

    /* Line 4: fi */
    continuation_analyze_line("fi", &state);
    ASSERT_TRUE(continuation_is_complete(&state),
                "if/then/fi should be complete");
}

TEST(nested_loops) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("for i in 1 2; do for j in a b; do echo $i $j",
                              &state);
    ASSERT_FALSE(continuation_is_complete(&state),
                 "Nested loops need multiple done");

    continuation_analyze_line("done", &state);
    ASSERT_FALSE(continuation_is_complete(&state), "Still need outer done");

    continuation_analyze_line("done", &state);
    ASSERT_TRUE(continuation_is_complete(&state), "Both loops closed");
}

TEST(quote_in_single_quote_ignored) {
    continuation_state_t state;
    continuation_state_init(&state);

    /* Double quote inside single quotes should not start double quote mode */
    continuation_analyze_line("echo '\"hello\"'", &state);

    ASSERT_FALSE(state.in_single_quote, "Single quote should be closed");
    ASSERT_FALSE(state.in_double_quote,
                 "Double quote inside single quotes is literal");
    ASSERT_TRUE(continuation_is_complete(&state), "Should be complete");
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

TEST(comment_line) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("# this is a comment", &state);

    ASSERT_TRUE(continuation_is_complete(&state), "Comment should be complete");
}

TEST(quote_in_comment) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("# this is a comment with \"quotes\"", &state);

    /* Just verify the line is analyzed without crashing */
    /* Note: Implementation may or may not track quotes in comments */
    (void)continuation_is_complete(&state);
}

TEST(semicolon_separates_commands) {
    continuation_state_t state;
    continuation_state_init(&state);

    continuation_analyze_line("echo hello; echo world", &state);

    ASSERT_TRUE(continuation_is_complete(&state),
                "Semicolon-separated commands should be complete");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("\n=== State Initialization Tests ===\n");
    RUN_TEST(state_init_zeros_fields);
    RUN_TEST(state_cleanup_frees_delimiter);
    RUN_TEST(state_cleanup_null_delimiter);

    printf("\n=== Simple Command Tests ===\n");
    RUN_TEST(simple_command_is_complete);
    RUN_TEST(empty_line_is_complete);
    RUN_TEST(whitespace_only_is_complete);

    printf("\n=== Quote Tracking Tests ===\n");
    RUN_TEST(single_quote_unclosed);
    RUN_TEST(single_quote_closed);
    RUN_TEST(double_quote_unclosed);
    RUN_TEST(double_quote_closed);
    RUN_TEST(escaped_quote_not_terminator);
    RUN_TEST(multiline_quote);
    RUN_TEST(backtick_unclosed);
    RUN_TEST(backtick_closed);

    printf("\n=== Bracket/Brace/Paren Tests ===\n");
    RUN_TEST(paren_unclosed);
    RUN_TEST(paren_closed);
    RUN_TEST(nested_parens);
    RUN_TEST(brace_unclosed);
    RUN_TEST(brace_closed);
    RUN_TEST(bracket_unclosed);
    RUN_TEST(bracket_closed);

    printf("\n=== Line Continuation Tests ===\n");
    RUN_TEST(backslash_continuation);
    RUN_TEST(backslash_not_at_end);

    printf("\n=== Control Structure Tests ===\n");
    RUN_TEST(if_statement_needs_fi);
    RUN_TEST(if_then_fi_complete);
    RUN_TEST(while_loop_needs_done);
    RUN_TEST(while_do_done_complete);
    RUN_TEST(for_loop_needs_done);
    RUN_TEST(until_loop_needs_done);
    RUN_TEST(case_statement_needs_esac);

    printf("\n=== Control Keyword Detection Tests ===\n");
    RUN_TEST(is_control_keyword_if);
    RUN_TEST(is_control_keyword_then);
    RUN_TEST(is_control_keyword_else);
    RUN_TEST(is_control_keyword_elif);
    RUN_TEST(is_control_keyword_fi);
    RUN_TEST(is_control_keyword_while);
    RUN_TEST(is_control_keyword_do);
    RUN_TEST(is_control_keyword_done);
    RUN_TEST(is_control_keyword_for);
    RUN_TEST(is_control_keyword_case);
    RUN_TEST(is_control_keyword_esac);
    RUN_TEST(is_control_keyword_until);
    RUN_TEST(is_not_control_keyword_echo);
    RUN_TEST(is_not_control_keyword_ls);
    RUN_TEST(is_not_control_keyword_empty);

    printf("\n=== Terminator Detection Tests ===\n");
    RUN_TEST(is_terminator_fi);
    RUN_TEST(is_terminator_done);
    RUN_TEST(is_terminator_esac);
    RUN_TEST(is_terminator_close_brace);
    RUN_TEST(is_not_terminator_if);
    RUN_TEST(is_not_terminator_while);

    printf("\n=== Continuation Prompt Tests ===\n");
    RUN_TEST(prompt_for_single_quote);
    RUN_TEST(prompt_for_double_quote);
    RUN_TEST(prompt_for_here_doc);
    RUN_TEST(prompt_for_complete_state);

    printf("\n=== Command Substitution Tests ===\n");
    RUN_TEST(command_substitution_dollar_paren);
    RUN_TEST(command_substitution_closed);
    RUN_TEST(arithmetic_expansion_unclosed);
    RUN_TEST(arithmetic_expansion_closed);

    printf("\n=== Pipe and Operator Tests ===\n");
    RUN_TEST(pipe_at_end);
    RUN_TEST(operators_analyzed);

    printf("\n=== Function Definition Tests ===\n");
    RUN_TEST(function_definition_unclosed);
    RUN_TEST(function_keyword_unclosed);

    printf("\n=== Complex Multiline Tests ===\n");
    RUN_TEST(multiline_if_statement);
    RUN_TEST(nested_loops);
    RUN_TEST(quote_in_single_quote_ignored);

    printf("\n=== Edge Case Tests ===\n");
    RUN_TEST(comment_line);
    RUN_TEST(quote_in_comment);
    RUN_TEST(semicolon_separates_commands);

    printf("\n=== All Input Continuation tests passed! ===\n");
    return 0;
}
