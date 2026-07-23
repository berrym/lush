/**
 * @file arithmetic.h
 * @brief POSIX-compliant arithmetic expansion module
 *
 * Provides arithmetic expansion using the shunting yard algorithm.
 * Supports all POSIX arithmetic operators, variables, and proper error
 * handling.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef ARITHMETIC_H
#define ARITHMETIC_H

#include "shell_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Evaluate arithmetic expression with full POSIX operator support
 *
 * Evaluates an arithmetic expression string and returns the result as a string.
 *
 * Supported operators:
 * - Basic: + - * / %
 * - Comparison: == != < <= > >=
 * - Logical: && || !
 * - Bitwise: & | ^ ~ << >>
 * - Assignment: = += -= *= /= %= <<= >>= &= ^= |=
 * - Increment/Decrement: ++ --
 * - Exponentiation: **
 * - Ternary: ? :
 * - Parentheses for grouping
 * - Variables and numeric literals
 * - Octal (0123) and hexadecimal (0x123) numbers
 *
 * @param orig_expr Arithmetic expression string (with or without $(( ))
 * wrapper)
 * @return String representation of the result, or NULL on error
 */
char *arithm_expand(const char *orig_expr);

/**
 * @brief Evaluate arithmetic expression with executor context for scoped
 * variables
 *
 * This function is identical to arithm_expand but uses the executor's
 * symbol table for variable resolution, allowing access to function
 * parameters like $1, $2, etc.
 *
 * @param executor Executor context for scoped variable resolution
 * @param orig_expr Arithmetic expression string (with or without $(( ))
 * wrapper)
 * @return String representation of the result, or NULL on error
 */
typedef struct executor executor_t;
char *arithm_expand_with_executor(executor_t *executor, const char *orig_expr);

/**
 * @brief Initialize the arithmetic expansion module
 *
 * Call this once during shell initialization to set up any required
 * state for arithmetic evaluation.
 */
void arithm_init(void);

/**
 * @brief Clean up arithmetic expansion module resources
 *
 * Call this during shell shutdown to free any allocated resources.
 */
void arithm_cleanup(void);

/* ============================================================================
 * Error reporting
 * ============================================================================
 *
 * Arithmetic evaluation can fail in many specific ways (division by zero,
 * invalid assignment target, stack overflow, mismatched parens, etc.).
 * Each failure mode reports a specific shell_error_code_t along with a
 * `while:` context string and a `help:` suggestion. Callers (the executor
 * arithmetic-expansion path) drain the typed error state and emit a
 * structured shell error via shell_error_create() / shell_error_display().
 *
 * The state is file-scope inside arithmetic.c and accessed only through
 * the accessors below. The pre-2026-04 bare globals
 * (`arithm_error_flag` / `arithm_error_message`) were deleted as part
 * of the error-system migration.
 */

/**
 * @brief Set the arithmetic error state with a structured code, while:
 * context, help: suggestion, and a printf-style message.
 *
 * @param code Specific shell_error_code_t for this failure mode
 * @param while_context Short phrase for the `while:` line
 *                      (e.g. "evaluating / operator"). May be NULL.
 * @param help Short phrase for the `help:` line
 *             (e.g. "divisor must be non-zero"). May be NULL.
 * @param fmt printf-style format for the error message text
 */
void arithm_set_error(shell_error_code_t code, const char *while_context,
                      const char *help, const char *fmt, ...);

/** @brief Clear arithmetic error state. */
void arithm_clear_error(void);

/** @brief Returns true if the arithmetic error flag is set. */
bool arithm_error_is_flagged(void);

/**
 * @brief True while a nested arithmetic expansion is in progress.
 *
 * A nested $((...)) is expanded during another arithmetic expression's Layer-0
 * pre-pass. The executor's arithmetic-expansion path uses this to leave a
 * nested failure flagged for the enclosing expression to report once, rather
 * than rendering it (which would double-report the same error).
 */
bool arithm_expansion_in_progress(void);

/** @brief Returns the structured code for the current error. */
shell_error_code_t arithm_error_code(void);

/** @brief Returns the message text for the current error. May be NULL. */
const char *arithm_error_message(void);

/** @brief Returns the `while:` context for the current error. May be NULL. */
const char *arithm_error_while(void);

/** @brief Returns the `help:` suggestion for the current error. May be NULL. */
const char *arithm_error_help(void);

#endif /// ARITHMETIC_H
