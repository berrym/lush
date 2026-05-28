/**
 * @file init.h
 * @brief Shell initialization and type detection
 *
 * Provides shell initialization routines and functions for detecting
 * shell type (interactive, login, non-interactive).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef INIT_H
#define INIT_H

#include <stdbool.h>
#include <stdio.h>

/**
 * @brief Shell type constants
 */
enum {
    SHELL_NON_INTERACTIVE = 0, ///< Script files, -c commands, piped input
    SHELL_INTERACTIVE = 1,     ///< Interactive shell (stdin is terminal)
    SHELL_LOGIN = 2            ///< Login shell (argv[0] starts with - or -l)
};

/** @brief Legacy alias for SHELL_NON_INTERACTIVE */
#define NORMAL_SHELL SHELL_NON_INTERACTIVE

/** @brief Legacy alias for SHELL_INTERACTIVE */
#define INTERACTIVE_SHELL SHELL_INTERACTIVE

/** @brief Legacy alias for SHELL_LOGIN */
#define LOGIN_SHELL SHELL_LOGIN

/**
 * @brief Initialize the shell
 *
 * Performs all shell initialization including:
 * - Parsing command-line arguments
 * - Detecting shell type (interactive, login, etc.)
 * - Setting up signal handlers
 * - Initializing subsystems (history, aliases, etc.)
 * - Executing startup scripts
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param in Pointer to input file stream (may be modified)
 * @return 0 on success, non-zero on error
 */
int init(int argc, char **argv, FILE **in);

/**
 * @brief Get the current shell type
 *
 * Returns the detected shell type based on how the shell
 * was invoked.
 *
 * @return SHELL_NON_INTERACTIVE, SHELL_INTERACTIVE, or SHELL_LOGIN
 */
int shell_type(void);

/**
 * @brief Check if running as an interactive shell
 *
 * @return true if shell is interactive, false otherwise
 */
bool is_interactive_shell(void);

/**
 * @brief Check if running as a login shell
 *
 * @return true if shell is a login shell, false otherwise
 */
bool is_login_shell(void);

/**
 * @brief Check if shell is the session leader
 *
 * A session leader is a process whose PID equals its session ID.
 * This is useful for determining terminal ownership and signal
 * handling behavior.
 *
 * @return true if session leader, false otherwise
 */
bool is_session_leader(void);

#endif
