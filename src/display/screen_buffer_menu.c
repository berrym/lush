/**
 * @file screen_buffer_menu.c
 * @brief Completion Menu Rendering Through Screen Buffer
 *
 * Provides screen_buffer_render_menu() for completion menu rendering
 * through the virtual layout system.
 *
 * Features:
 * - Proper ANSI code handling (menu has syntax highlighting)
 * - Correct column width calculations (no shifting)
 * - Terminal width awareness
 * - UTF-8 and wide character support
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/screen_buffer.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Render completion menu through screen buffer virtual layout
 *
 * Unlike direct terminal writes, this:
 * 1. Parses ANSI codes properly
 * 2. Calculates actual visual width
 * 3. Handles line wrapping correctly
 * 4. Returns proper line count
 *
 * @param buffer Screen buffer to use for layout calculation
 * @param menu_text The rendered menu text with ANSI codes
 * @param terminal_width Current terminal width
 * @return Number of lines the menu occupies
 */
int screen_buffer_render_menu(screen_buffer_t *buffer, const char *menu_text,
                              int terminal_width) {
    if (!buffer || !menu_text || !*menu_text) {
        return 0;
    }

    // Save current screen buffer state
    int saved_row = buffer->cursor_row;
    int saved_col = buffer->cursor_col;
    int saved_num_rows = buffer->num_rows;

    // Start menu on a new line after current content
    buffer->cursor_row = buffer->num_rows;
    buffer->cursor_col = 0;

    /* Use screen buffer's virtual layout to process menu text
     * This handles:
     * - ANSI escape sequences (colors, bold, etc)
     * - UTF-8 characters
     * - Wide characters (emoji, CJK)
     * - Line wrapping at terminal width
     */
    const char *p = menu_text;
    int menu_start_row = buffer->cursor_row;

    while (*p) {
        if (*p == '\033' && p[1] == '[') {
            // ANSI escape sequence - let screen buffer handle it
            const char *seq_end = strchr(p, 'm');
            if (seq_end) {
                /* Screen buffer would normally process this for colors
                 * For now, skip it in position calculations */
                p = seq_end + 1;
                continue;
            }
        }

        if (*p == '\n') {
            // Newline - move to next row
            buffer->cursor_row++;
            buffer->cursor_col = 0;
            if (buffer->cursor_row >= buffer->num_rows) {
                buffer->num_rows = buffer->cursor_row + 1;
            }
            p++;
            continue;
        }

        // Regular character - account for width
        if ((*p & 0x80) == 0) {
            // ASCII character - 1 column
            buffer->cursor_col++;
        } else {
            // UTF-8 character - could be 1 or 2 columns wide
            // Simple approximation for now
            buffer->cursor_col++;
        }

        // Handle line wrapping
        if (buffer->cursor_col >= terminal_width) {
            buffer->cursor_row++;
            buffer->cursor_col = 0;
            if (buffer->cursor_row >= buffer->num_rows) {
                buffer->num_rows = buffer->cursor_row + 1;
            }
        }

        p++;
    }

    // Calculate menu height
    int menu_lines = buffer->cursor_row - menu_start_row + 1;

    // Restore original cursor position (menu doesn't move cursor)
    buffer->cursor_row = saved_row;
    buffer->cursor_col = saved_col;

    /* CRITICAL: Don't permanently modify num_rows!
     * The menu is temporary and shouldn't affect buffer state */
    buffer->num_rows = saved_num_rows;

    return menu_lines;
}

/**
 * @brief Calculate visual width of menu without rendering
 *
 * Useful for determining if menu needs multiple columns.
 *
 * @param menu_text The menu text with ANSI codes
 * @return Maximum visual width of any line in the menu
 */
int screen_buffer_calculate_menu_width(const char *menu_text) {
    if (!menu_text || !*menu_text) {
        return 0;
    }

    int max_width = 0;
    int current_width = 0;
    const char *p = menu_text;

    while (*p) {
        if (*p == '\033' && p[1] == '[') {
            // Skip ANSI sequence
            const char *seq_end = strchr(p, 'm');
            if (seq_end) {
                p = seq_end + 1;
                continue;
            }
        }

        if (*p == '\n') {
            // End of line - check if this line was wider
            if (current_width > max_width) {
                max_width = current_width;
            }
            current_width = 0;
            p++;
            continue;
        }

        // Count visual width
        if ((*p & 0x80) == 0) {
            // ASCII
            current_width++;
        } else if ((*p & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            current_width++;
            p++; // Skip second byte
        } else if ((*p & 0xF0) == 0xE0) {
            // 3-byte UTF-8 (often CJK - 2 columns)
            current_width += 2;
            p += 2; // Skip next 2 bytes
        } else if ((*p & 0xF8) == 0xF0) {
            // 4-byte UTF-8 (emoji - usually 2 columns)
            current_width += 2;
            p += 3; // Skip next 3 bytes
        }

        p++;
    }

    // Check last line
    if (current_width > max_width) {
        max_width = current_width;
    }

    return max_width;
}

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

    // Ensure we have at least the starting row
    if (current_row >= buffer->num_rows) {
        buffer->num_rows = current_row + 1;
    }

    while (i < text_len && current_row < SCREEN_BUFFER_MAX_ROWS) {
        unsigned char ch = (unsigned char)text[i];

        // Handle ANSI escape sequences (skip, take 0 columns)
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

        // Handle newlines
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

        // Handle regular characters - calculate visual width
        int char_bytes = 1;
        int visual_width = 1;

        if ((ch & 0x80) == 0) {
            // ASCII - 1 byte, 1 column
            char_bytes = 1;
            visual_width = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            char_bytes = 2;
            visual_width = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            // 3-byte UTF-8 (often CJK - 2 columns)
            char_bytes = 3;
            visual_width = 2;
        } else if ((ch & 0xF8) == 0xF0) {
            // 4-byte UTF-8 (emoji - usually 2 columns)
            char_bytes = 4;
            visual_width = 2;
        }

        // Check for line wrapping before writing
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

        // Write character to buffer cell
        if (col < SCREEN_BUFFER_MAX_COLS) {
            screen_cell_t *cell = &buffer->lines[current_row].cells[col];

            // Copy UTF-8 bytes
            for (int b = 0; b < char_bytes && b < 4 && (i + b) < text_len;
                 b++) {
                cell->utf8_bytes[b] = text[i + b];
            }
            // Zero unused bytes
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

    // Count the current row if we wrote anything to it
    if (col > 0 && rows_added == 0) {
        rows_added = 1;
    } else if (col > 0) {
        // Last line after final newline
        rows_added++;
    }

    // Update total_display_rows to track menu
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

    /* Total rows minus 1 (for 0-indexing) gives last row index.
     * Cursor is at cursor_row.
     * Rows below cursor = (last_row) - cursor_row
     *
     * Example: num_rows=10 (rows 0-9), cursor at row 3
     * Rows below = 9 - 3 = 6 (rows 4,5,6,7,8,9)
     */
    int last_row = buffer->num_rows - 1;
    if (last_row < 0)
        last_row = 0;

    int rows_below = last_row - buffer->cursor_row;
    if (rows_below < 0)
        rows_below = 0;

    return rows_below;
}