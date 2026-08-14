/**
 * @file test_word.c
 * @brief Foundation tests for the Word CST type + ownership API (Phase 2 / Step
 * 1).
 *
 * Proves the load-bearing properties of word.c before the parser/evaluator are
 * built on it:
 *   1. Construction of every structural kind via the ownership API.
 *   2. word_copy is a genuine DEEP copy -- the copy survives freeing the
 *      original, and mutating/freeing one does not touch the other.
 *   3. word_reconstruct is lossless -- it recovers the exact original source
 *      slice from the parts' spans.
 *   4. Leak-cleanliness (asserted under ASan/leaks in CI, exercised here).
 *
 * The completeness guarantee (a new WP_* kind cannot ride through
 * word_copy/word_part_free with an owned field dropped) is a COMPILE-TIME
 * property enforced by the `#pragma GCC diagnostic error "-Wswitch-enum"`
 * regions in word.c, and needs no runtime test.
 */
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"
#include "word.h"

/// Build a WP_LITERAL leaf with owned text.
static word_part_t *lit(const char *text, uint32_t off, uint32_t len) {
    word_part_t *p = word_part_new(WP_LITERAL, off, len);
    p->u.leaf.text = strdup(text);
    return p;
}

/// Assert two words have identical structure (kinds, text, spans) but DISTINCT
/// storage -- the deep-copy invariant.
static void assert_deep_equal(const word_t *a, const word_t *b) {
    ASSERT_NE((void *)a, (void *)b, "words must be distinct objects");
    ASSERT_EQ(a->n_parts, b->n_parts, "part count differs");
    ASSERT_EQ(a->src_off, b->src_off, "word src_off differs");
    ASSERT_EQ(a->src_len, b->src_len, "word src_len differs");
    for (uint32_t i = 0; i < a->n_parts; i++) {
        word_part_t *pa = WORD_PART(a, i), *pb = WORD_PART(b, i);
        ASSERT_NE((void *)pa, (void *)pb, "parts must be distinct objects");
        ASSERT_EQ(pa->kind, pb->kind, "part kind differs");
        ASSERT_EQ(pa->src_off, pb->src_off, "part src_off differs");
        ASSERT_EQ(pa->src_len, pb->src_len, "part src_len differs");
    }
}

TEST(word_rebase_shifts_spans_to_absolute) {
    /// A word parsed from a COPY of its source span carries spans relative to
    /// that copy. word_rebase makes them absolute so word_reconstruct reads the
    /// right bytes -- without it the offsets address the wrong text SILENTLY,
    /// because word_reconstruct only checks that a span FITS the buffer it is
    /// handed, not that it is the right span.
    const char *real = "echo aaaaaaaaaa hello";
    const uint32_t base = 16; /// where "hello" starts in `real`

    /// Shaped like a word parsed from the copy "hello": word and leaf both at
    /// offset 0, length 5.
    word_t *w = word_new(0, 5);
    ASSERT_NOT_NULL(w, "word");
    word_part_t *leaf = word_part_new(WP_LITERAL, 0, 5);
    ASSERT_NOT_NULL(leaf, "leaf");
    leaf->u.leaf.text = strdup("hello");
    ASSERT_TRUE(word_add_part(w, leaf), "add part");

    /// Against the real input the copy-relative span resolves to the wrong
    /// slice -- and RETURNS SUCCESSFULLY, which is what makes it dangerous.
    char *wrong = word_reconstruct(w, real, strlen(real));
    ASSERT_NOT_NULL(wrong, "reconstruct succeeds even though it is wrong");
    ASSERT_STR_EQ(wrong, "echo ", "wrong slice, silently");
    free(wrong);

    word_rebase(w, base);
    ASSERT_EQ(w->src_off, base, "word span is absolute after rebase");
    ASSERT_EQ(WORD_PART(w, 0)->src_off, base, "part span too");
    char *right = word_reconstruct(w, real, strlen(real));
    ASSERT_NOT_NULL(right, "reconstruct");
    ASSERT_STR_EQ(right, "hello", "the actual source text");
    free(right);

    /// A zero base is a no-op, and a NULL word is safe.
    word_rebase(w, 0);
    ASSERT_EQ(w->src_off, base, "zero base does not move it");
    word_rebase(NULL, 4);

    word_free(w);
}

TEST(word_construct_and_free_all_kinds) {
    /// One word holding a representative of every structural kind, so
    /// word_free walks every switch arm.
    word_t *w = word_new(0, 0);
    ASSERT_NOT_NULL(w, "word_new");

    ASSERT_TRUE(word_add_part(w, lit("abc", 0, 3)), "add literal");

    word_part_t *sq = word_part_new(WP_SINGLE, 0, 0);
    sq->u.leaf.text = strdup("raw");
    sq->u.leaf.literal_meta = true;
    ASSERT_TRUE(word_add_part(w, sq), "add single");

    word_part_t *ansic = word_part_new(WP_ANSIC, 0, 0);
    ansic->u.leaf.text = strdup("a\tb");
    ASSERT_TRUE(word_add_part(w, ansic), "add ansic");

    /// A DOUBLE group wrapping an interior body of two parts.
    word_part_t *dq = word_part_new(WP_DOUBLE, 0, 0);
    word_t *body = word_new(0, 0);
    word_add_part(body, lit("x", 0, 1));
    word_part_t *inner_param = word_part_new(WP_PARAM, 0, 0);
    inner_param->u.param.name = strdup("y");
    inner_param->u.param.op = -1;
    word_add_part(body, inner_param);
    word_set_body(dq, body);
    ASSERT_TRUE(word_add_part(w, dq), "add double");

    /// A BARE group.
    word_part_t *bare = word_part_new(WP_BARE, 0, 0);
    word_t *bbody = word_new(0, 0);
    word_add_part(bbody, lit("*.txt", 0, 5));
    word_set_body(bare, bbody);
    ASSERT_TRUE(word_add_part(w, bare), "add bare");

    /// A BRACE group with two alternative Words.
    word_part_t *brace = word_part_new(WP_BRACE, 0, 0);
    word_t *alt1 = word_new(0, 0);
    word_add_part(alt1, lit("a", 0, 1));
    word_t *alt2 = word_new(0, 0);
    word_add_part(alt2, lit("b", 0, 1));
    ASSERT_TRUE(word_add_item(brace, alt1), "brace item 1");
    ASSERT_TRUE(word_add_item(brace, alt2), "brace item 2");
    ASSERT_TRUE(word_add_part(w, brace), "add brace");

    /// An ARRAY_LIT with one element.
    word_part_t *arr = word_part_new(WP_ARRAY_LIT, 0, 0);
    word_t *elem = word_new(0, 0);
    word_add_part(elem, lit("one", 0, 3));
    ASSERT_TRUE(word_add_item(arr, elem), "array item");
    ASSERT_TRUE(word_add_part(w, arr), "add array_lit");

    /// A PARAM with a subscript, operand and operand2 (all child Words).
    word_part_t *param = word_part_new(WP_PARAM, 0, 0);
    param->u.param.name = strdup("arr");
    param->u.param.op = 16; /// pretend OP_REPLACE
    word_t *sub = word_new(0, 0);
    word_add_part(sub, lit("k", 0, 1));
    param->u.param.subscript = sub;
    word_t *opnd = word_new(0, 0);
    word_add_part(opnd, lit("p", 0, 1));
    param->u.param.operand = opnd;
    word_t *opnd2 = word_new(0, 0);
    word_add_part(opnd2, lit("r", 0, 1));
    param->u.param.operand2 = opnd2;
    ASSERT_TRUE(word_add_part(w, param), "add param");

    /// Deferred-body leaves.
    word_part_t *cmdsub = word_part_new(WP_CMDSUB, 0, 0);
    cmdsub->u.defer.body = strdup("echo hi");
    ASSERT_TRUE(word_add_part(w, cmdsub), "add cmdsub");

    word_part_t *bt = word_part_new(WP_BACKTICK, 0, 0);
    bt->u.defer.body = strdup("date");
    ASSERT_TRUE(word_add_part(w, bt), "add backtick");

    word_part_t *arith = word_part_new(WP_ARITH, 0, 0);
    arith->u.defer.body = strdup("1+2");
    ASSERT_TRUE(word_add_part(w, arith), "add arith");

    /// Tilde (owner user string) and tilde with NULL user ($HOME).
    word_part_t *tilde = word_part_new(WP_TILDE, 0, 0);
    tilde->u.tilde.user = strdup("mberry");
    ASSERT_TRUE(word_add_part(w, tilde), "add tilde");
    word_part_t *tilde_home = word_part_new(WP_TILDE, 0, 0);
    tilde_home->u.tilde.user = NULL;
    ASSERT_TRUE(word_add_part(w, tilde_home), "add tilde home");

    /// Process substitutions.
    word_part_t *psin = word_part_new(WP_PROCSUB_IN, 0, 0);
    psin->u.procsub.body = strdup("sort");
    ASSERT_TRUE(word_add_part(w, psin), "add procsub in");
    word_part_t *psout = word_part_new(WP_PROCSUB_OUT, 0, 0);
    psout->u.procsub.body = strdup("tee f");
    ASSERT_TRUE(word_add_part(w, psout), "add procsub out");

    /// Kind sigil.
    word_part_t *ks = word_part_new(WP_KINDSIGIL, 0, 0);
    ks->u.kindsigil.name = strdup("m");
    ks->u.kindsigil.sigil = '%';
    ASSERT_TRUE(word_add_part(w, ks), "add kindsigil");

    ASSERT_EQ(w->n_parts, 16u, "expected 16 parts");
    word_free(w); /// must free every arm without leak/UAF
}

TEST(word_copy_is_deep_and_independent) {
    /// Build a nested word, copy it, free the ORIGINAL, then assert the copy is
    /// still fully valid (proves no shared storage).
    word_t *orig = word_new(5, 10);
    word_add_part(orig, lit("pre", 5, 3));
    word_part_t *dq = word_part_new(WP_DOUBLE, 8, 5);
    word_t *body = word_new(9, 3);
    word_part_t *param = word_part_new(WP_PARAM, 9, 3);
    param->u.param.name = strdup("v");
    param->u.param.op = -1;
    word_add_part(body, param);
    word_set_body(dq, body);
    word_add_part(orig, dq);

    word_t *copy = word_copy(orig);
    ASSERT_NOT_NULL(copy, "word_copy");
    assert_deep_equal(orig, copy);

    /// Distinct storage at the leaf text level.
    ASSERT_NE((void *)WORD_PART(orig, 0)->u.leaf.text,
              (void *)WORD_PART(copy, 0)->u.leaf.text,
              "copied leaf text must be distinct storage");

    /// Free the original; the copy must remain intact.
    word_free(orig);
    ASSERT_STR_EQ(WORD_PART(copy, 0)->u.leaf.text, "pre",
                  "copy literal survives original free");
    word_part_t *cdq = WORD_PART(copy, 1);
    ASSERT_EQ(cdq->kind, WP_DOUBLE, "copy double kind survives");
    word_part_t *cparam = WORD_PART(PART_BODY(cdq), 0);
    ASSERT_STR_EQ(cparam->u.param.name, "v",
                  "copy nested param name survives original free");

    word_free(copy);
}

TEST(word_copy_null_is_null) {
    ASSERT_NULL(word_copy(NULL), "word_copy(NULL) is NULL");
    word_free(NULL); /// must not crash
}

TEST(word_reconstruct_is_lossless) {
    /// Spans reference this exact source; reconstruct recovers the slice
    /// verbatim, quotes and all -- the lossless-CST property.
    const char *src = "echo pre\"$v\"post";
    /// The word is the second token: pre"$v"post at offset 5, length 11.
    word_t *w = word_new(5, 11);
    /// (Parts' internal spans are not needed for whole-word reconstruct, but a
    /// realistic tree is attached to exercise free alongside.)
    word_add_part(w, lit("pre", 5, 3));
    word_part_t *dq = word_part_new(WP_DOUBLE, 8, 4);
    word_t *body = word_new(9, 2);
    word_part_t *param = word_part_new(WP_PARAM, 9, 2);
    param->u.param.name = strdup("v");
    param->u.param.op = -1;
    word_add_part(body, param);
    word_set_body(dq, body);
    word_add_part(w, dq);
    word_add_part(w, lit("post", 12, 4));

    char *recon = word_reconstruct(w, src, strlen(src));
    ASSERT_NOT_NULL(recon, "word_reconstruct");
    ASSERT_STR_EQ(recon, "pre\"$v\"post", "reconstruct recovers exact source");
    free(recon);

    /// Out-of-contract span (past the buffer) must return NULL, not read OOB.
    word_t *bad = word_new(5, 1000);
    ASSERT_NULL(word_reconstruct(bad, src, strlen(src)),
                "oversized span returns NULL");
    word_free(bad);
    word_free(w);
}

TEST(word_reconstruct_empty_word) {
    const char *src = "x";
    word_t *w = word_new(1, 0); /// empty span (e.g. "")
    char *recon = word_reconstruct(w, src, strlen(src));
    ASSERT_NOT_NULL(recon, "reconstruct empty");
    ASSERT_STR_EQ(recon, "", "empty span reconstructs to empty string");
    free(recon);
    word_free(w);
}

TEST(word_add_part_grows_capacity) {
    /// Push more than the initial capacity (4) to exercise the realloc growth.
    word_t *w = word_new(0, 0);
    for (int i = 0; i < 100; i++) {
        ASSERT_TRUE(word_add_part(w, lit("a", 0, 1)), "grow add");
    }
    ASSERT_EQ(w->n_parts, 100u, "all parts retained");
    ASSERT_GE(w->cap_parts, 100u, "capacity grew to hold them");
    word_free(w);
}

int main(void) {
    printf("=== Word CST Foundation Tests ===\n\n");
    RUN_TEST(word_rebase_shifts_spans_to_absolute);
    RUN_TEST(word_construct_and_free_all_kinds);
    RUN_TEST(word_copy_is_deep_and_independent);
    RUN_TEST(word_copy_null_is_null);
    RUN_TEST(word_reconstruct_is_lossless);
    RUN_TEST(word_reconstruct_empty_word);
    RUN_TEST(word_add_part_grows_capacity);
    return TEST_RESULT();
}
