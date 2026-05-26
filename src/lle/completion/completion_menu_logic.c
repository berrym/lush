/**
 * @file completion_menu_logic.c
 * @brief Completion Menu Navigation Logic Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements menu navigation logic including up/down/left/right movement,
 * page navigation, category jumping, and selection handling.
 */

#include "lle/completion/completion_menu_logic.h"
#include <stddef.h>

/**
 * @brief Ensure selected item is visible in the current view
 *
 * Adjusts first_visible index if needed to keep selection in view.
 *
 * @param state Menu state to adjust
 */
static void ensure_visible(lle_completion_menu_state_t *state) {
    if (state == NULL || state->result == NULL) {
        return;
    }

    size_t total_items = state->result->count;
    if (total_items == 0) {
        return;
    }

    /// If selected is before visible range, scroll up
    if (state->selected_index < state->first_visible) {
        state->first_visible = state->selected_index;
    }
    /// If selected is after visible range, scroll down
    else if (state->selected_index >=
             state->first_visible + state->visible_count) {
        state->first_visible = state->selected_index - state->visible_count + 1;
    }

    /// Ensure first_visible doesn't go past the end
    if (state->first_visible + state->visible_count > total_items) {
        if (total_items >= state->visible_count) {
            state->first_visible = total_items - state->visible_count;
        } else {
            state->first_visible = 0;
        }
    }
}

/**
 * @brief Get number of columns from menu state
 *
 * Uses the dynamically calculated value from
 * lle_completion_menu_update_layout().
 *
 * @param state Menu state to query
 * @return Number of columns (defaults to 1)
 */
static size_t get_columns(const lle_completion_menu_state_t *state) {
    if (state && state->num_columns > 0) {
        return state->num_columns;
    }
    return 1; /// Default to single column
}

/**
 * @brief Find which category an item belongs to
 * @param state Menu state to search
 * @param item_index Index of item to find category for
 * @param category_start Output for category start index (may be NULL)
 * @param category_end Output for category end index (may be NULL)
 * @return Category index (0-based)
 */
static size_t find_category_for_index(const lle_completion_menu_state_t *state,
                                      size_t item_index, size_t *category_start,
                                      size_t *category_end) {
    if (!state || state->category_count == 0) {
        /// No categories, treat all items as one category
        if (category_start)
            *category_start = 0;
        if (category_end)
            *category_end = state ? state->result->count : 0;
        return 0;
    }

    size_t total_items = state->result->count;

    for (size_t i = 0; i < state->category_count; i++) {
        size_t cat_start = state->category_positions[i];
        size_t cat_end;

        if (i + 1 < state->category_count) {
            cat_end = state->category_positions[i + 1];
        } else {
            cat_end = total_items;
        }

        if (item_index >= cat_start && item_index < cat_end) {
            if (category_start)
                *category_start = cat_start;
            if (category_end)
                *category_end = cat_end;
            return i;
        }
    }

    /// Fallback: return last category
    if (category_start)
        *category_start = state->category_positions[state->category_count - 1];
    if (category_end)
        *category_end = total_items;
    return state->category_count - 1;
}

/**
 * @brief Move selection down in the completion menu
 *
 * Moves to the next row within the current category, or wraps to the
 * first row of the next category if at the bottom.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_move_down(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t columns = get_columns(state);

    /// Find current category
    size_t cat_start, cat_end;
    size_t current_cat = find_category_for_index(state, state->selected_index,
                                                 &cat_start, &cat_end);

    /// Calculate position within current category
    size_t index_in_cat = state->selected_index - cat_start;
    size_t items_in_cat = cat_end - cat_start;
    size_t current_row_in_cat = index_in_cat / columns;
    size_t rows_in_cat = (items_in_cat + columns - 1) / columns;

    /// Try to move to next row in same category
    size_t next_row = current_row_in_cat + 1;

    if (next_row < rows_in_cat) {
        /// Stay in same category, move to next row
        size_t row_start = cat_start + next_row * columns;
        size_t row_end = row_start + columns;
        if (row_end > cat_end) {
            row_end = cat_end;
        }
        size_t items_in_row = row_end - row_start;

        /// Use target column, fall back to last item in row
        size_t new_col = state->target_column;
        if (new_col >= items_in_row) {
            new_col = items_in_row - 1;
        }
        state->selected_index = row_start + new_col;
    } else {
        /// Move to next category (or wrap to first)
        size_t next_cat;
        if (state->category_count > 0 &&
            current_cat + 1 < state->category_count) {
            next_cat = current_cat + 1;
        } else {
            next_cat = 0; /// Wrap to first category
        }

        /// Get next category boundaries
        size_t next_cat_start, next_cat_end;
        if (state->category_count > 0) {
            next_cat_start = state->category_positions[next_cat];
            if (next_cat + 1 < state->category_count) {
                next_cat_end = state->category_positions[next_cat + 1];
            } else {
                next_cat_end = state->result->count;
            }
        } else {
            next_cat_start = 0;
            next_cat_end = state->result->count;
        }

        /// Go to first row of next category, preserving target column
        size_t items_in_next_cat = next_cat_end - next_cat_start;
        size_t items_in_first_row =
            items_in_next_cat < columns ? items_in_next_cat : columns;

        size_t new_col = state->target_column;
        if (new_col >= items_in_first_row) {
            new_col = items_in_first_row - 1;
        }
        state->selected_index = next_cat_start + new_col;
    }

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move selection up in the completion menu
 *
 * Moves to the previous row within the current category, or wraps to the
 * last row of the previous category if at the top.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_move_up(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t columns = get_columns(state);

    /// Find current category
    size_t cat_start, cat_end;
    size_t current_cat = find_category_for_index(state, state->selected_index,
                                                 &cat_start, &cat_end);

    /// Calculate position within current category
    size_t index_in_cat = state->selected_index - cat_start;
    size_t current_row_in_cat = index_in_cat / columns;

    if (current_row_in_cat > 0) {
        /// Stay in same category, move to previous row
        size_t prev_row = current_row_in_cat - 1;
        size_t row_start = cat_start + prev_row * columns;
        size_t row_end = row_start + columns;
        if (row_end > cat_end) {
            row_end = cat_end;
        }
        size_t items_in_row = row_end - row_start;

        /// Use target column, fall back to last item in row
        size_t new_col = state->target_column;
        if (new_col >= items_in_row) {
            new_col = items_in_row - 1;
        }
        state->selected_index = row_start + new_col;
    } else {
        /// Move to previous category (or wrap to last)
        size_t prev_cat;
        if (state->category_count > 0 && current_cat > 0) {
            prev_cat = current_cat - 1;
        } else if (state->category_count > 0) {
            prev_cat = state->category_count - 1; /// Wrap to last category
        } else {
            prev_cat = 0;
        }

        /// Get previous category boundaries
        size_t prev_cat_start, prev_cat_end;
        if (state->category_count > 0) {
            prev_cat_start = state->category_positions[prev_cat];
            if (prev_cat + 1 < state->category_count) {
                prev_cat_end = state->category_positions[prev_cat + 1];
            } else {
                prev_cat_end = state->result->count;
            }
        } else {
            prev_cat_start = 0;
            prev_cat_end = state->result->count;
        }

        /// Go to last row of previous category, preserving target column
        size_t items_in_prev_cat = prev_cat_end - prev_cat_start;
        size_t rows_in_prev_cat = (items_in_prev_cat + columns - 1) / columns;
        size_t last_row = rows_in_prev_cat > 0 ? rows_in_prev_cat - 1 : 0;

        size_t row_start = prev_cat_start + last_row * columns;
        size_t row_end = row_start + columns;
        if (row_end > prev_cat_end) {
            row_end = prev_cat_end;
        }
        size_t items_in_row = row_end - row_start;

        size_t new_col = state->target_column;
        if (new_col >= items_in_row) {
            new_col = items_in_row - 1;
        }
        state->selected_index = row_start + new_col;
    }

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move selection down by one page
 *
 * Jumps down by visible_count items, stopping at the last item.
 * Does not wrap around.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_page_down(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t total_items = state->result->count;

    /// Move down by visible_count, stop at last item (no wrap)
    state->selected_index += state->visible_count;
    if (state->selected_index >= total_items) {
        state->selected_index = total_items - 1;
    }

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move selection up by one page
 *
 * Jumps up by visible_count items, stopping at the first item.
 * Does not wrap around.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_page_up(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Move up by visible_count, stop at first item (no wrap)
    if (state->selected_index < state->visible_count) {
        state->selected_index = 0;
    } else {
        state->selected_index -= state->visible_count;
    }

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move selection right within the current row
 *
 * Moves to the next column in the current row, wrapping to the first
 * column if at the end. Updates the target column for sticky behavior.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_move_right(lle_completion_menu_state_t *state) {
    if (state == NULL || !state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t columns = get_columns(state);

    /// Find current category
    size_t cat_start, cat_end;
    find_category_for_index(state, state->selected_index, &cat_start, &cat_end);

    /// Calculate position within current category
    size_t index_in_cat = state->selected_index - cat_start;
    size_t current_row_in_cat = index_in_cat / columns;
    size_t current_col = index_in_cat % columns;

    /// Calculate row boundaries within category
    size_t row_start_in_cat = current_row_in_cat * columns;
    size_t row_end_in_cat = row_start_in_cat + columns;
    size_t items_in_cat = cat_end - cat_start;
    if (row_end_in_cat > items_in_cat) {
        row_end_in_cat = items_in_cat;
    }
    size_t items_in_row = row_end_in_cat - row_start_in_cat;

    /// Move to next column
    current_col++;

    /// If we went past the last item in row, wrap to first column
    if (current_col >= items_in_row) {
        current_col = 0;
    }

    state->selected_index = cat_start + row_start_in_cat + current_col;

    /// Update target column for sticky behavior
    state->target_column = current_col;

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move selection left within the current row
 *
 * Moves to the previous column in the current row, wrapping to the last
 * column if at the beginning. Updates the target column for sticky behavior.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_move_left(lle_completion_menu_state_t *state) {
    if (state == NULL || !state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    size_t columns = get_columns(state);

    /// Find current category
    size_t cat_start, cat_end;
    find_category_for_index(state, state->selected_index, &cat_start, &cat_end);

    /// Calculate position within current category
    size_t index_in_cat = state->selected_index - cat_start;
    size_t current_row_in_cat = index_in_cat / columns;
    size_t current_col = index_in_cat % columns;

    /// Calculate row boundaries within category
    size_t row_start_in_cat = current_row_in_cat * columns;
    size_t row_end_in_cat = row_start_in_cat + columns;
    size_t items_in_cat = cat_end - cat_start;
    if (row_end_in_cat > items_in_cat) {
        row_end_in_cat = items_in_cat;
    }
    size_t items_in_row = row_end_in_cat - row_start_in_cat;

    /// Move to previous column
    if (current_col == 0) {
        /// Wrap to last item in this row
        current_col = items_in_row - 1;
    } else {
        current_col--;
    }

    state->selected_index = cat_start + row_start_in_cat + current_col;

    /// Update target column for sticky behavior
    state->target_column = current_col;

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move to next item sequentially (TAB cycling)
 *
 * Moves right across columns, then wraps to first column of next row.
 * Wraps from last item to first item.
 */
lle_result_t lle_completion_menu_move_next(lle_completion_menu_state_t *state) {
    if (state == NULL || !state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Simple sequential increment with wrap
    state->selected_index++;
    if (state->selected_index >= state->result->count) {
        state->selected_index = 0;
    }

    /// Update target column for arrow key navigation
    size_t columns = get_columns(state);
    size_t cat_start, cat_end;
    find_category_for_index(state, state->selected_index, &cat_start, &cat_end);
    size_t index_in_cat = state->selected_index - cat_start;
    state->target_column = index_in_cat % columns;

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Move to previous item sequentially (Shift+TAB cycling)
 *
 * Moves left across columns, then wraps to last column of previous row.
 * Wraps from first item to last item.
 */
lle_result_t lle_completion_menu_move_prev(lle_completion_menu_state_t *state) {
    if (state == NULL || !state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Simple sequential decrement with wrap
    if (state->selected_index == 0) {
        state->selected_index = state->result->count - 1;
    } else {
        state->selected_index--;
    }

    /// Update target column for arrow key navigation
    size_t columns = get_columns(state);
    size_t cat_start, cat_end;
    find_category_for_index(state, state->selected_index, &cat_start, &cat_end);
    size_t index_in_cat = state->selected_index - cat_start;
    state->target_column = index_in_cat % columns;

    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Jump to the next category
 *
 * Moves selection to the first item of the next category,
 * wrapping to the first category if at the end.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_next_category(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->category_count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Find current category
    size_t current_category = 0;
    for (size_t i = 0; i < state->category_count; i++) {
        if (state->selected_index >= state->category_positions[i]) {
            current_category = i;
        } else {
            break;
        }
    }

    /// Move to next category (wrap to first if at end)
    current_category++;
    if (current_category >= state->category_count) {
        current_category = 0;
    }

    state->selected_index = state->category_positions[current_category];
    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Jump to the previous category
 *
 * Moves selection to the first item of the previous category,
 * wrapping to the last category if at the beginning.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_prev_category(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->category_count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Find current category
    size_t current_category = 0;
    for (size_t i = 0; i < state->category_count; i++) {
        if (state->selected_index >= state->category_positions[i]) {
            current_category = i;
        } else {
            break;
        }
    }

    /// Move to previous category (wrap to last if at beginning)
    if (current_category == 0) {
        current_category = state->category_count - 1;
    } else {
        current_category--;
    }

    state->selected_index = state->category_positions[current_category];
    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Select the first item in the menu
 *
 * Moves selection to index 0 and ensures it is visible.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_select_first(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    state->selected_index = 0;
    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Select the last item in the menu
 *
 * Moves selection to the last item and ensures it is visible.
 *
 * @param state Menu state to modify
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_select_last(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    state->selected_index = state->result->count - 1;
    ensure_visible(state);
    return LLE_SUCCESS;
}

/**
 * @brief Accept the currently selected completion item
 *
 * Returns a pointer to the selected item and deactivates the menu.
 *
 * @param state Menu state to query and deactivate
 * @param selected_item Output pointer to receive the selected item
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_completion_menu_accept(lle_completion_menu_state_t *state,
                           const lle_completion_item_t **selected_item) {
    if (state == NULL || selected_item == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->result == NULL || state->result->count == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!state->menu_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state->selected_index >= state->result->count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Return pointer to selected item
    *selected_item = &state->result->items[state->selected_index];

    /// Deactivate menu
    state->menu_active = false;

    return LLE_SUCCESS;
}

/**
 * @brief Cancel the completion menu without accepting
 *
 * Deactivates the menu without returning a selection.
 *
 * @param state Menu state to deactivate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_cancel(lle_completion_menu_state_t *state) {
    if (state == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    state->menu_active = false;
    return LLE_SUCCESS;
}

/**
 * @brief Handle a character input while menu is active
 *
 * Currently cancels the menu on any character input.
 * Future: implement incremental filtering.
 *
 * @param state Menu state to modify
 * @param c Character that was typed
 * @param should_cancel Output flag indicating if menu should be cancelled
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_completion_menu_handle_char(lle_completion_menu_state_t *state,
                                             char c, bool *should_cancel) {
    if (state == NULL || should_cancel == NULL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// For now, any character input cancels the menu
    /// Future: implement incremental filtering
    (void)c; /// Unused for now
    *should_cancel = true;
    state->menu_active = false;

    return LLE_SUCCESS;
}
