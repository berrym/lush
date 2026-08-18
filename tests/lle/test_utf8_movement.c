/**
 * @file test_utf8_movement.c
 * @brief LLE tests for utf8 movement
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * UTF-8 Movement Function Test
 *
 * Tests the fixed movement functions (lle_forward_char, lle_backward_char,
 * lle_forward_word, lle_backward_word) with multi-byte UTF-8 characters.
 *
 * These functions were broken before cursor_manager integration because they
 * used naive byte arithmetic instead of proper grapheme cluster detection.
 *
 * Test Coverage:
 * - ASCII characters (1 byte)
 * - Latin extended characters (2 bytes: e-acute, n-tilde)
 * - CJK characters (3 bytes: U+4E2D, U+6587)
 * - Emoji (4 bytes: U+1F525, U+1F3AF)
 * - Combining diacritics (multi-codepoint graphemes)
 * - Mixed ASCII and multi-byte
 * - Word boundaries with UTF-8
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lle/buffer_management.h"
#include "lle/keybinding_actions.h"
#include "lle/lle_editor.h"
#include "lush_memory_pool.h"

/// External global_memory_pool (defined in lush_memory_pool.c)
extern lush_memory_pool_t *global_memory_pool;

/// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name)                                                       \
    do {                                                                       \
        tests_run++;                                                           \
        printf("\n[TEST %d] %s\n", tests_run, name);                           \
    } while (0)

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("  \xe2\x9c\x97 FAILED: %s\n", message);                    \
            printf("    at %s:%d\n", __FILE__, __LINE__);                      \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define TEST_PASS()                                                            \
    do {                                                                       \
        printf("  \xe2\x9c\x93 PASSED\n");                                     \
        tests_passed++;                                                        \
    } while (0)

/// Helper: Create editor with test content
static lle_editor_t *create_editor_with_content(const char *content,
                                                lush_memory_pool_t *pool) {
    lle_editor_t *editor = NULL;
    lle_result_t result;

    result = lle_editor_create(&editor, pool);
    if (result != LLE_SUCCESS || !editor) {
        return NULL;
    }

    /// Insert content
    if (content && *content) {
        result =
            lle_buffer_insert_text(editor->buffer, 0, content, strlen(content));
        if (result != LLE_SUCCESS) {
            lle_editor_destroy(editor);
            return NULL;
        }
    }

    /// Reset cursor to beginning
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, 0);

    return editor;
}

/// Helper: Get current cursor position
static void get_cursor_position(lle_editor_t *editor, size_t *byte_offset,
                                size_t *codepoint_index,
                                size_t *grapheme_index) {
    lle_cursor_position_t pos;
    lle_cursor_manager_get_position(editor->cursor_manager, &pos);

    if (byte_offset)
        *byte_offset = pos.byte_offset;
    if (codepoint_index)
        *codepoint_index = pos.codepoint_index;
    if (grapheme_index)
        *grapheme_index = pos.grapheme_index;
}

/* ============================================================================
 * FORWARD CHAR TESTS
 * ============================================================================
 */

static void test_forward_char_ascii(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: ASCII text");

    lle_editor_t *editor = create_editor_with_content("hello", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, cp_idx, gr_idx;

    /// Start at position 0
    get_cursor_position(editor, &byte_off, &cp_idx, &gr_idx);
    TEST_ASSERT(byte_off == 0 && gr_idx == 0, "Initial position incorrect");

    /// Move forward 1 char: should be at 'e'
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, &cp_idx, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1,
                "Position after 1 forward incorrect");

    /// Move forward 3 more chars: should be at 'o'
    lle_forward_char(editor);
    lle_forward_char(editor);
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, &cp_idx, &gr_idx);
    TEST_ASSERT(byte_off == 4 && gr_idx == 4,
                "Position after 4 forwards incorrect");

    /// Move forward 1 more: should be at end
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, &cp_idx, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 5, "Position at end incorrect");

    /// Try to move past end: should stay at end
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, &cp_idx, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 5, "Should not move past end");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_utf8_2byte(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: 2-byte UTF-8 (Latin extended)");

    /// "cafe-acute" - e-acute is 2 bytes (0xC3 0xA9)
    /// Byte layout: c(0) a(1) f(2) e-acute(3-4)
    lle_editor_t *editor = create_editor_with_content("caf\xc3\xa9", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    /// Move forward 1: should be at 'a' (byte 1)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1,
                "Position after 1 forward incorrect");

    /// Move forward 2: should be at 'f' (byte 2)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 2 && gr_idx == 2,
                "Position after 2 forwards incorrect");

    /// Move forward 3: should be at 'e-acute' start (byte 3)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 3 && gr_idx == 3,
                "Position after 3 forwards incorrect");

    /// Move forward 4: should be past 'e-acute' (byte 5, end of string)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 4,
                "Position after '\xc3\xa9' incorrect (2-byte char)");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_decomposed_cluster(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: DECOMPOSED cluster moves as one character");

    /// Every other case in this file uses input where one codepoint IS one
    /// grapheme -- precomposed e-acute, CJK, single-codepoint emoji. Under
    /// that input a codepoint-stepping cursor is indistinguishable from a
    /// grapheme-stepping one, so those tests cannot detect a regression to
    /// codepoint movement. This one can.
    ///
    /// "caf" + e(0x65) + COMBINING ACUTE(0xCC 0x81): 6 bytes, 5 codepoints,
    /// 4 grapheme clusters. Byte layout: c(0) a(1) f(2) e+mark(3-5).
    lle_editor_t *editor = create_editor_with_content("cafe\xcc\x81", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    lle_forward_char(editor);
    lle_forward_char(editor);
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 3 && gr_idx == 3,
                "Should sit at the cluster start");

    /// One more must cross the WHOLE cluster -- base and mark together --
    /// landing at end of text, not between the base and its accent.
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 6,
                "Cursor stopped INSIDE a grapheme cluster (byte 4 means it "
                "stepped by codepoint, splitting the base from its mark)");
    TEST_ASSERT(gr_idx == 4, "Grapheme index after the cluster incorrect");

    /// And back over it in one step.
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 3 && gr_idx == 3,
                "Backward did not cross the whole cluster");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_zwj_sequence(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: ZWJ emoji sequence is ONE character");

    /// "a" + U+1F468 ZWJ U+1F4BB + "b": the emoji is 3 codepoints (11 bytes)
    /// joined into a single grapheme cluster. Byte layout:
    /// a(0) emoji(1-11) b(12).
    lle_editor_t *editor = create_editor_with_content(
        "a\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb"
        "b",
        pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1, "Past the leading 'a'");

    /// One step must cross all three codepoints of the sequence.
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 12,
                "Cursor landed inside the ZWJ sequence (byte 5 or 8 means it "
                "stepped by codepoint, splitting a single character)");
    TEST_ASSERT(gr_idx == 2, "Grapheme index after the sequence incorrect");

    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1,
                "Backward did not cross the whole ZWJ sequence");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_delete_removes_whole_clusters(lush_memory_pool_t *pool) {
    TEST_START("delete/backspace remove a WHOLE grapheme cluster");

    /// A deletion that removes only the last codepoint leaves an orphaned
    /// combining mark in the buffer -- the character visibly changes into a
    /// different one rather than disappearing. Precomposed input cannot catch
    /// that, because there one codepoint IS the whole cluster.

    /// "caf" + e + COMBINING ACUTE = 6 bytes, 4 clusters.
    lle_editor_t *e = create_editor_with_content("cafe\xcc\x81", pool);
    TEST_ASSERT(e != NULL, "Failed to create editor");
    lle_end_of_line(e);
    lle_backward_delete_char(e);
    TEST_ASSERT(e->buffer->length == 3,
                "Backspace left part of the cluster behind (4 would mean the "
                "combining mark is still there without its base)");
    lle_editor_destroy(e);

    /// "a" + a three-codepoint ZWJ sequence = 12 bytes, 2 clusters.
    e = create_editor_with_content(
        "a\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb", pool);
    TEST_ASSERT(e != NULL, "Failed to create editor");
    lle_end_of_line(e);
    lle_backward_delete_char(e);
    TEST_ASSERT(e->buffer->length == 1,
                "Backspace removed only part of the ZWJ sequence");
    lle_editor_destroy(e);

    /// Forward delete at the START of a cluster must take all of it.
    e = create_editor_with_content("cafe\xcc\x81z", pool);
    TEST_ASSERT(e != NULL, "Failed to create editor");
    lle_beginning_of_line(e);
    lle_forward_char(e);
    lle_forward_char(e);
    lle_forward_char(e);
    lle_delete_char(e);
    TEST_ASSERT(e->buffer->length == 4,
                "Forward delete split the cluster, leaving its mark");
    lle_editor_destroy(e);

    TEST_PASS();
}

static void test_word_motion_over_clusters(lush_memory_pool_t *pool) {
    TEST_START("word motion and kill land on cluster boundaries");

    /// A word whose last character is a decomposed cluster: forward-word must
    /// land past the whole thing, not between the base and its mark.
    lle_editor_t *e = create_editor_with_content("cafe\xcc\x81 xy", pool);
    TEST_ASSERT(e != NULL, "Failed to create editor");
    lle_beginning_of_line(e);
    lle_forward_word(e);
    size_t byte_off;
    get_cursor_position(e, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 6,
                "forward-word stopped inside the trailing cluster (5 would "
                "mean it split the base from its mark)");
    lle_editor_destroy(e);

    /// backward-kill-word must remove the cluster-bearing word entirely.
    e = create_editor_with_content("xy cafe\xcc\x81", pool);
    TEST_ASSERT(e != NULL, "Failed to create editor");
    lle_end_of_line(e);
    lle_backward_kill_word(e);
    TEST_ASSERT(e->buffer->length == 3,
                "backward-kill-word left part of the cluster in the buffer");
    lle_editor_destroy(e);

    TEST_PASS();
}

static void test_forward_char_utf8_3byte(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: 3-byte UTF-8 (CJK)");

    /// "U+4E2D U+6587" - each character is 3 bytes
    lle_editor_t *editor =
        create_editor_with_content("\xe4\xb8\xad\xe6\x96\x87", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    /// Start at beginning
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 0 && gr_idx == 0, "Initial position incorrect");

    /// Move forward 1 char: should skip 3 bytes
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 3 && gr_idx == 1,
                "Position after first CJK char incorrect");

    /// Move forward 1 more: should skip another 3 bytes
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 6 && gr_idx == 2,
                "Position after second CJK char incorrect");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_utf8_4byte(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: 4-byte UTF-8 (Emoji)");

    /// "U+1F525 U+1F3AF" - each emoji is 4 bytes
    lle_editor_t *editor =
        create_editor_with_content("\xf0\x9f\x94\xa5\xf0\x9f\x8e\xaf", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    /// Move forward 1 emoji: should skip 4 bytes
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 4 && gr_idx == 1,
                "Position after first emoji incorrect");

    /// Move forward 1 more emoji: should skip another 4 bytes
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 8 && gr_idx == 2,
                "Position after second emoji incorrect");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_mixed(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: Mixed ASCII and multi-byte");

    /// "aU+4E2DbU+1F525c" - mix of 1, 3, 1, 4, 1 bytes
    lle_editor_t *editor = create_editor_with_content("a\xe4\xb8\xad"
                                                      "b\xf0\x9f\x94\xa5"
                                                      "c",
                                                      pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    /// 'a' (1 byte)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1, "After 'a'");

    /// 'U+4E2D' (3 bytes)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 4 && gr_idx == 2, "After '\xe4\xb8\xad'");

    /// 'b' (1 byte)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 3, "After 'b'");

    /// 'U+1F525' (4 bytes)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 9 && gr_idx == 4, "After '\xf0\x9f\x94\xa5'");

    /// 'c' (1 byte)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 10 && gr_idx == 5, "After 'c'");

    lle_editor_destroy(editor);
    TEST_PASS();
}

/* ============================================================================
 * BACKWARD CHAR TESTS
 * ============================================================================
 */

static void test_backward_char_utf8(lush_memory_pool_t *pool) {
    TEST_START("lle_backward_char: UTF-8 text");

    /// "helloU+4E2D U+6587 U+1F525"
    lle_editor_t *editor = create_editor_with_content(
        "hello\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x94\xa5", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    /// Move to end
    size_t end_byte = strlen("hello\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x94\xa5");
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, end_byte);

    size_t byte_off, gr_idx;
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 8, "Not at end (should be 8 graphemes)");

    /// Backward from U+1F525 (4 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 7, "After backward from emoji");

    /// Backward from U+6587 (3 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 6, "After backward from \xe6\x96\x87");

    /// Backward from U+4E2D (3 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 5, "After backward from \xe4\xb8\xad");

    /// Continue backward through ASCII
    lle_backward_char(editor);
    lle_backward_char(editor);
    lle_backward_char(editor);
    lle_backward_char(editor);
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 0 && gr_idx == 0, "Should be at beginning");

    /// Try to move before beginning
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 0 && gr_idx == 0,
                "Should not move before beginning");

    lle_editor_destroy(editor);
    TEST_PASS();
}

/* ============================================================================
 * WORD MOVEMENT TESTS
 * ============================================================================
 */

static void test_forward_word_ascii(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_word: ASCII words");

    lle_editor_t *editor = create_editor_with_content("hello world test", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off;

    /// Forward to end of "hello"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 5, "Should be at end of 'hello'");

    /// Forward to end of "world"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 11, "Should be at end of 'world'");

    /// Forward to end of "test"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 16, "Should be at end of 'test'");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_word_utf8(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_word: UTF-8 words");

    /// "hello U+4E2D U+6587 world"
    lle_editor_t *editor = create_editor_with_content(
        "hello \xe4\xb8\xad\xe6\x96\x87 world", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off;

    /// Forward to end of "hello"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 5, "Should be at end of 'hello'");

    /// Forward to end of "U+4E2D U+6587" - this is 6 bytes (3+3)
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(
        byte_off == 12,
        "Should be at end of '\xe4\xb8\xad\xe6\x96\x87' (6 bytes after space)");

    /// Forward to end of "world"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    size_t expected = 12 + 1 + 5; /// U+4E2D U+6587 + space + world
    TEST_ASSERT(byte_off == expected, "Should be at end of 'world'");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_backward_word_ascii(lush_memory_pool_t *pool) {
    TEST_START("lle_backward_word: ASCII words");

    lle_editor_t *editor = create_editor_with_content("hello world test", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    /// Move to end
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, 16);

    size_t byte_off;

    /// Backward to start of "test"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 12, "Should be at start of 'test'");

    /// Backward to start of "world"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 6, "Should be at start of 'world'");

    /// Backward to start of "hello"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 0, "Should be at start of 'hello'");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_backward_word_utf8(lush_memory_pool_t *pool) {
    TEST_START("lle_backward_word: UTF-8 words");

    /// "hello U+4E2D U+6587 world"
    lle_editor_t *editor = create_editor_with_content(
        "hello \xe4\xb8\xad\xe6\x96\x87 world", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    /// Move to end
    size_t end_byte = strlen("hello \xe4\xb8\xad\xe6\x96\x87 world");
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, end_byte);

    size_t byte_off;

    /// Backward to start of "world"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 13, "Should be at start of 'world'");

    /// Backward to start of "U+4E2D U+6587"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 6,
                "Should be at start of '\xe4\xb8\xad\xe6\x96\x87'");

    /// Backward to start of "hello"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 0, "Should be at start of 'hello'");

    lle_editor_destroy(editor);
    TEST_PASS();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("========================================\n");
    printf("UTF-8 Movement Functions Test Suite\n");
    printf("========================================\n");
    printf("Testing: lle_forward_char, lle_backward_char,\n");
    printf("         lle_forward_word, lle_backward_word\n");
    printf("========================================\n");

    /// Initialize global memory pool with default configuration
    lush_pool_config_t config = lush_pool_get_default_config();

    if (lush_pool_init(&config) != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "FATAL: Failed to initialize memory pool\n");
        return 1;
    }

    /// Pass NULL as pool - lle_editor_create will use global_memory_pool
    lush_memory_pool_t *pool = NULL;

    /// Run all tests
    test_forward_char_ascii(pool);
    test_forward_char_utf8_2byte(pool);
    test_delete_removes_whole_clusters(pool);
    test_word_motion_over_clusters(pool);
    test_forward_char_decomposed_cluster(pool);
    test_forward_char_zwj_sequence(pool);
    test_forward_char_utf8_3byte(pool);
    test_forward_char_utf8_4byte(pool);
    test_forward_char_mixed(pool);

    test_backward_char_utf8(pool);

    test_forward_word_ascii(pool);
    test_forward_word_utf8(pool);
    test_backward_word_ascii(pool);
    test_backward_word_utf8(pool);

    /// Print results
    printf("\n========================================\n");
    printf("TEST RESULTS\n");
    printf("========================================\n");
    printf("Total:  %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("========================================\n");

    if (tests_failed == 0) {
        printf("\xe2\x9c\x93 ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\xe2\x9c\x97 SOME TESTS FAILED\n");
        return 1;
    }
}
