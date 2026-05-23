/**
 * @file formatting_engine.c
 * @brief Command formatting with multiple style options
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 *
 * This module provides command formatting capabilities with support for
 * multiple formatting styles (compact, readable, expanded). It handles
 * whitespace normalization, operator spacing, and pipeline formatting.
 */

#include "lle/formatting_engine.h"
#include "lle/command_structure.h"
#include "lle/memory_management.h"
#include "lle/structure_analyzer.h"
#include <ctype.h>
#include <string.h>

// Default configuration values
#define DEFAULT_SPACES_PER_LEVEL 2
#define DEFAULT_MAX_LINE_LENGTH 80

// Formatting engine implementation
struct lle_formatting_engine {
    lle_memory_pool_t *memory_pool;
    lle_structure_analyzer_t *analyzer;
    lle_formatting_options_t options;
    bool active;
};

// Forward declarations for internal functions
static lle_result_t format_compact(lle_formatting_engine_t *engine,
                                   const char *text, size_t length,
                                   char **output, size_t *output_len);
static lle_result_t format_readable(lle_formatting_engine_t *engine,
                                    const char *text, size_t length,
                                    char **output, size_t *output_len);
static lle_result_t format_expanded(lle_formatting_engine_t *engine,
                                    const char *text, size_t length,
                                    char **output, size_t *output_len);
static bool should_add_space_before(char c);
static bool should_add_space_after(char c);

/* ============================================================================
 * PUBLIC API - CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Get default formatting engine options
 * @param options Pointer to options structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if options is
 * NULL
 */
lle_result_t
lle_formatting_engine_get_default_options(lle_formatting_options_t *options) {
    if (!options) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    options->style = LLE_FORMAT_READABLE;
    options->indent_char = ' ';
    options->spaces_per_level = DEFAULT_SPACES_PER_LEVEL;
    options->break_long_lines = false;
    options->max_line_length = DEFAULT_MAX_LINE_LENGTH;
    options->normalize_spaces = true;
    options->trim_trailing = true;
    options->break_pipes = false;
    options->align_pipe_operators = false;
    options->space_around_operators = true;
    options->space_after_keywords = true;
    memset(options->reserved, 0, sizeof(options->reserved));

    return LLE_SUCCESS;
}

/**
 * @brief Get preset options for a specific formatting style
 * @param style The formatting style preset
 * @param options Pointer to options structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if options is
 * NULL
 */
lle_result_t
lle_formatting_engine_get_preset_options(lle_formatting_style_t style,
                                         lle_formatting_options_t *options) {
    if (!options) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_formatting_engine_get_default_options(options);
    options->style = style;

    switch (style) {
    case LLE_FORMAT_COMPACT:
        options->normalize_spaces = true;
        options->trim_trailing = true;
        options->break_pipes = false;
        options->align_pipe_operators = false;
        options->space_around_operators = false;
        options->space_after_keywords = false;
        break;

    case LLE_FORMAT_READABLE:
        // Use defaults
        break;

    case LLE_FORMAT_EXPANDED:
        options->break_long_lines = true;
        options->break_pipes = true;
        options->align_pipe_operators = true;
        options->space_around_operators = true;
        options->space_after_keywords = true;
        break;

    case LLE_FORMAT_CUSTOM:
        // Keep current options
        break;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new formatting engine instance
 * @param engine Pointer to store the created engine
 * @param memory_pool Memory pool for allocations (can be NULL for global pool)
 * @param analyzer Structure analyzer for command analysis
 * @param options Formatting options (can be NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_formatting_engine_create(lle_formatting_engine_t **engine,
                             lle_memory_pool_t *memory_pool,
                             lle_structure_analyzer_t *analyzer,
                             const lle_formatting_options_t *options) {
    if (!engine || !analyzer) /* memory_pool can be NULL */ {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_formatting_engine_t *new_engine =
        lle_pool_alloc(sizeof(lle_formatting_engine_t));
    if (!new_engine) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    new_engine->memory_pool = memory_pool;
    new_engine->analyzer = analyzer;

    if (options) {
        memcpy(&new_engine->options, options, sizeof(lle_formatting_options_t));
    } else {
        lle_formatting_engine_get_default_options(&new_engine->options);
    }

    new_engine->active = true;

    *engine = new_engine;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a formatting engine and release resources
 * @param engine The engine to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if engine is NULL
 */
lle_result_t lle_formatting_engine_destroy(lle_formatting_engine_t *engine) {
    if (!engine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    engine->active = false;
    // Memory pool owns all allocations, no explicit frees needed

    return LLE_SUCCESS;
}

/**
 * @brief Update formatting engine options
 * @param engine The formatting engine
 * @param options New options to apply
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_formatting_engine_set_options(lle_formatting_engine_t *engine,
                                  const lle_formatting_options_t *options) {
    if (!engine || !engine->active || !options) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memcpy(&engine->options, options, sizeof(lle_formatting_options_t));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - FORMATTING OPERATIONS
 * ============================================================================
 */

/**
 * @brief Normalize whitespace in command text
 * @param engine The formatting engine
 * @param command_text The command text to normalize
 * @param command_length Length of the command text
 * @param normalized_text Pointer to store normalized text
 * @param normalized_length Pointer to store normalized text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_normalize(lle_formatting_engine_t *engine,
                                             const char *command_text,
                                             size_t command_length,
                                             char **normalized_text,
                                             size_t *normalized_length) {
    if (!engine || !engine->active || !command_text || !normalized_text ||
        !normalized_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate output buffer
    char *output = lle_pool_alloc(command_length + 1);
    if (!output) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t out_pos = 0;
    bool last_was_space = false;
    bool in_quote = false;
    char quote_char = '\0';

    for (size_t i = 0; i < command_length; i++) {
        char c = command_text[i];

        // Handle quotes
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true;
            quote_char = c;
            output[out_pos++] = c;
            last_was_space = false;
            continue;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
            quote_char = '\0';
            output[out_pos++] = c;
            last_was_space = false;
            continue;
        }

        // Don't normalize inside quotes
        if (in_quote) {
            output[out_pos++] = c;
            last_was_space = false;
            continue;
        }

        // Normalize whitespace
        if (engine->options.normalize_spaces && isspace(c)) {
            if (!last_was_space && c != '\n') {
                output[out_pos++] = ' ';
                last_was_space = true;
            } else if (c == '\n') {
                // Handle trailing whitespace
                if (engine->options.trim_trailing && out_pos > 0 &&
                    output[out_pos - 1] == ' ') {
                    out_pos--; // Remove trailing space
                }
                output[out_pos++] = '\n';
                last_was_space = false;
            }
        } else {
            output[out_pos++] = c;
            last_was_space = false;
        }
    }

    // Trim final trailing whitespace
    if (engine->options.trim_trailing && out_pos > 0) {
        while (out_pos > 0 && isspace(output[out_pos - 1])) {
            out_pos--;
        }
    }

    output[out_pos] = '\0';

    *normalized_text = output;
    *normalized_length = out_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Apply a specific formatting style to command text
 * @param engine The formatting engine
 * @param command_text The command text to format
 * @param command_length Length of the command text
 * @param style The formatting style to apply
 * @param formatted_text Pointer to store formatted text
 * @param formatted_length Pointer to store formatted text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_apply_style(lle_formatting_engine_t *engine,
                                               const char *command_text,
                                               size_t command_length,
                                               lle_formatting_style_t style,
                                               char **formatted_text,
                                               size_t *formatted_length) {
    if (!engine || !engine->active || !command_text || !formatted_text ||
        !formatted_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result;

    switch (style) {
    case LLE_FORMAT_COMPACT:
        result = format_compact(engine, command_text, command_length,
                                formatted_text, formatted_length);
        break;

    case LLE_FORMAT_READABLE:
        result = format_readable(engine, command_text, command_length,
                                 formatted_text, formatted_length);
        break;

    case LLE_FORMAT_EXPANDED:
        result = format_expanded(engine, command_text, command_length,
                                 formatted_text, formatted_length);
        break;

    case LLE_FORMAT_CUSTOM:
        // Use current options for formatting
        result = format_readable(engine, command_text, command_length,
                                 formatted_text, formatted_length);
        break;

    default:
        return LLE_ERROR_INVALID_PARAMETER;
    }

    return result;
}

/**
 * @brief Format command text using configured options
 * @param engine The formatting engine
 * @param command_text The command text to format
 * @param command_length Length of the command text
 * @param result Pointer to store the formatted command result
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_formatting_engine_format(lle_formatting_engine_t *engine,
                                          const char *command_text,
                                          size_t command_length,
                                          lle_formatted_command_t **result) {
    if (!engine || !engine->active || !command_text || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate result structure
    lle_formatted_command_t *formatted =
        lle_pool_alloc(sizeof(lle_formatted_command_t));
    if (!formatted) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(formatted, 0, sizeof(lle_formatted_command_t));

    // Apply formatting according to configured style
    char *formatted_text = NULL;
    size_t formatted_length = 0;

    lle_result_t res = lle_formatting_engine_apply_style(
        engine, command_text, command_length, engine->options.style,
        &formatted_text, &formatted_length);

    if (res != LLE_SUCCESS) {
        return res;
    }

    formatted->text = formatted_text;
    formatted->length = formatted_length;
    formatted->style_applied = engine->options.style;
    formatted->was_reformatted =
        (formatted_length != command_length ||
         memcmp(formatted_text, command_text, command_length) != 0);

    *result = formatted;
    return LLE_SUCCESS;
}

/**
 * @brief Free a formatted command result
 * @param engine The formatting engine
 * @param result The result to free
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_formatting_engine_free_result(lle_formatting_engine_t *engine,
                                  lle_formatted_command_t *result) {
    if (!engine || !engine->active || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Memory pool owns all allocations, no explicit frees needed

    return LLE_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Format command in compact style (minimize whitespace)
 * @param engine The formatting engine
 * @param text The text to format
 * @param length Length of the text
 * @param output Pointer to store formatted output
 * @param output_len Pointer to store output length
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t format_compact(lle_formatting_engine_t *engine,
                                   const char *text, size_t length,
                                   char **output, size_t *output_len) {
    (void)engine; // Reserved for engine-specific formatting options
    // Compact format: minimize whitespace
    char *result = lle_pool_alloc(length + 1);
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t out_pos = 0;
    bool in_quote = false;
    char quote_char = '\0';
    bool last_was_space = false;

    for (size_t i = 0; i < length; i++) {
        char c = text[i];

        // Handle quotes
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true;
            quote_char = c;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
            quote_char = '\0';
        }

        if (in_quote) {
            result[out_pos++] = c;
            last_was_space = false;
        } else if (c == '\n') {
            // Keep newlines but remove preceding spaces
            if (out_pos > 0 && result[out_pos - 1] == ' ') {
                out_pos--;
            }
            result[out_pos++] = '\n';
            last_was_space = false;
        } else if (isspace(c)) {
            // Replace multiple spaces with single space
            if (!last_was_space && out_pos > 0) {
                result[out_pos++] = ' ';
                last_was_space = true;
            }
        } else {
            result[out_pos++] = c;
            last_was_space = false;
        }
    }

    result[out_pos] = '\0';
    *output = result;
    *output_len = out_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Format command in readable style (balanced whitespace)
 * @param engine The formatting engine
 * @param text The text to format
 * @param length Length of the text
 * @param output Pointer to store formatted output
 * @param output_len Pointer to store output length
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t format_readable(lle_formatting_engine_t *engine,
                                    const char *text, size_t length,
                                    char **output, size_t *output_len) {
    // Readable format: balanced whitespace and indentation
    size_t estimated_size = length + (length / 10); // 10% overhead estimate
    char *result = lle_pool_alloc(estimated_size);
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t out_pos = 0;
    bool in_quote = false;
    char quote_char = '\0';
    bool last_was_space = false;

    for (size_t i = 0; i < length; i++) {
        char c = text[i];

        // Handle quotes
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true;
            quote_char = c;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
            quote_char = '\0';
        }

        if (in_quote) {
            result[out_pos++] = c;
            last_was_space = false;
        } else if (isspace(c)) {
            if (c == '\n' || !last_was_space) {
                result[out_pos++] = c;
                last_was_space = (c != '\n');
            }
        } else {
            // Add space around operators if configured
            if (engine->options.space_around_operators) {
                if (should_add_space_before(c) && out_pos > 0 &&
                    !isspace(result[out_pos - 1])) {
                    result[out_pos++] = ' ';
                }
            }

            result[out_pos++] = c;
            last_was_space = false;

            if (engine->options.space_around_operators &&
                should_add_space_after(c)) {
                result[out_pos++] = ' ';
                last_was_space = true;
            }
        }
    }

    result[out_pos] = '\0';
    *output = result;
    *output_len = out_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Format command in expanded style (maximum readability)
 * @param engine The formatting engine
 * @param text The text to format
 * @param length Length of the text
 * @param output Pointer to store formatted output
 * @param output_len Pointer to store output length
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t format_expanded(lle_formatting_engine_t *engine,
                                    const char *text, size_t length,
                                    char **output, size_t *output_len) {
    // Expanded format: maximum readability with line breaks
    size_t estimated_size = length * 2; // 100% overhead estimate for expansion
    char *result = lle_pool_alloc(estimated_size);
    if (!result) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t out_pos = 0;
    bool in_quote = false;
    char quote_char = '\0';

    for (size_t i = 0; i < length; i++) {
        char c = text[i];

        // Handle quotes
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true;
            quote_char = c;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
            quote_char = '\0';
        }

        if (in_quote) {
            result[out_pos++] = c;
        } else if (c == '|' && engine->options.break_pipes) {
            // Break pipelines into separate lines
            result[out_pos++] = ' ';
            result[out_pos++] = '|';
            result[out_pos++] = '\n';

            // Add indentation for next line
            for (uint8_t j = 0; j < engine->options.spaces_per_level; j++) {
                result[out_pos++] = engine->options.indent_char;
            }
        } else {
            result[out_pos++] = c;
        }
    }

    result[out_pos] = '\0';
    *output = result;
    *output_len = out_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Check if a space should be added before a character
 * @param c The character to check
 * @return true if space should be added before, false otherwise
 */
static bool should_add_space_before(char c) {
    return (c == '|' || c == '&' || c == ';');
}

/**
 * @brief Check if a space should be added after a character
 * @param c The character to check
 * @return true if space should be added after, false otherwise
 */
static bool should_add_space_after(char c) {
    return (c == '|' || c == '&' || c == ';');
}
