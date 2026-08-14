/**
 * @file test_word_parse.c
 * @brief Tests for parse_word (Step 1b-1): tokenizer stream -> word_t.
 *
 * Runs the real tokenizer over an input line and asserts parse_word builds the
 * expected Word CST for the covered single-token cases + adjacency fusion, that
 * the whole-word source span round-trips losslessly via word_reconstruct, and
 * that not-yet-covered constructs report fully_handled == false (never a silent
 * mis-parse). The bench runs the same code the live parser eventually will:
 * tokenizer_new(line) -> parse_word(tok, ...).
 */
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"
#include "tokenizer.h"
#include "word.h"
#include "word_parse.h"

/// Parse the first word of `line`. word_t owns copies of all text (spans are
/// integer offsets), so the tokenizer can be freed immediately;
/// word_reconstruct takes the original line separately.
static word_t *parse_line(const char *line, bool *fully) {
    tokenizer_t *tok = tokenizer_new(line);
    word_t *w = parse_word(tok, WORD_CTX_ARG, fully);
    tokenizer_free(tok);
    return w;
}

/// Assert word_reconstruct recovers `line` exactly (whole-word span coverage).
static void assert_reconstruct(word_t *w, const char *line) {
    char *r = word_reconstruct(w, line, strlen(line));
    ASSERT_NOT_NULL(r, "reconstruct");
    ASSERT_STR_EQ(r, line, "reconstruct recovers exact source");
    free(r);
}

TEST(parse_bare_literal) {
    bool fully = false;
    word_t *w = parse_line("abc", &fully);
    ASSERT_NOT_NULL(w, "word");
    ASSERT_TRUE(fully, "bare literal fully handled");
    ASSERT_EQ(w->n_parts, 1u, "one top-level part");
    word_part_t *bare = WORD_PART(w, 0);
    ASSERT_EQ(bare->kind, WP_BARE, "top part is BARE");
    ASSERT_EQ(PART_BODY(bare)->n_parts, 1u, "one leaf");
    word_part_t *leaf = WORD_PART(PART_BODY(bare), 0);
    ASSERT_EQ(leaf->kind, WP_LITERAL, "leaf is LITERAL");
    ASSERT_STR_EQ(leaf->u.leaf.text, "abc", "literal text");
    ASSERT_FALSE(leaf->u.leaf.literal_meta,
                 "bare literal has active metachars");
    assert_reconstruct(w, "abc");
    word_free(w);
}

TEST(parse_bare_glob_stays_active) {
    bool fully = false;
    word_t *w = parse_line("a*b", &fully);
    ASSERT_TRUE(fully, "glob word fully handled");
    word_part_t *leaf = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_STR_EQ(leaf->u.leaf.text, "a*b", "glob text preserved");
    ASSERT_FALSE(leaf->u.leaf.literal_meta, "unquoted glob is active");
    assert_reconstruct(w, "a*b");
    word_free(w);
}

TEST(parse_single_quoted) {
    bool fully = false;
    word_t *w = parse_line("'lit'", &fully);
    ASSERT_TRUE(fully, "single-quoted fully handled");
    ASSERT_EQ(w->n_parts, 1u, "one part");
    word_part_t *sq = WORD_PART(w, 0);
    ASSERT_EQ(sq->kind, WP_SINGLE, "SINGLE leaf");
    ASSERT_STR_EQ(sq->u.leaf.text, "lit", "interior only");
    ASSERT_TRUE(sq->u.leaf.literal_meta, "single-quoted is literal");
    assert_reconstruct(w, "'lit'");
    word_free(w);
}

TEST(parse_double_quoted_with_var) {
    bool fully = false;
    word_t *w = parse_line("\"d $v e\"", &fully);
    ASSERT_TRUE(fully, "double-quoted fully handled");
    ASSERT_EQ(w->n_parts, 1u, "one part");
    word_part_t *dq = WORD_PART(w, 0);
    ASSERT_EQ(dq->kind, WP_DOUBLE, "DOUBLE group");
    word_t *body = PART_BODY(dq);
    ASSERT_EQ(body->n_parts, 3u, "lit, param, lit");
    ASSERT_EQ(WORD_PART(body, 0)->kind, WP_LITERAL, "leading literal");
    ASSERT_STR_EQ(WORD_PART(body, 0)->u.leaf.text, "d ",
                  "leading literal text");
    ASSERT_TRUE(WORD_PART(body, 0)->u.leaf.literal_meta, "DQ literal is inert");
    ASSERT_EQ(WORD_PART(body, 1)->kind, WP_PARAM, "middle param");
    ASSERT_STR_EQ(WORD_PART(body, 1)->u.param.name, "v", "param name");
    ASSERT_EQ(WORD_PART(body, 1)->u.param.op, -1, "simple ref op");
    ASSERT_EQ(WORD_PART(body, 2)->kind, WP_LITERAL, "trailing literal");
    ASSERT_STR_EQ(WORD_PART(body, 2)->u.leaf.text, " e",
                  "trailing literal text");
    assert_reconstruct(w, "\"d $v e\"");
    word_free(w);
}

TEST(parse_simple_var) {
    bool fully = false;
    word_t *w = parse_line("$x", &fully);
    ASSERT_TRUE(fully, "simple var fully handled");
    word_part_t *bare = WORD_PART(w, 0);
    ASSERT_EQ(bare->kind, WP_BARE, "bare wrapper");
    word_part_t *p = WORD_PART(PART_BODY(bare), 0);
    ASSERT_EQ(p->kind, WP_PARAM, "PARAM");
    ASSERT_STR_EQ(p->u.param.name, "x", "name");
    assert_reconstruct(w, "$x");
    word_free(w);
}

TEST(parse_braced_var) {
    bool fully = false;
    word_t *w = parse_line("${name}", &fully);
    ASSERT_TRUE(fully, "braced var fully handled");
    word_part_t *p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_EQ(p->kind, WP_PARAM, "PARAM");
    ASSERT_STR_EQ(p->u.param.name, "name", "name");
    assert_reconstruct(w, "${name}");
    word_free(w);
}

TEST(parse_double_two_vars_and_trailing_dollar) {
    /// Two adjacent params in a DQ string, and a trailing literal `$`.
    bool fully = false;
    word_t *w = parse_line("\"$a$b\"", &fully);
    ASSERT_TRUE(fully, "two-var DQ fully handled");
    word_t *body = PART_BODY(WORD_PART(w, 0));
    ASSERT_EQ(body->n_parts, 2u, "two params");
    ASSERT_STR_EQ(WORD_PART(body, 0)->u.param.name, "a", "first param");
    ASSERT_STR_EQ(WORD_PART(body, 1)->u.param.name, "b", "second param");
    word_free(w);

    fully = false;
    w = parse_line("\"ab$\"", &fully);
    ASSERT_TRUE(fully, "trailing literal $ fully handled");
    body = PART_BODY(WORD_PART(w, 0));
    ASSERT_EQ(body->n_parts, 1u, "one literal");
    ASSERT_STR_EQ(WORD_PART(body, 0)->u.leaf.text, "ab$",
                  "trailing $ is literal");
    assert_reconstruct(w, "\"ab$\"");
    word_free(w);
}

TEST(parse_required_param_message_is_raw) {
    /// The `:?` / `?` message operand is captured RAW -- one literal leaf
    /// holding the source bytes -- rather than tokenized into a word. That is
    /// what makes a message with spaces (or quotes, or glob metacharacters)
    /// representable: the legacy expander keeps those bytes verbatim, so a
    /// tokenized operand would drop or dequote them. A `$` in the message
    /// defers instead, because legacy expands it eagerly (issue #692).
    bool fully = false;
    word_t *w = parse_line("${v:?must be set}", &fully);
    ASSERT_TRUE(fully, "raw message fully handled");
    word_part_t *p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_EQ(p->kind, WP_PARAM, "PARAM");
    ASSERT_STR_EQ(p->u.param.name, "v", "name");
    ASSERT_EQ(p->u.param.op, 18, "op 18 = :?");
    ASSERT_NOT_NULL(p->u.param.operand, "message operand present");
    ASSERT_EQ(p->u.param.operand->n_parts, 1u, "one literal leaf");
    ASSERT_EQ(WORD_PART(p->u.param.operand, 0)->kind, WP_LITERAL, "literal");
    ASSERT_STR_EQ(WORD_PART(p->u.param.operand, 0)->u.leaf.text, "must be set",
                  "message kept verbatim, spaces and all");
    /// literal_meta records ORIGIN, and these are raw unquoted source bytes --
    /// the value parse_word would give them. Pinned because it is otherwise
    /// invisible: nothing globs an operand word, so either value "works".
    ASSERT_FALSE(WORD_PART(p->u.param.operand, 0)->u.leaf.literal_meta,
                 "raw message bytes are unquoted-origin");
    assert_reconstruct(w, "${v:?must be set}");
    word_free(w);

    fully = false;
    w = parse_line("${v?m}", &fully);
    ASSERT_TRUE(fully, "`?` form fully handled");
    p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_EQ(p->u.param.op, 19, "op 19 = ?");
    word_free(w);

    fully = false;
    w = parse_line("${v:?}", &fully);
    ASSERT_TRUE(fully, "empty message fully handled");
    p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_EQ(p->u.param.op, 18, "op 18");
    ASSERT_NULL(p->u.param.operand, "empty message -> no operand word");
    word_free(w);

    /// A `$` message is covered since #692: legacy no longer builds it on the
    /// branch that discards it, so there is no eager side effect left to
    /// preserve. The message is captured raw, `$` and all -- it is consulted
    /// only when the diagnostic fires, and a firing expansion defers.
    fully = false;
    w = parse_line("${v:?$x}", &fully);
    ASSERT_TRUE(fully, "a `$` message is captured raw and covered");
    p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_STR_EQ(WORD_PART(p->u.param.operand, 0)->u.leaf.text, "$x",
                  "captured verbatim, not expanded");
    word_free(w);

    /// A quoting byte defers: lush_find_matching_brace treats a quoted span as
    /// atomic, while the legacy double-quoted-word expander counts braces
    /// naively, so `"${v:?a'}'b}"` ends in two different places.
    fully = false;
    w = parse_line("${v:?a'q'b}", &fully);
    ASSERT_FALSE(fully, "a quote in the message defers");
    word_free(w);
}

TEST(parse_special_param) {
    bool fully = false;
    word_t *w = parse_line("$?", &fully);
    ASSERT_TRUE(fully, "special param fully handled");
    word_part_t *p = WORD_PART(PART_BODY(WORD_PART(w, 0)), 0);
    ASSERT_EQ(p->kind, WP_PARAM, "PARAM");
    ASSERT_STR_EQ(p->u.param.name, "?", "special name");
    assert_reconstruct(w, "$?");
    word_free(w);
}

TEST(parse_adjacency_fusion) {
    /// `pre$x` fuses two adjacent tokens (WORD + VARIABLE) into one word with
    /// two top-level parts -- the boundary the current parser destroys.
    bool fully = false;
    word_t *w = parse_line("pre$x", &fully);
    ASSERT_TRUE(fully, "fusion fully handled");
    ASSERT_EQ(w->n_parts, 2u, "two fused parts");
    ASSERT_EQ(WORD_PART(w, 0)->kind, WP_BARE, "literal part");
    ASSERT_STR_EQ(WORD_PART(PART_BODY(WORD_PART(w, 0)), 0)->u.leaf.text, "pre",
                  "literal");
    ASSERT_EQ(WORD_PART(w, 1)->kind, WP_BARE, "var part");
    ASSERT_STR_EQ(WORD_PART(PART_BODY(WORD_PART(w, 1)), 0)->u.param.name, "x",
                  "var name");
    assert_reconstruct(w, "pre$x");
    word_free(w);
}

TEST(parse_ansic_string) {
    /// $'...' decodes escapes at parse time into a WP_ANSIC leaf.
    bool fully = false;
    word_t *w = parse_line("$'a\\tb'", &fully);
    ASSERT_TRUE(fully, "ANSI-C fully handled");
    ASSERT_EQ(w->n_parts, 1u, "one part");
    word_part_t *ac = WORD_PART(w, 0);
    ASSERT_EQ(ac->kind, WP_ANSIC, "ANSIC kind");
    ASSERT_STR_EQ(ac->u.leaf.text, "a\tb", "decoded tab (0x09)");
    ASSERT_TRUE(ac->u.leaf.literal_meta, "ANSI-C is single-quote-like literal");
    assert_reconstruct(w, "$'a\\tb'");
    word_free(w);
}

TEST(parse_ansic_empty_hex_glob) {
    bool fully = false;
    word_t *w = parse_line("$''", &fully); /// empty
    ASSERT_TRUE(fully, "empty ANSI-C handled");
    ASSERT_EQ(WORD_PART(w, 0)->kind, WP_ANSIC, "empty is ANSIC");
    ASSERT_STR_EQ(WORD_PART(w, 0)->u.leaf.text, "", "empty text");
    word_free(w);

    w = parse_line("$'x\\x41y'", &fully); /// hex escape
    ASSERT_STR_EQ(WORD_PART(w, 0)->u.leaf.text, "xAy", "hex \\x41 -> A");
    word_free(w);

    w = parse_line("$'a*b'", &fully); /// glob metachar stays literal
    ASSERT_STR_EQ(WORD_PART(w, 0)->u.leaf.text, "a*b", "metachar literal");
    ASSERT_TRUE(WORD_PART(w, 0)->u.leaf.literal_meta, "not an active glob");
    word_free(w);
}

TEST(parse_ansic_midword_and_unterminated_deferred) {
    /// Mid-word $'...' (one bare WORD, decoded in place by the live executor)
    /// and an unterminated $'... both defer -- never a mis-decode.
    bool fully = true;
    word_t *w = parse_line("pre$'\\t'", &fully);
    ASSERT_FALSE(fully, "mid-word ANSI-C deferred");
    word_free(w);
}

TEST(parse_stops_at_whitespace) {
    /// parse_word consumes only the first word; `foo bar` yields `foo`.
    bool fully = false;
    word_t *w = parse_line("foo bar", &fully);
    ASSERT_TRUE(fully, "first word handled");
    ASSERT_STR_EQ(WORD_PART(PART_BODY(WORD_PART(w, 0)), 0)->u.leaf.text, "foo",
                  "first word only");
    assert_reconstruct(w, "foo");
    word_free(w);
}

TEST(deferred_constructs_report_not_handled) {
    /// Not-yet-covered constructs must set fully == false, never mis-parse.
    /// The whole-word span still round-trips (spans are complete regardless).
    const char *deferred[] = {
        "$(echo hi)", /// command substitution
        "${v@Q}",     /// PE operator: the transform family still defers (the
                      /// alternation, strip, case, substring, substitution,
                      /// assign and required-parameter families are covered)
        "${v:?'q'}",  /// covered operator, but a QUOTING byte in the message
                      /// defers: the CST and the legacy double-quoted-word
                      /// expander disagree on where such a `${...}` ends
        "${#v}",      /// length operator
        "pre$'\\t'",  /// mid-word ANSI-C (standalone $'...' is now covered)
        "pre$x\"$y\"post", /// tokenizer pre-fused mixed-quote token
        "\"a\"b",          /// mixed-quote adjacency
        "$((1+2))",        /// arithmetic
        "\"\\$x\"",        /// escaped $ in DQ: MUST NOT be read as expansion
        "\"a\\$x b\"",     /// escaped $ mid-string
        "\"\\$100\"",      /// dollar-amount idiom
        "\"\\\\$x\"",      /// even backslashes (real expansion) -- deferred too
        "\"a\\\"b\"",      /// escaped quote in DQ
        "$(cmd)more",      /// cmdsub fused with a suffix
    };
    for (size_t i = 0; i < sizeof(deferred) / sizeof(*deferred); i++) {
        bool fully = true;
        word_t *w = parse_line(deferred[i], &fully);
        ASSERT_NOT_NULL(w, "deferred word still parses");
        ASSERT_FALSE(fully, "deferred construct reports not-fully-handled");
        assert_reconstruct(w, deferred[i]);
        word_free(w);
    }
}

TEST(non_word_returns_null) {
    bool fully = false;
    word_t *w = parse_line("| pipe", &fully);
    ASSERT_NULL(w, "leading non-word token yields no word");
    word_free(w);
}

int main(void) {
    printf("=== parse_word (Step 1b-1) Tests ===\n\n");
    RUN_TEST(parse_bare_literal);
    RUN_TEST(parse_bare_glob_stays_active);
    RUN_TEST(parse_single_quoted);
    RUN_TEST(parse_double_quoted_with_var);
    RUN_TEST(parse_double_two_vars_and_trailing_dollar);
    RUN_TEST(parse_simple_var);
    RUN_TEST(parse_braced_var);
    RUN_TEST(parse_required_param_message_is_raw);
    RUN_TEST(parse_special_param);
    RUN_TEST(parse_adjacency_fusion);
    RUN_TEST(parse_ansic_string);
    RUN_TEST(parse_ansic_empty_hex_glob);
    RUN_TEST(parse_ansic_midword_and_unterminated_deferred);
    RUN_TEST(parse_stops_at_whitespace);
    RUN_TEST(deferred_constructs_report_not_handled);
    RUN_TEST(non_word_returns_null);
    return TEST_RESULT();
}
