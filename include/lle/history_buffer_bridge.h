/**
 * @file history_buffer_bridge.h
 * @brief Bidirectional integration between history and buffer systems
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 3
 * Provides transfer operations between history entries and edit buffers.
 */

#ifndef LLE_HISTORY_BUFFER_BRIDGE_H
#define LLE_HISTORY_BUFFER_BRIDGE_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Forward declarations
typedef struct lle_history_buffer_bridge lle_history_buffer_bridge_t;
typedef struct lle_history_core lle_history_core_t;
typedef struct lle_multiline_parser lle_multiline_parser_t;
typedef struct lle_reconstruction_engine lle_reconstruction_engine_t;

/**
 * @brief Buffer type - already defined in buffer_management.h
 */
typedef struct lle_buffer_t lle_buffer_t;

/**
 * @brief Transfer direction enumeration
 */
typedef enum lle_transfer_direction {
    LLE_TRANSFER_HISTORY_TO_BUFFER = 0, ///< Load from history into buffer
    LLE_TRANSFER_BUFFER_TO_HISTORY      ///< Save from buffer to history
} lle_transfer_direction_t;

/**
 * @brief Transfer options structure
 */
typedef struct lle_transfer_options {
    bool apply_reconstruction; /**< Apply reconstruction when loading to buffer
                                */
    bool parse_multiline;      ///< Parse multiline structure when loading
    bool preserve_indentation; ///< Preserve indentation
    bool create_new_entry;     ///< Create new entry vs update existing
    void *reserved[4];         ///< Reserved for future use
} lle_transfer_options_t;

/**
 * @brief Transfer result structure
 */
typedef struct lle_transfer_result {
    bool success;             ///< Success flag
    size_t entry_index;       ///< Entry index (for history operations)
    size_t buffer_position;   ///< Buffer position (for buffer operations)
    size_t bytes_transferred; ///< Bytes transferred
    bool is_multiline;        ///< Whether multiline structure was detected
    void *reserved[2];        ///< Reserved for future use
} lle_transfer_result_t;

/**
 * @brief Create a history-buffer bridge
 * @param bridge Output parameter for created bridge
 * @param memory_pool Memory pool for allocations
 * @param history_core History core for entry access
 * @param parser Multiline parser for structure analysis
 * @param reconstruction Reconstruction engine for formatting
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_buffer_bridge_create(
    lle_history_buffer_bridge_t **bridge, lle_memory_pool_t *memory_pool,
    lle_history_core_t *history_core, lle_multiline_parser_t *parser,
    lle_reconstruction_engine_t *reconstruction);

/**
 * @brief Destroy a history-buffer bridge
 * @param bridge Bridge to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_history_buffer_bridge_destroy(lle_history_buffer_bridge_t *bridge);

/**
 * @brief Transfer a history entry to a buffer
 * @param bridge History-buffer bridge
 * @param history_index History entry index
 * @param buffer Target buffer
 * @param options Transfer options (NULL for defaults)
 * @param result Output parameter for transfer result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_buffer_bridge_load_to_buffer(
    lle_history_buffer_bridge_t *bridge, size_t history_index,
    lle_buffer_t *buffer, const lle_transfer_options_t *options,
    lle_transfer_result_t *result);

/**
 * @brief Transfer buffer contents to history
 * @param bridge History-buffer bridge
 * @param buffer Source buffer
 * @param options Transfer options (NULL for defaults)
 * @param result Output parameter for transfer result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_history_buffer_bridge_save_from_buffer(
    lle_history_buffer_bridge_t *bridge, lle_buffer_t *buffer,
    const lle_transfer_options_t *options, lle_transfer_result_t *result);

/**
 * @brief Get buffer contents as string
 * @param bridge History-buffer bridge
 * @param buffer Source buffer
 * @param text Output parameter for text (allocated by function)
 * @param length Output parameter for text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_history_buffer_bridge_get_buffer_text(lle_history_buffer_bridge_t *bridge,
                                          lle_buffer_t *buffer, char **text,
                                          size_t *length);

/**
 * @brief Set buffer contents from string
 * @param bridge History-buffer bridge
 * @param buffer Target buffer
 * @param text Text to set
 * @param length Text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_history_buffer_bridge_set_buffer_text(lle_history_buffer_bridge_t *bridge,
                                          lle_buffer_t *buffer,
                                          const char *text, size_t length);

/**
 * @brief Clear buffer contents
 * @param bridge History-buffer bridge
 * @param buffer Buffer to clear
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_history_buffer_bridge_clear_buffer(lle_history_buffer_bridge_t *bridge,
                                       lle_buffer_t *buffer);

/**
 * @brief Get default transfer options
 * @param options Options structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_history_buffer_bridge_get_default_options(lle_transfer_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /// LLE_HISTORY_BUFFER_BRIDGE_H
