/**
 * @file structure_analyzer.h
 * @brief Shell construct detection and keyword extraction
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 * Analyzes shell commands to detect constructs, keywords, and structure.
 */

#ifndef LLE_STRUCTURE_ANALYZER_H
#define LLE_STRUCTURE_ANALYZER_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct lle_structure_analyzer lle_structure_analyzer_t;

/**
 * @brief Configuration for structure analysis
 */
typedef struct lle_analyzer_config {
    size_t max_nesting_depth;  /**< Maximum nesting depth to analyze */
    bool detect_incomplete;    /**< Whether to detect incomplete constructs */
    bool validate_syntax;      /**< Whether to perform syntax validation */
    bool track_indentation;    /**< Whether to track indentation */
    size_t max_command_length; /**< Maximum command length to analyze (safety
                                  limit) */
    void *reserved[4];         /**< Reserved for future use */
} lle_analyzer_config_t;

/**
 * @brief Analysis context for tracking state during parsing
 */
typedef struct lle_analysis_context {
    size_t current_line;   /**< Current line being analyzed */
    size_t current_offset; /**< Current character offset in command */
    size_t current_depth;  /**< Current nesting depth */
    bool in_quoted_string; /**< Whether currently inside a quoted string */
    char quote_char;       /**< Quote character if in_quoted_string is true */
    bool last_was_escape;  /**< Whether last character was an escape */
    bool in_comment;       /**< Whether currently inside a comment */
    size_t indent_level;   /**< Current indentation level */
    void *reserved[4];     /**< Reserved for future use */
} lle_analysis_context_t;

/**
 * @brief Keyword detection result
 */
typedef struct lle_keyword_match {
    lle_keyword_type_t type; /**< Type of keyword matched */
    size_t start_offset;     /**< Start offset in command text */
    size_t length;           /**< Length of keyword */
    size_t line_number;      /**< Line number where keyword appears */
    bool is_command_start;   /**< Whether keyword is at start of command/line */
    void *reserved[2];       /**< Reserved for future use */
} lle_keyword_match_t;

/**
 * @brief Create a structure analyzer instance
 * @param analyzer Output parameter for created analyzer
 * @param memory_pool Memory pool for allocations
 * @param config Configuration for analyzer (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_create(lle_structure_analyzer_t **analyzer,
                                           lle_memory_pool_t *memory_pool,
                                           const lle_analyzer_config_t *config);

/**
 * @brief Destroy a structure analyzer instance
 * @param analyzer Analyzer to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_destroy(lle_structure_analyzer_t *analyzer);

/**
 * @brief Analyze a command text and build its structure
 * @param analyzer Structure analyzer instance
 * @param command_text Command text to analyze
 * @param command_length Length of command text
 * @param structure Output parameter for command structure
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_analyze(lle_structure_analyzer_t *analyzer,
                               const char *command_text, size_t command_length,
                               lle_command_structure_t **structure);

/**
 * @brief Detect all keywords in a command text
 *
 * This is a lower-level function that just extracts keywords without
 * building the full structure tree.
 *
 * @param analyzer Structure analyzer instance
 * @param command_text Command text to analyze
 * @param command_length Length of command text
 * @param keywords Output array of keyword matches (allocated by function)
 * @param keyword_count Output parameter for number of keywords found
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_detect_keywords(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_keyword_match_t **keywords,
    size_t *keyword_count);

/**
 * @brief Determine the primary construct type of a command
 * @param analyzer Structure analyzer instance
 * @param command_text Command text to analyze
 * @param command_length Length of command text
 * @param construct_type Output parameter for construct type
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_detect_type(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_construct_type_t *construct_type);

/**
 * @brief Check if a command is complete (all constructs properly closed)
 * @param analyzer Structure analyzer instance
 * @param command_text Command text to analyze
 * @param command_length Length of command text
 * @param is_complete Output parameter for completeness status
 * @param missing_keyword Output parameter for missing closing keyword type (if
 * incomplete)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_check_complete(lle_structure_analyzer_t *analyzer,
                                      const char *command_text,
                                      size_t command_length, bool *is_complete,
                                      lle_keyword_type_t *missing_keyword);

/**
 * @brief Calculate indentation information for a multiline command
 * @param analyzer Structure analyzer instance
 * @param command_text Command text to analyze
 * @param command_length Length of command text
 * @param indent_info Output parameter for indentation information
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_calculate_indentation(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_indentation_info_t **indent_info);

/**
 * @brief Get default analyzer configuration
 * @param config Configuration structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_get_default_config(lle_analyzer_config_t *config);

/**
 * @brief Reset analyzer state for reuse
 * @param analyzer Structure analyzer instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_reset(lle_structure_analyzer_t *analyzer);

#ifdef __cplusplus
}
#endif

#endif /* LLE_STRUCTURE_ANALYZER_H */
