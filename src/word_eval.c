/**
 * @file word_eval.c
 * @brief word_eval (Step 1c-1): evaluate a Word CST to argv fields.
 *
 * See word_eval.h for the contract and the 1c-1 scope. This slice evaluates the
 * lush-mode (no-split) covered subset: it descends the parts building a single
 * field, honouring quote context for the null-word rule, and flushes that field
 * through the shared argv_append_word (so null-word removal is the exact code
 * the live executor uses). Splitting, globs, specials, vectors, and operators
 * are not-yet-covered and set *out_ok = false rather than producing a wrong
 * vector.
 */
#include "word_eval.h"

#include <stdlib.h>
#include <string.h>

#include "field_split.h"

/// Field accumulator: the argv list being built plus the current field.
typedef struct {
    char **fields;
    int count;
    int cap;
    char *cur;       ///< current field, always malloc'd + NUL-terminated
    size_t cur_len;  ///< strlen(cur)
    size_t cur_cap;  ///< allocated size of cur
    bool cur_quoted; ///< a quoted part contributed to cur (survives null-word)
} eval_acc_t;

static bool acc_init(eval_acc_t *a) {
    memset(a, 0, sizeof(*a));
    a->cur = malloc(1);
    if (!a->cur) {
        return false;
    }
    a->cur[0] = '\0';
    a->cur_cap = 1;
    return true;
}

static bool acc_append(eval_acc_t *a, const char *s, size_t n) {
    if (a->cur_len + n + 1 > a->cur_cap) {
        size_t cap = a->cur_cap ? a->cur_cap : 1;
        while (cap < a->cur_len + n + 1) {
            cap *= 2;
        }
        char *grown = realloc(a->cur, cap);
        if (!grown) {
            return false;
        }
        a->cur = grown;
        a->cur_cap = cap;
    }
    memcpy(a->cur + a->cur_len, s, n);
    a->cur_len += n;
    a->cur[a->cur_len] = '\0';
    return true;
}

/// Flush the current field through the shared null-word rule, then reopen a
/// fresh empty field. Ownership of the old buffer transfers to argv_append_word
/// (which frees it if null-word-dropped).
static bool acc_flush(eval_acc_t *a) {
    if (!argv_append_word(&a->fields, &a->count, &a->cap, a->cur,
                          a->cur_quoted)) {
        a->cur = NULL; /// argv_append_word freed it
        return false;
    }
    a->cur = malloc(1);
    if (!a->cur) {
        return false;
    }
    a->cur[0] = '\0';
    a->cur_len = 0;
    a->cur_cap = 1;
    a->cur_quoted = false;
    return true;
}

static void acc_free(eval_acc_t *a) {
    for (int i = 0; i < a->count; i++) {
        free(a->fields[i]);
    }
    free(a->fields);
    free(a->cur);
}

static bool is_plain_ident(const char *s) {
    if (!s || !*s) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                  *p == '_' || (p != s && *p >= '0' && *p <= '9');
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// A bare (unquoted) literal must NOT be treated as a plain literal if it
/// carries a character that triggers a later expansion pass this slice does not
/// model. Conservative: the glob metacharacters `* ? [ ]`, the extglob group
/// parens `( )` (covering `@(`/`?(`/`+(`/`!(`/`*(`, whose introducers are
/// otherwise plain bytes), and the brace-expansion braces `{ }` (`{a,b}` ->
/// a vector). Deferring a borderline word to the legacy path is always safe;
/// wrongly covering an expanding word is not.
static bool has_word_expansion_meta(const char *s) {
    return strpbrk(s, "*?[](){}") != NULL;
}

static bool eval_parts(const word_t *body, eval_acc_t *a,
                       const word_eval_env_t *env, bool in_dquote, bool *ok);

/// Evaluate one part into the accumulator. Returns false on allocation failure;
/// sets *ok = false (and returns true) for a not-yet-covered construct.
static bool eval_part(const word_part_t *p, eval_acc_t *a,
                      const word_eval_env_t *env, bool in_dquote, bool *ok) {
    switch (p->kind) {
    case WP_LITERAL:
    case WP_SINGLE:
    case WP_ANSIC: {
        /// `$'...'` decodes ANSI-C escapes only under FEATURE_ANSI_QUOTING (off
        /// in POSIX mode, where legacy leaves `$'...'` literal). WP_ANSIC was
        /// decoded at parse time, so when the feature is off defer the whole
        /// word to the legacy expander rather than emit the decoded bytes.
        if (p->kind == WP_ANSIC && !env->ansi_c_quoting) {
            *ok = false;
            return true;
        }
        const char *text = p->u.leaf.text ? p->u.leaf.text : "";
        /// A glob/brace metacharacter that the legacy path would re-expand must
        /// defer. Legacy re-globs the DEQUOTED value, so a genuinely quoted
        /// char (single/double/ANSI-C) is inert, but a backslash-ESCAPED one
        /// (a WP_LITERAL leaf with literal_meta, e.g. `\*`) is NOT -- legacy
        /// still globs it. So gate on quoted CONTEXT (part kind / in_dquote),
        /// not on literal_meta, which conflates "escaped" with "quoted". Under
        /// zsh-extended-glob `#`/`^` are glob metacharacters too.
        bool quoted_ctx =
            in_dquote || p->kind == WP_SINGLE || p->kind == WP_ANSIC;
        if (!quoted_ctx && (has_word_expansion_meta(text) ||
                            (env->zsh_extended_glob && strpbrk(text, "#^")))) {
            *ok = false;
            return true;
        }
        if (!acc_append(a, text, strlen(text))) {
            return false;
        }
        if (in_dquote || p->u.leaf.literal_meta) {
            a->cur_quoted = true;
        }
        return true;
    }
    case WP_BARE:
        return eval_parts(PART_BODY(p), a, env, false, ok);
    case WP_DOUBLE:
        /// A double-quoted section makes an otherwise-empty field survive.
        a->cur_quoted = true;
        return eval_parts(PART_BODY(p), a, env, true, ok);
    case WP_PARAM: {
        /// 1c-1 covers only a simple $name / ${name}: no operator, no
        /// subscript/operands, no zsh flags, a plain identifier. Specials
        /// ($@/$?/... and the identifier-shaped $_ = last-arg) are deferred.
        if (p->u.param.op != -1 || p->u.param.subscript || p->u.param.operand ||
            p->u.param.operand2 || p->u.param.flags != 0 ||
            !is_plain_ident(p->u.param.name) ||
            strcmp(p->u.param.name, "_") == 0) {
            *ok = false;
            return true;
        }
        /// An unquoted parameter under bash-mode word splitting needs IFS
        /// splitting (Step 1c-1b); defer it here.
        if (!in_dquote && env->word_split_default) {
            *ok = false;
            return true;
        }
        /// A bare `$name` bound to a list/map expands to a VECTOR (its elements
        /// / values) -- this single-field slice cannot represent that, so defer
        /// rather than emit one joined field the legacy path would not.
        if (env->is_scalar && !env->is_scalar(env->ctx, p->u.param.name)) {
            *ok = false;
            return true;
        }
        /// The get callback returns an OWNED string (or NULL for unset); we
        /// copy it into the field and free it. This lets the live executor
        /// source values from the symtable (which returns owned copies) without
        /// leaking; bench callbacks wrap getenv in strdup to match the
        /// contract.
        char *got = env->get ? env->get(env->ctx, p->u.param.name) : NULL;
        bool appended = acc_append(a, got ? got : "", got ? strlen(got) : 0);
        free(got);
        if (!appended) {
            return false;
        }
        if (in_dquote) {
            a->cur_quoted = true;
        }
        return true;
    }
    default:
        /// WP_BRACE/ARRAY_LIT/CMDSUB/BACKTICK/ARITH/TILDE/PROCSUB/KINDSIGIL
        /// are not-yet-covered in 1c-1.
        *ok = false;
        return true;
    }
}

static bool eval_parts(const word_t *body, eval_acc_t *a,
                       const word_eval_env_t *env, bool in_dquote, bool *ok) {
    for (uint32_t i = 0; i < body->n_parts; i++) {
        if (!eval_part(WORD_PART(body, i), a, env, in_dquote, ok)) {
            return false;
        }
        if (!*ok) {
            return true; /// stop at the first not-yet-covered part
        }
    }
    return true;
}

char **word_eval(const word_t *w, const word_eval_env_t *env, int *nfields,
                 bool *out_ok) {
    *nfields = 0;
    *out_ok = false;
    if (!w || !env) {
        return NULL;
    }

    eval_acc_t a;
    if (!acc_init(&a)) {
        return NULL;
    }

    bool ok = true;
    /// The word's top-level parts are context groups; evaluate them in the
    /// unquoted (word) context -- each group sets its own inner context.
    if (!eval_parts(w, &a, env, false, &ok)) {
        acc_free(&a);
        return NULL; /// allocation failure
    }
    if (!ok) {
        acc_free(&a);
        return NULL; /// not-yet-covered
    }

    /// Flush the single assembled field (null-word removal applied here).
    if (!acc_flush(&a)) {
        acc_free(&a);
        return NULL;
    }

    /// NUL-terminate the argv vector.
    if (!add_to_argv_list(&a.fields, &a.count, &a.cap, NULL)) {
        acc_free(&a);
        return NULL;
    }
    a.count--; /// the NULL terminator is not a field

    free(a.cur);
    *nfields = a.count;
    *out_ok = true;
    return a.fields;
}
