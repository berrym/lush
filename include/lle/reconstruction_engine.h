/**
 * @file reconstruction_engine.h
 * @brief Reconstruction engine for multiline commands with indentation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 * Reconstructs multiline commands with proper indentation from history.
 */

#ifndef LLE_RECONSTRUCTION_ENGINE_H
#define LLE_RECONSTRUCTION_ENGINE_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/multiline_parser.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct lle_reconstruction_engine lle_reconstruction_engine_t;
typedef struct lle_structure_analyzer lle_structure_analyzer_t;
typedef struct lle_multiline_parser lle_multiline_parser_t;

/**
 * @brief Reconstruction options for command processing
 */
typedef struct lle_reconstruction_options {
    bool apply_indentation;    /**< Whether to apply automatic indentation */
    char indent_char;          /**< Indentation character (' ' or '\t') */
    uint8_t spaces_per_level;  /**< Spaces per indentation level */
    bool preserve_line_breaks; /**< Whether to preserve original line breaks */
    bool normalize_whitespace; /**< Whether to normalize whitespace */
    size_t max_output_length;  /**< Maximum output length (safety limit) */
    void *reserved[4];         /**< Reserved for future use */
} lle_reconstruction_options_t;

/**
 * @brief Reconstructed command result structure
 */
typedef struct lle_reconstructed_command {
    char *text;               /**< Reconstructed command text */
    size_t length;            /**< Length of reconstructed text */
    size_t line_count;        /**< Number of lines in result */
    size_t *line_offsets;     /**< Offset of each line in text */
    bool indentation_applied; /**< Whether indentation was applied */
    void *reserved[2];        /**< Reserved for future use */
} lle_reconstructed_command_t;

/**
 * @brief Create a reconstruction engine instance
 * @param engine Output parameter for created engine
 * @param memory_pool Memory pool for allocations
 * @param analyzer Structure analyzer for command analysis
 * @param parser Multiline parser for parsing
 * @param options Reconstruction options (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_create(
    lle_reconstruction_engine_t **engine, lle_memory_pool_t *memory_pool,
    lle_structure_analyzer_t *analyzer, lle_multiline_parser_t *parser,
    const lle_reconstruction_options_t *options);

/**
 * @brief Destroy a reconstruction engine instance
 * @param engine Engine to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_reconstruction_engine_destroy(lle_reconstruction_engine_t *engine);

/**
 * @brief Reconstruct a command from history with indentation
 * @param engine Reconstruction engine instance
 * @param command_text Original command text
 * @param command_length Length of command text
 * @param result Output parameter for reconstructed command
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_reconstruct(
    lle_reconstruction_engine_t *engine, const char *command_text,
    size_t command_length, lle_reconstructed_command_t **result);

/**
 * @brief Apply indentation to a command based on its structure
 * @param engine Reconstruction engine instance
 * @param structure Command structure
 * @param original_text Original command text
 * @param original_length Length of original text
 * @param indented_text Output parameter for indented text (allocated by
 * function)
 * @param indented_length Output parameter for indented text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_apply_indentation(
    lle_reconstruction_engine_t *engine, lle_command_structure_t *structure,
    const char *original_text, size_t original_length, char **indented_text,
    size_t *indented_length);

/**
 * @brief Normalize whitespace in a command
 * @param engine Reconstruction engine instance
 * @param command_text Command text to normalize
 * @param command_length Length of command text
 * @param normalized_text Output parameter for normalized text (allocated by
 * function)
 * @param normalized_length Output parameter for normalized text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_normalize_whitespace(
    lle_reconstruction_engine_t *engine, const char *command_text,
    size_t command_length, char **normalized_text, size_t *normalized_length);

/**
 * @brief Free a reconstructed command result
 * @param engine Reconstruction engine instance
 * @param result Reconstructed command to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_reconstruction_engine_free_result(lle_reconstruction_engine_t *engine,
                                      lle_reconstructed_command_t *result);

/**
 * @brief Get default reconstruction options
 * @param options Options structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_get_default_options(
    lle_reconstruction_options_t *options);

/**
 * @brief Update reconstruction options
 * @param engine Reconstruction engine instance
 * @param options New options to apply
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_set_options(
    lle_reconstruction_engine_t *engine,
    const lle_reconstruction_options_t *options);

#ifdef __cplusplus
}
#endif

#endif // LLE_RECONSTRUCTION_ENGINE_H
