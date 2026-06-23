/**
 * @file multiline_parser.c
 * @brief Multiline command parsing and line splitting
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration
 *
 * This module provides multiline command parsing capabilities, including
 * line splitting, continuation detection, and keyword analysis for shell
 * command constructs.
 */

#include "lle/multiline_parser.h"
#include "lle/command_structure.h"
#include "lle/memory_management.h"
#include "lle/structure_analyzer.h"
#include <ctype.h>
#include <string.h>

// Default configuration values
#define DEFAULT_MAX_LINES 10000

// Multiline parser implementation
struct lle_multiline_parser {
    lle_memory_pool_t *memory_pool;
    lle_structure_analyzer_t *analyzer;
    lle_parser_config_t config;
    bool active;
};

// Forward declarations for internal functions
static lle_parsed_line_t *create_parsed_line(lle_multiline_parser_t *parser,
                                             const char *content, size_t length,
                                             size_t line_number);
static void free_parsed_line_list(lle_parsed_line_t *first_line);
static bool has_backslash_continuation(const char *line, size_t length);
static size_t calculate_indent_level(const char *line, size_t length);

/* ============================================================================
 * PUBLIC API - CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Get default multiline parser configuration
 * @param config Pointer to configuration structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if config is NULL
 */
lle_result_t
lle_multiline_parser_get_default_config(lle_parser_config_t *config) {
    if (!config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    config->preserve_indentation = true;
    config->detect_continuations = true;
    config->validate_syntax = true;
    config->max_lines = DEFAULT_MAX_LINES;
    memset(config->reserved, 0, sizeof(config->reserved));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new multiline parser instance
 * @param parser Pointer to store the created parser
 * @param memory_pool Memory pool for allocations (can be NULL for global pool)
 * @param analyzer Structure analyzer for command analysis
 * @param config Parser configuration (can be NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_create(lle_multiline_parser_t **parser,
                                         lle_memory_pool_t *memory_pool,
                                         lle_structure_analyzer_t *analyzer,
                                         const lle_parser_config_t *config) {
    if (!parser || !analyzer) /* memory_pool can be NULL */ {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_multiline_parser_t *new_parser =
        lle_pool_alloc(sizeof(lle_multiline_parser_t));
    if (!new_parser) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline parser allocation failed");
    }

    new_parser->memory_pool = memory_pool;
    new_parser->analyzer = analyzer;

    if (config) {
        memcpy(&new_parser->config, config, sizeof(lle_parser_config_t));
    } else {
        lle_multiline_parser_get_default_config(&new_parser->config);
    }

    new_parser->active = true;

    *parser = new_parser;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a multiline parser and release resources
 * @param parser The parser to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parser is NULL
 */
lle_result_t lle_multiline_parser_destroy(lle_multiline_parser_t *parser) {
    if (!parser) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    parser->active = false;
    // Memory pool owns all allocations, no explicit frees needed

    return LLE_SUCCESS;
}

/**
 * @brief Reset the parser state for a new parsing session
 * @param parser The parser to reset
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_multiline_parser_reset(lle_multiline_parser_t *parser) {
    if (!parser || !parser->active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Reset analyzer if needed
    return lle_structure_analyzer_reset(parser->analyzer);
}

/* ============================================================================
 * PUBLIC API - PARSING OPERATIONS
 * ============================================================================
 */

/**
 * @brief Check if a line has a backslash continuation
 * @param parser The multiline parser
 * @param line_text The line text to check
 * @param line_length Length of the line
 * @param has_continuation Pointer to store continuation status
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_multiline_parser_check_continuation(
    lle_multiline_parser_t *parser, const char *line_text, size_t line_length,
    bool *has_continuation) {
    if (!parser || !parser->active || !line_text || !has_continuation) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!parser->config.detect_continuations) {
        *has_continuation = false;
        return LLE_SUCCESS;
    }

    *has_continuation = has_backslash_continuation(line_text, line_length);

    return LLE_SUCCESS;
}

/**
 * @brief Split command text into individual lines
 * @param parser The multiline parser
 * @param command_text The command text to split
 * @param command_length Length of the command text
 * @param lines Pointer to store array of parsed lines
 * @param line_count Pointer to store number of lines
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_split_lines(lle_multiline_parser_t *parser,
                                              const char *command_text,
                                              size_t command_length,
                                              lle_parsed_line_t ***lines,
                                              size_t *line_count) {
    if (!parser || !parser->active || !command_text || !lines || !line_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Count lines first
    size_t count = 1;
    for (size_t i = 0; i < command_length; i++) {
        if (command_text[i] == '\n') {
            count++;
        }
    }

    if (count > parser->config.max_lines) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate line array
    lle_parsed_line_t **line_array =
        lle_pool_alloc(count * sizeof(lle_parsed_line_t *));
    if (!line_array) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline parser allocation failed");
    }

    // Split into lines
    size_t line_idx = 0;
    size_t line_start = 0;

    for (size_t i = 0; i <= command_length; i++) {
        if (i == command_length || command_text[i] == '\n') {
            size_t line_len = i - line_start;

            lle_parsed_line_t *line = create_parsed_line(
                parser, &command_text[line_start], line_len, line_idx);

            if (!line) {
                return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                                 "multiline parser allocation failed");
            }

            line_array[line_idx] = line;
            line_idx++;
            line_start = i + 1;
        }
    }

    *lines = line_array;
    *line_count = count;

    return LLE_SUCCESS;
}

/**
 * @brief Parse command text into a structured result
 * @param parser The multiline parser
 * @param command_text The command text to parse
 * @param command_length Length of the command text
 * @param result Pointer to store the parse result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_multiline_parser_parse(lle_multiline_parser_t *parser,
                                        const char *command_text,
                                        size_t command_length,
                                        lle_multiline_parse_result_t **result) {
    if (!parser || !parser->active || !command_text || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate parse result
    lle_multiline_parse_result_t *parse_result =
        lle_pool_alloc(sizeof(lle_multiline_parse_result_t));
    if (!parse_result) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline parser allocation failed");
    }

    memset(parse_result, 0, sizeof(lle_multiline_parse_result_t));

    // Split into lines
    lle_parsed_line_t **lines = NULL;
    size_t line_count = 0;

    lle_result_t res = lle_multiline_parser_split_lines(
        parser, command_text, command_length, &lines, &line_count);

    if (res != LLE_SUCCESS) {
        return res;
    }

    // Build linked list of lines
    if (line_count > 0) {
        parse_result->first_line = lines[0];
        parse_result->last_line = lines[line_count - 1];

        for (size_t i = 0; i < line_count - 1; i++) {
            lines[i]->next = lines[i + 1];
        }
    }

    parse_result->line_count = line_count;
    parse_result->total_length = command_length;

    // Analyze command structure
    lle_command_structure_t *structure = NULL;
    res = lle_structure_analyzer_analyze(parser->analyzer, command_text,
                                         command_length, &structure);

    if (res == LLE_SUCCESS) {
        parse_result->structure = structure;
        parse_result->is_complete = structure->is_complete;
        parse_result->has_syntax_error = structure->has_syntax_error;
    } else {
        parse_result->structure = NULL;
        parse_result->is_complete = false;
        parse_result->has_syntax_error = true;
    }

    // Check for expected closing keyword if incomplete
    if (!parse_result->is_complete) {
        lle_keyword_type_t expected = LLE_KEYWORD_NONE;
        lle_structure_analyzer_check_complete(
            parser->analyzer, command_text, command_length,
            &parse_result->is_complete, &expected);
        parse_result->expected_keyword = expected;
    }

    *result = parse_result;
    return LLE_SUCCESS;
}

/**
 * @brief Free a parse result and its resources
 * @param parser The multiline parser
 * @param result The parse result to free
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_multiline_parser_free_result(lle_multiline_parser_t *parser,
                                 lle_multiline_parse_result_t *result) {
    if (!parser || !parser->active || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Free line list
    if (result->first_line) {
        free_parsed_line_list(result->first_line);
    }

    // Free command structure
    if (result->structure) {
        lle_command_structure_destroy(result->structure);
    }

    // Memory pool owns parse_result allocation

    return LLE_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create a parsed line structure from line content
 * @param parser The multiline parser
 * @param content The line content
 * @param length Length of the content
 * @param line_number Zero-based line number
 * @return Pointer to the created parsed line, or NULL on failure
 */
static lle_parsed_line_t *create_parsed_line(lle_multiline_parser_t *parser,
                                             const char *content, size_t length,
                                             size_t line_number) {
    lle_parsed_line_t *line = lle_pool_alloc(sizeof(lle_parsed_line_t));
    if (!line) {
        return NULL;
    }

    memset(line, 0, sizeof(lle_parsed_line_t));

    // Allocate and copy content
    line->content = lle_pool_alloc(length + 1);
    if (!line->content) {
        return NULL;
    }

    memcpy(line->content, content, length);
    line->content[length] = '\0';
    line->length = length;
    line->line_number = line_number;

    // Calculate indentation if configured
    if (parser->config.preserve_indentation) {
        line->indent_level = calculate_indent_level(content, length);
    }

    // Check for continuation
    if (parser->config.detect_continuations) {
        line->has_continuation = has_backslash_continuation(content, length);
    }

    // Detect primary keyword (simple detection)
    line->primary_keyword = LLE_KEYWORD_NONE;
    line->keyword_count = 0;

    // Skip leading whitespace to find first word
    size_t start = 0;
    while (start < length && isspace(content[start])) {
        start++;
    }

    if (start < length) {
        // Check for common keywords
        const char *keywords[] = {"for",  "while",    "until", "if",
                                  "case", "function", NULL};
        const lle_keyword_type_t types[] = {
            LLE_KEYWORD_FOR, LLE_KEYWORD_WHILE, LLE_KEYWORD_UNTIL,
            LLE_KEYWORD_IF,  LLE_KEYWORD_CASE,  LLE_KEYWORD_FUNCTION};

        for (size_t i = 0; keywords[i] != NULL; i++) {
            size_t kw_len = strlen(keywords[i]);
            if (start + kw_len <= length &&
                strncmp(&content[start], keywords[i], kw_len) == 0 &&
                (start + kw_len == length ||
                 isspace(content[start + kw_len]))) {
                line->primary_keyword = types[i];
                line->keyword_count = 1;
                break;
            }
        }
    }

    return line;
}

/**
 * @brief Free a linked list of parsed lines
 * @param first_line Head of the line list to free
 *
 * Memory pool owns all allocations, so this function exists for
 * API completeness and potential future enhancements.
 */
static void free_parsed_line_list(lle_parsed_line_t *first_line) {
    // Memory pool owns all allocations, no explicit frees needed
    // This function exists for API completeness and future enhancements
    (void)first_line;
}

/**
 * @brief Check if a line ends with a backslash continuation
 * @param line The line text
 * @param length Length of the line
 * @return true if the line has a backslash continuation, false otherwise
 */
static bool has_backslash_continuation(const char *line, size_t length) {
    if (length == 0) {
        return false;
    }

    // Skip trailing whitespace
    size_t end = length;
    while (end > 0 && isspace(line[end - 1])) {
        end--;
    }

    if (end == 0) {
        return false;
    }

    // Check for backslash
    if (line[end - 1] == '\\') {
        // Count preceding backslashes to handle escapes
        size_t backslash_count = 1;
        size_t pos = end - 1;

        while (pos > 0 && line[pos - 1] == '\\') {
            backslash_count++;
            pos--;
        }

        // Odd number of backslashes means continuation
        return (backslash_count % 2) == 1;
    }

    return false;
}

/**
 * @brief Calculate the indentation level of a line
 * @param line The line text
 * @param length Length of the line
 * @return Indentation level in spaces (tabs count as 4 spaces)
 */
static size_t calculate_indent_level(const char *line, size_t length) {
    size_t indent = 0;

    for (size_t i = 0; i < length; i++) {
        if (line[i] == ' ') {
            indent++;
        } else if (line[i] == '\t') {
            indent += 4; // Tab = 4 spaces
        } else {
            break; // Non-whitespace found
        }
    }

    return indent;
}
