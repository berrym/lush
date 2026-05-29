/**
 * @file buffer_validator.c
 * @brief Buffer Validation Subsystem Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Spec Reference: Spec 03 Section 8 - Buffer Validation and Integrity
 *
 * Validates buffer integrity including UTF-8 encoding, line structure,
 * cursor positions, and bounds checking to detect corruption early.
 */

#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/time_util.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/// External UTF-8 validation functions from utf8_support.c
extern bool lle_utf8_is_valid(const char *text, size_t length);

/**
 * @brief Initialize buffer validator
 *
 * @param validator Pointer to receive initialized validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validator_init(lle_buffer_validator_t **validator) {
    if (!validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate validator structure
    lle_buffer_validator_t *val = calloc(1, sizeof(lle_buffer_validator_t));
    if (!val) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize with all validations enabled by default
    val->utf8_validation_enabled = true;
    val->line_structure_validation = true;
    val->cursor_validation_enabled = true;
    val->bounds_checking_enabled = true;

    /// Initialize statistics to zero (already done by calloc)
    val->validation_count = 0;
    val->validation_failures = 0;
    val->corruption_detections = 0;
    val->bounds_violations = 0;

    /// No UTF-8 processor reference by default
    val->utf8_processor = NULL;

    /// Initialize last validation results
    val->last_validation_result = LLE_SUCCESS;
    val->last_validation_time = 0;

    *validator = val;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy buffer validator
 *
 * @param validator Buffer validator to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validator_destroy(lle_buffer_validator_t *validator) {
    if (!validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free validator structure
    free(validator);

    return LLE_SUCCESS;
}

/**
 * @brief Validate UTF-8 encoding
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validate_utf8(lle_buffer_t *buffer,
                                      lle_buffer_validator_t *validator) {
    if (!buffer || !validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Skip if UTF-8 validation is disabled
    if (!validator->utf8_validation_enabled) {
        return LLE_SUCCESS;
    }

    /// Validate UTF-8 encoding
    if (!lle_utf8_is_valid(buffer->data, buffer->length)) {
        validator->validation_failures++;
        validator->corruption_detections++;
        return LLE_ERROR_INVALID_ENCODING;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Validate line structure
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_buffer_validate_line_structure(lle_buffer_t *buffer,
                                   lle_buffer_validator_t *validator) {
    if (!buffer || !validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Skip if line structure validation is disabled
    if (!validator->line_structure_validation) {
        return LLE_SUCCESS;
    }

    /// Check that line count doesn't exceed capacity
    if (buffer->line_count > buffer->line_capacity) {
        validator->validation_failures++;
        validator->corruption_detections++;
        return LLE_ERROR_INVALID_STATE;
    }

    /// Validate each line's offsets
    for (size_t i = 0; i < buffer->line_count; i++) {
        lle_line_info_t *line = &buffer->lines[i];

        /// Check that start offset is within buffer
        if (line->start_offset > buffer->length) {
            validator->validation_failures++;
            validator->bounds_violations++;
            return LLE_ERROR_INVALID_RANGE;
        }

        /// Check that line doesn't extend past buffer end
        if (line->start_offset + line->length > buffer->length) {
            validator->validation_failures++;
            validator->bounds_violations++;
            return LLE_ERROR_INVALID_RANGE;
        }

        /// Check line ordering (each line should start after previous)
        if (i > 0) {
            lle_line_info_t *prev_line = &buffer->lines[i - 1];
            if (line->start_offset <
                prev_line->start_offset + prev_line->length) {
                validator->validation_failures++;
                validator->corruption_detections++;
                return LLE_ERROR_MEMORY_CORRUPTION;
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Validate cursor position
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_buffer_validate_cursor_position(lle_buffer_t *buffer,
                                    lle_buffer_validator_t *validator) {
    if (!buffer || !validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Skip if cursor validation is disabled
    if (!validator->cursor_validation_enabled) {
        return LLE_SUCCESS;
    }

    /// Check that cursor byte offset is within buffer bounds
    if (buffer->cursor.byte_offset > buffer->length) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_INVALID_RANGE;
    }

    /// Check that cursor line number is within line count
    if (buffer->cursor.line_number >= buffer->line_count &&
        buffer->line_count > 0) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_INVALID_RANGE;
    }

    /// If cursor position is marked as valid, check version consistency
    if (buffer->cursor.position_valid) {
        /// Cursor version should not be ahead of buffer modification count
        if (buffer->cursor.buffer_version > buffer->modification_count) {
            validator->validation_failures++;
            validator->corruption_detections++;
            return LLE_ERROR_STATE_CORRUPTION;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Validate buffer bounds
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validate_bounds(lle_buffer_t *buffer,
                                        lle_buffer_validator_t *validator) {
    if (!buffer || !validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Skip if bounds checking is disabled
    if (!validator->bounds_checking_enabled) {
        return LLE_SUCCESS;
    }

    /// Check that length doesn't exceed capacity
    if (buffer->length > buffer->capacity) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Check that used space doesn't exceed capacity
    if (buffer->used > buffer->capacity) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Check that length doesn't exceed used space
    if (buffer->length > buffer->used) {
        validator->validation_failures++;
        validator->corruption_detections++;
        return LLE_ERROR_INVALID_STATE;
    }

    /// Check minimum capacity requirement
    if (buffer->capacity < LLE_BUFFER_MIN_CAPACITY) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_INVALID_STATE;
    }

    /// Check maximum capacity limit
    if (buffer->capacity > LLE_BUFFER_MAX_CAPACITY) {
        validator->validation_failures++;
        validator->bounds_violations++;
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Check that data pointer is not NULL if capacity > 0
    if (buffer->capacity > 0 && buffer->data == NULL) {
        validator->validation_failures++;
        validator->corruption_detections++;
        return LLE_ERROR_NULL_POINTER;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Validate complete buffer
 *
 * Spec Reference: Spec 03, Section 8.1
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validate_complete(lle_buffer_t *buffer,
                                          lle_buffer_validator_t *validator) {
    if (!buffer || !validator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = LLE_SUCCESS;

    /// Update validation statistics
    validator->validation_count++;
    uint64_t start_time = lle_get_current_time_microseconds();

    /// Step 1: Validate buffer bounds first (most critical)
    result = lle_buffer_validate_bounds(buffer, validator);
    if (result != LLE_SUCCESS) {
        validator->last_validation_result = result;
        validator->last_validation_time =
            lle_get_current_time_microseconds() - start_time;
        return result;
    }

    /// Step 2: Validate UTF-8 encoding
    result = lle_buffer_validate_utf8(buffer, validator);
    if (result != LLE_SUCCESS) {
        validator->last_validation_result = result;
        validator->last_validation_time =
            lle_get_current_time_microseconds() - start_time;
        return result;
    }

    /// Step 3: Validate null termination
    if (buffer->data && buffer->data[buffer->length] != '\0') {
        validator->validation_failures++;
        validator->corruption_detections++;
        validator->last_validation_result = LLE_ERROR_MEMORY_CORRUPTION;
        validator->last_validation_time =
            lle_get_current_time_microseconds() - start_time;
        return LLE_ERROR_MEMORY_CORRUPTION;
    }

    /// Step 4: Validate line structure
    result = lle_buffer_validate_line_structure(buffer, validator);
    if (result != LLE_SUCCESS) {
        validator->last_validation_result = result;
        validator->last_validation_time =
            lle_get_current_time_microseconds() - start_time;
        return result;
    }

    /// Step 5: Validate cursor position
    result = lle_buffer_validate_cursor_position(buffer, validator);
    if (result != LLE_SUCCESS) {
        validator->last_validation_result = result;
        validator->last_validation_time =
            lle_get_current_time_microseconds() - start_time;
        return result;
    }

    /// All validations passed
    validator->last_validation_result = LLE_SUCCESS;
    validator->last_validation_time =
        lle_get_current_time_microseconds() - start_time;

    return LLE_SUCCESS;
}
