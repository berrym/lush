/**
 * @file command_structure.h
 * @brief Shell command structure analysis and construct detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Part of Spec 22 Phase 2: Multiline Reconstruction Engine
 * Defines structures and types for analyzing shell command constructs
 * (loops, conditionals, functions, etc.) and preserving their structure.
 */

#ifndef LLE_COMMAND_STRUCTURE_H
#define LLE_COMMAND_STRUCTURE_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Shell construct types
 */
typedef enum lle_construct_type {
    LLE_CONSTRUCT_SIMPLE = 0,     ///< Simple command
    LLE_CONSTRUCT_PIPELINE,       ///< Pipeline (|)
    LLE_CONSTRUCT_FOR_LOOP,       ///< for ... do ... done
    LLE_CONSTRUCT_WHILE_LOOP,     ///< while ... do ... done
    LLE_CONSTRUCT_UNTIL_LOOP,     ///< until ... do ... done
    LLE_CONSTRUCT_IF_STATEMENT,   ///< if ... then ... fi
    LLE_CONSTRUCT_CASE_STATEMENT, ///< case ... esac
    LLE_CONSTRUCT_FUNCTION,       ///< function name() { ... }
    LLE_CONSTRUCT_SUBSHELL,       ///< ( ... )
    LLE_CONSTRUCT_COMMAND_GROUP,  ///< { ... }
    LLE_CONSTRUCT_SELECT,         ///< select ... do ... done
    LLE_CONSTRUCT_UNKNOWN         ///< Unparseable construct
} lle_construct_type_t;

/**
 * @brief Keyword types for shell constructs
 */
typedef enum lle_keyword_type {
    LLE_KEYWORD_FOR,         ///< for keyword
    LLE_KEYWORD_WHILE,       ///< while keyword
    LLE_KEYWORD_UNTIL,       ///< until keyword
    LLE_KEYWORD_DO,          ///< do keyword
    LLE_KEYWORD_DONE,        ///< done keyword
    LLE_KEYWORD_IF,          ///< if keyword
    LLE_KEYWORD_THEN,        ///< then keyword
    LLE_KEYWORD_ELIF,        ///< elif keyword
    LLE_KEYWORD_ELSE,        ///< else keyword
    LLE_KEYWORD_FI,          ///< fi keyword
    LLE_KEYWORD_CASE,        ///< case keyword
    LLE_KEYWORD_ESAC,        ///< esac keyword
    LLE_KEYWORD_IN,          ///< in keyword
    LLE_KEYWORD_FUNCTION,    ///< function keyword
    LLE_KEYWORD_SELECT,      ///< select keyword
    LLE_KEYWORD_OPEN_BRACE,  ///< {
    LLE_KEYWORD_CLOSE_BRACE, ///< }
    LLE_KEYWORD_OPEN_PAREN,  ///< (
    LLE_KEYWORD_CLOSE_PAREN, ///< )
    LLE_KEYWORD_NONE         ///< No keyword
} lle_keyword_type_t;

/* ============================================================================
 * STRUCTURES
 * ============================================================================
 */

/**
 * @brief Keyword position in command text
 */
typedef struct lle_keyword_position {
    lle_keyword_type_t type;           ///< Keyword type
    size_t offset;                     ///< Character offset in text
    size_t line_number;                ///< Line number (0-based)
    size_t indent_level;               ///< Indentation level
    struct lle_keyword_position *next; ///< Next keyword in list
} lle_keyword_position_t;

/**
 * @brief Indentation information
 */
typedef struct lle_indentation_info {
    size_t *level_per_line;   ///< Indentation level for each line
    size_t line_count;        ///< Number of lines
    char indent_char;         ///< Indent character (space or tab)
    uint8_t spaces_per_level; ///< Spaces per indentation level
    bool use_tabs;            ///< Use tabs for indentation
} lle_indentation_info_t;

/**
 * @brief Line mapping information
 */
typedef struct lle_line_mapping {
    size_t *line_offsets; ///< Byte offset of each line start
    size_t *line_lengths; ///< Length of each line
    size_t line_count;    ///< Number of lines
} lle_line_mapping_t;

/**
 * @brief Nested construct information
 */
typedef struct lle_nested_construct {
    lle_construct_type_t type;                 ///< Construct type
    size_t start_line;                         ///< Starting line number
    size_t end_line;                           ///< Ending line number
    size_t depth;                              ///< Nesting depth
    struct lle_nested_construct *parent;       ///< Parent construct
    struct lle_nested_construct *first_child;  ///< First child construct
    struct lle_nested_construct *next_sibling; ///< Next sibling
} lle_nested_construct_t;

/**
 * @brief Command structure information
 */
typedef struct lle_command_structure {
    lle_construct_type_t primary_type;      ///< Primary construct type
    lle_keyword_position_t *first_keyword;  ///< First keyword in list
    size_t keyword_count;                   ///< Number of keywords
    lle_nested_construct_t *root_construct; ///< Root of nesting tree
    size_t max_depth;                       ///< Maximum nesting depth
    lle_indentation_info_t *indentation;    ///< Indentation information
    lle_line_mapping_t *line_mapping;       ///< Line boundaries
    bool is_complete;                       ///< Structure is complete
    bool has_syntax_error;                  ///< Syntax error detected
    size_t total_lines;                     ///< Total number of lines
    lle_memory_pool_t *pool;                ///< Memory pool for allocations
} lle_command_structure_t;

/**
 * @brief Multiline information (extended)
 */
typedef struct lle_multiline_info {
    char *original_text;                ///< Original multiline text
    size_t original_length;             ///< Length of original
    lle_command_structure_t *structure; ///< Parsed structure
    bool needs_reconstruction;          ///< Reconstruction required
} lle_multiline_info_t;

/* ============================================================================
 * STRUCTURE MANAGEMENT FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create command structure
 * @param structure Output pointer for structure
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_command_structure_create(lle_command_structure_t **structure,
                                          lle_memory_pool_t *memory_pool);

/**
 * @brief Destroy command structure
 * @param structure Structure to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_command_structure_destroy(lle_command_structure_t *structure);

/**
 * @brief Add keyword position to structure
 * @param structure Command structure
 * @param type Keyword type
 * @param offset Character offset in text
 * @param line_number Line number
 * @param indent_level Indentation level
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_command_structure_add_keyword(lle_command_structure_t *structure,
                                  lle_keyword_type_t type, size_t offset,
                                  size_t line_number, size_t indent_level);

/**
 * @brief Get keyword count for type
 * @param structure Command structure
 * @param type Keyword type to count
 * @param count Output pointer for count
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_command_structure_count_keywords(lle_command_structure_t *structure,
                                     lle_keyword_type_t type, size_t *count);

/**
 * @brief Find matching keyword (e.g., 'done' for 'do')
 * @param structure Command structure
 * @param keyword Keyword position to find match for
 * @param match Output pointer for matching keyword
 * @return LLE_SUCCESS or error code (LLE_ERROR_NOT_FOUND if no match)
 */
lle_result_t
lle_command_structure_find_matching_keyword(lle_command_structure_t *structure,
                                            lle_keyword_position_t *keyword,
                                            lle_keyword_position_t **match);

#endif /// LLE_COMMAND_STRUCTURE_H
