/**
 * @file toml_parser.h
 * @brief Generic TOML-subset Parser for Lush Configuration
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Dependency-free TOML-subset parser for parsing configuration files.
 * This parser supports the TOML features needed for lush configuration:
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
 *
 * This is a core lush utility used by:
 * - Configuration system (config.c)
 * - Theme parser (theme_parser.c)
 * - Keybinding configuration
 * - Completion configuration
 */

#ifndef TOML_PARSER_H
#define TOML_PARSER_H

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
#define TOML_PARSER_ERROR_MAX 256

/** @brief Maximum key name length */
#define TOML_PARSER_KEY_MAX 64

/** @brief Maximum string value length */
#define TOML_PARSER_STRING_MAX 1024

/** @brief Maximum section path depth (e.g., [a.b.c.d]) */
#define TOML_PARSER_SECTION_DEPTH_MAX 4

/** @brief Maximum number of elements in an array */
#define TOML_PARSER_ARRAY_MAX 32

/** @brief Maximum number of entries in an inline table */
#define TOML_PARSER_TABLE_ENTRIES_MAX 16

/* ============================================================================
 * RESULT CODES
 * ============================================================================
 */

/**
 * @brief TOML parser result codes
 */
typedef enum toml_result {
    TOML_SUCCESS = 0,             ///< Operation succeeded
    TOML_ERROR_INVALID_PARAMETER, ///< NULL or invalid parameter
    TOML_ERROR_INVALID_FORMAT,    ///< Parse error in input
    TOML_ERROR_OUT_OF_MEMORY,     ///< Memory allocation failed
    TOML_ERROR_NOT_FOUND,         ///< Key not found in table
    TOML_ERROR_CALLBACK_ABORT     ///< Callback requested abort
} toml_result_t;

/* ============================================================================
 * VALUE TYPES
 * ============================================================================
 */

/**
 * @brief Parsed value types
 */
typedef enum toml_value_type {
    TOML_VALUE_STRING,  ///< String value: "text"
    TOML_VALUE_INTEGER, ///< Integer value: 123
    TOML_VALUE_BOOLEAN, ///< Boolean value: true/false
    TOML_VALUE_ARRAY,   ///< Array value: ["a", "b"]
    TOML_VALUE_TABLE    ///< Inline table: { key = "value" }
} toml_value_type_t;

/**
 * @brief Key-value pair for inline tables
 */
typedef struct toml_table_entry {
    char key[TOML_PARSER_KEY_MAX];
    struct toml_value *value;
} toml_table_entry_t;

/**
 * @brief Parsed value (supports all types)
 */
typedef struct toml_value {
    toml_value_type_t type;

    union {
        char string[TOML_PARSER_STRING_MAX];
        int64_t integer;
        bool boolean;

        struct {
            struct toml_value *items;
            size_t count;
        } array;

        struct {
            toml_table_entry_t entries[TOML_PARSER_TABLE_ENTRIES_MAX];
            size_t count;
        } table;
    } data;
} toml_value_t;

/* ============================================================================
 * PARSER STATE
 * ============================================================================
 */

/**
 * @brief Parser state structure
 *
 * Tracks the current position in the input and error state.
 */
typedef struct toml_parser {
    const char *input; ///< Input string being parsed
    size_t input_len;  ///< Length of input string
    size_t pos;        ///< Current position in input
    size_t line;       ///< Current line number (1-based)
    size_t column;     ///< Current column number (1-based)

    /** Current section path (e.g., "shell" or "shell.features") */
    char current_section[TOML_PARSER_KEY_MAX * TOML_PARSER_SECTION_DEPTH_MAX];

    /** Error message if parsing failed */
    char error_msg[TOML_PARSER_ERROR_MAX];

    /** Error position information */
    size_t error_line;
    size_t error_column;

    /** Statistics */
    size_t keys_parsed;
    size_t sections_parsed;
} toml_parser_t;

/* ============================================================================
 * PARSER CALLBACK TYPE
 * ============================================================================
 */

/**
 * @brief Callback type for handling parsed key-value pairs
 *
 * @param section      Current section path (e.g., "shell" or "shell.features")
 * @param key          Key name
 * @param value        Parsed value
 * @param user_data    User-provided context
 * @return TOML_SUCCESS to continue parsing, error code to stop
 */
typedef toml_result_t (*toml_callback_t)(const char *section, const char *key,
                                         const toml_value_t *value,
                                         void *user_data);

/* ============================================================================
 * CORE PARSER API
 * ============================================================================
 */

/**
 * @brief Initialize a TOML parser
 *
 * @param parser  Parser to initialize
 * @param input   Input string to parse (must remain valid during parsing)
 * @return TOML_SUCCESS or error code
 */
toml_result_t toml_parser_init(toml_parser_t *parser, const char *input);

/**
 * @brief Initialize a TOML parser with explicit length
 *
 * @param parser  Parser to initialize
 * @param input   Input string to parse (must remain valid during parsing)
 * @param len     Length of input string
 * @return TOML_SUCCESS or error code
 */
toml_result_t toml_parser_init_with_length(toml_parser_t *parser,
                                           const char *input, size_t len);

/**
 * @brief Reset parser to beginning of input
 *
 * @param parser  Parser to reset
 */
void toml_parser_reset(toml_parser_t *parser);

/**
 * @brief Parse input and call callback for each key-value pair
 *
 * Parses the entire input, calling the callback for each key-value pair.
 * Stops on first error or if callback returns an error.
 *
 * @param parser     Parser to use
 * @param callback   Callback function for each key-value pair
 * @param user_data  User data passed to callback
 * @return TOML_SUCCESS or error code
 */
toml_result_t toml_parser_parse(toml_parser_t *parser, toml_callback_t callback,
                                void *user_data);

/**
 * @brief Get error message from parser
 *
 * @param parser  Parser to query
 * @return Error message string (empty if no error)
 */
const char *toml_parser_error(const toml_parser_t *parser);

/**
 * @brief Get error line number
 *
 * @param parser  Parser to query
 * @return Line number where error occurred (1-based)
 */
size_t toml_parser_error_line(const toml_parser_t *parser);

/**
 * @brief Get error column number
 *
 * @param parser  Parser to query
 * @return Column number where error occurred (1-based)
 */
size_t toml_parser_error_column(const toml_parser_t *parser);

/**
 * @brief Clean up parser resources
 *
 * Currently a no-op as parser uses no dynamic allocation,
 * but provided for API completeness and future compatibility.
 *
 * @param parser  Parser to clean up
 */
void toml_parser_cleanup(toml_parser_t *parser);

/* ============================================================================
 * VALUE HELPERS
 * ============================================================================
 */

/**
 * @brief Create a string value
 *
 * @param value   Value structure to initialize
 * @param str     String content
 * @return TOML_SUCCESS or error code
 */
toml_result_t toml_value_set_string(toml_value_t *value, const char *str);

/**
 * @brief Create an integer value
 *
 * @param value    Value structure to initialize
 * @param integer  Integer content
 */
void toml_value_set_integer(toml_value_t *value, int64_t integer);

/**
 * @brief Create a boolean value
 *
 * @param value    Value structure to initialize
 * @param boolean  Boolean content
 */
void toml_value_set_boolean(toml_value_t *value, bool boolean);

/**
 * @brief Free resources used by a value
 *
 * Frees any dynamically allocated memory in the value (e.g., array items).
 *
 * @param value  Value to free
 */
void toml_value_free(toml_value_t *value);

/**
 * @brief Get a string from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output string buffer
 * @param out_len Size of output buffer
 * @return TOML_SUCCESS if found, TOML_ERROR_NOT_FOUND if not
 */
toml_result_t toml_value_table_get_string(const toml_value_t *value,
                                          const char *key, char *out,
                                          size_t out_len);

/**
 * @brief Get an integer from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output integer
 * @return TOML_SUCCESS if found, TOML_ERROR_NOT_FOUND if not
 */
toml_result_t toml_value_table_get_integer(const toml_value_t *value,
                                           const char *key, int64_t *out);

/**
 * @brief Get a boolean from a table value by key
 *
 * @param value   Table value to search
 * @param key     Key to find
 * @param out     Output boolean
 * @return TOML_SUCCESS if found, TOML_ERROR_NOT_FOUND if not
 */
toml_result_t toml_value_table_get_boolean(const toml_value_t *value,
                                           const char *key, bool *out);

/* ============================================================================
 * CONVENIENCE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Check if a value is of a specific type
 *
 * @param value  Value to check
 * @param type   Expected type
 * @return true if value is of the expected type
 */
static inline bool toml_value_is_type(const toml_value_t *value,
                                      toml_value_type_t type) {
    return value && value->type == type;
}

/**
 * @brief Get string from value (returns NULL if not a string)
 *
 * @param value  Value to get string from
 * @return Pointer to string data, or NULL if not a string
 */
static inline const char *toml_value_get_string(const toml_value_t *value) {
    if (value && value->type == TOML_VALUE_STRING) {
        return value->data.string;
    }
    return NULL;
}

/**
 * @brief Get integer from value (returns 0 if not an integer)
 *
 * @param value  Value to get integer from
 * @param out    Output integer
 * @return true if value was an integer
 */
static inline bool toml_value_get_integer(const toml_value_t *value,
                                          int64_t *out) {
    if (value && value->type == TOML_VALUE_INTEGER && out) {
        *out = value->data.integer;
        return true;
    }
    return false;
}

/**
 * @brief Get boolean from value (returns false if not a boolean)
 *
 * @param value  Value to get boolean from
 * @param out    Output boolean
 * @return true if value was a boolean
 */
static inline bool toml_value_get_boolean(const toml_value_t *value,
                                          bool *out) {
    if (value && value->type == TOML_VALUE_BOOLEAN && out) {
        *out = value->data.boolean;
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif /// TOML_PARSER_H
