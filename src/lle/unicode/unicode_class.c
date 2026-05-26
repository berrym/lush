/**
 * @file unicode_class.c
 * @brief Unicode general-category classification implementation
 *
 * Implements the predicates declared in lle/unicode_class.h. ASCII
 * fast-paths use the standard C ctype predicates (well-defined and
 * fast on the universal subset); non-ASCII paths use explicit ranges
 * for the categories within the module's scope.
 *
 * The letter predicates piggy-back on the existing case-mapping
 * tables in unicode_case.c: any codepoint that has either an
 * uppercase or lowercase mapping is a letter by definition. This
 * keeps a single Unicode data source per concept (case mapping) and
 * avoids duplicating per-letter ranges.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/unicode_class.h"

#include "lle/unicode_case.h"

#include <ctype.h>

/* ============================================================================
 * Letter / alpha-numeric
 * ============================================================================
 */

bool lle_unicode_is_alpha(uint32_t cp) {
    if (cp < 0x80) {
        return isalpha((int)(unsigned char)cp) != 0;
    }
    /// Primary path: case-mapping tables identify Lu/Ll cleanly. A
    /// codepoint with either an uppercase or a lowercase mapping is
    /// a letter by definition.
    if (lle_unicode_is_upper(cp) || lle_unicode_is_lower(cp)) {
        return true;
    }
    /// Secondary path: blocks that are entirely letters but whose
    /// individual codepoints may not have entries in the simple
    /// case-mapping table (e.g. U+017F long s maps to ASCII S, which
    /// the table skips because the destination is the ASCII subset).
    ///
    /// Latin Extended-A (U+0100-U+017F): all 128 codepoints are letters.
    if (cp >= 0x0100 && cp <= 0x017F) {
        return true;
    }
    /// Latin Extended-B (U+0180-U+024F): mostly letters; a few non-
    /// letter codepoints exist (U+01BB phonetic, U+01C0-U+01C3 click
    /// letters which IS letters; this block is uniformly L category).
    if (cp >= 0x0180 && cp <= 0x024F) {
        return true;
    }
    /// IPA Extensions (U+0250-U+02AF): all letters.
    if (cp >= 0x0250 && cp <= 0x02AF) {
        return true;
    }
    /// Greek and Coptic (U+0370-U+03FF) letters. The block has some
    /// non-letter codepoints (combining diacritics, punctuation) but
    /// the main letter ranges are well-defined.
    if ((cp >= 0x0370 && cp <= 0x0373) || /// Greek capital heta and stigma
        (cp >= 0x0376 && cp <= 0x0377) || /// Greek capital pamphylian digamma
        (cp >= 0x037A && cp <= 0x037D) || /// Greek ypogegrammeni etc.
        cp == 0x037F ||                   /// Greek capital yot
        (cp >= 0x0386 && cp <= 0x03FF && cp != 0x0387 && cp != 0x0374 &&
         cp != 0x0375)) {
        return true;
    }
    /// Cyrillic (U+0400-U+04FF): mostly letters.
    if (cp >= 0x0400 && cp <= 0x04FF) {
        return true;
    }
    /// Cyrillic Supplement (U+0500-U+052F): all letters.
    if (cp >= 0x0500 && cp <= 0x052F) {
        return true;
    }
    return false;
}

bool lle_unicode_is_digit(uint32_t cp) {
    /// ASCII fast path
    if (cp >= '0' && cp <= '9') {
        return true;
    }
    if (cp < 0x80) {
        return false;
    }
    /// Common Unicode decimal-digit blocks (category Nd). Each block
    /// covers ten consecutive codepoints for digits 0-9 in that
    /// script. Listing the most frequently-encountered scripts;
    /// additional Nd ranges can be added without API change.
    static const uint32_t digit_block_starts[] = {
        0x0660, /// Arabic-Indic
        0x06F0, /// Extended Arabic-Indic
        0x07C0, /// NKo
        0x0966, /// Devanagari
        0x09E6, /// Bengali
        0x0A66, /// Gurmukhi
        0x0AE6, /// Gujarati
        0x0B66, /// Oriya
        0x0BE6, /// Tamil
        0x0C66, /// Telugu
        0x0CE6, /// Kannada
        0x0D66, /// Malayalam
        0x0DE6, /// Sinhala Lith
        0x0E50, /// Thai
        0x0ED0, /// Lao
        0x0F20, /// Tibetan
        0x1040, /// Myanmar
        0x1090, /// Myanmar Shan
        0x17E0, /// Khmer
        0x1810, /// Mongolian
        0x1946, /// Limbu
        0x19D0, /// New Tai Lue
        0x1A80, /// Tai Tham Hora
        0x1A90, /// Tai Tham Tham
        0x1B50, /// Balinese
        0x1BB0, /// Sundanese
        0x1C40, /// Lepcha
        0x1C50, /// Ol Chiki
        0xA620, /// Vai
        0xA8D0, /// Saurashtra
        0xA900, /// Kayah Li
        0xA9D0, /// Javanese
        0xA9F0, /// Myanmar Tai Laing
        0xAA50, /// Cham
        0xABF0, /// Meetei Mayek
        0xFF10, /// Fullwidth
    };
    size_t n = sizeof(digit_block_starts) / sizeof(digit_block_starts[0]);
    for (size_t i = 0; i < n; i++) {
        uint32_t start = digit_block_starts[i];
        if (cp >= start && cp <= start + 9) {
            return true;
        }
    }
    return false;
}

bool lle_unicode_is_alnum(uint32_t cp) {
    return lle_unicode_is_alpha(cp) || lle_unicode_is_digit(cp);
}

/* ============================================================================
 * Whitespace / blank
 * ============================================================================
 */

bool lle_unicode_is_space(uint32_t cp) {
    if (cp < 0x80) {
        return isspace((int)(unsigned char)cp) != 0;
    }
    /// Common Unicode whitespace beyond ASCII. Source: Unicode
    /// general categories Zs / Zl / Zp, plus the formatting-related
    /// whitespace codepoints used in real text.
    switch (cp) {
    case 0x00A0: /// NO-BREAK SPACE
    case 0x1680: /// OGHAM SPACE MARK
    case 0x2028: /// LINE SEPARATOR
    case 0x2029: /// PARAGRAPH SEPARATOR
    case 0x202F: /// NARROW NO-BREAK SPACE
    case 0x205F: /// MEDIUM MATHEMATICAL SPACE
    case 0x3000: /// IDEOGRAPHIC SPACE
        return true;
    default:
        break;
    }
    /// EN QUAD..HAIR SPACE range (U+2000-U+200A) -- all Zs members
    if (cp >= 0x2000 && cp <= 0x200A) {
        return true;
    }
    return false;
}

bool lle_unicode_is_blank(uint32_t cp) {
    /// POSIX-defined as ASCII space and tab; locale-invariant.
    return cp == ' ' || cp == '\t';
}

/* ============================================================================
 * Control / printable / graphical
 * ============================================================================
 */

bool lle_unicode_is_cntrl(uint32_t cp) {
    if (cp < 0x80) {
        return iscntrl((int)(unsigned char)cp) != 0;
    }
    /// C1 control range (Latin-1 Supplement controls)
    return cp >= 0x80 && cp <= 0x9F;
}

bool lle_unicode_is_print(uint32_t cp) {
    if (lle_unicode_is_cntrl(cp)) {
        return false;
    }
    /// Format / zero-width / direction-marker codepoints in the
    /// General Punctuation block are technically non-printing.
    /// Filter the common ones.
    if (cp >= 0x200B && cp <= 0x200F) { /// ZWSP..RLM
        return false;
    }
    if (cp >= 0x202A && cp <= 0x202E) { /// LRE..RLO
        return false;
    }
    if (cp >= 0x2060 && cp <= 0x2064) { /// WORD JOINER..INVISIBLE PLUS
        return false;
    }
    if (cp == 0xFEFF) { /// ZERO WIDTH NO-BREAK SPACE / BOM
        return false;
    }
    return true;
}

bool lle_unicode_is_graph(uint32_t cp) {
    return lle_unicode_is_print(cp) && !lle_unicode_is_space(cp);
}

/* ============================================================================
 * Punctuation
 * ============================================================================
 */

bool lle_unicode_is_punct(uint32_t cp) {
    if (cp < 0x80) {
        return ispunct((int)(unsigned char)cp) != 0;
    }
    /// Latin-1 Supplement punctuation
    switch (cp) {
    case 0x00A1: /// INVERTED EXCLAMATION MARK
    case 0x00A7: /// SECTION SIGN
    case 0x00AB: /// LEFT DOUBLE ANGLE QUOTATION
    case 0x00B6: /// PILCROW SIGN
    case 0x00B7: /// MIDDLE DOT
    case 0x00BB: /// RIGHT DOUBLE ANGLE QUOTATION
    case 0x00BF: /// INVERTED QUESTION MARK
        return true;
    default:
        break;
    }
    /// General Punctuation block (U+2000..U+206F) -- this is the
    /// block-level category Po / Pc / Pd / Ps / Pe / Pi / Pf. Most
    /// codepoints here are punctuation; exclude the spaces (already
    /// covered by is_space) and the zero-width / formatting (already
    /// excluded from is_print).
    if (cp >= 0x2010 && cp <= 0x2027) { /// hyphens..semicolon-equivalents
        return true;
    }
    if (cp >= 0x2030 && cp <= 0x205E) { /// per-mille..vertical-four-dots
        return true;
    }
    return false;
}

/* ============================================================================
 * Hex digit (POSIX ASCII)
 * ============================================================================
 */

bool lle_unicode_is_xdigit(uint32_t cp) {
    /// POSIX defines xdigit as exactly the ASCII hex alphabet,
    /// locale-invariant. Bash and zsh match.
    if (cp >= '0' && cp <= '9') {
        return true;
    }
    if (cp >= 'a' && cp <= 'f') {
        return true;
    }
    if (cp >= 'A' && cp <= 'F') {
        return true;
    }
    return false;
}
