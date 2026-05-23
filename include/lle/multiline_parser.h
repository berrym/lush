/**
 * @file multiline_parser.h
 * @brief Parse and preserve multiline command structure
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 * Provides multiline command parsing with structure preservation.
 */

#ifndef LLE_MULTILINE_PARSER_H
#define LLE_MULTILINE_PARSER_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct lle_multiline_parser lle_multiline_parser_t;
typedef struct lle_structure_analyzer lle_structure_analyzer_t;

/**
 * @brief Parsed line information structure
 */
typedef struct lle_parsed_line {
    char *content;         /**< Line content */
    size_t length;         /**< Length of content */
    size_t line_number;    /**< Line number in command */
    size_t indent_level;   /**< Indentation level */
    bool has_continuation; /**< Whether line ends with continuation */
    lle_keyword_type_t primary_keyword; /**< Primary keyword on line */
    size_t keyword_count;               /**< Number of keywords on line */
    struct lle_parsed_line *next;       /**< Next line in list */
} lle_parsed_line_t;

/**
 * @brief Multiline parse result structure
 */
typedef struct lle_multiline_parse_result {
    lle_parsed_line_t *first_line;      /**< First parsed line */
    lle_parsed_line_t *last_line;       /**< Last parsed line */
    size_t line_count;                  /**< Number of lines */
    lle_command_structure_t *structure; /**< Command structure */
    bool is_complete;                   /**< Whether command is complete */
    bool has_syntax_error;              /**< Whether syntax error detected */
    size_t total_length;                /**< Total character length */
    lle_keyword_type_t
        expected_keyword; /**< Expected closing keyword (if incomplete) */
    void *reserved[2];    /**< Reserved for future use */
} lle_multiline_parse_result_t;

/**
 * @brief Parser configuration structure
 */
typedef struct lle_parser_config {
    bool preserve_indentation; /**< Whether to preserve original indentation */
    bool detect_continuations; /**< Whether to detect continuation characters */
    bool validate_syntax;      /**< Whether to validate syntax */
    size_t max_lines;          /**< Maximum lines to parse (safety limit) */
    void *reserved[4];         /**< Reserved for future use */
} lle_parser_config_t;

/**
 * @brief Create a multiline parser instance
 * @param parser Output parameter for created parser
 * @param memory_pool Memory pool for allocations
 * @param analyzer Structure analyzer for command analysis
 * @param config Configuration (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_create(lle_multiline_parser_t **parser,
                                         lle_memory_pool_t *memory_pool,
                                         lle_structure_analyzer_t *analyzer,
                                         const lle_parser_config_t *config);

/**
 * @brief Destroy a multiline parser instance
 * @param parser Parser to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_destroy(lle_multiline_parser_t *parser);

/**
 * @brief Parse a multiline command
 * @param parser Multiline parser instance
 * @param command_text Command text to parse
 * @param command_length Length of command text
 * @param result Output parameter for parse result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_parse(lle_multiline_parser_t *parser,
                                        const char *command_text,
                                        size_t command_length,
                                        lle_multiline_parse_result_t **result);

/**
 * @brief Split command into individual lines
 * @param parser Multiline parser instance
 * @param command_text Command text to split
 * @param command_length Length of command text
 * @param lines Output parameter for line array (allocated by function)
 * @param line_count Output parameter for number of lines
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_split_lines(lle_multiline_parser_t *parser,
                                              const char *command_text,
                                              size_t command_length,
                                              lle_parsed_line_t ***lines,
                                              size_t *line_count);

/**
 * @brief Check if a line has a continuation character
 * @param parser Multiline parser instance
 * @param line_text Line text to check
 * @param line_length Length of line text
 * @param has_continuation Output parameter for continuation status
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_check_continuation(
    lle_multiline_parser_t *parser, const char *line_text, size_t line_length,
    bool *has_continuation);

/**
 * @brief Free a parse result
 * @param parser Multiline parser instance
 * @param result Parse result to free
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_multiline_parser_free_result(lle_multiline_parser_t *parser,
                                 lle_multiline_parse_result_t *result);

/**
 * @brief Get default parser configuration
 * @param config Configuration structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_multiline_parser_get_default_config(lle_parser_config_t *config);

/**
 * @brief Reset parser state for reuse
 * @param parser Multiline parser instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_reset(lle_multiline_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif // LLE_MULTILINE_PARSER_H
