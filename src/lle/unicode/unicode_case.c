/**
 * @file unicode_case.c
 * @brief Unicode Case Conversion Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements Unicode-aware case conversion for the shell.
 *
 * Uses lookup tables for Latin-1 Supplement, Latin Extended-A/B, and Greek
 * which cover the vast majority of real-world shell usage scenarios.
 *
 * Reference: Unicode Standard, Chapter 3 (Conformance) and Chapter 4
 * (Character Properties), specifically the Simple_Uppercase_Mapping
 * and Simple_Lowercase_Mapping properties.
 */

#include "lle/unicode_case.h"
#include "lle/utf8_support.h"
#include <string.h>

/* ============================================================================
 * UNICODE CASE MAPPING TABLES
 *
 * These tables cover common characters that have case mappings.
 * Focus on Latin-1 Supplement (U+0080-U+00FF), Latin Extended-A
 * (U+0100-U+017F), Latin Extended-B (U+0180-U+024F), and Greek
 * (U+0370-U+03FF) which covers most European languages and Greek.
 *
 * Tables are sorted by codepoint for binary search lookup.
 * ============================================================================
 */

/**
 * @brief Case mapping entry (lowercase -> uppercase)
 */
typedef struct {
    uint32_t lower;
    uint32_t upper;
} case_map_entry_t;

/**
 * @brief Uppercase to lowercase mappings
 *
 * Sorted by uppercase codepoint for binary search.
 * Covers Latin-1 Supplement, Latin Extended-A/B, Greek.
 */
static const case_map_entry_t upper_to_lower_table[] = {
    /// Latin-1 Supplement uppercase (U+00C0-U+00DE)
    {0x00C0, 0x00E0}, /// À -> à
    {0x00C1, 0x00E1}, /// Á -> á
    {0x00C2, 0x00E2}, /// Â -> â
    {0x00C3, 0x00E3}, /// Ã -> ã
    {0x00C4, 0x00E4}, /// Ä -> ä
    {0x00C5, 0x00E5}, /// Å -> å
    {0x00C6, 0x00E6}, /// Æ -> æ
    {0x00C7, 0x00E7}, /// Ç -> ç
    {0x00C8, 0x00E8}, /// È -> è
    {0x00C9, 0x00E9}, /// É -> é
    {0x00CA, 0x00EA}, /// Ê -> ê
    {0x00CB, 0x00EB}, /// Ë -> ë
    {0x00CC, 0x00EC}, /// Ì -> ì
    {0x00CD, 0x00ED}, /// Í -> í
    {0x00CE, 0x00EE}, /// Î -> î
    {0x00CF, 0x00EF}, /// Ï -> ï
    {0x00D0, 0x00F0}, /// Ð -> ð
    {0x00D1, 0x00F1}, /// Ñ -> ñ
    {0x00D2, 0x00F2}, /// Ò -> ò
    {0x00D3, 0x00F3}, /// Ó -> ó
    {0x00D4, 0x00F4}, /// Ô -> ô
    {0x00D5, 0x00F5}, /// Õ -> õ
    {0x00D6, 0x00F6}, /// Ö -> ö
    /// 0x00D7 is × (multiplication sign), not a letter
    {0x00D8, 0x00F8}, /// Ø -> ø
    {0x00D9, 0x00F9}, /// Ù -> ù
    {0x00DA, 0x00FA}, /// Ú -> ú
    {0x00DB, 0x00FB}, /// Û -> û
    {0x00DC, 0x00FC}, /// Ü -> ü
    {0x00DD, 0x00FD}, /// Ý -> ý
    {0x00DE, 0x00FE}, /// Þ -> þ

    /// Latin Extended-A (U+0100-U+017F) - pairs
    {0x0100, 0x0101}, /// Ā -> ā
    {0x0102, 0x0103}, /// Ă -> ă
    {0x0104, 0x0105}, /// Ą -> ą
    {0x0106, 0x0107}, /// Ć -> ć
    {0x0108, 0x0109}, /// Ĉ -> ĉ
    {0x010A, 0x010B}, /// Ċ -> ċ
    {0x010C, 0x010D}, /// Č -> č
    {0x010E, 0x010F}, /// Ď -> ď
    {0x0110, 0x0111}, /// Đ -> đ
    {0x0112, 0x0113}, /// Ē -> ē
    {0x0114, 0x0115}, /// Ĕ -> ĕ
    {0x0116, 0x0117}, /// Ė -> ė
    {0x0118, 0x0119}, /// Ę -> ę
    {0x011A, 0x011B}, /// Ě -> ě
    {0x011C, 0x011D}, /// Ĝ -> ĝ
    {0x011E, 0x011F}, /// Ğ -> ğ
    {0x0120, 0x0121}, /// Ġ -> ġ
    {0x0122, 0x0123}, /// Ģ -> ģ
    {0x0124, 0x0125}, /// Ĥ -> ĥ
    {0x0126, 0x0127}, /// Ħ -> ħ
    {0x0128, 0x0129}, /// Ĩ -> ĩ
    {0x012A, 0x012B}, /// Ī -> ī
    {0x012C, 0x012D}, /// Ĭ -> ĭ
    {0x012E, 0x012F}, /// Į -> į
    {0x0130, 0x0069}, /// İ -> i (Turkish dotted I)
    {0x0132, 0x0133}, /// Ĳ -> ĳ
    {0x0134, 0x0135}, /// Ĵ -> ĵ
    {0x0136, 0x0137}, /// Ķ -> ķ
    {0x0139, 0x013A}, /// Ĺ -> ĺ
    {0x013B, 0x013C}, /// Ļ -> ļ
    {0x013D, 0x013E}, /// Ľ -> ľ
    {0x013F, 0x0140}, /// Ŀ -> ŀ
    {0x0141, 0x0142}, /// Ł -> ł
    {0x0143, 0x0144}, /// Ń -> ń
    {0x0145, 0x0146}, /// Ņ -> ņ
    {0x0147, 0x0148}, /// Ň -> ň
    {0x014A, 0x014B}, /// Ŋ -> ŋ
    {0x014C, 0x014D}, /// Ō -> ō
    {0x014E, 0x014F}, /// Ŏ -> ŏ
    {0x0150, 0x0151}, /// Ő -> ő
    {0x0152, 0x0153}, /// Œ -> œ
    {0x0154, 0x0155}, /// Ŕ -> ŕ
    {0x0156, 0x0157}, /// Ŗ -> ŗ
    {0x0158, 0x0159}, /// Ř -> ř
    {0x015A, 0x015B}, /// Ś -> ś
    {0x015C, 0x015D}, /// Ŝ -> ŝ
    {0x015E, 0x015F}, /// Ş -> ş
    {0x0160, 0x0161}, /// Š -> š
    {0x0162, 0x0163}, /// Ţ -> ţ
    {0x0164, 0x0165}, /// Ť -> ť
    {0x0166, 0x0167}, /// Ŧ -> ŧ
    {0x0168, 0x0169}, /// Ũ -> ũ
    {0x016A, 0x016B}, /// Ū -> ū
    {0x016C, 0x016D}, /// Ŭ -> ŭ
    {0x016E, 0x016F}, /// Ů -> ů
    {0x0170, 0x0171}, /// Ű -> ű
    {0x0172, 0x0173}, /// Ų -> ų
    {0x0174, 0x0175}, /// Ŵ -> ŵ
    {0x0176, 0x0177}, /// Ŷ -> ŷ
    {0x0178, 0x00FF}, /// Ÿ -> ÿ
    {0x0179, 0x017A}, /// Ź -> ź
    {0x017B, 0x017C}, /// Ż -> ż
    {0x017D, 0x017E}, /// Ž -> ž

    /// Latin Extended-B (selected common characters)
    {0x0181, 0x0253}, /// Ɓ -> ɓ
    {0x0182, 0x0183}, /// Ƃ -> ƃ
    {0x0184, 0x0185}, /// Ƅ -> ƅ
    {0x0186, 0x0254}, /// Ɔ -> ɔ
    {0x0187, 0x0188}, /// Ƈ -> ƈ
    {0x0189, 0x0256}, /// Ɖ -> ɖ
    {0x018A, 0x0257}, /// Ɗ -> ɗ
    {0x018B, 0x018C}, /// Ƌ -> ƌ
    {0x018E, 0x01DD}, /// Ǝ -> ǝ
    {0x018F, 0x0259}, /// Ə -> ə
    {0x0190, 0x025B}, /// Ɛ -> ɛ
    {0x0191, 0x0192}, /// Ƒ -> ƒ
    {0x0193, 0x0260}, /// Ɠ -> ɠ
    {0x0194, 0x0263}, /// Ɣ -> ɣ
    {0x0196, 0x0269}, /// Ɩ -> ɩ
    {0x0197, 0x0268}, /// Ɨ -> ɨ
    {0x0198, 0x0199}, /// Ƙ -> ƙ
    {0x019C, 0x026F}, /// Ɯ -> ɯ
    {0x019D, 0x0272}, /// Ɲ -> ɲ
    {0x019F, 0x0275}, /// Ɵ -> ɵ
    {0x01A0, 0x01A1}, /// Ơ -> ơ
    {0x01A2, 0x01A3}, /// Ƣ -> ƣ
    {0x01A4, 0x01A5}, /// Ƥ -> ƥ
    {0x01A6, 0x0280}, /// Ʀ -> ʀ
    {0x01A7, 0x01A8}, /// Ƨ -> ƨ
    {0x01A9, 0x0283}, /// Ʃ -> ʃ
    {0x01AC, 0x01AD}, /// Ƭ -> ƭ
    {0x01AE, 0x0288}, /// Ʈ -> ʈ
    {0x01AF, 0x01B0}, /// Ư -> ư
    {0x01B1, 0x028A}, /// Ʊ -> ʊ
    {0x01B2, 0x028B}, /// Ʋ -> ʋ
    {0x01B3, 0x01B4}, /// Ƴ -> ƴ
    {0x01B5, 0x01B6}, /// Ƶ -> ƶ
    {0x01B7, 0x0292}, /// Ʒ -> ʒ
    {0x01B8, 0x01B9}, /// Ƹ -> ƹ
    {0x01BC, 0x01BD}, /// Ƽ -> ƽ
    {0x01C4, 0x01C6}, /// Ǆ -> ǆ (DZ digraph)
    {0x01C5, 0x01C6}, /// ǅ -> ǆ (titlecase to lower)
    {0x01C7, 0x01C9}, /// Ǉ -> ǉ (LJ digraph)
    {0x01C8, 0x01C9}, /// ǈ -> ǉ
    {0x01CA, 0x01CC}, /// Ǌ -> ǌ (NJ digraph)
    {0x01CB, 0x01CC}, /// ǋ -> ǌ
    {0x01CD, 0x01CE}, /// Ǎ -> ǎ
    {0x01CF, 0x01D0}, /// Ǐ -> ǐ
    {0x01D1, 0x01D2}, /// Ǒ -> ǒ
    {0x01D3, 0x01D4}, /// Ǔ -> ǔ
    {0x01D5, 0x01D6}, /// Ǖ -> ǖ
    {0x01D7, 0x01D8}, /// Ǘ -> ǘ
    {0x01D9, 0x01DA}, /// Ǚ -> ǚ
    {0x01DB, 0x01DC}, /// Ǜ -> ǜ
    {0x01DE, 0x01DF}, /// Ǟ -> ǟ
    {0x01E0, 0x01E1}, /// Ǡ -> ǡ
    {0x01E2, 0x01E3}, /// Ǣ -> ǣ
    {0x01E4, 0x01E5}, /// Ǥ -> ǥ
    {0x01E6, 0x01E7}, /// Ǧ -> ǧ
    {0x01E8, 0x01E9}, /// Ǩ -> ǩ
    {0x01EA, 0x01EB}, /// Ǫ -> ǫ
    {0x01EC, 0x01ED}, /// Ǭ -> ǭ
    {0x01EE, 0x01EF}, /// Ǯ -> ǯ
    {0x01F1, 0x01F3}, /// Ǳ -> ǳ (DZ digraph variant)
    {0x01F2, 0x01F3}, /// ǲ -> ǳ
    {0x01F4, 0x01F5}, /// Ǵ -> ǵ
    {0x01F6, 0x0195}, /// Ƕ -> ƕ
    {0x01F7, 0x01BF}, /// Ƿ -> ƿ
    {0x01F8, 0x01F9}, /// Ǹ -> ǹ
    {0x01FA, 0x01FB}, /// Ǻ -> ǻ
    {0x01FC, 0x01FD}, /// Ǽ -> ǽ
    {0x01FE, 0x01FF}, /// Ǿ -> ǿ
    {0x0200, 0x0201}, /// Ȁ -> ȁ
    {0x0202, 0x0203}, /// Ȃ -> ȃ
    {0x0204, 0x0205}, /// Ȅ -> ȅ
    {0x0206, 0x0207}, /// Ȇ -> ȇ
    {0x0208, 0x0209}, /// Ȉ -> ȉ
    {0x020A, 0x020B}, /// Ȋ -> ȋ
    {0x020C, 0x020D}, /// Ȍ -> ȍ
    {0x020E, 0x020F}, /// Ȏ -> ȏ
    {0x0210, 0x0211}, /// Ȑ -> ȑ
    {0x0212, 0x0213}, /// Ȓ -> ȓ
    {0x0214, 0x0215}, /// Ȕ -> ȕ
    {0x0216, 0x0217}, /// Ȗ -> ȗ
    {0x0218, 0x0219}, /// Ș -> ș
    {0x021A, 0x021B}, /// Ț -> ț
    {0x021C, 0x021D}, /// Ȝ -> ȝ
    {0x021E, 0x021F}, /// Ȟ -> ȟ
    {0x0220, 0x019E}, /// Ƞ -> ƞ
    {0x0222, 0x0223}, /// Ȣ -> ȣ
    {0x0224, 0x0225}, /// Ȥ -> ȥ
    {0x0226, 0x0227}, /// Ȧ -> ȧ
    {0x0228, 0x0229}, /// Ȩ -> ȩ
    {0x022A, 0x022B}, /// Ȫ -> ȫ
    {0x022C, 0x022D}, /// Ȭ -> ȭ
    {0x022E, 0x022F}, /// Ȯ -> ȯ
    {0x0230, 0x0231}, /// Ȱ -> ȱ
    {0x0232, 0x0233}, /// Ȳ -> ȳ
    {0x023A, 0x2C65}, /// Ⱥ -> ⱥ
    {0x023B, 0x023C}, /// Ȼ -> ȼ
    {0x023D, 0x019A}, /// Ƚ -> ƚ
    {0x023E, 0x2C66}, /// Ⱦ -> ⱦ
    {0x0241, 0x0242}, /// Ɂ -> ɂ
    {0x0243, 0x0180}, /// Ƀ -> ƀ
    {0x0244, 0x0289}, /// Ʉ -> ʉ
    {0x0245, 0x028C}, /// Ʌ -> ʌ
    {0x0246, 0x0247}, /// Ɇ -> ɇ
    {0x0248, 0x0249}, /// Ɉ -> ɉ
    {0x024A, 0x024B}, /// Ɋ -> ɋ
    {0x024C, 0x024D}, /// Ɍ -> ɍ
    {0x024E, 0x024F}, /// Ɏ -> ɏ

    /// Greek (U+0370-U+03FF)
    {0x0370, 0x0371}, /// Ͱ -> ͱ
    {0x0372, 0x0373}, /// Ͳ -> ͳ
    {0x0376, 0x0377}, /// Ͷ -> ͷ
    {0x037F, 0x03F3}, /// Ϳ -> ϳ
    {0x0386, 0x03AC}, /// Ά -> ά
    {0x0388, 0x03AD}, /// Έ -> έ
    {0x0389, 0x03AE}, /// Ή -> ή
    {0x038A, 0x03AF}, /// Ί -> ί
    {0x038C, 0x03CC}, /// Ό -> ό
    {0x038E, 0x03CD}, /// Ύ -> ύ
    {0x038F, 0x03CE}, /// Ώ -> ώ
    {0x0391, 0x03B1}, /// Α -> α
    {0x0392, 0x03B2}, /// Β -> β
    {0x0393, 0x03B3}, /// Γ -> γ
    {0x0394, 0x03B4}, /// Δ -> δ
    {0x0395, 0x03B5}, /// Ε -> ε
    {0x0396, 0x03B6}, /// Ζ -> ζ
    {0x0397, 0x03B7}, /// Η -> η
    {0x0398, 0x03B8}, /// Θ -> θ
    {0x0399, 0x03B9}, /// Ι -> ι
    {0x039A, 0x03BA}, /// Κ -> κ
    {0x039B, 0x03BB}, /// Λ -> λ
    {0x039C, 0x03BC}, /// Μ -> μ
    {0x039D, 0x03BD}, /// Ν -> ν
    {0x039E, 0x03BE}, /// Ξ -> ξ
    {0x039F, 0x03BF}, /// Ο -> ο
    {0x03A0, 0x03C0}, /// Π -> π
    {0x03A1, 0x03C1}, /// Ρ -> ρ
    {0x03A3, 0x03C3}, /// Σ -> σ
    {0x03A4, 0x03C4}, /// Τ -> τ
    {0x03A5, 0x03C5}, /// Υ -> υ
    {0x03A6, 0x03C6}, /// Φ -> φ
    {0x03A7, 0x03C7}, /// Χ -> χ
    {0x03A8, 0x03C8}, /// Ψ -> ψ
    {0x03A9, 0x03C9}, /// Ω -> ω
    {0x03AA, 0x03CA}, /// Ϊ -> ϊ
    {0x03AB, 0x03CB}, /// Ϋ -> ϋ
    {0x03CF, 0x03D7}, /// Ϗ -> ϗ
    {0x03D8, 0x03D9}, /// Ϙ -> ϙ
    {0x03DA, 0x03DB}, /// Ϛ -> ϛ
    {0x03DC, 0x03DD}, /// Ϝ -> ϝ
    {0x03DE, 0x03DF}, /// Ϟ -> ϟ
    {0x03E0, 0x03E1}, /// Ϡ -> ϡ
    {0x03E2, 0x03E3}, /// Ϣ -> ϣ
    {0x03E4, 0x03E5}, /// Ϥ -> ϥ
    {0x03E6, 0x03E7}, /// Ϧ -> ϧ
    {0x03E8, 0x03E9}, /// Ϩ -> ϩ
    {0x03EA, 0x03EB}, /// Ϫ -> ϫ
    {0x03EC, 0x03ED}, /// Ϭ -> ϭ
    {0x03EE, 0x03EF}, /// Ϯ -> ϯ
    {0x03F4, 0x03B8}, /// ϴ -> θ (theta symbol -> theta)
    {0x03F7, 0x03F8}, /// Ϸ -> ϸ
    {0x03F9, 0x03F2}, /// Ϲ -> ϲ (lunate sigma)
    {0x03FA, 0x03FB}, /// Ϻ -> ϻ
    {0x03FD, 0x037B}, /// Ͻ -> ͻ
    {0x03FE, 0x037C}, /// Ͼ -> ͼ
    {0x03FF, 0x037D}, /// Ͽ -> ͽ

    /// Cyrillic (U+0400-U+04FF) - common letters
    {0x0400, 0x0450}, /// Ѐ -> ѐ
    {0x0401, 0x0451}, /// Ё -> ё
    {0x0402, 0x0452}, /// Ђ -> ђ
    {0x0403, 0x0453}, /// Ѓ -> ѓ
    {0x0404, 0x0454}, /// Є -> є
    {0x0405, 0x0455}, /// Ѕ -> ѕ
    {0x0406, 0x0456}, /// І -> і
    {0x0407, 0x0457}, /// Ї -> ї
    {0x0408, 0x0458}, /// Ј -> ј
    {0x0409, 0x0459}, /// Љ -> љ
    {0x040A, 0x045A}, /// Њ -> њ
    {0x040B, 0x045B}, /// Ћ -> ћ
    {0x040C, 0x045C}, /// Ќ -> ќ
    {0x040D, 0x045D}, /// Ѝ -> ѝ
    {0x040E, 0x045E}, /// Ў -> ў
    {0x040F, 0x045F}, /// Џ -> џ
    {0x0410, 0x0430}, /// А -> а
    {0x0411, 0x0431}, /// Б -> б
    {0x0412, 0x0432}, /// В -> в
    {0x0413, 0x0433}, /// Г -> г
    {0x0414, 0x0434}, /// Д -> д
    {0x0415, 0x0435}, /// Е -> е
    {0x0416, 0x0436}, /// Ж -> ж
    {0x0417, 0x0437}, /// З -> з
    {0x0418, 0x0438}, /// И -> и
    {0x0419, 0x0439}, /// Й -> й
    {0x041A, 0x043A}, /// К -> к
    {0x041B, 0x043B}, /// Л -> л
    {0x041C, 0x043C}, /// М -> м
    {0x041D, 0x043D}, /// Н -> н
    {0x041E, 0x043E}, /// О -> о
    {0x041F, 0x043F}, /// П -> п
    {0x0420, 0x0440}, /// Р -> р
    {0x0421, 0x0441}, /// С -> с
    {0x0422, 0x0442}, /// Т -> т
    {0x0423, 0x0443}, /// У -> у
    {0x0424, 0x0444}, /// Ф -> ф
    {0x0425, 0x0445}, /// Х -> х
    {0x0426, 0x0446}, /// Ц -> ц
    {0x0427, 0x0447}, /// Ч -> ч
    {0x0428, 0x0448}, /// Ш -> ш
    {0x0429, 0x0449}, /// Щ -> щ
    {0x042A, 0x044A}, /// Ъ -> ъ
    {0x042B, 0x044B}, /// Ы -> ы
    {0x042C, 0x044C}, /// Ь -> ь
    {0x042D, 0x044D}, /// Э -> э
    {0x042E, 0x044E}, /// Ю -> ю
    {0x042F, 0x044F}, /// Я -> я
};

static const size_t upper_to_lower_table_size =
    sizeof(upper_to_lower_table) / sizeof(upper_to_lower_table[0]);

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Binary search for uppercase -> lowercase mapping
 */
static const case_map_entry_t *find_upper_to_lower(uint32_t cp) {
    size_t left = 0;
    size_t right = upper_to_lower_table_size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (upper_to_lower_table[mid].lower < cp) {
            left = mid + 1;
        } else if (upper_to_lower_table[mid].lower > cp) {
            right = mid;
        } else {
            return &upper_to_lower_table[mid];
        }
    }

    return NULL;
}

/**
 * @brief Linear search for lowercase -> uppercase mapping
 *
 * Since the table is sorted by uppercase, we need linear search for reverse.
 * Could add a separate sorted table if performance is critical.
 */
static uint32_t find_lower_to_upper(uint32_t cp) {
    for (size_t i = 0; i < upper_to_lower_table_size; i++) {
        if (upper_to_lower_table[i].upper == cp) {
            return upper_to_lower_table[i].lower;
        }
    }
    return cp; /// No mapping found
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Convert a Unicode codepoint to uppercase
 */
uint32_t lle_unicode_toupper_codepoint(uint32_t cp) {
    /// ASCII fast path
    if (cp >= 'a' && cp <= 'z') {
        return cp - ('a' - 'A');
    }

    /// Non-letter ASCII
    if (cp < 0x80) {
        return cp;
    }

    /// Table lookup for extended Latin, Greek, Cyrillic
    return find_lower_to_upper(cp);
}

/**
 * @brief Convert a Unicode codepoint to lowercase
 */
uint32_t lle_unicode_tolower_codepoint(uint32_t cp) {
    /// ASCII fast path
    if (cp >= 'A' && cp <= 'Z') {
        return cp + ('a' - 'A');
    }

    /// Non-letter ASCII
    if (cp < 0x80) {
        return cp;
    }

    /// Table lookup for extended Latin, Greek, Cyrillic
    const case_map_entry_t *entry = find_upper_to_lower(cp);
    if (entry) {
        return entry->upper;
    }

    return cp; /// No mapping found
}

/**
 * @brief Check if a codepoint is uppercase
 */
bool lle_unicode_is_upper(uint32_t cp) {
    /// ASCII
    if (cp >= 'A' && cp <= 'Z') {
        return true;
    }

    if (cp < 0x80) {
        return false;
    }

    /// Check if it has a lowercase mapping (meaning it's uppercase)
    const case_map_entry_t *entry = find_upper_to_lower(cp);
    return entry != NULL;
}

/**
 * @brief Check if a codepoint is lowercase
 */
bool lle_unicode_is_lower(uint32_t cp) {
    /// ASCII
    if (cp >= 'a' && cp <= 'z') {
        return true;
    }

    if (cp < 0x80) {
        return false;
    }

    /// Check if it exists as a lowercase target in the table
    for (size_t i = 0; i < upper_to_lower_table_size; i++) {
        if (upper_to_lower_table[i].upper == cp) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Convert a UTF-8 string to uppercase
 */
size_t lle_utf8_toupper(const char *input, size_t input_len, char *output,
                        size_t output_size) {
    if (!input || !output || output_size == 0) {
        return (size_t)-1;
    }

    const char *in_ptr = input;
    const char *in_end = input + input_len;
    char *out_ptr = output;
    size_t remaining = output_size - 1; /// Reserve for null terminator

    while (in_ptr < in_end) {
        uint32_t codepoint;
        int seq_len =
            lle_utf8_decode_codepoint(in_ptr, in_end - in_ptr, &codepoint);
        if (seq_len <= 0) {
            return (size_t)-1; /// Invalid UTF-8
        }

        /// Convert to uppercase
        uint32_t upper_cp = lle_unicode_toupper_codepoint(codepoint);

        /// Encode to output
        char buf[4];
        int out_len = lle_utf8_encode_codepoint(upper_cp, buf);
        if (out_len <= 0 || (size_t)out_len > remaining) {
            return (size_t)-1; /// Encoding error or buffer too small
        }

        memcpy(out_ptr, buf, out_len);
        out_ptr += out_len;
        remaining -= out_len;
        in_ptr += seq_len;
    }

    *out_ptr = '\0';
    return out_ptr - output;
}

/**
 * @brief Convert a UTF-8 string to lowercase
 */
size_t lle_utf8_tolower(const char *input, size_t input_len, char *output,
                        size_t output_size) {
    if (!input || !output || output_size == 0) {
        return (size_t)-1;
    }

    const char *in_ptr = input;
    const char *in_end = input + input_len;
    char *out_ptr = output;
    size_t remaining = output_size - 1; /// Reserve for null terminator

    while (in_ptr < in_end) {
        uint32_t codepoint;
        int seq_len =
            lle_utf8_decode_codepoint(in_ptr, in_end - in_ptr, &codepoint);
        if (seq_len <= 0) {
            return (size_t)-1; /// Invalid UTF-8
        }

        /// Convert to lowercase
        uint32_t lower_cp = lle_unicode_tolower_codepoint(codepoint);

        /// Encode to output
        char buf[4];
        int out_len = lle_utf8_encode_codepoint(lower_cp, buf);
        if (out_len <= 0 || (size_t)out_len > remaining) {
            return (size_t)-1; /// Encoding error or buffer too small
        }

        memcpy(out_ptr, buf, out_len);
        out_ptr += out_len;
        remaining -= out_len;
        in_ptr += seq_len;
    }

    *out_ptr = '\0';
    return out_ptr - output;
}

/**
 * @brief Convert first character of UTF-8 string to uppercase
 */
size_t lle_utf8_toupper_first(const char *input, size_t input_len, char *output,
                              size_t output_size) {
    if (!input || !output || output_size == 0) {
        return (size_t)-1;
    }

    if (input_len == 0) {
        output[0] = '\0';
        return 0;
    }

    const char *in_ptr = input;
    const char *in_end = input + input_len;
    char *out_ptr = output;
    size_t remaining = output_size - 1;

    /// Process first character
    uint32_t codepoint;
    int seq_len =
        lle_utf8_decode_codepoint(in_ptr, in_end - in_ptr, &codepoint);
    if (seq_len <= 0) {
        return (size_t)-1;
    }

    /// Convert first character to uppercase
    uint32_t upper_cp = lle_unicode_toupper_codepoint(codepoint);

    char buf[4];
    int out_len = lle_utf8_encode_codepoint(upper_cp, buf);
    if (out_len <= 0 || (size_t)out_len > remaining) {
        return (size_t)-1;
    }

    memcpy(out_ptr, buf, out_len);
    out_ptr += out_len;
    remaining -= out_len;
    in_ptr += seq_len;

    /// Copy rest of string unchanged
    size_t rest_len = in_end - in_ptr;
    if (rest_len > remaining) {
        return (size_t)-1; /// Buffer too small
    }

    memcpy(out_ptr, in_ptr, rest_len);
    out_ptr += rest_len;

    *out_ptr = '\0';
    return out_ptr - output;
}

/**
 * @brief Convert first character of UTF-8 string to lowercase
 */
size_t lle_utf8_tolower_first(const char *input, size_t input_len, char *output,
                              size_t output_size) {
    if (!input || !output || output_size == 0) {
        return (size_t)-1;
    }

    if (input_len == 0) {
        output[0] = '\0';
        return 0;
    }

    const char *in_ptr = input;
    const char *in_end = input + input_len;
    char *out_ptr = output;
    size_t remaining = output_size - 1;

    /// Process first character
    uint32_t codepoint;
    int seq_len =
        lle_utf8_decode_codepoint(in_ptr, in_end - in_ptr, &codepoint);
    if (seq_len <= 0) {
        return (size_t)-1;
    }

    /// Convert first character to lowercase
    uint32_t lower_cp = lle_unicode_tolower_codepoint(codepoint);

    char buf[4];
    int out_len = lle_utf8_encode_codepoint(lower_cp, buf);
    if (out_len <= 0 || (size_t)out_len > remaining) {
        return (size_t)-1;
    }

    memcpy(out_ptr, buf, out_len);
    out_ptr += out_len;
    remaining -= out_len;
    in_ptr += seq_len;

    /// Copy rest of string unchanged
    size_t rest_len = in_end - in_ptr;
    if (rest_len > remaining) {
        return (size_t)-1; /// Buffer too small
    }

    memcpy(out_ptr, in_ptr, rest_len);
    out_ptr += rest_len;

    *out_ptr = '\0';
    return out_ptr - output;
}

/**
 * @brief Case-fold a UTF-8 string for case-insensitive comparison
 *
 * Uses simple lowercase folding for now. Full Unicode case folding
 * would require additional tables for special cases like:
 * - German sharp s: ß -> ss
 * - Greek final sigma: ς -> σ
 * - etc.
 *
 * For shell usage, simple lowercase is sufficient for the vast majority
 * of real-world cases.
 */
size_t lle_utf8_casefold(const char *input, size_t input_len, char *output,
                         size_t output_size) {
    /// For now, case folding is equivalent to lowercase
    return lle_utf8_tolower(input, input_len, output, output_size);
}
