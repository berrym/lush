/**
 * @file lle_shell_integration.h
 * @brief LLE Shell Integration - Centralized LLE initialization and lifecycle
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This module provides the shell-level LLE initialization system that:
 * - Initializes LLE subsystems in proper sequence
 * - Manages the global LLE editor instance
 * - Provides three-tier reset hierarchy (Soft/Hard/Nuclear)
 * - Tracks initialization state and error recovery
 * - Coordinates with shell event hub for lifecycle events
 *
 * Initialization sequence:
 * 1. Verify global_memory_pool exists
 * 2. Verify terminal detection complete
 * 3. Create shell event hub
 * 4. Create and configure LLE editor
 * 5. Initialize history system
 * 6. Register atexit handler for cleanup
 *
 * Reset hierarchy:
 * - Soft Reset: Abort current line, return to prompt
 * - Hard Reset: Save history, destroy/recreate editor
 * - Nuclear Reset: Hard reset + terminal reset sequences
 *
 * Specification: docs/lle_specification/26_initialization_system_complete.md
 * Date: 2025-01-16
 */

#ifndef LLE_SHELL_INTEGRATION_H
#define LLE_SHELL_INTEGRATION_H

#include "lle/arena.h"
#include "lle/error_handling.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_event_hub.h"

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration - full type in lle/prompt/composer.h */
typedef struct lle_prompt_composer lle_prompt_composer_t;

/* ============================================================================
 * INITIALIZATION STATE
 * ============================================================================
 */

/**
 * @brief Initialization state tracking
 *
 * Tracks which LLE subsystems have been successfully initialized.
 * Used for proper cleanup and graceful degradation on partial init.
 */
typedef struct lle_init_state {
    bool memory_pool_verified;  /**< global_memory_pool exists and valid */
    bool terminal_detected;     /**< Terminal capabilities detected */
    bool event_hub_initialized; /**< Shell event hub created */
    bool editor_initialized;    /**< LLE editor created and configured */
    bool history_initialized;   /**< History system loaded */
    bool prompt_initialized;    /**< Prompt composer registered (Spec 25) */
    bool shell_hooks_installed; /**< Shell integration hooks active */
    bool atexit_registered;     /**< Cleanup handler registered */
} lle_init_state_t;

/* ============================================================================
 * SHELL INTEGRATION STRUCTURE
 * ============================================================================
 */

/**
 * @brief Shell integration structure
 *
 * Central structure for LLE shell integration. Contains the LLE editor,
 * shell event hub, prompt composer, initialization state, and safety tracking.
 *
 * Lifecycle:
 * - Created by lle_shell_integration_init() during shell startup
 * - Accessed via g_lle_integration global pointer
 * - Destroyed by lle_shell_integration_shutdown() or atexit handler
 */
typedef struct lle_shell_integration {
    /* Initialization state */
    lle_init_state_t init_state; /**< Which subsystems are initialized */

    /* Memory management - session arena is root of arena hierarchy */
    lle_arena_t *session_arena; /**< Session-lifetime arena (root) */

    /* Core components */
    lle_editor_t *editor;                   /**< LLE editor instance */
    lle_shell_event_hub_t *event_hub;       /**< Shell event hub */
    lle_prompt_composer_t *prompt_composer; /**< Prompt composer (Spec 25) */

    /* Safety and recovery state */
    bool recovery_mode;           /**< True if in recovery after error */
    uint32_t error_count;         /**< Consecutive error count */
    uint32_t ctrl_g_count;        /**< Ctrl+G count for panic detection */
    uint64_t last_ctrl_g_time_us; /**< Timestamp of last Ctrl+G */

    /* Statistics */
    uint64_t total_readline_calls; /**< Total lle_readline() invocations */
    uint64_t successful_reads;     /**< Successful line reads */
    uint64_t recovery_count;       /**< Times recovery was triggered */
    uint64_t hard_reset_count;     /**< Hard resets performed */
    uint64_t nuclear_reset_count;  /**< Nuclear resets performed */

    /* Timestamps */
    uint64_t init_time_us;       /**< When integration was initialized */
    uint64_t last_reset_time_us; /**< When last reset occurred */
} lle_shell_integration_t;

/* ============================================================================
 * GLOBAL INSTANCE
 * ============================================================================
 */

/**
 * @brief Global shell integration instance
 *
 * Pointer to the shell integration structure. NULL when LLE is not active.
 * Checked by shell main loop to determine whether to use LLE or GNU readline.
 */
extern lle_shell_integration_t *g_lle_integration;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize LLE shell integration
 *
 * Called during shell startup (in init.c) after display integration.
 * Initializes all LLE subsystems in proper sequence:
 * 1. Verifies prerequisites (memory pool, terminal)
 * 2. Creates shell event hub
 * 3. Creates LLE editor with all subsystems
 * 4. Loads command history
 * 5. Registers atexit cleanup handler
 *
 * On failure, cleans up partial state and returns error code.
 * The shell should fall back to GNU readline if this fails.
 *
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_shell_integration_init(void);

/**
 * @brief Shutdown LLE shell integration
 *
 * Called during shell exit. Saves history, destroys editor and event hub,
 * and frees all resources. Safe to call multiple times.
 *
 * Also called by atexit handler if not explicitly called.
 */
void lle_shell_integration_shutdown(void);

/**
 * @brief Get the shell integration instance
 *
 * Returns the global shell integration instance, or NULL if not initialized.
 * Prefer using g_lle_integration directly for simple null checks.
 *
 * @return Shell integration instance or NULL
 */
lle_shell_integration_t *lle_get_shell_integration(void);

/**
 * @brief Check if LLE is active
 *
 * Quick check for whether LLE shell integration is initialized and active.
 * Used by shell main loop to choose between LLE and GNU readline.
 *
 * @return true if LLE is active, false otherwise
 */
bool lle_is_active(void);

/**
 * @brief Get the global LLE editor instance
 *
 * Returns the global LLE editor instance used by the shell.
 * The editor is created during shell initialization and destroyed on exit.
 *
 * @return The global editor instance, or NULL if LLE is not initialized
 */
lle_editor_t *lle_get_global_editor(void);

/**
 * @brief Update the shell prompt
 *
 * Renders the prompt using the LLE prompt composer and updates PS1/PS2
 * in the symbol table. This is the main entry point for prompt generation.
 *
 * Actions:
 * - Updates background job count from executor
 * - Calls lle_composer_render() to generate prompt strings
 * - Sets PS1 and PS2 in the symbol table
 * - Clears the regeneration flag
 *
 * If the prompt composer is not initialized or rendering fails,
 * falls back to a minimal "$ " or "# " prompt.
 */
void lle_shell_update_prompt(void);

/**
 * @brief Get the most recently rendered PS1 prompt
 *
 * Returns a pointer to the static buffer holding the rendered prompt.
 * Valid after lle_shell_update_prompt() has been called. The returned
 * string is the fully expanded prompt ready for display.
 *
 * @return Rendered prompt string (static, do not free)
 */
const char *lle_shell_get_rendered_prompt(void);

/**
 * @brief Notify that PS1 or PROMPT was set by user code
 *
 * Called from the executor's variable assignment path. Marks PS1 as
 * user-owned so the theme system won't overwrite it. Also syncs
 * PROMPT ↔ PS1 bidirectionally.
 *
 * @param var_name  Variable that was set ("PS1", "PS2", or "PROMPT")
 * @param value     The value that was assigned
 */
void lle_shell_notify_prompt_var_set(const char *var_name, const char *value);

/* ============================================================================
 * RESET FUNCTIONS (THREE-TIER HIERARCHY)
 * ============================================================================
 */

/**
 * @brief Soft reset - Abort current line
 *
 * Tier 1 reset: Aborts the current line editing operation and returns
 * the user to a fresh prompt. Does not destroy any state.
 *
 * Triggered by:
 * - Ctrl+G (abort)
 * - Recoverable errors during line editing
 *
 * Actions:
 * - Sets abort flag on editor
 * - Clears current buffer
 * - Returns empty string from lle_readline()
 */
void lle_soft_reset(void);

/**
 * @brief Hard reset - Destroy and recreate editor
 *
 * Tier 2 reset: Destroys the LLE editor and recreates it from scratch.
 * Used when editor state may be corrupted.
 *
 * Triggered by:
 * - Triple Ctrl+G within 2 seconds
 * - 5 consecutive errors
 * - `display lle reset` command
 *
 * Actions:
 * - Saves command history
 * - Destroys current editor
 * - Creates new editor instance
 * - Reloads history
 * - Resets error counters
 */
void lle_hard_reset(void);

/**
 * @brief Nuclear reset - Hard reset plus terminal reset
 *
 * Tier 3 reset: Performs hard reset and also resets terminal state.
 * Used when terminal may be in corrupted state.
 *
 * Triggered by:
 * - `display lle reset --terminal` command
 * - Detection of terminal corruption
 *
 * Actions:
 * - Performs hard reset
 * - Sends terminal reset sequence (ESC c)
 * - Re-detects terminal capabilities
 * - Reinitializes display system
 */
void lle_nuclear_reset(void);

/* ============================================================================
 * ERROR TRACKING
 * ============================================================================
 */

/**
 * @brief Record an error for tracking
 *
 * Called when an error occurs during LLE operation. Increments error
 * counter and may trigger automatic reset if threshold exceeded.
 *
 * @param error The error code that occurred
 */
void lle_record_error(lle_result_t error);

/**
 * @brief Reset error counter
 *
 * Called after successful operation to reset the consecutive error count.
 * Prevents accumulated errors from triggering spurious resets.
 */
void lle_reset_error_counter(void);

/**
 * @brief Record Ctrl+G press for panic detection
 *
 * Called when Ctrl+G is pressed. Tracks timing for triple-Ctrl+G
 * panic detection which triggers hard reset.
 */
void lle_record_ctrl_g(void);

/* ============================================================================
 * CONFIGURATION
 * ============================================================================
 */

/** Error threshold for automatic hard reset */
#define LLE_ERROR_THRESHOLD 5

/** Ctrl+G count for panic detection */
#define LLE_CTRL_G_PANIC_COUNT 3

/** Time window for Ctrl+G panic detection (microseconds) */
#define LLE_CTRL_G_PANIC_WINDOW_US 2000000

/* ============================================================================
 * EDITING MODE
 * ============================================================================
 */

/**
 * @brief Update editing mode from shell options
 *
 * Syncs LLE editor mode with shell_opts.vi_mode/emacs_mode settings.
 * Called when user changes mode via `set -o vi` or `set -o emacs`.
 */
void lush_update_editing_mode(void);

/**
 * @brief Shell-facing readline wrapper
 *
 * Reads a line of input using LLE. This is the main entry point for
 * interactive input in the shell.
 *
 * @param prompt The prompt to display (or NULL for default prompt)
 * @return Newly allocated string with the input line, or NULL on EOF/error
 */
char *lush_readline_with_prompt(const char *prompt);

#endif /* LLE_SHELL_INTEGRATION_H */
