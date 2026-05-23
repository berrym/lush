/**
 * @file builtin_completions.h
 * @brief LLE Builtin Command Completions - Context-Aware Argument Completions
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides context-aware tab completions for all shell builtins including:
 * - Options (e.g., echo -n, -e, -E)
 * - Subcommands (e.g., display lle theme list)
 * - Dynamic arguments (e.g., cd → directories, unalias → aliases)
 *
 * This module is part of the LLE completion system (Spec 12).
 */

#ifndef LLE_BUILTIN_COMPLETIONS_H
#define LLE_BUILTIN_COMPLETIONS_H

#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ARGUMENT TYPE ENUMERATION
// ============================================================================

/**
 * @brief Types of dynamic arguments for builtins
 */
typedef enum {
    LLE_BUILTIN_ARG_NONE,      /**< No dynamic arguments */
    LLE_BUILTIN_ARG_FILE,      /**< File paths */
    LLE_BUILTIN_ARG_DIRECTORY, /**< Directory paths only */
    LLE_BUILTIN_ARG_VARIABLE,  /**< Shell/environment variables */
    LLE_BUILTIN_ARG_ALIAS,     /**< Defined aliases */
    LLE_BUILTIN_ARG_COMMAND,   /**< Commands (builtins + PATH) */
    LLE_BUILTIN_ARG_SIGNAL,    /**< Signal names */
    LLE_BUILTIN_ARG_JOB,       /**< Job IDs */
    LLE_BUILTIN_ARG_THEME,     /**< Theme names */
    LLE_BUILTIN_ARG_FEATURE,   /**< Shell features (for setopt/unsetopt) */
} lle_builtin_arg_type_t;

// ============================================================================
// OPTION SPECIFICATION
// ============================================================================

/**
 * @brief Single option specification for a builtin
 */
typedef struct {
    const char *name;        /**< Option name (e.g., "-n", "--verbose") */
    const char *description; /**< Brief description */
} lle_builtin_option_t;

// ============================================================================
// SUBCOMMAND SPECIFICATION
// ============================================================================

/**
 * @brief Subcommand specification with nested subcommands
 */
typedef struct lle_builtin_subcommand {
    const char *name; /**< Subcommand name */

    // Nested subcommands (for hierarchies like display lle theme)
    const struct lle_builtin_subcommand *subcommands; /**< Child subcommands */
    size_t subcommand_count; /**< Number of subcommands */

    // Options for this subcommand
    const lle_builtin_option_t *options; /**< Options array */
    size_t option_count;                 /**< Number of options */

    // Dynamic argument type for this subcommand
    lle_builtin_arg_type_t arg_type; /**< Type of arguments to complete */
} lle_builtin_subcommand_t;

// ============================================================================
// BUILTIN COMPLETION SPECIFICATION
// ============================================================================

/**
 * @brief Complete specification for a builtin command's completions
 */
typedef struct {
    const char *name; /**< Builtin command name */

    // Top-level options
    const lle_builtin_option_t *options; /**< Options array */
    size_t option_count;                 /**< Number of options */

    // Subcommands (for complex builtins like display, debug)
    const lle_builtin_subcommand_t *subcommands; /**< Subcommands array */
    size_t subcommand_count;                     /**< Number of subcommands */

    // Default argument type when no subcommand matches
    lle_builtin_arg_type_t default_arg_type; /**< Default argument type */
} lle_builtin_completion_spec_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Get completion spec for a builtin command
 *
 * @param builtin_name Name of the builtin command
 * @return Pointer to spec (static, do not free) or NULL if not found
 */
const lle_builtin_completion_spec_t *
lle_builtin_get_spec(const char *builtin_name);

/**
 * @brief Check if builtin completions are applicable for context
 *
 * Returns true when:
 * - Context is ARGUMENT and command_name is a builtin
 *
 * @param context Completion context
 * @return true if builtin completions should be generated
 */
bool lle_builtin_completions_applicable(const lle_word_context_t *context);

/**
 * @brief Generate builtin argument completions
 *
 * Parses the command line to determine position in subcommand hierarchy,
 * then generates appropriate completions (options, subcommands, or dynamic).
 *
 * @param pool Memory pool for allocations
 * @param context Word context with command info; the prefix is read
 *                from context->dequoted_filename_prefix
 * @param result Result structure to append completions to
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_builtin_completions_generate(lle_memory_pool_t *pool,
                                              const lle_word_context_t *context,
                                              lle_completion_result_t *result);

/**
 * @brief Get all defined signal names for trap completion
 *
 * @return NULL-terminated array of signal names (static, do not free)
 */
const char **lle_builtin_get_signal_names(void);

/**
 * @brief Get number of builtin specs defined
 *
 * @return Number of builtins with completion specs
 */
size_t lle_builtin_get_spec_count(void);

#ifdef __cplusplus
}
#endif

#endif // LLE_BUILTIN_COMPLETIONS_H
