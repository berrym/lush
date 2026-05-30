/**
 * @file char_width.h
 * @brief Unicode character display width calculation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides functions to determine the display width of Unicode codepoints
 * based on the Unicode East Asian Width property and other factors.
 */

#ifndef LLE_CHAR_WIDTH_H
#define LLE_CHAR_WIDTH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Get display width of a Unicode codepoint
 *
 * Returns the number of terminal columns the character occupies:
 * - 0: Control characters, combining marks, zero-width
 * - 1: Normal characters
 * - 2: Wide characters (CJK, emoji, etc.)
 *
 * Codepoints in the TR11 'Ambiguous' class follow the global policy
 * set via `lle_codepoint_width_set_ambiguous_policy`; default is 1
 * (narrow), matching traditional wcwidth behavior.
 *
 * @param codepoint Unicode codepoint
 * @return Display width (0, 1, or 2)
 */
int lle_codepoint_width(uint32_t codepoint);

/**
 * @brief Check if a codepoint is a wide character
 * @param codepoint Unicode codepoint
 * @return true if wide (2 columns), false otherwise
 */
bool lle_is_wide_character(uint32_t codepoint);

/**
 * @brief Check if a codepoint is in the East Asian Ambiguous class.
 *
 * Ambiguous-class codepoints (per UAX #11) render with different
 * widths in different terminal+font pairs -- e.g. U+2300-U+23FF
 * Miscellaneous Technical, U+2600-U+27BF Miscellaneous Symbols,
 * Greek and Cyrillic letters in CJK contexts. Lush honours a single
 * global policy for the whole class (see
 * `lle_codepoint_width_set_ambiguous_policy`) rather than hardcoding
 * one of the two interpretations.
 *
 * @param cp Unicode codepoint
 * @return true if `cp` is in the TR11 'A' (Ambiguous) class
 */
bool lle_codepoint_is_east_asian_ambiguous(uint32_t cp);

/**
 * @brief Set the global East Asian Ambiguous-width policy.
 *
 * Width returned by `lle_codepoint_width` for codepoints in the
 * Ambiguous class. Pass 1 for narrow (traditional wcwidth default),
 * 2 for wide (some Asian-language terminals). Any other value is
 * coerced to 1.
 *
 * @param width 1 (narrow) or 2 (wide)
 */
void lle_codepoint_width_set_ambiguous_policy(int width);

#endif /// LLE_CHAR_WIDTH_H
