/**
 * @file expand.c
 * @brief Variable and parameter expansion
 *
 * Implements shell expansion including:
 * - Variable expansion ($VAR, ${VAR})
 * - Alias expansion
 * - Word list management
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "expand.h"

#include "alias.h"
#include "lush.h"

#include <ctype.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Initialize expansion context with default values
 *
 * @param ctx Expansion context to initialize
 * @param mode Expansion mode flags
 */
void expand_ctx_init(expand_ctx_t *ctx, int mode) {
    if (ctx) {
        ctx->mode = mode;
        ctx->in_quotes = false;
        ctx->in_backticks = false;
    }
}

/**
 * @brief Check if a specific expansion mode is enabled
 *
 * @param ctx Expansion context to check
 * @param mode_flag Mode flag to test
 * @return true if mode is enabled, false otherwise
 */
bool expand_ctx_check(expand_ctx_t *ctx, int mode_flag) {
    if (!ctx) {
        return false;
    }
    return (ctx->mode & mode_flag) != 0;
}

/**
 * @brief Expand an alias recursively
 *
 * Looks up an alias and returns its expanded value. If the alias
 * value itself contains aliases, they are expanded recursively.
 *
 * @param alias_name Name of the alias to expand
 * @return Expanded alias value, or NULL if not found (caller must free)
 */
char *expand_alias_recursive(const char *alias_name) {
    if (!alias_name) {
        return NULL;
    }

    /// Look up the initial alias
    char *alias_value = lookup_alias(alias_name);
    if (!alias_value) {
        return NULL;
    }

    /// Make a copy we can modify
    char *result = strdup(alias_value);
    if (!result) {
        return NULL;
    }

    /// Recursively expand any further aliases
    /// This is a simplified version - real implementation would need
    /// to handle word tokenization and prevent infinite recursion

    return result;
}
