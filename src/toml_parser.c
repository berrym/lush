/**
 * @file toml_parser.c
 * @brief Generic TOML-subset Parser Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Dependency-free TOML-subset parser for lush configuration files.
 * This is a core utility used by the configuration system, theme parser,
 * and other components that need to parse TOML files.
 */

#include "toml_parser.h"

#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Unicode String Comparison Helper
 * ============================================================================
 */

/**
 * @brief Unicode-aware string equality check for TOML keys
 *
 * Uses LLE's Unicode comparison with NFC normalization to ensure
 * equivalent Unicode sequences compare as equal.
 */
static inline bool toml_streq(const char *s1, const char *s2) {
    if (s1 == s2)
        return true;
    if (!s1 || !s2)
        return false;
    return lle_unicode_strings_equal(s1, s2, &LLE_UNICODE_COMPARE_DEFAULT);
}

/* ============================================================================
 * Internal Helper Macros
 * ============================================================================
 */

#define PARSER_EOF(p) ((p)->pos >= (p)->input_len)
#define PARSER_PEEK(p) (PARSER_EOF(p) ? '\0' : (p)->input[(p)->pos])
#define PARSER_ADVANCE(p)                                                      \
    do {                                                                       \
        if (!PARSER_EOF(p)) {                                                  \
            if ((p)->input[(p)->pos] == '\n') {                                \
                (p)->line++;                                                   \
                (p)->column = 1;                                               \
            } else {                                                           \
                (p)->column++;                                                 \
            }                                                                  \
            (p)->pos++;                                                        \
        }                                                                      \
    } while (0)

/* ============================================================================
 * Forward Declarations
 * ============================================================================
 */

static void parser_skip_whitespace(toml_parser_t *parser);
static void parser_skip_whitespace_and_newlines(toml_parser_t *parser);
static void parser_skip_comment(toml_parser_t *parser);
static void parser_skip_line(toml_parser_t *parser);
static toml_result_t parser_set_error(toml_parser_t *parser,
                                      const char *message);

static toml_result_t parser_parse_section(toml_parser_t *parser);
static toml_result_t parser_parse_key(toml_parser_t *parser, char *key,
                                      size_t key_size);
static toml_result_t parser_parse_value(toml_parser_t *parser,
                                        toml_value_t *value);
static toml_result_t parser_parse_string(toml_parser_t *parser, char *out,
                                         size_t out_size);
static toml_result_t parser_parse_integer(toml_parser_t *parser, int64_t *out);
static toml_result_t parser_parse_boolean(toml_parser_t *parser, bool *out);
static toml_result_t parser_parse_array(toml_parser_t *parser,
                                        toml_value_t *value);
static toml_result_t parser_parse_inline_table(toml_parser_t *parser,
                                               toml_value_t *value);

/* ============================================================================
 * Parser Core API
 * ============================================================================
 */

toml_result_t toml_parser_init(toml_parser_t *parser, const char *input) {
    if (!parser || !input) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    return toml_parser_init_with_length(parser, input, strlen(input));
}

toml_result_t toml_parser_init_with_length(toml_parser_t *parser,
                                           const char *input, size_t len) {
    if (!parser || !input) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    memset(parser, 0, sizeof(*parser));
    parser->input = input;
    parser->input_len = len;
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->current_section[0] = '\0';
    parser->error_msg[0] = '\0';

    return TOML_SUCCESS;
}

void toml_parser_reset(toml_parser_t *parser) {
    if (!parser) {
        return;
    }

    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->current_section[0] = '\0';
    parser->error_msg[0] = '\0';
    parser->error_line = 0;
    parser->error_column = 0;
    parser->keys_parsed = 0;
    parser->sections_parsed = 0;
}

toml_result_t toml_parser_parse(toml_parser_t *parser, toml_callback_t callback,
                                void *user_data) {
    if (!parser || !callback) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    while (!PARSER_EOF(parser)) {
        parser_skip_whitespace_and_newlines(parser);

        if (PARSER_EOF(parser)) {
            break;
        }

        char c = PARSER_PEEK(parser);

        /// Skip comments
        if (c == '#') {
            parser_skip_line(parser);
            continue;
        }

        /// Parse section header
        if (c == '[') {
            toml_result_t result = parser_parse_section(parser);
            if (result != TOML_SUCCESS) {
                return result;
            }
            parser->sections_parsed++;
            continue;
        }

        /// Parse key-value pair
        if (isalpha((unsigned char)c) || c == '_') {
            char key[TOML_PARSER_KEY_MAX];
            toml_result_t result = parser_parse_key(parser, key, sizeof(key));
            if (result != TOML_SUCCESS) {
                return result;
            }

            parser_skip_whitespace(parser);

            /// Expect '='
            if (PARSER_PEEK(parser) != '=') {
                return parser_set_error(parser, "Expected '=' after key");
            }
            PARSER_ADVANCE(parser); /// Skip '='

            parser_skip_whitespace(parser);

            /// Parse value
            toml_value_t value;
            memset(&value, 0, sizeof(value));
            result = parser_parse_value(parser, &value);
            if (result != TOML_SUCCESS) {
                toml_value_free(&value);
                return result;
            }

            /// Call callback
            result = callback(parser->current_section, key, &value, user_data);
            toml_value_free(&value);

            if (result != TOML_SUCCESS) {
                return result;
            }

            parser->keys_parsed++;
            continue;
        }

        /// Unknown character
        return parser_set_error(parser, "Unexpected character");
    }

    return TOML_SUCCESS;
}

const char *toml_parser_error(const toml_parser_t *parser) {
    if (!parser) {
        return "";
    }
    return parser->error_msg;
}

size_t toml_parser_error_line(const toml_parser_t *parser) {
    if (!parser) {
        return 0;
    }
    return parser->error_line;
}

size_t toml_parser_error_column(const toml_parser_t *parser) {
    if (!parser) {
        return 0;
    }
    return parser->error_column;
}

void toml_parser_cleanup(toml_parser_t *parser) {
    /// Currently no dynamic allocations in parser itself
    (void)parser;
}

/* ============================================================================
 * Internal Parsing Helpers
 * ============================================================================
 */

static void parser_skip_whitespace(toml_parser_t *parser) {
    while (!PARSER_EOF(parser)) {
        char c = PARSER_PEEK(parser);
        if (c == ' ' || c == '\t') {
            PARSER_ADVANCE(parser);
        } else {
            break;
        }
    }
}

static void parser_skip_whitespace_and_newlines(toml_parser_t *parser) {
    while (!PARSER_EOF(parser)) {
        char c = PARSER_PEEK(parser);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            PARSER_ADVANCE(parser);
        } else if (c == '#') {
            /// Skip comment lines
            parser_skip_line(parser);
        } else {
            break;
        }
    }
}

static void parser_skip_comment(toml_parser_t *parser) {
    if (PARSER_PEEK(parser) == '#') {
        parser_skip_line(parser);
    }
}

static void parser_skip_line(toml_parser_t *parser) {
    while (!PARSER_EOF(parser) && PARSER_PEEK(parser) != '\n') {
        PARSER_ADVANCE(parser);
    }
    if (PARSER_PEEK(parser) == '\n') {
        PARSER_ADVANCE(parser);
    }
}

static toml_result_t parser_set_error(toml_parser_t *parser,
                                      const char *message) {
    parser->error_line = parser->line;
    parser->error_column = parser->column;
    snprintf(parser->error_msg, sizeof(parser->error_msg),
             "Line %zu, column %zu: %s", parser->line, parser->column, message);
    return TOML_ERROR_INVALID_FORMAT;
}

static toml_result_t parser_parse_section(toml_parser_t *parser) {
    if (PARSER_PEEK(parser) != '[') {
        return parser_set_error(parser, "Expected '['");
    }
    PARSER_ADVANCE(parser); /// Skip '['

    /// Read section name
    char section[TOML_PARSER_KEY_MAX * TOML_PARSER_SECTION_DEPTH_MAX];
    size_t len = 0;

    while (!PARSER_EOF(parser) && PARSER_PEEK(parser) != ']') {
        char c = PARSER_PEEK(parser);

        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
            if (len >= sizeof(section) - 1) {
                return parser_set_error(parser, "Section name too long");
            }
            section[len++] = c;
            PARSER_ADVANCE(parser);
        } else if (c == ' ' || c == '\t') {
            /// Skip whitespace in section name
            PARSER_ADVANCE(parser);
        } else {
            return parser_set_error(parser,
                                    "Invalid character in section name");
        }
    }

    if (PARSER_PEEK(parser) != ']') {
        return parser_set_error(parser, "Expected ']'");
    }
    PARSER_ADVANCE(parser); /// Skip ']'

    section[len] = '\0';
    snprintf(parser->current_section, sizeof(parser->current_section), "%s",
             section);

    /// Skip rest of line
    parser_skip_whitespace(parser);
    if (PARSER_PEEK(parser) == '#') {
        parser_skip_comment(parser);
    } else if (PARSER_PEEK(parser) != '\n' && !PARSER_EOF(parser)) {
        return parser_set_error(parser, "Unexpected content after section");
    }

    return TOML_SUCCESS;
}

static toml_result_t parser_parse_key(toml_parser_t *parser, char *key,
                                      size_t key_size) {
    size_t len = 0;

    while (!PARSER_EOF(parser)) {
        char c = PARSER_PEEK(parser);

        /// Accept '.' as part of the key so dotted keys (finder.match) read
        /// back as a single key string. The config registry models keys as
        /// flat dotted paths and emits them in this form, so the callback
        /// receives "finder.match" and composes "<section>.finder.match".
        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
            if (len >= key_size - 1) {
                return parser_set_error(parser, "Key name too long");
            }
            key[len++] = c;
            PARSER_ADVANCE(parser);
        } else {
            break;
        }
    }

    if (len == 0) {
        return parser_set_error(parser, "Empty key name");
    }

    key[len] = '\0';
    return TOML_SUCCESS;
}

static toml_result_t parser_parse_value(toml_parser_t *parser,
                                        toml_value_t *value) {
    memset(value, 0, sizeof(*value));

    char c = PARSER_PEEK(parser);

    /// String
    if (c == '"') {
        value->type = TOML_VALUE_STRING;
        return parser_parse_string(parser, value->data.string,
                                   sizeof(value->data.string));
    }

    /// Array
    if (c == '[') {
        return parser_parse_array(parser, value);
    }

    /// Inline table
    if (c == '{') {
        return parser_parse_inline_table(parser, value);
    }

    /// Boolean: true or false
    if (c == 't' || c == 'f') {
        value->type = TOML_VALUE_BOOLEAN;
        return parser_parse_boolean(parser, &value->data.boolean);
    }

    /// Integer (including negative)
    if (isdigit((unsigned char)c) || c == '-' || c == '+') {
        value->type = TOML_VALUE_INTEGER;
        return parser_parse_integer(parser, &value->data.integer);
    }

    return parser_set_error(parser, "Invalid value");
}

static toml_result_t parser_parse_string(toml_parser_t *parser, char *out,
                                         size_t out_size) {
    if (PARSER_PEEK(parser) != '"') {
        return parser_set_error(parser, "Expected '\"'");
    }
    PARSER_ADVANCE(parser); /// Skip opening quote

    size_t len = 0;

    while (!PARSER_EOF(parser)) {
        char c = PARSER_PEEK(parser);

        if (c == '"') {
            PARSER_ADVANCE(parser); /// Skip closing quote
            out[len] = '\0';
            return TOML_SUCCESS;
        }

        if (c == '\n') {
            return parser_set_error(parser, "Unterminated string");
        }

        if (c == '\\') {
            /// Escape sequence
            PARSER_ADVANCE(parser);
            if (PARSER_EOF(parser)) {
                return parser_set_error(parser, "Unterminated escape sequence");
            }

            char escaped = PARSER_PEEK(parser);
            PARSER_ADVANCE(parser);

            switch (escaped) {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case '\\':
                c = '\\';
                break;
            case '"':
                c = '"';
                break;
            case 'u':
            case 'U': {
                /// Unicode escape: \uXXXX or \UXXXXXXXX
                int hex_digits = (escaped == 'u') ? 4 : 8;
                uint32_t codepoint = 0;

                for (int i = 0; i < hex_digits; i++) {
                    if (PARSER_EOF(parser)) {
                        return parser_set_error(
                            parser, "Incomplete Unicode escape sequence");
                    }
                    char h = PARSER_PEEK(parser);
                    PARSER_ADVANCE(parser);

                    int digit;
                    if (h >= '0' && h <= '9') {
                        digit = h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                        digit = 10 + (h - 'a');
                    } else if (h >= 'A' && h <= 'F') {
                        digit = 10 + (h - 'A');
                    } else {
                        return parser_set_error(
                            parser, "Invalid hex digit in Unicode escape");
                    }
                    codepoint = (codepoint << 4) | (uint32_t)digit;
                }

                /// Validate codepoint range
                if (codepoint > 0x10FFFF) {
                    return parser_set_error(parser,
                                            "Unicode codepoint out of range");
                }
                /// Reject surrogates (reserved for UTF-16)
                if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                    return parser_set_error(parser,
                                            "Surrogate codepoints not allowed");
                }

                /// Encode to UTF-8 using LLE's Unicode support
                char utf8_buf[4];
                int utf8_len = lle_utf8_encode_codepoint(codepoint, utf8_buf);
                if (utf8_len <= 0) {
                    return parser_set_error(
                        parser, "Failed to encode Unicode codepoint");
                }

                /// Add UTF-8 bytes to output
                if (len + (size_t)utf8_len >= out_size) {
                    return parser_set_error(parser, "String too long");
                }
                for (int i = 0; i < utf8_len; i++) {
                    out[len++] = utf8_buf[i];
                }
                continue; /// Skip the normal character append
            }
            default:
                return parser_set_error(parser, "Invalid escape sequence");
            }
        } else {
            PARSER_ADVANCE(parser);
        }

        if (len >= out_size - 1) {
            return parser_set_error(parser, "String too long");
        }
        out[len++] = c;
    }

    return parser_set_error(parser, "Unterminated string");
}

static toml_result_t parser_parse_integer(toml_parser_t *parser, int64_t *out) {
    char buffer[32];
    size_t len = 0;

    /// Sign
    char c = PARSER_PEEK(parser);
    if (c == '-' || c == '+') {
        buffer[len++] = c;
        PARSER_ADVANCE(parser);
    }

    /// Digits
    while (!PARSER_EOF(parser)) {
        c = PARSER_PEEK(parser);
        if (isdigit((unsigned char)c)) {
            if (len >= sizeof(buffer) - 1) {
                return parser_set_error(parser, "Integer too long");
            }
            buffer[len++] = c;
            PARSER_ADVANCE(parser);
        } else {
            break;
        }
    }

    if (len == 0 || (len == 1 && (buffer[0] == '-' || buffer[0] == '+'))) {
        return parser_set_error(parser, "Invalid integer");
    }

    buffer[len] = '\0';
    *out = strtoll(buffer, NULL, 10);
    return TOML_SUCCESS;
}

static toml_result_t parser_parse_boolean(toml_parser_t *parser, bool *out) {
    char word[6] = {0};
    size_t i = 0;

    while (!PARSER_EOF(parser) && i < 5) {
        char c = PARSER_PEEK(parser);
        if (isalpha((unsigned char)c)) {
            word[i++] = (char)tolower((unsigned char)c);
            PARSER_ADVANCE(parser);
        } else {
            break;
        }
    }
    word[i] = '\0';

    if (strcmp(word, "true") == 0) {
        *out = true;
        return TOML_SUCCESS;
    } else if (strcmp(word, "false") == 0) {
        *out = false;
        return TOML_SUCCESS;
    }

    return parser_set_error(parser,
                            "Invalid boolean (expected 'true' or 'false')");
}

static toml_result_t parser_parse_array(toml_parser_t *parser,
                                        toml_value_t *value) {
    if (PARSER_PEEK(parser) != '[') {
        return parser_set_error(parser, "Expected '['");
    }
    PARSER_ADVANCE(parser); /// Skip '['

    value->type = TOML_VALUE_ARRAY;
    value->data.array.items = NULL;
    value->data.array.count = 0;

    /// Allocate array storage
    toml_value_t *items = calloc(TOML_PARSER_ARRAY_MAX, sizeof(toml_value_t));
    if (!items) {
        return TOML_ERROR_OUT_OF_MEMORY;
    }

    parser_skip_whitespace_and_newlines(parser);

    while (!PARSER_EOF(parser) && PARSER_PEEK(parser) != ']') {
        if (value->data.array.count >= TOML_PARSER_ARRAY_MAX) {
            free(items);
            return parser_set_error(parser, "Array too large");
        }

        /// Parse array element
        toml_result_t result =
            parser_parse_value(parser, &items[value->data.array.count]);
        if (result != TOML_SUCCESS) {
            /// Free already parsed items
            for (size_t i = 0; i < value->data.array.count; i++) {
                toml_value_free(&items[i]);
            }
            free(items);
            return result;
        }
        value->data.array.count++;

        parser_skip_whitespace_and_newlines(parser);

        /// Check for comma or end
        if (PARSER_PEEK(parser) == ',') {
            PARSER_ADVANCE(parser);
            parser_skip_whitespace_and_newlines(parser);
        } else if (PARSER_PEEK(parser) != ']') {
            for (size_t i = 0; i < value->data.array.count; i++) {
                toml_value_free(&items[i]);
            }
            free(items);
            return parser_set_error(parser, "Expected ',' or ']' in array");
        }
    }

    if (PARSER_PEEK(parser) != ']') {
        for (size_t i = 0; i < value->data.array.count; i++) {
            toml_value_free(&items[i]);
        }
        free(items);
        return parser_set_error(parser, "Unterminated array");
    }
    PARSER_ADVANCE(parser); /// Skip ']'

    value->data.array.items = items;
    return TOML_SUCCESS;
}

static toml_result_t parser_parse_inline_table(toml_parser_t *parser,
                                               toml_value_t *value) {
    if (PARSER_PEEK(parser) != '{') {
        return parser_set_error(parser, "Expected '{'");
    }
    PARSER_ADVANCE(parser); /// Skip '{'

    value->type = TOML_VALUE_TABLE;
    value->data.table.count = 0;

    parser_skip_whitespace(parser);

    while (!PARSER_EOF(parser) && PARSER_PEEK(parser) != '}') {
        if (value->data.table.count >= TOML_PARSER_TABLE_ENTRIES_MAX) {
            return parser_set_error(parser, "Inline table too large");
        }

        /// Parse key
        toml_table_entry_t *entry =
            &value->data.table.entries[value->data.table.count];
        toml_result_t result =
            parser_parse_key(parser, entry->key, sizeof(entry->key));
        if (result != TOML_SUCCESS) {
            return result;
        }

        parser_skip_whitespace(parser);

        /// Expect '='
        if (PARSER_PEEK(parser) != '=') {
            return parser_set_error(parser, "Expected '=' in inline table");
        }
        PARSER_ADVANCE(parser);

        parser_skip_whitespace(parser);

        /// Parse value
        entry->value = calloc(1, sizeof(toml_value_t));
        if (!entry->value) {
            return TOML_ERROR_OUT_OF_MEMORY;
        }

        result = parser_parse_value(parser, entry->value);
        if (result != TOML_SUCCESS) {
            free(entry->value);
            entry->value = NULL;
            return result;
        }

        value->data.table.count++;
        parser_skip_whitespace(parser);

        /// Check for comma or end
        if (PARSER_PEEK(parser) == ',') {
            PARSER_ADVANCE(parser);
            parser_skip_whitespace(parser);
        } else if (PARSER_PEEK(parser) != '}') {
            return parser_set_error(parser,
                                    "Expected ',' or '}' in inline table");
        }
    }

    if (PARSER_PEEK(parser) != '}') {
        return parser_set_error(parser, "Unterminated inline table");
    }
    PARSER_ADVANCE(parser); /// Skip '}'

    return TOML_SUCCESS;
}

/* ============================================================================
 * Value Helpers
 * ============================================================================
 */

toml_result_t toml_value_set_string(toml_value_t *value, const char *str) {
    if (!value || !str) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    value->type = TOML_VALUE_STRING;
    snprintf(value->data.string, sizeof(value->data.string), "%s", str);
    return TOML_SUCCESS;
}

void toml_value_set_integer(toml_value_t *value, int64_t integer) {
    if (!value) {
        return;
    }

    value->type = TOML_VALUE_INTEGER;
    value->data.integer = integer;
}

void toml_value_set_boolean(toml_value_t *value, bool boolean) {
    if (!value) {
        return;
    }

    value->type = TOML_VALUE_BOOLEAN;
    value->data.boolean = boolean;
}

void toml_value_free(toml_value_t *value) {
    if (!value) {
        return;
    }

    if (value->type == TOML_VALUE_ARRAY && value->data.array.items) {
        for (size_t i = 0; i < value->data.array.count; i++) {
            toml_value_free(&value->data.array.items[i]);
        }
        free(value->data.array.items);
        value->data.array.items = NULL;
        value->data.array.count = 0;
    }

    if (value->type == TOML_VALUE_TABLE) {
        for (size_t i = 0; i < value->data.table.count; i++) {
            if (value->data.table.entries[i].value) {
                toml_value_free(value->data.table.entries[i].value);
                free(value->data.table.entries[i].value);
                value->data.table.entries[i].value = NULL;
            }
        }
        value->data.table.count = 0;
    }
}

toml_result_t toml_value_table_get_string(const toml_value_t *value,
                                          const char *key, char *out,
                                          size_t out_len) {
    if (!value || !key || !out || out_len == 0) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    if (value->type != TOML_VALUE_TABLE) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        /// Use Unicode-aware comparison for key lookup
        if (toml_streq(value->data.table.entries[i].key, key)) {
            const toml_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == TOML_VALUE_STRING) {
                snprintf(out, out_len, "%s", v->data.string);
                return TOML_SUCCESS;
            }
            return TOML_ERROR_INVALID_PARAMETER;
        }
    }

    return TOML_ERROR_NOT_FOUND;
}

toml_result_t toml_value_table_get_integer(const toml_value_t *value,
                                           const char *key, int64_t *out) {
    if (!value || !key || !out) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    if (value->type != TOML_VALUE_TABLE) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        /// Use Unicode-aware comparison for key lookup
        if (toml_streq(value->data.table.entries[i].key, key)) {
            const toml_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == TOML_VALUE_INTEGER) {
                *out = v->data.integer;
                return TOML_SUCCESS;
            }
            return TOML_ERROR_INVALID_PARAMETER;
        }
    }

    return TOML_ERROR_NOT_FOUND;
}

toml_result_t toml_value_table_get_boolean(const toml_value_t *value,
                                           const char *key, bool *out) {
    if (!value || !key || !out) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    if (value->type != TOML_VALUE_TABLE) {
        return TOML_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        /// Use Unicode-aware comparison for key lookup
        if (toml_streq(value->data.table.entries[i].key, key)) {
            const toml_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == TOML_VALUE_BOOLEAN) {
                *out = v->data.boolean;
                return TOML_SUCCESS;
            }
            return TOML_ERROR_INVALID_PARAMETER;
        }
    }

    return TOML_ERROR_NOT_FOUND;
}
