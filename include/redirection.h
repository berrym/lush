/**
 * @file redirection.h
 * @brief I/O redirection handling for shell commands
 *
 * Provides comprehensive I/O redirection functionality including basic output
 * redirection (>), append (>>), input (<), error (2>, 2>>), combined (&>),
 * here strings (<<<), and here documents (<<, <<-).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef REDIRECTION_H
#define REDIRECTION_H

#include "executor.h"
#include "node.h"

#include <stdbool.h>

/**
 * @brief Set up all redirections for a command
 *
 * Processes all redirection nodes attached to a command and
 * sets up the corresponding file descriptors.
 *
 * @param executor Executor context
 * @param command Command node with redirection children
 * @return 0 on success, -1 on error
 */
int setup_redirections(executor_t *executor, node_t *command);

/**
 * @brief Check if a node is a redirection type
 *
 * @param node Node to check
 * @return true if node is a redirection, false otherwise
 */
bool is_redirection_node(node_t *node);

/**
 * @brief Count redirection nodes in a command
 *
 * @param command Command node to examine
 * @return Number of redirection children
 */
int count_redirections(node_t *command);

/**
 * @brief Redirection state for save/restore operations
 *
 * Stores original file descriptor values so they can be
 * restored after command execution.
 */
typedef struct redirection_state {
    int saved_stdin;   /**< Saved stdin file descriptor */
    int saved_stdout;  /**< Saved stdout file descriptor */
    int saved_stderr;  /**< Saved stderr file descriptor */
    bool stdin_saved;  /**< Whether stdin was saved */
    bool stdout_saved; /**< Whether stdout was saved */
    bool stderr_saved; /**< Whether stderr was saved */
} redirection_state_t;

/**
 * @brief Save current file descriptors
 *
 * Duplicates stdin, stdout, and stderr so they can be
 * restored after redirections are applied.
 *
 * @param state State structure to store saved descriptors
 * @return 0 on success, -1 on error
 */
int save_file_descriptors(redirection_state_t *state);

/**
 * @brief Restore saved file descriptors
 *
 * Restores stdin, stdout, and stderr from the saved state
 * and closes any redirected file descriptors.
 *
 * @param state State structure with saved descriptors
 * @return 0 on success, -1 on error
 */
int restore_file_descriptors(redirection_state_t *state);

/**
 * @brief Report a redirection error
 *
 * Prints an error message related to redirection failure.
 *
 * @param message Error message to display
 */
void redirection_error(const char *message);

#endif /* REDIRECTION_H */
