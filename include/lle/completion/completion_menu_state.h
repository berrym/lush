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
    lle_completion_result_t *result; /**< Completion result (not owned) */

    // Navigation state
    size_t selected_index; /**< Currently selected item (global index) */
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

    // Configuration
    lle_completion_menu_config_t config; /**< Menu configuration */

    // Memory pool
    lle_memory_pool_t *memory_pool; /**< Memory pool for allocations */
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
 * @param state output parameter for created state
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_state_create(lle_memory_pool_t *memory_pool,
                                 lle_completion_result_t *result,
                                 const lle_completion_menu_config_t *config,
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

#ifdef __cplusplus
}
#endif

#endif // LLE_COMPLETION_MENU_STATE_H
