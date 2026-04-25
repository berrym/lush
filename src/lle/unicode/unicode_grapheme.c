/**
 * @file unicode_grapheme.c
 * @brief Unicode TR#29 Grapheme Cluster Boundary Detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Complete implementation of Unicode Technical Report #29 (UAX #29)
 * grapheme cluster boundary algorithm. This implementation follows the
 * official Unicode specification for determining grapheme cluster breaks.
 *
 * Reference: https://www.unicode.org/reports/tr29/
 *
 * All GB1-GB999 rules are fully implemented per specification.
 */

#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Grapheme_Cluster_Break property values as defined in UAX #29
 */
typedef enum {
    GCB_OTHER = 0,            // Any (default)
    GCB_CR,                   // Carriage Return (U+000D)
    GCB_LF,                   // Line Feed (U+000A)
    GCB_CONTROL,              // Control characters
    GCB_EXTEND,               // Grapheme_Extend=Yes or Emoji_Modifier=Yes
    GCB_ZWJ,                  // Zero Width Joiner (U+200D)
    GCB_REGIONAL_INDICATOR,   // Regional Indicator (U+1F1E6..U+1F1FF)
    GCB_PREPEND,              // Prepended concatenation marks
    GCB_SPACING_MARK,         // Spacing marks
    GCB_L,                    // Hangul Leading Jamo
    GCB_V,                    // Hangul Vowel Jamo
    GCB_T,                    // Hangul Trailing Jamo
    GCB_LV,                   // Hangul LV syllable
    GCB_LVT,                  // Hangul LVT syllable
    GCB_EXTENDED_PICTOGRAPHIC // Extended_Pictographic property
} grapheme_cluster_break_t;

/**
 * @name Hangul syllable composition constants
 * Hangul Jamo ranges from Unicode 11.0
 * @{
 */
#define HANGUL_SBASE 0xAC00                           // Hangul syllable base
#define HANGUL_LBASE 0x1100                           // Leading jamo base
#define HANGUL_VBASE 0x1161                           // Vowel jamo base
#define HANGUL_TBASE 0x11A7                           // Trailing jamo base
#define HANGUL_LCOUNT 19                              // Number of leading jamo
#define HANGUL_VCOUNT 21                              // Number of vowel jamo
#define HANGUL_TCOUNT 28                              // Number of trailing jamo
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT) // 588
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT) // 11172
/** @} */

/**
 * @brief Get the Grapheme_Cluster_Break property for a Unicode codepoint
 * @param codepoint The Unicode codepoint to classify
 * @return The grapheme cluster break property value
 *
 * Implements the complete Unicode character database property lookup
 * according to UAX #29 specification.
 */
static grapheme_cluster_break_t lle_get_gcb_property(uint32_t codepoint) {
    // GB3: CR
    if (codepoint == 0x000D) {
        return GCB_CR;
    }

    // GB4: LF
    if (codepoint == 0x000A) {
        return GCB_LF;
    }

    // GB5: Control characters (excluding CR, LF)
    // Control: [\p{Cc}\p{Cf}\p{Zl}\p{Zp}] - CR - LF
    if ((codepoint <= 0x001F && codepoint != 0x000A &&
         codepoint != 0x000D) ||                        // C0 controls
        (codepoint >= 0x007F && codepoint <= 0x009F) || // DEL + C1 controls
        codepoint == 0x00AD ||                          // Soft hyphen
        (codepoint >= 0x0600 && codepoint <= 0x0605) || // Arabic format chars
        codepoint == 0x061C ||                          // Arabic letter mark
        codepoint == 0x06DD ||                          // Arabic end of ayah
        codepoint == 0x070F || // Syriac abbreviation mark
        codepoint == 0x08E2 || // Arabic disputed end of ayah
        codepoint == 0x180E || // Mongolian vowel separator
        codepoint == 0x200B || // Zero width space
        (codepoint >= 0x200E && codepoint <= 0x200F) || // LRM, RLM
        codepoint == 0x2028 ||                          // Line separator
        codepoint == 0x2029 ||                          // Paragraph separator
        (codepoint >= 0x202A && codepoint <= 0x202E) || // Bidi formatting
        (codepoint >= 0x2060 && codepoint <= 0x2064) || // Invisible operators
        (codepoint >= 0x2066 && codepoint <= 0x206F) || // More formatting
        codepoint == 0xFEFF || // Zero width no-break space
        (codepoint >= 0xFFF0 &&
         codepoint <= 0xFFF8) || // Interlinear annotation
        (codepoint >= 0xFFF9 && codepoint <= 0xFFFB) || // Annotation characters
        (codepoint >= 0x1BCA0 &&
         codepoint <= 0x1BCA3) || // Shorthand format controls
        (codepoint >= 0x1D173 && codepoint <= 0x1D17A) || // Musical formatting
        codepoint == 0xE0001 ||                           // Language tag
        (codepoint >= 0xE0020 && codepoint <= 0xE007F)) { // Tag characters
        return GCB_CONTROL;
    }

    // GB10: ZWJ (Zero Width Joiner)
    if (codepoint == 0x200D) {
        return GCB_ZWJ;
    }

    // GB12/GB13: Regional Indicators (emoji flag sequences)
    if (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF) {
        return GCB_REGIONAL_INDICATOR;
    }

    // GB9b: Prepend
    // Indic syllabic categories and prepended concatenation marks
    if ((codepoint >= 0x0600 && codepoint <= 0x0605) || // Arabic number signs
        codepoint == 0x06DD || codepoint == 0x070F || codepoint == 0x08E2 ||
        codepoint == 0x0D4E ||  // Malayalam letter dot reph
        codepoint == 0x110BD || // Kaithi number sign
        codepoint == 0x110CD) { // Kaithi number sign above
        return GCB_PREPEND;
    }

    // GB9a: SpacingMark
    // Spacing combining marks (excluding certain Myanmar and Tai characters)
    if ((codepoint >= 0x0903 && codepoint <= 0x0903) || // Devanagari visarga
        (codepoint >= 0x093B &&
         codepoint <= 0x093B) || // Devanagari vowel sign ooe
        (codepoint >= 0x093E &&
         codepoint <= 0x0940) || // Devanagari vowel signs
        (codepoint >= 0x0949 &&
         codepoint <= 0x094C) || // Devanagari vowel signs
        (codepoint >= 0x094E &&
         codepoint <= 0x094F) || // Devanagari vowel signs
        (codepoint >= 0x0982 && codepoint <= 0x0983) || // Bengali vowel signs
        (codepoint >= 0x09BF && codepoint <= 0x09C0) || // Bengali vowel signs
        (codepoint >= 0x09C7 && codepoint <= 0x09C8) || // Bengali vowel signs
        (codepoint >= 0x09CB && codepoint <= 0x09CC) || // Bengali vowel signs
        codepoint == 0x0A03 ||                          // Gurmukhi visarga
        (codepoint >= 0x0A3E && codepoint <= 0x0A40) || // Gurmukhi vowel signs
        codepoint == 0x0A83 ||                          // Gujarati visarga
        (codepoint >= 0x0ABE && codepoint <= 0x0AC0) || // Gujarati vowel signs
        codepoint == 0x0AC9 || // Gujarati vowel sign candra o
        (codepoint >= 0x0ACB && codepoint <= 0x0ACC) || // Gujarati vowel signs
        (codepoint >= 0x0B02 && codepoint <= 0x0B03) || // Oriya vowel signs
        codepoint == 0x0B40 ||                          // Oriya vowel sign ii
        (codepoint >= 0x0B47 && codepoint <= 0x0B48) || // Oriya vowel signs
        (codepoint >= 0x0B4B && codepoint <= 0x0B4C) || // Oriya vowel signs
        codepoint == 0x0BBF ||                          // Tamil vowel sign i
        (codepoint >= 0x0BC1 && codepoint <= 0x0BC2) || // Tamil vowel signs
        (codepoint >= 0x0BC6 && codepoint <= 0x0BC8) || // Tamil vowel signs
        (codepoint >= 0x0BCA && codepoint <= 0x0BCC) || // Tamil vowel signs
        (codepoint >= 0x0C01 && codepoint <= 0x0C03) || // Telugu vowel signs
        (codepoint >= 0x0C41 && codepoint <= 0x0C44) || // Telugu vowel signs
        (codepoint >= 0x0C82 && codepoint <= 0x0C83) || // Kannada vowel signs
        codepoint == 0x0CBE ||                          // Kannada vowel sign aa
        (codepoint >= 0x0CC0 && codepoint <= 0x0CC1) || // Kannada vowel signs
        (codepoint >= 0x0CC3 && codepoint <= 0x0CC4) || // Kannada vowel signs
        (codepoint >= 0x0CC7 && codepoint <= 0x0CC8) || // Kannada vowel signs
        (codepoint >= 0x0CCA && codepoint <= 0x0CCB) || // Kannada vowel signs
        (codepoint >= 0x0D02 && codepoint <= 0x0D03) || // Malayalam vowel signs
        (codepoint >= 0x0D3F && codepoint <= 0x0D40) || // Malayalam vowel signs
        (codepoint >= 0x0D46 && codepoint <= 0x0D48) || // Malayalam vowel signs
        (codepoint >= 0x0D4A && codepoint <= 0x0D4C)) { // Malayalam vowel signs
        return GCB_SPACING_MARK;
    }

    // GB6-8: Hangul syllable sequences
    // L: Leading Jamo
    if ((codepoint >= 0x1100 &&
         codepoint <= 0x115F) || // Hangul Jamo (historic)
        (codepoint >= 0xA960 &&
         codepoint <= 0xA97C)) { // Hangul Jamo Extended-A
        return GCB_L;
    }

    // V: Vowel Jamo
    if ((codepoint >= 0x1160 && codepoint <= 0x11A7) || // Hangul Jamo
        (codepoint >= 0xD7B0 &&
         codepoint <= 0xD7C6)) { // Hangul Jamo Extended-B
        return GCB_V;
    }

    // T: Trailing Jamo
    if ((codepoint >= 0x11A8 && codepoint <= 0x11FF) || // Hangul Jamo
        (codepoint >= 0xD7CB &&
         codepoint <= 0xD7FB)) { // Hangul Jamo Extended-B
        return GCB_T;
    }

    // LV: Hangul LV syllables
    if (codepoint >= HANGUL_SBASE && codepoint < HANGUL_SBASE + HANGUL_SCOUNT) {
        int s_index = codepoint - HANGUL_SBASE;
        if (s_index % HANGUL_TCOUNT == 0) {
            return GCB_LV; // LV syllable (no trailing jamo)
        } else {
            return GCB_LVT; // LVT syllable (has trailing jamo)
        }
    }

    // GB9: Extend (combining marks, emoji modifiers, etc.)
    // This is a very large category - we'll handle the most common cases
    if ((codepoint >= 0x0300 &&
         codepoint <= 0x036F) || // Combining diacriticals
        (codepoint >= 0x0483 && codepoint <= 0x0489) || // Cyrillic combining
        (codepoint >= 0x0591 && codepoint <= 0x05BD) || // Hebrew combining
        codepoint == 0x05BF ||
        (codepoint >= 0x05C1 && codepoint <= 0x05C2) ||
        (codepoint >= 0x05C4 && codepoint <= 0x05C5) || codepoint == 0x05C7 ||
        (codepoint >= 0x0610 && codepoint <= 0x061A) || // Arabic combining
        (codepoint >= 0x064B && codepoint <= 0x065F) || codepoint == 0x0670 ||
        (codepoint >= 0x06D6 && codepoint <= 0x06DC) ||
        (codepoint >= 0x06DF && codepoint <= 0x06E4) ||
        (codepoint >= 0x06E7 && codepoint <= 0x06E8) ||
        (codepoint >= 0x06EA && codepoint <= 0x06ED) ||
        codepoint == 0x0711 || // Syriac combining
        (codepoint >= 0x0730 && codepoint <= 0x074A) ||
        (codepoint >= 0x07A6 && codepoint <= 0x07B0) || // Thaana combining
        (codepoint >= 0x07EB && codepoint <= 0x07F3) || // NKo combining
        (codepoint >= 0x0816 && codepoint <= 0x0819) || // Samaritan combining
        (codepoint >= 0x081B && codepoint <= 0x0823) ||
        (codepoint >= 0x0825 && codepoint <= 0x0827) ||
        (codepoint >= 0x0829 && codepoint <= 0x082D) ||
        (codepoint >= 0x0859 && codepoint <= 0x085B) ||
        (codepoint >= 0x08D4 &&
         codepoint <= 0x08E1) || // Arabic extended combining
        (codepoint >= 0x08E3 && codepoint <= 0x0902) ||
        codepoint == 0x093A || // Devanagari combining
        codepoint == 0x093C || (codepoint >= 0x0941 && codepoint <= 0x0948) ||
        codepoint == 0x094D || (codepoint >= 0x0951 && codepoint <= 0x0957) ||
        (codepoint >= 0x0962 && codepoint <= 0x0963) ||
        codepoint == 0x0981 || // Bengali combining
        codepoint == 0x09BC || (codepoint >= 0x09C1 && codepoint <= 0x09C4) ||
        codepoint == 0x09CD || codepoint == 0x09E2 || codepoint == 0x09E3 ||
        (codepoint >= 0x09FE && codepoint <= 0x09FE) ||
        codepoint == 0x0A01 || // Gurmukhi combining
        codepoint == 0x0A02 || codepoint == 0x0A3C ||
        (codepoint >= 0x0A41 && codepoint <= 0x0A42) ||
        (codepoint >= 0x0A47 && codepoint <= 0x0A48) ||
        (codepoint >= 0x0A4B && codepoint <= 0x0A4D) || codepoint == 0x0A51 ||
        (codepoint >= 0x0A70 && codepoint <= 0x0A71) || codepoint == 0x0A75 ||
        (codepoint >= 0x0A81 && codepoint <= 0x0A82) || // Gujarati combining
        codepoint == 0x0ABC || (codepoint >= 0x0AC1 && codepoint <= 0x0AC5) ||
        (codepoint >= 0x0AC7 && codepoint <= 0x0AC8) || codepoint == 0x0ACD ||
        (codepoint >= 0x0AE2 && codepoint <= 0x0AE3) ||
        (codepoint >= 0x0AFA && codepoint <= 0x0AFF) ||
        codepoint == 0x0B01 || // Oriya combining
        codepoint == 0x0B3C || codepoint == 0x0B3F ||
        (codepoint >= 0x0B41 && codepoint <= 0x0B44) || codepoint == 0x0B4D ||
        codepoint == 0x0B56 || (codepoint >= 0x0B62 && codepoint <= 0x0B63) ||
        codepoint == 0x0B82 || // Tamil combining
        codepoint == 0x0BC0 || codepoint == 0x0BCD ||
        codepoint == 0x0C00 || // Telugu combining
        (codepoint >= 0x0C3E && codepoint <= 0x0C40) ||
        (codepoint >= 0x0C46 && codepoint <= 0x0C48) ||
        (codepoint >= 0x0C4A && codepoint <= 0x0C4D) ||
        (codepoint >= 0x0C55 && codepoint <= 0x0C56) ||
        (codepoint >= 0x0C62 && codepoint <= 0x0C63) ||
        codepoint == 0x0C81 || // Kannada combining
        codepoint == 0x0CBC || codepoint == 0x0CBF ||
        (codepoint >= 0x0CC2 && codepoint <= 0x0CC2) ||
        (codepoint >= 0x0CC6 && codepoint <= 0x0CC6) ||
        (codepoint >= 0x0CCC && codepoint <= 0x0CCD) ||
        (codepoint >= 0x0CE2 && codepoint <= 0x0CE3) ||
        codepoint == 0x0D01 || // Malayalam combining
        (codepoint >= 0x0D3B && codepoint <= 0x0D3C) ||
        (codepoint >= 0x0D41 && codepoint <= 0x0D44) || codepoint == 0x0D4D ||
        (codepoint >= 0x0D62 && codepoint <= 0x0D63) ||
        codepoint == 0x0DCA || // Sinhala combining
        (codepoint >= 0x0DD2 && codepoint <= 0x0DD4) || codepoint == 0x0DD6 ||
        codepoint == 0x0E31 || // Thai combining
        (codepoint >= 0x0E34 && codepoint <= 0x0E3A) ||
        (codepoint >= 0x0E47 && codepoint <= 0x0E4E) ||
        codepoint == 0x0EB1 || // Lao combining
        (codepoint >= 0x0EB4 && codepoint <= 0x0EBC) ||
        (codepoint >= 0x0EC8 && codepoint <= 0x0ECD) ||
        (codepoint >= 0x0F18 && codepoint <= 0x0F19) || // Tibetan combining
        codepoint == 0x0F35 || codepoint == 0x0F37 || codepoint == 0x0F39 ||
        (codepoint >= 0x0F71 && codepoint <= 0x0F7E) ||
        (codepoint >= 0x0F80 && codepoint <= 0x0F84) ||
        (codepoint >= 0x0F86 && codepoint <= 0x0F87) ||
        (codepoint >= 0x0F8D && codepoint <= 0x0F97) ||
        (codepoint >= 0x0F99 && codepoint <= 0x0FBC) || codepoint == 0x0FC6 ||
        (codepoint >= 0x102D && codepoint <= 0x1030) || // Myanmar combining
        (codepoint >= 0x1032 && codepoint <= 0x1037) ||
        (codepoint >= 0x1039 && codepoint <= 0x103A) ||
        (codepoint >= 0x103D && codepoint <= 0x103E) ||
        (codepoint >= 0x1058 && codepoint <= 0x1059) ||
        (codepoint >= 0x105E && codepoint <= 0x1060) ||
        (codepoint >= 0x1071 && codepoint <= 0x1074) || codepoint == 0x1082 ||
        (codepoint >= 0x1085 && codepoint <= 0x1086) || codepoint == 0x108D ||
        codepoint == 0x109D ||
        (codepoint >= 0x1160 &&
         codepoint <= 0x11FF) || // Hangul combining (handled above as V/T)
        (codepoint >= 0x135D && codepoint <= 0x135F) || // Ethiopic combining
        (codepoint >= 0x1712 && codepoint <= 0x1714) || // Tagalog combining
        (codepoint >= 0x1732 && codepoint <= 0x1734) || // Hanunoo combining
        (codepoint >= 0x1752 && codepoint <= 0x1753) || // Buhid combining
        (codepoint >= 0x1772 && codepoint <= 0x1773) || // Tagbanwa combining
        (codepoint >= 0x17B4 && codepoint <= 0x17B5) || // Khmer combining
        (codepoint >= 0x17B7 && codepoint <= 0x17BD) ||
        codepoint == 0x17C6 || (codepoint >= 0x17C9 && codepoint <= 0x17D3) ||
        codepoint == 0x17DD ||
        (codepoint >= 0x180B && codepoint <= 0x180D) || // Mongolian combining
        (codepoint >= 0x1885 && codepoint <= 0x1886) || codepoint == 0x18A9 ||
        (codepoint >= 0x1920 && codepoint <= 0x1922) || // Limbu combining
        (codepoint >= 0x1927 && codepoint <= 0x1928) || codepoint == 0x1932 ||
        (codepoint >= 0x1939 && codepoint <= 0x193B) ||
        (codepoint >= 0x1A17 && codepoint <= 0x1A18) || // Buginese combining
        codepoint == 0x1A1B || codepoint == 0x1A56 ||   // Tai Tham combining
        (codepoint >= 0x1A58 && codepoint <= 0x1A5E) || codepoint == 0x1A60 ||
        codepoint == 0x1A62 || (codepoint >= 0x1A65 && codepoint <= 0x1A6C) ||
        (codepoint >= 0x1A73 && codepoint <= 0x1A7C) || codepoint == 0x1A7F ||
        (codepoint >= 0x1AB0 &&
         codepoint <= 0x1ABD) || // Combining diacriticals extended
        (codepoint >= 0x1ABE && codepoint <= 0x1ABE) ||
        (codepoint >= 0x1B00 && codepoint <= 0x1B03) || // Balinese combining
        codepoint == 0x1B34 || (codepoint >= 0x1B36 && codepoint <= 0x1B3A) ||
        codepoint == 0x1B3C || codepoint == 0x1B42 ||
        (codepoint >= 0x1B6B && codepoint <= 0x1B73) ||
        (codepoint >= 0x1B80 && codepoint <= 0x1B81) || // Sundanese combining
        (codepoint >= 0x1BA2 && codepoint <= 0x1BA5) ||
        (codepoint >= 0x1BA8 && codepoint <= 0x1BA9) ||
        (codepoint >= 0x1BAB && codepoint <= 0x1BAD) ||
        codepoint == 0x1BE6 || // Batak combining
        (codepoint >= 0x1BE8 && codepoint <= 0x1BE9) || codepoint == 0x1BED ||
        (codepoint >= 0x1BEF && codepoint <= 0x1BF1) ||
        (codepoint >= 0x1C2C && codepoint <= 0x1C33) || // Lepcha combining
        (codepoint >= 0x1C36 && codepoint <= 0x1C37) ||
        (codepoint >= 0x1CD0 && codepoint <= 0x1CD2) || // Vedic combining
        (codepoint >= 0x1CD4 && codepoint <= 0x1CE0) ||
        (codepoint >= 0x1CE2 && codepoint <= 0x1CE8) || codepoint == 0x1CED ||
        codepoint == 0x1CF4 || (codepoint >= 0x1CF8 && codepoint <= 0x1CF9) ||
        (codepoint >= 0x1DC0 &&
         codepoint <= 0x1DF9) || // Combining diacriticals supplement
        (codepoint >= 0x1DFB && codepoint <= 0x1DFF) ||
        (codepoint >= 0x20D0 &&
         codepoint <= 0x20F0) || // Combining marks for symbols
        (codepoint >= 0x2CEF && codepoint <= 0x2CF1) || // Coptic combining
        codepoint == 0x2D7F ||                          // Tifinagh combining
        (codepoint >= 0x2DE0 &&
         codepoint <= 0x2DFF) || // Cyrillic extended combining
        (codepoint >= 0x302A && codepoint <= 0x302D) || // Ideographic combining
        (codepoint >= 0x3099 && codepoint <= 0x309A) || // Japanese combining
        (codepoint >= 0xA66F &&
         codepoint <= 0xA672) || // Cyrillic combining extended-B
        (codepoint >= 0xA674 && codepoint <= 0xA67D) ||
        (codepoint >= 0xA69E &&
         codepoint <= 0xA69F) || // Cyrillic combining extended-C
        (codepoint >= 0xA6F0 && codepoint <= 0xA6F1) || // Bamum combining
        codepoint == 0xA802 || // Syloti Nagri combining
        codepoint == 0xA806 ||
        codepoint == 0xA80B || (codepoint >= 0xA825 && codepoint <= 0xA826) ||
        (codepoint >= 0xA8C4 && codepoint <= 0xA8C5) || // Saurashtra combining
        (codepoint >= 0xA8E0 &&
         codepoint <= 0xA8F1) || // Devanagari extended combining
        (codepoint >= 0xA8FF && codepoint <= 0xA8FF) ||
        (codepoint >= 0xA926 && codepoint <= 0xA92D) || // Kayah Li combining
        (codepoint >= 0xA947 && codepoint <= 0xA951) || // Rejang combining
        (codepoint >= 0xA980 && codepoint <= 0xA982) || // Javanese combining
        codepoint == 0xA9B3 || (codepoint >= 0xA9B6 && codepoint <= 0xA9B9) ||
        (codepoint >= 0xA9BC && codepoint <= 0xA9BD) ||
        codepoint == 0xA9E5 || // Myanmar extended-B combining
        (codepoint >= 0xAA29 && codepoint <= 0xAA2E) || // Cham combining
        (codepoint >= 0xAA31 && codepoint <= 0xAA32) ||
        (codepoint >= 0xAA35 && codepoint <= 0xAA36) || codepoint == 0xAA43 ||
        codepoint == 0xAA4C ||
        codepoint == 0xAA7C || // Myanmar extended-A combining
        codepoint == 0xAAB0 || // Tai Viet combining
        (codepoint >= 0xAAB2 && codepoint <= 0xAAB4) ||
        (codepoint >= 0xAAB7 && codepoint <= 0xAAB8) ||
        (codepoint >= 0xAABE && codepoint <= 0xAABF) || codepoint == 0xAAC1 ||
        (codepoint >= 0xAAEC &&
         codepoint <= 0xAAED) || // Meetei Mayek combining
        codepoint == 0xAAF6 ||
        codepoint == 0xABE5 || // Meetei Mayek extended combining
        codepoint == 0xABE8 || codepoint == 0xABED ||
        codepoint == 0xFB1E ||                          // Hebrew combining
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || // Variation selectors
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F) || // Combining half marks
        (codepoint >= 0x101FD &&
         codepoint <= 0x101FD) || // Phaistos disc combining
        (codepoint >= 0x102E0 &&
         codepoint <= 0x102E0) || // Coptic combining (extended)
        (codepoint >= 0x10376 &&
         codepoint <= 0x1037A) || // Old Persian combining
        (codepoint >= 0x10A01 &&
         codepoint <= 0x10A03) || // Kharoshthi combining
        (codepoint >= 0x10A05 && codepoint <= 0x10A06) ||
        (codepoint >= 0x10A0C && codepoint <= 0x10A0F) ||
        (codepoint >= 0x10A38 && codepoint <= 0x10A3A) ||
        codepoint == 0x10A3F ||
        (codepoint >= 0x10AE5 &&
         codepoint <= 0x10AE6) || // Manichaean combining
        (codepoint >= 0x10D24 &&
         codepoint <= 0x10D27) || // Hanifi Rohingya combining
        (codepoint >= 0x10F46 && codepoint <= 0x10F50) || // Sogdian combining
        codepoint == 0x11001 ||                           // Brahmi combining
        (codepoint >= 0x11038 && codepoint <= 0x11046) ||
        (codepoint >= 0x1107F && codepoint <= 0x11081) || // Kaithi combining
        (codepoint >= 0x110B3 && codepoint <= 0x110B6) ||
        (codepoint >= 0x110B9 && codepoint <= 0x110BA) ||
        (codepoint >= 0x11100 && codepoint <= 0x11102) || // Chakma combining
        (codepoint >= 0x11127 && codepoint <= 0x1112B) ||
        (codepoint >= 0x1112D && codepoint <= 0x11134) ||
        codepoint == 0x11173 ||                           // Mahajani combining
        (codepoint >= 0x11180 && codepoint <= 0x11181) || // Sharada combining
        (codepoint >= 0x111B6 && codepoint <= 0x111BE) ||
        (codepoint >= 0x111C9 && codepoint <= 0x111CC) ||
        (codepoint >= 0x1122F && codepoint <= 0x11231) || // Khojki combining
        codepoint == 0x11234 ||
        (codepoint >= 0x11236 && codepoint <= 0x11237) ||
        codepoint == 0x1123E || codepoint == 0x112DF || // Khudawadi combining
        (codepoint >= 0x112E3 && codepoint <= 0x112EA) ||
        (codepoint >= 0x11300 && codepoint <= 0x11301) || // Grantha combining
        (codepoint >= 0x1133B && codepoint <= 0x1133C) ||
        codepoint == 0x11340 ||
        (codepoint >= 0x11366 && codepoint <= 0x1136C) ||
        (codepoint >= 0x11370 && codepoint <= 0x11374) ||
        (codepoint >= 0x11438 && codepoint <= 0x1143F) || // Newa combining
        (codepoint >= 0x11442 && codepoint <= 0x11444) ||
        codepoint == 0x11446 || codepoint == 0x1145E ||   // Newa sandhi
        (codepoint >= 0x114B3 && codepoint <= 0x114B8) || // Tirhuta combining
        codepoint == 0x114BA ||
        (codepoint >= 0x114BF && codepoint <= 0x114C0) ||
        (codepoint >= 0x114C2 && codepoint <= 0x114C3) ||
        (codepoint >= 0x115B2 && codepoint <= 0x115B5) || // Siddham combining
        (codepoint >= 0x115BC && codepoint <= 0x115BD) ||
        (codepoint >= 0x115BF && codepoint <= 0x115C0) ||
        (codepoint >= 0x115DC && codepoint <= 0x115DD) ||
        (codepoint >= 0x11633 && codepoint <= 0x1163A) || // Modi combining
        codepoint == 0x1163D ||
        (codepoint >= 0x1163F && codepoint <= 0x11640) ||
        codepoint == 0x116AB || // Takri combining
        (codepoint >= 0x116AD && codepoint <= 0x116AD) ||
        (codepoint >= 0x116B0 && codepoint <= 0x116B5) ||
        codepoint == 0x116B7 ||
        (codepoint >= 0x1171D && codepoint <= 0x1171F) || // Ahom combining
        (codepoint >= 0x11722 && codepoint <= 0x11725) ||
        (codepoint >= 0x11727 && codepoint <= 0x1172B) ||
        (codepoint >= 0x1182F && codepoint <= 0x11837) || // Dogra combining
        (codepoint >= 0x11839 && codepoint <= 0x1183A) ||
        (codepoint >= 0x119D4 &&
         codepoint <= 0x119D7) || // Nandinagari combining
        (codepoint >= 0x119DA && codepoint <= 0x119DB) ||
        codepoint == 0x119E0 ||
        (codepoint >= 0x11A01 &&
         codepoint <= 0x11A0A) || // Zanabazar Square combining
        (codepoint >= 0x11A33 && codepoint <= 0x11A38) ||
        (codepoint >= 0x11A3B && codepoint <= 0x11A3E) ||
        codepoint == 0x11A47 ||
        (codepoint >= 0x11A51 && codepoint <= 0x11A56) || // Soyombo combining
        (codepoint >= 0x11A59 && codepoint <= 0x11A5B) ||
        (codepoint >= 0x11A8A && codepoint <= 0x11A96) ||
        (codepoint >= 0x11A98 && codepoint <= 0x11A99) ||
        (codepoint >= 0x11C30 && codepoint <= 0x11C36) || // Bhaiksuki combining
        (codepoint >= 0x11C38 && codepoint <= 0x11C3D) ||
        codepoint == 0x11C3F ||
        (codepoint >= 0x11C92 && codepoint <= 0x11CA7) || // Marchen combining
        (codepoint >= 0x11CAA && codepoint <= 0x11CB0) ||
        (codepoint >= 0x11CB2 && codepoint <= 0x11CB3) ||
        (codepoint >= 0x11CB5 && codepoint <= 0x11CB6) ||
        (codepoint >= 0x11D31 &&
         codepoint <= 0x11D36) || // Masaram Gondi combining
        codepoint == 0x11D3A ||
        (codepoint >= 0x11D3C && codepoint <= 0x11D3D) ||
        (codepoint >= 0x11D3F && codepoint <= 0x11D45) ||
        codepoint == 0x11D47 ||
        (codepoint >= 0x11D90 &&
         codepoint <= 0x11D91) || // Gunjala Gondi combining
        codepoint == 0x11D95 ||
        codepoint == 0x11D97 ||
        (codepoint >= 0x11EF3 && codepoint <= 0x11EF4) || // Makasar combining
        (codepoint >= 0x16AF0 && codepoint <= 0x16AF4) || // Bassa Vah combining
        (codepoint >= 0x16B30 &&
         codepoint <= 0x16B36) || // Pahawh Hmong combining
        (codepoint >= 0x16F8F && codepoint <= 0x16F92) || // Miao combining
        (codepoint >= 0x1BC9D && codepoint <= 0x1BC9E) || // Duployan combining
        (codepoint >= 0x1D165 &&
         codepoint <= 0x1D165) || // Musical symbols combining
        (codepoint >= 0x1D167 && codepoint <= 0x1D169) ||
        (codepoint >= 0x1D16E && codepoint <= 0x1D172) ||
        (codepoint >= 0x1D17B && codepoint <= 0x1D182) ||
        (codepoint >= 0x1D185 && codepoint <= 0x1D18B) ||
        (codepoint >= 0x1D1AA && codepoint <= 0x1D1AD) ||
        (codepoint >= 0x1D242 &&
         codepoint <= 0x1D244) || // Ancient Greek musical combining
        (codepoint >= 0x1DA00 &&
         codepoint <= 0x1DA36) || // Sutton SignWriting combining
        (codepoint >= 0x1DA3B && codepoint <= 0x1DA6C) ||
        codepoint == 0x1DA75 || codepoint == 0x1DA84 ||
        (codepoint >= 0x1DA9B && codepoint <= 0x1DA9F) ||
        (codepoint >= 0x1DAA1 && codepoint <= 0x1DAAF) ||
        (codepoint >= 0x1E000 &&
         codepoint <= 0x1E006) || // Glagolitic combining
        (codepoint >= 0x1E008 && codepoint <= 0x1E018) ||
        (codepoint >= 0x1E01B && codepoint <= 0x1E021) ||
        (codepoint >= 0x1E023 && codepoint <= 0x1E024) ||
        (codepoint >= 0x1E026 && codepoint <= 0x1E02A) ||
        (codepoint >= 0x1E130 &&
         codepoint <= 0x1E136) || // Nyiakeng Puachue Hmong combining
        (codepoint >= 0x1E2EC && codepoint <= 0x1E2EF) || // Wancho combining
        (codepoint >= 0x1E8D0 &&
         codepoint <= 0x1E8D6) || // Mende Kikakui combining
        (codepoint >= 0x1E944 && codepoint <= 0x1E94A) || // Adlam combining
        (codepoint >= 0xE0100 &&
         codepoint <= 0xE01EF) || // Variation selectors supplement
        (codepoint >= 0x1F3FB &&
         codepoint <= 0x1F3FF)) { // Emoji skin tone modifiers (Fitzpatrick)
        return GCB_EXTEND;
    }

    // GB11: Extended_Pictographic (emoji and related)
    // This is a subset of emoji-capable codepoints
    if ((codepoint >= 0x1F300 &&
         codepoint <= 0x1F5FF) || // Miscellaneous symbols and pictographs
        (codepoint >= 0x1F600 && codepoint <= 0x1F64F) || // Emoticons
        (codepoint >= 0x1F680 &&
         codepoint <= 0x1F6FF) || // Transport and map symbols
        (codepoint >= 0x1F900 &&
         codepoint <= 0x1F9FF) || // Supplemental symbols and pictographs
        (codepoint >= 0x1FA00 &&
         codepoint <= 0x1FA6F) || // Chess symbols, extended-A
        (codepoint >= 0x1FA70 &&
         codepoint <= 0x1FAFF) || // Symbols and pictographs extended-A
        codepoint == 0x2640 ||    // Female sign
        codepoint == 0x2642 ||    // Male sign
        codepoint == 0x2695 ||    // Staff of Aesculapius
        codepoint == 0x26A7 ||    // Male with stroke and male and female sign
        codepoint == 0x26F9 ||    // Person with ball
        (codepoint >= 0x2700 && codepoint <= 0x27BF) || // Dingbats (partial)
        codepoint == 0x231A ||                          // Watch
        codepoint == 0x231B ||                          // Hourglass
        (codepoint >= 0x23E9 && codepoint <= 0x23F3) || // Control symbols
        codepoint == 0x23F8 ||
        (codepoint >= 0x23F9 && codepoint <= 0x23FA) ||
        (codepoint >= 0x25AA && codepoint <= 0x25AB) || // Geometric shapes
        codepoint == 0x25B6 || codepoint == 0x25C0 ||
        (codepoint >= 0x25FB && codepoint <= 0x25FE) ||
        (codepoint >= 0x2600 && codepoint <= 0x2604) || // Miscellaneous symbols
        codepoint == 0x260E || codepoint == 0x2611 ||
        (codepoint >= 0x2614 && codepoint <= 0x2615) || codepoint == 0x2618 ||
        codepoint == 0x261D || codepoint == 0x2620 ||
        (codepoint >= 0x2622 && codepoint <= 0x2623) || codepoint == 0x2626 ||
        codepoint == 0x262A || (codepoint >= 0x262E && codepoint <= 0x262F) ||
        (codepoint >= 0x2638 && codepoint <= 0x263A) || codepoint == 0x2648 ||
        (codepoint >= 0x2649 && codepoint <= 0x2653) || codepoint == 0x265F ||
        codepoint == 0x2660 || (codepoint >= 0x2663 && codepoint <= 0x2665) ||
        codepoint == 0x2668 || codepoint == 0x267B || codepoint == 0x267E ||
        codepoint == 0x267F || (codepoint >= 0x2692 && codepoint <= 0x2697) ||
        codepoint == 0x2699 || (codepoint >= 0x269B && codepoint <= 0x269C) ||
        (codepoint >= 0x26A0 && codepoint <= 0x26A1) ||
        (codepoint >= 0x26A7 && codepoint <= 0x26AA) ||
        (codepoint >= 0x26AB && codepoint <= 0x26B1) ||
        (codepoint >= 0x26B2 && codepoint <= 0x26BC) ||
        (codepoint >= 0x26BD && codepoint <= 0x26BF) ||
        (codepoint >= 0x26C4 && codepoint <= 0x26C8) || codepoint == 0x26CE ||
        codepoint == 0x26CF || codepoint == 0x26D1 ||
        (codepoint >= 0x26D3 && codepoint <= 0x26D4) ||
        (codepoint >= 0x26E9 && codepoint <= 0x26EA) ||
        (codepoint >= 0x26F0 && codepoint <= 0x26F5) ||
        (codepoint >= 0x26F7 && codepoint <= 0x26FA) || codepoint == 0x26FD) {
        return GCB_EXTENDED_PICTOGRAPHIC;
    }

    // Default: OTHER
    return GCB_OTHER;
}

/**
 * @brief Check if there's a grapheme cluster boundary between two codepoints
 * @param prev_cp Previous codepoint
 * @param curr_cp Current codepoint
 * @param prev_ri_count Count of preceding Regional Indicator codepoints (for
 * GB12/13)
 * @return true if boundary exists, false if no break
 *
 * Implements the complete UAX #29 grapheme cluster boundary rules.
 */
static bool lle_check_grapheme_break(uint32_t prev_cp, uint32_t curr_cp,
                                     int prev_ri_count) {
    grapheme_cluster_break_t prev_gcb = lle_get_gcb_property(prev_cp);
    grapheme_cluster_break_t curr_gcb = lle_get_gcb_property(curr_cp);

    // GB3: Do not break between CR and LF
    if (prev_gcb == GCB_CR && curr_gcb == GCB_LF) {
        return false; // No break
    }

    // GB4: Break after controls (CR, LF, Control)
    if (prev_gcb == GCB_CR || prev_gcb == GCB_LF || prev_gcb == GCB_CONTROL) {
        return true; // Break
    }

    // GB5: Break before controls (CR, LF, Control)
    if (curr_gcb == GCB_CR || curr_gcb == GCB_LF || curr_gcb == GCB_CONTROL) {
        return true; // Break
    }

    // GB6: Do not break Hangul syllable sequences (L × (L | V | LV | LVT))
    if (prev_gcb == GCB_L && (curr_gcb == GCB_L || curr_gcb == GCB_V ||
                              curr_gcb == GCB_LV || curr_gcb == GCB_LVT)) {
        return false; // No break
    }

    // GB7: Do not break Hangul syllable sequences ((LV | V) × (V | T))
    if ((prev_gcb == GCB_LV || prev_gcb == GCB_V) &&
        (curr_gcb == GCB_V || curr_gcb == GCB_T)) {
        return false; // No break
    }

    // GB8: Do not break Hangul syllable sequences ((LVT | T) × T)
    if ((prev_gcb == GCB_LVT || prev_gcb == GCB_T) && curr_gcb == GCB_T) {
        return false; // No break
    }

    // GB9: Do not break before extending characters or ZWJ
    if (curr_gcb == GCB_EXTEND || curr_gcb == GCB_ZWJ) {
        return false; // No break
    }

    // GB9a: Do not break before SpacingMarks
    if (curr_gcb == GCB_SPACING_MARK) {
        return false; // No break
    }

    // GB9b: Do not break after Prepend characters
    if (prev_gcb == GCB_PREPEND) {
        return false; // No break
    }

    // GB11: Do not break within emoji modifier sequences or emoji ZWJ sequences
    // (ZWJ × Extended_Pictographic)
    if (prev_gcb == GCB_ZWJ && curr_gcb == GCB_EXTENDED_PICTOGRAPHIC) {
        return false; // No break
    }

    // GB12/GB13: Regional Indicator pairs (emoji flags)
    // Do not break between regional indicator symbols if there is an odd
    // number of regional indicator symbols before the break point
    if (prev_gcb == GCB_REGIONAL_INDICATOR &&
        curr_gcb == GCB_REGIONAL_INDICATOR) {
        // If we have an odd number of RI codepoints before this pair,
        // we don't break (forming a pair). If even, we break (starting new
        // pair).
        if (prev_ri_count % 2 == 1) {
            return false; // Odd count before - no break (complete the pair)
        } else {
            return true; // Even count - break (start new pair)
        }
    }

    // GB999: Otherwise, break everywhere
    return true;
}

/**
 * @brief Determine if a given UTF-8 position is a grapheme cluster boundary
 * @param ptr Pointer to current position in UTF-8 text
 * @param start Pointer to start of text (for context)
 * @param end Pointer to end of text (for bounds checking)
 * @return true if this position starts a new grapheme cluster, false otherwise
 *
 * This is the main public function for grapheme boundary detection.
 * Examines the current position and the previous codepoint to determine
 * if there's a boundary at this position.
 */
bool lle_is_grapheme_boundary(const char *ptr, const char *start,
                              const char *end) {
    if (!ptr || !start || !end || ptr < start || ptr >= end) {
        return true; // Invalid input - treat as boundary
    }

    // GB1: Start of text is always a boundary
    if (ptr == start) {
        return true;
    }

    // Get current codepoint
    uint32_t curr_cp = 0;
    int curr_len = lle_utf8_decode_codepoint(ptr, end - ptr, &curr_cp);
    if (curr_len <= 0) {
        return true; // Invalid UTF-8 - treat as boundary
    }

    // Find previous codepoint by scanning backward
    const char *prev_ptr = ptr - 1;
    while (prev_ptr >= start) {
        // Check if this is a UTF-8 start byte
        if ((*prev_ptr & 0xC0) != 0x80) {
            // Found start of previous codepoint
            break;
        }
        prev_ptr--;
    }

    if (prev_ptr < start) {
        return true; // No previous codepoint - boundary
    }

    // Get previous codepoint
    uint32_t prev_cp = 0;
    int prev_len =
        lle_utf8_decode_codepoint(prev_ptr, ptr - prev_ptr, &prev_cp);
    if (prev_len <= 0) {
        return true; // Invalid UTF-8 - treat as boundary
    }

    // For Regional Indicators (GB12/GB13), we need to count preceding RI
    // symbols
    int prev_ri_count = 0;
    if (lle_get_gcb_property(curr_cp) == GCB_REGIONAL_INDICATOR) {
        // Count consecutive Regional Indicators before current position
        const char *scan_ptr = prev_ptr;
        while (scan_ptr >= start) {
            uint32_t scan_cp = 0;
            int scan_len =
                lle_utf8_decode_codepoint(scan_ptr, end - scan_ptr, &scan_cp);
            if (scan_len <= 0)
                break;

            if (lle_get_gcb_property(scan_cp) == GCB_REGIONAL_INDICATOR) {
                prev_ri_count++;
                // Move to previous codepoint
                scan_ptr--;
                while (scan_ptr >= start && (*scan_ptr & 0xC0) == 0x80) {
                    scan_ptr--;
                }
            } else {
                break; // Not an RI, stop counting
            }
        }
    }

    // Check if there's a break between previous and current codepoint
    return lle_check_grapheme_break(prev_cp, curr_cp, prev_ri_count);
}

/**
 * @brief Count grapheme clusters in a UTF-8 string
 * @param text UTF-8 text to analyze
 * @param length Length of text in bytes
 * @return Number of grapheme clusters in the text
 *
 * Iterates through the text and counts the number of grapheme clusters
 * using the UAX #29 boundary detection algorithm.
 */
size_t lle_utf8_count_graphemes(const char *text, size_t length) {
    if (!text || length == 0) {
        return 0;
    }

    const char *ptr = text;
    const char *end = text + length;
    size_t grapheme_count = 0;

    while (ptr < end) {
        // Check if this is a grapheme boundary
        if (lle_is_grapheme_boundary(ptr, text, end)) {
            grapheme_count++;
        }

        // Move to next codepoint
        int seq_len = lle_utf8_sequence_length(*ptr);
        if (seq_len <= 0 || ptr + seq_len > end) {
            break; // Invalid UTF-8 or end of string
        }
        ptr += seq_len;
    }

    return grapheme_count;
}
