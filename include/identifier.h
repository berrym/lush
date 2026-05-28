/**
 * @file identifier.h
 * @brief Shell identifier-syntax predicates
 *
 * Single canonical predicate for "is this byte / codepoint a valid
 * shell identifier character?" Replaces the ~30 isalpha/isalnum
 * byte-tests scattered across tokenizer, parser, executor, arithmetic,
 * and builtins. The predicates are feature-flag-aware:
 *
 *   FEATURE_UNICODE_IDENTIFIERS off (POSIX/bash/zsh default):
 *     Start    accepts [A-Za-z_]
 *     Continue accepts [A-Za-z0-9_]
 *
 *   FEATURE_UNICODE_IDENTIFIERS on (lush default; opt-in elsewhere):
 *     Start    accepts [A-Za-z_] or any Unicode letter
 *                      (per the LLE Unicode-letter table)
 *     Continue accepts [A-Za-z0-9_] or any Unicode letter / digit
 *                      (per the LLE Unicode-alnum table)
 *
 * The ASCII-byte fast path is taken in both modes; the Unicode slow
 * path runs only when the feature is on AND the byte at the cursor
 * is >= 0x80. Callers iterate by the byte length consumed (the
 * return value), so multi-byte UTF-8 sequences advance the cursor
 * correctly.
 *
 * Identifier names that come back from these predicates as a whole
 * string (e.g. assignment targets, function names) should be
 * NFC-normalised on ingest via lle_unicode_normalize_nfc_alloc, so
 * different Unicode encodings of the same user-visible identifier
 * collapse to one binding. The predicates themselves do not
 * normalise -- they only classify -- but they accept both NFC and
 * NFD forms.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_IDENTIFIER_H
#define LUSH_IDENTIFIER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Length in bytes of the identifier-Start codepoint at @p p, or 0
 *
 * Tests whether the byte sequence at @p p (with @p remaining bytes
 * available) starts a valid identifier-Start codepoint. Returns the
 * byte length consumed on match (1 for ASCII, 2-4 for multi-byte
 * UTF-8 in Unicode mode), 0 on no match.
 *
 * The fast path is the ASCII test: a single byte equal to '_' or
 * an ASCII letter returns 1 immediately, regardless of the feature
 * flag. The slow path (non-ASCII byte, FEATURE_UNICODE_IDENTIFIERS
 * on) decodes the UTF-8 codepoint and consults
 * lle_unicode_is_alpha.
 *
 * @param p Pointer to the byte sequence (must be valid for at
 *          least 1 byte, ideally more for multi-byte handling)
 * @param remaining Bytes available starting at @p p
 * @return Byte length consumed (>= 1) on identifier-Start match,
 *         0 on no match or NULL / zero-remaining input
 */
size_t lush_ident_match_start(const char *p, size_t remaining);

/**
 * @brief Length in bytes of the identifier-Continue codepoint at @p p, or 0
 *
 * Like lush_ident_match_start but also accepts digits (ASCII or
 * Unicode-aware).
 *
 * @param p Pointer to the byte sequence
 * @param remaining Bytes available starting at @p p
 * @return Byte length consumed (>= 1) on identifier-Continue match,
 *         0 on no match
 */
size_t lush_ident_match_continue(const char *p, size_t remaining);

/**
 * @brief Validate a NUL-terminated string as an identifier
 *
 * Walks @p name front-to-back applying lush_ident_match_start at
 * position 0 and lush_ident_match_continue thereafter. Returns true
 * if and only if the entire string is a valid identifier under the
 * currently-active feature-flag setting (so a call from POSIX mode
 * with `FEATURE_UNICODE_IDENTIFIERS` off rejects "café" while a call
 * from lush mode accepts it).
 *
 * Empty input or NULL returns false.
 *
 * @param name NUL-terminated candidate name
 * @return true if the entire string is a valid identifier
 */
bool lush_is_valid_identifier(const char *name);

/**
 * @brief Canonicalize @p name to NFC for storage and lookup
 *
 * Lush stores identifier names in NFC form so that NFC-encoded `café`
 * and NFD-encoded `cafe + combining-acute` collapse to one binding
 * (project-wide NFC-everywhere policy). This helper returns a malloc'd
 * NFC normalization of @p name; the caller frees.
 *
 * On any failure (NULL input, allocation failure, normalization
 * failure for malformed UTF-8), returns strdup(name) -- the original
 * bytes -- so the call site can continue without special-casing.
 * Returns NULL only if both normalization and the strdup fallback
 * fail to allocate.
 *
 * Pure ASCII inputs round-trip unchanged via NFC (every ASCII
 * codepoint is its own canonical form), so the helper is safe to
 * apply unconditionally without an ASCII fast path.
 *
 * @param name NUL-terminated identifier to canonicalize (may be NULL)
 * @return Newly-allocated NFC form (or strdup(name) fallback) or NULL
 *         only on total allocation failure
 */
char *lush_ident_canonicalize_alloc(const char *name);

#ifdef __cplusplus
}
#endif

#endif /// LUSH_IDENTIFIER_H
