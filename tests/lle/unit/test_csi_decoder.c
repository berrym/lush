/**
 * @file test_csi_decoder.c
 * @brief Unit tests for lle_csi_decode + lle_ss3_decode.
 *
 * Every tilde-form, letter-final form, and SS3 form the live reader
 * emits is exercised here so the coverage gate reflects the decode
 * logic that production users actually hit.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/csi_decoder.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Helper: zero an event, call decoder, return whether-mapped.
static bool decode_csi(const char *params, char final, lle_input_event_t *e) {
    memset(e, 0, sizeof(*e));
    return lle_csi_decode(params, params ? strlen(params) : 0, final, e);
}

static bool decode_ss3(char final, lle_input_event_t *e) {
    memset(e, 0, sizeof(*e));
    return lle_ss3_decode(final, e);
}

/* ============================================================================
 * NULL / empty params
 * ============================================================================
 */

TEST(csi_null_event_returns_false) {
    ASSERT(!lle_csi_decode("5", 1, '~', NULL), "NULL event rejected");
    ASSERT(!lle_ss3_decode('P', NULL), "NULL event rejected (ss3)");
}

TEST(csi_empty_params_letter_works) {
    /// Plain arrows: ESC [ A, no params.
    lle_input_event_t e;
    ASSERT(decode_csi("", 'A', &e), "ESC [ A maps");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_UP, "UP");
    ASSERT_EQ((int)e.data.special_key.modifiers, 0, "no modifiers");
}

/* ============================================================================
 * Plain (unmodified) letter-final keys
 * ============================================================================
 */

TEST(csi_plain_arrows) {
    lle_input_event_t e;
    ASSERT(decode_csi("", 'A', &e), "A");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_UP, "UP");
    ASSERT(decode_csi("", 'B', &e), "B");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_DOWN, "DOWN");
    ASSERT(decode_csi("", 'C', &e), "C");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_RIGHT, "RIGHT");
    ASSERT(decode_csi("", 'D', &e), "D");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_LEFT, "LEFT");
}

TEST(csi_plain_home_end_letter_form) {
    lle_input_event_t e;
    ASSERT(decode_csi("", 'H', &e), "H");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_HOME, "HOME");
    ASSERT(decode_csi("", 'F', &e), "F");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_END, "END");
}

TEST(csi_unknown_letter_returns_false) {
    lle_input_event_t e;
    ASSERT(!decode_csi("", 'Z', &e), "Z unmapped");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_UNKNOWN, "UNKNOWN");
}

/* ============================================================================
 * Plain tilde-form keys
 * ============================================================================
 */

TEST(csi_plain_tilde_keys) {
    lle_input_event_t e;
    ASSERT(decode_csi("1", '~', &e), "1");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_HOME, "HOME(1)");
    ASSERT(decode_csi("7", '~', &e), "7");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_HOME, "HOME(7)");
    ASSERT(decode_csi("2", '~', &e), "2");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_INSERT, "INSERT");
    ASSERT(decode_csi("3", '~', &e), "3");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_DELETE, "DELETE");
    ASSERT(decode_csi("4", '~', &e), "4");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_END, "END(4)");
    ASSERT(decode_csi("8", '~', &e), "8");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_END, "END(8)");
    ASSERT(decode_csi("5", '~', &e), "5");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_PAGE_UP, "PgUp");
    ASSERT(decode_csi("6", '~', &e), "6");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_PAGE_DOWN, "PgDn");
}

TEST(csi_unknown_tilde_num_returns_false) {
    lle_input_event_t e;
    ASSERT(!decode_csi("99", '~', &e), "99~ unmapped");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_UNKNOWN, "UNKNOWN");
}

/* ============================================================================
 * F-keys via CSI tilde (F5-F12)
 * ============================================================================
 */

TEST(csi_f5_through_f12_tilde) {
    lle_input_event_t e;
    ASSERT(decode_csi("15", '~', &e), "15");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F5, "F5");
    ASSERT(decode_csi("17", '~', &e), "17");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F6, "F6");
    ASSERT(decode_csi("18", '~', &e), "18");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F7, "F7");
    ASSERT(decode_csi("19", '~', &e), "19");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F8, "F8");
    ASSERT(decode_csi("20", '~', &e), "20");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F9, "F9");
    ASSERT(decode_csi("21", '~', &e), "21");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F10, "F10");
    ASSERT(decode_csi("23", '~', &e), "23");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F11, "F11");
    ASSERT(decode_csi("24", '~', &e), "24");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F12, "F12");
}

TEST(csi_skipped_xterm_numbers_unmapped) {
    /// 16 and 22 are intentionally skipped in xterm's table.
    lle_input_event_t e;
    ASSERT(!decode_csi("16", '~', &e), "16~ unmapped");
    ASSERT(!decode_csi("22", '~', &e), "22~ unmapped");
}

/* ============================================================================
 * Modifier bits: every combination
 * ============================================================================
 */

TEST(csi_modifier_shift) {
    /// Shift+PageUp = ESC[5;2~ -- the exact form from #155.
    lle_input_event_t e;
    ASSERT(decode_csi("5;2", '~', &e), "5;2~");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_PAGE_UP, "PgUp");
    ASSERT_EQ((int)e.data.special_key.modifiers, (int)LLE_MOD_SHIFT, "Shift");
}

TEST(csi_modifier_alt) {
    /// Alt+PageDown = ESC[6;3~
    lle_input_event_t e;
    ASSERT(decode_csi("6;3", '~', &e), "6;3~");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_PAGE_DOWN, "PgDn");
    ASSERT_EQ((int)e.data.special_key.modifiers, (int)LLE_MOD_ALT, "Alt");
}

TEST(csi_modifier_ctrl_letter) {
    /// Ctrl+End = ESC[1;5F -- another form from #155.
    lle_input_event_t e;
    ASSERT(decode_csi("1;5", 'F', &e), "1;5 F");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_END, "END");
    ASSERT_EQ((int)e.data.special_key.modifiers, (int)LLE_MOD_CTRL, "Ctrl");
}

TEST(csi_modifier_shift_alt) {
    /// Shift+Alt = mod 4 (1 + 1 + 2)
    lle_input_event_t e;
    ASSERT(decode_csi("1;4", 'H', &e), "1;4 H");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_HOME, "HOME");
    ASSERT_EQ((int)e.data.special_key.modifiers,
              (int)(LLE_MOD_SHIFT | LLE_MOD_ALT), "Shift+Alt");
}

TEST(csi_modifier_ctrl_alt) {
    /// Ctrl+Alt = mod 7 (1 + 2 + 4)
    lle_input_event_t e;
    ASSERT(decode_csi("3;7", '~', &e), "3;7~");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_DELETE, "DELETE");
    ASSERT_EQ((int)e.data.special_key.modifiers,
              (int)(LLE_MOD_ALT | LLE_MOD_CTRL), "Alt+Ctrl");
}

TEST(csi_modifier_all_three) {
    /// Shift+Alt+Ctrl = mod 8 (1 + 1 + 2 + 4)
    lle_input_event_t e;
    ASSERT(decode_csi("2;8", '~', &e), "2;8~");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_INSERT, "INSERT");
    ASSERT_EQ((int)e.data.special_key.modifiers,
              (int)(LLE_MOD_SHIFT | LLE_MOD_ALT | LLE_MOD_CTRL), "all three");
}

TEST(csi_modifier_zero_or_one_means_no_modifier) {
    /// mod=1 is the "no modifier" floor in xterm's encoding.
    lle_input_event_t e;
    ASSERT(decode_csi("5;1", '~', &e), "5;1~");
    ASSERT_EQ((int)e.data.special_key.modifiers, 0, "no mod");
    ASSERT(decode_csi("5;0", '~', &e), "5;0~");
    ASSERT_EQ((int)e.data.special_key.modifiers, 0, "no mod (0)");
}

/* ============================================================================
 * SS3 (ESC O <final>)
 * ============================================================================
 */

TEST(ss3_home_end) {
    lle_input_event_t e;
    ASSERT(decode_ss3('H', &e), "OH");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_HOME, "HOME");
    ASSERT(decode_ss3('F', &e), "OF");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_END, "END");
}

TEST(ss3_f1_through_f4) {
    lle_input_event_t e;
    ASSERT(decode_ss3('P', &e), "OP");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F1, "F1");
    ASSERT(decode_ss3('Q', &e), "OQ");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F2, "F2");
    ASSERT(decode_ss3('R', &e), "OR");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F3, "F3");
    ASSERT(decode_ss3('S', &e), "OS");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_F4, "F4");
}

TEST(ss3_unknown_returns_false) {
    lle_input_event_t e;
    ASSERT(!decode_ss3('Z', &e), "OZ unmapped");
    ASSERT_EQ((int)e.data.special_key.key, (int)LLE_KEY_UNKNOWN, "UNKNOWN");
}

TEST(ss3_modifiers_always_zero) {
    /// SS3 has no modifier param in this dialect.
    lle_input_event_t e;
    ASSERT(decode_ss3('P', &e), "OP");
    ASSERT_EQ((int)e.data.special_key.modifiers, 0, "no modifiers");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running CSI/SS3 decoder tests...\n\n");

    printf("Edge cases:\n");
    RUN_TEST(csi_null_event_returns_false);
    RUN_TEST(csi_empty_params_letter_works);

    printf("\nPlain letter-final keys:\n");
    RUN_TEST(csi_plain_arrows);
    RUN_TEST(csi_plain_home_end_letter_form);
    RUN_TEST(csi_unknown_letter_returns_false);

    printf("\nPlain tilde-form keys:\n");
    RUN_TEST(csi_plain_tilde_keys);
    RUN_TEST(csi_unknown_tilde_num_returns_false);

    printf("\nF-keys via CSI tilde:\n");
    RUN_TEST(csi_f5_through_f12_tilde);
    RUN_TEST(csi_skipped_xterm_numbers_unmapped);

    printf("\nModifier combinations:\n");
    RUN_TEST(csi_modifier_shift);
    RUN_TEST(csi_modifier_alt);
    RUN_TEST(csi_modifier_ctrl_letter);
    RUN_TEST(csi_modifier_shift_alt);
    RUN_TEST(csi_modifier_ctrl_alt);
    RUN_TEST(csi_modifier_all_three);
    RUN_TEST(csi_modifier_zero_or_one_means_no_modifier);

    printf("\nSS3 forms:\n");
    RUN_TEST(ss3_home_end);
    RUN_TEST(ss3_f1_through_f4);
    RUN_TEST(ss3_unknown_returns_false);
    RUN_TEST(ss3_modifiers_always_zero);

    return TEST_RESULT();
}
