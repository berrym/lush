/**
 * @file completion_menu_state.h
 * @brief LLE Completion Menu State Management
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Menu state management for completion system.
 * This module contains ONLY state structures and lifecycle - NO rendering.
 * All rendering is handled by the display layer.
 *
 * State includes:
 * - Selected item index
 * - Visible window (for scrolling)
 * - Category positions
 * - Configuration
 */

#ifndef LLE_COMPLETION_MENU_STATE_H
#define LLE_COMPLETION_MENU_STATE_H

#include "lle/completion/completion_types.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MENU CONFIGURATION
// ============================================================================

/**
 * @brief Menu configuration
 */
typedef struct {
    size_t max_visible_items;   /**< Maximum items visible at once */
    bool show_category_headers; /**< Show category headers */
    bool show_type_indicators;  /**< Show type indicators */
    bool show_descriptions;     /**< Show item descriptions */
    bool enable_scrolling;      /**< Enable scrolling for long lists */
    size_t min_items_for_menu;  /**< Minimum items before showing menu */
} lle_completion_menu_config_t;

// ============================================================================
// MENU STATE
// ============================================================================

/**
 * @brief Completion menu state
 */
typedef struct {
    lle_completion_result_t
        *result; /**< Active view (unfiltered or filtered) */

    // Navigation state
    size_t selected_index; /**< Currently selected item (index into result) */
    size_t first_visible;  /**< First visible item index (for scrolling) */
    size_t visible_count;  /**< Number of visible items */
    size_t target_column;  /**< Sticky column for UP/DOWN navigation */

    // Layout state (for multi-column navigation)
    size_t terminal_width; /**< Current terminal width */
    size_t column_width;   /**< Width of each column */
    size_t num_columns;    /**< Number of columns in layout */

    // Category tracking
    size_t *category_positions; /**< Start position of each category */
    size_t category_count;      /**< Number of categories present */

    // State flags
    bool menu_active; /**< Whether menu is currently active */

    /**
     * @brief Pre-navigation: the menu is shown but the user has not yet
     * committed to a candidate.
     *
     * A freshly opened menu starts in this state. The command buffer holds only
     * what the user typed; the top candidate is offered as shadow ghost text
     * (rendered by the display controller, never spliced into the buffer); and
     * ENTER accepts the buffer as-is. The first TAB transitions to navigation
     * (selected_index 0 is committed and cycling begins). This keeps a single
     * TAB from descending past a directory boundary and keeps ENTER accepting
     * exactly what is on the command line.
     */
    bool awaiting_navigation;

    // Configuration
    lle_completion_menu_config_t config; /**< Menu configuration */

    // Memory pool
    lle_memory_pool_t *memory_pool; /**< Memory pool for allocations */

    /**
     * @name In-menu type-to-filter state
     *
     * The unfiltered result is the candidate set the engine produced at
     * TAB time. When the user types printable characters while the
     * menu is visible, filter_string accumulates the typed input and
     * the menu narrows by applying the configured completion predicate
     * against (original_prefix + filter_string). The narrowed view is
     * stored in filtered_result; @c result swaps to point at it.
     * Backspace pops a character and re-applies. When filter_string is
     * empty @c result points back at @c unfiltered_result.
     */
    /**@{*/
    lle_completion_result_t
        *unfiltered_result; /**< Original candidate set (not owned) */
    lle_completion_result_t
        *filtered_result;   /**< Narrowed view (pool-allocated) */
    char *filter_string;    /**< Chars typed since menu opened */
    size_t filter_len;      /**< Length of filter_string in bytes */
    size_t filter_capacity; /**< Allocated capacity of filter_string */
    char *original_prefix;  /**< TAB-time word prefix (pool-allocated) */
    /**@}*/
} lle_completion_menu_state_t;

// ============================================================================
// LIFECYCLE FUNCTIONS
// ============================================================================

/**
 * @brief Create default menu configuration
 *
 * @return default configuration
 */
lle_completion_menu_config_t lle_completion_menu_default_config(void);

/**
 * @brief Create menu state from completion result
 *
 * @param memory_pool memory pool for allocations
 * @param result completion result (caller retains ownership)
 * @param config menu configuration (NULL for defaults)
 * @param original_prefix the word prefix at TAB time, used to
 *        construct the combined prefix during in-menu filtering.
 *        May be NULL or empty when the menu has no notion of a typed
 *        prefix; the filter then operates against filter_string alone.
 * @param state output parameter for created state
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_state_create(
    lle_memory_pool_t *memory_pool, lle_completion_result_t *result,
    const lle_completion_menu_config_t *config, const char *original_prefix,
    lle_completion_menu_state_t **state);

/**
 * @brief Free menu state
 *
 * Does NOT free the result (caller retains ownership).
 *
 * @param state menu state to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_state_free(lle_completion_menu_state_t *state);

// ============================================================================
// STATE QUERIES
// ============================================================================

/**
 * @brief Check if menu should be displayed
 *
 * @param state menu state
 * @return true if menu should be shown
 */
bool lle_completion_menu_should_show(const lle_completion_menu_state_t *state);

/**
 * @brief Get selected item
 *
 * @param state menu state
 * @return selected item, or NULL if none
 */
const lle_completion_item_t *
lle_completion_menu_get_selected(const lle_completion_menu_state_t *state);

/**
 * @brief Get selected item text
 *
 * @param state menu state
 * @return selected item text, or NULL if none
 */
const char *
lle_completion_menu_get_selected_text(const lle_completion_menu_state_t *state);

/**
 * @brief Get total item count
 *
 * @param state menu state
 * @return total number of items
 */
size_t
lle_completion_menu_get_item_count(const lle_completion_menu_state_t *state);

/**
 * @brief Get selected index
 *
 * @param state menu state
 * @return selected item index
 */
size_t lle_completion_menu_get_selected_index(
    const lle_completion_menu_state_t *state);

/**
 * @brief Get visible items range
 *
 * @param state menu state
 * @param first_visible output parameter for first visible index
 * @param visible_count output parameter for number of visible items
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_get_visible_range(const lle_completion_menu_state_t *state,
                                      size_t *first_visible,
                                      size_t *visible_count);

/**
 * @brief Check if menu is active
 *
 * @param state menu state
 * @return true if menu is active
 */
bool lle_completion_menu_is_active(const lle_completion_menu_state_t *state);

/**
 * @brief Get category count
 *
 * @param state menu state
 * @return number of categories
 */
size_t lle_completion_menu_get_category_count(
    const lle_completion_menu_state_t *state);

/**
 * @brief Update menu layout based on terminal width
 *
 * Calculates optimal column width and number of columns based on
 * the current terminal width and item widths. Should be called
 * whenever the menu is displayed or terminal is resized.
 *
 * @param state menu state
 * @param terminal_width current terminal width
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_update_layout(lle_completion_menu_state_t *state,
                                  size_t terminal_width);

/**
 * @brief Get number of columns in current layout
 *
 * @param state menu state
 * @return number of columns (1 if not set)
 */
size_t
lle_completion_menu_get_num_columns(const lle_completion_menu_state_t *state);

// ============================================================================
// TYPE-TO-FILTER OPERATIONS
// ============================================================================

/**
 * @brief Append one Unicode codepoint to the type-to-filter buffer.
 *
 * Encodes the codepoint as UTF-8, appends it to filter_string (growing
 * the capacity as needed), then re-applies the active completion
 * predicate against unfiltered_result. The narrowed view is stored in
 * filtered_result and selected_index resets to 0.
 *
 * @param state menu state
 * @param codepoint Unicode codepoint to append. The caller is expected
 *        to have already verified that this is a printable character;
 *        non-printables are not validated here.
 * @return LLE_SUCCESS, LLE_ERROR_INVALID_PARAMETER on NULL state,
 *         LLE_ERROR_OUT_OF_MEMORY on allocation failure.
 */
lle_result_t
lle_completion_menu_append_filter_char(lle_completion_menu_state_t *state,
                                       uint32_t codepoint);

/**
 * @brief Remove the last codepoint from the type-to-filter buffer.
 *
 * Walks the filter_string backwards over UTF-8 continuation bytes to
 * find the start of the last codepoint, truncates there, then
 * re-applies the predicate. When filter_string becomes empty the menu
 * view swaps back to unfiltered_result. No-op (returns LLE_SUCCESS)
 * when the buffer is already empty.
 *
 * @param state menu state
 * @return LLE_SUCCESS, LLE_ERROR_INVALID_PARAMETER on NULL state.
 */
lle_result_t
lle_completion_menu_pop_filter_char(lle_completion_menu_state_t *state);

/**
 * @brief Reapply the filter against the current filter_string.
 *
 * Constructs the combined prefix (original_prefix + filter_string) and
 * calls lle_completion_filter_invoke for each item in unfiltered_result.
 * Items that admit become entries in filtered_result. Empty
 * filter_string short-circuits to swapping result back at
 * unfiltered_result without allocating a filtered copy. selected_index
 * is reset to 0; first_visible likewise so the new top item is
 * immediately in view.
 *
 * @param state menu state
 * @return LLE_SUCCESS, LLE_ERROR_INVALID_PARAMETER on NULL state,
 *         LLE_ERROR_OUT_OF_MEMORY on allocation failure.
 */
lle_result_t
lle_completion_menu_apply_filter(lle_completion_menu_state_t *state);

/**
 * @brief True if a non-empty filter string is currently in effect.
 *
 * Used by keybinding intercepts and the renderer to distinguish "menu
 * open with type-to-filter active" from "menu open at TAB-time state".
 *
 * @param state menu state (may be NULL)
 * @return true when filter_string is non-empty.
 */
bool lle_completion_menu_filter_active(
    const lle_completion_menu_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // LLE_COMPLETION_MENU_STATE_H
