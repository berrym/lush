/**
 * @file source_manager.h
 * @brief LLE Source Manager - Spec 12 Core Component
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Manages multiple completion sources and orchestrates querying.
 * Each source provides completions for specific contexts.
 */

#ifndef LLE_SOURCE_MANAGER_H
#define LLE_SOURCE_MANAGER_H

#include "lle/completion/completion_types.h"
#include "lle/completion/context_analyzer.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_COMPLETION_SOURCES 16

/**
 * @brief Completion source types
 */
typedef enum {
    LLE_SOURCE_BUILTINS,          /**< Shell builtin commands */
    LLE_SOURCE_EXTERNAL_COMMANDS, /**< External commands in PATH */
    LLE_SOURCE_FILES,             /**< File/directory paths */
    LLE_SOURCE_VARIABLES,         /**< Environment variables */
    LLE_SOURCE_HISTORY,           /**< Command history */
    LLE_SOURCE_ALIASES,           /**< Shell aliases (future) */
    LLE_SOURCE_FUNCTIONS,         /**< Shell functions (future) */
    LLE_SOURCE_CUSTOM,            /**< User-registered custom sources */
} lle_source_type_t;

/* Forward declarations */
typedef struct lle_completion_source lle_completion_source_t;
typedef struct lle_source_manager lle_source_manager_t;

/**
 * @brief Source generation function signature
 *
 * @param pool Memory pool for allocations
 * @param context Completion context
 * @param prefix Prefix to match
 * @param result Result structure to append completions to
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_source_generate_fn)(
    lle_memory_pool_t *pool, const lle_context_analyzer_t *context,
    const char *prefix, lle_completion_result_t *result);

/**
 * @brief Source applicability function signature
 *
 * @param context Completion context
 * @return true if source is applicable for this context
 */
typedef bool (*lle_source_applicable_fn)(const lle_context_analyzer_t *context);

/**
 * @brief Single completion source
 */
struct lle_completion_source {
    lle_source_type_t type; /**< Source type identifier */
    const char *name;       /**< Human-readable source name */

    /* Source function - generates completions for given prefix */
    lle_source_generate_fn generate; /**< Generation callback */

    /* Optional: Check if source is applicable for context */
    lle_source_applicable_fn is_applicable; /**< Applicability callback */

    void *user_data; /**< Source-specific data */
};

/**
 * @brief Source manager - registry of all completion sources
 */
struct lle_source_manager {
    lle_completion_source_t
        *sources[MAX_COMPLETION_SOURCES]; /**< Registered sources */
    size_t num_sources;                   /**< Number of registered sources */
    lle_memory_pool_t *pool;              /**< Memory pool for allocations */
};

/**
 * @brief Create source manager and register default sources
 *
 * @param pool Memory pool
 * @param out_manager Output source manager
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_source_manager_create(lle_memory_pool_t *pool,
                                       lle_source_manager_t **out_manager);

/**
 * @brief Free source manager
 *
 * @param manager Source manager to free
 */
void lle_source_manager_free(lle_source_manager_t *manager);

/**
 * @brief Register a completion source
 *
 * @param manager Source manager
 * @param type Source type
 * @param name Source name
 * @param generate_fn Generation function
 * @param applicable_fn Applicability function (can be NULL)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_source_manager_register(
    lle_source_manager_t *manager, lle_source_type_t type, const char *name,
    lle_source_generate_fn generate_fn, lle_source_applicable_fn applicable_fn);

/**
 * @brief Query all applicable sources for completions
 *
 * @param manager Source manager
 * @param context Completion context
 * @param prefix Prefix to match
 * @param result Result structure to append to
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_source_manager_query(lle_source_manager_t *manager,
                                      const lle_context_analyzer_t *context,
                                      const char *prefix,
                                      lle_completion_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_SOURCE_MANAGER_H */
