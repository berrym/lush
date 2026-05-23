/**
 * @file theme_parser.h
 * @brief LLE Theme File Parser - TOML-subset Parser for Theme Files
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Issue #21 - Theme File Loading System
 * Version: 1.0.0
 *
 * Custom TOML-subset parser for parsing theme configuration files.
 * Designed to be dependency-free and focused on the subset of TOML
 * needed for theme files.
 *
 * Supported TOML Features:
 * - Key-value pairs: key = "value"
 * - Sections: [section] and [section.subsection]
 * - Inline tables: { key = "value", other = 123 }
 * - Arrays: ["item1", "item2"]
 * - Booleans: true, false
 * - Integers: 123, -45
 * - Strings: "quoted" with \n, \\, \" escapes
 * - Comments: # comment
 */

#ifndef LLE_PROMPT_THEME_PARSER_H
#define LLE_PROMPT_THEME_PARSER_H

#include "lle/error_handling.h"
#include "lle/prompt/theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND LIMITS
 * ============================================================================
 */

/** @brief Maximum parser error message length */
#define LLE_THEME_PARSER_ERROR_MAX 256

/** @brief Maximum key name length */
#define LLE_THEME_PARSER_KEY_MAX 64

/** @brief Maximum string value length */
#define LLE_THEME_PARSER_STRING_MAX 1024

/** @brief Maximum section path depth (e.g., [a.b.c.d]) */
#define LLE_THEME_PARSER_SECTION_DEPTH_MAX 4

/** @brief Maximum number of elements in an array */
#define LLE_THEME_PARSER_ARRAY_MAX 32

/** @brief Maximum number of entries in an inline table */
#define LLE_THEME_PARSER_TABLE_ENTRIES_MAX 16

/* ============================================================================
 * VALUE TYPES
 * ============================================================================
 */

/**
 * @brief Parsed value types
 */
typedef enum lle_theme_value_type {
    LLE_THEME_VALUE_STRING,  /**< String value: "text" */
    LLE_THEME_VALUE_INTEGER, /**< Integer value: 123 */
    LLE_THEME_VALUE_BOOLEAN, /**< Boolean value: true/false */
    LLE_THEME_VALUE_ARRAY,   /**< Array value: ["a", "b"] */
    LLE_THEME_VALUE_TABLE    /**< Inline table: { key = "value" } */
} lle_theme_value_type_t;

/**
 * @brief Key-value pair for inline tables
 */
typedef struct lle_theme_table_entry {
    char key[LLE_THEME_PARSER_KEY_MAX];
    struct lle_theme_value *value;
} lle_theme_table_entry_t;

/**
 * @brief Parsed value (supports all types)
 */
typedef struct lle_theme_value {
    lle_theme_value_type_t type;

    union {
        char string[LLE_THEME_PARSER_STRING_MAX];
        int64_t integer;
        bool boolean;

        struct {
            struct lle_theme_value *items;
            size_t count;
        } array;

        struct {
            lle_theme_table_entry_t entries[LLE_THEME_PARSER_TABLE_ENTRIES_MAX];
            size_t count;
        } table;
    } data;
} lle_theme_value_t;

/* ============================================================================
 * PARSER STATE
 * ============================================================================
 */

/**
 * @brief Parser state structure
 *
 * Tracks the current position in the input and error state.
 */
typedef struct lle_theme_parser {
    const char *input; /**< Input string being parsed */
    size_t input_len;  /**< Length of input string */
    size_t pos;        /**< Current position in input */
    size_t line;       /**< Current line number (1-based) */
    size_t column;     /**< Current column number (1-based) */

    /** Current section path (e.g., "colors" or "symbols.ascii") */
    char current_section[LLE_THEME_PARSER_KEY_MAX *
                         LLE_THEME_PARSER_SECTION_DEPTH_MAX];

    /** Error message if parsing failed */
    char error_msg[LLE_THEME_PARSER_ERROR_MAX];

    /** Error position information */
    size_t error_line;
    size_t error_column;

    /** Statistics */
    size_t keys_parsed;
    size_t sections_parsed;
} lle_theme_parser_t;

/* ============================================================================
 * PARSER CALLBACK TYPES
 * ============================================================================
 */

/**
 * @brief Callback type for handling parsed key-value pairs
 *
 * @param section      Current section path (e.g., "colors" or "theme")
 * @param key          Key name
 * @param value        Parsed value
 * @param user_data    User-provided context
 * @return LLE_SUCCESS to continue parsing, error to stop
 */
typedef lle_result_t (*lle_theme_parser_callback_t)(
    const char *section, const char *key, const lle_theme_value_t *value,
    void *user_data);

/* ============================================================================
 * CORE PARSER API
 * ============================================================================
 */

/**
 * @brief Initialize a theme parser
 *
 * @param parser  Parser to initialize
 * @param input   Input string to parse (must remain valid during parsing)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_parser_init(lle_theme_parser_t *parser,
                                   const char *input);

/**
 * @brief Reset parser to beginning of input
 *
 * @param parser  Parser to reset
 */
void lle_theme_parser_reset(lle_theme_parser_t *parser);

/**
 * @brief Parse input and call callback for each key-value pair
 *
 * Parses the entire input, calling the callback for each key-value pair.
 * Stops on first error or if callback returns an error.
 *
 * @param parser     Parser to use
 * @param callback   Callback function for each key-value pair
 * @param user_data  User data passed to callback
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_parser_parse(lle_theme_parser_t *parser,
                                    lle_theme_parser_callback_t callback,
                                    void *user_data);

/**
 * @brief Get error message from parser
 *
 * @param parser  Parser to query
 * @return Error message string (empty if no error)
 */
const char *lle_theme_parser_error(const lle_theme_parser_t *parser);

/**
 * @brief Get error line number
 *
 * @param parser  Parser to query
 * @return Line number where error occurred (1-based)
 */
size_t lle_theme_parser_error_line(const lle_theme_parser_t *parser);

/**
 * @brief Get error column number
 *
 * @param parser  Parser to query
 * @return Column number where error occurred (1-based)
 */
size_t lle_theme_parser_error_column(const lle_theme_parser_t *parser);

/* ============================================================================
 * DIRECT PARSING TO THEME STRUCTURE
 * ============================================================================
 */

/**
 * @brief Parse input directly into a theme structure
 *
 * High-level function that parses theme file content and populates
 * the theme structure. This is the primary API for loading themes.
 *
 * @param parser  Initialized parser
 * @param theme   Theme structure to populate (should be pre-initialized)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_parser_parse_to_theme(lle_theme_parser_t *parser,
                                             lle_theme_t *theme);

/* ============================================================================
 * VALUE HELPERS
 * ============================================================================
 */

/**
 * @brief Create a string value
 *
 * @param value   Value structure to initialize
 * @param str     String content
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_theme_value_set_string(lle_theme_value_t *value,
                                        const char *str);

/**
 * @brief Create an integer value
 *
 * @param value    Value structure to initialize
 * @param integer  Integer content
 */
void lle_theme_value_set_integer(lle_theme_value_t *value, int64_t integer);

/**
 * @brief Create a boolean value
 *
 * @param value    Value structure to initialize
 * @param boolean  Boolean content
 */
void lle_theme_value_set_boolean(lle_theme_value_t *value, bool boolean);

/**
 * @brief Free resources used by a value
 *
 * Frees any dynamically allocated memory in the value (e.g., array items).
 *
 * @param value  Value to free
 */
void lle_theme_value_free(lle_theme_value_t *value);

/**
 * @brief Get a string from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output string buffer
 * @param out_len Size of output buffer
 * @return LLE_SUCCESS if found, LLE_ERROR_NOT_FOUND if not
 */
lle_result_t lle_theme_value_table_get_string(const lle_theme_value_t *value,
                                              const char *key, char *out,
                                              size_t out_len);

/**
 * @brief Get an integer from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output integer
 * @return LLE_SUCCESS if found, LLE_ERROR_NOT_FOUND if not
 */
lle_result_t lle_theme_value_table_get_integer(const lle_theme_value_t *value,
                                               const char *key, int64_t *out);

/**
 * @brief Get a boolean from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output boolean
 * @return LLE_SUCCESS if found, LLE_ERROR_NOT_FOUND if not
 */
lle_result_t lle_theme_value_table_get_boolean(const lle_theme_value_t *value,
                                               const char *key, bool *out);

/* ============================================================================
 * COLOR PARSING
 * ============================================================================
 */

/**
 * @brief Parse a color specification string into lle_color_t
 *
 * Supported formats:
 * - Basic ANSI names: "red", "blue", "green", etc.
 * - 256-color index: 196, 255, etc.
 * - Hex RGB: "#ff5500", "#f50"
 * - RGB function: "rgb(255, 85, 0)"
 *
 * @param spec   Color specification string
 * @param color  Output color structure
 * @return LLE_SUCCESS or LLE_ERROR_INVALID_FORMAT
 */
lle_result_t lle_parse_color_spec(const char *spec, lle_color_t *color);

/**
 * @brief Parse an inline table into a color with attributes
 *
 * Parses table entries like:
 * { fg = "blue", bold = true, italic = false }
 *
 * @param value  Table value to parse
 * @param color  Output color structure
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_parse_color_table(const lle_theme_value_t *value,
                                   lle_color_t *color);

/* ============================================================================
 * VALIDATION
 * ============================================================================
 */

/**
 * @brief Validate a parsed theme structure
 *
 * Checks for:
 * - Required fields (name)
 * - Valid color values
 * - Valid segment names
 * - Template syntax
 *
 * @param theme  Theme to validate
 * @param error_buf  Buffer for error message (optional)
 * @param error_buf_len  Size of error buffer
 * @return LLE_SUCCESS or validation error
 */
lle_result_t lle_theme_parser_validate(const lle_theme_t *theme,
                                       char *error_buf, size_t error_buf_len);

#ifdef __cplusplus
}
#endif

#endif // LLE_PROMPT_THEME_PARSER_H
