/**
 * @file utf8_support.h
 * @brief Complete UTF-8 Unicode support for LLE
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides comprehensive UTF-8 encoding/decoding, validation, and
 * manipulation functions for the Line Editing Engine.
 */

#ifndef LLE_UTF8_SUPPORT_H
#define LLE_UTF8_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Get the length of a UTF-8 sequence from its first byte
 * @param first_byte The first byte of a UTF-8 sequence
 * @return Length of the sequence in bytes (1-4), or 0 if invalid
 */
int lle_utf8_sequence_length(unsigned char first_byte);

/**
 * @brief Validate a complete UTF-8 sequence
 * @param ptr Pointer to the UTF-8 sequence
 * @param length Expected length of the sequence
 * @return true if the sequence is valid, false otherwise
 */
bool lle_utf8_is_valid_sequence(const char *ptr, int length);

/**
 * @brief Validate an entire UTF-8 string
 * @param text UTF-8 text to validate
 * @param length Length of text in bytes
 * @return true if all text is valid UTF-8, false otherwise
 */
bool lle_utf8_is_valid(const char *text, size_t length);

/**
 * @brief Decode a UTF-8 sequence to a Unicode codepoint
 * @param ptr Pointer to UTF-8 sequence
 * @param max_len Maximum bytes to read
 * @param codepoint Output parameter for decoded codepoint
 * @return Number of bytes consumed (1-4), or negative on error
 */
int lle_utf8_decode_codepoint(const char *ptr, size_t max_len,
                              uint32_t *codepoint);

/**
 * @brief Encode a Unicode codepoint to UTF-8
 * @param codepoint Unicode codepoint to encode
 * @param buffer Output buffer (must be at least 4 bytes)
 * @return Number of bytes written (1-4), or 0 on error
 */
int lle_utf8_encode_codepoint(uint32_t codepoint, char *buffer);

/**
 * @brief Count the number of Unicode codepoints in a UTF-8 string
 * @param text UTF-8 text to analyze
 * @param length Length of text in bytes
 * @return Number of codepoints in the string
 */
size_t lle_utf8_count_codepoints(const char *text, size_t length);

/**
 * @brief Convert byte offset to codepoint index
 * @param text UTF-8 text
 * @param byte_offset Byte offset in text
 * @param cp_index Output parameter for codepoint index
 * @return 0 on success, negative on error
 */
int lle_utf8_byte_to_codepoint_index(const char *text, size_t byte_offset,
                                     size_t *cp_index);

/**
 * @brief Convert codepoint index to byte offset
 * @param text UTF-8 text
 * @param cp_index Codepoint index
 * @param byte_offset Output parameter for byte offset
 * @return 0 on success, negative on error
 */
int lle_utf8_codepoint_to_byte_offset(const char *text, size_t cp_index,
                                      size_t *byte_offset);

/**
 * @brief Convert codepoint index to grapheme cluster index
 * @param text UTF-8 text
 * @param cp_index Codepoint index
 * @param grapheme_index Output parameter for grapheme cluster index
 * @return 0 on success, negative on error
 */
int lle_utf8_codepoint_to_grapheme_index(const char *text, size_t cp_index,
                                         size_t *grapheme_index);

/**
 * @brief Get the display width of a UTF-8 string
 *
 * Per-codepoint widths come from lle_codepoint_width (canonical
 * table in char_width.c), which honours the configured East Asian
 * Ambiguous policy.
 *
 * @param text UTF-8 text
 * @param length Length in bytes
 * @return Total display width
 */
size_t lle_utf8_string_width(const char *text, size_t length);

/**
 * @brief Display width of text that may contain ANSI escape sequences
 *
 * Like lle_utf8_string_width, but escape sequences contribute nothing. A
 * sequence ends at its FINAL BYTE -- ECMA-48 defines that as the range
 * 0x40-0x7E, not "the first letter", so `\033[3~` terminates at the `~`.
 *
 * Three places in LLE measured this and each carried its own escape walk with
 * its own terminator set: the prompt composer ended a sequence only on `m`,
 * so `\033[K` left it measuring the rest of the prompt as zero width. The
 * display subsystem has the equivalent computation on its side of the seam;
 * display depends on LLE and not the reverse, so the two layers keep one each
 * rather than one reaching across into the other.
 *
 * @param text UTF-8 text, may contain escape sequences
 * @param length Length in bytes
 * @return Total display width in columns
 */
size_t lle_utf8_visible_width(const char *text, size_t length);

#endif /// LLE_UTF8_SUPPORT_H
