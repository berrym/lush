/**
 * @file lle_editor.c
 * @brief LLE Editor Context Lifecycle Management
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements the lifecycle functions for lle_editor_t, including creation,
 * initialization, and destruction of the editor context and all its subsystems.
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md Date:
 * 2025-11-06
 */

#include "lle/lle_editor.h"
#include "lle/buffer_management.h"
#include "lle/completion/completion_system.h"
#include "lle/completion/custom_source.h"
#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/keybinding.h"
#include "lle/kill_ring.h"
#include "lle/memory_management.h"
#include "lle/widget_hooks.h"
#include "lle/widget_system.h"
#include <stdlib.h>
#include <string.h>

/// Forward declarations for builtin widget registration
lle_result_t lle_register_builtin_widgets(lle_widget_registry_t *registry);
lle_result_t
lle_register_builtin_widget_hooks(lle_widget_hooks_manager_t *hooks_manager);

/* ============================================================================
 * EDITOR LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * Create a new editor context
 *
 * Initializes the editor with core subsystems. Subsystems are initialized
 * in dependency order:
 * 1. Memory pool (if not provided)
 * 2. Buffer
 * 3. Cursor manager (depends on buffer)
 * 4. Kill ring
 * 5. Other subsystems initialized on-demand
 *
 * @param editor Pointer to receive new editor context
 * @param pool Memory pool for allocations (NULL = use global pool)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_create(lle_editor_t **editor,
                               lush_memory_pool_t *pool) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Use global pool if none provided
    if (!pool) {
        pool = global_memory_pool;
    }

    /// Allocate editor structure
    lle_editor_t *ed = (lle_editor_t *)lle_pool_alloc(sizeof(lle_editor_t));
    if (!ed) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "editor",
                         "editor allocation failed");
    }

    /// Zero-initialize entire structure
    memset(ed, 0, sizeof(lle_editor_t));

    /// Store Lush memory pool reference
    ed->lush_pool = pool;

    /// Create LLE memory pool wrapper for unified memory management
    lle_result_t result = lle_memory_pool_create_from_lush(
        &ed->lle_pool, pool, LLE_POOL_BUFFER /// Editor uses buffer pool type
    );
    if (result != LLE_SUCCESS) {
        lle_pool_free(ed);
        return result;
    }

    /// Initialize editing mode to Emacs (default)
    ed->editing_mode = LLE_EDITING_MODE_EMACS;
    ed->current_keymap = LLE_KEYMAP_EMACS;

    /// Create buffer (core subsystem)
    result = lle_buffer_create(&ed->buffer, pool, 0);
    if (result != LLE_SUCCESS) {
        lle_pool_free(ed);
        return result;
    }

    /// Create cursor manager (depends on buffer)
    result = lle_cursor_manager_init(&ed->cursor_manager, ed->buffer);
    if (result != LLE_SUCCESS) {
        lle_buffer_destroy(ed->buffer);
        lle_pool_free(ed);
        return result;
    }

    /// Create kill ring with default max entries (100) using unified pool
    result = lle_kill_ring_create(&ed->kill_ring, 100, ed->lle_pool);
    if (result != LLE_SUCCESS) {
        lle_cursor_manager_destroy(ed->cursor_manager);
        lle_buffer_destroy(ed->buffer);
        lle_memory_pool_destroy(ed->lle_pool);
        lle_pool_free(ed);
        return result;
    }

    /// Create change tracker for undo/redo support
    result = lle_change_tracker_init(&ed->change_tracker, pool,
                                     LLE_BUFFER_MAX_UNDO_LEVELS);
    if (result != LLE_SUCCESS) {
        lle_kill_ring_destroy(ed->kill_ring);
        lle_cursor_manager_destroy(ed->cursor_manager);
        lle_buffer_destroy(ed->buffer);
        lle_memory_pool_destroy(ed->lle_pool);
        lle_pool_free(ed);
        return result;
    }

    /// Enable change tracking on the buffer
    ed->buffer->change_tracking_enabled = true;

    /// Create completion system (Spec 12) using unified pool
    result = lle_completion_system_create(ed->lle_pool, &ed->completion_system);
    if (result != LLE_SUCCESS) {
        lle_change_tracker_destroy(ed->change_tracker);
        lle_kill_ring_destroy(ed->kill_ring);
        lle_cursor_manager_destroy(ed->cursor_manager);
        lle_buffer_destroy(ed->buffer);
        lle_memory_pool_destroy(ed->lle_pool);
        lle_pool_free(ed);
        return result;
    }

    /// Initialize custom completion source subsystem
    result = lle_custom_source_init(ed->completion_system->source_manager,
                                    ed->lle_pool);
    if (result != LLE_SUCCESS) {
        /// Non-fatal - custom sources won't be available but continue
    }

    /// Load user completion config (non-fatal if fails)
    lle_completion_load_config();

    /// Initialize statistics
    ed->total_keystrokes = 0;
    ed->command_count = 0;
    ed->edit_session_start = 0; /// Will be set when editing starts

    /// Other subsystems (history, display, etc.) are initialized on-demand
    ed->history_system = NULL;
    ed->history_buffer_integration = NULL;
    ed->keybinding_manager = NULL;
    ed->display_controller = NULL;
    ed->vi_state = NULL;
    ed->plugin_manager = NULL;
    ed->script_integration = NULL;
    ed->history_edit_callbacks = NULL;

    /// Initialize state flags
    ed->history_search_active = false;
    ed->history_search_direction = 0;
    ed->quoted_insert_mode = false;

    /// === Widget System Initialization (Spec 07) ===
    /// Initialize widget registry for ZSH-style widget system
    result = lle_widget_registry_init(&ed->widget_registry, ed->lle_pool);
    if (result != LLE_SUCCESS) {
        /// Widget system initialization failed - non-fatal, continue without
        /// widgets
        ed->widget_registry = NULL;
        ed->widget_hooks_manager = NULL;
    } else {
        /// Register builtin widgets
        lle_register_builtin_widgets(ed->widget_registry);

        /// Initialize widget hooks manager
        result = lle_widget_hooks_manager_init(
            &ed->widget_hooks_manager, ed->widget_registry, ed->lle_pool);
        if (result != LLE_SUCCESS) {
            /// Hooks manager failed - widgets work but hooks won't trigger
            ed->widget_hooks_manager = NULL;
        } else {
            /// Register builtin widget hooks (e.g., transient-prompt)
            lle_register_builtin_widget_hooks(ed->widget_hooks_manager);
        }
    }

    *editor = ed;
    return LLE_SUCCESS;
}

/**
 * Destroy editor context
 *
 * Frees all resources associated with the editor, including all subsystems.
 * Subsystems are destroyed in reverse dependency order.
 *
 * @param editor Editor context to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_destroy(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Destroy subsystems in reverse order of dependencies

    /// Destroy widget hooks manager first (depends on widget registry)
    if (editor->widget_hooks_manager) {
        lle_widget_hooks_manager_destroy(editor->widget_hooks_manager);
        editor->widget_hooks_manager = NULL;
    }

    /// Destroy widget registry
    if (editor->widget_registry) {
        lle_widget_registry_destroy(editor->widget_registry);
        editor->widget_registry = NULL;
    }

    /// Destroy optional subsystems if allocated
    if (editor->vi_state) {
        lle_pool_free(editor->vi_state);
        editor->vi_state = NULL;
    }

    /// Free history navigation seen hashes array
    if (editor->history_nav_seen_hashes) {
        free(editor->history_nav_seen_hashes);
        editor->history_nav_seen_hashes = NULL;
        editor->history_nav_seen_count = 0;
        editor->history_nav_seen_capacity = 0;
    }

    /// Free history navigation display stack (issue #40 symmetric navigation)
    if (editor->history_nav_display_stack) {
        free(editor->history_nav_display_stack);
        editor->history_nav_display_stack = NULL;
        editor->history_nav_display_count = 0;
        editor->history_nav_display_capacity = 0;
    }

    /// Destroy keybinding manager
    if (editor->keybinding_manager) {
        lle_keybinding_manager_destroy(editor->keybinding_manager);
        editor->keybinding_manager = NULL;
    }

    /// Destroy history system
    if (editor->history_system) {
        lle_history_core_destroy(editor->history_system);
        editor->history_system = NULL;
    }

    /// Shutdown custom completion sources before destroying completion system
    lle_custom_source_shutdown();

    /// Destroy completion system (Spec 12)
    if (editor->completion_system) {
        lle_completion_system_destroy(editor->completion_system);
        editor->completion_system = NULL;
    }

    /// Destroy change tracker
    if (editor->change_tracker) {
        lle_change_tracker_destroy(editor->change_tracker);
        editor->change_tracker = NULL;
    }

    /// Destroy kill ring
    if (editor->kill_ring) {
        lle_kill_ring_destroy(editor->kill_ring);
        editor->kill_ring = NULL;
    }

    /// Destroy cursor manager
    if (editor->cursor_manager) {
        lle_cursor_manager_destroy(editor->cursor_manager);
        editor->cursor_manager = NULL;
    }

    /// Destroy buffer
    if (editor->buffer) {
        lle_buffer_destroy(editor->buffer);
        editor->buffer = NULL;
    }

    /// Destroy LLE memory pool wrapper
    if (editor->lle_pool) {
        lle_memory_pool_destroy(editor->lle_pool);
        editor->lle_pool = NULL;
    }

    /// Note: lush_pool is not destroyed here - it's managed externally

    /// Free editor structure
    lle_pool_free(editor);

    return LLE_SUCCESS;
}

/**
 * Reset editor to initial state
 *
 * Clears the buffer and resets cursor to beginning, but keeps all
 * subsystems initialized.
 *
 * @param editor Editor context to reset
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_reset(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear buffer
    lle_result_t result = lle_buffer_clear(editor->buffer);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Reset cursor to beginning
    if (editor->cursor_manager) {
        result =
            lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, 0);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    /// Clear history search state
    editor->history_search_active = false;
    editor->history_search_direction = 0;

    /// Clear quoted insert mode
    editor->quoted_insert_mode = false;

    return LLE_SUCCESS;
}

/**
 * Get buffer contents as string
 *
 * Returns the current buffer contents. The returned string is owned by
 * the buffer and should not be modified or freed by the caller.
 *
 * @param editor Editor context
 * @return Buffer contents or NULL on error
 */
const char *lle_editor_get_content(const lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return NULL;
    }

    return editor->buffer->data;
}

/**
 * Check if editor has unsaved changes
 *
 * @param editor Editor context
 * @return true if buffer has been modified, false otherwise
 */
bool lle_editor_has_unsaved_changes(const lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return false;
    }

    return editor->buffer->modification_count > 0;
}

/**
 * Cancel current operation
 *
 * Used for Ctrl-G (abort) functionality. Resets any active modes
 * and returns editor to normal state.
 *
 * @param editor Editor context
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_cancel_operation(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Cancel history search if active
    editor->history_search_active = false;
    editor->history_search_direction = 0;

    /// Exit quoted insert mode
    editor->quoted_insert_mode = false;

    /// If in vi command mode, return to insert mode
    if (editor->editing_mode == LLE_EDITING_MODE_VI_COMMAND) {
        editor->editing_mode = LLE_EDITING_MODE_VI_INSERT;
    }

    return LLE_SUCCESS;
}
