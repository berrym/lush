/**
 * @file utf8_index.h
 * @brief UTF-8 index structure for bidirectional position mapping
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides fast bidirectional mapping between:
 * - Byte offsets (storage)
 * - Codepoint indices (logical characters)
 * - Grapheme cluster indices (user-visible characters)
 * - Display columns (visual position)
 *
 * Based on Spec 03, Section 4.1-4.2
 */

#ifndef LLE_UTF8_INDEX_H
#define LLE_UTF8_INDEX_H

#include "error_handling.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief UTF-8 Index Structure
 *
 * Full definition in buffer_management.h
 */
typedef struct lle_utf8_index_t lle_utf8_index_t;

/**
 * @brief Initialize UTF-8 index structure
 * @param index Pointer to index structure to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_init(lle_utf8_index_t *index);

/**
 * @brief Clean up UTF-8 index structure
 * @param index Pointer to index structure to clean up
 */
void lle_utf8_index_cleanup(lle_utf8_index_t *index);

/**
 * @brief Rebuild UTF-8 index from text
 *
 * Analyzes text and builds all mapping arrays.
 * This is the core function - see Spec 03 lines 612-763 for algorithm.
 *
 * @param index Index structure to rebuild
 * @param text Text to index (UTF-8 encoded)
 * @param text_length Length of text in bytes
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_rebuild(lle_utf8_index_t *index, const char *text,
                                    size_t text_length);

/**
 * @brief Convert byte offset to codepoint index
 * @param index UTF-8 index
 * @param byte_offset Byte offset in buffer
 * @param codepoint_index Output: codepoint index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_byte_to_codepoint(const lle_utf8_index_t *index,
                                              size_t byte_offset,
                                              size_t *codepoint_index);

/**
 * @brief Convert codepoint index to byte offset
 * @param index UTF-8 index
 * @param codepoint_index Codepoint index
 * @param byte_offset Output: byte offset
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_codepoint_to_byte(const lle_utf8_index_t *index,
                                              size_t codepoint_index,
                                              size_t *byte_offset);

/**
 * @brief Convert codepoint index to grapheme cluster index
 * @param index UTF-8 index
 * @param codepoint_index Codepoint index
 * @param grapheme_index Output: grapheme cluster index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_codepoint_to_grapheme(const lle_utf8_index_t *index,
                                                  size_t codepoint_index,
                                                  size_t *grapheme_index);

/**
 * @brief Convert grapheme cluster index to codepoint index (start of cluster)
 * @param index UTF-8 index
 * @param grapheme_index Grapheme cluster index
 * @param codepoint_index Output: codepoint index (first codepoint in cluster)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_grapheme_to_codepoint(const lle_utf8_index_t *index,
                                                  size_t grapheme_index,
                                                  size_t *codepoint_index);

/**
 * @brief Convert grapheme cluster index to display column
 * @param index UTF-8 index
 * @param grapheme_index Grapheme cluster index
 * @param display_column Output: display column
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_grapheme_to_display(const lle_utf8_index_t *index,
                                                size_t grapheme_index,
                                                size_t *display_column);

/**
 * @brief Convert display column to grapheme cluster index
 * @param index UTF-8 index
 * @param display_column Display column
 * @param grapheme_index Output: grapheme cluster index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_display_to_grapheme(const lle_utf8_index_t *index,
                                                size_t display_column,
                                                size_t *grapheme_index);

/**
 * @brief Invalidate index (mark as needing rebuild)
 * @param index Index to invalidate
 */
void lle_utf8_index_invalidate(lle_utf8_index_t *index);

/**
 * @brief Check if index is valid
 * @param index Index to check
 * @return true if valid, false if needs rebuild
 */
bool lle_utf8_index_is_valid(const lle_utf8_index_t *index);

#endif /// LLE_UTF8_INDEX_H
