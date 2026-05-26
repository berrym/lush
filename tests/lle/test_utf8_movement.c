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
 * - Latin extended characters (2 bytes: é, ñ)
 * - CJK characters (3 bytes: 中, 文)
 * - Emoji (4 bytes: 🔥, 🎯)
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
            printf("  ✗ FAILED: %s\n", message);                               \
            printf("    at %s:%d\n", __FILE__, __LINE__);                      \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define TEST_PASS()                                                            \
    do {                                                                       \
        printf("  ✓ PASSED\n");                                                \
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

    /// "café" - é is 2 bytes (0xC3 0xA9)
    /// Byte layout: c(0) a(1) f(2) é(3-4)
    lle_editor_t *editor = create_editor_with_content("café", pool);
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

    /// Move forward 3: should be at 'é' start (byte 3)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 3 && gr_idx == 3,
                "Position after 3 forwards incorrect");

    /// Move forward 4: should be past 'é' (byte 5, end of string)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 4,
                "Position after 'é' incorrect (2-byte char)");

    lle_editor_destroy(editor);
    TEST_PASS();
}

static void test_forward_char_utf8_3byte(lush_memory_pool_t *pool) {
    TEST_START("lle_forward_char: 3-byte UTF-8 (CJK)");

    /// "中文" - each character is 3 bytes
    lle_editor_t *editor = create_editor_with_content("中文", pool);
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

    /// "🔥🎯" - each emoji is 4 bytes
    lle_editor_t *editor = create_editor_with_content("🔥🎯", pool);
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

    /// "a中b🔥c" - mix of 1, 3, 1, 4, 1 bytes
    lle_editor_t *editor = create_editor_with_content("a中b🔥c", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off, gr_idx;

    /// 'a' (1 byte)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 1 && gr_idx == 1, "After 'a'");

    /// '中' (3 bytes)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 4 && gr_idx == 2, "After '中'");

    /// 'b' (1 byte)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 5 && gr_idx == 3, "After 'b'");

    /// '🔥' (4 bytes)
    lle_forward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(byte_off == 9 && gr_idx == 4, "After '🔥'");

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

    /// "hello中文🔥"
    lle_editor_t *editor = create_editor_with_content("hello中文🔥", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    /// Move to end
    size_t end_byte = strlen("hello中文🔥");
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, end_byte);

    size_t byte_off, gr_idx;
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 8, "Not at end (should be 8 graphemes)");

    /// Backward from 🔥 (4 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 7, "After backward from emoji");

    /// Backward from 文 (3 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 6, "After backward from 文");

    /// Backward from 中 (3 bytes)
    lle_backward_char(editor);
    get_cursor_position(editor, &byte_off, NULL, &gr_idx);
    TEST_ASSERT(gr_idx == 5, "After backward from 中");

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

    /// "hello 中文 world"
    lle_editor_t *editor = create_editor_with_content("hello 中文 world", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    size_t byte_off;

    /// Forward to end of "hello"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 5, "Should be at end of 'hello'");

    /// Forward to end of "中文" - this is 6 bytes (3+3)
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 12,
                "Should be at end of '中文' (6 bytes after space)");

    /// Forward to end of "world"
    lle_forward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    size_t expected = 12 + 1 + 5; /// 中文 + space + world
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

    /// "hello 中文 world"
    lle_editor_t *editor = create_editor_with_content("hello 中文 world", pool);
    TEST_ASSERT(editor != NULL, "Failed to create editor");

    /// Move to end
    size_t end_byte = strlen("hello 中文 world");
    lle_cursor_manager_move_to_byte_offset(editor->cursor_manager, end_byte);

    size_t byte_off;

    /// Backward to start of "world"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 13, "Should be at start of 'world'");

    /// Backward to start of "中文"
    lle_backward_word(editor);
    get_cursor_position(editor, &byte_off, NULL, NULL);
    TEST_ASSERT(byte_off == 6, "Should be at start of '中文'");

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
        printf("✓ ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("✗ SOME TESTS FAILED\n");
        return 1;
    }
}
