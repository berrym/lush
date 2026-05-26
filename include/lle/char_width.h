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
 * - -1: Invalid or unassigned codepoints
 *
 * Based on Unicode East Asian Width property and other factors.
 *
 * @param codepoint Unicode codepoint
 * @return Display width (0, 1, 2, or -1)
 */
int lle_codepoint_width(uint32_t codepoint);

/**
 * @brief Check if a codepoint is a wide character
 * @param codepoint Unicode codepoint
 * @return true if wide (2 columns), false otherwise
 */
bool lle_is_wide_character(uint32_t codepoint);

#endif /// LLE_CHAR_WIDTH_H
