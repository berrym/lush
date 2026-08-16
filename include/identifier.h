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
 * NFC-normalized on ingest via lle_unicode_normalize_nfc_alloc, so
 * different Unicode encodings of the same user-visible identifier
 * collapse to one binding. The predicates themselves do not
 * normalize -- they only classify -- but they accept both NFC and
 * NFD forms.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_IDENTIFIER_H
#define LUSH_IDENTIFIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Codepoint-level identifier-Start test
 *
 * Single source of truth for "may this codepoint begin an identifier?"
 * lush_ident_match_start decodes a UTF-8 byte sequence and defers here;
 * codepoint-oriented scanners (the completion word-context analyzer,
 * which iterates decoded codepoints) call this directly. ASCII
 * [_A-Za-z] always; any Unicode letter when FEATURE_UNICODE_IDENTIFIERS
 * is on.
 *
 * @param cp Unicode codepoint
 * @return true if @p cp may start an identifier under the active mode
 */
bool lush_ident_is_start_cp(uint32_t cp);

/**
 * @brief Codepoint-level identifier-Continue test
 *
 * Like lush_ident_is_start_cp but also accepts digits and, under
 * FEATURE_UNICODE_IDENTIFIERS, the UAX #31 Continue combining marks
 * (grapheme Extend / SpacingMark) so NFD sequences extend the name.
 *
 * @param cp Unicode codepoint
 * @return true if @p cp may continue an identifier under the active mode
 */
bool lush_ident_is_continue_cp(uint32_t cp);

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
 * with `FEATURE_UNICODE_IDENTIFIERS` off rejects "cafe-acute" while a call
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
 * Lush stores identifier names in NFC form so that NFC-encoded `cafe-acute`
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

/**
 * @brief Detect whether an identifier mixes Unicode scripts
 *
 * A single identifier that draws letters from more than one script --
 * Latin `p` next to Cyrillic `U+0430` in `pU+0430sswd` -- is the classic
 * homograph vector: visually indistinguishable from a single-script
 * name to a human reading the source. This walks @p name's codepoints
 * (after NFC canonicalization) and reports whether two distinct scripts
 * appear. Script-neutral codepoints (ASCII digits, `_`) and combining
 * marks do not count as a script, so `cafe-acute`, `Sigma`, `U+0438 U+043C
 * U+044F`, and `x1_y` are all single-script.
 *
 * Detection only; it never rejects. Callers decide the posture: the
 * predictive analyzer surfaces it as an advisory, and a feature-matrix
 * flag can turn it into a definition-time rejection. The environment-
 * import path must NOT call this -- inherited names are external bytes,
 * not lush-authored identifiers.
 *
 * @param name NUL-terminated identifier (may be NULL -> returns false)
 * @param script_a If non-NULL and the name mixes scripts, set to the
 *                 first script's name (a stable string; do not free)
 * @param script_b If non-NULL and the name mixes scripts, set to the
 *                 conflicting script's name (stable string; do not free)
 * @return true if @p name mixes two or more scripts
 */
bool lush_ident_mixes_scripts(const char *name, const char **script_a,
                              const char **script_b);

#ifdef __cplusplus
}
#endif

#endif /// LUSH_IDENTIFIER_H
