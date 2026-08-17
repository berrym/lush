/**
 * @file test_screen_buffer.c
 * @brief Unit tests for screen buffer management
 *
 * Tests the screen buffer layer's virtual screen management, UTF-8 handling,
 * line prefix support, visual width calculation, and rendering functions.
 *
 * The screen_buffer layer maintains a virtual representation of terminal
 * state for differential updates. These tests focus on core functionality:
 * initialization, rendering, prefix management, and width calculation.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/screen_buffer.h"
#include "test_framework.h"

/* Local 1-arg / 2-arg ASSERT_* helpers (no message) bridge to the
 * framework's variants by synthesizing a stringified-expression
 * message. Replaces the historical exit(1) failure path with
 * longjmp-based isolation per RUN_TEST. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")
#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL, #p " is NULL")
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL, #p " is non-NULL")

/// Test framework macros

/* ============================================================
 * INITIALIZATION TESTS
 * ============================================================ */

static int test_init_null_buffer(void) {
    /// A NULL buffer must be guarded against: the call returns without
    /// dereferencing it. Removing that guard would segfault here, so this
    /// pins the crash-safety contract even though there is no state to read.
    screen_buffer_init(NULL, 80);
    return 1;
}

static int test_init_default_width(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    ASSERT_EQ(buffer.terminal_width, 80);
    ASSERT_EQ(buffer.num_rows, 0);
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 0);
    ASSERT_EQ(buffer.command_start_row, 0);
    ASSERT_EQ(buffer.command_start_col, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_init_zero_width(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 0);

    /// Zero width should default to 80
    ASSERT_EQ(buffer.terminal_width, 80);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_init_negative_width(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, -10);

    /// Negative width should default to 80
    ASSERT_EQ(buffer.terminal_width, 80);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_init_large_width(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 500);

    ASSERT_EQ(buffer.terminal_width, 500);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_init_menu_tracking_fields(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    ASSERT_EQ(buffer.menu_lines, 0);
    ASSERT_EQ(buffer.ghost_text_lines, 0);
    ASSERT_EQ(buffer.total_display_rows, 0);
    ASSERT_EQ(buffer.command_end_row, 0);
    ASSERT_EQ(buffer.command_end_col, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_init_prefix_pointers_null(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// All prefix pointers should be NULL initially
    for (int i = 0; i < 10; i++) {
        ASSERT_NULL(buffer.lines[i].prefix);
        ASSERT_EQ(buffer.lines[i].prefix_dirty, false);
    }

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * CLEAR TESTS
 * ============================================================ */

static int test_clear_null_buffer(void) {
    /// Should not crash
    screen_buffer_clear(NULL);
    return 1;
}

static int test_clear_resets_state(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Render some content
    screen_buffer_render(&buffer, "$ ", "hello", 5);

    /// Clear should reset
    screen_buffer_clear(&buffer);

    ASSERT_EQ(buffer.num_rows, 0);
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 0);
    ASSERT_EQ(buffer.menu_lines, 0);
    ASSERT_EQ(buffer.ghost_text_lines, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_clear_preserves_terminal_width(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 120);

    screen_buffer_render(&buffer, "$ ", "test", 4);
    screen_buffer_clear(&buffer);

    /// Terminal width should be preserved
    ASSERT_EQ(buffer.terminal_width, 120);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * CLEANUP TESTS
 * ============================================================ */

static int test_cleanup_null_buffer(void) {
    /// Should not crash
    screen_buffer_cleanup(NULL);
    return 1;
}

static int test_cleanup_frees_prefixes(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Set some prefixes
    screen_buffer_set_line_prefix(&buffer, 0, "prefix1> ");
    screen_buffer_set_line_prefix(&buffer, 1, "prefix2> ");

    ASSERT_NOT_NULL(buffer.lines[0].prefix);
    ASSERT_NOT_NULL(buffer.lines[1].prefix);

    /// Cleanup should free them
    screen_buffer_cleanup(&buffer);

    ASSERT_NULL(buffer.lines[0].prefix);
    ASSERT_NULL(buffer.lines[1].prefix);

    return 1;
}

/* ============================================================
 * COPY TESTS
 * ============================================================ */

static int test_copy_null_dest(void) {
    screen_buffer_t src;
    screen_buffer_init(&src, 80);

    /// A NULL destination is rejected by an early return; the source must be
    /// left untouched (its initialized width still 80), proving copy did not
    /// corrupt the valid operand.
    screen_buffer_copy(NULL, &src);
    ASSERT_EQ(src.terminal_width, 80);

    screen_buffer_cleanup(&src);
    return 1;
}

static int test_copy_null_src(void) {
    screen_buffer_t dest;
    screen_buffer_init(&dest, 80);

    /// A NULL source is rejected by an early return, so the destination keeps
    /// its initialized state rather than being overwritten or zeroed.
    screen_buffer_copy(&dest, NULL);
    ASSERT_EQ(dest.terminal_width, 80);

    screen_buffer_cleanup(&dest);
    return 1;
}

static int test_copy_basic(void) {
    screen_buffer_t src, dest;
    screen_buffer_init(&src, 100);
    screen_buffer_init(&dest, 80);

    /// Render to source
    screen_buffer_render(&src, "prompt> ", "command", 7);

    /// Copy
    screen_buffer_copy(&dest, &src);

    ASSERT_EQ(dest.terminal_width, src.terminal_width);
    ASSERT_EQ(dest.num_rows, src.num_rows);
    ASSERT_EQ(dest.cursor_row, src.cursor_row);
    ASSERT_EQ(dest.cursor_col, src.cursor_col);
    ASSERT_EQ(dest.command_start_row, src.command_start_row);
    ASSERT_EQ(dest.command_start_col, src.command_start_col);

    screen_buffer_cleanup(&src);
    screen_buffer_cleanup(&dest);
    return 1;
}

/* ============================================================
 * VISUAL WIDTH TESTS
 * ============================================================ */

static int test_visual_width_null_text(void) {
    size_t width = screen_buffer_visual_width(NULL, 0);
    ASSERT_EQ(width, 0);
    return 1;
}

static int test_visual_width_empty_string(void) {
    size_t width = screen_buffer_visual_width("", 0);
    ASSERT_EQ(width, 0);
    return 1;
}

static int test_visual_width_ascii(void) {
    const char *text = "hello";
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 5);
    return 1;
}

static int test_visual_width_with_ansi_color(void) {
    /// ANSI codes should not count toward width
    const char *text = "\033[31mred\033[0m";
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 3); /// Just "red"
    return 1;
}

static int test_visual_width_with_bold_ansi(void) {
    const char *text = "\033[1mbold\033[0m";
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 4); /// Just "bold"
    return 1;
}

static int test_visual_width_multiple_ansi(void) {
    const char *text = "\033[31;1mbold red\033[0m";
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 8); /// Just "bold red"
    return 1;
}

static int test_visual_width_readline_markers(void) {
    /// Readline markers \001 and \002 should not count
    const char *text = "\001\033[31m\002red\001\033[0m\002";
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 3); /// Just "red"
    return 1;
}

static int test_visual_width_utf8_2byte(void) {
    /// e-acute is 2 bytes UTF-8, 1 column width
    const char *text = "caf\xc3\xa9"; /// c a f e-acute
    size_t width = screen_buffer_visual_width(text, strlen(text));
    ASSERT_EQ(width, 4);
    return 1;
}

static int test_render_escape_sequences_end_at_the_final_byte(void) {
    /// The same ECMA-48 rule as test_escape_sequences_end_at_the_final_byte,
    /// asserted against screen_buffer_render rather than the width helper.
    /// The helper was corrected; render carried its own inline copies of the
    /// walk, and they still enumerated "any ASCII letter" as the terminator.
    /// A sequence whose final byte is not a letter therefore never terminated,
    /// and the rest of the prompt was swallowed as part of the escape --
    /// so the command started at column 0 instead of after the prompt.
    struct {
        const char *prompt;
        int want_col;
        const char *what;
    } cases[] = {
        {         "$ ", 2,                                "no escape at all"},
        { "\033[31m$ ", 2,                               "SGR, terminator m"},
        {"\033[?25l$ ", 2,                       "cursor hide, terminator l"},
        {  "\033[3~$ ", 2, "terminator ~, a final byte that is not a letter"},
        {  "\033[@x$ ", 3,     "terminator @, the low end of the 0x40 range"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        screen_buffer_t buffer;
        screen_buffer_init(&buffer, 80);
        screen_buffer_render(&buffer, cases[i].prompt, "cmd", 3);
        ASSERT_TRUE(buffer.command_start_col == cases[i].want_col,
                    cases[i].what);
        screen_buffer_cleanup(&buffer);
    }
    return 1;
}

static int test_escape_sequences_end_at_the_final_byte(void) {
    /// An ANSI sequence ends at its FINAL BYTE, which ECMA-48 defines as the
    /// range 0x40-0x7E -- not "the first letter". Copies of this walk around
    /// the tree enumerated terminators instead, and every enumeration missed
    /// something: composition_engine listed m K J H A B C D G f s u, so a
    /// sequence ending in `h` or `l` never terminated -- and `\033[?25l` /
    /// `\033[?25h` is the cursor hide/show pair a shell emits constantly.
    /// Everything after one measured as ZERO width.
    struct {
        const char *text;
        size_t want;
        const char *what;
    } cases[] = {
        {   "\033[31mabc\033[0m", 3,                                      "SGR colour"},
        {"\033[?25labc\033[?25h", 3,            "cursor hide/show, terminator l and h"},
        {            "\033[Kabc", 3,                        "erase line, terminator K"},
        {           "\033[2Jabc", 3,                      "clear screen, terminator J"},
        {       "\033[10;20Habc", 3,                   "cursor position, terminator H"},
        {           "\033[3~abc", 3, "terminator ~, a final byte that is not a letter"},
        {   "\033[1;2;3;4;5mabc", 3,                                 "many parameters"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t w = screen_buffer_calculate_visual_width(cases[i].text, 0);
        ASSERT_TRUE(w == cases[i].want, cases[i].what);
    }
    return 1;
}

static int test_width_agrees_on_wide_characters(void) {
    /// The two exported width functions must answer the same question the
    /// same way. screen_buffer_visual_width counted EVERY multi-byte
    /// character as one column, so a CJK character (3 bytes, 2 columns) came
    /// back as 1 and a combining mark (0 columns) came back as 1 -- while
    /// screen_buffer_calculate_visual_width, which walks grapheme clusters
    /// and asks the width table, got them right. Anything positioned with the
    /// first function (the right prompt) therefore drifted by one column per
    /// wide character.
    ///
    /// Asserted as AGREEMENT plus the absolute value, so a future change that
    /// breaks both in the same way still fails.
    struct {
        const char *text;
        size_t want;
        const char *what;
    } cases[] = {
        {                      "hello", 5,                                  "ascii"},
        {                "caf\xc3\xa9", 4,                       "2-byte, 1 column"},
        {               "\xe3\x81\x82", 2,       "CJK hiragana, 3 bytes, 2 columns"},
        {                 "a\xe3\x81\x82"
                 "b", 4,                      "CJK between ascii"         },
        {   "\xe4\xb8\xad\xe6\x96\x87", 4,                     "two CJK ideographs"},
        {                  "e\xcc\x81", 1, "base plus combining mark is one column"},
        {"\033[31m\xe3\x81\x82\033[0m", 2,                        "CJK inside ANSI"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t a =
            screen_buffer_visual_width(cases[i].text, strlen(cases[i].text));
        size_t b = screen_buffer_calculate_visual_width(cases[i].text, 0);
        ASSERT_TRUE(a == cases[i].want, cases[i].what);
        ASSERT_TRUE(b == cases[i].want, cases[i].what);
        ASSERT_TRUE(a == b, "the two width functions agree");
    }
    return 1;
}

/* ============================================================
 * CALCULATE VISUAL WIDTH TESTS (with start_col for tabs)
 * ============================================================ */

static int test_calculate_visual_width_null(void) {
    size_t width = screen_buffer_calculate_visual_width(NULL, 0);
    ASSERT_EQ(width, 0);
    return 1;
}

static int test_calculate_visual_width_empty(void) {
    size_t width = screen_buffer_calculate_visual_width("", 0);
    ASSERT_EQ(width, 0);
    return 1;
}

static int test_calculate_visual_width_ascii(void) {
    size_t width = screen_buffer_calculate_visual_width("hello", 0);
    ASSERT_EQ(width, 5);
    return 1;
}

static int test_calculate_visual_width_ansi(void) {
    size_t width =
        screen_buffer_calculate_visual_width("\033[32mgreen\033[0m", 0);
    ASSERT_EQ(width, 5); /// Just "green"
    return 1;
}

/* ============================================================
 * RENDER TESTS
 * ============================================================ */

static int test_render_null_buffer(void) {
    /// Should not crash
    screen_buffer_render(NULL, "$ ", "hello", 5);
    return 1;
}

static int test_render_null_prompt(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, NULL, "hello", 5);

    /// Command start should be at 0,0
    ASSERT_EQ(buffer.command_start_row, 0);
    ASSERT_EQ(buffer.command_start_col, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_null_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", NULL, 0);

    ASSERT_EQ(buffer.num_rows, 1);
    ASSERT_EQ(buffer.command_start_row, 0);
    ASSERT_EQ(buffer.command_start_col, 2);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_simple_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "ls -la", 6);

    ASSERT_EQ(buffer.num_rows, 1);
    ASSERT_EQ(buffer.command_start_col, 2); /// After "$ "
    ASSERT_EQ(buffer.cursor_col, 8);        /// 2 (prompt) + 6 (command)

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_cursor_at_start(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "hello", 0);

    /// Cursor at start of command
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 2); /// Right after prompt

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_cursor_in_middle(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "hello", 2);

    /// Cursor after "he"
    ASSERT_EQ(buffer.cursor_row, 0);
    ASSERT_EQ(buffer.cursor_col, 4); /// 2 (prompt) + 2 (offset)

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_empty_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "", 0);

    ASSERT_EQ(buffer.num_rows, 1);
    ASSERT_EQ(buffer.cursor_col, 2);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_prompt_with_newline(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Multi-line prompt
    screen_buffer_render(&buffer, "line1\nline2> ", "cmd", 3);

    ASSERT_EQ(buffer.command_start_row, 1); /// Second row
    ASSERT(buffer.num_rows >= 2);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_command_with_newline(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "line1\nline2", 11);

    ASSERT(buffer.num_rows >= 2);
    ASSERT_EQ(buffer.command_end_row, 1); /// Second row

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_tracks_command_end(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "hello world", 11);

    ASSERT_EQ(buffer.command_end_row, 0);
    ASSERT_EQ(buffer.command_end_col, 13); /// 2 + 11

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * LINE PREFIX TESTS
 * ============================================================ */

static int test_set_prefix_null_buffer(void) {
    bool result = screen_buffer_set_line_prefix(NULL, 0, "prefix");
    ASSERT_EQ(result, false);
    return 1;
}

static int test_set_prefix_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result = screen_buffer_set_line_prefix(&buffer, -1, "prefix");
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_set_prefix_line_too_large(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result = screen_buffer_set_line_prefix(&buffer, SCREEN_BUFFER_MAX_ROWS,
                                                "prefix");
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_set_prefix_null_text_clears(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Set a prefix first
    screen_buffer_set_line_prefix(&buffer, 0, "test> ");
    ASSERT_NOT_NULL(buffer.lines[0].prefix);

    /// NULL text should clear it
    bool result = screen_buffer_set_line_prefix(&buffer, 0, NULL);
    ASSERT_EQ(result, true);
    ASSERT_NULL(buffer.lines[0].prefix);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_set_prefix_basic(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result = screen_buffer_set_line_prefix(&buffer, 0, "loop> ");
    ASSERT_EQ(result, true);
    ASSERT_NOT_NULL(buffer.lines[0].prefix);
    ASSERT_STR_EQ(buffer.lines[0].prefix->text, "loop> ");
    ASSERT_EQ(buffer.lines[0].prefix->length, 6);
    ASSERT_EQ(buffer.lines[0].prefix->visual_width, 6);
    ASSERT_EQ(buffer.lines[0].prefix->dirty, true);
    ASSERT_EQ(buffer.lines[0].prefix_dirty, true);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_set_prefix_with_ansi(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result =
        screen_buffer_set_line_prefix(&buffer, 0, "\033[32m> \033[0m");
    ASSERT_EQ(result, true);
    ASSERT_NOT_NULL(buffer.lines[0].prefix);
    ASSERT_EQ(buffer.lines[0].prefix->contains_ansi, true);
    ASSERT_EQ(buffer.lines[0].prefix->visual_width, 2); /// Just "> "

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_set_prefix_replaces_existing(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "old> ");
    screen_buffer_set_line_prefix(&buffer, 0, "new> ");

    ASSERT_STR_EQ(buffer.lines[0].prefix->text, "new> ");

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * GET PREFIX TESTS
 * ============================================================ */

static int test_get_prefix_null_buffer(void) {
    const char *prefix = screen_buffer_get_line_prefix(NULL, 0);
    ASSERT_NULL(prefix);
    return 1;
}

static int test_get_prefix_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    const char *prefix = screen_buffer_get_line_prefix(&buffer, -1);
    ASSERT_NULL(prefix);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_get_prefix_no_prefix_set(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    const char *prefix = screen_buffer_get_line_prefix(&buffer, 0);
    ASSERT_NULL(prefix);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_get_prefix_returns_text(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "test> ");

    const char *prefix = screen_buffer_get_line_prefix(&buffer, 0);
    ASSERT_NOT_NULL(prefix);
    ASSERT_STR_EQ(prefix, "test> ");

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * CLEAR PREFIX TESTS
 * ============================================================ */

static int test_clear_prefix_null_buffer(void) {
    bool result = screen_buffer_clear_line_prefix(NULL, 0);
    ASSERT_EQ(result, false);
    return 1;
}

static int test_clear_prefix_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result = screen_buffer_clear_line_prefix(&buffer, -1);
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_clear_prefix_no_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Clearing when no prefix exists should still succeed
    bool result = screen_buffer_clear_line_prefix(&buffer, 0);
    ASSERT_EQ(result, true);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_clear_prefix_removes_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "prefix> ");
    ASSERT_NOT_NULL(buffer.lines[0].prefix);

    bool result = screen_buffer_clear_line_prefix(&buffer, 0);
    ASSERT_EQ(result, true);
    ASSERT_NULL(buffer.lines[0].prefix);
    ASSERT_EQ(buffer.lines[0].prefix_dirty, true);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * PREFIX VISUAL WIDTH TESTS
 * ============================================================ */

static int test_prefix_visual_width_null_buffer(void) {
    size_t width = screen_buffer_get_line_prefix_visual_width(NULL, 0);
    ASSERT_EQ(width, 0);
    return 1;
}

static int test_prefix_visual_width_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    size_t width = screen_buffer_get_line_prefix_visual_width(&buffer, -1);
    ASSERT_EQ(width, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_prefix_visual_width_no_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    size_t width = screen_buffer_get_line_prefix_visual_width(&buffer, 0);
    ASSERT_EQ(width, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_prefix_visual_width_basic(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "loop> ");

    size_t width = screen_buffer_get_line_prefix_visual_width(&buffer, 0);
    ASSERT_EQ(width, 6);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_prefix_visual_width_with_ansi(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "\033[31mloop> \033[0m");

    size_t width = screen_buffer_get_line_prefix_visual_width(&buffer, 0);
    ASSERT_EQ(width, 6); /// ANSI codes don't count

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * PREFIX DIRTY FLAG TESTS
 * ============================================================ */

static int test_prefix_dirty_null_buffer(void) {
    bool dirty = screen_buffer_is_line_prefix_dirty(NULL, 0);
    ASSERT_EQ(dirty, false);
    return 1;
}

static int test_prefix_dirty_initially_false(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool dirty = screen_buffer_is_line_prefix_dirty(&buffer, 0);
    ASSERT_EQ(dirty, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_prefix_dirty_after_set(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "prefix> ");

    bool dirty = screen_buffer_is_line_prefix_dirty(&buffer, 0);
    ASSERT_EQ(dirty, true);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_clear_prefix_dirty_null_buffer(void) {
    /// Should not crash
    screen_buffer_clear_line_prefix_dirty(NULL, 0);
    return 1;
}

static int test_clear_prefix_dirty_clears_flag(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "prefix> ");
    ASSERT_EQ(screen_buffer_is_line_prefix_dirty(&buffer, 0), true);

    screen_buffer_clear_line_prefix_dirty(&buffer, 0);
    ASSERT_EQ(screen_buffer_is_line_prefix_dirty(&buffer, 0), false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * COLUMN TRANSLATION TESTS
 * ============================================================ */

static int test_buffer_to_display_col_null_buffer(void) {
    int result = screen_buffer_translate_buffer_to_display_col(NULL, 0, 5);
    ASSERT_EQ(result, -1);
    return 1;
}

static int test_buffer_to_display_col_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    int result = screen_buffer_translate_buffer_to_display_col(&buffer, -1, 5);
    ASSERT_EQ(result, -1);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_buffer_to_display_col_negative_col(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    int result = screen_buffer_translate_buffer_to_display_col(&buffer, 0, -1);
    ASSERT_EQ(result, -1);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_buffer_to_display_col_no_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Without prefix, buffer col == display col
    int result = screen_buffer_translate_buffer_to_display_col(&buffer, 0, 5);
    ASSERT_EQ(result, 5);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_buffer_to_display_col_with_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "loop> "); /// 6 columns

    int result = screen_buffer_translate_buffer_to_display_col(&buffer, 0, 5);
    ASSERT_EQ(result, 11); /// 6 + 5

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_display_to_buffer_col_null_buffer(void) {
    int result = screen_buffer_translate_display_to_buffer_col(NULL, 0, 10);
    ASSERT_EQ(result, -1);
    return 1;
}

static int test_display_to_buffer_col_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    int result = screen_buffer_translate_display_to_buffer_col(&buffer, -1, 10);
    ASSERT_EQ(result, -1);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_display_to_buffer_col_negative_col(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    int result = screen_buffer_translate_display_to_buffer_col(&buffer, 0, -1);
    ASSERT_EQ(result, -1);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_display_to_buffer_col_no_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    int result = screen_buffer_translate_display_to_buffer_col(&buffer, 0, 10);
    ASSERT_EQ(result, 10);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_display_to_buffer_col_with_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "loop> "); /// 6 columns

    int result = screen_buffer_translate_display_to_buffer_col(&buffer, 0, 10);
    ASSERT_EQ(result, 4); /// 10 - 6

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_display_to_buffer_col_within_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_set_line_prefix(&buffer, 0, "loop> "); /// 6 columns

    /// Display col 3 is within prefix, should return 0
    int result = screen_buffer_translate_display_to_buffer_col(&buffer, 0, 3);
    ASSERT_EQ(result, 0);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * RENDER LINE WITH PREFIX TESTS
 * ============================================================ */

static int test_render_line_with_prefix_null_buffer(void) {
    char output[256];
    bool result =
        screen_buffer_render_line_with_prefix(NULL, 0, output, sizeof(output));
    ASSERT_EQ(result, false);
    return 1;
}

static int test_render_line_with_prefix_null_output(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result = screen_buffer_render_line_with_prefix(&buffer, 0, NULL, 256);
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_line_with_prefix_negative_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    char output[256];

    bool result = screen_buffer_render_line_with_prefix(&buffer, -1, output,
                                                        sizeof(output));
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_line_with_prefix_no_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    char output[256];

    screen_buffer_render(&buffer, "", "hello", 5);

    /// With no prompt prefix, line 0 renders just the command content.
    bool result = screen_buffer_render_line_with_prefix(&buffer, 0, output,
                                                        sizeof(output));
    ASSERT_EQ(result, true);
    ASSERT_STR_EQ(output, "hello");

    screen_buffer_cleanup(&buffer);
    return 1;
}

static bool is_valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int need;
        if (*p < 0x80) {
            need = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            need = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            need = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            need = 3;
        } else {
            return false; /// continuation byte or invalid lead
        }
        p++;
        for (int i = 0; i < need; i++) {
            if ((*p & 0xC0) != 0x80) {
                return false; /// truncated sequence
            }
            p++;
        }
    }
    return true;
}

static int test_render_line_never_truncates_utf8(void) {
    /// The copy budget was checked per BYTE, so a line whose last character
    /// did not fit was cut between its lead byte and its continuations and
    /// the caller received invalid UTF-8 (issue #706). Every consumer of this
    /// function writes the result to a terminal, including all debugger
    /// output.
    ///
    /// A byte-oriented size makes the failure deterministic: the budget runs
    /// out partway through a three-byte character.
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// Box-drawing U+2500 is three bytes each.
    screen_buffer_render(&buffer, "", "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
                         0);

    for (size_t cap = 2; cap <= 12; cap++) {
        char output[16];
        memset(output, 0x7f, sizeof(output));
        bool result =
            screen_buffer_render_line_with_prefix(&buffer, 0, output, cap);
        if (result) {
            ASSERT(is_valid_utf8(output));
            ASSERT(strlen(output) < cap);
        }
    }

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_line_stops_before_a_partial_character(void) {
    /// Stopping BEFORE a character is never worse than stopping inside one:
    /// with room for two of three characters, exactly two are emitted.
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    screen_buffer_render(&buffer, "", "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
                         0);

    char output[16];
    /// 7 bytes of room holds two 3-byte characters plus the NUL; the third
    /// character needs three more and must be dropped whole.
    bool result = screen_buffer_render_line_with_prefix(&buffer, 0, output, 8);
    ASSERT_EQ(result, true);
    ASSERT(is_valid_utf8(output));
    ASSERT_EQ(strlen(output), (size_t)6);
    ASSERT_STR_EQ(output, "\xe2\x94\x80\xe2\x94\x80");

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_multiline_fits_a_full_wide_row(void) {
    /// The multiline caller sized its scratch line at MAX_COLS * 2, but a row
    /// holds MAX_COLS cells and a cell holds up to 4 bytes. A row of
    /// three-byte characters therefore overran the budget and was truncated
    /// by it -- which is what made the per-byte cut above reachable in
    /// ordinary use rather than only at extremes (issue #706).
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, SCREEN_BUFFER_MAX_COLS);

    /// Fill a row with three-byte box-drawing characters. Each is one column
    /// wide, so this fills the row without wrapping.
    static char wide[SCREEN_BUFFER_MAX_COLS * 3 + 1];
    size_t n = 0;
    for (int i = 0; i < SCREEN_BUFFER_MAX_COLS - 1; i++) {
        wide[n++] = (char)0xe2;
        wide[n++] = (char)0x94;
        wide[n++] = (char)0x80;
    }
    wide[n] = '\0';

    screen_buffer_render(&buffer, "", wide, 0);

    static char output[SCREEN_BUFFER_MAX_COLS * 4 + 64];
    bool result = screen_buffer_render_multiline_with_prefixes(
        &buffer, 0, 1, output, sizeof(output));
    ASSERT_EQ(result, true);
    ASSERT(is_valid_utf8(output));
    /// Every character survives: the scratch buffer must hold the whole row.
    ASSERT_EQ(strlen(output), n);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * RENDER MULTILINE WITH PREFIXES TESTS
 * ============================================================ */

static int test_render_multiline_null_buffer(void) {
    char output[1024];
    bool result = screen_buffer_render_multiline_with_prefixes(
        NULL, 0, 2, output, sizeof(output));
    ASSERT_EQ(result, false);
    return 1;
}

static int test_render_multiline_null_output(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    bool result =
        screen_buffer_render_multiline_with_prefixes(&buffer, 0, 2, NULL, 1024);
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_multiline_negative_start(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    char output[1024];

    bool result = screen_buffer_render_multiline_with_prefixes(
        &buffer, -1, 2, output, sizeof(output));
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_multiline_zero_lines(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    char output[1024];

    bool result = screen_buffer_render_multiline_with_prefixes(
        &buffer, 0, 0, output, sizeof(output));
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_multiline_range_too_large(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    char output[1024];

    bool result = screen_buffer_render_multiline_with_prefixes(
        &buffer, SCREEN_BUFFER_MAX_ROWS - 1, 5, output, sizeof(output));
    ASSERT_EQ(result, false);

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * MENU RENDERING TESTS
 * ============================================================ */

static int test_add_text_rows_null_buffer(void) {
    int result = screen_buffer_add_text_rows(NULL, 0, "menu text");
    ASSERT_EQ(result, -1);
    return 1;
}

static int test_add_text_rows_null_text(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// NULL text is rejected with -1; a real two-line string adds two rows.
    ASSERT_EQ(screen_buffer_add_text_rows(&buffer, 0, NULL), -1);
    ASSERT_EQ(screen_buffer_add_text_rows(&buffer, 0, "line1\nline2"), 2);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_get_total_display_rows_null_buffer(void) {
    int result = screen_buffer_get_total_display_rows(NULL);
    ASSERT_EQ(result, 0);
    return 1;
}

static int test_get_total_display_rows_basic(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "hello", 5);

    int rows = screen_buffer_get_total_display_rows(&buffer);
    ASSERT_EQ(rows, 1);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_get_rows_below_cursor_null_buffer(void) {
    int result = screen_buffer_get_rows_below_cursor(NULL);
    ASSERT_EQ(result, 0);
    return 1;
}

static int test_get_rows_below_cursor_single_row(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render(&buffer, "$ ", "hello", 5);

    int rows = screen_buffer_get_rows_below_cursor(&buffer);
    ASSERT_EQ(rows, 0); /// Cursor on last row

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * RENDER WITH CONTINUATION TESTS
 * ============================================================ */

/// Simple continuation callback for testing
static const char *test_continuation_cb(const char *line_text, size_t line_len,
                                        int line_number, void *user_data) {
    (void)line_text;
    (void)line_len;
    (void)line_number;
    (void)user_data;
    return "> ";
}

static int test_render_with_continuation_null_buffer(void) {
    /// A NULL buffer must be guarded: the render returns without writing
    /// through it. Dropping that guard would segfault here, so this pins the
    /// crash-safety contract.
    screen_buffer_render_with_continuation(NULL, "$ ", "cmd", 3,
                                           test_continuation_cb, NULL);
    return 1;
}

static int test_render_with_continuation_null_callback(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// NULL callback should work (no prefixes added)
    screen_buffer_render_with_continuation(&buffer, "$ ", "line1\nline2", 11,
                                           NULL, NULL);

    ASSERT(buffer.num_rows >= 2);

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_with_continuation_adds_prefix(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    screen_buffer_render_with_continuation(&buffer, "$ ", "line1\nline2", 11,
                                           test_continuation_cb, NULL);

    /// Check that continuation prefix was set on line 1
    const char *prefix = screen_buffer_get_line_prefix(&buffer, 1);
    ASSERT_NOT_NULL(prefix);
    ASSERT_STR_EQ(prefix, "> ");

    screen_buffer_cleanup(&buffer);
    return 1;
}

static int test_render_with_continuation_single_line(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    /// No newline means callback never called
    screen_buffer_render_with_continuation(&buffer, "$ ", "hello", 5,
                                           test_continuation_cb, NULL);

    /// No continuation prompt on first line
    ASSERT_NULL(screen_buffer_get_line_prefix(&buffer, 0));

    screen_buffer_cleanup(&buffer);
    return 1;
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void) {
    printf("=== Screen Buffer Unit Tests ===\n\n");

    printf("=== Initialization Tests ===\n");
    RUN_TEST(init_null_buffer);
    RUN_TEST(init_default_width);
    RUN_TEST(init_zero_width);
    RUN_TEST(init_negative_width);
    RUN_TEST(init_large_width);
    RUN_TEST(init_menu_tracking_fields);
    RUN_TEST(init_prefix_pointers_null);

    printf("\n=== Clear Tests ===\n");
    RUN_TEST(clear_null_buffer);
    RUN_TEST(clear_resets_state);
    RUN_TEST(clear_preserves_terminal_width);

    printf("\n=== Cleanup Tests ===\n");
    RUN_TEST(cleanup_null_buffer);
    RUN_TEST(cleanup_frees_prefixes);

    printf("\n=== Copy Tests ===\n");
    RUN_TEST(copy_null_dest);
    RUN_TEST(copy_null_src);
    RUN_TEST(copy_basic);

    printf("\n=== Visual Width Tests ===\n");
    RUN_TEST(visual_width_null_text);
    RUN_TEST(visual_width_empty_string);
    RUN_TEST(visual_width_ascii);
    RUN_TEST(visual_width_with_ansi_color);
    RUN_TEST(visual_width_with_bold_ansi);
    RUN_TEST(visual_width_multiple_ansi);
    RUN_TEST(visual_width_readline_markers);
    RUN_TEST(visual_width_utf8_2byte);
    RUN_TEST(width_agrees_on_wide_characters);
    RUN_TEST(escape_sequences_end_at_the_final_byte);
    RUN_TEST(render_escape_sequences_end_at_the_final_byte);

    printf("\n=== Calculate Visual Width Tests ===\n");
    RUN_TEST(calculate_visual_width_null);
    RUN_TEST(calculate_visual_width_empty);
    RUN_TEST(calculate_visual_width_ascii);
    RUN_TEST(calculate_visual_width_ansi);

    printf("\n=== Render Tests ===\n");
    RUN_TEST(render_null_buffer);
    RUN_TEST(render_null_prompt);
    RUN_TEST(render_null_command);
    RUN_TEST(render_simple_command);
    RUN_TEST(render_cursor_at_start);
    RUN_TEST(render_cursor_in_middle);
    RUN_TEST(render_empty_command);
    RUN_TEST(render_prompt_with_newline);
    RUN_TEST(render_command_with_newline);
    RUN_TEST(render_tracks_command_end);

    printf("\n=== Set Prefix Tests ===\n");
    RUN_TEST(set_prefix_null_buffer);
    RUN_TEST(set_prefix_negative_line);
    RUN_TEST(set_prefix_line_too_large);
    RUN_TEST(set_prefix_null_text_clears);
    RUN_TEST(set_prefix_basic);
    RUN_TEST(set_prefix_with_ansi);
    RUN_TEST(set_prefix_replaces_existing);

    printf("\n=== Get Prefix Tests ===\n");
    RUN_TEST(get_prefix_null_buffer);
    RUN_TEST(get_prefix_negative_line);
    RUN_TEST(get_prefix_no_prefix_set);
    RUN_TEST(get_prefix_returns_text);

    printf("\n=== Clear Prefix Tests ===\n");
    RUN_TEST(clear_prefix_null_buffer);
    RUN_TEST(clear_prefix_negative_line);
    RUN_TEST(clear_prefix_no_prefix);
    RUN_TEST(clear_prefix_removes_prefix);

    printf("\n=== Prefix Visual Width Tests ===\n");
    RUN_TEST(prefix_visual_width_null_buffer);
    RUN_TEST(prefix_visual_width_negative_line);
    RUN_TEST(prefix_visual_width_no_prefix);
    RUN_TEST(prefix_visual_width_basic);
    RUN_TEST(prefix_visual_width_with_ansi);

    printf("\n=== Prefix Dirty Flag Tests ===\n");
    RUN_TEST(prefix_dirty_null_buffer);
    RUN_TEST(prefix_dirty_initially_false);
    RUN_TEST(prefix_dirty_after_set);
    RUN_TEST(clear_prefix_dirty_null_buffer);
    RUN_TEST(clear_prefix_dirty_clears_flag);

    printf("\n=== Column Translation Tests ===\n");
    RUN_TEST(buffer_to_display_col_null_buffer);
    RUN_TEST(buffer_to_display_col_negative_line);
    RUN_TEST(buffer_to_display_col_negative_col);
    RUN_TEST(buffer_to_display_col_no_prefix);
    RUN_TEST(buffer_to_display_col_with_prefix);
    RUN_TEST(display_to_buffer_col_null_buffer);
    RUN_TEST(display_to_buffer_col_negative_line);
    RUN_TEST(display_to_buffer_col_negative_col);
    RUN_TEST(display_to_buffer_col_no_prefix);
    RUN_TEST(display_to_buffer_col_with_prefix);
    RUN_TEST(display_to_buffer_col_within_prefix);

    printf("\n=== Render Line with Prefix Tests ===\n");
    RUN_TEST(render_line_with_prefix_null_buffer);
    RUN_TEST(render_line_with_prefix_null_output);
    RUN_TEST(render_line_with_prefix_negative_line);
    RUN_TEST(render_line_with_prefix_no_prefix);
    RUN_TEST(render_line_never_truncates_utf8);
    RUN_TEST(render_line_stops_before_a_partial_character);
    RUN_TEST(render_multiline_fits_a_full_wide_row);

    printf("\n=== Render Multiline with Prefixes Tests ===\n");
    RUN_TEST(render_multiline_null_buffer);
    RUN_TEST(render_multiline_null_output);
    RUN_TEST(render_multiline_negative_start);
    RUN_TEST(render_multiline_zero_lines);
    RUN_TEST(render_multiline_range_too_large);

    printf("\n=== Menu Rendering Tests ===\n");
    RUN_TEST(add_text_rows_null_buffer);
    RUN_TEST(add_text_rows_null_text);
    RUN_TEST(get_total_display_rows_null_buffer);
    RUN_TEST(get_total_display_rows_basic);
    RUN_TEST(get_rows_below_cursor_null_buffer);
    RUN_TEST(get_rows_below_cursor_single_row);

    printf("\n=== Render with Continuation Tests ===\n");
    RUN_TEST(render_with_continuation_null_buffer);
    RUN_TEST(render_with_continuation_null_callback);
    RUN_TEST(render_with_continuation_adds_prefix);
    RUN_TEST(render_with_continuation_single_line);

    printf("\n=== Summary ===\n");
    return TEST_RESULT();
}
