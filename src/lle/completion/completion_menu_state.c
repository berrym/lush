/**
 * @file completion_menu_state.c
 * @brief LLE Completion Menu State Implementation
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
 * This module contains ONLY state management - NO rendering.
 */

#include "lle/completion/completion_menu_state.h"
#include <string.h>

/// ============================================================================
/// DEFAULT CONFIGURATION
/// ============================================================================

/**
 * @brief Get default menu configuration
 * @return Default configuration structure
 */
lle_completion_menu_config_t lle_completion_menu_default_config(void) {
    lle_completion_menu_config_t config = {.max_visible_items = 10,
                                           .show_category_headers = true,
                                           .show_type_indicators = true,
                                           .show_descriptions = false,
                                           .enable_scrolling = true,
                                           .min_items_for_menu = 2};
    return config;
}

/// ============================================================================
/// CATEGORY POSITION CALCULATION
/// ============================================================================

/**
 * @brief Calculate category positions in result set
 * @param state Menu state to update
 * @return LLE_SUCCESS or error code
 */
static lle_result_t
calculate_category_positions(lle_completion_menu_state_t *state) {
    if (!state || !state->result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result->count == 0) {
        state->category_count = 0;
        state->category_positions = NULL;
        return LLE_SUCCESS;
    }

    /// Count unique categories
    size_t cat_count = 0;
    lle_completion_type_t current_type = LLE_COMPLETION_TYPE_UNKNOWN;

    for (size_t i = 0; i < state->result->count; i++) {
        if (state->result->items[i].type != current_type) {
            current_type = state->result->items[i].type;
            cat_count++;
        }
    }

    if (cat_count == 0) {
        state->category_count = 0;
        state->category_positions = NULL;
        return LLE_SUCCESS;
    }

    /// Allocate category positions array
    state->category_positions =
        (size_t *)lle_pool_alloc(sizeof(size_t) * cat_count);
    if (!state->category_positions) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Fill category positions
    size_t cat_index = 0;
    current_type = LLE_COMPLETION_TYPE_UNKNOWN;

    for (size_t i = 0; i < state->result->count; i++) {
        if (state->result->items[i].type != current_type) {
            current_type = state->result->items[i].type;
            state->category_positions[cat_index++] = i;
        }
    }

    state->category_count = cat_count;
    return LLE_SUCCESS;
}

/// ============================================================================
/// LIFECYCLE FUNCTIONS
/// ============================================================================

/**
 * @brief Create a new completion menu state
 * @param memory_pool Memory pool for allocations
 * @param result Completion results to display
 * @param config Menu configuration (NULL for defaults)
 * @param state Output for created state
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_state_create(lle_memory_pool_t *memory_pool,
                                 lle_completion_result_t *result,
                                 const lle_completion_menu_config_t *config,
                                 lle_completion_menu_state_t **state) {
    if (!memory_pool || !result || !state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate state structure
    lle_completion_menu_state_t *new_state =
        (lle_completion_menu_state_t *)lle_pool_alloc(
            sizeof(lle_completion_menu_state_t));
    if (!new_state) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize state
    new_state->result = result;
    new_state->selected_index = 0;
    new_state->first_visible = 0;
    new_state->visible_count = 0;
    new_state->target_column = 0; /// Sticky column for UP/DOWN navigation
    new_state->category_positions = NULL;
    new_state->category_count = 0;
    new_state->menu_active = true; /// Menu is active when created
    new_state->memory_pool = memory_pool;

    /// Copy configuration or use defaults
    if (config) {
        new_state->config = *config;
    } else {
        new_state->config = lle_completion_menu_default_config();
    }

    /// Calculate visible count
    size_t total_items = result->count;
    if (new_state->config.enable_scrolling) {
        new_state->visible_count =
            total_items < new_state->config.max_visible_items
                ? total_items
                : new_state->config.max_visible_items;
    } else {
        new_state->visible_count = total_items;
    }

    /// Calculate category positions
    lle_result_t res = calculate_category_positions(new_state);
    if (res != LLE_SUCCESS) {
        lle_pool_free(new_state);
        return res;
    }

    *state = new_state;
    return LLE_SUCCESS;
}

/**
 * @brief Free menu state resources
 * @param state State to free
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_state_free(lle_completion_menu_state_t *state) {
    if (!state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free category positions if allocated
    if (state->category_positions) {
        lle_pool_free(state->category_positions);
    }

    /// Free state structure
    /// Note: result is owned by caller, so we don't free it
    lle_pool_free(state);

    return LLE_SUCCESS;
}

/// ============================================================================
/// STATE QUERIES
/// ============================================================================

/**
 * @brief Check if menu should be shown based on item count
 * @param state Menu state to check
 * @return true if menu should be shown
 */
bool lle_completion_menu_should_show(const lle_completion_menu_state_t *state) {
    if (!state || !state->result) {
        return false;
    }

    return state->result->count >= state->config.min_items_for_menu;
}

/**
 * @brief Get the currently selected completion item
 * @param state Menu state
 * @return Selected item or NULL
 */
const lle_completion_item_t *
lle_completion_menu_get_selected(const lle_completion_menu_state_t *state) {
    if (!state || !state->result) {
        return NULL;
    }

    if (state->selected_index >= state->result->count) {
        return NULL;
    }

    return &state->result->items[state->selected_index];
}

/**
 * @brief Get text of currently selected item
 * @param state Menu state
 * @return Selected item text or NULL
 */
const char *lle_completion_menu_get_selected_text(
    const lle_completion_menu_state_t *state) {
    const lle_completion_item_t *item = lle_completion_menu_get_selected(state);
    return item ? item->text : NULL;
}

/**
 * @brief Get total number of items in menu
 * @param state Menu state
 * @return Item count
 */
size_t
lle_completion_menu_get_item_count(const lle_completion_menu_state_t *state) {
    if (!state || !state->result) {
        return 0;
    }

    return state->result->count;
}

/**
 * @brief Get index of currently selected item
 * @param state Menu state
 * @return Selected index
 */
size_t lle_completion_menu_get_selected_index(
    const lle_completion_menu_state_t *state) {
    if (!state) {
        return 0;
    }

    return state->selected_index;
}

/**
 * @brief Get visible range of items in menu
 * @param state Menu state
 * @param first_visible Output for first visible index
 * @param visible_count Output for number of visible items
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_get_visible_range(const lle_completion_menu_state_t *state,
                                      size_t *first_visible,
                                      size_t *visible_count) {
    if (!state || !first_visible || !visible_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *first_visible = state->first_visible;
    *visible_count = state->visible_count;

    return LLE_SUCCESS;
}

/**
 * @brief Check if menu is currently active
 * @param state Menu state
 * @return true if active
 */
bool lle_completion_menu_is_active(const lle_completion_menu_state_t *state) {
    if (!state) {
        return false;
    }

    return state->menu_active;
}

/**
 * @brief Get number of categories in menu
 * @param state Menu state
 * @return Category count
 */
size_t lle_completion_menu_get_category_count(
    const lle_completion_menu_state_t *state) {
    if (!state) {
        return 0;
    }

    return state->category_count;
}

/// ============================================================================
/// LAYOUT FUNCTIONS
/// ============================================================================

/**
 * @brief Calculate visual width of text (excluding ANSI codes)
 * @param text Text to measure
 * @return Visual width in columns
 */
static size_t calc_visual_width(const char *text) {
    if (!text)
        return 0;

    size_t width = 0;
    bool in_escape = false;

    for (const char *p = text; *p; p++) {
        if (*p == '\033' || *p == '\x1b') {
            in_escape = true;
            continue;
        }
        if (in_escape) {
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                *p == 'm') {
                in_escape = false;
            }
            continue;
        }
        width++;
    }

    return width;
}

/**
 * @brief Update menu layout based on terminal width
 * @param state Menu state to update
 * @param terminal_width Current terminal width
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_completion_menu_update_layout(lle_completion_menu_state_t *state,
                                  size_t terminal_width) {
    if (!state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Store terminal width
    state->terminal_width = terminal_width > 0 ? terminal_width : 80;

    /// Calculate max item width
    size_t max_item_width = 0;
    if (state->result && state->result->count > 0) {
        for (size_t i = 0; i < state->result->count; i++) {
            if (state->result->items[i].text) {
                size_t len = calc_visual_width(state->result->items[i].text);
                if (len > max_item_width) {
                    max_item_width = len;
                }
            }
        }
    }

    /// Add padding for selection indicator and spacing
    const size_t padding = 4; /// "  " separator + selection indicator space
    state->column_width = max_item_width + padding;

    /// Ensure minimum column width
    if (state->column_width < 10) {
        state->column_width = 10;
    }

    /// Calculate number of columns that fit
    if (state->column_width >= state->terminal_width) {
        state->num_columns = 1;
    } else {
        state->num_columns = state->terminal_width / state->column_width;
        if (state->num_columns == 0) {
            state->num_columns = 1;
        }
        /// Cap at reasonable maximum
        if (state->num_columns > 6) {
            state->num_columns = 6;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get number of columns in menu layout
 * @param state Menu state
 * @return Number of columns (minimum 1)
 */
size_t
lle_completion_menu_get_num_columns(const lle_completion_menu_state_t *state) {
    if (!state || state->num_columns == 0) {
        return 1; /// Default to single column
    }

    return state->num_columns;
}
