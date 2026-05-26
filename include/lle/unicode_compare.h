/**
 * @file unicode_compare.h
 * @brief Unicode-aware string comparison for LLE
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides Unicode-normalized string comparison using NFC (Canonical
 * Composition) normalization. This ensures that equivalent Unicode sequences
 * compare as equal regardless of their byte representation.
 *
 * Example: "e" (U+00E9 precomposed) == "e" + "'" (U+0065 + U+0301 decomposed)
 *
 * For shell commands, most input is ASCII, but this handles edge cases where
 * users type accented characters, emoji, or other Unicode that may have
 * multiple valid representations.
 */

#ifndef LLE_UNICODE_COMPARE_H
#define LLE_UNICODE_COMPARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Unicode comparison options structure
 */
typedef struct lle_unicode_compare_options {
    bool normalize;        ///< Apply NFC normalization before comparison
    bool case_insensitive; ///< Case-insensitive comparison
    bool trim_whitespace;  ///< Trim leading/trailing whitespace
} lle_unicode_compare_options_t;

/**
 * @brief Default comparison options (normalize only)
 */
extern const lle_unicode_compare_options_t LLE_UNICODE_COMPARE_DEFAULT;

/**
 * @brief Strict byte comparison (no normalization, same as strcmp)
 */
extern const lle_unicode_compare_options_t LLE_UNICODE_COMPARE_STRICT;

/**
 * @brief Compare two UTF-8 strings with Unicode normalization
 *
 * This function compares strings using NFC normalization to ensure
 * equivalent Unicode sequences compare as equal. It handles:
 * - Precomposed vs decomposed characters (e vs e + combining acute)
 * - Different byte representations of the same codepoint
 * - Optional case-insensitive comparison
 * - Optional whitespace trimming
 *
 * @param str1 First UTF-8 string
 * @param str2 Second UTF-8 string
 * @param options Comparison options (NULL for defaults)
 * @return true if strings are equal after normalization, false otherwise
 */
bool lle_unicode_strings_equal(const char *str1, const char *str2,
                               const lle_unicode_compare_options_t *options);

/**
 * @brief Compare two UTF-8 strings with length limits
 *
 * Like lle_unicode_strings_equal but with explicit length parameters.
 * Useful when strings may not be null-terminated.
 *
 * @param str1 First UTF-8 string
 * @param len1 Length of first string in bytes
 * @param str2 Second UTF-8 string
 * @param len2 Length of second string in bytes
 * @param options Comparison options (NULL for defaults)
 * @return true if strings are equal after normalization, false otherwise
 */
bool lle_unicode_strings_equal_n(const char *str1, size_t len1,
                                 const char *str2, size_t len2,
                                 const lle_unicode_compare_options_t *options);

/**
 * @brief Normalize a UTF-8 string to NFC form
 *
 * Converts a UTF-8 string to Normalization Form C (Canonical Composition).
 * This is a two-step process:
 * 1. Canonical decomposition (NFD)
 * 2. Canonical composition
 *
 * @param input Input UTF-8 string
 * @param input_len Length of input in bytes
 * @param output Output buffer for normalized string
 * @param output_size Size of output buffer
 * @param output_len Receives actual length of normalized string
 * @return 0 on success, negative on error (-1: Invalid parameter, -2: Buffer
 * too small, -3: Invalid UTF-8)
 */
int lle_unicode_normalize_nfc(const char *input, size_t input_len, char *output,
                              size_t output_size, size_t *output_len);

/**
 * @brief Check if a codepoint is a combining character
 * @param codepoint Unicode codepoint to check
 * @return true if the codepoint is a combining character (Mn, Mc, Me
 * categories)
 */
bool lle_unicode_is_combining(uint32_t codepoint);

/**
 * @brief Get the canonical combining class of a codepoint
 *
 * The combining class is used for canonical ordering of combining marks
 * during normalization. Class 0 means the character is a starter (base).
 *
 * @param codepoint Unicode codepoint
 * @return Canonical combining class (0-254)
 */
uint8_t lle_unicode_combining_class(uint32_t codepoint);

/**
 * @brief Get the canonical decomposition of a codepoint
 *
 * Returns the canonical decomposition mapping for precomposed characters.
 * For example, U+00E9 (e) decomposes to U+0065 U+0301 (e + combining acute).
 *
 * @param codepoint Unicode codepoint to decompose
 * @param decomposed Output buffer for decomposed codepoints
 * @param max_length Maximum number of codepoints to output
 * @return Number of codepoints in decomposition (0 if no decomposition)
 */
int lle_unicode_decompose(uint32_t codepoint, uint32_t *decomposed,
                          int max_length);

/**
 * @brief Compose two codepoints into a precomposed form
 *
 * Attempts to compose a base character with a combining character
 * into a single precomposed codepoint.
 *
 * @param base Base character
 * @param combining Combining character
 * @param composed Receives composed character if successful
 * @return true if composition successful, false if no composition exists
 */
bool lle_unicode_compose(uint32_t base, uint32_t combining, uint32_t *composed);

/**
 * @brief Check if one UTF-8 string is a prefix of another (Unicode-aware)
 *
 * This function performs Unicode-normalized prefix matching using NFC
 * normalization. It correctly handles:
 * - Precomposed vs decomposed characters (cafe vs cafe + combining acute)
 * - Different byte representations of equivalent Unicode sequences
 * - Grapheme cluster boundaries (won't match partial graphemes)
 *
 * For shell autosuggestions, this ensures that typing "cafe" will match
 * history entry "cafe_script.sh" regardless of how either was encoded.
 *
 * @param prefix The prefix string to check (UTF-8)
 * @param prefix_len Length of prefix in bytes
 * @param str The string to check against (UTF-8)
 * @param str_len Length of string in bytes
 * @param options Comparison options (NULL for defaults with normalization)
 * @return true if prefix is a Unicode-normalized prefix of str, false otherwise
 */
bool lle_unicode_is_prefix(const char *prefix, size_t prefix_len,
                           const char *str, size_t str_len,
                           const lle_unicode_compare_options_t *options);

/**
 * @brief Check if one null-terminated UTF-8 string is a prefix of another
 *
 * Convenience wrapper around lle_unicode_is_prefix() for null-terminated
 * strings.
 *
 * @param prefix The prefix string to check (UTF-8, null-terminated)
 * @param str The string to check against (UTF-8, null-terminated)
 * @param options Comparison options (NULL for defaults with normalization)
 * @return true if prefix is a Unicode-normalized prefix of str, false otherwise
 */
bool lle_unicode_is_prefix_z(const char *prefix, const char *str,
                             const lle_unicode_compare_options_t *options);

#endif /// LLE_UNICODE_COMPARE_H
