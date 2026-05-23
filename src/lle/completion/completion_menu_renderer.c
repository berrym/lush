/**
 * @file completion_menu_renderer.c
 * @brief Completion Menu Renderer Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Unicode-aware menu rendering with proper display width calculation
 * for CJK characters, emoji, combining marks, and other Unicode.
 */

#include "lle/completion/completion_menu_renderer.h"
#include "lle/completion/completion_types.h"
#include "lle/utf8_support.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
// HELPER FUNCTIONS
/* ========================================================================== */

/**
 * @brief Get human-readable type name for category header
 * @param type Completion type to get name for
 * @return Static string with category name
 */
static const char *get_type_category_name(lle_completion_type_t type) {
    switch (type) {
    case LLE_COMPLETION_TYPE_BUILTIN:
        return "builtin command";
    case LLE_COMPLETION_TYPE_COMMAND:
        return "external command";
    case LLE_COMPLETION_TYPE_FILE:
        return "file";
    case LLE_COMPLETION_TYPE_DIRECTORY:
        return "directory";
    case LLE_COMPLETION_TYPE_VARIABLE:
        return "parameter";
    case LLE_COMPLETION_TYPE_ALIAS:
        return "alias";
    case LLE_COMPLETION_TYPE_HISTORY:
        return "history";
    case LLE_COMPLETION_TYPE_CUSTOM:
        return "option";
    default:
        return "completion";
    }
}

/**
 * @brief Calculate visual width of string, skipping ANSI escape sequences
 *
 * Uses lle_utf8_string_width() for proper Unicode display width calculation:
 * - CJK characters: 2 columns
 * - Emoji: 2 columns
 * - Combining marks: 0 columns
 * - Regular ASCII/Latin: 1 column
 *
 * ANSI escape sequences have zero visual width but take up bytes.
 * Format: ESC [ ... final_byte (where final_byte is 0x40-0x7E)
 *
 * @param str String to measure
 * @return Visual width in terminal columns
 */
static size_t visual_width(const char *str) {
    if (!str)
        return 0;

    size_t len = strlen(str);
    if (len == 0)
        return 0;

    // Fast path: no ANSI escape sequences
    const char *esc = memchr(str, 0x1B, len);
    if (!esc) {
        // No escape sequences - use lle_utf8_string_width directly
        return lle_utf8_string_width(str, len);
    }

    /* Slow path: string contains ANSI escape sequences
     * Process segments between escape sequences */
    size_t width = 0;
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)str[i];

        // Check for ANSI escape sequence (ESC = 0x1B)
        if (c == 0x1B && i + 1 < len) {
            unsigned char next = (unsigned char)str[i + 1];
            if (next == '[') {
                /* CSI sequence: ESC [ ... final_byte
                 * Skip until we find the final byte (0x40-0x7E) */
                i += 2; // Skip ESC [
                while (i < len) {
                    unsigned char seq_char = (unsigned char)str[i];
                    i++;
                    if (seq_char >= 0x40 && seq_char <= 0x7E) {
                        break; // Found final byte, sequence complete
                    }
                }
                continue; // Don't add to width, continue to next character
            }
            // Other escape sequences (ESC + single char)
            i += 2;
            continue;
        }

        // Find length of text segment until next escape or end
        size_t segment_start = i;
        while (i < len && (unsigned char)str[i] != 0x1B) {
            i++;
        }

        // Calculate width of this segment using proper UTF-8 width
        if (i > segment_start) {
            width +=
                lle_utf8_string_width(str + segment_start, i - segment_start);
        }
    }

    return width;
}

/**
 * @brief Pad string to target visual width
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to pad
 * @param target_width Target visual width
 * @return Number of bytes written to dest
 */
static size_t pad_string(char *dest, size_t dest_size, const char *src,
                         size_t target_width) {
    if (!dest || !src || dest_size == 0)
        return 0;

    size_t src_width = visual_width(src);
    size_t written = 0;

    // Copy source
    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        src_len = dest_size - 1;
    }
    memcpy(dest, src, src_len);
    written = src_len;

    // Add padding if needed
    if (src_width < target_width && written < dest_size - 1) {
        size_t padding = target_width - src_width;
        if (padding > dest_size - written - 1) {
            padding = dest_size - written - 1;
        }
        memset(dest + written, ' ', padding);
        written += padding;
    }

    dest[written] = '\0';
    return written;
}

/* ========================================================================== */
// PUBLIC API FUNCTIONS
/* ========================================================================== */

/**
 * @brief Get default menu rendering options
 * @param terminal_width Terminal width in columns
 * @return Default options structure
 */
lle_menu_render_options_t
lle_menu_renderer_default_options(size_t terminal_width) {
    lle_menu_render_options_t options = {
        .show_category_headers = true,
        .show_type_indicators = true,
        .use_multi_column = true,
        .highlight_selection = true,
        .max_rows = 20,
        .terminal_width = terminal_width > 0 ? terminal_width : 80,
        .selection_prefix = "> ",
        .item_separator = "  "};
    return options;
}

/**
 * @brief Calculate optimal column width for completion items
 * @param items Array of completion items
 * @param count Number of items
 * @param terminal_width Terminal width in columns
 * @param max_columns Maximum number of columns (reserved for future use)
 * @return Column width in characters
 */
size_t
lle_menu_renderer_calculate_column_width(const lle_completion_item_t *items,
                                         size_t count, size_t terminal_width,
                                         size_t max_columns) {
    (void)max_columns; // Reserved for future use

    if (!items || count == 0) {
        return LLE_MENU_RENDERER_MIN_COL_WIDTH;
    }

    // Find longest item
    size_t max_len = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].text) {
            size_t len = visual_width(items[i].text);

            // Account for type indicator if present
            if (items[i].type_indicator) {
                len += strlen(items[i].type_indicator);
            }

            if (len > max_len) {
                max_len = len;
            }
        }
    }

    // Add padding
    max_len += LLE_MENU_RENDERER_COL_PADDING;

    // Ensure minimum width
    if (max_len < LLE_MENU_RENDERER_MIN_COL_WIDTH) {
        max_len = LLE_MENU_RENDERER_MIN_COL_WIDTH;
    }

    // Ensure we can fit at least one column
    if (max_len > terminal_width) {
        max_len = terminal_width;
    }

    return max_len;
}

/**
 * @brief Calculate number of columns that fit in terminal
 * @param terminal_width Terminal width in columns
 * @param column_width Width of each column
 * @param padding Padding between columns
 * @return Number of columns
 */
size_t lle_menu_renderer_calculate_columns(size_t terminal_width,
                                           size_t column_width,
                                           size_t padding) {
    if (column_width == 0 || terminal_width == 0) {
        return 1;
    }

    size_t cols = terminal_width / (column_width + padding);
    if (cols == 0)
        cols = 1;
    if (cols > LLE_MENU_RENDERER_MAX_COLS) {
        cols = LLE_MENU_RENDERER_MAX_COLS;
    }

    return cols;
}

/**
 * @brief Format a category header string
 * @param type Completion type for the category
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @param use_bold Whether to use bold ANSI formatting
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_menu_renderer_format_category_header(lle_completion_type_t type,
                                         char *output, size_t output_size,
                                         bool use_bold) {
    if (!output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *type_name = get_type_category_name(type);
    int written;

    if (use_bold) {
        written =
            snprintf(output, output_size, "%scompleting %s%s",
                     LLE_MENU_CATEGORY_START, type_name, LLE_MENU_CATEGORY_END);
    } else {
        written = snprintf(output, output_size, "completing %s", type_name);
    }

    if (written < 0 || (size_t)written >= output_size) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Format a single completion item for display
 * @param item Completion item to format
 * @param is_selected Whether item is currently selected
 * @param show_indicator Whether to show type indicator
 * @param selection_prefix Prefix string for selected items
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_menu_renderer_format_item(const lle_completion_item_t *item,
                                           bool is_selected,
                                           bool show_indicator,
                                           const char *selection_prefix,
                                           char *output, size_t output_size) {
    if (!item || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    char formatted[256];
    size_t pos = 0;

    // Add selection prefix if selected
    if (is_selected && selection_prefix) {
        size_t prefix_len = strlen(selection_prefix);
        if (prefix_len < sizeof(formatted) - pos) {
            strcpy(formatted + pos, selection_prefix);
            pos += prefix_len;
        }
    } else if (selection_prefix) {
        // Add spaces to align non-selected items
        size_t prefix_len = strlen(selection_prefix);
        if (prefix_len < sizeof(formatted) - pos) {
            memset(formatted + pos, ' ', prefix_len);
            pos += prefix_len;
        }
    }

    // Add item text
    if (item->text && pos < sizeof(formatted) - 1) {
        size_t text_len = strlen(item->text);
        if (text_len > sizeof(formatted) - pos - 1) {
            text_len = sizeof(formatted) - pos - 1;
        }
        memcpy(formatted + pos, item->text, text_len);
        pos += text_len;
    }

    // Add type indicator
    if (show_indicator && item->type_indicator && pos < sizeof(formatted) - 1) {
        size_t ind_len = strlen(item->type_indicator);
        if (ind_len > sizeof(formatted) - pos - 1) {
            ind_len = sizeof(formatted) - pos - 1;
        }
        memcpy(formatted + pos, item->type_indicator, ind_len);
        pos += ind_len;
    }

    formatted[pos] = '\0';

    // Apply highlighting if selected
    int written;
    if (is_selected) {
        written =
            snprintf(output, output_size, "%s%s%s", LLE_MENU_SELECTION_START,
                     formatted, LLE_MENU_SELECTION_END);
    } else {
        written = snprintf(output, output_size, "%s", formatted);
    }

    if (written < 0 || (size_t)written >= output_size) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Estimate output buffer size needed for rendering
 * @param state Menu state to render
 * @param options Rendering options (reserved for future use)
 * @return Estimated buffer size in bytes
 */
size_t
lle_menu_renderer_estimate_size(const lle_completion_menu_state_t *state,
                                const lle_menu_render_options_t *options) {
    (void)options; // Reserved for future use

    if (!state || !state->result) {
        return 1024; // Default estimate
    }

    size_t visible = state->visible_count;
    if (visible > state->result->count - state->first_visible) {
        visible = state->result->count - state->first_visible;
    }

    // Rough estimate:
    // - Average 20 chars per item
    // - Category headers: 30 chars each (max 8 types)
    // - ANSI codes: ~20 chars per item
    // - Newlines and padding

    size_t estimate = visible * 40 + 8 * 30 + 256;

    if (estimate > LLE_MENU_RENDERER_MAX_OUTPUT) {
        estimate = LLE_MENU_RENDERER_MAX_OUTPUT;
    }

    return estimate;
}

/**
 * @brief Render the completion menu to a string buffer
 * @param state Menu state to render
 * @param options Rendering options (NULL for defaults)
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @param stats Optional output for render statistics
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_render(const lle_completion_menu_state_t *state,
                           const lle_menu_render_options_t *options,
                           char *output, size_t output_size,
                           lle_menu_render_stats_t *stats) {
    if (!state || !state->result || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active || state->result->count == 0) {
        output[0] = '\0';
        if (stats) {
            memset(stats, 0, sizeof(*stats));
        }
        return LLE_SUCCESS;
    }

    // Use default options if not provided
    lle_menu_render_options_t default_opts;
    if (!options) {
        default_opts = lle_menu_renderer_default_options(80);
        options = &default_opts;
    }

    // Initialize stats
    lle_menu_render_stats_t local_stats = {0};

    // Get visible range
    size_t start_idx = state->first_visible;
    size_t end_idx = start_idx + state->visible_count;
    if (end_idx > state->result->count) {
        end_idx = state->result->count;
    }

    const lle_completion_item_t *items = state->result->items;
    size_t output_pos = 0;
    size_t rows_used = 0;

    // Use pre-calculated column layout from menu state if available
    // This ensures stable layout during navigation (no column shifting)
    size_t col_width = 0;
    size_t columns = 1;
    if (options->use_multi_column) {
        // Prefer cached layout from state (set by
        // lle_completion_menu_update_layout)
        if (state->column_width > 0 && state->num_columns > 0) {
            col_width = state->column_width;
            columns = state->num_columns;
        } else {
            // Fallback: calculate fresh if state doesn't have layout info
            col_width = lle_menu_renderer_calculate_column_width(
                items + start_idx, end_idx - start_idx, options->terminal_width,
                LLE_MENU_RENDERER_MAX_COLS);
            columns = lle_menu_renderer_calculate_columns(
                options->terminal_width, col_width,
                LLE_MENU_RENDERER_COL_PADDING);
        }
        local_stats.columns_used = columns;
    }

    lle_completion_type_t current_category = LLE_COMPLETION_TYPE_UNKNOWN;
    size_t column = 0;

    for (size_t i = start_idx; i < end_idx && rows_used < options->max_rows;
         i++) {
        const lle_completion_item_t *item = &items[i];

        // Category header if type changed
        if (options->show_category_headers && item->type != current_category) {
            // Finish current row if mid-row
            if (column > 0 && output_pos < output_size - 1) {
                output[output_pos++] = '\n';
                rows_used++;
                column = 0;
            }

            // Check row limit
            if (rows_used >= options->max_rows) {
                local_stats.truncated = true;
                break;
            }

            // Format category header
            char header[128];
            lle_result_t result = lle_menu_renderer_format_category_header(
                item->type, header, sizeof(header),
                true // Use bold
            );

            if (result == LLE_SUCCESS) {
                size_t header_len = strlen(header);
                if (output_pos + header_len + 1 < output_size) {
                    memcpy(output + output_pos, header, header_len);
                    output_pos += header_len;
                    output[output_pos++] = '\n';
                    rows_used++;
                    local_stats.categories_shown++;
                    current_category = item->type;
                }
            }
        }

        // Check row limit before rendering item
        if (rows_used >= options->max_rows) {
            local_stats.truncated = true;
            break;
        }

        // Format item
        char formatted[256];
        bool is_selected = (i == state->selected_index);
        lle_result_t result = lle_menu_renderer_format_item(
            item, is_selected && options->highlight_selection,
            options->show_type_indicators,
            options->use_multi_column ? NULL : options->selection_prefix,
            formatted, sizeof(formatted));

        if (result != LLE_SUCCESS) {
            continue; // Skip this item
        }

        // Add to output with column layout
        if (options->use_multi_column) {
            // Pad to column width
            char padded[256];
            pad_string(padded, sizeof(padded), formatted, col_width);

            size_t padded_len = strlen(padded);
            if (output_pos + padded_len + 1 < output_size) {
                memcpy(output + output_pos, padded, padded_len);
                output_pos += padded_len;

                column++;
                if (column >= columns) {
                    // End of row
                    output[output_pos++] = '\n';
                    rows_used++;
                    column = 0;
                } else {
                    // Add column separator (included in padding)
                }
            }
        } else {
            // Single column layout
            size_t formatted_len = strlen(formatted);
            if (output_pos + formatted_len + 1 < output_size) {
                memcpy(output + output_pos, formatted, formatted_len);
                output_pos += formatted_len;
                output[output_pos++] = '\n';
                rows_used++;
            }
        }

        local_stats.items_rendered++;
    }

    // Finish last row if needed
    if (column > 0 && output_pos < output_size - 1) {
        output[output_pos++] = '\n';
        rows_used++;
    }

    // Null terminate
    if (output_pos < output_size) {
        output[output_pos] = '\0';
    } else {
        output[output_size - 1] = '\0';
        local_stats.truncated = true;
    }

    local_stats.rows_used = rows_used;

    // Copy stats if requested
    if (stats) {
        *stats = local_stats;
    }

    return LLE_SUCCESS;
}
