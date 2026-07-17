/**
 * @file test_brace_match.c
 * @brief Unit tests for lush_find_matching_brace
 *
 * Every test pins a concrete behavioral promise. The four "legacy-fix"
 * tests directly exercise defects the original src/strings.c
 * find_closing_brace got wrong; if the implementation drifts back to
 * the old behavior, these fail first.
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

/// Convenience: scan a NUL-terminated literal, return the closing index
/// or SIZE_MAX on failure (so we can ASSERT_EQ against a known good value).
static size_t scan(const char *s) {
    size_t off = 0;
    return lush_find_matching_brace(s, 0, &off) ? off : (size_t)-1;
}

/* ============================================================================
 * Basic shape
 * ============================================================================
 */

TEST(brace_simple_empty_brace) {
    ASSERT_EQ(scan("{}"), (size_t)1, "scan result matches expected");
}

TEST(brace_simple_empty_paren) {
    ASSERT_EQ(scan("()"), (size_t)1, "scan result matches expected");
}

TEST(brace_simple_content) {
    ASSERT_EQ(scan("{abc}"), (size_t)4, "scan result matches expected");
    ASSERT_EQ(scan("(abc)"), (size_t)4, "scan result matches expected");
}

TEST(brace_nested_braces) {
    ASSERT_EQ(scan("{{}}"), (size_t)3, "scan result matches expected");
    ASSERT_EQ(scan("{{a}b}"), (size_t)5, "scan result matches expected");
}

TEST(brace_nested_parens) {
    ASSERT_EQ(scan("(())"), (size_t)3, "scan result matches expected");
    ASSERT_EQ(scan("((a)b)"), (size_t)5, "scan result matches expected");
}

TEST(brace_brace_and_paren_do_not_cross) {
    /// `{(}` has a `}` at index 2, paren is unbalanced but irrelevant
    /// to brace counting since `(` is not the active opener.
    ASSERT_EQ(scan("{(}"), (size_t)2, "ok");
}

/* ============================================================================
 * Bad opener / unbalanced / short input
 * ============================================================================
 */

TEST(brace_bad_opener_returns_false) {
    size_t off = 0;
    ASSERT(!lush_find_matching_brace("xyz", 0, &off),
           "bad opener should be rejected");
    ASSERT(!lush_find_matching_brace("[abc]", 0, &off),
           "[ is not in the recognized opener set");
}

TEST(brace_short_input_returns_false) {
    size_t off = 0;
    ASSERT(!lush_find_matching_brace("", 0, &off), "empty input rejected");
    ASSERT(!lush_find_matching_brace("{", 0, &off), "lone open brace");
}

TEST(brace_unbalanced_returns_false) {
    size_t off = 0;
    ASSERT(!lush_find_matching_brace("{abc", 0, &off), "unterminated brace");
    ASSERT(!lush_find_matching_brace("{{}", 0, &off), "missing outer close");
}

TEST(brace_null_input_returns_false) {
    size_t off = 99;
    ASSERT(!lush_find_matching_brace(NULL, 0, &off), "NULL string rejected");
    ASSERT_EQ(off, (size_t)99, "ok"); ///< out_offset unchanged on failure
}

/* ============================================================================
 * Legacy-fix #1: backslash escapes outside quotes
 *
 * The old code's one-byte lookback for `s[i-1] == '\\'` got confused by
 * `\\` (a literal backslash that does NOT escape the next byte). Our
 * consume-forward `i += 2` correctly treats `\\` as one unit.
 * ============================================================================
 */

TEST(brace_double_backslash_does_not_shadow_next) {
    /// `\\` is two literal backslashes; the `}` after them closes.
    /// Legacy code: sees `\\` and `}`, looks back, mistakes `\` at
    /// i-1 as escaping the `}`, never closes.
    ASSERT_EQ(scan("{\\\\}"), (size_t)3, "ok");
}

TEST(brace_lone_backslash_escapes_next_byte) {
    /// `\}` -- `\` consumes the `}` so it does NOT close. Then a real
    /// `}` afterwards does.
    ASSERT_EQ(scan("{\\}}"), (size_t)3, "ok");
}

/* ============================================================================
 * Legacy-fix #2: POSIX single quotes are LITERAL (no escapes)
 *
 * The old code treated `\'` inside `'...'` as escaped, breaking the
 * `'\''` idiom. We treat `'...'` as fully literal.
 * ============================================================================
 */

TEST(brace_single_quote_is_literal_no_escape) {
    /// `{'\\'X}` -- inside POSIX `'...'`, `\\` is two LITERAL
    /// backslashes (no escape processing); the `'` after them closes
    /// the quote cleanly, then `X}` is outside the quote and the brace
    /// closes at index 6.
    ///
    /// The legacy code's escape-aware quoted-loop misclassifies the
    /// `'` as escaped by the preceding `\`, runs past the real close,
    /// keeps going to end-of-string, and reports failure (0).
    ASSERT_EQ(scan("{'\\\\'X}"), (size_t)6, "POSIX '...' is literal");
}

TEST(brace_brace_inside_single_quotes_is_not_counted) {
    /// `}` inside `'...'` does not close.
    ASSERT_EQ(scan("{'}'}"), (size_t)4, "ok");
}

TEST(brace_unterminated_single_quote_returns_false) {
    size_t off = 0;
    ASSERT(!lush_find_matching_brace("{'abc}", 0, &off),
           "unterminated '...' should fail");
}

/* ============================================================================
 * Legacy-fix #3: $'...' ANSI-C quoting is distinct from POSIX
 *
 * In `$'...'` a backslash IS an escape; `\'` is a literal quote that
 * does NOT close the quote. The legacy code did not recognize $'...'
 * at all and treated the `'` as a plain single quote.
 * ============================================================================
 */

TEST(brace_ansi_c_quote_backslash_escapes_close) {
    /// $'\''     -> literal backslash, literal quote, no close yet.
    /// Wrap with `{...}` so we can use the scanner.
    /// `{$'\''}` -- the `\'` is a literal `'`, so the inner $'...'
    /// is not yet closed. Then `'` closes. Then `}` closes the brace.
    ASSERT_EQ(scan("{$'\\''}"), (size_t)6, "ok");
}

TEST(brace_ansi_c_quote_normal_close) {
    /// `{$'abc'}` -- normal $'...' content followed by `}`.
    ASSERT_EQ(scan("{$'abc'}"), (size_t)7, "ok");
}

TEST(brace_ansi_c_quote_brace_inside) {
    /// `}` inside $'...' must not close.
    ASSERT_EQ(scan("{$'}'}"), (size_t)5, "ok");
}

/* ============================================================================
 * Double quote + backtick handling
 * ============================================================================
 */

TEST(brace_double_quote_backslash_consumes_pair) {
    /// `{"\""}` -- the inner `\"` is a one-unit escape; then `"` closes.
    ASSERT_EQ(scan("{\"\\\"\"}"), (size_t)5, "ok");
}

TEST(brace_double_quote_double_backslash) {
    /// `{"\\"}` -- the `\\` inside is escaped backslash + close quote.
    /// Length-wise: `{` `"` `\` `\` `"` `}` = 6 bytes, `}` at index 5.
    ASSERT_EQ(scan("{\"\\\\\"}"), (size_t)5, "ok");
}

TEST(brace_backtick_works_like_double_quote) {
    ASSERT_EQ(scan("{`abc`}"), (size_t)6, "ok");
    /// Inner `}` does not close.
    ASSERT_EQ(scan("{`}`}"), (size_t)4, "ok");
}

TEST(brace_unterminated_double_quote_returns_false) {
    size_t off = 0;
    ASSERT(!lush_find_matching_brace("{\"abc}", 0, &off),
           "unterminated \"...\" should fail");
}

/* ============================================================================
 * Const + length-based input (legacy `char *` + strlen)
 * ============================================================================
 */

TEST(brace_explicit_length_no_nul_dependency) {
    /// Pass a length shorter than the would-be NUL-terminated string;
    /// the function must stop at `len` and report unbalanced.
    const char *s = "{abc}xxx";
    size_t off = 0;
    /// First 5 bytes only: `{abc}` is balanced at offset 4.
    ASSERT(lush_find_matching_brace(s, 5, &off), "balanced within len");
    ASSERT_EQ(off, (size_t)4, "ok");
    /// Truncate to "{abc" -- now unbalanced.
    off = 99;
    ASSERT(!lush_find_matching_brace(s, 4, &off),
           "truncated to no close should fail");
    ASSERT_EQ(off, (size_t)99, "ok");
}

/* ============================================================================
 * Multi-byte UTF-8 content
 * ============================================================================
 */

TEST(brace_utf8_content_does_not_false_match) {
    /// Multi-byte sequences must advance atomically so a continuation
    /// byte that aliases an ASCII brace byte does not trigger.
    /// "café" is c-a-f-é where é is U+00E9 = 0xC3 0xA9.
    ASSERT_EQ(scan("{café}"), (size_t)6, "ok");
    /// CJK content (3-byte each):
    ASSERT_EQ(scan("{中文}"), (size_t)7, "ok");
}

/* ----------------------------------------------------------------------------
 * Case-aware `(` scan (#494): a case-pattern `)` has no matching `(` and must
 * not close the group. Each input is written so the real closing `)` is the
 * final byte, so the expected offset is strlen(s) - 1.
 * ------------------------------------------------------------------------- */

/// Assert the group opened at s[0] closes at the final byte.
#define ASSERT_CLOSES_AT_END(s, msg) ASSERT_EQ(scan(s), strlen(s) - 1, msg)

TEST(brace_case_pattern_paren_not_a_close) {
    ASSERT_CLOSES_AT_END("(case x in x) echo M;; esac)", "pattern ) skipped");
}

TEST(brace_case_multi_arm) {
    ASSERT_CLOSES_AT_END("(case ab in a) A;; b) B;; *) S;; esac)", "multi-arm");
}

TEST(brace_case_nested) {
    ASSERT_CLOSES_AT_END("(case a in a) case b in b) X;; esac;; esac)",
                         "nested case");
}

TEST(brace_case_esac_as_argument) {
    /// `esac` as an argument in an arm body is not the case terminator, so the
    /// group must not close early at the following pattern list.
    ASSERT_CLOSES_AT_END("(case x in x) echo esac;; esac)", "esac argument");
    ASSERT_CLOSES_AT_END("(case x in x) echo case in y;; esac)",
                         "case/in as arguments");
}

TEST(brace_case_alternation_and_leading_paren) {
    ASSERT_CLOSES_AT_END("(case b in a|b) AB;; esac)", "alternation");
    ASSERT_CLOSES_AT_END("(case x in (x) M;; esac)", "leading-( pattern");
    ASSERT_CLOSES_AT_END("(case foo in @(a|b)) E;; esac)", "extglob pattern");
}

TEST(brace_case_fallthrough_terminators) {
    ASSERT_CLOSES_AT_END("(case a in a) F;& b) G;; esac)", ";& fallthrough");
    ASSERT_CLOSES_AT_END("(case a in a) P;;& a) Q;; esac)", ";;& continue");
}

TEST(brace_case_subshell_in_body) {
    /// A real subshell `)` in an arm body IS balanced and counted.
    ASSERT_CLOSES_AT_END("(case a in a) (echo x);; esac)", "subshell in body");
}

TEST(brace_case_quoted_esac_pattern) {
    /// A quoted `esac` is pattern text, not the terminator.
    ASSERT_CLOSES_AT_END("(case x in \"esac\") Q;; x) X;; esac)",
                         "quoted esac is a pattern");
}

TEST(brace_case_keywords_as_arguments_inert) {
    /// Bare `case`/`esac` used as ordinary command words do not open or close a
    /// case; the group still closes at the right `)`.
    ASSERT_CLOSES_AT_END("(echo case)", "case as argument");
    ASSERT_CLOSES_AT_END("(echo esac)", "esac as argument");
}

TEST(brace_case_comment_swallows_paren) {
    /// A `#` comment at a word boundary runs to end of line, so a `)` inside it
    /// is not a group close.
    ASSERT_CLOSES_AT_END("(echo hi # ) esac\n)", "comment ) ignored");
}

TEST(brace_case_state_machine_inert_without_case) {
    /// Plain groups are byte-identical to the pre-case behavior.
    ASSERT_CLOSES_AT_END("(echo hi)", "plain group");
    ASSERT_CLOSES_AT_END("( (a) )", "nested subshell");
    ASSERT_EQ(scan("(echo \")\")"), strlen("(echo \")\")") - 1,
              "quoted ) not a close (#486)");
}

TEST(brace_case_after_compound_introducer) {
    /// A `case` on the SAME line as a compound-command reserved word keeps
    /// command position, so its pattern `)` is recognized (do/then/else/if/
    /// while/brace-group), not mis-read as the group close.
    ASSERT_CLOSES_AT_END("(for x in a; do case $x in a) echo A;; esac; done)",
                         "do case");
    ASSERT_CLOSES_AT_END("(if true; then case x in x) echo T;; esac; fi)",
                         "then case");
    ASSERT_CLOSES_AT_END("(if false; then :; else case x in x) E;; esac; fi)",
                         "else case");
    ASSERT_CLOSES_AT_END("(while false; do case x in x) W;; esac; done)",
                         "while ... do case");
    ASSERT_CLOSES_AT_END("({ case x in x) B;; esac; })", "brace-group case");
    /// `do`/`then` as ordinary arguments do NOT introduce a case.
    ASSERT_CLOSES_AT_END("(echo do then else)", "reserved words as arguments");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running brace_match tests...\n\n");

    printf("Basic shape:\n");
    RUN_TEST(brace_simple_empty_brace);
    RUN_TEST(brace_simple_empty_paren);
    RUN_TEST(brace_simple_content);
    RUN_TEST(brace_nested_braces);
    RUN_TEST(brace_nested_parens);
    RUN_TEST(brace_brace_and_paren_do_not_cross);

    printf("\nBad opener / unbalanced / short input:\n");
    RUN_TEST(brace_bad_opener_returns_false);
    RUN_TEST(brace_short_input_returns_false);
    RUN_TEST(brace_unbalanced_returns_false);
    RUN_TEST(brace_null_input_returns_false);

    printf("\nLegacy fix #1 (backslash outside quotes):\n");
    RUN_TEST(brace_double_backslash_does_not_shadow_next);
    RUN_TEST(brace_lone_backslash_escapes_next_byte);

    printf("\nLegacy fix #2 (POSIX '...' is literal):\n");
    RUN_TEST(brace_single_quote_is_literal_no_escape);
    RUN_TEST(brace_brace_inside_single_quotes_is_not_counted);
    RUN_TEST(brace_unterminated_single_quote_returns_false);

    printf("\nLegacy fix #3 ($'...' ANSI-C quoting):\n");
    RUN_TEST(brace_ansi_c_quote_backslash_escapes_close);
    RUN_TEST(brace_ansi_c_quote_normal_close);
    RUN_TEST(brace_ansi_c_quote_brace_inside);

    printf("\nDouble quote + backtick:\n");
    RUN_TEST(brace_double_quote_backslash_consumes_pair);
    RUN_TEST(brace_double_quote_double_backslash);
    RUN_TEST(brace_backtick_works_like_double_quote);
    RUN_TEST(brace_unterminated_double_quote_returns_false);

    printf("\nConst + explicit length:\n");
    RUN_TEST(brace_explicit_length_no_nul_dependency);

    printf("\nMulti-byte UTF-8:\n");
    RUN_TEST(brace_utf8_content_does_not_false_match);

    printf("\nCase-aware ( scan (#494):\n");
    RUN_TEST(brace_case_pattern_paren_not_a_close);
    RUN_TEST(brace_case_multi_arm);
    RUN_TEST(brace_case_nested);
    RUN_TEST(brace_case_esac_as_argument);
    RUN_TEST(brace_case_alternation_and_leading_paren);
    RUN_TEST(brace_case_fallthrough_terminators);
    RUN_TEST(brace_case_subshell_in_body);
    RUN_TEST(brace_case_quoted_esac_pattern);
    RUN_TEST(brace_case_keywords_as_arguments_inert);
    RUN_TEST(brace_case_comment_swallows_paren);
    RUN_TEST(brace_case_state_machine_inert_without_case);
    RUN_TEST(brace_case_after_compound_introducer);

    return TEST_RESULT();
}
