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
        /// Transition to SEARCH mode and clear any prior pattern;
        /// the SEARCH-mode keys append into search_pattern, and
        /// Enter commits the search via pager_layer_search_advance.
        pager_layer_set_mode(pager, PAGER_MODE_SEARCH);
        pager_layer_clear_search(pager);
        pager->search_direction = PAGER_SEARCH_FORWARD;
        return false;
    case PAGER_ACTION_BEGIN_SEARCH_REVERSE:
        pager_layer_set_mode(pager, PAGER_MODE_SEARCH);
        pager_layer_clear_search(pager);
        pager->search_direction = PAGER_SEARCH_BACKWARD;
        return false;
    case PAGER_ACTION_NEXT_MATCH: {
        /// Repeat the last search in its original direction.
        size_t origin = (pager->current_match_line != PAGER_SEARCH_NO_MATCH)
                            ? pager->current_match_line
                            : pager->top_line;
        size_t hit =
            pager_layer_search_advance(pager, origin, pager->search_direction);
        if (hit != PAGER_SEARCH_NO_MATCH) {
            pager->current_match_line = hit;
            pager->top_line = hit;
        }
        return false;
    }
    case PAGER_ACTION_PREV_MATCH: {
        /// Repeat the last search in the opposite direction.
        pager_search_direction_t rev =
            (pager->search_direction == PAGER_SEARCH_FORWARD)
                ? PAGER_SEARCH_BACKWARD
                : PAGER_SEARCH_FORWARD;
        size_t origin = (pager->current_match_line != PAGER_SEARCH_NO_MATCH)
                            ? pager->current_match_line
                            : pager->top_line;
        size_t hit = pager_layer_search_advance(pager, origin, rev);
        if (hit != PAGER_SEARCH_NO_MATCH) {
            pager->current_match_line = hit;
            pager->top_line = hit;
        }
        return false;
    }
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

/**
 * @brief Per-key handler for SEARCH mode
 *
 * Routes character keys into pager_layer_search_append_byte so the
 * user can type the search pattern visibly at the search prompt
 * (rendered by display_controller_render_pager's SEARCH-mode
 * branch). Enter commits via pager_layer_search_advance and
 * transitions back to VIEW; Esc / Ctrl-C / Ctrl-G cancel without
 * committing. Returns true to terminate the outer loop entirely
 * (only on EOF; the cancel paths just go back to VIEW).
 */
static bool pager_handle_search_key(pager_layer_t *pager, int key) {
    /// Cancel / abort: Esc, Ctrl-C, single Ctrl-G all drop the
    /// in-progress pattern and return to VIEW. The nuclear
    /// Ctrl-G x3 quit is detected by the outer loop, not here.
    if (key == KEY_ESC || key == KEY_CTRL_C || key == KEY_CTRL_G) {
        pager_layer_clear_search(pager);
        pager_layer_set_mode(pager, PAGER_MODE_VIEW);
        return false;
    }
    /// Enter / Return: commit the pattern and execute the search.
    /// An empty pattern at commit time leaves search_pattern empty
    /// and current_match_line at NO_MATCH; future n / N press is a
    /// no-op until the user runs another search.
    if (key == '\r' || key == '\n') {
        if (pager->search_pattern && pager->search_pattern[0] != '\0') {
            size_t hit = pager_layer_search_advance(pager, pager->top_line,
                                                    pager->search_direction);
            if (hit != PAGER_SEARCH_NO_MATCH) {
                pager->current_match_line = hit;
                pager->top_line = hit;
            }
        }
        pager_layer_set_mode(pager, PAGER_MODE_VIEW);
        return false;
    }
    /// Backspace: shrink the pattern one byte. Both 0x7F (Delete in
    /// most terminal mappings of Backspace) and 0x08 (literal BS)
    /// are accepted.
    if (key == 127 || key == 8) {
        pager_layer_search_backspace(pager);
        return false;
    }
    /// Printable ASCII and any byte in the UTF-8 continuation range
    /// (>= 0x80, < 0xFF) appends to the pattern. The pattern is
    /// stored as raw UTF-8; multi-byte sequences delivered as
    /// successive bytes accumulate naturally.
    if ((key >= 0x20 && key < 0x7F) || (key >= 0x80 && key < 0xFF)) {
        (void)pager_layer_search_append_byte(pager, (unsigned char)key);
    }
    /// Unrecognised control sequence inside SEARCH mode -- ignore.
    return false;
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
        if (pager->mode == PAGER_MODE_SEARCH) {
            /// In SEARCH mode, characters build the pending pattern
            /// rather than dispatch through the VIEW-mode action
            /// table. The mode itself transitions back to VIEW when
            /// the user commits (Enter) or cancels (Esc).
            if (pager_handle_search_key(pager, key)) {
                break;
            }
            continue;
        }
        pager_action_t act = pager_key_to_action(key, pager->mode);
        bool should_exit = pager_apply_action(pager, act);
        if (should_exit) {
            break;
        }
    }
    lle_set_pager_active(false);
}
