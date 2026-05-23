/**
 * @file test_utf8_index.c
 * @brief Unit tests for LLE UTF-8 position index
 *
 * Exercises lle_utf8_index_t and its eleven public functions:
 *   init, cleanup, rebuild, byte_to_codepoint, codepoint_to_byte,
 *   codepoint_to_grapheme, grapheme_to_codepoint, grapheme_to_display,
 *   display_to_grapheme, invalidate, is_valid
 *
 * Tests cover:
 *   - Pure ASCII (1 byte = 1 codepoint = 1 grapheme = 1 column)
 *   - Multi-byte UTF-8 (CJK, Latin-1) where bytes != codepoints
 *   - Combining marks where codepoints != graphemes
 *   - Wide characters where graphemes != display columns
 *   - Round-trip: byte → codepoint → byte and similar
 *   - Error paths: NULL pointers, invalid state, out-of-range indices
 *   - State machine: init → rebuild → invalidate → is_valid
 *   - Invalid UTF-8 input handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/buffer_management.h" // full struct definition
#include "lle/error_handling.h"
#include "lle/utf8_index.h"
#include "test_framework.h"

#include <string.h>

/* ============================================================================
 * Lifecycle: init / cleanup / is_valid
 * ============================================================================
 */

TEST(init_zeroes_the_struct_and_invalidates) {
    lle_utf8_index_t idx;
    // Fill with garbage to confirm init clears it
    memset(&idx, 0xAB, sizeof(idx));
    ASSERT_EQ(lle_utf8_index_init(&idx), LLE_SUCCESS, "init returns SUCCESS");
    ASSERT_FALSE(lle_utf8_index_is_valid(&idx), "fresh index is not yet valid");
    ASSERT_EQ(idx.byte_count, 0, "byte_count zeroed");
    ASSERT_EQ(idx.codepoint_count, 0, "codepoint_count zeroed");
    ASSERT_EQ(idx.grapheme_count, 0, "grapheme_count zeroed");
    ASSERT_EQ(idx.display_width, 0, "display_width zeroed");
    lle_utf8_index_cleanup(&idx);
}

TEST(init_rejects_null) {
    ASSERT_EQ(lle_utf8_index_init(NULL), LLE_ERROR_INVALID_PARAMETER,
              "init(NULL) returns INVALID_PARAMETER");
}

TEST(cleanup_tolerates_null) {
    lle_utf8_index_cleanup(NULL); // must not crash
    // If we got here, the cleanup did not crash.
    ASSERT_TRUE(1, "cleanup(NULL) did not crash");
}

TEST(is_valid_returns_false_on_null) {
    ASSERT_FALSE(lle_utf8_index_is_valid(NULL), "is_valid(NULL) returns false");
}

/* ============================================================================
 * rebuild — basic shapes
 * ============================================================================
 */

TEST(rebuild_empty_text) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, "", 0), LLE_SUCCESS,
              "rebuild('') succeeds");
    ASSERT_TRUE(lle_utf8_index_is_valid(&idx), "index is valid after rebuild");
    ASSERT_EQ(idx.byte_count, 0, "empty text -> 0 bytes");
    ASSERT_EQ(idx.codepoint_count, 0, "empty text -> 0 codepoints");
    ASSERT_EQ(idx.grapheme_count, 0, "empty text -> 0 graphemes");
    ASSERT_EQ(idx.display_width, 0, "empty text -> 0 columns");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_pure_ascii) {
    // "abc": 3 bytes, 3 codepoints, 3 graphemes, 3 display columns
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, "abc", 3), LLE_SUCCESS,
              "rebuild('abc') succeeds");
    ASSERT_EQ(idx.byte_count, 3, "byte_count");
    ASSERT_EQ(idx.codepoint_count, 3, "codepoint_count");
    ASSERT_EQ(idx.grapheme_count, 3, "grapheme_count");
    ASSERT_EQ(idx.display_width, 3, "display_width");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_multibyte_cjk) {
    // "中" (U+4E2D) is 3 bytes, 1 codepoint, 1 grapheme, 2 columns wide
    const char *text = "\xE4\xB8\xAD";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 3), LLE_SUCCESS,
              "rebuild('中') succeeds");
    ASSERT_EQ(idx.byte_count, 3, "byte_count for one CJK char");
    ASSERT_EQ(idx.codepoint_count, 1, "codepoint_count for one CJK char");
    ASSERT_EQ(idx.grapheme_count, 1, "grapheme_count for one CJK char");
    ASSERT_EQ(idx.display_width, 2, "CJK is 2 columns wide");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_combining_mark_collapses_graphemes) {
    /* "e" + U+0301 (combining acute) = "é" as 2 codepoints, 1 grapheme.
     * "e" is 1 byte, U+0301 is 2 bytes (0xCC 0x81). */
    const char *text = "e\xCC\x81";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 3), LLE_SUCCESS,
              "rebuild succeeds");
    ASSERT_EQ(idx.byte_count, 3, "3 bytes total");
    ASSERT_EQ(idx.codepoint_count, 2, "2 codepoints");
    ASSERT_EQ(idx.grapheme_count, 1, "1 grapheme (combining mark attaches)");
    ASSERT_EQ(idx.display_width, 1, "1 column wide");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_mixed_ascii_and_cjk) {
    /* "A中B": 1 + 3 + 1 = 5 bytes, 3 codepoints, 3 graphemes,
     * 1 + 2 + 1 = 4 columns. */
    const char *text = "A\xE4\xB8\xAD"
                       "B";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 5), LLE_SUCCESS,
              "rebuild succeeds");
    ASSERT_EQ(idx.byte_count, 5, "byte count");
    ASSERT_EQ(idx.codepoint_count, 3, "codepoint count");
    ASSERT_EQ(idx.grapheme_count, 3, "grapheme count");
    ASSERT_EQ(idx.display_width, 4, "1 + 2 + 1");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_rejects_null_text) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, NULL, 0),
              LLE_ERROR_INVALID_PARAMETER,
              "rebuild(NULL text) returns INVALID_PARAMETER");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_rejects_null_index) {
    ASSERT_EQ(lle_utf8_index_rebuild(NULL, "x", 1), LLE_ERROR_INVALID_PARAMETER,
              "rebuild(NULL index) returns INVALID_PARAMETER");
}

TEST(rebuild_rejects_invalid_utf8_truncated) {
    // 0xE4 introduces a 3-byte sequence but the buffer ends after 1 byte
    const char *text = "\xE4";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 1), LLE_ERROR_INVALID_ENCODING,
              "truncated UTF-8 -> INVALID_ENCODING");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_rejects_invalid_utf8_lone_continuation) {
    // 0x80 alone is a continuation byte with no lead byte
    const char *text = "\x80";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 1), LLE_ERROR_INVALID_ENCODING,
              "lone continuation -> INVALID_ENCODING");
    lle_utf8_index_cleanup(&idx);
}

TEST(rebuild_rejects_invalid_utf8_overlong) {
    // Overlong encoding of '/': 0xC0 0xAF (should be 1 byte)
    const char *text = "\xC0\xAF";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 2), LLE_ERROR_INVALID_ENCODING,
              "overlong encoding -> INVALID_ENCODING");
    lle_utf8_index_cleanup(&idx);
}

/* ============================================================================
 * Round-trip lookups on a known shape
 *
 * Use "A中B" — three graphemes, three codepoints, five bytes, four columns.
 * Byte map (0-indexed):
 *   0    : 'A'   (codepoint 0, grapheme 0, column 0)
 *   1-3  : '中'  (codepoint 1, grapheme 1, column 1)
 *   4    : 'B'   (codepoint 2, grapheme 2, column 3)
 * ============================================================================
 */

TEST(roundtrip_byte_codepoint_and_back) {
    const char *text = "A\xE4\xB8\xAD"
                       "B";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 5), LLE_SUCCESS, "rebuild");

    // Byte 0 -> codepoint 0
    size_t cp;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 0, &cp), LLE_SUCCESS,
              "byte 0 lookup");
    ASSERT_EQ(cp, 0, "byte 0 -> codepoint 0");

    // Byte 1 (start of '中') -> codepoint 1
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 1, &cp), LLE_SUCCESS,
              "byte 1 lookup");
    ASSERT_EQ(cp, 1, "byte 1 (CJK lead byte) -> codepoint 1");

    // Byte 4 (start of 'B') -> codepoint 2
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 4, &cp), LLE_SUCCESS,
              "byte 4 lookup");
    ASSERT_EQ(cp, 2, "byte 4 -> codepoint 2");

    // And the reverse direction: codepoint -> byte
    size_t b;
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 0, &b), LLE_SUCCESS,
              "cp 0");
    ASSERT_EQ(b, 0, "codepoint 0 -> byte 0");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 1, &b), LLE_SUCCESS,
              "cp 1");
    ASSERT_EQ(b, 1, "codepoint 1 -> byte 1 (start of CJK)");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 2, &b), LLE_SUCCESS,
              "cp 2");
    ASSERT_EQ(b, 4, "codepoint 2 -> byte 4");

    lle_utf8_index_cleanup(&idx);
}

TEST(roundtrip_codepoint_grapheme_and_back) {
    /* "e\xCC\x81X": 'e', combining acute, 'X'.
     *   bytes: 0='e', 1-2 = combining, 3='X'
     *   codepoints: 0='e', 1=combining, 2='X'
     *   graphemes:  0=é (cp 0+1), 1=X (cp 2) */
    const char *text = "e\xCC\x81"
                       "X";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 4), LLE_SUCCESS, "rebuild");

    ASSERT_EQ(idx.codepoint_count, 3, "3 codepoints");
    ASSERT_EQ(idx.grapheme_count, 2, "2 graphemes");

    // Codepoint 0 ('e') -> grapheme 0
    size_t g;
    ASSERT_EQ(lle_utf8_index_codepoint_to_grapheme(&idx, 0, &g), LLE_SUCCESS,
              "cp 0 -> g");
    ASSERT_EQ(g, 0, "'e' -> grapheme 0");

    // Codepoint 1 (combining) attaches to grapheme 0
    ASSERT_EQ(lle_utf8_index_codepoint_to_grapheme(&idx, 1, &g), LLE_SUCCESS,
              "cp 1 -> g");
    ASSERT_EQ(g, 0, "combining mark -> still grapheme 0");

    // Codepoint 2 ('X') -> grapheme 1
    ASSERT_EQ(lle_utf8_index_codepoint_to_grapheme(&idx, 2, &g), LLE_SUCCESS,
              "cp 2 -> g");
    ASSERT_EQ(g, 1, "'X' -> grapheme 1");

    // Reverse: grapheme -> first codepoint of cluster
    size_t cp;
    ASSERT_EQ(lle_utf8_index_grapheme_to_codepoint(&idx, 0, &cp), LLE_SUCCESS,
              "g 0 -> cp");
    ASSERT_EQ(cp, 0, "grapheme 0 starts at codepoint 0");
    ASSERT_EQ(lle_utf8_index_grapheme_to_codepoint(&idx, 1, &cp), LLE_SUCCESS,
              "g 1 -> cp");
    ASSERT_EQ(cp, 2, "grapheme 1 starts at codepoint 2");

    lle_utf8_index_cleanup(&idx);
}

TEST(roundtrip_grapheme_display_and_back) {
    /* "A中B": graphemes at columns 0, 1, 3.
     * display columns: 0 -> 'A', 1 -> '中' (start), 2 -> '中' (cont), 3 -> 'B'
     */
    const char *text = "A\xE4\xB8\xAD"
                       "B";
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, text, 5), LLE_SUCCESS, "rebuild");

    // grapheme -> display
    size_t col;
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(&idx, 0, &col), LLE_SUCCESS,
              "g 0");
    ASSERT_EQ(col, 0, "grapheme 0 ('A') at column 0");
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(&idx, 1, &col), LLE_SUCCESS,
              "g 1");
    ASSERT_EQ(col, 1, "grapheme 1 ('中') at column 1");
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(&idx, 2, &col), LLE_SUCCESS,
              "g 2");
    ASSERT_EQ(col, 3, "grapheme 2 ('B') at column 3 (after 中's 2 cols)");

    // display -> grapheme
    size_t g;
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(&idx, 0, &g), LLE_SUCCESS,
              "col 0");
    ASSERT_EQ(g, 0, "column 0 -> grapheme 0");
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(&idx, 1, &g), LLE_SUCCESS,
              "col 1");
    ASSERT_EQ(g, 1, "column 1 -> grapheme 1 (lead column of 中)");
    /* Column 2 is the continuation column of 中 — implementation-defined,
     * must be either grapheme 1 or grapheme 2 (the next cluster). */
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(&idx, 2, &g), LLE_SUCCESS,
              "col 2");
    ASSERT_TRUE(g == 1 || g == 2,
                "column 2 maps to grapheme 1 or 2 (cont col of CJK)");
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(&idx, 3, &g), LLE_SUCCESS,
              "col 3");
    ASSERT_EQ(g, 2, "column 3 -> grapheme 2");

    lle_utf8_index_cleanup(&idx);
}

/* ============================================================================
 * Error paths
 * ============================================================================
 */

TEST(lookups_reject_null_index) {
    size_t out;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "byte_to_codepoint NULL");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "codepoint_to_byte NULL");
    ASSERT_EQ(lle_utf8_index_codepoint_to_grapheme(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "codepoint_to_grapheme NULL");
    ASSERT_EQ(lle_utf8_index_grapheme_to_codepoint(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "grapheme_to_codepoint NULL");
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "grapheme_to_display NULL");
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(NULL, 0, &out),
              LLE_ERROR_INVALID_PARAMETER, "display_to_grapheme NULL");
}

TEST(lookups_reject_null_output) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    lle_utf8_index_rebuild(&idx, "abc", 3);
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 0, NULL),
              LLE_ERROR_INVALID_PARAMETER, "byte_to_codepoint NULL out");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 0, NULL),
              LLE_ERROR_INVALID_PARAMETER, "codepoint_to_byte NULL out");
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(&idx, 0, NULL),
              LLE_ERROR_INVALID_PARAMETER, "grapheme_to_display NULL out");
    lle_utf8_index_cleanup(&idx);
}

TEST(lookups_reject_invalid_state_when_index_not_built) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx); // not yet rebuilt
    size_t out;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 0, &out),
              LLE_ERROR_INVALID_STATE,
              "lookup before rebuild -> INVALID_STATE");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 0, &out),
              LLE_ERROR_INVALID_STATE, "codepoint_to_byte INVALID_STATE");
    lle_utf8_index_cleanup(&idx);
}

TEST(lookups_reject_out_of_range_indices) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    lle_utf8_index_rebuild(&idx, "abc", 3); /* 3 bytes, 3 cps, 3 graphemes,
                                             * 3 display cols */
    size_t out;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "byte 3 (off-end) out of range");
    ASSERT_EQ(lle_utf8_index_codepoint_to_byte(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "codepoint 3 out of range");
    ASSERT_EQ(lle_utf8_index_codepoint_to_grapheme(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "codepoint 3 -> grapheme out of range");
    ASSERT_EQ(lle_utf8_index_grapheme_to_codepoint(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "grapheme 3 out of range");
    ASSERT_EQ(lle_utf8_index_grapheme_to_display(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "grapheme 3 -> display out of range");
    ASSERT_EQ(lle_utf8_index_display_to_grapheme(&idx, 3, &out),
              LLE_ERROR_INVALID_RANGE, "column 3 out of range");
    lle_utf8_index_cleanup(&idx);
}

/* ============================================================================
 * State machine: invalidate / is_valid / rebuild
 * ============================================================================
 */

TEST(invalidate_marks_index_invalid) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    lle_utf8_index_rebuild(&idx, "abc", 3);
    ASSERT_TRUE(lle_utf8_index_is_valid(&idx), "valid after rebuild");
    lle_utf8_index_invalidate(&idx);
    ASSERT_FALSE(lle_utf8_index_is_valid(&idx), "invalid after invalidate()");
    // Lookups should now refuse to operate
    size_t out;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 0, &out),
              LLE_ERROR_INVALID_STATE,
              "lookup after invalidate -> INVALID_STATE");
    lle_utf8_index_cleanup(&idx);
}

TEST(invalidate_tolerates_null) {
    lle_utf8_index_invalidate(NULL); // must not crash
    ASSERT_TRUE(1, "invalidate(NULL) did not crash");
}

TEST(rebuild_after_invalidate_restores_validity) {
    lle_utf8_index_t idx;
    lle_utf8_index_init(&idx);
    lle_utf8_index_rebuild(&idx, "abc", 3);
    lle_utf8_index_invalidate(&idx);
    ASSERT_FALSE(lle_utf8_index_is_valid(&idx), "invalidated");
    ASSERT_EQ(lle_utf8_index_rebuild(&idx, "xyz", 3), LLE_SUCCESS,
              "second rebuild succeeds");
    ASSERT_TRUE(lle_utf8_index_is_valid(&idx), "valid again after rebuild");
    // And the new content is reflected
    size_t cp;
    ASSERT_EQ(lle_utf8_index_byte_to_codepoint(&idx, 2, &cp), LLE_SUCCESS,
              "byte 2 lookup");
    ASSERT_EQ(cp, 2, "third byte -> third codepoint");
    lle_utf8_index_cleanup(&idx);
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE UTF-8 Index Tests ===\n\n");

    printf("--- Lifecycle ---\n");
    RUN_TEST(init_zeroes_the_struct_and_invalidates);
    RUN_TEST(init_rejects_null);
    RUN_TEST(cleanup_tolerates_null);
    RUN_TEST(is_valid_returns_false_on_null);

    printf("\n--- Rebuild: shapes ---\n");
    RUN_TEST(rebuild_empty_text);
    RUN_TEST(rebuild_pure_ascii);
    RUN_TEST(rebuild_multibyte_cjk);
    RUN_TEST(rebuild_combining_mark_collapses_graphemes);
    RUN_TEST(rebuild_mixed_ascii_and_cjk);

    printf("\n--- Rebuild: input validation ---\n");
    RUN_TEST(rebuild_rejects_null_text);
    RUN_TEST(rebuild_rejects_null_index);
    RUN_TEST(rebuild_rejects_invalid_utf8_truncated);
    RUN_TEST(rebuild_rejects_invalid_utf8_lone_continuation);
    RUN_TEST(rebuild_rejects_invalid_utf8_overlong);

    printf("\n--- Round-trip lookups ---\n");
    RUN_TEST(roundtrip_byte_codepoint_and_back);
    RUN_TEST(roundtrip_codepoint_grapheme_and_back);
    RUN_TEST(roundtrip_grapheme_display_and_back);

    printf("\n--- Error paths ---\n");
    RUN_TEST(lookups_reject_null_index);
    RUN_TEST(lookups_reject_null_output);
    RUN_TEST(lookups_reject_invalid_state_when_index_not_built);
    RUN_TEST(lookups_reject_out_of_range_indices);

    printf("\n--- State machine ---\n");
    RUN_TEST(invalidate_marks_index_invalid);
    RUN_TEST(invalidate_tolerates_null);
    RUN_TEST(rebuild_after_invalidate_restores_validity);

    return TEST_RESULT();
}
