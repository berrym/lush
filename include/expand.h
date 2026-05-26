/**
 * @file expand.h
 * @brief Word expansion module for shell variable and command substitution
 *
 * Provides expansion context and flags for controlling shell word expansion
 * including variable expansion, command substitution, and globbing.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef EXPAND_H
#define EXPAND_H

#include <stdbool.h>

/** @brief Normal word expansion with all features enabled */
#define EXPAND_NORMAL 0x00

/** @brief Expanding an alias value */
#define EXPAND_ALIAS 0x01

/** @brief Do not remove quotes from result */
#define EXPAND_NOQUOTE 0x02

/** @brief Do not expand variables ($VAR, ${VAR}) */
#define EXPAND_NOVAR 0x04

/** @brief Do not expand command substitutions ($(cmd), `cmd`) */
#define EXPAND_NOCMD 0x08

/** @brief Do not expand globs/wildcards (*, ?, [...]) */
#define EXPAND_NOGLOB 0x10

/**
 * @brief Expansion context structure
 *
 * Tracks the current state during word expansion to properly handle
 * nested quotes, backticks, and expansion mode flags.
 */
typedef struct {
    int mode;          ///< Expansion mode flags (EXPAND_*)
    bool in_quotes;    ///< Currently inside quotes
    bool in_backticks; ///< Currently inside backticks
} expand_ctx_t;

/**
 * @brief Initialize an expansion context with default values
 *
 * Sets up an expansion context with the specified mode flags
 * and resets quote/backtick tracking.
 *
 * @param ctx Pointer to context structure to initialize
 * @param mode Expansion mode flags (EXPAND_* constants)
 */
void expand_ctx_init(expand_ctx_t *ctx, int mode);

/**
 * @brief Check if a specific expansion mode is enabled
 *
 * Tests whether a particular expansion feature is active
 * in the given context.
 *
 * @param ctx Pointer to expansion context
 * @param mode_flag Mode flag to check (EXPAND_* constant)
 * @return true if the mode flag is set, false otherwise
 */
bool expand_ctx_check(expand_ctx_t *ctx, int mode_flag);

#endif /// EXPAND_H
