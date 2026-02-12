/**
 * @file git_command.h
 * @brief Portable timed git command execution
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides a timeout-safe alternative to popen() for git commands.
 * Uses fork/exec/pipe/select/waitpid (all POSIX) to enforce a
 * wall-clock timeout on git subprocess execution.
 *
 * This prevents shell freezes when git commands hang due to:
 * - Network filesystem stalls (NFS, FUSE, SSHFS)
 * - Unreachable git remotes (upstream tracking queries)
 * - Large repository index operations
 * - Stale lock files (.git/index.lock)
 */

#ifndef LLE_GIT_COMMAND_H
#define LLE_GIT_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default timeout for synchronous prompt git commands (ms) */
#define GIT_CMD_SYNC_TIMEOUT_MS 3000

/** Default timeout for async worker git commands (ms) */
#define GIT_CMD_ASYNC_TIMEOUT_MS 5000

/**
 * @brief Result of a timed git command execution
 */
typedef struct {
    int exit_status;  /**< Shell exit status, or -1 on timeout/error */
    bool timed_out;   /**< true if command was killed due to timeout */
} git_cmd_result_t;

/**
 * @brief Execute a shell command with a wall-clock timeout
 *
 * Portable implementation using fork/exec/pipe/select/waitpid.
 * If the command does not complete within timeout_ms, the child
 * process is terminated (SIGTERM, then SIGKILL) and the function
 * returns with timed_out=true.
 *
 * The command is executed via /bin/sh -c, same as popen().
 * Stdout is captured into the output buffer. Stderr is discarded.
 *
 * @param cmd         Shell command string to execute
 * @param output      Buffer for captured stdout (NULL to discard)
 * @param output_size Size of output buffer
 * @param timeout_ms  Timeout in milliseconds (0 = GIT_CMD_SYNC_TIMEOUT_MS)
 * @return Result with exit_status and timed_out flag
 */
git_cmd_result_t git_command_with_timeout(const char *cmd, char *output,
                                          size_t output_size,
                                          uint32_t timeout_ms);

/**
 * @brief Convenience: run a git command with -C dir prefix and timeout
 *
 * Constructs "git -C <dir> <args>" and executes with timeout.
 * This avoids process-wide chdir() which is unsafe from worker threads.
 *
 * @param dir         Working directory for git (must not be NULL)
 * @param args        Git arguments (e.g., "status --porcelain")
 * @param output      Buffer for captured stdout (NULL to discard)
 * @param output_size Size of output buffer
 * @param timeout_ms  Timeout in milliseconds (0 = GIT_CMD_SYNC_TIMEOUT_MS)
 * @return Result with exit_status and timed_out flag
 */
git_cmd_result_t git_command_in_dir(const char *dir, const char *args,
                                    char *output, size_t output_size,
                                    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LLE_GIT_COMMAND_H */
