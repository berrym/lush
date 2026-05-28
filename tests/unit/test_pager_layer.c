/**
 * @file test_pager_layer.c
 * @brief Unit tests for the pager display layer
 *
 * Covers lifecycle (init / destroy / re-init), scroll math at line
 * and page granularity, top / bottom anchoring, view-visible-count
 * computation, resize handling, mode transitions, and search-state
 * storage. No terminal I/O is exercised -- this is the data-and-math
 * layer.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/pager_layer.h"
#include "test_framework.h"
#include <stdio.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Helpers
 * ============================================================================
 */

static const char FIVE_LINE_CONTENT[] = "alpha\nbeta\ngamma\ndelta\nepsilon\n";

/* ============================================================================
 * Lifecycle
 * ============================================================================
 */

TEST(init_builds_line_index) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    int rc = pager_layer_init(&pager, FIVE_LINE_CONTENT,
                              strlen(FIVE_LINE_CONTENT), 10, 80);
    ASSERT_EQ(rc, 0, "init succeeds");
    ASSERT(pager.active, "layer marked active");
    ASSERT_EQ(pager.line_index.count, (size_t)5, "five logical lines");
    ASSERT_EQ(pager.top_line, (size_t)0, "view parked at top");
    ASSERT_EQ(pager.mode, PAGER_MODE_VIEW, "default mode is VIEW");
    pager_layer_destroy(&pager);
    ASSERT(!pager.active, "destroy clears active flag");
    ASSERT_EQ(pager.line_index.count, (size_t)0, "line index released");
}

TEST(destroy_safe_on_zeroed_struct) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_destroy(&pager); /// must not crash on never-initialized
}

TEST(destroy_safe_on_double_call) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "x", 1, 5, 80);
    pager_layer_destroy(&pager);
    pager_layer_destroy(&pager); /// idempotent
}

TEST(init_with_empty_content) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    int rc = pager_layer_init(&pager, "", 0, 10, 80);
    ASSERT_EQ(rc, 0, "init succeeds on empty content");
    ASSERT_EQ(pager.line_index.count, (size_t)0, "no lines");
    ASSERT_EQ(pager.top_line, (size_t)0, "view at top");
    pager_layer_destroy(&pager);
}

TEST(reinit_overwrites_previous_content) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));

    pager_layer_init(&pager, "one\ntwo\n", 8, 5, 80);
    ASSERT_EQ(pager.line_index.count, (size_t)2, "first init");

    pager_layer_destroy(&pager);
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 10,
                     80);
    ASSERT_EQ(pager.line_index.count, (size_t)5, "second init replaces");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Scroll: lines
 * ============================================================================
 */

TEST(scroll_lines_down_advances_top) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_lines(&pager, 2);
    ASSERT_EQ(pager.top_line, (size_t)2, "moved down 2 lines");
    pager_layer_scroll_lines(&pager, 1);
    ASSERT_EQ(pager.top_line, (size_t)3, "moved down 1 more");
    pager_layer_destroy(&pager);
}

TEST(scroll_lines_up_decreases_top_clamped_at_zero) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_lines(&pager, 3);
    ASSERT_EQ(pager.top_line, (size_t)3, "moved down 3");
    pager_layer_scroll_lines(&pager, -1);
    ASSERT_EQ(pager.top_line, (size_t)2, "moved up 1");
    pager_layer_scroll_lines(&pager, -10);
    ASSERT_EQ(pager.top_line, (size_t)0, "clamped to top");
    pager_layer_destroy(&pager);
}

TEST(scroll_lines_down_clamped_at_last_line) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_lines(&pager, 100);
    ASSERT_EQ(pager.top_line, (size_t)4, "clamped to line_count - 1");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Scroll: pages
 * ============================================================================
 */

TEST(scroll_pages_uses_view_rows_stride) {
    /// 10 lines, view_rows = 4, so a page = 4 lines.
    const char *ten_lines = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n";
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, ten_lines, strlen(ten_lines), 4, 80);
    ASSERT_EQ(pager.line_index.count, (size_t)10, "10 lines");

    pager_layer_scroll_pages(&pager, 1);
    ASSERT_EQ(pager.top_line, (size_t)4, "1 page down = 4 lines");
    pager_layer_scroll_pages(&pager, 1);
    ASSERT_EQ(pager.top_line, (size_t)8, "another page");
    pager_layer_scroll_pages(&pager, 1);
    ASSERT_EQ(pager.top_line, (size_t)9, "clamped to last line");
    pager_layer_scroll_pages(&pager, -2);
    ASSERT_EQ(pager.top_line, (size_t)1, "page up clamps");
    pager_layer_destroy(&pager);
}

TEST(scroll_pages_zero_view_rows_treated_as_one) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 0,
                     80);
    pager_layer_scroll_pages(&pager, 1);
    /// view_rows = 0 should be treated as stride 1 to avoid no-op
    ASSERT_EQ(pager.top_line, (size_t)1,
              "stride defaults to 1 when view_rows=0");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Scroll: top / bottom
 * ============================================================================
 */

TEST(scroll_top_resets_to_zero) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_lines(&pager, 4);
    pager_layer_scroll_top(&pager);
    ASSERT_EQ(pager.top_line, (size_t)0, "back at top");
    pager_layer_destroy(&pager);
}

TEST(scroll_bottom_anchors_view) {
    /// 5 lines, each 1 visual row, view_rows = 3.
    /// scroll_bottom should anchor at top_line=2 so lines 2,3,4 are visible.
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_bottom(&pager);
    ASSERT_EQ(pager.top_line, (size_t)2,
              "bottom-anchor shows last 3 of 5 lines");
    pager_layer_destroy(&pager);
}

TEST(scroll_bottom_content_fits_entirely) {
    /// 5 lines, view_rows = 10. Whole content fits; bottom = top.
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 10,
                     80);
    pager_layer_scroll_bottom(&pager);
    ASSERT_EQ(pager.top_line, (size_t)0,
              "everything fits; bottom-anchor is top");
    pager_layer_destroy(&pager);
}

TEST(scroll_bottom_empty_content_is_safe) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "", 0, 5, 80);
    pager_layer_scroll_bottom(&pager); /// must not crash
    ASSERT_EQ(pager.top_line, (size_t)0, "empty content keeps top at 0");
    pager_layer_destroy(&pager);
}

TEST(scroll_bottom_respects_wrapped_lines) {
    /// 3 lines, one wraps 3x at the chosen width:
    ///   "alpha"          (1 row)
    ///   20-char line     (2 rows at width 10)
    ///   "gamma"          (1 row)
    /// view_rows = 3: bottom-anchor should put the wrapped line and
    /// gamma both visible. visual rows = 2+1 = 3 fits. So top = 1.
    const char *content = "alpha\nabcdefghijklmnopqrst\ngamma\n";
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, content, strlen(content), 3, 10);
    pager_layer_scroll_bottom(&pager);
    ASSERT_EQ(pager.top_line, (size_t)1,
              "wrapped middle line + gamma fit in view");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Visible line count
 * ============================================================================
 */

TEST(visible_count_unwrapped) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    ASSERT_EQ(pager_layer_visible_line_count(&pager), (size_t)3,
              "3 of 5 lines fit in view_rows=3");
    pager_layer_destroy(&pager);
}

TEST(visible_count_with_wrapped_line) {
    /// 1st line wraps 2x at width 10; view_rows = 3 holds wrapped + 1 more.
    const char *content = "abcdefghijklmnopqrst\nsecond\nthird\n";
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, content, strlen(content), 3, 10);
    ASSERT_EQ(pager_layer_visible_line_count(&pager), (size_t)2,
              "wrapped line (2 rows) + 'second' (1 row) = 3 rows");
    pager_layer_destroy(&pager);
}

TEST(visible_count_zero_view_rows) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 0,
                     80);
    ASSERT_EQ(pager_layer_visible_line_count(&pager), (size_t)0,
              "zero rows visible");
    pager_layer_destroy(&pager);
}

TEST(visible_count_line_taller_than_view_still_one) {
    /// A logical line that wraps to 5 rows on a 3-row view. The
    /// matcher should still report 1 visible line so the renderer
    /// has something to draw.
    const char *content = "abcdefghijklmno\n";
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, content, strlen(content), 3, 5);
    ASSERT(pager.line_index.entries[0].visual_height >= 3,
           "line is taller than view");
    ASSERT_EQ(pager_layer_visible_line_count(&pager), (size_t)1,
              "still reports 1 (avoid empty page)");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Resize
 * ============================================================================
 */

TEST(resize_recomputes_visual_heights) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    const char *content = "abcdefghijklmnopqrst\n";
    pager_layer_init(&pager, content, strlen(content), 5, 10);
    ASSERT_EQ(pager.line_index.entries[0].visual_height, (size_t)2,
              "2 rows at width 10");
    pager_layer_resize(&pager, 20, 5);
    ASSERT_EQ(pager.line_index.entries[0].visual_height, (size_t)1,
              "1 row at width 20");
    pager_layer_resize(&pager, 5, 5);
    ASSERT_EQ(pager.line_index.entries[0].visual_height, (size_t)4,
              "4 rows at width 5");
    pager_layer_destroy(&pager);
}

TEST(resize_clamps_top_line) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_scroll_lines(&pager, 4);
    ASSERT_EQ(pager.top_line, (size_t)4, "moved to last line");
    /// Resize to a width that doesn't change line count; top stays valid.
    pager_layer_resize(&pager, 20, 3);
    ASSERT_EQ(pager.top_line, (size_t)4, "top_line preserved across resize");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Mode transitions
 * ============================================================================
 */

TEST(mode_transitions) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    ASSERT_EQ(pager.mode, PAGER_MODE_VIEW, "default VIEW");
    pager_layer_set_mode(&pager, PAGER_MODE_SEARCH);
    ASSERT_EQ(pager.mode, PAGER_MODE_SEARCH, "transition to SEARCH");
    pager_layer_set_mode(&pager, PAGER_MODE_HELP);
    ASSERT_EQ(pager.mode, PAGER_MODE_HELP, "transition to HELP");
    pager_layer_set_mode(&pager, PAGER_MODE_VIEW);
    ASSERT_EQ(pager.mode, PAGER_MODE_VIEW, "back to VIEW");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * Search state (storage only)
 * ============================================================================
 */

TEST(set_search_stores_pattern_copy) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);

    int rc = pager_layer_set_search(&pager, "needle");
    ASSERT_EQ(rc, 0, "set_search succeeds");
    ASSERT_TRUE(pager.search_pattern != NULL, "pattern stored");
    ASSERT_STR_EQ(pager.search_pattern, "needle", "pattern copied");

    /// Replace
    rc = pager_layer_set_search(&pager, "haystack");
    ASSERT_EQ(rc, 0, "replace succeeds");
    ASSERT_STR_EQ(pager.search_pattern, "haystack", "pattern replaced");

    pager_layer_destroy(&pager);
}

TEST(set_search_null_clears) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_set_search(&pager, "x");
    pager_layer_set_search(&pager, NULL);
    ASSERT(pager.search_pattern == NULL, "NULL clears");
    pager_layer_destroy(&pager);
}

TEST(clear_search_is_equivalent_to_null) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, FIVE_LINE_CONTENT, strlen(FIVE_LINE_CONTENT), 3,
                     80);
    pager_layer_set_search(&pager, "x");
    pager_layer_clear_search(&pager);
    ASSERT(pager.search_pattern == NULL, "clear_search clears");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * SEARCH ENGINE (pager_layer_search_advance + buffer edits)
 * ============================================================================
 */

#define SEARCH_CONTENT                                                         \
    "alpha\nbravo\ncharlie\ndelta\necho\nfoxtrot\nalpha-too\n"
/// Lines 0..6: alpha, bravo, charlie, delta, echo, foxtrot, alpha-too

TEST(search_advance_finds_forward_match) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager_layer_set_search(&pager, "charlie");
    size_t hit = pager_layer_search_advance(&pager, 0, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, 2, "charlie is on line 2");
    pager_layer_destroy(&pager);
}

TEST(search_advance_finds_backward_match) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager_layer_set_search(&pager, "bravo");
    size_t hit = pager_layer_search_advance(&pager, 6, PAGER_SEARCH_BACKWARD);
    ASSERT_EQ(hit, 1, "bravo is on line 1; backward from 6 finds it");
    pager_layer_destroy(&pager);
}

TEST(search_advance_no_match_returns_sentinel) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager_layer_set_search(&pager, "zzz_not_present");
    size_t hit = pager_layer_search_advance(&pager, 0, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, PAGER_SEARCH_NO_MATCH, "missing pattern yields sentinel");
    pager_layer_destroy(&pager);
}

TEST(search_advance_skips_origin) {
    /// Origin line itself is excluded; advance lands on the next
    /// match. Multiple matches: "alpha" hits line 0 AND line 6;
    /// from origin 0 forward must land on 6, not 0.
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager_layer_set_search(&pager, "alpha");
    size_t hit = pager_layer_search_advance(&pager, 0, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, 6, "advance from 0 skips line 0; next alpha is at 6");
    pager_layer_destroy(&pager);
}

TEST(search_advance_wraps_when_enabled) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager.wrap_searches = true;
    pager_layer_set_search(&pager, "alpha");
    /// Forward from line 6 (the last match): wrap to 0.
    size_t hit = pager_layer_search_advance(&pager, 6, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, 0, "wrap returns to alpha at line 0");
    pager_layer_destroy(&pager);
}

TEST(search_advance_no_wrap_when_disabled) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager.wrap_searches = false;
    pager_layer_set_search(&pager, "alpha");
    size_t hit = pager_layer_search_advance(&pager, 6, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, PAGER_SEARCH_NO_MATCH, "no wrap means no match past end");
    pager_layer_destroy(&pager);
}

TEST(search_advance_empty_pattern_returns_sentinel) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    size_t hit = pager_layer_search_advance(&pager, 0, PAGER_SEARCH_FORWARD);
    ASSERT_EQ(hit, PAGER_SEARCH_NO_MATCH,
              "no committed pattern means no match");
    pager_layer_destroy(&pager);
}

TEST(search_append_byte_grows_pattern) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    ASSERT_EQ(pager_layer_search_append_byte(&pager, 'a'), 0, "append 'a'");
    ASSERT_EQ(pager_layer_search_append_byte(&pager, 'b'), 0, "append 'b'");
    ASSERT_EQ(pager_layer_search_append_byte(&pager, 'c'), 0, "append 'c'");
    ASSERT_STR_EQ(pager.search_pattern, "abc", "pattern accumulates");
    pager_layer_destroy(&pager);
}

TEST(search_backspace_shrinks_pattern) {
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, SEARCH_CONTENT, strlen(SEARCH_CONTENT), 3, 80);
    pager_layer_search_append_byte(&pager, 'a');
    pager_layer_search_append_byte(&pager, 'b');
    pager_layer_search_backspace(&pager);
    ASSERT_STR_EQ(pager.search_pattern, "a", "backspace removes last byte");
    pager_layer_search_backspace(&pager);
    ASSERT_STR_EQ(pager.search_pattern, "", "second backspace empties");
    pager_layer_search_backspace(&pager); /// no-op on empty
    ASSERT_STR_EQ(pager.search_pattern, "", "backspace on empty is safe");
    pager_layer_destroy(&pager);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running pager_layer tests...\n\n");

    printf("Lifecycle:\n");
    RUN_TEST(init_builds_line_index);
    RUN_TEST(destroy_safe_on_zeroed_struct);
    RUN_TEST(destroy_safe_on_double_call);
    RUN_TEST(init_with_empty_content);
    RUN_TEST(reinit_overwrites_previous_content);

    printf("\nScroll (lines):\n");
    RUN_TEST(scroll_lines_down_advances_top);
    RUN_TEST(scroll_lines_up_decreases_top_clamped_at_zero);
    RUN_TEST(scroll_lines_down_clamped_at_last_line);

    printf("\nScroll (pages):\n");
    RUN_TEST(scroll_pages_uses_view_rows_stride);
    RUN_TEST(scroll_pages_zero_view_rows_treated_as_one);

    printf("\nScroll (top / bottom):\n");
    RUN_TEST(scroll_top_resets_to_zero);
    RUN_TEST(scroll_bottom_anchors_view);
    RUN_TEST(scroll_bottom_content_fits_entirely);
    RUN_TEST(scroll_bottom_empty_content_is_safe);
    RUN_TEST(scroll_bottom_respects_wrapped_lines);

    printf("\nVisible line count:\n");
    RUN_TEST(visible_count_unwrapped);
    RUN_TEST(visible_count_with_wrapped_line);
    RUN_TEST(visible_count_zero_view_rows);
    RUN_TEST(visible_count_line_taller_than_view_still_one);

    printf("\nResize:\n");
    RUN_TEST(resize_recomputes_visual_heights);
    RUN_TEST(resize_clamps_top_line);

    printf("\nMode transitions:\n");
    RUN_TEST(mode_transitions);

    printf("\nSearch state:\n");
    RUN_TEST(set_search_stores_pattern_copy);
    RUN_TEST(set_search_null_clears);
    RUN_TEST(clear_search_is_equivalent_to_null);

    printf("\nSearch engine:\n");
    RUN_TEST(search_advance_finds_forward_match);
    RUN_TEST(search_advance_finds_backward_match);
    RUN_TEST(search_advance_no_match_returns_sentinel);
    RUN_TEST(search_advance_skips_origin);
    RUN_TEST(search_advance_wraps_when_enabled);
    RUN_TEST(search_advance_no_wrap_when_disabled);
    RUN_TEST(search_advance_empty_pattern_returns_sentinel);
    RUN_TEST(search_append_byte_grows_pattern);
    RUN_TEST(search_backspace_shrinks_pattern);

    return TEST_RESULT();
}
