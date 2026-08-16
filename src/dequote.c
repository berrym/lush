/**
 * @file dequote.c
 * @brief Canonical one-level quote removal with per-byte provenance.
 *
 * See dequote.h for the contract. This is a faithful, position-independent
 * extraction of the tokenizer quoted-string reader's dequoting semantics
 * (tokenizer.c Site 1): single/double/escape handling, the deferred
 * double-quote
 * `\X` representation, and `$(...)` / backtick verbatim copy. It deliberately
 * omits the tokenizer-only concerns (cursor column/line, adjacency-to-word
 * boundary, trailing glob-qualifier fusion, fused ANSI-C `$'...'`), which stay
 * with the tokenizer. A differential test (test_dequote.c) asserts
 * byte-for-byte parity with the reader on the in-scope corpus.
 */
#include "dequote.h"

#include <stdlib.h>
#include <string.h>

#include "brace_match.h"
#include "tokenizer.h" /// QUOTE_PROV_* byte values

/// Parallel text/prov/map builder. Grows the buffers in lockstep so out_prov
/// and out_map stay byte-aligned 1:1 with out_text.
///
/// The map is built only when a caller asks for it. Dequoting runs on every
/// quoted token, so a caller that does not need source positions should not
/// pay for a third array.
typedef struct {
    char *text;
    char *prov;
    size_t *map;
    size_t len;
    size_t cap;
    bool ok;
    bool want_map;
} dq_builder_t;

static void dq_reserve(dq_builder_t *b, size_t extra) {
    if (!b->ok) {
        return;
    }
    if (b->len + extra + 1 <= b->cap) {
        return;
    }
    size_t nc = b->cap ? b->cap : 32;
    while (b->len + extra + 1 > nc) {
        nc *= 2;
    }
    char *nt = realloc(b->text, nc);
    char *np = realloc(b->prov, nc);
    if (nt) {
        b->text = nt;
    }
    if (np) {
        b->prov = np;
    }
    if (!nt || !np) {
        b->ok = false;
        return;
    }
    if (b->want_map) {
        size_t *nm = realloc(b->map, nc * sizeof(*nm));
        if (!nm) {
            b->ok = false;
            return;
        }
        b->map = nm;
    }
    b->cap = nc;
}

/// Append `n` bytes of `src` all tagged with provenance `p`.
///
/// `src_off` is the offset in the RAW span of the first byte appended. Every
/// append below copies a CONTIGUOUS run of raw bytes, so byte k of the run
/// came from `src_off + k`. The one site that drops a byte -- the unquoted
/// `\X` escape, where the backslash is removed -- passes the offset of the
/// byte it keeps, which is what a consumer wants to point at.
static void dq_put(dq_builder_t *b, const char *src, size_t n, char p,
                   size_t src_off) {
    dq_reserve(b, n);
    if (!b->ok) {
        return;
    }
    memcpy(b->text + b->len, src, n);
    memset(b->prov + b->len, p, n);
    if (b->map) {
        for (size_t k = 0; k < n; k++) {
            b->map[b->len + k] = src_off + k;
        }
    }
    b->len += n;
    b->text[b->len] = '\0';
    b->prov[b->len] = '\0';
}

/// Append one byte tagged `p`, recording the raw offset it came from.
static void dq_putc(dq_builder_t *b, char c, char p, size_t src_off) {
    dq_put(b, &c, 1, p, src_off);
}

bool lush_dequote_span(const char *raw, size_t len, char **out_text,
                       char **out_prov, dequote_flags_t *out_flags) {
    return lush_dequote_span_mapped(raw, len, out_text, out_prov, out_flags,
                                    NULL);
}

bool lush_dequote_span_mapped(const char *raw, size_t len, char **out_text,
                              char **out_prov, dequote_flags_t *out_flags,
                              size_t **out_map) {
    if (out_text) {
        *out_text = NULL;
    }
    if (out_prov) {
        *out_prov = NULL;
    }
    if (out_map) {
        *out_map = NULL;
    }
    dequote_flags_t flags = {false, false, false};
    if (!raw || !out_text || !out_prov) {
        return false;
    }

    dq_builder_t b = {NULL, NULL, NULL, 0, 0, true, out_map != NULL};
    dq_reserve(&b, 0); /// seed the buffers so an empty span returns ""
    if (b.ok) {
        b.text[0] = '\0';
        b.prov[0] = '\0';
    }

    size_t i = 0;
    while (i < len && b.ok) {
        char c = raw[i];

        if (c == '\'') {
            /// Single-quoted: interior literal, no escapes, tagged S.
            flags.any_quoted = true;
            flags.any_single = true;
            i++; /// opening quote
            while (i < len && raw[i] != '\'') {
                dq_putc(&b, raw[i], QUOTE_PROV_SINGLE, i);
                i++;
            }
            if (i < len) {
                i++; /// closing quote
            }
        } else if (c == '"') {
            /// Double-quoted: interior tagged D; $(...) / `...` verbatim; `\X`
            /// kept (deferred); `\<newline>` removed.
            flags.any_quoted = true;
            flags.expandable = true;
            i++; /// opening quote
            while (i < len && raw[i] != '"') {
                if (raw[i] == '$' && i + 1 < len && raw[i + 1] == '(') {
                    /// Command substitution: copy $(...) verbatim via the
                    /// canonical matcher anchored at the '('.
                    size_t off = 0;
                    size_t end = lush_find_matching_brace(&raw[i + 1],
                                                          len - (i + 1), &off)
                                     ? (i + 1 + off + 1)
                                     : len;
                    dq_put(&b, &raw[i], end - i, QUOTE_PROV_DOUBLE, i);
                    i = end;
                } else if (raw[i] == '`') {
                    /// Backtick substitution: copy verbatim through the closing
                    /// backtick, honoring `\` inside.
                    size_t start = i;
                    i++; /// opening backtick
                    while (i < len && raw[i] != '`') {
                        if (raw[i] == '\\' && i + 1 < len) {
                            i++;
                        }
                        i++;
                    }
                    if (i < len) {
                        i++; /// closing backtick
                    }
                    dq_put(&b, &raw[start], i - start, QUOTE_PROV_DOUBLE,
                           start);
                } else if (raw[i] == '\\' && i + 1 < len) {
                    if (raw[i + 1] == '\n') {
                        i += 2; /// line continuation: drop both
                    } else {
                        /// Keep backslash + char (deferred DQ escape rules).
                        dq_put(&b, &raw[i], 2, QUOTE_PROV_DOUBLE, i);
                        i += 2;
                    }
                } else {
                    /// Regular char (incl. literal newline).
                    dq_putc(&b, raw[i], QUOTE_PROV_DOUBLE, i);
                    i++;
                }
            }
            if (i < len) {
                i++; /// closing quote
            }
        } else if (c == '\\' && i + 1 < len) {
            /// Unquoted escape: drop the backslash, keep the char tagged E;
            /// `\<newline>` line continuation removed. Unquoted content is
            /// expandable (matches the reader's has_expandable, which it sets
            /// for every character of an adjacent unquoted run).
            if (raw[i + 1] == '\n') {
                i += 2;
            } else {
                flags.expandable = true;
                dq_putc(&b, raw[i + 1], QUOTE_PROV_ESCAPED, i + 1);
                i += 2;
            }
        } else {
            /// Unquoted literal, tagged U. Any unquoted content marks the span
            /// expandable, matching the reader's has_expandable.
            flags.expandable = true;
            dq_putc(&b, c, QUOTE_PROV_UNQUOTED, i);
            i++;
        }
    }

    if (!b.ok) {
        free(b.text);
        free(b.prov);
        free(b.map);
        return false;
    }
    *out_text = b.text;
    *out_prov = b.prov;
    if (out_map) {
        *out_map = b.map;
    }
    if (out_flags) {
        *out_flags = flags;
    }
    return true;
}
