/**
 * @file formatting_engine.h
 * @brief Intelligent formatting engine for shell commands
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 * Provides configurable formatting styles for shell command presentation.
 */

#ifndef LLE_FORMATTING_ENGINE_H
#define LLE_FORMATTING_ENGINE_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct lle_formatting_engine lle_formatting_engine_t;
typedef struct lle_structure_analyzer lle_structure_analyzer_t;

/**
 * @brief Formatting style enumeration
 */
typedef enum lle_formatting_style {
    LLE_FORMAT_COMPACT = 0, /**< Minimal whitespace */
    LLE_FORMAT_READABLE,    /**< Balanced readability */
    LLE_FORMAT_EXPANDED,    /**< Maximum readability */
    LLE_FORMAT_CUSTOM       /**< Custom formatting rules */
} lle_formatting_style_t;

/**
 * @brief Formatting options structure
 */
typedef struct lle_formatting_options {
    lle_formatting_style_t style; /**< Overall formatting style */
    char indent_char;             /**< Indentation character (' ' or '\t') */
    uint8_t spaces_per_level;     /**< Spaces per indentation level */
    bool break_long_lines;       /**< Break lines longer than max_line_length */
    size_t max_line_length;      /**< Maximum line length (0 = no limit) */
    bool normalize_spaces;       /**< Normalize whitespace */
    bool trim_trailing;          /**< Trim trailing whitespace */
    bool break_pipes;            /**< Break pipelines into multiple lines */
    bool align_pipe_operators;   /**< Align pipe operators vertically */
    bool space_around_operators; /**< Add spaces around operators */
    bool space_after_keywords;   /**< Add space after keywords */
    void *reserved[4];           /**< Reserved for future use */
} lle_formatting_options_t;

/**
 * @brief Formatted command result structure
 */
typedef struct lle_formatted_command {
    char *text;                           /**< Formatted command text */
    size_t length;                        /**< Length of formatted text */
    lle_formatting_style_t style_applied; /**< Formatting applied */
    bool was_reformatted;                 /**< Whether reformatting occurred */
    void *reserved[2];                    /**< Reserved for future use */
} lle_formatted_command_t;

/**
 * @brief Create a formatting engine instance
 * @param engine Output parameter for created engine
 * @param memory_pool Memory pool for allocations
 * @param analyzer Structure analyzer for command analysis
 * @param options Formatting options (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_formatting_engine_create(lle_formatting_engine_t **engine,
                             lle_memory_pool_t *memory_pool,
                             lle_structure_analyzer_t *analyzer,
                             const lle_formatting_options_t *options);

/**
 * @brief Destroy a formatting engine instance
 * @param engine Engine to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_destroy(lle_formatting_engine_t *engine);

/**
 * @brief Format a command according to specified style
 * @param engine Formatting engine instance
 * @param command_text Command text to format
 * @param command_length Length of command text
 * @param result Output parameter for formatted command
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_format(lle_formatting_engine_t *engine,
                                          const char *command_text,
                                          size_t command_length,
                                          lle_formatted_command_t **result);

/**
 * @brief Apply specific formatting style to a command
 * @param engine Formatting engine instance
 * @param command_text Command text to format
 * @param command_length Length of command text
 * @param style Formatting style to apply
 * @param formatted_text Output parameter for formatted text (allocated by
 * function)
 * @param formatted_length Output parameter for formatted text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_apply_style(lle_formatting_engine_t *engine,
                                               const char *command_text,
                                               size_t command_length,
                                               lle_formatting_style_t style,
                                               char **formatted_text,
                                               size_t *formatted_length);

/**
 * @brief Normalize whitespace in a command
 * @param engine Formatting engine instance
 * @param command_text Command text to normalize
 * @param command_length Length of command text
 * @param normalized_text Output parameter for normalized text (allocated by
 * function)
 * @param normalized_length Output parameter for normalized text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_normalize(lle_formatting_engine_t *engine,
                                             const char *command_text,
                                             size_t command_length,
                                             char **normalized_text,
                                             size_t *normalized_length);

/**
 * @brief Free a formatted command result
 * @param engine Formatting engine instance
 * @param result Formatted command to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_free_result(lle_formatting_engine_t *engine,
                                               lle_formatted_command_t *result);

/**
 * @brief Get default formatting options
 * @param options Options structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_formatting_engine_get_default_options(lle_formatting_options_t *options);

/**
 * @brief Get preset options for a specific style
 * @param style Formatting style
 * @param options Options structure to fill with preset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_formatting_engine_get_preset_options(lle_formatting_style_t style,
                                         lle_formatting_options_t *options);

/**
 * @brief Update formatting options
 * @param engine Formatting engine instance
 * @param options New options to apply
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_formatting_engine_set_options(lle_formatting_engine_t *engine,
                                  const lle_formatting_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* LLE_FORMATTING_ENGINE_H */
