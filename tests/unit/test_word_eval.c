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
#include <stdlib.h>
#include <string.h>

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

/// Bench apply_op: the four alternation operators, mirroring the executor's
/// apply_param_operator (op 0/1/10/11).
static char *map_apply_op(void *ctx, const char *name, const char *value,
                          const char *deflt, int op) {
    (void)ctx;
    (void)name;
    bool empty = !value || value[0] == '\0';
    const char *d = deflt ? deflt : "";
    switch (op) {
    case 0:
        return strdup(empty ? d : value);
    case 1:
        return strdup(!empty ? d : "");
    case 10:
        return strdup(!value ? d : value);
    case 11:
        return strdup(value ? d : "");
    default:
        return strdup("");
    }
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
    RUN_TEST(eval_ansic);
    RUN_TEST(eval_bash_split_deferred);
    return TEST_RESULT();
}
