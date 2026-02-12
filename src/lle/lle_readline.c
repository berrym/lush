/**
 * @file lle_readline.c
 * @brief LLE Readline Function - Core Orchestration
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This file implements lle_readline(), the main readline loop that orchestrates
 * all LLE subsystems to provide interactive line editing.
 *
 * CRITICAL: This implementation uses ONLY proper LLE subsystem APIs.
 * NO direct terminal I/O, NO escape sequences, NO architectural violations.
 *
 * Implementation: Step 7 - Signal Handling
 * - Creates local terminal abstraction instance
 * - Uses terminal abstraction for raw mode
 * - Uses input processor for reading
 * - Uses lle_buffer_t for proper buffer management
 * - Uses event system to dispatch events to handlers
 * - Uses display generator and display client for rendering
 * - Event handlers modify buffer (decoupled architecture)
 * - Display refreshed after buffer modifications
 * - Returns on Enter key
 * - Adds support for arrow keys (Left/Right cursor movement)
 * - Adds support for Home/End keys
 * - Adds support for Delete key
 * - Adds support for Ctrl-K (kill to end of line)
 * - Adds support for Ctrl-U (kill entire line)
 * - Adds multiline detection for unclosed quotes
 * - Inserts newline and continues reading if input incomplete
 * - Supports multi-line input within quoted strings
 *
 * Step 7 - Signal Handling:
 * - SIGWINCH: Window resize events trigger display refresh
 * - SIGTSTP: Ctrl-Z handled by terminal_unix_interface (exits raw mode before
 * suspend)
 * - SIGCONT: Resume handled by terminal_unix_interface (re-enters raw mode)
 * - SIGINT: Ctrl-C coordinated with lush's signal handler (src/signals.c)
 *   - Signal handler sets flag when SIGINT received during readline
 *   - Input loop checks flag and aborts line with ^C echo (bash-like behavior)
 *   - User gets fresh prompt without exiting shell
 * - All signal handlers installed by
 * lle_unix_interface_install_signal_handlers()
 *
 * Step 5 Enhancement - Complete Emacs Keybindings:
 * - Ctrl-A: Beginning of line
 * - Ctrl-B: Back one character
 * - Ctrl-E: End of line
 * - Ctrl-F: Forward one character
 * - Ctrl-G: Abort/cancel current line
 * - Ctrl-L: Clear screen and redraw
 * - Ctrl-W: Kill word backwards
 * - Ctrl-Y: Yank (paste) from kill buffer
 * - Kill buffer stores text from Ctrl-K/U/W for Ctrl-Y
 *
 * NOTE: Creates own terminal, buffer, and event system for now.
 * Future steps will integrate with full LLE system initialization.
 */

#include "config.h" /* For config_values_t and history config options */
#include "display/display_controller.h"
#include "display/prompt_layer.h"
#include "display_integration.h" /* Lush display integration */
#include "input_continuation.h"
#include "lle/arena.h" /* Hierarchical arena allocator */
#include "lle/buffer_management.h"
#include "lle/completion/completion_system.h" /* Completion system for menu visibility */
#include "lle/display_integration.h" /* Spec 08: Complete display integration */
#include "lle/error_handling.h"
#include "lle/event_system.h"
#include "lle/history.h"    /* History system for UP/DOWN navigation */
#include "lle/keybinding.h" /* Keybinding manager for Group 1+ migration */
#include "lle/keybinding_actions.h"    /* Smart arrow navigation functions */
#include "lle/notification.h"          /* Notification system for hints */
#include "lle/keybinding_config.h"     /* User keybinding configuration */
#include "lle/lle_editor.h"            /* Proper LLE editor architecture */
#include "lle/lle_readline_state.h"    /* State machine for input handling */
#include "lle/lle_shell_integration.h" /* Spec 26: Shell integration */
#include "lle/lle_watchdog.h" /* Watchdog timer for deadlock detection */
#include "lle/memory_management.h"
#include "lle/terminal_abstraction.h"
#include "lle/unicode_compare.h" /* TR#29 compliant Unicode prefix matching */
#include "lle/widget_hooks.h"    /* Widget hooks for lifecycle events */
#include "signals.h"             /* For SIGINT flag coordination with LLE */

/* Forward declarations for history action functions */
lle_result_t lle_history_previous(lle_editor_t *editor);
lle_result_t lle_history_next(lle_editor_t *editor);

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Global LLE editor instance (proper architecture) */
static lle_editor_t *global_lle_editor = NULL;

/**
 * @brief Get the global LLE editor instance
 *
 * Returns the editor from shell integration (Spec 26) if available,
 * otherwise falls back to the local global_lle_editor for backward
 * compatibility during lazy initialization.
 *
 * Allows other modules to access the editor for adding history, etc.
 */
lle_editor_t *lle_get_global_editor(void) {
    /* Prefer shell integration's editor (Spec 26) */
    if (g_lle_integration && g_lle_integration->editor) {
        return g_lle_integration->editor;
    }
    /* Fallback for backward compatibility */
    return global_lle_editor;
}

/**
 * @brief Populate LLE history config from Lush config system
 * Maps LLE history config options from config.h to lle_history_config_t
 *
 * This bridges Lush's config system with LLE's history core, ensuring
 * all user preferences are properly applied to the history subsystem.
 */
static void
populate_history_config_from_lush_config(lle_history_config_t *hist_config) {
    if (!hist_config)
        return;

    /* Initialize to zero */
    memset(hist_config, 0, sizeof(lle_history_config_t));

    /* Capacity settings */
    hist_config->max_entries =
        config.history_size > 0 ? config.history_size : 5000;
    hist_config->max_command_length =
        8192; /* Support long multiline commands */

    /* File settings */
    if (config.lle_history_file && config.lle_history_file[0] != '\0') {
        hist_config->history_file_path = config.lle_history_file;
    } else {
        hist_config->history_file_path =
            NULL; /* Use default ~/.lush_history */
    }
    hist_config->auto_save = true;    /* Always auto-save for safety */
    hist_config->load_on_init = true; /* Load existing history on startup */

    /* Deduplication behavior */
    hist_config->ignore_duplicates =
        config.lle_enable_deduplication &&
        (config.lle_dedup_scope != LLE_DEDUP_SCOPE_NONE);

    /* Map config dedup strategy to history dedup strategy */
    switch (config.lle_dedup_strategy) {
    case LLE_DEDUP_STRATEGY_IGNORE:
        hist_config->dedup_strategy = LLE_DEDUP_IGNORE;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_FREQUENT:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_FREQUENT;
        break;
    case LLE_DEDUP_STRATEGY_MERGE:
        hist_config->dedup_strategy = LLE_DEDUP_MERGE_METADATA;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_ALL:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_ALL;
        break;
    case LLE_DEDUP_STRATEGY_KEEP_RECENT:
    default:
        hist_config->dedup_strategy = LLE_DEDUP_KEEP_RECENT;
        break;
    }

    /* Map dedup scope (issue #41) */
    switch (config.lle_dedup_scope) {
    case LLE_DEDUP_SCOPE_NONE:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_NONE;
        break;
    case LLE_DEDUP_SCOPE_SESSION:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_SESSION;
        break;
    case LLE_DEDUP_SCOPE_RECENT:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_RECENT;
        break;
    case LLE_DEDUP_SCOPE_GLOBAL:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_GLOBAL;
        break;
    default:
        hist_config->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_SESSION;
        break;
    }

    /* Configure Unicode normalization for dedup comparison */
    hist_config->unicode_normalize = config.lle_dedup_unicode_normalize;

    hist_config->ignore_space_prefix =
        false; /* Standard bash behavior: space = don't save */

    /* Metadata to save */
    hist_config->save_timestamps =
        config.history_timestamps; /* Use global setting */
    hist_config->save_working_dir = config.lle_enable_forensic_tracking;
    hist_config->save_exit_codes = config.lle_enable_forensic_tracking;

    /* Performance settings */
    hist_config->initial_capacity =
        config.lle_enable_history_cache && config.lle_cache_size > 0
            ? config.lle_cache_size
            : 1000;
    hist_config->use_indexing =
        config.lle_enable_history_cache; /* Enable fast lookups if cache enabled
                                          */
}

/* Event handler context for Step 6 */
typedef struct readline_context {
    lle_buffer_t *buffer;
    bool *done;
    char **final_line;
    lle_terminal_abstraction_t *term;
    const char *prompt;
    continuation_state_t
        *continuation_state; /* Step 6: Shared multiline parser state */
    char *kill_buffer; /* Step 5 enhancement: Simple kill buffer for yank */
    size_t kill_buffer_size; /* Allocated size of kill buffer */

    /* Memory management - edit arena for per-readline allocations */
    lle_arena_t *edit_arena; /* Edit-session arena (child of session arena) */

    /* LLE Editor - proper architecture */
    lle_editor_t *editor; /* Full LLE editor context */

    /* Keybinding manager - incremental migration (Group 1+) */
    lle_keybinding_manager_t
        *keybinding_manager; /* Replaces hardcoded keybindings */

    /* Fish-style autosuggestions - direct LLE history integration */
    char *current_suggestion; /* Current suggestion text (the part to append) */
    size_t suggestion_alloc_size; /* Allocated size of suggestion buffer */
    bool suppress_autosuggestion; /* Temporarily suppress autosuggestion
                                     regeneration */

    /* Explicit state machine for input handling
     * Provides guaranteed exit paths and replaces implicit flag checks */
    lle_readline_state_t state;          /* Current state */
    lle_readline_state_t previous_state; /* For debugging/recovery */

    /* Notification system for transient hints (multiline history hint, etc.) */
    lle_notification_state_t notification; /* Notification state (inline, not pointer) */
} readline_context_t;

/* ============================================================================
 * CHANGE TRACKING HELPERS
 *
 * These helpers wrap buffer modifications in change sequences for undo/redo.
 * Each user action (keystroke, delete, etc.) is tracked as a sequence.
 * ============================================================================
 */

/**
 * @brief Begin a change sequence for tracking
 *
 * Call before making buffer modifications. Sets up buffer->current_sequence
 * so that buffer operations can record changes for undo.
 *
 * @param ctx Readline context with editor and buffer
 * @param description Description of the change (for debugging)
 * @return true if sequence started successfully, false otherwise
 */
static bool begin_change_sequence(readline_context_t *ctx,
                                  const char *description) {
    if (!ctx || !ctx->editor || !ctx->editor->change_tracker || !ctx->buffer) {
        return false;
    }

    /* Don't start a new sequence if one is already in progress */
    if (ctx->buffer->current_sequence) {
        return true; /* Already have a sequence */
    }

    lle_change_sequence_t *seq = NULL;
    lle_result_t result = lle_change_tracker_begin_sequence(
        ctx->editor->change_tracker, description, &seq);

    if (result == LLE_SUCCESS && seq) {
        ctx->buffer->current_sequence = seq;
        return true;
    }

    return false;
}

/**
 * @brief Complete the current change sequence
 *
 * Call after buffer modifications are done. Finalizes the sequence
 * so it can be undone as a single unit.
 *
 * @param ctx Readline context with editor and buffer
 */
static void end_change_sequence(readline_context_t *ctx) {
    if (!ctx || !ctx->editor || !ctx->editor->change_tracker || !ctx->buffer) {
        return;
    }

    if (ctx->buffer->current_sequence) {
        lle_change_tracker_complete_sequence(ctx->editor->change_tracker);
        ctx->buffer->current_sequence = NULL;
    }
}

/**
 * @brief Update autosuggestion based on current buffer content
 *
 * Searches LLE history for prefix matches and stores the suggestion
 * (the remaining text to append) in ctx->current_suggestion.
 *
 * Fish-style autosuggestion rules:
 * - Only suggest when cursor is at end of buffer
 * - Only suggest for non-empty input (>= 2 chars)
 * - Don't suggest if input ends with space
 * - Don't suggest in multiline mode
 * - Search history most-recent-first for best prefix match
 *
 * @param ctx Readline context with buffer and editor
 */
static void update_autosuggestion(readline_context_t *ctx) {
    /* Clear existing suggestion */
    if (ctx->current_suggestion) {
        ctx->current_suggestion[0] = '\0';
    }

    /* Bail out conditions */
    if (!ctx || !ctx->buffer || !ctx->editor || !ctx->editor->history_system) {
        return;
    }

    /* Check if autosuggestion regeneration is suppressed (e.g., during Ctrl+G
     * clear) */
    if (ctx->suppress_autosuggestion) {
        return;
    }

    /* Only suggest when cursor at end */
    if (ctx->buffer->cursor.byte_offset != ctx->buffer->length) {
        return;
    }

    /* Need at least 2 characters */
    if (ctx->buffer->length < 2) {
        return;
    }

    const char *input = ctx->buffer->data;
    if (!input || !*input) {
        return;
    }

    /* Don't suggest if ends with space */
    if (input[ctx->buffer->length - 1] == ' ') {
        return;
    }

    /* Don't suggest in multiline mode */
    if (strchr(input, '\n') != NULL) {
        return;
    }

    /* Search LLE history for prefix match (most recent first) */
    lle_history_core_t *history = ctx->editor->history_system;
    size_t count = 0;
    lle_history_get_entry_count(history, &count);

    if (count == 0) {
        return;
    }

    size_t input_len = ctx->buffer->length;

    /* Limit history search to prevent hangs with very large histories */
    #define MAX_SUGGESTION_SEARCH_ITERATIONS 5000
    size_t iterations = 0;

    /* Search backwards (most recent first) */
    for (size_t i = count; i > 0 && iterations < MAX_SUGGESTION_SEARCH_ITERATIONS; i--, iterations++) {
        /* Check watchdog every 500 iterations to allow abort on timeout */
        if (iterations > 0 && (iterations % 500) == 0 && lle_watchdog_check()) {
            return;  /* Watchdog fired - abort suggestion search */
        }

        lle_history_entry_t *entry = NULL;
        lle_result_t result =
            lle_history_get_entry_by_index(history, i - 1, &entry);

        if (result != LLE_SUCCESS || !entry || !entry->command) {
            continue;
        }

        /* Skip multiline history entries - don't suggest them for single-line
         * input This prevents suggesting "echo \"\nhello\"" when user types
         * "echo"
         *
         * Note: LLE history stores newlines as escaped "\n" (two chars:
         * backslash + n) not as actual newline bytes. Check for both formats.
         */
        if (strchr(entry->command, '\n') != NULL ||
            strstr(entry->command, "\\n") != NULL) {
            continue;
        }

        /* Check for prefix match using Unicode-aware comparison (TR#29
         * compliant) This handles NFC normalization to ensure equivalent
         * Unicode sequences match regardless of encoding (e.g., precomposed vs
         * decomposed) */
        if (lle_unicode_is_prefix(input, input_len, entry->command,
                                  strlen(entry->command), NULL)) {
            /* Found a match - get the remaining text */
            const char *remaining = entry->command + input_len;

            /* Only suggest if there's something to add */
            if (*remaining) {
                size_t remaining_len = strlen(remaining);

                /* Ensure buffer is large enough */
                if (remaining_len + 1 > ctx->suggestion_alloc_size) {
                    size_t new_size = remaining_len + 64;
                    char *new_buf = realloc(ctx->current_suggestion, new_size);
                    if (new_buf) {
                        ctx->current_suggestion = new_buf;
                        ctx->suggestion_alloc_size = new_size;
                    } else {
                        return; /* Allocation failed */
                    }
                }

                strcpy(ctx->current_suggestion, remaining);
                return; /* Found best match */
            }
        }
    }
}

/**
 * @brief Check if autosuggestion is available/visible
 *
 * Checks both the local suggestion buffer AND the display controller's
 * autosuggestions layer, since the displayed ghost text comes from the
 * display controller.
 */
static bool has_autosuggestion(readline_context_t *ctx) {
    if (!ctx)
        return false;

    /* Check local suggestion buffer */
    if (ctx->current_suggestion && ctx->current_suggestion[0] != '\0') {
        return true;
    }

    /* Also check display controller - it may have a suggestion we don't know
     * about */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        const char *dc_suggestion = display_controller_get_autosuggestion(dc);
        if (dc_suggestion && dc_suggestion[0] != '\0') {
            return true;
        }
    }

    return false;
}

/**
 * @brief Accept current autosuggestion into buffer
 * @return true if suggestion was accepted, false otherwise
 */
static bool accept_autosuggestion(readline_context_t *ctx) {
    if (!has_autosuggestion(ctx)) {
        return false;
    }

    /* Insert suggestion text at cursor (which is at end) */
    size_t suggestion_len = strlen(ctx->current_suggestion);
    lle_result_t result =
        lle_buffer_insert_text(ctx->buffer, ctx->buffer->cursor.byte_offset,
                               ctx->current_suggestion, suggestion_len);

    if (result == LLE_SUCCESS) {
        /* Sync cursor manager */
        if (ctx->editor && ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }

        /* Clear suggestion after acceptance */
        ctx->current_suggestion[0] = '\0';
        return true;
    }

    return false;
}

/* Forward declaration for refresh_display */
static void refresh_display(readline_context_t *ctx);

/**
 * @brief Context-aware RIGHT arrow action (Fish-style autosuggestion
 * acceptance)
 *
 * Priority order:
 * 1. If completion menu is active, navigate menu (delegate to lle_forward_char)
 * 2. If cursor at end of buffer with autosuggestion, accept suggestion
 * 3. Otherwise, move cursor right by one grapheme
 *
 * This is a context-aware action that requires readline_context_t access
 * for the autosuggestion buffer.
 */
lle_result_t lle_forward_char_or_accept_suggestion(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Priority 1: If completion menu is active, delegate to lle_forward_char
     * which handles menu navigation (move right in multi-column menu) */
    if (ctx->editor) {
        bool menu_active = false;

        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {
            menu_active = true;
        }

        if (menu_active) {
            /* Delegate to lle_forward_char which handles menu navigation */
            lle_result_t result = lle_forward_char(ctx->editor);
            refresh_display(ctx);
            return result;
        }
    }

    /* Priority 2: Fish-style autosuggestion - if at end of buffer with
     * suggestion, accept it */
    if (ctx->buffer->cursor.byte_offset == ctx->buffer->length &&
        has_autosuggestion(ctx)) {
        if (accept_autosuggestion(ctx)) {
            refresh_display(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Priority 3: Normal behavior - move cursor right */
    if (ctx->buffer->cursor.grapheme_index < ctx->buffer->grapheme_count &&
        ctx->editor && ctx->editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);
        lle_cursor_manager_move_by_graphemes(ctx->editor->cursor_manager, 1);
        lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                        &ctx->buffer->cursor);
        refresh_display(ctx);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Context-aware END key action (Fish-style autosuggestion acceptance)
 *
 * If cursor is already at end of buffer AND autosuggestion is available, accept
 * it. Otherwise, move cursor to end of line.
 *
 * This is a context-aware action that requires readline_context_t access
 * for the autosuggestion buffer.
 */
lle_result_t lle_end_of_line_or_accept_suggestion(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Fish-style: If already at end of buffer with suggestion, accept it
     * Note: Suggestion acceptance only applies at absolute buffer end,
     * since suggestions extend the entire command, not individual lines */
    if (ctx->buffer->cursor.byte_offset == ctx->buffer->length &&
        has_autosuggestion(ctx)) {
        if (accept_autosuggestion(ctx)) {
            refresh_display(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Normal behavior: move cursor to end of CURRENT LINE (not buffer)
     * This is critical for multiline mode - Ctrl+E should move to end of
     * the current logical line, while Alt+> moves to end of buffer */
    if (ctx->editor) {
        lle_end_of_line(ctx->editor);
        /* Sync buffer cursor from editor after the move */
        if (ctx->editor->cursor_manager) {
            lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                            &ctx->buffer->cursor);
        }
    } else {
        /* Fallback for no editor: move to buffer end (single-line behavior) */
        ctx->buffer->cursor.byte_offset = ctx->buffer->length;
    }

    refresh_display(ctx);

    return LLE_SUCCESS;
}

/**
 * @brief Find next word boundary in suggestion text
 *
 * Finds the end of the next word in the suggestion, used for partial
 * acceptance with Ctrl+Right.
 *
 * @param suggestion The suggestion text to search
 * @return Byte offset to end of next word (including trailing space if any)
 */
static size_t find_next_word_boundary_in_suggestion(const char *suggestion) {
    if (!suggestion || !*suggestion) {
        return 0;
    }

    size_t pos = 0;

    /* Skip leading whitespace */
    while (suggestion[pos] &&
           (suggestion[pos] == ' ' || suggestion[pos] == '\t')) {
        pos++;
    }

    /* Find end of word (non-whitespace characters) */
    while (suggestion[pos] && suggestion[pos] != ' ' &&
           suggestion[pos] != '\t') {
        pos++;
    }

    /* Include one trailing space if present (feels more natural) */
    if (suggestion[pos] == ' ') {
        pos++;
    }

    return pos;
}

/**
 * @brief Accept partial autosuggestion (one word) into buffer
 *
 * Inserts the next word from the suggestion into the buffer and updates
 * the suggestion to show remaining text.
 *
 * @param ctx Readline context
 * @return true if partial suggestion was accepted, false otherwise
 */
static bool accept_partial_autosuggestion(readline_context_t *ctx) {
    if (!has_autosuggestion(ctx)) {
        return false;
    }

    /* Find next word boundary */
    size_t word_len =
        find_next_word_boundary_in_suggestion(ctx->current_suggestion);
    if (word_len == 0) {
        return false;
    }

    /* Insert just the word portion at cursor (which is at end) */
    lle_result_t result =
        lle_buffer_insert_text(ctx->buffer, ctx->buffer->cursor.byte_offset,
                               ctx->current_suggestion, word_len);

    if (result == LLE_SUCCESS) {
        /* Sync cursor manager */
        if (ctx->editor && ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }

        /* Update suggestion to show remaining text */
        size_t remaining_len = strlen(ctx->current_suggestion) - word_len;
        if (remaining_len > 0) {
            memmove(ctx->current_suggestion, ctx->current_suggestion + word_len,
                    remaining_len + 1); /* +1 for null terminator */
        } else {
            /* No more suggestion text */
            ctx->current_suggestion[0] = '\0';
        }

        return true;
    }

    return false;
}

/**
 * @brief Refresh display WITHOUT regenerating autosuggestion
 *
 * Used after partial suggestion acceptance when we want to keep
 * the remaining suggestion text rather than regenerating from history.
 * This is a copy of refresh_display() but skips update_autosuggestion().
 */
static void refresh_display_keep_suggestion(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return;
    }

    /* Pass existing suggestion to display controller (don't regenerate) */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        display_controller_set_autosuggestion(dc, ctx->current_suggestion);
    }

    /* Get the global Spec 08 display integration instance */
    lle_display_integration_t *display_integration =
        lle_display_integration_get_global();
    if (!display_integration) {
        return;
    }

    /* Get render controller from display integration */
    lle_render_controller_t *render_controller =
        display_integration->render_controller;
    if (!render_controller) {
        return;
    }

    /* Render buffer content through Spec 08 render system */
    lle_render_output_t *render_output = NULL;
    lle_result_t result = lle_render_buffer_content(
        render_controller, ctx->buffer, &ctx->buffer->cursor, &render_output);

    if (result != LLE_SUCCESS || !render_output) {
        return;
    }

    /* Send rendered output through display_bridge */
    lle_display_bridge_t *display_bridge = display_integration->display_bridge;
    if (display_bridge) {
        lle_display_bridge_send_output(display_bridge, render_output,
                                       &ctx->buffer->cursor);
    }

    /* Free the render output */
    lle_render_output_free(render_output);
}

/**
 * @brief Context-aware Ctrl+Right action (Fish-style partial autosuggestion
 * acceptance)
 *
 * If cursor is at end of buffer AND autosuggestion is available, accept one
 * word. Otherwise, move cursor forward by one word.
 *
 * This is a context-aware action that requires readline_context_t access
 * for the autosuggestion buffer.
 */
lle_result_t
lle_forward_word_or_accept_partial_suggestion(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Fish-style: If at end of buffer with suggestion, accept one word */
    if (ctx->buffer->cursor.byte_offset == ctx->buffer->length &&
        has_autosuggestion(ctx)) {
        if (accept_partial_autosuggestion(ctx)) {
            /* Use special refresh that keeps remaining suggestion intact */
            refresh_display_keep_suggestion(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Normal behavior: move cursor forward by one word */
    if (ctx->editor) {
        lle_forward_word(ctx->editor);
        /* Sync buffer cursor from editor */
        if (ctx->editor->cursor_manager) {
            lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                            &ctx->buffer->cursor);
        }
        refresh_display(ctx);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Multiline detection using shared continuation parser
 * Step 6: Uses input_continuation.c for proper shell construct detection
 *
 * Analyzes buffer content using the shared multiline parser which handles:
 * - Quote tracking (single, double, backtick)
 * - Bracket/brace/parenthesis counting
 * - Control structures (if/then/fi, case, loops)
 * - Here documents
 * - Function definitions
 *
 * @param buffer_data The buffer content to check
 * @param state Continuation state to analyze with
 * @return true if input appears incomplete, false otherwise
 */
static bool is_input_incomplete(const char *buffer_data,
                                continuation_state_t *state) {
    if (buffer_data == NULL || state == NULL) {
        return false;
    }

    /* Reset state for fresh analysis */
    continuation_state_cleanup(state);
    continuation_state_init(state);

    /* Analyze the entire buffer content */
    continuation_analyze_line(buffer_data, state);

    /* Check if continuation is needed */
    return continuation_needs_continuation(state);
}

/**
 * @brief Refresh display after buffer modification using Spec 08 render system
 *
 * PROPER IMPLEMENTATION: Uses Spec 08 display integration components:
 * - render_controller: Coordinates rendering operations
 * - render_cache: Caches rendered output for performance
 * - display_bridge: Communicates with Lush display system
 *
 * This is the CORRECT way to render - using the Spec 08 rendering pipeline
 * instead of calling display_controller directly.
 */
static void refresh_display(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return;
    }

    /* Early abort if watchdog has fired - don't do expensive rendering */
    if (lle_watchdog_check()) {
        return;
    }

    /* Fish-style autosuggestions using LLE history
     *
     * This must happen BEFORE rendering so the ghost text is available
     * for display_controller to render in dc_handle_redraw_needed().
     *
     * We generate the suggestion here using LLE history (not GNU readline
     * history) and pass it directly to display_controller for rendering.
     */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        /* Generate suggestion from LLE history */
        update_autosuggestion(ctx);

        /* Pass suggestion to display controller for rendering */
        display_controller_set_autosuggestion(dc, ctx->current_suggestion);
    }

    /* Get the global Spec 08 display integration instance */
    lle_display_integration_t *display_integration =
        lle_display_integration_get_global();
    if (!display_integration) {
        /* Spec 08 display integration not initialized - cannot render */
        return;
    }

    /* Get render controller from display integration */
    lle_render_controller_t *render_controller =
        display_integration->render_controller;
    if (!render_controller) {
        return;
    }

    /* Render buffer content through Spec 08 render system */
    lle_render_output_t *render_output = NULL;
    lle_result_t result = lle_render_buffer_content(
        render_controller, ctx->buffer, &ctx->buffer->cursor, &render_output);

    if (result != LLE_SUCCESS || !render_output) {
        return;
    }

    /* SPEC 08 COMPLIANT: Send rendered output through display_bridge to
     * command_layer
     *
     * This is THE CORRECT ARCHITECTURE per Spec 08:
     * 1. render_controller produces rendered output
     * 2. display_bridge sends output to command_layer
     * 3. command_layer sends to display_controller
     * 4. display_controller handles terminal I/O
     *
     * NO DIRECT TERMINAL WRITES - this is a critical architectural principle!
     *
     * IMPORTANT: Always send to display_bridge even if buffer is empty, because
     * we still need to display the prompt. The prompt is rendered separately by
     * the prompt_layer and combined in the display_controller.
     */
    lle_display_bridge_t *display_bridge = display_integration->display_bridge;
    if (display_bridge) {
        result = lle_display_bridge_send_output(display_bridge, render_output,
                                                &ctx->buffer->cursor);

        if (result != LLE_SUCCESS) {
            /* Log error but continue - display bridge will track consecutive
             * errors */
        }
    }

    /* Free the render output */
    lle_render_output_free(render_output);
}

/**
 * @brief Event handler for character input
 * Step 4: Handler modifies buffer and refreshes display
 */
static lle_result_t handle_character_input(lle_event_t *event,
                                           void *user_data) {
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Get UTF-8 character from event */
    const char *utf8_char = event->event_data.key.utf8_char;
    size_t char_len = strlen(utf8_char);

    /* CRITICAL: Reset history navigation when user types a character */
    /* This follows bash/readline behavior: typing exits history mode */
    if (ctx->editor && ctx->editor->history_navigation_pos > 0) {
        ctx->editor->history_navigation_pos = 0;
        /* Clear the seen set for unique-only navigation mode */
        ctx->editor->history_nav_seen_count = 0;
    }

    /* Re-enable autosuggestion if it was suppressed (e.g., by Ctrl+G) */
    ctx->suppress_autosuggestion = false;

    /* Clear completion menu on character input */
    if (ctx->editor) {
        bool menu_cleared = false;

        /* Check and clear completion system */
        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {
            lle_completion_system_clear(ctx->editor->completion_system);
            menu_cleared = true;
        }

        /* Clear menu from display controller if any menu was cleared */
        if (menu_cleared) {
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_clear_completion_menu(dc);
            }
        }
    }

    /* Dismiss notification on character input (any action should dismiss it) */
    if (lle_notification_is_visible(&ctx->notification)) {
        lle_notification_dismiss(&ctx->notification);
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_clear_notification(dc);
        }
    }

    /* Begin change sequence for undo tracking */
    begin_change_sequence(ctx, "insert char");

    /* Insert character into buffer at cursor position */
    lle_result_t result = lle_buffer_insert_text(
        ctx->buffer, ctx->buffer->cursor.byte_offset, utf8_char, char_len);

    /* End change sequence */
    end_change_sequence(ctx);

    /* Synchronize cursor fields after insert (PHASE 2 STEP 0 FIX) */
    if (result == LLE_SUCCESS && ctx->editor && ctx->editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);
    }

    /* Refresh display after buffer modification */
    if (result == LLE_SUCCESS) {
        refresh_display(ctx);
    }

    return result;
}

/**
 * @brief Event handler for backspace
 * Step 4: Handler modifies buffer and refreshes display
 * PHASE 2 FIX: Delete entire grapheme cluster, not just one codepoint
 */
static lle_result_t handle_backspace(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* CRITICAL FIX: Reset history navigation when user backspaces */
    /* This follows bash/readline behavior: editing exits history mode */
    if (ctx->editor && ctx->editor->history_navigation_pos > 0) {
        ctx->editor->history_navigation_pos = 0;
        ctx->editor->history_nav_seen_count = 0;
    }

    /* CRITICAL FIX: Clear completion menu on backspace */
    /* Prevents stale menu state with indices pointing to deleted buffer
     * positions */
    if (ctx->editor) {
        bool menu_cleared = false;

        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {
            lle_completion_system_clear(ctx->editor->completion_system);
            menu_cleared = true;
        }

        if (menu_cleared) {
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_clear_completion_menu(dc);
                /* Also clear autosuggestion to prevent conflict with stale menu
                 */
                display_controller_set_autosuggestion(dc, NULL);
            }
            /* Clear current suggestion in context */
            if (ctx->current_suggestion) {
                ctx->current_suggestion[0] = '\0';
            }
            /* Suppress autosuggestion regeneration for this refresh cycle */
            ctx->suppress_autosuggestion = true;
        }
    }

    if (ctx->buffer->cursor.byte_offset > 0 && ctx->editor &&
        ctx->editor->cursor_manager) {
        /* Sync cursor manager position with buffer cursor before moving */
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);

        /* Now check if we can move back (after sync) */
        if (ctx->buffer->cursor.grapheme_index == 0) {
            return LLE_SUCCESS; /* Already at beginning */
        }

        /* Move cursor back by one grapheme to find the start of the grapheme to
         * delete */
        size_t current_byte = ctx->buffer->cursor.byte_offset;

        lle_result_t result = lle_cursor_manager_move_by_graphemes(
            ctx->editor->cursor_manager, -1);
        if (result == LLE_SUCCESS) {
            /* CRITICAL: Sync buffer cursor back from cursor manager after
             * movement */
            lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                            &ctx->buffer->cursor);

            size_t grapheme_start = ctx->buffer->cursor.byte_offset;
            size_t grapheme_len = current_byte - grapheme_start;

            /* Begin change sequence for undo tracking */
            begin_change_sequence(ctx, "backspace");

            /* Delete the entire grapheme cluster */
            result = lle_buffer_delete_text(ctx->buffer, grapheme_start,
                                            grapheme_len);

            /* End change sequence */
            end_change_sequence(ctx);

            /* Refresh display after buffer modification */
            if (result == LLE_SUCCESS) {
                refresh_display(ctx);
            }
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Enter key
 * Step 6: Check for multiline continuation before completing
 */
static lle_result_t handle_enter(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Check for incomplete input using shared continuation parser */
    bool incomplete =
        is_input_incomplete(ctx->buffer->data, ctx->continuation_state);

    if (incomplete) {
        /* SAFETY CHECK: Limit maximum line count to prevent infinite loops
         * If the parser has a bug and always reports incomplete, this prevents
         * the shell from inserting newlines forever. 1000 lines is generous
         * for any legitimate shell command but prevents runaway input.
         */
        size_t line_count = 1;
        for (const char *p = ctx->buffer->data; *p; p++) {
            if (*p == '\n') {
                line_count++;
            }
        }
        const size_t MAX_MULTILINE_LINES = 1000;
        if (line_count >= MAX_MULTILINE_LINES) {
            fprintf(stderr,
                    "\nlle: maximum line count (%zu) reached, forcing accept\n",
                    MAX_MULTILINE_LINES);
            incomplete = false; /* Force accept */
        }
    }

    if (incomplete) {
        /* Input incomplete - insert newline and continue */
        lle_result_t result = lle_buffer_insert_text(
            ctx->buffer, ctx->buffer->cursor.byte_offset, "\n", 1);

        /* Synchronize cursor fields after insert (PHASE 2 STEP 0 FIX) */
        if (result == LLE_SUCCESS && ctx->editor &&
            ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }

        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }

        return result;
    }

    /* Line complete - accept entire buffer regardless of cursor position */

    /* Clear autosuggestion ghost text before accepting line
     * Without this, partial suggestions remain visible after Enter */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        display_controller_set_autosuggestion(dc, NULL);
    }
    if (ctx->current_suggestion) {
        ctx->current_suggestion[0] = '\0';
    }

    /* Add to LLE history before completing */
    if (ctx->editor && ctx->editor->history_system && ctx->buffer->data &&
        ctx->buffer->data[0] != '\0') {
        lle_history_add_entry(ctx->editor->history_system, ctx->buffer->data, 0,
                              NULL);

        /* Save to history file (auto-save enabled in config) */
        const char *home = getenv("HOME");
        if (home) {
            char history_path[1024];
            snprintf(history_path, sizeof(history_path), "%s/.lush_history",
                     home);
            lle_history_save_to_file(ctx->editor->history_system, history_path);
        }
    }

    *ctx->done = true;
    *ctx->final_line =
        ctx->buffer->data ? strdup(ctx->buffer->data) : strdup("");

    return LLE_SUCCESS;
}

/**
 * @brief Context-aware ENTER key action (Dual-Action Architecture)
 *
 * Full readline-aware ENTER key handler with direct access to readline context.
 * This is a context-aware action (type LLE_ACTION_TYPE_CONTEXT) that has full
 * access to readline state, eliminating the need for flags or complex state
 * management.
 *
 * Architecture:
 * - Checks for incomplete input using continuation state parser
 * - If incomplete: inserts newline, syncs cursor, refreshes display, continues
 * editing
 * - If complete: adds to history, saves history file, sets done flag, returns
 * final line
 *
 * This replaces the flag-based approach (line_accepted flag) which caused
 * system-wide regressions due to complex state management and flag checking
 * after every action.
 *
 * See: docs/lle_implementation/DUAL_ACTION_ARCHITECTURE.md
 *
 * @param ctx Readline context with full access to buffer, history, completion
 * state
 * @return LLE_SUCCESS on successful handling
 */
lle_result_t lle_accept_line_context(readline_context_t *ctx) {
    if (!ctx || !ctx->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* If completion menu is active, accept the selected completion instead of
     * the line */
    if (ctx->editor) {
        /* Check completion system first */
        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {

            /* NOTE: The inline preview has already updated the buffer with the
             * selected completion text. When navigating completions
             * (UP/DOWN/TAB), update_inline_completion() replaces the current
             * word with each selected item. So the buffer already contains the
             * correct text.
             *
             * We just need to clear the menu and NOT modify the buffer further.
             * The old code tried to replace based on stale context, causing
             * duplicates.
             */

            /* Clear the completion menu */
            lle_completion_system_clear(ctx->editor->completion_system);
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_clear_completion_menu(dc);
            }

            /* Refresh display and return - don't accept line yet */
            refresh_display(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Check for incomplete input using shared continuation parser */
    bool incomplete =
        is_input_incomplete(ctx->buffer->data, ctx->continuation_state);

    if (incomplete) {
        /* Input incomplete - insert newline and continue editing */
        lle_result_t result = lle_buffer_insert_text(
            ctx->buffer, ctx->buffer->cursor.byte_offset, "\n", 1);

        /* Synchronize cursor fields after insert */
        if (result == LLE_SUCCESS && ctx->editor &&
            ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }

        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }

        return result;
    }

    /* Line complete - accept entire buffer regardless of cursor position */

    /* CRITICAL: Move buffer cursor to end before accepting
     *
     * Issue #1 Fix: When accepting multiline input, the buffer cursor must be
     * positioned at the end before the display system renders. This ensures
     * the screen cursor is positioned after all lines when returning to shell.
     *
     * This is the architecturally correct solution: move the cursor in the
     * buffer (LLE's responsibility) and let the display system render it
     * naturally (display system's responsibility). No direct terminal writes
     * from LLE.
     */
    if (ctx->buffer->length > 0 &&
        ctx->buffer->cursor.byte_offset != ctx->buffer->length) {
        /* Move buffer cursor to end */
        ctx->buffer->cursor.byte_offset = ctx->buffer->length;
        ctx->buffer->cursor.codepoint_index = ctx->buffer->length;
        ctx->buffer->cursor.grapheme_index = ctx->buffer->length;

        /* Sync cursor_manager with new position */
        if (ctx->editor && ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                                   ctx->buffer->length);
        }

        /* Refresh display to render cursor at new position */
        refresh_display(ctx);
    }

    /* Add to LLE history before completing */
    if (ctx->editor && ctx->editor->history_system && ctx->buffer->data &&
        ctx->buffer->data[0] != '\0') {
        lle_history_add_entry(ctx->editor->history_system, ctx->buffer->data, 0,
                              NULL);

        /* Save to history file (auto-save enabled in config) */
        const char *home = getenv("HOME");
        if (home) {
            char history_path[1024];
            snprintf(history_path, sizeof(history_path), "%s/.lush_history",
                     home);
            lle_history_save_to_file(ctx->editor->history_system, history_path);
        }
    }

    /* Signal completion to readline loop */
    *ctx->done = true;
    *ctx->final_line =
        ctx->buffer->data ? strdup(ctx->buffer->data) : strdup("");

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-D (EOF/delete-char)
 * Step 3: Handler signals EOF on empty line
 * Step 5 enhancement: Delete character at cursor when line is non-empty
 *
 * Ctrl-D has dual behavior in readline:
 * - Empty line: Signals EOF (exits shell)
 * - Non-empty line: Deletes character at cursor (same as Delete key)
 */
static lle_result_t handle_eof(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (ctx->buffer->length == 0) {
        /* EOF on empty line - exit shell */
        *ctx->done = true;
        *ctx->final_line = NULL;
    } else {
        /* Non-empty line - delete grapheme cluster at cursor (same as Delete
         * key) */
        if (ctx->buffer->cursor.grapheme_index < ctx->buffer->grapheme_count &&
            ctx->editor && ctx->editor->cursor_manager) {
            /* Sync cursor manager position with buffer cursor before moving */
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);

            /* Move cursor forward by one grapheme to find the end of the
             * grapheme to delete */
            size_t grapheme_start = ctx->buffer->cursor.byte_offset;

            lle_result_t result = lle_cursor_manager_move_by_graphemes(
                ctx->editor->cursor_manager, 1);
            if (result == LLE_SUCCESS) {
                /* CRITICAL: Sync buffer cursor back from cursor manager after
                 * movement */
                lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                                &ctx->buffer->cursor);

                size_t grapheme_end = ctx->buffer->cursor.byte_offset;
                size_t grapheme_len = grapheme_end - grapheme_start;

                /* Delete the entire grapheme cluster */
                result = lle_buffer_delete_text(ctx->buffer, grapheme_start,
                                                grapheme_len);

                /* Refresh display after buffer modification */
                if (result == LLE_SUCCESS) {
                    refresh_display(ctx);
                }
            }

            return result;
        }
    }

    return LLE_SUCCESS;
}

/*
 * NOTE: handle_interrupt() removed - Ctrl+C now handled via SIGINT signal.
 * With ISIG enabled in raw mode, Ctrl+C generates SIGINT caught by lush's
 * sigint_handler() in src/signals.c, which properly manages child processes.
 */

/**
 * @brief Event handler for Ctrl-G (abort/cancel line) - LEGACY FALLBACK
 * Step 5 enhancement: Abort readline and return empty line to shell
 *
 * This is the Emacs-style abort - it cancels the current input
 * and returns an empty line, causing the shell to display a fresh prompt.
 *
 * NOTE: This is the legacy fallback handler. The context-aware action
 * lle_abort_line_context() is now used when keybinding manager is available.
 */
static lle_result_t handle_abort(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Record Ctrl+G for panic detection (triple Ctrl+G triggers hard reset) */
    lle_record_ctrl_g();

    /* Signal done with empty result - this aborts the readline */
    *ctx->done = true;
    *ctx->final_line =
        strdup(""); /* Return empty string, not NULL (NULL signals EOF) */

    return LLE_SUCCESS;
}

/**
 * @brief Context-aware Ctrl-G action (abort-line) - Dual-Action Architecture
 *
 * Emacs-style abort that cancels current input and returns empty line to shell.
 * This is a context-aware action that directly manages readline completion
 * state.
 *
 * Unlike the flag-based simple action approach, this directly sets
 * done/final_line, eliminating the need for abort_requested flag and preventing
 * state persistence bugs.
 *
 * BEHAVIOR (tiered dismissal):
 * 1. If completion menu is visible: dismiss menu (first press)
 * 2. If autosuggestion is visible: clear suggestion (second press)
 * 3. If buffer is empty: no-op (nothing to abort)
 * 4. If nothing to dismiss and buffer has content: abort line and return to
 * shell
 *
 * @param ctx Readline context with full access to done/final_line
 * @return LLE_SUCCESS
 */
lle_result_t lle_abort_line_context(readline_context_t *ctx) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Tier 0: Check if notification is visible - dismiss it first */
    if (lle_notification_is_visible(&ctx->notification)) {
        lle_notification_dismiss(&ctx->notification);

        /* Clear from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_clear_notification(dc);
        }

        /* Refresh display to clear notification from screen */
        refresh_display(ctx);

        return LLE_SUCCESS; /* Notification dismissed, don't abort line */
    }

    /* Tier 1: Check if completion menu is active - if so, dismiss it first */
    if (ctx->editor) {
        bool menu_visible = false;

        /* Check completion system */
        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {
            menu_visible = true;
        }

        if (menu_visible) {
            /* Dismiss completion menu - don't abort line yet */
            if (ctx->editor->completion_system) {
                lle_completion_system_clear(ctx->editor->completion_system);
            }

            /* Clear menu from display controller */
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_clear_completion_menu(dc);
            }

            /* Refresh display to clear menu from screen */
            refresh_display(ctx);

            return LLE_SUCCESS; /* Menu dismissed, don't abort line */
        }
    }

    /* Check if autosuggestion is visible - if so, clear it first */
    if (has_autosuggestion(ctx)) {
        /* Clear the local autosuggestion buffer (if allocated) */
        if (ctx->current_suggestion) {
            ctx->current_suggestion[0] = '\0';
        }

        /* Suppress autosuggestion regeneration during this refresh
         * Otherwise refresh_display() will immediately regenerate it */
        ctx->suppress_autosuggestion = true;

        /* Clear from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_set_autosuggestion(dc, NULL);
        }

        /* Refresh display to redraw without ghost text */
        refresh_display(ctx);

        return LLE_SUCCESS; /* Suggestion cleared, don't abort line */
    }

    /* No completion menu or autosuggestion active - abort the line
     *
     * ZSH-style behavior: Always abort, even on empty buffer.
     * This serves as a recovery mechanism - if the display or editor gets into
     * a weird state, Ctrl+G guarantees a fresh start with a new prompt.
     * Unlike the no-op approach, this ensures users always have an escape
     * hatch.
     */

    /* Clear display before aborting to ensure clean state
     * This clears any ghost text, completion menu residue, etc. */

    /* Clear completion system state (CRITICAL: must clear both system AND
     * display) */
    if (ctx->editor && ctx->editor->completion_system) {
        lle_completion_system_clear(ctx->editor->completion_system);
    }

    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        /* Clear autosuggestion from display */
        display_controller_set_autosuggestion(dc, NULL);
        /* Clear completion menu if any */
        display_controller_clear_completion_menu(dc);
        /* Clear notification if any */
        display_controller_clear_notification(dc);
    }

    /* Clear notification state */
    lle_notification_dismiss(&ctx->notification);

    /* Suppress autosuggestion during final refresh */
    ctx->suppress_autosuggestion = true;

    /* Clear local suggestion buffer */
    if (ctx->current_suggestion) {
        ctx->current_suggestion[0] = '\0';
    }

    /* Final refresh to clear ghost text from screen before abort */
    refresh_display(ctx);

    /* Reset display state for fresh prompt on next lle_readline() call.
     * Don't call dc_finalize_input() here - it will be called in lle_readline()
     * cleanup when final_line is returned. Calling it here would cause double
     * newline. */
    dc_reset_prompt_display_state();

    /* Record Ctrl+G for panic detection (triple Ctrl+G triggers hard reset) */
    lle_record_ctrl_g();

    *ctx->done = true;
    *ctx->final_line =
        strdup(""); /* Return empty string, not NULL (NULL signals EOF) */

    return LLE_SUCCESS;
}

/**
 * @brief Context-aware ESC action - Dismiss completion menu and autosuggestion
 *
 * ESC dismisses completion menus and autosuggestions (tiered dismissal).
 * Unlike Ctrl-G, ESC does NOT abort the line - it only clears visual overlays.
 *
 * BEHAVIOR (tiered dismissal, like Ctrl-G but without abort):
 * 1. If completion menu is visible: dismiss menu (first press)
 * 2. If autosuggestion is visible: clear suggestion (second press)
 * 3. If nothing to dismiss: no-op (does NOT abort line)
 *
 * @param ctx Readline context with full access
 * @return LLE_SUCCESS
 */
lle_result_t lle_escape_context(readline_context_t *ctx) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Tier 0: Check if notification is visible - dismiss it first */
    if (lle_notification_is_visible(&ctx->notification)) {
        lle_notification_dismiss(&ctx->notification);

        /* Clear from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_clear_notification(dc);
        }

        /* Refresh display to clear notification from screen */
        refresh_display(ctx);

        return LLE_SUCCESS; /* Notification dismissed, stop here */
    }

    /* Tier 1: Check if completion menu is active - dismiss it first */
    if (ctx->editor) {
        bool menu_visible = false;

        /* Check completion system */
        if (ctx->editor->completion_system &&
            lle_completion_system_is_menu_visible(
                ctx->editor->completion_system)) {
            menu_visible = true;
        }

        if (menu_visible) {
            /* NOTE: ESC dismisses menu but keeps current buffer state.
             * Restoring original pre-completion text would require storing
             * the buffer state before completion started, which is not yet
             * implemented. Current behavior matches many shells where ESC
             * just dismisses the menu without reverting the text. */

            if (ctx->editor->completion_system) {
                lle_completion_system_clear(ctx->editor->completion_system);
            }

            /* Clear menu from display controller */
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_clear_completion_menu(dc);
            }

            /* Refresh display to clear menu from screen */
            refresh_display(ctx);

            return LLE_SUCCESS; /* Menu dismissed, stop here */
        }
    }

    /* Tier 2: Check if autosuggestion is visible - clear it */
    if (has_autosuggestion(ctx)) {
        /* Clear the local autosuggestion buffer */
        if (ctx->current_suggestion) {
            ctx->current_suggestion[0] = '\0';
        }

        /* Suppress autosuggestion regeneration during this refresh
         * Otherwise refresh_display() will immediately regenerate it */
        ctx->suppress_autosuggestion = true;

        /* Clear from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_set_autosuggestion(dc, NULL);
        }

        /* Refresh display to redraw without ghost text */
        refresh_display(ctx);

        return LLE_SUCCESS; /* Suggestion cleared, stop here */
    }

    /* Tier 3: Nothing to dismiss - ESC is a no-op (unlike Ctrl-G which aborts)
     */
    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-L (clear screen and redraw)
 * Step 5 enhancement: Trigger full screen refresh
 */
static lle_result_t handle_clear_screen(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Get the global display integration instance */
    lle_display_integration_t *display_integration =
        lle_display_integration_get_global();
    if (!display_integration || !display_integration->lush_display) {
        return LLE_ERROR_INVALID_STATE;
    }

    /* Clear screen through display controller */
    display_controller_error_t result =
        display_controller_clear_screen(display_integration->lush_display);
    if (result != DISPLAY_CONTROLLER_SUCCESS) {
        return LLE_ERROR_DISPLAY_INTEGRATION;
    }

    /* Refresh display to redraw the prompt and current input */
    refresh_display(ctx);

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-W (kill word backwards)
 * Step 5 enhancement: Delete word before cursor and save to kill buffer
 */
static lle_result_t handle_kill_word(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (ctx->buffer->cursor.byte_offset == 0) {
        return LLE_SUCCESS; /* At beginning, nothing to kill */
    }

    const char *data = ctx->buffer->data;
    size_t pos = ctx->buffer->cursor.byte_offset;

    /* Scan backwards past whitespace */
    while (pos > 0 && (data[pos - 1] == ' ' || data[pos - 1] == '\t')) {
        pos--;
    }

    /* Scan backwards past non-whitespace (the word) */
    size_t word_start = pos;
    while (word_start > 0 && data[word_start - 1] != ' ' &&
           data[word_start - 1] != '\t') {
        word_start--;
    }

    if (word_start < pos) {
        size_t kill_len = pos - word_start;

        /* Save killed text to kill buffer */
        if (ctx->kill_buffer_size < kill_len + 1) {
            char *new_buf = realloc(ctx->kill_buffer, kill_len + 1);
            if (new_buf) {
                ctx->kill_buffer = new_buf;
                ctx->kill_buffer_size = kill_len + 1;
            }
        }

        if (ctx->kill_buffer && ctx->kill_buffer_size >= kill_len + 1) {
            memcpy(ctx->kill_buffer, data + word_start, kill_len);
            ctx->kill_buffer[kill_len] = '\0';
        }

        /* Begin change sequence for undo tracking */
        begin_change_sequence(ctx, "kill-word");

        /* Delete the word */
        lle_result_t result =
            lle_buffer_delete_text(ctx->buffer, word_start, kill_len);

        /* End change sequence */
        end_change_sequence(ctx);

        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-Y (yank from kill buffer)
 * Step 5 enhancement: Insert killed text at cursor
 */
static lle_result_t handle_yank(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Insert kill buffer contents at cursor if we have something */
    if (ctx->kill_buffer && ctx->kill_buffer[0] != '\0') {
        size_t kill_len = strlen(ctx->kill_buffer);

        /* Begin change sequence for undo tracking */
        begin_change_sequence(ctx, "yank");

        lle_result_t result =
            lle_buffer_insert_text(ctx->buffer, ctx->buffer->cursor.byte_offset,
                                   ctx->kill_buffer, kill_len);

        /* End change sequence */
        end_change_sequence(ctx);

        /* Synchronize cursor fields after insert (PHASE 2 STEP 0 FIX) */
        if (result == LLE_SUCCESS && ctx->editor &&
            ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }

        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl+_ (undo)
 * Undoes the last change sequence using change_tracker
 */
static lle_result_t handle_undo(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (!ctx || !ctx->editor || !ctx->editor->change_tracker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Check if undo is available */
    if (!lle_change_tracker_can_undo(ctx->editor->change_tracker)) {
        /* Nothing to undo - could beep here */
        return LLE_SUCCESS;
    }

    /* Perform undo */
    lle_result_t result =
        lle_change_tracker_undo(ctx->editor->change_tracker, ctx->buffer);

    if (result == LLE_SUCCESS) {
        /* Sync cursor manager with buffer cursor position */
        if (ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }
        refresh_display(ctx);
    }

    return result;
}

/**
 * @brief Event handler for Alt+_ or Ctrl+Shift+_ (redo)
 * Redoes the last undone change sequence using change_tracker
 */
static lle_result_t handle_redo(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (!ctx || !ctx->editor || !ctx->editor->change_tracker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Check if redo is available */
    if (!lle_change_tracker_can_redo(ctx->editor->change_tracker)) {
        /* Nothing to redo - could beep here */
        return LLE_SUCCESS;
    }

    /* Perform redo */
    lle_result_t result =
        lle_change_tracker_redo(ctx->editor->change_tracker, ctx->buffer);

    if (result == LLE_SUCCESS) {
        /* Sync cursor manager with buffer cursor position */
        if (ctx->editor->cursor_manager) {
            lle_cursor_manager_move_to_byte_offset(
                ctx->editor->cursor_manager, ctx->buffer->cursor.byte_offset);
        }
        refresh_display(ctx);
    }

    return result;
}

/**
 * @brief Event handler for TAB key
 * Triggers completion via lle_complete()
 */
static lle_result_t handle_tab(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (!ctx || !ctx->editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Call completion function to set up menu */
    lle_result_t result = lle_complete(ctx->editor);

    /* Refresh display to render buffer content with menu appended
     * The menu was set by lle_complete(), and command_layer_set_command()
     * will re-append it after syntax highlighting thanks to our fix
     */
    if (result == LLE_SUCCESS) {
        refresh_display(ctx);
    }

    return result;
}

/**
 * @brief Event handler for Left arrow key
 * Step 5: Move cursor left one grapheme cluster
 * PHASE 2 FIX: Use grapheme-based movement instead of codepoint-based
 */
static lle_result_t handle_arrow_left(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Move cursor left by one grapheme cluster if not at beginning */
    if (ctx->buffer->cursor.grapheme_index > 0 && ctx->editor &&
        ctx->editor->cursor_manager) {
        /* Sync cursor manager position with buffer cursor before moving */
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);

        /* Move cursor by graphemes */
        lle_cursor_manager_move_by_graphemes(ctx->editor->cursor_manager, -1);

        /* CRITICAL: Sync buffer cursor back from cursor manager after movement
         */
        lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                        &ctx->buffer->cursor);

        refresh_display(ctx);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Right arrow key
 * Step 5: Move cursor right one grapheme cluster
 * PHASE 2 FIX: Use grapheme-based movement instead of codepoint-based
 */
static lle_result_t handle_arrow_right(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Fish-style autosuggestion acceptance:
     * If cursor is at end of buffer AND an autosuggestion is available,
     * accept the suggestion instead of normal cursor movement. */
    if (ctx->buffer->cursor.byte_offset == ctx->buffer->length &&
        has_autosuggestion(ctx)) {
        if (accept_autosuggestion(ctx)) {
            refresh_display(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Normal behavior: Move cursor right by one grapheme cluster if not at end
     */
    if (ctx->buffer->cursor.grapheme_index < ctx->buffer->grapheme_count &&
        ctx->editor && ctx->editor->cursor_manager) {
        /* Sync cursor manager position with buffer cursor before moving */
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);

        /* Move cursor by graphemes */
        lle_cursor_manager_move_by_graphemes(ctx->editor->cursor_manager, 1);

        /* CRITICAL: Sync buffer cursor back from cursor manager after movement
         */
        lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                        &ctx->buffer->cursor);

        refresh_display(ctx);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Home key
 * Step 5: Move cursor to beginning of line
 */
static lle_result_t handle_home(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Move to beginning */
    ctx->buffer->cursor.byte_offset = 0;
    refresh_display(ctx);

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for End key
 * Step 5: Move cursor to end of line
 * Fish-style: Accept autosuggestion if cursor already at end
 */
static lle_result_t handle_end(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Fish-style autosuggestion acceptance:
     * If cursor is already at end of buffer AND an autosuggestion is available,
     * accept the suggestion. Otherwise, move cursor to end normally. */
    if (ctx->buffer->cursor.byte_offset == ctx->buffer->length &&
        has_autosuggestion(ctx)) {
        if (accept_autosuggestion(ctx)) {
            refresh_display(ctx);
            return LLE_SUCCESS;
        }
    }

    /* Normal behavior: Move cursor to end */
    ctx->buffer->cursor.byte_offset = ctx->buffer->length;
    refresh_display(ctx);

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Delete key
 * Step 5: Delete character at cursor position
 * PHASE 2 FIX: Delete entire grapheme cluster, not just one codepoint
 */
static lle_result_t handle_delete(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Delete grapheme cluster at cursor if not at end */
    if (ctx->buffer->cursor.grapheme_index < ctx->buffer->grapheme_count &&
        ctx->editor && ctx->editor->cursor_manager) {
        /* Sync cursor manager position with buffer cursor before moving */
        lle_cursor_manager_move_to_byte_offset(ctx->editor->cursor_manager,
                                               ctx->buffer->cursor.byte_offset);

        /* Move cursor forward by one grapheme to find the end of the grapheme
         * to delete */
        size_t grapheme_start = ctx->buffer->cursor.byte_offset;

        lle_result_t result = lle_cursor_manager_move_by_graphemes(
            ctx->editor->cursor_manager, 1);
        if (result == LLE_SUCCESS) {
            /* CRITICAL: Sync buffer cursor back from cursor manager after
             * movement */
            lle_cursor_manager_get_position(ctx->editor->cursor_manager,
                                            &ctx->buffer->cursor);

            size_t grapheme_end = ctx->buffer->cursor.byte_offset;
            size_t grapheme_len = grapheme_end - grapheme_start;

            /* Begin change sequence for undo tracking */
            begin_change_sequence(ctx, "delete");

            /* Delete the entire grapheme cluster */
            result = lle_buffer_delete_text(ctx->buffer, grapheme_start,
                                            grapheme_len);

            /* End change sequence */
            end_change_sequence(ctx);

            /* Refresh display after buffer modification */
            if (result == LLE_SUCCESS) {
                refresh_display(ctx);
            }
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-K
 * Step 5: Kill (delete) text from cursor to end of line
 * Step 5 enhancement: Save killed text to kill buffer
 */
static lle_result_t handle_kill_to_end(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Delete from cursor to end of buffer */
    if (ctx->buffer->cursor.byte_offset < ctx->buffer->length) {
        size_t delete_length =
            ctx->buffer->length - ctx->buffer->cursor.byte_offset;

        /* Save killed text to kill buffer */
        if (ctx->kill_buffer_size < delete_length + 1) {
            char *new_buf = realloc(ctx->kill_buffer, delete_length + 1);
            if (new_buf) {
                ctx->kill_buffer = new_buf;
                ctx->kill_buffer_size = delete_length + 1;
            }
        }

        if (ctx->kill_buffer && ctx->kill_buffer_size >= delete_length + 1) {
            memcpy(ctx->kill_buffer,
                   ctx->buffer->data + ctx->buffer->cursor.byte_offset,
                   delete_length);
            ctx->kill_buffer[delete_length] = '\0';
        }

        /* Begin change sequence for undo tracking */
        begin_change_sequence(ctx, "kill-to-end");

        lle_result_t result = lle_buffer_delete_text(
            ctx->buffer, ctx->buffer->cursor.byte_offset, delete_length);

        /* End change sequence */
        end_change_sequence(ctx);

        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Event handler for Ctrl-U
 * Step 5: Kill (delete) entire line
 * Step 5 enhancement: Save killed text to kill buffer
 */
static lle_result_t handle_kill_line(lle_event_t *event, void *user_data) {
    (void)event; /* Unused */
    readline_context_t *ctx = (readline_context_t *)user_data;

    /* Ctrl-U: Kill from beginning of line to cursor (backward-kill-line) */
    size_t kill_length = ctx->buffer->cursor.byte_offset;

    if (kill_length > 0) {
        /* Save killed text to kill buffer */
        if (ctx->kill_buffer_size < kill_length + 1) {
            char *new_buf = realloc(ctx->kill_buffer, kill_length + 1);
            if (new_buf) {
                ctx->kill_buffer = new_buf;
                ctx->kill_buffer_size = kill_length + 1;
            }
        }

        if (ctx->kill_buffer && ctx->kill_buffer_size >= kill_length + 1) {
            memcpy(ctx->kill_buffer, ctx->buffer->data, kill_length);
            ctx->kill_buffer[kill_length] = '\0';
        }

        /* Begin change sequence for undo tracking */
        begin_change_sequence(ctx, "kill-line");

        lle_result_t result =
            lle_buffer_delete_text(ctx->buffer, 0, kill_length);

        /* End change sequence */
        end_change_sequence(ctx);

        if (result == LLE_SUCCESS) {
            /* Cursor automatically moves to position 0 after deleting from
             * start */
            refresh_display(ctx);
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Show multiline history navigation hint
 *
 * Shows a notification hint when up/down arrow hits a boundary in multiline
 * mode, reminding the user to use Ctrl+P/Ctrl+N for history navigation.
 *
 * Only shows if arrow_key_mode is CONTEXT_AWARE (the default Lush mode).
 *
 * @param ctx Readline context
 * @param is_up_arrow true for up arrow, false for down arrow
 */
static void show_multiline_history_hint(readline_context_t *ctx,
                                        bool is_up_arrow) {
    if (!ctx) {
        return;
    }

    /* Only show hint in context-aware mode (Lush default) */
    if (config.lle_arrow_key_mode != LLE_ARROW_MODE_CONTEXT_AWARE) {
        return;
    }

    /* Determine trigger action for suppress-on-repeat */
    lle_notification_trigger_action_t trigger =
        is_up_arrow ? LLE_NOTIF_ACTION_UP_ARROW : LLE_NOTIF_ACTION_DOWN_ARROW;

    /* Check if same action is repeating - don't dismiss in that case */
    if (lle_notification_is_visible(&ctx->notification) &&
        ctx->notification.trigger_action == trigger) {
        /* Same action repeating at boundary - keep notification visible */
        return;
    }

    /* Show the hint notification */
    const char *msg = "Use Ctrl+P/Ctrl+N for history navigation in multiline mode";
    lle_notification_show_with_trigger(&ctx->notification, msg,
                                       LLE_NOTIFICATION_HINT, trigger);

    /* Pass notification to display controller for rendering */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        display_controller_set_notification(dc, &ctx->notification);
    }
}

/**
 * @brief Dismiss notification if visible and action differs from trigger
 *
 * Called before actions that should dismiss the notification.
 * Implements suppress-on-repeat: if the action matches the trigger,
 * the notification stays visible.
 *
 * @param ctx Readline context
 * @param action The action being performed (or NONE for generic action)
 */
static void maybe_dismiss_notification(readline_context_t *ctx,
                                       lle_notification_trigger_action_t action) {
    if (!ctx || !lle_notification_is_visible(&ctx->notification)) {
        return;
    }

    if (lle_notification_should_dismiss_for_action(&ctx->notification, action)) {
        lle_notification_dismiss(&ctx->notification);

        /* Clear from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc) {
            display_controller_clear_notification(dc);
        }
    }
}

/**
 * @brief Smart up arrow with readline context (full context-aware)
 *
 * Context-aware action with full access to readline state. In multiline mode,
 * shows notification hint when hitting first line boundary, reminding users
 * to use Ctrl+P/Ctrl+N for history navigation.
 */
lle_result_t lle_smart_up_arrow_context(readline_context_t *ctx) {
    /* Check if editor is available */
    if (!ctx->editor || !ctx->editor->buffer) {
        return LLE_SUCCESS; /* No editor available */
    }

    lle_editor_t *editor = ctx->editor;

    /* If completion menu is active, navigate within menu (delegate to smart
     * arrow) */
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_result_t result = lle_smart_up_arrow(editor);
        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return result;
    }

    /* Check if buffer is multiline */
    bool is_multiline =
        (editor->buffer->length > 0 &&
         memchr(editor->buffer->data, '\n', editor->buffer->length) != NULL);

    if (is_multiline) {
        /* Multi-line mode: use extended function with boundary detection */
        lle_line_nav_result_t nav = lle_previous_line_ex(editor);

        if (nav.hit_boundary) {
            /* Hit first line - show hint about Ctrl+P/Ctrl+N */
            show_multiline_history_hint(ctx, true);
        } else {
            /* Moved successfully - dismiss any existing notification */
            maybe_dismiss_notification(ctx, LLE_NOTIF_ACTION_UP_ARROW);
        }

        if (nav.result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return nav.result;
    } else {
        /* Single-line mode: navigate history, dismiss any notification */
        maybe_dismiss_notification(ctx, LLE_NOTIF_ACTION_NONE);
        lle_result_t result = lle_history_previous(editor);
        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return result;
    }
}

/**
 * @brief Smart down arrow with readline context (full context-aware)
 *
 * Context-aware action with full access to readline state. In multiline mode,
 * shows notification hint when hitting last line boundary, reminding users
 * to use Ctrl+P/Ctrl+N for history navigation.
 */
lle_result_t lle_smart_down_arrow_context(readline_context_t *ctx) {

    /* Check if editor is available */
    if (!ctx->editor || !ctx->editor->buffer) {
        return LLE_SUCCESS; /* No editor available */
    }

    lle_editor_t *editor = ctx->editor;

    /* If completion menu is active, navigate within menu (delegate to smart
     * arrow) */
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_result_t result = lle_smart_down_arrow(editor);
        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return result;
    }

    /* Check if buffer is multiline */
    bool is_multiline =
        (editor->buffer->length > 0 &&
         memchr(editor->buffer->data, '\n', editor->buffer->length) != NULL);

    if (is_multiline) {
        /* Multi-line mode: use extended function with boundary detection */
        lle_line_nav_result_t nav = lle_next_line_ex(editor);

        if (nav.hit_boundary) {
            /* Hit last line - show hint about Ctrl+P/Ctrl+N */
            show_multiline_history_hint(ctx, false);
        } else {
            /* Moved successfully - dismiss any existing notification */
            maybe_dismiss_notification(ctx, LLE_NOTIF_ACTION_DOWN_ARROW);
        }

        if (nav.result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return nav.result;
    } else {
        /* Single-line mode: navigate history, dismiss any notification */
        maybe_dismiss_notification(ctx, LLE_NOTIF_ACTION_NONE);
        lle_result_t result = lle_history_next(editor);
        if (result == LLE_SUCCESS) {
            refresh_display(ctx);
        }
        return result;
    }
}

/**
 * @brief Event handler wrapper for up arrow (calls context-aware function)
 */
static lle_result_t handle_arrow_up(lle_event_t *event, void *user_data) {
    (void)event;
    return lle_smart_up_arrow_context((readline_context_t *)user_data);
}

/**
 * @brief Event handler wrapper for down arrow (calls context-aware function)
 */
static lle_result_t handle_arrow_down(lle_event_t *event, void *user_data) {
    (void)event;
    return lle_smart_down_arrow_context((readline_context_t *)user_data);
}

/* ============================================================================
 * CTRL+R INTERACTIVE HISTORY SEARCH
 * ============================================================================
 */

/**
 * @brief Refresh display during interactive search mode
 *
 * Updates the display to show the search prompt and current match.
 * The search prompt replaces the normal prompt during search.
 *
 * Uses direct terminal output for simplicity since we're in a modal
 * search state that doesn't need the full rendering pipeline.
 */
static void refresh_search_display(readline_context_t *ctx) {
    if (!ctx)
        return;

    /* Get the search prompt (e.g., "(reverse-i-search)`query': ") */
    const char *search_prompt = lle_history_interactive_search_get_prompt();

    /* Get the currently matched command (to show in buffer area) */
    const char *current_match =
        lle_history_interactive_search_get_current_command();

    /* Clear line and move to beginning */
    /* \r = carriage return, \033[K = clear to end of line */
    write(STDOUT_FILENO, "\r\033[K", 4);

    /* Write the search prompt */
    if (search_prompt && *search_prompt) {
        write(STDOUT_FILENO, search_prompt, strlen(search_prompt));
    }

    /* Write the matched command */
    if (current_match && *current_match) {
        write(STDOUT_FILENO, current_match, strlen(current_match));
    }
}

/**
 * @brief Exit search mode and restore normal display
 *
 * Clears the search display, restores the normal prompt, and
 * triggers a full redraw with the buffer content.
 */
static void exit_search_mode_and_refresh(readline_context_t *ctx) {
    if (!ctx)
        return;

    /* Clear the search line */
    write(STDOUT_FILENO, "\r\033[K", 4);

    /* Clear autosuggestion to prevent ghost text */
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        display_controller_set_autosuggestion(dc, NULL);
    }

    /* Suppress autosuggestion regeneration temporarily */
    ctx->suppress_autosuggestion = true;

    /* Restore normal prompt in the prompt layer */
    if (dc && dc->compositor && dc->compositor->prompt_layer && ctx->prompt) {
        prompt_layer_set_content(dc->compositor->prompt_layer, ctx->prompt);
    }

    /* Reset the prompt display state to force full redraw */
    dc_reset_prompt_display_state();

    /* Refresh display with the buffer content */
    refresh_display(ctx);

    /* Re-enable autosuggestion */
    ctx->suppress_autosuggestion = false;
}

/**
 * @brief Enter interactive search mode (Ctrl+R handler)
 */
static lle_result_t handle_interactive_search_start(lle_event_t *event,
                                                    void *user_data) {
    (void)event;
    readline_context_t *ctx = (readline_context_t *)user_data;

    if (!ctx || !ctx->editor || !ctx->editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* editor->history_system IS the history_core directly */
    lle_history_core_t *history_core = ctx->editor->history_system;

    /* Save current buffer content and cursor for cancel operation */
    const char *current_line = ctx->buffer->data ? ctx->buffer->data : "";
    size_t cursor_pos = ctx->buffer->cursor.byte_offset;

    /* Initialize search session */
    lle_result_t result = lle_history_interactive_search_init(
        history_core, current_line, cursor_pos);

    if (result == LLE_SUCCESS) {
        /* Refresh display to show search prompt */
        refresh_search_display(ctx);
    }

    return result;
}

/**
 * @brief Handle input during interactive search mode
 *
 * Routes input to appropriate search functions based on key pressed.
 * Returns true if the input was handled (search mode processed it),
 * false if search mode should exit and the key should be processed normally.
 *
 * @param ctx Readline context
 * @param codepoint Unicode codepoint of the key pressed
 * @param is_ctrl True if Ctrl modifier was held
 * @param special_key Special key code (0 if regular character)
 * @return true if handled by search mode, false to exit search and process key
 * normally
 */
static bool handle_search_mode_input(readline_context_t *ctx,
                                     uint32_t codepoint, bool is_ctrl,
                                     uint32_t special_key) {
    if (!lle_history_interactive_search_is_active()) {
        return false; /* Not in search mode */
    }

    /* Ctrl+R during search: move to next (older) match */
    if (is_ctrl && codepoint == 'R') {
        lle_history_interactive_search_next();
        refresh_search_display(ctx);
        return true;
    }

    /* Ctrl+S during search: move to previous (newer) match */
    if (is_ctrl && codepoint == 'S') {
        lle_history_interactive_search_prev();
        refresh_search_display(ctx);
        return true;
    }

    /* Ctrl+G or Ctrl+C: cancel search, restore original line */
    if (is_ctrl && (codepoint == 'G' || codepoint == 'C')) {
        const char *original = lle_history_interactive_search_cancel();

        /* Restore original buffer content */
        lle_buffer_clear(ctx->buffer);
        if (original && *original) {
            lle_buffer_insert_text(ctx->buffer, 0, original, strlen(original));
        }

        /* Exit search mode and restore display */
        exit_search_mode_and_refresh(ctx);
        return true;
    }

    /* Enter: accept current match */
    if (codepoint == '\n' || codepoint == '\r' ||
        special_key == LLE_KEY_ENTER) {
        const char *selected = lle_history_interactive_search_accept();

        /* Put selected command in buffer */
        lle_buffer_clear(ctx->buffer);
        if (selected && *selected) {
            lle_buffer_insert_text(ctx->buffer, 0, selected, strlen(selected));
            /* Move cursor to end */
            ctx->buffer->cursor.byte_offset = ctx->buffer->length;
            ctx->buffer->cursor.grapheme_index =
                ctx->buffer->length; /* Approximate */
        }

        /* Exit search mode and restore display */
        exit_search_mode_and_refresh(ctx);
        return true;
    }

    /* Backspace: remove last character from query */
    if (codepoint == 127 || codepoint == 8) {
        lle_history_interactive_search_backspace();
        refresh_search_display(ctx);
        return true;
    }

    /* Escape: cancel search */
    if (codepoint == 0x1B || special_key == LLE_KEY_ESCAPE) {
        const char *original = lle_history_interactive_search_cancel();

        /* Restore original buffer content */
        lle_buffer_clear(ctx->buffer);
        if (original && *original) {
            lle_buffer_insert_text(ctx->buffer, 0, original, strlen(original));
        }

        /* Exit search mode and restore display */
        exit_search_mode_and_refresh(ctx);
        return true;
    }

    /* Printable characters: add to search query */
    if (codepoint >= 32 && codepoint < 127 && !is_ctrl) {
        lle_history_interactive_search_update_query((char)codepoint);
        refresh_search_display(ctx);
        return true;
    }

    /* Arrow keys and other special keys: exit search mode, process key normally
     */
    if (special_key == LLE_KEY_UP || special_key == LLE_KEY_DOWN ||
        special_key == LLE_KEY_LEFT || special_key == LLE_KEY_RIGHT) {
        /* Accept current match and exit search */
        const char *selected = lle_history_interactive_search_accept();

        /* Put selected command in buffer */
        lle_buffer_clear(ctx->buffer);
        if (selected && *selected) {
            lle_buffer_insert_text(ctx->buffer, 0, selected, strlen(selected));
            ctx->buffer->cursor.byte_offset = ctx->buffer->length;
            ctx->buffer->cursor.grapheme_index = ctx->buffer->length;
        }

        /* Exit search mode and restore display */
        exit_search_mode_and_refresh(ctx);

        /* Return false so the arrow key gets processed normally */
        return false;
    }

    /* Any other key: exit search mode and let the key be processed normally */
    const char *selected = lle_history_interactive_search_accept();
    lle_buffer_clear(ctx->buffer);
    if (selected && *selected) {
        lle_buffer_insert_text(ctx->buffer, 0, selected, strlen(selected));
        ctx->buffer->cursor.byte_offset = ctx->buffer->length;
        ctx->buffer->cursor.grapheme_index = ctx->buffer->length;
    }

    /* Exit search mode and restore display */
    exit_search_mode_and_refresh(ctx);

    return false; /* Let caller process the key */
}

/**
 * @brief Execute a keybinding action via the keybinding manager
 * Group 1+ migration: Routes key sequences through keybinding manager
 * Falls back to provided handler function if keybinding manager lookup fails
 *
 * @param ctx Readline context
 * @param key_sequence Key sequence string (e.g., "LEFT", "RIGHT", "HOME",
 * "END")
 * @param fallback_handler Fallback handler function if lookup fails (can be
 * NULL)
 * @return LLE_SUCCESS or error code
 */
static lle_result_t execute_keybinding_action(
    readline_context_t *ctx, const char *key_sequence,
    lle_result_t (*fallback_handler)(lle_event_t *, void *)) {
    /* Try keybinding manager first (Group 1+ migration) */
    if (ctx->keybinding_manager && ctx->editor) {
        lle_keybinding_action_t *action = NULL;
        lle_result_t result = lle_keybinding_manager_lookup(
            ctx->keybinding_manager, key_sequence, &action);

        if (result == LLE_SUCCESS && action != NULL) {
            lle_result_t exec_result;

            /* Dispatch based on action type */
            if (action->type == LLE_ACTION_TYPE_SIMPLE) {
                /* Simple action: operate on editor, then handle special flags
                 */
                exec_result = action->func.simple(ctx->editor);

                /* Check for EOF request (legacy flag - used by Ctrl-D) */
                if (ctx->editor->eof_requested) {
                    *ctx->done = true;
                    *ctx->final_line = NULL;
                    return exec_result;
                }

                /* Check for abort request (legacy flag - used by Ctrl-G) */
                if (ctx->editor->abort_requested) {
                    *ctx->done = true;
                    *ctx->final_line = strdup(""); /* Return empty string, not
                                                      NULL (NULL signals EOF) */
                    return exec_result;
                }

                /* Refresh display after simple action */
                if (exec_result == LLE_SUCCESS) {
                    refresh_display(ctx);
                }

                return exec_result;
            } else if (action->type == LLE_ACTION_TYPE_CONTEXT) {
                /* Context-aware action: has full access, handles everything
                 * including display refresh */
                exec_result = action->func.context(ctx);
                return exec_result;
            }

            /* Unknown action type - should never happen */
            return LLE_ERROR_FATAL_INTERNAL;
        }
    }

    /* Fallback to hardcoded handler if keybinding manager not available or
     * lookup failed */
    if (fallback_handler) {
        return fallback_handler(NULL, ctx);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Read a line of input from the user with line editing
 *
 * This is the core readline function that replaces GNU readline when LLE is
 * enabled.
 *
 * Step 2 Implementation:
 * - Create terminal abstraction
 * - Enter raw terminal mode
 * - Create lle_buffer_t for line editing
 * - Read input events one at a time
 * - Use lle_buffer_insert_text() for character input
 * - Use lle_buffer_delete_text() for backspace
 * - Return on Enter key
 * - Exit raw mode and cleanup
 *
 * @param prompt The prompt string to display to the user
 * @return Newly allocated string containing the line (caller must free), or
 * NULL on error/EOF
 */
char *lle_readline(const char *prompt) {
    lle_result_t result;

    /*
     * CRITICAL: Check if stdin is a TTY before attempting raw mode.
     *
     * When the shell is forced into interactive mode with -i but stdin is
     * a pipe or file, we cannot use raw mode terminal features. Fall back
     * to simple getline() for input while still showing the prompt.
     *
     * This handles: echo "cmd" | lush -i
     */
    if (!isatty(STDIN_FILENO)) {
        /* Print prompt to stdout if provided */
        if (prompt && *prompt) {
            fputs(prompt, stdout);
            fflush(stdout);
        }

        /* Use simple getline for non-TTY stdin */
        char *line = NULL;
        size_t len = 0;
        ssize_t read = getline(&line, &len, stdin);

        if (read == -1) {
            free(line);
            return NULL; /* EOF or error */
        }

        /* Remove trailing newline */
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }

        return line;
    }

    /* Reset prompt display state for new input session */
    dc_reset_prompt_display_state();

    /* Get display controller from display_integration */
    void *display_controller = display_integration_get_controller();

    /* === STEP 1: Create terminal abstraction instance === */
    lle_terminal_abstraction_t *term = NULL;
    result = lle_terminal_abstraction_init(
        &term, (lush_display_context_t *)display_controller);
    if (result != LLE_SUCCESS || term == NULL) {
        /* Failed to initialize terminal abstraction */
        return NULL;
    }

    /* === STEP 2: Get unix interface for raw mode === */
    if (term->unix_interface == NULL) {
        lle_terminal_abstraction_destroy(term);
        return NULL;
    }

    lle_unix_interface_t *unix_iface = term->unix_interface;

    /* === STEP 3: Enter raw mode === */
    result = lle_unix_interface_enter_raw_mode(unix_iface);
    if (result != LLE_SUCCESS) {
        /* Failed to enter raw mode */
        lle_terminal_abstraction_destroy(term);
        return NULL;
    }

    /* Notify signal handler that LLE readline is active
     * This allows SIGINT (Ctrl+C) to be handled properly by setting a flag
     * that we check in the input loop, rather than using the default behavior
     */
    set_lle_readline_active(1);

    /* === STEP 4: Create buffer for line editing === */
    lle_buffer_t *buffer = NULL;
    result = lle_buffer_create(&buffer, global_memory_pool, 256);
    if (result != LLE_SUCCESS || buffer == NULL) {
        /* Failed to create buffer */
        lle_unix_interface_exit_raw_mode(unix_iface);
        lle_terminal_abstraction_destroy(term);
        return NULL;
    }

    /* Enable change tracking for undo/redo support */
    buffer->change_tracking_enabled = true;

    /* === STEP 5: Create event system === */
    /* Step 3: Add event system for decoupled architecture */
    lle_event_system_t *event_system = NULL;
    result = lle_event_system_init(&event_system,
                                   (lle_memory_pool_t *)global_memory_pool);
    if (result != LLE_SUCCESS || event_system == NULL) {
        /* Failed to create event system */
        lle_buffer_destroy(buffer);
        lle_unix_interface_exit_raw_mode(unix_iface);
        lle_terminal_abstraction_destroy(term);
        return NULL;
    }

    /* === STEP 5.5: Create continuation state === */
    /* Step 6: Initialize shared multiline parser state */
    continuation_state_t continuation_state;
    continuation_state_init(&continuation_state);

    /* === STEP 5.55: Create edit-session arena === */
    /* Arena for per-readline allocations (kill buffer, suggestions, etc.)
     * Child of session arena - automatically freed when session ends,
     * but we explicitly destroy it at end of lle_readline() for prompt cleanup.
     * 8KB initial size is sufficient for typical editing session. */
    lle_arena_t *edit_arena = NULL;
    if (g_lle_integration && g_lle_integration->session_arena) {
        edit_arena = lle_arena_create(g_lle_integration->session_arena,
                                      "edit", 8 * 1024);
    }

    /* === STEP 5.6: Create kill buffer === */
    /* Step 5 enhancement: Initialize kill buffer for yank operations
     * Allocated from edit_arena if available, otherwise falls back to malloc */
    char *kill_buffer = NULL;
    size_t kill_buffer_size = 0;

    /* === STEP 5.7: Get display controller for prompt display === */
    display_controller_t *display = display_integration_get_controller();

    /* === STEP 5.8: Initialize LLE display integration (Spec 08) === */
    lle_display_integration_t *lle_display_integ =
        lle_display_integration_get_global();
    if (!lle_display_integ && display) {
        /* Initialize display integration if not already initialized */
        result = lle_display_integration_init(
            &lle_display_integ, NULL, /* editor context - not needed yet */
            display, (lle_memory_pool_t *)global_memory_pool);
        /* Non-fatal if display integration fails - LLE will still work for
         * input */
    }

    /* === STEP 6: Register event handlers === */
    /* Step 4: Register handlers that will modify buffer and refresh display */
    bool done = false;
    char *final_line = NULL;
    /* === STEP 6.5: Initialize LLE editor (proper architecture) === */
    /* Spec 26: Use shell integration's editor if available, otherwise
     * fall back to lazy initialization for backward compatibility.
     */
    lle_editor_t *editor_to_use = NULL;

    if (g_lle_integration && g_lle_integration->editor) {
        /* Use editor from shell integration (Spec 26) */
        editor_to_use = g_lle_integration->editor;
    } else {
        /* Fallback: Create global editor instance if it doesn't exist */
        if (!global_lle_editor) {
            result = lle_editor_create(&global_lle_editor, global_memory_pool);
            if (result != LLE_SUCCESS || !global_lle_editor) {
                /* Failed to create editor - non-fatal, history won't work */
                global_lle_editor = NULL;
            } else {
                /* Initialize history subsystem with config from Lush */
                lle_history_config_t hist_config;
                populate_history_config_from_lush_config(&hist_config);

                result = lle_history_core_create(
                    &global_lle_editor->history_system,
                    global_lle_editor->lle_pool, &hist_config);

                if (result == LLE_SUCCESS &&
                    global_lle_editor->history_system) {
                    /* Load existing history from LLE history file */
                    const char *history_file = getenv("HOME");
                    char history_path[1024];
                    if (history_file) {
                        snprintf(history_path, sizeof(history_path),
                                 "%s/.lush_history", history_file);
                        lle_history_load_from_file(
                            global_lle_editor->history_system, history_path);
                    }
                }
            }
        }
        editor_to_use = global_lle_editor;
    }

    /* Set buffer in editor if editor exists */
    if (editor_to_use) {
        editor_to_use->buffer = buffer;

        /* Update cursor_manager's buffer reference to stay synchronized */
        if (editor_to_use->cursor_manager) {
            editor_to_use->cursor_manager->buffer = buffer;
        }

        /* CRITICAL: Reset history navigation position for new readline session
         */
        /* Each readline() call starts fresh - not in history navigation mode */
        editor_to_use->history_navigation_pos = 0;
        /* Clear the seen set for unique-only navigation mode */
        editor_to_use->history_nav_seen_count = 0;
    }

    /* === STEP 6.6: Create keybinding manager and load Emacs preset === */
    lle_keybinding_manager_t *keybinding_manager = NULL;
    result =
        lle_keybinding_manager_create(&keybinding_manager, global_memory_pool);
    if (result != LLE_SUCCESS || keybinding_manager == NULL) {
        /* Failed to create keybinding manager - non-fatal, will use hardcoded
         * fallbacks */
        keybinding_manager = NULL;
    } else {
        /* Load the full Emacs preset first - this provides all GNU Readline
         * bindings */
        result = lle_keybinding_manager_load_emacs_preset(keybinding_manager);
        if (result != LLE_SUCCESS) {
            /* Preset load failed - continue with context-aware overrides only
             */
        }

        /* Load user keybinding configuration from
         * ~/.config/lush/keybindings.toml This allows users to override
         * default Emacs bindings with custom ones. Errors are logged but don't
         * prevent shell from starting. */
        lle_keybinding_load_result_t load_result;
        lle_keybinding_load_user_config(keybinding_manager, &load_result);
        /* Note: We don't check load_result here - user config is optional.
         * If file doesn't exist, that's fine (LLE_ERROR_NOT_FOUND).
         * If there are parse errors, they're logged to stderr. */

        /* Override specific bindings with context-aware variants that need
         * readline_context_t. These provide Fish-style autosuggestion
         * acceptance and completion menu handling. The preset's simple actions
         * are overridden where context awareness is needed. */

        /* Fish-style autosuggestion acceptance: RIGHT/END/C-f/C-e accept
         * suggestions */
        lle_keybinding_manager_bind_context(
            keybinding_manager, "RIGHT", lle_forward_char_or_accept_suggestion,
            "forward-char-or-accept");
        lle_keybinding_manager_bind_context(
            keybinding_manager, "END", lle_end_of_line_or_accept_suggestion,
            "end-of-line-or-accept");
        lle_keybinding_manager_bind_context(
            keybinding_manager, "C-e", lle_end_of_line_or_accept_suggestion,
            "end-of-line-or-accept");
        lle_keybinding_manager_bind_context(
            keybinding_manager, "C-f", lle_forward_char_or_accept_suggestion,
            "forward-char-or-accept");

        /* Partial suggestion acceptance: Ctrl+RIGHT accepts one word at a time
         */
        lle_keybinding_manager_bind_context(
            keybinding_manager, "C-RIGHT",
            lle_forward_word_or_accept_partial_suggestion,
            "forward-word-or-accept-partial");

        /* Context-aware abort: first press dismisses completion menu, second
         * aborts line */
        lle_keybinding_manager_bind_context(
            keybinding_manager, "C-g", lle_abort_line_context, "abort-line");

        /* ESC: Context-aware escape - dismisses completion menu or
         * autosuggestion */
        lle_keybinding_manager_bind_context(keybinding_manager, "ESC",
                                            lle_escape_context, "escape");

        /* ENTER: Context-aware accept line with multiline/completion awareness
         */
        lle_keybinding_manager_bind_context(keybinding_manager, "ENTER",
                                            lle_accept_line_context,
                                            "accept-line");

        /* UP/DOWN: Context-aware arrow keys with multiline notification hints */
        lle_keybinding_manager_bind_context(keybinding_manager, "UP",
                                            lle_smart_up_arrow_context,
                                            "smart-up-arrow");
        lle_keybinding_manager_bind_context(keybinding_manager, "DOWN",
                                            lle_smart_down_arrow_context,
                                            "smart-down-arrow");
    }

    readline_context_t ctx = {
        .buffer = buffer,
        .done = &done,
        .final_line = &final_line,
        .term = term,
        .prompt = prompt,
        .continuation_state = &continuation_state,
        .kill_buffer = kill_buffer,
        .kill_buffer_size = kill_buffer_size,

        /* Memory management - edit arena for per-readline allocations */
        .edit_arena = edit_arena,

        /* LLE Editor - proper architecture (Spec 26: prefer shell integration)
         */
        .editor = editor_to_use,

        /* Keybinding manager - Group 1+ migration */
        .keybinding_manager = keybinding_manager,

        /* Fish-style autosuggestions - LLE history integration */
        .current_suggestion = NULL,
        .suggestion_alloc_size = 0,
        .suppress_autosuggestion = false,

        /* State machine - start in IDLE state */
        .state = LLE_READLINE_STATE_IDLE,
        .previous_state = LLE_READLINE_STATE_IDLE,

        /* Notification system - initialized to empty state */
        .notification = {.message = {0},
                         .type = LLE_NOTIFICATION_HINT,
                         .visible = false,
                         .trigger_action = LLE_NOTIF_ACTION_NONE}};

    /* CRITICAL: Reset per-readline-call flags on editor
     * The editor is persistent across readline calls, but these flags
     * must be reset at the start of each new readline session.
     * Without this reset, state persists across readline calls, causing
     * all subsequent actions to behave incorrectly.
     *
     * State categories that MUST be reset:
     * 1. Abort/EOF signals - prevent immediate exit on next readline
     * 2. History navigation - start fresh each command
     * 3. Interactive search - don't inherit previous search state
     * 4. Special input modes - prevent stuck quoted insert mode
     */
    if (editor_to_use) {
        /* Category 1: Abort/EOF signals */
        editor_to_use->abort_requested = false;
        editor_to_use->eof_requested = false;

        /* Category 2: History navigation state */
        editor_to_use->history_navigation_pos = 0;
        editor_to_use->history_nav_seen_count = 0;
        /* Note: history_nav_seen_hashes array is reused, just reset count */

        /* Category 3: Interactive search state */
        editor_to_use->history_search_active = false;
        editor_to_use->history_search_direction = 0;

        /* Category 4: Special input modes */
        editor_to_use->quoted_insert_mode = false;
    }

    /* Register handler for character input */
    result = lle_event_handler_register(event_system, LLE_EVENT_KEY_PRESS,
                                        handle_character_input, &ctx,
                                        "character_input");
    if (result != LLE_SUCCESS) {
        lle_event_system_destroy(event_system);
        lle_buffer_destroy(buffer);
        lle_unix_interface_exit_raw_mode(unix_iface);
        lle_terminal_abstraction_destroy(term);
        return NULL;
    }

    /* === STEP 7: Display prompt === */
    /* Step 4: Set prompt in prompt_layer and display initial prompt */
    if (lle_display_integ && lle_display_integ->lush_display) {
        display_controller_t *dc = lle_display_integ->lush_display;
        if (dc->compositor && dc->compositor->prompt_layer && prompt) {
            /* Set the prompt content in the prompt_layer */
            prompt_layer_set_content(dc->compositor->prompt_layer, prompt);
        }
    }

    /* Initial display refresh to show prompt */
    refresh_display(&ctx);

    /* === WIDGET HOOK: LINE_INIT === */
    /* Trigger line-init hook at start of readline (ZSH zle-line-init) */
    if (editor_to_use && editor_to_use->widget_hooks_manager) {
        lle_widget_hook_trigger(editor_to_use->widget_hooks_manager,
                                LLE_HOOK_LINE_INIT, editor_to_use);
    }

    /* === STEP 8: Main input loop === */

    /* CRITICAL FIX: Timeout counter to prevent infinite loops
     * If we get too many consecutive timeouts without any user input,
     * something is wrong (e.g., terminal state corruption, fd closed).
     * The watchdog (SIGALRM) handles actual processing freezes.
     * Idle waiting for user input is normal and should not be interrupted.
     */

    while (!done) {

        /* WATCHDOG: Pet the watchdog at the top of each iteration.
         * This resets the 10-second alarm timer so it covers the ENTIRE
         * iteration: rendering, completion, event processing, and input
         * read. Previously only covered the input read, meaning hangs
         * in rendering or completion went undetected. */
        lle_watchdog_pet(0); /* 0 = use default timeout (10 seconds) */

        /* WATCHDOG: Check at start of each iteration (defense in depth).
         * This catches cases where the previous iteration got stuck but
         * somehow returned to the loop without hitting the post-read check.
         */
        if (lle_watchdog_check_and_clear()) {
            fprintf(stderr, "\nlle: watchdog timeout at loop start\n");
            if (ctx.editor && ctx.editor->completion_system) {
                lle_completion_system_clear(ctx.editor->completion_system);
            }
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_set_autosuggestion(dc, NULL);
                display_controller_clear_completion_menu(dc);
            }
            dc_reset_prompt_display_state();
            lle_readline_state_force_timeout(&ctx);
            done = true;
            final_line = strdup("");
            continue;
        }

        /* CRITICAL FIX: Reset suppress_autosuggestion at start of each
         * iteration This flag is set during operations that temporarily
         * suppress autosuggestion (e.g., Ctrl+G clearing, menu dismissal).
         * Resetting here ensures the flag cannot stay stuck - it will be re-set
         * if needed by the current operation. This prevents the bug where
         * suppress_autosuggestion stays true forever if a handler sets it but
         * doesn't reset it before returning.
         */
        ctx.suppress_autosuggestion = false;

        /* Check if SIGINT was received (Ctrl+C)
         * The signal handler sets a flag instead of interrupting directly,
         * allowing us to handle it cleanly here in the input loop.
         * This provides bash-like behavior: Ctrl+C aborts current line and
         * displays a fresh prompt, rather than exiting the shell. */
        if (check_and_clear_sigint_flag()) {
            /* Echo ^C to show user that Ctrl+C was pressed */
            write(STDOUT_FILENO, "^C\n", 3);

            /* Clear any completion menu or autosuggestion */
            if (ctx.editor && ctx.editor->completion_system) {
                lle_completion_system_clear(ctx.editor->completion_system);
            }
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_set_autosuggestion(dc, NULL);
                display_controller_clear_completion_menu(dc);
            }

            /* Clear local suggestion buffer */
            if (ctx.current_suggestion) {
                ctx.current_suggestion[0] = '\0';
            }
            ctx.suppress_autosuggestion = true;

            /* Reset display state for fresh prompt */
            dc_reset_prompt_display_state();

            /* STATE MACHINE: Force abort state - this ALWAYS succeeds */
            lle_readline_state_force_abort(&ctx);

            /* Abort line - return empty string (not NULL, which signals EOF) */
            done = true;
            final_line = strdup("");
            continue;
        }

        /* Read next input event */
        lle_input_event_t *event = NULL;

        result = lle_input_processor_read_next_event(
            term->input_processor, &event, 100 /* 100ms timeout */
        );

        /* WATCHDOG: Check if watchdog fired during processing.
         * This catches scenarios where event processing hangs.
         */
        if (lle_watchdog_check_and_clear()) {
            fprintf(stderr, "\nlle: watchdog timeout - forcing recovery\n");
            /* Attempt recovery: clear all subsystem state */
            if (ctx.editor && ctx.editor->completion_system) {
                lle_completion_system_clear(ctx.editor->completion_system);
            }
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_set_autosuggestion(dc, NULL);
                display_controller_clear_completion_menu(dc);
            }
            dc_reset_prompt_display_state();

            /* STATE MACHINE: Force timeout state */
            lle_readline_state_force_timeout(&ctx);
            done = true;
            final_line = strdup("");
            continue;
        }

        /* Handle timeout and null events - just continue waiting
         * Idle waiting for user input is completely normal.
         * The watchdog catches actual processing freezes. */
        if (result == LLE_ERROR_TIMEOUT || event == NULL) {
            continue;
        }

        /* ALSO check event type for timeout */
        if (event->type == LLE_INPUT_TYPE_TIMEOUT) {
            continue;
        }

        /* STATE MACHINE: Transition from IDLE to EDITING on first real input */
        if (ctx.state == LLE_READLINE_STATE_IDLE) {
            lle_readline_state_transition(&ctx, LLE_READLINE_STATE_EDITING);
        }

        /* CRITICAL FIX: Re-check Ctrl+C after input read
         * SIGINT can arrive during the blocking read call. We check again here
         * to catch Ctrl+C pressed during the read, ensuring responsive
         * handling. This fixes the race window where Ctrl+C between the initial
         * check and the read would be delayed until the next iteration.
         */
        if (check_and_clear_sigint_flag()) {
            write(STDOUT_FILENO, "^C\n", 3);
            if (ctx.editor && ctx.editor->completion_system) {
                lle_completion_system_clear(ctx.editor->completion_system);
            }
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                display_controller_set_autosuggestion(dc, NULL);
                display_controller_clear_completion_menu(dc);
            }
            if (ctx.current_suggestion) {
                ctx.current_suggestion[0] = '\0';
            }
            dc_reset_prompt_display_state();
            /* STATE MACHINE: Force abort state */
            lle_readline_state_force_abort(&ctx);
            done = true;
            final_line = strdup("");
            continue;
        }

        /* Handle read errors */
        if (result != LLE_SUCCESS) {
            /* Error reading input - abort */
            /* STATE MACHINE: Force error state */
            lle_readline_state_force_error(&ctx);
            done = true;
            final_line = NULL;
            continue;
        }

        /* === INTERACTIVE SEARCH MODE HANDLING === */
        /* When Ctrl+R search is active, route input to search handler first */
        if (lle_history_interactive_search_is_active()) {
            bool handled = false;

            if (event->type == LLE_INPUT_TYPE_CHARACTER) {
                uint32_t codepoint = event->data.character.codepoint;
                handled = handle_search_mode_input(&ctx, codepoint, false, 0);
            } else if (event->type == LLE_INPUT_TYPE_SPECIAL_KEY) {
                uint32_t keycode = event->data.special_key.keycode;
                bool is_ctrl =
                    (event->data.special_key.modifiers & LLE_MOD_CTRL) != 0;
                uint32_t special = event->data.special_key.key;
                handled =
                    handle_search_mode_input(&ctx, keycode, is_ctrl, special);
            }

            if (handled) {
                continue; /* Search mode consumed the input */
            }
            /* If not handled, fall through to normal processing */
        }

        /* === STEP 9: Convert input event to LLE event and dispatch === */
        /* Step 3: Use event system instead of direct buffer manipulation */

        switch (event->type) {
        case LLE_INPUT_TYPE_CHARACTER: {
            /* Regular character input */
            uint32_t codepoint = event->data.character.codepoint;

            /* Check for Enter key (newline) */
            if (codepoint == '\n' || codepoint == '\r') {
                /* GROUP 5 MIGRATION: ENTER routed through keybinding manager
                 * (context-aware action) */
                execute_keybinding_action(&ctx, "ENTER", handle_enter);
                break;
            }

            /* NOTE: Ctrl-A through Ctrl-Z are now handled as SPECIAL_KEY events
             * with modifiers See SPECIAL_KEY case below for Ctrl+letter
             * handling. Ctrl-C (codepoint == 3) is still handled via SIGINT
             * signal (src/signals.c). */

            /* Check for backspace */
            if (codepoint == 127 || codepoint == 8) { /* DEL or BS */
                execute_keybinding_action(&ctx, "BACKSPACE", handle_backspace);
                break;
            }

            /* Check for TAB - trigger completion */
            if (codepoint == '\t' || codepoint == 9) {
                execute_keybinding_action(&ctx, "TAB", handle_tab);
                break;
            }

            /* Check for ESC (0x1B = 27) - dismiss completion menu */
            /* ESC arrives as CHARACTER when timeout expires without escape
             * sequence */
            if (codepoint == 0x1B || codepoint == 27) {
                execute_keybinding_action(&ctx, "ESC", NULL);
                break;
            }

            /* Check for Ctrl+_ or Ctrl+/ (both send 0x1F = 31) - undo
             * Both are standard Emacs/readline undo keybindings */
            if (codepoint == 0x1F) {
                execute_keybinding_action(&ctx, "C-_", handle_undo);
                break;
            }

            /* Check for Ctrl+^ (0x1E = 30) - redo
             * Note: Ctrl+^ is typed as Ctrl+Shift+6 on most keyboards */
            if (codepoint == 0x1E) {
                execute_keybinding_action(&ctx, "C-^", handle_redo);
                break;
            }

            /* Regular character - create LLE event and dispatch */
            lle_event_t *lle_event = NULL;
            result = lle_event_create(event_system, LLE_EVENT_KEY_PRESS, NULL,
                                      0, &lle_event);
            if (result == LLE_SUCCESS && lle_event != NULL) {
                /* Set event data */
                lle_event->event_data.key.key_code = codepoint;
                lle_event->event_data.key.modifiers = 0;
                lle_event->event_data.key.is_special = false;

                /* Copy UTF-8 bytes */
                size_t copy_len = event->data.character.byte_count;
                if (copy_len > 7)
                    copy_len = 7; /* utf8_char is char[8] */
                memcpy(lle_event->event_data.key.utf8_char,
                       event->data.character.utf8_bytes, copy_len);
                lle_event->event_data.key.utf8_char[copy_len] = '\0';

                /* Dispatch event - handler will modify buffer */
                lle_event_dispatch(event_system, lle_event);

                /* Destroy event after dispatch to prevent memory leak */
                lle_event_destroy(event_system, lle_event);
            }
            break;
        }

        case LLE_INPUT_TYPE_SPECIAL_KEY: {
            /* Special keys */
            /* GROUP 5 MIGRATION: ENTER routed through keybinding manager
             * (context-aware action) */
            if (event->data.special_key.key == LLE_KEY_ENTER &&
                !(event->data.special_key.modifiers & LLE_MOD_ALT)) {
                /* Plain Enter - accept line */
                execute_keybinding_action(&ctx, "ENTER", handle_enter);
            }
            /* Alt-Enter: Insert literal newline (for editing complete multiline
               commands) */
            else if (event->data.special_key.key == LLE_KEY_ENTER &&
                     (event->data.special_key.modifiers & LLE_MOD_ALT)) {
                execute_keybinding_action(&ctx, "M-ENTER", NULL);
            }
            /* GROUP 1 MIGRATION: Navigation keys routed through keybinding
               manager */
            else if (event->data.special_key.key == LLE_KEY_LEFT) {
                execute_keybinding_action(&ctx, "LEFT", handle_arrow_left);
            }
            /* Ctrl+Right: Partial suggestion acceptance (must check before
               plain RIGHT) */
            else if (event->data.special_key.key == LLE_KEY_RIGHT &&
                     (event->data.special_key.modifiers & LLE_MOD_CTRL)) {
                execute_keybinding_action(&ctx, "C-RIGHT", NULL);
            } else if (event->data.special_key.key == LLE_KEY_RIGHT) {
                execute_keybinding_action(&ctx, "RIGHT", handle_arrow_right);
            }
            /* History navigation with UP/DOWN arrows */
            else if (event->data.special_key.key == LLE_KEY_UP) {
                execute_keybinding_action(&ctx, "UP", handle_arrow_up);
            } else if (event->data.special_key.key == LLE_KEY_DOWN) {
                execute_keybinding_action(&ctx, "DOWN", handle_arrow_down);
            }
            /* GROUP 1 MIGRATION: Home/End keys routed through keybinding
               manager */
            else if (event->data.special_key.key == LLE_KEY_HOME) {
                execute_keybinding_action(&ctx, "HOME", handle_home);
            } else if (event->data.special_key.key == LLE_KEY_END) {
                execute_keybinding_action(&ctx, "END", handle_end);
            }
            /* Step 5: Delete key */
            else if (event->data.special_key.key == LLE_KEY_DELETE) {
                execute_keybinding_action(&ctx, "DELETE", handle_delete);
            }
            /* Alt+Backspace: backward-kill-word */
            else if (event->data.special_key.key == LLE_KEY_BACKSPACE &&
                     (event->data.special_key.modifiers & LLE_MOD_ALT)) {
                execute_keybinding_action(&ctx, "M-BACKSPACE", NULL);
            }
            /* ESC key - dismiss completion menu */
            else if (event->data.special_key.key == LLE_KEY_ESCAPE) {
                execute_keybinding_action(&ctx, "ESC", NULL);
            }
            /* Handle Ctrl+letter combinations (now SPECIAL_KEY events with
               keycode) */
            else if (event->data.special_key.key == LLE_KEY_UNKNOWN &&
                     (event->data.special_key.modifiers & LLE_MOD_CTRL)) {
                uint32_t keycode = event->data.special_key.keycode;

                switch (keycode) {
                case 'A': /* Ctrl-A: Beginning of line */
                    execute_keybinding_action(&ctx, "C-a", handle_home);
                    break;
                case 'B': /* Ctrl-B: Back one character */
                    execute_keybinding_action(&ctx, "C-b", handle_arrow_left);
                    break;
                case 'D': /* Ctrl-D: EOF */
                    execute_keybinding_action(&ctx, "C-d", handle_eof);
                    break;
                case 'E': /* Ctrl-E: End of line */
                    execute_keybinding_action(&ctx, "C-e", handle_end);
                    break;
                case 'F': /* Ctrl-F: Forward one character */
                    execute_keybinding_action(&ctx, "C-f", handle_arrow_right);
                    break;
                case 'G': /* Ctrl-G: Abort/cancel line */
                    execute_keybinding_action(&ctx, "C-g", handle_abort);
                    break;
                case 'K': /* Ctrl-K: Kill to end of line */
                    execute_keybinding_action(&ctx, "C-k", handle_kill_to_end);
                    break;
                case 'L': /* Ctrl-L: Clear screen */
                    execute_keybinding_action(&ctx, "C-l", handle_clear_screen);
                    break;
                case 'N': /* Ctrl-N: Next history (always navigate history) */
                    execute_keybinding_action(&ctx, "C-n", NULL);
                    break;
                case 'P': /* Ctrl-P: Previous history (always navigate history)
                           */
                    execute_keybinding_action(&ctx, "C-p", NULL);
                    break;
                case 'R': /* Ctrl-R: Interactive history search */
                    execute_keybinding_action(&ctx, "C-r",
                                              handle_interactive_search_start);
                    break;
                case 'S': /* Ctrl-S: Forward search (when in search mode,
                             handled there) */
                    /* In normal mode, Ctrl-S is often flow control (XOFF) -
                     * ignore */
                    break;
                case 'U': /* Ctrl-U: Kill entire line */
                    execute_keybinding_action(&ctx, "C-u", handle_kill_line);
                    break;
                case 'W': /* Ctrl-W: Kill word backwards */
                    execute_keybinding_action(&ctx, "C-w", handle_kill_word);
                    break;
                case 'Y': /* Ctrl-Y: Yank */
                    execute_keybinding_action(&ctx, "C-y", handle_yank);
                    break;
                default: {
                    /* Try keybinding manager for other Ctrl+letter combinations
                     */
                    char keybind[8];
                    snprintf(keybind, sizeof(keybind), "C-%c",
                             (char)tolower(keycode));
                    execute_keybinding_action(&ctx, keybind, NULL);
                    break;
                }
                }
            }
            /* Handle Meta/Alt+letter combinations (Group 6 keybindings) */
            else if (event->data.special_key.key == LLE_KEY_UNKNOWN &&
                     (event->data.special_key.modifiers & LLE_MOD_ALT)) {
                uint32_t keycode = event->data.special_key.keycode;

                switch (keycode) {
                case 'f': /* Alt-F: Forward word */
                    execute_keybinding_action(&ctx, "M-f", NULL);
                    break;
                case 'b': /* Alt-B: Backward word */
                    execute_keybinding_action(&ctx, "M-b", NULL);
                    break;
                case '<': /* Alt-<: Beginning of buffer */
                    execute_keybinding_action(&ctx, "M-<", NULL);
                    break;
                case '>': /* Alt->: End of buffer */
                    execute_keybinding_action(&ctx, "M->", NULL);
                    break;
                case 'c': /* Alt-C: Capitalize word */
                    execute_keybinding_action(&ctx, "M-c", NULL);
                    break;
                case 'd': /* Alt-D: Kill word forward */
                    execute_keybinding_action(&ctx, "M-d", NULL);
                    break;
                case 'l': /* Alt-L: Downcase word */
                    execute_keybinding_action(&ctx, "M-l", NULL);
                    break;
                case 'u': /* Alt-U: Upcase word */
                    execute_keybinding_action(&ctx, "M-u", NULL);
                    break;
                case '_': /* Alt-_: Redo (undo the undo) */
                    execute_keybinding_action(&ctx, "M-_", handle_redo);
                    break;
                default: {
                    /* Try keybinding manager for other Alt+letter combinations
                     */
                    char keybind[8];
                    snprintf(keybind, sizeof(keybind), "M-%c", (char)keycode);
                    execute_keybinding_action(&ctx, keybind, NULL);
                    break;
                }
                }
            }
            /* Other special keys ignored */
            break;
        }

        case LLE_INPUT_TYPE_EOF: {
            /* EOF received */
            handle_eof(NULL, &ctx);
            break;
        }

        case LLE_INPUT_TYPE_SIGNAL: {
            /* Signal received (Ctrl-C, etc.) */
            if (event->data.signal.signal_number == 2) { /* SIGINT */
                done = true;
                final_line = NULL;
            }
            break;
        }

        case LLE_INPUT_TYPE_WINDOW_RESIZE: {
            /* Step 7: Window resize - refresh display with new dimensions */
            /* If completion menu is active, recalculate layout for new terminal
             * width */
            if (ctx.editor && ctx.editor->completion_system) {
                lle_completion_menu_state_t *menu =
                    lle_completion_system_get_menu(
                        ctx.editor->completion_system);
                if (menu && menu->menu_active) {
                    size_t new_width = event->data.resize.new_width;
                    lle_completion_menu_update_layout(menu, new_width);
                }
            }

            /* Trigger terminal-resize hook for registered widgets */
            if (editor_to_use && editor_to_use->widget_hooks_manager) {
                lle_widget_hook_trigger(editor_to_use->widget_hooks_manager,
                                        LLE_HOOK_TERMINAL_RESIZE,
                                        editor_to_use);
            }

            refresh_display(&ctx);
            break;
        }

        case LLE_INPUT_TYPE_ERROR: {
            /* Input error */
            done = true;
            final_line = NULL;
            break;
        }

        case LLE_INPUT_TYPE_TIMEOUT: {
            /* Timeout - continue loop */
            break;
        }

        default: {
            /* Unknown event type - ignore */
            break;
        }
        }

        /* WATCHDOG: Check after event dispatch (catches hangs in handlers).
         * This is a secondary check - handlers should already abort early
         * if watchdog fired, but this catches any that don't.
         */
        if (lle_watchdog_check()) {
            /* Don't clear here - let the loop start check handle it.
             * Just note that we detected it after dispatch. */
        }

        /* Event processed - in Step 1 we don't free events (managed by input
         * processor) */
    }

    /* === WIDGET HOOK: LINE_FINISH === */
    /* Trigger line-finish hook at end of readline (ZSH zle-line-finish) */
    if (editor_to_use && editor_to_use->widget_hooks_manager) {
        lle_widget_hook_trigger(editor_to_use->widget_hooks_manager,
                                LLE_HOOK_LINE_FINISH, editor_to_use);
    }

    /* === STEP 10: Exit raw mode and finalize input === */

    /* Clear the LLE readline active flag before exiting raw mode
     * This ensures the signal handler knows we're no longer in readline */
    set_lle_readline_active(0);

    lle_unix_interface_exit_raw_mode(unix_iface);

    /* If we got a line, fire LINE_ACCEPTED hook then finalize input.
     *
     * LINE_ACCEPTED Hook (Spec 25 Section 12):
     * This hook fires when the user has pressed Enter and the line is
     * complete, but BEFORE dc_finalize_input() which:
     * 1. Writes \n to move cursor down
     * 2. Resets screen buffer state (prompt metrics lost)
     *
     * At this point:
     * - Cursor is at end of command line
     * - Screen buffer has valid prompt metrics (command_start_row, etc.)
     * - Handlers can use relative cursor movement to modify display
     *
     * Primary use case: Transient prompts - replace fancy prompt with
     * minimal version in scrollback before cursor moves to output area.
     */
    if (final_line) {
        /* === WIDGET HOOK: LINE_ACCEPTED === */
        if (editor_to_use && editor_to_use->widget_hooks_manager) {
            lle_widget_hook_trigger(editor_to_use->widget_hooks_manager,
                                    LLE_HOOK_LINE_ACCEPTED, editor_to_use);
        }

        dc_finalize_input();
    }

    /* === STEP 11: Cleanup and return === */
    /* Step 5 enhancement: Free kill buffer */
    if (kill_buffer) {
        free(kill_buffer);
    }

    /* Fish-style autosuggestions: Free suggestion buffer */
    if (ctx.current_suggestion) {
        free(ctx.current_suggestion);
    }

    /* Group 1+ migration: Cleanup keybinding manager */
    if (keybinding_manager) {
        lle_keybinding_manager_destroy(keybinding_manager);
    }

    /* Clear completion system state at end of readline to prevent memory leaks.
     * Any active completion results from TAB presses during this edit session
     * need to be freed before the next readline call or shell exit. */
    if (editor_to_use && editor_to_use->completion_system) {
        lle_completion_system_clear(editor_to_use->completion_system);
    }

    /* Step 6: Cleanup continuation state, destroy event system, buffer, and
     * terminal */
    continuation_state_cleanup(&continuation_state);
    lle_event_system_destroy(event_system);
    lle_buffer_destroy(buffer);

    /* Destroy edit-session arena - frees all per-readline allocations.
     * Currently kill_buffer and current_suggestion are still using malloc,
     * but future allocations can use lle_arena_alloc(edit_arena, ...) and
     * will be automatically freed here. */
    if (edit_arena) {
        lle_arena_destroy(edit_arena);
    }

    /* Clear editor's buffer pointer to prevent double-free on shell exit.
     * The buffer was created per-readline call and assigned to the persistent
     * editor (g_lle_integration->editor). Now that we've destroyed it, we must
     * clear the reference so lle_editor_destroy won't try to free it again. */
    if (editor_to_use) {
        editor_to_use->buffer = NULL;
        if (editor_to_use->cursor_manager) {
            editor_to_use->cursor_manager->buffer = NULL;
        }
    }

    lle_terminal_abstraction_destroy(term);

    /* WATCHDOG: Stop watchdog on normal exit */
    lle_watchdog_stop();

    return final_line;
}
