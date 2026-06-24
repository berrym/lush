/**
 * @file keybinding_actions.c
 * @brief Default Keybinding Action Function Implementations
 *
 * Complete implementation of all 44 GNU Readline compatible keybinding actions.
 * Provides 100% compatibility with Emacs-style keybindings including movement,
 * editing, history navigation, completion, and shell-specific operations.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md
 */

#include "lle/keybinding_actions.h"
#include "config.h"          /// For lle_dedup_navigation config option
#include "config_registry.h" /// For completion.chain_directories
#include "display_controller.h"
#include "display_integration.h"
#include "lle/buffer_management.h"
#include "lle/completion/completion_menu_logic.h"
#include "lle/completion/completion_system.h"
#include "lle/completion/splicer.h"
#include "lle/completion/word_context.h"
#include "lle/display_integration.h"
#include "lle/history.h"
#include "lle/keybinding.h"
#include "lle/kill_ring.h"
#include "lle/notification.h"     /// For multiline history hint notifications
#include "lle/unicode_compare.h"  /// For Unicode-aware dedup comparison
#include "lle/unicode_grapheme.h" /// For grapheme boundary detection
#include "lle/utf8_support.h"     /// For UTF-8 decoding
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wctype.h> /// For Unicode-aware iswalpha/iswalnum

/* ============================================================================
 * DEBUG LOGGING (disabled - enable for debugging)
 * ============================================================================
 */
#if 0
static void debug_log(const char *fmt, ...) {
    FILE *f = fopen("/tmp/lle_arrow_debug.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}
#else
#define debug_log(...) ((void)0)
#endif

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/* History navigation position is now stored in editor->history_navigation_pos
 */

/**
 * @brief Check if a Unicode codepoint is a word character
 *
 * Uses wctype.h for Unicode-aware character classification.
 * Word characters are alphanumeric or underscore.
 *
 * @param cp Unicode codepoint to check
 * @return true if word character, false otherwise
 */
LLE_MAYBE_UNUSED
static bool is_word_codepoint(uint32_t cp) {
    /// Underscore is always a word character
    if (cp == '_')
        return true;

    /// Use Unicode-aware classification for alphanumeric
    return iswalnum((wint_t)cp);
}

/**
 * @brief Check if a Unicode codepoint is a shell metacharacter
 * @param cp Unicode codepoint to check
 * @return true if shell metacharacter (word boundary), false otherwise
 */
static bool is_shell_metachar(uint32_t cp) {
    return cp == '|' || cp == '&' || cp == ';' || cp == '(' || cp == ')' ||
           cp == '<' || cp == '>' || cp == '\'' || cp == '"' || cp == '`' ||
           cp == '$' || cp == '\\';
}

/**
 * @brief Check if a Unicode codepoint is whitespace
 * @param cp Unicode codepoint to check
 * @return true if whitespace, false otherwise
 */
static bool is_whitespace_codepoint(uint32_t cp) {
    return iswspace((wint_t)cp);
}

/**
 * @brief Check if character is Unix word boundary (whitespace only, for Ctrl-W)
 * @param c Character to check
 * @return true if word boundary (whitespace or null), false otherwise
 */
static bool is_unix_word_boundary(char c) {
    return isspace((unsigned char)c) || c == '\0';
}

/**
 * @brief Find start of previous grapheme cluster from byte position
 * @param text Text buffer
 * @param len Length of text buffer
 * @param pos Current byte position
 * @return Byte offset of the start of the previous grapheme cluster
 */
static size_t find_prev_grapheme_start(const char *text, size_t len,
                                       size_t pos) {
    if (pos == 0)
        return 0;

    const char *start = text;
    const char *end = text + len;
    const char *ptr = text + pos;

    /// Move back one byte at minimum
    ptr--;

    /// Scan backwards until we find a grapheme boundary
    while (ptr > start && !lle_is_grapheme_boundary(ptr, start, end)) {
        ptr--;
    }

    return (size_t)(ptr - start);
}

/**
 * @brief Find end of current grapheme cluster from byte position
 * @param text Text buffer
 * @param len Length of text buffer
 * @param pos Current byte position
 * @return Byte offset just past the end of the current grapheme cluster
 */
static size_t find_next_grapheme_end(const char *text, size_t len, size_t pos) {
    if (pos >= len)
        return len;

    const char *start = text;
    const char *end = text + len;
    const char *ptr = text + pos;

    /// Move to next UTF-8 character
    int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
    if (seq_len <= 0)
        seq_len = 1;
    ptr += seq_len;

    /// Continue until we find a grapheme boundary
    while (ptr < end && !lle_is_grapheme_boundary(ptr, start, end)) {
        seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len <= 0)
            seq_len = 1;
        ptr += seq_len;
    }

    return (size_t)(ptr - start);
}

/**
 * @brief Decode codepoint at grapheme cluster start position
 * @param text Text buffer
 * @param len Length of text buffer
 * @param pos Byte position to decode at
 * @return Unicode codepoint at position, or byte value on invalid UTF-8
 */
static uint32_t decode_codepoint_at(const char *text, size_t len, size_t pos) {
    if (pos >= len)
        return 0;

    uint32_t cp = 0;
    int decoded = lle_utf8_decode_codepoint(text + pos, len - pos, &cp);
    if (decoded <= 0) {
        /// Fallback to byte value for invalid UTF-8
        return (unsigned char)text[pos];
    }
    return cp;
}

/**
 * @brief Find start of current word from position (grapheme-aware)
 *
 * Works by scanning grapheme clusters backwards, checking if each
 * grapheme's first codepoint is a word character.
 *
 * @param text Text buffer
 * @param pos Current byte position
 * @return Byte offset of the start of the word
 */
static size_t find_word_start(const char *text, size_t pos) {
    if (pos == 0)
        return 0;

    size_t len = strlen(text);
    size_t current = pos;

    /// Skip whitespace backward (by grapheme clusters)
    while (current > 0) {
        size_t prev = find_prev_grapheme_start(text, len, current);
        uint32_t cp = decode_codepoint_at(text, len, prev);

        if (!is_whitespace_codepoint(cp)) {
            break;
        }
        current = prev;
    }

    /// Find beginning of word (by grapheme clusters)
    while (current > 0) {
        size_t prev = find_prev_grapheme_start(text, len, current);
        uint32_t cp = decode_codepoint_at(text, len, prev);

        /// Stop at whitespace or shell metacharacters
        if (is_whitespace_codepoint(cp) || is_shell_metachar(cp)) {
            break;
        }
        current = prev;
    }

    return current;
}

/**
 * @brief Find end of current word from position (grapheme-aware)
 *
 * Works by scanning grapheme clusters forward, checking if each
 * grapheme's first codepoint is a word character.
 *
 * @param text Text buffer
 * @param len Length of text buffer
 * @param pos Current byte position
 * @return Byte offset just past the end of the word
 */
static size_t find_word_end(const char *text, size_t len, size_t pos) {
    size_t current = pos;

    /// Skip whitespace forward (by grapheme clusters)
    while (current < len) {
        uint32_t cp = decode_codepoint_at(text, len, current);

        if (!is_whitespace_codepoint(cp)) {
            break;
        }
        current = find_next_grapheme_end(text, len, current);
    }

    /// Find end of word (by grapheme clusters)
    while (current < len) {
        uint32_t cp = decode_codepoint_at(text, len, current);

        /// Stop at whitespace or shell metacharacters
        if (is_whitespace_codepoint(cp) || is_shell_metachar(cp)) {
            break;
        }
        current = find_next_grapheme_end(text, len, current);
    }

    return current;
}

/* ============================================================================
 * COMPLETION HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * NOTE: With proper architecture, completion menu is managed by
 * display_controller No need to access command_layer directly anymore - removed
 * get_command_layer_from_display()
 */

/**
 * @brief Trigger display refresh after completion changes
 *
 * With proper architecture, menu changes are picked up automatically via
 * events.
 *
 * @param dc Display controller instance
 */
static void refresh_after_completion(display_controller_t *dc) {
    if (!dc) {
        return;
    }

    /// Directly publish REDRAW_NEEDED event and process immediately
    /// Event-based approach wasn't reliably triggering display update
    if (dc->event_system) {
        layer_event_t event = {.type = LAYER_EVENT_REDRAW_NEEDED,
                               .source_layer = LAYER_ID_DISPLAY_CONTROLLER,
                               .timestamp = 0};
        layer_events_publish(dc->event_system, &event);

        /// Process events immediately to ensure display updates
        layer_events_process_pending(dc->event_system, 100, 0);
    }
}

/**
 * @brief Update inline text with selected completion
 *
 * CRITICAL: Must use current word boundaries, not original context
 * because after the first replacement, the original boundaries are stale.
 *
 * For external commands that shadow builtins/aliases, the description field
 * contains the full path (e.g., "/usr/bin/echo"). When the user selects
 * such a command, we insert the full path to explicitly bypass the builtin.
 *
 * @param editor Editor instance
 * @param menu Completion menu state
 * @param state Completion state with results
 */
static void update_inline_completion(lle_editor_t *editor,
                                     lle_completion_menu_state_t *menu,
                                     lle_completion_state_t *state) {
    if (!editor || !menu || !state || !state->results) {
        return;
    }

    if (menu->selected_index >= state->results->count) {
        return;
    }

    const lle_completion_item_t *selected =
        &state->results->items[menu->selected_index];

    /// Re-analyze the buffer at its CURRENT cursor: prior cycling
    /// iterations have mutated the filename-portion bytes, so the
    /// boundaries from the initial generation are stale.
    lle_word_context_t *current_context = NULL;
    lle_result_t ctx_result = lle_word_context_analyze(
        editor->buffer->data, editor->buffer->cursor.byte_offset,
        editor->lle_pool, &current_context);
    if (ctx_result != LLE_SUCCESS || !current_context)
        return;

    /// External commands that shadow a builtin or alias carry their
    /// full PATH-resolved path in description; the engine splices that
    /// full path so the user picks the disambiguating absolute name.
    /// For all other types the candidate's text is what gets spliced.
    lle_completion_item_t synthetic_item = *selected;
    if (selected->type == LLE_COMPLETION_TYPE_COMMAND &&
        selected->description != NULL) {
        synthetic_item.text = (char *)selected->description;
    }

    (void)lle_splicer_apply_preview(editor->buffer, editor->cursor_manager,
                                    current_context, &synthetic_item,
                                    editor->lle_pool);
    lle_word_context_free(current_context);
}

/**
 * @brief Clear active completion menu
 *
 * Clears both the completion system state and the display_controller menu.
 * The caller (lle_self_insert) is a SIMPLE action, so refresh_display() will
 * be called automatically by execute_keybinding_action() framework.
 *
 * @param editor Editor instance
 */
static void clear_completion_menu(lle_editor_t *editor) {
    if (!editor) {
        return;
    }

    /// Clear completion system if available
    if (editor->completion_system) {
        lle_completion_system_clear(editor->completion_system);
    }

    /// Clear menu from display_controller
    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        display_controller_clear_completion_menu(dc);
        /// Note: menu_state_changed flag is set, will trigger redraw in
        /// refresh_display()
    }
}

/**
 * @brief Get current line boundaries for multiline buffer
 * @param buffer Buffer instance
 * @param start Pointer to store line start offset
 * @param end Pointer to store line end offset
 */
static void get_current_line_bounds(lle_buffer_t *buffer, size_t *start,
                                    size_t *end) {
    const char *data = buffer->data;
    size_t cursor = buffer->cursor.byte_offset;
    size_t len = buffer->length;

    /// Find start of current line
    *start = cursor;
    while (*start > 0 && data[*start - 1] != '\n') {
        (*start)--;
    }

    /// Find end of current line
    *end = cursor;
    while (*end < len && data[*end] != '\n') {
        (*end)++;
    }
}

/* ============================================================================
 * MOVEMENT ACTIONS
 * ============================================================================
 */

/**
 * @brief Move cursor to beginning of current line (C-a, HOME)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on NULL editor
 */
lle_result_t lle_beginning_of_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column on horizontal movement
    if (editor->cursor_manager) {
        editor->cursor_manager->sticky_column = false;
    }

    /// For multiline: move to beginning of current logical line
    if (editor->buffer->length > 0 && strchr(editor->buffer->data, '\n')) {
        size_t line_start, line_end;
        get_current_line_bounds(editor->buffer, &line_start, &line_end);
        editor->buffer->cursor.byte_offset = line_start;
        editor->buffer->cursor.codepoint_index = line_start;
        editor->buffer->cursor.grapheme_index = line_start;
    } else {
        /// Single line: move to buffer beginning
        editor->buffer->cursor.byte_offset = 0;
        editor->buffer->cursor.codepoint_index = 0;
        editor->buffer->cursor.grapheme_index = 0;
    }

    /// CRITICAL: Sync cursor_manager with buffer cursor after direct
    /// modification
    if (editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Move cursor to end of current line (C-e, END)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on NULL editor
 */
lle_result_t lle_end_of_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column on horizontal movement
    if (editor->cursor_manager) {
        editor->cursor_manager->sticky_column = false;
    }

    /// For multiline: move to end of current logical line
    if (editor->buffer->length > 0 && strchr(editor->buffer->data, '\n')) {
        size_t line_start, line_end;
        get_current_line_bounds(editor->buffer, &line_start, &line_end);
        editor->buffer->cursor.byte_offset = line_end;
        editor->buffer->cursor.codepoint_index = line_end;
        editor->buffer->cursor.grapheme_index = line_end;
    } else {
        /// Single line: move to buffer end
        editor->buffer->cursor.byte_offset = editor->buffer->length;
        editor->buffer->cursor.codepoint_index = editor->buffer->length;
        editor->buffer->cursor.grapheme_index = editor->buffer->length;
    }

    /// CRITICAL: Sync cursor_manager with buffer cursor after direct
    /// modification
    if (editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Move cursor forward one character (C-f, RIGHT)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_forward_char(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If completion menu is active, navigate columns
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu) {
            lle_completion_menu_move_right(menu);

            /// Update inline text for completion
            lle_completion_state_t *state =
                lle_completion_system_get_state(editor->completion_system);
            if (state) {
                update_inline_completion(editor, menu, state);
            }

            /// Menu state has changed, trigger refresh
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                refresh_after_completion(dc);
            }
            return LLE_SUCCESS;
        }
    }

    /// Clear sticky column on horizontal movement
    editor->cursor_manager->sticky_column = false;

    /// Use cursor_manager to move forward by one grapheme cluster
    lle_result_t result =
        lle_cursor_manager_move_by_graphemes(editor->cursor_manager, 1);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/**
 * @brief Move cursor backward one character (C-b, LEFT)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_backward_char(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If completion menu is active, navigate columns
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu) {
            lle_completion_menu_move_left(menu);

            /// Update inline text for completion
            lle_completion_state_t *state =
                lle_completion_system_get_state(editor->completion_system);
            if (state) {
                update_inline_completion(editor, menu, state);
            }

            /// Menu state has changed, trigger refresh
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                refresh_after_completion(dc);
            }
            return LLE_SUCCESS;
        }
    }

    /// Clear sticky column on horizontal movement
    editor->cursor_manager->sticky_column = false;

    /// Use cursor_manager to move backward by one grapheme cluster
    lle_result_t result =
        lle_cursor_manager_move_by_graphemes(editor->cursor_manager, -1);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/**
 * @brief Move cursor forward one word (M-f)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_forward_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column on horizontal movement
    editor->cursor_manager->sticky_column = false;

    /// Find the end of the current word
    size_t new_pos = find_word_end(editor->buffer->data, editor->buffer->length,
                                   editor->buffer->cursor.byte_offset);

    /// Use cursor_manager to move to the calculated position
    lle_result_t result =
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, new_pos);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/**
 * @brief Move cursor backward one word (M-b)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_backward_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column on horizontal movement
    editor->cursor_manager->sticky_column = false;

    /// Find the start of the current/previous word
    size_t new_pos = find_word_start(editor->buffer->data,
                                     editor->buffer->cursor.byte_offset);

    /// Use cursor_manager to move to the calculated position
    lle_result_t result =
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, new_pos);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/* ============================================================================
 * LINE AND BUFFER NAVIGATION
 * ============================================================================
 */

/**
 * @brief Move cursor to beginning of buffer (M-<)
 *
 * Always moves to position 0, regardless of lines.
 * Clears sticky column since this is a deliberate jump.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_beginning_of_buffer(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column - deliberate jump to buffer start
    editor->cursor_manager->sticky_column = false;

    /// Move to byte offset 0 using cursor manager
    lle_result_t result =
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, 0);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/**
 * @brief Move cursor to end of buffer (M->)
 *
 * Always moves to buffer length, regardless of lines.
 * Clears sticky column since this is a deliberate jump.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_end_of_buffer(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear sticky column - deliberate jump to buffer end
    editor->cursor_manager->sticky_column = false;

    /// Move to end of buffer using cursor manager
    lle_result_t result = lle_cursor_manager_move_to_byte_offset(
        editor->cursor_manager, editor->buffer->length);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return result;
}

/**
 * @brief Move cursor to previous line (up arrow in multiline mode)
 *
 * Preserves horizontal column position using sticky_column.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_previous_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *data = editor->buffer->data;
    size_t cursor = editor->buffer->cursor.byte_offset;

    debug_log("previous_line: cursor=%zu, buffer_len=%zu", cursor,
              editor->buffer->length);

    /// Get current line boundaries
    size_t curr_line_start, curr_line_end;
    get_current_line_bounds(editor->buffer, &curr_line_start, &curr_line_end);

    debug_log("previous_line: curr_line_start=%zu, curr_line_end=%zu",
              curr_line_start, curr_line_end);

    /// If we're on the first line, can't move up
    if (curr_line_start == 0) {
        debug_log("previous_line: already on first line, returning no-op");
        return LLE_SUCCESS; /// No-op, stay on first line
    }

    /// Find previous line boundaries
    /// curr_line_start points to first char of current line (after the '\n')
    /// So curr_line_start - 1 is the '\n' that ends the previous line

    /// The newline at curr_line_start - 1 terminates the previous line.
    /// We need to find where that previous line starts.
    ///
    /// Example: "line1\n\nline3" with cursor on line3
    ///          012345 6 789...
    ///          line1  \n \n line3
    ///                 ^  ^
    ///                 |  curr_line_start = 7
    ///                 prev line's terminating newline = 6
    ///
    /// The previous line (empty) spans from index 6 to 6 (just the newline).
    /// For an empty line, prev_line_start = prev_line_end = position after
    /// prior newline.

    size_t prev_line_terminator =
        curr_line_start - 1; /// The '\n' that ends prev line

    /// Find start of previous line by searching backwards for newline (or
    /// buffer start)
    size_t prev_line_start = prev_line_terminator;
    while (prev_line_start > 0 && data[prev_line_start - 1] != '\n') {
        prev_line_start--;
    }

    /// Calculate or retrieve preferred column
    size_t target_column;
    if (editor->cursor_manager->sticky_column) {
        /// Use saved preferred column
        target_column = editor->cursor_manager->preferred_visual_column;
    } else {
        /// First vertical movement - save current column
        target_column = cursor - curr_line_start;
        editor->cursor_manager->preferred_visual_column = target_column;
        editor->cursor_manager->sticky_column = true;
    }

    /// Calculate new cursor position on previous line
    /// prev_line_start points to first character of line content
    /// prev_line_terminator points to the '\n' that ends the line
    /// Line length = prev_line_terminator - prev_line_start (0 for empty lines)
    size_t prev_line_length = prev_line_terminator - prev_line_start;
    size_t new_cursor = prev_line_start + target_column;

    /// Clamp to end of previous line if column is too far right
    if (target_column > prev_line_length) {
        new_cursor = prev_line_terminator; /* Position cursor at end of line
                                              (before newline) */
    }

    /// Temporarily disable sticky_column to prevent move_to_byte_offset from
    /// overwriting preferred_visual_column
    bool was_sticky = editor->cursor_manager->sticky_column;
    size_t saved_preferred = editor->cursor_manager->preferred_visual_column;
    editor->cursor_manager->sticky_column = false;

    /// Use cursor_manager to move (updates all fields properly)
    lle_result_t result = lle_cursor_manager_move_to_byte_offset(
        editor->cursor_manager, new_cursor);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    /// Restore sticky_column state
    editor->cursor_manager->sticky_column = was_sticky;
    editor->cursor_manager->preferred_visual_column = saved_preferred;

    return result;
}

/**
 * @brief Move cursor to previous line with boundary detection
 *
 * Extended version that reports when the cursor was already on the first line.
 *
 * @param editor Editor instance
 * @return Result with hit_boundary flag
 */
lle_line_nav_result_t lle_previous_line_ex(lle_editor_t *editor) {
    lle_line_nav_result_t nav_result = {LLE_SUCCESS, false};

    if (!editor || !editor->buffer || !editor->cursor_manager) {
        nav_result.result = LLE_ERROR_INVALID_PARAMETER;
        return nav_result;
    }

    /// Get current line boundaries
    size_t curr_line_start, curr_line_end;
    get_current_line_bounds(editor->buffer, &curr_line_start, &curr_line_end);

    /// Check if we're on the first line (boundary condition)
    if (curr_line_start == 0) {
        nav_result.hit_boundary = true;
        nav_result.result = LLE_SUCCESS; /// No-op, stay on first line
        return nav_result;
    }

    /// Not at boundary - perform the actual movement
    nav_result.result = lle_previous_line(editor);
    return nav_result;
}

/**
 * @brief Move cursor to next line (down arrow in multiline mode)
 *
 * Preserves horizontal column position using sticky_column.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_next_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *data = editor->buffer->data;
    size_t cursor = editor->buffer->cursor.byte_offset;
    size_t len = editor->buffer->length;

    /// Get current line boundaries
    size_t curr_line_start, curr_line_end;
    get_current_line_bounds(editor->buffer, &curr_line_start, &curr_line_end);

    /// If we're on the last line, can't move down
    if (curr_line_end >= len || data[curr_line_end] != '\n') {
        return LLE_SUCCESS; /// No-op, stay on last line
    }

    /// Find next line boundaries
    size_t next_line_start = curr_line_end + 1; /// Skip the '\n'
    size_t next_line_end = next_line_start;

    while (next_line_end < len && data[next_line_end] != '\n') {
        next_line_end++;
    }

    /// Calculate or retrieve preferred column
    size_t target_column;
    if (editor->cursor_manager->sticky_column) {
        /// Use saved preferred column
        target_column = editor->cursor_manager->preferred_visual_column;
    } else {
        /// First vertical movement - save current column
        target_column = cursor - curr_line_start;
        editor->cursor_manager->preferred_visual_column = target_column;
        editor->cursor_manager->sticky_column = true;
    }

    /// Calculate new cursor position on next line
    /// next_line_end points to newline at end of line (or buffer end)
    /// next_line_start points to first character
    /// Line length is distance between them, cursor at next_line_end positions
    /// after last char
    size_t next_line_length = next_line_end - next_line_start;
    size_t new_cursor = next_line_start + target_column;

    /// Clamp to end of next line if column is too far right
    if (target_column > next_line_length) {
        new_cursor = next_line_end; /// Position cursor at/after last character
    }

    /// Temporarily disable sticky_column to prevent move_to_byte_offset from
    /// overwriting preferred_visual_column
    bool was_sticky = editor->cursor_manager->sticky_column;
    size_t saved_preferred = editor->cursor_manager->preferred_visual_column;
    editor->cursor_manager->sticky_column = false;

    /// Use cursor_manager to move (updates all fields properly)
    lle_result_t result = lle_cursor_manager_move_to_byte_offset(
        editor->cursor_manager, new_cursor);

    /// CRITICAL: Sync buffer cursor back from cursor manager after movement
    if (result == LLE_SUCCESS) {
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    /// Restore sticky_column state
    editor->cursor_manager->sticky_column = was_sticky;
    editor->cursor_manager->preferred_visual_column = saved_preferred;

    return result;
}

/**
 * @brief Move cursor to next line with boundary detection
 *
 * Extended version that reports when the cursor was already on the last line.
 *
 * @param editor Editor instance
 * @return Result with hit_boundary flag
 */
lle_line_nav_result_t lle_next_line_ex(lle_editor_t *editor) {
    lle_line_nav_result_t nav_result = {LLE_SUCCESS, false};

    if (!editor || !editor->buffer || !editor->cursor_manager) {
        nav_result.result = LLE_ERROR_INVALID_PARAMETER;
        return nav_result;
    }

    const char *data = editor->buffer->data;
    size_t len = editor->buffer->length;

    /// Get current line boundaries
    size_t curr_line_start, curr_line_end;
    get_current_line_bounds(editor->buffer, &curr_line_start, &curr_line_end);

    /// Check if we're on the last line (boundary condition)
    if (curr_line_end >= len || data[curr_line_end] != '\n') {
        nav_result.hit_boundary = true;
        nav_result.result = LLE_SUCCESS; /// No-op, stay on last line
        return nav_result;
    }

    /// Not at boundary - perform the actual movement
    nav_result.result = lle_next_line(editor);
    return nav_result;
}

/**
 * @brief Smart up arrow: Navigate completion menu, buffer lines, or history
 *
 * Behavior:
 * - Completion menu active: Move up in menu
 * - Multi-line mode: Navigate to previous line in buffer
 * - Single-line mode: Navigate command history (backward)
 *
 * This prevents accidental history navigation while editing multi-line
 * constructs.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_smart_up_arrow(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    debug_log("smart_up_arrow: buffer_len=%zu, cursor=%zu",
              editor->buffer->length, editor->buffer->cursor.byte_offset);

    /// If completion menu is active, navigate within menu
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu) {
            lle_completion_menu_move_up(menu);

            /// Update inline text for completion
            lle_completion_state_t *state =
                lle_completion_system_get_state(editor->completion_system);
            if (state) {
                update_inline_completion(editor, menu, state);
            }

            /// Menu state has changed, trigger refresh
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                refresh_after_completion(dc);
            }
            return LLE_SUCCESS;
        }
    }

    /// Check if buffer is multiline (contains newline)
    bool is_multiline =
        (editor->buffer->length > 0 &&
         memchr(editor->buffer->data, '\n', editor->buffer->length) != NULL);

    debug_log("smart_up_arrow: is_multiline=%d", is_multiline);

    if (is_multiline) {
        /// Multi-line mode: navigate within buffer
        debug_log("smart_up_arrow: calling lle_previous_line");
        lle_result_t res = lle_previous_line(editor);
        debug_log(
            "smart_up_arrow: lle_previous_line returned %d, cursor now=%zu",
            res, editor->buffer->cursor.byte_offset);
        return res;
    } else {
        /// Single-line mode: navigate history
        debug_log("smart_up_arrow: calling lle_history_previous");
        return lle_history_previous(editor);
    }
}

/**
 * @brief Smart down arrow: Navigate completion menu, buffer lines, or history
 *
 * Behavior:
 * - Completion menu active: Move down in menu
 * - Multi-line mode: Navigate to next line in buffer
 * - Single-line mode: Navigate command history (forward)
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_smart_down_arrow(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If completion menu is active, navigate within menu
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu) {
            lle_completion_menu_move_down(menu);

            /// Update inline text for completion
            lle_completion_state_t *state =
                lle_completion_system_get_state(editor->completion_system);
            if (state) {
                update_inline_completion(editor, menu, state);
            }

            /// Menu state has changed, trigger refresh
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                refresh_after_completion(dc);
            }
            return LLE_SUCCESS;
        }
    }

    /// Check if buffer is multiline (contains newline)
    bool is_multiline =
        (editor->buffer->length > 0 &&
         memchr(editor->buffer->data, '\n', editor->buffer->length) != NULL);

    if (is_multiline) {
        /// Multi-line mode: navigate within buffer
        return lle_next_line(editor);
    } else {
        /// Single-line mode: navigate history
        return lle_history_next(editor);
    }
}

/* ============================================================================
 * EDITING ACTIONS - DELETION AND KILLING
 * ============================================================================
 */

/**
 * @brief Delete character at cursor position (C-d, DELETE)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_delete_char(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Dismiss completion menu on delete
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        clear_completion_menu(editor);
    }

    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t buffer_length = editor->buffer->length;

    /// If at end of buffer and buffer is empty, send EOF (bash behavior)
    if (cursor_pos >= buffer_length && buffer_length == 0) {
        return lle_send_eof(editor);
    }

    /// Delete grapheme cluster at cursor if not at end
    if (editor->buffer->cursor.grapheme_index <
            editor->buffer->grapheme_count &&
        editor->cursor_manager) {
        /// Sync cursor manager position with buffer cursor before moving
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);

        /// Move cursor forward by one grapheme to find the end of the grapheme
        /// to delete
        size_t grapheme_start = editor->buffer->cursor.byte_offset;

        lle_result_t result =
            lle_cursor_manager_move_by_graphemes(editor->cursor_manager, 1);
        if (result == LLE_SUCCESS) {
            /// CRITICAL: Sync buffer cursor back from cursor manager after
            /// movement
            lle_cursor_manager_get_position(editor->cursor_manager,
                                            &editor->buffer->cursor);

            size_t grapheme_end = editor->buffer->cursor.byte_offset;
            size_t grapheme_len = grapheme_end - grapheme_start;

            /// Delete the entire grapheme cluster
            result = lle_buffer_delete_text(editor->buffer, grapheme_start,
                                            grapheme_len);

            /// CRITICAL: After deletion, cursor should be at deletion point,
            /// sync cursor_manager
            if (result == LLE_SUCCESS) {
                lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                                       grapheme_start);
            }
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Delete character before cursor position (BACKSPACE)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_backward_delete_char(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// In-menu type-to-filter: when the menu is open and the user has
    /// already typed filter characters, backspace widens the filter
    /// by one codepoint rather than mutating the buffer. When no
    /// filter is active, backspace dismisses the menu and proceeds
    /// to a normal buffer delete -- the legacy behavior.
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu && lle_completion_menu_filter_active(menu)) {
            lle_result_t pres = lle_completion_menu_pop_filter_char(menu);
            if (pres == LLE_SUCCESS && menu->result &&
                menu->result->count > 0) {
                lle_completion_state_t *cstate =
                    lle_completion_system_get_state(editor->completion_system);
                if (cstate) {
                    update_inline_completion(editor, menu, cstate);
                }
                display_controller_t *dc = display_integration_get_controller();
                if (dc) {
                    dc->menu_state_changed = true;
                }
                return LLE_SUCCESS;
            }
        }
        clear_completion_menu(editor);
    }

    if (editor->buffer->cursor.byte_offset > 0 && editor->cursor_manager) {
        /// Sync cursor manager position with buffer cursor before moving
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);

        /// Check if we can move back (after sync)
        if (editor->buffer->cursor.grapheme_index == 0) {
            return LLE_SUCCESS; /// Already at beginning
        }

        /// Move cursor back by one grapheme to find the start of the grapheme
        /// to delete
        size_t current_byte = editor->buffer->cursor.byte_offset;

        lle_result_t result =
            lle_cursor_manager_move_by_graphemes(editor->cursor_manager, -1);
        if (result == LLE_SUCCESS) {
            /// CRITICAL: Sync buffer cursor back from cursor manager after
            /// movement
            lle_cursor_manager_get_position(editor->cursor_manager,
                                            &editor->buffer->cursor);

            size_t grapheme_start = editor->buffer->cursor.byte_offset;
            size_t grapheme_len = current_byte - grapheme_start;

            /// Delete the entire grapheme cluster
            result = lle_buffer_delete_text(editor->buffer, grapheme_start,
                                            grapheme_len);

            /// CRITICAL: After deletion, ensure cursor_manager is synced with
            /// buffer cursor
            if (result == LLE_SUCCESS) {
                lle_cursor_manager_move_to_byte_offset(
                    editor->cursor_manager, editor->buffer->cursor.byte_offset);
            }
        }

        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Kill text from cursor to end of line (C-k)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t kill_end;

    /// For multiline: kill to end of current logical line
    if (editor->buffer->length > 0 && strchr(editor->buffer->data, '\n')) {
        size_t line_start, line_end;
        get_current_line_bounds(editor->buffer, &line_start, &line_end);
        kill_end = line_end;
    } else {
        /// Single line: kill to end of buffer
        kill_end = editor->buffer->length;
    }

    if (cursor_pos < kill_end) {
        size_t kill_len = kill_end - cursor_pos;
        char *killed_text =
            strndup(editor->buffer->data + cursor_pos, kill_len);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete the text
        return lle_buffer_delete_text(editor->buffer, cursor_pos, kill_len);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Kill text from beginning of line to cursor (C-u backward kill)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_backward_kill_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t kill_start;

    /// For multiline: kill from beginning of current logical line
    if (editor->buffer->length > 0 && strchr(editor->buffer->data, '\n')) {
        size_t line_start, line_end;
        get_current_line_bounds(editor->buffer, &line_start, &line_end);
        kill_start = line_start;
    } else {
        /// Single line: kill from beginning (bash behavior)
        kill_start = 0;
    }

    if (cursor_pos > kill_start) {
        size_t kill_len = cursor_pos - kill_start;
        char *killed_text =
            strndup(editor->buffer->data + kill_start, kill_len);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete the text
        lle_result_t result =
            lle_buffer_delete_text(editor->buffer, kill_start, kill_len);
        if (result == LLE_SUCCESS) {
            editor->buffer->cursor.byte_offset = kill_start;
            editor->buffer->cursor.codepoint_index = kill_start;
            editor->buffer->cursor.grapheme_index = kill_start;

            /// CRITICAL: Sync cursor_manager after modifying buffer cursor
            if (editor->cursor_manager) {
                lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                                       kill_start);
            }
        }
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Kill text from cursor to end of word (M-d)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t word_end =
        find_word_end(editor->buffer->data, editor->buffer->length, cursor_pos);

    if (word_end > cursor_pos) {
        size_t kill_len = word_end - cursor_pos;
        char *killed_text =
            strndup(editor->buffer->data + cursor_pos, kill_len);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete the text
        return lle_buffer_delete_text(editor->buffer, cursor_pos, kill_len);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Kill text from beginning of word to cursor (M-BACKSPACE)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_backward_kill_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t word_start = find_word_start(editor->buffer->data, cursor_pos);

    if (cursor_pos > word_start) {
        size_t kill_len = cursor_pos - word_start;
        char *killed_text =
            strndup(editor->buffer->data + word_start, kill_len);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete the text
        lle_result_t result =
            lle_buffer_delete_text(editor->buffer, word_start, kill_len);
        if (result == LLE_SUCCESS) {
            editor->buffer->cursor.byte_offset = word_start;
            editor->buffer->cursor.codepoint_index = word_start;
            editor->buffer->cursor.grapheme_index = word_start;

            /// CRITICAL: Sync cursor_manager after modifying buffer cursor
            if (editor->cursor_manager) {
                lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                                       word_start);
            }
        }
        return result;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * EDITING ACTIONS - YANK AND TRANSPOSE
 * ============================================================================
 */

/**
 * @brief Yank (paste) text from kill ring (C-y)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_yank(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->kill_ring) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *yank_text = NULL;
    lle_result_t result =
        lle_kill_ring_get_current(editor->kill_ring, &yank_text);

    if (result != LLE_SUCCESS || !yank_text) {
        return LLE_SUCCESS; /// Nothing to yank
    }

    size_t yank_length = strlen(yank_text);
    result = lle_buffer_insert_text(editor->buffer,
                                    editor->buffer->cursor.byte_offset,
                                    yank_text, yank_length);

    /// CRITICAL: Sync cursor_manager after insertion moves cursor
    if (result == LLE_SUCCESS && editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);
    }

    return result;
}

/**
 * @brief Cycle through kill ring entries after yank (M-y)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_yank_pop(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->kill_ring) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Get next entry from kill ring (includes state check)
    const char *yank_text = NULL;
    lle_result_t result = lle_kill_ring_yank_pop(editor->kill_ring, &yank_text);

    if (result != LLE_SUCCESS || !yank_text) {
        return LLE_SUCCESS; /// Ignore if error or no text
    }

    /// Delete previously yanked text and insert new text
    size_t yank_length = strlen(yank_text);
    return lle_buffer_insert_text(editor->buffer,
                                  editor->buffer->cursor.byte_offset, yank_text,
                                  yank_length);
}

/**
 * @brief Transpose characters at cursor (C-t)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_transpose_chars(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *data = editor->buffer->data;
    size_t len = editor->buffer->length;
    size_t cursor = editor->buffer->cursor.byte_offset;

    /// Need at least 2 graphemes to transpose
    if (editor->buffer->grapheme_count < 2) {
        return LLE_SUCCESS;
    }

    /// Find grapheme boundaries around cursor.
    /// Transpose swaps the grapheme before cursor with the one at/after cursor.
    /// If at end of buffer, swap the last two graphemes.
    size_t g1_start, g1_end, g2_start, g2_end;

    if (cursor >= len) {
        /// At end of buffer: swap last two graphemes
        g2_end = len;
        g2_start = find_prev_grapheme_start(data, len, g2_end);
        g1_end = g2_start;
        g1_start = find_prev_grapheme_start(data, len, g1_end);
    } else if (cursor == 0) {
        /// At beginning: nothing before to transpose
        return LLE_SUCCESS;
    } else {
        /// Normal case: swap grapheme before cursor with grapheme at cursor
        g1_end = cursor;
        g1_start = find_prev_grapheme_start(data, len, g1_end);
        g2_start = cursor;
        g2_end = find_next_grapheme_end(data, len, g2_start);
    }

    /// Validate boundaries
    if (g1_start >= g1_end || g2_start >= g2_end || g1_end != g2_start) {
        return LLE_SUCCESS; /// Invalid state, no-op
    }

    size_t g1_len = g1_end - g1_start;
    size_t g2_len = g2_end - g2_start;

    /// Allocate temp buffers for the two graphemes
    char *g1_copy = malloc(g1_len);
    char *g2_copy = malloc(g2_len);
    if (!g1_copy || !g2_copy) {
        free(g1_copy);
        free(g2_copy);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "keybinding",
                         "keybinding action allocation failed");
    }

    /// Copy graphemes
    memcpy(g1_copy, data + g1_start, g1_len);
    memcpy(g2_copy, data + g2_start, g2_len);

    /// Delete both graphemes (from end to preserve positions)
    lle_buffer_delete_text(editor->buffer, g2_start, g2_len);
    lle_buffer_delete_text(editor->buffer, g1_start, g1_len);

    /// Insert in swapped order: g2 then g1
    lle_buffer_insert_text(editor->buffer, g1_start, g2_copy, g2_len);
    lle_buffer_insert_text(editor->buffer, g1_start + g2_len, g1_copy, g1_len);

    free(g1_copy);
    free(g2_copy);

    /// Move cursor to end of the swapped region
    size_t new_cursor = g1_start + g1_len + g2_len;
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, new_cursor);
    lle_cursor_manager_get_position(editor->cursor_manager,
                                    &editor->buffer->cursor);

    return LLE_SUCCESS;
}

/**
 * @brief Transpose words at cursor (M-t)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_transpose_words(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor = editor->buffer->cursor.byte_offset;
    const char *data = editor->buffer->data;
    size_t len = editor->buffer->length;

    if (len == 0) {
        return LLE_SUCCESS;
    }

    /// Find word2: the word at or after cursor using grapheme-aware boundaries
    size_t word2_start = cursor;
    size_t word2_end;

    /// If cursor is in the middle of a word, find its boundaries
    /// First, check if we're inside a word by looking at current position
    uint32_t cp_at_cursor = decode_codepoint_at(data, len, cursor);

    if (cursor < len && !is_whitespace_codepoint(cp_at_cursor) &&
        !is_shell_metachar(cp_at_cursor)) {
        /// We're inside a word - find its start
        word2_start = find_word_start(data, cursor);
        /// find_word_start returns position at start of word, but we need to
        /// check if we need to look for next word instead
    }

    /// Find end of word2
    word2_end = find_word_end(data, len, word2_start);

    /// If word2 is empty (e.g., cursor was in whitespace), find next word
    if (word2_start >= word2_end) {
        word2_start = find_word_end(data, len, cursor); /// Skip whitespace
        if (word2_start >= len) {
            return LLE_SUCCESS; /// No word at or after cursor
        }
        word2_end = find_word_end(data, len, word2_start);
    }

    /// Find word1: the word before word2
    size_t word1_end = word2_start;

    /// Skip whitespace/punctuation backward to find end of word1
    while (word1_end > 0) {
        size_t prev = find_prev_grapheme_start(data, len, word1_end);
        uint32_t cp = decode_codepoint_at(data, len, prev);
        if (!is_whitespace_codepoint(cp) && !is_shell_metachar(cp)) {
            /// Found end of word1
            break;
        }
        word1_end = prev;
    }

    if (word1_end == 0) {
        return LLE_SUCCESS; /// No previous word
    }

    /// Find start of word1
    size_t word1_start = find_word_start(data, word1_end);

    /// Validate we have two distinct words
    if (word1_start >= word1_end || word2_start >= word2_end ||
        word1_end > word2_start) {
        return LLE_SUCCESS; /// Invalid word boundaries
    }

    /// Extract words
    size_t word1_len = word1_end - word1_start;
    size_t word2_len = word2_end - word2_start;

    char *word1 = strndup(data + word1_start, word1_len);
    char *word2 = strndup(data + word2_start, word2_len);

    if (!word1 || !word2) {
        free(word1);
        free(word2);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "keybinding",
                         "keybinding action allocation failed");
    }

    /// Delete word2 first (higher position), then word1
    lle_buffer_delete_text(editor->buffer, word2_start, word2_len);
    lle_buffer_delete_text(editor->buffer, word1_start, word1_len);

    /// Calculate separator between the original words
    size_t sep_len = word2_start - word1_end;
    char *separator = NULL;
    if (sep_len > 0) {
        separator = strndup(data + word1_end, sep_len);
    }

    /// Insert in swapped order: word2, separator, word1
    lle_buffer_insert_text(editor->buffer, word1_start, word2, word2_len);
    if (separator) {
        lle_buffer_insert_text(editor->buffer, word1_start + word2_len,
                               separator, sep_len);
        free(separator);
    }
    lle_buffer_insert_text(editor->buffer, word1_start + word2_len + sep_len,
                           word1, word1_len);

    free(word1);
    free(word2);

    /// Move cursor to end of swapped region
    size_t new_cursor = word1_start + word1_len + word2_len + sep_len;
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, new_cursor);
    lle_cursor_manager_get_position(editor->cursor_manager,
                                    &editor->buffer->cursor);

    return LLE_SUCCESS;
}

/* ============================================================================
 * EDITING ACTIONS - CASE CHANGES
 * ============================================================================
 */

/**
 * @brief Convert a codepoint to uppercase and encode back to UTF-8
 * @param cp Unicode codepoint to convert
 * @param out Output buffer for UTF-8 bytes
 * @param max_len Maximum bytes to write
 * @return Number of bytes written, or 0 on error
 */
static size_t codepoint_to_upper_utf8(uint32_t cp, char *out, size_t max_len) {
    (void)max_len; /// Output buffer assumed sufficient for single codepoint
    wint_t upper = towupper((wint_t)cp);
    return (size_t)lle_utf8_encode_codepoint((uint32_t)upper, out);
}

/**
 * @brief Convert a codepoint to lowercase and encode back to UTF-8
 * @param cp Unicode codepoint to convert
 * @param out Output buffer for UTF-8 bytes
 * @param max_len Maximum bytes to write
 * @return Number of bytes written, or 0 on error
 */
static size_t codepoint_to_lower_utf8(uint32_t cp, char *out, size_t max_len) {
    (void)max_len; /// Output buffer assumed sufficient for single codepoint
    wint_t lower = towlower((wint_t)cp);
    return (size_t)lle_utf8_encode_codepoint((uint32_t)lower, out);
}

/**
 * @brief Apply case transformation to a word region
 * @param editor Editor instance
 * @param word_start Byte offset of word start
 * @param word_end Byte offset of word end
 * @param mode 0 = uppercase all, 1 = lowercase all, 2 = capitalize
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t transform_word_case(lle_editor_t *editor, size_t word_start,
                                        size_t word_end, int mode) {
    if (!editor || !editor->buffer || word_start >= word_end) {
        return LLE_SUCCESS;
    }

    const char *data = editor->buffer->data;
    size_t len = editor->buffer->length;

    /// Build new word with transformed case
    size_t word_len = word_end - word_start;
    /// Allocate generous buffer - case conversion can change byte length
    size_t max_new_len = word_len * 4; /// UTF-8 max 4 bytes per codepoint
    char *new_word = malloc(max_new_len + 1);
    if (!new_word) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "keybinding",
                         "keybinding action allocation failed");
    }

    size_t new_pos = 0;
    size_t pos = word_start;
    bool first_alpha = true; /// For capitalize mode

    while (pos < word_end && pos < len) {
        /// Decode codepoint
        uint32_t cp;
        int decoded = lle_utf8_decode_codepoint(data + pos, len - pos, &cp);
        if (decoded <= 0) {
            /// Invalid UTF-8, copy byte as-is
            if (new_pos < max_new_len) {
                new_word[new_pos++] = data[pos];
            }
            pos++;
            continue;
        }

        /// Transform case based on mode
        char transformed[4];
        size_t transformed_len = 0;

        if (mode == 0) {
            /// Uppercase all
            transformed_len =
                codepoint_to_upper_utf8(cp, transformed, sizeof(transformed));
        } else if (mode == 1) {
            /// Lowercase all
            transformed_len =
                codepoint_to_lower_utf8(cp, transformed, sizeof(transformed));
        } else if (mode == 2) {
            /// Capitalize: first alphabetic char uppercase, rest lowercase
            if (first_alpha && iswalpha((wint_t)cp)) {
                transformed_len = codepoint_to_upper_utf8(cp, transformed,
                                                          sizeof(transformed));
                first_alpha = false;
            } else {
                transformed_len = codepoint_to_lower_utf8(cp, transformed,
                                                          sizeof(transformed));
            }
        }

        if (transformed_len == 0) {
            /// Fallback: copy original bytes
            if (new_pos + (size_t)decoded <= max_new_len) {
                memcpy(new_word + new_pos, data + pos, (size_t)decoded);
                new_pos += (size_t)decoded;
            }
        } else if (new_pos + transformed_len <= max_new_len) {
            memcpy(new_word + new_pos, transformed, transformed_len);
            new_pos += transformed_len;
        }

        pos += (size_t)decoded;
    }

    new_word[new_pos] = '\0';

    /// Replace word in buffer
    lle_buffer_delete_text(editor->buffer, word_start, word_len);
    lle_buffer_insert_text(editor->buffer, word_start, new_word, new_pos);

    free(new_word);

    /// Move cursor past transformed word
    size_t new_cursor = word_start + new_pos;
    if (editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                               new_cursor);
        lle_cursor_manager_get_position(editor->cursor_manager,
                                        &editor->buffer->cursor);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Convert word at cursor to uppercase (M-u)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_upcase_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor = editor->buffer->cursor.byte_offset;
    size_t len = editor->buffer->length;

    /// Find word boundaries using grapheme-aware functions
    size_t word_start = cursor;

    /// Skip whitespace forward
    while (word_start < len) {
        uint32_t cp =
            decode_codepoint_at(editor->buffer->data, len, word_start);
        if (!is_whitespace_codepoint(cp)) {
            break;
        }
        word_start =
            find_next_grapheme_end(editor->buffer->data, len, word_start);
    }

    /// Find end of word
    size_t word_end = find_word_end(editor->buffer->data, len, word_start);

    if (word_start >= word_end) {
        return LLE_SUCCESS; /// No word found
    }

    return transform_word_case(editor, word_start, word_end,
                               0); /// 0 = uppercase
}

/**
 * @brief Convert word at cursor to lowercase (M-l)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_downcase_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor = editor->buffer->cursor.byte_offset;
    size_t len = editor->buffer->length;

    /// Find word boundaries using grapheme-aware functions
    size_t word_start = cursor;

    /// Skip whitespace forward
    while (word_start < len) {
        uint32_t cp =
            decode_codepoint_at(editor->buffer->data, len, word_start);
        if (!is_whitespace_codepoint(cp)) {
            break;
        }
        word_start =
            find_next_grapheme_end(editor->buffer->data, len, word_start);
    }

    /// Find end of word
    size_t word_end = find_word_end(editor->buffer->data, len, word_start);

    if (word_start >= word_end) {
        return LLE_SUCCESS; /// No word found
    }

    return transform_word_case(editor, word_start, word_end,
                               1); /// 1 = lowercase
}

/**
 * @brief Capitalize word at cursor (M-c)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_capitalize_word(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor = editor->buffer->cursor.byte_offset;
    size_t len = editor->buffer->length;

    /// Find word boundaries using grapheme-aware functions
    size_t word_start = cursor;

    /// Skip whitespace forward
    while (word_start < len) {
        uint32_t cp =
            decode_codepoint_at(editor->buffer->data, len, word_start);
        if (!is_whitespace_codepoint(cp)) {
            break;
        }
        word_start =
            find_next_grapheme_end(editor->buffer->data, len, word_start);
    }

    /// Find end of word
    size_t word_end = find_word_end(editor->buffer->data, len, word_start);

    if (word_start >= word_end) {
        return LLE_SUCCESS; /// No word found
    }

    return transform_word_case(editor, word_start, word_end,
                               2); /// 2 = capitalize
}

/* ============================================================================
 * HISTORY NAVIGATION
 * ============================================================================
 */

/**
 * @brief Get current buffer content as null-terminated string
 * @param editor Editor instance
 * @return Buffer content or NULL if empty. Caller must NOT free.
 */
static const char *get_current_buffer_content(lle_editor_t *editor) {
    if (!editor || !editor->buffer || editor->buffer->length == 0) {
        return NULL;
    }
    return editor->buffer->data;
}

/**
 * @brief Compare two strings for navigation-time deduplication
 * @param s1 First string
 * @param s2 Second string
 * @return true if strings are equal (with optional Unicode normalization)
 */
static bool history_nav_strings_equal(const char *s1, const char *s2) {
    if (!s1 || !s2)
        return false;

    if (config.lle_dedup_unicode_normalize) {
        /// Use Unicode-aware comparison with NFC normalization
        return lle_unicode_strings_equal(s1, s2, &LLE_UNICODE_COMPARE_DEFAULT);
    } else {
        /// Fast byte-level comparison
        return strcmp(s1, s2) == 0;
    }
}

/**
 * @brief Simple FNV-1a hash for command strings
 * @param cmd Command string to hash
 * @return 32-bit hash value
 */
static uint32_t hash_command_string(const char *cmd) {
    if (!cmd)
        return 0;

    uint32_t hash = 2166136261u; /// FNV offset basis
    while (*cmd) {
        hash ^= (uint8_t)*cmd++;
        hash *= 16777619u; /// FNV prime
    }
    return hash;
}

/* ============================================================================
 * HISTORY NAVIGATION SESSION
 *
 * One cursor over a fixed, ordered, deduped candidate list of history indices
 * (newest first), built when navigation starts. Replaces the former
 * navigation-position + seen-hash-set + display-stack mechanisms: a single
 * cursor makes up and down symmetric by construction and behaves correctly
 * under mixed up/down sequences.
 * ============================================================================
 */

/**
 * @brief End the navigation session and free its snapshot
 * @param editor Editor instance
 */
void lle_history_nav_session_end(lle_editor_t *editor) {
    if (!editor) {
        return;
    }
    free(editor->history_nav_original);
    editor->history_nav_original = NULL;
    free(editor->history_nav_candidates);
    editor->history_nav_candidates = NULL;
    editor->history_nav_count = 0;
    editor->history_nav_capacity = 0;
    editor->history_nav_cursor = -1;
    editor->history_nav_active = false;
}

/**
 * @brief Append a history index to the session's candidate list
 * @return true on success, false on allocation failure
 */
static bool nav_candidates_append(lle_editor_t *editor, size_t idx) {
    if (editor->history_nav_count >= editor->history_nav_capacity) {
        size_t cap = editor->history_nav_capacity
                         ? editor->history_nav_capacity * 2
                         : 64;
        size_t *grown =
            realloc(editor->history_nav_candidates, cap * sizeof(size_t));
        if (!grown) {
            return false;
        }
        editor->history_nav_candidates = grown;
        editor->history_nav_capacity = cap;
    }
    editor->history_nav_candidates[editor->history_nav_count++] = idx;
    return true;
}

/**
 * @brief Build the candidate list (newest first) for a navigation session
 *
 * Includes active entries only. Honors the existing navigation-dedup configs:
 * lle_dedup_navigation skips entries equal to the line being edited, and
 * lle_dedup_navigation_unique shows each distinct command at most once. (A
 * later change adds the history.search_mode prefix filter here.)
 *
 * @param editor Editor instance
 * @param original The line being edited when navigation started (may be NULL)
 */
static void nav_build_candidates(lle_editor_t *editor, const char *original) {
    editor->history_nav_count = 0;
    size_t entry_count = 0;
    if (lle_history_get_entry_count(editor->history_system, &entry_count) !=
            LLE_SUCCESS ||
        entry_count == 0) {
        return;
    }

    bool skip_current = config.lle_dedup_navigation;
    bool unique_only = config.lle_dedup_navigation_unique;

    /// Prefix search mode: keep only entries beginning with the text typed
    /// before the cursor when navigation started (history.search_mode). An
    /// empty prefix matches everything, so an up-arrow on a blank line browses
    /// all history -- the same as plain mode.
    bool prefix_mode =
        (config.history_search_mode == HISTORY_SEARCH_MODE_PREFIX);
    size_t prefix_len = editor->history_nav_prefix_len;

    /// Local seen-hash set for unique-only dedup (freed before returning).
    uint32_t *seen = NULL;
    size_t seen_count = 0;
    size_t seen_cap = 0;

    for (size_t i = entry_count; i > 0; i--) {
        size_t idx = i - 1;
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(editor->history_system, idx,
                                           &entry) != LLE_SUCCESS ||
            !entry || !entry->command) {
            continue;
        }
        if (entry->state != LLE_HISTORY_STATE_ACTIVE) {
            continue;
        }
        if (prefix_mode && prefix_len > 0 && original &&
            !lle_unicode_is_prefix(original, prefix_len, entry->command,
                                   strlen(entry->command), NULL)) {
            continue; /// Doesn't match the typed prefix
        }
        if (skip_current && original &&
            history_nav_strings_equal(entry->command, original)) {
            continue; /// Don't re-offer the line already being edited
        }
        if (unique_only) {
            uint32_t h = hash_command_string(entry->command);
            bool dup = false;
            for (size_t s = 0; s < seen_count; s++) {
                if (seen[s] == h) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            if (seen_count >= seen_cap) {
                size_t nc = seen_cap ? seen_cap * 2 : 64;
                uint32_t *g = realloc(seen, nc * sizeof(uint32_t));
                if (g) {
                    seen = g;
                    seen_cap = nc;
                }
            }
            if (seen_count < seen_cap) {
                seen[seen_count++] = h;
            }
        }
        if (!nav_candidates_append(editor, idx)) {
            break;
        }
    }

    free(seen);
}

/**
 * @brief Load the entry at the session cursor into the buffer
 *
 * Cursor -1 restores the original line; 0..count-1 loads that candidate. In
 * prefix search mode the buffer cursor is kept at the typed prefix boundary
 * (zsh history-beginning-search style, Option A) so the user can immediately
 * edit the rest of the recalled command; otherwise it sits at end of line.
 */
static void nav_show_cursor(lle_editor_t *editor) {
    lle_buffer_clear(editor->buffer);

    const char *text = NULL;
    if (editor->history_nav_cursor < 0) {
        text = editor->history_nav_original;
    } else {
        lle_history_entry_t *entry = NULL;
        size_t idx = editor->history_nav_candidates[editor->history_nav_cursor];
        if (lle_history_get_entry_by_index(editor->history_system, idx,
                                           &entry) == LLE_SUCCESS &&
            entry && entry->command) {
            text = entry->command;
        }
    }

    if (text && *text) {
        lle_buffer_insert_text(editor->buffer, 0, text, strlen(text));
    }

    /// End of line, except in prefix search mode keep the cursor at the typed
    /// prefix boundary so editing resumes where the user left off.
    size_t target = editor->buffer->length;
    if (config.history_search_mode == HISTORY_SEARCH_MODE_PREFIX &&
        editor->history_nav_prefix_len > 0 &&
        editor->history_nav_prefix_len <= editor->buffer->length) {
        target = editor->history_nav_prefix_len;
    }

    if (editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, target);
        editor->buffer->cursor = editor->cursor_manager->position;
    } else {
        editor->buffer->cursor.byte_offset = target;
        editor->buffer->cursor.codepoint_index = target;
        editor->buffer->cursor.grapheme_index = target;
    }
}

/**
 * @brief Navigate to previous history entry (C-p, UP in single-line mode)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_previous(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Start a session on the first up: snapshot the current line (restored
    /// when navigation returns to the bottom) and build the candidate list.
    if (!editor->history_nav_active) {
        const char *cur = get_current_buffer_content(editor);
        free(editor->history_nav_original);
        editor->history_nav_original = strdup(cur ? cur : "");
        /// The text before the cursor is the prefix filter (and, in prefix
        /// mode, where the cursor is kept on each recalled candidate).
        editor->history_nav_prefix_len = editor->buffer->cursor.byte_offset;
        nav_build_candidates(editor, editor->history_nav_original);
        editor->history_nav_cursor = -1;
        editor->history_nav_active = true;
    }

    /// Move toward older entries; stop at the oldest.
    if ((size_t)(editor->history_nav_cursor + 1) < editor->history_nav_count) {
        editor->history_nav_cursor++;
        nav_show_cursor(editor);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Navigate to next history entry (C-n, DOWN in single-line mode)
 *
 * Moves the session cursor toward newer entries, exactly retracing the up
 * path. At the bottom the original line is restored; the session stays active
 * so a following up resumes without a rebuild.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_next(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Not navigating, or already at the original line: nothing to do.
    if (!editor->history_nav_active || editor->history_nav_cursor < 0) {
        return LLE_SUCCESS;
    }

    editor
        ->history_nav_cursor--; /// Toward newer; -1 restores the original line
    nav_show_cursor(editor);

    return LLE_SUCCESS;
}

/**
 * @brief Start incremental reverse history search (C-r)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reverse_search_history(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Start interactive reverse search
    editor->history_search_active = true;
    editor->history_search_direction = -1; /// Reverse

    return LLE_SUCCESS;
}

/**
 * @brief Start incremental forward history search (C-s)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_forward_search_history(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Start interactive forward search
    editor->history_search_active = true;
    editor->history_search_direction = 1; /// Forward

    return LLE_SUCCESS;
}

/**
 * @brief Search history backward for prefix match (M-p)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_search_backward(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Search backward for command starting with current buffer content
    const char *search_prefix =
        editor->buffer->data ? editor->buffer->data : "";

    if (strlen(search_prefix) == 0) {
        return LLE_SUCCESS; /// Nothing to search for
    }

    /// Use prefix search
    lle_history_search_results_t *results = lle_history_search_prefix(
        editor->history_system, search_prefix, 10 /// max results
    );

    if (results && lle_history_search_results_get_count(results) > 0) {
        const lle_search_result_t *result =
            lle_history_search_results_get(results, 0);
        if (result && result->command) {
            /// Replace buffer with found entry
            lle_buffer_clear(editor->buffer);
            lle_buffer_insert_text(editor->buffer, 0, result->command,
                                   strlen(result->command));
            editor->buffer->cursor.byte_offset = editor->buffer->length;
            editor->buffer->cursor.codepoint_index = editor->buffer->length;
            editor->buffer->cursor.grapheme_index = editor->buffer->length;
        }
    }

    if (results) {
        lle_history_search_results_destroy(results);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Search history forward for prefix match (M-n)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_search_forward(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->history_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Search forward for command starting with current buffer content
    /// Note: This is essentially the same as backward but would track position
    return lle_history_search_backward(editor);
}

/* ============================================================================
 * COMPLETION ACTIONS
 * ============================================================================
 */

/**
 * @brief Trigger tab completion (TAB)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_complete(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!editor->completion_system) {
        return LLE_SUCCESS; /// No completion system available
    }

    /// If completion is already active, cycle to next item
    /// This is standard shell behavior: TAB cycles through completions
    bool is_active = lle_completion_system_is_active(editor->completion_system);
    bool is_menu_visible =
        lle_completion_system_is_menu_visible(editor->completion_system);

    if (is_active && is_menu_visible) {
        /// Completion is active WITH a visible menu - cycle through items
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (menu) {
            /// Move to next item sequentially (across columns, then next row)
            lle_completion_menu_move_next(menu);

            /// Update command line with newly selected completion (inline
            /// update)
            lle_completion_state_t *state =
                lle_completion_system_get_state(editor->completion_system);

            if (state) {
                update_inline_completion(editor, menu, state);
            }

            /// Menu selection changed, trigger refresh
            display_controller_t *dc = display_integration_get_controller();
            if (dc) {
                dc->menu_state_changed = true;
            }
            return LLE_SUCCESS;
        }
        /// Menu is NULL despite is_menu_visible - fall through to regenerate
    }

    /// If is_active but no menu, clear stale state before regenerating
    if (is_active && !is_menu_visible) {
        lle_completion_system_clear(editor->completion_system);
    }

    /// Generate completions for current cursor position
    lle_cursor_position_t cursor_info;
    lle_cursor_manager_get_position(editor->cursor_manager, &cursor_info);
    size_t cursor_pos = cursor_info.byte_offset;
    const char *buffer = editor->buffer->data;

    lle_completion_result_t *result = NULL;

    /// Use Spec 12 generation (PROPER - with deduplication)
    lle_result_t gen_result = lle_completion_system_generate(
        editor->completion_system, buffer, cursor_pos, &result);

    if (gen_result != LLE_SUCCESS || !result) {
        return LLE_SUCCESS; /// No completions - not an error
    }

    /// If no items, clean up and return.
    /// NOTE: result is owned by completion_system->current_state, so we call
    /// lle_completion_system_clear() to properly free it - NOT result_free.
    if (result->count == 0) {
        lle_completion_system_clear(editor->completion_system);
        return LLE_SUCCESS;
    }

    /// Single match: splice the candidate into the buffer with the
    /// accept-phase suffix (close-quote + trailing space for files,
    /// "/" for directories). Multi-match: open the menu and let the
    /// preview path render the first candidate.
    if (result->count == 1) {
        const lle_completion_item_t *item = &result->items[0];

        /// External commands shadowing a builtin/alias are spliced as
        /// their full PATH-resolved path so the user disambiguates
        /// away from the builtin.
        lle_completion_item_t synthetic_item = *item;
        if (item->type == LLE_COMPLETION_TYPE_COMMAND &&
            item->description != NULL) {
            synthetic_item.text = (char *)item->description;
        }

        /// Re-analyze the buffer to obtain the splice context. The
        /// completion_system already analyzed once during generate,
        /// but the analyzer is cheap and re-running keeps the apply
        /// path self-contained.
        lle_word_context_t *splice_context = NULL;
        lle_result_t ctx_result = lle_word_context_analyze(
            buffer, cursor_pos, editor->lle_pool, &splice_context);
        if (ctx_result != LLE_SUCCESS || !splice_context) {
            lle_completion_system_clear(editor->completion_system);
            return LLE_SUCCESS;
        }

        lle_result_t replace_result = lle_splicer_apply_accept(
            editor->buffer, editor->cursor_manager, splice_context,
            &synthetic_item, editor->lle_pool);
        lle_word_context_free(splice_context);

        /// Directory-chain (issue #85): if the accepted item was a
        /// directory and completion.chain_directories is on, recurse
        /// into lle_complete() instead of clearing. This re-triggers
        /// completion at the new cursor position (after the inserted
        /// "/") so the next-level menu opens immediately, fish-style.
        /// The buffer + cursor are already updated by the splice, so
        /// re-entry is clean.
        bool chain = false;
        (void)config_registry_get_boolean("completion.chain_directories",
                                          &chain);
        if (replace_result == LLE_SUCCESS && chain &&
            synthetic_item.type == LLE_COMPLETION_TYPE_DIRECTORY) {
            lle_completion_system_clear(editor->completion_system);
            return lle_complete(editor);
        }

        /// Single-match path consumed the active completion state;
        /// clear it so the next TAB starts a fresh session.
        lle_completion_system_clear(editor->completion_system);

        display_controller_t *dc = display_integration_get_controller();
        if (dc)
            refresh_after_completion(dc);

        return replace_result;
    }

    /// Multiple completions: open the menu. The completion system
    /// already stored the state during generate, so we just preview
    /// the first candidate inline and hand the menu to the display
    /// controller.
    lle_completion_menu_state_t *menu =
        lle_completion_system_get_menu(editor->completion_system);
    if (!menu) {
        lle_completion_system_clear(editor->completion_system);
        return LLE_SUCCESS;
    }

    display_controller_t *dc = display_integration_get_controller();
    if (dc) {
        lle_completion_state_t *state =
            lle_completion_system_get_state(editor->completion_system);
        if (state) {
            update_inline_completion(editor, menu, state);
        }
        display_controller_set_completion_menu(dc, menu);
    }
    return LLE_SUCCESS;
}

/**
 * @brief Show all possible completions (M-?)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_possible_completions(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Completion system requires Spec 12 implementation
    return LLE_SUCCESS;
}

/**
 * @brief Insert all possible completions (M-*)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_insert_completions(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Completion system requires Spec 12 implementation
    return LLE_SUCCESS;
}

/* ============================================================================
 * SHELL-SPECIFIC OPERATIONS
 * ============================================================================
 */

/**
 * @brief Accept the current line for execution (ENTER, RET)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_accept_line(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// This is the simple keybinding action for line acceptance. The
    /// full menu-aware ENTER handling -- including splicer-driven
    /// finalization of a cycled completion candidate (close-quote,
    /// trailing space, "/" for directories) -- lives in
    /// lle_accept_line_context (src/lle/lle_readline.c), which
    /// overrides this action for the ENTER binding via
    /// lle_keybinding_manager_bind_context. lle_accept_line itself is
    /// a fallback for any binding path that does not have access to
    /// the readline_context_t and so cannot run the splicer. In that
    /// fallback path the most we can do for an active menu is clear
    /// it; the buffer content (possibly a preview from cycling) is
    /// left as-is.
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        clear_completion_menu(editor);
    }

    /// Signal that line is accepted (caller handles execution)
    return LLE_SUCCESS;
}

/**
 * @brief Abort the current line (C-g, ESC)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_abort_line(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If completion menu is active, just cancel it without aborting
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        clear_completion_menu(editor);
        return LLE_SUCCESS;
    }

    /// Signal abort to readline loop
    editor->abort_requested = true;

    return LLE_SUCCESS;
}

/**
 * @brief Send EOF signal (C-d on empty line)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_send_eof(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Signal EOF to readline loop
    editor->eof_requested = true;
    return LLE_SUCCESS;
}

/**
 * @brief Send interrupt signal (C-c)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_interrupt(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Send SIGINT to self
    raise(SIGINT);

    return LLE_SUCCESS;
}

/**
 * @brief Suspend shell (C-z)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_suspend(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Send SIGTSTP to self
    raise(SIGTSTP);

    return LLE_SUCCESS;
}

/**
 * @brief Clear screen and redraw prompt (C-l)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_clear_screen(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Get the global display integration instance
    lle_display_integration_t *display_integration =
        lle_display_integration_get_global();
    if (!display_integration || !display_integration->lush_display) {
        /// Fallback: use ANSI escape sequence if display controller not
        /// available
        printf("\033[H\033[2J");
        fflush(stdout);
        return LLE_SUCCESS;
    }

    /// Clear screen through display controller
    display_controller_error_t result =
        display_controller_clear_screen(display_integration->lush_display);
    if (result != DISPLAY_CONTROLLER_SUCCESS) {
        return LLE_ERROR_DISPLAY_INTEGRATION;
    }

    /// CRITICAL: Reset display state so refresh_display knows to redraw
    /// everything
    /// After clearing the physical screen, the display system's internal state
    /// (screen buffers) is out of sync. dc_reset_prompt_display_state() clears
    /// the screen buffer state so the next refresh_display() will render
    /// everything from scratch.
    dc_reset_prompt_display_state();

    /// Note: refresh_display() will be called by execute_keybinding_action
    /// after this returns
    return LLE_SUCCESS;
}

/**
 * Insert literal newline regardless of completion status
 *
 * This action inserts a newline at the cursor position without checking
 * whether the input is complete. Useful for editing complete multiline
 * commands when user wants to add more lines in the middle.
 *
 * Bound to: Shift-Enter, Alt-Enter
 *
 * @param editor LLE editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_insert_newline_literal(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Insert newline at cursor position
    lle_result_t result = lle_buffer_insert_text(
        editor->buffer, editor->buffer->cursor.byte_offset, "\n", 1);

    /// Synchronize cursor fields after insert
    if (result == LLE_SUCCESS && editor->cursor_manager) {
        lle_cursor_manager_move_to_byte_offset(
            editor->cursor_manager, editor->buffer->cursor.byte_offset);
    }

    return result;
}

/* ============================================================================
 * UTILITY ACTIONS
 * ============================================================================
 */

/**
 * @brief Enable quoted insert mode for next character (C-q, C-v)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_quoted_insert(lle_editor_t *editor) {
    if (!editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Set flag for next character to be inserted literally
    editor->quoted_insert_mode = true;

    return LLE_SUCCESS;
}

/**
 * @brief Discard entire line (C-u Unix style)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_line_discard(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Kill entire line (bash Ctrl-U behavior)
    size_t cursor_pos = editor->buffer->cursor.byte_offset;

    if (cursor_pos > 0) {
        char *killed_text = strndup(editor->buffer->data, cursor_pos);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete from beginning to cursor
        lle_result_t result =
            lle_buffer_delete_text(editor->buffer, 0, cursor_pos);
        if (result == LLE_SUCCESS) {
            /// CRITICAL: Sync cursor_manager after cursor is moved to position
            /// 0
            if (editor->cursor_manager) {
                lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                                       0);
            }
        }
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Delete word backward Unix style (C-w)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_word_rubout(lle_editor_t *editor) {
    if (!editor || !editor->buffer || !editor->cursor_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (editor->buffer->cursor.byte_offset == 0) {
        return LLE_SUCCESS; /// Already at beginning
    }

    /// Sync cursor_manager with buffer cursor
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                           editor->buffer->cursor.byte_offset);

    const char *data = editor->buffer->data;
    size_t cursor_pos = editor->buffer->cursor.byte_offset;
    size_t word_start = cursor_pos;

    /// Move backward by graphemes, skipping trailing whitespace
    while (word_start > 0) {
        /// Move back one grapheme
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                               word_start);
        lle_result_t result =
            lle_cursor_manager_move_by_graphemes(editor->cursor_manager, -1);
        if (result != LLE_SUCCESS) {
            break;
        }

        lle_cursor_position_t pos;
        lle_cursor_manager_get_position(editor->cursor_manager, &pos);
        size_t prev_pos = pos.byte_offset;

        /// Check if this grapheme is whitespace (check first byte)
        if (!is_unix_word_boundary(data[prev_pos])) {
            word_start = prev_pos;
            break;
        }

        word_start = prev_pos;
    }

    /// Now find beginning of word
    while (word_start > 0) {
        /// Move back one grapheme
        lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                               word_start);
        lle_result_t result =
            lle_cursor_manager_move_by_graphemes(editor->cursor_manager, -1);
        if (result != LLE_SUCCESS) {
            break;
        }

        lle_cursor_position_t pos;
        lle_cursor_manager_get_position(editor->cursor_manager, &pos);
        size_t prev_pos = pos.byte_offset;

        /// If we hit whitespace, stop
        if (is_unix_word_boundary(data[prev_pos])) {
            break;
        }

        word_start = prev_pos;
    }

    if (cursor_pos > word_start) {
        size_t kill_len = cursor_pos - word_start;
        char *killed_text = strndup(data + word_start, kill_len);

        if (killed_text) {
            /// Add to kill ring
            if (editor->kill_ring) {
                lle_kill_ring_add(editor->kill_ring, killed_text, false);
            }
            free(killed_text);
        }

        /// Delete the text
        lle_result_t result =
            lle_buffer_delete_text(editor->buffer, word_start, kill_len);
        if (result == LLE_SUCCESS) {
            /// CRITICAL: Sync cursor_manager after deletion
            lle_cursor_manager_move_to_byte_offset(editor->cursor_manager,
                                                   word_start);
        }
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Delete whitespace around cursor (M-\)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_delete_horizontal_space(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t cursor = editor->buffer->cursor.byte_offset;
    const char *data = editor->buffer->data;
    size_t len = editor->buffer->length;

    /// Find start of whitespace
    size_t start = cursor;
    while (start > 0 && isspace((unsigned char)data[start - 1])) {
        start--;
    }

    /// Find end of whitespace
    size_t end = cursor;
    while (end < len && isspace((unsigned char)data[end])) {
        end++;
    }

    if (end > start) {
        /// Delete whitespace
        lle_result_t result =
            lle_buffer_delete_text(editor->buffer, start, end - start);
        if (result == LLE_SUCCESS) {
            editor->buffer->cursor.byte_offset = start;
            editor->buffer->cursor.codepoint_index = start;
            editor->buffer->cursor.grapheme_index = start;
        }
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Insert a character at cursor position
 * @param editor Editor instance
 * @param codepoint Unicode codepoint to insert
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_self_insert(lle_editor_t *editor, uint32_t codepoint) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// In-menu type-to-filter: when the completion menu is visible and
    /// the user types a printable character, treat it as a filter
    /// keystroke rather than a buffer insert. The narrowed menu's new
    /// top candidate is previewed in place of the prior preview, so
    /// the buffer continuously reflects the most-relevant completion
    /// for what has been typed so far. Non-printable input or a
    /// filter that admits zero candidates falls through to the
    /// legacy dismiss-on-char path; the character is then inserted
    /// into the buffer normally.
    if (editor->completion_system &&
        lle_completion_system_is_menu_visible(editor->completion_system)) {
        bool printable = codepoint >= 0x20 && codepoint != 0x7F;
        lle_completion_menu_state_t *menu =
            lle_completion_system_get_menu(editor->completion_system);
        if (printable && menu) {
            lle_result_t fres =
                lle_completion_menu_append_filter_char(menu, codepoint);
            if (fres == LLE_SUCCESS && menu->result &&
                menu->result->count > 0) {
                lle_completion_state_t *cstate =
                    lle_completion_system_get_state(editor->completion_system);
                if (cstate) {
                    update_inline_completion(editor, menu, cstate);
                }
                display_controller_t *dc = display_integration_get_controller();
                if (dc) {
                    dc->menu_state_changed = true;
                }
                return LLE_SUCCESS;
            }
            /// Filter admitted nothing or allocation failed -- drop
            /// to the normal dismiss path so the user retains
            /// progress and the typed character lands in the buffer.
        }
        clear_completion_menu(editor);
    }

    /// Convert codepoint to UTF-8 bytes via the canonical encoder.
    char utf8_bytes[4];
    int encoded = lle_utf8_encode_codepoint(codepoint, utf8_bytes);
    if (encoded <= 0) {
        /// Invalid codepoint (surrogate or out of range): nothing to insert.
        return LLE_ERROR_INVALID_PARAMETER;
    }
    size_t byte_count = (size_t)encoded;

    /// Insert at cursor
    return lle_buffer_insert_text(editor->buffer,
                                  editor->buffer->cursor.byte_offset,
                                  utf8_bytes, byte_count);
}

/**
 * @brief Insert newline character at cursor (C-j)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_newline(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Insert newline at cursor
    return lle_buffer_insert_text(editor->buffer,
                                  editor->buffer->cursor.byte_offset, "\n", 1);
}

/**
 * @brief Insert tab character (expanded to spaces) at cursor (M-TAB)
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_tab_insert(lle_editor_t *editor) {
    if (!editor || !editor->buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Expand tab to spaces based on visual column position.
    /// This ensures true character-by-character tracking in the display layer
    /// since each space is a single character with width 1, rather than a
    /// tab character that requires formula-based expansion during rendering.
    int tab_width = config.tab_width > 0 ? config.tab_width : 4;
    size_t visual_col = editor->buffer->cursor.visual_column;
    size_t spaces_to_insert = tab_width - (visual_col % tab_width);

    /// Create a string of spaces
    char spaces[16]; /// Max reasonable tab width
    if (spaces_to_insert > sizeof(spaces) - 1) {
        spaces_to_insert = sizeof(spaces) - 1;
    }
    memset(spaces, ' ', spaces_to_insert);
    spaces[spaces_to_insert] = '\0';

    return lle_buffer_insert_text(editor->buffer,
                                  editor->buffer->cursor.byte_offset, spaces,
                                  spaces_to_insert);
}

/* ============================================================================
 * PRESET MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Load Emacs keybinding preset for the editor
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_load_emacs_preset(lle_editor_t *editor) {
    if (!editor || !editor->keybinding_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Bind all Emacs-style keybindings
    /// This calls lle_keybinding_manager_bind() for each binding

    lle_keybinding_manager_t *mgr = editor->keybinding_manager;

    /// Movement - Character level
    lle_keybinding_manager_bind(mgr, "C-f", lle_forward_char, "forward-char");
    lle_keybinding_manager_bind(mgr, "C-b", lle_backward_char, "backward-char");
    lle_keybinding_manager_bind(mgr, "LEFT", lle_backward_char,
                                "backward-char");
    lle_keybinding_manager_bind(mgr, "RIGHT", lle_forward_char, "forward-char");

    /// Movement - Line level
    lle_keybinding_manager_bind(mgr, "C-a", lle_beginning_of_line,
                                "beginning-of-line");
    lle_keybinding_manager_bind(mgr, "C-e", lle_end_of_line, "end-of-line");
    lle_keybinding_manager_bind(mgr, "HOME", lle_beginning_of_line,
                                "beginning-of-line");
    lle_keybinding_manager_bind(mgr, "END", lle_end_of_line, "end-of-line");

    /// Movement - Word level
    lle_keybinding_manager_bind(mgr, "M-f", lle_forward_word, "forward-word");
    lle_keybinding_manager_bind(mgr, "M-b", lle_backward_word, "backward-word");

    /// Movement - Buffer level
    lle_keybinding_manager_bind(mgr, "M-<", lle_beginning_of_buffer,
                                "beginning-of-buffer");
    lle_keybinding_manager_bind(mgr, "M->", lle_end_of_buffer, "end-of-buffer");

    /// Editing
    lle_keybinding_manager_bind(mgr, "C-d", lle_delete_char, "delete-char");
    lle_keybinding_manager_bind(mgr, "DEL", lle_backward_delete_char,
                                "backward-delete-char");
    lle_keybinding_manager_bind(mgr, "C-k", lle_kill_line, "kill-line");
    lle_keybinding_manager_bind(mgr, "C-u", lle_backward_kill_line,
                                "backward-kill-line");
    lle_keybinding_manager_bind(mgr, "M-d", lle_kill_word, "kill-word");
    lle_keybinding_manager_bind(mgr, "M-DEL", lle_backward_kill_word,
                                "backward-kill-word");
    lle_keybinding_manager_bind(mgr, "C-w", lle_unix_word_rubout,
                                "unix-word-rubout");
    lle_keybinding_manager_bind(mgr, "C-y", lle_yank, "yank");
    lle_keybinding_manager_bind(mgr, "M-y", lle_yank_pop, "yank-pop");
    lle_keybinding_manager_bind(mgr, "C-t", lle_transpose_chars,
                                "transpose-chars");
    lle_keybinding_manager_bind(mgr, "M-t", lle_transpose_words,
                                "transpose-words");

    /// Case changes
    lle_keybinding_manager_bind(mgr, "M-u", lle_upcase_word, "upcase-word");
    lle_keybinding_manager_bind(mgr, "M-l", lle_downcase_word, "downcase-word");
    lle_keybinding_manager_bind(mgr, "M-c", lle_capitalize_word,
                                "capitalize-word");

    /// History - Always navigate history (Ctrl-P/N)
    lle_keybinding_manager_bind(mgr, "C-p", lle_history_previous,
                                "previous-history");
    lle_keybinding_manager_bind(mgr, "C-n", lle_history_next, "next-history");
    lle_keybinding_manager_bind(mgr, "C-r", lle_reverse_search_history,
                                "reverse-search-history");
    lle_keybinding_manager_bind(mgr, "C-s", lle_forward_search_history,
                                "forward-search-history");
    lle_keybinding_manager_bind(mgr, "M-p", lle_history_search_backward,
                                "history-search-backward");
    lle_keybinding_manager_bind(mgr, "M-n", lle_history_search_forward,
                                "history-search-forward");

    /// Navigation - Smart arrows (context-aware: buffer lines in multiline,
    /// history in single-line)
    lle_keybinding_manager_bind(mgr, "UP", lle_smart_up_arrow,
                                "smart-up-arrow");
    lle_keybinding_manager_bind(mgr, "DOWN", lle_smart_down_arrow,
                                "smart-down-arrow");

    /// Completion
    lle_keybinding_manager_bind(mgr, "TAB", lle_complete, "complete");
    lle_keybinding_manager_bind(mgr, "M-?", lle_possible_completions,
                                "possible-completions");
    lle_keybinding_manager_bind(mgr, "M-*", lle_insert_completions,
                                "insert-completions");

    /// Shell operations
    lle_keybinding_manager_bind(mgr, "RET", lle_accept_line, "accept-line");
    lle_keybinding_manager_bind(mgr, "C-g", lle_abort_line, "abort");
    lle_keybinding_manager_bind(mgr, "ESC", lle_abort_line, "abort");
    lle_keybinding_manager_bind(mgr, "C-l", lle_clear_screen, "clear-screen");
    lle_keybinding_manager_bind(mgr, "C-c", lle_interrupt, "interrupt");
    lle_keybinding_manager_bind(mgr, "C-z", lle_suspend, "suspend");

    /// Utilities
    lle_keybinding_manager_bind(mgr, "C-q", lle_quoted_insert, "quoted-insert");
    lle_keybinding_manager_bind(mgr, "C-v", lle_quoted_insert, "quoted-insert");
    lle_keybinding_manager_bind(mgr, "M-\\", lle_delete_horizontal_space,
                                "delete-horizontal-space");
    lle_keybinding_manager_bind(mgr, "C-j", lle_newline, "newline");
    lle_keybinding_manager_bind(mgr, "M-TAB", lle_tab_insert, "tab-insert");

    return LLE_SUCCESS;
}

/**
 * @brief Load Vi keybinding preset for the editor
 * @param editor Editor instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_keybinding_load_vi_preset(lle_editor_t *editor) {
    if (!editor || !editor->keybinding_manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Vi mode preset requires additional implementation
    return LLE_SUCCESS;
}
