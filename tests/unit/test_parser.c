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

#include "node.h"
#include "parser.h"
#include "shell_error.h"
#include "test_shell_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// AST navigation helpers for structural assertions. The parsed result is a
/// forest: parser_parse returns the first top-level node, with further
/// statements chained through next_sibling, and each node's children chained
/// through first_child/next_sibling.

/// True if any node in the forest has the given type.
static bool tree_has_type(const node_t *root, node_type_t type) {
    for (const node_t *n = root; n; n = n->next_sibling) {
        if (n->type == type) {
            return true;
        }
        if (tree_has_type(n->first_child, type)) {
            return true;
        }
    }
    return false;
}

/// Count of nodes in the forest with the given type.
static size_t tree_count_type(const node_t *root, node_type_t type) {
    size_t k = 0;
    for (const node_t *n = root; n; n = n->next_sibling) {
        if (n->type == type) {
            k++;
        }
        k += tree_count_type(n->first_child, type);
    }
    return k;
}

/// First node in the forest with the given type, or NULL.
static const node_t *tree_find_type(const node_t *root, node_type_t type) {
    for (const node_t *n = root; n; n = n->next_sibling) {
        if (n->type == type) {
            return n;
        }
        const node_t *found = tree_find_type(n->first_child, type);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/// Assert the AST contains a redirection of `type` whose target filename
/// (the first child of the redirection node) equals `target`. Used by the
/// redirection tests to verify the parsed target, not merely that a
/// redirection node of the right kind exists.
static void assert_redir_target(const node_t *ast, node_type_t type,
                                const char *target) {
    const node_t *r = tree_find_type(ast, type);
    ASSERT_NOT_NULL(r, "redirection node present");
    ASSERT_NOT_NULL(r->first_child, "redirection has a target node");
    ASSERT_NOT_NULL(r->first_child->val.str, "target node carries a string");
    ASSERT_STR_EQ(r->first_child->val.str, target,
                  "redirection target filename matches");
}

/// Test framework macros

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
    parser_t *parser = parser_new_with_source("echo hello", "test.sh", 1);
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

    /// Root should be a command or command list
    ASSERT(ast->type == NODE_COMMAND || ast->type == NODE_COMMAND_LIST,
           "Root should be command or command list");

    /// The command word is the node's value; a bare "echo" has no arguments.
    const node_t *cmd = tree_find_type(ast, NODE_COMMAND);
    ASSERT_NOT_NULL(cmd, "command node present");
    ASSERT_STR_EQ(cmd->val.str, "echo", "command word should be echo");
    ASSERT_EQ(cmd->children, 0, "bare command has no argument children");

    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_with_args) {
    parser_t *parser = parser_new("echo hello world");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    /// The two arguments hang off the command node as ordered children.
    const node_t *cmd = tree_find_type(ast, NODE_COMMAND);
    ASSERT_NOT_NULL(cmd, "command node present");
    ASSERT_STR_EQ(cmd->val.str, "echo", "command word should be echo");
    ASSERT_EQ(cmd->children, 2, "two arguments parsed");
    ASSERT_NOT_NULL(cmd->first_child, "first argument node present");
    ASSERT_STR_EQ(cmd->first_child->val.str, "hello",
                  "first argument is hello");
    ASSERT_NOT_NULL(cmd->first_child->next_sibling, "second argument present");
    ASSERT_STR_EQ(cmd->first_child->next_sibling->val.str, "world",
                  "second argument is world");

    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_with_quoted_args) {
    parser_t *parser = parser_new("echo 'hello world' \"foo bar\"");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    /// Each quoted phrase is a single argument: the space inside the quotes
    /// must not split it, so there are exactly two arguments and the quotes
    /// are stripped from the stored values.
    const node_t *cmd = tree_find_type(ast, NODE_COMMAND);
    ASSERT_NOT_NULL(cmd, "command node present");
    ASSERT_STR_EQ(cmd->val.str, "echo", "command word should be echo");
    ASSERT_EQ(cmd->children, 2, "two quoted phrases parse as two arguments");
    ASSERT_STR_EQ(cmd->first_child->val.str, "hello world",
                  "single-quoted phrase kept intact, quotes stripped");
    ASSERT_STR_EQ(cmd->first_child->next_sibling->val.str, "foo bar",
                  "double-quoted phrase kept intact, quotes stripped");

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

    /// Should have a PIPE or PIPELINE node somewhere in tree
    ASSERT(tree_has_type(ast, NODE_PIPE), "pipe should parse to a NODE_PIPE");
    /// A two-stage pipeline connects exactly two commands.
    ASSERT_EQ(tree_count_type(ast, NODE_COMMAND), 2,
              "two-stage pipeline has two commands");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multi_pipe) {
    parser_t *parser = parser_new("cat file | grep pat | wc -l");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT_EQ(tree_count_type(ast, NODE_PIPE), 2,
              "three-stage pipeline has two pipe nodes");
    ASSERT_EQ(tree_count_type(ast, NODE_COMMAND), 3,
              "three-stage pipeline has three commands");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_pipe_stderr) {
    parser_t *parser = parser_new("cmd |& grep error");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_PIPE), "|& should parse to a NODE_PIPE");
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

    ASSERT_EQ(tree_count_type(ast, NODE_COMMAND), 3,
              "semicolon list has three commands");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_logical_and) {
    parser_t *parser = parser_new("test -f file && cat file");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    const node_t *op = tree_find_type(ast, NODE_LOGICAL_AND);
    ASSERT_NOT_NULL(op, "&& should parse to a NODE_LOGICAL_AND");
    /// The operator joins exactly two operands; left is `test`, right is `cat`.
    ASSERT_EQ(op->children, 2, "&& has two operands");
    ASSERT_STR_EQ(op->first_child->val.str, "test", "left operand is test");
    ASSERT_STR_EQ(op->first_child->next_sibling->val.str, "cat",
                  "right operand is cat");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_logical_or) {
    parser_t *parser = parser_new("test -f file || echo 'not found'");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    const node_t *op = tree_find_type(ast, NODE_LOGICAL_OR);
    ASSERT_NOT_NULL(op, "|| should parse to a NODE_LOGICAL_OR");
    /// Two operands; left is `test`, right is `echo`.
    ASSERT_EQ(op->children, 2, "|| has two operands");
    ASSERT_STR_EQ(op->first_child->val.str, "test", "left operand is test");
    ASSERT_STR_EQ(op->first_child->next_sibling->val.str, "echo",
                  "right operand is echo");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_background) {
    parser_t *parser = parser_new("sleep 10 &");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    const node_t *bg = tree_find_type(ast, NODE_BACKGROUND);
    ASSERT_NOT_NULL(bg, "& should parse to a NODE_BACKGROUND");
    /// The backgrounded job is its single command child.
    ASSERT_EQ(bg->children, 1, "background wraps one job");
    ASSERT_EQ(bg->first_child->type, NODE_COMMAND, "job is a command");
    ASSERT_STR_EQ(bg->first_child->val.str, "sleep",
                  "backgrounded command is sleep");
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

    ASSERT(tree_has_type(ast, NODE_REDIR_IN),
           "< should attach a NODE_REDIR_IN");
    assert_redir_target(ast, NODE_REDIR_IN, "file.txt");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_out) {
    parser_t *parser = parser_new("echo hello > output.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_OUT),
           "> should attach a NODE_REDIR_OUT");
    assert_redir_target(ast, NODE_REDIR_OUT, "output.txt");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_append) {
    parser_t *parser = parser_new("echo line >> log.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_APPEND),
           ">> should attach a NODE_REDIR_APPEND (not a truncate)");
    assert_redir_target(ast, NODE_REDIR_APPEND, "log.txt");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_stderr) {
    parser_t *parser = parser_new("cmd 2> /dev/null");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_ERR),
           "2> should attach a NODE_REDIR_ERR");
    assert_redir_target(ast, NODE_REDIR_ERR, "/dev/null");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_redirect_both) {
    parser_t *parser = parser_new("cmd &> output.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_BOTH),
           "&> should attach a NODE_REDIR_BOTH");
    assert_redir_target(ast, NODE_REDIR_BOTH, "output.txt");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_heredoc) {
    parser_t *parser = parser_new("cat << EOF\nhello\nEOF");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    const node_t *hd = tree_find_type(ast, NODE_REDIR_HEREDOC);
    ASSERT_NOT_NULL(hd, "<< should attach a NODE_REDIR_HEREDOC");
    /// A heredoc stores its delimiter as the node value and the body line(s)
    /// as children, unlike a file redirection where the child is the filename.
    ASSERT_STR_EQ(hd->val.str, "EOF", "heredoc delimiter is EOF");
    ASSERT_NOT_NULL(hd->first_child, "heredoc body present");
    /// The body line retains its terminating newline.
    ASSERT_STR_EQ(hd->first_child->val.str, "hello\n",
                  "heredoc body line is hello");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_herestring) {
    parser_t *parser = parser_new("cat <<< 'hello world'");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_HERESTRING),
           "<<< should attach a NODE_REDIR_HERESTRING");
    /// The here-string body is the quoted phrase with quotes stripped.
    assert_redir_target(ast, NODE_REDIR_HERESTRING, "hello world");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multiple_redirects) {
    parser_t *parser = parser_new("cmd < in.txt > out.txt 2> err.txt");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_REDIR_IN) &&
               tree_has_type(ast, NODE_REDIR_OUT) &&
               tree_has_type(ast, NODE_REDIR_ERR),
           "all three redirections should be attached");
    /// Each redirection keeps its own target; they must not be conflated.
    assert_redir_target(ast, NODE_REDIR_IN, "in.txt");
    assert_redir_target(ast, NODE_REDIR_OUT, "out.txt");
    assert_redir_target(ast, NODE_REDIR_ERR, "err.txt");
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

    ASSERT(tree_has_type(ast, NODE_IF), "should parse to a NODE_IF");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_if_then_else_fi) {
    parser_t *parser = parser_new("if true; then echo yes; else echo no; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    {
        const node_t *iff = tree_find_type(ast, NODE_IF);
        ASSERT_NOT_NULL((void *)iff, "should parse to a NODE_IF");
        ASSERT_EQ(iff->children, 3,
                  "if/else has condition, then, and else children");
    }
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_if_elif_else_fi) {
    parser_t *parser = parser_new(
        "if test1; then echo 1; elif test2; then echo 2; else echo 3; fi");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    {
        const node_t *iff = tree_find_type(ast, NODE_IF);
        ASSERT_NOT_NULL((void *)iff, "should parse to a NODE_IF");
        ASSERT_EQ(iff->children, 5,
                  "if/elif/else has cond, then, elif-cond, elif-then, else");
    }
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_for_loop) {
    parser_t *parser = parser_new("for i in 1 2 3; do echo $i; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_FOR), "should parse to a NODE_FOR");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_for_loop_no_in) {
    /// POSIX: for without 'in' iterates over positional params ($@)
    /// Issue #55 - FIXED: lush now supports this valid POSIX syntax
    parser_t *parser = parser_new("for arg; do echo $arg; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast,
                    "parser_parse should return AST for 'for var;' syntax");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_FOR), "should parse to a NODE_FOR");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_while_loop) {
    parser_t *parser = parser_new("while true; do echo loop; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_WHILE), "should parse to a NODE_WHILE");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_until_loop) {
    parser_t *parser = parser_new("until false; do echo loop; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_UNTIL),
           "until should parse to NODE_UNTIL, distinct from NODE_WHILE");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS -- Issue #45
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
    ASSERT(tree_has_type(ast, NODE_WHILE) &&
               tree_has_type(ast, NODE_LOGICAL_AND),
           "while condition should contain a logical-AND");
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
    ASSERT(tree_has_type(ast, NODE_WHILE) &&
               tree_has_type(ast, NODE_LOGICAL_OR),
           "while condition should contain a logical-OR");
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
    ASSERT(tree_has_type(ast, NODE_UNTIL) &&
               tree_has_type(ast, NODE_LOGICAL_AND),
           "until condition should contain a logical-AND");
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
    ASSERT(tree_has_type(ast, NODE_UNTIL) &&
               tree_has_type(ast, NODE_LOGICAL_OR),
           "until condition should contain a logical-OR");
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
    ASSERT(tree_has_type(ast, NODE_WHILE) &&
               tree_has_type(ast, NODE_LOGICAL_AND) &&
               tree_count_type(ast, NODE_ARITH_CMD) >= 2,
           "while cond is a logical-AND of two arithmetic commands");
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
    ASSERT(tree_has_type(ast, NODE_WHILE) &&
               tree_has_type(ast, NODE_LOGICAL_AND) &&
               tree_has_type(ast, NODE_BRACE_GROUP),
           "while cond logical-AND with a brace-group side");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_case) {
    parser_t *parser = parser_new("case $x in a) echo a;; b) echo b;; esac");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_CASE), "should parse to a NODE_CASE");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_case_with_patterns) {
    parser_t *parser =
        parser_new("case $x in [0-9]) echo num;; *) echo other;; esac");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_CASE) &&
               tree_count_type(ast, NODE_CASE_ITEM) >= 2,
           "case has at least two pattern clauses");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * FUNCTION TESTS
 * ============================================================================
 */

TEST(parse_function_keyword) {
    /// ksh/bash style: function name { body; }
    /// Issue #56 - FIXED: lush now supports this syntax
    parser_t *parser = parser_new("function foo { echo bar; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(
        ast, "parser_parse should return AST for 'function name { }' syntax");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_FUNCTION),
           "should parse to a NODE_FUNCTION");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_posix) {
    parser_t *parser = parser_new("foo() { echo bar; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_FUNCTION),
           "should parse to a NODE_FUNCTION");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS -- Issue #46
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_SUBSHELL),
           "function body should be a subshell");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) && tree_has_type(ast, NODE_IF),
           "function body should be an if");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) && tree_has_type(ast, NODE_WHILE),
           "function body should be a while");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) && tree_has_type(ast, NODE_FOR),
           "function body should be a for");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) && tree_has_type(ast, NODE_CASE),
           "function body should be a case");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_ARITH_CMD),
           "function body should be an arithmetic command");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_body_keyword_form_subshell) {
    parser_t *parser = parser_new("function f() ( echo body )");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(
        ast,
        "'function name() ( body )' (keyword form, subshell body) must parse");
    ASSERT(!parser_has_error(parser),
           "keyword form with subshell body must not produce parse error");
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_SUBSHELL),
           "function body should be a subshell");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * REGRESSION TESTS -- Issue #43
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_OUT),
           "function should carry a trailing output redirection");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_OUT),
           "function should carry a trailing output redirection");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_OUT),
           "function should carry a trailing output redirection");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_ERR),
           "function should carry a trailing stderr redirection");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_IN),
           "function should carry a trailing input redirection");
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
    ASSERT(tree_has_type(ast, NODE_FUNCTION) &&
               tree_has_type(ast, NODE_REDIR_APPEND),
           "function should carry a trailing append redirection");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_function_trailing_redir_subshell_body) {
    /// Cross-checks Issue #46 + #43: non-brace body + trailing redir.
    /// The redirection may end up attached to the subshell (consumed by
    /// parse_subshell's own trailing-redir call) rather than the function
    /// node; either is functionally equivalent at runtime. The test only
    /// asserts that the input parses cleanly.
    parser_t *parser = parser_new("f() ( echo x ) > /tmp/lush_test");
    ASSERT_NOT_NULL(parser, "parser_new failed");
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "function with subshell body + > redirect must parse");
    ASSERT(!parser_has_error(parser), "function with subshell body + > "
                                      "redirect must not produce parse error");
    ASSERT(tree_has_type(ast, NODE_REDIR_OUT),
           "a trailing output redirection should be present");
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

    ASSERT(tree_has_type(ast, NODE_SUBSHELL),
           "( ) should parse to a NODE_SUBSHELL");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_brace_group) {
    parser_t *parser = parser_new("{ echo hello; echo world; }");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_BRACE_GROUP),
           "{ } should parse to a NODE_BRACE_GROUP");
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

    ASSERT(tree_has_type(ast, NODE_ARITH_CMD),
           "(( )) should parse to a NODE_ARITH_CMD");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_extended_test) {
    parser_t *parser = parser_new("[[ -f file && -r file ]]");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_EXTENDED_TEST),
           "[[ ]] should parse to a NODE_EXTENDED_TEST");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_process_substitution_in) {
    parser_t *parser = parser_new("diff <(cat a) <(cat b)");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_PROC_SUB_IN),
           "<( ) should parse to a NODE_PROC_SUB_IN");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_process_substitution_out) {
    parser_t *parser = parser_new("tee >(cat > file)");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_PROC_SUB_OUT),
           ">( ) should parse to a NODE_PROC_SUB_OUT");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_command_substitution) {
    parser_t *parser = parser_new("echo $(pwd)");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_COMMAND_SUB),
           "$( ) should parse to a NODE_COMMAND_SUB");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_arithmetic_expansion) {
    parser_t *parser = parser_new("echo $((1 + 2 * 3))");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_ARITH_EXP),
           "$(( )) should parse to a NODE_ARITH_EXP");
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

    ASSERT(ast->type == NODE_COMMAND, "bare assignment parses to a command");
    ASSERT(ast->val_type == VAL_STR && ast->val.str != NULL,
           "command word should be set");
    ASSERT_STR_EQ(ast->val.str, "FOO=bar",
                  "assignment word should be preserved verbatim");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_multiple_assignments) {
    parser_t *parser = parser_new("A=1 B=2 C=3 cmd");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT_EQ(tree_count_type(ast, NODE_ASSIGN), 3,
              "three prefix assignments should be parsed");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_export_assignment) {
    parser_t *parser = parser_new("export FOO=bar");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(ast->type == NODE_COMMAND && ast->val_type == VAL_STR &&
               ast->val.str != NULL,
           "export parses to a command");
    ASSERT_STR_EQ(ast->val.str, "export", "command word should be export");
    ASSERT_NOT_NULL((void *)ast->first_child,
                    "export should carry an argument");
    ASSERT(ast->first_child->val_type == VAL_STR &&
               ast->first_child->val.str != NULL,
           "argument word should be set");
    ASSERT_STR_EQ(ast->first_child->val.str, "FOO=bar",
                  "argument should be the assignment FOO=bar");
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

    ASSERT(tree_count_type(ast, NODE_IF) >= 2,
           "nested if should produce two NODE_IF nodes");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_nested_loops) {
    parser_t *parser =
        parser_new("for i in 1 2; do for j in a b; do echo $i$j; done; done");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_count_type(ast, NODE_FOR) >= 2,
           "nested for should produce two NODE_FOR nodes");
    free_node_tree(ast);
    parser_free(parser);
}

TEST(parse_complex_pipeline) {
    parser_t *parser =
        parser_new("cat file | { grep pat; echo done; } | wc -l");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "parser_parse should return AST");
    ASSERT(!parser_has_error(parser), "Should not have parse error");

    ASSERT(tree_has_type(ast, NODE_PIPE) &&
               tree_has_type(ast, NODE_BRACE_GROUP),
           "pipeline stage should be a brace-group");
    free_node_tree(ast);
    parser_free(parser);
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

/// A malformed construct must yield no AST, flag parser_has_error, and
/// record a structured diagnostic in the error collector.
static void assert_parse_error(const char *src, const char *what) {
    parser_t *parser = parser_new(src);
    ASSERT_NOT_NULL(parser, "parser_new failed");

    node_t *ast = parser_parse(parser);
    ASSERT(ast == NULL, what);
    ASSERT(parser_has_error(parser), "error flag should be set");
    ASSERT(shell_error_collector_has_errors(parser_get_error_collector(parser)),
           "structured diagnostic should be recorded");

    if (ast) {
        free_node_tree(ast);
    }
    parser_free(parser);
}

TEST(parse_error_unclosed_if) {
    assert_parse_error("if true; then echo yes",
                       "unclosed if should not produce an AST");
}

TEST(parse_error_unclosed_quote) {
    assert_parse_error("echo 'unterminated",
                       "unterminated quote should not produce an AST");
}

TEST(parse_error_missing_done) {
    assert_parse_error("for i in 1 2; do echo $i",
                       "for without done should not produce an AST");
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
    ASSERT_NOT_NULL(ast, "Valid input should parse to an AST");
    ASSERT(!parser_has_error(parser), "Valid parse should not have error");
    ASSERT(
        !shell_error_collector_has_errors(parser_get_error_collector(parser)),
        "Valid parse should record no diagnostics");

    free_node_tree(ast);
    parser_free(parser);
}

TEST(parser_error_message_api) {
    /// The error collector is the diagnostic surface: empty before parsing,
    /// populated after a malformed parse, and reachable via the accessor.
    parser_t *parser = parser_new("if true; then");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    shell_error_collector_t *collector = parser_get_error_collector(parser);
    ASSERT_NOT_NULL(collector, "parser should expose an error collector");
    ASSERT(!shell_error_collector_has_errors(collector),
           "collector is empty before parsing");

    node_t *ast = parser_parse(parser);
    ASSERT(parser_has_error(parser), "malformed input should flag an error");
    ASSERT(shell_error_collector_has_errors(collector),
           "collector should hold the diagnostic after a failed parse");

    if (ast)
        free_node_tree(ast);
    parser_free(parser);
}

TEST(parser_set_source_name) {
    parser_t *parser = parser_new("echo hello");
    ASSERT_NOT_NULL(parser, "parser_new failed");

    parser_set_source_name(parser, "test_script.sh");
    ASSERT_NOT_NULL((void *)parser->source_name, "source name should be set");
    ASSERT_STR_EQ(parser->source_name, "test_script.sh",
                  "source name should round-trip through the setter");

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

    printf("\nRegression tests \xe2\x80\x94 Issue #45 (while/until logical "
           "conditions):\n");
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

    printf("\nRegression tests \xe2\x80\x94 Issue #46 (function compound "
           "bodies):\n");
    RUN_TEST(parse_function_body_subshell);
    RUN_TEST(parse_function_body_if);
    RUN_TEST(parse_function_body_while);
    RUN_TEST(parse_function_body_for);
    RUN_TEST(parse_function_body_case);
    RUN_TEST(parse_function_body_arith);
    RUN_TEST(parse_function_body_keyword_form_subshell);

    printf("\nRegression tests \xe2\x80\x94 Issue #43 (function trailing "
           "redirections):\n");
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

    return TEST_RESULT();
}
