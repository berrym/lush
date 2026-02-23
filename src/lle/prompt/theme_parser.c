/**
 * @file theme_parser.c
 * @brief LLE Theme File Parser - Theme-specific TOML Parser Implementation
 *
 * Specification: Issue #21 - Theme File Loading System
 * Version: 2.0.0
 *
 * This module provides theme-specific parsing on top of the generic
 * TOML parser (toml_parser.c). It handles:
 * - Conversion from generic toml_value_t to theme-specific lle_theme_value_t
 * - Color parsing (hex, RGB, ANSI names, 256-color palette)
 * - Theme structure population from parsed values
 * - Theme validation
 */

#include "lle/prompt/theme_parser.h"
#include "toml_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Forward declaration for portability (see ht_fnv1a.c) */
int strcasecmp(const char *s1, const char *s2);

/* ============================================================================
 * Forward Declarations
 * ============================================================================
 */

/* Theme construction callback */
static lle_result_t theme_builder_callback(const char *section, const char *key,
                                           const lle_theme_value_t *value,
                                           void *user_data);

/* Value conversion from generic TOML to theme-specific */
static void convert_toml_value_to_theme_value(const toml_value_t *src,
                                              lle_theme_value_t *dst);

/* ============================================================================
 * Adapter Context for Generic Parser
 * ============================================================================
 */

/**
 * @brief Adapter context for bridging generic TOML parser to theme parser
 */
typedef struct {
    lle_theme_parser_callback_t user_callback;
    void *user_data;
    lle_theme_parser_t *theme_parser; /* For syncing state */
} theme_parser_adapter_t;

/**
 * @brief Adapter callback that converts toml_value_t to lle_theme_value_t
 */
static toml_result_t theme_parser_adapter_callback(const char *section,
                                                   const char *key,
                                                   const toml_value_t *value,
                                                   void *user_data) {
    theme_parser_adapter_t *adapter = (theme_parser_adapter_t *)user_data;

    /* Convert toml_value_t to lle_theme_value_t */
    lle_theme_value_t theme_value;
    memset(&theme_value, 0, sizeof(theme_value));
    convert_toml_value_to_theme_value(value, &theme_value);

    /* Call user's callback with converted value */
    lle_result_t result =
        adapter->user_callback(section, key, &theme_value, adapter->user_data);

    /* Free any allocated resources in converted value */
    lle_theme_value_free(&theme_value);

    /* Convert result code */
    if (result != LLE_SUCCESS) {
        return TOML_ERROR_CALLBACK_ABORT;
    }
    return TOML_SUCCESS;
}

/* ============================================================================
 * Value Conversion
 * ============================================================================
 */

/**
 * @brief Recursively convert toml_value_t to lle_theme_value_t
 */
static void convert_toml_value_to_theme_value(const toml_value_t *src,
                                              lle_theme_value_t *dst) {
    if (!src || !dst) {
        return;
    }

    switch (src->type) {
    case TOML_VALUE_STRING:
        dst->type = LLE_THEME_VALUE_STRING;
        snprintf(dst->data.string, sizeof(dst->data.string), "%s",
                 src->data.string);
        break;

    case TOML_VALUE_INTEGER:
        dst->type = LLE_THEME_VALUE_INTEGER;
        dst->data.integer = src->data.integer;
        break;

    case TOML_VALUE_BOOLEAN:
        dst->type = LLE_THEME_VALUE_BOOLEAN;
        dst->data.boolean = src->data.boolean;
        break;

    case TOML_VALUE_ARRAY:
        dst->type = LLE_THEME_VALUE_ARRAY;
        dst->data.array.count = src->data.array.count;
        if (src->data.array.count > 0) {
            dst->data.array.items =
                calloc(src->data.array.count, sizeof(lle_theme_value_t));
            if (dst->data.array.items) {
                for (size_t i = 0; i < src->data.array.count; i++) {
                    convert_toml_value_to_theme_value(&src->data.array.items[i],
                                                      &dst->data.array.items[i]);
                }
            }
        } else {
            dst->data.array.items = NULL;
        }
        break;

    case TOML_VALUE_TABLE:
        dst->type = LLE_THEME_VALUE_TABLE;
        dst->data.table.count = src->data.table.count;
        for (size_t i = 0; i < src->data.table.count; i++) {
            snprintf(dst->data.table.entries[i].key,
                     sizeof(dst->data.table.entries[i].key), "%s",
                     src->data.table.entries[i].key);

            dst->data.table.entries[i].value =
                calloc(1, sizeof(lle_theme_value_t));
            if (dst->data.table.entries[i].value &&
                src->data.table.entries[i].value) {
                convert_toml_value_to_theme_value(
                    src->data.table.entries[i].value,
                    dst->data.table.entries[i].value);
            }
        }
        break;
    }
}

/* ============================================================================
 * Parser Core API
 * ============================================================================
 */

/**
 * @brief Initialize a theme parser
 *
 * Sets up a parser instance with the given input string. The parser
 * will be ready to parse key-value pairs from the TOML-subset format.
 *
 * @param parser Pointer to parser structure to initialize
 * @param input  TOML-subset input string to parse (must remain valid during parsing)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parser or input is NULL
 */
lle_result_t lle_theme_parser_init(lle_theme_parser_t *parser,
                                   const char *input) {
    if (!parser || !input) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(parser, 0, sizeof(*parser));
    parser->input = input;
    parser->input_len = strlen(input);
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->current_section[0] = '\0';
    parser->error_msg[0] = '\0';

    return LLE_SUCCESS;
}

/**
 * @brief Reset parser to beginning of input
 *
 * Resets the parser state to allow re-parsing the same input from the
 * beginning. Clears position, line/column counters, current section,
 * and any error state.
 *
 * @param parser Pointer to parser to reset (ignored if NULL)
 */
void lle_theme_parser_reset(lle_theme_parser_t *parser) {
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

/**
 * @brief Parse input and call callback for each key-value pair
 *
 * Parses the TOML-subset input, calling the provided callback function
 * for each key-value pair encountered. Uses the generic TOML parser
 * internally and converts values to theme-specific types.
 *
 * @param parser    Pointer to initialized parser
 * @param callback  Function to call for each parsed key-value pair
 * @param user_data User context passed to callback function
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parser or callback is NULL,
 *         or LLE_ERROR_INVALID_FORMAT on parse error
 */
lle_result_t lle_theme_parser_parse(lle_theme_parser_t *parser,
                                    lle_theme_parser_callback_t callback,
                                    void *user_data) {
    if (!parser || !callback) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Initialize generic TOML parser */
    toml_parser_t toml_parser;
    toml_result_t result =
        toml_parser_init_with_length(&toml_parser, parser->input, parser->input_len);
    if (result != TOML_SUCCESS) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Failed to initialize TOML parser");
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Set up adapter context */
    theme_parser_adapter_t adapter = {
        .user_callback = callback,
        .user_data = user_data,
        .theme_parser = parser
    };

    /* Parse using generic parser with adapter */
    result = toml_parser_parse(&toml_parser, theme_parser_adapter_callback, &adapter);

    /* Sync state from generic parser to theme parser */
    parser->pos = toml_parser.pos;
    parser->line = toml_parser.line;
    parser->column = toml_parser.column;
    parser->keys_parsed = toml_parser.keys_parsed;
    parser->sections_parsed = toml_parser.sections_parsed;
    snprintf(parser->current_section, sizeof(parser->current_section), "%s",
             toml_parser.current_section);

    /* Handle errors */
    if (result != TOML_SUCCESS) {
        parser->error_line = toml_parser.error_line;
        parser->error_column = toml_parser.error_column;
        snprintf(parser->error_msg, sizeof(parser->error_msg), "%s",
                 toml_parser.error_msg);

        /* Map TOML result to LLE result */
        switch (result) {
        case TOML_ERROR_INVALID_PARAMETER:
            return LLE_ERROR_INVALID_PARAMETER;
        case TOML_ERROR_OUT_OF_MEMORY:
            return LLE_ERROR_OUT_OF_MEMORY;
        case TOML_ERROR_CALLBACK_ABORT:
            /* Callback aborted - preserve error from callback */
            return LLE_ERROR_INVALID_FORMAT;
        default:
            return LLE_ERROR_INVALID_FORMAT;
        }
    }

    toml_parser_cleanup(&toml_parser);
    return LLE_SUCCESS;
}

/**
 * @brief Get error message from parser
 *
 * Returns the human-readable error message from the last parsing error.
 * The message includes line and column information for locating the error.
 *
 * @param parser Pointer to parser (may be NULL)
 * @return Error message string, or empty string if parser is NULL or no error
 */
const char *lle_theme_parser_error(const lle_theme_parser_t *parser) {
    if (!parser) {
        return "";
    }
    return parser->error_msg;
}

/**
 * @brief Get error line number
 *
 * Returns the line number where the last parsing error occurred.
 *
 * @param parser Pointer to parser (may be NULL)
 * @return Line number of error (1-based), or 0 if parser is NULL or no error
 */
size_t lle_theme_parser_error_line(const lle_theme_parser_t *parser) {
    if (!parser) {
        return 0;
    }
    return parser->error_line;
}

/**
 * @brief Get error column number
 *
 * Returns the column number where the last parsing error occurred.
 *
 * @param parser Pointer to parser (may be NULL)
 * @return Column number of error (1-based), or 0 if parser is NULL or no error
 */
size_t lle_theme_parser_error_column(const lle_theme_parser_t *parser) {
    if (!parser) {
        return 0;
    }
    return parser->error_column;
}

/* ============================================================================
 * Value Helpers
 * ============================================================================
 */

/**
 * @brief Create a string value
 *
 * Sets a theme value to hold a string. The string is copied into the
 * value's internal storage.
 *
 * @param value Pointer to value structure to set
 * @param str   String content to store
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if value or str is NULL
 */
lle_result_t lle_theme_value_set_string(lle_theme_value_t *value,
                                        const char *str) {
    if (!value || !str) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    value->type = LLE_THEME_VALUE_STRING;
    snprintf(value->data.string, sizeof(value->data.string), "%s", str);
    return LLE_SUCCESS;
}

/**
 * @brief Create an integer value
 *
 * Sets a theme value to hold an integer.
 *
 * @param value   Pointer to value structure to set (ignored if NULL)
 * @param integer Integer value to store
 */
void lle_theme_value_set_integer(lle_theme_value_t *value, int64_t integer) {
    if (!value) {
        return;
    }

    value->type = LLE_THEME_VALUE_INTEGER;
    value->data.integer = integer;
}

/**
 * @brief Create a boolean value
 *
 * Sets a theme value to hold a boolean.
 *
 * @param value   Pointer to value structure to set (ignored if NULL)
 * @param boolean Boolean value to store
 */
void lle_theme_value_set_boolean(lle_theme_value_t *value, bool boolean) {
    if (!value) {
        return;
    }

    value->type = LLE_THEME_VALUE_BOOLEAN;
    value->data.boolean = boolean;
}

/**
 * @brief Free resources used by a value
 *
 * Recursively frees any dynamically allocated resources within the value,
 * including array items and inline table entries.
 *
 * @param value Pointer to value to free (ignored if NULL)
 */
void lle_theme_value_free(lle_theme_value_t *value) {
    if (!value) {
        return;
    }

    if (value->type == LLE_THEME_VALUE_ARRAY && value->data.array.items) {
        for (size_t i = 0; i < value->data.array.count; i++) {
            lle_theme_value_free(&value->data.array.items[i]);
        }
        free(value->data.array.items);
        value->data.array.items = NULL;
        value->data.array.count = 0;
    }

    if (value->type == LLE_THEME_VALUE_TABLE) {
        for (size_t i = 0; i < value->data.table.count; i++) {
            if (value->data.table.entries[i].value) {
                lle_theme_value_free(value->data.table.entries[i].value);
                free(value->data.table.entries[i].value);
                value->data.table.entries[i].value = NULL;
            }
        }
        value->data.table.count = 0;
    }
}

/**
 * @brief Get a string from a table value by key
 *
 * Searches an inline table value for an entry with the given key
 * and retrieves its string value.
 *
 * @param value   Pointer to table value to search
 * @param key     Key name to look up
 * @param out     Output buffer for string value
 * @param out_len Size of output buffer
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters invalid,
 *         LLE_ERROR_NOT_FOUND if key not found
 */
lle_result_t lle_theme_value_table_get_string(const lle_theme_value_t *value,
                                              const char *key, char *out,
                                              size_t out_len) {
    if (!value || !key || !out || out_len == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (value->type != LLE_THEME_VALUE_TABLE) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        if (strcmp(value->data.table.entries[i].key, key) == 0) {
            const lle_theme_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == LLE_THEME_VALUE_STRING) {
                snprintf(out, out_len, "%s", v->data.string);
                return LLE_SUCCESS;
            }
            return LLE_ERROR_INVALID_PARAMETER;
        }
    }

    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Get an integer from a table value by key
 *
 * Searches an inline table value for an entry with the given key
 * and retrieves its integer value.
 *
 * @param value Pointer to table value to search
 * @param key   Key name to look up
 * @param out   Output pointer for integer value
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters invalid,
 *         LLE_ERROR_NOT_FOUND if key not found
 */
lle_result_t lle_theme_value_table_get_integer(const lle_theme_value_t *value,
                                               const char *key, int64_t *out) {
    if (!value || !key || !out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (value->type != LLE_THEME_VALUE_TABLE) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        if (strcmp(value->data.table.entries[i].key, key) == 0) {
            const lle_theme_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == LLE_THEME_VALUE_INTEGER) {
                *out = v->data.integer;
                return LLE_SUCCESS;
            }
            return LLE_ERROR_INVALID_PARAMETER;
        }
    }

    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Get a boolean from a table value by key
 *
 * Searches an inline table value for an entry with the given key
 * and retrieves its boolean value.
 *
 * @param value Pointer to table value to search
 * @param key   Key name to look up
 * @param out   Output pointer for boolean value
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters invalid,
 *         LLE_ERROR_NOT_FOUND if key not found
 */
lle_result_t lle_theme_value_table_get_boolean(const lle_theme_value_t *value,
                                               const char *key, bool *out) {
    if (!value || !key || !out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (value->type != LLE_THEME_VALUE_TABLE) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < value->data.table.count; i++) {
        if (strcmp(value->data.table.entries[i].key, key) == 0) {
            const lle_theme_value_t *v = value->data.table.entries[i].value;
            if (v && v->type == LLE_THEME_VALUE_BOOLEAN) {
                *out = v->data.boolean;
                return LLE_SUCCESS;
            }
            return LLE_ERROR_INVALID_PARAMETER;
        }
    }

    return LLE_ERROR_NOT_FOUND;
}

/* ============================================================================
 * Color Parsing
 * ============================================================================
 */

/* Basic ANSI color name lookup table */
static const struct {
    const char *name;
    lle_basic_color_t color;
} color_names[] = {{"black", LLE_COLOR_BLACK},
                   {"red", LLE_COLOR_RED},
                   {"green", LLE_COLOR_GREEN},
                   {"yellow", LLE_COLOR_YELLOW},
                   {"blue", LLE_COLOR_BLUE},
                   {"magenta", LLE_COLOR_MAGENTA},
                   {"cyan", LLE_COLOR_CYAN},
                   {"white", LLE_COLOR_WHITE},
                   {NULL, 0}};

/**
 * @brief Parse hex digit to integer
 *
 * Converts a hexadecimal character to its numeric value.
 *
 * @param c Character to convert ('0'-'9', 'a'-'f', or 'A'-'F')
 * @return Integer value 0-15 on success, -1 if not a valid hex digit
 */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

/**
 * @brief Parse a color specification string into lle_color_t
 *
 * Parses various color formats including:
 * - Hex colors: #RGB or #RRGGBB
 * - RGB function: rgb(r, g, b)
 * - Named colors: black, red, green, yellow, blue, magenta, cyan, white
 * - 256-color palette index: 0-255
 *
 * @param spec  Color specification string
 * @param color Output structure for parsed color
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if spec or color is NULL,
 *         LLE_ERROR_INVALID_FORMAT if color format is invalid
 */
lle_result_t lle_parse_color_spec(const char *spec, lle_color_t *color) {
    if (!spec || !color) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(color, 0, sizeof(*color));

    /* Skip leading whitespace */
    while (*spec == ' ' || *spec == '\t') {
        spec++;
    }

    size_t len = strlen(spec);
    if (len == 0) {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Check for hex color: #RGB or #RRGGBB */
    if (spec[0] == '#') {
        if (len == 4) {
            /* #RGB -> expand to #RRGGBB */
            int r = hex_digit(spec[1]);
            int g = hex_digit(spec[2]);
            int b = hex_digit(spec[3]);
            if (r < 0 || g < 0 || b < 0) {
                return LLE_ERROR_INVALID_FORMAT;
            }
            color->mode = LLE_COLOR_MODE_TRUE;
            color->value.rgb.r = (uint8_t)(r * 17); /* 0xF -> 0xFF */
            color->value.rgb.g = (uint8_t)(g * 17);
            color->value.rgb.b = (uint8_t)(b * 17);
            return LLE_SUCCESS;
        } else if (len == 7) {
            /* #RRGGBB */
            int r1 = hex_digit(spec[1]);
            int r2 = hex_digit(spec[2]);
            int g1 = hex_digit(spec[3]);
            int g2 = hex_digit(spec[4]);
            int b1 = hex_digit(spec[5]);
            int b2 = hex_digit(spec[6]);
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
                return LLE_ERROR_INVALID_FORMAT;
            }
            color->mode = LLE_COLOR_MODE_TRUE;
            color->value.rgb.r = (uint8_t)((r1 << 4) | r2);
            color->value.rgb.g = (uint8_t)((g1 << 4) | g2);
            color->value.rgb.b = (uint8_t)((b1 << 4) | b2);
            return LLE_SUCCESS;
        }
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Check for rgb(r, g, b) function */
    if (strncmp(spec, "rgb(", 4) == 0) {
        int r, g, b;
        if (sscanf(spec, "rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            sscanf(spec, "rgb( %d , %d , %d )", &r, &g, &b) == 3) {
            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
                return LLE_ERROR_INVALID_FORMAT;
            }
            color->mode = LLE_COLOR_MODE_TRUE;
            color->value.rgb.r = (uint8_t)r;
            color->value.rgb.g = (uint8_t)g;
            color->value.rgb.b = (uint8_t)b;
            return LLE_SUCCESS;
        }
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Check for basic color name */
    for (int i = 0; color_names[i].name != NULL; i++) {
        if (strcasecmp(spec, color_names[i].name) == 0) {
            color->mode = LLE_COLOR_MODE_BASIC;
            color->value.basic = (uint8_t)color_names[i].color;
            return LLE_SUCCESS;
        }
    }

    /* Check for numeric 256-color index */
    char *endptr;
    long index = strtol(spec, &endptr, 10);
    if (*endptr == '\0' && index >= 0 && index <= 255) {
        color->mode = LLE_COLOR_MODE_256;
        color->value.palette = (uint8_t)index;
        return LLE_SUCCESS;
    }

    return LLE_ERROR_INVALID_FORMAT;
}

/**
 * @brief Parse an inline table into a color with attributes
 *
 * Parses a TOML inline table containing color specification and optional
 * text attributes (bold, italic, underline, dim).
 *
 * @param value Pointer to table value containing color definition
 * @param color Output structure for parsed color with attributes
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if value or color is NULL
 *         or value is not a table, LLE_ERROR_INVALID_FORMAT if color spec is invalid
 */
lle_result_t lle_parse_color_table(const lle_theme_value_t *value,
                                   lle_color_t *color) {
    if (!value || !color) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (value->type != LLE_THEME_VALUE_TABLE) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(color, 0, sizeof(*color));

    /* Look for foreground color specification */
    char fg_spec[64] = {0};
    int64_t fg_int = -1;

    if (lle_theme_value_table_get_string(value, "fg", fg_spec,
                                         sizeof(fg_spec)) == LLE_SUCCESS) {
        lle_result_t result = lle_parse_color_spec(fg_spec, color);
        if (result != LLE_SUCCESS) {
            return result;
        }
    } else if (lle_theme_value_table_get_integer(value, "fg", &fg_int) ==
               LLE_SUCCESS) {
        if (fg_int >= 0 && fg_int <= 255) {
            color->mode = LLE_COLOR_MODE_256;
            color->value.palette = (uint8_t)fg_int;
        } else {
            return LLE_ERROR_INVALID_FORMAT;
        }
    }

    /* Parse attributes */
    bool attr;
    if (lle_theme_value_table_get_boolean(value, "bold", &attr) ==
        LLE_SUCCESS) {
        color->bold = attr;
    }
    if (lle_theme_value_table_get_boolean(value, "italic", &attr) ==
        LLE_SUCCESS) {
        color->italic = attr;
    }
    if (lle_theme_value_table_get_boolean(value, "underline", &attr) ==
        LLE_SUCCESS) {
        color->underline = attr;
    }
    if (lle_theme_value_table_get_boolean(value, "dim", &attr) == LLE_SUCCESS) {
        color->dim = attr;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * Theme Construction
 * ============================================================================
 */

/**
 * @brief Theme builder context for callback
 */
typedef struct {
    lle_theme_t *theme;
    char error_msg[256];
    lle_result_t error;
} theme_builder_ctx_t;

/**
 * @brief Map category string to enum
 *
 * Converts a theme category name string to the corresponding enum value.
 *
 * @param str Category name (case-insensitive)
 * @return Corresponding lle_theme_category_t value, or LLE_THEME_CATEGORY_CUSTOM if unknown
 */
static lle_theme_category_t parse_category(const char *str) {
    if (strcasecmp(str, "minimal") == 0)
        return LLE_THEME_CATEGORY_MINIMAL;
    if (strcasecmp(str, "classic") == 0)
        return LLE_THEME_CATEGORY_CLASSIC;
    if (strcasecmp(str, "modern") == 0)
        return LLE_THEME_CATEGORY_MODERN;
    if (strcasecmp(str, "powerline") == 0)
        return LLE_THEME_CATEGORY_POWERLINE;
    if (strcasecmp(str, "professional") == 0)
        return LLE_THEME_CATEGORY_PROFESSIONAL;
    if (strcasecmp(str, "creative") == 0)
        return LLE_THEME_CATEGORY_CREATIVE;
    return LLE_THEME_CATEGORY_CUSTOM;
}

/**
 * @brief Apply a color value to a color field
 *
 * Parses a theme value (string, integer, or table) into a color structure.
 * Handles string color specs, integer palette indices, and table-based
 * color definitions with attributes.
 *
 * @param value Pointer to theme value containing color definition
 * @param color Output pointer for parsed color
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_FORMAT on parse error
 */
static lle_result_t apply_color(const lle_theme_value_t *value,
                                lle_color_t *color) {
    if (value->type == LLE_THEME_VALUE_STRING) {
        return lle_parse_color_spec(value->data.string, color);
    } else if (value->type == LLE_THEME_VALUE_INTEGER) {
        if (value->data.integer >= 0 && value->data.integer <= 255) {
            color->mode = LLE_COLOR_MODE_256;
            color->value.palette = (uint8_t)value->data.integer;
            return LLE_SUCCESS;
        }
        return LLE_ERROR_INVALID_FORMAT;
    } else if (value->type == LLE_THEME_VALUE_TABLE) {
        return lle_parse_color_table(value, color);
    }
    return LLE_ERROR_INVALID_FORMAT;
}

/**
 * @brief Convert lle_color_t to uint32_t RGB format (0x00RRGGBB)
 *
 * Used for syntax highlighting colors which use RGB uint32_t format.
 * Converts from various color modes (true color, 256-color, basic ANSI)
 * to a single 24-bit RGB representation.
 *
 * @param color Pointer to color structure to convert (may be NULL)
 * @return 24-bit RGB value (0x00RRGGBB), or 0 if color is NULL or mode is none
 */
static uint32_t color_to_rgb(const lle_color_t *color) {
    if (!color) {
        return 0;
    }

    switch (color->mode) {
    case LLE_COLOR_MODE_TRUE:
        return ((uint32_t)color->value.rgb.r << 16) |
               ((uint32_t)color->value.rgb.g << 8) |
               (uint32_t)color->value.rgb.b;

    case LLE_COLOR_MODE_256: {
        /* Convert 256-color palette to approximate RGB */
        uint8_t idx = color->value.palette;
        if (idx < 16) {
            /* Basic 16 colors */
            static const uint32_t basic[16] = {
                0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                0x008080, 0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
                0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
            return basic[idx];
        } else if (idx < 232) {
            /* 216-color cube (6x6x6) */
            int i = idx - 16;
            int r = (i / 36) * 51;
            int g = ((i / 6) % 6) * 51;
            int b = (i % 6) * 51;
            return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        } else {
            /* Grayscale */
            int gray = 8 + (idx - 232) * 10;
            return ((uint32_t)gray << 16) | ((uint32_t)gray << 8) |
                   (uint32_t)gray;
        }
    }

    case LLE_COLOR_MODE_BASIC: {
        /* Basic 8 ANSI colors */
        static const uint32_t basic8[8] = {0x000000, 0xAA0000, 0x00AA00,
                                           0xAAAA00, 0x0000AA, 0xAA00AA,
                                           0x00AAAA, 0xAAAAAA};
        if (color->value.basic < 8) {
            return basic8[color->value.basic];
        }
        return 0xFFFFFF;
    }

    case LLE_COLOR_MODE_NONE:
    default:
        return 0;
    }
}

/**
 * @brief Apply a syntax color value (parse and convert to RGB uint32_t)
 *
 * Parses a theme value as a color and converts it to 24-bit RGB format
 * suitable for syntax highlighting.
 *
 * @param value   Pointer to theme value containing color definition
 * @param rgb_out Output pointer for 24-bit RGB value
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t apply_syntax_color(const lle_theme_value_t *value,
                                       uint32_t *rgb_out) {
    lle_color_t color;
    lle_result_t result = apply_color(value, &color);
    if (result != LLE_SUCCESS) {
        return result;
    }
    *rgb_out = color_to_rgb(&color);
    return LLE_SUCCESS;
}

/**
 * @brief Theme builder callback - populates theme from parsed values
 *
 * Parser callback function that maps parsed TOML sections and keys to
 * theme structure fields. Handles metadata, capabilities, layout, segments,
 * colors, symbols, and syntax highlighting sections.
 *
 * @param section   Current section name (e.g., "theme", "colors", "symbols")
 * @param key       Key name within the section
 * @param value     Parsed value for the key
 * @param user_data Pointer to theme_builder_ctx_t context
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t theme_builder_callback(const char *section, const char *key,
                                           const lle_theme_value_t *value,
                                           void *user_data) {
    theme_builder_ctx_t *ctx = (theme_builder_ctx_t *)user_data;
    lle_theme_t *theme = ctx->theme;

    /* [theme] section - metadata */
    if (strcmp(section, "theme") == 0) {
        if (strcmp(key, "name") == 0 && value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->name, sizeof(theme->name), "%.63s",
                     value->data.string);
        } else if (strcmp(key, "description") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->description, sizeof(theme->description), "%.255s",
                     value->data.string);
        } else if (strcmp(key, "author") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->author, sizeof(theme->author), "%.63s",
                     value->data.string);
        } else if (strcmp(key, "version") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->version, sizeof(theme->version), "%.15s",
                     value->data.string);
        } else if (strcmp(key, "category") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            theme->category = parse_category(value->data.string);
        } else if (strcmp(key, "inherits_from") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->inherits_from, sizeof(theme->inherits_from),
                     "%.63s", value->data.string);
        }
        return LLE_SUCCESS;
    }

    /* [capabilities] section */
    if (strcmp(section, "capabilities") == 0) {
        if (value->type == LLE_THEME_VALUE_BOOLEAN && value->data.boolean) {
            if (strcmp(key, "unicode") == 0) {
                theme->capabilities |= LLE_THEME_CAP_UNICODE;
            } else if (strcmp(key, "multiline") == 0) {
                theme->capabilities |= LLE_THEME_CAP_MULTILINE;
            } else if (strcmp(key, "transient") == 0) {
                theme->capabilities |= LLE_THEME_CAP_TRANSIENT;
            } else if (strcmp(key, "async_segments") == 0) {
                theme->capabilities |= LLE_THEME_CAP_ASYNC_SEGMENTS;
            } else if (strcmp(key, "powerline") == 0) {
                theme->capabilities |= LLE_THEME_CAP_POWERLINE;
            } else if (strcmp(key, "nerd_font") == 0) {
                theme->capabilities |= LLE_THEME_CAP_NERD_FONT;
            } else if (strcmp(key, "right_prompt") == 0) {
                theme->capabilities |= LLE_THEME_CAP_RIGHT_PROMPT;
            } else if (strcmp(key, "true_color") == 0) {
                theme->capabilities |= LLE_THEME_CAP_TRUE_COLOR;
            } else if (strcmp(key, "256_color") == 0) {
                theme->capabilities |= LLE_THEME_CAP_256_COLOR;
            } else if (strcmp(key, "ascii_fallback") == 0) {
                theme->capabilities |= LLE_THEME_CAP_ASCII_FALLBACK;
            }
        }
        return LLE_SUCCESS;
    }

    /* [layout] section */
    if (strcmp(section, "layout") == 0) {
        if (strcmp(key, "ps1") == 0 && value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->layout.ps1_format, sizeof(theme->layout.ps1_format),
                     "%s", value->data.string);
        } else if (strcmp(key, "ps2") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->layout.ps2_format, sizeof(theme->layout.ps2_format),
                     "%s", value->data.string);
        } else if (strcmp(key, "rprompt") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->layout.rps1_format,
                     sizeof(theme->layout.rps1_format), "%s",
                     value->data.string);
            theme->layout.enable_right_prompt = true;
        } else if (strcmp(key, "transient") == 0 &&
                   value->type == LLE_THEME_VALUE_STRING) {
            snprintf(theme->layout.transient_format,
                     sizeof(theme->layout.transient_format), "%s",
                     value->data.string);
            theme->layout.enable_transient = true;
        } else if (strcmp(key, "newline_before") == 0 &&
                   value->type == LLE_THEME_VALUE_BOOLEAN) {
            theme->layout.newline_before = value->data.boolean ? 1 : 0;
        } else if (strcmp(key, "newline_after") == 0 &&
                   value->type == LLE_THEME_VALUE_BOOLEAN) {
            theme->layout.newline_after = value->data.boolean ? 1 : 0;
        } else if (strcmp(key, "multiline") == 0 &&
                   value->type == LLE_THEME_VALUE_BOOLEAN) {
            theme->layout.enable_multiline = value->data.boolean;
        } else if (strcmp(key, "compact") == 0 &&
                   value->type == LLE_THEME_VALUE_BOOLEAN) {
            theme->layout.compact_mode = value->data.boolean;
        }
        return LLE_SUCCESS;
    }

    /* [segments] section */
    if (strcmp(section, "segments") == 0) {
        if (strcmp(key, "enabled") == 0 &&
            value->type == LLE_THEME_VALUE_ARRAY) {
            theme->enabled_segment_count = 0;
            for (size_t i = 0;
                 i < value->data.array.count && i < LLE_THEME_MAX_SEGMENTS;
                 i++) {
                if (value->data.array.items[i].type == LLE_THEME_VALUE_STRING) {
                    snprintf(theme->enabled_segments[i],
                             sizeof(theme->enabled_segments[i]), "%.31s",
                             value->data.array.items[i].data.string);
                    theme->enabled_segment_count++;
                }
            }
        }
        return LLE_SUCCESS;
    }

    /* [segments.<name>] subsections - per-segment configuration */
    if (strncmp(section, "segments.", 9) == 0 && section[9] != '\0') {
        const char *seg_name = section + 9;

        /* Find or create segment config entry */
        lle_segment_config_t *seg_cfg = NULL;
        for (size_t i = 0; i < theme->segment_config_count; i++) {
            if (strcmp(theme->segment_configs[i].name, seg_name) == 0) {
                seg_cfg = &theme->segment_configs[i];
                break;
            }
        }
        if (!seg_cfg && theme->segment_config_count < LLE_THEME_MAX_SEGMENT_CONFIGS) {
            seg_cfg = &theme->segment_configs[theme->segment_config_count++];
            memset(seg_cfg, 0, sizeof(*seg_cfg));
            snprintf(seg_cfg->name, sizeof(seg_cfg->name), "%.31s", seg_name);
            seg_cfg->configured = true;
        }

        if (seg_cfg) {
            if (strcmp(key, "show") == 0 && value->type == LLE_THEME_VALUE_BOOLEAN) {
                seg_cfg->show = value->data.boolean;
                seg_cfg->show_set = true;
            } else if (strcmp(key, "truncation_length") == 0 &&
                       value->type == LLE_THEME_VALUE_INTEGER) {
                seg_cfg->truncation_length = (int)value->data.integer;
                seg_cfg->truncation_length_set = true;
            } else if (strcmp(key, "style") == 0 &&
                       value->type == LLE_THEME_VALUE_STRING) {
                snprintf(seg_cfg->style, sizeof(seg_cfg->style), "%.31s",
                         value->data.string);
                seg_cfg->style_set = true;
            } else if (strcmp(key, "format") == 0 &&
                       value->type == LLE_THEME_VALUE_STRING) {
                snprintf(seg_cfg->format, sizeof(seg_cfg->format), "%.63s",
                         value->data.string);
                seg_cfg->format_set = true;
            } else if (strcmp(key, "home_symbol") == 0 &&
                       value->type == LLE_THEME_VALUE_STRING) {
                snprintf(seg_cfg->home_symbol, sizeof(seg_cfg->home_symbol),
                         "%.15s", value->data.string);
                seg_cfg->home_symbol_set = true;
            } else if (strcmp(key, "show_branch") == 0 &&
                       value->type == LLE_THEME_VALUE_BOOLEAN) {
                seg_cfg->show_branch = value->data.boolean;
                seg_cfg->show_branch_set = true;
            } else if (strcmp(key, "show_status") == 0 &&
                       value->type == LLE_THEME_VALUE_BOOLEAN) {
                seg_cfg->show_status = value->data.boolean;
                seg_cfg->show_status_set = true;
            } else if (strcmp(key, "show_ahead_behind") == 0 &&
                       value->type == LLE_THEME_VALUE_BOOLEAN) {
                seg_cfg->show_ahead_behind = value->data.boolean;
                seg_cfg->show_ahead_behind_set = true;
            } else if (strcmp(key, "show_stash") == 0 &&
                       value->type == LLE_THEME_VALUE_BOOLEAN) {
                seg_cfg->show_stash = value->data.boolean;
                seg_cfg->show_stash_set = true;
            }
        }
        return LLE_SUCCESS;
    }

    /* [colors] section */
    if (strcmp(section, "colors") == 0) {
        lle_color_t *target = NULL;

        /* Map key to color field */
        if (strcmp(key, "primary") == 0)
            target = &theme->colors.primary;
        else if (strcmp(key, "secondary") == 0)
            target = &theme->colors.secondary;
        else if (strcmp(key, "success") == 0)
            target = &theme->colors.success;
        else if (strcmp(key, "warning") == 0)
            target = &theme->colors.warning;
        else if (strcmp(key, "error") == 0)
            target = &theme->colors.error;
        else if (strcmp(key, "info") == 0)
            target = &theme->colors.info;
        else if (strcmp(key, "text") == 0)
            target = &theme->colors.text;
        else if (strcmp(key, "text_dim") == 0)
            target = &theme->colors.text_dim;
        else if (strcmp(key, "text_bright") == 0)
            target = &theme->colors.text_bright;
        else if (strcmp(key, "border") == 0)
            target = &theme->colors.border;
        else if (strcmp(key, "background") == 0)
            target = &theme->colors.background;
        else if (strcmp(key, "highlight") == 0)
            target = &theme->colors.highlight;
        else if (strcmp(key, "git_clean") == 0)
            target = &theme->colors.git_clean;
        else if (strcmp(key, "git_dirty") == 0)
            target = &theme->colors.git_dirty;
        else if (strcmp(key, "git_staged") == 0)
            target = &theme->colors.git_staged;
        else if (strcmp(key, "git_untracked") == 0)
            target = &theme->colors.git_untracked;
        else if (strcmp(key, "git_branch") == 0)
            target = &theme->colors.git_branch;
        else if (strcmp(key, "git_ahead") == 0)
            target = &theme->colors.git_ahead;
        else if (strcmp(key, "git_behind") == 0)
            target = &theme->colors.git_behind;
        else if (strcmp(key, "path_home") == 0)
            target = &theme->colors.path_home;
        else if (strcmp(key, "path_root") == 0)
            target = &theme->colors.path_root;
        else if (strcmp(key, "path_normal") == 0)
            target = &theme->colors.path_normal;
        else if (strcmp(key, "path_separator") == 0)
            target = &theme->colors.path_separator;
        else if (strcmp(key, "status_ok") == 0)
            target = &theme->colors.status_ok;
        else if (strcmp(key, "status_error") == 0)
            target = &theme->colors.status_error;
        else if (strcmp(key, "status_running") == 0)
            target = &theme->colors.status_running;

        if (target) {
            lle_result_t result = apply_color(value, target);
            if (result != LLE_SUCCESS) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "Invalid color value for '%s'", key);
                ctx->error = result;
                return result;
            }
        }
        return LLE_SUCCESS;
    }

    /* [symbols] section */
    if (strcmp(section, "symbols") == 0 &&
        value->type == LLE_THEME_VALUE_STRING) {
        char *target = NULL;
        size_t target_size = LLE_SYMBOL_MAX;

        if (strcmp(key, "prompt") == 0)
            target = theme->symbols.prompt;
        else if (strcmp(key, "prompt_root") == 0)
            target = theme->symbols.prompt_root;
        else if (strcmp(key, "continuation") == 0)
            target = theme->symbols.continuation;
        else if (strcmp(key, "separator_left") == 0)
            target = theme->symbols.separator_left;
        else if (strcmp(key, "separator_right") == 0)
            target = theme->symbols.separator_right;
        else if (strcmp(key, "branch") == 0)
            target = theme->symbols.branch;
        else if (strcmp(key, "staged") == 0)
            target = theme->symbols.staged;
        else if (strcmp(key, "unstaged") == 0)
            target = theme->symbols.unstaged;
        else if (strcmp(key, "untracked") == 0)
            target = theme->symbols.untracked;
        else if (strcmp(key, "ahead") == 0)
            target = theme->symbols.ahead;
        else if (strcmp(key, "behind") == 0)
            target = theme->symbols.behind;
        else if (strcmp(key, "stash") == 0)
            target = theme->symbols.stash;
        else if (strcmp(key, "conflict") == 0)
            target = theme->symbols.conflict;
        else if (strcmp(key, "directory") == 0)
            target = theme->symbols.directory;
        else if (strcmp(key, "home") == 0)
            target = theme->symbols.home;
        else if (strcmp(key, "error") == 0)
            target = theme->symbols.error;
        else if (strcmp(key, "success") == 0)
            target = theme->symbols.success;
        else if (strcmp(key, "time") == 0)
            target = theme->symbols.time;
        else if (strcmp(key, "jobs") == 0)
            target = theme->symbols.jobs;

        if (target) {
            /* Limit to LLE_SYMBOL_MAX-1 chars to prevent truncation warnings */
            snprintf(target, target_size, "%.15s", value->data.string);
        }
        return LLE_SUCCESS;
    }

    /* [symbols.ascii] section - ASCII fallback symbols */
    if (strcmp(section, "symbols.ascii") == 0 &&
        value->type == LLE_THEME_VALUE_STRING) {
        /* Note: ASCII fallbacks are stored in the same symbol set when
         * the theme is used in ASCII mode. For now, we just ignore these
         * during parsing and let the theme loader handle mode selection. */
        return LLE_SUCCESS;
    }

    /* [syntax] section - syntax highlighting colors */
    if (strcmp(section, "syntax") == 0) {
        uint32_t *target = NULL;
        uint8_t *attr_target = NULL;

        /* Map key to syntax color field */
        if (strcmp(key, "command_valid") == 0)
            target = &theme->syntax_colors.command_valid;
        else if (strcmp(key, "command_invalid") == 0)
            target = &theme->syntax_colors.command_invalid;
        else if (strcmp(key, "command_builtin") == 0)
            target = &theme->syntax_colors.command_builtin;
        else if (strcmp(key, "command_alias") == 0)
            target = &theme->syntax_colors.command_alias;
        else if (strcmp(key, "command_function") == 0)
            target = &theme->syntax_colors.command_function;
        else if (strcmp(key, "keyword") == 0)
            target = &theme->syntax_colors.keyword;
        else if (strcmp(key, "string") == 0)
            target = &theme->syntax_colors.string;
        else if (strcmp(key, "string_escape") == 0)
            target = &theme->syntax_colors.string_escape;
        else if (strcmp(key, "variable") == 0)
            target = &theme->syntax_colors.variable;
        else if (strcmp(key, "variable_special") == 0)
            target = &theme->syntax_colors.variable_special;
        else if (strcmp(key, "path_valid") == 0)
            target = &theme->syntax_colors.path_valid;
        else if (strcmp(key, "path_invalid") == 0)
            target = &theme->syntax_colors.path_invalid;
        else if (strcmp(key, "pipe") == 0)
            target = &theme->syntax_colors.pipe;
        else if (strcmp(key, "redirect") == 0)
            target = &theme->syntax_colors.redirect;
        else if (strcmp(key, "operator") == 0)
            target = &theme->syntax_colors.operator_other;
        else if (strcmp(key, "assignment") == 0)
            target = &theme->syntax_colors.assignment;
        else if (strcmp(key, "comment") == 0)
            target = &theme->syntax_colors.comment;
        else if (strcmp(key, "number") == 0)
            target = &theme->syntax_colors.number;
        else if (strcmp(key, "option") == 0)
            target = &theme->syntax_colors.option;
        else if (strcmp(key, "glob") == 0)
            target = &theme->syntax_colors.glob;
        else if (strcmp(key, "argument") == 0)
            target = &theme->syntax_colors.argument;
        else if (strcmp(key, "error") == 0)
            target = &theme->syntax_colors.error;
        /* Here-documents and here-strings */
        else if (strcmp(key, "heredoc_op") == 0)
            target = &theme->syntax_colors.heredoc_op;
        else if (strcmp(key, "heredoc_delim") == 0)
            target = &theme->syntax_colors.heredoc_delim;
        else if (strcmp(key, "heredoc_content") == 0)
            target = &theme->syntax_colors.heredoc_content;
        else if (strcmp(key, "herestring") == 0)
            target = &theme->syntax_colors.herestring;
        /* Process substitution */
        else if (strcmp(key, "procsub") == 0)
            target = &theme->syntax_colors.procsub;
        /* ANSI-C quoting */
        else if (strcmp(key, "string_ansic") == 0)
            target = &theme->syntax_colors.string_ansic;
        /* Arithmetic expansion */
        else if (strcmp(key, "arithmetic") == 0)
            target = &theme->syntax_colors.arithmetic;
        /* Text attributes */
        else if (strcmp(key, "keyword_bold") == 0)
            attr_target = &theme->syntax_colors.keyword_bold;
        else if (strcmp(key, "command_bold") == 0)
            attr_target = &theme->syntax_colors.command_bold;
        else if (strcmp(key, "error_underline") == 0)
            attr_target = &theme->syntax_colors.error_underline;
        else if (strcmp(key, "path_underline") == 0)
            attr_target = &theme->syntax_colors.path_underline;
        else if (strcmp(key, "comment_dim") == 0)
            attr_target = &theme->syntax_colors.comment_dim;

        if (target) {
            lle_result_t result = apply_syntax_color(value, target);
            if (result != LLE_SUCCESS) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "Invalid syntax color value for '%s'", key);
                ctx->error = result;
                return result;
            }
            theme->has_syntax_colors = true;
        } else if (attr_target && value->type == LLE_THEME_VALUE_BOOLEAN) {
            *attr_target = value->data.boolean ? 1 : 0;
            theme->has_syntax_colors = true;
        }
        return LLE_SUCCESS;
    }

    /* Unknown section/key - ignore (allows future extensions) */
    return LLE_SUCCESS;
}

/**
 * @brief Parse input directly into a theme structure
 *
 * Convenience function that parses TOML input and populates a theme
 * structure in a single call, using the theme builder callback internally.
 *
 * @param parser Pointer to initialized parser
 * @param theme  Pointer to theme structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parser or theme is NULL,
 *         or error code from parsing
 */
lle_result_t lle_theme_parser_parse_to_theme(lle_theme_parser_t *parser,
                                             lle_theme_t *theme) {
    if (!parser || !theme) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    theme_builder_ctx_t ctx = {
        .theme = theme, .error_msg = {0}, .error = LLE_SUCCESS};

    lle_result_t result =
        lle_theme_parser_parse(parser, theme_builder_callback, &ctx);

    if (result != LLE_SUCCESS) {
        return result;
    }

    if (ctx.error != LLE_SUCCESS) {
        snprintf(parser->error_msg, sizeof(parser->error_msg), "%s",
                 ctx.error_msg);
        return ctx.error;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * Theme Validation
 * ============================================================================
 */

/**
 * @brief Validate a parsed theme structure
 *
 * Checks that a theme has required fields and valid values. Currently
 * validates that the theme name is present and contains only valid characters.
 *
 * @param theme         Pointer to theme to validate
 * @param error_buf     Optional buffer for error message (may be NULL)
 * @param error_buf_len Size of error buffer
 * @return LLE_SUCCESS if valid, LLE_ERROR_INVALID_PARAMETER if theme is NULL,
 *         LLE_ERROR_INVALID_FORMAT if validation fails
 */
lle_result_t lle_theme_parser_validate(const lle_theme_t *theme,
                                       char *error_buf, size_t error_buf_len) {
    if (!theme) {
        if (error_buf && error_buf_len > 0) {
            snprintf(error_buf, error_buf_len, "Theme is NULL");
        }
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Theme name is required */
    if (strlen(theme->name) == 0) {
        if (error_buf && error_buf_len > 0) {
            snprintf(error_buf, error_buf_len, "Theme name is required");
        }
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Validate theme name characters */
    for (size_t i = 0; theme->name[i] != '\0'; i++) {
        char c = theme->name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            if (error_buf && error_buf_len > 0) {
                snprintf(error_buf, error_buf_len,
                         "Theme name contains invalid character '%c'", c);
            }
            return LLE_ERROR_INVALID_FORMAT;
        }
    }

    /* PS1 format is recommended but not required (will inherit from parent) */

    return LLE_SUCCESS;
}
