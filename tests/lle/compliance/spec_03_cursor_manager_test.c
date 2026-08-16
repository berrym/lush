/**
 * @file spec_03_cursor_manager_test.c
 * @brief Spec 03 cursor manager compliance test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * Spec 03 Cursor Manager Compliance Tests
 *
 * Drives the cursor manager over a real UTF-8, multi-line buffer and verifies
 * the guarantees in Spec 03 Section 6:
 * - Byte offsets convert to codepoint, grapheme, line, and column positions
 * - Grapheme and codepoint movement step over multi-byte clusters and newlines
 * - Line navigation finds line boundaries and clamps at the buffer edges
 * - Out-of-range positions are kept in bounds
 */

#include "lle/buffer_management.h"
#include "lush_memory_pool.h"

#include <stdio.h>
#include <string.h>

/// Defined in lush_memory_pool.c; set by lush_pool_init.
extern lush_memory_pool_t *global_memory_pool;

/// Buffer content, written with explicit UTF-8 bytes so the layout does not
/// depend on the source file encoding:
///   line 0 "cafeU+0301" spelled "caf" + U+00E9 (C3 A9)  -> 5 bytes, 4
///   codepoints line 1 "alpha beta gamma" U+03B1 U+03B2 U+03B3       -> 6
///   bytes, 3 codepoints line 2 "hi" -> 2 bytes, 2 codepoints
/// Plus two '\n' separators: 15 bytes, 11 codepoints, 11 graphemes total.
#define CONTENT "caf\xC3\xA9\n\xCE\xB1\xCE\xB2\xCE\xB3\nhi"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    do {                                                                       \
        printf("  Testing: %s ... ", name);                                    \
        fflush(stdout);                                                        \
        tests_run++;                                                           \
    } while (0)

#define PASS()                                                                 \
    do {                                                                       \
        printf("PASS\n");                                                      \
        tests_passed++;                                                        \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("FAIL: %s\n", msg);                                             \
        tests_failed++;                                                        \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            printf("\n    Expected: %zu, Got: %zu - %s\n", (size_t)(b),        \
                   (size_t)(a), msg);                                          \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("\n    Error code: %d - %s\n", (result), msg);              \
            FAIL(msg);                                                         \
            return;                                                            \
        }                                                                      \
    } while (0)

/// Build a buffer holding CONTENT and a cursor manager bound to it.
static bool make_fixture(lle_buffer_t **buf, lle_cursor_manager_t **mgr) {
    if (lle_buffer_create(buf, global_memory_pool, 0) != LLE_SUCCESS) {
        return false;
    }
    if (lle_buffer_insert_text(*buf, 0, CONTENT, strlen(CONTENT)) !=
        LLE_SUCCESS) {
        return false;
    }
    return lle_cursor_manager_init(mgr, *buf) == LLE_SUCCESS;
}

static void destroy_fixture(lle_buffer_t *buf, lle_cursor_manager_t *mgr) {
    lle_cursor_manager_destroy(mgr);
    lle_buffer_destroy(buf);
}

/// Test: manager binds to a real buffer and reports a valid position.
static void test_cursor_manager_init(void) {
    TEST("Cursor manager initializes against a real buffer");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");
    ASSERT_TRUE(mgr != NULL, "manager allocated");
    ASSERT_TRUE(mgr->buffer == buf, "manager references the buffer");

    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos),
                   "get_position after init");
    ASSERT_TRUE(pos.position_valid, "initial position is valid");

    destroy_fixture(buf, mgr);
    PASS();
}

/// Test: a byte offset resolves to every position dimension.
static void test_cursor_move_to_byte_offset(void) {
    TEST("move_to_byte_offset resolves codepoint/grapheme/line/column");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");

    /// Byte 5 is the end of line 0 ("caf" + 2-byte U+00E9): 4 codepoints in.
    ASSERT_SUCCESS(lle_cursor_manager_move_to_byte_offset(mgr, 5),
                   "move to byte 5");

    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 5, "byte offset");
    ASSERT_EQ(pos.codepoint_index, 4, "codepoint index (multi-byte aware)");
    ASSERT_EQ(pos.grapheme_index, 4, "grapheme index");
    ASSERT_EQ(pos.line_number, 0, "still on line 0");
    ASSERT_EQ(pos.column_codepoint, 4, "column in codepoints");

    destroy_fixture(buf, mgr);
    PASS();
}

/// Test: grapheme movement steps over newlines and multi-byte clusters.
static void test_cursor_move_by_graphemes(void) {
    TEST("move_by_graphemes crosses newlines and multi-byte clusters");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");

    ASSERT_SUCCESS(lle_cursor_manager_move_to_byte_offset(mgr, 5),
                   "start at end of line 0");

    /// One grapheme forward crosses the newline onto line 1.
    ASSERT_SUCCESS(lle_cursor_manager_move_by_graphemes(mgr, 1),
                   "advance past newline");
    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 6, "byte offset at line 1 start");
    ASSERT_EQ(pos.line_number, 1, "advanced to line 1");
    ASSERT_EQ(pos.column_codepoint, 0, "column reset at line start");

    /// The next grapheme is a 2-byte cluster (U+03B1): byte 6 -> 8.
    ASSERT_SUCCESS(lle_cursor_manager_move_by_graphemes(mgr, 1),
                   "advance past 2-byte cluster");
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 8, "byte offset past 2-byte cluster");
    ASSERT_EQ(pos.column_codepoint, 1, "one codepoint into line 1");

    destroy_fixture(buf, mgr);
    PASS();
}

/// Test: codepoint movement advances one codepoint, even multi-byte ones.
static void test_cursor_move_by_codepoints(void) {
    TEST("move_by_codepoints advances a single codepoint");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");

    ASSERT_SUCCESS(lle_cursor_manager_move_to_byte_offset(mgr, 6),
                   "start at line 1");
    ASSERT_SUCCESS(lle_cursor_manager_move_by_codepoints(mgr, 1),
                   "advance one codepoint");

    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 8, "byte offset past one 2-byte codepoint");
    ASSERT_EQ(pos.codepoint_index, 6, "codepoint index advanced by one");

    destroy_fixture(buf, mgr);
    PASS();
}

/// Test: line start/end find line boundaries; move_by_lines clamps at edges.
static void test_cursor_line_movement(void) {
    TEST("line start/end and move_by_lines navigate and clamp");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");

    /// Land in the middle of line 1, then snap to its boundaries.
    ASSERT_SUCCESS(lle_cursor_manager_move_to_byte_offset(mgr, 8),
                   "mid line 1");

    ASSERT_SUCCESS(lle_cursor_manager_move_to_line_start(mgr), "line start");
    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 6, "line 1 starts at byte 6");
    ASSERT_EQ(pos.column_codepoint, 0, "column 0 at line start");

    ASSERT_SUCCESS(lle_cursor_manager_move_to_line_end(mgr), "line end");
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.byte_offset, 12, "line 1 ends at byte 12 (before newline)");
    ASSERT_EQ(pos.line_number, 1, "still on line 1");

    /// Down one line lands on line 2 within its bounds (bytes 13..15).
    ASSERT_SUCCESS(lle_cursor_manager_move_by_lines(mgr, 1), "down one line");
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.line_number, 2, "advanced to line 2");
    ASSERT_TRUE(pos.byte_offset >= 13 && pos.byte_offset <= 15,
                "within line 2");

    /// Moving up past the top clamps to line 0.
    ASSERT_SUCCESS(lle_cursor_manager_move_by_lines(mgr, -5), "up past top");
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_EQ(pos.line_number, 0, "clamped to line 0");
    ASSERT_TRUE(pos.byte_offset <= 5, "within line 0");

    destroy_fixture(buf, mgr);
    PASS();
}

/// Test: out-of-range offsets and validation keep the cursor in bounds.
static void test_cursor_position_bounds_safety(void) {
    TEST("out-of-range positions are corrected to stay in bounds");

    lle_buffer_t *buf = NULL;
    lle_cursor_manager_t *mgr = NULL;
    ASSERT_TRUE(make_fixture(&buf, &mgr), "fixture construction");

    /// An offset past the end is rejected as out of range, and the existing
    /// position is left in bounds rather than corrupted.
    ASSERT_EQ(lle_cursor_manager_move_to_byte_offset(mgr, 99999),
              LLE_ERROR_INVALID_RANGE, "out-of-range offset is rejected");
    lle_cursor_position_t pos;
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_TRUE(pos.byte_offset <= buf->length,
                "position kept within buffer length");

    /// A deliberately corrupted stored position is repaired by validation.
    mgr->position.byte_offset = 99999;
    mgr->position.codepoint_index = 99999;
    mgr->position.position_valid = false;
    ASSERT_SUCCESS(lle_cursor_manager_validate_and_correct(mgr),
                   "validate_and_correct");
    ASSERT_SUCCESS(lle_cursor_manager_get_position(mgr, &pos), "get_position");
    ASSERT_TRUE(pos.byte_offset <= buf->length, "corrected within bounds");
    ASSERT_TRUE(pos.byte_offset != 99999, "corrupt offset was replaced");
    ASSERT_TRUE(pos.position_valid, "position marked valid after correction");

    destroy_fixture(buf, mgr);
    PASS();
}

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Spec 03: Cursor Manager Behavior\n");
    printf("=================================================\n\n");

    lush_pool_config_t config = lush_pool_get_default_config();
    if (lush_pool_init(&config) != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "FATAL: failed to initialize memory pool\n");
        return 1;
    }

    printf("Cursor Manager Behavior Tests:\n");
    test_cursor_manager_init();
    test_cursor_move_to_byte_offset();
    test_cursor_move_by_graphemes();
    test_cursor_move_by_codepoints();
    test_cursor_line_movement();
    test_cursor_position_bounds_safety();

    printf("\n");
    printf("=================================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
