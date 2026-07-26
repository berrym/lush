/**
 * @file subscript_key.h
 * @brief Canonical normalization of an array subscript's interior to its key.
 *
 * Part of the #631 subscript-scanner consolidation. The span of a `[...]`
 * subscript is located by `scan_subscript_bounds` (brace_match.c); turning the
 * raw interior of that span into the string a program actually keys on is a
 * separate, executor-level concern handled here.
 *
 * The defect this resolves: lush disagreed with itself about a key's bytes. The
 * write store and the read extractor both applied only `$`-expansion to the raw
 * interior (never quote/escape removal), so a key written `m["a b"]=v` or
 * `m[a\ b]=v` was stored with its quotes/backslashes intact while a subsequent
 * `${m[a b]}` looked up the space-bearing form -- the two never matched.
 * Routing both directions through this one normalizer makes the round-trip
 * canonical.
 *
 * Scope note: the primitive in brace_match.c stays a pure span finder. This
 * normalizer lives at the executor level because canonicalization needs the
 * executor to perform `$`-expansion.
 */
#ifndef SUBSCRIPT_KEY_H
#define SUBSCRIPT_KEY_H

#include <stddef.h>

#include "executor.h"

/**
 * @brief Canonicalize the raw interior of an associative-array subscript.
 *
 * Given the raw bytes between `[` and `]` (as produced verbatim by the parser
 * write path or the read extractor -- both hand raw bytes), remove exactly one
 * level of quoting/escaping and perform `$`-expansion, with NO word-splitting
 * and NO globbing (a subscript is a single-word context). Segment by segment:
 *
 *   - `'...'` single-quoted: quotes removed; interior literal; no `$`-expansion
 *     (`m['$x']` keys on the literal `$x`).
 *   - `"..."` double-quoted: quotes removed; `$`/`${...}`/`$(...)`/`$((...))`
 *     expand; backslash is special only before `"`, `\`, `$`, `` ` ``.
 *   - `\X` unquoted escape: one backslash removed, `X` kept literally
 *     (`\ ` -> space, `\]` -> `]`, `\\` -> `\`).
 *   - bare `$name` / `${...}` / `$(...)` / `$((...))`: expanded.
 *   - anything else: copied literally.
 *
 * The operation is idempotent on already-canonical bytes: a key with no quote,
 * escape, or `$` normalizes to itself. That is what lets the write side (which
 * sees the raw quoted/escaped form) and the read side (which may see either the
 * raw form or a partially-processed one) converge on identical bytes.
 *
 * This does NOT decide key-vs-index: callers apply it only for associative
 * arrays. Indexed subscripts are arithmetic-evaluated on their own path.
 *
 * @param executor Executor context (used for `$`-expansion).
 * @param interior Raw subscript interior (need not be NUL-terminated at @p
 * len).
 * @param len      Number of bytes of @p interior to consider.
 * @return Newly allocated canonical key (caller frees); NULL only on OOM.
 */
char *subscript_normalize_key(executor_t *executor, const char *interior,
                              size_t len);

#endif /* SUBSCRIPT_KEY_H */
