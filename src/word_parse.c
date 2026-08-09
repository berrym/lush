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
#include "escape.h"

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

/// Detect a COVERED parameter-expansion operator at the start of `s` (the bytes
/// right after the name in `${name<op>operand}`). Returns the op_type (an index
/// into the executor's param_operators[]: 0=`:-` 1=`:+` 10=`-` 11=`+`) and sets
/// *op_len, or -1 when `s` does not begin a covered operator. This first slice
/// covers only the four alternation operators; every other operator (`#`/`%`/
/// `^`/`,`/`/`/`:=`/`:?`/`:off`/subscript `[` ...) returns -1 so the whole
/// `${...}` defers. `:=`/`:?`/`:off` start with `:` but are not `:-`/`:+`.
static int detect_covered_pe_op(const char *s, size_t n, size_t *op_len) {
    if (n >= 2 && s[0] == ':' && s[1] == '-') {
        *op_len = 2;
        return 0;
    }
    if (n >= 2 && s[0] == ':' && s[1] == '+') {
        *op_len = 2;
        return 1;
    }
    if (n >= 1 && s[0] == '-') {
        *op_len = 1;
        return 10;
    }
    if (n >= 1 && s[0] == '+') {
        *op_len = 1;
        return 11;
    }
    return -1;
}

/// Build a WP_PARAM for `${name<op>operand}` (an alternation operator). The
/// operand is parsed as a child Word (it may itself contain expansions). An
/// EMPTY operand (`${var:-}`) yields a NULL operand Word (an empty default),
/// which the evaluator treats as "". A non-empty operand that parse_word does
/// not fully cover means the default cannot be faithfully evaluated -> defer
/// (sets *handled = false, returns NULL). Returns NULL with *handled unchanged
/// on allocation failure.
static word_part_t *make_operator_param(const char *name, size_t namelen,
                                        int op, const char *operand_str,
                                        size_t operand_len, bool *handled) {
    /// Legacy expands the operand through a `$`-only pass (expand_variables_in_
    /// string) that does NOT remove quotes or backslashes -- `${un:-'x'}` keeps
    /// the quotes literal, `${un:-a\b}` keeps the backslash. parse_word (below)
    /// WOULD dequote them, diverging from legacy. So defer any operand carrying
    /// a quote or backslash; a plain / `$`-only operand dequotes to itself and
    /// is faithful. (Whether legacy SHOULD dequote here is a separate question;
    /// the CST matches legacy for now.)
    for (size_t i = 0; i < operand_len; i++) {
        if (operand_str[i] == '\'' || operand_str[i] == '"' ||
            operand_str[i] == '\\') {
            *handled = false;
            return NULL;
        }
    }
    word_t *operand = NULL;
    if (operand_len > 0) {
        char *ostr = dup_n(operand_str, operand_len);
        if (!ostr) {
            return NULL;
        }
        tokenizer_t *otok = tokenizer_new(ostr);
        if (!otok) {
            free(ostr);
            return NULL;
        }
        bool ofully = false;
        operand = parse_word(otok, WORD_CTX_ARG, &ofully);
        /// The operand is a single default VALUE whose spaces are literal
        /// (`${var:-a b}` -> the default "a b"), but parse_word stops at
        /// whitespace and would silently drop everything after the first space.
        /// Require the whole operand to consume to ONE fully-covered word; a
        /// trailing token (a space-separated remainder) means we cannot
        /// faithfully represent the default -> defer.
        token_t *rest = tokenizer_current(otok);
        bool consumed_all = (!rest || rest->type == TOK_EOF);
        tokenizer_free(otok);
        free(ostr);
        if (!operand || !ofully || !consumed_all) {
            word_free(operand);
            *handled = false;
            return NULL;
        }
    }
    word_part_t *p = word_part_new(WP_PARAM, 0, 0);
    if (!p) {
        word_free(operand);
        return NULL;
    }
    p->u.param.op = op;
    p->u.param.operand = operand;
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
    /// This parser is `$`-anchored (see the contract above). A TOK_VARIABLE may
    /// also carry a kind-sigil expansion (`@name`/`%name`) whose leading byte
    /// is not `$`; those are a later slice, so defer rather than mis-read the
    /// sigil as `$` and resolve the bare name (which would drop the sigil's
    /// semantics).
    if (n < 1 || s[0] != '$') {
        *handled = false;
        return NULL;
    }
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
        /// Scan the plain-identifier name.
        size_t namelen = 1;
        while (namelen < ilen && is_name_char(interior[namelen])) {
            namelen++;
        }
        word_part_t *p = NULL;
        if (namelen == ilen) {
            /// Bare ${name} -- no operator.
            p = make_simple_param(interior, ilen);
        } else {
            /// A covered alternation operator (:-, :+, -, +) may follow the
            /// name; anything else (a subscript `[`, another operator) defers.
            size_t op_len = 0;
            int op = detect_covered_pe_op(interior + namelen, ilen - namelen,
                                          &op_len);
            if (op < 0) {
                *handled = false; /// ${name[k]}, ${name#pat}, ${name:off} ...
                return NULL;
            }
            p = make_operator_param(interior, namelen, op,
                                    interior + namelen + op_len,
                                    ilen - namelen - op_len, handled);
        }
        if (!p) {
            return NULL; /// allocation failure, or a not-covered operand
                         /// (make_operator_param set *handled = false)
        }
        *consumed = 1 + close + 1; /// `$` + `{`..`}` span
        return p;
    }

    if (is_name_start(c)) {
        size_t j = 1;
        while (j < n && is_name_char(s[j])) {
            j++;
        }
        /// The name scan is ASCII-only, but lush supports Unicode identifiers
        /// (`$café` in NFC/NFD). A high-bit byte right after the ASCII run may
        /// continue the identifier, which this slice does not decode -- defer
        /// rather than truncate the name and resolve the wrong variable.
        if (j < n && (unsigned char)s[j] >= 0x80) {
            *handled = false;
            return NULL;
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
        /// A bare word carrying a mid-word `$'...'` (`pre$'\t'`) is handed back
        /// by the tokenizer as one WORD (not a standalone $'...' TOK_STRING),
        /// where the live executor's ANSI-C handling is context-dependent
        /// (decoded in an assignment/word RHS, left literal on the positional
        /// path). Rather than replicate that here or treat `$'...'` as a
        /// literal single-quoted run and risk claiming false coverage, defer it
        /// -- mid-word ANSI-C is a later slice.
        for (uint32_t i = 0; i + 1 < len; i++) {
            if (raw[i] == '$' && raw[i + 1] == '\'') {
                *fully = false;
                return true;
            }
        }
        char *text = NULL, *prov = NULL;
        if (!lush_dequote_span(raw, len, &text, &prov, NULL)) {
            *fully = false;
            return true;
        }
        /// A `$` that survives dequoting as an UNQUOTED byte is an expansion
        /// introducer the tokenizer did not fold into a TOK_VARIABLE -- notably
        /// a positional `$0`..`$9`, which the tokenizer splits into a lone `$`
        /// token plus a following digit token, so the `$` lands here dangling
        /// at a token end. Defer whenever an unquoted `$` is followed by an
        /// introducer within the token, OR sits at the token end (a following
        /// adjacent token would form the expansion; a truly trailing literal
        /// `$` defers too, which is harmless). The literal path cannot resolve
        /// any of these.
        size_t tlen = strlen(text);
        for (size_t i = 0; i < tlen; i++) {
            if (text[i] != '$' || prov[i] != QUOTE_PROV_UNQUOTED) {
                continue;
            }
            bool at_end = (i + 1 == tlen);
            char nx = at_end ? '\0' : text[i + 1];
            if (at_end || is_name_start(nx) || nx == '{' || nx == '(' ||
                is_special_param(nx)) {
                free(text);
                free(prov);
                *fully = false;
                return true;
            }
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
        /// begins with `$`. Decode the escapes at parse time into a WP_ANSIC
        /// leaf via lush's canonical decoder -- the SAME function, dialect, and
        /// interior bounds live lush's expand_arg_node uses, so wordtool and
        /// the live shell produce identical bytes wherever both decode. Two
        /// caveats keep that claim honest: live lush gates the decode on
        /// FEATURE_ANSI_QUOTING (off in POSIX mode, where it passes $'...'
        /// through literally); this bench runs only the default (feature-on)
        /// mode, so the POSIX-mode gate is a deferred concern for integration.
        /// And the last-byte terminator test below is a heuristic that lush's
        /// own guard shares (see the note there), not a full paired-quote scan.
        if (len >= 1 && raw[0] == '$') {
            /// Require the shape `$'` ... `'` by the same last-byte check
            /// expand_arg_node uses (str[len-1]=='\''). A truly unterminated
            /// $'... (ran to EOF, last byte not `'`) defers. This does NOT
            /// distinguish a real closing quote from an escaped trailing `\'`
            /// (`$'\'`), so such a token still decodes -- but live lush's
            /// identical guard decodes it too, so wordtool stays == live lush.
            if (len < 3 || raw[1] != '\'' || raw[len - 1] != '\'') {
                *fully = false;
                return true;
            }
            /// Interior = raw[2 .. len-1), stripping the `$'` prefix and the
            /// trailing `'`. Empty $'' -> zero-length interior.
            char *decoded =
                lush_expand_escapes(raw + 2, len - 3, LUSH_ESC_ANSI_C);
            if (!decoded) {
                return false; /// allocation failure
            }
            word_part_t *ac = word_part_new(WP_ANSIC, off, len);
            if (!ac) {
                free(decoded);
                return false;
            }
            ac->u.leaf.literal_meta = true; /// single-quote-like: globs literal
            ac->u.leaf.text = decoded;      /// decoded bytes (owned)
            if (!word_add_part(w, ac)) {
                word_part_free(ac);
                return false;
            }
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

    /// A word beginning with an unquoted `~` is subject to tilde expansion, a
    /// word-start construct this slice does not model. A quoted or escaped
    /// tilde starts with a quote/backslash byte, and a mid-word `~` is not at
    /// word start, so gating on the first raw byte defers exactly the expansion
    /// case (a literal `~nouser` legacy leaves alone is deferred too -- always
    /// safe).
    if (tok->input[word_start] == '~') {
        fully = false;
    }

    w->src_off = word_start;
    w->src_len = word_end - word_start;
    if (out_fully_handled) {
        *out_fully_handled = fully;
    }
    return w;
}
