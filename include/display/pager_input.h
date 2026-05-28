/**
 * @file pager_input.h
 * @brief Pager-mode key dispatch and input loop
 *
 * Translates incoming key events into pager actions and applies them
 * to a pager_layer. Two entry points:
 *
 *   - pager_dispatch_key(): pure key->action mapping + state mutation,
 *     no I/O. The unit of testability.
 *   - pager_run_input_loop(): callback-driven loop -- the caller
 *     supplies a key-reader callback so a production caller can wire
 *     it to the real terminal while tests inject scripted key streams.
 *
 * Quit hierarchy (per project memory project-modal-quit-hierarchy):
 *   - Esc / q (in VIEW): light quit
 *   - Esc (in SEARCH / HELP): cancel sub-mode, return to VIEW
 *   - Ctrl-C: standard quit
 *   - Ctrl-G x3 in rapid succession: nuclear force-exit
 *
 * The nuclear three-press detection lives at the loop level so the
 * dispatch function stays pure. Single Ctrl-G is treated as a
 * standard quit pending the integration with the LLE input layer's
 * timer support.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef PAGER_INPUT_H
#define PAGER_INPUT_H

#include "display/pager_layer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Special-key sentinels above the ASCII range
 *
 * ASCII keys are represented by their byte value (0-127) so a `char`
 * directly maps to a key code. Special keys (arrows, function keys)
 * use sentinel values >= 256 to avoid collision with any byte.
 */
typedef enum {
    PAGER_KEY_UP = 256,
    PAGER_KEY_DOWN,
    PAGER_KEY_LEFT,
    PAGER_KEY_RIGHT,
    PAGER_KEY_PAGE_UP,
    PAGER_KEY_PAGE_DOWN,
    PAGER_KEY_HOME,
    PAGER_KEY_END,
} pager_special_key_t;

/**
 * @brief Pager-mode actions
 *
 * Output of pager_key_to_action; consumed by pager_apply_action.
 * The action set is intentionally small -- navigation + search
 * triggers + modal exits. Mode-specific behaviors (e.g. the search
 * prompt's character-by-character pattern buildup) are not
 * actions; the search prompt has its own input handling once
 * BEGIN_SEARCH activates SEARCH mode.
 */
typedef enum {
    PAGER_ACTION_NONE = 0, ///< Key is unbound in current mode
    PAGER_ACTION_LINE_DOWN,
    PAGER_ACTION_LINE_UP,
    PAGER_ACTION_PAGE_DOWN,
    PAGER_ACTION_PAGE_UP,
    PAGER_ACTION_TOP,
    PAGER_ACTION_BOTTOM,
    PAGER_ACTION_BEGIN_SEARCH,         ///< / from VIEW -> SEARCH mode
    PAGER_ACTION_BEGIN_SEARCH_REVERSE, ///< ? from VIEW -> SEARCH mode
    PAGER_ACTION_NEXT_MATCH,           ///< n
    PAGER_ACTION_PREV_MATCH,           ///< N
    PAGER_ACTION_HELP,                 ///< h: toggle help overlay
    PAGER_ACTION_CANCEL_SUBMODE,       ///< Esc in SEARCH or HELP
    PAGER_ACTION_QUIT,                 ///< standard quit -> exit loop
} pager_action_t;

/**
 * @brief Map a key event to a pager action given the current mode
 *
 * Pure function: same (key, mode) inputs always produce the same
 * action. Does not mutate state.
 *
 * @param key  Key event (ASCII byte 0-127 or pager_special_key_t)
 * @param mode Current pager mode (VIEW / SEARCH / HELP)
 * @return Action to perform; PAGER_ACTION_NONE for unbound keys
 */
pager_action_t pager_key_to_action(int key, pager_mode_t mode);

/**
 * @brief Apply an action to the pager layer
 *
 * Mutates the pager (scrolls, transitions mode, etc.) per the
 * action. Returns true if the loop should exit (currently:
 * PAGER_ACTION_QUIT).
 *
 * @param pager  Layer to mutate
 * @param action Action to apply
 * @return true if the input loop should terminate
 */
bool pager_apply_action(pager_layer_t *pager, pager_action_t action);

/**
 * @brief Key-reader callback contract
 *
 * Called by pager_run_input_loop to obtain the next key event.
 * Implementations return a non-negative key code (ASCII byte or
 * pager_special_key_t) or a negative value to signal EOF / error /
 * disconnect, which terminates the loop.
 */
typedef int (*pager_key_source_fn)(void *userdata);

/**
 * @brief Run the pager input loop
 *
 * Sets `lle_in_pager()` true on entry. Repeatedly calls `src` for
 * key events, dispatches them via pager_key_to_action + pager_apply_action,
 * and exits when an action returns true (currently only QUIT) or
 * when the source signals EOF. Clears `lle_in_pager()` on exit.
 *
 * Renders are not driven from this loop -- the display controller's
 * pager branch composes frames; this loop just consumes input and
 * updates layer state. The integration layer (lle_pager_present)
 * arranges for the loop and the render cycle to take turns.
 *
 * @param pager  Pager layer (must be initialized and active)
 * @param src    Key source callback
 * @param ud     Opaque userdata passed to each src() call
 */
void pager_run_input_loop(pager_layer_t *pager, pager_key_source_fn src,
                          void *ud);

#ifdef __cplusplus
}
#endif

#endif /// PAGER_INPUT_H
