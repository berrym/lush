/**
 * @file errors.h
 * @brief Legacy error-reporting helpers for pre-executor and fatal paths
 *
 * These helpers predate the structured shell-error system in shell_error.h
 * and remain in use only for paths that fall outside its model:
 *   - error_return: nonfatal OOM/infrastructure failures with no executor
 *     context (set_node_val_str)
 *   - error_syscall: fatal syscall failures during early init or low-level
 *     allocation (init.c, input.c, node.c)
 *   - error_abort: fatal assertion-style failures during init
 *   - sigsegv_handler: SIGSEGV signal handler
 *
 * All user-facing shell errors must use shell_error.h instead.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef ERRORS_H
#define ERRORS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Report a nonfatal error with errno information
 *
 * Prints "lush: <fmt>: <strerror(errno)>\n" to stderr and returns.
 * For OOM/infrastructure paths only; user-facing shell errors must
 * use the structured-error API in shell_error.h.
 *
 * @param fmt Format string
 * @param ... Format arguments
 */
void error_return(const char *fmt, ...);

/**
 * @brief Report a fatal error with errno information
 *
 * Prints "lush: <fmt>: <strerror(errno)>\n" to stderr and exits with
 * EXIT_FAILURE. For early-init syscall failures.
 *
 * @param fmt Format string
 * @param ... Format arguments
 */
void error_syscall(const char *fmt, ...);

/**
 * @brief Report a fatal error and abort
 *
 * Prints the message and calls abort() to dump core. For internal
 * assertion-style invariants during init.
 *
 * @param fmt Format string
 * @param ... Format arguments
 */
void error_abort(const char *fmt, ...);

/**
 * @brief Signal handler for SIGSEGV
 *
 * @param sig Signal number (should be SIGSEGV)
 */
void sigsegv_handler(int sig);

#endif
