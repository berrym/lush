/**
 * @file widget_hooks.h
 * @brief LLE Widget Hooks Manager
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Manages hook points in the editor lifecycle where widgets can be
 * automatically triggered. Inspired by ZSH's hook system.
 *
 * Specification: docs/lle_specification/07_extensibility_framework_complete.md
 * Section: 5 - Widget Hooks Integration
 *
 * Hook Types (ZSH-inspired):
 * - line-init:  Called when line editing starts (zle-line-init)
 * - line-finish: Called when line editing ends (zle-line-finish)
 * - buffer-modified: Called when buffer content changes
 * - pre-command: Called before command execution (precmd)
 * - post-command: Called after command execution
 * - completion-start: Called when tab completion is triggered
 * - completion-end: Called when completion finishes
 * - history-search: Called when history search starts
 * - terminal-resize: Called when terminal is resized
 *
 * Example Usage:
 *
 *     /// Initialize hooks manager
 *     lle_widget_hooks_manager_t *manager;
 *     lle_widget_hooks_manager_init(&manager, widget_registry, memory_pool);
 *
 *     /// Register widget for hook
 *     lle_widget_hook_register(manager, LLE_HOOK_BUFFER_MODIFIED, "my-widget");
 *
 *     /// Trigger hook (called by system)
 *     lle_widget_hook_trigger(manager, LLE_HOOK_BUFFER_MODIFIED, editor);
 */

#ifndef LLE_WIDGET_HOOKS_H
#define LLE_WIDGET_HOOKS_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/widget_system.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * TYPES AND STRUCTURES
 * ============================================================================
 */

/**
 * Widget hook types (ZSH-inspired)
 *
 * These represent lifecycle points where widgets can be automatically
 * triggered without explicit invocation.
 *
 * Lifecycle order for line editing:
 *   LINE_INIT -> [editing] -> LINE_ACCEPTED -> LINE_FINISH -> PRE_COMMAND
 */
typedef enum {
    LLE_HOOK_LINE_INIT,        ///< Start of line editing (zle-line-init)
    LLE_HOOK_LINE_ACCEPTED,    ///< Line accepted, before display finalized
    LLE_HOOK_LINE_FINISH,      ///< End of line editing (zle-line-finish)
    LLE_HOOK_BUFFER_MODIFIED,  ///< Buffer content changed
    LLE_HOOK_PRE_COMMAND,      ///< Before command execution (precmd)
    LLE_HOOK_POST_COMMAND,     ///< After command execution
    LLE_HOOK_COMPLETION_START, ///< Tab completion triggered
    LLE_HOOK_COMPLETION_END,   ///< Completion finished
    LLE_HOOK_HISTORY_SEARCH,   ///< History search started
    LLE_HOOK_TERMINAL_RESIZE,  ///< Terminal resized
    LLE_HOOK_COUNT             ///< Total hook types (not a valid hook)
} lle_widget_hook_type_t;

/**
 * Hook registration structure
 *
 * Represents a widget registered for a specific hook type.
 * Multiple widgets can be registered for the same hook.
 */
typedef struct lle_hook_registration {
    lle_widget_t *widget;               ///< Widget to execute
    lle_widget_hook_type_t hook_type;   ///< Hook type
    uint64_t trigger_count;             ///< Times triggered
    bool enabled;                       ///< Registration enabled
    struct lle_hook_registration *next; ///< Next registration in list
} lle_hook_registration_t;

/**
 * Widget hooks manager structure
 *
 * Manages all hook registrations and triggers. Maintains separate lists
 * for each hook type for efficient triggering.
 */
typedef struct lle_widget_hooks_manager {
    lle_widget_registry_t *registry; ///< Widget registry reference
    lle_hook_registration_t *hooks[LLE_HOOK_COUNT]; ///< Hooks per type
    lle_memory_pool_t *memory_pool;                 ///< Memory pool
    uint64_t total_hooks_triggered; ///< Total triggers across all hooks
    uint64_t hook_trigger_counts[LLE_HOOK_COUNT]; ///< Triggers per hook type
    bool hooks_enabled;                           ///< Global enable/disable
} lle_widget_hooks_manager_t;

/* ============================================================================
 * HOOKS MANAGER LIFECYCLE
 * ============================================================================
 */

/**
 * Initialize widget hooks manager
 *
 * Creates and initializes the hooks manager. The widget registry must
 * already be initialized and remain valid for the lifetime of the manager.
 *
 * @param manager Output pointer for created manager (must not be NULL)
 * @param registry Widget registry reference (must not be NULL)
 * @param memory_pool Memory pool for allocations (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if any parameter is NULL
 * @return LLE_ERROR_OUT_OF_MEMORY if allocation fails
 */
lle_result_t lle_widget_hooks_manager_init(lle_widget_hooks_manager_t **manager,
                                           lle_widget_registry_t *registry,
                                           lle_memory_pool_t *memory_pool);

/**
 * Destroy widget hooks manager
 *
 * Frees all hook registrations and manager resources. After this call,
 * the manager pointer is invalid and should not be used.
 *
 * Note: This does not destroy the widget registry, only the hook registrations.
 *
 * @param manager Manager to destroy (may be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if manager is NULL
 */
lle_result_t
lle_widget_hooks_manager_destroy(lle_widget_hooks_manager_t *manager);

/* ============================================================================
 * HOOK MANAGEMENT
 * ============================================================================
 */

/**
 * Register widget for hook
 *
 * Registers a widget to be executed when the specified hook is triggered.
 * Multiple widgets can be registered for the same hook and will be executed
 * in registration order.
 *
 * The widget must already be registered in the widget registry. The widget
 * name is looked up at registration time and the widget pointer is cached
 * for efficiency.
 *
 * @param manager Hooks manager (must not be NULL)
 * @param hook_type Type of hook (must be < LLE_HOOK_COUNT)
 * @param widget_name Name of widget to trigger (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if manager or widget_name is NULL
 * @return LLE_ERROR_INVALID_PARAMETER if hook_type >= LLE_HOOK_COUNT
 * @return LLE_ERROR_NOT_FOUND if widget does not exist in registry
 * @return LLE_ERROR_ALREADY_EXISTS if widget already registered for this hook
 * @return LLE_ERROR_OUT_OF_MEMORY if allocation fails
 */
lle_result_t lle_widget_hook_register(lle_widget_hooks_manager_t *manager,
                                      lle_widget_hook_type_t hook_type,
                                      const char *widget_name);

/**
 * Unregister widget from hook
 *
 * Removes widget from the specified hook. The widget will no longer be
 * executed when the hook is triggered.
 *
 * @param manager Hooks manager (must not be NULL)
 * @param hook_type Type of hook (must be < LLE_HOOK_COUNT)
 * @param widget_name Name of widget to remove (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if manager or widget_name is NULL
 * @return LLE_ERROR_INVALID_PARAMETER if hook_type >= LLE_HOOK_COUNT
 * @return LLE_ERROR_NOT_FOUND if widget not registered for this hook
 */
lle_result_t lle_widget_hook_unregister(lle_widget_hooks_manager_t *manager,
                                        lle_widget_hook_type_t hook_type,
                                        const char *widget_name);

/**
 * Trigger all widgets registered for a hook
 *
 * This is the main function called when a hook point is reached in the
 * editor lifecycle. It executes all widgets registered for the specified
 * hook type in registration order.
 *
 * Widget execution is error-resilient: if one widget fails, the remaining
 * widgets are still executed. The first error encountered is returned.
 *
 * Called by:
 * - input_widget_hooks.c for input-related hooks
 * - lle_readline.c for line editing hooks
 * - completion system for completion hooks
 * - history system for history hooks
 *
 * @param manager Hooks manager (must not be NULL)
 * @param hook_type Type of hook to trigger (must be < LLE_HOOK_COUNT)
 * @param editor Editor context (must not be NULL)
 * @return LLE_SUCCESS if all widgets executed successfully
 * @return LLE_ERROR_INVALID_PARAMETER if manager, editor is NULL or hook_type
 * invalid
 * @return First error code from widget execution (other widgets still executed)
 */
lle_result_t lle_widget_hook_trigger(lle_widget_hooks_manager_t *manager,
                                     lle_widget_hook_type_t hook_type,
                                     lle_editor_t *editor);

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * Get hook name string
 *
 * Returns human-readable name for hook type. Useful for debugging and logging.
 *
 * @param hook_type Hook type
 * @return Hook name string (static, never NULL)
 */
const char *lle_widget_hook_get_name(lle_widget_hook_type_t hook_type);

/**
 * Get widget count for hook
 *
 * Returns number of widgets registered for the specified hook type.
 *
 * @param manager Hooks manager (must not be NULL)
 * @param hook_type Hook type (must be < LLE_HOOK_COUNT)
 * @return Number of widgets registered for hook, or 0 if parameters invalid
 */
size_t lle_widget_hook_get_count(lle_widget_hooks_manager_t *manager,
                                 lle_widget_hook_type_t hook_type);

/**
 * Check if hooks are enabled globally
 *
 * @param manager Hooks manager (must not be NULL)
 * @return true if hooks enabled, false otherwise
 */
bool lle_widget_hooks_enabled(lle_widget_hooks_manager_t *manager);

/**
 * Enable hooks globally
 *
 * Enables hook triggering. When disabled, lle_widget_hook_trigger() returns
 * immediately without executing any widgets.
 *
 * @param manager Hooks manager (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if manager is NULL
 */
lle_result_t lle_widget_hooks_enable(lle_widget_hooks_manager_t *manager);

/**
 * Disable hooks globally
 *
 * Disables hook triggering. Useful for temporarily suspending hooks during
 * complex operations.
 *
 * @param manager Hooks manager (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if manager is NULL
 */
lle_result_t lle_widget_hooks_disable(lle_widget_hooks_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /// LLE_WIDGET_HOOKS_H
