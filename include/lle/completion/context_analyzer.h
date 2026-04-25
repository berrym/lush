/**
 * @file context_analyzer.h
 * @brief LLE Context Analyzer - Spec 12 Core Component
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Analyzes buffer and cursor position to determine completion context.
 * This determines WHAT we're completing and which sources to query.
 */

#ifndef LLE_CONTEXT_ANALYZER_H
#define LLE_CONTEXT_ANALYZER_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context types for completion
 */
typedef enum {
    LLE_CONTEXT_COMMAND,    /**< Start of command (complete command names) */
    LLE_CONTEXT_ARGUMENT,   /**< Command argument (complete files/dirs) */
    LLE_CONTEXT_VARIABLE,   /**< Variable expansion ($VAR) */
    LLE_CONTEXT_REDIRECT,   /**< After redirect operator (>, <, |) */
    LLE_CONTEXT_ASSIGNMENT, /**< Variable assignment (VAR=) */
    LLE_CONTEXT_UNKNOWN     /**< Can't determine context */
} lle_completion_context_type_t;

/**
 * @brief Analysis result for current completion context
 */
typedef struct lle_context_analyzer {
    /* Context type */
    lle_completion_context_type_t type; /**< Type of completion context */

    /* Position information */
    size_t word_start;  /**< Start of word being completed */
    size_t word_end;    /**< End of word being completed */
    char *partial_word; /**< The partial word to complete */

    /* Command context (if applicable) */
    char *command_name; /**< Current command (if in argument position) */
    int argument_index; /**< Which argument (0-based) */
    char **arguments; /**< Array of argument strings (for subcommand lookup) */
    int argument_count; /**< Number of arguments in array */

    /* State flags */
    bool in_quotes;      /**< Inside quotes? */
    bool after_redirect; /**< After >, <, |, etc. */
    bool in_assignment;  /**< In VAR=value ? */

    /* Memory pool reference */
    lle_memory_pool_t *pool; /**< For allocations */
} lle_context_analyzer_t;

/**
 * @brief Analyze buffer to determine completion context
 *
 * @param buffer Input buffer
 * @param cursor_pos Cursor position in buffer
 * @param pool Memory pool for allocations
 * @param out_context Output context (allocated from pool)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_context_analyze(const char *buffer, size_t cursor_pos,
                                 lle_memory_pool_t *pool,
                                 lle_context_analyzer_t **out_context);

/**
 * @brief Free context analyzer resources
 *
 * @param context Context to free
 */
void lle_context_analyzer_free(lle_context_analyzer_t *context);

#ifdef __cplusplus
}
#endif

#endif /* LLE_CONTEXT_ANALYZER_H */
