/**
 * @file test_shell_harness.h
 * @brief Shared shell-driving fixtures for lush unit tests
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Sits on top of test_framework.h. Provides three families of helpers
 * that almost every shell test reinvents by hand:
 *
 *   1. Output-capturing executor invocation. run_shell() runs a shell
 *      string through an in-process executor with stdout and stderr
 *      redirected to temp files, then returns exit status plus
 *      captured streams in a single struct. This unlocks behavioral
 *      assertions like ASSERT_STDOUT_EQ(result, "hello\n") that the
 *      pre-existing tests could not express.
 *
 *   2. Subprocess executor invocation. run_shell_subprocess() forks a
 *      fresh lush child via -c and pipes its stdio. Required for any
 *      test that exercises a code path that mutates or exits the
 *      hosting process (exit, signals, exec, traps).
 *
 *   3. Parser plus AST inspection. parse_shell() returns a heap AST
 *      from a source string; node_child_count(), node_child(), and
 *      node_first_arg() replace the hand-rolled first_child /
 *      next_sibling walks scattered through test_parser.c.
 *
 * Why header-only: matches test_framework.h. Each test binary inlines
 * the helpers it actually uses; unused statics are stripped by the
 * compiler. No separate library to link.
 *
 * Why temp files instead of pipes for in-process capture: a write to
 * a pipe whose reader has not drained will block once the kernel pipe
 * buffer fills (typically 64 KB). The capturing process is also the
 * writer here, so a blocking write would deadlock. Temp files have no
 * such limit; data is read back after the captured call returns. The
 * captured-output buffers are bounded at 4 KB each — tests producing
 * more should split, page, or escalate to a subprocess capture.
 */

#ifndef LUSH_TEST_SHELL_HARNESS_H
#define LUSH_TEST_SHELL_HARNESS_H

#include "executor.h"
#include "lush_fork.h"
#include "node.h"
#include "parser.h"
#include "test_framework.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ============================================================================
 * Result type
 *
 * Field names avoid `stdout`/`stderr` to keep <stdio.h>'s macros from
 * being expanded over them.
 * ============================================================================
 */

#define LUSH_HARNESS_BUF_SIZE 4096

typedef struct {
    int exit_status;
    char out[LUSH_HARNESS_BUF_SIZE];
    char err[LUSH_HARNESS_BUF_SIZE];
} run_result_t;

/* ============================================================================
 * In-process execution with output capture
 * ============================================================================
 */

/**
 * @brief Run a shell string through @p exec, capturing stdout and stderr
 *
 * Saves the calling process's stdout/stderr file descriptors, swings
 * them onto a pair of temp files, runs the executor, then restores
 * the original descriptors and reads back the captured bytes. The
 * returned result_t.out and .err are NUL-terminated and bounded at
 * LUSH_HARNESS_BUF_SIZE - 1 bytes; longer output is silently truncated
 * (the test should split or use the subprocess variant if that matters).
 *
 * @param exec Executor to drive (caller owns lifetime)
 * @param src Shell source string (no trailing newline required)
 * @return Exit status plus captured stdout and stderr
 */
static inline run_result_t run_shell_with_executor(executor_t *exec,
                                                   const char *src) {
    run_result_t r = {0};

    FILE *out_tmp = tmpfile();
    FILE *err_tmp = tmpfile();
    if (!out_tmp || !err_tmp) {
        /// Fall back to uncaptured execution rather than failing the test;
        /// the caller's exit-status assertion will still run.
        r.exit_status = executor_execute_command_line(exec, src, 1);
        if (out_tmp) {
            fclose(out_tmp);
        }
        if (err_tmp) {
            fclose(err_tmp);
        }
        return r;
    }

    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);

    fflush(stdout);
    fflush(stderr);
    dup2(fileno(out_tmp), STDOUT_FILENO);
    dup2(fileno(err_tmp), STDERR_FILENO);

    r.exit_status = executor_execute_command_line(exec, src, 1);

    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);

    rewind(out_tmp);
    rewind(err_tmp);
    size_t n_out = fread(r.out, 1, sizeof(r.out) - 1, out_tmp);
    r.out[n_out] = '\0';
    size_t n_err = fread(r.err, 1, sizeof(r.err) - 1, err_tmp);
    r.err[n_err] = '\0';

    fclose(out_tmp);
    fclose(err_tmp);

    return r;
}

/**
 * @brief Run a shell string through a one-shot in-process executor
 *
 * Convenience wrapper: creates a fresh executor, runs the source,
 * disposes the executor, returns the captured result.
 *
 * @param src Shell source string
 * @return Exit status plus captured stdout and stderr
 */
static inline run_result_t run_shell(const char *src) {
    executor_t *exec = executor_new();
    if (!exec) {
        run_result_t r = {0};
        r.exit_status = -1;
        return r;
    }
    run_result_t r = run_shell_with_executor(exec, src);
    executor_free(exec);
    return r;
}

/* ============================================================================
 * Subprocess execution
 *
 * Use these when the test needs a fresh process state — exit, signals,
 * trap firing, or anything else the in-process executor short-circuits.
 * ============================================================================
 */

/**
 * @brief Locate the lush binary for subprocess execution
 *
 * Honors the LUSH_TEST_BINARY environment variable so meson and CI
 * can point at an out-of-tree build. Falls back to ./build/lush which
 * matches the project's documented build directory layout.
 *
 * @return NUL-terminated path string (statically allocated, do not free)
 */
static inline const char *lush_test_binary_path(void) {
    const char *env = getenv("LUSH_TEST_BINARY");
    return env && env[0] ? env : "./build/lush";
}

/**
 * @brief Read up to @p cap-1 bytes from @p fd into @p buf, NUL-terminate
 *
 * Drains the descriptor in a tight loop until EOF or the buffer fills.
 * Bytes beyond @p cap-1 are discarded after a non-blocking probe so
 * the child does not deadlock waiting for a reader.
 */
static inline void lush_harness_drain_fd(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n < cap - 1) {
        ssize_t r = read(fd, buf + n, cap - 1 - n);
        if (r <= 0) {
            break;
        }
        n += (size_t)r;
    }
    buf[n] = '\0';
    // Drain anything remaining so the child's writes do not block.
    char scratch[1024];
    while (read(fd, scratch, sizeof(scratch)) > 0) {
        // discard
    }
}

/**
 * @brief Run a shell string in a forked lush subprocess via -c
 *
 * Pipes the child's stdout and stderr back into the result struct.
 * Returns the child's exit status (WEXITSTATUS) on normal exit, or
 * 128 + signal number on signal termination, matching shell exit
 * conventions.
 *
 * @param src Shell source string passed to lush -c
 * @return Exit status plus captured stdout and stderr
 */
static inline run_result_t run_shell_subprocess(const char *src) {
    run_result_t r = {0};
    int out_pipe[2], err_pipe[2];

    if (pipe(out_pipe) != 0) {
        r.exit_status = -1;
        return r;
    }
    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        r.exit_status = -1;
        return r;
    }

    /// Use the project's fork wrapper so any pending stdio buffer
    /// content is flushed before the child inherits it — otherwise
    /// captured output may include duplicated bytes from the parent's
    /// pre-fork stdio state.
    pid_t pid = lush_fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        r.exit_status = -1;
        return r;
    }

    if (pid == 0) {
        // Child: rewire stdio to the pipe write ends, exec lush -c.
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);

        const char *bin = lush_test_binary_path();
        execl(bin, bin, "-c", src, (char *)NULL);
        _exit(127); // exec failed
    }

    // Parent: close write ends, drain read ends, reap child.
    close(out_pipe[1]);
    close(err_pipe[1]);
    lush_harness_drain_fd(out_pipe[0], r.out, sizeof(r.out));
    lush_harness_drain_fd(err_pipe[0], r.err, sizeof(r.err));
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        r.exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.exit_status = 128 + WTERMSIG(status);
    } else {
        r.exit_status = -1;
    }
    return r;
}

/* ============================================================================
 * Parser and AST helpers
 * ============================================================================
 */

/**
 * @brief Parse a shell source string and return the AST root
 *
 * Caller owns the returned tree and must free it with free_node_tree().
 * Returns NULL on parse failure.
 */
static inline node_t *parse_shell(const char *src) {
    parser_t *p = parser_new(src);
    if (!p) {
        return NULL;
    }
    node_t *ast = parser_parse(p);
    if (parser_has_error(p)) {
        if (ast) {
            free_node_tree(ast);
        }
        parser_free(p);
        return NULL;
    }
    parser_free(p);
    return ast;
}

/**
 * @brief Count the children of an AST node
 */
static inline size_t node_child_count(const node_t *n) {
    if (!n) {
        return 0;
    }
    size_t count = 0;
    for (const node_t *c = n->first_child; c; c = c->next_sibling) {
        count++;
    }
    return count;
}

/**
 * @brief Get the @p index-th child of @p n, or NULL if out of range
 */
static inline node_t *node_child(const node_t *n, size_t index) {
    if (!n) {
        return NULL;
    }
    node_t *c = n->first_child;
    while (c && index > 0) {
        c = c->next_sibling;
        index--;
    }
    return c;
}

/**
 * @brief First WORD-typed child string of a NODE_COMMAND, or NULL
 *
 * Looks for the first child whose val_type is VAL_STR — typically the
 * command name itself for a NODE_COMMAND. Returns NULL if no such child
 * exists or the node has no children.
 */
static inline const char *node_first_arg(const node_t *n) {
    if (!n) {
        return NULL;
    }
    for (const node_t *c = n->first_child; c; c = c->next_sibling) {
        if (c->val_type == VAL_STR && c->val.str) {
            return c->val.str;
        }
    }
    return NULL;
}

/* ============================================================================
 * Shell-level assertion shortcuts
 *
 * Names follow the framework's argument convention (actual, expected,
 * message). Wrap (result).out / .err so call sites read like prose.
 * ============================================================================
 */

#define ASSERT_EXIT_STATUS(result, expected)                                   \
    ASSERT_EQ((result).exit_status, (expected), "exit status")

#define ASSERT_STDOUT_EQ(result, expected)                                     \
    ASSERT_STR_EQ((result).out, (expected), "stdout")

#define ASSERT_STDERR_EQ(result, expected)                                     \
    ASSERT_STR_EQ((result).err, (expected), "stderr")

#define ASSERT_STDOUT_CONTAINS(result, substr)                                 \
    ASSERT_TRUE(strstr((result).out, (substr)) != NULL,                        \
                "stdout contains \"" substr "\"")

#define ASSERT_STDERR_CONTAINS(result, substr)                                 \
    ASSERT_TRUE(strstr((result).err, (substr)) != NULL,                        \
                "stderr contains \"" substr "\"")

#define ASSERT_NODE_TYPE(node, expected_type)                                  \
    do {                                                                       \
        ASSERT_NOT_NULL((node), "node");                                       \
        ASSERT_EQ((node)->type, (expected_type), "node type");                 \
    } while (0)

#define ASSERT_NODE_CHILD_COUNT(node, expected)                                \
    ASSERT_EQ(node_child_count(node), (expected), "child count")

#endif // LUSH_TEST_SHELL_HARNESS_H
