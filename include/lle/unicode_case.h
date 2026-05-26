/**
 * @file unicode_case.h
 * @brief Unicode Case Conversion for LLE
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides Unicode-aware case conversion functions that work correctly
 * with multi-byte UTF-8 characters. Used by shell features like ${var^^},
 * ${var,,}, nocasematch, and nocaseglob.
 *
 * Covers Latin-1 Supplement (U+0080-U+00FF), Latin Extended-A (U+0100-U+017F),
 * Latin Extended-B (U+0180-U+024F), and Greek (U+0370-U+03FF) which handle
 * the vast majority of real-world shell usage.
 *
 * For full Unicode coverage, a complete implementation would need the
 * Unicode Character Database (UCD), but this focused coverage is sufficient
 * for shell command and filename operations.
 */

#ifndef LLE_UNICODE_CASE_H
#define LLE_UNICODE_CASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Convert a Unicode codepoint to uppercase
 *
 * Returns the uppercase equivalent of the given codepoint, or the
 * codepoint itself if it has no uppercase mapping.
 *
 * Handles:
 * - ASCII (a-z)
 * - Latin-1 Supplement accented characters
 * - Latin Extended-A/B characters
 * - Greek alphabet
 *
 * Note: Some characters have special case mappings (e.g., German sharp s
 * 'ß' -> 'SS') which expand to multiple characters. This function returns
 * only single-character mappings; use lle_utf8_toupper() for full string
 * conversion that handles expansion.
 *
 * @param cp Unicode codepoint
 * @return Uppercase codepoint, or original if no mapping exists
 */
uint32_t lle_unicode_toupper_codepoint(uint32_t cp);

/**
 * @brief Convert a Unicode codepoint to lowercase
 *
 * Returns the lowercase equivalent of the given codepoint, or the
 * codepoint itself if it has no lowercase mapping.
 *
 * Handles:
 * - ASCII (A-Z)
 * - Latin-1 Supplement accented characters
 * - Latin Extended-A/B characters
 * - Greek alphabet
 *
 * @param cp Unicode codepoint
 * @return Lowercase codepoint, or original if no mapping exists
 */
uint32_t lle_unicode_tolower_codepoint(uint32_t cp);

/**
 * @brief Check if a codepoint is uppercase
 *
 * @param cp Unicode codepoint
 * @return true if the codepoint is an uppercase letter
 */
bool lle_unicode_is_upper(uint32_t cp);

/**
 * @brief Check if a codepoint is lowercase
 *
 * @param cp Unicode codepoint
 * @return true if the codepoint is a lowercase letter
 */
bool lle_unicode_is_lower(uint32_t cp);

/**
 * @brief Convert a UTF-8 string to uppercase
 *
 * Converts all lowercase characters in the input string to uppercase,
 * writing the result to the output buffer. Properly handles multi-byte
 * UTF-8 sequences and characters that may change byte length when
 * converted (though this is rare in practice).
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer for uppercase string
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written to output (excluding null terminator),
 *         or (size_t)-1 on error (invalid UTF-8, buffer too small)
 */
size_t lle_utf8_toupper(const char *input, size_t input_len, char *output,
                        size_t output_size);

/**
 * @brief Convert a UTF-8 string to lowercase
 *
 * Converts all uppercase characters in the input string to lowercase,
 * writing the result to the output buffer. Properly handles multi-byte
 * UTF-8 sequences.
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer for lowercase string
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written to output (excluding null terminator),
 *         or (size_t)-1 on error (invalid UTF-8, buffer too small)
 */
size_t lle_utf8_tolower(const char *input, size_t input_len, char *output,
                        size_t output_size);

/**
 * @brief Convert first character of UTF-8 string to uppercase
 *
 * Converts only the first character to uppercase, leaving the rest unchanged.
 * Used for ${var^} parameter expansion.
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written to output, or (size_t)-1 on error
 */
size_t lle_utf8_toupper_first(const char *input, size_t input_len, char *output,
                              size_t output_size);

/**
 * @brief Convert first character of UTF-8 string to lowercase
 *
 * Converts only the first character to lowercase, leaving the rest unchanged.
 * Used for ${var,} parameter expansion.
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written to output, or (size_t)-1 on error
 */
size_t lle_utf8_tolower_first(const char *input, size_t input_len, char *output,
                              size_t output_size);

/**
 * @brief Case-fold a UTF-8 string for case-insensitive comparison
 *
 * Converts the string to a canonical form suitable for case-insensitive
 * comparison. This is similar to lowercase but uses Unicode case folding
 * rules which handle some edge cases differently.
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer for case-folded string
 * @param output_size Size of output buffer in bytes
 * @return Number of bytes written to output, or (size_t)-1 on error
 */
size_t lle_utf8_casefold(const char *input, size_t input_len, char *output,
                         size_t output_size);

#endif /// LLE_UNICODE_CASE_H
