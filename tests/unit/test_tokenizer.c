/**
 * @file test_tokenizer.c
 * @brief Unit tests for shell tokenizer
 *
 * Tests the tokenizer including:
 * - Basic token types (words, strings, numbers)
 * - Operators and redirections
 * - Keywords (if, then, else, for, while, case, etc.)
 * - Edge cases and error handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "test_framework.h"
#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Test framework macros

/* ============================================================================
 * LIFECYCLE TESTS
 * ============================================================================
 */

TEST(tokenizer_new_simple) {
    tokenizer_t *tok = tokenizer_new("echo hello");
    ASSERT_NOT_NULL(tok, "tokenizer_new should return non-NULL");
    tokenizer_free(tok);
}

TEST(tokenizer_new_empty) {
    tokenizer_t *tok = tokenizer_new("");
    ASSERT_NOT_NULL(tok, "tokenizer_new with empty string should succeed");

    token_t *token = tokenizer_current(tok);
    ASSERT_NOT_NULL(token, "Should have EOF token");
    ASSERT_EQ(token->type, TOK_EOF, "Empty input should yield EOF");

    tokenizer_free(tok);
}

TEST(tokenizer_new_null) {
    tokenizer_t *tok = tokenizer_new(NULL);
    // Should handle NULL gracefully - either return NULL or empty tokenizer
    if (tok != NULL) {
        token_t *token = tokenizer_current(tok);
        if (token != NULL) {
            ASSERT_EQ(token->type, TOK_EOF, "NULL input should yield EOF");
        }
        tokenizer_free(tok);
    }
}

/* ============================================================================
 * BASIC TOKEN TESTS
 * ============================================================================
 */

TEST(tokenize_single_word) {
    tokenizer_t *tok = tokenizer_new("hello");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_NOT_NULL(token, "Should have token");
    ASSERT_EQ(token->type, TOK_WORD, "Should be WORD token");
    ASSERT_STR_EQ(token->text, "hello", "Token text mismatch");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_NOT_NULL(token, "Should have EOF token");
    ASSERT_EQ(token->type, TOK_EOF, "Should be at EOF");

    tokenizer_free(tok);
}

TEST(tokenize_multiple_words) {
    tokenizer_t *tok = tokenizer_new("echo hello world");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "First token should be WORD");
    ASSERT_STR_EQ(token->text, "echo", "First word mismatch");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Second token should be WORD");
    ASSERT_STR_EQ(token->text, "hello", "Second word mismatch");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Third token should be WORD");
    ASSERT_STR_EQ(token->text, "world", "Third word mismatch");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_EOF, "Should be at EOF");

    tokenizer_free(tok);
}

TEST(tokenize_single_quoted_string) {
    tokenizer_t *tok = tokenizer_new("'hello world'");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_STRING, "Should be STRING token");
    /* String content may or may not include quotes depending on implementation
     */
    ASSERT_NOT_NULL(token->text, "Token text should not be NULL");

    tokenizer_free(tok);
}

TEST(tokenize_double_quoted_string) {
    tokenizer_t *tok = tokenizer_new("\"hello world\"");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_EXPANDABLE_STRING,
              "Should be EXPANDABLE_STRING");
    ASSERT_NOT_NULL(token->text, "Token text should not be NULL");

    tokenizer_free(tok);
}

TEST(tokenize_number) {
    tokenizer_t *tok = tokenizer_new("42");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    // Numbers might be recognized as WORD or NUMBER depending on context
    ASSERT(token->type == TOK_NUMBER || token->type == TOK_WORD,
           "Should be NUMBER or WORD token");
    ASSERT_STR_EQ(token->text, "42", "Number text mismatch");

    tokenizer_free(tok);
}

/* ============================================================================
 * OPERATOR TESTS
 * ============================================================================
 */

TEST(tokenize_semicolon) {
    tokenizer_t *tok = tokenizer_new("echo; ls");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "First should be WORD");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_SEMICOLON, "Should be SEMICOLON");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Third should be WORD");

    tokenizer_free(tok);
}

TEST(tokenize_pipe) {
    tokenizer_t *tok = tokenizer_new("cat file | grep pattern");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cat
    tokenizer_advance(tok); // skip file

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_PIPE, "Should be PIPE");

    tokenizer_free(tok);
}

TEST(tokenize_logical_and) {
    tokenizer_t *tok = tokenizer_new("cmd1 && cmd2");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cmd1

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_LOGICAL_AND, "Should be LOGICAL_AND");

    tokenizer_free(tok);
}

TEST(tokenize_logical_or) {
    tokenizer_t *tok = tokenizer_new("cmd1 || cmd2");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cmd1

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_LOGICAL_OR, "Should be LOGICAL_OR");

    tokenizer_free(tok);
}

TEST(tokenize_background) {
    tokenizer_t *tok = tokenizer_new("sleep 10 &");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip sleep
    tokenizer_advance(tok); // skip 10

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_AND, "Should be AND (background)");

    tokenizer_free(tok);
}

/* ============================================================================
 * REDIRECTION TESTS
 * ============================================================================
 */

TEST(tokenize_redirect_in) {
    tokenizer_t *tok = tokenizer_new("cat < file");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cat

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_REDIRECT_IN, "Should be REDIRECT_IN");

    tokenizer_free(tok);
}

TEST(tokenize_redirect_out) {
    tokenizer_t *tok = tokenizer_new("echo hello > file");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip echo
    tokenizer_advance(tok); // skip hello

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_REDIRECT_OUT, "Should be REDIRECT_OUT");

    tokenizer_free(tok);
}

TEST(tokenize_append) {
    tokenizer_t *tok = tokenizer_new("echo hello >> file");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip echo
    tokenizer_advance(tok); // skip hello

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_APPEND, "Should be APPEND");

    tokenizer_free(tok);
}

TEST(tokenize_heredoc) {
    tokenizer_t *tok = tokenizer_new("cat << EOF");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cat

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_HEREDOC, "Should be HEREDOC");

    tokenizer_free(tok);
}

TEST(tokenize_herestring) {
    tokenizer_t *tok = tokenizer_new("cat <<< 'hello'");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cat

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_HERESTRING, "Should be HERESTRING");

    tokenizer_free(tok);
}

TEST(tokenize_stderr_redirect) {
    tokenizer_t *tok = tokenizer_new("cmd 2> /dev/null");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cmd

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_REDIRECT_ERR, "Should be REDIRECT_ERR");

    tokenizer_free(tok);
}

TEST(tokenize_redirect_both) {
    tokenizer_t *tok = tokenizer_new("cmd &> file");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cmd

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_REDIRECT_BOTH, "Should be REDIRECT_BOTH");

    tokenizer_free(tok);
}

/* ============================================================================
 * KEYWORD TESTS
 * ============================================================================
 */

TEST(tokenize_if_then_else_fi) {
    tokenizer_t *tok =
        tokenizer_new("if true; then echo yes; else echo no; fi");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_IF, "Should be IF keyword");

    tokenizer_advance(tok); // skip true
    tokenizer_advance(tok); // skip ;
    tokenizer_advance(tok);

    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_THEN, "Should be THEN keyword");

    tokenizer_free(tok);
}

TEST(tokenize_for_in_do_done) {
    tokenizer_t *tok = tokenizer_new("for i in 1 2 3; do echo $i; done");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_FOR, "Should be FOR keyword");

    tokenizer_advance(tok); // skip i
    tokenizer_advance(tok);

    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_IN, "Should be IN keyword");

    tokenizer_free(tok);
}

TEST(tokenize_while_do_done) {
    tokenizer_t *tok = tokenizer_new("while true; do echo loop; done");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WHILE, "Should be WHILE keyword");

    tokenizer_free(tok);
}

TEST(tokenize_case_esac) {
    tokenizer_t *tok = tokenizer_new("case $x in a) echo a;; esac");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_CASE, "Should be CASE keyword");

    tokenizer_free(tok);
}

TEST(tokenize_until) {
    tokenizer_t *tok = tokenizer_new("until false; do echo loop; done");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_UNTIL, "Should be UNTIL keyword");

    tokenizer_free(tok);
}

TEST(tokenize_function) {
    tokenizer_t *tok = tokenizer_new("function foo { echo bar; }");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_FUNCTION, "Should be FUNCTION keyword");

    tokenizer_free(tok);
}

/* ============================================================================
 * DELIMITER TESTS
 * ============================================================================
 */

TEST(tokenize_parentheses) {
    tokenizer_t *tok = tokenizer_new("(echo hello)");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_LPAREN, "Should be LPAREN");

    tokenizer_advance(tok); // skip echo
    tokenizer_advance(tok); // skip hello
    tokenizer_advance(tok);

    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_RPAREN, "Should be RPAREN");

    tokenizer_free(tok);
}

TEST(tokenize_braces) {
    tokenizer_t *tok = tokenizer_new("{ echo hello; }");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_LBRACE, "Should be LBRACE");

    tokenizer_free(tok);
}

TEST(tokenize_double_parens) {
    tokenizer_t *tok = tokenizer_new("(( x + y ))");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_DOUBLE_LPAREN, "Should be DOUBLE_LPAREN");

    tokenizer_free(tok);
}

TEST(tokenize_double_brackets) {
    tokenizer_t *tok = tokenizer_new("[[ -f file ]]");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_DOUBLE_LBRACKET, "Should be DOUBLE_LBRACKET");

    tokenizer_free(tok);
}

/* ============================================================================
 * EXTENDED SYNTAX TESTS
 * ============================================================================
 */

TEST(tokenize_process_sub_in) {
    tokenizer_t *tok = tokenizer_new("diff <(cat a) <(cat b)");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip diff

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_PROC_SUB_IN, "Should be PROC_SUB_IN");

    tokenizer_free(tok);
}

TEST(tokenize_process_sub_out) {
    tokenizer_t *tok = tokenizer_new("tee >(cat > file)");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip tee

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_PROC_SUB_OUT, "Should be PROC_SUB_OUT");

    tokenizer_free(tok);
}

TEST(tokenize_pipe_stderr) {
    tokenizer_t *tok = tokenizer_new("cmd |& grep error");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip cmd

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_PIPE_STDERR, "Should be PIPE_STDERR");

    tokenizer_free(tok);
}

TEST(tokenize_plus_assign) {
    tokenizer_t *tok = tokenizer_new("arr+=value");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip arr

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_PLUS_ASSIGN, "Should be PLUS_ASSIGN");

    tokenizer_free(tok);
}

/* ============================================================================
 * UTILITY FUNCTION TESTS
 * ============================================================================
 */

TEST(token_type_name_coverage) {
    // Test that token_type_name returns non-NULL for various types
    ASSERT_NOT_NULL(token_type_name(TOK_EOF), "EOF name should not be NULL");
    ASSERT_NOT_NULL(token_type_name(TOK_WORD), "WORD name should not be NULL");
    ASSERT_NOT_NULL(token_type_name(TOK_PIPE), "PIPE name should not be NULL");
    ASSERT_NOT_NULL(token_type_name(TOK_IF), "IF name should not be NULL");
    ASSERT_NOT_NULL(token_type_name(TOK_ERROR),
                    "ERROR name should not be NULL");
}

TEST(token_is_keyword) {
    ASSERT(token_is_keyword(TOK_IF), "IF should be a keyword");
    ASSERT(token_is_keyword(TOK_THEN), "THEN should be a keyword");
    ASSERT(token_is_keyword(TOK_ELSE), "ELSE should be a keyword");
    ASSERT(token_is_keyword(TOK_FOR), "FOR should be a keyword");
    ASSERT(token_is_keyword(TOK_WHILE), "WHILE should be a keyword");
    ASSERT(token_is_keyword(TOK_CASE), "CASE should be a keyword");
    ASSERT(!token_is_keyword(TOK_WORD), "WORD should not be a keyword");
    ASSERT(!token_is_keyword(TOK_PIPE), "PIPE should not be a keyword");
}

TEST(token_is_operator) {
    ASSERT(token_is_operator(TOK_PIPE), "PIPE should be an operator");
    ASSERT(token_is_operator(TOK_SEMICOLON), "SEMICOLON should be an operator");
    ASSERT(token_is_operator(TOK_LOGICAL_AND),
           "LOGICAL_AND should be an operator");
    ASSERT(token_is_operator(TOK_REDIRECT_OUT),
           "REDIRECT_OUT should be an operator");
    ASSERT(!token_is_operator(TOK_WORD), "WORD should not be an operator");
    ASSERT(!token_is_operator(TOK_IF), "IF should not be an operator");
}

TEST(token_is_word_like) {
    ASSERT(token_is_word_like(TOK_WORD), "WORD should be word-like");
    ASSERT(token_is_word_like(TOK_STRING), "STRING should be word-like");
    ASSERT(token_is_word_like(TOK_EXPANDABLE_STRING),
           "EXPANDABLE_STRING should be word-like");
    ASSERT(!token_is_word_like(TOK_PIPE), "PIPE should not be word-like");
    ASSERT(!token_is_word_like(TOK_IF), "IF should not be word-like");
}

/* ============================================================================
 * TOKENIZER CONTROL TESTS
 * ============================================================================
 */

TEST(tokenizer_peek) {
    tokenizer_t *tok = tokenizer_new("echo hello");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *current = tokenizer_current(tok);
    token_t *next = tokenizer_peek(tok);

    ASSERT_NOT_NULL(current, "Current should not be NULL");
    ASSERT_NOT_NULL(next, "Peek should not be NULL");
    ASSERT_EQ(current->type, TOK_WORD, "Current should be WORD");
    ASSERT_EQ(next->type, TOK_WORD, "Peek should be WORD");
    ASSERT_STR_EQ(current->text, "echo", "Current text mismatch");
    ASSERT_STR_EQ(next->text, "hello", "Peek text mismatch");

    // Peek should not advance
    token_t *still_current = tokenizer_current(tok);
    ASSERT_STR_EQ(still_current->text, "echo", "Peek should not advance");

    tokenizer_free(tok);
}

TEST(tokenizer_match) {
    tokenizer_t *tok = tokenizer_new("echo");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    ASSERT(tokenizer_match(tok, TOK_WORD), "Should match WORD");
    ASSERT(!tokenizer_match(tok, TOK_PIPE), "Should not match PIPE");

    tokenizer_free(tok);
}

TEST(tokenizer_consume) {
    tokenizer_t *tok = tokenizer_new("echo hello");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    ASSERT(tokenizer_consume(tok, TOK_WORD), "Should consume WORD");

    token_t *token = tokenizer_current(tok);
    ASSERT_STR_EQ(token->text, "hello", "Should have advanced to hello");

    ASSERT(!tokenizer_consume(tok, TOK_PIPE), "Should not consume PIPE");

    // Should still be at hello since consume failed
    token = tokenizer_current(tok);
    ASSERT_STR_EQ(token->text, "hello", "Should still be at hello");

    tokenizer_free(tok);
}

TEST(tokenizer_disable_keywords) {
    tokenizer_t *tok = tokenizer_new("if");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    // By default, keywords should be recognized
    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_IF, "Should recognize IF keyword by default");

    tokenizer_free(tok);

    // Test that enable_keywords flag exists and can be set
    tok = tokenizer_new("echo");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    // Verify the flag can be toggled without crashing
    tokenizer_enable_keywords(tok, false);
    tokenizer_enable_keywords(tok, true);

    // Current token should still be valid
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "echo should be WORD");

    tokenizer_free(tok);
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

TEST(tokenize_whitespace_only) {
    tokenizer_t *tok = tokenizer_new("   \t  ");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_EOF, "Whitespace-only should yield EOF");

    tokenizer_free(tok);
}

TEST(tokenize_comment) {
    tokenizer_t *tok = tokenizer_new("echo hello # this is a comment");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "First should be WORD");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Second should be WORD");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    // Tokenizer returns TOK_COMMENT for comments (parser handles skipping)
    ASSERT_EQ(token->type, TOK_COMMENT, "Should have COMMENT token");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_EOF, "After comment should be EOF");

    tokenizer_free(tok);
}

TEST(tokenize_newline) {
    tokenizer_t *tok = tokenizer_new("echo\nls");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "First should be WORD");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_NEWLINE, "Should have NEWLINE");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Third should be WORD");

    tokenizer_free(tok);
}

TEST(tokenize_variable) {
    tokenizer_t *tok = tokenizer_new("echo $HOME");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip echo

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_VARIABLE, "Should be VARIABLE");

    tokenizer_free(tok);
}

TEST(tokenize_command_substitution) {
    tokenizer_t *tok = tokenizer_new("echo $(pwd)");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip echo

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_COMMAND_SUB, "Should be COMMAND_SUB");

    tokenizer_free(tok);
}

TEST(tokenize_arithmetic_expansion) {
    tokenizer_t *tok = tokenizer_new("echo $((1+2))");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    tokenizer_advance(tok); // skip echo

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_ARITH_EXP, "Should be ARITH_EXP");

    tokenizer_free(tok);
}

TEST(tokenize_special_chars_in_word) {
    tokenizer_t *tok = tokenizer_new("file_name-with.dots");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->type, TOK_WORD, "Should be WORD");
    ASSERT_STR_EQ(token->text, "file_name-with.dots",
                  "Word with special chars");

    tokenizer_free(tok);
}

TEST(tokenize_line_position_tracking) {
    tokenizer_t *tok = tokenizer_new("echo hello");
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");

    token_t *token = tokenizer_current(tok);
    ASSERT_EQ(token->line, 1, "First token should be on line 1");
    ASSERT_EQ(token->column, 1, "First token should start at column 1");

    tokenizer_advance(tok);
    token = tokenizer_current(tok);
    ASSERT_EQ(token->line, 1, "Second token should be on line 1");
    ASSERT(token->column > 1, "Second token should be after first");

    tokenizer_free(tok);
}

/* ============================================================================
 * REGRESSION — issue #50
 * Brace-expansion suffix scanner used to advance scan_pos by the claimed
 * UTF-8 sequence length without bounds-checking against input_length, so
 * a partial sequence at end-of-input caused token_new's memcpy to read
 * past the buffer (heap-buffer-overflow under ASan). The minimal trigger
 * is "{x}" + a 4-byte UTF-8 lead byte (0xF0) with zero continuations.
 * Behavioural assertion: parses successfully without crashing.
 * ============================================================================
 */

TEST(utf8_partial_sequence_at_eof_brace_expansion) {
    /* 4 bytes total: '{', 'x', '}', 0xF0. The lead byte claims a 4-byte
     * UTF-8 sequence; no continuation bytes follow. */
    const char input[] = {'{', 'x', '}', (char)0xF0, '\0'};
    tokenizer_t *t = tokenizer_new(input);
    ASSERT_NOT_NULL(t, "tokenizer_new failed");
    /* Drain all tokens. The previous defect crashed inside token_new during
     * the first WORD emission; reaching TOK_EOF here proves the bound holds. */
    token_t *tok = NULL;
    do {
        tok = tokenizer_current(t);
        ASSERT_NOT_NULL(tok, "tokenizer_current returned NULL");
        if (tok->type != TOK_EOF) {
            tokenizer_advance(t);
        }
    } while (tok->type != TOK_EOF);
    tokenizer_free(t);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running tokenizer unit tests...\n\n");

    printf("Lifecycle tests:\n");
    RUN_TEST(tokenizer_new_simple);
    RUN_TEST(tokenizer_new_empty);
    RUN_TEST(tokenizer_new_null);

    printf("\nBasic token tests:\n");
    RUN_TEST(tokenize_single_word);
    RUN_TEST(tokenize_multiple_words);
    RUN_TEST(tokenize_single_quoted_string);
    RUN_TEST(tokenize_double_quoted_string);
    RUN_TEST(tokenize_number);

    printf("\nOperator tests:\n");
    RUN_TEST(tokenize_semicolon);
    RUN_TEST(tokenize_pipe);
    RUN_TEST(tokenize_logical_and);
    RUN_TEST(tokenize_logical_or);
    RUN_TEST(tokenize_background);

    printf("\nRedirection tests:\n");
    RUN_TEST(tokenize_redirect_in);
    RUN_TEST(tokenize_redirect_out);
    RUN_TEST(tokenize_append);
    RUN_TEST(tokenize_heredoc);
    RUN_TEST(tokenize_herestring);
    RUN_TEST(tokenize_stderr_redirect);
    RUN_TEST(tokenize_redirect_both);

    printf("\nKeyword tests:\n");
    RUN_TEST(tokenize_if_then_else_fi);
    RUN_TEST(tokenize_for_in_do_done);
    RUN_TEST(tokenize_while_do_done);
    RUN_TEST(tokenize_case_esac);
    RUN_TEST(tokenize_until);
    RUN_TEST(tokenize_function);

    printf("\nDelimiter tests:\n");
    RUN_TEST(tokenize_parentheses);
    RUN_TEST(tokenize_braces);
    RUN_TEST(tokenize_double_parens);
    RUN_TEST(tokenize_double_brackets);

    printf("\nExtended syntax tests:\n");
    RUN_TEST(tokenize_process_sub_in);
    RUN_TEST(tokenize_process_sub_out);
    RUN_TEST(tokenize_pipe_stderr);
    RUN_TEST(tokenize_plus_assign);

    printf("\nUtility function tests:\n");
    RUN_TEST(token_type_name_coverage);
    RUN_TEST(token_is_keyword);
    RUN_TEST(token_is_operator);
    RUN_TEST(token_is_word_like);

    printf("\nTokenizer control tests:\n");
    RUN_TEST(tokenizer_peek);
    RUN_TEST(tokenizer_match);
    RUN_TEST(tokenizer_consume);
    RUN_TEST(tokenizer_disable_keywords);

    printf("\nEdge case tests:\n");
    RUN_TEST(tokenize_whitespace_only);
    RUN_TEST(tokenize_comment);
    RUN_TEST(tokenize_newline);
    RUN_TEST(tokenize_variable);
    RUN_TEST(tokenize_command_substitution);
    RUN_TEST(tokenize_arithmetic_expansion);
    RUN_TEST(tokenize_special_chars_in_word);
    RUN_TEST(tokenize_line_position_tracking);

    printf("\nRegression tests:\n");
    RUN_TEST(utf8_partial_sequence_at_eof_brace_expansion);

    printf("\n========================================\n");
    printf("All tokenizer tests PASSED!\n");
    printf("========================================\n");

    return 0;
}
