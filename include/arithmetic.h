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

/** @brief Flag indicating an arithmetic error occurred */
extern bool arithm_error_flag;

/** @brief Last arithmetic error message */
extern char *arithm_error_message;

/**
 * @brief Get the last arithmetic error message
 *
 * @return Error message string, or NULL if no error
 */
const char *arithm_get_last_error(void);

/**
 * @brief Set an arithmetic error message
 *
 * @param message Error message to set
 */
void arithm_set_error(const char *message);

/**
 * @brief Clear the current arithmetic error state
 *
 * Resets the error flag and clears the error message.
 */
void arithm_clear_error(void);

#endif /* ARITHMETIC_H */
