/**
 * @file lle_pager_state.h
 * @brief Active-mode flag for the LLE pager
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Parallel to lle_debug_prompt_state: a single boolean answering "is
 * a pager-mode input loop currently in flight?" Code that needs to
 * know the difference between editing a command and reading
 * paginated content -- LLE keybinding dispatchers, completion
 * sources, prompt rendering -- consults lle_in_pager() rather than
 * inferring from a context-specific signal.
 *
 * Kept in its own translation unit so consumers that only need the
 * probe (and not the full pager-input module) don't drag the input
 * loop's dependencies into their link.
 */

#ifndef LLE_PAGER_STATE_H
#define LLE_PAGER_STATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Is a pager-mode input loop currently active?
 *
 * Set to true between pager_run_input_loop entry and its return.
 * Consumers read this flag to switch behaviour (e.g. LLE input
 * routing, completion availability) for the duration.
 */
bool lle_in_pager(void);

/**
 * @brief Set the pager-active flag
 *
 * Called by the pager input loop on entry (`true`) and exit
 * (`false`). External callers should not flip this directly.
 */
void lle_set_pager_active(bool active);

#ifdef __cplusplus
}
#endif

#endif /// LLE_PAGER_STATE_H
