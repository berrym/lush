/**
 * @file input_utf8_processor.c
 * @brief UTF-8 Stream Processing for Input Parsing
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides streaming UTF-8 decoding and validation for terminal input.
 * This is a thin adapter layer over Spec 03's utf8_support.c and
 * unicode_grapheme.c, adding input-specific functionality:
 * - Streaming support for partial/incomplete sequences
 * - Input-specific error recovery
 * - Codepoint and grapheme cluster tracking
 * - Statistics for debugging and testing
 *
 * Spec 06: Input Parsing - Phase 2
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include "lle/unicode_grapheme.h"
#include "lle/utf8_support.h"
#include <string.h>

/**
 * @brief Initialize a UTF-8 processor
 *
 * Creates and initializes a new UTF-8 processor for streaming input.
 *
 * @param processor Output pointer for created processor
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_utf8_processor_init(lle_utf8_processor_t **processor,
                                           lle_memory_pool_t *memory_pool) {
    if (!processor || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate processor structure
    lle_utf8_processor_t *new_proc =
        lle_pool_alloc(sizeof(lle_utf8_processor_t));
    if (!new_proc) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize all fields to zero
    memset(new_proc, 0, sizeof(lle_utf8_processor_t));

    /// Initialize state
    new_proc->utf8_pos = 0;
    new_proc->expected_bytes = 0;
    new_proc->current_codepoint = 0;
    new_proc->previous_codepoint = 0;
    new_proc->in_grapheme_cluster = false;

    /// Initialize statistics
    new_proc->codepoints_processed = 0;
    new_proc->grapheme_clusters_detected = 0;
    new_proc->invalid_sequences_handled = 0;

    *processor = new_proc;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a UTF-8 processor
 *
 * Frees all resources associated with the processor.
 *
 * @param processor Processor to destroy
 */
void lle_input_utf8_processor_destroy(lle_utf8_processor_t *processor) {
    if (!processor) {
        return;
    }

    lle_pool_free(processor);
}

/**
 * @brief Reset a UTF-8 processor to initial state
 *
 * Clears any partial sequences and resets state.
 *
 * @param processor Processor to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_utf8_processor_reset(lle_utf8_processor_t *processor) {
    if (!processor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear UTF-8 buffer
    memset(processor->utf8_buffer, 0, sizeof(processor->utf8_buffer));
    processor->utf8_pos = 0;
    processor->expected_bytes = 0;
    processor->current_codepoint = 0;
    processor->in_grapheme_cluster = false;

    /// Note: We don't reset previous_codepoint or statistics
    /// to maintain continuity for grapheme boundary detection

    return LLE_SUCCESS;
}

/**
 * @brief Check if processor has a partial sequence buffered
 *
 * @param processor Processor to check
 * @return true if partial sequence is buffered, false otherwise
 */
bool lle_input_utf8_processor_has_partial(
    const lle_utf8_processor_t *processor) {
    if (!processor) {
        return false;
    }

    return processor->utf8_pos > 0 &&
           processor->utf8_pos < processor->expected_bytes;
}

/**
 * @brief Get the number of bytes needed to complete current sequence
 *
 * @param processor Processor to query
 * @return Number of bytes needed, or 0 if sequence is complete
 */
size_t
lle_input_utf8_processor_bytes_needed(const lle_utf8_processor_t *processor) {
    if (!processor || processor->utf8_pos == 0) {
        return 0;
    }

    if (processor->utf8_pos >= processor->expected_bytes) {
        return 0; /// Sequence is complete
    }

    return processor->expected_bytes - processor->utf8_pos;
}

/**
 * @brief Process a single byte from input stream
 *
 * This is the main streaming function. It processes bytes one at a time,
 * buffering partial sequences and emitting complete codepoints.
 *
 * @param processor UTF-8 processor instance
 * @param byte Input byte to process
 * @param codepoint_out Output for decoded codepoint (if complete)
 * @param is_grapheme_boundary Output for grapheme boundary detection
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_ENCODING on invalid UTF-8
 */
lle_result_t lle_input_utf8_processor_process_byte(
    lle_utf8_processor_t *processor, unsigned char byte,
    uint32_t *codepoint_out, bool *is_grapheme_boundary) {
    if (!processor || !codepoint_out || !is_grapheme_boundary) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *codepoint_out = 0;
    *is_grapheme_boundary = false;

    /// If we're not currently processing a sequence, check if this is a start
    /// byte
    if (processor->utf8_pos == 0) {
        int seq_len = lle_utf8_sequence_length(byte);
        if (seq_len == 0) {
            /// Invalid start byte - handle error
            processor->invalid_sequences_handled++;
            return LLE_ERROR_INVALID_ENCODING;
        }

        /// Start new sequence
        processor->utf8_buffer[0] = byte;
        processor->utf8_pos = 1;
        processor->expected_bytes = seq_len;

        /// Single-byte ASCII - complete immediately
        if (seq_len == 1) {
            processor->previous_codepoint = processor->current_codepoint;
            processor->current_codepoint = byte;
            *codepoint_out = byte;
            processor->codepoints_processed++;

            /// ASCII always starts a new grapheme cluster
            *is_grapheme_boundary = true;
            processor->grapheme_clusters_detected++;

            /// Reset for next sequence
            processor->utf8_pos = 0;
            processor->expected_bytes = 0;

            return LLE_SUCCESS;
        }

        /// Multi-byte sequence - need more bytes
        return LLE_SUCCESS;
    }

    /// We're in the middle of a multi-byte sequence
    /// Validate that this is a continuation byte (10xxxxxx)
    if ((byte & 0xC0) != 0x80) {
        /// Invalid continuation byte - reset and try to recover
        processor->invalid_sequences_handled++;
        processor->utf8_pos = 0;
        processor->expected_bytes = 0;
        return LLE_ERROR_INVALID_ENCODING;
    }

    /// Add continuation byte to buffer
    processor->utf8_buffer[processor->utf8_pos++] = byte;

    /// Check if sequence is complete
    if (processor->utf8_pos < processor->expected_bytes) {
        /// Still need more bytes
        return LLE_SUCCESS;
    }

    /// Sequence is complete - validate and decode
    if (!lle_utf8_is_valid_sequence(processor->utf8_buffer,
                                    processor->expected_bytes)) {
        /// Invalid sequence - reset and report error
        processor->invalid_sequences_handled++;
        processor->utf8_pos = 0;
        processor->expected_bytes = 0;
        return LLE_ERROR_INVALID_ENCODING;
    }

    /// Decode codepoint
    uint32_t codepoint = 0;
    int decoded_len = lle_utf8_decode_codepoint(
        processor->utf8_buffer, processor->expected_bytes, &codepoint);
    if (decoded_len <= 0) {
        /// Decoding failed - reset and report error
        processor->invalid_sequences_handled++;
        processor->utf8_pos = 0;
        processor->expected_bytes = 0;
        return LLE_ERROR_INVALID_ENCODING;
    }

    /// Successfully decoded codepoint
    processor->previous_codepoint = processor->current_codepoint;
    processor->current_codepoint = codepoint;
    *codepoint_out = codepoint;
    processor->codepoints_processed++;

    /// Check for grapheme boundary
    /// For streaming input, we need to check if the new codepoint
    /// forms a boundary with the previous one
    if (processor->previous_codepoint == 0) {
        /// First codepoint is always a boundary
        *is_grapheme_boundary = true;
        processor->grapheme_clusters_detected++;
    } else {
        /// Use the existing grapheme boundary detection
        /// We need to construct a temporary buffer with prev + current
        char boundary_test[8];
        int prev_len = lle_utf8_encode_codepoint(processor->previous_codepoint,
                                                 boundary_test);
        int curr_len = lle_utf8_encode_codepoint(processor->current_codepoint,
                                                 boundary_test + prev_len);

        if (prev_len > 0 && curr_len > 0) {
            *is_grapheme_boundary = lle_is_grapheme_boundary(
                boundary_test + prev_len, boundary_test,
                boundary_test + prev_len + curr_len);
            if (*is_grapheme_boundary) {
                processor->grapheme_clusters_detected++;
            }
        } else {
            /// Encoding failed - assume boundary
            *is_grapheme_boundary = true;
            processor->grapheme_clusters_detected++;
        }
    }

    /// Reset for next sequence
    processor->utf8_pos = 0;
    processor->expected_bytes = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Process multiple bytes from input buffer
 *
 * Processes as many complete sequences as possible from the buffer.
 * Returns the number of bytes consumed.
 *
 * @param processor UTF-8 processor instance
 * @param buffer Input buffer to process
 * @param buffer_len Length of input buffer
 * @param codepoints Output array for decoded codepoints
 * @param max_codepoints Maximum codepoints to decode
 * @param codepoints_decoded Output for number of codepoints decoded
 * @param bytes_consumed Output for number of bytes consumed
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_utf8_processor_process_buffer(
    lle_utf8_processor_t *processor, const char *buffer, size_t buffer_len,
    lle_codepoint_info_t *codepoints, size_t max_codepoints,
    size_t *codepoints_decoded, size_t *bytes_consumed) {
    if (!processor || !buffer || !codepoints || !codepoints_decoded ||
        !bytes_consumed) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *codepoints_decoded = 0;
    *bytes_consumed = 0;

    size_t pos = 0;
    while (pos < buffer_len && *codepoints_decoded < max_codepoints) {
        uint32_t codepoint = 0;
        bool is_boundary = false;

        lle_result_t result = lle_input_utf8_processor_process_byte(
            processor, (unsigned char)buffer[pos], &codepoint, &is_boundary);

        pos++; /// Always consume the byte

        if (result == LLE_ERROR_INVALID_ENCODING) {
            /// Skip invalid byte and continue
            continue;
        }

        /// If we got a complete codepoint, add it to output
        if (codepoint != 0) {
            codepoints[*codepoints_decoded].codepoint = codepoint;
            codepoints[*codepoints_decoded].is_grapheme_boundary = is_boundary;
            codepoints[*codepoints_decoded].width =
                lle_utf8_codepoint_width(codepoint);
            (*codepoints_decoded)++;
        }
    }

    *bytes_consumed = pos;
    return LLE_SUCCESS;
}

/**
 * @brief Get statistics from UTF-8 processor
 *
 * @param processor Processor to query
 * @param stats Output structure for statistics
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_input_utf8_processor_get_stats(const lle_utf8_processor_t *processor,
                                   lle_utf8_processor_stats_t *stats) {
    if (!processor || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    stats->codepoints_processed = processor->codepoints_processed;
    stats->grapheme_clusters_detected = processor->grapheme_clusters_detected;
    stats->invalid_sequences_handled = processor->invalid_sequences_handled;
    stats->partial_sequence_length = processor->utf8_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Validate a complete UTF-8 string (convenience wrapper)
 *
 * @param text Text to validate
 * @param length Length of text
 * @return true if valid UTF-8, false otherwise
 */
bool lle_input_utf8_validate_string(const char *text, size_t length) {
    return lle_utf8_is_valid(text, length);
}

/**
 * @brief Count codepoints in a string (convenience wrapper)
 *
 * @param text Text to count
 * @param length Length of text
 * @return Number of codepoints in the string
 */
size_t lle_input_utf8_count_codepoints(const char *text, size_t length) {
    return lle_utf8_count_codepoints(text, length);
}

/**
 * @brief Count grapheme clusters in a string (convenience wrapper)
 *
 * @param text Text to count
 * @param length Length of text
 * @return Number of grapheme clusters in the string
 */
size_t lle_input_utf8_count_graphemes(const char *text, size_t length) {
    return lle_utf8_count_graphemes(text, length);
}

/**
 * @brief Get display width of string (convenience wrapper)
 *
 * @param text Text to measure
 * @param length Length of text
 * @return Display width in terminal columns
 */
size_t lle_input_utf8_get_display_width(const char *text, size_t length) {
    return lle_utf8_string_width(text, length);
}
