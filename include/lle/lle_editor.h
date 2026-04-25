/**
 * @file lle_editor.h
 * @brief LLE Editor Context Structure
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Central editor context that provides access to all LLE subsystems.
 * This structure is passed to all keybinding action functions and provides
 * unified access to buffer, history, kill ring, and other editor components.
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md Date:
 * 2025-11-02
 */

#ifndef LLE_EDITOR_H
#define LLE_EDITOR_H

#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/keybinding.h"
#include "lle/kill_ring.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

typedef struct lle_display_controller lle_display_controller_t;
typedef struct lle_widget_registry lle_widget_registry_t;
typedef struct lle_widget_hooks_manager lle_widget_hooks_manager_t;
typedef struct lle_plugin_manager lle_plugin_manager_t;
typedef struct lle_script_integration lle_script_integration_t;
typedef struct lle_history_buffer_integration lle_history_buffer_integration_t;
/* lle_cursor_manager_t is already defined in buffer_management.h */

/* ============================================================================
 * EDITOR STRUCTURE
 * ============================================================================
 */

/**
 * Editor mode enumeration
 */
typedef enum {
    LLE_EDITING_MODE_EMACS,     /* Emacs keybindings (default) */
    LLE_EDITING_MODE_VI_INSERT, /* Vi insert mode */
    LLE_EDITING_MODE_VI_COMMAND /* Vi command mode */
} lle_editing_mode_t;

/**
 * Vi mode state (for vi keybindings)
 */
typedef struct {
    bool enabled;
    int count;             /* Repeat count for vi commands */
    char pending_operator; /* Pending operator (d, c, y, etc.) */
    bool visual_mode;      /* Visual selection mode */
} lle_vi_state_t;

/**
 * Editor context structure
 *
 * This is the central structure that provides access to all LLE subsystems.
 * It is passed to every keybinding action function to provide access to:
 * - Buffer management for text editing
 * - History system for command history
 * - Kill ring for yank/kill operations
 * - Keybinding manager for key processing
 * - Display controller for output
 * - Completion system for tab completion
 * - And other editor subsystems
 *
 * Design:
 * - All fields are pointers to allow lazy initialization
 * - NULL fields indicate subsystem not available (graceful degradation)
 * - Memory managed through unified memory pool
 * - Thread-safe when used from single editing thread
 */
typedef struct lle_editor {
    /* Core editing subsystems */
    lle_buffer_t *buffer;                 /* Text buffer */
    lle_cursor_manager_t *cursor_manager; /* Cursor position management */
    lle_kill_ring_t *kill_ring;           /* Kill/yank ring */
    lle_keybinding_manager_t *keybinding_manager; /* Key bindings */
    lle_change_tracker_t *change_tracker; /* Undo/redo change tracking */

    /* History and search */
    lle_history_core_t *history_system; /* Command history */
    lle_history_buffer_integration_t
        *history_buffer_integration; /* History<->buffer bridge */
    size_t history_navigation_pos; /* Current position in history navigation (0
                                      = current line) */
    bool history_search_active;    /* Interactive search active */
    int history_search_direction;  /* 1=forward, -1=reverse */

    /* Unique-only navigation tracking (for lle_dedup_navigation_unique) */
    uint32_t
        *history_nav_seen_hashes;  /* Hash set of commands seen this session */
    size_t history_nav_seen_count; /* Number of seen hashes */
    size_t history_nav_seen_capacity; /* Capacity of seen hash array */

    /* Navigation display stack for symmetric up/down navigation (issue #40)
     * Tracks which entries were actually displayed during up navigation
     * so down navigation can retrace the exact same path in reverse */
    size_t *history_nav_display_stack;   /* Stack of displayed entry indices */
    size_t history_nav_display_count;    /* Current stack depth */
    size_t history_nav_display_capacity; /* Stack capacity */

    /* Display and output */
    lle_display_controller_t *display_controller; /* Display management */

    /* Completion */
    struct lle_completion_system
        *completion_system; /* Tab completion (Spec 12) */

    /* Editing mode and state */
    lle_editing_mode_t editing_mode;  /* Current editing mode */
    lle_vi_state_t *vi_state;         /* Vi mode state (if enabled) */
    lle_keymap_mode_t current_keymap; /* Active keymap */

    /* Special modes */
    bool quoted_insert_mode; /* Ctrl-Q/Ctrl-V mode */
    bool eof_requested;      /* EOF signal (Ctrl-D on empty line) */
    bool abort_requested;    /* Abort signal (Ctrl-G) */

    /* Extensibility and Widgets */
    lle_widget_registry_t *widget_registry; /* Widget system registry */
    lle_widget_hooks_manager_t *widget_hooks_manager; /* Widget hooks manager */
    lle_plugin_manager_t *plugin_manager; /* Plugin system (future) */
    lle_script_integration_t
        *script_integration;      /* Script integration (future) */
    void *history_edit_callbacks; /* History edit callbacks (opaque) */

    /* Memory management */
    lush_memory_pool_t *lush_pool; /* Lush memory pool (source) */
    lle_memory_pool_t *lle_pool;   /* LLE memory pool (wrapper) */

    /* Editor statistics */
    uint64_t total_keystrokes;   /* Total keys processed */
    uint64_t command_count;      /* Commands executed */
    uint64_t edit_session_start; /* Session start timestamp */

} lle_editor_t;

/* ============================================================================
 * EDITOR LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * Create a new editor context
 *
 * Initializes the editor with all core subsystems. Some subsystems may be
 * initialized lazily on first use.
 *
 * @param editor Pointer to receive new editor context
 * @param pool Memory pool for allocations (NULL = use global pool)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_create(lle_editor_t **editor, lush_memory_pool_t *pool);

/**
 * Destroy editor context
 *
 * Frees all resources associated with the editor, including all subsystems.
 *
 * @param editor Editor context to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_destroy(lle_editor_t *editor);

/**
 * Initialize editor subsystem
 *
 * Initializes a specific subsystem on demand. Used for lazy initialization.
 *
 * @param editor Editor context
 * @param subsystem Name of subsystem to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_init_subsystem(lle_editor_t *editor,
                                       const char *subsystem);

/**
 * Reset editor to clean state
 *
 * Clears buffer, resets cursor, cancels active operations.
 * Used for Ctrl-G (abort) and other reset operations.
 *
 * @param editor Editor context
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_reset(lle_editor_t *editor);

/* ============================================================================
 * EDITOR HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * Cancel active operations
 *
 * Cancels any active operations like search, completion, etc.
 * Called by lle_abort_line() and other cancellation actions.
 *
 * @param editor Editor context
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_editor_cancel_active_operations(lle_editor_t *editor);

/**
 * Check if editor has unsaved changes
 *
 * @param editor Editor context
 * @return true if buffer has unsaved changes
 */
bool lle_editor_has_unsaved_changes(const lle_editor_t *editor);

/**
 * Get current buffer content
 *
 * @param editor Editor context
 * @return Pointer to buffer content (NULL-terminated), or NULL on error
 */
const char *lle_editor_get_content(const lle_editor_t *editor);

/**
 * Get current buffer length
 *
 * @param editor Editor context
 * @return Buffer length in bytes
 */
size_t lle_editor_get_content_length(const lle_editor_t *editor);

#endif /* LLE_EDITOR_H */
