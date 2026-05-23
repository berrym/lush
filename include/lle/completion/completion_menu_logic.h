/**
 * @file completion_menu_logic.h
 * @brief Completion Menu Navigation Logic
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides navigation and interaction logic for the completion menu.
 * All functions work with menu state; no rendering is performed here.
 * Rendering is handled by the display layer per LLE architecture.
 *
 * Navigation Actions:
 * - Single item: up, down
 * - Paging: page up, page down
 * - Category jumping: next category, previous category
 * - Selection: first, last
 *
 * All navigation respects the current menu configuration (scrolling,
 * visible items, etc.) and updates the menu state accordingly.
 */

#ifndef LLE_COMPLETION_MENU_LOGIC_H
#define LLE_COMPLETION_MENU_LOGIC_H

#include "lle/completion/completion_menu_state.h"
#include "lle/error_handling.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Move selection down by one item
 *
 * Wraps to first item if at end. Updates first_visible if needed
 * to keep selection visible.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_down(lle_completion_menu_state_t *state);

/**
 * @brief Move selection up by one item
 *
 * Wraps to last item if at beginning. Updates first_visible if needed
 * to keep selection visible.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_up(lle_completion_menu_state_t *state);

/**
 * @brief Move selection right in menu (next column)
 *
 * Wraps to first column when at last.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_right(lle_completion_menu_state_t *state);

/**
 * @brief Move selection left in menu (previous column)
 *
 * Wraps to last column when at first.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_left(lle_completion_menu_state_t *state);

/**
 * @brief Move to next item sequentially (TAB cycling)
 *
 * Moves right across columns, then wraps to first column of next row.
 * Wraps from last item to first item.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_next(lle_completion_menu_state_t *state);

/**
 * @brief Move to previous item sequentially (Shift+TAB cycling)
 *
 * Moves left across columns, then wraps to last column of previous row.
 * Wraps from first item to last item.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_move_prev(lle_completion_menu_state_t *state);

/**
 * @brief Move selection down by one page
 *
 * A page is defined by visible_count items. Does not wrap - stops at last item.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_page_down(lle_completion_menu_state_t *state);

/**
 * @brief Move selection up by one page
 *
 * A page is defined by visible_count items. Does not wrap - stops at first
 * item.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_page_up(lle_completion_menu_state_t *state);

/**
 * @brief Jump to the next category
 *
 * Categories are determined by completion type. Wraps to first category if at
 * end.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_next_category(lle_completion_menu_state_t *state);

/**
 * @brief Jump to the previous category
 *
 * Categories are determined by completion type. Wraps to last category if at
 * beginning.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_prev_category(lle_completion_menu_state_t *state);

/**
 * @brief Move selection to first item
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_select_first(lle_completion_menu_state_t *state);

/**
 * @brief Move selection to last item
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_select_last(lle_completion_menu_state_t *state);

/**
 * @brief Apply the currently selected completion
 *
 * Returns the selected item via output parameter. Does NOT modify buffer -
 * caller is responsible for insertion.
 *
 * @param state Menu state
 * @param selected_item Output pointer to selected item (do not free)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_completion_menu_accept(lle_completion_menu_state_t *state,
                           const lle_completion_item_t **selected_item);

/**
 * @brief Cancel menu interaction
 *
 * Sets menu_active to false. Does not free state - caller manages lifecycle.
 *
 * @param state Menu state to update
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_cancel(lle_completion_menu_state_t *state);

/**
 * @brief Handle character input during menu interaction
 *
 * Updates filtering/search if implemented. For now, just cancels menu
 * and returns the character.
 *
 * @param state Menu state
 * @param c Input character
 * @param should_cancel Output flag indicating if menu should be dismissed
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_completion_menu_handle_char(lle_completion_menu_state_t *state,
                                             char c, bool *should_cancel);

#ifdef __cplusplus
}
#endif

#endif // LLE_COMPLETION_MENU_LOGIC_H
