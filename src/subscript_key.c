/**
 * @file subscript_key.c
 * @brief Canonicalize an array subscript's raw interior to its key.
 *
 * See subscript_key.h for the contract. The span of the `[...]` is found by
 * scan_subscript_bounds (brace_match.c); this file turns the raw interior into
 * the canonical associative key by composing the two shared primitives:
 * lush_dequote_span (one level of quote/escape removal + U/S/D/E provenance)
 * and expand_dequoted_key (provenance-driven `$`-expansion, tilde suppressed).
 * The bespoke quote-stripping state machine this file used to carry was folded
 * onto lush_dequote_span in the #631 quote-removal consolidation.
 */
#include "subscript_key.h"

#include <stdlib.h>

#include "dequote.h"

char *subscript_normalize_key(executor_t *executor, const char *interior,
                              size_t len) {
    if (!executor || !interior) {
        return NULL;
    }

    /// Step 1: strip one level of quoting, recording per-byte provenance so the
    /// expander protects single-quoted / escaped bytes and expands the rest.
    char *text = NULL, *prov = NULL;
    if (!lush_dequote_span(interior, len, &text, &prov, NULL, false)) {
        return NULL;
    }

    /// Step 2: apply `$`-expansion under that provenance. A subscript is a
    /// single-word key context: no word-splitting, no globbing, and no tilde
    /// expansion (the key uses a literal `~`).
    char *key = expand_dequoted_key(executor, text, prov);

    free(text);
    free(prov);
    return key;
}
