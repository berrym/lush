/**
 * @file utf8_index.c
 * @brief UTF-8 Index for Fast Position Mapping
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides O(1) mapping between byte offsets, codepoint indices,
 * grapheme cluster indices, and display column positions.
 */

#include "lle/utf8_index.h"
#include "lle/buffer_management.h"
#include "lle/char_width.h"
#include "lle/grapheme_detector.h"
#include "lle/utf8_support.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Initialize a UTF-8 index structure
 * @param index The index structure to initialize
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_init(lle_utf8_index_t *index) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(index, 0, sizeof(lle_utf8_index_t));
    index->index_valid = false;

    return LLE_SUCCESS;
}

/**
 * @brief Clean up and free all resources in a UTF-8 index
 * @param index The index to clean up
 */
void lle_utf8_index_cleanup(lle_utf8_index_t *index) {
    if (!index)
        return;

    free(index->byte_to_codepoint);
    free(index->codepoint_to_byte);
    free(index->grapheme_to_codepoint);
    free(index->codepoint_to_grapheme);
    free(index->grapheme_to_display);
    free(index->display_to_grapheme);

    memset(index, 0, sizeof(lle_utf8_index_t));
}

/**
 * @brief Validate a UTF-8 sequence (internal helper)
 * @param ptr Pointer to the sequence
 * @param length Expected length of the sequence
 * @return true if valid, false otherwise
 */
static bool is_valid_utf8_sequence(const char *ptr, int length) {
    if (!ptr || length < 1 || length > 4) {
        return false;
    }

    unsigned char byte1 = (unsigned char)ptr[0];

    /// Validate first byte matches expected length
    if (length == 1 && (byte1 & 0x80) != 0x00)
        return false; /// 0xxxxxxx
    if (length == 2 && (byte1 & 0xE0) != 0xC0)
        return false; /// 110xxxxx
    if (length == 3 && (byte1 & 0xF0) != 0xE0)
        return false; /// 1110xxxx
    if (length == 4 && (byte1 & 0xF8) != 0xF0)
        return false; /// 11110xxx

    /// Validate continuation bytes (10xxxxxx)
    for (int i = 1; i < length; i++) {
        unsigned char byte = (unsigned char)ptr[i];
        if ((byte & 0xC0) != 0x80) {
            return false;
        }
    }

    /// Check for overlong encodings
    if (length == 2 && (byte1 & 0xFE) == 0xC0)
        return false;
    if (length == 3 && byte1 == 0xE0 && ((unsigned char)ptr[1] & 0xE0) == 0x80)
        return false;
    if (length == 4 && byte1 == 0xF0 && ((unsigned char)ptr[1] & 0xF0) == 0x80)
        return false;

    /// Check for surrogate pairs (invalid in UTF-8)
    if (length == 3 && byte1 == 0xED && ((unsigned char)ptr[1] & 0xE0) == 0xA0)
        return false;

    return true;
}

/**
 * @brief Rebuild the UTF-8 index for new text content
 * @param index The index to rebuild
 * @param text The UTF-8 text to index
 * @param text_length Length of text in bytes
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * This is a multi-phase operation:
 * 1. Count codepoints and grapheme clusters
 * 2. Allocate index arrays
 * 3. Build all mapping tables
 * 4. Update metadata and timing statistics
 */
lle_result_t lle_utf8_index_rebuild(lle_utf8_index_t *index, const char *text,
                                    size_t text_length) {
    if (!index || !text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Start timing for performance tracking
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    lle_result_t result = LLE_SUCCESS;

    /* === PHASE 1: Count codepoints and grapheme clusters === */

    size_t codepoint_count = 0;
    size_t grapheme_count = 0;
    size_t display_width_total = 0;

    const char *ptr = text;
    const char *end = text + text_length;

    while (ptr < end) {
        /// Get UTF-8 sequence length
        int sequence_length = lle_utf8_sequence_length(*ptr);

        /// Validate sequence length
        if (sequence_length == 0 || ptr + sequence_length > end) {
            return LLE_ERROR_INVALID_ENCODING;
        }

        /// Validate complete UTF-8 sequence
        if (!is_valid_utf8_sequence(ptr, sequence_length)) {
            return LLE_ERROR_INVALID_ENCODING;
        }

        codepoint_count++;

        /// Check if this starts a new grapheme cluster
        if (is_grapheme_boundary_at_position(ptr, text, end)) {
            grapheme_count++;

            /// Decode codepoint to get its width
            uint32_t codepoint;
            lle_utf8_decode_codepoint(ptr, sequence_length, &codepoint);
            int width = lle_codepoint_width(codepoint);
            if (width < 0)
                width = 1; /// Treat invalid as normal width
            display_width_total += width;
        }

        ptr += sequence_length;
    }

    /* === PHASE 2: Allocate index arrays === */

    size_t *new_byte_to_codepoint = NULL;
    size_t *new_codepoint_to_byte = NULL;
    size_t *new_grapheme_to_codepoint = NULL;
    size_t *new_codepoint_to_grapheme = NULL;
    size_t *new_grapheme_to_display = NULL;
    size_t *new_display_to_grapheme = NULL;

    /// Allocate byte_to_codepoint (one entry per byte)
    new_byte_to_codepoint = calloc(text_length + 1, sizeof(size_t));
    if (!new_byte_to_codepoint) {
        result = LLE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    /// Allocate codepoint_to_byte (one entry per codepoint)
    new_codepoint_to_byte = calloc(codepoint_count + 1, sizeof(size_t));
    if (!new_codepoint_to_byte) {
        result = LLE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    /// Allocate grapheme arrays
    new_grapheme_to_codepoint = calloc(grapheme_count + 1, sizeof(size_t));
    new_codepoint_to_grapheme = calloc(codepoint_count + 1, sizeof(size_t));
    new_grapheme_to_display = calloc(grapheme_count + 1, sizeof(size_t));
    new_display_to_grapheme = calloc(display_width_total + 1, sizeof(size_t));

    if (!new_grapheme_to_codepoint || !new_codepoint_to_grapheme ||
        !new_grapheme_to_display || !new_display_to_grapheme) {
        result = LLE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    /* === PHASE 3: Build index mappings === */

    ptr = text;
    size_t byte_pos = 0;
    size_t codepoint_pos = 0;
    size_t grapheme_pos = 0;
    size_t display_col = 0;
    size_t current_grapheme_start_codepoint = 0;

    while (ptr < end) {
        int sequence_length = lle_utf8_sequence_length(*ptr);

        /* Update byte-to-codepoint mapping (all bytes in sequence map to same
         * codepoint) */
        for (int i = 0; i < sequence_length; i++) {
            new_byte_to_codepoint[byte_pos + i] = codepoint_pos;
        }

        /// Update codepoint-to-byte mapping
        new_codepoint_to_byte[codepoint_pos] = byte_pos;

        /// Check for grapheme boundary
        if (is_grapheme_boundary_at_position(ptr, text, end)) {
            if (grapheme_pos > 0) {
                /// Complete previous grapheme cluster
                for (size_t i = current_grapheme_start_codepoint;
                     i < codepoint_pos; i++) {
                    new_codepoint_to_grapheme[i] = grapheme_pos - 1;
                }
            }

            /// Start new grapheme cluster
            new_grapheme_to_codepoint[grapheme_pos] = codepoint_pos;
            new_grapheme_to_display[grapheme_pos] = display_col;
            current_grapheme_start_codepoint = codepoint_pos;

            /// Update display column mapping
            uint32_t codepoint;
            lle_utf8_decode_codepoint(ptr, sequence_length, &codepoint);
            int width = lle_codepoint_width(codepoint);
            if (width < 0)
                width = 1;

            /* Fill display_to_grapheme for all columns this grapheme occupies
             */
            for (int w = 0; w < width; w++) {
                new_display_to_grapheme[display_col + w] = grapheme_pos;
            }

            display_col += width;
            grapheme_pos++;
        }

        codepoint_pos++;
        byte_pos += sequence_length;
        ptr += sequence_length;
    }

    /* === PHASE 4: Complete final grapheme cluster === */

    if (codepoint_count > 0 &&
        current_grapheme_start_codepoint < codepoint_count) {
        for (size_t i = current_grapheme_start_codepoint; i < codepoint_count;
             i++) {
            new_codepoint_to_grapheme[i] = grapheme_pos - 1;
        }
    }

    /* === PHASE 5: Replace old arrays with new ones === */

    free(index->byte_to_codepoint);
    free(index->codepoint_to_byte);
    free(index->grapheme_to_codepoint);
    free(index->codepoint_to_grapheme);
    free(index->grapheme_to_display);
    free(index->display_to_grapheme);

    index->byte_to_codepoint = new_byte_to_codepoint;
    index->codepoint_to_byte = new_codepoint_to_byte;
    index->grapheme_to_codepoint = new_grapheme_to_codepoint;
    index->codepoint_to_grapheme = new_codepoint_to_grapheme;
    index->grapheme_to_display = new_grapheme_to_display;
    index->display_to_grapheme = new_display_to_grapheme;

    /// Update metadata
    index->byte_count = text_length;
    index->codepoint_count = codepoint_count;
    index->grapheme_count = grapheme_count;
    index->display_width = display_width_total;
    index->index_valid = true;
    index->rebuild_count++;

    /// Update timing
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    uint64_t elapsed_ns =
        (end_time.tv_sec - start_time.tv_sec) * 1000000000ULL +
        (end_time.tv_nsec - start_time.tv_nsec);
    index->total_rebuild_time_ns += elapsed_ns;
    index->last_update_time = elapsed_ns;

    return LLE_SUCCESS;

cleanup:
    free(new_byte_to_codepoint);
    free(new_codepoint_to_byte);
    free(new_grapheme_to_codepoint);
    free(new_codepoint_to_grapheme);
    free(new_grapheme_to_display);
    free(new_display_to_grapheme);

    return result;
}

/**
 * @brief Convert byte offset to codepoint index
 * @param index The UTF-8 index to query
 * @param byte_offset Byte offset to convert
 * @param codepoint_index Output: corresponding codepoint index
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_byte_to_codepoint(const lle_utf8_index_t *index,
                                              size_t byte_offset,
                                              size_t *codepoint_index) {
    if (!index || !codepoint_index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (byte_offset >= index->byte_count) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *codepoint_index = index->byte_to_codepoint[byte_offset];
    return LLE_SUCCESS;
}

/**
 * @brief Convert codepoint index to byte offset
 * @param index The UTF-8 index to query
 * @param codepoint_index Codepoint index to convert
 * @param byte_offset Output: corresponding byte offset
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_codepoint_to_byte(const lle_utf8_index_t *index,
                                              size_t codepoint_index,
                                              size_t *byte_offset) {
    if (!index || !byte_offset) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (codepoint_index >= index->codepoint_count) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *byte_offset = index->codepoint_to_byte[codepoint_index];
    return LLE_SUCCESS;
}

/**
 * @brief Convert codepoint index to grapheme cluster index
 * @param index The UTF-8 index to query
 * @param codepoint_index Codepoint index to convert
 * @param grapheme_index Output: corresponding grapheme cluster index
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_codepoint_to_grapheme(const lle_utf8_index_t *index,
                                                  size_t codepoint_index,
                                                  size_t *grapheme_index) {
    if (!index || !grapheme_index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (codepoint_index >= index->codepoint_count) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *grapheme_index = index->codepoint_to_grapheme[codepoint_index];
    return LLE_SUCCESS;
}

/**
 * @brief Convert grapheme cluster index to codepoint index
 * @param index The UTF-8 index to query
 * @param grapheme_index Grapheme cluster index to convert
 * @param codepoint_index Output: codepoint index at start of grapheme
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_grapheme_to_codepoint(const lle_utf8_index_t *index,
                                                  size_t grapheme_index,
                                                  size_t *codepoint_index) {
    if (!index || !codepoint_index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (grapheme_index >= index->grapheme_count) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *codepoint_index = index->grapheme_to_codepoint[grapheme_index];
    return LLE_SUCCESS;
}

/**
 * @brief Convert grapheme cluster index to display column
 * @param index The UTF-8 index to query
 * @param grapheme_index Grapheme cluster index to convert
 * @param display_column Output: display column position
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_grapheme_to_display(const lle_utf8_index_t *index,
                                                size_t grapheme_index,
                                                size_t *display_column) {
    if (!index || !display_column) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (grapheme_index >= index->grapheme_count) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *display_column = index->grapheme_to_display[grapheme_index];
    return LLE_SUCCESS;
}

/**
 * @brief Convert display column to grapheme cluster index
 * @param index The UTF-8 index to query
 * @param display_column Display column position to convert
 * @param grapheme_index Output: grapheme cluster at that column
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_utf8_index_display_to_grapheme(const lle_utf8_index_t *index,
                                                size_t display_column,
                                                size_t *grapheme_index) {
    if (!index || !grapheme_index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!index->index_valid) {
        return LLE_ERROR_INVALID_STATE;
    }

    if (display_column >= index->display_width) {
        return LLE_ERROR_INVALID_RANGE;
    }

    *grapheme_index = index->display_to_grapheme[display_column];
    return LLE_SUCCESS;
}

/**
 * @brief Mark the index as invalid (requires rebuild)
 * @param index The index to invalidate
 */
void lle_utf8_index_invalidate(lle_utf8_index_t *index) {
    if (index) {
        index->index_valid = false;
    }
}

/**
 * @brief Check if the index is valid and up-to-date
 * @param index The index to check
 * @return true if valid, false if rebuild is needed
 */
bool lle_utf8_index_is_valid(const lle_utf8_index_t *index) {
    return index && index->index_valid;
}
