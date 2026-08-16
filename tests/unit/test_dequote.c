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

/// The map's two structural invariants, asserted for every span in a corpus
/// rather than spot-checked:
///
///   1. text[j] == raw[map[j]] -- every output byte IS the raw byte it names.
///      Dequoting only ever COPIES or DROPS bytes; it never synthesizes one,
///      so this holds for the deferred `\X` inside "..." (both bytes copied)
///      and for the unquoted `\X` (the backslash is dropped and the map names
///      the surviving char), which are the two shapes most likely to be got
///      wrong.
///   2. the map is STRICTLY INCREASING -- each raw byte is consumed at most
///      once, so no two output bytes may claim the same source offset, and a
///      consumer can binary-search it.
///
/// Both invariants fail loudly if a future append site forgets to pass its
/// offset, which is the realistic way this decays.
static void assert_map_invariants(const char *raw) {
    char *text = NULL, *prov = NULL;
    size_t *map = NULL;
    size_t len = strlen(raw);
    bool ok = lush_dequote_span_mapped(raw, len, &text, &prov, NULL, &map);
    ASSERT_TRUE(ok, "dequote_mapped succeeds");
    ASSERT_TRUE(map != NULL, "a map is produced when requested");

    size_t n = strlen(text);
    for (size_t j = 0; j < n; j++) {
        ASSERT_TRUE(map[j] < len, "offset is inside the raw span");
        ASSERT_TRUE(raw[map[j]] == text[j],
                    "output byte is the raw byte it names");
        if (j > 0) {
            ASSERT_TRUE(map[j] > map[j - 1], "offsets strictly increase");
        }
    }
    free(text);
    free(prov);
    free(map);
}

TEST(dequote_map_invariants_over_corpus) {
    static const char *corpus[] = {
        "plain",
        "'a b'",
        "\"a b\"",
        "a\\ b",
        "\"a\\\"b\"",
        "m[\"a b\"]",
        "m['$x']",
        "pre'mid'post",
        "\"$(echo hi)\"",
        "\"`echo hi`\"",
        "\"a`echo x`b\"",
        "a\\\\b",
        "\"\"",
        "''",
        "$x",
        "\"${m[\"a b\"]}\"",
        "one'two'\"three\"four",
        "\\a\\b\\c",
        "\"a\nb\"",
    };
    for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
        assert_map_invariants(corpus[i]);
    }
}

TEST(dequote_map_names_the_kept_byte_of_an_unquoted_escape) {
    /// `a\ b` -> `a b`. The backslash at offset 1 is DROPPED, so the space in
    /// the output must name offset 2, the byte that survived -- not offset 1,
    /// which is what a naive "advance by one per output byte" would record.
    char *text = NULL, *prov = NULL;
    size_t *map = NULL;
    ASSERT_TRUE(lush_dequote_span_mapped("a\\ b", 4, &text, &prov, NULL, &map),
                "dequote succeeds");
    ASSERT_STR_EQ(text, "a b", "text");
    ASSERT_TRUE(map[0] == 0, "a comes from offset 0");
    ASSERT_TRUE(map[1] == 2, "the escaped space comes from offset 2");
    ASSERT_TRUE(map[2] == 3, "b comes from offset 3");
    free(text);
    free(prov);
    free(map);
}

TEST(dequote_map_skips_quote_delimiters) {
    /// `m["a b"]` -> `m[a b]`. The delimiters at 2 and 6 are gone, so the
    /// output offsets jump over them: this is the non-linearity that makes
    /// the map necessary rather than derivable by arithmetic.
    char *text = NULL, *prov = NULL;
    size_t *map = NULL;
    ASSERT_TRUE(
        lush_dequote_span_mapped("m[\"a b\"]", 8, &text, &prov, NULL, &map),
        "dequote succeeds");
    ASSERT_STR_EQ(text, "m[a b]", "text");
    size_t want[] = {0, 1, 3, 4, 5, 7};
    for (size_t j = 0; j < 6; j++) {
        ASSERT_TRUE(map[j] == want[j], "offset jumps the delimiters");
    }
    free(text);
    free(prov);
    free(map);
}

TEST(dequote_map_is_optional_and_the_wrapper_is_unchanged) {
    /// The 5-argument entry point is now a wrapper. It must produce exactly
    /// what it always did, and asking for no map must not allocate one.
    char *t1 = NULL, *p1 = NULL, *t2 = NULL, *p2 = NULL;
    dequote_flags_t f1, f2;
    const char *raw = "pre'mid'\"post\"";
    size_t len = strlen(raw);

    ASSERT_TRUE(lush_dequote_span(raw, len, &t1, &p1, &f1), "wrapper succeeds");
    ASSERT_TRUE(lush_dequote_span_mapped(raw, len, &t2, &p2, &f2, NULL),
                "mapped form with a NULL map succeeds");
    ASSERT_STR_EQ(t1, t2, "wrapper text matches");
    ASSERT_STR_EQ(p1, p2, "wrapper prov matches");
    ASSERT_TRUE(f1.any_quoted == f2.any_quoted, "flags agree");
    ASSERT_TRUE(f1.any_single == f2.any_single, "flags agree");
    ASSERT_TRUE(f1.expandable == f2.expandable, "flags agree");
    free(t1);
    free(p1);
    free(t2);
    free(p2);
}

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
    RUN_TEST(dequote_map_invariants_over_corpus);
    RUN_TEST(dequote_map_names_the_kept_byte_of_an_unquoted_escape);
    RUN_TEST(dequote_map_skips_quote_delimiters);
    RUN_TEST(dequote_map_is_optional_and_the_wrapper_is_unchanged);
    RUN_TEST(dequote_flags_single_vs_double);
    return TEST_RESULT();
}
