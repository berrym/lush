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

#include "lle/completion/menu_filter.h"
#include "lle/utf8_support.h"

#include <stdint.h>
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
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion menu allocation failed");
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
lle_result_t lle_completion_menu_state_create(
    lle_memory_pool_t *memory_pool, lle_completion_result_t *result,
    const lle_completion_menu_config_t *config, const char *original_prefix,
    lle_completion_menu_state_t **state) {
    if (!memory_pool || !result || !state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate state structure
    lle_completion_menu_state_t *new_state =
        (lle_completion_menu_state_t *)lle_pool_alloc(
            sizeof(lle_completion_menu_state_t));
    if (!new_state) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion menu allocation failed");
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

    /// In-menu type-to-filter state. unfiltered_result aliases the
    /// engine's result so apply_filter has a stable source to scan;
    /// filter_string starts empty and lazily allocates on first
    /// append. original_prefix is duplicated into the pool when the
    /// caller passes a non-empty value so it survives the source
    /// context's lifetime.
    new_state->unfiltered_result = result;
    new_state->filtered_result = NULL;
    new_state->filter_string = NULL;
    new_state->filter_len = 0;
    new_state->filter_capacity = 0;
    new_state->original_prefix = NULL;
    if (original_prefix && original_prefix[0] != '\0') {
        size_t pref_len = strlen(original_prefix);
        new_state->original_prefix = (char *)lle_pool_alloc(pref_len + 1);
        if (new_state->original_prefix) {
            memcpy(new_state->original_prefix, original_prefix, pref_len);
            new_state->original_prefix[pref_len] = '\0';
        }
    }

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

    /// Filter-state buffers are pool-allocated; release them so a
    /// recycled pool does not retain stale entries. unfiltered_result
    /// is owned by the completion system; filtered_result owns its
    /// items array (pool-allocated) but not the per-item strings
    /// (those alias into unfiltered_result and the completion
    /// system's pool).
    if (state->filter_string) {
        lle_pool_free(state->filter_string);
    }
    if (state->original_prefix) {
        lle_pool_free(state->original_prefix);
    }
    if (state->filtered_result) {
        if (state->filtered_result->items) {
            lle_pool_free(state->filtered_result->items);
        }
        lle_pool_free(state->filtered_result);
    }

    /// Free state structure
    /// Note: result is owned by caller, so we don't free it
    lle_pool_free(state);

    return LLE_SUCCESS;
}

/// ============================================================================
/// TYPE-TO-FILTER STATE OPERATIONS
/// ============================================================================

/// Build the combined prefix used by the filter predicate. Writes up
/// to out_size-1 bytes followed by a NUL. Returns the bytes written
/// (excluding NUL), or -1 if the combined string would not fit.
static int combine_prefix(const lle_completion_menu_state_t *state, char *out,
                          size_t out_size) {
    size_t orig_len =
        state->original_prefix ? strlen(state->original_prefix) : 0;
    size_t total = orig_len + state->filter_len;
    if (total + 1 > out_size) {
        return -1;
    }
    if (orig_len) {
        memcpy(out, state->original_prefix, orig_len);
    }
    if (state->filter_len) {
        memcpy(out + orig_len, state->filter_string, state->filter_len);
    }
    out[total] = '\0';
    return (int)total;
}

bool lle_completion_menu_filter_active(
    const lle_completion_menu_state_t *state) {
    return state != NULL && state->filter_len > 0;
}

lle_result_t
lle_completion_menu_apply_filter(lle_completion_menu_state_t *state) {
    if (!state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    if (!state->unfiltered_result) {
        /// Menu has no source to scan -- nothing to do.
        return LLE_SUCCESS;
    }

    /// Empty filter clamps back to the unfiltered view. The filtered
    /// items buffer is intentionally retained so future filter
    /// additions can reuse its capacity without a second allocation.
    if (state->filter_len == 0) {
        state->result = state->unfiltered_result;
        state->selected_index = 0;
        state->first_visible = 0;
        return LLE_SUCCESS;
    }

    char combined[1024];
    if (combine_prefix(state, combined, sizeof(combined)) < 0) {
        /// Combined prefix exceeds our scratch buffer; refuse to
        /// narrow rather than truncate (truncation would silently
        /// admit candidates that don't match the actual input).
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion menu allocation failed");
    }

    /// Lazily allocate the filtered_result container and items array
    /// on first use. The items array is sized to the full unfiltered
    /// capacity so it never reallocates as filtered_count grows or
    /// shrinks between filter passes.
    if (!state->filtered_result) {
        state->filtered_result = (lle_completion_result_t *)lle_pool_alloc(
            sizeof(lle_completion_result_t));
        if (!state->filtered_result) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion menu allocation failed");
        }
        memset(state->filtered_result, 0, sizeof(lle_completion_result_t));
        size_t cap = state->unfiltered_result->count;
        if (cap == 0) {
            cap = 1;
        }
        state->filtered_result->items = (lle_completion_item_t *)lle_pool_alloc(
            cap * sizeof(lle_completion_item_t));
        if (!state->filtered_result->items) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion menu allocation failed");
        }
        state->filtered_result->capacity = cap;
    }

    /// Shallow-copy admitted items; per-item strings live in the
    /// unfiltered result's pool and are not duplicated.
    size_t kept = 0;
    for (size_t i = 0; i < state->unfiltered_result->count; i++) {
        const lle_completion_item_t *item = &state->unfiltered_result->items[i];
        if (!item->text) {
            continue;
        }
        if (lle_completion_filter_invoke(combined, item->text)) {
            state->filtered_result->items[kept++] = *item;
        }
    }
    state->filtered_result->count = kept;

    state->result = state->filtered_result;
    state->selected_index = 0;
    state->first_visible = 0;
    return LLE_SUCCESS;
}

lle_result_t
lle_completion_menu_append_filter_char(lle_completion_menu_state_t *state,
                                       uint32_t codepoint) {
    if (!state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    char encoded[4];
    int n = lle_utf8_encode_codepoint(codepoint, encoded);
    if (n <= 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Grow filter_string to fit. Start at 16 bytes (enough for any
    /// realistic shell filter session) and double thereafter so the
    /// amortized cost stays linear in the typed length.
    size_t needed = state->filter_len + (size_t)n + 1;
    if (needed > state->filter_capacity) {
        size_t new_cap = state->filter_capacity ? state->filter_capacity : 16;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *new_buf = (char *)lle_pool_alloc(new_cap);
        if (!new_buf) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion menu allocation failed");
        }
        if (state->filter_len) {
            memcpy(new_buf, state->filter_string, state->filter_len);
        }
        if (state->filter_string) {
            lle_pool_free(state->filter_string);
        }
        state->filter_string = new_buf;
        state->filter_capacity = new_cap;
    }

    memcpy(state->filter_string + state->filter_len, encoded, (size_t)n);
    state->filter_len += (size_t)n;
    state->filter_string[state->filter_len] = '\0';

    return lle_completion_menu_apply_filter(state);
}

lle_result_t
lle_completion_menu_pop_filter_char(lle_completion_menu_state_t *state) {
    if (!state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    if (state->filter_len == 0) {
        return LLE_SUCCESS;
    }

    /// Walk backward over UTF-8 continuation bytes (0x80-0xBF) to find
    /// the start of the last codepoint, then truncate there. A single
    /// ASCII byte hits the != continuation test on the first
    /// iteration so the cost is constant for the common case.
    size_t i = state->filter_len;
    while (i > 0) {
        i--;
        unsigned char b = (unsigned char)state->filter_string[i];
        if ((b & 0xC0) != 0x80) {
            break;
        }
    }
    state->filter_len = i;
    state->filter_string[i] = '\0';

    return lle_completion_menu_apply_filter(state);
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
