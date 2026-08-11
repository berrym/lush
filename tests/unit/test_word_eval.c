/**
 * @file test_word_eval.c
 * @brief Tests for word_eval (Step 1c-1): word_t -> argv fields, lush-mode.
 *
 * Drives the real path tokenizer -> parse_word -> word_eval against a
 * deterministic variable map (no process environment), asserting the exact
 * field vector for the covered lush-mode subset, the null-word rule (unquoted
 * empty drops, quoted empty survives), concatenation fusion, and that
 * not-yet-covered constructs report ok == false rather than a wrong vector.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "param_op.h"
#include "test_framework.h"
#include "tokenizer.h"
#include "word.h"
#include "word_eval.h"
#include "word_parse.h"

/// Value source: `vars` is a NULL-terminated name,value,name,value,... array.
/// Returns an OWNED copy (strdup) per the word_eval get contract.
static char *map_get(void *ctx, const char *name) {
    const char **kv = ctx;
    for (int i = 0; kv[i]; i += 2) {
        if (strcmp(kv[i], name) == 0) {
            return strdup(kv[i + 1]);
        }
    }
    return NULL;
}

/// Bench apply_op: the PE operators are evaluated by the same shared core the
/// executor uses (lush_param_op_apply in src/param_op.c), so the bench asserts
/// the REAL operator semantics by construction -- glob patterns, `#`/`%`
/// anchors and grapheme-aware substrings included. A hand-written bench copy
/// used to model only literal patterns, which made the differential harness
/// report false BUG verdicts for every form it could not model (issue #681).
static char *map_apply_op(void *ctx, const char *name, const char *value,
                          const char *deflt, int op) {
    (void)ctx;
    (void)name;
    return lush_param_op_apply(op, value, deflt, NULL);
}

/// Parse + evaluate the first word of `line` in lush mode (no split) against
/// `vars`. Returns the field vector (or NULL); sets *n, *ok (word_eval covered)
/// and *fully (parse_word covered).
static char **peval(const char *line, const char **vars, int *n, bool *ok,
                    bool *fully) {
    tokenizer_t *tok = tokenizer_new(line);
    word_t *w = parse_word(tok, WORD_CTX_ARG, fully);
    char **fields = NULL;
    *n = 0;
    *ok = false;
    if (w && *fully) {
        word_eval_env_t env = {.get = map_get,
                               .apply_op = map_apply_op,
                               .ctx = vars,
                               .ifs = NULL,
                               .word_split_default = false,
                               .ansi_c_quoting = true};
        fields = word_eval(w, &env, n, ok);
    }
    word_free(w);
    tokenizer_free(tok);
    return fields;
}

/// peval with a caller-supplied env, for the flags peval does not set
/// (nounset, word_split_default, ...). Same contract otherwise.
static char **peval_env(const char *line, word_eval_env_t *env, int *n,
                        bool *ok, bool *fully) {
    tokenizer_t *tok = tokenizer_new(line);
    word_t *w = parse_word(tok, WORD_CTX_ARG, fully);
    char **fields = NULL;
    *n = 0;
    *ok = false;
    if (w && *fully) {
        fields = word_eval(w, env, n, ok);
    }
    word_free(w);
    tokenizer_free(tok);
    return fields;
}

static void free_fields(char **f, int n) {
    if (!f) {
        return;
    }
    for (int i = 0; i < n; i++) {
        free(f[i]);
    }
    free(f);
}

/// Assert the evaluated field vector equals `expected` (a NULL-terminated
/// list).
static void assert_fields(const char *line, const char **vars,
                          const char **expected) {
    int n = 0, en = 0;
    bool ok = false, fully = false;
    char **got = peval(line, vars, &n, &ok, &fully);
    ASSERT_TRUE(fully, "parse_word covered");
    ASSERT_TRUE(ok, "word_eval covered");
    while (expected[en]) {
        en++;
    }
    ASSERT_EQ(n, en, "field count");
    for (int i = 0; i < n && i < en; i++) {
        ASSERT_STR_EQ(got[i], expected[i], "field value");
    }
    free_fields(got, n);
}

TEST(eval_bare_literal) {
    const char *vars[] = {NULL};
    const char *exp[] = {"abc", NULL};
    assert_fields("abc", vars, exp);
}

TEST(eval_single_quoted) {
    const char *vars[] = {NULL};
    const char *exp[] = {"a b", NULL};
    assert_fields("'a b'", vars, exp);
}

TEST(eval_double_quoted) {
    const char *vars[] = {NULL};
    const char *exp[] = {"a b", NULL};
    assert_fields("\"a b\"", vars, exp);
}

TEST(eval_var_no_split_lush) {
    /// Unquoted $X in lush mode does NOT split -> one field.
    const char *vars[] = {"X", "a b c", NULL};
    const char *exp[] = {"a b c", NULL};
    assert_fields("$X", vars, exp);
}

TEST(eval_quoted_var) {
    const char *vars[] = {"X", "a b c", NULL};
    const char *exp[] = {"a b c", NULL};
    assert_fields("\"$X\"", vars, exp);
}

TEST(eval_null_word_unquoted_empty_drops) {
    /// $E with E="" contributes zero fields (null-word removal).
    const char *vars[] = {"E", "", NULL};
    int n = -1;
    bool ok = false, fully = false;
    char **got = peval("$E", vars, &n, &ok, &fully);
    ASSERT_TRUE(fully && ok, "covered");
    ASSERT_EQ(n, 0, "unquoted empty expansion -> zero fields");
    free_fields(got, n);
}

TEST(eval_null_word_unset_drops) {
    const char *vars[] = {NULL};
    int n = -1;
    bool ok = false, fully = false;
    char **got = peval("$UNSET", vars, &n, &ok, &fully);
    ASSERT_TRUE(fully && ok, "covered");
    ASSERT_EQ(n, 0, "unset unquoted -> zero fields");
    free_fields(got, n);
}

TEST(eval_quoted_empty_survives) {
    /// "$E" and '' keep one empty field.
    const char *vars[] = {"E", "", NULL};
    const char *exp[] = {"", NULL};
    assert_fields("\"$E\"", vars, exp);
    const char *nov[] = {NULL};
    assert_fields("''", nov, exp);
}

TEST(eval_concatenation_fusion) {
    /// pre$X -> one fused field; $X"$Y" -> one field, survives in lush mode.
    const char *vars[] = {"X", "a b c", "Y", "cd", NULL};
    const char *e1[] = {"prea b c", NULL};
    assert_fields("pre$X", vars, e1);
    const char *e2[] = {"a b ccd", NULL};
    assert_fields("$X\"$Y\"", vars, e2);
}

TEST(eval_glob_is_not_yet_covered) {
    /// An unquoted active glob metachar defers (ok=false), never mis-evals.
    const char *vars[] = {NULL};
    int n = -1;
    bool ok = true, fully = false;
    char **got = peval("a*b", vars, &n, &ok, &fully);
    ASSERT_TRUE(fully, "parse covers a*b");
    ASSERT_FALSE(ok, "word_eval defers glob");
    ASSERT_NULL(got, "no fields on not-covered");
    free_fields(got, n);
}

TEST(eval_quoted_glob_is_literal) {
    /// A quoted glob metachar is literal -> covered, one field.
    const char *vars[] = {NULL};
    const char *exp[] = {"a*b", NULL};
    assert_fields("\"a*b\"", vars, exp);
    const char *exp2[] = {"a*b", NULL};
    assert_fields("'a*b'", vars, exp2);
}

TEST(eval_special_param_scalar_covered) {
    /// The scalar specials $?/$$/$#/$!/$- are covered, resolved through the get
    /// callback like any scalar (here the map). The vector specials $@/$* and
    /// the identifier-shaped last-arg $_ remain deferred.
    const char *vars[] = {"?", "0",  "$", "4242",  "#", "3",
                          "!", "77", "-", "himBH", NULL};
    const char *ex_q[] = {"0", NULL};
    assert_fields("$?", vars, ex_q);
    const char *ex_d[] = {"4242", NULL};
    assert_fields("$$", vars, ex_d);
    const char *ex_h[] = {"3", NULL};
    assert_fields("$#", vars, ex_h);
    const char *ex_b[] = {"77", NULL};
    assert_fields("$!", vars, ex_b);
    const char *ex_o[] = {"himBH", NULL};
    assert_fields("$-", vars, ex_o);

    int n = -1;
    bool ok = true, fully = false;
    char **got = peval("$@", vars, &n, &ok, &fully);
    ASSERT_FALSE(ok, "$@ (vector special) deferred");
    free_fields(got, n);
    ok = true;
    got = peval("$*", vars, &n, &ok, &fully);
    ASSERT_FALSE(ok, "$* (vector special) deferred");
    free_fields(got, n);

    ok = true;
    got = peval("$_", vars, &n, &ok, &fully);
    ASSERT_FALSE(ok, "$_ (last-arg special) deferred, not read as a var");
    free_fields(got, n);
}

TEST(eval_positional_param_covered) {
    /// Single-digit positionals $0..$9 are covered, resolved through the get
    /// callback like any scalar (here the map), including in concatenation.
    const char *vars[] = {"0", "prog", "1", "A", "2", "B", NULL};
    const char *ex0[] = {"prog", NULL};
    assert_fields("$0", vars, ex0);
    const char *ex1[] = {"A", NULL};
    assert_fields("$1", vars, ex1);
    const char *exf[] = {"xAyB", NULL};
    assert_fields("x$1y$2", vars, exf);
}

TEST(eval_pe_alternation_operators) {
    /// The four alternation operators via the map + apply_op stub. :-/:+ treat
    /// empty as unset; -/+ distinguish unset (map miss -> NULL) from empty.
    const char *vars[] = {"set", "x", "empty", "", NULL};
    const char *ex[] = {"x", NULL};
    assert_fields("${set:-D}", vars, ex); /// set, non-empty -> value
    const char *eD[] = {"D", NULL};
    assert_fields("${empty:-D}", vars, eD); /// empty -> default
    assert_fields("${un:-D}", vars, eD);    /// unset -> default
    assert_fields("${un-D}", vars, eD);     /// unset -> default (- form)
    const char *eA[] = {"A", NULL};
    assert_fields("${set:+A}", vars, eA); /// set, non-empty -> alternative

    /// Empty results null-word-drop (unquoted).
    int n = -1;
    bool ok = false, fully = false;
    char **got = peval("${empty-D}", vars, &n, &ok, &fully);
    ASSERT_TRUE(ok && n == 0, "${empty-D}: empty stays empty -> drops");
    free_fields(got, n);
    got = peval("${empty:+A}", vars, &n, &ok, &fully);
    ASSERT_TRUE(ok && n == 0, "${empty:+A}: empty -> no alternative -> drops");
    free_fields(got, n);
}

TEST(eval_pe_pattern_strip_operators) {
    /// Pattern-strip via the map + the literal-pattern apply_op stub (glob
    /// patterns are validated live where the real matcher runs).
    const char *vars[] = {"p", "abcdef", NULL};
    const char *e1[] = {"def", NULL};
    assert_fields("${p#abc}", vars, e1);  /// # shortest prefix
    assert_fields("${p##abc}", vars, e1); /// ## longest prefix
    const char *e2[] = {"abc", NULL};
    assert_fields("${p%def}", vars, e2);  /// % shortest suffix
    assert_fields("${p%%def}", vars, e2); /// %% longest suffix
    const char *e3[] = {"abcdef", NULL};
    assert_fields("${p#xyz}", vars, e3); /// no match -> unchanged
    assert_fields("${p#}", vars, e3);    /// empty pattern -> unchanged
}

TEST(eval_pe_case_conversion_operators) {
    /// No-pattern case conversion via the map + the apply_op stub. (Pattern-
    /// restricted forms defer and are exercised live where the real matcher
    /// runs.)
    const char *vars[] = {"s", "heLLo", NULL};
    const char *up[] = {"HELLO", NULL};
    assert_fields("${s^^}", vars, up); /// ^^ upper all
    const char *lo[] = {"hello", NULL};
    assert_fields("${s,,}", vars, lo); /// ,, lower all
    const char *uf[] = {"HeLLo", NULL};
    assert_fields("${s^}", vars, uf); /// ^ upper first
    const char *lf[] = {"heLLo", NULL};
    assert_fields("${s,}", vars, lf); /// , lower first
}

TEST(eval_pe_substring_operators) {
    /// Substring ${var:off} / ${var:off:len} via the map + apply_op stub
    /// (ASCII byte slice; UTF-8 grapheme parity runs live). Only the simple
    /// non-negative numeric spec is covered.
    const char *vars[] = {"s", "abcdef", NULL};
    const char *from2[] = {"cdef", NULL};
    assert_fields("${s:2}", vars, from2); /// offset only -> to end
    const char *mid[] = {"cd", NULL};
    assert_fields("${s:2:2}", vars, mid); /// offset + length
    const char *head[] = {"abc", NULL};
    assert_fields("${s:0:3}", vars, head); /// from start
    const char *clip[] = {"ef", NULL};
    assert_fields("${s:4:99}", vars, clip); /// length past end clamps
    const char *none[] = {NULL};
    assert_fields("${s:9}", vars,
                  none); /// offset past end -> "" dropped unquoted
}

TEST(eval_pe_substitution_operators) {
    /// Substitution ${var/pat/repl} (first) and ${var//pat/repl} (all) via the
    /// map + apply_op stub (LITERAL pattern; glob patterns and /#/% anchors run
    /// live). The operand is the whole `pat/repl` string, split inside
    /// apply_op.
    const char *vars[] = {"s", "abcabc", NULL};
    const char *first[] = {"Xbcabc", NULL};
    assert_fields("${s/a/X}", vars, first); /// replace first `a`
    const char *all[] = {"XbcXbc", NULL};
    assert_fields("${s//a/X}", vars, all); /// replace all `a`
    const char *strip[] = {"bcbc", NULL};
    assert_fields("${s//a/}", vars, strip); /// empty replacement -> remove all
    const char *nomatch[] = {"abcabc", NULL};
    assert_fields("${s/zzz/Q}", vars, nomatch); /// no match -> unchanged
    const char *multi[] = {"aQQaQQ", NULL};
    assert_fields("${s//bc/QQ}", vars, multi); /// multi-char pattern -> all
    const char *empty_pat[] = {"abcabc", NULL};
    assert_fields("${s///X}", vars, empty_pat); /// empty pattern -> unchanged
    const char *no_sep[] = {"aabc", NULL};
    assert_fields("${s/bc}", vars, no_sep); /// no separator -> delete first
}

TEST(eval_nounset_defers_unbound) {
    /// #686: under nounset an unbound read DEFERS (word_eval reports
    /// not-covered) so the legacy expander can report E1122; a bound name, the
    /// exempt specials and $0 stay covered, as do the operator forms that
    /// supply a value for an unset name.
    const char *vars[] = {"s", "abc", NULL};
    int n = 0;
    bool ok = false, fully = false;
    word_eval_env_t env = {.get = map_get,
                           .apply_op = map_apply_op,
                           .ctx = vars,
                           .ansi_c_quoting = true,
                           .nounset = true};
    char **f = peval_env("$missing", &env, &n, &ok, &fully);
    ASSERT_TRUE(fully, "parses");
    ASSERT_FALSE(ok, "unbound read defers under nounset");
    free_fields(f, n);

    f = peval_env("$s", &env, &n, &ok, &fully);
    ASSERT_TRUE(ok, "bound read stays covered under nounset");
    free_fields(f, n);

    f = peval_env("${missing:-d}", &env, &n, &ok, &fully);
    ASSERT_TRUE(ok, "an operator supplying a value is exempt");
    free_fields(f, n);

    env.nounset = false;
    f = peval_env("$missing", &env, &n, &ok, &fully);
    ASSERT_TRUE(ok, "without nounset an unbound read is covered (empty)");
    free_fields(f, n);
}

TEST(eval_pe_assign_operators) {
    /// Assign ${var:=x} / ${var=x} through the shared core. The bench map is
    /// read-only, so this pins the RESULT (the value the word expands to);
    /// the write-back itself is exercised live in test_executor and modelled
    /// by wordtool through setenv.
    const char *vars[] = {"s", "abc", "e", "", NULL};
    const char *unset_colon[] = {"D", NULL};
    assert_fields("${u:=D}", vars, unset_colon); /// unset -> default
    const char *empty_colon[] = {"D", NULL};
    assert_fields("${e:=D}", vars, empty_colon); /// empty -> default
    const char *set_colon[] = {"abc", NULL};
    assert_fields("${s:=D}", vars, set_colon); /// set -> keep value
    const char *unset_plain[] = {"D", NULL};
    assert_fields("${u=D}", vars, unset_plain); /// unset -> default
    const char *empty_plain[] = {NULL};
    assert_fields("${e=D}", vars, empty_plain); /// empty stays empty (dropped)
}

TEST(eval_pe_operand_glob_patterns) {
    /// Since #681 the bench runs the operators through the shared
    /// lush_param_op_apply, so a GLOB pattern operand is matched by the real
    /// matcher rather than approximated -- these assertions would have been
    /// impossible against the old literal-only stub, and they are what makes
    /// the differential corpus able to carry glob/anchor forms.
    const char *vars[] = {"s", "foobar", "n", "ab1", NULL};
    const char *shortest[] = {"obar", NULL};
    assert_fields("${s#f*o}", vars, shortest); /// shortest prefix match
    const char *longest[] = {"bar", NULL};
    assert_fields("${s##f*o}", vars, longest); /// longest prefix match
    const char *suffix[] = {"fo", NULL};
    assert_fields("${s%o*r}", vars, suffix); /// glob suffix strip
    const char *subst[] = {"fZar", NULL};
    assert_fields("${s/o*b/Z}", vars, subst); /// glob substitution
    const char *glob_all[] = {"f..bar", NULL};
    assert_fields("${s//o/.}", vars, glob_all); /// literal, global
    const char *star_all[] = {"f", NULL};
    assert_fields("${s//o*/}", vars, star_all); /// glob, delete
    const char *anchored[] = {"abX", NULL};
    assert_fields("${n/%1/X}", vars, anchored); /// %-anchored substitution
}

TEST(eval_ansic) {
    /// $'...' decoded bytes become one literal field; empty stays one field;
    /// a decoded glob metachar is literal (no glob).
    const char *vars[] = {NULL};
    const char *tab[] = {"a\tb", NULL};
    assert_fields("$'a\\tb'", vars, tab);
    const char *empty[] = {"", NULL};
    assert_fields("$''", vars, empty);
    const char *glob[] = {"a*b", NULL};
    assert_fields("$'a*b'", vars, glob);
}

TEST(eval_bash_split_deferred) {
    /// Under bash-mode word splitting, an unquoted parameter needs IFS
    /// splitting (Step 1c-1b) and must defer -- never mis-eval as one field.
    const char *vars[] = {"X", "a b c", NULL};
    tokenizer_t *tok = tokenizer_new("$X");
    bool fully = false;
    word_t *w = parse_word(tok, WORD_CTX_ARG, &fully);
    ASSERT_TRUE(fully, "parse covers $X");
    word_eval_env_t env = {.get = map_get,
                           .apply_op = map_apply_op,
                           .ctx = vars,
                           .ifs = NULL,
                           .word_split_default = true,
                           .ansi_c_quoting = true};
    int n = 0;
    bool ok = false;
    char **got = word_eval(w, &env, &n, &ok);
    ASSERT_FALSE(ok, "unquoted $X under bash split defers");
    ASSERT_NULL(got, "no fields when deferred");
    /// A quoted "$X" does NOT split even in bash mode -> covered, one field.
    word_free(w);
    tokenizer_free(tok);
    tok = tokenizer_new("\"$X\"");
    w = parse_word(tok, WORD_CTX_ARG, &fully);
    got = word_eval(w, &env, &n, &ok);
    ASSERT_TRUE(ok, "quoted var covered even in bash mode");
    ASSERT_EQ(n, 1, "one field");
    ASSERT_STR_EQ(got[0], "a b c", "quoted value not split");
    free_fields(got, n);
    word_free(w);
    tokenizer_free(tok);
}

int main(void) {
    printf("=== word_eval (Step 1c-1) Tests ===\n\n");
    RUN_TEST(eval_bare_literal);
    RUN_TEST(eval_single_quoted);
    RUN_TEST(eval_double_quoted);
    RUN_TEST(eval_var_no_split_lush);
    RUN_TEST(eval_quoted_var);
    RUN_TEST(eval_null_word_unquoted_empty_drops);
    RUN_TEST(eval_null_word_unset_drops);
    RUN_TEST(eval_quoted_empty_survives);
    RUN_TEST(eval_concatenation_fusion);
    RUN_TEST(eval_glob_is_not_yet_covered);
    RUN_TEST(eval_quoted_glob_is_literal);
    RUN_TEST(eval_special_param_scalar_covered);
    RUN_TEST(eval_positional_param_covered);
    RUN_TEST(eval_pe_alternation_operators);
    RUN_TEST(eval_pe_pattern_strip_operators);
    RUN_TEST(eval_pe_case_conversion_operators);
    RUN_TEST(eval_pe_substring_operators);
    RUN_TEST(eval_pe_substitution_operators);
    RUN_TEST(eval_nounset_defers_unbound);
    RUN_TEST(eval_pe_assign_operators);
    RUN_TEST(eval_pe_operand_glob_patterns);
    RUN_TEST(eval_ansic);
    RUN_TEST(eval_bash_split_deferred);
    return TEST_RESULT();
}
