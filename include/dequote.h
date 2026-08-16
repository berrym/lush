/**
 * @file dequote.h
 * @brief Canonical one-level quote removal with per-byte provenance.
 *
 * The single producer of lush's `(dequoted text, U/S/D/E provenance)` pair. The
 * provenance vocabulary (QUOTE_PROV_UNQUOTED/SINGLE/DOUBLE/ESCAPED, see
 * tokenizer.h) is the already-canonical data model from #495/#498; what was
 * missing was one function that turns a raw byte span INTO that pair. Several
 * subsystems each rolled their own (the tokenizer's quoted-string reader, the
 * parser's word dequoter, the subscript key normalizer, an expansion-time
 * stripper) -- this consolidates the operation, mirroring how brace_match.c
 * consolidated bracket scanning (#631).
 *
 * Scope: this removes ONE level of quoting and records where each surviving
 * byte's quoting came from. It does NOT expand `$`/backtick/arithmetic (that is
 * expand_quoted_string_prov's job, downstream), does NOT word-split or glob,
 * and does NOT decide whether the result is a subscript key or an arithmetic
 * index (caller policy). It is also position-independent: no tokenizer cursor,
 * column/line, adjacency-to-word-boundary, or glob-qualifier fusion -- those
 * stay the tokenizer's concern.
 */
#ifndef DEQUOTE_H
#define DEQUOTE_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Coarse facts about a dequoted span the caller would otherwise
 * recompute.
 */
typedef struct {
    bool any_quoted; /**< a `'` or `"` delimiter was removed */
    bool any_single; /**< a `'` delimiter was removed (single-quote protection)
                      */
    bool expandable; /**< double-quoted content, or any unquoted content, was
                      *   seen -- the token-type signal (EXPANDABLE vs literal).
                      *   Matches the tokenizer's has_expandable, which it sets
                      *   for a double segment and for every unquoted-run char.
                      */
} dequote_flags_t;

/**
 * @brief Remove one level of quoting from a raw span, recording provenance.
 *
 * Walks `[raw, raw+len)` as a sequence of unquoted runs and quoted segments and
 * builds the dequoted text with a parallel per-byte U/S/D/E map:
 *
 *   - `'...'`  single-quoted: interior copied literally, tagged S; no escapes.
 *   - `"..."`  double-quoted: interior tagged D; `$(...)` and `` `...` ``
 * copied verbatim; `\X` KEPT as backslash+char (deferred for the double-quote
 * expander); `\<newline>` line continuation removed.
 *   - `\X`     unquoted escape: backslash DROPPED, `X` kept, tagged E;
 *              `\<newline>` line continuation removed.
 *   - anything else: copied literally, tagged U.
 *
 * The double-quote `\X`-kept vs unquoted `\X`-dropped asymmetry is deliberate
 * and matches the tokenizer/expander contract: expand_quoted_string_prov
 * applies the remaining double-quote backslash rules, while an unquoted `\X` is
 * already final. Trailing zsh glob-qualifier groups are NOT handled here (a
 * tokenizer-adjacency concern). ANSI-C `$'...'` is NOT modeled either: a bare
 * `$'x'` is treated as an unquoted `$` followed by a single-quoted `x`, not as
 * an escape-decoded C string -- so a caller that may see `$'...'` (e.g. a
 * future subscript path) must handle or reject it before calling here.
 *
 * @param raw       Raw span (need not be NUL-terminated at @p len).
 * @param len       Number of bytes of @p raw to process.
 * @param out_text  Receives the malloc'd, NUL-terminated dequoted text.
 * @param out_prov  Receives the malloc'd U/S/D/E map, byte-aligned 1:1 with
 *                  @p out_text (NOT NUL-terminated to its own length; a
 * trailing NUL is written for convenience).
 * @param out_flags Optional; receives the coarse facts. May be NULL.
 * @return true on success (both outputs owned by caller); false on OOM (outputs
 *         set to NULL).
 */
bool lush_dequote_span(const char *raw, size_t len, char **out_text,
                       char **out_prov, dequote_flags_t *out_flags);

/**
 * @brief lush_dequote_span, plus a raw-offset position map.
 *
 * Identical in every respect to lush_dequote_span -- which is now a wrapper
 * that passes NULL here -- except that it can also report where each output
 * byte came from.
 *
 * Dequoting DELETES bytes: quote delimiters, the unquoted `\X` backslash, and
 * `\<newline>`. Output offsets therefore do not map linearly back to the raw
 * span and cannot be recovered by arithmetic on the result. A consumer holding
 * a slice of the dequoted text needs this map to name the raw bytes it came
 * from, which is what a source span on a Word CST part must be absolute
 * against (issue #701).
 *
 * The map records POSITION only. It is not a second provenance channel and
 * says nothing about quote context; @p out_prov remains the single answer to
 * that question.
 *
 * @param out_map Optional; receives a malloc'd array of @p raw offsets, one
 *                per output byte, aligned 1:1 with @p out_text: entry j is the
 *                offset in @p raw of the byte that produced out_text[j]. Only
 *                the first strlen(*out_text) entries are meaningful -- there is
 *                no terminator, because 0 is a legitimate offset. May be NULL,
 *                in which case the map is neither built nor allocated, and the
 *                call costs exactly what lush_dequote_span costs.
 * @return true on success (outputs owned by caller); false on OOM.
 */
bool lush_dequote_span_mapped(const char *raw, size_t len, char **out_text,
                              char **out_prov, dequote_flags_t *out_flags,
                              size_t **out_map);

#endif /* DEQUOTE_H */
