/**
 * @file completion_menu_renderer.h
 * @brief Completion Menu Renderer - Text Formatting from Menu State
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Converts completion menu state (from completion_menu_state.h) into
 * formatted text ready for display via screen_buffer system.
 *
 * Architecture:
 * - Pure formatting - no state management
 * - No terminal I/O - outputs text only
 * - Respects menu state's visible range (scrolling support)
 * - Supports selection highlighting, category headers, type indicators
 * - Multi-column layout for efficient screen usage
 *
 * Integration:
 * - Input: lle_completion_menu_state_t (from Phase 4)
 * - Output: Formatted text string with \n separators
 * - Consumer: command_layer or display_controller
 * - Display: screen_buffer_render() handles actual rendering
 */

#ifndef LLE_COMPLETION_MENU_RENDERER_H
#define LLE_COMPLETION_MENU_RENDERER_H

#include "lle/completion/completion_menu_state.h"
#include "lle/error_handling.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

#define LLE_MENU_RENDERER_MAX_OUTPUT 16384 // Max output buffer size
#define LLE_MENU_RENDERER_MAX_COLS 8       // Max columns for layout
#define LLE_MENU_RENDERER_MIN_COL_WIDTH 12 // Min width per column
#define LLE_MENU_RENDERER_COL_PADDING 2    // Padding between columns

// ANSI codes for selection highlighting
#define LLE_MENU_SELECTION_START "\033[7m"   // Reverse video
#define LLE_MENU_SELECTION_END "\033[0m"     // Reset
#define LLE_MENU_CATEGORY_START "\033[1;36m" // Bold cyan
#define LLE_MENU_CATEGORY_END "\033[0m"      // Reset

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

/**
 * Menu rendering options
 *
 * Controls how menu is formatted and displayed.
 */
typedef struct {
    bool show_category_headers;   // Show "completing <type>" headers
    bool show_type_indicators;    // Show type indicators (e.g., "/", "$")
    bool use_multi_column;        // Use multi-column layout
    bool highlight_selection;     // Highlight selected item
    size_t max_rows;              // Maximum rows to render
    size_t terminal_width;        // Terminal width for layout
    const char *selection_prefix; // Prefix for selected item (e.g., "> ")
    const char *item_separator;   // Separator between items (default: "  ")
} lle_menu_render_options_t;

/**
 * Menu rendering statistics
 *
 * Information about what was rendered.
 */
typedef struct {
    size_t items_rendered;   // Number of items rendered
    size_t rows_used;        // Number of rows used
    size_t categories_shown; // Number of category headers shown
    size_t columns_used;     // Columns used in multi-column layout
    bool truncated;          // True if menu was truncated
} lle_menu_render_stats_t;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

/**
 * Create default rendering options
 *
 * Returns sensible defaults for menu rendering.
 *
 * @param terminal_width Terminal width in columns
 * @return Default rendering options
 */
lle_menu_render_options_t
lle_menu_renderer_default_options(size_t terminal_width);

/**
 * Render completion menu to text
 *
 * Generates formatted menu text from menu state. Respects visible range
 * from menu state (first_visible, visible_count) for scrolling support.
 *
 * Output format:
 * ```
 * completing external command
 * ls           lsb-release  lscpu        lsiio
 * lsar         lsb_release  lsdiff       lsinitrd
 * completing alias
 * > ls   lsa   lsd   lsl
 *   ^^^ (selected item, highlighted)
 * ```
 *
 * Features:
 * - Multi-column layout (fits more items per row)
 * - Category headers (bold text)
 * - Selection highlighting (reverse video or prefix marker)
 * - Type indicators (if enabled)
 * - Scrolling support (renders only visible range)
 *
 * @param state Menu state (contains results, selection, visible range)
 * @param options Rendering options (NULL for defaults)
 * @param output Buffer for rendered menu text
 * @param output_size Size of output buffer
 * @param stats Optional statistics output (can be NULL)
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_render(const lle_completion_menu_state_t *state,
                           const lle_menu_render_options_t *options,
                           char *output, size_t output_size,
                           lle_menu_render_stats_t *stats);

/**
 * Calculate column width for items
 *
 * Determines optimal column width for multi-column layout based on
 * item text lengths.
 *
 * @param items Array of completion items
 * @param count Number of items
 * @param terminal_width Terminal width in columns
 * @param max_columns Maximum columns to use
 * @return Recommended column width
 */
size_t
lle_menu_renderer_calculate_column_width(const lle_completion_item_t *items,
                                         size_t count, size_t terminal_width,
                                         size_t max_columns);

/**
 * Calculate number of columns that fit
 *
 * Determines how many columns can fit given terminal width and column width.
 *
 * @param terminal_width Terminal width in columns
 * @param column_width Width of each column
 * @param padding Padding between columns
 * @return Number of columns that fit
 */
size_t lle_menu_renderer_calculate_columns(size_t terminal_width,
                                           size_t column_width, size_t padding);

/**
 * Format category header
 *
 * Formats a category header line (e.g., "completing external command").
 *
 * @param type Completion type for this category
 * @param output Buffer for formatted header
 * @param output_size Size of output buffer
 * @param use_bold Use bold formatting (ANSI codes)
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_menu_renderer_format_category_header(lle_completion_type_t type,
                                         char *output, size_t output_size,
                                         bool use_bold);

/**
 * Format single item
 *
 * Formats a single completion item with optional highlighting and indicators.
 *
 * @param item The completion item
 * @param is_selected True if this item is selected
 * @param show_indicator Show type indicator
 * @param selection_prefix Prefix for selected items (e.g., "> ")
 * @param output Buffer for formatted item
 * @param output_size Size of output buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_menu_renderer_format_item(const lle_completion_item_t *item,
                                           bool is_selected,
                                           bool show_indicator,
                                           const char *selection_prefix,
                                           char *output, size_t output_size);

/**
 * Estimate rendered size
 *
 * Estimates how much buffer space will be needed for rendering.
 * Useful for pre-allocation.
 *
 * @param state Menu state
 * @param options Rendering options
 * @return Estimated bytes needed
 */
size_t
lle_menu_renderer_estimate_size(const lle_completion_menu_state_t *state,
                                const lle_menu_render_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* LLE_COMPLETION_MENU_RENDERER_H */
