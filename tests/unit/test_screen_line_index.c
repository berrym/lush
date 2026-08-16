/**
 * @file test_screen_line_index.c
 * @brief Unit tests for screen_buffer's line-index helper
 *
 * Verifies the pure-cursor-math invariants the pager layer will depend
 * on: correct byte spans, correct visual_height when wrapping at the
 * terminal width, correct handling of empty input, missing-trailing-
 * newline, ANSI escape skipping, UTF-8 wide-character width, and the
 * rewidth-on-resize path.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/screen_buffer.h"
#include "test_framework.h"
#include <stdio.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Empty / degenerate input
 * ============================================================================
 */

TEST(empty_content_yields_zero_entries) {
    screen_line_index_t idx = {0};
    int rc = screen_line_index_build(&idx, "", 0, 80);
    ASSERT_EQ(rc, 0, "build returns 0 on empty");
    ASSERT_EQ(idx.count, (size_t)0, "no entries");
    ASSERT_EQ(idx.total_visual_rows, (size_t)0, "no rows");
    screen_line_index_free(&idx);
}

TEST(null_content_yields_zero_entries) {
    screen_line_index_t idx = {0};
    int rc = screen_line_index_build(&idx, NULL, 0, 80);
    ASSERT_EQ(rc, 0, "build returns 0 on NULL content + 0 len");
    ASSERT_EQ(idx.count, (size_t)0, "no entries");
    screen_line_index_free(&idx);
}

TEST(zero_terminal_width_clamps_to_one) {
    screen_line_index_t idx = {0};
    const char *content = "hello\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 0);
    ASSERT_EQ(rc, 0, "build succeeds with width=0");
    ASSERT_EQ(idx.terminal_width, 1, "width clamped to 1");
    ASSERT_EQ(idx.count, (size_t)1, "one entry");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)5,
              "5 cols at width-1 = 5 rows");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * Byte-span correctness
 * ============================================================================
 */

TEST(single_line_no_newline) {
    screen_line_index_t idx = {0};
    const char *content = "hello";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)1, "one entry");
    ASSERT_EQ(idx.entries[0].byte_offset, (size_t)0, "starts at 0");
    ASSERT_EQ(idx.entries[0].byte_length, (size_t)5, "covers 'hello'");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)1, "fits on one row");
    screen_line_index_free(&idx);
}

TEST(single_line_with_trailing_newline) {
    screen_line_index_t idx = {0};
    const char *content = "hello\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)1, "one entry (trailing \\n not a row)");
    ASSERT_EQ(idx.entries[0].byte_length, (size_t)5,
              "byte_length excludes \\n");
    screen_line_index_free(&idx);
}

TEST(two_lines_with_trailing_newline) {
    screen_line_index_t idx = {0};
    const char *content = "foo\nbar\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)2, "two entries");
    ASSERT_EQ(idx.entries[0].byte_offset, (size_t)0, "first at 0");
    ASSERT_EQ(idx.entries[0].byte_length, (size_t)3, "'foo'");
    ASSERT_EQ(idx.entries[1].byte_offset, (size_t)4, "second after \\n");
    ASSERT_EQ(idx.entries[1].byte_length, (size_t)3, "'bar'");
    screen_line_index_free(&idx);
}

TEST(empty_middle_line) {
    screen_line_index_t idx = {0};
    const char *content = "a\n\nb\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)3, "three entries");
    ASSERT_EQ(idx.entries[1].byte_length, (size_t)0, "middle line is empty");
    ASSERT_EQ(idx.entries[1].visual_height, (size_t)1,
              "empty line still occupies one row");
    ASSERT_EQ(idx.total_visual_rows, (size_t)3, "total = 1 + 1 + 1");
    screen_line_index_free(&idx);
}

TEST(no_trailing_newline_after_multiple_lines) {
    screen_line_index_t idx = {0};
    const char *content = "alpha\nbeta\ngamma";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)3, "three logical lines, last has no \\n");
    ASSERT_EQ(idx.entries[2].byte_length, (size_t)5, "'gamma'");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * Visual height / wrapping
 * ============================================================================
 */

TEST(narrow_terminal_wraps_long_line) {
    screen_line_index_t idx = {0};
    /// 20 columns at width=10 -> 2 rows
    const char *content = "abcdefghijklmnopqrst\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 10);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.count, (size_t)1, "one entry");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)2,
              "20 cols on width-10 wraps to 2 rows");
    ASSERT_EQ(idx.total_visual_rows, (size_t)2, "total tracks wrap");
    screen_line_index_free(&idx);
}

TEST(exact_terminal_width_one_row) {
    screen_line_index_t idx = {0};
    /// 10 columns at width=10 -> 1 row exactly
    const char *content = "abcdefghij\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 10);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)1,
              "exact-fit content takes 1 row, not 2");
    screen_line_index_free(&idx);
}

TEST(one_over_width_two_rows) {
    screen_line_index_t idx = {0};
    /// 11 columns at width=10 -> 2 rows (1 over wraps)
    const char *content = "abcdefghijk\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 10);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)2,
              "11 cols on width-10 wraps to 2 rows");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * ANSI escape handling (0-column)
 * ============================================================================
 */

TEST(ansi_escapes_excluded_from_visual_width) {
    screen_line_index_t idx = {0};
    /// "\033[31mhi\033[0m" -- visual width is 2 ("hi"), not the byte count
    const char *content = "\033[31mhi\033[0m\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)1, "2 visible cols");
    /// byte_length is the full source-bytes length (excl. trailing \n)
    size_t expected_bytes = strlen("\033[31mhi\033[0m");
    ASSERT_EQ(idx.entries[0].byte_length, expected_bytes,
              "byte_length includes ANSI bytes; only visual_height excludes");
    screen_line_index_free(&idx);
}

TEST(ansi_styled_long_line_still_wraps_by_visual_width) {
    screen_line_index_t idx = {0};
    /// 12 visible columns of "x" wrapped in ANSI; width=5 -> 3 rows
    const char *content = "\033[1mxxxxxxxxxxxx\033[0m\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 5);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)3,
              "12 cols on width-5 = 3 rows");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * UTF-8 wide-character width
 * ============================================================================
 */

TEST(cjk_wide_char_takes_two_columns) {
    screen_line_index_t idx = {0};
    /// "U+4E2D" is one codepoint, 3 UTF-8 bytes, 2 visual columns.
    /// "U+4E2D U+6587" = 6 bytes, 4 visual columns. At width=3 -> 2 rows.
    const char *content = "\xe4\xb8\xad\xe6\x96\x87\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 3);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].byte_length, (size_t)6,
              "byte_length tracks 6 UTF-8 bytes");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)2,
              "4 visual cols on width-3 = 2 rows");
    screen_line_index_free(&idx);
}

TEST(mixed_ascii_and_wide_char) {
    screen_line_index_t idx = {0};
    /// "aU+4E2D" = 4 bytes, 3 visual cols (a=1 + U+4E2D=2)
    const char *content = "a\xe4\xb8\xad\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    ASSERT_EQ(idx.entries[0].byte_length, (size_t)4, "4 bytes");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)1,
              "3 cols on width-80 fits in 1 row");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * Rewidth (resize handling)
 * ============================================================================
 */

TEST(rewidth_recomputes_visual_heights) {
    screen_line_index_t idx = {0};
    const char *content = "abcdefghijklmnopqrst\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 10);
    ASSERT_EQ(rc, 0, "initial build at width 10");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)2, "2 rows at width 10");

    screen_line_index_rewidth(&idx, content, strlen(content), 20);
    ASSERT_EQ(idx.terminal_width, 20, "width updated");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)1, "1 row at width 20");
    ASSERT_EQ(idx.total_visual_rows, (size_t)1, "total recomputed");

    screen_line_index_rewidth(&idx, content, strlen(content), 5);
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)4,
              "20 cols on width 5 = 4 rows");
    ASSERT_EQ(idx.total_visual_rows, (size_t)4, "total recomputed again");

    screen_line_index_free(&idx);
}

TEST(rewidth_with_zero_width_clamps_to_one) {
    screen_line_index_t idx = {0};
    const char *content = "abc\n";
    int rc = screen_line_index_build(&idx, content, strlen(content), 80);
    ASSERT_EQ(rc, 0, "build succeeds");
    screen_line_index_rewidth(&idx, content, strlen(content), -1);
    ASSERT_EQ(idx.terminal_width, 1, "negative width clamped to 1");
    ASSERT_EQ(idx.entries[0].visual_height, (size_t)3,
              "3 cols at width-1 = 3 rows");
    screen_line_index_free(&idx);
}

/* ============================================================================
 * Rebuild on existing index reuses storage safely
 * ============================================================================
 */

TEST(rebuild_overwrites_existing_index) {
    screen_line_index_t idx = {0};
    const char *first = "alpha\nbeta\n";
    int rc = screen_line_index_build(&idx, first, strlen(first), 80);
    ASSERT_EQ(rc, 0, "first build");
    ASSERT_EQ(idx.count, (size_t)2, "two entries");

    const char *second = "one\ntwo\nthree\n";
    rc = screen_line_index_build(&idx, second, strlen(second), 80);
    ASSERT_EQ(rc, 0, "rebuild");
    ASSERT_EQ(idx.count, (size_t)3, "three entries after rebuild");
    ASSERT_EQ(idx.entries[2].byte_offset, (size_t)8, "'three' offset");

    screen_line_index_free(&idx);
}

TEST(free_idempotent) {
    screen_line_index_t idx = {0};
    screen_line_index_free(&idx); /// free on empty -- defensive
    int rc = screen_line_index_build(&idx, "x", 1, 80);
    ASSERT_EQ(rc, 0, "build after empty free");
    screen_line_index_free(&idx);
    screen_line_index_free(&idx); /// double-free is safe (no-op)
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running screen_line_index tests...\n\n");

    printf("Empty / degenerate input:\n");
    RUN_TEST(empty_content_yields_zero_entries);
    RUN_TEST(null_content_yields_zero_entries);
    RUN_TEST(zero_terminal_width_clamps_to_one);

    printf("\nByte-span correctness:\n");
    RUN_TEST(single_line_no_newline);
    RUN_TEST(single_line_with_trailing_newline);
    RUN_TEST(two_lines_with_trailing_newline);
    RUN_TEST(empty_middle_line);
    RUN_TEST(no_trailing_newline_after_multiple_lines);

    printf("\nVisual height / wrapping:\n");
    RUN_TEST(narrow_terminal_wraps_long_line);
    RUN_TEST(exact_terminal_width_one_row);
    RUN_TEST(one_over_width_two_rows);

    printf("\nANSI escape handling:\n");
    RUN_TEST(ansi_escapes_excluded_from_visual_width);
    RUN_TEST(ansi_styled_long_line_still_wraps_by_visual_width);

    printf("\nUTF-8 wide characters:\n");
    RUN_TEST(cjk_wide_char_takes_two_columns);
    RUN_TEST(mixed_ascii_and_wide_char);

    printf("\nRewidth on resize:\n");
    RUN_TEST(rewidth_recomputes_visual_heights);
    RUN_TEST(rewidth_with_zero_width_clamps_to_one);

    printf("\nIndex reuse / safety:\n");
    RUN_TEST(rebuild_overwrites_existing_index);
    RUN_TEST(free_idempotent);

    return TEST_RESULT();
}
