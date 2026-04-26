/**
 * @file test_node_to_source.c
 * @brief Unit tests for AST to source code regeneration
 *
 * Tests the node_to_source module including:
 * - Basic node to source conversion
 * - Node equality comparison
 * - Complex AST structures
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "node_to_source.h"
#include "parser.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */

/* ============================================================================
 * NODE TO SOURCE BASIC TESTS
 * ============================================================================
 */

TEST(node_to_source_null) {
    char *source = node_to_source(NULL);
    /* Implementation returns empty string for NULL, not NULL */
    ASSERT_NOT_NULL(source,
                    "node_to_source should return empty string for NULL");
    ASSERT_STR_EQ(source, "", "NULL node should produce empty string");
    free(source);
}

TEST(node_to_source_var) {
    node_t *var = new_node(NODE_VAR);
    ASSERT_NOT_NULL(var, "Node creation should succeed");
    set_node_val_str(var, "hello");

    char *source = node_to_source(var);
    ASSERT_NOT_NULL(source, "node_to_source should return non-NULL");
    ASSERT(strstr(source, "hello") != NULL, "Source should contain 'hello'");

    free(source);
    free_node_tree(var);
}

TEST(node_to_source_command) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *word1 = new_node(NODE_VAR);
    node_t *word2 = new_node(NODE_VAR);

    set_node_val_str(word1, "echo");
    set_node_val_str(word2, "hello");

    add_child_node(cmd, word1);
    add_child_node(cmd, word2);

    char *source = node_to_source(cmd);
    ASSERT_NOT_NULL(source, "node_to_source should return non-NULL");
    ASSERT(strstr(source, "echo") != NULL, "Source should contain 'echo'");
    ASSERT(strstr(source, "hello") != NULL, "Source should contain 'hello'");

    free(source);
    free_node_tree(cmd);
}

TEST(node_to_source_pipe) {
    node_t *pipe = new_node(NODE_PIPE);
    node_t *cmd1 = new_node(NODE_COMMAND);
    node_t *cmd2 = new_node(NODE_COMMAND);
    node_t *word1 = new_node(NODE_VAR);
    node_t *word2 = new_node(NODE_VAR);

    set_node_val_str(word1, "ls");
    set_node_val_str(word2, "grep");

    add_child_node(cmd1, word1);
    add_child_node(cmd2, word2);
    add_child_node(pipe, cmd1);
    add_child_node(pipe, cmd2);

    char *source = node_to_source(pipe);
    ASSERT_NOT_NULL(source, "node_to_source should return non-NULL");
    ASSERT(strstr(source, "ls") != NULL, "Source should contain 'ls'");
    ASSERT(strstr(source, "grep") != NULL, "Source should contain 'grep'");
    ASSERT(strstr(source, "|") != NULL, "Source should contain pipe");

    free(source);
    free_node_tree(pipe);
}

/* ============================================================================
 * NODE EQUALS TESTS
 * ============================================================================
 */

TEST(node_equals_null) {
    ASSERT_EQ(node_equals(NULL, NULL), 1, "NULL equals NULL");

    node_t *node = new_node(NODE_VAR);
    ASSERT_EQ(node_equals(node, NULL), 0, "Non-NULL does not equal NULL");
    ASSERT_EQ(node_equals(NULL, node), 0, "NULL does not equal non-NULL");
    free_node_tree(node);
}

TEST(node_equals_same_type) {
    node_t *a = new_node(NODE_VAR);
    node_t *b = new_node(NODE_VAR);

    set_node_val_str(a, "test");
    set_node_val_str(b, "test");

    ASSERT_EQ(node_equals(a, b), 1, "Same type and value should be equal");

    free_node_tree(a);
    free_node_tree(b);
}

TEST(node_equals_different_type) {
    node_t *a = new_node(NODE_VAR);
    node_t *b = new_node(NODE_COMMAND);

    ASSERT_EQ(node_equals(a, b), 0, "Different types should not be equal");

    free_node_tree(a);
    free_node_tree(b);
}

TEST(node_equals_different_value) {
    node_t *a = new_node(NODE_VAR);
    node_t *b = new_node(NODE_VAR);

    set_node_val_str(a, "hello");
    set_node_val_str(b, "world");

    ASSERT_EQ(node_equals(a, b), 0, "Different values should not be equal");

    free_node_tree(a);
    free_node_tree(b);
}

TEST(node_equals_with_children) {
    node_t *a = new_node(NODE_COMMAND);
    node_t *b = new_node(NODE_COMMAND);
    node_t *child_a = new_node(NODE_VAR);
    node_t *child_b = new_node(NODE_VAR);

    set_node_val_str(child_a, "echo");
    set_node_val_str(child_b, "echo");

    add_child_node(a, child_a);
    add_child_node(b, child_b);

    ASSERT_EQ(node_equals(a, b), 1, "Same structure should be equal");

    free_node_tree(a);
    free_node_tree(b);
}

TEST(node_equals_different_children_count) {
    node_t *a = new_node(NODE_COMMAND);
    node_t *b = new_node(NODE_COMMAND);
    node_t *child1 = new_node(NODE_VAR);
    node_t *child2 = new_node(NODE_VAR);
    node_t *child3 = new_node(NODE_VAR);

    set_node_val_str(child1, "echo");
    set_node_val_str(child2, "echo");
    set_node_val_str(child3, "hello");

    add_child_node(a, child1);
    add_child_node(b, child2);
    add_child_node(b, child3);

    ASSERT_EQ(node_equals(a, b), 0,
              "Different child count should not be equal");

    free_node_tree(a);
    free_node_tree(b);
}

/* ============================================================================
 * ROUND-TRIP TESTS
 * ============================================================================
 */

TEST(round_trip_simple_command) {
    const char *input = "echo hello";
    parser_t *parser = parser_new(input);
    ASSERT_NOT_NULL(parser, "Parser creation should succeed");

    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    /* Parse the regenerated source */
    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);
    ASSERT_NOT_NULL(ast2, "Re-parsing should succeed");

    /* ASTs should be structurally equal */
    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

TEST(round_trip_with_arguments) {
    const char *input = "ls -la /home";
    parser_t *parser = parser_new(input);
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);

    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

TEST(round_trip_pipeline) {
    const char *input = "ls | grep foo";
    parser_t *parser = parser_new(input);
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);

    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

TEST(round_trip_if_statement) {
    const char *input = "if true; then echo yes; fi";
    parser_t *parser = parser_new(input);
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);

    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

TEST(round_trip_for_loop) {
    const char *input = "for i in 1 2 3; do echo $i; done";
    parser_t *parser = parser_new(input);
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);

    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

TEST(round_trip_while_loop) {
    const char *input = "while true; do sleep 1; done";
    parser_t *parser = parser_new(input);
    node_t *ast = parser_parse(parser);
    ASSERT_NOT_NULL(ast, "Parsing should succeed");

    char *source = node_to_source(ast);
    ASSERT_NOT_NULL(source, "node_to_source should succeed");

    parser_t *parser2 = parser_new(source);
    node_t *ast2 = parser_parse(parser2);

    ASSERT_EQ(node_equals(ast, ast2), 1, "Round-trip AST should be equal");

    free(source);
    free_node_tree(ast);
    free_node_tree(ast2);
    parser_free(parser);
    parser_free(parser2);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running node_to_source.c tests...\n\n");

    printf("Node to Source Basic Tests:\n");
    RUN_TEST(node_to_source_null);
    RUN_TEST(node_to_source_var);
    RUN_TEST(node_to_source_command);
    RUN_TEST(node_to_source_pipe);

    printf("\nNode Equals Tests:\n");
    RUN_TEST(node_equals_null);
    RUN_TEST(node_equals_same_type);
    RUN_TEST(node_equals_different_type);
    RUN_TEST(node_equals_different_value);
    RUN_TEST(node_equals_with_children);
    RUN_TEST(node_equals_different_children_count);

    printf("\nRound-Trip Tests:\n");
    RUN_TEST(round_trip_simple_command);
    RUN_TEST(round_trip_with_arguments);
    RUN_TEST(round_trip_pipeline);
    RUN_TEST(round_trip_if_statement);
    RUN_TEST(round_trip_for_loop);
    RUN_TEST(round_trip_while_loop);

    return TEST_RESULT();
}
