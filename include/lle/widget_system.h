/**
 * @file widget_system.h
 * @brief LLE Widget Registry System
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * ZSH-inspired widget system for LLE. Widgets are named editing operations
 * that can be bound to keys, triggered by hooks, or invoked programmatically.
 *
 * Specification: docs/lle_specification/07_extensibility_framework_complete.md
 * Section: 4 - Widget System Architecture
 *
 * Design Principles:
 * - Simple, focused API following LLE patterns
 * - Hash table based for O(1) widget lookup
 * - Memory pool integration for all allocations
 * - Thread-safe (single editing thread, no locks needed)
 * - Error resilient (widget failures don't crash editor)
 *
 * Example Usage:
 *
 *     /// Initialize registry
 *     lle_widget_registry_t *registry;
 *     lle_widget_registry_init(&registry, memory_pool);
 *
 *     /// Register a widget
 *     lle_widget_register(registry, "my-widget", my_callback,
 *                        LLE_WIDGET_USER, NULL);
 *
 *     /// Execute widget
 *     lle_widget_execute(registry, "my-widget", editor);
 *
 *     /// Cleanup
 *     lle_widget_registry_destroy(registry);
 */

#ifndef LLE_WIDGET_SYSTEM_H
#define LLE_WIDGET_SYSTEM_H

#include "lle/error_handling.h"
#include "lle/hashtable.h" /// IWYU pragma: keep (provides lle_hashtable_t)
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Forward declaration to avoid circular dependency
typedef struct lle_editor lle_editor_t;

/* ============================================================================
 * TYPES AND STRUCTURES
 * ============================================================================
 */

/**
 * Widget types
 */
typedef enum {
    LLE_WIDGET_BUILTIN, ///< Built-in LLE widgets
    LLE_WIDGET_USER,    ///< User-defined widgets
    LLE_WIDGET_PLUGIN,  ///< Plugin-provided widgets
} lle_widget_type_t;

/**
 * Widget callback function signature
 *
 * Widgets receive full editor context and optional user data.
 * Widget should return LLE_SUCCESS on success, error code on failure.
 *
 * @param editor Editor context with buffer, history, cursor, etc.
 * @param user_data User-provided data (may be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
typedef lle_result_t (*lle_widget_callback_t)(lle_editor_t *editor,
                                              void *user_data);

/**
 * Widget structure
 *
 * Represents a named editing operation that can be invoked by
 * keybindings, hooks, or programmatically.
 */
typedef struct lle_widget {
    char *name;                     ///< Widget name (unique identifier)
    lle_widget_callback_t callback; ///< Execution function
    lle_widget_type_t type;         ///< Widget type
    void *user_data;                ///< Optional user data

    /// Performance tracking
    uint64_t execution_count;         ///< Times executed
    uint64_t total_execution_time_us; ///< Total execution time in microseconds

    /// State
    bool enabled; ///< Widget enabled state

    /// Linked list for iteration
    struct lle_widget *next; ///< Next widget in list
} lle_widget_t;

/**
 * Widget registry structure
 *
 * Central registry for all widgets. Provides O(1) lookup by name
 * and maintains linked list for iteration.
 */
typedef struct lle_widget_registry {
    lle_hashtable_t *widgets;       ///< name -> widget lookup
    lle_widget_t *widget_list;      ///< Linked list of all widgets
    size_t widget_count;            ///< Total widgets registered
    lle_memory_pool_t *memory_pool; ///< Memory pool for allocations
    bool registry_active;           ///< Registry operational state
} lle_widget_registry_t;

/* ============================================================================
 * REGISTRY LIFECYCLE
 * ============================================================================
 */

/**
 * Initialize widget registry
 *
 * Creates and initializes the widget registry with hash table for O(1) lookup.
 * All allocations use the provided memory pool.
 *
 * @param registry Output pointer for created registry (must not be NULL)
 * @param memory_pool Memory pool for allocations (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry or memory_pool is NULL
 * @return LLE_ERROR_OUT_OF_MEMORY if allocation fails
 */
lle_result_t lle_widget_registry_init(lle_widget_registry_t **registry,
                                      lle_memory_pool_t *memory_pool);

/**
 * Destroy widget registry
 *
 * Frees all widgets and registry resources. After this call, the registry
 * pointer is invalid and should not be used.
 *
 * @param registry Registry to destroy (may be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry is NULL
 */
lle_result_t lle_widget_registry_destroy(lle_widget_registry_t *registry);

/* ============================================================================
 * WIDGET MANAGEMENT
 * ============================================================================
 */

/**
 * Register a widget
 *
 * Registers a new widget with the given name and callback. Widget names
 * must be unique within the registry. The name is copied internally.
 *
 * Widget names follow these conventions:
 * - Built-in widgets: lowercase-with-hyphens (e.g., "forward-char")
 * - User widgets: any valid identifier
 * - Plugin widgets: plugin-name/widget-name (e.g., "myplugin/custom-action")
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name (must not be NULL, must be unique)
 * @param callback Widget callback function (must not be NULL)
 * @param type Widget type
 * @param user_data Optional user data (may be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry, name, or callback is NULL
 * @return LLE_ERROR_ALREADY_EXISTS if widget with same name exists
 * @return LLE_ERROR_OUT_OF_MEMORY if allocation fails
 */
lle_result_t lle_widget_register(lle_widget_registry_t *registry,
                                 const char *name,
                                 lle_widget_callback_t callback,
                                 lle_widget_type_t type, void *user_data);

/**
 * Unregister a widget
 *
 * Removes widget from registry and frees its resources. Any keybindings
 * or hooks referencing this widget will fail if invoked after unregistration.
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name to unregister (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry or name is NULL
 * @return LLE_ERROR_NOT_FOUND if widget does not exist
 */
lle_result_t lle_widget_unregister(lle_widget_registry_t *registry,
                                   const char *name);

/**
 * Lookup a widget by name
 *
 * Returns pointer to widget structure for the given name.
 * Returned pointer is valid until widget is unregistered or registry destroyed.
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name (must not be NULL)
 * @return Widget pointer if found, NULL otherwise
 */
lle_widget_t *lle_widget_lookup(lle_widget_registry_t *registry,
                                const char *name);

/**
 * Execute a widget by name
 *
 * Looks up widget by name and executes it with the given editor context.
 * Tracks execution time and updates widget statistics.
 *
 * If widget execution fails, the error is returned but the editor remains
 * in a valid state (error resilience).
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name to execute (must not be NULL)
 * @param editor Editor context (must not be NULL)
 * @return LLE_SUCCESS if widget executed successfully
 * @return LLE_ERROR_INVALID_PARAMETER if any parameter is NULL
 * @return LLE_ERROR_NOT_FOUND if widget does not exist
 * @return LLE_ERROR_DISABLED if widget is disabled
 * @return Other error codes from widget callback
 */
lle_result_t lle_widget_execute(lle_widget_registry_t *registry,
                                const char *name, lle_editor_t *editor);

/**
 * Execute a widget directly
 *
 * Executes widget without name lookup. Useful when widget pointer is
 * already available (e.g., from lle_widget_lookup or hook registration).
 *
 * @param widget Widget to execute (must not be NULL)
 * @param editor Editor context (must not be NULL)
 * @return LLE_SUCCESS if widget executed successfully
 * @return LLE_ERROR_INVALID_PARAMETER if widget or editor is NULL
 * @return LLE_ERROR_DISABLED if widget is disabled
 * @return Other error codes from widget callback
 */
lle_result_t lle_widget_execute_direct(lle_widget_t *widget,
                                       lle_editor_t *editor);

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * Get widget count
 *
 * @param registry Widget registry (must not be NULL)
 * @return Number of registered widgets, or 0 if registry is NULL
 */
size_t lle_widget_registry_get_count(lle_widget_registry_t *registry);

/**
 * Check if widget exists
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name (must not be NULL)
 * @return true if widget exists, false otherwise
 */
bool lle_widget_exists(lle_widget_registry_t *registry, const char *name);

/**
 * Enable widget
 *
 * Enables a previously disabled widget.
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry or name is NULL
 * @return LLE_ERROR_NOT_FOUND if widget does not exist
 */
lle_result_t lle_widget_enable(lle_widget_registry_t *registry,
                               const char *name);

/**
 * Disable widget
 *
 * Disables a widget. Disabled widgets cannot be executed and will
 * return LLE_ERROR_DISABLED when invoked.
 *
 * @param registry Widget registry (must not be NULL)
 * @param name Widget name (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if registry or name is NULL
 * @return LLE_ERROR_NOT_FOUND if widget does not exist
 */
lle_result_t lle_widget_disable(lle_widget_registry_t *registry,
                                const char *name);

#ifdef __cplusplus
}
#endif

#endif /// LLE_WIDGET_SYSTEM_H
