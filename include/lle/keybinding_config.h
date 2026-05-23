/**
 * @file keybinding_config.h
 * @brief LLE User Keybinding Configuration System
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides user-customizable keybindings via configuration files.
 * Users can override default Emacs keybindings by creating:
 *   ~/.config/lush/keybindings.toml
 *
 * Example config:
 *   [bindings]
 *   "C-a" = "end-of-line"        # Swap C-a and C-e
 *   "C-e" = "beginning-of-line"
 *   "M-p" = "history-search-backward"
 *   "C-s" = "none"               # Unbind
 *
 * Specification: Spec 13 (User Customization), Spec 22 (User Interface)
 * Version: 1.0.0
 */

#ifndef LLE_KEYBINDING_CONFIG_H
#define LLE_KEYBINDING_CONFIG_H

#include "lle/error_handling.h"
#include "lle/keybinding.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** @brief Maximum path length for config files */
#define LLE_KEYBINDING_CONFIG_PATH_MAX 4096

/** @brief Maximum config file size (64KB) */
#define LLE_KEYBINDING_CONFIG_FILE_MAX (64 * 1024)

/** @brief Config filename */
#define LLE_KEYBINDING_CONFIG_FILENAME "keybindings.toml"

/* ============================================================================
 * ACTION REGISTRY TYPES
 * ============================================================================
 */

/**
 * @brief Action registry entry mapping name to function pointer
 *
 * Maps GNU Readline action names (e.g., "forward-char") to LLE
 * function pointers (e.g., lle_forward_char).
 */
typedef struct lle_action_registry_entry {
    const char *name;       /**< GNU Readline action name */
    lle_action_type_t type; /**< Action type (SIMPLE or CONTEXT) */
    union {
        lle_action_simple_t simple;   /**< Simple action function */
        lle_action_context_t context; /**< Context-aware action function */
    } func;
    const char *description; /**< Human-readable description */
} lle_action_registry_entry_t;

/* ============================================================================
 * LOAD RESULT TYPES
 * ============================================================================
 */

/**
 * @brief Result of loading user keybindings
 */
typedef struct lle_keybinding_load_result {
    lle_result_t status;                           /**< Overall status */
    char filepath[LLE_KEYBINDING_CONFIG_PATH_MAX]; /**< File that was loaded */
    size_t bindings_applied;    /**< Number of bindings successfully applied */
    size_t bindings_overridden; /**< Number of defaults overridden */
    size_t errors_count; /**< Number of errors (invalid entries skipped) */
    char error_msg[256]; /**< Error message if failed */
    size_t error_line;   /**< Error line number */
    size_t error_column; /**< Error column number */
} lle_keybinding_load_result_t;

/* ============================================================================
 * ACTION REGISTRY API
 * ============================================================================
 */

/**
 * @brief Look up an action by name
 *
 * @param name GNU Readline action name (e.g., "forward-char")
 * @return Pointer to registry entry, or NULL if not found
 *
 * @note The returned pointer is to static data and should not be freed
 */
const lle_action_registry_entry_t *lle_action_registry_lookup(const char *name);

/**
 * @brief Get list of all registered actions
 *
 * @param count_out Output: number of actions in registry
 * @return Pointer to static array of registry entries
 *
 * @note The returned array is terminated by an entry with name == NULL
 */
const lle_action_registry_entry_t *
lle_action_registry_get_all(size_t *count_out);

/**
 * @brief Get the number of registered actions
 *
 * @return Number of actions in the registry
 */
size_t lle_action_registry_count(void);

/**
 * @brief Get action by index
 *
 * @param index Index into the registry (0-based)
 * @return Pointer to registry entry, or NULL if index out of bounds
 *
 * @note Use with lle_action_registry_count() to iterate all actions
 */
const lle_action_registry_entry_t *
lle_action_registry_get_by_index(size_t index);

/* ============================================================================
 * CONFIG FILE API
 * ============================================================================
 */

/**
 * @brief Get user keybinding config file path
 *
 * Returns the path to the user's keybinding configuration file.
 * Checks $XDG_CONFIG_HOME/lush/keybindings.toml first,
 * then falls back to ~/.config/lush/keybindings.toml.
 *
 * @param buffer Output buffer for path
 * @param buffer_size Size of output buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_keybinding_get_user_config_path(char *buffer,
                                                 size_t buffer_size);

/**
 * @brief Load user keybinding configuration
 *
 * Loads keybindings from the user's config file and applies them
 * to the keybinding manager. User bindings override default bindings.
 *
 * @param manager Keybinding manager to apply bindings to
 * @param result Output: load result with statistics
 * @return LLE_SUCCESS, LLE_ERROR_NOT_FOUND (file doesn't exist), or error
 *
 * @note LLE_ERROR_NOT_FOUND is not an error - user config is optional
 */
lle_result_t
lle_keybinding_load_user_config(lle_keybinding_manager_t *manager,
                                lle_keybinding_load_result_t *result);

/**
 * @brief Load keybindings from a specific file
 *
 * Loads keybindings from the specified file and applies them
 * to the keybinding manager.
 *
 * @param manager Keybinding manager to apply bindings to
 * @param filepath Path to config file
 * @param result Output: load result with statistics
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_keybinding_load_from_file(lle_keybinding_manager_t *manager,
                              const char *filepath,
                              lle_keybinding_load_result_t *result);

/**
 * @brief Load keybindings from a string
 *
 * Parses keybinding configuration from a string and applies
 * bindings to the manager. Useful for testing.
 *
 * @param manager Keybinding manager to apply bindings to
 * @param content Config file content as string
 * @param result Output: load result with statistics
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_keybinding_load_from_string(lle_keybinding_manager_t *manager,
                                const char *content,
                                lle_keybinding_load_result_t *result);

/**
 * @brief Reload user keybinding configuration
 *
 * Reloads the user's config file, re-applying all custom bindings.
 * Useful for the "display lle keybindings reload" command.
 *
 * @param manager Keybinding manager to reload
 * @param result Output: load result with statistics
 * @return LLE_SUCCESS or error code
 *
 * @note This does NOT reset to defaults first - only applies user overrides
 */
lle_result_t
lle_keybinding_reload_user_config(lle_keybinding_manager_t *manager,
                                  lle_keybinding_load_result_t *result);

#ifdef __cplusplus
}
#endif

#endif // LLE_KEYBINDING_CONFIG_H
