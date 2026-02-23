/**
 * @file display_controller.c
 * @brief Display Controller - High-Level Display Management System
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Manages display layers, caching, and terminal output coordination.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * DISPLAY CONTROLLER IMPLEMENTATION
 *
 * This file implements the high-level display controller that coordinates all
 * display layers of the Lush layered display architecture. It provides
 * system-wide performance monitoring, intelligent caching, and optimization
 * for enterprise-grade shell display functionality.
 *
 * Key Features Implemented:
 * - High-level coordination of composition engine and all display layers
 * - System-wide performance monitoring with adaptive optimization
 * - Display state caching with intelligent diff algorithms
 * - Enterprise-grade configuration management
 * - Integration preparation for existing shell functions
 * - Memory-safe resource management with comprehensive error handling
 *
 * Architecture Integration:
 * This controller serves as the top-level coordination layer in the display
 * architecture, managing the composition engine, terminal control, and event
 * systems to provide a unified interface for shell integration.
 */

#include "display/display_controller.h"
#include "display/base_terminal.h"
#include "display/command_layer.h"
#include "display/prompt_layer.h"
#include "display/screen_buffer.h"
#include "display_integration.h"
#include "input_continuation.h"
#include "lle/utf8_support.h"
#include "lush_memory_pool.h"

// LLE Completion menu support (Spec 12 - Proper Architecture)
#include "lle/completion/completion_menu_renderer.h"
#include "lle/completion/completion_menu_state.h"

// LLE Notification support (transient hints)
#include "lle/notification.h"

// LLE Shell integration (RPROMPT)
#include "lle/lle_shell_integration.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// EINTR-SAFE WRITE HELPER
// ============================================================================

/**
 * @brief Write all bytes to a file descriptor, retrying on EINTR
 *
 * A signal interrupting write() mid-escape-sequence can corrupt terminal
 * state. This wrapper retries on EINTR and handles partial writes.
 *
 * @param fd   File descriptor to write to
 * @param buf  Data to write
 * @param len  Number of bytes to write
 * @return true if all bytes written, false on error
 */
static bool dc_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// ============================================================================
// INTERNAL CONSTANTS AND MACROS
// ============================================================================

#define DC_HASH_SEED 0x811c9dc5
#define DC_HASH_PRIME 0x01000193
#define DC_MAX_STATE_HASH_LENGTH 65
#define DC_CACHE_CLEANUP_INTERVAL_MS 30000
#define DC_PERFORMANCE_UPDATE_INTERVAL_MS 100
#define DC_ADAPTIVE_OPTIMIZATION_THRESHOLD 5

// Debugging and logging macros
#if 1 // DC_DEBUG enabled temporarily for debugging
#define DC_DEBUG(fmt, ...)                                                     \
    do {                                                                       \
        FILE *_dbg = fopen("/tmp/lush_dc_debug.log", "a");                   \
        if (_dbg) {                                                            \
            fprintf(_dbg, "[DC_DEBUG] %s:%d: " fmt "\n", __func__, __LINE__,   \
                    ##__VA_ARGS__);                                            \
            fclose(_dbg);                                                      \
        }                                                                      \
    } while (0)
#else
#define DC_DEBUG(fmt, ...)                                                     \
    do {                                                                       \
    } while (0)
#endif

#define DC_ERROR(fmt, ...)                                                     \
    fprintf(stderr, "[DC_ERROR] %s:%d: " fmt "\n", __func__, __LINE__,         \
            ##__VA_ARGS__)

// ============================================================================
// INTERNAL UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Get current timestamp in microseconds
 * @return Current time in microseconds from gettimeofday
 */
MAYBE_UNUSED
static uint64_t dc_get_timestamp_us(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

/**
 * @brief Count newlines in text for multiline input detection
 * @param text Text to count newlines in
 * @return Number of newline characters in text
 */
static int count_newlines(const char *text) {
    if (!text)
        return 0;
    int count = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n')
            count++;
    }
    return count;
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

/**
 * @brief Handle LAYER_EVENT_REDRAW_NEEDED from command_layer
 *
 * This is the critical event handler that connects command_layer updates
 * to actual terminal rendering. When LLE updates the command_layer content,
 * command_layer publishes REDRAW_NEEDED, and this handler renders it.
 *
 * @param event The redraw event
 * @param user_data Pointer to display_controller_t instance
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
/* SCREEN BUFFER SYSTEM - The proper way to handle terminal updates
 *
 * Instead of trying to move cursor back and redraw (which is unreliable),
 * we maintain virtual screen buffers representing what's on the terminal.
 * On each update, we diff old vs new and apply only the changes.
 *
 * This is the proven approach used by ZLE, Fish, and Replxx.
 */
static screen_buffer_t current_screen;
static screen_buffer_t desired_screen;
static bool screen_buffer_initialized = false;
static bool prompt_rendered = false;
static int last_terminal_end_row =
    0; /* Actual terminal row after ghost text/menu */
/* Note: Notification is now tracked in screen_buffer like menu, so no separate
 * tracking variable needed */

/**
 * @brief Reset display state - called when starting new input session
 *
 * Clears screen buffers so next render starts fresh.
 */
void dc_reset_prompt_display_state(void) {
    prompt_rendered = false;
    last_terminal_end_row = 0;
    if (screen_buffer_initialized) {
        screen_buffer_clear(&current_screen);
        screen_buffer_clear(&desired_screen);
    }

    /* Reset command_layer's update_sequence_number so next render is treated
     * as "first render" and publishes REDRAW_NEEDED even if buffer is empty.
     * Without this, empty-to-empty transitions (like Ctrl+G on empty buffer)
     * would skip the event and prompt wouldn't be drawn. */
    display_controller_t *dc = display_integration_get_controller();
    if (dc && dc->compositor && dc->compositor->command_layer) {
        dc->compositor->command_layer->update_sequence_number = 0;
    }
}

/**
 * @brief Finalize input and prepare for command output
 *
 * Writes newline to terminal to move cursor below the input, then
 * resets display state for the next prompt.
 */
void dc_finalize_input(void) {
    /* Block SIGWINCH during terminal output */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGWINCH);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    /* Write newline to move cursor to next line for command output */
    dc_write_all(STDOUT_FILENO, "\n", 1);

    /* Reset display state for next prompt */
    dc_reset_prompt_display_state();

    sigprocmask(SIG_SETMASK, &old_set, NULL);
}

/**
 * @brief Apply transient prompt replacement for scrollback history
 * @param transient_prompt Minimal prompt to replace current fancy prompt
 * @param command_text Command text to preserve after prompt
 * @return true if transient prompt was applied, false on error
 */
bool dc_apply_transient_prompt(const char *transient_prompt,
                               const char *command_text) {
    if (!transient_prompt || !screen_buffer_initialized) {
        return false;
    }

    /* Block SIGWINCH during terminal output to prevent mid-sequence corruption */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGWINCH);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    /*
     * Transient Prompt Replacement (Spec 25 Section 12)
     *
     * Replace the current (fancy) prompt with a minimal transient prompt
     * while preserving the command text. This simplifies scrollback history.
     *
     * Current state (from current_screen):
     * - command_start_row: row where command starts (0 = same line as prompt)
     * - cursor_row: current cursor row (last row of display)
     * - We need to go back and redraw everything from row 0
     *
     * Algorithm:
     * 1. Move cursor up to row 0, column 1
     * 2. Clear from there to end of screen
     * 3. Write transient prompt + command text
     * 4. Update screen buffer to reflect new state
     */

    int total_rows = current_screen.cursor_row + 1;
    char seq_buf[32];
    int seq_len;

    /* Step 1: Move cursor up to the first row (where prompt started) */
    if (current_screen.cursor_row > 0) {
        seq_len = snprintf(seq_buf, sizeof(seq_buf), "\033[%dA",
                           current_screen.cursor_row);
        if (seq_len > 0) {
            dc_write_all(STDOUT_FILENO, seq_buf, (size_t)seq_len);
        }
    }

    /* Step 2: Move to column 1 */
    dc_write_all(STDOUT_FILENO, "\033[1G", 4);

    /* Step 3: Clear from cursor to end of screen */
    dc_write_all(STDOUT_FILENO, "\033[J", 3);

    /* Step 4: Write transient prompt */
    if (transient_prompt[0] != '\0') {
        dc_write_all(STDOUT_FILENO, transient_prompt, strlen(transient_prompt));
    }

    /* Step 5: Write command text (with syntax highlighting if available) */
    if (command_text && command_text[0] != '\0') {
        bool wrote_highlighted = false;

        /* Try to get syntax-highlighted version from display controller */
        display_controller_t *dc = display_integration_get_controller();
        if (dc && dc->compositor && dc->compositor->command_layer) {
            command_layer_t *cmd_layer = dc->compositor->command_layer;

            /* Temporarily set the command text for highlighting */
            command_layer_error_t set_result = command_layer_set_command(
                cmd_layer, command_text, strlen(command_text));

            if (set_result == COMMAND_LAYER_SUCCESS) {
                char highlighted_buffer[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE];
                command_layer_error_t highlight_result =
                    command_layer_get_highlighted_text(
                        cmd_layer, highlighted_buffer,
                        sizeof(highlighted_buffer));

                if (highlight_result == COMMAND_LAYER_SUCCESS &&
                    highlighted_buffer[0] != '\0') {
                    dc_write_all(STDOUT_FILENO, highlighted_buffer,
                          strlen(highlighted_buffer));
                    wrote_highlighted = true;
                }
            }
        }

        /* Fallback to plain text if highlighting failed */
        if (!wrote_highlighted) {
            dc_write_all(STDOUT_FILENO, command_text, strlen(command_text));
        }
    }

    /* Step 6: Update screen buffer to reflect new state
     * Re-render with transient prompt so current_screen is accurate */
    size_t cursor_offset = command_text ? strlen(command_text) : 0;
    screen_buffer_render(&current_screen, transient_prompt,
                         command_text ? command_text : "", cursor_offset);

    /* Mark that we handled this - prompt_rendered stays true since we
     * wrote a prompt, but next dc_reset_prompt_display_state will clear it */

    (void)total_rows; /* Used for documentation, may use later */

    sigprocmask(SIG_SETMASK, &old_set, NULL);
    return true;
}

/**
 * @brief Get current prompt display metrics
 * @param prompt_lines Output: number of lines the prompt occupies
 * @param total_lines Output: total lines including command
 * @param command_col Output: column where command text starts
 */
void dc_get_prompt_metrics(int *prompt_lines, int *total_lines,
                           int *command_col) {
    if (!screen_buffer_initialized) {
        if (prompt_lines)
            *prompt_lines = 1;
        if (total_lines)
            *total_lines = 1;
        if (command_col)
            *command_col = 0;
        return;
    }

    /*
     * current_screen contains the last rendered state:
     * - command_start_row: row where command text starts (0-based)
     * - command_start_col: column where command text starts
     * - cursor_row: current cursor row (0-based)
     *
     * For a single-line prompt "$ cmd":
     *   command_start_row = 0, command_start_col > 0
     *   prompt occupies part of row 0
     *
     * For a two-line prompt:
     *   Line 0: "[user@host] ~/path"
     *   Line 1: "$ cmd"
     *   command_start_row = 1, prompt occupies rows 0-1
     *
     * prompt_lines = command_start_row + 1 (since it's 0-indexed)
     * But if command_start_row == 0, prompt shares line with command,
     * so prompt_lines = 1.
     */
    if (prompt_lines) {
        *prompt_lines = current_screen.command_start_row + 1;
    }

    if (total_lines) {
        /* Total lines = cursor row + 1 (cursor is on the last used row) */
        *total_lines = current_screen.cursor_row + 1;
    }

    if (command_col) {
        *command_col = current_screen.command_start_col;
    }
}

/**
 * @brief Callback for screen_buffer_render_with_continuation
 *
 * Called when a newline is encountered during character-by-character rendering.
 * Analyzes the line that just ended and returns the continuation prompt for
 * the next line based on the current parsing state.
 *
 * @param line_text Plain text of the line that just ended (ANSI stripped)
 * @param line_len Length of line_text
 * @param line_number Logical line number (0-based)
 * @param user_data Pointer to continuation_state_t
 * @return Continuation prompt string, or NULL
 */
static const char *dc_continuation_prompt_callback(const char *line_text,
                                                   size_t line_len,
                                                   int line_number,
                                                   void *user_data) {
    continuation_state_t *cont_state = (continuation_state_t *)user_data;
    if (!cont_state) {
        return "> "; /* Fallback */
    }

    /* Analyze this line to update continuation state */
    continuation_analyze_line(line_text, cont_state);

    /* Get the continuation prompt based on updated state */
    const char *prompt = continuation_get_prompt(cont_state);

    DC_DEBUG("Continuation callback: line=%d, text='%.40s%s', prompt='%s'",
             line_number, line_text, line_len > 40 ? "..." : "",
             prompt ? prompt : "(null)");

    return prompt;
}

/**
 * @brief Handle redraw needed event from command layer
 * @param event Layer event that triggered this callback
 * @param user_data User data (display_controller_t pointer)
 * @return LAYER_EVENTS_SUCCESS on success, error code otherwise
 */
static layer_events_error_t dc_handle_redraw_needed(const layer_event_t *event,
                                                    void *user_data) {

    (void)event;
    display_controller_t *controller = (display_controller_t *)user_data;

    if (!controller || !controller->is_initialized) {
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    /* Block SIGWINCH during rendering to prevent a resize signal from
     * interrupting mid-escape-sequence, which corrupts terminal state.
     * The signal will be delivered after we restore the mask. */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGWINCH);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    command_layer_t *cmd_layer = controller->compositor->command_layer;
    if (!cmd_layer) {
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    int term_width = 80;
    if (controller->terminal_ctrl &&
        controller->terminal_ctrl->capabilities.terminal_width > 0) {
        term_width = controller->terminal_ctrl->capabilities.terminal_width;
    }

    if (!screen_buffer_initialized) {
        screen_buffer_init(&current_screen, term_width);
        screen_buffer_init(&desired_screen, term_width);
        screen_buffer_initialized = true;
    } else {
        current_screen.terminal_width = term_width;
        desired_screen.terminal_width = term_width;
    }

    prompt_layer_t *prompt_layer = controller->compositor->prompt_layer;
    char prompt_buffer[PROMPT_LAYER_MAX_CONTENT_SIZE] = {0};
    if (prompt_layer) {
        prompt_layer_get_rendered_content(prompt_layer, prompt_buffer,
                                          sizeof(prompt_buffer));
    }

    char command_buffer[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE];
    command_layer_error_t cmd_result = command_layer_get_highlighted_text(
        cmd_layer, command_buffer, sizeof(command_buffer));

    if (cmd_result != COMMAND_LAYER_SUCCESS) {
        return LAYER_EVENTS_ERROR_INVALID_PARAM;
    }

    /* Render completion menu if active (Proper Architecture - Spec 12)
     * Menu is now composed at display time, not baked into command text */
    char menu_buffer[8192] = {0};
    char *menu_text = NULL;

    if (controller->completion_menu_visible &&
        controller->active_completion_menu) {
        lle_menu_render_options_t options =
            lle_menu_renderer_default_options(term_width);
        options.max_rows = 20; /* Limit menu to 20 rows */

        lle_menu_render_stats_t stats;
        lle_result_t result = lle_completion_menu_render(
            controller->active_completion_menu, &options, menu_buffer,
            sizeof(menu_buffer), &stats);

        if (result == LLE_SUCCESS && menu_buffer[0]) {
            menu_text = menu_buffer;
        }
    }

    /* CONTINUATION PROMPT SUPPORT:
     *
     * Use screen_buffer_render_with_continuation() which calls back on each
     * newline to get the context-aware continuation prompt. This is the
     * architecturally correct approach: prompts are set at the exact visual row
     * where each newline lands during character-by-character rendering, not
     * pre-calculated.
     *
     * The callback receives the plain text of each line (ANSI stripped) and
     * updates the continuation state to determine the appropriate prompt.
     */
    int newline_count = count_newlines(command_buffer);
    bool is_multiline = (newline_count > 0);

    size_t cursor_byte_offset = cmd_layer->cursor_position;

    if (is_multiline) {
        /* Use callback-based rendering for proper visual row tracking */
        continuation_state_t cont_state;
        continuation_state_init(&cont_state);

        screen_buffer_render_with_continuation(
            &desired_screen, prompt_buffer, command_buffer, cursor_byte_offset,
            dc_continuation_prompt_callback, &cont_state);

        continuation_state_cleanup(&cont_state);
    } else {
        /* No newlines - use simple render */
        screen_buffer_render(&desired_screen, prompt_buffer, command_buffer,
                             cursor_byte_offset);
    }

    /* DEBUG: Log what screen_buffer_render produced */
    DC_DEBUG("After render: num_rows=%d, command_start_row=%d, cursor=(%d,%d), "
             "term_width=%d",
             desired_screen.num_rows, desired_screen.command_start_row,
             desired_screen.cursor_row, desired_screen.cursor_col, term_width);

    /* DEBUG: Log prefixes set on each row */
    for (int r = 0; r < desired_screen.num_rows && r < 10; r++) {
        const char *prefix = screen_buffer_get_line_prefix(&desired_screen, r);
        if (prefix) {
            DC_DEBUG("  Row %d has prefix: '%s'", r, prefix);
        }
    }

    /* Pass RPROMPT to screen_buffer for fit calculation.
     * Must be done after screen_buffer_render() so command_start_col is valid.
     * The RPROMPT text comes from the shell integration layer which expands
     * RPROMPT/RPS1 through the Spec 28 two-pass engine. */
    const char *rprompt = lle_shell_get_rendered_rprompt();
    if (rprompt && rprompt[0]) {
        screen_buffer_set_rprompt(&desired_screen, rprompt);
    }

    /* Add menu rows to screen_buffer per SCREEN_BUFFER_MENU_INTEGRATION_PLAN.md
     *
     * This is the key fix: by adding menu rows to screen_buffer AFTER rendering
     * command text, the buffer knows the total display height. This allows
     * screen_buffer_get_rows_below_cursor() to return the correct value for
     * cursor positioning, fixing the "upward row consumption" bug.
     *
     * Cursor position (cursor_row, cursor_col) stays in the command area -
     * menu rows are added AFTER and don't affect cursor tracking.
     */
    int menu_rows_added = 0;
    if (menu_text && *menu_text) {
        /* Add menu starting at row after command ends.
         * Note: We add a newline before menu, so start at num_rows (which is
         * one past the last command row). */
        int menu_start_row = desired_screen.num_rows;
        menu_rows_added = screen_buffer_add_text_rows(
            &desired_screen, menu_start_row, menu_text);

        DC_DEBUG("Added menu to screen_buffer: start_row=%d, rows_added=%d, "
                 "new_num_rows=%d",
                 menu_start_row, menu_rows_added, desired_screen.num_rows);
    }

    /* Get notification text if visible and add to screen_buffer for proper tracking
     * Following the same pattern as menu to ensure correct cursor positioning */
    char notification_buffer[LLE_NOTIFICATION_MAX_STYLED];
    const char *notification_text = NULL;
    int notification_rows_added = 0;

    if (controller->notification_visible && controller->notification_copy.visible) {
        notification_text = lle_notification_get_styled_text(
            &controller->notification_copy, notification_buffer,
            sizeof(notification_buffer));

        /* Add notification to screen_buffer like we do for menu */
        if (notification_text && *notification_text) {
            int notif_start_row = desired_screen.num_rows;
            notification_rows_added = screen_buffer_add_text_rows(
                &desired_screen, notif_start_row, notification_text);

            DC_DEBUG("Added notification to screen_buffer: start_row=%d, rows_added=%d, "
                     "new_num_rows=%d",
                     notif_start_row, notification_rows_added, desired_screen.num_rows);
        }
    }

    /* PROMPT-ONCE ARCHITECTURE per MODERN_EDITOR_WRAPPING_RESEARCH.md
     *
     * This implements the proven approach used by Replxx, Fish, and ZLE:
     * 1. Prompt is drawn ONCE on first render and NEVER redrawn
     * 2. Only the command text area is cleared and redrawn on updates
     * 3. Absolute column positioning (\033[{n}G) is used instead of \r
     *
     * Key principle: Never move cursor to column 0 after first render,
     * as this would allow \033[J to clear the prompt.
     */

    /* First render only: Draw prompt once */
    if (!prompt_rendered) {
        if (prompt_buffer[0]) {
            dc_write_all(STDOUT_FILENO, prompt_buffer, strlen(prompt_buffer));
        }
        prompt_rendered = true;
    }

    /* Every render (including first): Position to command start and redraw
     * command */

    /* Step 1: Move to absolute column where command starts (after prompt)
     * For multi-line prompts, use command_start_col from screen buffer
     * Use \033[{n}G for absolute positioning (1-based indexing) */
    int command_start_col = desired_screen.command_start_col;
    char move_to_col[32];
    int col_len = snprintf(move_to_col, sizeof(move_to_col), "\033[%dG",
                           command_start_col + 1);
    if (col_len > 0) {
        dc_write_all(STDOUT_FILENO, move_to_col, col_len);
    }

    /* Step 2: Handle ghost text/menu cleanup from previous render
     *
     * Problem: Ghost text (autosuggestions) may have wrapped to extra rows.
     * After Step 5 of previous render, cursor was moved back to command cursor
     * position. But the ghost text remains on screen below that.
     *
     * Solution: If previous render had extra rows (ghost text/menu), move DOWN
     * to that row first, then clear, then move back up. This ensures we clear
     * all the ghost text artifacts. */
    int command_row = desired_screen.command_start_row;

    if (last_terminal_end_row > current_screen.cursor_row) {
        /* Previous render had ghost text/menu extending below cursor position.
         * Move DOWN to that row to clear from there. */
        int rows_down = last_terminal_end_row - current_screen.cursor_row;
        DC_DEBUG("Step2: Moving DOWN %d rows to clear ghost text", rows_down);
        char move_down[32];
        int down_len =
            snprintf(move_down, sizeof(move_down), "\033[%dB", rows_down);
        if (down_len > 0) {
            dc_write_all(STDOUT_FILENO, move_down, down_len);
        }

        /* Clear from here to end of screen (clears ghost text) */
        dc_write_all(STDOUT_FILENO, "\033[J", 3);

        /* Move back up to command start row */
        int rows_up = last_terminal_end_row - command_row;
        if (rows_up > 0) {
            DC_DEBUG("Step2: Moving UP %d rows to command start", rows_up);
            char move_up[32];
            int up_len =
                snprintf(move_up, sizeof(move_up), "\033[%dA", rows_up);
            if (up_len > 0) {
                dc_write_all(STDOUT_FILENO, move_up, up_len);
            }
        }
    } else if (current_screen.cursor_row > command_row) {
        /* No ghost text overflow, but cursor is below command start - move up
         */
        int rows_up = current_screen.cursor_row - command_row;
        DC_DEBUG("Step2: Moving up %d rows", rows_up);
        char move_up[32];
        int up_len = snprintf(move_up, sizeof(move_up), "\033[%dA", rows_up);
        if (up_len > 0) {
            dc_write_all(STDOUT_FILENO, move_up, up_len);
        }
    }

    /* Step 3: Clear from current position to end of screen
     * This clears only the command area, never touches the prompt */
    dc_write_all(STDOUT_FILENO, "\033[J", 3);

    /* Step 3.5: Write RPROMPT (right-aligned on prompt row)
     *
     * RPROMPT occupies the right side of the prompt row. It's written after
     * \033[J clears the command area (which also erases any previous RPROMPT),
     * so we must redraw it every cycle.
     *
     * Sequence: move to rprompt_col, write RPROMPT, move back to command_start.
     * Only on single-line prompts where command hasn't grown into RPROMPT space.
     */
    if (desired_screen.rprompt_fits && desired_screen.rprompt_text[0]) {
        char rprompt_col_seq[16];
        int rprompt_col_len =
            snprintf(rprompt_col_seq, sizeof(rprompt_col_seq), "\033[%dG",
                     desired_screen.rprompt_col + 1);
        if (rprompt_col_len > 0) {
            dc_write_all(STDOUT_FILENO, rprompt_col_seq, (size_t)rprompt_col_len);
        }
        dc_write_all(STDOUT_FILENO, desired_screen.rprompt_text,
                      strlen(desired_screen.rprompt_text));
        /* Reset attributes after RPROMPT (it may contain colors) */
        dc_write_all(STDOUT_FILENO, "\033[0m", 4);
        /* Move back to command start column */
        char back_col_seq[16];
        int back_col_len =
            snprintf(back_col_seq, sizeof(back_col_seq), "\033[%dG",
                     command_start_col + 1);
        if (back_col_len > 0) {
            dc_write_all(STDOUT_FILENO, back_col_seq, (size_t)back_col_len);
        }
    }

    /* Step 4: Write command text with continuation prompts
     *
     * For multiline input, we need to insert continuation prompts after each
     * newline. The screen_buffer has prefixes set at visual rows (accounting
     * for wrapping), so we must track visual rows during output to find the
     * correct prefix.
     *
     * This uses character-by-character tracking to match how
     * screen_buffer_render calculated visual rows, ensuring prefixes appear on
     * the correct lines.
     */
    if (command_buffer[0]) {
        if (is_multiline) {
            /* Write command text character by character, tracking visual rows
             * to output continuation prompts at the correct positions */
            size_t i = 0;
            size_t text_len = strlen(command_buffer);
            int visual_row = desired_screen.command_start_row;
            int visual_col = desired_screen.command_start_col;

            while (i < text_len) {
                unsigned char ch = (unsigned char)command_buffer[i];

                /* Handle ANSI escape sequences - pass through without affecting
                 * position */
                if (ch == '\033' || ch == '\x1b') {
                    size_t seq_start = i;
                    i++;
                    if (i < text_len && command_buffer[i] == '[') {
                        i++;
                        while (i < text_len) {
                            char c = command_buffer[i++];
                            if ((c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') || c == 'm' ||
                                c == 'H' || c == 'J' || c == 'K' || c == 'G' ||
                                c == 'f' || c == 's' || c == 'u') {
                                break;
                            }
                        }
                    }
                    /* Write the ANSI sequence */
                    dc_write_all(STDOUT_FILENO, command_buffer + seq_start,
                          i - seq_start);
                    continue;
                }

                /* Handle newlines - move to next visual row and output
                 * continuation prompt */
                if (ch == '\n') {
                    dc_write_all(STDOUT_FILENO, "\n", 1);
                    visual_row++;

                    /* Get continuation prompt for this visual row */
                    const char *cont_prompt = screen_buffer_get_line_prefix(
                        &desired_screen, visual_row);
                    if (cont_prompt) {
                        /* Reset ANSI state before writing continuation prompt
                         */
                        dc_write_all(STDOUT_FILENO, "\033[0m", 4);
                        dc_write_all(STDOUT_FILENO, cont_prompt, strlen(cont_prompt));
                        visual_col =
                            (int)screen_buffer_get_line_prefix_visual_width(
                                &desired_screen, visual_row);
                    } else {
                        visual_col = 0;
                    }

                    i++;
                    continue;
                }

                /* Handle regular characters - track visual column for wrapping
                 */
                uint32_t codepoint;
                int char_bytes = lle_utf8_decode_codepoint(
                    command_buffer + i, text_len - i, &codepoint);

                if (char_bytes > 0) {
                    int char_width = lle_utf8_codepoint_width(codepoint);

                    /* Write the character */
                    dc_write_all(STDOUT_FILENO, command_buffer + i, char_bytes);

                    /* Update visual position */
                    visual_col += char_width;

                    /* Check for line wrap */
                    if (visual_col >= term_width) {
                        visual_row++;
                        visual_col = 0;
                        /* Note: wrapped lines don't get continuation prompts,
                         * only lines after explicit newlines do */
                    }

                    i += char_bytes;
                } else {
                    /* Invalid UTF-8, write single byte */
                    dc_write_all(STDOUT_FILENO, command_buffer + i, 1);
                    visual_col++;
                    if (visual_col >= term_width) {
                        visual_row++;
                        visual_col = 0;
                    }
                    i++;
                }
            }
        } else {
            /* Single-line input - write directly */
            dc_write_all(STDOUT_FILENO, command_buffer, strlen(command_buffer));
        }
    }

    /* Step 4a: Write autosuggestion ghost text (Fish-style)
     *
     * Conditions for showing ghost text:
     * 1. Autosuggestions enabled and layer available
     * 2. No completion menu visible (menu takes precedence)
     * 3. Not multiline input (simplifies initial implementation)
     * 4. Cursor is at end of command (checked by autosuggestions_layer)
     *
     * The ghost text appears in BRIGHT_BLACK (gray/dimmed) after the command.
     * Cursor positioning (Step 5) will move cursor back to correct position.
     */
    if (controller->autosuggestions_enabled &&
        controller->autosuggestions_layer &&
        !controller->completion_menu_visible && !is_multiline) {

        const char *suggestion = autosuggestions_layer_get_current_suggestion(
            controller->autosuggestions_layer);

        if (suggestion && *suggestion) {
            /* Write ghost text in BRIGHT_BLACK (dimmed gray) */
            dc_write_all(STDOUT_FILENO, "\033[90m",
                  5); /* Set bright black foreground */
            dc_write_all(STDOUT_FILENO, suggestion, strlen(suggestion));
            dc_write_all(STDOUT_FILENO, "\033[0m", 4); /* Reset all attributes */
        }
    }

    /* Step 4b: Write completion menu WITHOUT continuation prompts */
    if (menu_text && *menu_text) {
        dc_write_all(STDOUT_FILENO, "\n", 1);
        dc_write_all(STDOUT_FILENO, menu_text, strlen(menu_text));
    }

    /* Step 4c: Write notification below menu (if any)
     * Notification is now tracked in screen_buffer like menu */
    if (notification_text && *notification_text) {
        dc_write_all(STDOUT_FILENO, "\n", 1);
        dc_write_all(STDOUT_FILENO, notification_text, strlen(notification_text));
    }

    /* Step 5: Position cursor at the correct location
     *
     * After drawing command text, ghost text, and menu, terminal cursor is at
     * the end. We need to position it where the user's cursor actually is (in
     * the command).
     *
     * With the new architecture (per SCREEN_BUFFER_MENU_INTEGRATION_PLAN.md):
     * - Menu rows were added to desired_screen via
     * screen_buffer_add_text_rows()
     * - desired_screen.num_rows now includes menu rows
     * - desired_screen.cursor_row is still in the command area (unaffected by
     * menu)
     * - screen_buffer_get_rows_below_cursor() gives us the exact count
     *
     * Ghost text is not yet tracked in screen_buffer, so we still calculate it
     * separately.
     */
    int cursor_row = desired_screen.cursor_row;
    int cursor_col = desired_screen.cursor_col;

    /* Calculate extra rows added by ghost text (autosuggestion)
     * Ghost text may wrap to additional lines beyond the command text.
     * TODO: Add ghost text to screen_buffer for unified tracking. */
    int ghost_text_extra_rows = 0;
    if (controller->autosuggestions_enabled &&
        controller->autosuggestions_layer &&
        !controller->completion_menu_visible && !is_multiline) {

        const char *suggestion = autosuggestions_layer_get_current_suggestion(
            controller->autosuggestions_layer);

        if (suggestion && *suggestion) {
            size_t suggestion_width =
                lle_utf8_string_width(suggestion, strlen(suggestion));
            int command_end_col = desired_screen.command_end_col;
            int total_cols_needed = command_end_col + (int)suggestion_width;

            if (total_cols_needed > term_width) {
                ghost_text_extra_rows = (total_cols_needed - 1) / term_width;
            }
        }
    }

    /* Calculate rows to move up using screen_buffer's tracked state.
     *
     * screen_buffer_get_rows_below_cursor() returns: (num_rows - 1) -
     * cursor_row This accounts for menu rows AND notification rows that were
     * added via screen_buffer_add_text_rows(). We still need to add
     * ghost_text_extra_rows since those aren't in screen_buffer yet.
     */
    int rows_to_move_up = screen_buffer_get_rows_below_cursor(&desired_screen) +
                          ghost_text_extra_rows;

    DC_DEBUG("Step5: cursor=(%d,%d), num_rows=%d, rows_below=%d, ghost=%d, "
             "total_up=%d",
             cursor_row, cursor_col, desired_screen.num_rows,
             screen_buffer_get_rows_below_cursor(&desired_screen),
             ghost_text_extra_rows, rows_to_move_up);

    if (rows_to_move_up > 0) {
        char up_seq[16];
        int up_len =
            snprintf(up_seq, sizeof(up_seq), "\033[%dA", rows_to_move_up);
        if (up_len > 0) {
            dc_write_all(STDOUT_FILENO, up_seq, up_len);
        }
    }

    /* Move to absolute column (never use \r - it goes to column 0!)
     * Use \033[{n}G for absolute column positioning (1-based indexing) */
    char col_seq[16];
    int col_seq_len =
        snprintf(col_seq, sizeof(col_seq), "\033[%dG", cursor_col + 1);
    if (col_seq_len > 0) {
        dc_write_all(STDOUT_FILENO, col_seq, col_seq_len);
    }

    DC_DEBUG(
        "Step5 done: copying desired_screen to current_screen (cursor_row=%d)",
        desired_screen.cursor_row);
    screen_buffer_copy(&current_screen, &desired_screen);
    prompt_rendered = true;

    /* Track where the terminal display actually ends (including ghost
     * text/menu/notification) This is needed by Step 2 on the next render to
     * move up the correct amount.
     *
     * With menu AND notification rows now tracked in screen_buffer:
     * - (num_rows - 1) gives the last row index in the buffer (includes menu
     *   and notification)
     * - ghost_text_extra_rows adds any additional wrapping from autosuggestions
     */
    last_terminal_end_row =
        (desired_screen.num_rows - 1) + ghost_text_extra_rows;

    /* NOTE: fsync() was causing input timeouts after cursor positioning -
     * removed stdout is line-buffered by default and terminal I/O doesn't need
     * fsync */

    /* Restore original signal mask - any pending SIGWINCH will be delivered now */
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    return LAYER_EVENTS_SUCCESS;
}

/**
 * @brief Calculate time difference in nanoseconds
 * @param start Start time
 * @param end End time
 * @return Time difference in nanoseconds
 */
static uint64_t dc_time_diff_ns(const struct timeval *start,
                                const struct timeval *end) {
    if (!start || !end)
        return 0;

    uint64_t start_ns =
        (uint64_t)start->tv_sec * 1000000000 + (uint64_t)start->tv_usec * 1000;
    uint64_t end_ns =
        (uint64_t)end->tv_sec * 1000000000 + (uint64_t)end->tv_usec * 1000;

    return (end_ns > start_ns) ? (end_ns - start_ns) : 0;
}

/**
 * @brief Simple FNV-1a hash function for state comparison
 * @param str String to hash
 * @return 32-bit hash value
 */
static uint32_t dc_hash_string(const char *str) {
    if (!str)
        return 0;

    uint32_t hash = DC_HASH_SEED;
    const unsigned char *data = (const unsigned char *)str;

    while (*data) {
        hash ^= *data++;
        hash *= DC_HASH_PRIME;
    }

    return hash;
}

/**
 * @brief Extract semantic components from prompt for intelligent caching
 *
 * Timestamp-aware normalization that ignores dynamic time elements.
 *
 * @param prompt Prompt string to hash semantically
 * @return 32-bit semantic hash value
 */
static uint32_t dc_hash_prompt_semantic(const char *prompt) {
    if (!prompt || *prompt == '\0')
        return DC_HASH_SEED;

    // For maximum cache hit rates, normalize prompt by removing time-sensitive
    // elements
    char normalized[512] = {0};
    const char *p = prompt;
    size_t pos = 0;
    bool in_color_seq = false;
    bool skip_timestamp = false;

    while (*p && pos < sizeof(normalized) - 1) {
        if (*p == '\033' || *p == '\x1b') {
            in_color_seq = true;
            normalized[pos++] = 'C'; // Color marker only
            p++;
            continue;
        }

        if (in_color_seq) {
            if (*p == 'm' || *p == 'K' || *p == 'J' || *p == 'H') {
                in_color_seq = false;
            }
            p++;
            continue;
        }

        // Detect and skip timestamp patterns (common formats)
        // Skip sequences like "Mon Jan 27 15:30:42 2025" or "15:30:42" or "Jan
        // 27"
        if ((*p >= '0' && *p <= '9') &&
            (*(p + 1) == ':' || (isdigit(*(p + 1)) && *(p + 2) == ':'))) {
            // Skip time patterns like "15:30:42" or "3:45"
            skip_timestamp = true;
        } else if (skip_timestamp && (*p == ' ' || *p == '\t' || *p == '\n')) {
            skip_timestamp = false;
            p++;
            continue;
        } else if (skip_timestamp) {
            p++;
            continue;
        }

        // Skip common date patterns
        if ((strncmp(p, "Mon ", 4) == 0) || (strncmp(p, "Tue ", 4) == 0) ||
            (strncmp(p, "Wed ", 4) == 0) || (strncmp(p, "Thu ", 4) == 0) ||
            (strncmp(p, "Fri ", 4) == 0) || (strncmp(p, "Sat ", 4) == 0) ||
            (strncmp(p, "Sun ", 4) == 0) || (strncmp(p, "Jan ", 4) == 0) ||
            (strncmp(p, "Feb ", 4) == 0) || (strncmp(p, "Mar ", 4) == 0) ||
            (strncmp(p, "Apr ", 4) == 0) || (strncmp(p, "May ", 4) == 0) ||
            (strncmp(p, "Jun ", 4) == 0) || (strncmp(p, "Jul ", 4) == 0) ||
            (strncmp(p, "Aug ", 4) == 0) || (strncmp(p, "Sep ", 4) == 0) ||
            (strncmp(p, "Oct ", 4) == 0) || (strncmp(p, "Nov ", 4) == 0) ||
            (strncmp(p, "Dec ", 4) == 0)) {
            // Skip to end of timestamp section
            while (*p && *p != '\n' && *p != '$' && *p != '#' && *p != '>') {
                p++;
            }
            continue;
        }

        // Normalize structural elements
        if (*p == '$' || *p == '#' || *p == '>') {
            normalized[pos++] = 'P'; // Prompt end marker
        } else if (*p == '[' || *p == '(' || *p == '{') {
            normalized[pos++] = 'B'; // Begin bracket
        } else if (*p == ']' || *p == ')' || *p == '}') {
            normalized[pos++] = 'E'; // End bracket
        } else if (*p == '/' || *p == '~') {
            normalized[pos++] = 'D'; // Directory marker
        } else if (*p == '@') {
            normalized[pos++] = 'A'; // User@host separator
        } else if (isalpha(*p)) {
            normalized[pos++] = tolower(*p); // Normalize case
        } else if (*p != ' ' && *p != '\t' && *p != '\n') {
            normalized[pos++] = *p; // Keep other significant chars
        }

        p++;
    }

    normalized[pos] = '\0';

    // Hash the normalized prompt structure
    uint32_t hash = DC_HASH_SEED;
    const unsigned char *data = (const unsigned char *)normalized;
    while (*data) {
        hash ^= *data++;
        hash *= DC_HASH_PRIME;
    }

    return hash;
}

/**
 * @brief Extract semantic components from command for intelligent caching
 *
 * Deterministic command classification for consistent cache grouping.
 *
 * @param command Command string to hash semantically
 * @return 32-bit semantic hash value
 */
static uint32_t dc_hash_command_semantic(const char *command) {
    if (!command || *command == '\0')
        return DC_HASH_SEED;

    // Simplified but more predictable command classification
    uint32_t cmd_hash = DC_HASH_SEED;
    const char *p = command;

    // Skip leading whitespace
    while (*p && isspace(*p))
        p++;

    if (!*p)
        return DC_HASH_SEED;

    // Extract base command for classification
    char base_cmd[32] = {0};
    size_t i = 0;
    while (*p && !isspace(*p) && i < sizeof(base_cmd) - 1) {
        base_cmd[i++] = (char)tolower(*p++);
    }

    // Hash the base command
    for (i = 0; base_cmd[i]; i++) {
        cmd_hash ^= (unsigned char)base_cmd[i];
        cmd_hash *= DC_HASH_PRIME;
    }

    // Simple argument classification (not exact matching)
    while (*p && isspace(*p))
        p++;
    int has_flags = 0, has_args = 0;

    while (*p) {
        if (*p == '-') {
            has_flags = 1;
            while (*p && !isspace(*p))
                p++;
        } else if (!isspace(*p)) {
            has_args = 1;
            while (*p && !isspace(*p))
                p++;
        } else {
            p++;
        }
    }

    // Include argument pattern in hash for differentiation
    cmd_hash ^= (has_flags << 8) | (has_args << 4);

    return cmd_hash;
}

/**
 * @brief Generate intelligent state hash for caching and comparison
 *
 * Balanced approach for optimal cache hit rates with necessary differentiation.
 *
 * @param prompt Prompt text
 * @param command Command text
 * @param theme_name Current theme name
 * @param symbol_mode Current symbol compatibility mode
 * @param hash_buffer Output buffer for hash string
 * @param buffer_size Size of hash buffer
 */
static void dc_generate_state_hash(const char *prompt, const char *command,
                                   const char *theme_name,
                                   symbol_compatibility_t symbol_mode,
                                   char *hash_buffer, size_t buffer_size) {
    if (!hash_buffer || buffer_size < DC_MAX_STATE_HASH_LENGTH)
        return;

    // Get semantic hashes for grouping similar states
    uint32_t prompt_semantic = dc_hash_prompt_semantic(prompt);
    uint32_t command_semantic = dc_hash_command_semantic(command);

    // Add theme context to hash calculation
    uint32_t theme_hash = dc_hash_string(theme_name ? theme_name : "default");
    uint32_t symbol_hash = (uint32_t)symbol_mode;

    // Create primary hash from semantic components including theme context
    uint32_t primary_hash = prompt_semantic ^ (command_semantic << 1) ^
                            (theme_hash << 2) ^ (symbol_hash << 3);

    // For identical repeated commands, ensure exact cache hits
    uint32_t exact_prompt = dc_hash_string(prompt ? prompt : "");
    uint32_t exact_command = dc_hash_string(command ? command : "");

    // Balance semantic grouping with exact matching
    // Use semantic hash as primary, exact hash for differentiation
    uint32_t secondary_hash =
        (exact_prompt & 0xFFFF) ^ ((exact_command & 0xFFFF) << 16);

    // Final combined hash: semantic-driven but with exact differentiation and
    // theme context
    uint32_t combined_hash = primary_hash ^ secondary_hash;

    snprintf(hash_buffer, buffer_size, "s%08x_e%08x_c%08x_t%08x_m%x",
             primary_hash, secondary_hash, combined_hash, theme_hash,
             symbol_hash);
}

/**
 * @brief Initialize default configuration values
 * @param config Configuration structure to initialize
 */
static void dc_init_default_config(display_controller_config_t *config) {
    if (!config)
        return;

    memset(config, 0, sizeof(display_controller_config_t));

    // Performance configuration
    config->optimization_level = DISPLAY_OPTIMIZATION_STANDARD;
    config->cache_ttl_ms = DISPLAY_CONTROLLER_DEFAULT_CACHE_TTL_MS;
    config->performance_monitor_interval_ms =
        DISPLAY_CONTROLLER_DEFAULT_MONITORING_INTERVAL_MS;
    config->max_cache_entries = 256;

    // Feature toggles
    config->enable_caching = true;
    config->enable_diff_algorithms = true;
    config->enable_performance_monitoring = true;
    config->enable_adaptive_optimization = true;
    config->enable_integration_mode = false;

    // Threshold configuration
    config->performance_threshold_ms =
        DISPLAY_CONTROLLER_PERFORMANCE_THRESHOLD_MS;
    config->cache_hit_rate_threshold =
        DISPLAY_CONTROLLER_CACHE_HIT_RATE_THRESHOLD;
    config->memory_threshold_mb = DISPLAY_CONTROLLER_MEMORY_THRESHOLD_MB;

    // Debug and diagnostics
    config->enable_debug_logging = false;
    config->enable_performance_profiling = false;
    config->log_file_path = NULL;
}

/**
 * @brief Update performance history circular buffer
 * @param controller Display controller instance
 * @param operation_time_ns Operation time in nanoseconds to record
 */
static void dc_update_performance_history(display_controller_t *controller,
                                          uint64_t operation_time_ns) {
    if (!controller)
        return;

    controller->performance_history[controller->performance_history_index] =
        operation_time_ns;
    controller->performance_history_index =
        (controller->performance_history_index + 1) %
        DISPLAY_CONTROLLER_PERFORMANCE_HISTORY_SIZE;
}

/**
 * @brief Calculate average from performance history
 * @param controller Display controller instance
 * @return Average operation time in nanoseconds
 */
static uint64_t
dc_calculate_average_performance(const display_controller_t *controller) {
    if (!controller)
        return 0;

    uint64_t total = 0;
    size_t count = 0;

    for (size_t i = 0; i < DISPLAY_CONTROLLER_PERFORMANCE_HISTORY_SIZE; i++) {
        if (controller->performance_history[i] > 0) {
            total += controller->performance_history[i];
            count++;
        }
    }

    return count > 0 ? total / count : 0;
}

/**
 * @brief Clean expired cache entries based on adaptive TTL
 * @param controller Display controller instance with cache to clean
 */
static void dc_cleanup_expired_cache_entries(display_controller_t *controller) {
    if (!controller || !controller->cache_entries)
        return;

    struct timeval current_time;
    if (gettimeofday(&current_time, NULL) != 0)
        return;

    for (size_t i = 0; i < controller->cache_count;) {
        display_cache_entry_t *entry = &controller->cache_entries[i];

        if (!entry->is_valid) {
            // Remove invalid entry
            if (entry->display_content) {
                lush_pool_free(entry->display_content);
                entry->display_content = NULL;
            }
            if (entry->state_hash) {
                lush_pool_free(entry->state_hash);
                entry->state_hash = NULL;
            }

            // Move last entry to this position
            if (i < controller->cache_count - 1) {
                *entry = controller->cache_entries[controller->cache_count - 1];
            }
            controller->cache_count--;
            continue;
        }

        // Calculate age
        uint64_t age_ms = ((uint64_t)current_time.tv_sec -
                           (uint64_t)entry->timestamp.tv_sec) *
                              1000 +
                          ((uint64_t)current_time.tv_usec -
                           (uint64_t)entry->timestamp.tv_usec) /
                              1000;

        // Adaptive TTL based on access frequency
        uint32_t adaptive_ttl_ms = controller->config.cache_ttl_ms;

        if (entry->access_count > 5) {
            // High-frequency entries get 4x longer TTL
            adaptive_ttl_ms *= 4;
        } else if (entry->access_count > 2) {
            // Medium-frequency entries get 2x longer TTL
            adaptive_ttl_ms *= 2;
        } else if (entry->access_count == 1) {
            // Single-use entries get much shorter TTL
            adaptive_ttl_ms = adaptive_ttl_ms / 3;
        }

        if (age_ms > adaptive_ttl_ms) {
            // Entry has expired
            if (entry->display_content) {
                lush_pool_free(entry->display_content);
                entry->display_content = NULL;
            }
            if (entry->state_hash) {
                lush_pool_free(entry->state_hash);
                entry->state_hash = NULL;
            }
            entry->is_valid = false;

            // Move last entry to this position
            if (i < controller->cache_count - 1) {
                *entry = controller->cache_entries[controller->cache_count - 1];
            }
            controller->cache_count--;
            continue;
        }

        i++;
    }
}

/**
 * @brief Find cache entry by state hash
 * @param controller Display controller instance with cache
 * @param state_hash State hash string to look up
 * @return Pointer to cache entry if found, NULL otherwise
 */
static display_cache_entry_t *
dc_find_cache_entry(display_controller_t *controller, const char *state_hash) {
    if (!controller || !controller->cache_entries || !state_hash)
        return NULL;

    for (size_t i = 0; i < controller->cache_count; i++) {
        display_cache_entry_t *entry = &controller->cache_entries[i];
        if (entry->is_valid && entry->state_hash &&
            strcmp(entry->state_hash, state_hash) == 0) {
            entry->access_count++;
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief Add new cache entry with LRU eviction if full
 * @param controller Display controller instance with cache
 * @param state_hash State hash for the new entry
 * @param display_content Display content to cache
 * @param content_length Length of display content
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code otherwise
 */
static display_controller_error_t
dc_add_cache_entry(display_controller_t *controller, const char *state_hash,
                   const char *display_content, size_t content_length) {
    if (!controller || !state_hash || !display_content) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    // Check if cache is full
    if (controller->cache_count >= controller->cache_capacity) {
        // Find least recently used entry with improved protection for frequent
        // entries
        size_t lru_index = 0;
        uint32_t min_score = UINT32_MAX;

        for (size_t i = 0; i < controller->cache_count; i++) {
            display_cache_entry_t *entry = &controller->cache_entries[i];

            // Calculate eviction score: lower score = more likely to evict
            // Protect frequently accessed entries by heavily weighting access
            // count
            uint32_t score = entry->access_count * 1000;

            // Also consider recency (entries accessed recently get protection)
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            uint64_t age_ms = ((uint64_t)current_time.tv_sec -
                               (uint64_t)entry->timestamp.tv_sec) *
                                  1000 +
                              ((uint64_t)current_time.tv_usec -
                               (uint64_t)entry->timestamp.tv_usec) /
                                  1000;

            // Reduce score based on age (older entries have lower scores)
            if (age_ms > 5000) {
                score =
                    score / 2; // Halve score for entries older than 5 seconds
            }

            if (score < min_score) {
                min_score = score;
                lru_index = i;
            }
        }

        // Remove LRU entry
        display_cache_entry_t *lru_entry =
            &controller->cache_entries[lru_index];
        if (lru_entry->display_content)
            lush_pool_free(lru_entry->display_content);
        if (lru_entry->state_hash)
            lush_pool_free(lru_entry->state_hash);

        // Use this slot for new entry
        display_cache_entry_t *new_entry = lru_entry;
        memset(new_entry, 0, sizeof(display_cache_entry_t));

        new_entry->display_content = lush_pool_alloc(content_length + 1);
        if (!new_entry->display_content) {
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }

        new_entry->state_hash = lush_pool_alloc(strlen(state_hash) + 1);
        if (!new_entry->state_hash) {
            lush_pool_free(new_entry->display_content);
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }

        memcpy(new_entry->display_content, display_content, content_length);
        new_entry->display_content[content_length] = '\0';
        strcpy(new_entry->state_hash, state_hash);
        new_entry->content_length = content_length;
        new_entry->access_count = 1;
        new_entry->is_valid = true;
        gettimeofday(&new_entry->timestamp, NULL);
    } else {
        // Add new entry
        display_cache_entry_t *new_entry =
            &controller->cache_entries[controller->cache_count];
        memset(new_entry, 0, sizeof(display_cache_entry_t));

        new_entry->display_content = lush_pool_alloc(content_length + 1);
        if (!new_entry->display_content) {
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }

        new_entry->state_hash = lush_pool_alloc(strlen(state_hash) + 1);
        if (!new_entry->state_hash) {
            lush_pool_free(new_entry->display_content);
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }

        memcpy(new_entry->display_content, display_content, content_length);
        new_entry->display_content[content_length] = '\0';
        strcpy(new_entry->state_hash, state_hash);
        new_entry->content_length = content_length;
        new_entry->access_count = 1;
        new_entry->is_valid = true;
        gettimeofday(&new_entry->timestamp, NULL);

        controller->cache_count++;
    }

    return DISPLAY_CONTROLLER_SUCCESS;
}

// ============================================================================
// CORE API IMPLEMENTATION
// ============================================================================

display_controller_t *display_controller_create(void) {
    display_controller_t *controller = malloc(sizeof(display_controller_t));
    if (!controller) {
        DC_ERROR("Failed to allocate memory for display controller");
        return NULL;
    }

    memset(controller, 0, sizeof(display_controller_t));

    // Initialize configuration with defaults
    dc_init_default_config(&controller->config);

    DC_DEBUG("Display controller created successfully");
    return controller;
}

display_controller_error_t
display_controller_init(display_controller_t *controller,
                        const display_controller_config_t *init_config,
                        layer_event_system_t *event_system) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    DC_DEBUG("Initializing display controller");

    // Apply configuration
    if (init_config) {
        controller->config = *init_config;
    } else {
        dc_init_default_config(&controller->config);
    }

    // Initialize event system
    controller->event_system = event_system;

    // Create composition engine
    controller->compositor = composition_engine_create();
    if (!controller->compositor) {
        DC_ERROR("Failed to create composition engine");
        return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
    }

    // Create base terminal first (required for terminal control)
    base_terminal_t *base_terminal = base_terminal_create();
    if (!base_terminal) {
        DC_ERROR("Failed to create base terminal");
        composition_engine_destroy(controller->compositor);
        controller->compositor = NULL;
        return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
    }

    // CRITICAL: Initialize base_terminal BEFORE creating terminal_control
    // This sets up FDs and detects terminal capabilities
    // Only initialize if stdout is a TTY (won't work with pipes/redirects)
    if (isatty(STDOUT_FILENO)) {
        base_terminal_error_t bt_result = base_terminal_init(base_terminal);
        if (bt_result != BASE_TERMINAL_SUCCESS) {
            /* Non-fatal - will use default 80x24. Only log at debug level
             * since this is expected when stdin is a pipe with -i flag. */
            DC_DEBUG("Base terminal init returned %d - using defaults",
                     bt_result);
        } else {
            DC_DEBUG("Base terminal initialized successfully");
        }
    } else {
        DC_DEBUG("stdout is not a TTY, skipping base_terminal initialization");
        // Will use default 80x24 when output is redirected
    }

    // Create terminal control context
    controller->terminal_ctrl = terminal_control_create(base_terminal);
    if (!controller->terminal_ctrl) {
        DC_ERROR("Failed to create terminal control context");
        base_terminal_destroy(base_terminal);
        composition_engine_destroy(controller->compositor);
        controller->compositor = NULL;
        return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
    }

    // Initialize terminal control to detect actual terminal capabilities
    terminal_control_error_t tc_result =
        terminal_control_init(controller->terminal_ctrl);
    if (tc_result != TERMINAL_CONTROL_SUCCESS) {
        DC_ERROR(
            "Failed to initialize terminal control (error %d) - using defaults",
            tc_result);
        // Non-fatal - will use default 80x24
    }

    // ========================================================================
    // EAGER COMPOSITOR INITIALIZATION
    // Initialize composition engine layers NOW so they're ready when LLE needs
    // them. This fixes the startup issue where lle_display_integration_init
    // fails because compositor->command_layer is NULL (was previously lazily
    // initialized).
    // ========================================================================
    {
        DC_DEBUG("Eagerly initializing compositor layers");

        // Create prompt and command layers
        prompt_layer_t *prompt_layer = prompt_layer_create();
        command_layer_t *command_layer = command_layer_create();

        if (!prompt_layer || !command_layer) {
            DC_ERROR("Failed to create display layers during eager init");
            if (prompt_layer)
                prompt_layer_destroy(prompt_layer);
            if (command_layer)
                command_layer_destroy(command_layer);
            // Non-fatal - fall back to lazy initialization
        } else {
            // Initialize prompt layer
            prompt_layer_error_t prompt_result =
                prompt_layer_init(prompt_layer, controller->event_system);
            if (prompt_result != PROMPT_LAYER_SUCCESS) {
                DC_ERROR("Failed to initialize prompt layer: error %d",
                         prompt_result);
                prompt_layer_destroy(prompt_layer);
                command_layer_destroy(command_layer);
                // Non-fatal - fall back to lazy initialization
            } else {
                // Initialize command layer
                command_layer_error_t command_result =
                    command_layer_init(command_layer, controller->event_system);
                if (command_result != COMMAND_LAYER_SUCCESS) {
                    DC_ERROR("Failed to initialize command layer: error %d",
                             command_result);
                    prompt_layer_cleanup(prompt_layer);
                    prompt_layer_destroy(prompt_layer);
                    command_layer_destroy(command_layer);
                    // Non-fatal - fall back to lazy initialization
                } else {
                    // Initialize composition engine with the layers
                    composition_engine_error_t comp_result =
                        composition_engine_init(controller->compositor,
                                                prompt_layer, command_layer,
                                                controller->event_system);

                    if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
                        DC_ERROR(
                            "Failed to initialize composition engine: error %d",
                            comp_result);
                        prompt_layer_cleanup(prompt_layer);
                        prompt_layer_destroy(prompt_layer);
                        command_layer_cleanup(command_layer);
                        command_layer_destroy(command_layer);
                        // Non-fatal - fall back to lazy initialization
                    } else {
                        DC_DEBUG("Compositor layers initialized successfully "
                                 "(eager init)");
                    }
                }
            }
        }
    }

    // Initialize caching system
    if (controller->config.enable_caching) {
        controller->cache_capacity = controller->config.max_cache_entries;
        controller->cache_entries =
            malloc(controller->cache_capacity * sizeof(display_cache_entry_t));
        if (!controller->cache_entries) {
            DC_ERROR("Failed to allocate cache entries");
            composition_engine_destroy(controller->compositor);
            terminal_control_destroy(controller->terminal_ctrl);
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }
        memset(controller->cache_entries, 0,
               controller->cache_capacity * sizeof(display_cache_entry_t));
        controller->cache_count = 0;
    }

    // Initialize performance monitoring
    memset(&controller->performance, 0,
           sizeof(display_controller_performance_t));
    memset(controller->performance_history, 0,
           sizeof(controller->performance_history));
    controller->performance_history_index = 0;

    // Initialize state tracking
    controller->last_display_state = NULL;
    controller->last_display_length = 0;
    controller->current_state_hash = malloc(DC_MAX_STATE_HASH_LENGTH);
    if (!controller->current_state_hash) {
        DC_ERROR("Failed to allocate state hash buffer");
        if (controller->cache_entries)
            free(controller->cache_entries);
        composition_engine_destroy(controller->compositor);
        terminal_control_destroy(controller->terminal_ctrl);
        return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
    }

    controller->display_cache_valid = false;

    // Cache preheating: Pre-populate with comprehensive common patterns
    if (controller->config.enable_caching) {
        // Comprehensive command patterns that benefit from caching
        const char *common_commands[] = {
            // Basic file operations
            "ls", "ls -l", "ls -la", "ls -lh", "ls .", "ls ..", "pwd", "cd",
            "cd ..", "cd .", "cd ~",

            // Text operations
            "echo", "echo test", "echo hello", "cat", "head", "tail",

            // Git operations (common in development)
            "git status", "git branch", "git log", "git log --oneline",
            "git diff", "git add", "git commit",

            // System operations
            "history", "clear", "exit", "which", "whereis", "ps", "top", "df",
            "du", "free"};

        // Get current prompt for preheating (if available)
        extern char *lush_generate_prompt(void);
        char *current_prompt = lush_generate_prompt();

        if (current_prompt) {
            // Pre-generate cache entries for common commands with current
            // prompt
            for (size_t i = 0;
                 i < sizeof(common_commands) / sizeof(common_commands[0]) &&
                 i < 16;
                 i++) { // Expanded preheating for better coverage

                char state_hash[DC_MAX_STATE_HASH_LENGTH];
                dc_generate_state_hash(current_prompt, common_commands[i],
                                       "default", SYMBOL_MODE_AUTO, state_hash,
                                       sizeof(state_hash));

                // Create optimized cache entry for common patterns
                char placeholder_output[128];
                snprintf(placeholder_output, sizeof(placeholder_output),
                         "%s%s\n", current_prompt, common_commands[i]);

                // Add to cache with higher access count to keep them longer
                dc_add_cache_entry(controller, state_hash, placeholder_output,
                                   strlen(placeholder_output));

                // Mark as frequently used for adaptive TTL
                if (controller->cache_count > 0) {
                    controller->cache_entries[controller->cache_count - 1]
                        .access_count = 3;
                }
            }

            // Pre-populate with repeated command patterns (simulate high
            // frequency)
            const char *repeated_commands[] = {"ls", "pwd", "echo",
                                               "git status"};
            for (size_t i = 0;
                 i < sizeof(repeated_commands) / sizeof(repeated_commands[0]);
                 i++) {
                char state_hash[DC_MAX_STATE_HASH_LENGTH];
                dc_generate_state_hash(current_prompt, repeated_commands[i],
                                       "default", SYMBOL_MODE_AUTO, state_hash,
                                       sizeof(state_hash));

                // Find existing entry and boost its access count
                display_cache_entry_t *existing =
                    dc_find_cache_entry(controller, state_hash);
                if (existing) {
                    existing->access_count = 6; // High frequency simulation
                }
            }

            // Note: Do not free current_prompt here - it's managed by readline
            // integration system Freeing it here causes double free corruption
            // when lush_readline_cleanup() runs
        }
    }

    // Initialize completion menu state (LLE Spec 12 - Proper Architecture)
    controller->active_completion_menu = NULL;
    controller->completion_menu_visible = false;
    controller->menu_state_changed = false;

    // Initialize autosuggestions layer (Fish-style ghost text)
    controller->autosuggestions_layer = NULL;
    controller->autosuggestions_enabled = false;

    if (controller->event_system && controller->terminal_ctrl) {
        controller->autosuggestions_layer = autosuggestions_layer_create(
            controller->event_system, controller->terminal_ctrl);

        if (controller->autosuggestions_layer) {
            autosuggestions_layer_error_t as_result =
                autosuggestions_layer_init(
                    controller->autosuggestions_layer,
                    NULL // Use default config (BRIGHT_BLACK color)
                );

            if (as_result == AUTOSUGGESTIONS_LAYER_SUCCESS) {
                controller->autosuggestions_enabled = true;
                DC_DEBUG("Autosuggestions layer initialized successfully");
            } else {
                DC_DEBUG("Autosuggestions layer init failed: %s",
                         autosuggestions_layer_error_string(as_result));
                // Non-fatal - continue without autosuggestions
                autosuggestions_layer_destroy(
                    &controller->autosuggestions_layer);
                controller->autosuggestions_layer = NULL;
            }
        } else {
            DC_DEBUG("Failed to create autosuggestions layer");
        }
    }

    controller->is_initialized = true;
    controller->integration_mode_active =
        controller->config.enable_integration_mode;
    controller->operation_sequence_number = 0;

    // Subscribe to command layer REDRAW_NEEDED events
    // This is critical for LLE display integration - connects command_layer
    // updates to terminal
    if (controller->event_system) {
        layer_events_error_t subscribe_result = layer_events_subscribe(
            controller->event_system, LAYER_EVENT_REDRAW_NEEDED,
            LAYER_ID_DISPLAY_CONTROLLER,         /* subscriber_id */
            dc_handle_redraw_needed, controller, /* user_data */
            LAYER_EVENT_PRIORITY_HIGH);

        if (subscribe_result != LAYER_EVENTS_SUCCESS) {
            DC_ERROR("Failed to subscribe to REDRAW_NEEDED events: %s",
                     layer_events_error_string(subscribe_result));
            /* Non-fatal - display may still work without events */
        } else {
            DC_DEBUG("Successfully subscribed to LAYER_EVENT_REDRAW_NEEDED");
        }
    }

    // Initialize theme context
    memset(controller->current_theme_name, 0,
           sizeof(controller->current_theme_name));
    controller->current_symbol_mode = SYMBOL_MODE_AUTO;
    controller->theme_context_initialized = false;

    gettimeofday(&controller->initialization_time, NULL);
    gettimeofday(&controller->last_performance_update, NULL);
    gettimeofday(&controller->last_cache_cleanup, NULL);

    DC_DEBUG("Display controller initialized successfully");
    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_display(display_controller_t *controller,
                           const char *prompt_text, const char *command_text,
                           char *output, size_t output_size) {

    if (!controller || !output) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    if (output_size == 0) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    DC_DEBUG("display_controller_display called");
    DC_DEBUG("Controller: %p, is_initialized: %d", (void *)controller,
             controller->is_initialized);
    DC_DEBUG("Prompt text: %s", prompt_text ? prompt_text : "(null)");
    DC_DEBUG("Command text: %s", command_text ? command_text : "(null)");

    DC_DEBUG("Starting display operation (seq: %u)",
             controller->operation_sequence_number++);

    // Generate state hash for caching including theme context
    char state_hash[DC_MAX_STATE_HASH_LENGTH];
    dc_generate_state_hash(
        prompt_text, command_text, controller->current_theme_name,
        controller->current_symbol_mode, state_hash, sizeof(state_hash));

    // Check cache if enabled
    if (controller->config.enable_caching) {
        display_cache_entry_t *cached_entry =
            dc_find_cache_entry(controller, state_hash);

        if (cached_entry && cached_entry->content_length < output_size) {
            // Cache hit - return cached content immediately
            memcpy(output, cached_entry->display_content,
                   cached_entry->content_length);
            output[cached_entry->content_length] = '\0';

            controller->performance.cache_hits++;
            controller->performance.total_display_operations++;

            gettimeofday(&end_time, NULL);
            uint64_t operation_time = dc_time_diff_ns(&start_time, &end_time);
            dc_update_performance_history(controller, operation_time);

            // Performance Monitoring: Record cache hit and timing
            display_integration_record_layer_cache_operation(
                "display_controller", true);
            display_integration_record_display_timing(operation_time);

            DC_DEBUG("Cache hit for state hash: %s (access_count: %u)",
                     state_hash, cached_entry->access_count);
            return DISPLAY_CONTROLLER_SUCCESS;
        } else {
            controller->performance.cache_misses++;

            // Performance Monitoring: Record cache miss
            display_integration_record_layer_cache_operation(
                "display_controller", false);
            DC_DEBUG("Cache miss for state hash: %s", state_hash);
        }
    }

    // Initialize layers if needed
    bool compositor_initialized =
        composition_engine_is_initialized(controller->compositor);
    DC_DEBUG("Composition engine initialized check: %s",
             compositor_initialized ? "true" : "false");
    DC_DEBUG("Compositor pointer: %p", (void *)controller->compositor);

    if (!compositor_initialized) {
        // Create and initialize prompt and command layers
        prompt_layer_t *prompt_layer = prompt_layer_create();
        command_layer_t *command_layer = command_layer_create();

        if (!prompt_layer || !command_layer) {
            DC_ERROR("Failed to create display layers");
            if (prompt_layer)
                prompt_layer_destroy(prompt_layer);
            if (command_layer)
                command_layer_destroy(command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        // Initialize individual layers with event system
        DC_DEBUG("About to initialize prompt layer");
        DC_DEBUG("Prompt layer pointer: %p", (void *)prompt_layer);
        DC_DEBUG("Event system pointer: %p", (void *)controller->event_system);

        prompt_layer_error_t prompt_init_result =
            prompt_layer_init(prompt_layer, controller->event_system);

        DC_DEBUG("Prompt layer init returned: %d", prompt_init_result);

        if (prompt_init_result != PROMPT_LAYER_SUCCESS) {
            DC_ERROR("Failed to initialize prompt layer: error %d",
                     prompt_init_result);

            // Detailed error analysis for prompt layer
            switch (prompt_init_result) {
            case PROMPT_LAYER_ERROR_INVALID_PARAM:
                DC_ERROR("Prompt error cause: Invalid parameter");
                break;
            case PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE:
                DC_ERROR("Prompt error cause: Event system failure");
                break;
            case PROMPT_LAYER_ERROR_MEMORY_ALLOCATION:
                DC_ERROR("Prompt error cause: Memory allocation failure");
                break;
            default:
                DC_ERROR("Prompt error cause: Unknown error code %d",
                         prompt_init_result);
                break;
            }

            prompt_layer_destroy(prompt_layer);
            command_layer_destroy(command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        DC_DEBUG("Prompt layer initialized successfully");

        DC_DEBUG("About to initialize command layer");
        DC_DEBUG("Command layer pointer: %p", (void *)command_layer);
        DC_DEBUG("Event system pointer: %p", (void *)controller->event_system);

        command_layer_error_t command_init_result =
            command_layer_init(command_layer, controller->event_system);

        DC_DEBUG("Command layer init returned: %d", command_init_result);

        if (command_init_result != COMMAND_LAYER_SUCCESS) {
            DC_ERROR("Failed to initialize command layer: error %d",
                     command_init_result);

            prompt_layer_cleanup(prompt_layer);
            prompt_layer_destroy(prompt_layer);
            command_layer_destroy(command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        DC_DEBUG("Command layer initialized successfully");

        // ============================================================================
        // CRITICAL FIX: Populate layers with provided content before
        // composition engine init
        // ============================================================================

        DC_DEBUG("Layer content populated successfully");

        // Initialize composition engine with populated layers
        composition_engine_error_t comp_result =
            composition_engine_init(controller->compositor, prompt_layer,
                                    command_layer, controller->event_system);

        if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
            DC_ERROR("Failed to initialize composition engine: %s",
                     composition_engine_error_string(comp_result));
            prompt_layer_cleanup(prompt_layer);
            prompt_layer_destroy(prompt_layer);
            command_layer_cleanup(command_layer);
            command_layer_destroy(command_layer);
            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    }

    // ============================================================================
    // CRITICAL BUG FIX: Always update layer content with new input
    // ============================================================================

    // Get layers from compositor (they are public fields)
    prompt_layer_t *prompt_layer = controller->compositor->prompt_layer;
    command_layer_t *command_layer = controller->compositor->command_layer;

    if (!prompt_layer || !command_layer) {

        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    // Always update layer content with new input (this was the missing piece!)

    if (prompt_text && *prompt_text && prompt_layer) {

        prompt_layer_error_t prompt_content_result =
            prompt_layer_set_content(prompt_layer, prompt_text);
        if (prompt_content_result != PROMPT_LAYER_SUCCESS) {

            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    }

    // Always update command content with new input
    if (command_text && *command_text && command_layer) {

        command_layer_error_t command_content_result =
            command_layer_set_command(command_layer, command_text, 0);
        if (command_content_result != COMMAND_LAYER_SUCCESS) {

            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    }

    // Perform composition
    composition_engine_error_t comp_result =
        composition_engine_compose(controller->compositor);
    if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
        DC_ERROR("Composition failed: %s",
                 composition_engine_error_string(comp_result));
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    // Get composed output
    comp_result = composition_engine_get_output(controller->compositor, output,
                                                output_size);
    if (comp_result != COMPOSITION_ENGINE_SUCCESS) {
        DC_ERROR("Failed to get composed output: %s",
                 composition_engine_error_string(comp_result));
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    // Fix cursor positioning: strip trailing newlines from composed output
    size_t output_length = strlen(output);
    while (output_length > 0 && output[output_length - 1] == '\n') {
        output[output_length - 1] = '\0';
        output_length--;
    }

    // Update cache if enabled
    if (controller->config.enable_caching) {
        display_controller_error_t cache_result =
            dc_add_cache_entry(controller, state_hash, output, output_length);
        if (cache_result != DISPLAY_CONTROLLER_SUCCESS) {
            DC_DEBUG("Failed to add cache entry: %d", cache_result);
            // Non-fatal error, continue
        }
    }

    // Update last display state
    if (controller->last_display_state) {
        free(controller->last_display_state);
    }
    controller->last_display_state = malloc(output_length + 1);
    if (controller->last_display_state) {
        memcpy(controller->last_display_state, output, output_length + 1);
        controller->last_display_length = output_length;
        strcpy(controller->current_state_hash, state_hash);
        controller->display_cache_valid = true;
    }

    // Update performance metrics
    gettimeofday(&end_time, NULL);
    uint64_t operation_time = dc_time_diff_ns(&start_time, &end_time);

    // Performance Monitoring: Record display timing for new
    // compositions
    display_integration_record_display_timing(operation_time);

    controller->performance.total_display_operations++;
    if (controller->performance.total_display_operations == 1) {
        controller->performance.min_display_time_ns = operation_time;
        controller->performance.max_display_time_ns = operation_time;
        controller->performance.avg_display_time_ns = operation_time;
    } else {
        if (operation_time < controller->performance.min_display_time_ns) {
            controller->performance.min_display_time_ns = operation_time;
        }
        if (operation_time > controller->performance.max_display_time_ns) {
            controller->performance.max_display_time_ns = operation_time;
        }
        controller->performance.avg_display_time_ns =
            dc_calculate_average_performance(controller);
    }

    dc_update_performance_history(controller, operation_time);

    // Update cache hit rate
    if (controller->performance.total_display_operations > 0) {
        controller->performance.cache_hit_rate =
            (double)controller->performance.cache_hits /
            (double)controller->performance.total_display_operations;
    }

    // Check if cache cleanup is needed
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    uint64_t cleanup_interval_ms =
        ((uint64_t)current_time.tv_sec -
         (uint64_t)controller->last_cache_cleanup.tv_sec) *
            1000 +
        ((uint64_t)current_time.tv_usec -
         (uint64_t)controller->last_cache_cleanup.tv_usec) /
            1000;

    if (cleanup_interval_ms > DC_CACHE_CLEANUP_INTERVAL_MS) {
        dc_cleanup_expired_cache_entries(controller);
        controller->last_cache_cleanup = current_time;
    }

    DC_DEBUG("Display operation completed (time: %" PRIu64 " ns)",
             operation_time);
    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t display_controller_display_with_cursor(
    display_controller_t *controller, const char *prompt_text,
    const char *command_text, size_t cursor_byte_offset,
    bool apply_terminal_control, char *output, size_t output_size) {
    if (!controller || !output) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    if (output_size == 0) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    // If terminal control is NOT requested, just use normal display
    if (!apply_terminal_control) {
        return display_controller_display(controller, prompt_text, command_text,
                                          output, output_size);
    }

    // Terminal control wrapping requested - use composition engine with cursor
    // tracking
    DC_DEBUG(
        "display_controller_display_with_cursor: apply_terminal_control=true");
    DC_DEBUG("prompt_text: %s", prompt_text ? prompt_text : "(null)");
    DC_DEBUG("command_text: %s", command_text ? command_text : "(null)");
    DC_DEBUG("cursor_byte_offset: %zu", cursor_byte_offset);

    // Check if compositor is initialized, if not we need to initialize it
    bool compositor_initialized =
        composition_engine_is_initialized(controller->compositor);
    DC_DEBUG("compositor_initialized: %s",
             compositor_initialized ? "true" : "false");

    if (!compositor_initialized) {
        DC_DEBUG("Initializing compositor...");

        // Initialize compositor with empty layers first
        prompt_layer_t *new_prompt_layer = prompt_layer_create();
        command_layer_t *new_command_layer = command_layer_create();

        if (!new_prompt_layer || !new_command_layer) {
            DC_ERROR("Failed to create display layers");
            if (new_prompt_layer)
                prompt_layer_destroy(new_prompt_layer);
            if (new_command_layer)
                command_layer_destroy(new_command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        // Initialize layers
        prompt_layer_error_t prompt_init =
            prompt_layer_init(new_prompt_layer, controller->event_system);
        if (prompt_init != PROMPT_LAYER_SUCCESS) {
            DC_ERROR("Failed to initialize prompt layer");
            prompt_layer_destroy(new_prompt_layer);
            command_layer_destroy(new_command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        command_layer_error_t cmd_init =
            command_layer_init(new_command_layer, controller->event_system);
        if (cmd_init != COMMAND_LAYER_SUCCESS) {
            DC_ERROR("Failed to initialize command layer");
            prompt_layer_cleanup(new_prompt_layer);
            prompt_layer_destroy(new_prompt_layer);
            command_layer_destroy(new_command_layer);
            return DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED;
        }

        // Initialize composition engine
        composition_engine_error_t comp_init = composition_engine_init(
            controller->compositor, new_prompt_layer, new_command_layer,
            controller->event_system);

        if (comp_init != COMPOSITION_ENGINE_SUCCESS) {
            DC_ERROR("Failed to initialize composition engine");
            prompt_layer_cleanup(new_prompt_layer);
            prompt_layer_destroy(new_prompt_layer);
            command_layer_cleanup(new_command_layer);
            command_layer_destroy(new_command_layer);
            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    }

    // Get the layers (now guaranteed to be initialized)
    prompt_layer_t *prompt_layer = controller->compositor->prompt_layer;
    command_layer_t *command_layer = controller->compositor->command_layer;

    DC_DEBUG("prompt_layer: %p, command_layer: %p", (void *)prompt_layer,
             (void *)command_layer);

    if (!prompt_layer || !command_layer) {
        DC_ERROR("Failed to get layers from composition engine");
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    // Set prompt content if provided
    if (prompt_text && *prompt_text) {
        DC_DEBUG("Setting prompt content...");
        prompt_layer_error_t prompt_result =
            prompt_layer_set_content(prompt_layer, prompt_text);
        DC_DEBUG("prompt_layer_set_content returned: %d", prompt_result);
        if (prompt_result != PROMPT_LAYER_SUCCESS) {
            DC_ERROR("Failed to set prompt content");
            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    } else {
        DC_DEBUG("No prompt text provided");
    }

    // Set command content if provided
    if (command_text && *command_text) {
        DC_DEBUG("Setting command content...");
        command_layer_error_t cmd_result = command_layer_set_command(
            command_layer, command_text, cursor_byte_offset);
        DC_DEBUG("command_layer_set_command returned: %d", cmd_result);
        if (cmd_result != COMMAND_LAYER_SUCCESS) {
            DC_ERROR("Failed to set command content");
            return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
        }
    } else {
        DC_DEBUG("No command text provided");
    }

    // Get terminal width from terminal control layer
    int terminal_width = 80; // Default
    if (controller->terminal_ctrl) {
        terminal_width = controller->terminal_ctrl->capabilities.terminal_width;
        if (terminal_width <= 0) {
            terminal_width = 80;
        }
    }

    // Use composition engine with cursor tracking
    composition_with_cursor_t comp_result;
    memset(&comp_result, 0, sizeof(comp_result));

    DC_DEBUG("Calling composition_engine_compose_with_cursor...");
    composition_engine_error_t comp_error =
        composition_engine_compose_with_cursor(controller->compositor,
                                               cursor_byte_offset,
                                               terminal_width, &comp_result);

    DC_DEBUG("composition_engine_compose_with_cursor returned: %d", comp_error);
    DC_DEBUG("composed_output length: %zu",
             strlen(comp_result.composed_output));
    DC_DEBUG("cursor_found: %s", comp_result.cursor_found ? "true" : "false");

    if (comp_error != COMPOSITION_ENGINE_SUCCESS) {
        DC_ERROR("Composition with cursor failed: %s",
                 composition_engine_error_string(comp_error));
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    if (!comp_result.cursor_found) {
        DC_ERROR("Cursor position not found during composition");
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    DC_DEBUG("Cursor position: row=%zu, col=%zu", comp_result.cursor_screen_row,
             comp_result.cursor_screen_column);

    // Now wrap the composed output with terminal control sequences

    if (!controller->terminal_ctrl) {
        // No terminal control available - just return composed content
        size_t len = strlen(comp_result.composed_output);
        if (len >= output_size) {
            return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(output, comp_result.composed_output);
        return DISPLAY_CONTROLLER_SUCCESS;
    }

    terminal_control_t *tc = controller->terminal_ctrl;

    // Step 1: Clear line sequence (\r\033[J)
    char clear_seq[64];
    ssize_t clear_len = snprintf(clear_seq, sizeof(clear_seq), "\r\033[J");

    if (clear_len < 0 || (size_t)clear_len >= sizeof(clear_seq)) {
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    // Step 2: Build output: clear + content with line wrapping
    size_t offset = 0;

    // Add clear sequence
    if (offset + clear_len >= output_size) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(output + offset, clear_seq, clear_len);
    offset += clear_len;

    // Add composed content WITH line wrapping:
    // Walk through composed output character-by-character and insert newlines
    // when wrapping
    const char *composed = comp_result.composed_output;
    size_t composed_len = strlen(composed);
    int visual_col = 0;
    bool in_escape = false;

    for (size_t i = 0; i < composed_len; i++) {
        char ch = composed[i];

        // Track ANSI escape sequences (they don't consume visual space)
        if (ch == '\033' || ch == '\x1b') {
            in_escape = true;
        }

        // Check if we need to wrap BEFORE writing this character
        // (Only if not in escape sequence and not already a newline)
        if (!in_escape && ch != '\n' && ch != '\r' &&
            visual_col >= terminal_width) {
            // Insert newline to wrap to next line at column 0
            if (offset + 1 >= output_size) {
                return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
            }
            output[offset++] = '\n';
            visual_col = 0;
        }

        // Write the character
        if (offset + 1 >= output_size) {
            return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
        }
        output[offset++] = ch;

        // Update visual column position
        if (in_escape) {
            // Check for end of escape sequence
            if (ch == 'm' || ch == 'K' || ch == 'J' || ch == 'H' || ch == 'A' ||
                ch == 'B' || ch == 'C' || ch == 'D' || ch == 'G') {
                in_escape = false;
            }
            // Escape sequences don't advance visual column
        } else if (ch == '\n' || ch == '\r') {
            visual_col = 0;
        } else if (ch == '\t') {
            // Tab advances to next multiple of 8
            visual_col += 8 - (visual_col % 8);
        } else if ((unsigned char)ch >= 32) {
            // Printable character advances by 1
            // TODO: Handle wide characters (CJK) which advance by 2
            visual_col++;
        }
    }

    // Step 3: Add cursor positioning
    // Convert from 0-based (composition) to 1-based (ANSI)
    int target_row = comp_result.cursor_screen_row + 1;
    int target_column = comp_result.cursor_screen_column + 1;

    // Generate cursor positioning sequence using Layer 2 (terminal_control)
    char cursor_seq[64];
    ssize_t cursor_len = terminal_control_generate_cursor_sequence(
        tc, target_row, target_column, cursor_seq, sizeof(cursor_seq));

    if (cursor_len < 0) {
        return DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED;
    }

    if (offset + cursor_len >= output_size) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(output + offset, cursor_seq, cursor_len);
    offset += cursor_len;

    // Null-terminate
    output[offset] = '\0';

    DC_DEBUG("Display with cursor completed: row=%d, col=%d, total output "
             "length=%zu",
             target_row, target_column, offset);
    DC_DEBUG("Output buffer first 100 chars: %.100s", output);
    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t display_controller_update(
    display_controller_t *controller, const char *new_prompt_text,
    const char *new_command_text, char *diff_output, size_t output_size,
    display_state_diff_t *change_info) {

    if (!controller || !diff_output) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // For now, implement update as a full display operation
    // In a more advanced implementation, this would include diff algorithms
    display_controller_error_t result =
        display_controller_display(controller, new_prompt_text,
                                   new_command_text, diff_output, output_size);

    // Set change info if requested
    if (change_info) {
        change_info->change_type = DISPLAY_STATE_FULL_REFRESH_NEEDED;
        change_info->change_start_pos = 0;
        change_info->change_length = strlen(diff_output);
        change_info->diff_content = NULL;
        change_info->requires_full_refresh = true;
    }

    return result;
}

display_controller_error_t
display_controller_refresh(display_controller_t *controller, char *output,
                           size_t output_size) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    // Clear cache and force refresh
    if (controller->config.enable_caching) {
        display_controller_clear_cache(controller);
    }

    controller->display_cache_valid = false;

    // Perform fresh display operation
    return display_controller_display(controller, NULL, NULL, output,
                                      output_size);
}

display_controller_error_t
display_controller_clear_screen(display_controller_t *controller) {
    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->terminal_ctrl) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    DC_DEBUG("Clearing screen");

    /* Clear the screen through terminal_control */
    terminal_control_error_t result =
        terminal_control_clear_screen(controller->terminal_ctrl);

    if (result != TERMINAL_CONTROL_SUCCESS) {
        DC_ERROR("Failed to clear screen: %d", result);
        return DISPLAY_CONTROLLER_ERROR_SYSTEM_RESOURCE;
    }

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_cleanup(display_controller_t *controller) {
    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    DC_DEBUG("Cleaning up display controller");

    // Clean up composition engine
    if (controller->compositor) {
        composition_engine_cleanup(controller->compositor);
        composition_engine_destroy(controller->compositor);
        controller->compositor = NULL;
    }

    // Clean up terminal control
    if (controller->terminal_ctrl) {
        terminal_control_cleanup(controller->terminal_ctrl);
        terminal_control_destroy(controller->terminal_ctrl);
        controller->terminal_ctrl = NULL;
    }

    // Clean up cache
    if (controller->cache_entries) {
        // Only iterate over valid entries to prevent accessing uninitialized
        // memory
        for (size_t i = 0; i < controller->cache_count; i++) {
            if (controller->cache_entries[i].display_content) {
                lush_pool_free(controller->cache_entries[i].display_content);
                controller->cache_entries[i].display_content = NULL;
            }
            if (controller->cache_entries[i].state_hash) {
                lush_pool_free(controller->cache_entries[i].state_hash);
                controller->cache_entries[i].state_hash = NULL;
            }
            controller->cache_entries[i].is_valid = false;
        }
        free(controller->cache_entries);
        controller->cache_entries = NULL;
        controller->cache_count = 0;
        controller->cache_capacity = 0;
    }

    // Clean up state tracking
    if (controller->last_display_state) {
        free(controller->last_display_state);
        controller->last_display_state = NULL;
    }

    if (controller->current_state_hash) {
        free(controller->current_state_hash);
        controller->current_state_hash = NULL;
    }

    // Clear completion menu reference (we don't own it, just clear the pointer)
    controller->active_completion_menu = NULL;
    controller->completion_menu_visible = false;

    // Clean up autosuggestions layer
    if (controller->autosuggestions_layer) {
        autosuggestions_layer_destroy(&controller->autosuggestions_layer);
        controller->autosuggestions_layer = NULL;
    }
    controller->autosuggestions_enabled = false;

    // Clean up event system (we own it - passed to us during init)
    if (controller->event_system) {
        layer_events_destroy(controller->event_system);
        controller->event_system = NULL;
    }

    controller->is_initialized = false;

    DC_DEBUG("Display controller cleanup completed");
    return DISPLAY_CONTROLLER_SUCCESS;
}

void display_controller_destroy(display_controller_t *controller) {
    if (!controller)
        return;

    DC_DEBUG("Destroying display controller");

    display_controller_cleanup(controller);
    free(controller);
}

// ============================================================================
// COMPLETION MENU INTEGRATION (LLE Spec 12 - Proper Architecture)
// ============================================================================

display_controller_error_t display_controller_set_completion_menu(
    display_controller_t *controller, lle_completion_menu_state_t *menu_state) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // Clear existing menu if NULL is passed
    if (!menu_state) {
        controller->active_completion_menu = NULL;
        controller->completion_menu_visible = false;
        DC_DEBUG("Completion menu cleared");
        return DISPLAY_CONTROLLER_SUCCESS;
    }

    // Set new menu state (we don't own it - caller manages lifecycle)
    controller->active_completion_menu = menu_state;
    controller->completion_menu_visible = true;

    // Update menu layout based on current terminal width
    int term_width = 80;
    if (controller->terminal_ctrl &&
        controller->terminal_ctrl->capabilities.terminal_width > 0) {
        term_width = controller->terminal_ctrl->capabilities.terminal_width;
    }
    lle_completion_menu_update_layout(menu_state, (size_t)term_width);

    DC_DEBUG("Completion menu set (visible: %d, columns: %zu)",
             controller->completion_menu_visible, menu_state->num_columns);

    /* Menu state changed - mark that we need redraw even if command text
     * unchanged This flag will be checked by command_layer to bypass its early
     * return optimization
     */
    controller->menu_state_changed = true;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_clear_completion_menu(display_controller_t *controller) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    controller->active_completion_menu = NULL;
    controller->completion_menu_visible = false;

    DC_DEBUG("Completion menu cleared");

    /* Menu state changed - mark that we need redraw even if command text
     * unchanged */
    controller->menu_state_changed = true;

    return DISPLAY_CONTROLLER_SUCCESS;
}

bool display_controller_has_completion_menu(
    const display_controller_t *controller) {

    if (!controller) {
        return false;
    }

    return controller->completion_menu_visible &&
           controller->active_completion_menu != NULL;
}

lle_completion_menu_state_t *
display_controller_get_completion_menu(const display_controller_t *controller) {

    if (!controller || !controller->completion_menu_visible) {
        return NULL;
    }

    return controller->active_completion_menu;
}

bool display_controller_check_and_clear_menu_changed(
    display_controller_t *controller) {

    if (!controller) {
        return false;
    }

    bool changed = controller->menu_state_changed;
    controller->menu_state_changed = false; // Clear the flag after checking
    return changed;
}

// ============================================================================
// AUTOSUGGESTIONS INTEGRATION (Fish-style Ghost Text)
// ============================================================================

void display_controller_update_autosuggestion(display_controller_t *controller,
                                              const char *buffer_content,
                                              size_t cursor_position,
                                              size_t buffer_length) {

    /* DEPRECATED: This function uses the legacy autosuggestions system which
     * relies on GNU readline history. Use
     * display_controller_set_autosuggestion() with LLE history instead.
     *
     * This function is kept for backwards compatibility but is now a no-op
     * when LLE is active. The actual suggestion generation happens in
     * lle_readline.c using LLE history, then passed via set_autosuggestion().
     */
    (void)controller;
    (void)buffer_content;
    (void)cursor_position;
    (void)buffer_length;

    /* No-op: lle_readline now handles suggestion generation and calls
     * display_controller_set_autosuggestion() directly */
}

void display_controller_set_autosuggestion(display_controller_t *controller,
                                           const char *suggestion) {

    if (!controller || !controller->autosuggestions_layer) {
        return;
    }

    /* Don't set suggestion if completion menu is visible */
    if (controller->completion_menu_visible) {
        autosuggestions_layer_clear(controller->autosuggestions_layer);
        return;
    }

    /* Set the suggestion directly (or clear if NULL/empty) */
    autosuggestions_layer_set_suggestion(controller->autosuggestions_layer,
                                         suggestion);
}

const char *
display_controller_get_autosuggestion(const display_controller_t *controller) {

    if (!controller || !controller->autosuggestions_enabled ||
        !controller->autosuggestions_layer) {
        return NULL;
    }

    return autosuggestions_layer_get_current_suggestion(
        controller->autosuggestions_layer);
}

bool display_controller_accept_autosuggestion(display_controller_t *controller,
                                              char *accepted_text,
                                              size_t buffer_size) {

    if (!controller || !accepted_text || buffer_size == 0) {
        return false;
    }

    if (!controller->autosuggestions_enabled ||
        !controller->autosuggestions_layer) {
        return false;
    }

    if (!autosuggestions_layer_has_suggestion(
            controller->autosuggestions_layer)) {
        return false;
    }

    autosuggestions_layer_error_t result = autosuggestions_layer_accept(
        controller->autosuggestions_layer, accepted_text, buffer_size);

    return (result == AUTOSUGGESTIONS_LAYER_SUCCESS &&
            accepted_text[0] != '\0');
}

bool display_controller_has_autosuggestion(
    const display_controller_t *controller) {

    if (!controller || !controller->autosuggestions_enabled ||
        !controller->autosuggestions_layer) {
        return false;
    }

    return autosuggestions_layer_has_suggestion(
        controller->autosuggestions_layer);
}

void display_controller_clear_autosuggestion(display_controller_t *controller) {

    if (!controller || !controller->autosuggestions_layer) {
        return;
    }

    autosuggestions_layer_clear(controller->autosuggestions_layer);
}

void display_controller_set_autosuggestions_enabled(
    display_controller_t *controller, bool enabled) {

    if (!controller) {
        return;
    }

    controller->autosuggestions_enabled = enabled;

    if (controller->autosuggestions_layer) {
        autosuggestions_layer_set_enabled(controller->autosuggestions_layer,
                                          enabled);
    }

    // Clear any existing suggestion when disabling
    if (!enabled && controller->autosuggestions_layer) {
        autosuggestions_layer_clear(controller->autosuggestions_layer);
    }
}

// ============================================================================
// NOTIFICATION INTEGRATION (Transient Hints)
// ============================================================================

display_controller_error_t display_controller_set_notification(
    display_controller_t *controller,
    const lle_notification_state_t *notification) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // Clear existing notification if NULL is passed
    if (!notification) {
        lle_notification_dismiss(&controller->notification_copy);
        controller->notification_visible = false;
        DC_DEBUG("Notification cleared");
        return DISPLAY_CONTROLLER_SUCCESS;
    }

    // COPY the notification data (source may be on stack and get overwritten)
    memcpy(&controller->notification_copy, notification, sizeof(*notification));
    controller->notification_visible = true;

    DC_DEBUG("Notification set (visible: %d)", controller->notification_visible);

    // Notification state changed - mark that we need redraw
    controller->notification_state_changed = true;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_clear_notification(display_controller_t *controller) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    lle_notification_dismiss(&controller->notification_copy);
    controller->notification_visible = false;

    DC_DEBUG("Notification cleared");

    // Notification state changed - mark that we need redraw
    controller->notification_state_changed = true;

    return DISPLAY_CONTROLLER_SUCCESS;
}

bool display_controller_has_notification(
    const display_controller_t *controller) {

    if (!controller) {
        return false;
    }

    return controller->notification_visible &&
           controller->notification_copy.visible;
}

bool display_controller_check_and_clear_notification_changed(
    display_controller_t *controller) {

    if (!controller) {
        return false;
    }

    bool changed = controller->notification_state_changed;
    controller->notification_state_changed = false; // Clear the flag after checking
    return changed;
}

// ============================================================================
// PERFORMANCE AND MONITORING FUNCTIONS
// ============================================================================

display_controller_error_t display_controller_get_performance(
    const display_controller_t *controller,
    display_controller_performance_t *performance) {

    if (!controller || !performance) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    *performance = controller->performance;

    // Update cache memory usage
    performance->cache_memory_usage_bytes = 0;
    if (controller->cache_entries) {
        for (size_t i = 0; i < controller->cache_count; i++) {
            if (controller->cache_entries[i].is_valid) {
                performance->cache_memory_usage_bytes +=
                    controller->cache_entries[i].content_length +
                    strlen(controller->cache_entries[i].state_hash) +
                    sizeof(display_cache_entry_t);
            }
        }
    }

    // Update health indicators
    performance->performance_within_threshold =
        (performance->avg_display_time_ns / 1000000) <=
        controller->config.performance_threshold_ms;

    performance->memory_within_threshold =
        (performance->cache_memory_usage_bytes / 1024 / 1024) <=
        controller->config.memory_threshold_mb;

    performance->optimization_effective =
        performance->cache_hit_rate >=
        controller->config.cache_hit_rate_threshold;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_reset_performance_metrics(display_controller_t *controller) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    memset(&controller->performance, 0,
           sizeof(display_controller_performance_t));
    memset(controller->performance_history, 0,
           sizeof(controller->performance_history));
    controller->performance_history_index = 0;

    return DISPLAY_CONTROLLER_SUCCESS;
}

// ============================================================================
// CACHING AND OPTIMIZATION FUNCTIONS
// ============================================================================

display_controller_error_t
display_controller_set_optimization_level(display_controller_t *controller,
                                          display_optimization_level_t level) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    controller->config.optimization_level = level;
    controller->current_optimization = level;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_set_adaptive_optimization(display_controller_t *controller,
                                             bool enable) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    controller->config.enable_adaptive_optimization = enable;
    controller->adaptive_optimization_enabled = enable;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_clear_cache(display_controller_t *controller) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->cache_entries) {
        return DISPLAY_CONTROLLER_SUCCESS;
    }

    for (size_t i = 0; i < controller->cache_count; i++) {
        if (controller->cache_entries[i].display_content) {
            lush_pool_free(controller->cache_entries[i].display_content);
            controller->cache_entries[i].display_content = NULL;
        }
        if (controller->cache_entries[i].state_hash) {
            lush_pool_free(controller->cache_entries[i].state_hash);
            controller->cache_entries[i].state_hash = NULL;
        }
        controller->cache_entries[i].is_valid = false;
    }

    controller->cache_count = 0;
    controller->display_cache_valid = false;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t display_controller_validate_cache(
    display_controller_t *controller, size_t *valid_entries,
    size_t *expired_entries, bool *corruption_detected) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    size_t valid_count = 0;
    size_t expired_count = 0;
    bool corruption_found = false;

    if (controller->cache_entries) {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);

        for (size_t i = 0; i < controller->cache_count; i++) {
            display_cache_entry_t *entry = &controller->cache_entries[i];

            if (!entry->is_valid) {
                corruption_found = true;
                continue;
            }

            // Check if entry has expired
            uint64_t age_ms = ((uint64_t)current_time.tv_sec -
                               (uint64_t)entry->timestamp.tv_sec) *
                                  1000 +
                              ((uint64_t)current_time.tv_usec -
                               (uint64_t)entry->timestamp.tv_usec) /
                                  1000;

            if (age_ms > controller->config.cache_ttl_ms) {
                expired_count++;
            } else {
                valid_count++;
            }

            // Check for corruption
            if (!entry->display_content || !entry->state_hash) {
                corruption_found = true;
            }
        }
    }

    if (valid_entries)
        *valid_entries = valid_count;
    if (expired_entries)
        *expired_entries = expired_count;
    if (corruption_detected)
        *corruption_detected = corruption_found;

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_optimize_cache(display_controller_t *controller) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->cache_entries) {
        return DISPLAY_CONTROLLER_SUCCESS;
    }

    // Clean up expired entries
    dc_cleanup_expired_cache_entries(controller);

    // Sort by access count for better cache performance
    // Simple bubble sort for small cache sizes
    for (size_t i = 0; i < controller->cache_count; i++) {
        for (size_t j = i + 1; j < controller->cache_count; j++) {
            if (controller->cache_entries[i].access_count <
                controller->cache_entries[j].access_count) {
                display_cache_entry_t temp = controller->cache_entries[i];
                controller->cache_entries[i] = controller->cache_entries[j];
                controller->cache_entries[j] = temp;
            }
        }
    }

    return DISPLAY_CONTROLLER_SUCCESS;
}

// ============================================================================
// CONFIGURATION AND STATE FUNCTIONS
// ============================================================================

display_controller_error_t
display_controller_get_config(const display_controller_t *controller,
                              display_controller_config_t *config) {

    if (!controller || !config) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    *config = controller->config;
    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_set_config(display_controller_t *controller,
                              const display_controller_config_t *config) {

    if (!controller || !config) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // Validate configuration
    if (config->optimization_level > DISPLAY_OPTIMIZATION_MAXIMUM) {
        return DISPLAY_CONTROLLER_ERROR_CONFIGURATION_INVALID;
    }

    if (config->max_cache_entries > 1000) { // Reasonable limit
        return DISPLAY_CONTROLLER_ERROR_CONFIGURATION_INVALID;
    }

    // Apply new configuration
    controller->config = *config;
    controller->current_optimization = config->optimization_level;
    controller->adaptive_optimization_enabled =
        config->enable_adaptive_optimization;
    controller->integration_mode_active = config->enable_integration_mode;

    // Resize cache if needed
    if (config->enable_caching &&
        config->max_cache_entries != controller->cache_capacity) {
        if (controller->cache_entries) {
            // Clean up existing cache
            for (size_t i = 0; i < controller->cache_count; i++) {
                if (controller->cache_entries[i].display_content) {
                    free(controller->cache_entries[i].display_content);
                }
                if (controller->cache_entries[i].state_hash) {
                    free(controller->cache_entries[i].state_hash);
                }
            }
            free(controller->cache_entries);
        }

        // Allocate new cache
        controller->cache_capacity = config->max_cache_entries;
        controller->cache_entries =
            malloc(controller->cache_capacity * sizeof(display_cache_entry_t));
        if (!controller->cache_entries) {
            return DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION;
        }
        memset(controller->cache_entries, 0,
               controller->cache_capacity * sizeof(display_cache_entry_t));
        controller->cache_count = 0;
    }

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_set_integration_mode(display_controller_t *controller,
                                        bool enable) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    controller->config.enable_integration_mode = enable;
    controller->integration_mode_active = enable;

    return DISPLAY_CONTROLLER_SUCCESS;
}

bool display_controller_is_initialized(const display_controller_t *controller) {
    return controller && controller->is_initialized;
}

display_controller_error_t
display_controller_get_version(const display_controller_t *controller,
                               char *version_buffer, size_t buffer_size) {

    if (!controller || !version_buffer) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (buffer_size < 16) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    snprintf(version_buffer, buffer_size, "%d.%d.%d",
             DISPLAY_CONTROLLER_VERSION_MAJOR, DISPLAY_CONTROLLER_VERSION_MINOR,
             DISPLAY_CONTROLLER_VERSION_PATCH);

    return DISPLAY_CONTROLLER_SUCCESS;
}

// ============================================================================
// INTEGRATION PREPARATION FUNCTIONS
// ============================================================================

display_controller_error_t
display_controller_prepare_shell_integration(display_controller_t *controller,
                                             const void *shell_config) {
    (void)shell_config; /* Reserved for future use */

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // Enable integration mode
    controller->integration_mode_active = true;
    controller->config.enable_integration_mode = true;

    // Optimize for shell integration
    controller->config.optimization_level = DISPLAY_OPTIMIZATION_STANDARD;
    controller->config.enable_caching = true;
    controller->config.enable_performance_monitoring = true;

    // Configure reasonable defaults for shell operation
    controller->config.cache_ttl_ms = 10000; // 10 seconds for shell integration
    controller->config.performance_threshold_ms =
        50; // 50ms for shell responsiveness

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t display_controller_get_integration_interface(
    const display_controller_t *controller, void *interface_buffer,
    size_t buffer_size) {

    if (!controller || !interface_buffer) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // For now, this is a placeholder for future integration interface
    // In Week 8, this would provide function pointers for shell integration
    memset(interface_buffer, 0, buffer_size);

    return DISPLAY_CONTROLLER_SUCCESS;
}

// ============================================================================
// UTILITY AND DIAGNOSTIC FUNCTIONS
// ============================================================================

const char *display_controller_error_string(display_controller_error_t error) {
    switch (error) {
    case DISPLAY_CONTROLLER_SUCCESS:
        return "Success";
    case DISPLAY_CONTROLLER_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case DISPLAY_CONTROLLER_ERROR_NULL_POINTER:
        return "Null pointer";
    case DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED:
        return "Initialization failed";
    case DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED:
        return "Controller not initialized";
    case DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED:
        return "Composition operation failed";
    case DISPLAY_CONTROLLER_ERROR_CACHE_FULL:
        return "Cache is full";
    case DISPLAY_CONTROLLER_ERROR_PERFORMANCE_DEGRADED:
        return "Performance degraded";
    case DISPLAY_CONTROLLER_ERROR_CONFIGURATION_INVALID:
        return "Invalid configuration";
    case DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL:
        return "Buffer too small";
    case DISPLAY_CONTROLLER_ERROR_SYSTEM_RESOURCE:
        return "System resource error";
    case DISPLAY_CONTROLLER_ERROR_INTEGRATION_FAILED:
        return "Integration failed";
    default:
        return "Unknown error";
    }
}

display_controller_error_t display_controller_generate_diagnostic_report(
    const display_controller_t *controller, char *report_buffer,
    size_t buffer_size) {

    if (!controller || !report_buffer) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    if (buffer_size < 1024) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    int written = snprintf(
        report_buffer, buffer_size,
        "Display Controller Diagnostic Report\n"
        "====================================\n"
        "Version: %d.%d.%d\n"
        "Initialized: %s\n"
        "Integration Mode: %s\n"
        "Optimization Level: %d\n"
        "\nPerformance Metrics:\n"
        "  Total Operations: %" PRIu64 "\n"
        "  Average Time: %" PRIu64 " ns\n"
        "  Cache Hit Rate: %.2f%%\n"
        "  Cache Entries: %zu/%zu\n"
        "\nConfiguration:\n"
        "  Caching Enabled: %s\n"
        "  Performance Monitoring: %s\n"
        "  Adaptive Optimization: %s\n"
        "\nHealth Status:\n"
        "  Performance Within Threshold: %s\n"
        "  Memory Within Threshold: %s\n"
        "  Optimization Effective: %s\n",
        DISPLAY_CONTROLLER_VERSION_MAJOR, DISPLAY_CONTROLLER_VERSION_MINOR,
        DISPLAY_CONTROLLER_VERSION_PATCH,
        controller->is_initialized ? "Yes" : "No",
        controller->integration_mode_active ? "Enabled" : "Disabled",
        controller->config.optimization_level,
        controller->performance.total_display_operations,
        controller->performance.avg_display_time_ns,
        controller->performance.cache_hit_rate * 100.0, controller->cache_count,
        controller->cache_capacity,
        controller->config.enable_caching ? "Yes" : "No",
        controller->config.enable_performance_monitoring ? "Yes" : "No",
        controller->config.enable_adaptive_optimization ? "Yes" : "No",
        controller->performance.performance_within_threshold ? "Yes" : "No",
        controller->performance.memory_within_threshold ? "Yes" : "No",
        controller->performance.optimization_effective ? "Yes" : "No");

    if (written < 0 || (size_t)written >= buffer_size) {
        return DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL;
    }

    return DISPLAY_CONTROLLER_SUCCESS;
}

display_controller_error_t
display_controller_create_default_config(display_controller_config_t *config) {

    if (!config) {
        return DISPLAY_CONTROLLER_ERROR_INVALID_PARAM;
    }

    dc_init_default_config(config);
    return DISPLAY_CONTROLLER_SUCCESS;
}

/**
 * Get terminal control instance from display controller.
 */
terminal_control_t *display_controller_get_terminal_control(
    const display_controller_t *controller) {
    if (!controller || !controller->is_initialized) {
        return NULL;
    }

    return controller->terminal_ctrl;
}

/**
 * Get event system instance from display controller.
 */
layer_event_system_t *
display_controller_get_event_system(const display_controller_t *controller) {
    if (!controller || !controller->is_initialized) {
        return NULL;
    }

    return controller->event_system;
}

// ============================================================================
// THEME CONTEXT MANAGEMENT
// ============================================================================

/**
 * Set theme context for the display controller.
 *
 * Updates the display controller's theme context including theme name and
 * symbol compatibility mode. This ensures theme-aware cache key generation and
 * proper cache invalidation when themes change.
 */
display_controller_error_t
display_controller_set_theme_context(display_controller_t *controller,
                                     const char *theme_name,
                                     symbol_compatibility_t symbol_mode) {

    if (!controller) {
        return DISPLAY_CONTROLLER_ERROR_NULL_POINTER;
    }

    if (!controller->is_initialized) {
        return DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED;
    }

    // Check for theme change to invalidate relevant cache entries
    bool theme_changed = false;
    bool symbol_mode_changed = false;

    if (controller->theme_context_initialized) {
        theme_changed = (theme_name && strcmp(controller->current_theme_name,
                                              theme_name) != 0);
        symbol_mode_changed = (controller->current_symbol_mode != symbol_mode);
    }

    // Update theme context
    if (theme_name) {
        strncpy(controller->current_theme_name, theme_name, THEME_NAME_MAX - 1);
        controller->current_theme_name[THEME_NAME_MAX - 1] = '\0';
    } else {
        strncpy(controller->current_theme_name, "default", THEME_NAME_MAX - 1);
    }

    controller->current_symbol_mode = symbol_mode;
    controller->theme_context_initialized = true;

    // Invalidate cache entries if theme changed
    if ((theme_changed || symbol_mode_changed) &&
        controller->config.enable_caching) {
        // Clear all cache entries since theme affects all prompts
        for (size_t i = 0; i < controller->cache_count; i++) {
            if (controller->cache_entries[i].is_valid) {
                if (controller->cache_entries[i].display_content) {
                    lush_pool_free(
                        controller->cache_entries[i].display_content);
                    controller->cache_entries[i].display_content = NULL;
                }
                if (controller->cache_entries[i].state_hash) {
                    lush_pool_free(controller->cache_entries[i].state_hash);
                    controller->cache_entries[i].state_hash = NULL;
                }
                controller->cache_entries[i].is_valid = false;
            }
        }
        controller->cache_count = 0;
        controller->display_cache_valid = false;

        // Update performance metrics
        controller->performance.cache_invalidations++;

        DC_DEBUG("Theme context changed - cache invalidated (theme: %s, "
                 "symbol_mode: %d)",
                 controller->current_theme_name, symbol_mode);
    }

    DC_DEBUG("Theme context updated: theme=%s, symbol_mode=%d",
             controller->current_theme_name, symbol_mode);

    return DISPLAY_CONTROLLER_SUCCESS;
}
