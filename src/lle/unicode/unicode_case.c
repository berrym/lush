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
    {0x00C0, 0x00E0}, /// A-grave -> a-grave
    {0x00C1, 0x00E1}, /// U+00C1 -> U+00E1
    {0x00C2, 0x00E2}, /// U+00C2 -> U+00E2
    {0x00C3, 0x00E3}, /// U+00C3 -> U+00E3
    {0x00C4, 0x00E4}, /// U+00C4 -> a-umlaut
    {0x00C5, 0x00E5}, /// U+00C5 -> U+00E5
    {0x00C6, 0x00E6}, /// U+00C6 -> U+00E6
    {0x00C7, 0x00E7}, /// U+00C7 -> c-cedilla
    {0x00C8, 0x00E8}, /// U+00C8 -> e-grave
    {0x00C9, 0x00E9}, /// E-acute -> e-acute
    {0x00CA, 0x00EA}, /// U+00CA -> U+00EA
    {0x00CB, 0x00EB}, /// U+00CB -> U+00EB
    {0x00CC, 0x00EC}, /// U+00CC -> U+00EC
    {0x00CD, 0x00ED}, /// U+00CD -> U+00ED
    {0x00CE, 0x00EE}, /// U+00CE -> U+00EE
    {0x00CF, 0x00EF}, /// U+00CF -> i-umlaut
    {0x00D0, 0x00F0}, /// U+00D0 -> U+00F0
    {0x00D1, 0x00F1}, /// U+00D1 -> n-tilde
    {0x00D2, 0x00F2}, /// U+00D2 -> U+00F2
    {0x00D3, 0x00F3}, /// U+00D3 -> U+00F3
    {0x00D4, 0x00F4}, /// U+00D4 -> U+00F4
    {0x00D5, 0x00F5}, /// U+00D5 -> U+00F5
    {0x00D6, 0x00F6}, /// U+00D6 -> o-umlaut
    /// 0x00D7 is x (multiplication sign), not a letter
    {0x00D8, 0x00F8}, /// U+00D8 -> U+00F8
    {0x00D9, 0x00F9}, /// U+00D9 -> U+00F9
    {0x00DA, 0x00FA}, /// U+00DA -> U+00FA
    {0x00DB, 0x00FB}, /// U+00DB -> U+00FB
    {0x00DC, 0x00FC}, /// U+00DC -> u-umlaut
    {0x00DD, 0x00FD}, /// U+00DD -> U+00FD
    {0x00DE, 0x00FE}, /// U+00DE -> U+00FE

    /// Latin Extended-A (U+0100-U+017F) - pairs
    {0x0100, 0x0101}, /// U+0100 -> U+0101
    {0x0102, 0x0103}, /// U+0102 -> U+0103
    {0x0104, 0x0105}, /// U+0104 -> U+0105
    {0x0106, 0x0107}, /// U+0106 -> U+0107
    {0x0108, 0x0109}, /// U+0108 -> U+0109
    {0x010A, 0x010B}, /// U+010A -> U+010B
    {0x010C, 0x010D}, /// U+010C -> U+010D
    {0x010E, 0x010F}, /// U+010E -> U+010F
    {0x0110, 0x0111}, /// U+0110 -> U+0111
    {0x0112, 0x0113}, /// U+0112 -> U+0113
    {0x0114, 0x0115}, /// U+0114 -> U+0115
    {0x0116, 0x0117}, /// U+0116 -> U+0117
    {0x0118, 0x0119}, /// U+0118 -> U+0119
    {0x011A, 0x011B}, /// U+011A -> U+011B
    {0x011C, 0x011D}, /// U+011C -> U+011D
    {0x011E, 0x011F}, /// U+011E -> U+011F
    {0x0120, 0x0121}, /// U+0120 -> U+0121
    {0x0122, 0x0123}, /// U+0122 -> U+0123
    {0x0124, 0x0125}, /// U+0124 -> U+0125
    {0x0126, 0x0127}, /// U+0126 -> U+0127
    {0x0128, 0x0129}, /// U+0128 -> U+0129
    {0x012A, 0x012B}, /// U+012A -> U+012B
    {0x012C, 0x012D}, /// U+012C -> U+012D
    {0x012E, 0x012F}, /// U+012E -> U+012F
    {0x0130, 0x0069}, /// U+0130 -> i (Turkish dotted I)
    {0x0132, 0x0133}, /// U+0132 -> U+0133
    {0x0134, 0x0135}, /// U+0134 -> U+0135
    {0x0136, 0x0137}, /// U+0136 -> U+0137
    {0x0139, 0x013A}, /// U+0139 -> U+013A
    {0x013B, 0x013C}, /// U+013B -> U+013C
    {0x013D, 0x013E}, /// U+013D -> U+013E
    {0x013F, 0x0140}, /// U+013F -> U+0140
    {0x0141, 0x0142}, /// U+0141 -> U+0142
    {0x0143, 0x0144}, /// U+0143 -> U+0144
    {0x0145, 0x0146}, /// U+0145 -> U+0146
    {0x0147, 0x0148}, /// U+0147 -> U+0148
    {0x014A, 0x014B}, /// U+014A -> U+014B
    {0x014C, 0x014D}, /// U+014C -> U+014D
    {0x014E, 0x014F}, /// U+014E -> U+014F
    {0x0150, 0x0151}, /// U+0150 -> U+0151
    {0x0152, 0x0153}, /// U+0152 -> U+0153
    {0x0154, 0x0155}, /// U+0154 -> U+0155
    {0x0156, 0x0157}, /// U+0156 -> U+0157
    {0x0158, 0x0159}, /// U+0158 -> U+0159
    {0x015A, 0x015B}, /// U+015A -> U+015B
    {0x015C, 0x015D}, /// U+015C -> U+015D
    {0x015E, 0x015F}, /// U+015E -> U+015F
    {0x0160, 0x0161}, /// U+0160 -> U+0161
    {0x0162, 0x0163}, /// U+0162 -> U+0163
    {0x0164, 0x0165}, /// U+0164 -> U+0165
    {0x0166, 0x0167}, /// U+0166 -> U+0167
    {0x0168, 0x0169}, /// U+0168 -> U+0169
    {0x016A, 0x016B}, /// U+016A -> U+016B
    {0x016C, 0x016D}, /// U+016C -> U+016D
    {0x016E, 0x016F}, /// U+016E -> U+016F
    {0x0170, 0x0171}, /// U+0170 -> U+0171
    {0x0172, 0x0173}, /// U+0172 -> U+0173
    {0x0174, 0x0175}, /// U+0174 -> U+0175
    {0x0176, 0x0177}, /// U+0176 -> U+0177
    {0x0178, 0x00FF}, /// U+0178 -> y-umlaut
    {0x0179, 0x017A}, /// U+0179 -> U+017A
    {0x017B, 0x017C}, /// U+017B -> U+017C
    {0x017D, 0x017E}, /// U+017D -> U+017E

    /// Latin Extended-B (selected common characters)
    {0x0181, 0x0253}, /// U+0181 -> U+0253
    {0x0182, 0x0183}, /// U+0182 -> U+0183
    {0x0184, 0x0185}, /// U+0184 -> U+0185
    {0x0186, 0x0254}, /// U+0186 -> U+0254
    {0x0187, 0x0188}, /// U+0187 -> U+0188
    {0x0189, 0x0256}, /// U+0189 -> U+0256
    {0x018A, 0x0257}, /// U+018A -> U+0257
    {0x018B, 0x018C}, /// U+018B -> U+018C
    {0x018E, 0x01DD}, /// U+018E -> U+01DD
    {0x018F, 0x0259}, /// U+018F -> U+0259
    {0x0190, 0x025B}, /// U+0190 -> U+025B
    {0x0191, 0x0192}, /// U+0191 -> U+0192
    {0x0193, 0x0260}, /// U+0193 -> U+0260
    {0x0194, 0x0263}, /// U+0194 -> U+0263
    {0x0196, 0x0269}, /// U+0196 -> U+0269
    {0x0197, 0x0268}, /// U+0197 -> U+0268
    {0x0198, 0x0199}, /// U+0198 -> U+0199
    {0x019C, 0x026F}, /// U+019C -> U+026F
    {0x019D, 0x0272}, /// U+019D -> U+0272
    {0x019F, 0x0275}, /// U+019F -> U+0275
    {0x01A0, 0x01A1}, /// U+01A0 -> U+01A1
    {0x01A2, 0x01A3}, /// U+01A2 -> U+01A3
    {0x01A4, 0x01A5}, /// U+01A4 -> U+01A5
    {0x01A6, 0x0280}, /// U+01A6 -> U+0280
    {0x01A7, 0x01A8}, /// U+01A7 -> U+01A8
    {0x01A9, 0x0283}, /// U+01A9 -> U+0283
    {0x01AC, 0x01AD}, /// U+01AC -> U+01AD
    {0x01AE, 0x0288}, /// U+01AE -> U+0288
    {0x01AF, 0x01B0}, /// U+01AF -> U+01B0
    {0x01B1, 0x028A}, /// U+01B1 -> U+028A
    {0x01B2, 0x028B}, /// U+01B2 -> U+028B
    {0x01B3, 0x01B4}, /// U+01B3 -> U+01B4
    {0x01B5, 0x01B6}, /// U+01B5 -> U+01B6
    {0x01B7, 0x0292}, /// U+01B7 -> U+0292
    {0x01B8, 0x01B9}, /// U+01B8 -> U+01B9
    {0x01BC, 0x01BD}, /// U+01BC -> U+01BD
    {0x01C4, 0x01C6}, /// U+01C4 -> U+01C6 (DZ digraph)
    {0x01C5, 0x01C6}, /// U+01C5 -> U+01C6 (titlecase to lower)
    {0x01C7, 0x01C9}, /// U+01C7 -> U+01C9 (LJ digraph)
    {0x01C8, 0x01C9}, /// U+01C8 -> U+01C9
    {0x01CA, 0x01CC}, /// U+01CA -> U+01CC (NJ digraph)
    {0x01CB, 0x01CC}, /// U+01CB -> U+01CC
    {0x01CD, 0x01CE}, /// U+01CD -> U+01CE
    {0x01CF, 0x01D0}, /// U+01CF -> U+01D0
    {0x01D1, 0x01D2}, /// U+01D1 -> U+01D2
    {0x01D3, 0x01D4}, /// U+01D3 -> U+01D4
    {0x01D5, 0x01D6}, /// U+01D5 -> U+01D6
    {0x01D7, 0x01D8}, /// U+01D7 -> U+01D8
    {0x01D9, 0x01DA}, /// U+01D9 -> U+01DA
    {0x01DB, 0x01DC}, /// U+01DB -> U+01DC
    {0x01DE, 0x01DF}, /// U+01DE -> U+01DF
    {0x01E0, 0x01E1}, /// U+01E0 -> U+01E1
    {0x01E2, 0x01E3}, /// U+01E2 -> U+01E3
    {0x01E4, 0x01E5}, /// U+01E4 -> U+01E5
    {0x01E6, 0x01E7}, /// U+01E6 -> U+01E7
    {0x01E8, 0x01E9}, /// U+01E8 -> U+01E9
    {0x01EA, 0x01EB}, /// U+01EA -> U+01EB
    {0x01EC, 0x01ED}, /// U+01EC -> U+01ED
    {0x01EE, 0x01EF}, /// U+01EE -> U+01EF
    {0x01F1, 0x01F3}, /// U+01F1 -> U+01F3 (DZ digraph variant)
    {0x01F2, 0x01F3}, /// U+01F2 -> U+01F3
    {0x01F4, 0x01F5}, /// U+01F4 -> U+01F5
    {0x01F6, 0x0195}, /// U+01F6 -> U+0195
    {0x01F7, 0x01BF}, /// U+01F7 -> U+01BF
    {0x01F8, 0x01F9}, /// U+01F8 -> U+01F9
    {0x01FA, 0x01FB}, /// U+01FA -> U+01FB
    {0x01FC, 0x01FD}, /// U+01FC -> U+01FD
    {0x01FE, 0x01FF}, /// U+01FE -> U+01FF
    {0x0200, 0x0201}, /// U+0200 -> U+0201
    {0x0202, 0x0203}, /// U+0202 -> U+0203
    {0x0204, 0x0205}, /// U+0204 -> U+0205
    {0x0206, 0x0207}, /// U+0206 -> U+0207
    {0x0208, 0x0209}, /// U+0208 -> U+0209
    {0x020A, 0x020B}, /// U+020A -> U+020B
    {0x020C, 0x020D}, /// U+020C -> U+020D
    {0x020E, 0x020F}, /// U+020E -> U+020F
    {0x0210, 0x0211}, /// U+0210 -> U+0211
    {0x0212, 0x0213}, /// U+0212 -> U+0213
    {0x0214, 0x0215}, /// U+0214 -> U+0215
    {0x0216, 0x0217}, /// U+0216 -> U+0217
    {0x0218, 0x0219}, /// U+0218 -> U+0219
    {0x021A, 0x021B}, /// U+021A -> U+021B
    {0x021C, 0x021D}, /// U+021C -> U+021D
    {0x021E, 0x021F}, /// U+021E -> U+021F
    {0x0220, 0x019E}, /// U+0220 -> U+019E
    {0x0222, 0x0223}, /// U+0222 -> U+0223
    {0x0224, 0x0225}, /// U+0224 -> U+0225
    {0x0226, 0x0227}, /// U+0226 -> U+0227
    {0x0228, 0x0229}, /// U+0228 -> U+0229
    {0x022A, 0x022B}, /// U+022A -> U+022B
    {0x022C, 0x022D}, /// U+022C -> U+022D
    {0x022E, 0x022F}, /// U+022E -> U+022F
    {0x0230, 0x0231}, /// U+0230 -> U+0231
    {0x0232, 0x0233}, /// U+0232 -> U+0233
    {0x023A, 0x2C65}, /// U+023A -> U+2C65
    {0x023B, 0x023C}, /// U+023B -> U+023C
    {0x023D, 0x019A}, /// U+023D -> U+019A
    {0x023E, 0x2C66}, /// U+023E -> U+2C66
    {0x0241, 0x0242}, /// U+0241 -> U+0242
    {0x0243, 0x0180}, /// U+0243 -> U+0180
    {0x0244, 0x0289}, /// U+0244 -> U+0289
    {0x0245, 0x028C}, /// U+0245 -> U+028C
    {0x0246, 0x0247}, /// U+0246 -> U+0247
    {0x0248, 0x0249}, /// U+0248 -> U+0249
    {0x024A, 0x024B}, /// U+024A -> U+024B
    {0x024C, 0x024D}, /// U+024C -> U+024D
    {0x024E, 0x024F}, /// U+024E -> U+024F

    /// Greek (U+0370-U+03FF)
    {0x0370, 0x0371}, /// U+0370 -> U+0371
    {0x0372, 0x0373}, /// U+0372 -> U+0373
    {0x0376, 0x0377}, /// U+0376 -> U+0377
    {0x037F, 0x03F3}, /// U+037F -> U+03F3
    {0x0386, 0x03AC}, /// U+0386 -> U+03AC
    {0x0388, 0x03AD}, /// U+0388 -> U+03AD
    {0x0389, 0x03AE}, /// U+0389 -> U+03AE
    {0x038A, 0x03AF}, /// U+038A -> U+03AF
    {0x038C, 0x03CC}, /// U+038C -> U+03CC
    {0x038E, 0x03CD}, /// U+038E -> U+03CD
    {0x038F, 0x03CE}, /// U+038F -> U+03CE
    {0x0391, 0x03B1}, /// U+0391 -> alpha
    {0x0392, 0x03B2}, /// U+0392 -> beta
    {0x0393, 0x03B3}, /// U+0393 -> U+03B3
    {0x0394, 0x03B4}, /// U+0394 -> U+03B4
    {0x0395, 0x03B5}, /// U+0395 -> U+03B5
    {0x0396, 0x03B6}, /// U+0396 -> U+03B6
    {0x0397, 0x03B7}, /// U+0397 -> U+03B7
    {0x0398, 0x03B8}, /// U+0398 -> U+03B8
    {0x0399, 0x03B9}, /// U+0399 -> U+03B9
    {0x039A, 0x03BA}, /// U+039A -> U+03BA
    {0x039B, 0x03BB}, /// U+039B -> U+03BB
    {0x039C, 0x03BC}, /// U+039C -> mu
    {0x039D, 0x03BD}, /// U+039D -> U+03BD
    {0x039E, 0x03BE}, /// U+039E -> U+03BE
    {0x039F, 0x03BF}, /// U+039F -> U+03BF
    {0x03A0, 0x03C0}, /// U+03A0 -> U+03C0
    {0x03A1, 0x03C1}, /// U+03A1 -> U+03C1
    {0x03A3, 0x03C3}, /// Sigma -> U+03C3
    {0x03A4, 0x03C4}, /// U+03A4 -> U+03C4
    {0x03A5, 0x03C5}, /// U+03A5 -> U+03C5
    {0x03A6, 0x03C6}, /// U+03A6 -> U+03C6
    {0x03A7, 0x03C7}, /// U+03A7 -> U+03C7
    {0x03A8, 0x03C8}, /// U+03A8 -> U+03C8
    {0x03A9, 0x03C9}, /// U+03A9 -> omega
    {0x03AA, 0x03CA}, /// U+03AA -> U+03CA
    {0x03AB, 0x03CB}, /// U+03AB -> U+03CB
    {0x03CF, 0x03D7}, /// U+03CF -> U+03D7
    {0x03D8, 0x03D9}, /// U+03D8 -> U+03D9
    {0x03DA, 0x03DB}, /// U+03DA -> U+03DB
    {0x03DC, 0x03DD}, /// U+03DC -> U+03DD
    {0x03DE, 0x03DF}, /// U+03DE -> U+03DF
    {0x03E0, 0x03E1}, /// U+03E0 -> U+03E1
    {0x03E2, 0x03E3}, /// U+03E2 -> U+03E3
    {0x03E4, 0x03E5}, /// U+03E4 -> U+03E5
    {0x03E6, 0x03E7}, /// U+03E6 -> U+03E7
    {0x03E8, 0x03E9}, /// U+03E8 -> U+03E9
    {0x03EA, 0x03EB}, /// U+03EA -> U+03EB
    {0x03EC, 0x03ED}, /// U+03EC -> U+03ED
    {0x03EE, 0x03EF}, /// U+03EE -> U+03EF
    {0x03F4, 0x03B8}, /// U+03F4 -> U+03B8 (theta symbol -> theta)
    {0x03F7, 0x03F8}, /// U+03F7 -> U+03F8
    {0x03F9, 0x03F2}, /// U+03F9 -> U+03F2 (lunate sigma)
    {0x03FA, 0x03FB}, /// U+03FA -> U+03FB
    {0x03FD, 0x037B}, /// U+03FD -> U+037B
    {0x03FE, 0x037C}, /// U+03FE -> U+037C
    {0x03FF, 0x037D}, /// U+03FF -> U+037D

    /// Cyrillic (U+0400-U+04FF) - common letters
    {0x0400, 0x0450}, /// U+0400 -> U+0450
    {0x0401, 0x0451}, /// U+0401 -> U+0451
    {0x0402, 0x0452}, /// U+0402 -> U+0452
    {0x0403, 0x0453}, /// U+0403 -> U+0453
    {0x0404, 0x0454}, /// U+0404 -> U+0454
    {0x0405, 0x0455}, /// U+0405 -> U+0455
    {0x0406, 0x0456}, /// U+0406 -> U+0456
    {0x0407, 0x0457}, /// U+0407 -> U+0457
    {0x0408, 0x0458}, /// U+0408 -> U+0458
    {0x0409, 0x0459}, /// U+0409 -> U+0459
    {0x040A, 0x045A}, /// U+040A -> U+045A
    {0x040B, 0x045B}, /// U+040B -> U+045B
    {0x040C, 0x045C}, /// U+040C -> U+045C
    {0x040D, 0x045D}, /// U+040D -> U+045D
    {0x040E, 0x045E}, /// U+040E -> U+045E
    {0x040F, 0x045F}, /// U+040F -> U+045F
    {0x0410, 0x0430}, /// U+0410 -> U+0430
    {0x0411, 0x0431}, /// U+0411 -> U+0431
    {0x0412, 0x0432}, /// U+0412 -> U+0432
    {0x0413, 0x0433}, /// U+0413 -> U+0433
    {0x0414, 0x0434}, /// U+0414 -> U+0434
    {0x0415, 0x0435}, /// U+0415 -> U+0435
    {0x0416, 0x0436}, /// U+0416 -> U+0436
    {0x0417, 0x0437}, /// U+0417 -> U+0437
    {0x0418, 0x0438}, /// U+0418 -> U+0438
    {0x0419, 0x0439}, /// U+0419 -> U+0439
    {0x041A, 0x043A}, /// U+041A -> U+043A
    {0x041B, 0x043B}, /// U+041B -> U+043B
    {0x041C, 0x043C}, /// U+041C -> U+043C
    {0x041D, 0x043D}, /// U+041D -> U+043D
    {0x041E, 0x043E}, /// U+041E -> U+043E
    {0x041F, 0x043F}, /// U+041F -> U+043F
    {0x0420, 0x0440}, /// U+0420 -> U+0440
    {0x0421, 0x0441}, /// U+0421 -> U+0441
    {0x0422, 0x0442}, /// U+0422 -> U+0442
    {0x0423, 0x0443}, /// U+0423 -> U+0443
    {0x0424, 0x0444}, /// U+0424 -> U+0444
    {0x0425, 0x0445}, /// U+0425 -> U+0445
    {0x0426, 0x0446}, /// U+0426 -> U+0446
    {0x0427, 0x0447}, /// U+0427 -> U+0447
    {0x0428, 0x0448}, /// U+0428 -> U+0448
    {0x0429, 0x0449}, /// U+0429 -> U+0449
    {0x042A, 0x044A}, /// U+042A -> U+044A
    {0x042B, 0x044B}, /// U+042B -> U+044B
    {0x042C, 0x044C}, /// U+042C -> U+044C
    {0x042D, 0x044D}, /// U+042D -> U+044D
    {0x042E, 0x044E}, /// U+042E -> U+044E
    {0x042F, 0x044F}, /// U+042F -> U+044F
};

static const size_t upper_to_lower_table_size =
    sizeof(upper_to_lower_table) / sizeof(upper_to_lower_table[0]);

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/// @brief Binary search for uppercase -> lowercase mapping
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
 * - German sharp s: U+00DF -> ss
 * - Greek final sigma: U+03C2 -> U+03C3
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
