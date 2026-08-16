/**
 * @file utf8_support.c
 * @brief Complete UTF-8 Unicode Support Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides comprehensive UTF-8 encoding/decoding, validation, and
 * manipulation functions including codepoint width calculation.
 */

#include "lle/utf8_support.h"

#include "lle/char_width.h"
#include "lle/unicode_grapheme.h"
#include <string.h>

/**
 * @brief Get the length of a UTF-8 sequence from its first byte
 * @param first_byte The first byte of a UTF-8 sequence
 * @return Sequence length (1-4), or 0 if invalid start byte
 */
int lle_utf8_sequence_length(unsigned char first_byte) {
    /// Single-byte character (ASCII): 0xxxxxxx
    if ((first_byte & 0x80) == 0x00) {
        return 1;
    }

    /// Two-byte sequence: 110xxxxx
    if ((first_byte & 0xE0) == 0xC0) {
        return 2;
    }

    /// Three-byte sequence: 1110xxxx
    if ((first_byte & 0xF0) == 0xE0) {
        return 3;
    }

    /// Four-byte sequence: 11110xxx
    if ((first_byte & 0xF8) == 0xF0) {
        return 4;
    }

    /// Invalid UTF-8 start byte
    return 0;
}

/**
 * @brief Validate a complete UTF-8 sequence
 * @param ptr Pointer to the start of the sequence
 * @param length Expected length of the sequence (1-4)
 * @return true if valid UTF-8, false otherwise
 *
 * Checks continuation bytes, overlong encodings, and invalid ranges.
 */
bool lle_utf8_is_valid_sequence(const char *ptr, int length) {
    if (!ptr || length <= 0 || length > 4) {
        return false;
    }

    unsigned char first = (unsigned char)ptr[0];

    /// Validate sequence length matches first byte
    int expected_len = lle_utf8_sequence_length(first);
    if (expected_len != length) {
        return false;
    }

    /// Single-byte ASCII
    if (length == 1) {
        return true;
    }

    /// Multi-byte sequences - validate continuation bytes
    for (int i = 1; i < length; i++) {
        unsigned char byte = (unsigned char)ptr[i];
        /// Continuation bytes must be 10xxxxxx
        if ((byte & 0xC0) != 0x80) {
            return false;
        }
    }

    /// Additional validation for overlong encodings and invalid ranges
    uint32_t codepoint = 0;

    if (length == 2) {
        /// Two-byte: 110xxxxx 10xxxxxx
        codepoint = ((first & 0x1F) << 6) | (ptr[1] & 0x3F);
        /// Must be >= 0x80 (not overlong)
        if (codepoint < 0x80) {
            return false;
        }
    } else if (length == 3) {
        /// Three-byte: 1110xxxx 10xxxxxx 10xxxxxx
        codepoint =
            ((first & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);
        /// Must be >= 0x800 (not overlong)
        /// Must not be in surrogate range (0xD800-0xDFFF)
        if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
    } else if (length == 4) {
        /// Four-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        codepoint = ((first & 0x07) << 18) | ((ptr[1] & 0x3F) << 12) |
                    ((ptr[2] & 0x3F) << 6) | (ptr[3] & 0x3F);
        /// Must be >= 0x10000 (not overlong)
        /// Must be <= 0x10FFFF (maximum Unicode codepoint)
        if (codepoint < 0x10000 || codepoint > 0x10FFFF) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Validate an entire UTF-8 string
 * @param text The UTF-8 text to validate
 * @param length Length of text in bytes
 * @return true if entire string is valid UTF-8, false otherwise
 */
bool lle_utf8_is_valid(const char *text, size_t length) {
    if (!text) {
        return false;
    }

    const char *ptr = text;
    const char *end = text + length;

    while (ptr < end) {
        int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len == 0 || ptr + seq_len > end) {
            return false;
        }

        if (!lle_utf8_is_valid_sequence(ptr, seq_len)) {
            return false;
        }

        ptr += seq_len;
    }

    return true;
}

/**
 * @brief Decode a UTF-8 sequence to a Unicode codepoint
 * @param ptr Pointer to the start of the sequence
 * @param max_len Maximum bytes available to read
 * @param codepoint Output: decoded Unicode codepoint
 * @return Number of bytes consumed, or -1 on error
 */
int lle_utf8_decode_codepoint(const char *ptr, size_t max_len,
                              uint32_t *codepoint) {
    if (!ptr || !codepoint || max_len == 0) {
        return -1;
    }

    unsigned char first = (unsigned char)ptr[0];
    int seq_len = lle_utf8_sequence_length(first);

    if (seq_len == 0 || (size_t)seq_len > max_len) {
        return -1;
    }

    /// Validate the sequence
    if (!lle_utf8_is_valid_sequence(ptr, seq_len)) {
        return -1;
    }

    /// Decode based on length
    if (seq_len == 1) {
        /// ASCII
        *codepoint = first;
    } else if (seq_len == 2) {
        /// Two-byte sequence
        *codepoint = ((first & 0x1F) << 6) | (ptr[1] & 0x3F);
    } else if (seq_len == 3) {
        /// Three-byte sequence
        *codepoint =
            ((first & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);
    } else if (seq_len == 4) {
        /// Four-byte sequence
        *codepoint = ((first & 0x07) << 18) | ((ptr[1] & 0x3F) << 12) |
                     ((ptr[2] & 0x3F) << 6) | (ptr[3] & 0x3F);
    }

    return seq_len;
}

/**
 * @brief Encode a Unicode codepoint to UTF-8
 * @param codepoint The Unicode codepoint to encode
 * @param buffer Output buffer (must have room for 4 bytes)
 * @return Number of bytes written (1-4), or 0 if invalid codepoint
 */
int lle_utf8_encode_codepoint(uint32_t codepoint, char *buffer) {
    if (!buffer) {
        return 0;
    }

    /// Validate codepoint range
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return 0; /// Invalid codepoint
    }

    if (codepoint <= 0x7F) {
        /// Single-byte (ASCII)
        buffer[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        /// Two-byte sequence
        buffer[0] = (char)(0xC0 | (codepoint >> 6));
        buffer[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint <= 0xFFFF) {
        /// Three-byte sequence
        buffer[0] = (char)(0xE0 | (codepoint >> 12));
        buffer[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else {
        /// Four-byte sequence
        buffer[0] = (char)(0xF0 | (codepoint >> 18));
        buffer[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buffer[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
}

/**
 * @brief Count the number of Unicode codepoints in a UTF-8 string
 * @param text The UTF-8 text to analyze
 * @param length Length of text in bytes
 * @return Number of codepoints in the string
 */
size_t lle_utf8_count_codepoints(const char *text, size_t length) {
    if (!text || length == 0) {
        return 0;
    }

    const char *ptr = text;
    const char *end = text + length;
    size_t count = 0;

    while (ptr < end) {
        int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len == 0 || ptr + seq_len > end) {
            break; /// Invalid UTF-8 or end of string
        }

        count++;
        ptr += seq_len;
    }

    return count;
}

/**
 * @brief Convert byte offset to codepoint index
 * @param text The UTF-8 text
 * @param byte_offset Byte offset to convert
 * @param cp_index Output: codepoint index at that byte offset
 * @return 0 on success, -1 on error
 */
int lle_utf8_byte_to_codepoint_index(const char *text, size_t byte_offset,
                                     size_t *cp_index) {
    if (!text || !cp_index) {
        return -1;
    }

    const char *ptr = text;
    size_t index = 0;
    size_t current_byte = 0;

    while (current_byte < byte_offset) {
        int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len == 0) {
            return -1; /// Invalid UTF-8
        }

        current_byte += seq_len;
        if (current_byte <= byte_offset) {
            index++;
            ptr += seq_len;
        }
    }

    *cp_index = index;
    return 0;
}

/**
 * @brief Convert codepoint index to byte offset
 * @param text The UTF-8 text
 * @param cp_index Codepoint index to convert
 * @param byte_offset Output: byte offset of that codepoint
 * @return 0 on success, -1 on error
 */
int lle_utf8_codepoint_to_byte_offset(const char *text, size_t cp_index,
                                      size_t *byte_offset) {
    if (!text || !byte_offset) {
        return -1;
    }

    const char *ptr = text;
    size_t index = 0;
    size_t offset = 0;

    while (index < cp_index) {
        int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len == 0) {
            return -1; /// Invalid UTF-8 or past end
        }

        offset += seq_len;
        ptr += seq_len;
        index++;
    }

    *byte_offset = offset;
    return 0;
}

/**
 * @brief Convert codepoint index to grapheme cluster index
 * @param text The UTF-8 text
 * @param cp_index Codepoint index to convert
 * @param grapheme_index Output: grapheme cluster index
 * @return 0 on success, -1 on error
 */
int lle_utf8_codepoint_to_grapheme_index(const char *text, size_t cp_index,
                                         size_t *grapheme_index) {
    if (!text || !grapheme_index) {
        return -1;
    }

    const char *ptr = text;
    const char *start = text;
    size_t cp_count = 0;
    size_t grapheme_count = 0;

    /// Count grapheme clusters up to the specified codepoint
    while (cp_count <= cp_index) {
        int seq_len = lle_utf8_sequence_length((unsigned char)*ptr);
        if (seq_len == 0) {
            return -1; /// Invalid UTF-8
        }

        /// Check if this is a grapheme boundary
        if (lle_is_grapheme_boundary(ptr, start, ptr + seq_len)) {
            if (cp_count < cp_index) {
                grapheme_count++;
            }
        }

        if (cp_count == cp_index) {
            break;
        }

        ptr += seq_len;
        cp_count++;
    }

    *grapheme_index = grapheme_count;
    return 0;
}

/**
 * @brief Get the display width of a UTF-8 string
 * @param text The UTF-8 text to measure
 * @param length Length of text in bytes
 * @return Total display width in terminal columns
 *
 * Sums per-codepoint widths via lle_codepoint_width (the canonical
 * codepoint-width table in char_width.c), which honours the
 * configured TR11 'Ambiguous' policy.
 */
size_t lle_utf8_string_width(const char *text, size_t length) {
    if (!text || length == 0) {
        return 0;
    }

    const char *ptr = text;
    const char *end = text + length;
    size_t total_width = 0;

    while (ptr < end) {
        uint32_t codepoint = 0;
        int seq_len = lle_utf8_decode_codepoint(ptr, end - ptr, &codepoint);
        if (seq_len <= 0) {
            break; /// Invalid UTF-8
        }

        int w = lle_codepoint_width(codepoint);
        if (w > 0) {
            total_width += (size_t)w;
        }
        ptr += seq_len;
    }

    return total_width;
}

size_t lle_utf8_visible_width(const char *text, size_t length) {
    if (!text || length == 0) {
        return 0;
    }

    /// Fast path: no escape byte at all.
    if (!memchr(text, 0x1B, length)) {
        return lle_utf8_string_width(text, length);
    }

    size_t width = 0;
    size_t i = 0;

    while (i < length) {
        if ((unsigned char)text[i] == 0x1B) {
            i++;
            if (i < length && (text[i] == '[' || text[i] == ']')) {
                i++;
                /// Run to the final byte (ECMA-48: 0x40-0x7E).
                while (i < length) {
                    unsigned char fin = (unsigned char)text[i];
                    i++;
                    if (fin >= 0x40 && fin <= 0x7E) {
                        break;
                    }
                }
            } else if (i < length) {
                /// A two-character escape ends at once.
                i++;
            }
            continue;
        }

        /// Measure the run up to the next escape in one call.
        size_t run_start = i;
        while (i < length && (unsigned char)text[i] != 0x1B) {
            i++;
        }
        width += lle_utf8_string_width(text + run_start, i - run_start);
    }

    return width;
}
