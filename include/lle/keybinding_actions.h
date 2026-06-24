/**
 * @file keybinding_actions.h
 * @brief Default keybinding action functions for GNU Readline compatibility
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements all 42 GNU Readline compatible keybinding actions for
 * interactive line editing. Provides 100% compatibility with standard
 * Emacs-style keybindings used in bash, zsh, and other shells.
 *
 * Key Features:
 * - Complete GNU Readline function set
 * - Movement operations (beginning/end of line, word navigation)
 * - Editing operations (kill/yank, transpose, case changes)
 * - History navigation (previous/next, search)
 * - Completion operations (tab completion, listing)
 * - Shell-specific operations (EOF, suspend, clear screen)
 * - Multiline command support for all operations
 *
 * Performance:
 * - All actions complete within 100 microseconds
 * - Optimized for interactive use
 */

#ifndef LLE_KEYBINDING_ACTIONS_H
#define LLE_KEYBINDING_ACTIONS_H

#include "lle/error_handling.h"
#include "lle/lle_editor.h"

/* ============================================================================
 * MOVEMENT ACTIONS
 * ============================================================================
 */

/**
 * @brief Move cursor to beginning of line (Ctrl-A)
 *
 * For multiline: move to beginning of current logical line
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_beginning_of_line(lle_editor_t *editor);

/**
 * @brief Move cursor to end of line (Ctrl-E)
 *
 * For multiline: move to end of current logical line
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_end_of_line(lle_editor_t *editor);

/**
 * @brief Move cursor forward one character (Ctrl-F, RIGHT arrow)
 *
 * Handles multi-byte UTF-8 characters correctly
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_forward_char(lle_editor_t *editor);

/**
 * @brief Move cursor backward one character (Ctrl-B, LEFT arrow)
 *
 * Handles multi-byte UTF-8 characters correctly
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_backward_char(lle_editor_t *editor);

/**
 * @brief Move cursor forward one word (Meta-F)
 *
 * Word boundary: whitespace and shell metacharacters
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_forward_word(lle_editor_t *editor);

/**
 * @brief Move cursor backward one word (Meta-B)
 *
 * Word boundary: whitespace and shell metacharacters
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_backward_word(lle_editor_t *editor);

/* ============================================================================
 * EDITING ACTIONS - DELETION AND KILLING
 * ============================================================================
 */

/**
 * @brief Delete character at cursor (Ctrl-D)
 *
 * If buffer empty at end, sends EOF
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_delete_char(lle_editor_t *editor);

/**
 * @brief Delete character before cursor (DEL, Backspace)
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_backward_delete_char(lle_editor_t *editor);

/**
 * @brief Kill from cursor to end of line (Ctrl-K)
 *
 * For multiline: kill to end of current logical line
 * Text added to kill ring
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_line(lle_editor_t *editor);

/**
 * @brief Kill from beginning of line to cursor (Ctrl-U)
 *
 * For multiline: kill from beginning of current logical line
 * Text added to kill ring
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_backward_kill_line(lle_editor_t *editor);

/**
 * @brief Kill from cursor to end of current word (Meta-D)
 *
 * Text added to kill ring
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_word(lle_editor_t *editor);

/**
 * @brief Kill from cursor to beginning of current word (Meta-DEL, Ctrl-W)
 *
 * Text added to kill ring
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_backward_kill_word(lle_editor_t *editor);

/* ============================================================================
 * EDITING ACTIONS - YANK AND TRANSPOSE
 * ============================================================================
 */

/**
 * @brief Yank (paste) most recent kill (Ctrl-Y)
 *
 * Inserts text from kill ring at cursor
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_yank(lle_editor_t *editor);

/**
 * @brief Cycle through kill ring (Meta-Y)
 *
 * Must be preceded by yank or yank-pop
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_yank_pop(lle_editor_t *editor);

/**
 * @brief Transpose characters (Ctrl-T)
 *
 * Swaps character at cursor with previous character
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_transpose_chars(lle_editor_t *editor);

/**
 * @brief Transpose words (Meta-T)
 *
 * Swaps current word with previous word
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_transpose_words(lle_editor_t *editor);

/* ============================================================================
 * EDITING ACTIONS - CASE CHANGES
 * ============================================================================
 */

/**
 * @brief Convert word to uppercase (Meta-U)
 *
 * Uppercases from cursor to end of word, moves cursor past word
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_upcase_word(lle_editor_t *editor);

/**
 * @brief Convert word to lowercase (Meta-L)
 *
 * Lowercases from cursor to end of word, moves cursor past word
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_downcase_word(lle_editor_t *editor);

/**
 * @brief Capitalize word (Meta-C)
 *
 * Capitalizes first letter, lowercases rest, moves cursor past word
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_capitalize_word(lle_editor_t *editor);

/* ============================================================================
 * HISTORY NAVIGATION
 * ============================================================================
 */

/**
 * @brief Previous history entry (Ctrl-P, UP arrow)
 *
 * Loads previous command from history into buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_previous(lle_editor_t *editor);

/**
 * @brief Next history entry (Ctrl-N, DOWN arrow)
 *
 * Loads next command from history into buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_next(lle_editor_t *editor);

/**
 * @brief End the history navigation session and free its snapshot
 *
 * Called when the user edits the line, accepts it, or a new readline session
 * begins, so the next up-arrow rebuilds the candidate list from the current
 * line. Safe to call when no session is active.
 *
 * @param editor Editor instance
 */
void lle_history_nav_session_end(lle_editor_t *editor);

/**
 * @brief Reverse incremental search (Ctrl-R)
 *
 * Interactive search backward through history
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_reverse_search_history(lle_editor_t *editor);

/**
 * @brief Forward incremental search (Ctrl-S)
 *
 * Interactive search forward through history
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_forward_search_history(lle_editor_t *editor);

/**
 * @brief History search backward (Meta-P)
 *
 * Search backward for command starting with current buffer content
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_search_backward(lle_editor_t *editor);

/**
 * @brief History search forward (Meta-N)
 *
 * Search forward for command starting with current buffer content
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_search_forward(lle_editor_t *editor);

/* ============================================================================
 * LINE AND BUFFER NAVIGATION
 * ============================================================================
 */

/**
 * @brief Previous line (multiline buffer navigation)
 *
 * Moves cursor up one line while preserving column position via sticky_column.
 * Returns success with no-op if already on first line.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_previous_line(lle_editor_t *editor);

/**
 * @brief Result type for extended line navigation functions
 *
 * Contains both the operation result and a flag indicating if the
 * boundary was hit (first line for up, last line for down).
 */
typedef struct {
    lle_result_t result; ///< Operation result code
    bool hit_boundary;   ///< True if cursor was already at boundary
} lle_line_nav_result_t;

/**
 * @brief Previous line with boundary detection
 *
 * Extended version of lle_previous_line that reports when the cursor
 * was already on the first line (boundary hit). Used by smart arrow
 * functions to trigger notifications.
 *
 * @param editor Editor instance
 * @return Result with boundary detection flag
 */
lle_line_nav_result_t lle_previous_line_ex(lle_editor_t *editor);

/**
 * @brief Next line with boundary detection
 *
 * Extended version of lle_next_line that reports when the cursor
 * was already on the last line (boundary hit). Used by smart arrow
 * functions to trigger notifications.
 *
 * @param editor Editor instance
 * @return Result with boundary detection flag
 */
lle_line_nav_result_t lle_next_line_ex(lle_editor_t *editor);

/**
 * @brief Next line (multiline buffer navigation)
 *
 * Moves cursor down one line while preserving column position via
 * sticky_column. Returns success with no-op if already on last line.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_next_line(lle_editor_t *editor);

/**
 * @brief Beginning of buffer (Alt-<)
 *
 * Moves cursor to position 0 (start of entire buffer).
 * Clears sticky column.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_beginning_of_buffer(lle_editor_t *editor);

/**
 * @brief End of buffer (Alt->)
 *
 * Moves cursor to end of entire buffer.
 * Clears sticky column.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_end_of_buffer(lle_editor_t *editor);

/**
 * @brief Smart up arrow (context-aware navigation)
 *
 * Single-line mode: Navigate history backward (like Ctrl-P)
 * Multi-line mode: Navigate to previous line in buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_smart_up_arrow(lle_editor_t *editor);

/**
 * @brief Smart down arrow (context-aware navigation)
 *
 * Single-line mode: Navigate history forward (like Ctrl-N)
 * Multi-line mode: Navigate to next line in buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_smart_down_arrow(lle_editor_t *editor);

/**
 * @brief Smart up arrow with readline context (full context-aware)
 *
 * Context-aware action with full access to readline state. In multiline mode,
 * shows notification hint when hitting first line boundary, reminding users
 * to use Ctrl+P/Ctrl+N for history navigation.
 *
 * Behavior:
 * - Completion menu active: Move up in menu
 * - Multi-line mode: Navigate to previous line, show hint at boundary
 * - Single-line mode: Navigate command history (backward)
 *
 * @param ctx Readline context with full access
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_smart_up_arrow_context(struct readline_context *ctx);

/**
 * @brief Smart down arrow with readline context (full context-aware)
 *
 * Context-aware action with full access to readline state. In multiline mode,
 * shows notification hint when hitting last line boundary, reminding users
 * to use Ctrl+P/Ctrl+N for history navigation.
 *
 * Behavior:
 * - Completion menu active: Move down in menu
 * - Multi-line mode: Navigate to next line, show hint at boundary
 * - Single-line mode: Navigate command history (forward)
 *
 * @param ctx Readline context with full access
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_smart_down_arrow_context(struct readline_context *ctx);

/* ============================================================================
 * COMPLETION ACTIONS
 * ============================================================================
 */

/**
 * @brief Complete current word (TAB)
 *
 * Attempts to complete command, filename, or variable
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_complete(lle_editor_t *editor);

/**
 * @brief List possible completions (Meta-?)
 *
 * Shows all possible completions for current word
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_possible_completions(lle_editor_t *editor);

/**
 * @brief Insert all completions (Meta-*)
 *
 * Inserts all possible completions at cursor
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_insert_completions(lle_editor_t *editor);

/* ============================================================================
 * SHELL-SPECIFIC OPERATIONS
 * ============================================================================
 */

/**
 * @brief Accept line (Return, Ctrl-M, Ctrl-J)
 *
 * Accepts current line for execution.
 * For multiline: checks if command is complete before accepting.
 *
 * @note This is a legacy simple action that sets the line_accepted flag.
 *       New code should use lle_accept_line_context() for direct readline
 * integration.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_accept_line(lle_editor_t *editor);

/**
 * @brief Accept line with context (Return, Ctrl-M, Ctrl-J) - Context-Aware
 * Action
 *
 * Full readline-aware ENTER key handler with direct access to readline context.
 * Checks for incomplete input using continuation state, handles history
 * integration, and directly manages readline completion (done/final_line).
 *
 * Behavior:
 * - Incomplete input: Inserts newline, syncs cursor, refreshes display,
 * continues editing
 * - Complete input: Adds to history, sets done=true, returns final line
 *
 * This is a context-aware action that requires readline_context_t access.
 *
 * @param ctx Readline context with full access to buffer, history, completion
 * state
 * @return LLE_SUCCESS on successful handling
 */
lle_result_t lle_accept_line_context(struct readline_context *ctx);

/**
 * @brief Abort current operation (Ctrl-G)
 *
 * Clears buffer, cancels search/completion, resets to clean state.
 *
 * @note This is a legacy simple action. New code should use
 *       lle_abort_line_context() for direct readline integration.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_abort_line(lle_editor_t *editor);

/**
 * @brief Abort line with context (Ctrl-G) - Context-Aware Action
 *
 * Emacs-style abort that cancels current input and returns empty line to shell.
 * Directly manages readline completion state (done/final_line) without using
 * flags.
 *
 * This eliminates the abort_requested flag pattern which caused state
 * persistence bugs across readline sessions.
 *
 * @param ctx Readline context with full access to done/final_line
 * @return LLE_SUCCESS
 */
lle_result_t lle_abort_line_context(struct readline_context *ctx);

/**
 * @brief Send EOF (Ctrl-D on empty line)
 *
 * Signals end of input
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_send_eof(lle_editor_t *editor);

/**
 * @brief Interrupt (Ctrl-C)
 *
 * Sends SIGINT signal
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_interrupt(lle_editor_t *editor);

/**
 * @brief Suspend shell (Ctrl-Z)
 *
 * Sends SIGTSTP signal
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_suspend(lle_editor_t *editor);

/**
 * @brief Clear screen (Ctrl-L)
 *
 * Clears terminal screen and redraws prompt/buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_clear_screen(lle_editor_t *editor);

/* ============================================================================
 * UTILITY ACTIONS
 * ============================================================================
 */

/**
 * @brief Quoted insert (Ctrl-Q, Ctrl-V)
 *
 * Next character inserted literally (ignores keybindings)
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_quoted_insert(lle_editor_t *editor);

/**
 * @brief Unix line discard (Ctrl-U in bash mode)
 *
 * Kills entire line (alternative binding for backward-kill-line)
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_unix_line_discard(lle_editor_t *editor);

/**
 * @brief Unix word rubout (Ctrl-W)
 *
 * Kills word backward using Unix word boundaries (whitespace only)
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_unix_word_rubout(lle_editor_t *editor);

/**
 * @brief Delete horizontal space (Meta-\)
 *
 * Deletes all whitespace around cursor
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_delete_horizontal_space(lle_editor_t *editor);

/**
 * @brief Self-insert (default action for printable characters)
 *
 * Inserts the character into the buffer
 *
 * @param editor Editor instance
 * @param codepoint Unicode codepoint to insert
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_self_insert(lle_editor_t *editor, uint32_t codepoint);

/**
 * @brief Newline (Ctrl-J, in multiline editing)
 *
 * Inserts a newline character into the buffer
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_newline(lle_editor_t *editor);

/**
 * @brief Insert literal newline (Shift-Enter, Alt-Enter)
 *
 * Inserts a newline at cursor position regardless of completion status.
 * Useful for editing complete multiline commands when adding lines in the
 * middle.
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_insert_newline_literal(lle_editor_t *editor);

/**
 * @brief Tab insert (Meta-TAB)
 *
 * Inserts a literal tab character
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_tab_insert(lle_editor_t *editor);

/* ============================================================================
 * PRESET MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Load Emacs preset keybindings
 *
 * Binds all GNU Readline Emacs-style keybindings
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_keybinding_load_emacs_preset(lle_editor_t *editor);

/**
 * @brief Load Vi preset keybindings
 *
 * Binds all Vi-style keybindings (insert and command modes)
 *
 * @param editor Editor instance
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_keybinding_load_vi_preset(lle_editor_t *editor);

#endif /// LLE_KEYBINDING_ACTIONS_H
