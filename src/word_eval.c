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

/// The scalar special parameters this slice covers: `$?` (last exit status),
/// `$$` (shell PID), `$#` (positional count), `$!` (last background PID, or
/// empty), `$-` (current option flags). Each is a plain scalar -- no vector --
/// so it fits the single-field model. The value is sourced through the same
/// expander legacy uses (see the get callback), so it matches by construction.
/// Vector specials (`$@`/`$*`) and the identifier-shaped last-arg `$_` remain
/// deferred.
static bool is_covered_special(const char *name) {
    return name && name[0] != '\0' && name[1] == '\0' &&
           (name[0] == '?' || name[0] == '$' || name[0] == '#' ||
            name[0] == '!' || name[0] == '-');
}

/// A single-digit positional parameter `$0`..`$9` (name is exactly one digit).
/// `$0` is the shell/script name; `$1`..`$9` are positional arguments -- each a
/// scalar sourced through the same expander legacy uses. Two-plus digits are
/// spelled `${10}` (an operator/brace form) and are handled elsewhere.
static bool is_positional(const char *name) {
    return name && name[0] >= '0' && name[0] <= '9' && name[1] == '\0';
}

/// The alternation operators: `${var:-x}` (0), `${var:+x}` (1), `${var-x}`
/// (10), `${var+x}` (11). The operand is a scalar default VALUE (word_eval'd).
static bool is_covered_alternation_op(int32_t op) {
    return op == 0 || op == 1 || op == 10 || op == 11;
}

/// The case-conversion operators: `${var^p}` (8), `${var^^p}` (4), `${var,p}`
/// (9), `${var,,p}` (5). The (optional) pattern restricts which characters
/// convert; this slice covers only the NO-pattern form (convert all / first),
/// so a case op carrying a pattern defers. (The original reason was that the
/// bench could not model the pattern-restricted match; since #681 the bench
/// runs the real lush_case_pattern, so covering it is now just a widening
/// slice of its own -- see #683.)
static bool is_case_op(int32_t op) {
    return op == 4 || op == 5 || op == 8 || op == 9;
}

/// The substring operator `${var:offset:length}` (14). The operand is a numeric
/// offset[:length] spec (validated simple + non-negative at parse); it is
/// recovered literally and handed to apply_op, which runs the same grapheme-
/// aware lush_substring_extract legacy uses -- parity by construction. zsh `:h`
/// modifier chains and negative/arith/`$` operands defer at parse.
static bool is_substring_op(int32_t op) { return op == 14; }

/// The substitution operators `${var/pat/repl}` (16, first) and
/// `${var//pat/repl}` (15, all). The whole `pattern/replacement` operand is
/// recovered literally and handed to apply_op, which splits it at the first
/// unescaped `/` and runs the same lush_pattern_substitute legacy uses (the
/// glob pattern is matched there) -- parity by construction. An operand that
/// does not tokenize to one literal word defers at parse: a `$`-expanding,
/// quoted, or backslash-carrying operand (incl. the `\/` escaped-slash idiom),
/// and
/// `${var/#pat/repl}` (a leading `#` starts a comment). `${var/%pat/repl}`
/// defers only when `%` is followed by an identifier-start byte (the kind-sigil
/// token, so coverage there is mode-dependent on FEATURE_KIND_SIGILS); other
/// `%` anchors are COVERED, and parity still holds by construction because
/// lush_pattern_substitute strips the anchor on both paths.
static bool is_substitution_op(int32_t op) { return op == 15 || op == 16; }

/// Operators whose operand is recovered as a LITERAL string and passed to
/// apply_op verbatim (word_eval must not glob, dequote, or word-split it):
/// pattern-strip (`${var#p}` 6/2/7/3, a glob pattern), case-conversion
/// (8/4/9/5, an optional restricting glob pattern), substring (14, a numeric
/// spec), and substitution (15/16, a pattern/replacement spec).
static bool op_has_literal_operand(int32_t op) {
    return op == 2 || op == 3 || op == 6 || op == 7 || is_case_op(op) ||
           is_substring_op(op) || is_substitution_op(op);
}

/// The assign operators `${var:=x}` (12, when unset or empty) and `${var=x}`
/// (13, when unset). UNLIKE every other covered operator these have a SIDE
/// EFFECT: the value is written back to the variable.
///
/// A CST evaluation is PROVISIONAL -- this function can still defer a later
/// part of the same word, and the caller then re-expands the whole word on the
/// legacy path -- so the write must NOT be applied as it is computed. It would
/// be visible to that re-expansion and the earlier parts would expand against
/// mutated state (`echo A$u-B${u:=x}-C$g` printing `Ax-Bx` where legacy prints
/// `A-Bx`). Operator idempotency does not help; what breaks is the rest of the
/// word. The apply_op implementation therefore BUFFERS the write and its
/// caller commits it only once the word is known covered -- see
/// word_assign_txn_t in executor.c, whose read callback also serves the
/// pending value so within-word visibility (`${u:=x}${u}`) still matches
/// legacy. A bench apply_op with no store can simply ignore the write.
///
/// The operand is a scalar default VALUE (word-eval'd), like alternation.
static bool is_assign_op(int32_t op) { return op == 12 || op == 13; }

/// Every parameter-expansion operator this slice evaluates through apply_op.
/// The error (`:?`/`?`) and transform (`@`) ops still defer.
static bool is_covered_pe_op(int32_t op) {
    return is_covered_alternation_op(op) || op_has_literal_operand(op) ||
           is_assign_op(op);
}

/// Append the literal text of a pure-literal operand Word to the accumulator,
/// keeping glob metacharacters (they are the strip PATTERN, matched by apply_op
/// -- word_eval must not glob them). Returns false on a non-literal part (a `$`
/// expansion / cmdsub -- deferred at parse for this slice) or allocation
/// failure.
static bool collect_literal_text(const word_t *w, eval_acc_t *a) {
    for (uint32_t i = 0; i < w->n_parts; i++) {
        const word_part_t *p = WORD_PART(w, i);
        switch (p->kind) {
        case WP_BARE:
        case WP_DOUBLE:
            if (!collect_literal_text(PART_BODY(p), a)) {
                return false;
            }
            break;
        case WP_LITERAL:
        case WP_SINGLE:
        case WP_ANSIC: {
            const char *t = p->u.leaf.text ? p->u.leaf.text : "";
            if (!acc_append(a, t, strlen(t))) {
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

/// Recover the glob PATTERN string of a pattern-strip operand (NULL operand ->
/// "", an empty pattern). Owned; NULL means the operand is not a pure literal
/// (or allocation failed) -> defer.
static char *pattern_operand_string(const word_t *operand) {
    eval_acc_t a;
    if (!acc_init(&a)) {
        return NULL;
    }
    if (operand && !collect_literal_text(operand, &a)) {
        acc_free(&a);
        return NULL;
    }
    char *s = a.cur; /// take ownership; acc_free then frees only a.fields
    a.cur = NULL;
    acc_free(&a);
    return s;
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
        /// A covered PE operator -- alternation (${var:-x} ...), pattern-strip
        /// / case-conversion (${var#p}, ${var^^} ...), substring
        /// (${var:off:len}), substitution (${var/pat/repl}), or assign
        /// (${var:=x}, the one family with a side effect): resolve the
        /// value, evaluate the operand (a scalar default VALUE for alternation,
        /// a literal glob PATTERN / numeric / pattern-replacement spec
        /// otherwise), and apply the operator through the shared apply_op
        /// primitive (so the semantics match legacy by construction). Requires
        /// a plain-identifier scalar name, no subscript/operand2/flags, the
        /// apply_op callback, and -- unquoted -- no bash-mode splitting of the
        /// result.
        if (is_covered_pe_op(p->u.param.op)) {
            if (p->u.param.subscript || p->u.param.operand2 ||
                p->u.param.flags != 0 || !env->apply_op ||
                !is_plain_ident(p->u.param.name) ||
                (!in_dquote && env->word_split_default) ||
                (env->is_scalar &&
                 !env->is_scalar(env->ctx, p->u.param.name))) {
                *ok = false;
                return true;
            }
            char *value = env->get ? env->get(env->ctx, p->u.param.name) : NULL;
            char *deflt = NULL;
            if (op_has_literal_operand(p->u.param.op)) {
                /// The operand is recovered as a LITERAL string with its bytes
                /// intact (a glob pattern for strip/case, a numeric
                /// offset[:len] spec for substring); apply_op does the match /
                /// conversion / slice. A NULL operand is the empty string (an
                /// empty pattern; case ops then convert all characters).
                deflt = pattern_operand_string(p->u.param.operand);
                if (!deflt) {
                    free(value);
                    *ok = false;
                    return true;
                }
                /// A case op with a restricting pattern (${var^^pat}) defers:
                /// only the convert-all/first (empty-pattern) form is covered.
                if (is_case_op(p->u.param.op) && deflt[0] != '\0') {
                    free(value);
                    free(deflt);
                    *ok = false;
                    return true;
                }
            } else if (p->u.param.operand) {
                /// Alternation operand: a scalar default value. The operator
                /// RESULT (not the operand) is what the outer word may split,
                /// and the covered outer word does not split, so the operand
                /// must yield at most one field. More than one, or not-covered,
                /// defers.
                int dn = 0;
                bool dok = false;
                char **df = word_eval(p->u.param.operand, env, &dn, &dok);
                if (!dok || dn > 1) {
                    for (int i = 0; i < dn; i++) {
                        free(df[i]);
                    }
                    free(df);
                    free(value);
                    *ok = false;
                    return true;
                }
                deflt =
                    (dn == 1) ? df[0] : NULL; /// take ownership of the field
                free(df);
            }
            char *result = env->apply_op(env->ctx, p->u.param.name, value,
                                         deflt ? deflt : "", p->u.param.op);
            free(value);
            free(deflt);
            if (!result) {
                *ok = false;
                return true;
            }
            /// The operator result is an expansion value: an unquoted one
            /// carrying a glob/brace metachar is re-expanded by legacy, so
            /// defer (mirror the $var value gate).
            if (!in_dquote &&
                (has_word_expansion_meta(result) ||
                 (env->zsh_extended_glob && strpbrk(result, "#^")))) {
                free(result);
                *ok = false;
                return true;
            }
            if (!acc_append(a, result, strlen(result))) {
                free(result);
                return false;
            }
            free(result);
            if (in_dquote) {
                a->cur_quoted = true;
            }
            return true;
        }
        /// Covers a simple $name / ${name} (plain identifier), the scalar
        /// specials $?/$$/$# (is_covered_special), and single-digit positionals
        /// $0..$9 (is_positional): no operator, no subscript/operands, no zsh
        /// flags. Vector specials ($@/$*), the identifier-shaped $_ (last-arg),
        /// and other specials stay deferred.
        if (p->u.param.op != -1 || p->u.param.subscript || p->u.param.operand ||
            p->u.param.operand2 || p->u.param.flags != 0 ||
            (!is_plain_ident(p->u.param.name) &&
             !is_covered_special(p->u.param.name) &&
             !is_positional(p->u.param.name)) ||
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
        /// An UNQUOTED parameter whose VALUE carries a glob/brace metacharacter
        /// is re-expanded by the legacy path: it globs / brace-expands the
        /// expansion result (per bash/POSIX -- `v='*'; echo $v` lists files,
        /// `v='{a,b}'; echo $v` yields `a b`). This single-field slice does not
        /// model that later pass, so defer. A quoted expansion is glob/brace-
        /// inert, so this only fires unquoted. Under zsh-extended-glob `#`/`^`
        /// are metacharacters too.
        if (!in_dquote && got &&
            (has_word_expansion_meta(got) ||
             (env->zsh_extended_glob && strpbrk(got, "#^")))) {
            free(got);
            *ok = false;
            return true;
        }
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
