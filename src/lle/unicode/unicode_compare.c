/**
 * @file unicode_compare.c
 * @brief Unicode-aware String Comparison Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements NFC normalization and Unicode-aware string comparison.
 * Uses lookup tables for common Latin characters with diacritics.
 *
 * For full Unicode coverage, a complete implementation would need the
 * Unicode Character Database (UCD), but for shell command comparison
 * we focus on Latin-1 Supplement and Latin Extended-A/B which cover
 * the vast majority of real-world cases.
 */

#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"
#include <ctype.h>
#include <string.h>

/// Default comparison options
const lle_unicode_compare_options_t LLE_UNICODE_COMPARE_DEFAULT = {
    .normalize = true, .case_insensitive = false, .trim_whitespace = false};

/// Strict byte comparison
const lle_unicode_compare_options_t LLE_UNICODE_COMPARE_STRICT = {
    .normalize = false, .case_insensitive = false, .trim_whitespace = false};

/* ============================================================================
 * UNICODE DATA TABLES
 *
 * These tables cover common characters that have canonical decompositions.
 * Focus on Latin-1 Supplement (U+0080-U+00FF) and Latin Extended-A
 * (U+0100-U+017F) which covers most European languages.
 * ============================================================================
 */

/**
 * @brief Canonical decomposition table entry
 *
 * Maps a precomposed codepoint to its canonical decomposition
 * (base character + combining mark).
 */
typedef struct {
    uint32_t composed;  ///< Precomposed codepoint
    uint32_t base;      ///< Base character
    uint32_t combining; ///< Combining character (0 if single)
} decomposition_entry_t;

/**
 * @brief Canonical decomposition mappings for common Latin characters
 *
 * Sorted by composed codepoint for binary search lookup.
 * Covers Latin-1 Supplement and Latin Extended-A.
 */
static const decomposition_entry_t decomposition_table[] = {
    /// Latin-1 Supplement precomposed characters
    {0x00C0, 0x0041, 0x0300}, /// À = A + grave
    {0x00C1, 0x0041, 0x0301}, /// Á = A + acute
    {0x00C2, 0x0041, 0x0302}, /// Â = A + circumflex
    {0x00C3, 0x0041, 0x0303}, /// Ã = A + tilde
    {0x00C4, 0x0041, 0x0308}, /// Ä = A + diaeresis
    {0x00C5, 0x0041, 0x030A}, /// Å = A + ring above
    {0x00C7, 0x0043, 0x0327}, /// Ç = C + cedilla
    {0x00C8, 0x0045, 0x0300}, /// È = E + grave
    {0x00C9, 0x0045, 0x0301}, /// É = E + acute
    {0x00CA, 0x0045, 0x0302}, /// Ê = E + circumflex
    {0x00CB, 0x0045, 0x0308}, /// Ë = E + diaeresis
    {0x00CC, 0x0049, 0x0300}, /// Ì = I + grave
    {0x00CD, 0x0049, 0x0301}, /// Í = I + acute
    {0x00CE, 0x0049, 0x0302}, /// Î = I + circumflex
    {0x00CF, 0x0049, 0x0308}, /// Ï = I + diaeresis
    {0x00D1, 0x004E, 0x0303}, /// Ñ = N + tilde
    {0x00D2, 0x004F, 0x0300}, /// Ò = O + grave
    {0x00D3, 0x004F, 0x0301}, /// Ó = O + acute
    {0x00D4, 0x004F, 0x0302}, /// Ô = O + circumflex
    {0x00D5, 0x004F, 0x0303}, /// Õ = O + tilde
    {0x00D6, 0x004F, 0x0308}, /// Ö = O + diaeresis
    {0x00D9, 0x0055, 0x0300}, /// Ù = U + grave
    {0x00DA, 0x0055, 0x0301}, /// Ú = U + acute
    {0x00DB, 0x0055, 0x0302}, /// Û = U + circumflex
    {0x00DC, 0x0055, 0x0308}, /// Ü = U + diaeresis
    {0x00DD, 0x0059, 0x0301}, /// Ý = Y + acute
    {0x00E0, 0x0061, 0x0300}, /// à = a + grave
    {0x00E1, 0x0061, 0x0301}, /// á = a + acute
    {0x00E2, 0x0061, 0x0302}, /// â = a + circumflex
    {0x00E3, 0x0061, 0x0303}, /// ã = a + tilde
    {0x00E4, 0x0061, 0x0308}, /// ä = a + diaeresis
    {0x00E5, 0x0061, 0x030A}, /// å = a + ring above
    {0x00E7, 0x0063, 0x0327}, /// ç = c + cedilla
    {0x00E8, 0x0065, 0x0300}, /// è = e + grave
    {0x00E9, 0x0065, 0x0301}, /// é = e + acute
    {0x00EA, 0x0065, 0x0302}, /// ê = e + circumflex
    {0x00EB, 0x0065, 0x0308}, /// ë = e + diaeresis
    {0x00EC, 0x0069, 0x0300}, /// ì = i + grave
    {0x00ED, 0x0069, 0x0301}, /// í = i + acute
    {0x00EE, 0x0069, 0x0302}, /// î = i + circumflex
    {0x00EF, 0x0069, 0x0308}, /// ï = i + diaeresis
    {0x00F1, 0x006E, 0x0303}, /// ñ = n + tilde
    {0x00F2, 0x006F, 0x0300}, /// ò = o + grave
    {0x00F3, 0x006F, 0x0301}, /// ó = o + acute
    {0x00F4, 0x006F, 0x0302}, /// ô = o + circumflex
    {0x00F5, 0x006F, 0x0303}, /// õ = o + tilde
    {0x00F6, 0x006F, 0x0308}, /// ö = o + diaeresis
    {0x00F9, 0x0075, 0x0300}, /// ù = u + grave
    {0x00FA, 0x0075, 0x0301}, /// ú = u + acute
    {0x00FB, 0x0075, 0x0302}, /// û = u + circumflex
    {0x00FC, 0x0075, 0x0308}, /// ü = u + diaeresis
    {0x00FD, 0x0079, 0x0301}, /// ý = y + acute
    {0x00FF, 0x0079, 0x0308}, /// ÿ = y + diaeresis

    /// Latin Extended-A
    {0x0100, 0x0041, 0x0304}, /// Ā = A + macron
    {0x0101, 0x0061, 0x0304}, /// ā = a + macron
    {0x0102, 0x0041, 0x0306}, /// Ă = A + breve
    {0x0103, 0x0061, 0x0306}, /// ă = a + breve
    {0x0104, 0x0041, 0x0328}, /// Ą = A + ogonek
    {0x0105, 0x0061, 0x0328}, /// ą = a + ogonek
    {0x0106, 0x0043, 0x0301}, /// Ć = C + acute
    {0x0107, 0x0063, 0x0301}, /// ć = c + acute
    {0x0108, 0x0043, 0x0302}, /// Ĉ = C + circumflex
    {0x0109, 0x0063, 0x0302}, /// ĉ = c + circumflex
    {0x010A, 0x0043, 0x0307}, /// Ċ = C + dot above
    {0x010B, 0x0063, 0x0307}, /// ċ = c + dot above
    {0x010C, 0x0043, 0x030C}, /// Č = C + caron
    {0x010D, 0x0063, 0x030C}, /// č = c + caron
    {0x010E, 0x0044, 0x030C}, /// Ď = D + caron
    {0x010F, 0x0064, 0x030C}, /// ď = d + caron
    {0x0112, 0x0045, 0x0304}, /// Ē = E + macron
    {0x0113, 0x0065, 0x0304}, /// ē = e + macron
    {0x0114, 0x0045, 0x0306}, /// Ĕ = E + breve
    {0x0115, 0x0065, 0x0306}, /// ĕ = e + breve
    {0x0116, 0x0045, 0x0307}, /// Ė = E + dot above
    {0x0117, 0x0065, 0x0307}, /// ė = e + dot above
    {0x0118, 0x0045, 0x0328}, /// Ę = E + ogonek
    {0x0119, 0x0065, 0x0328}, /// ę = e + ogonek
    {0x011A, 0x0045, 0x030C}, /// Ě = E + caron
    {0x011B, 0x0065, 0x030C}, /// ě = e + caron
    {0x011C, 0x0047, 0x0302}, /// Ĝ = G + circumflex
    {0x011D, 0x0067, 0x0302}, /// ĝ = g + circumflex
    {0x011E, 0x0047, 0x0306}, /// Ğ = G + breve
    {0x011F, 0x0067, 0x0306}, /// ğ = g + breve
    {0x0120, 0x0047, 0x0307}, /// Ġ = G + dot above
    {0x0121, 0x0067, 0x0307}, /// ġ = g + dot above
    {0x0122, 0x0047, 0x0327}, /// Ģ = G + cedilla
    {0x0123, 0x0067, 0x0327}, /// ģ = g + cedilla
    {0x0124, 0x0048, 0x0302}, /// Ĥ = H + circumflex
    {0x0125, 0x0068, 0x0302}, /// ĥ = h + circumflex
    {0x0128, 0x0049, 0x0303}, /// Ĩ = I + tilde
    {0x0129, 0x0069, 0x0303}, /// ĩ = i + tilde
    {0x012A, 0x0049, 0x0304}, /// Ī = I + macron
    {0x012B, 0x0069, 0x0304}, /// ī = i + macron
    {0x012C, 0x0049, 0x0306}, /// Ĭ = I + breve
    {0x012D, 0x0069, 0x0306}, /// ĭ = i + breve
    {0x012E, 0x0049, 0x0328}, /// Į = I + ogonek
    {0x012F, 0x0069, 0x0328}, /// į = i + ogonek
    {0x0130, 0x0049, 0x0307}, /// İ = I + dot above
    {0x0134, 0x004A, 0x0302}, /// Ĵ = J + circumflex
    {0x0135, 0x006A, 0x0302}, /// ĵ = j + circumflex
    {0x0136, 0x004B, 0x0327}, /// Ķ = K + cedilla
    {0x0137, 0x006B, 0x0327}, /// ķ = k + cedilla
    {0x0139, 0x004C, 0x0301}, /// Ĺ = L + acute
    {0x013A, 0x006C, 0x0301}, /// ĺ = l + acute
    {0x013B, 0x004C, 0x0327}, /// Ļ = L + cedilla
    {0x013C, 0x006C, 0x0327}, /// ļ = l + cedilla
    {0x013D, 0x004C, 0x030C}, /// Ľ = L + caron
    {0x013E, 0x006C, 0x030C}, /// ľ = l + caron
    {0x0143, 0x004E, 0x0301}, /// Ń = N + acute
    {0x0144, 0x006E, 0x0301}, /// ń = n + acute
    {0x0145, 0x004E, 0x0327}, /// Ņ = N + cedilla
    {0x0146, 0x006E, 0x0327}, /// ņ = n + cedilla
    {0x0147, 0x004E, 0x030C}, /// Ň = N + caron
    {0x0148, 0x006E, 0x030C}, /// ň = n + caron
    {0x014C, 0x004F, 0x0304}, /// Ō = O + macron
    {0x014D, 0x006F, 0x0304}, /// ō = o + macron
    {0x014E, 0x004F, 0x0306}, /// Ŏ = O + breve
    {0x014F, 0x006F, 0x0306}, /// ŏ = o + breve
    {0x0150, 0x004F, 0x030B}, /// Ő = O + double acute
    {0x0151, 0x006F, 0x030B}, /// ő = o + double acute
    {0x0154, 0x0052, 0x0301}, /// Ŕ = R + acute
    {0x0155, 0x0072, 0x0301}, /// ŕ = r + acute
    {0x0156, 0x0052, 0x0327}, /// Ŗ = R + cedilla
    {0x0157, 0x0072, 0x0327}, /// ŗ = r + cedilla
    {0x0158, 0x0052, 0x030C}, /// Ř = R + caron
    {0x0159, 0x0072, 0x030C}, /// ř = r + caron
    {0x015A, 0x0053, 0x0301}, /// Ś = S + acute
    {0x015B, 0x0073, 0x0301}, /// ś = s + acute
    {0x015C, 0x0053, 0x0302}, /// Ŝ = S + circumflex
    {0x015D, 0x0073, 0x0302}, /// ŝ = s + circumflex
    {0x015E, 0x0053, 0x0327}, /// Ş = S + cedilla
    {0x015F, 0x0073, 0x0327}, /// ş = s + cedilla
    {0x0160, 0x0053, 0x030C}, /// Š = S + caron
    {0x0161, 0x0073, 0x030C}, /// š = s + caron
    {0x0162, 0x0054, 0x0327}, /// Ţ = T + cedilla
    {0x0163, 0x0074, 0x0327}, /// ţ = t + cedilla
    {0x0164, 0x0054, 0x030C}, /// Ť = T + caron
    {0x0165, 0x0074, 0x030C}, /// ť = t + caron
    {0x0168, 0x0055, 0x0303}, /// Ũ = U + tilde
    {0x0169, 0x0075, 0x0303}, /// ũ = u + tilde
    {0x016A, 0x0055, 0x0304}, /// Ū = U + macron
    {0x016B, 0x0075, 0x0304}, /// ū = u + macron
    {0x016C, 0x0055, 0x0306}, /// Ŭ = U + breve
    {0x016D, 0x0075, 0x0306}, /// ŭ = u + breve
    {0x016E, 0x0055, 0x030A}, /// Ů = U + ring above
    {0x016F, 0x0075, 0x030A}, /// ů = u + ring above
    {0x0170, 0x0055, 0x030B}, /// Ű = U + double acute
    {0x0171, 0x0075, 0x030B}, /// ű = u + double acute
    {0x0172, 0x0055, 0x0328}, /// Ų = U + ogonek
    {0x0173, 0x0075, 0x0328}, /// ų = u + ogonek
    {0x0174, 0x0057, 0x0302}, /// Ŵ = W + circumflex
    {0x0175, 0x0077, 0x0302}, /// ŵ = w + circumflex
    {0x0176, 0x0059, 0x0302}, /// Ŷ = Y + circumflex
    {0x0177, 0x0079, 0x0302}, /// ŷ = y + circumflex
    {0x0178, 0x0059, 0x0308}, /// Ÿ = Y + diaeresis
    {0x0179, 0x005A, 0x0301}, /// Ź = Z + acute
    {0x017A, 0x007A, 0x0301}, /// ź = z + acute
    {0x017B, 0x005A, 0x0307}, /// Ż = Z + dot above
    {0x017C, 0x007A, 0x0307}, /// ż = z + dot above
    {0x017D, 0x005A, 0x030C}, /// Ž = Z + caron
    {0x017E, 0x007A, 0x030C}, /// ž = z + caron
};

static const size_t decomposition_table_size =
    sizeof(decomposition_table) / sizeof(decomposition_table[0]);

/**
 * @brief Canonical combining class table entry
 */
typedef struct {
    uint32_t codepoint;
    uint8_t ccc; ///< Canonical Combining Class
} combining_class_entry_t;

static const combining_class_entry_t combining_class_table[] = {
    {0x0300, 230}, /// Combining grave accent
    {0x0301, 230}, /// Combining acute accent
    {0x0302, 230}, /// Combining circumflex
    {0x0303, 230}, /// Combining tilde
    {0x0304, 230}, /// Combining macron
    {0x0305, 230}, /// Combining overline
    {0x0306, 230}, /// Combining breve
    {0x0307, 230}, /// Combining dot above
    {0x0308, 230}, /// Combining diaeresis
    {0x0309, 230}, /// Combining hook above
    {0x030A, 230}, /// Combining ring above
    {0x030B, 230}, /// Combining double acute
    {0x030C, 230}, /// Combining caron
    {0x030D, 230}, /// Combining vertical line above
    {0x030E, 230}, /// Combining double vertical line above
    {0x030F, 230}, /// Combining double grave
    {0x0310, 230}, /// Combining candrabindu
    {0x0311, 230}, /// Combining inverted breve
    {0x0312, 230}, /// Combining turned comma above
    {0x0313, 230}, /// Combining comma above
    {0x0314, 230}, /// Combining reversed comma above
    {0x0315, 232}, /// Combining comma above right
    {0x0316, 220}, /// Combining grave below
    {0x0317, 220}, /// Combining acute below
    {0x0318, 220}, /// Combining left tack below
    {0x0319, 220}, /// Combining right tack below
    {0x031A, 232}, /// Combining left angle above
    {0x031B, 216}, /// Combining horn
    {0x031C, 220}, /// Combining left half ring below
    {0x031D, 220}, /// Combining up tack below
    {0x031E, 220}, /// Combining down tack below
    {0x031F, 220}, /// Combining plus sign below
    {0x0320, 220}, /// Combining minus sign below
    {0x0321, 202}, /// Combining palatalized hook below
    {0x0322, 202}, /// Combining retroflex hook below
    {0x0323, 220}, /// Combining dot below
    {0x0324, 220}, /// Combining diaeresis below
    {0x0325, 220}, /// Combining ring below
    {0x0326, 220}, /// Combining comma below
    {0x0327, 202}, /// Combining cedilla
    {0x0328, 202}, /// Combining ogonek
    {0x0329, 220}, /// Combining vertical line below
    {0x032A, 220}, /// Combining bridge below
    {0x032B, 220}, /// Combining inverted double arch below
    {0x032C, 220}, /// Combining caron below
    {0x032D, 220}, /// Combining circumflex below
    {0x032E, 220}, /// Combining breve below
    {0x032F, 220}, /// Combining inverted breve below
    {0x0330, 220}, /// Combining tilde below
    {0x0331, 220}, /// Combining macron below
    {0x0332, 220}, /// Combining low line
    {0x0333, 220}, /// Combining double low line
    {0x0334,   1}, /// Combining tilde overlay
    {0x0335,   1}, /// Combining short stroke overlay
    {0x0336,   1}, /// Combining long stroke overlay
    {0x0337,   1}, /// Combining short solidus overlay
    {0x0338,   1}, /// Combining long solidus overlay
    {0x0339, 220}, /// Combining right half ring below
    {0x033A, 220}, /// Combining inverted bridge below
    {0x033B, 220}, /// Combining square below
    {0x033C, 220}, /// Combining seagull below
    {0x033D, 230}, /// Combining x above
    {0x033E, 230}, /// Combining vertical tilde
    {0x033F, 230}, /// Combining double overline
    {0x0340, 230}, /// Combining grave tone mark
    {0x0341, 230}, /// Combining acute tone mark
    {0x0342, 230}, /// Combining Greek perispomeni
    {0x0343, 230}, /// Combining Greek koronis
    {0x0344, 230}, /// Combining Greek dialytika tonos
    {0x0345, 240}, /// Combining Greek ypogegrammeni
};

static const size_t combining_class_table_size =
    sizeof(combining_class_table) / sizeof(combining_class_table[0]);

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Binary search for decomposition entry
 * @param codepoint The codepoint to look up
 * @return Pointer to decomposition entry, or NULL if not found
 */
static const decomposition_entry_t *find_decomposition(uint32_t codepoint) {
    size_t left = 0;
    size_t right = decomposition_table_size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (decomposition_table[mid].composed < codepoint) {
            left = mid + 1;
        } else if (decomposition_table[mid].composed > codepoint) {
            right = mid;
        } else {
            return &decomposition_table[mid];
        }
    }

    return NULL;
}

/**
 * @brief Find composition (reverse lookup from base + combining to composed)
 * @param base The base character
 * @param combining The combining mark
 * @param composed Output: the precomposed character
 * @return true if composition found, false otherwise
 */
static bool find_composition(uint32_t base, uint32_t combining,
                             uint32_t *composed) {
    /// Linear search for composition (could optimize with hash table)
    for (size_t i = 0; i < decomposition_table_size; i++) {
        if (decomposition_table[i].base == base &&
            decomposition_table[i].combining == combining) {
            *composed = decomposition_table[i].composed;
            return true;
        }
    }
    return false;
}

/**
 * @brief Convert codepoint to lowercase (simple case folding)
 * @param cp The codepoint to convert
 * @return Lowercase equivalent, or original if no conversion
 */
static uint32_t to_lowercase(uint32_t cp) {
    /// ASCII range
    if (cp >= 'A' && cp <= 'Z') {
        return cp + ('a' - 'A');
    }

    /// Latin-1 Supplement uppercase
    if (cp >= 0x00C0 && cp <= 0x00D6) {
        return cp + 0x20;
    }
    if (cp >= 0x00D8 && cp <= 0x00DE) {
        return cp + 0x20;
    }

    /// Latin Extended-A pairs (most are sequential uppercase/lowercase)
    if (cp >= 0x0100 && cp <= 0x0137 && (cp & 1) == 0) {
        return cp + 1;
    }
    if (cp >= 0x0139 && cp <= 0x0148 && (cp & 1) == 1) {
        return cp + 1;
    }
    if (cp >= 0x014A && cp <= 0x0177 && (cp & 1) == 0) {
        return cp + 1;
    }
    if (cp >= 0x0179 && cp <= 0x017E && (cp & 1) == 1) {
        return cp + 1;
    }

    return cp;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Check if a codepoint is a combining character
 * @param codepoint The codepoint to check
 * @return true if combining mark, false otherwise
 */
bool lle_unicode_is_combining(uint32_t codepoint) {
    /// Combining Diacritical Marks (0300-036F)
    if (codepoint >= 0x0300 && codepoint <= 0x036F) {
        return true;
    }
    /// Combining Diacritical Marks Extended (1AB0-1AFF)
    if (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) {
        return true;
    }
    /// Combining Diacritical Marks Supplement (1DC0-1DFF)
    if (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) {
        return true;
    }
    /// Combining Diacritical Marks for Symbols (20D0-20FF)
    if (codepoint >= 0x20D0 && codepoint <= 0x20FF) {
        return true;
    }
    /// Combining Half Marks (FE20-FE2F)
    if (codepoint >= 0xFE20 && codepoint <= 0xFE2F) {
        return true;
    }
    return false;
}

/**
 * @brief Get the canonical combining class for a codepoint
 * @param codepoint The codepoint to look up
 * @return Combining class (0 for base characters, >0 for combining marks)
 */
uint8_t lle_unicode_combining_class(uint32_t codepoint) {
    /// Binary search in combining class table
    size_t left = 0;
    size_t right = combining_class_table_size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (combining_class_table[mid].codepoint < codepoint) {
            left = mid + 1;
        } else if (combining_class_table[mid].codepoint > codepoint) {
            right = mid;
        } else {
            return combining_class_table[mid].ccc;
        }
    }

    /// Default: 0 for base characters, 230 for combining marks
    if (lle_unicode_is_combining(codepoint)) {
        return 230; /// Default above class
    }
    return 0; /// Starter
}

/**
 * @brief Decompose a codepoint to its canonical form
 * @param codepoint The codepoint to decompose
 * @param decomposed Output array for decomposed codepoints
 * @param max_length Maximum entries in output array
 * @return Number of codepoints written, or 0 if no decomposition
 */
int lle_unicode_decompose(uint32_t codepoint, uint32_t *decomposed,
                          int max_length) {
    if (!decomposed || max_length < 1) {
        return 0;
    }

    const decomposition_entry_t *entry = find_decomposition(codepoint);
    if (!entry) {
        return 0; /// No decomposition
    }

    decomposed[0] = entry->base;
    if (entry->combining != 0 && max_length >= 2) {
        decomposed[1] = entry->combining;
        return 2;
    }

    return 1;
}

/**
 * @brief Compose a base character and combining mark into precomposed form
 * @param base The base character
 * @param combining The combining mark
 * @param composed Output: the precomposed character
 * @return true if composition exists, false otherwise
 */
bool lle_unicode_compose(uint32_t base, uint32_t combining,
                         uint32_t *composed) {
    if (!composed) {
        return false;
    }
    return find_composition(base, combining, composed);
}

/**
 * @brief Normalize a UTF-8 string to NFC form
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer for normalized string
 * @param output_size Size of output buffer
 * @param output_len Output: length of normalized string
 * @return 0 on success, -1 for invalid params, -2 buffer too small, -3 invalid
 * UTF-8
 */
int lle_unicode_normalize_nfc(const char *input, size_t input_len, char *output,
                              size_t output_size, size_t *output_len) {
    if (!input || !output || output_size == 0) {
        return -1;
    }

/// Step 1: Decompose to NFD (canonical decomposition)
#define MAX_DECOMPOSED 256
    uint32_t decomposed[MAX_DECOMPOSED];
    size_t decomposed_count = 0;

    const char *ptr = input;
    const char *end = input + input_len;

    while (ptr < end && decomposed_count < MAX_DECOMPOSED - 4) {
        uint32_t codepoint;
        int seq_len = lle_utf8_decode_codepoint(ptr, end - ptr, &codepoint);
        if (seq_len <= 0) {
            return -3; /// Invalid UTF-8
        }

        /// Try to decompose
        uint32_t parts[4];
        int parts_count = lle_unicode_decompose(codepoint, parts, 4);

        if (parts_count > 0) {
            /// Has decomposition
            for (int i = 0;
                 i < parts_count && decomposed_count < MAX_DECOMPOSED; i++) {
                decomposed[decomposed_count++] = parts[i];
            }
        } else {
            /// No decomposition, keep original
            decomposed[decomposed_count++] = codepoint;
        }

        ptr += seq_len;
    }

    /// Step 2: Canonical ordering of combining marks
    for (size_t i = 1; i < decomposed_count; i++) {
        uint8_t ccc = lle_unicode_combining_class(decomposed[i]);
        if (ccc == 0)
            continue; /// Starter, don't reorder

        size_t j = i;
        while (j > 0) {
            uint8_t prev_ccc = lle_unicode_combining_class(decomposed[j - 1]);
            if (prev_ccc == 0 || prev_ccc <= ccc) {
                break; /// Correct order
            }
            /// Swap
            uint32_t tmp = decomposed[j];
            decomposed[j] = decomposed[j - 1];
            decomposed[j - 1] = tmp;
            j--;
        }
    }

    /// Step 3: Compose (NFC)
    size_t composed_count = 0;
    uint32_t composed[MAX_DECOMPOSED];

    for (size_t i = 0; i < decomposed_count; i++) {
        uint32_t cp = decomposed[i];

        if (composed_count == 0) {
            composed[composed_count++] = cp;
            continue;
        }

        /// Try to compose with last starter
        size_t last_starter = composed_count - 1;
        while (last_starter > 0 &&
               lle_unicode_combining_class(composed[last_starter]) != 0) {
            last_starter--;
        }

        uint32_t result;
        if (lle_unicode_compose(composed[last_starter], cp, &result)) {
            composed[last_starter] = result;
        } else {
            composed[composed_count++] = cp;
        }
    }

    /// Step 4: Encode back to UTF-8
    char *out_ptr = output;
    size_t remaining = output_size - 1; /// Reserve for null terminator

    for (size_t i = 0; i < composed_count; i++) {
        char buf[4];
        int len = lle_utf8_encode_codepoint(composed[i], buf);
        if (len <= 0 || (size_t)len > remaining) {
            return -2; /// Buffer too small
        }
        memcpy(out_ptr, buf, len);
        out_ptr += len;
        remaining -= len;
    }

    *out_ptr = '\0';
    if (output_len) {
        *output_len = out_ptr - output;
    }

    return 0;
}

/**
 * @brief Compare two null-terminated UTF-8 strings for equality
 * @param str1 First string
 * @param str2 Second string
 * @param options Comparison options (NULL for defaults)
 * @return true if equal according to options, false otherwise
 */
bool lle_unicode_strings_equal(const char *str1, const char *str2,
                               const lle_unicode_compare_options_t *options) {
    if (!str1 && !str2)
        return true;
    if (!str1 || !str2)
        return false;

    return lle_unicode_strings_equal_n(str1, strlen(str1), str2, strlen(str2),
                                       options);
}

/**
 * @brief Check if one string is a prefix of another
 * @param prefix The potential prefix
 * @param prefix_len Length of prefix in bytes
 * @param str The string to check against
 * @param str_len Length of string in bytes
 * @param options Comparison options (NULL for defaults)
 * @return true if prefix matches start of str, false otherwise
 */
bool lle_unicode_is_prefix(const char *prefix, size_t prefix_len,
                           const char *str, size_t str_len,
                           const lle_unicode_compare_options_t *options) {
    /// Handle edge cases
    if (!prefix || prefix_len == 0) {
        return true; /// Empty prefix matches everything
    }
    if (!str || str_len == 0) {
        return false; /// Non-empty prefix can't match empty string
    }
    if (prefix_len > str_len) {
        return false; /// Prefix longer than string - quick rejection
    }

    /// Use default options if none provided
    lle_unicode_compare_options_t opts =
        options ? *options : LLE_UNICODE_COMPARE_DEFAULT;

    /*
     * Fast path: If no normalization needed, use simple byte comparison.
     * This handles the common case of ASCII-only input efficiently.
     */
    if (!opts.normalize && !opts.case_insensitive) {
        return memcmp(prefix, str, prefix_len) == 0;
    }

    /*
     * Unicode-aware path: Normalize both strings and compare.
     * We need to be careful about grapheme boundaries - the prefix
     * should end at a grapheme boundary in the normalized form.
     */
#define PREFIX_NORM_BUF_SIZE 4096
    char norm_prefix[PREFIX_NORM_BUF_SIZE];
    char norm_str[PREFIX_NORM_BUF_SIZE];
    size_t norm_prefix_len, norm_str_len;

    /// Normalize prefix
    if (opts.normalize) {
        if (lle_unicode_normalize_nfc(prefix, prefix_len, norm_prefix,
                                      PREFIX_NORM_BUF_SIZE,
                                      &norm_prefix_len) != 0) {
            /// Normalization failed, fall back to byte comparison
            return memcmp(prefix, str, prefix_len) == 0;
        }

        /// Normalize string
        if (lle_unicode_normalize_nfc(str, str_len, norm_str,
                                      PREFIX_NORM_BUF_SIZE,
                                      &norm_str_len) != 0) {
            /// Normalization failed, fall back to byte comparison
            return memcmp(prefix, str, prefix_len) == 0;
        }
    } else {
        /// No normalization - copy to buffers for uniform handling
        if (prefix_len >= PREFIX_NORM_BUF_SIZE ||
            str_len >= PREFIX_NORM_BUF_SIZE) {
            /// Strings too long for buffers, fall back
            return memcmp(prefix, str, prefix_len) == 0;
        }
        memcpy(norm_prefix, prefix, prefix_len);
        norm_prefix[prefix_len] = '\0';
        norm_prefix_len = prefix_len;
        memcpy(norm_str, str, str_len);
        norm_str[str_len] = '\0';
        norm_str_len = str_len;
    }

    /// Quick length check after normalization
    if (norm_prefix_len > norm_str_len) {
        return false;
    }

    /*
     * Compare codepoint by codepoint, optionally with case folding.
     * Track position in both strings to ensure we match exactly
     * norm_prefix_len worth of normalized prefix.
     */
    const char *pp = norm_prefix;
    const char *pp_end = norm_prefix + norm_prefix_len;
    const char *sp = norm_str;
    const char *sp_end = norm_str + norm_str_len;

    if (opts.case_insensitive) {
        while (pp < pp_end && sp < sp_end) {
            uint32_t cp_prefix, cp_str;
            int len_prefix =
                lle_utf8_decode_codepoint(pp, pp_end - pp, &cp_prefix);
            int len_str = lle_utf8_decode_codepoint(sp, sp_end - sp, &cp_str);

            if (len_prefix <= 0 || len_str <= 0) {
                return false; /// Invalid UTF-8
            }

            if (to_lowercase(cp_prefix) != to_lowercase(cp_str)) {
                return false;
            }

            pp += len_prefix;
            sp += len_str;
        }
    } else {
        /// Direct comparison without case folding
        if (memcmp(norm_prefix, norm_str, norm_prefix_len) != 0) {
            return false;
        }
        pp = pp_end; /// Mark as fully consumed
    }

    /// Prefix matched if we consumed all of it
    return (pp >= pp_end);
}

/**
 * @brief Check if one null-terminated string is a prefix of another
 * @param prefix The potential prefix (null-terminated)
 * @param str The string to check against (null-terminated)
 * @param options Comparison options (NULL for defaults)
 * @return true if prefix matches start of str, false otherwise
 */
bool lle_unicode_is_prefix_z(const char *prefix, const char *str,
                             const lle_unicode_compare_options_t *options) {
    if (!prefix) {
        return true; /// NULL prefix matches everything
    }
    if (!str) {
        return false; /// Non-null prefix can't match NULL string
    }

    return lle_unicode_is_prefix(prefix, strlen(prefix), str, strlen(str),
                                 options);
}

/**
 * @brief Compare two UTF-8 strings with explicit lengths for equality
 * @param str1 First string
 * @param len1 Length of first string in bytes
 * @param str2 Second string
 * @param len2 Length of second string in bytes
 * @param options Comparison options (NULL for defaults)
 * @return true if equal according to options, false otherwise
 */
bool lle_unicode_strings_equal_n(const char *str1, size_t len1,
                                 const char *str2, size_t len2,
                                 const lle_unicode_compare_options_t *options) {
    if (!str1 && !str2)
        return true;
    if (!str1 || !str2)
        return false;

    /// Use default options if none provided
    lle_unicode_compare_options_t opts =
        options ? *options : LLE_UNICODE_COMPARE_DEFAULT;

    /// Fast path: if no normalization needed and same length, use memcmp
    if (!opts.normalize && !opts.case_insensitive && !opts.trim_whitespace) {
        if (len1 != len2)
            return false;
        return memcmp(str1, str2, len1) == 0;
    }

    /// Handle whitespace trimming
    const char *s1 = str1;
    const char *e1 = str1 + len1;
    const char *s2 = str2;
    const char *e2 = str2 + len2;

    if (opts.trim_whitespace) {
        while (s1 < e1 && (*s1 == ' ' || *s1 == '\t'))
            s1++;
        while (e1 > s1 && (e1[-1] == ' ' || e1[-1] == '\t'))
            e1--;
        while (s2 < e2 && (*s2 == ' ' || *s2 == '\t'))
            s2++;
        while (e2 > s2 && (e2[-1] == ' ' || e2[-1] == '\t'))
            e2--;
    }

    size_t trimmed_len1 = e1 - s1;
    size_t trimmed_len2 = e2 - s2;

    /// Normalize if requested
    if (opts.normalize) {
#define NORM_BUF_SIZE 4096
        char norm1[NORM_BUF_SIZE];
        char norm2[NORM_BUF_SIZE];
        size_t norm1_len, norm2_len;

        if (lle_unicode_normalize_nfc(s1, trimmed_len1, norm1, NORM_BUF_SIZE,
                                      &norm1_len) != 0) {
            /// Normalization failed, fall back to byte comparison
            goto byte_compare;
        }
        if (lle_unicode_normalize_nfc(s2, trimmed_len2, norm2, NORM_BUF_SIZE,
                                      &norm2_len) != 0) {
            goto byte_compare;
        }

        if (opts.case_insensitive) {
            /// Compare codepoint by codepoint with case folding
            const char *p1 = norm1;
            const char *end1 = norm1 + norm1_len;
            const char *p2 = norm2;
            const char *end2 = norm2 + norm2_len;

            while (p1 < end1 && p2 < end2) {
                uint32_t cp1, cp2;
                int len1 = lle_utf8_decode_codepoint(p1, end1 - p1, &cp1);
                int len2 = lle_utf8_decode_codepoint(p2, end2 - p2, &cp2);

                if (len1 <= 0 || len2 <= 0) {
                    return false;
                }

                if (to_lowercase(cp1) != to_lowercase(cp2)) {
                    return false;
                }

                p1 += len1;
                p2 += len2;
            }

            return (p1 == end1 && p2 == end2);
        } else {
            /// Direct comparison of normalized strings
            if (norm1_len != norm2_len)
                return false;
            return memcmp(norm1, norm2, norm1_len) == 0;
        }
    }

byte_compare:
    /// Case-insensitive without normalization
    if (opts.case_insensitive) {
        const char *p1 = s1;
        const char *p2 = s2;

        while (p1 < e1 && p2 < e2) {
            uint32_t cp1, cp2;
            int len1 = lle_utf8_decode_codepoint(p1, e1 - p1, &cp1);
            int len2 = lle_utf8_decode_codepoint(p2, e2 - p2, &cp2);

            if (len1 <= 0 || len2 <= 0) {
                return false;
            }

            if (to_lowercase(cp1) != to_lowercase(cp2)) {
                return false;
            }

            p1 += len1;
            p2 += len2;
        }

        return (p1 == e1 && p2 == e2);
    }

    /// Simple byte comparison
    if (trimmed_len1 != trimmed_len2)
        return false;
    return memcmp(s1, s2, trimmed_len1) == 0;
}
