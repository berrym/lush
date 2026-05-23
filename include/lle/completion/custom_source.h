/**
 * @file custom_source.h
 * @brief LLE Custom Completion Source API
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Public API for registering custom completion sources programmatically.
 * Allows builtins, plugins, and other code to add custom completion providers.
 *
 * Example usage:
 * @code
 * static lle_result_t my_generate(void *user_data,
 *                                  const lle_completion_context_t *context,
 *                                  const char *prefix,
 *                                  lle_completion_result_t *result) {
 *     // Add completions matching prefix
 *     lle_completion_add_item(result, "option1", " ", "First option", 800);
 *     lle_completion_add_item(result, "option2", " ", "Second option", 700);
 *     return LLE_SUCCESS;
 * }
 *
 * static bool my_applicable(void *user_data,
 *                           const lle_completion_context_t *context) {
 *     return context->command_name &&
 *            strcmp(context->command_name, "mycommand") == 0;
 * }
 *
 * lle_custom_completion_source_t my_source = {
 *     .name = "my-completions",
 *     .description = "Custom completions for mycommand",
 *     .priority = 800,
 *     .generate = my_generate,
 *     .is_applicable = my_applicable,
 *     .user_data = NULL
 * };
 *
 * lle_completion_register_source(&my_source);
 * @endcode
 *
 * Specification: Spec 12 (Completion System), Spec 07 (Extensibility)
 * Version: 1.0.0
 */

#ifndef LLE_CUSTOM_SOURCE_H
#define LLE_CUSTOM_SOURCE_H

#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "lle/error_handling.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

// Context type used by generate/is_applicable callbacks
typedef struct lle_completion_context lle_completion_context_t;

/* ============================================================================
 * CUSTOM SOURCE DEFINITION
 * ============================================================================
 */

/**
 * @brief Custom completion source definition (public API)
 *
 * Defines a custom completion source that can be registered with the
 * completion system. Sources provide completions for specific contexts
 * based on command name, argument position, etc.
 */
typedef struct lle_custom_completion_source {
    /** @brief Source identifier (must be unique) */
    const char *name;

    /** @brief Human-readable description (optional, can be NULL) */
    const char *description;

    /** @brief Priority (higher = queried earlier, default 500) */
    int priority;

    /**
     * @brief Generate completions for given prefix (required)
     *
     * Called when the source is applicable for the current context.
     * Should add matching completions to the result.
     *
     * @param user_data User data passed during registration
     * @param context Completion context with command info
     * @param prefix Current word prefix to match
     * @param result Result structure to add completions to
     * @return LLE_SUCCESS or error code
     */
    lle_result_t (*generate)(void *user_data, const lle_word_context_t *context,
                             lle_completion_result_t *result);

    /**
     * @brief Check if source applies to this context (optional)
     *
     * If NULL, source is always queried. Otherwise, source is only
     * queried if this function returns true.
     *
     * @param user_data User data passed during registration
     * @param context Completion context with command info
     * @return true if source should be queried, false otherwise
     */
    bool (*is_applicable)(void *user_data, const lle_word_context_t *context);

    /**
     * @brief Cleanup function called on unregistration (optional)
     *
     * If not NULL, called when the source is unregistered to allow
     * cleanup of user_data resources.
     *
     * @param user_data User data passed during registration
     */
    void (*cleanup)(void *user_data);

    /** @brief User data passed to all callbacks */
    void *user_data;

} lle_custom_completion_source_t;

/* ============================================================================
 * REGISTRATION API
 * ============================================================================
 */

/**
 * @brief Register a custom completion source
 *
 * Registers a new completion source with the system. The source will be
 * queried during completion generation if applicable.
 *
 * @param source Source definition (copied, caller retains ownership)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if source or source->name is NULL
 * @return LLE_ERROR_INVALID_PARAMETER if source->generate is NULL
 * @return LLE_ERROR_ALREADY_EXISTS if source with same name exists
 * @return LLE_ERROR_CAPACITY if max sources limit reached (16)
 *
 * @note The source struct is copied; caller can free it after registration.
 * @note String fields (name, description) are duplicated internally.
 */
lle_result_t
lle_completion_register_source(const lle_custom_completion_source_t *source);

/**
 * @brief Unregister a custom completion source
 *
 * Removes a previously registered source. If the source has a cleanup
 * callback, it will be called with the user_data.
 *
 * @param name Source name to unregister
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if name is NULL
 * @return LLE_ERROR_NOT_FOUND if source not found
 */
lle_result_t lle_completion_unregister_source(const char *name);

/**
 * @brief Unregister all custom completion sources
 *
 * Removes all custom sources. Cleanup callbacks are called for each.
 * Built-in sources are not affected.
 */
void lle_completion_unregister_all_custom_sources(void);

/* ============================================================================
 * QUERY API
 * ============================================================================
 */

/**
 * @brief Get total number of registered sources (built-in + custom)
 *
 * @return Number of registered sources
 */
size_t lle_completion_get_source_count(void);

/**
 * @brief Get number of custom (user-registered) sources
 *
 * @return Number of custom sources
 */
size_t lle_completion_get_custom_source_count(void);

/**
 * @brief Get source name by index
 *
 * @param index Source index (0 to count-1)
 * @return Source name, or NULL if index out of bounds
 */
const char *lle_completion_get_source_name(size_t index);

/**
 * @brief Check if source at index is a custom source
 *
 * @param index Source index
 * @return true if custom source, false if built-in or index out of bounds
 */
bool lle_completion_source_is_custom(size_t index);

/**
 * @brief Check if a source with given name is registered
 *
 * @param name Source name to check
 * @return true if source exists, false otherwise
 */
bool lle_completion_source_exists(const char *name);

/* ============================================================================
 * HELPER API - For use in generate callbacks
 * ============================================================================
 */

/**
 * @brief Add a completion item to the result
 *
 * Helper function for use in generate callbacks. Adds a completion
 * item with the CUSTOM type.
 *
 * @param result Result structure to add to
 * @param text Completion text
 * @param suffix Suffix to append (" " for space, "/" for dirs, NULL for
 * default)
 * @param description Optional description (can be NULL)
 * @param score Relevance score (0-1000, higher = better match)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_add_item(lle_completion_result_t *result,
                                     const char *text, const char *suffix,
                                     const char *description, int score);

/**
 * @brief Add a completion item with specific type
 *
 * Like lle_completion_add_item but allows specifying the completion type.
 *
 * @param result Result structure to add to
 * @param text Completion text
 * @param suffix Suffix to append
 * @param description Optional description
 * @param type Completion type (e.g., LLE_COMPLETION_TYPE_FILE)
 * @param score Relevance score (0-1000)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_add_typed_item(lle_completion_result_t *result,
                                           const char *text, const char *suffix,
                                           const char *description,
                                           lle_completion_type_t type,
                                           int score);

/* ============================================================================
 * CONFIG-BASED SOURCE DEFINITION (for completions.toml)
 * ============================================================================
 */

/**
 * @brief Config-based completion source (uses shell command)
 *
 * Represents a completion source defined in the user's completions.toml file.
 * These sources execute shell commands to generate completion candidates.
 */
typedef struct lle_command_source_config {
    char *name;              /**< Source identifier (from TOML section name) */
    char *description;       /**< Human-readable description */
    char **applies_to;       /**< Array of "cmd [subcommand]" patterns */
    size_t applies_to_count; /**< Number of applies_to patterns */
    int argument;            /**< Argument position (0 = any position) */
    char *command;           /**< Shell command to execute for completions */
    char *suffix;            /**< Suffix to append after completion */
    int cache_seconds;       /**< Cache TTL in seconds (0 = no cache) */

    // Runtime state (managed internally)
    char **cached_results; /**< Cached completion results */
    size_t cached_count;   /**< Number of cached results */
    time_t cache_time;     /**< When cache was populated */
} lle_command_source_config_t;

/**
 * @brief Config file state
 *
 * Holds all sources loaded from the completions.toml config file.
 */
typedef struct lle_completion_config {
    lle_command_source_config_t *sources; /**< Array of config sources */
    size_t source_count;                  /**< Number of sources */
    size_t source_capacity;               /**< Allocated capacity */
    char *config_path;                    /**< Path to loaded config file */
    time_t config_mtime;                  /**< Config file modification time */
} lle_completion_config_t;

/* ============================================================================
 * CONFIG FILE API
 * ============================================================================
 */

/**
 * @brief Load completion sources from config file
 *
 * Loads and parses the completions.toml file from the standard location
 * (~/.config/lush/completions.toml). Each source is registered with
 * the completion system.
 *
 * @return LLE_SUCCESS on success (including if file doesn't exist)
 * @return LLE_ERROR_PARSE if config file has syntax errors
 */
lle_result_t lle_completion_load_config(void);

/**
 * @brief Load completion sources from specific path
 *
 * @param path Path to config file
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_NOT_FOUND if file doesn't exist
 * @return LLE_ERROR_PARSE if config file has syntax errors
 */
lle_result_t lle_completion_load_config_file(const char *path);

/**
 * @brief Reload completion config
 *
 * Unregisters all config-based sources and reloads from the config file.
 *
 * @return LLE_SUCCESS on success
 */
lle_result_t lle_completion_reload_config(void);

/**
 * @brief Get the currently loaded config
 *
 * @return Pointer to config, or NULL if not loaded
 */
const lle_completion_config_t *lle_completion_get_config(void);

/**
 * @brief Free a command source config
 *
 * @param config Config to free
 */
void lle_command_source_config_free(lle_command_source_config_t *config);

/**
 * @brief Clear cached results for a config source
 *
 * @param config Config source to clear cache for
 */
void lle_command_source_clear_cache(lle_command_source_config_t *config);

/**
 * @brief Clear all config source caches
 */
void lle_completion_clear_all_caches(void);

/* ============================================================================
 * CUSTOM SOURCE LISTING (for display commands)
 * ============================================================================
 */

/**
 * @brief Get custom source name by index
 *
 * @param index Custom source index (0 to custom_count-1)
 * @return Source name, or NULL if index out of bounds
 */
const char *lle_completion_get_custom_source_name(size_t index);

/**
 * @brief Get custom source description by index
 *
 * @param index Custom source index (0 to custom_count-1)
 * @return Source description, or NULL if index out of bounds or no description
 */
const char *lle_completion_get_custom_source_description(size_t index);

/* ============================================================================
 * INITIALIZATION (Internal use)
 * ============================================================================
 */

// Forward declaration for internal types
typedef struct lle_source_manager lle_source_manager_t;
typedef struct lle_memory_pool_t lle_memory_pool_t;

/**
 * @brief Initialize custom source subsystem
 *
 * Called during LLE initialization. Registers the custom source
 * meta-source with the source manager.
 *
 * @param manager Source manager to register with
 * @param pool Memory pool for allocations
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_custom_source_init(lle_source_manager_t *manager,
                                    lle_memory_pool_t *pool);

/**
 * @brief Shutdown custom source subsystem
 *
 * Called during LLE shutdown. Unregisters all custom sources and
 * calls their cleanup callbacks.
 */
void lle_custom_source_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // LLE_CUSTOM_SOURCE_H
