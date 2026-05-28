/**
 * @file identifier.c
 * @brief Shell identifier-syntax predicate implementation
 *
 * See include/identifier.h for the contract and the
 * FEATURE_UNICODE_IDENTIFIERS gating rationale.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "identifier.h"

#include "lle/grapheme_detector.h"
#include "lle/unicode_class.h"
#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"
#include "shell_mode.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Test whether an ASCII byte is a valid identifier-Start byte
 *
 * `_` plus the 52 ASCII letters. Matches POSIX
 * [A-Za-z_][A-Za-z0-9_]* on the Start position. This is the test
 * the previous ~30 sites did inline; centralising it here is the
 * point of the helper.
 */
static bool ascii_is_start(unsigned char b) {
    return b == '_' || (isalpha((int)b) != 0);
}

/**
 * @brief Test whether an ASCII byte is a valid identifier-Continue byte
 *
 * `_` plus the 52 ASCII letters plus the 10 ASCII digits.
 */
static bool ascii_is_continue(unsigned char b) {
    return b == '_' || (isalnum((int)b) != 0);
}

size_t lush_ident_match_start(const char *p, size_t remaining) {
    if (!p || remaining == 0) {
        return 0;
    }
    unsigned char b = (unsigned char)p[0];

    /// ASCII fast path. One byte test, no UTF-8 decode, no feature-
    /// flag lookup -- this is the common case and must not regress
    /// in cost vs the previous inline isalpha checks.
    if (b < 0x80) {
        return ascii_is_start(b) ? 1u : 0u;
    }

    /// Non-ASCII slow path. Only honored when the feature is on;
    /// when off, identifier syntax remains POSIX ASCII-only and any
    /// high byte terminates the identifier. The codepoint test goes
    /// through the LLE Unicode-alpha table (Latin Supplement, Latin
    /// Extended-A/B, IPA, Greek, Cyrillic, Cyrillic Supplement -- the
    /// scope the project's existing case table also covers).
    if (!shell_mode_allows(FEATURE_UNICODE_IDENTIFIERS)) {
        return 0;
    }
    uint32_t cp;
    int n = lle_utf8_decode_codepoint(p, remaining, &cp);
    if (n <= 0) {
        return 0;
    }
    return lle_unicode_is_alpha(cp) ? (size_t)n : 0u;
}

size_t lush_ident_match_continue(const char *p, size_t remaining) {
    if (!p || remaining == 0) {
        return 0;
    }
    unsigned char b = (unsigned char)p[0];

    if (b < 0x80) {
        return ascii_is_continue(b) ? 1u : 0u;
    }
    if (!shell_mode_allows(FEATURE_UNICODE_IDENTIFIERS)) {
        return 0;
    }
    uint32_t cp;
    int n = lle_utf8_decode_codepoint(p, remaining, &cp);
    if (n <= 0) {
        return 0;
    }
    /// Continue accepts letters and digits via the Unicode-aware
    /// category test...
    if (lle_unicode_is_alnum(cp)) {
        return (size_t)n;
    }
    /// ...and, per UAX #31 Continue, also accepts combining marks that
    /// extend a preceding base character. This is what lets an NFD-
    /// encoded identifier (e.g. `cafe` + U+0301 combining acute)
    /// tokenise as a single unit: the mark is pulled into the name
    /// token here, then the NFC-canonicalization at the storage /
    /// lookup boundary (lush_ident_canonicalize_alloc) collapses it to
    /// the same binding as the precomposed NFC form. Without this the
    /// scan stopped at the mark, truncating the name to "cafe" and
    /// missing the NFC binding entirely.
    ///
    /// GB_EXTEND covers nonspacing combining marks (Mn) -- the class
    /// NFD decomposition of Latin/Greek/Cyrillic produces -- and
    /// GB_SPACING_MARK covers spacing combining marks (Mc) used by
    /// Indic scripts. Marks are accepted only in Continue position;
    /// lush_ident_match_start never reaches this code for a mark
    /// because lle_unicode_is_alpha is false for the Mark category, so
    /// a bare leading diacritic correctly fails to start an identifier.
    grapheme_break_property_t gb = get_grapheme_break_property(cp);
    if (gb == GB_EXTEND || gb == GB_SPACING_MARK) {
        return (size_t)n;
    }
    return 0;
}

char *lush_ident_canonicalize_alloc(const char *name) {
    if (!name) {
        return NULL;
    }
    /// Pure ASCII inputs are NFC by construction, so the strdup
    /// fallback is the canonical form for them. The unicode normalizer
    /// also passes ASCII through unchanged, but the fallback keeps the
    /// fast path obvious and lets us skip the allocator dance when
    /// nothing could possibly need normalizing.
    bool any_high = false;
    for (const char *p = name; *p; p++) {
        if ((unsigned char)*p >= 0x80) {
            any_high = true;
            break;
        }
    }
    if (!any_high) {
        return strdup(name);
    }
    char *norm = lle_unicode_normalize_nfc_alloc(name);
    if (norm) {
        return norm;
    }
    /// Normalization failed (malformed UTF-8, alloc failure inside the
    /// normalizer). Fall back to the raw bytes so the caller can
    /// continue; downstream validation will still reject malformed
    /// sequences via the predicate.
    return strdup(name);
}

bool lush_is_valid_identifier(const char *name) {
    if (!name || !*name) {
        return false;
    }
    /// NFC-canonicalize before validating so an NFD-encoded name
    /// (letter + combining mark) is accepted on equal terms with the
    /// NFC-encoded equivalent (precomposed letter). The combining
    /// marks themselves are not in the project's Unicode-letter
    /// table -- they are Mark category, not Letter -- so without
    /// canonicalization NFD input would always fail the per-codepoint
    /// _continue test. Pure-ASCII names round-trip unchanged through
    /// the helper's any-high-byte fast path.
    char *canon = lush_ident_canonicalize_alloc(name);
    if (!canon) {
        return false;
    }
    size_t total = 0;
    while (canon[total] != '\0') {
        total++;
    }

    bool ok = false;
    size_t n = lush_ident_match_start(canon, total);
    if (n > 0) {
        size_t pos = n;
        ok = true;
        while (pos < total) {
            n = lush_ident_match_continue(canon + pos, total - pos);
            if (n == 0) {
                ok = false;
                break;
            }
            pos += n;
        }
    }
    free(canon);
    return ok;
}
