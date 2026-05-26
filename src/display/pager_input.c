/**
 * @file pager_input.c
 * @brief Pager-mode key dispatch and input loop
 *
 * Implements the contract declared in include/display/pager_input.h.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/pager_input.h"

#include "lle/lle_pager_state.h"

#define KEY_ESC 27
#define KEY_CTRL_C 3
#define KEY_CTRL_G 7
#define KEY_CTRL_F 6
#define KEY_CTRL_B 2

pager_action_t pager_key_to_action(int key, pager_mode_t mode) {
    /// Modal-quit hierarchy first: in SEARCH or HELP, Esc cancels
    /// the sub-mode without exiting the pager. Standard quit keys
    /// always quit regardless of mode.
    if (key == KEY_ESC) {
        if (mode == PAGER_MODE_SEARCH || mode == PAGER_MODE_HELP) {
            return PAGER_ACTION_CANCEL_SUBMODE;
        }
        return PAGER_ACTION_QUIT;
    }
    if (key == KEY_CTRL_C || key == KEY_CTRL_G) {
        return PAGER_ACTION_QUIT;
    }

    /// In SEARCH mode, navigation keys do nothing; only Esc (handled
    /// above) cancels back to VIEW. Pattern-typing in the search
    /// prompt is the responsibility of a separate input loop that
    /// runs while in SEARCH mode; this dispatch fields VIEW-mode
    /// keys only.
    if (mode == PAGER_MODE_SEARCH) {
        return PAGER_ACTION_NONE;
    }

    /// HELP mode: any non-Esc key returns to VIEW (cancel sub-mode).
    if (mode == PAGER_MODE_HELP) {
        return PAGER_ACTION_CANCEL_SUBMODE;
    }

    /// VIEW-mode dispatch.
    switch (key) {
    case ' ':
    case KEY_CTRL_F:
    case PAGER_KEY_PAGE_DOWN:
        return PAGER_ACTION_PAGE_DOWN;
    case 'b':
    case KEY_CTRL_B:
    case PAGER_KEY_PAGE_UP:
        return PAGER_ACTION_PAGE_UP;
    case 'j':
    case PAGER_KEY_DOWN:
        return PAGER_ACTION_LINE_DOWN;
    case 'k':
    case PAGER_KEY_UP:
        return PAGER_ACTION_LINE_UP;
    case 'g':
    case PAGER_KEY_HOME:
        return PAGER_ACTION_TOP;
    case 'G':
    case PAGER_KEY_END:
        return PAGER_ACTION_BOTTOM;
    case '/':
        return PAGER_ACTION_BEGIN_SEARCH;
    case '?':
        return PAGER_ACTION_BEGIN_SEARCH_REVERSE;
    case 'n':
        return PAGER_ACTION_NEXT_MATCH;
    case 'N':
        return PAGER_ACTION_PREV_MATCH;
    case 'h':
        return PAGER_ACTION_HELP;
    case 'q':
        return PAGER_ACTION_QUIT;
    default:
        return PAGER_ACTION_NONE;
    }
}

bool pager_apply_action(pager_layer_t *pager, pager_action_t action) {
    if (!pager) {
        return false;
    }
    switch (action) {
    case PAGER_ACTION_LINE_DOWN:
        pager_layer_scroll_lines(pager, 1);
        return false;
    case PAGER_ACTION_LINE_UP:
        pager_layer_scroll_lines(pager, -1);
        return false;
    case PAGER_ACTION_PAGE_DOWN:
        pager_layer_scroll_pages(pager, 1);
        return false;
    case PAGER_ACTION_PAGE_UP:
        pager_layer_scroll_pages(pager, -1);
        return false;
    case PAGER_ACTION_TOP:
        pager_layer_scroll_top(pager);
        return false;
    case PAGER_ACTION_BOTTOM:
        pager_layer_scroll_bottom(pager);
        return false;
    case PAGER_ACTION_BEGIN_SEARCH:
    case PAGER_ACTION_BEGIN_SEARCH_REVERSE:
        /// The search engine itself lands separately. For now,
        /// transition to SEARCH mode and clear any prior pattern;
        /// the search-prompt input loop populates the pattern.
        pager_layer_set_mode(pager, PAGER_MODE_SEARCH);
        pager_layer_clear_search(pager);
        return false;
    case PAGER_ACTION_NEXT_MATCH:
    case PAGER_ACTION_PREV_MATCH:
        /// No-op until the search engine lands; the action exists
        /// for the dispatch to recognise the key without dropping
        /// the user into PAGER_ACTION_NONE confusion.
        return false;
    case PAGER_ACTION_HELP:
        pager_layer_set_mode(pager, PAGER_MODE_HELP);
        return false;
    case PAGER_ACTION_CANCEL_SUBMODE:
        pager_layer_set_mode(pager, PAGER_MODE_VIEW);
        return false;
    case PAGER_ACTION_QUIT:
        return true;
    case PAGER_ACTION_NONE:
    default:
        return false;
    }
}

void pager_run_input_loop(pager_layer_t *pager, pager_key_source_fn src,
                          void *ud) {
    if (!pager || !src) {
        return;
    }
    lle_set_pager_active(true);
    for (;;) {
        int key = src(ud);
        if (key < 0) {
            /// EOF / error from the source -- exit cleanly.
            break;
        }
        pager_action_t act = pager_key_to_action(key, pager->mode);
        bool should_exit = pager_apply_action(pager, act);
        if (should_exit) {
            break;
        }
    }
    lle_set_pager_active(false);
}
