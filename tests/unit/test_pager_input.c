/**
 * @file test_pager_input.c
 * @brief Unit tests for pager-mode key dispatch and input loop
 *
 * Three layers of coverage:
 *   - pager_key_to_action: pure mapping across all keys and modes
 *   - pager_apply_action: state mutation correctness
 *   - pager_run_input_loop: scripted-key-source drives the loop to
 *     verify quit propagation, mode transitions, and the lle_in_pager
 *     probe.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/pager_input.h"
#include "display/pager_layer.h"
#include "lle/lle_pager_state.h"
#include "test_framework.h"
#include <stdio.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

#define KEY_ESC 27
#define KEY_CTRL_C 3
#define KEY_CTRL_G 7
#define KEY_CTRL_F 6
#define KEY_CTRL_B 2

/* ============================================================================
 * pager_key_to_action: pure mapping
 * ============================================================================
 */

TEST(view_mode_navigation_keys) {
    ASSERT_EQ(pager_key_to_action(' ', PAGER_MODE_VIEW), PAGER_ACTION_PAGE_DOWN,
              "space -> page down");
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_F, PAGER_MODE_VIEW),
              PAGER_ACTION_PAGE_DOWN, "Ctrl-F -> page down");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_PAGE_DOWN, PAGER_MODE_VIEW),
              PAGER_ACTION_PAGE_DOWN, "PgDn -> page down");
    ASSERT_EQ(pager_key_to_action('b', PAGER_MODE_VIEW), PAGER_ACTION_PAGE_UP,
              "b -> page up");
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_B, PAGER_MODE_VIEW),
              PAGER_ACTION_PAGE_UP, "Ctrl-B -> page up");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_PAGE_UP, PAGER_MODE_VIEW),
              PAGER_ACTION_PAGE_UP, "PgUp -> page up");
    ASSERT_EQ(pager_key_to_action('j', PAGER_MODE_VIEW), PAGER_ACTION_LINE_DOWN,
              "j -> line down");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_DOWN, PAGER_MODE_VIEW),
              PAGER_ACTION_LINE_DOWN, "Down -> line down");
    ASSERT_EQ(pager_key_to_action('k', PAGER_MODE_VIEW), PAGER_ACTION_LINE_UP,
              "k -> line up");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_UP, PAGER_MODE_VIEW),
              PAGER_ACTION_LINE_UP, "Up -> line up");
    ASSERT_EQ(pager_key_to_action('g', PAGER_MODE_VIEW), PAGER_ACTION_TOP,
              "g -> top");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_HOME, PAGER_MODE_VIEW),
              PAGER_ACTION_TOP, "Home -> top");
    ASSERT_EQ(pager_key_to_action('G', PAGER_MODE_VIEW), PAGER_ACTION_BOTTOM,
              "G -> bottom");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_END, PAGER_MODE_VIEW),
              PAGER_ACTION_BOTTOM, "End -> bottom");
}

TEST(view_mode_search_keys) {
    ASSERT_EQ(pager_key_to_action('/', PAGER_MODE_VIEW),
              PAGER_ACTION_BEGIN_SEARCH, "/ -> search forward");
    ASSERT_EQ(pager_key_to_action('?', PAGER_MODE_VIEW),
              PAGER_ACTION_BEGIN_SEARCH_REVERSE, "? -> search reverse");
    ASSERT_EQ(pager_key_to_action('n', PAGER_MODE_VIEW),
              PAGER_ACTION_NEXT_MATCH, "n -> next match");
    ASSERT_EQ(pager_key_to_action('N', PAGER_MODE_VIEW),
              PAGER_ACTION_PREV_MATCH, "N -> prev match");
}

TEST(view_mode_help_and_quit) {
    ASSERT_EQ(pager_key_to_action('h', PAGER_MODE_VIEW), PAGER_ACTION_HELP,
              "h -> help");
    ASSERT_EQ(pager_key_to_action('q', PAGER_MODE_VIEW), PAGER_ACTION_QUIT,
              "q -> quit (light)");
    ASSERT_EQ(pager_key_to_action(KEY_ESC, PAGER_MODE_VIEW), PAGER_ACTION_QUIT,
              "Esc from VIEW -> quit (light)");
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_C, PAGER_MODE_VIEW),
              PAGER_ACTION_QUIT, "Ctrl-C -> quit");
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_G, PAGER_MODE_VIEW),
              PAGER_ACTION_QUIT, "Ctrl-G -> quit");
}

TEST(view_mode_unbound_keys) {
    ASSERT_EQ(pager_key_to_action('x', PAGER_MODE_VIEW), PAGER_ACTION_NONE,
              "x is unbound");
    ASSERT_EQ(pager_key_to_action('5', PAGER_MODE_VIEW), PAGER_ACTION_NONE,
              "5 is unbound");
    ASSERT_EQ(pager_key_to_action(PAGER_KEY_LEFT, PAGER_MODE_VIEW),
              PAGER_ACTION_NONE, "Left is unbound");
}

TEST(search_mode_esc_cancels) {
    ASSERT_EQ(pager_key_to_action(KEY_ESC, PAGER_MODE_SEARCH),
              PAGER_ACTION_CANCEL_SUBMODE, "Esc in SEARCH -> cancel sub-mode");
}

TEST(search_mode_quit_keys_still_quit) {
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_C, PAGER_MODE_SEARCH),
              PAGER_ACTION_QUIT, "Ctrl-C from SEARCH quits");
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_G, PAGER_MODE_SEARCH),
              PAGER_ACTION_QUIT, "Ctrl-G from SEARCH quits");
}

TEST(search_mode_other_keys_none) {
    /// Navigation / help / quit-letter keys don't have their VIEW-
    /// mode semantics inside SEARCH mode; they're consumed by the
    /// search prompt (which has its own input handling above this
    /// dispatch).
    ASSERT_EQ(pager_key_to_action(' ', PAGER_MODE_SEARCH), PAGER_ACTION_NONE,
              "space in SEARCH is not page-down");
    ASSERT_EQ(pager_key_to_action('q', PAGER_MODE_SEARCH), PAGER_ACTION_NONE,
              "q in SEARCH is not quit");
    ASSERT_EQ(pager_key_to_action('j', PAGER_MODE_SEARCH), PAGER_ACTION_NONE,
              "j in SEARCH does not line-down");
}

TEST(help_mode_any_key_cancels) {
    ASSERT_EQ(pager_key_to_action(KEY_ESC, PAGER_MODE_HELP),
              PAGER_ACTION_CANCEL_SUBMODE, "Esc in HELP cancels");
    ASSERT_EQ(pager_key_to_action(' ', PAGER_MODE_HELP),
              PAGER_ACTION_CANCEL_SUBMODE, "space in HELP cancels");
    ASSERT_EQ(pager_key_to_action('x', PAGER_MODE_HELP),
              PAGER_ACTION_CANCEL_SUBMODE, "x in HELP cancels");
}

TEST(help_mode_quit_keys_still_quit) {
    ASSERT_EQ(pager_key_to_action(KEY_CTRL_C, PAGER_MODE_HELP),
              PAGER_ACTION_QUIT, "Ctrl-C from HELP quits");
}

/* ============================================================================
 * pager_apply_action: state mutation
 * ============================================================================
 */

static void mk_pager(pager_layer_t *p) {
    memset(p, 0, sizeof(*p));
    const char *content = "alpha\nbeta\ngamma\ndelta\nepsilon\n";
    pager_layer_init(p, content, strlen(content), 3, 80);
}

TEST(apply_line_down_scrolls) {
    pager_layer_t p;
    mk_pager(&p);
    bool exit = pager_apply_action(&p, PAGER_ACTION_LINE_DOWN);
    ASSERT(!exit, "line down does not exit");
    ASSERT_EQ(p.top_line, (size_t)1, "scrolled 1");
    pager_layer_destroy(&p);
}

TEST(apply_page_down_scrolls_view_rows) {
    pager_layer_t p;
    mk_pager(&p);
    pager_apply_action(&p, PAGER_ACTION_PAGE_DOWN);
    ASSERT_EQ(p.top_line, (size_t)3, "page down by view_rows=3");
    pager_layer_destroy(&p);
}

TEST(apply_top_and_bottom) {
    pager_layer_t p;
    mk_pager(&p);
    pager_apply_action(&p, PAGER_ACTION_LINE_DOWN);
    pager_apply_action(&p, PAGER_ACTION_LINE_DOWN);
    pager_apply_action(&p, PAGER_ACTION_TOP);
    ASSERT_EQ(p.top_line, (size_t)0, "top resets");
    pager_apply_action(&p, PAGER_ACTION_BOTTOM);
    ASSERT_EQ(p.top_line, (size_t)2, "bottom anchors at last 3 lines");
    pager_layer_destroy(&p);
}

TEST(apply_begin_search_transitions_mode_and_clears_pattern) {
    pager_layer_t p;
    mk_pager(&p);
    pager_layer_set_search(&p, "old");
    pager_apply_action(&p, PAGER_ACTION_BEGIN_SEARCH);
    ASSERT_EQ(p.mode, PAGER_MODE_SEARCH, "mode -> SEARCH");
    ASSERT(p.search_pattern == NULL, "prior pattern cleared");
    pager_layer_destroy(&p);
}

TEST(apply_help_transitions_to_help) {
    pager_layer_t p;
    mk_pager(&p);
    pager_apply_action(&p, PAGER_ACTION_HELP);
    ASSERT_EQ(p.mode, PAGER_MODE_HELP, "mode -> HELP");
    pager_layer_destroy(&p);
}

TEST(apply_cancel_submode_returns_to_view) {
    pager_layer_t p;
    mk_pager(&p);
    pager_layer_set_mode(&p, PAGER_MODE_HELP);
    pager_apply_action(&p, PAGER_ACTION_CANCEL_SUBMODE);
    ASSERT_EQ(p.mode, PAGER_MODE_VIEW, "mode -> VIEW");
    pager_layer_destroy(&p);
}

TEST(apply_quit_signals_exit) {
    pager_layer_t p;
    mk_pager(&p);
    bool exit = pager_apply_action(&p, PAGER_ACTION_QUIT);
    ASSERT(exit, "QUIT signals loop exit");
    pager_layer_destroy(&p);
}

TEST(apply_none_is_noop) {
    pager_layer_t p;
    mk_pager(&p);
    size_t before = p.top_line;
    bool exit = pager_apply_action(&p, PAGER_ACTION_NONE);
    ASSERT(!exit, "NONE does not exit");
    ASSERT_EQ(p.top_line, before, "NONE does not move");
    pager_layer_destroy(&p);
}

/* ============================================================================
 * pager_run_input_loop: scripted key source
 * ============================================================================
 */

typedef struct {
    const int *keys;
    size_t count;
    size_t pos;
} scripted_source_t;

static int scripted_read(void *userdata) {
    scripted_source_t *s = (scripted_source_t *)userdata;
    if (s->pos >= s->count) {
        return -1; /// EOF -- loop exits cleanly
    }
    return s->keys[s->pos++];
}

TEST(loop_quit_via_q_exits) {
    pager_layer_t p;
    mk_pager(&p);
    int keys[] = {'j', 'j', 'q'};
    scripted_source_t src = {keys, 3, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.top_line, (size_t)2, "scrolled twice before quit");
    ASSERT(!lle_in_pager(), "active flag cleared on loop exit");
    pager_layer_destroy(&p);
}

typedef struct {
    bool flag_seen_during_loop;
    int step;
} active_flag_state_t;

static int active_flag_read(void *ud) {
    active_flag_state_t *s = (active_flag_state_t *)ud;
    s->flag_seen_during_loop = lle_in_pager();
    s->step++;
    return 'q'; /// quit immediately on first call
}

TEST(loop_sets_active_flag_while_running) {
    pager_layer_t p;
    mk_pager(&p);
    active_flag_state_t state = {false, 0};
    pager_run_input_loop(&p, active_flag_read, &state);
    ASSERT(state.flag_seen_during_loop,
           "lle_in_pager() returns true inside the loop");
    ASSERT(!lle_in_pager(), "lle_in_pager() returns false after the loop");
    pager_layer_destroy(&p);
}

TEST(loop_quit_via_esc_exits) {
    pager_layer_t p;
    mk_pager(&p);
    /// Esc must map to QUIT and exit the loop before the trailing 'j'
    /// (LINE_DOWN) is read; if Esc were ignored, 'j' would scroll top_line
    /// to 1 before the source's natural EOF ended the loop.
    int keys[] = {KEY_ESC, 'j'};
    scripted_source_t src = {keys, 2, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT(!lle_in_pager(), "active flag cleared");
    ASSERT_EQ(p.top_line, (size_t)0, "Esc quit before 'j' could scroll");
    pager_layer_destroy(&p);
}

TEST(loop_quit_via_ctrl_c_exits) {
    pager_layer_t p;
    mk_pager(&p);
    /// Ctrl-C must map to QUIT and exit before the trailing 'j' scrolls.
    int keys[] = {KEY_CTRL_C, 'j'};
    scripted_source_t src = {keys, 2, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT(!lle_in_pager(), "active flag cleared after Ctrl-C exit");
    ASSERT_EQ(p.top_line, (size_t)0, "Ctrl-C quit before 'j' could scroll");
    pager_layer_destroy(&p);
}

TEST(loop_search_then_cancel_then_quit) {
    pager_layer_t p;
    mk_pager(&p);
    /// / enters search, Esc cancels back to view, q quits
    int keys[] = {'/', KEY_ESC, 'q'};
    scripted_source_t src = {keys, 3, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.mode, PAGER_MODE_VIEW, "returned to VIEW before quitting");
    pager_layer_destroy(&p);
}

TEST(loop_search_types_pattern_commits_and_jumps) {
    /// Content set by mk_pager: alpha\nbeta\ngamma\ndelta\nepsilon
    /// / enters SEARCH mode, type "gamm", Enter commits, q quits.
    /// Expected: top_line lands on the gamma line (index 2).
    pager_layer_t p;
    mk_pager(&p);
    int keys[] = {'/', 'g', 'a', 'm', 'm', '\r', 'q'};
    scripted_source_t src = {keys, 7, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.mode, PAGER_MODE_VIEW, "ended in VIEW");
    ASSERT_EQ(p.top_line, (size_t)2, "view jumped to gamma");
    ASSERT_EQ(p.current_match_line, (size_t)2, "current match recorded");
    pager_layer_destroy(&p);
}

TEST(loop_search_reverse_finds_earlier_match) {
    pager_layer_t p;
    mk_pager(&p);
    /// Scroll down first via 'j' three times, then ? for reverse,
    /// type "beta", Enter commits, q quits. Expected: top_line on
    /// beta (line 1).
    int keys[] = {'j', 'j', 'j', '?', 'b', 'e', 't', 'a', '\r', 'q'};
    scripted_source_t src = {keys, 10, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.mode, PAGER_MODE_VIEW, "ended in VIEW");
    ASSERT_EQ(p.top_line, (size_t)1, "reverse search jumped backward");
    ASSERT_EQ(p.search_direction, PAGER_SEARCH_BACKWARD,
              "direction recorded as backward");
    pager_layer_destroy(&p);
}

TEST(loop_search_backspace_corrects_typed_pattern) {
    pager_layer_t p;
    mk_pager(&p);
    /// Type "gaXm" then backspace twice and continue with "mm" so
    /// the final pattern is "gamm"; expects gamma match at line 2.
    int keys[] = {'/', 'g', 'a', 'X', 'm', 8, 8, 'm', 'm', '\r', 'q'};
    scripted_source_t src = {keys, 11, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.top_line, (size_t)2, "backspace-corrected pattern matched");
    pager_layer_destroy(&p);
}

TEST(loop_search_n_repeats_in_direction) {
    pager_layer_t p;
    /// Three matches so n's stepping is visible without wrap
    /// ambiguity. Lines 0, 2, 4 contain "alpha"; from origin 0 the
    /// initial / search jumps to line 2, then n advances to line 4.
    memset(&p, 0, sizeof(p));
    const char *content = "alpha-1\nbeta\nalpha-2\ngamma\nalpha-3\ndelta\n";
    pager_layer_init(&p, content, strlen(content), 3, 80);
    int keys[] = {'/', 'a', 'l', 'p', 'h', 'a', '\r', 'n', 'q'};
    scripted_source_t src = {keys, 9, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.top_line, (size_t)4, "n advanced past the second match");
    pager_layer_destroy(&p);
}

TEST(loop_search_capital_n_reverses_direction) {
    pager_layer_t p;
    memset(&p, 0, sizeof(p));
    const char *content = "alpha\nbeta\ncharlie\nalpha-too\n";
    pager_layer_init(&p, content, strlen(content), 3, 80);
    /// / alpha Enter -> lands on alpha-too (line 3 from origin 0).
    /// N reverses direction; from line 3 backward to line 0.
    int keys[] = {'/', 'a', 'l', 'p', 'h', 'a', '\r', 'N', 'q'};
    scripted_source_t src = {keys, 9, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.top_line, (size_t)0, "N reversed back to first alpha");
    pager_layer_destroy(&p);
}

TEST(loop_help_then_cancel_then_quit) {
    pager_layer_t p;
    mk_pager(&p);
    int keys[] = {'h', 'x', 'q'};
    /// h -> HELP, then any key (x) cancels back to VIEW, then q quits
    scripted_source_t src = {keys, 3, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.mode, PAGER_MODE_VIEW, "back to VIEW after HELP cancel");
    pager_layer_destroy(&p);
}

TEST(loop_unbound_keys_dont_exit) {
    pager_layer_t p;
    mk_pager(&p);
    int keys[] = {'x', '5', 'z', 'q'};
    scripted_source_t src = {keys, 4, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT_EQ(p.top_line, (size_t)0, "unbound keys did not scroll");
    pager_layer_destroy(&p);
}

TEST(loop_eof_exits_cleanly) {
    pager_layer_t p;
    mk_pager(&p);
    /// Empty key source means immediate EOF -- loop should exit cleanly.
    scripted_source_t src = {NULL, 0, 0};
    pager_run_input_loop(&p, scripted_read, &src);
    ASSERT(!lle_in_pager(), "active flag cleared after EOF exit");
    pager_layer_destroy(&p);
}

TEST(loop_null_args_safe) {
    pager_run_input_loop(NULL, scripted_read, NULL); /// no crash
    pager_layer_t p;
    mk_pager(&p);
    pager_run_input_loop(&p, NULL, NULL); /// no crash
    pager_layer_destroy(&p);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running pager input tests...\n\n");

    printf("Key -> action (VIEW mode):\n");
    RUN_TEST(view_mode_navigation_keys);
    RUN_TEST(view_mode_search_keys);
    RUN_TEST(view_mode_help_and_quit);
    RUN_TEST(view_mode_unbound_keys);

    printf("\nKey -> action (SEARCH mode):\n");
    RUN_TEST(search_mode_esc_cancels);
    RUN_TEST(search_mode_quit_keys_still_quit);
    RUN_TEST(search_mode_other_keys_none);

    printf("\nKey -> action (HELP mode):\n");
    RUN_TEST(help_mode_any_key_cancels);
    RUN_TEST(help_mode_quit_keys_still_quit);

    printf("\nApply action -> state mutation:\n");
    RUN_TEST(apply_line_down_scrolls);
    RUN_TEST(apply_page_down_scrolls_view_rows);
    RUN_TEST(apply_top_and_bottom);
    RUN_TEST(apply_begin_search_transitions_mode_and_clears_pattern);
    RUN_TEST(apply_help_transitions_to_help);
    RUN_TEST(apply_cancel_submode_returns_to_view);
    RUN_TEST(apply_quit_signals_exit);
    RUN_TEST(apply_none_is_noop);

    printf("\nInput loop (scripted key source):\n");
    RUN_TEST(loop_quit_via_q_exits);
    RUN_TEST(loop_sets_active_flag_while_running);
    RUN_TEST(loop_quit_via_esc_exits);
    RUN_TEST(loop_quit_via_ctrl_c_exits);
    RUN_TEST(loop_search_then_cancel_then_quit);
    RUN_TEST(loop_search_types_pattern_commits_and_jumps);
    RUN_TEST(loop_search_reverse_finds_earlier_match);
    RUN_TEST(loop_search_backspace_corrects_typed_pattern);
    RUN_TEST(loop_search_n_repeats_in_direction);
    RUN_TEST(loop_search_capital_n_reverses_direction);
    RUN_TEST(loop_help_then_cancel_then_quit);
    RUN_TEST(loop_unbound_keys_dont_exit);
    RUN_TEST(loop_eof_exits_cleanly);
    RUN_TEST(loop_null_args_safe);

    return TEST_RESULT();
}
