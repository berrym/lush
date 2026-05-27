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

#include "lle/unicode_class.h"
#include "lle/utf8_support.h"
#include "shell_mode.h"

#include <ctype.h>

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

    /// Non-ASCII slow path. Only honoured when the feature is on;
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
    /// Continue accepts letters and digits; lle_unicode_is_alnum
    /// covers both via the same Unicode-aware category test.
    return lle_unicode_is_alnum(cp) ? (size_t)n : 0u;
}

bool lush_is_valid_identifier(const char *name) {
    if (!name || !*name) {
        return false;
    }
    size_t total = 0;
    while (name[total] != '\0') {
        total++;
    }

    size_t n = lush_ident_match_start(name, total);
    if (n == 0) {
        return false;
    }
    size_t pos = n;
    while (pos < total) {
        n = lush_ident_match_continue(name + pos, total - pos);
        if (n == 0) {
            return false;
        }
        pos += n;
    }
    return true;
}
