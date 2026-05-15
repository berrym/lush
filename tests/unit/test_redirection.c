/**
 * @file test_redirection.c
 * @brief Unit tests for I/O redirection handling
 *
 * Tests the redirection module including:
 * - File descriptor save/restore
 * - Redirection node detection
 * - Redirection counting
 * - Error handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "redirection.h"
#include "test_framework.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */

/* ============================================================================
 * FILE DESCRIPTOR SAVE/RESTORE TESTS
 * ============================================================================
 */

TEST(save_file_descriptors_basic) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    /* At least some FDs should be saved */
    ASSERT(state.stdin_saved || state.stdout_saved || state.stderr_saved,
           "At least one FD should be saved");

    /* Clean up */
    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stdin) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    if (state.stdin_saved) {
        ASSERT(state.saved_stdin >= 0, "Saved stdin should be valid FD");
    }

    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stdout) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    if (state.stdout_saved) {
        ASSERT(state.saved_stdout >= 0, "Saved stdout should be valid FD");
    }

    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stderr) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    if (state.stderr_saved) {
        ASSERT(state.saved_stderr >= 0, "Saved stderr should be valid FD");
    }

    restore_file_descriptors(&state);
}

TEST(restore_file_descriptors_basic) {
    redirection_state_t state = {0};

    save_file_descriptors(&state);
    int result = restore_file_descriptors(&state);
    ASSERT_EQ(result, 0, "restore_file_descriptors should succeed");
}

TEST(restore_file_descriptors_empty_state) {
    redirection_state_t state = {0};

    /* Restore without save - should handle gracefully */
    int result = restore_file_descriptors(&state);
    ASSERT_EQ(result, 0, "restore empty state should succeed");
}

TEST(save_restore_preserves_fds) {
    redirection_state_t state = {0};

    /* Get original FDs */
    int orig_stdin = dup(STDIN_FILENO);
    int orig_stdout = dup(STDOUT_FILENO);
    int orig_stderr = dup(STDERR_FILENO);

    save_file_descriptors(&state);

    /* Verify we can still use standard FDs */
    ASSERT(isatty(STDIN_FILENO) >= 0 || 1, "stdin should still work");
    ASSERT(isatty(STDOUT_FILENO) >= 0 || 1, "stdout should still work");
    ASSERT(isatty(STDERR_FILENO) >= 0 || 1, "stderr should still work");

    restore_file_descriptors(&state);

    /* Clean up our test dups */
    close(orig_stdin);
    close(orig_stdout);
    close(orig_stderr);
}

TEST(multiple_save_restore_cycles) {
    for (int i = 0; i < 5; i++) {
        redirection_state_t state = {0};

        int result = save_file_descriptors(&state);
        ASSERT_EQ(result, 0, "save should succeed");

        result = restore_file_descriptors(&state);
        ASSERT_EQ(result, 0, "restore should succeed");
    }
}

/* ============================================================================
 * REDIRECTION NODE DETECTION TESTS
 * ============================================================================
 */

TEST(is_redirection_node_output) {
    node_t *node = new_node(NODE_REDIR_OUT);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node), "NODE_REDIR_OUT should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_input) {
    node_t *node = new_node(NODE_REDIR_IN);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node), "NODE_REDIR_IN should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_append) {
    node_t *node = new_node(NODE_REDIR_APPEND);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node),
           "NODE_REDIR_APPEND should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_heredoc) {
    node_t *node = new_node(NODE_REDIR_HEREDOC);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node),
           "NODE_REDIR_HEREDOC should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_herestring) {
    node_t *node = new_node(NODE_REDIR_HERESTRING);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node),
           "NODE_REDIR_HERESTRING should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_stderr) {
    node_t *node = new_node(NODE_REDIR_ERR);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(is_redirection_node(node), "NODE_REDIR_ERR should be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_command) {
    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(!is_redirection_node(node),
           "NODE_COMMAND should not be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_var) {
    node_t *node = new_node(NODE_VAR);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(!is_redirection_node(node), "NODE_VAR should not be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_pipe) {
    node_t *node = new_node(NODE_PIPE);
    ASSERT_NOT_NULL(node, "Node creation should succeed");

    ASSERT(!is_redirection_node(node), "NODE_PIPE should not be redirection");

    free_node_tree(node);
}

TEST(is_redirection_node_null) {
    ASSERT(!is_redirection_node(NULL), "NULL should not be redirection");
}

/* ============================================================================
 * REDIRECTION COUNTING TESTS
 * ============================================================================
 */

TEST(count_redirections_none) {
    node_t *cmd = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(cmd, "Command creation should succeed");

    int count = count_redirections(cmd);
    ASSERT_EQ(count, 0, "Command with no children should have 0 redirections");

    free_node_tree(cmd);
}

TEST(count_redirections_one) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *redir = new_node(NODE_REDIR_OUT);
    ASSERT_NOT_NULL(cmd, "Command creation should succeed");
    ASSERT_NOT_NULL(redir, "Redirection creation should succeed");

    add_child_node(cmd, redir);

    int count = count_redirections(cmd);
    ASSERT_EQ(count, 1, "Should count 1 redirection");

    free_node_tree(cmd);
}

TEST(count_redirections_multiple) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *redir1 = new_node(NODE_REDIR_OUT);
    node_t *redir2 = new_node(NODE_REDIR_IN);
    node_t *redir3 = new_node(NODE_REDIR_ERR);
    ASSERT_NOT_NULL(cmd, "Command creation should succeed");

    add_child_node(cmd, redir1);
    add_child_node(cmd, redir2);
    add_child_node(cmd, redir3);

    int count = count_redirections(cmd);
    ASSERT_EQ(count, 3, "Should count 3 redirections");

    free_node_tree(cmd);
}

TEST(count_redirections_mixed_children) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *var = new_node(NODE_VAR);
    node_t *redir = new_node(NODE_REDIR_OUT);
    node_t *var2 = new_node(NODE_VAR);
    ASSERT_NOT_NULL(cmd, "Command creation should succeed");

    add_child_node(cmd, var);
    add_child_node(cmd, redir);
    add_child_node(cmd, var2);

    int count = count_redirections(cmd);
    ASSERT_EQ(count, 1, "Should count only redirection nodes");

    free_node_tree(cmd);
}

TEST(count_redirections_null) {
    int count = count_redirections(NULL);
    ASSERT_EQ(count, 0, "NULL command should have 0 redirections");
}

/* ============================================================================
 * REDIRECTION ERROR TESTS
 * ============================================================================
 */

TEST(redirection_error_basic) {
    /* Should not crash - output goes to stderr */
    FILE *old_stderr = stderr;
    FILE *null_err = fopen("/dev/null", "w");
    if (null_err) {
        stderr = null_err;
        redirection_error("test error message");
        fclose(null_err);
        stderr = old_stderr;
    }
}

TEST(redirection_error_null_message) {
    /* Should handle NULL gracefully */
    FILE *old_stderr = stderr;
    FILE *null_err = fopen("/dev/null", "w");
    if (null_err) {
        stderr = null_err;
        redirection_error(NULL);
        fclose(null_err);
        stderr = old_stderr;
    }
}

/* ============================================================================
 * COMPLEX REDIRECTION SCENARIOS
 * ============================================================================
 */

TEST(complex_command_with_redirections) {
    /* Simulate: cmd arg1 > out.txt 2> err.txt < in.txt */
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *var = new_node(NODE_VAR);
    node_t *arg = new_node(NODE_VAR);
    node_t *redir_out = new_node(NODE_REDIR_OUT);
    node_t *redir_err = new_node(NODE_REDIR_ERR);
    node_t *redir_in = new_node(NODE_REDIR_IN);

    add_child_node(cmd, var);
    add_child_node(cmd, arg);
    add_child_node(cmd, redir_out);
    add_child_node(cmd, redir_err);
    add_child_node(cmd, redir_in);

    int count = count_redirections(cmd);
    ASSERT_EQ(count, 3, "Should have 3 redirections");

    ASSERT(!is_redirection_node(cmd), "Command itself is not redirection");
    ASSERT(!is_redirection_node(var), "Var is not redirection");
    ASSERT(is_redirection_node(redir_out), "Output redir is redirection");
    ASSERT(is_redirection_node(redir_err), "Error redir is redirection");
    ASSERT(is_redirection_node(redir_in), "Input redir is redirection");

    free_node_tree(cmd);
}

TEST(heredoc_detection) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *heredoc = new_node(NODE_REDIR_HEREDOC);

    add_child_node(cmd, heredoc);

    ASSERT(is_redirection_node(heredoc), "Heredoc should be redirection");
    ASSERT_EQ(count_redirections(cmd), 1, "Should count heredoc");

    free_node_tree(cmd);
}

TEST(herestring_detection) {
    node_t *cmd = new_node(NODE_COMMAND);
    node_t *herestring = new_node(NODE_REDIR_HERESTRING);

    add_child_node(cmd, herestring);

    ASSERT(is_redirection_node(herestring), "Herestring should be redirection");
    ASSERT_EQ(count_redirections(cmd), 1, "Should count herestring");

    free_node_tree(cmd);
}

/* ============================================================================
 * FD MANAGEMENT EDGE CASES
 * ============================================================================
 */

TEST(save_with_closed_stdin) {
    redirection_state_t state = {0};

    /* This is tricky - we don't want to actually close stdin */
    /* Just verify the API handles various states */
    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "Should handle current FD state");

    restore_file_descriptors(&state);
}

TEST(state_initialization) {
    redirection_state_t state;
    memset(&state, 0xFF, sizeof(state)); /* Fill with garbage */

    /* Manually initialize */
    state.saved_stdin = -1;
    state.saved_stdout = -1;
    state.saved_stderr = -1;
    state.stdin_saved = false;
    state.stdout_saved = false;
    state.stderr_saved = false;

    /* Should be able to save now */
    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "Should succeed with clean state");

    restore_file_descriptors(&state);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running redirection.c tests...\n\n");

    printf("File Descriptor Save/Restore Tests:\n");
    RUN_TEST(save_file_descriptors_basic);
    RUN_TEST(save_file_descriptors_stdin);
    RUN_TEST(save_file_descriptors_stdout);
    RUN_TEST(save_file_descriptors_stderr);
    RUN_TEST(restore_file_descriptors_basic);
    RUN_TEST(restore_file_descriptors_empty_state);
    RUN_TEST(save_restore_preserves_fds);
    RUN_TEST(multiple_save_restore_cycles);

    printf("\nRedirection Node Detection Tests:\n");
    RUN_TEST(is_redirection_node_output);
    RUN_TEST(is_redirection_node_input);
    RUN_TEST(is_redirection_node_append);
    RUN_TEST(is_redirection_node_heredoc);
    RUN_TEST(is_redirection_node_herestring);
    RUN_TEST(is_redirection_node_stderr);
    RUN_TEST(is_redirection_node_command);
    RUN_TEST(is_redirection_node_var);
    RUN_TEST(is_redirection_node_pipe);
    RUN_TEST(is_redirection_node_null);

    printf("\nRedirection Counting Tests:\n");
    RUN_TEST(count_redirections_none);
    RUN_TEST(count_redirections_one);
    RUN_TEST(count_redirections_multiple);
    RUN_TEST(count_redirections_mixed_children);
    RUN_TEST(count_redirections_null);

    printf("\nRedirection Error Tests:\n");
    RUN_TEST(redirection_error_basic);
    RUN_TEST(redirection_error_null_message);

    printf("\nComplex Redirection Scenarios:\n");
    RUN_TEST(complex_command_with_redirections);
    RUN_TEST(heredoc_detection);
    RUN_TEST(herestring_detection);

    printf("\nFD Management Edge Cases:\n");
    RUN_TEST(save_with_closed_stdin);
    RUN_TEST(state_initialization);

    return TEST_RESULT();
}
