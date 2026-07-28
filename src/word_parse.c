/**
 * @file word_parse.c
 * @brief parse_word (Step 1b): build a word_t from the tokenizer's stream.
 *
 * See word_parse.h for the contract and the Step 1b-1 scope. This slice fuses
 * the adjacent argument word tokens starting at the tokenizer's current token
 * and maps the well-defined single-token cases onto the Word CST. Anything not
 * yet covered sets *out_fully_handled = false rather than fabricating a
 * best-effort mis-parse -- the differential harness classifies such a word as
 * not-yet-covered, distinct from a parity bug.
 *
 * Span policy for 1b-1: the whole word and each top-level part (one per token)
 * carry EXACT source spans from the token offsets. Sub-token part spans (leaves
 * and params inside a group) are token-granular in this slice -- exact byte
 * spans for sub-parts are a follow-up refinement. Neither word_reconstruct
 * (which uses the whole-word span) nor evaluation depends on sub-part spans.
 */
#include "word_parse.h"

#include <stdlib.h>
#include <string.h>

#include "brace_match.h"
#include "dequote.h"

/// Identifier-start / identifier-continue for a parameter name. ASCII only
/// here; the tokenizer handles Unicode identifiers upstream, and 1b-1 defers
/// any name that is not a plain ASCII identifier or a recognised special
/// parameter.
static bool is_name_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static bool is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

/// A single-character special parameter: $@ $* $# $? $! $$ $- and $0..$9.
static bool is_special_param(char c) {
    return c == '@' || c == '*' || c == '#' || c == '?' || c == '!' ||
           c == '$' || c == '-' || (c >= '0' && c <= '9');
}

/// Duplicate n bytes of s as a NUL-terminated string. NULL on allocation
/// failure.
static char *dup_n(const char *s, size_t n) {
    char *out = malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

/// Build a simple WP_PARAM (op = -1) with the given name.
static word_part_t *make_simple_param(const char *name, size_t namelen) {
    word_part_t *p = word_part_new(WP_PARAM, 0, 0);
    if (!p) {
        return NULL;
    }
    p->u.param.op = -1;
    p->u.param.name = dup_n(name, namelen);
    if (!p->u.param.name) {
        word_part_free(p);
        return NULL;
    }
    return p;
}

/**
 * @brief Parse a `$...` expansion at s (s[0] == '$'), for Step 1b-1.
 *
 * Handles a simple `$name`, a single-character special (`$@`, `$?`, ...), and
 * `${name}` with no operator or subscript.
 *
 * @param s        Points at the `$`.
 * @param n        Bytes available from s.
 * @param consumed On a recognised expansion, the byte count consumed.
 * @param handled  Set to false when s begins a real but not-yet-covered
 *                 expansion (an operator/subscript `${...}`, `$(...)`,
 * `$((...))`); left unchanged when s is a lone/literal `$`.
 * @return A WP_PARAM part on a simple expansion; NULL otherwise (see @p
 * handled). NULL with @p handled unchanged and *consumed == 0 means "treat `$`
 * as a literal character".
 */
static word_part_t *parse_simple_expansion(const char *s, size_t n,
                                           size_t *consumed, bool *handled) {
    *consumed = 0;
    if (n < 2) {
        return NULL; /// a lone `$` is a literal
    }
    char c = s[1];

    if (c == '{') {
        size_t close = 0;
        if (!lush_find_matching_brace(s + 1, n - 1, &close)) {
            *handled = false;
            return NULL;
        }
        /// interior is between `{` and `}`: s[2 .. 1+close), length close-1.
        const char *interior = s + 2;
        size_t ilen = (close >= 1) ? close - 1 : 0;
        if (ilen == 0 || !is_name_start(interior[0])) {
            *handled = false; /// ${}, ${#x}, ${!x}, operators, subscripts ...
            return NULL;
        }
        for (size_t i = 1; i < ilen; i++) {
            if (!is_name_char(interior[i])) {
                *handled = false; /// ${name:-x}, ${name[k]}, ${name/a/b} ...
                return NULL;
            }
        }
        word_part_t *p = make_simple_param(interior, ilen);
        if (!p) {
            return NULL;
        }
        *consumed = 1 + close + 1; /// `$` + `{`..`}` span
        return p;
    }

    if (is_name_start(c)) {
        size_t j = 1;
        while (j < n && is_name_char(s[j])) {
            j++;
        }
        word_part_t *p = make_simple_param(s + 1, j - 1);
        if (!p) {
            return NULL;
        }
        *consumed = j;
        return p;
    }

    if (is_special_param(c)) {
        word_part_t *p = make_simple_param(s + 1, 1);
        if (!p) {
            return NULL;
        }
        *consumed = 2;
        return p;
    }

    if (c == '(') {
        *handled = false; /// $( command sub / $(( arithmetic -- deferred
        return NULL;
    }

    return NULL; /// `$` followed by something else -> literal `$`
}

/// Append one WP_LITERAL leaf covering text[start, end) with the given
/// literal_meta and a token-granular span. Returns false on allocation failure.
static bool append_literal_leaf(word_t *body, const char *text, size_t start,
                                size_t end, bool literal_meta,
                                uint32_t span_off, uint32_t span_len) {
    word_part_t *leaf = word_part_new(WP_LITERAL, span_off, span_len);
    if (!leaf) {
        return false;
    }
    leaf->u.leaf.literal_meta = literal_meta;
    leaf->u.leaf.text = dup_n(text + start, end - start);
    if (!leaf->u.leaf.text || !word_add_part(body, leaf)) {
        word_part_free(leaf);
        return false;
    }
    return true;
}

/// Emit a bare literal run (text/prov from lush_dequote_span of an unquoted
/// TOK_WORD) as WP_LITERAL leaves grouped by glob-eligibility: an unquoted (U)
/// byte's metacharacters are active (literal_meta false); an escaped (E) byte
/// is literal (literal_meta true). Returns false on allocation failure.
static bool append_bare_literals(word_t *body, const char *text,
                                 const char *prov, size_t n, uint32_t span_off,
                                 uint32_t span_len) {
    size_t i = 0;
    while (i < n) {
        bool meta = prov[i] != QUOTE_PROV_UNQUOTED;
        size_t start = i;
        while (i < n && (prov[i] != QUOTE_PROV_UNQUOTED) == meta) {
            i++;
        }
        if (!append_literal_leaf(body, text, start, i, meta, span_off,
                                 span_len)) {
            return false;
        }
    }
    return true;
}

/// Scan the interior of a PURE double-quoted string (all bytes double-quoted,
/// no backslash -- guaranteed by the caller) into literal leaves and simple
/// `$...` params, appended to @p body (a WP_DOUBLE body). Because the interior
/// has no backslash, every `$` is a genuine expansion introducer. Every literal
/// byte is glob-inert (literal_meta true) because it is double-quoted. Sets
/// *handled = false on a not-yet-covered expansion (e.g. `$(...)`). Returns
/// false on allocation failure.
static bool parse_double_interior(word_t *body, const char *text, size_t n,
                                  uint32_t span_off, uint32_t span_len,
                                  bool *handled) {
    size_t i = 0, lit_start = 0;
    while (i < n) {
        if (text[i] != '$') {
            i++;
            continue;
        }
        size_t consumed = 0;
        bool exp_handled = true;
        word_part_t *param =
            parse_simple_expansion(text + i, n - i, &consumed, &exp_handled);
        if (param) {
            if (i > lit_start &&
                !append_literal_leaf(body, text, lit_start, i, true, span_off,
                                     span_len)) {
                word_part_free(param);
                return false;
            }
            if (!word_add_part(body, param)) {
                word_part_free(param);
                return false;
            }
            i += consumed;
            lit_start = i;
            continue;
        }
        if (!exp_handled) {
            /// A real but not-yet-covered expansion (operator, cmdsub, ...).
            /// Mark not-fully-handled and stop structuring; the trailing flush
            /// below emits the remainder as one literal leaf. That leaf is not
            /// a faithful structure, but the word is fully_handled == false so
            /// the harness skips it (and word_reconstruct uses the whole-word
            /// span, not the leaves), which is correct for 1b-1's
            /// token-granular spans.
            *handled = false;
            break;
        }
        i++; /// a literal `$`
    }
    if (i > lit_start && !append_literal_leaf(body, text, lit_start, n, true,
                                              span_off, span_len)) {
        return false;
    }
    return true;
}

/// Wrap a WP_* group (WP_BARE / WP_DOUBLE) around a fresh body and add it to
/// the word, returning the body (or NULL on failure, having freed nothing the
/// caller still owns). On success the group is owned by @p w.
static word_t *add_group(word_t *w, word_part_kind_t kind, uint32_t off,
                         uint32_t len) {
    word_part_t *group = word_part_new(kind, off, len);
    if (!group) {
        return NULL;
    }
    word_t *body = word_new(off, len);
    if (!body) {
        word_part_free(group);
        return NULL;
    }
    word_set_body(group, body);
    if (!word_add_part(w, group)) {
        word_part_free(group);
        return NULL;
    }
    return body;
}

/// Map one token onto part(s) appended to @p w. Returns false only on
/// allocation failure (fatal to the whole parse); sets *fully = false for a
/// not-yet-covered construct (non-fatal -- the word is still returned).
static bool emit_token_parts(word_t *w, tokenizer_t *tok, const token_t *t,
                             bool *fully) {
    uint32_t off = (uint32_t)t->position;
    uint32_t len = (uint32_t)(t->end_position - t->position);
    const char *raw = tok->input + t->position;

    switch (t->type) {
    case TOK_WORD:
    case TOK_NUMBER: {
        /// An absorbed quoted subscript (#631) rides a TOK_WORD with a
        /// provenance map; that structure is deferred in 1b-1.
        if (t->quote_prov) {
            *fully = false;
            return true;
        }
        char *text = NULL, *prov = NULL;
        if (!lush_dequote_span(raw, len, &text, &prov, NULL)) {
            *fully = false;
            return true;
        }
        word_t *body = add_group(w, WP_BARE, off, len);
        if (!body) {
            free(text);
            free(prov);
            return false;
        }
        bool ok =
            append_bare_literals(body, text, prov, strlen(text), off, len);
        free(text);
        free(prov);
        return ok;
    }

    case TOK_STRING: {
        /// $'...' (ANSI-C) shares TOK_STRING with plain '...' but the raw span
        /// begins with `$`; defer it (WP_ANSIC follow-up).
        if (len >= 1 && raw[0] == '$') {
            *fully = false;
            return true;
        }
        word_part_t *sq = word_part_new(WP_SINGLE, off, len);
        if (!sq) {
            return false;
        }
        sq->u.leaf.literal_meta = true;
        sq->u.leaf.text = strdup(t->text ? t->text : "");
        if (!sq->u.leaf.text || !word_add_part(w, sq)) {
            word_part_free(sq);
            return false;
        }
        return true;
    }

    case TOK_EXPANDABLE_STRING: {
        /// Re-scan the raw "..." span with lush_dequote_span (the single
        /// provenance producer) rather than trusting the token's pre-computed
        /// text/quote_prov. 1b-1 covers only a PURE double-quoted interior:
        /// every byte double-quoted (D) -- a U byte means the tokenizer fused
        /// an adjacent unquoted run (`"$y"post`), deferred -- AND no backslash,
        /// because lush_dequote_span defers double-quote escapes (`"\$x"` ->
        /// text `\$x`, all D), so a `\$` would otherwise be mis-read as an
        /// expansion. Both mixed-quote splitting and double-quote escape
        /// processing are follow-up slices.
        char *text = NULL, *prov = NULL;
        if (!lush_dequote_span(raw, len, &text, &prov, NULL)) {
            *fully = false;
            return true;
        }
        size_t ilen = strlen(text);
        bool pure = true;
        for (size_t i = 0; i < ilen; i++) {
            if (prov[i] != QUOTE_PROV_DOUBLE || text[i] == '\\') {
                pure = false;
                break;
            }
        }
        if (!pure) {
            free(text);
            free(prov);
            *fully = false;
            return true;
        }
        word_t *body = add_group(w, WP_DOUBLE, off, len);
        if (!body) {
            free(text);
            free(prov);
            return false;
        }
        bool ok = parse_double_interior(body, text, ilen, off, len, fully);
        free(text);
        free(prov);
        return ok;
    }

    case TOK_VARIABLE: {
        const char *vtext = t->text ? t->text : "";
        size_t vlen = strlen(vtext);
        size_t consumed = 0;
        bool exp_handled = true;
        word_part_t *param =
            parse_simple_expansion(vtext, vlen, &consumed, &exp_handled);
        /// Require the expansion to consume the WHOLE token; a trailing remnant
        /// (a subscript, a fused suffix) is not yet structured.
        if (!param || consumed != vlen) {
            word_part_free(param);
            *fully = false;
            return true;
        }
        param->src_off = off;
        param->src_len = len;
        word_t *body = add_group(w, WP_BARE, off, len);
        if (!body) {
            word_part_free(param);
            return false;
        }
        if (!word_add_part(body, param)) {
            word_part_free(param);
            return false;
        }
        return true;
    }

    default:
        /// Command/arith/backtick substitutions and operator-ish word tokens
        /// are deferred to later 1b slices.
        *fully = false;
        return true;
    }
}

word_t *parse_word(tokenizer_t *tok, word_ctx_t ctx, bool *out_fully_handled) {
    (void)ctx; /// structure is context-independent in 1b-1; ctx drives eval
               /// policy later

    /// Define the out-flag on every path up front. A NULL return (no word, or
    /// an allocation failure the caller must treat as fatal) leaves it false.
    if (out_fully_handled) {
        *out_fully_handled = false;
    }

    token_t *t = tokenizer_current(tok);
    if (!t || t->type == TOK_EOF || !token_is_argument_word_token(t->type)) {
        return NULL;
    }

    uint32_t word_start = (uint32_t)t->position;
    uint32_t word_end = (uint32_t)t->end_position;
    word_t *w = word_new(word_start, 0);
    if (!w) {
        return NULL;
    }

    bool fully = true;
    bool first = true;
    size_t prev_end = 0;
    while (t && t->type != TOK_EOF && token_is_argument_word_token(t->type)) {
        if (!first && t->position != prev_end) {
            break; /// whitespace before this token ends the word
        }
        if (!emit_token_parts(w, tok, t, &fully)) {
            word_free(w);
            return NULL; /// allocation failure
        }
        word_end = (uint32_t)t->end_position;
        prev_end = t->end_position;
        first = false;
        tokenizer_advance(tok);
        t = tokenizer_current(tok);
    }

    w->src_off = word_start;
    w->src_len = word_end - word_start;
    if (out_fully_handled) {
        *out_fully_handled = fully;
    }
    return w;
}
