/**
 * @file screen_buffer.c
 * @brief Screen Buffer Management Implementation
 *
 * Virtual screen buffer system for reliable terminal updates.
 * Based on techniques from ZLE (zsh), Fish, and Replxx.
 *
 * Architecture:
 * 1. Maintain a virtual representation of what's on the terminal screen
 * 2. When content changes, render new state into a new virtual screen
 * 3. Diff old vs new virtual screens to find what changed
 * 4. Generate minimal escape sequences to apply only the changes
 *
 * This solves line wrapping issues because we never rely on cursor movement
 * tricks or assumptions about terminal state.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/screen_buffer.h"
#include "config.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

void screen_buffer_init(screen_buffer_t *buffer, int terminal_width) {
    if (!buffer)
        return;

    memset(buffer, 0, sizeof(screen_buffer_t));
    buffer->terminal_width = terminal_width > 0 ? terminal_width : 80;
    buffer->num_rows = 0;
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
    buffer->command_start_row = 0;
    buffer->command_start_col = 0;

    // Initialize menu/overlay tracking fields
    buffer->menu_lines = 0;
    buffer->ghost_text_lines = 0;
    buffer->total_display_rows = 0;
    buffer->command_end_row = 0;
    buffer->command_end_col = 0;

    // Initialize RPROMPT fields
    buffer->rprompt_text[0] = '\0';
    buffer->rprompt_visual_width = 0;
    buffer->rprompt_fits = false;
    buffer->rprompt_col = 0;

    // Initialize all prefix pointers to NULL
    for (int i = 0; i < SCREEN_BUFFER_MAX_ROWS; i++) {
        buffer->lines[i].prefix = NULL;
        buffer->lines[i].prefix_dirty = false;
    }
}

void screen_buffer_clear(screen_buffer_t *buffer) {
    if (!buffer)
        return;

    for (int i = 0; i < buffer->num_rows; i++) {
        buffer->lines[i].length = 0;
        buffer->lines[i].dirty = false;

        // Zero all cells (important for UTF-8 structure with padding)
        memset(buffer->lines[i].cells, 0, sizeof(buffer->lines[i].cells));

        // Note: We do NOT free prefixes here - they persist across clears
        // Use screen_buffer_clear_line_prefix() to explicitly remove prefixes
    }
    buffer->num_rows = 0;
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;

    // Reset menu/overlay tracking fields
    buffer->menu_lines = 0;
    buffer->ghost_text_lines = 0;
    buffer->total_display_rows = 0;
    buffer->command_end_row = 0;
    buffer->command_end_col = 0;

    // Reset RPROMPT fields
    buffer->rprompt_text[0] = '\0';
    buffer->rprompt_visual_width = 0;
    buffer->rprompt_fits = false;
    buffer->rprompt_col = 0;
}

void screen_buffer_cleanup(screen_buffer_t *buffer) {
    if (!buffer)
        return;

    // Free all line prefixes
    for (int i = 0; i < SCREEN_BUFFER_MAX_ROWS; i++) {
        screen_buffer_clear_line_prefix(buffer, i);
    }

    // Clear the buffer
    screen_buffer_clear(buffer);
}

void screen_buffer_copy(screen_buffer_t *dest, const screen_buffer_t *src) {
    if (!dest || !src)
        return;
    memcpy(dest, src, sizeof(screen_buffer_t));
}

// ============================================================================
// TEXT WIDTH CALCULATION
// ============================================================================

void screen_buffer_set_rprompt(screen_buffer_t *buffer,
                               const char *rprompt_text) {
    if (!buffer) {
        return;
    }

    /* Clear RPROMPT state */
    buffer->rprompt_text[0] = '\0';
    buffer->rprompt_visual_width = 0;
    buffer->rprompt_fits = false;
    buffer->rprompt_col = 0;

    if (!rprompt_text || !rprompt_text[0]) {
        return;
    }

    /* Copy RPROMPT text (truncate if necessary) */
    size_t len = strlen(rprompt_text);
    if (len >= sizeof(buffer->rprompt_text)) {
        len = sizeof(buffer->rprompt_text) - 1;
    }
    memcpy(buffer->rprompt_text, rprompt_text, len);
    buffer->rprompt_text[len] = '\0';

    /* Calculate visual width (excludes ANSI escape codes) */
    buffer->rprompt_visual_width =
        (int)screen_buffer_visual_width(rprompt_text, strlen(rprompt_text));

    if (buffer->rprompt_visual_width <= 0) {
        return;
    }

    /* Fit check: RPROMPT fits only on the prompt row (command_start_row).
     *
     * If the command text is still on the same row as the prompt, we must
     * check against command_end_col (the rightmost extent of typed text).
     * If the command has wrapped to subsequent rows, the prompt row has
     * only the prompt itself, so command_start_col is the right boundary.
     *
     * Either way: rightmost_used_col + 1 (gap) + rprompt_width <= term_width
     * The 1-column gap prevents prompt/command text and rprompt from touching.
     */
    int rightmost_col = buffer->command_start_col;
    if (buffer->command_end_row == buffer->command_start_row) {
        /* Command hasn't wrapped — use the actual end of typed text */
        rightmost_col = buffer->command_end_col;
    }

    int space_needed = rightmost_col + 1 + buffer->rprompt_visual_width;
    if (space_needed <= buffer->terminal_width) {
        buffer->rprompt_fits = true;
        buffer->rprompt_col =
            buffer->terminal_width - buffer->rprompt_visual_width;
    }
}

size_t screen_buffer_visual_width(const char *text, size_t byte_length) {
    if (!text)
        return 0;

    size_t visual_width = 0;
    size_t i = 0;
    bool in_escape = false;

    while (i < byte_length && text[i]) {
        unsigned char ch = (unsigned char)text[i];

        // Handle ANSI escape sequences (they take 0 columns)
        if (ch == '\033' || ch == '\x1b') {
            in_escape = true;
            i++;
            continue;
        }

        if (in_escape) {
            i++;
            // Check for escape sequence terminator
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                ch == 'm' || ch == 'H' || ch == 'J' || ch == 'K' || ch == 'G') {
                in_escape = false;
            }
            continue;
        }

        // Skip readline markers \001 and \002
        if (ch == '\001' || ch == '\002') {
            i++;
            continue;
        }

        // Handle UTF-8 multi-byte sequences
        if ((ch & 0x80) == 0) {
            // ASCII: 1 byte, 1 column
            visual_width++;
            i++;
        } else if ((ch & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            visual_width++;
            i += 2;
        } else if ((ch & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            visual_width++;
            i += 3;
        } else if ((ch & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            visual_width++;
            i += 4;
        } else {
            // Invalid UTF-8, skip
            i++;
        }
    }

    return visual_width;
}

// ============================================================================
// RENDERING
// ============================================================================

/**
 * @brief Write a UTF-8 character to screen buffer at current position
 *
 * Handles wrapping to next line automatically when the column exceeds
 * the terminal width.
 *
 * @param buffer Screen buffer to write to
 * @param utf8_bytes UTF-8 byte sequence (1-4 bytes)
 * @param byte_len Number of bytes in the UTF-8 sequence (1-4)
 * @param visual_width Display width in columns (0, 1, or 2)
 * @param is_prompt True if this is part of the prompt
 * @param row Pointer to current row (may be incremented for wrapping)
 * @param col Pointer to current column (may be incremented for wrapping)
 */
static void write_char_to_buffer(screen_buffer_t *buffer,
                                 const char *utf8_bytes, int byte_len,
                                 int visual_width, bool is_prompt, int *row,
                                 int *col) {
    if (!buffer || !utf8_bytes || !row || !col)
        return;
    if (byte_len < 1 || byte_len > 4)
        return;

    // Check if we need to wrap to next line
    if (*col >= buffer->terminal_width) {
        (*row)++;
        *col = 0;

        // Ensure we have enough rows
        if (*row >= SCREEN_BUFFER_MAX_ROWS) {
            return; // Can't write beyond buffer
        }

        if (*row >= buffer->num_rows) {
            buffer->num_rows = *row + 1;
        }
    }

    // Write UTF-8 sequence to buffer
    screen_cell_t *cell = &buffer->lines[*row].cells[*col];
    memcpy(cell->utf8_bytes, utf8_bytes, byte_len);
    cell->byte_len = (uint8_t)byte_len;
    cell->visual_width = (uint8_t)visual_width;
    cell->is_prompt = is_prompt;

    // Zero out unused bytes for cleanliness and deterministic comparison
    for (int i = byte_len; i < 4; i++) {
        cell->utf8_bytes[i] = '\0';
    }

    if (*col >= buffer->lines[*row].length) {
        buffer->lines[*row].length = *col + 1;
    }

    (*col)++;
}

void screen_buffer_render(screen_buffer_t *buffer, const char *prompt_text,
                          const char *command_text, size_t cursor_byte_offset) {
    if (!buffer)
        return;

    // Clear buffer
    screen_buffer_clear(buffer);

    int row = 0;
    int col = 0;
    bool cursor_set = false;

    // Render prompt - calculate visual width (excluding ANSI codes)
    if (prompt_text) {
        size_t i = 0;
        size_t text_len = strlen(prompt_text);
        bool in_readline_marker = false;

        while (i < text_len) {
            unsigned char ch = (unsigned char)prompt_text[i];

            // Handle readline markers \001 and \002
            if (ch == '\001') {
                in_readline_marker = true;
                i++;
                continue;
            }
            if (ch == '\002') {
                in_readline_marker = false;
                i++;
                continue;
            }
            if (in_readline_marker) {
                i++;
                continue;
            }

            // Handle ANSI escape sequences (skip without advancing position)
            if (ch == '\033' || ch == '\x1b') {
                i++;
                if (i < text_len && prompt_text[i] == '[') {
                    i++;
                    while (i < text_len) {
                        char c = prompt_text[i++];
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            c == 'm' || c == 'H' || c == 'J' || c == 'K' ||
                            c == 'G') {
                            break;
                        }
                    }
                }
                continue;
            }

            // Handle newlines in multi-line prompts
            if (ch == '\n') {
                row++;
                col = 0;
                if (row >= buffer->num_rows) {
                    buffer->num_rows = row + 1;
                }
                i++;
                continue;
            }

            // Handle carriage returns (move to start of line without newline)
            if (ch == '\r') {
                col = 0;
                i++;
                continue;
            }

            // Handle tabs
            if (ch == '\t') {
                int tw = config.tab_width > 0 ? config.tab_width : 4;
                size_t tab_width = tw - (col % tw);
                col += tab_width;
                if (col >= buffer->terminal_width) {
                    row++;
                    col = 0;
                    if (row >= buffer->num_rows) {
                        buffer->num_rows = row + 1;
                    }
                }
                i++;
                continue;
            }

            // Decode UTF-8 codepoint for proper width calculation
            uint32_t codepoint;
            int bytes = lle_utf8_decode_codepoint(prompt_text + i, text_len - i,
                                                  &codepoint);

            if (bytes > 0 && codepoint >= 32) {
                int visual_width = lle_utf8_codepoint_width(codepoint);
                if (visual_width > 0) {
                    // Store full UTF-8 sequence
                    write_char_to_buffer(buffer, prompt_text + i, bytes,
                                         visual_width, true, &row, &col);

                    // For wide characters (width=2), we store the character in
                    // one cell but it occupies 2 columns visually, so advance
                    // col by 1 more
                    if (visual_width == 2) {
                        col++;
                        // Handle wrapping for wide characters at boundary
                        if (col >= buffer->terminal_width) {
                            row++;
                            col = 0;
                            if (row >= buffer->num_rows) {
                                buffer->num_rows = row + 1;
                            }
                        }
                    }
                }
                i += bytes;
            } else {
                i++;
            }
        }
    }

    // Save position where command starts (this is where cursor save happens)
    buffer->command_start_row = row;
    buffer->command_start_col = col;

    // Render command text using same approach as display_bridge.c
    if (command_text) {
        size_t i = 0;
        size_t text_len = strlen(command_text);
        size_t bytes_processed =
            0; // Actual bytes in raw text (excludes ANSI codes)

        while (i < text_len) {
            // Check cursor position BEFORE processing next character
            if (!cursor_set && bytes_processed == cursor_byte_offset) {
                buffer->cursor_row = row;
                buffer->cursor_col = col;
                cursor_set = true;
            }

            unsigned char ch = (unsigned char)command_text[i];

            // Handle ANSI escape sequences (skip without advancing
            // bytes_processed or position)
            if (ch == '\033' || ch == '\x1b') {
                i++;
                if (i < text_len && command_text[i] == '[') {
                    i++;
                    while (i < text_len) {
                        char c = command_text[i++];
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            c == 'm' || c == 'H' || c == 'J' || c == 'K' ||
                            c == 'G' || c == 'f' || c == 's' || c == 'u') {
                            break;
                        }
                    }
                }
                // Don't increment bytes_processed - ANSI codes don't count
                continue;
            }

            // Handle newlines
            if (ch == '\n') {
                row++;
                if (row >= buffer->num_rows) {
                    buffer->num_rows = row + 1;
                }

                // CONTINUATION PROMPT SUPPORT:
                // After a newline, check if the next row has a continuation
                // prompt prefix. If it does, start column position after the
                // prefix (not at column 0). This ensures cursor tracking
                // accounts for continuation prompts like "loop> "
                size_t prefix_width =
                    screen_buffer_get_line_prefix_visual_width(buffer, row);
                col = (int)prefix_width;

                i++;
                bytes_processed++;
                continue;
            }

            // Handle tabs
            if (ch == '\t') {
                int tw = config.tab_width > 0 ? config.tab_width : 4;
                size_t tab_width = tw - (col % tw);
                col += tab_width;
                if (col >= buffer->terminal_width) {
                    row++;
                    col = 0;
                    if (row >= buffer->num_rows) {
                        buffer->num_rows = row + 1;
                    }
                }
                i++;
                bytes_processed++;
                continue;
            }

            // Decode UTF-8 codepoint for proper width calculation
            uint32_t codepoint;
            int char_bytes = lle_utf8_decode_codepoint(
                command_text + i, text_len - i, &codepoint);

            if (char_bytes > 0 && codepoint >= 32) {
                int visual_width = lle_utf8_codepoint_width(codepoint);

                if (visual_width > 0) {
                    // Store full UTF-8 sequence
                    write_char_to_buffer(buffer, command_text + i, char_bytes,
                                         visual_width, false, &row, &col);

                    // For wide characters (width=2), we store the character in
                    // one cell but it occupies 2 columns visually, so advance
                    // col by 1 more
                    if (visual_width == 2) {
                        col++;
                        // Handle wrapping for wide characters at boundary
                        if (col >= buffer->terminal_width) {
                            row++;
                            col = 0;
                            if (row >= buffer->num_rows) {
                                buffer->num_rows = row + 1;
                            }
                        }
                    }
                }

                i += char_bytes;
                bytes_processed += char_bytes;
            } else {
                i++;
                bytes_processed++;
            }
        }

        // If cursor is at end of text
        if (!cursor_set && bytes_processed == cursor_byte_offset) {
            buffer->cursor_row = row;
            buffer->cursor_col = col;
            cursor_set = true;
        }

        // Track where command text ends (for menu/ghost text positioning)
        buffer->command_end_row = row;
        buffer->command_end_col = col;
    }

    // Ensure at least one row
    if (buffer->num_rows == 0) {
        buffer->num_rows = 1;
    }

    // Initialize total display rows (will be updated by caller if menu/ghost
    // text added)
    buffer->total_display_rows = buffer->num_rows;
}

void screen_buffer_render_with_continuation(
    screen_buffer_t *buffer, const char *prompt_text, const char *command_text,
    size_t cursor_byte_offset, screen_buffer_continuation_cb continuation_cb,
    void *user_data) {
    if (!buffer)
        return;

    // Clear buffer (prefixes persist across clears per design)
    screen_buffer_clear(buffer);

    // Also clear any old prefixes from previous render
    for (int r = 0; r < SCREEN_BUFFER_MAX_ROWS; r++) {
        screen_buffer_clear_line_prefix(buffer, r);
    }

    int row = 0;
    int col = 0;
    bool cursor_set = false;

    // Render prompt - same as screen_buffer_render
    if (prompt_text) {
        size_t i = 0;
        size_t text_len = strlen(prompt_text);
        bool in_readline_marker = false;

        while (i < text_len) {
            unsigned char ch = (unsigned char)prompt_text[i];

            if (ch == '\001') {
                in_readline_marker = true;
                i++;
                continue;
            }
            if (ch == '\002') {
                in_readline_marker = false;
                i++;
                continue;
            }
            if (in_readline_marker) {
                i++;
                continue;
            }

            if (ch == '\033' || ch == '\x1b') {
                i++;
                if (i < text_len && prompt_text[i] == '[') {
                    i++;
                    while (i < text_len) {
                        char c = prompt_text[i++];
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            c == 'm' || c == 'H' || c == 'J' || c == 'K' ||
                            c == 'G') {
                            break;
                        }
                    }
                }
                continue;
            }

            if (ch == '\n') {
                row++;
                col = 0;
                if (row >= buffer->num_rows) {
                    buffer->num_rows = row + 1;
                }
                i++;
                continue;
            }

            if (ch == '\r') {
                col = 0;
                i++;
                continue;
            }

            if (ch == '\t') {
                int tw = config.tab_width > 0 ? config.tab_width : 4;
                size_t tab_width = tw - (col % tw);
                col += tab_width;
                if (col >= buffer->terminal_width) {
                    row++;
                    col = 0;
                    if (row >= buffer->num_rows) {
                        buffer->num_rows = row + 1;
                    }
                }
                i++;
                continue;
            }

            uint32_t codepoint;
            int bytes = lle_utf8_decode_codepoint(prompt_text + i, text_len - i,
                                                  &codepoint);

            if (bytes > 0 && codepoint >= 32) {
                int visual_width = lle_utf8_codepoint_width(codepoint);
                if (visual_width > 0) {
                    write_char_to_buffer(buffer, prompt_text + i, bytes,
                                         visual_width, true, &row, &col);
                    if (visual_width == 2) {
                        col++;
                        if (col >= buffer->terminal_width) {
                            row++;
                            col = 0;
                            if (row >= buffer->num_rows) {
                                buffer->num_rows = row + 1;
                            }
                        }
                    }
                }
                i += bytes;
            } else {
                i++;
            }
        }
    }

    buffer->command_start_row = row;
    buffer->command_start_col = col;

    // Render command text with continuation prompt support
    if (command_text) {
        size_t i = 0;
        size_t text_len = strlen(command_text);
        size_t bytes_processed = 0;

        // Track current line for continuation callback
        int logical_line = 0;
        (void)logical_line;         /* Reserved for multi-line tracking */
        size_t line_start_byte = 0; // Start of current line in command_text
        (void)line_start_byte;      /* Reserved for line position tracking */

        // Buffer for plain text of current line (ANSI stripped)
        char plain_line[4096];
        size_t plain_pos = 0;
        bool in_ansi = false;

        while (i < text_len) {
            if (!cursor_set && bytes_processed == cursor_byte_offset) {
                buffer->cursor_row = row;
                buffer->cursor_col = col;
                cursor_set = true;
            }

            unsigned char ch = (unsigned char)command_text[i];

            // Handle ANSI escape sequences
            if (ch == '\033' || ch == '\x1b') {
                in_ansi = true;
                i++;
                if (i < text_len && command_text[i] == '[') {
                    i++;
                    while (i < text_len) {
                        char c = command_text[i++];
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            c == 'm' || c == 'H' || c == 'J' || c == 'K' ||
                            c == 'G' || c == 'f' || c == 's' || c == 'u') {
                            in_ansi = false;
                            break;
                        }
                    }
                }
                continue;
            }

            // Handle newlines - this is where we call the continuation callback
            if (ch == '\n') {
                // Null-terminate the plain text buffer for this line
                plain_line[plain_pos] = '\0';

                // Move to next row
                row++;
                if (row >= buffer->num_rows) {
                    buffer->num_rows = row + 1;
                }

                // Call continuation callback to get prompt for the next line
                if (continuation_cb) {
                    const char *cont_prompt = continuation_cb(
                        plain_line, plain_pos, logical_line, user_data);

                    if (cont_prompt) {
                        // Set the prefix on the CURRENT row (which is the new
                        // row after newline)
                        screen_buffer_set_line_prefix(buffer, row, cont_prompt);
                    }
                }

                // Get prefix width for cursor positioning
                size_t prefix_width =
                    screen_buffer_get_line_prefix_visual_width(buffer, row);
                col = (int)prefix_width;

                // Reset for next line
                logical_line++;
                line_start_byte = i + 1;
                plain_pos = 0;

                i++;
                bytes_processed++;
                continue;
            }

            // Accumulate plain text (skip if still in ANSI sequence)
            if (!in_ansi && plain_pos < sizeof(plain_line) - 1) {
                // For non-ANSI characters, add to plain buffer
                int seq_len = lle_utf8_sequence_length(ch);
                if (seq_len > 0 && i + seq_len <= text_len) {
                    for (int j = 0;
                         j < seq_len && plain_pos < sizeof(plain_line) - 1;
                         j++) {
                        plain_line[plain_pos++] = command_text[i + j];
                    }
                }
            }

            // Handle tabs
            if (ch == '\t') {
                int tw = config.tab_width > 0 ? config.tab_width : 4;
                size_t tab_width = tw - (col % tw);
                col += tab_width;
                if (col >= buffer->terminal_width) {
                    row++;
                    col = 0;
                    if (row >= buffer->num_rows) {
                        buffer->num_rows = row + 1;
                    }
                }
                i++;
                bytes_processed++;
                continue;
            }

            // Decode UTF-8 and render
            uint32_t codepoint;
            int char_bytes = lle_utf8_decode_codepoint(
                command_text + i, text_len - i, &codepoint);

            if (char_bytes > 0 && codepoint >= 32) {
                int visual_width = lle_utf8_codepoint_width(codepoint);

                if (visual_width > 0) {
                    write_char_to_buffer(buffer, command_text + i, char_bytes,
                                         visual_width, false, &row, &col);

                    if (visual_width == 2) {
                        col++;
                        if (col >= buffer->terminal_width) {
                            row++;
                            col = 0;
                            if (row >= buffer->num_rows) {
                                buffer->num_rows = row + 1;
                            }
                        }
                    }
                }

                i += char_bytes;
                bytes_processed += char_bytes;
            } else {
                i++;
                bytes_processed++;
            }
        }

        if (!cursor_set && bytes_processed == cursor_byte_offset) {
            buffer->cursor_row = row;
            buffer->cursor_col = col;
            cursor_set = true;
        }

        // Track where command text ends (for menu/ghost text positioning)
        buffer->command_end_row = row;
        buffer->command_end_col = col;
    }

    if (buffer->num_rows == 0) {
        buffer->num_rows = 1;
    }

    // Initialize total display rows (will be updated by caller if menu/ghost
    // text added)
    buffer->total_display_rows = buffer->num_rows;
}

// ============================================================================
// PREFIX SUPPORT FUNCTIONS (Continuation Prompts)
// ============================================================================

bool screen_buffer_set_line_prefix(screen_buffer_t *buffer, int line_num,
                                   const char *prefix_text) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return false;
    }

    if (!prefix_text) {
        // NULL text means clear prefix
        return screen_buffer_clear_line_prefix(buffer, line_num);
    }

    screen_line_t *line = &buffer->lines[line_num];

    // Allocate or reuse prefix structure
    if (!line->prefix) {
        line->prefix =
            (screen_line_prefix_t *)malloc(sizeof(screen_line_prefix_t));
        if (!line->prefix) {
            return false; // Allocation failed
        }
        line->prefix->text = NULL;
    }

    // Free old text if present
    if (line->prefix->text) {
        free(line->prefix->text);
    }

    // Copy prefix text
    line->prefix->text = strdup(prefix_text);
    if (!line->prefix->text) {
        free(line->prefix);
        line->prefix = NULL;
        return false; // Allocation failed
    }

    // Calculate properties
    line->prefix->length = strlen(prefix_text);
    line->prefix->visual_width =
        screen_buffer_calculate_visual_width(prefix_text, 0);
    line->prefix->contains_ansi = (strchr(prefix_text, '\033') != NULL);
    line->prefix->dirty = true;
    line->prefix_dirty = true;

    return true;
}

bool screen_buffer_clear_line_prefix(screen_buffer_t *buffer, int line_num) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return false;
    }

    screen_line_t *line = &buffer->lines[line_num];

    if (line->prefix) {
        if (line->prefix->text) {
            free(line->prefix->text);
        }
        free(line->prefix);
        line->prefix = NULL;
    }

    line->prefix_dirty = true; // Mark as changed (prefix removed)

    return true;
}

const char *screen_buffer_get_line_prefix(const screen_buffer_t *buffer,
                                          int line_num) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return NULL;
    }

    const screen_line_t *line = &buffer->lines[line_num];

    if (line->prefix && line->prefix->text) {
        return line->prefix->text;
    }

    return NULL;
}

size_t screen_buffer_get_line_prefix_visual_width(const screen_buffer_t *buffer,
                                                  int line_num) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return 0;
    }

    const screen_line_t *line = &buffer->lines[line_num];

    if (line->prefix) {
        return line->prefix->visual_width;
    }

    return 0;
}

bool screen_buffer_is_line_prefix_dirty(const screen_buffer_t *buffer,
                                        int line_num) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return false;
    }

    return buffer->lines[line_num].prefix_dirty;
}

void screen_buffer_clear_line_prefix_dirty(screen_buffer_t *buffer,
                                           int line_num) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return;
    }

    buffer->lines[line_num].prefix_dirty = false;
}

int screen_buffer_translate_buffer_to_display_col(const screen_buffer_t *buffer,
                                                  int line_num,
                                                  int buffer_col) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS ||
        buffer_col < 0) {
        return -1;
    }

    size_t prefix_width =
        screen_buffer_get_line_prefix_visual_width(buffer, line_num);

    return (int)prefix_width + buffer_col;
}

int screen_buffer_translate_display_to_buffer_col(const screen_buffer_t *buffer,
                                                  int line_num,
                                                  int display_col) {
    if (!buffer || line_num < 0 || line_num >= SCREEN_BUFFER_MAX_ROWS ||
        display_col < 0) {
        return -1;
    }

    size_t prefix_width =
        screen_buffer_get_line_prefix_visual_width(buffer, line_num);

    // If display column is within prefix area, return 0 (start of content)
    if ((size_t)display_col < prefix_width) {
        return 0;
    }

    return display_col - (int)prefix_width;
}

bool screen_buffer_render_line_with_prefix(const screen_buffer_t *buffer,
                                           int line_num, char *output,
                                           size_t output_size) {
    if (!buffer || !output || line_num < 0 ||
        line_num >= SCREEN_BUFFER_MAX_ROWS) {
        return false;
    }

    const screen_line_t *line = &buffer->lines[line_num];
    size_t pos = 0;

    // Add prefix if present
    if (line->prefix && line->prefix->text) {
        size_t prefix_len = line->prefix->length;
        if (pos + prefix_len >= output_size) {
            return false; // Buffer too small
        }
        memcpy(output + pos, line->prefix->text, prefix_len);
        pos += prefix_len;
    }

    // Add line content (full UTF-8 sequences)
    for (int i = 0; i < line->length && pos < output_size - 1; i++) {
        const screen_cell_t *cell = &line->cells[i];

        // Copy full UTF-8 sequence
        for (int b = 0; b < cell->byte_len && pos < output_size - 1; b++) {
            output[pos++] = cell->utf8_bytes[b];
        }
    }

    output[pos] = '\0';
    return true;
}

bool screen_buffer_render_multiline_with_prefixes(const screen_buffer_t *buffer,
                                                  int start_line, int num_lines,
                                                  char *output,
                                                  size_t output_size) {
    if (!buffer || !output || start_line < 0 || num_lines <= 0) {
        return false;
    }

    if (start_line + num_lines > SCREEN_BUFFER_MAX_ROWS) {
        return false; // Invalid range
    }

    size_t pos = 0;

    for (int i = 0; i < num_lines; i++) {
        int line_num = start_line + i;

        // Render line with prefix
        char line_buffer[SCREEN_BUFFER_MAX_COLS * 2];
        if (!screen_buffer_render_line_with_prefix(
                buffer, line_num, line_buffer, sizeof(line_buffer))) {
            return false;
        }

        // Add to output
        size_t line_len = strlen(line_buffer);
        if (pos + line_len + 1 >= output_size) {
            return false; // Buffer too small
        }

        memcpy(output + pos, line_buffer, line_len);
        pos += line_len;

        // Add newline between lines (except after last line)
        if (i < num_lines - 1) {
            output[pos++] = '\n';
        }
    }

    output[pos] = '\0';
    return true;
}

size_t screen_buffer_calculate_visual_width(const char *text,
                                            size_t start_col) {
    if (!text)
        return 0;

    size_t visual_width = 0;
    size_t col = start_col;
    size_t i = 0;
    size_t text_len = strlen(text);
    bool in_escape = false;

    while (i < text_len) {
        unsigned char ch = (unsigned char)text[i];

        // Handle ANSI escape sequences (they take 0 columns)
        if (ch == '\033' || ch == '\x1b') {
            in_escape = true;
            i++;
            continue;
        }

        if (in_escape) {
            i++;
            // Check for escape sequence terminator
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                ch == 'm' || ch == 'H' || ch == 'J' || ch == 'K' || ch == 'G') {
                in_escape = false;
            }
            continue;
        }

        // Skip readline markers \001 and \002
        if (ch == '\001' || ch == '\002') {
            i++;
            continue;
        }

        // Handle tab expansion
        if (ch == '\t') {
            int tw = config.tab_width > 0 ? config.tab_width : 4;
            size_t tab_width = tw - (col % tw);
            visual_width += tab_width;
            col += tab_width;
            i++;
            continue;
        }

        /* GRAPHEME-AWARE WIDTH CALCULATION
         *
         * Use LLE's full Unicode TR#29 grapheme cluster detection to properly
         * handle complex characters in continuation prompts:
         * - Emoji with modifiers (👨‍👩‍👧‍👦 = family emoji)
         * - ZWJ sequences (🏳️‍🌈 = rainbow flag)
         * - Regional indicator pairs (🇺🇸 = US flag)
         * - Combining marks (é = e + combining acute)
         * - CJK characters (中文 = 2 columns each)
         * - Emoji (🎉 = 2 columns)
         *
         * This allows users to configure continuation prompts with any Unicode:
         *   CONTINUATION_PROMPTS=([loop]="🔄 " [if]="❓ " [quote]="💬 ")
         */

        // Find the end of this grapheme cluster
        const char *grapheme_start = text + i;
        const char *grapheme_end = grapheme_start;

        // Scan forward by UTF-8 characters until we hit a grapheme boundary
        do {
            // Advance to next UTF-8 character
            int char_len =
                lle_utf8_sequence_length((unsigned char)*grapheme_end);
            if (char_len <= 0 || grapheme_end + char_len > text + text_len) {
                // Invalid UTF-8 or end of string - treat as single byte
                grapheme_end++;
                break;
            }
            grapheme_end += char_len;

            // Check if this is a grapheme boundary
            if (grapheme_end >= text + text_len ||
                lle_is_grapheme_boundary(grapheme_end, text, text + text_len)) {
                break;
            }
        } while (grapheme_end < text + text_len);

        size_t grapheme_bytes = grapheme_end - grapheme_start;

        // Calculate visual width of this grapheme cluster
        // Decode base codepoint (determines width of entire cluster)
        uint32_t base_codepoint = 0;
        int decode_result = lle_utf8_decode_codepoint(
            grapheme_start, grapheme_bytes, &base_codepoint);

        int char_width = 1; // Default to 1 column
        if (decode_result > 0 && base_codepoint >= 32) {
            // Use LLE's wcwidth implementation for proper width
            char_width = lle_utf8_codepoint_width(base_codepoint);
            if (char_width < 0) {
                char_width = 1; // Control characters default to 1
            }
        }

        // Add width of this grapheme cluster
        visual_width += char_width;
        col += char_width;

        // Move to next grapheme cluster
        i += grapheme_bytes;
    }

    return visual_width;
}
