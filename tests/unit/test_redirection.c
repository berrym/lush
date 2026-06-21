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

#include "executor.h"
#include "node.h"
#include "redirection.h"
#include "symtable.h"
#include "test_framework.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * FILE DESCRIPTOR SAVE/RESTORE TESTS
 * ============================================================================
 */

TEST(save_file_descriptors_basic) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    /// At least some FDs should be saved
    ASSERT(state.stdin_saved || state.stdout_saved || state.stderr_saved,
           "At least one FD should be saved");

    /// Clean up
    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stdin) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    /// stdin (fd 0) is open in the test process, so it must be recorded as
    /// saved and dup'd to a fresh descriptor distinct from fd 0 itself --
    /// not merely "valid if it happened to be saved".
    ASSERT(state.stdin_saved, "stdin should be recorded as saved");
    ASSERT(state.saved_stdin >= 0, "saved stdin fd should be valid");
    ASSERT(state.saved_stdin != STDIN_FILENO,
           "saved stdin should be a distinct dup, not fd 0");

    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stdout) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    ASSERT(state.stdout_saved, "stdout should be recorded as saved");
    ASSERT(state.saved_stdout >= 0, "saved stdout fd should be valid");
    ASSERT(state.saved_stdout != STDOUT_FILENO,
           "saved stdout should be a distinct dup, not fd 1");

    restore_file_descriptors(&state);
}

TEST(save_file_descriptors_stderr) {
    redirection_state_t state = {0};

    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "save_file_descriptors should succeed");

    ASSERT(state.stderr_saved, "stderr should be recorded as saved");
    ASSERT(state.saved_stderr >= 0, "saved stderr fd should be valid");
    ASSERT(state.saved_stderr != STDERR_FILENO,
           "saved stderr should be a distinct dup, not fd 2");

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

    /// Restore without save - should handle gracefully
    int result = restore_file_descriptors(&state);
    ASSERT_EQ(result, 0, "restore empty state should succeed");
}

TEST(save_restore_preserves_fds) {
    /// Identify the original stdout target so we can prove it is restored.
    struct stat before;
    ASSERT_EQ(fstat(STDOUT_FILENO, &before), 0, "stat stdout before save");

    redirection_state_t state = {0};
    ASSERT_EQ(save_file_descriptors(&state), 0, "save should succeed");
    ASSERT(state.stdout_saved, "stdout should be recorded as saved");
    ASSERT(state.saved_stdout >= 0, "saved stdout fd should be valid");

    /// Redirect stdout to a temp file (no assertions fire while redirected,
    /// so a longjmp on failure cannot leave stdout pointing at the temp fd).
    char tmpl[] = "/tmp/redir_test_XXXXXX";
    int tfd = mkstemp(tmpl);
    ASSERT(tfd >= 0, "temp file should be created");
    unlink(tmpl);
    dup2(tfd, STDOUT_FILENO);
    const char *marker = "redirected-output\n";
    ssize_t wrote = write(STDOUT_FILENO, marker, strlen(marker));

    int restored = restore_file_descriptors(&state);

    /// Back on the original stdout: assertions are safe again.
    ASSERT_EQ(restored, 0, "restore should succeed");
    ASSERT_EQ(wrote, (ssize_t)strlen(marker), "write while redirected");

    struct stat after;
    ASSERT_EQ(fstat(STDOUT_FILENO, &after), 0, "stat stdout after restore");
    ASSERT(before.st_dev == after.st_dev && before.st_ino == after.st_ino,
           "stdout should be restored to its original target");

    /// The bytes written while redirected landed in the temp file, not on
    /// the restored stdout.
    char buf[64] = {0};
    lseek(tfd, 0, SEEK_SET);
    ssize_t got = read(tfd, buf, sizeof(buf) - 1);
    ASSERT(got == (ssize_t)strlen(marker),
           "temp file holds the redirected bytes");
    ASSERT_STR_EQ(buf, marker,
                  "redirected output was captured by the temp file");
    close(tfd);
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

/// Capture exactly what redirection_error(message) writes to stderr into
/// `out`. stderr fd 2 is dup'd aside, pointed at a temp file for the call,
/// then restored before any assertion runs (so a longjmp on failure cannot
/// leave stderr pointing at the closed temp fd).
static void capture_redirection_error(const char *message, char *out,
                                      size_t outsz) {
    int saved = dup(STDERR_FILENO);
    char tmpl[] = "/tmp/redir_err_XXXXXX";
    int tfd = mkstemp(tmpl);
    unlink(tmpl);

    fflush(stderr);
    dup2(tfd, STDERR_FILENO);
    redirection_error(message);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);

    lseek(tfd, 0, SEEK_SET);
    ssize_t n = read(tfd, out, outsz - 1);
    out[n > 0 ? n : 0] = '\0';
    close(tfd);
}

TEST(redirection_error_basic) {
    /// redirection_error prefixes the message with "redirection: " and adds a
    /// trailing newline -- assert the exact rendered diagnostic, not just that
    /// the call returns.
    char out[256];
    capture_redirection_error("test error message", out, sizeof(out));
    ASSERT_STR_EQ(out, "redirection: test error message\n",
                  "renders the prefixed diagnostic");
}

TEST(redirection_error_null_message) {
    /// A NULL message is guarded: nothing is written to stderr at all.
    char out[256];
    capture_redirection_error(NULL, out, sizeof(out));
    ASSERT_STR_EQ(out, "", "NULL message produces no output");
}

/* ============================================================================
 * VARIABLE-ALLOCATED FD LIFECYCLE TESTS
 * ============================================================================
 *
 * These verify that `exec {var}<file` allocations are tracked by the
 * executor and reclaimed when executor_free runs — a script that opens
 * but never closes a {var}<file fd must not leak past shell exit.
 */

static bool fd_is_open(int fd) {
    struct stat st;
    return fstat(fd, &st) != -1;
}

TEST(alloc_fd_track_then_executor_free_closes) {
    executor_t *executor = executor_new();
    ASSERT_TRUE(executor != NULL, "executor_new");

    int fd = open("/etc/hosts", O_RDONLY);
    ASSERT_TRUE(fd >= 0, "open /etc/hosts");

    executor_track_alloc_fd(executor, fd);
    ASSERT_TRUE(fd_is_open(fd), "fd open before executor_free");

    executor_free(executor);
    ASSERT_FALSE(fd_is_open(fd), "fd closed after executor_free");
}

TEST(alloc_fd_untrack_then_executor_free_does_not_double_close) {
    executor_t *executor = executor_new();
    ASSERT_TRUE(executor != NULL, "executor_new");

    int fd = open("/etc/hosts", O_RDONLY);
    ASSERT_TRUE(fd >= 0, "open /etc/hosts");

    executor_track_alloc_fd(executor, fd);
    executor_untrack_alloc_fd(executor, fd);

    int rc = close(fd);
    ASSERT_EQ(rc, 0, "manual close after untrack succeeds");

    /// Open a fresh descriptor that the kernel will hand back the just-freed
    /// number for. Because the original fd was untracked, executor_free must
    /// not close that recycled number -- so the new descriptor stays open
    /// across the free. (Without the untrack, executor_free would close the
    /// recycled fd here, a real double-close-on-recycled-fd bug.)
    int recycled = open("/etc/hosts", O_RDONLY);
    ASSERT_TRUE(recycled >= 0, "reopen after close");

    executor_free(executor);

    ASSERT_TRUE(fd_is_open(recycled),
                "executor_free must not close the untracked, recycled fd");
    close(recycled);
}

TEST(alloc_fd_registry_grows_past_initial_capacity) {
    executor_t *executor = executor_new();
    ASSERT_TRUE(executor != NULL, "executor_new");

    /// Initial capacity is 8; allocate enough to force at least one realloc.
    int fds[24];
    size_t n = 0;
    for (; n < sizeof(fds) / sizeof(fds[0]); n++) {
        fds[n] = open("/etc/hosts", O_RDONLY);
        if (fds[n] < 0) {
            break;
        }
        executor_track_alloc_fd(executor, fds[n]);
    }
    ASSERT_TRUE(n > 8, "tracked more than initial capacity");

    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(fd_is_open(fds[i]), "fd open before cleanup");
    }

    executor_free(executor);

    for (size_t i = 0; i < n; i++) {
        ASSERT_FALSE(fd_is_open(fds[i]),
                     "fd closed by executor_free after grow");
    }
}

TEST(alloc_fd_untrack_unknown_is_noop) {
    executor_t *executor = executor_new();
    ASSERT_TRUE(executor != NULL, "executor_new");

    /// Track one real fd, then untrack descriptors that were never tracked
    /// (an out-of-range number and an invalid negative). Those untracks must
    /// be no-ops that leave the genuinely-tracked fd in the registry, so
    /// executor_free still closes it. This proves untrack does not silently
    /// corrupt or clear the registry when handed an unknown fd.
    int fd = open("/etc/hosts", O_RDONLY);
    ASSERT_TRUE(fd >= 0, "open /etc/hosts");
    executor_track_alloc_fd(executor, fd);

    executor_untrack_alloc_fd(executor, 999);
    executor_untrack_alloc_fd(executor, -1);
    ASSERT_TRUE(fd_is_open(fd), "tracked fd survives unknown untracks");

    executor_free(executor);
    ASSERT_FALSE(fd_is_open(fd),
                 "executor_free still closes the genuinely tracked fd");
}

/* ============================================================================
 * COMPLEX REDIRECTION SCENARIOS
 * ============================================================================
 */

TEST(complex_command_with_redirections) {
    /// Simulate: cmd arg1 > out.txt 2> err.txt < in.txt
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

    /// This is tricky - we don't want to actually close stdin
    /// Just verify the API handles various states
    int result = save_file_descriptors(&state);
    ASSERT_EQ(result, 0, "Should handle current FD state");

    restore_file_descriptors(&state);
}

TEST(state_initialization) {
    redirection_state_t state;
    memset(&state, 0xFF, sizeof(state)); /// Fill with garbage

    /// Manually initialize
    state.saved_stdin = -1;
    state.saved_stdout = -1;
    state.saved_stderr = -1;
    state.stdin_saved = false;
    state.stdout_saved = false;
    state.stderr_saved = false;

    /// Should be able to save now
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

    /// executor_new() pulls from the global symbol table manager; tests
    /// exercising the executor need it initialized.
    init_symtable();

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

    printf("\nVariable-Allocated FD Lifecycle:\n");
    RUN_TEST(alloc_fd_track_then_executor_free_closes);
    RUN_TEST(alloc_fd_untrack_then_executor_free_does_not_double_close);
    RUN_TEST(alloc_fd_registry_grows_past_initial_capacity);
    RUN_TEST(alloc_fd_untrack_unknown_is_noop);

    return TEST_RESULT();
}
