/**
 * @file test_dequote.c
 * @brief Tests for lush_dequote_span (#631 quote-removal consolidation, Phase
 * 0).
 *
 * Two layers:
 *  1. A DIFFERENTIAL harness that feeds a corpus of quote-starting single-token
 *     inputs through BOTH the live tokenizer (Site 1, the reference) and
 *     lush_dequote_span, asserting byte-for-byte identity of (text,
 * quote_prov). This proves the primitive is a faithful extraction without yet
 * rewiring Site 1.
 *  2. DIRECT unit tests, including the unquoted-starting spans (m["a b"]) that
 *     C1 / Phase 2c will feed but the tokenizer's Site 1 never sees.
 *
 * Out of the primitive's scope (excluded from the differential corpus):
 * trailing zsh glob-qualifier groups, fused ANSI-C $'...' (the reader disables
 * provenance there), and unquoted break characters -- all tokenizer-adjacency
 * concerns.
 */
#include <stdlib.h>
#include <string.h>

#include "dequote.h"
#include "test_framework.h"
#include "tokenizer.h"

/// Run `input` through the tokenizer and, if the first token carries a
/// provenance map, assert lush_dequote_span reproduces its text and map
/// exactly.
static void assert_matches_reader(const char *input) {
    tokenizer_t *tok = tokenizer_new(input);
    ASSERT_NOT_NULL(tok, "tokenizer_new failed");
    token_t *t = tokenizer_current(tok);
    ASSERT_NOT_NULL(t, "no token produced");
    ASSERT_NOT_NULL(t->text, "token has no text");
    ASSERT_NOT_NULL(t->quote_prov,
                    "reference token has no provenance (input out of scope)");

    char *text = NULL, *prov = NULL;
    dequote_flags_t flags;
    bool ok = lush_dequote_span(input, strlen(input), &text, &prov, &flags);
    ASSERT_TRUE(ok, "lush_dequote_span failed");
    ASSERT_STR_EQ(text, t->text, "dequoted text differs from reader");
    size_t n = strlen(t->text);
    ASSERT_EQ(memcmp(prov, t->quote_prov, n), 0,
              "provenance map differs from reader");

    free(text);
    free(prov);
    tokenizer_free(tok);
}

TEST(dequote_differential_vs_reader) {
    /// Each entry is one whole token the reader consumes end to end.
    const char *corpus[] = {
        "\"a b\"",
        "'a b'",
        "\"\"",
        "''",
        "\"hello\"",
        "'hello'",
        "\"a\\\"b\"", /// "a\"b"  -> a\"b (deferred DQ escape)
        "\"a\\\\b\"", /// "a\\b"  -> a\\b
        "\"a\\$b\"",  /// "a\$b"  -> a\$b (deferred)
        "\"a\\zb\"",  /// non-DQ-meaningful escape: reader keeps \z (DDDD)
        "\"$x\"",
        "\"pre$x\"",
        "\"a$(echo hi)b\"",     /// verbatim command substitution
        "\"a$()b\"",            /// empty command substitution
        "\"a$(f $(g))b\"",      /// nested command substitution
        "\"a`echo hi`b\"",      /// backtick substitution (separate code path)
        "\"a`echo \\`x\\``b\"", /// escaped backtick inside backtick
        "\"a\nb\"",             /// literal newline inside double quotes -> D
        "\"a\\\nb\"",           /// double-quote line continuation -> removed
        "'a\\b'", /// single-quote keeps the backslash literally (SSS)
        "'a'\"b\"",
        "\"a\"'b'",
        "'a'\"b\"'c'",
        "\"foo\"bar",
        "'x'yz",
        "\"a\"b\"c\"",
        "\"a\"b\\ c", /// unquoted escaped space in the fused tail -> E
    };
    for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
        assert_matches_reader(corpus[i]);
    }
}

/// Direct assertion: input -> expected dequoted text + expected provenance.
static void assert_dequote(const char *input, const char *want_text,
                           const char *want_prov) {
    char *text = NULL, *prov = NULL;
    dequote_flags_t flags;
    bool ok = lush_dequote_span(input, strlen(input), &text, &prov, &flags);
    ASSERT_TRUE(ok, "lush_dequote_span failed");
    ASSERT_STR_EQ(text, want_text, "text mismatch");
    ASSERT_EQ(memcmp(prov, want_prov, strlen(want_text)), 0, "prov mismatch");
    free(text);
    free(prov);
}

TEST(dequote_single_quoted) { assert_dequote("'a b'", "a b", "SSS"); }

TEST(dequote_double_quoted) { assert_dequote("\"a b\"", "a b", "DDD"); }

TEST(dequote_unquoted_escape_drops_backslash) {
    /// Unquoted \X drops the backslash; the char is tagged ESCAPED.
    assert_dequote("a\\ b", "a b", "UEU");
}

TEST(dequote_double_escape_keeps_backslash) {
    /// Inside "...", \X keeps the backslash (deferred), tagged DOUBLE.
    assert_dequote("\"a\\\"b\"", "a\\\"b", "DDDD");
}

TEST(dequote_bracket_word_double) {
    /// Phase 2c shape: unquoted + double + unquoted, one span.
    assert_dequote("m[\"a b\"]", "m[a b]", "UUDDDU");
}

TEST(dequote_bracket_word_single_dollar_literal) {
    /// Single-quoted $ stays literal (tagged S), not expanded here.
    assert_dequote("m['$x']", "m[$x]", "UUSSU");
}

TEST(dequote_empty_span) { assert_dequote("", "", ""); }

TEST(dequote_flags_single_vs_double) {
    char *text = NULL, *prov = NULL;
    dequote_flags_t f;
    lush_dequote_span("'x'", 3, &text, &prov, &f);
    ASSERT_TRUE(f.any_quoted && f.any_single && !f.expandable,
                "single-quote flags");
    free(text), free(prov);
    lush_dequote_span("\"x\"", 3, &text, &prov, &f);
    ASSERT_TRUE(f.any_quoted && !f.any_single && f.expandable,
                "double-quote flags");
    free(text), free(prov);
    /// A single-quoted segment fused with an adjacent unquoted run is
    /// expandable, matching the reader's has_expandable (`'x'yz`).
    lush_dequote_span("'x'yz", 5, &text, &prov, &f);
    ASSERT_TRUE(f.any_quoted && f.any_single && f.expandable,
                "single+unquoted fusion is expandable");
    free(text), free(prov);
}

int main(void) {
    printf("=== lush_dequote_span Tests ===\n\n");
    RUN_TEST(dequote_differential_vs_reader);
    RUN_TEST(dequote_single_quoted);
    RUN_TEST(dequote_double_quoted);
    RUN_TEST(dequote_unquoted_escape_drops_backslash);
    RUN_TEST(dequote_double_escape_keeps_backslash);
    RUN_TEST(dequote_bracket_word_double);
    RUN_TEST(dequote_bracket_word_single_dollar_literal);
    RUN_TEST(dequote_empty_span);
    RUN_TEST(dequote_flags_single_vs_double);
    return TEST_RESULT();
}
