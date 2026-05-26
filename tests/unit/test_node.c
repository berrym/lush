/**
 * @file test_node.c
 * @brief Unit tests for AST node operations
 *
 * Tests the AST node system including:
 * - Node creation and initialization
 * - Child/sibling relationships
 * - Tree structure and traversal
 * - Memory management
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * NODE CREATION TESTS
 * ============================================================================
 */

TEST(new_node_command) {
    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should return non-NULL");
    ASSERT_EQ(node->type, NODE_COMMAND, "Node type should be NODE_COMMAND");
    ASSERT_EQ(node->children, 0, "New node should have 0 children");
    ASSERT_NULL(node->first_child, "New node should have NULL first_child");
    ASSERT_NULL(node->next_sibling, "New node should have NULL next_sibling");
    ASSERT_NULL(node->prev_sibling, "New node should have NULL prev_sibling");
    free_node_tree(node);
}

TEST(new_node_various_types) {
    /// Test creation of various node types
    node_type_t types[] = {
        NODE_COMMAND,     NODE_PIPE,        NODE_IF,          NODE_FOR,
        NODE_WHILE,       NODE_CASE,        NODE_FUNCTION,    NODE_SUBSHELL,
        NODE_BRACE_GROUP, NODE_LOGICAL_AND, NODE_LOGICAL_OR,  NODE_BACKGROUND,
        NODE_REDIR_IN,    NODE_REDIR_OUT,   NODE_REDIR_APPEND};
    size_t num_types = sizeof(types) / sizeof(types[0]);

    for (size_t i = 0; i < num_types; i++) {
        node_t *node = new_node(types[i]);
        ASSERT_NOT_NULL(node, "new_node should succeed for all types");
        ASSERT_EQ(node->type, types[i], "Node type mismatch");
        free_node_tree(node);
    }
}

TEST(new_node_at_with_location) {
    source_location_t loc = {
        .filename = "test.sh", .line = 10, .column = 5, .offset = 100};

    node_t *node = new_node_at(NODE_COMMAND, loc);
    ASSERT_NOT_NULL(node, "new_node_at should return non-NULL");
    ASSERT_EQ(node->type, NODE_COMMAND, "Node type should be NODE_COMMAND");
    ASSERT_EQ(node->loc.line, 10, "Line number should be preserved");
    ASSERT_EQ(node->loc.column, 5, "Column should be preserved");

    free_node_tree(node);
}

/* ============================================================================
 * CHILD NODE TESTS
 * ============================================================================
 */

TEST(add_single_child) {
    node_t *parent = new_node(NODE_COMMAND);
    node_t *child = new_node(NODE_VAR);

    ASSERT_NOT_NULL(parent, "Parent creation failed");
    ASSERT_NOT_NULL(child, "Child creation failed");

    add_child_node(parent, child);

    ASSERT_EQ(parent->children, 1, "Parent should have 1 child");
    ASSERT(parent->first_child == child, "first_child should point to child");
    ASSERT_NULL(child->next_sibling,
                "Single child should have no next sibling");
    ASSERT_NULL(child->prev_sibling,
                "Single child should have no prev sibling");

    free_node_tree(parent);
}

TEST(add_multiple_children) {
    node_t *parent = new_node(NODE_COMMAND);
    node_t *child1 = new_node(NODE_VAR);
    node_t *child2 = new_node(NODE_STRING_LITERAL);
    node_t *child3 = new_node(NODE_VAR);

    add_child_node(parent, child1);
    add_child_node(parent, child2);
    add_child_node(parent, child3);

    ASSERT_EQ(parent->children, 3, "Parent should have 3 children");
    ASSERT(parent->first_child == child1, "first_child should be child1");

    /// Check sibling chain
    ASSERT(child1->next_sibling == child2, "child1->next should be child2");
    ASSERT(child2->next_sibling == child3, "child2->next should be child3");
    ASSERT_NULL(child3->next_sibling, "child3 should have no next sibling");

    /// Check prev siblings
    ASSERT_NULL(child1->prev_sibling, "child1 should have no prev sibling");
    ASSERT(child2->prev_sibling == child1, "child2->prev should be child1");
    ASSERT(child3->prev_sibling == child2, "child3->prev should be child2");

    free_node_tree(parent);
}

TEST(nested_children) {
    /* Create a tree structure:
     *     parent
     *       |
     *     child1
     *       |
     *   grandchild
     */
    node_t *parent = new_node(NODE_IF);
    node_t *child1 = new_node(NODE_COMMAND);
    node_t *grandchild = new_node(NODE_VAR);

    add_child_node(child1, grandchild);
    add_child_node(parent, child1);

    ASSERT_EQ(parent->children, 1, "Parent should have 1 child");
    ASSERT_EQ(child1->children, 1, "Child should have 1 grandchild");
    ASSERT(child1->first_child == grandchild,
           "Grandchild should be child's first_child");

    free_node_tree(parent); /// Should free all three nodes
}

/* ============================================================================
 * NODE VALUE TESTS
 * ============================================================================
 */

TEST(set_node_val_str_basic) {
    node_t *node = new_node(NODE_VAR);
    ASSERT_NOT_NULL(node, "Node creation failed");

    /// set_node_val_str takes ownership of the string
    char *value = strdup("test_value");
    set_node_val_str(node, value);

    ASSERT_EQ(node->val_type, VAL_STR, "Value type should be VAL_STR");
    ASSERT_STR_EQ(node->val.str, "test_value", "String value mismatch");

    free_node_tree(node); /// Should free the string too
}

TEST(set_node_val_str_overwrite) {
    node_t *node = new_node(NODE_VAR);

    set_node_val_str(node, strdup("first"));
    set_node_val_str(node, strdup("second"));

    ASSERT_STR_EQ(node->val.str, "second", "Value should be overwritten");

    free_node_tree(node);
}

/* ============================================================================
 * TREE STRUCTURE TESTS
 * ============================================================================
 */

TEST(pipeline_structure) {
    /// Create a pipeline: cmd1 | cmd2 | cmd3
    node_t *pipeline = new_node(NODE_PIPELINE);
    node_t *cmd1 = new_node(NODE_COMMAND);
    node_t *cmd2 = new_node(NODE_COMMAND);
    node_t *cmd3 = new_node(NODE_COMMAND);

    set_node_val_str(cmd1, strdup("cmd1"));
    set_node_val_str(cmd2, strdup("cmd2"));
    set_node_val_str(cmd3, strdup("cmd3"));

    add_child_node(pipeline, cmd1);
    add_child_node(pipeline, cmd2);
    add_child_node(pipeline, cmd3);

    ASSERT_EQ(pipeline->children, 3, "Pipeline should have 3 commands");
    ASSERT(pipeline->first_child == cmd1, "First command should be cmd1");
    ASSERT(cmd1->next_sibling == cmd2, "cmd2 follows cmd1");
    ASSERT(cmd2->next_sibling == cmd3, "cmd3 follows cmd2");

    free_node_tree(pipeline);
}

TEST(if_statement_structure) {
    /* Create an if statement structure:
     *     IF
     *    / | \
     * cond then else
     */
    node_t *if_node = new_node(NODE_IF);
    node_t *condition = new_node(NODE_COMMAND);
    node_t *then_branch = new_node(NODE_COMMAND);
    node_t *else_branch = new_node(NODE_COMMAND);

    add_child_node(if_node, condition);
    add_child_node(if_node, then_branch);
    add_child_node(if_node, else_branch);

    ASSERT_EQ(if_node->children, 3, "IF should have 3 children");
    ASSERT(if_node->first_child == condition, "First child is condition");
    ASSERT(condition->next_sibling == then_branch, "Then follows condition");
    ASSERT(then_branch->next_sibling == else_branch, "Else follows then");

    free_node_tree(if_node);
}

TEST(for_loop_structure) {
    /* Create a for loop structure:
     *     FOR
     *    / | \
     * var list body
     */
    node_t *for_node = new_node(NODE_FOR);
    node_t *var = new_node(NODE_VAR);
    node_t *list = new_node(NODE_COMMAND); /// Word list
    node_t *body = new_node(NODE_COMMAND);

    set_node_val_str(var, strdup("i"));

    add_child_node(for_node, var);
    add_child_node(for_node, list);
    add_child_node(for_node, body);

    ASSERT_EQ(for_node->children, 3, "FOR should have 3 children");
    ASSERT_STR_EQ(var->val.str, "i", "Loop variable should be 'i'");

    free_node_tree(for_node);
}

TEST(command_with_redirections) {
    /* Command with redirections:
     *     COMMAND
     *    /   |   \
     * arg1 arg2 redir
     */
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *arg1 = new_node(NODE_VAR);
    node_t *arg2 = new_node(NODE_STRING_LITERAL);
    node_t *redir = new_node(NODE_REDIR_OUT);

    set_node_val_str(arg1, strdup("echo"));
    set_node_val_str(arg2, strdup("hello"));
    set_node_val_str(redir, strdup("output.txt"));

    add_child_node(cmd, arg1);
    add_child_node(cmd, arg2);
    add_child_node(cmd, redir);

    ASSERT_EQ(cmd->children, 3, "Command should have 3 children");
    ASSERT_EQ(redir->type, NODE_REDIR_OUT, "Third child should be redirection");

    free_node_tree(cmd);
}

/* ============================================================================
 * MEMORY MANAGEMENT TESTS
 * ============================================================================
 */

TEST(free_node_tree_null) {
    /// Should handle NULL gracefully
    free_node_tree(NULL);
    /// If we get here without crashing, test passed
}

TEST(free_node_tree_single) {
    node_t *node = new_node(NODE_COMMAND);
    free_node_tree(node);
    /// Memory freed successfully if no crash
}

TEST(free_node_tree_with_value) {
    node_t *node = new_node(NODE_VAR);
    set_node_val_str(node, strdup("test_string"));
    free_node_tree(node);
    /// String should be freed along with node
}

TEST(free_node_tree_deep) {
    /// Create a deep tree and free it
    node_t *root = new_node(NODE_COMMAND_LIST);
    node_t *current = root;

    for (int i = 0; i < 10; i++) {
        node_t *child = new_node(NODE_COMMAND);
        add_child_node(current, child);
        current = child;
    }

    free_node_tree(root); /// Should free all 11 nodes
}

TEST(free_node_tree_wide) {
    /// Create a wide tree (many siblings)
    node_t *root = new_node(NODE_COMMAND_LIST);

    for (int i = 0; i < 20; i++) {
        node_t *child = new_node(NODE_COMMAND);
        char buf[16];
        snprintf(buf, sizeof(buf), "cmd%d", i);
        set_node_val_str(child, strdup(buf));
        add_child_node(root, child);
    }

    ASSERT_EQ(root->children, 20, "Should have 20 children");
    free_node_tree(root); /// Should free all 21 nodes
}

/* ============================================================================
 * EXTENDED NODE TYPES TESTS
 * ============================================================================
 */

TEST(arithmetic_nodes) {
    node_t *arith_cmd = new_node(NODE_ARITH_CMD);
    node_t *arith_exp = new_node(NODE_ARITH_EXP);

    ASSERT_NOT_NULL(arith_cmd, "ARITH_CMD creation failed");
    ASSERT_NOT_NULL(arith_exp, "ARITH_EXP creation failed");
    ASSERT_EQ(arith_cmd->type, NODE_ARITH_CMD, "Type mismatch");
    ASSERT_EQ(arith_exp->type, NODE_ARITH_EXP, "Type mismatch");

    free_node_tree(arith_cmd);
    free_node_tree(arith_exp);
}

TEST(array_nodes) {
    node_t *array_lit = new_node(NODE_ARRAY_LITERAL);
    node_t *array_acc = new_node(NODE_ARRAY_ACCESS);
    node_t *array_assign = new_node(NODE_ARRAY_ASSIGN);

    ASSERT_NOT_NULL(array_lit, "ARRAY_LITERAL creation failed");
    ASSERT_NOT_NULL(array_acc, "ARRAY_ACCESS creation failed");
    ASSERT_NOT_NULL(array_assign, "ARRAY_ASSIGN creation failed");

    free_node_tree(array_lit);
    free_node_tree(array_acc);
    free_node_tree(array_assign);
}

TEST(process_substitution_nodes) {
    node_t *proc_in = new_node(NODE_PROC_SUB_IN);
    node_t *proc_out = new_node(NODE_PROC_SUB_OUT);

    ASSERT_NOT_NULL(proc_in, "PROC_SUB_IN creation failed");
    ASSERT_NOT_NULL(proc_out, "PROC_SUB_OUT creation failed");
    ASSERT_EQ(proc_in->type, NODE_PROC_SUB_IN, "Type mismatch");
    ASSERT_EQ(proc_out->type, NODE_PROC_SUB_OUT, "Type mismatch");

    free_node_tree(proc_in);
    free_node_tree(proc_out);
}

TEST(extended_test_node) {
    node_t *ext_test = new_node(NODE_EXTENDED_TEST);
    ASSERT_NOT_NULL(ext_test, "EXTENDED_TEST creation failed");
    ASSERT_EQ(ext_test->type, NODE_EXTENDED_TEST, "Type mismatch");
    free_node_tree(ext_test);
}

TEST(redirection_node_types) {
    node_type_t redir_types[] = {
        NODE_REDIR_IN,   NODE_REDIR_OUT,     NODE_REDIR_APPEND,
        NODE_REDIR_ERR,  NODE_REDIR_HEREDOC, NODE_REDIR_HERESTRING,
        NODE_REDIR_BOTH, NODE_REDIR_FD,      NODE_REDIR_CLOBBER};
    size_t num_types = sizeof(redir_types) / sizeof(redir_types[0]);

    for (size_t i = 0; i < num_types; i++) {
        node_t *node = new_node(redir_types[i]);
        ASSERT_NOT_NULL(node, "Redirection node creation failed");
        ASSERT_EQ(node->type, redir_types[i], "Type mismatch");
        free_node_tree(node);
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running AST node unit tests...\n\n");

    printf("Node creation tests:\n");
    RUN_TEST(new_node_command);
    RUN_TEST(new_node_various_types);
    RUN_TEST(new_node_at_with_location);

    printf("\nChild node tests:\n");
    RUN_TEST(add_single_child);
    RUN_TEST(add_multiple_children);
    RUN_TEST(nested_children);

    printf("\nNode value tests:\n");
    RUN_TEST(set_node_val_str_basic);
    RUN_TEST(set_node_val_str_overwrite);

    printf("\nTree structure tests:\n");
    RUN_TEST(pipeline_structure);
    RUN_TEST(if_statement_structure);
    RUN_TEST(for_loop_structure);
    RUN_TEST(command_with_redirections);

    printf("\nMemory management tests:\n");
    RUN_TEST(free_node_tree_null);
    RUN_TEST(free_node_tree_single);
    RUN_TEST(free_node_tree_with_value);
    RUN_TEST(free_node_tree_deep);
    RUN_TEST(free_node_tree_wide);

    printf("\nExtended node types tests:\n");
    RUN_TEST(arithmetic_nodes);
    RUN_TEST(array_nodes);
    RUN_TEST(process_substitution_nodes);
    RUN_TEST(extended_test_node);
    RUN_TEST(redirection_node_types);

    printf("\n========================================\n");
    printf("All AST node tests PASSED!\n");
    printf("========================================\n");

    return 0;
}
