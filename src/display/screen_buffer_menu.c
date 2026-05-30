/**
 * @file screen_buffer_menu.c
 * @brief Below-cursor row management for the virtual screen buffer
 *
 * The canonical pattern for completion menus, notifications, hints, and
 * the debug command-list display is to **append** their content as
 * additional rows below the prompt+command in the same virtual screen
 * buffer (rather than render in a parallel pass), then query the row
 * count below the cursor for the cursor-up math after redraw. This file
 * holds the helpers that implement that pattern:
 *
 *   - screen_buffer_add_text_rows: extend the virtual screen with
 *     plain-text rows starting at the given row index.
 *   - screen_buffer_get_rows_below_cursor: report how many rows of the
 *     virtual screen lie below the cursor row (used by the
 *     display_controller cursor-positioning math).
 *   - screen_buffer_get_total_display_rows: total row count, including
 *     any added below-cursor content.
 *
 * The older parallel-render path (screen_buffer_render_menu /
 * screen_buffer_calculate_menu_width) was deleted; this row-append
 * approach is the canonical one and is referenced from
 * display_controller.c's menu/notification rendering.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/screen_buffer.h"
#include "lle/char_width.h"
#include "lle/utf8_support.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Add plain text rows to screen buffer for menu, hints, etc.
 *
 * This is the key function for proper menu integration per
 * SCREEN_BUFFER_MENU_INTEGRATION_PLAN.md - menu rows become part of
 * the virtual screen so cursor positioning works correctly.
 *
 * @param buffer Screen buffer to add rows to
 * @param start_row Row index to start adding text
 * @param text Plain text with optional ANSI codes to add
 * @return Number of rows added, or -1 on error
 */
int screen_buffer_add_text_rows(screen_buffer_t *buffer, int start_row,
                                const char *text) {
    if (!buffer || !text || start_row < 0 ||
        start_row >= SCREEN_BUFFER_MAX_ROWS) {
        return -1;
    }

    int current_row = start_row;
    int col = 0;
    size_t i = 0;
    size_t text_len = strlen(text);
    int rows_added = 0;

    /// Ensure we have at least the starting row
    if (current_row >= buffer->num_rows) {
        buffer->num_rows = current_row + 1;
    }

    while (i < text_len && current_row < SCREEN_BUFFER_MAX_ROWS) {
        unsigned char ch = (unsigned char)text[i];

        /// Handle ANSI escape sequences (skip, take 0 columns)
        if (ch == '\033' || ch == '\x1b') {
            i++;
            if (i < text_len && text[i] == '[') {
                i++;
                while (i < text_len) {
                    char c = text[i++];
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        c == 'm' || c == 'H' || c == 'J' || c == 'K' ||
                        c == 'G') {
                        break;
                    }
                }
            }
            continue;
        }

        /// Handle newlines
        if (ch == '\n') {
            current_row++;
            col = 0;
            rows_added++;

            if (current_row >= SCREEN_BUFFER_MAX_ROWS) {
                break;
            }

            if (current_row >= buffer->num_rows) {
                buffer->num_rows = current_row + 1;
            }

            i++;
            continue;
        }

        /// Handle regular characters - decode the codepoint and consult
        /// the Unicode East Asian Width property via the shared LLE
        /// primitive. The previous byte-count heuristic (3 bytes => 2
        /// columns) wrongly inflated narrow 3-byte glyphs like the box
        /// drawings U+2500..U+257F, causing frame borders to wrap.
        int char_bytes = 1;
        int visual_width = 1;
        uint32_t codepoint = 0;
        int decoded =
            lle_utf8_decode_codepoint(text + i, text_len - i, &codepoint);
        if (decoded > 0 && codepoint >= 32) {
            char_bytes = decoded;
            int w = lle_codepoint_width(codepoint);
            visual_width = w > 0 ? w : 1;
        } else if ((ch & 0x80) == 0) {
            char_bytes = 1;
            visual_width = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            char_bytes = 2;
            visual_width = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            char_bytes = 3;
            visual_width = 1;
        } else if ((ch & 0xF8) == 0xF0) {
            char_bytes = 4;
            visual_width = 2;
        }

        /// Check for line wrapping before writing
        if (col + visual_width > buffer->terminal_width) {
            current_row++;
            col = 0;
            rows_added++;

            if (current_row >= SCREEN_BUFFER_MAX_ROWS) {
                break;
            }

            if (current_row >= buffer->num_rows) {
                buffer->num_rows = current_row + 1;
            }
        }

        /// Write character to buffer cell
        if (col < SCREEN_BUFFER_MAX_COLS) {
            screen_cell_t *cell = &buffer->lines[current_row].cells[col];

            /// Copy UTF-8 bytes
            for (int b = 0; b < char_bytes && b < 4 && (i + b) < text_len;
                 b++) {
                cell->utf8_bytes[b] = text[i + b];
            }
            /// Zero unused bytes
            for (int b = char_bytes; b < 4; b++) {
                cell->utf8_bytes[b] = '\0';
            }

            cell->byte_len = (uint8_t)char_bytes;
            cell->visual_width = (uint8_t)visual_width;
            cell->is_prompt = false;

            if (col >= buffer->lines[current_row].length) {
                buffer->lines[current_row].length = col + 1;
            }
        }

        col += visual_width;
        i += char_bytes;
    }

    /// Count the current row if we wrote anything to it
    if (col > 0 && rows_added == 0) {
        rows_added = 1;
    } else if (col > 0) {
        /// Last line after final newline
        rows_added++;
    }

    /// Update total_display_rows to track menu
    buffer->total_display_rows = buffer->num_rows;
    buffer->menu_lines = rows_added;

    return rows_added;
}

/**
 * @brief Get total display rows including any added text rows
 * @param buffer Screen buffer to query
 * @return Total number of display rows
 */
int screen_buffer_get_total_display_rows(const screen_buffer_t *buffer) {
    if (!buffer) {
        return 0;
    }
    return buffer->num_rows;
}

/**
 * @brief Calculate rows from cursor to end of display
 *
 * This is critical for cursor positioning after drawing menu:
 * After writing all content (command + menu), we need to move
 * cursor back UP this many rows to reach cursor position.
 *
 * @param buffer Screen buffer to query
 * @return Number of rows below the cursor position
 */
int screen_buffer_get_rows_below_cursor(const screen_buffer_t *buffer) {
    if (!buffer) {
        return 0;
    }

    /// Total rows minus 1 (for 0-indexing) gives last row index.
    /// Cursor is at cursor_row.
    /// Rows below cursor = (last_row) - cursor_row
    ///
    /// Example: num_rows=10 (rows 0-9), cursor at row 3
    /// Rows below = 9 - 3 = 6 (rows 4,5,6,7,8,9)
    int last_row = buffer->num_rows - 1;
    if (last_row < 0)
        last_row = 0;

    int rows_below = last_row - buffer->cursor_row;
    if (rows_below < 0)
        rows_below = 0;

    return rows_below;
}