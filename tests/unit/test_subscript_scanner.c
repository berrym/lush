/**
 * @file test_subscript_scanner.c
 * @brief Unit tests for scan_subscript_bounds (see brace_match.h)
 *
 * The subscript-opener sibling of lush_find_matching_brace. Every test pins a
 * concrete behavioral promise for the unified [ ] span-finder that issue #631
 * introduces: quote/escape/nesting/subexpr/UTF-8 awareness, has_whitespace
 * reporting, codepoint-accurate column accounting, and pure span-finding (no
 * meaning decisions). These promises are the contract the tokenizer, parameter
 * expansion, and arithmetic call sites will migrate onto in later phases.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "brace_match.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Convenience: scan a NUL-terminated literal via the strlen fallback.
static subscript_span_t sub(const char *s) {
    return scan_subscript_bounds(s, 0);
}

/* ============================================================================
 * Basic shape + close offset
 * ============================================================================
 */

TEST(sub_simple_single) {
    subscript_span_t s = sub("[a]");
    ASSERT(s.is_valid, "[a] is valid");
    ASSERT_EQ(s.close, (size_t)2, "] at index 2");
    ASSERT_EQ(s.codepoints, (size_t)3, "3 codepoints");
    ASSERT(!s.has_ws, "no whitespace");
}

TEST(sub_empty_is_valid) {
    /// #2.6: an empty subscript (empty associative key) is valid, not
    /// malformed.
    subscript_span_t s = sub("[]");
    ASSERT(s.is_valid, "[] is valid (empty key)");
    ASSERT_EQ(s.close, (size_t)1, "] at index 1");
    ASSERT_EQ(s.codepoints, (size_t)2, "2 codepoints");
    ASSERT(!s.has_ws, "no whitespace");
}

TEST(sub_multichar_key) {
    subscript_span_t s = sub("[abc]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)4, "] at index 4");
    ASSERT_EQ(s.codepoints, (size_t)5, "5 codepoints");
}

TEST(sub_trailing_bytes_ignored) {
    /// The scan stops at the first balanced ]; anything after is the caller's.
    subscript_span_t s = sub("[a]=value");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)2, "] at index 2, trailing =value ignored");
}

/* ============================================================================
 * Whitespace (the #631 flagship: a spaced key must stay one span)
 * ============================================================================
 */

TEST(sub_space_key) {
    subscript_span_t s = sub("[a b]");
    ASSERT(s.is_valid, "[a b] is valid");
    ASSERT_EQ(s.close, (size_t)4, "] at index 4 (space does not terminate)");
    ASSERT(s.has_ws, "has_ws true for unquoted interior space");
}

TEST(sub_multi_space_key) {
    subscript_span_t s = sub("[a b c]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)6, "] at index 6");
    ASSERT(s.has_ws, "has_ws true");
}

TEST(sub_tab_key) {
    subscript_span_t s = sub("[a\tb]");
    ASSERT(s.is_valid, "valid");
    ASSERT(s.has_ws, "has_ws true for a tab");
}

TEST(sub_no_ws_flag_when_none) {
    subscript_span_t s = sub("[abc]");
    ASSERT(!s.has_ws, "has_ws false without interior whitespace");
}

/* ============================================================================
 * Nested subscripts (depth counting)
 * ============================================================================
 */

TEST(sub_nested_brackets) {
    /// a[b]c inside: the inner ] must not close the outer subscript.
    subscript_span_t s = sub("[a[b]c]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)6, "] at the OUTER index 6, not the inner 4");
}

TEST(sub_deep_nested_brackets) {
    subscript_span_t s = sub("[a[b[c]d]e]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)10, "] at the outermost index 10");
}

/* ============================================================================
 * Quote-boundary awareness (no [ site had this before -- the flagship bug fix)
 * ============================================================================
 */

TEST(sub_double_quoted_close_bracket) {
    /// ["a]b"] : the ] inside the double quotes must NOT close.
    subscript_span_t s = sub("[\"a]b\"]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)6, "] at index 6, not the quoted one at 3");
}

TEST(sub_single_quoted_close_bracket) {
    subscript_span_t s = sub("['a]b']");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)6, "] at index 6, not the quoted one at 3");
}

TEST(sub_backtick_close_bracket) {
    subscript_span_t s = sub("[`echo ]`]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)9, "] at index 9, not the one inside backticks");
}

TEST(sub_ansi_c_quoted_close_bracket) {
    /// $'a]b' : ANSI-C quoting; the ] inside must not close.
    subscript_span_t s = sub("[$'a]b']");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)7, "] at index 7, not the quoted one");
}

TEST(sub_quoted_space_not_flagged) {
    /// A space inside quotes is not the unquoted whitespace has_ws reports.
    subscript_span_t s = sub("[\"a b\"]");
    ASSERT(s.is_valid, "valid");
    ASSERT(!s.has_ws, "has_ws false for a quoted space");
}

/* ============================================================================
 * Escape awareness (\X atomic, per the confirmed curation)
 * ============================================================================
 */

TEST(sub_escaped_close_bracket) {
    /// [a\]b] : the escaped ] must NOT close (\X atomic).
    subscript_span_t s = sub("[a\\]b]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)5, "] at index 5, not the escaped one at 3");
}

TEST(sub_escaped_space_not_flagged) {
    /// [a\ b] : an escaped space does not break an outer word, so not has_ws.
    subscript_span_t s = sub("[a\\ b]");
    ASSERT(s.is_valid, "valid");
    ASSERT(!s.has_ws, "has_ws false for an escaped space");
}

TEST(sub_double_backslash_then_close) {
    /// [a\\] : \\ is a literal backslash pair, so the following ] DOES close.
    subscript_span_t s = sub("[a\\\\]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)4, "] at index 4 closes after the \\\\ pair");
}

/* ============================================================================
 * Subexpression skip (compose with lush_find_matching_brace)
 * ============================================================================
 */

TEST(sub_command_sub_with_close_bracket) {
    /// [$(echo ])] : the ] inside $( ) must not close the subscript.
    subscript_span_t s = sub("[$(echo ])]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)10, "] at index 10, not the one inside $( )");
}

TEST(sub_param_exp_with_close_bracket) {
    /// [${x:-]}] : the ] inside ${ } must not close.
    subscript_span_t s = sub("[${x:-]}]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)8, "] at index 8, not the one inside ${ }");
}

TEST(sub_arith_with_inner_subscript) {
    /// [$((a[1]+1))] : an arithmetic subexpr containing its own [ ] is skipped
    /// wholesale; the outer ] closes.
    subscript_span_t s = sub("[$((a[1]+1))]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)12, "outer ] at index 12");
}

TEST(sub_bare_dollar_var_then_close) {
    /// [$i] : a $ not opening ( or { is a plain byte; the ] closes normally.
    subscript_span_t s = sub("[$i]");
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)3, "] at index 3");
}

TEST(sub_unterminated_command_sub_invalid) {
    /// [$(cmd] : the ] is semantically INSIDE the unterminated $( ), so the
    /// subscript is NOT validly closed -- must be invalid, not a false-valid.
    subscript_span_t s = sub("[$(cmd]");
    ASSERT(!s.is_valid, "unterminated $( ) -> invalid");
    ASSERT_EQ(s.close, (size_t)0, "close 0 on invalid");
    ASSERT(!s.has_ws, "no stale has_ws on invalid");
}

TEST(sub_unterminated_param_exp_invalid) {
    subscript_span_t s = sub("[${x]");
    ASSERT(!s.is_valid, "unterminated ${ } -> invalid");
}

TEST(sub_unterminated_sub_with_space_invalid) {
    /// The space at index 4 is inside the unterminated $( ); the span is
    /// invalid and must not leak has_ws from inside the would-be construct.
    subscript_span_t s = sub("[$(a b]c]");
    ASSERT(!s.is_valid, "invalid");
}

TEST(sub_truncated_inside_construct_invalid) {
    /// len cuts the buffer off inside the opening $( -- no close reachable.
    subscript_span_t s = scan_subscript_bounds("[$(", 3);
    ASSERT(!s.is_valid, "truncated inside $( -> invalid");
}

TEST(sub_balanced_subexpr_space_not_flagged) {
    /// A space inside a BALANCED $( ) is part of the command, not interior
    /// subscript whitespace -- has_ws stays false, span is valid.
    subscript_span_t s = sub("[$(echo a b)]");
    ASSERT(s.is_valid, "valid");
    ASSERT(!s.has_ws, "has_ws false: the space is inside the balanced $( )");
}

/* ============================================================================
 * Multi-byte UTF-8 (codepoint-accurate, no false match)
 * ============================================================================
 */

TEST(sub_utf8_key_codepoint_count) {
    /// [cafe-acute] : the multi-byte codepoint counts as ONE codepoint though
    /// its close byte offset reflects the extra byte.
    subscript_span_t s = sub("[caf\xc3\xa9]"); /// caf + U+00E9 + ]
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.close, (size_t)6, "] byte offset 6 (e-acute is 2 bytes)");
    ASSERT_EQ(s.codepoints, (size_t)6, "6 codepoints: [ c a f e-acute ]");
}

TEST(sub_utf8_multibyte_not_false_close) {
    /// A multi-byte sequence never aliases the ASCII ] (0x5D) byte.
    subscript_span_t s = sub("[\xe4\xb8\xad\xe6\x96\x87]"); /// two CJK + ]
    ASSERT(s.is_valid, "valid");
    ASSERT_EQ(s.codepoints, (size_t)4, "[ + 2 CJK + ] = 4 codepoints");
}

/* ============================================================================
 * Invalid / edge (report, do not decide)
 * ============================================================================
 */

TEST(sub_unbalanced_no_close) {
    subscript_span_t s = sub("[a b");
    ASSERT(!s.is_valid, "[a b is not valid (no close)");
    ASSERT_EQ(s.close, (size_t)0, "close 0 on invalid");
}

TEST(sub_unbalanced_nested_unclosed) {
    subscript_span_t s = sub("[a[b]");
    ASSERT(!s.is_valid, "[a[b] leaves outer open -> invalid");
}

TEST(sub_bad_opener_brace) {
    subscript_span_t s = sub("{abc}");
    ASSERT(!s.is_valid, "not a [ opener");
}

TEST(sub_bad_opener_plain) {
    subscript_span_t s = sub("abc");
    ASSERT(!s.is_valid, "not a [ opener");
}

TEST(sub_unterminated_quote_invalid) {
    subscript_span_t s = sub("[\"a]");
    ASSERT(!s.is_valid, "unterminated quote -> invalid");
}

/* ============================================================================
 * Length-bounded (not NUL-reliant)
 * ============================================================================
 */

TEST(sub_explicit_length_bounds_scan) {
    /// len cuts off before the real close; the ] beyond len is not seen.
    subscript_span_t s = scan_subscript_bounds("[ab]extra", 4);
    ASSERT(s.is_valid, "valid within the 4-byte window");
    ASSERT_EQ(s.close, (size_t)3, "] at index 3");
}

TEST(sub_explicit_length_truncated_invalid) {
    subscript_span_t s = scan_subscript_bounds("[ab]", 3);
    ASSERT(!s.is_valid, "] falls outside the 3-byte window -> invalid");
}

TEST(sub_length_zero_uses_strlen) {
    subscript_span_t s = scan_subscript_bounds("[ab]", 0);
    ASSERT(s.is_valid, "len==0 falls back to strlen");
    ASSERT_EQ(s.close, (size_t)3, "] at index 3");
}

int main(void) {
    printf("Running scan_subscript_bounds tests...\n\n");

    printf("Basic shape + offset:\n");
    RUN_TEST(sub_simple_single);
    RUN_TEST(sub_empty_is_valid);
    RUN_TEST(sub_multichar_key);
    RUN_TEST(sub_trailing_bytes_ignored);

    printf("\nWhitespace:\n");
    RUN_TEST(sub_space_key);
    RUN_TEST(sub_multi_space_key);
    RUN_TEST(sub_tab_key);
    RUN_TEST(sub_no_ws_flag_when_none);

    printf("\nNested subscripts:\n");
    RUN_TEST(sub_nested_brackets);
    RUN_TEST(sub_deep_nested_brackets);

    printf("\nQuote-boundary awareness:\n");
    RUN_TEST(sub_double_quoted_close_bracket);
    RUN_TEST(sub_single_quoted_close_bracket);
    RUN_TEST(sub_backtick_close_bracket);
    RUN_TEST(sub_ansi_c_quoted_close_bracket);
    RUN_TEST(sub_quoted_space_not_flagged);

    printf("\nEscape awareness (\\X atomic):\n");
    RUN_TEST(sub_escaped_close_bracket);
    RUN_TEST(sub_escaped_space_not_flagged);
    RUN_TEST(sub_double_backslash_then_close);

    printf("\nSubexpression skip:\n");
    RUN_TEST(sub_command_sub_with_close_bracket);
    RUN_TEST(sub_param_exp_with_close_bracket);
    RUN_TEST(sub_arith_with_inner_subscript);
    RUN_TEST(sub_bare_dollar_var_then_close);
    RUN_TEST(sub_unterminated_command_sub_invalid);
    RUN_TEST(sub_unterminated_param_exp_invalid);
    RUN_TEST(sub_unterminated_sub_with_space_invalid);
    RUN_TEST(sub_truncated_inside_construct_invalid);
    RUN_TEST(sub_balanced_subexpr_space_not_flagged);

    printf("\nMulti-byte UTF-8:\n");
    RUN_TEST(sub_utf8_key_codepoint_count);
    RUN_TEST(sub_utf8_multibyte_not_false_close);

    printf("\nInvalid / edge:\n");
    RUN_TEST(sub_unbalanced_no_close);
    RUN_TEST(sub_unbalanced_nested_unclosed);
    RUN_TEST(sub_bad_opener_brace);
    RUN_TEST(sub_bad_opener_plain);
    RUN_TEST(sub_unterminated_quote_invalid);

    printf("\nLength-bounded:\n");
    RUN_TEST(sub_explicit_length_bounds_scan);
    RUN_TEST(sub_explicit_length_truncated_invalid);
    RUN_TEST(sub_length_zero_uses_strlen);

    return TEST_RESULT();
}
