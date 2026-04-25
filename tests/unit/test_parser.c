/**
 * @file test_parser.c
 * @brief Unit tests for shell parser
 *
 * Tests the recursive descent parser including:
 * - Simple commands and arguments
 * - Pipelines and command lists
 * - Control structures (if, for, while, case)
 * - Functions and redirections
 * - Error handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "parser.h"
#include "node.h"
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
 * LIFECYCLE TESTS
 * ============================================================================
 */

TEST(parser_new_simple) {
    parser_t *parser = parser_new("echo hello");
    ASSERT_NOT_NULL(parser, "parser_new should return non-NULL");
    ASSERT(!parser_has_error(parser), "New parser should not have error");
    parser_free(parser);
}

TEST(parser_new_empty) {
    parser_t *parser = parser_new("");
    ASSERT_NOT_NULL(parser, "parser_new with empty string should succeed");
    parser_free(parser);
}

TEST(parser_new_with_source) {
    parser_t *parser = parser_new_with_source("echo hello", "test.sh");
    ASSERT_NOT_NULL(parser, "parser_new_with_source should succeed");
    parser_free(parser);
}

/* ============================================================================
 * SIMPLE COMMAND TESTS
 * ============================================================================
 */

TEST(parse_simple_command) {
    parser_t *parser = parser_new("echo");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    /* Root should be a command or command list */
    ASSERT(ast->type == NODE_COMMAND || ast->type == NODE_COMMAND_LIST,
           "Root should be command or command list");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_with_args) {
    parser_t *parser = parser_new("echo hello world");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_with_quoted_args) {
    parser_t *parser = parser_new("echo 'hello world' \"foo bar\"");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * PIPELINE TESTS
 * ============================================================================
 */

TEST(parse_simple_pipe) {
    parser_t *parser = parser_new("cat file | grep pattern");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    /* Should have a PIPE or PIPELINE node somewhere in tree */
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multi_pipe) {
    parser_t *parser = parser_new("cat file | grep pat | wc -l");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_pipe_stderr) {
    parser_t *parser = parser_new("cmd |& grep error");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * COMMAND LIST TESTS
 * ============================================================================
 */

TEST(parse_semicolon_list) {
    parser_t *parser = parser_new("echo a; echo b; echo c");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_logical_and) {
    parser_t *parser = parser_new("test -f file && cat file");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_logical_or) {
    parser_t *parser = parser_new("test -f file || echo 'not found'");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_background) {
    parser_t *parser = parser_new("sleep 10 &");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REDIRECTION TESTS
 * ============================================================================
 */

TEST(parse_redirect_in) {
    parser_t *parser = parser_new("cat < file.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_out) {
    parser_t *parser = parser_new("echo hello > output.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_append) {
    parser_t *parser = parser_new("echo line >> log.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_stderr) {
    parser_t *parser = parser_new("cmd 2> /dev/null");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_both) {
    parser_t *parser = parser_new("cmd &> output.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_heredoc) {
    parser_t *parser = parser_new("cat << EOF\nhello\nEOF");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_herestring) {
    parser_t *parser = parser_new("cat <<< 'hello world'");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multiple_redirects) {
    parser_t *parser = parser_new("cmd < in.txt > out.txt 2> err.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(parse_if_then_fi) {
    parser_t *parser = parser_new("if true; then echo yes; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_if_then_else_fi) {
    parser_t *parser = parser_new("if true; then echo yes; else echo no; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_if_elif_else_fi) {
    parser_t *parser = parser_new("if test1; then echo 1; elif test2; then echo 2; else echo 3; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_for_loop) {
    parser_t *parser = parser_new("for i in 1 2 3; do echo $i; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_for_loop_no_in) {
    /* POSIX: for without 'in' iterates over positional params ($@)
     * Issue #55 - FIXED: lush now supports this valid POSIX syntax */
    parser_t *parser = parser_new("for arg; do echo $arg; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST for 'for var;' syntax");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_while_loop) {
    parser_t *parser = parser_new("while true; do echo loop; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_until_loop) {
    parser_t *parser = parser_new("until false; do echo loop; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS — Issue #45
 * `while` and `until` must accept logical-expression conditions
 * (&& and ||) as bash and zsh do. Until the fix lands, every test
 * below fails with `expected 'DO', got 'LOGICAL_AND'/'LOGICAL_OR'`.
 * ============================================================================
 */

TEST(parse_while_logical_and_simple) {
    parser_t *parser = parser_new(
        "i=0; while [ $i -lt 2 ] && true; do echo $i; i=$((i+1)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "while + && condition must parse");
    ASSERT(!parser_has_error(parser),
           "while + && condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_while_logical_or_simple) {
    parser_t *parser = parser_new(
        "i=0; while [ $i -lt 2 ] || false; do echo $i; i=$((i+1)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "while + || condition must parse");
    ASSERT(!parser_has_error(parser),
           "while + || condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_until_logical_and_simple) {
    parser_t *parser = parser_new(
        "i=2; until [ $i -le 0 ] && false; do echo $i; i=$((i-1)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "until + && condition must parse");
    ASSERT(!parser_has_error(parser),
           "until + && condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_until_logical_or_simple) {
    parser_t *parser = parser_new(
        "i=2; until [ $i -le 0 ] || false; do echo $i; i=$((i-1)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "until + || condition must parse");
    ASSERT(!parser_has_error(parser),
           "until + || condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_while_logical_arith_compound) {
    parser_t *parser = parser_new(
        "i=0; while ((i < 2)) && ((i >= 0)); do echo $i; ((i++)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "while + (( )) && (( )) condition must parse");
    ASSERT(!parser_has_error(parser),
           "while + (( )) && (( )) condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_while_logical_brace_group) {
    parser_t *parser = parser_new(
        "i=0; while { [ $i -lt 2 ]; } && true; do echo $i; i=$((i+1)); done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "while + brace-group + && condition must parse");
    ASSERT(!parser_has_error(parser),
           "while + brace-group + && condition must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_case) {
    parser_t *parser = parser_new("case $x in a) echo a;; b) echo b;; esac");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_case_with_patterns) {
    parser_t *parser = parser_new("case $x in [0-9]) echo num;; *) echo other;; esac");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * FUNCTION TESTS
 * ============================================================================
 */

TEST(parse_function_keyword) {
    /* ksh/bash style: function name { body; }
     * Issue #56 - FIXED: lush now supports this syntax */
    parser_t *parser = parser_new("function foo { echo bar; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST for 'function name { }' syntax");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_posix) {
    parser_t *parser = parser_new("foo() { echo bar; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS — Issue #46
 * Function bodies must accept any compound command, not only brace groups.
 * Bash and zsh both accept subshell, if, while, for, case, arithmetic,
 * and extended-test bodies. Until the fix lands, every test below fails
 * with `expected 'LBRACE', got '...'`.
 * ============================================================================
 */

TEST(parse_function_body_subshell) {
    parser_t *parser = parser_new("f() ( echo subshell-body )");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with subshell body must parse");
    ASSERT(!parser_has_error(parser),
           "function with subshell body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_if) {
    parser_t *parser = parser_new("f() if true; then echo yes; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with if body must parse");
    ASSERT(!parser_has_error(parser),
           "function with if body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_while) {
    parser_t *parser = parser_new("f() while false; do echo never; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with while body must parse");
    ASSERT(!parser_has_error(parser),
           "function with while body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_for) {
    parser_t *parser = parser_new("f() for i in a b; do echo $i; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with for body must parse");
    ASSERT(!parser_has_error(parser),
           "function with for body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_case) {
    parser_t *parser = parser_new("f() case x in x) echo match;; esac");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with case body must parse");
    ASSERT(!parser_has_error(parser),
           "function with case body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_arith) {
    parser_t *parser = parser_new("f() (( 1 + 1 ))");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with arithmetic body must parse");
    ASSERT(!parser_has_error(parser),
           "function with arithmetic body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_keyword_form_subshell) {
    parser_t *parser = parser_new("function f() ( echo body )");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast,
        "'function name() ( body )' (keyword form, subshell body) must parse");
    ASSERT(!parser_has_error(parser),
        "keyword form with subshell body must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS — Issue #43
 * Function definitions must accept trailing redirections after the body,
 * matching bash and zsh. Until the fix lands, every test below fails with
 * `expected command name, got '>'/'<'/'2>'/etc.` because the parser leaves
 * the redirection token at command position and tries to parse a new
 * statement.
 * ============================================================================
 */

TEST(parse_function_trailing_redir_out) {
    parser_t *parser = parser_new("f() { echo x; } > /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "POSIX form with > redirect must parse");
    ASSERT(!parser_has_error(parser),
           "POSIX form with > redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_keyword_form) {
    parser_t *parser = parser_new("function f { echo x; } > /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "keyword form with > redirect must parse");
    ASSERT(!parser_has_error(parser),
           "keyword form with > redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_keyword_parens_form) {
    parser_t *parser = parser_new("function f() { echo x; } > /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "keyword+parens form with > redirect must parse");
    ASSERT(!parser_has_error(parser),
           "keyword+parens form with > redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_stderr) {
    parser_t *parser = parser_new("f() { echo x; } 2> /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with 2> redirect must parse");
    ASSERT(!parser_has_error(parser),
           "function with 2> redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_in) {
    parser_t *parser = parser_new("f() { read line; } < /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with < redirect must parse");
    ASSERT(!parser_has_error(parser),
           "function with < redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_append) {
    parser_t *parser = parser_new("f() { echo x; } >> /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with >> redirect must parse");
    ASSERT(!parser_has_error(parser),
           "function with >> redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_subshell_body) {
    /* Cross-checks Issue #46 + #43: non-brace body + trailing redir.
     * The redirection may end up attached to the subshell (consumed by
     * parse_subshell's own trailing-redir call) rather than the function
     * node; either is functionally equivalent at runtime. The test only
     * asserts that the input parses cleanly. */
    parser_t *parser = parser_new("f() ( echo x ) > /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with subshell body + > redirect must parse");
    ASSERT(!parser_has_error(parser),
           "function with subshell body + > redirect must not produce parse error");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * GROUPING TESTS
 * ============================================================================
 */

TEST(parse_subshell) {
    parser_t *parser = parser_new("(echo hello; echo world)");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_brace_group) {
    parser_t *parser = parser_new("{ echo hello; echo world; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * EXTENDED SYNTAX TESTS
 * ============================================================================
 */

TEST(parse_arithmetic_command) {
    parser_t *parser = parser_new("(( x = 1 + 2 ))");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_extended_test) {
    parser_t *parser = parser_new("[[ -f file && -r file ]]");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_process_substitution_in) {
    parser_t *parser = parser_new("diff <(cat a) <(cat b)");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_process_substitution_out) {
    parser_t *parser = parser_new("tee >(cat > file)");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_substitution) {
    parser_t *parser = parser_new("echo $(pwd)");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_arithmetic_expansion) {
    parser_t *parser = parser_new("echo $((1 + 2 * 3))");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * VARIABLE TESTS
 * ============================================================================
 */

TEST(parse_variable_assignment) {
    parser_t *parser = parser_new("FOO=bar");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multiple_assignments) {
    parser_t *parser = parser_new("A=1 B=2 C=3 cmd");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_export_assignment) {
    parser_t *parser = parser_new("export FOO=bar");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * NESTED STRUCTURE TESTS
 * ============================================================================
 */

TEST(parse_nested_if) {
    parser_t *parser = parser_new("if a; then if b; then echo c; fi; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_nested_loops) {
    parser_t *parser = parser_new("for i in 1 2; do for j in a b; do echo $i$j; done; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_complex_pipeline) {
    parser_t *parser = parser_new("cat file | { grep pat; echo done; } | wc -l");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");
    
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(parse_error_unclosed_if) {
    parser_t *parser = parser_new("if true; then echo yes");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    /* Should either return NULL or have error flag set */
    if (ast) {
        free_node_tree(ast);
    }
    /* Note: Some parsers may allow incomplete input for interactive use */
    parser_free(parser);
}

TEST(parse_error_unclosed_quote) {
    parser_t *parser = parser_new("echo 'unterminated");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    if (ast) {
        free_node_tree(ast);
    }
    parser_free(parser);
}

TEST(parse_error_missing_done) {
    parser_t *parser = parser_new("for i in 1 2; do echo $i");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    node_t *ast = parser_parse(parser);
    if (ast) {
        free_node_tree(ast);
    }
    parser_free(parser);
}

/* ============================================================================
 * PARSER API TESTS
 * ============================================================================
 */

TEST(parser_has_error_api) {
    parser_t *parser = parser_new("echo hello");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    ASSERT(!parser_has_error(parser), "New parser should not have error");
    
    node_t *ast = parser_parse(parser);
    ASSERT(!parser_has_error(parser), "Valid parse should not have error");
    
    if (ast) free_node_tree(ast);
    parser_free(parser);
}

TEST(parser_error_message_api) {
    parser_t *parser = parser_new("echo hello");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    /* Valid input should have NULL or empty error */
    node_t *ast = parser_parse(parser);
    const char *err = parser_error(parser);
    /* Error message may be NULL or empty string for success */
    (void)err;
    
    if (ast) free_node_tree(ast);
    parser_free(parser);
}

TEST(parser_set_source_name) {
    parser_t *parser = parser_new("echo hello");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    
    parser_set_source_name(parser, "test_script.sh");
    /* Should not crash */
    
    parser_free(parser);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running parser unit tests...\n\n");
    
    printf("Lifecycle tests:\n");
    RUN_TEST(parser_new_simple);
    RUN_TEST(parser_new_empty);
    RUN_TEST(parser_new_with_source);
    
    printf("\nSimple command tests:\n");
    RUN_TEST(parse_simple_command);
    RUN_TEST(parse_command_with_args);
    RUN_TEST(parse_command_with_quoted_args);
    
    printf("\nPipeline tests:\n");
    RUN_TEST(parse_simple_pipe);
    RUN_TEST(parse_multi_pipe);
    RUN_TEST(parse_pipe_stderr);
    
    printf("\nCommand list tests:\n");
    RUN_TEST(parse_semicolon_list);
    RUN_TEST(parse_logical_and);
    RUN_TEST(parse_logical_or);
    RUN_TEST(parse_background);
    
    printf("\nRedirection tests:\n");
    RUN_TEST(parse_redirect_in);
    RUN_TEST(parse_redirect_out);
    RUN_TEST(parse_redirect_append);
    RUN_TEST(parse_redirect_stderr);
    RUN_TEST(parse_redirect_both);
    RUN_TEST(parse_heredoc);
    RUN_TEST(parse_herestring);
    RUN_TEST(parse_multiple_redirects);
    
    printf("\nControl structure tests:\n");
    RUN_TEST(parse_if_then_fi);
    RUN_TEST(parse_if_then_else_fi);
    RUN_TEST(parse_if_elif_else_fi);
    RUN_TEST(parse_for_loop);
    RUN_TEST(parse_for_loop_no_in);
    RUN_TEST(parse_while_loop);
    RUN_TEST(parse_until_loop);

    printf("\nRegression tests — Issue #45 (while/until logical conditions):\n");
    RUN_TEST(parse_while_logical_and_simple);
    RUN_TEST(parse_while_logical_or_simple);
    RUN_TEST(parse_until_logical_and_simple);
    RUN_TEST(parse_until_logical_or_simple);
    RUN_TEST(parse_while_logical_arith_compound);
    RUN_TEST(parse_while_logical_brace_group);

    RUN_TEST(parse_case);
    RUN_TEST(parse_case_with_patterns);
    
    printf("\nFunction tests:\n");
    RUN_TEST(parse_function_keyword);
    RUN_TEST(parse_function_posix);

    printf("\nRegression tests — Issue #46 (function compound bodies):\n");
    RUN_TEST(parse_function_body_subshell);
    RUN_TEST(parse_function_body_if);
    RUN_TEST(parse_function_body_while);
    RUN_TEST(parse_function_body_for);
    RUN_TEST(parse_function_body_case);
    RUN_TEST(parse_function_body_arith);
    RUN_TEST(parse_function_body_keyword_form_subshell);

    printf("\nRegression tests — Issue #43 (function trailing redirections):\n");
    RUN_TEST(parse_function_trailing_redir_out);
    RUN_TEST(parse_function_trailing_redir_keyword_form);
    RUN_TEST(parse_function_trailing_redir_keyword_parens_form);
    RUN_TEST(parse_function_trailing_redir_stderr);
    RUN_TEST(parse_function_trailing_redir_in);
    RUN_TEST(parse_function_trailing_redir_append);
    RUN_TEST(parse_function_trailing_redir_subshell_body);
    
    printf("\nGrouping tests:\n");
    RUN_TEST(parse_subshell);
    RUN_TEST(parse_brace_group);
    
    printf("\nExtended syntax tests:\n");
    RUN_TEST(parse_arithmetic_command);
    RUN_TEST(parse_extended_test);
    RUN_TEST(parse_process_substitution_in);
    RUN_TEST(parse_process_substitution_out);
    RUN_TEST(parse_command_substitution);
    RUN_TEST(parse_arithmetic_expansion);
    
    printf("\nVariable tests:\n");
    RUN_TEST(parse_variable_assignment);
    RUN_TEST(parse_multiple_assignments);
    RUN_TEST(parse_export_assignment);
    
    printf("\nNested structure tests:\n");
    RUN_TEST(parse_nested_if);
    RUN_TEST(parse_nested_loops);
    RUN_TEST(parse_complex_pipeline);
    
    printf("\nError handling tests:\n");
    RUN_TEST(parse_error_unclosed_if);
    RUN_TEST(parse_error_unclosed_quote);
    RUN_TEST(parse_error_missing_done);
    
    printf("\nParser API tests:\n");
    RUN_TEST(parser_has_error_api);
    RUN_TEST(parser_error_message_api);
    RUN_TEST(parser_set_source_name);
    
    printf("\n========================================\n");
    printf("All parser tests PASSED!\n");
    printf("========================================\n");
    
    return 0;
}
