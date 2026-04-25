/**
 * @file reconstruction_engine.c
 * @brief Command reconstruction with indentation support
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 2
 *
 * This module implements command reconstruction functionality, applying
 * proper indentation to multiline shell commands based on their syntactic
 * structure. It supports whitespace normalization and configurable
 * indentation options.
 */

#include "lle/reconstruction_engine.h"
#include "lle/command_structure.h"
#include "lle/memory_management.h"
#include "lle/multiline_parser.h"
#include "lle/structure_analyzer.h"
#include <ctype.h>
#include <string.h>

/* Default configuration values */
#define DEFAULT_SPACES_PER_LEVEL 2
#define DEFAULT_MAX_OUTPUT_LENGTH 1048576 /* 1MB */

/* Reconstruction engine implementation */
struct lle_reconstruction_engine {
    lle_memory_pool_t *memory_pool;
    lle_structure_analyzer_t *analyzer;
    lle_multiline_parser_t *parser;
    lle_reconstruction_options_t options;
    bool active;
};

/* Forward declarations for internal functions */
static size_t
calculate_indented_size(const char *text, size_t length,
                        lle_command_structure_t *structure,
                        const lle_reconstruction_options_t *options);
static lle_result_t apply_indentation_to_lines(
    lle_reconstruction_engine_t *engine, lle_parsed_line_t *first_line,
    lle_command_structure_t *structure, char *output, size_t *output_length);
static size_t get_indent_for_line(lle_command_structure_t *structure,
                                  size_t line_number,
                                  const lle_reconstruction_options_t *options);

/* ============================================================================
 * PUBLIC API - CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Get default reconstruction engine options
 * @param options Pointer to options structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if options is
 * NULL
 */
lle_result_t lle_reconstruction_engine_get_default_options(
    lle_reconstruction_options_t *options) {
    if (!options) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    options->apply_indentation = true;
    options->indent_char = ' ';
    options->spaces_per_level = DEFAULT_SPACES_PER_LEVEL;
    options->preserve_line_breaks = true;
    options->normalize_whitespace = false;
    options->max_output_length = DEFAULT_MAX_OUTPUT_LENGTH;
    memset(options->reserved, 0, sizeof(options->reserved));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new reconstruction engine instance
 * @param engine Pointer to store the created engine
 * @param memory_pool Memory pool for allocations (can be NULL for global pool)
 * @param analyzer Structure analyzer for command analysis
 * @param parser Multiline parser for line splitting
 * @param options Reconstruction options (can be NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_create(
    lle_reconstruction_engine_t **engine, lle_memory_pool_t *memory_pool,
    lle_structure_analyzer_t *analyzer, lle_multiline_parser_t *parser,
    const lle_reconstruction_options_t *options) {
    if (!engine || !analyzer || !parser) { /* memory_pool can be NULL */
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_reconstruction_engine_t *new_engine =
        lle_pool_alloc(sizeof(lle_reconstruction_engine_t));
    if (!new_engine) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    new_engine->memory_pool = memory_pool;
    new_engine->analyzer = analyzer;
    new_engine->parser = parser;

    if (options) {
        memcpy(&new_engine->options, options,
               sizeof(lle_reconstruction_options_t));
    } else {
        lle_reconstruction_engine_get_default_options(&new_engine->options);
    }

    new_engine->active = true;

    *engine = new_engine;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a reconstruction engine and release resources
 * @param engine The engine to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if engine is NULL
 */
lle_result_t
lle_reconstruction_engine_destroy(lle_reconstruction_engine_t *engine) {
    if (!engine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    engine->active = false;
    /* Memory pool owns all allocations, no explicit frees needed */

    return LLE_SUCCESS;
}

/**
 * @brief Update reconstruction engine options
 * @param engine The reconstruction engine
 * @param options New options to apply
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_reconstruction_engine_set_options(
    lle_reconstruction_engine_t *engine,
    const lle_reconstruction_options_t *options) {
    if (!engine || !engine->active || !options) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memcpy(&engine->options, options, sizeof(lle_reconstruction_options_t));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - RECONSTRUCTION OPERATIONS
 * ============================================================================
 */

/**
 * @brief Normalize whitespace in command text
 * @param engine The reconstruction engine
 * @param command_text The command text to normalize
 * @param command_length Length of the command text
 * @param normalized_text Pointer to store normalized text
 * @param normalized_length Pointer to store normalized text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_normalize_whitespace(
    lle_reconstruction_engine_t *engine, const char *command_text,
    size_t command_length, char **normalized_text, size_t *normalized_length) {
    if (!engine || !engine->active || !command_text || !normalized_text ||
        !normalized_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Allocate output buffer (same size as input is sufficient) */
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

        /* Handle quotes */
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

        /* Don't normalize inside quotes */
        if (in_quote) {
            output[out_pos++] = c;
            last_was_space = false;
            continue;
        }

        /* Normalize whitespace outside quotes */
        if (isspace(c)) {
            if (c == '\n') {
                /* Preserve newlines */
                output[out_pos++] = '\n';
                last_was_space = false;
            } else if (!last_was_space) {
                /* Replace multiple spaces with single space */
                output[out_pos++] = ' ';
                last_was_space = true;
            }
        } else {
            output[out_pos++] = c;
            last_was_space = false;
        }
    }

    output[out_pos] = '\0';

    *normalized_text = output;
    *normalized_length = out_pos;

    return LLE_SUCCESS;
}

/**
 * @brief Apply indentation to command text based on structure
 * @param engine The reconstruction engine
 * @param structure Command structure with indentation info
 * @param original_text The original command text
 * @param original_length Length of original text
 * @param indented_text Pointer to store indented text
 * @param indented_length Pointer to store indented text length
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_apply_indentation(
    lle_reconstruction_engine_t *engine, lle_command_structure_t *structure,
    const char *original_text, size_t original_length, char **indented_text,
    size_t *indented_length) {
    if (!engine || !engine->active || !structure || !original_text ||
        !indented_text || !indented_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!engine->options.apply_indentation) {
        /* No indentation requested, just copy */
        char *output = lle_pool_alloc(original_length + 1);
        if (!output) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        memcpy(output, original_text, original_length);
        output[original_length] = '\0';
        *indented_text = output;
        *indented_length = original_length;
        return LLE_SUCCESS;
    }

    /* Parse into lines */
    lle_multiline_parse_result_t *parse_result = NULL;
    lle_result_t res = lle_multiline_parser_parse(
        engine->parser, original_text, original_length, &parse_result);

    if (res != LLE_SUCCESS) {
        return res;
    }

    /* Calculate output size */
    size_t output_size = calculate_indented_size(original_text, original_length,
                                                 structure, &engine->options);

    if (output_size > engine->options.max_output_length) {
        lle_multiline_parser_free_result(engine->parser, parse_result);
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Allocate output buffer */
    char *output = lle_pool_alloc(output_size + 1);
    if (!output) {
        lle_multiline_parser_free_result(engine->parser, parse_result);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Apply indentation to lines */
    size_t out_len = 0;
    res = apply_indentation_to_lines(engine, parse_result->first_line,
                                     structure, output, &out_len);

    lle_multiline_parser_free_result(engine->parser, parse_result);

    if (res != LLE_SUCCESS) {
        return res;
    }

    output[out_len] = '\0';

    *indented_text = output;
    *indented_length = out_len;

    return LLE_SUCCESS;
}

/**
 * @brief Reconstruct a command with full formatting
 * @param engine The reconstruction engine
 * @param command_text The command text to reconstruct
 * @param command_length Length of the command text
 * @param result Pointer to store the reconstructed command
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_reconstruction_engine_reconstruct(
    lle_reconstruction_engine_t *engine, const char *command_text,
    size_t command_length, lle_reconstructed_command_t **result) {
    if (!engine || !engine->active || !command_text || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Allocate result structure */
    lle_reconstructed_command_t *recon =
        lle_pool_alloc(sizeof(lle_reconstructed_command_t));
    if (!recon) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(recon, 0, sizeof(lle_reconstructed_command_t));

    /* Analyze structure */
    lle_command_structure_t *structure = NULL;
    lle_result_t res = lle_structure_analyzer_analyze(
        engine->analyzer, command_text, command_length, &structure);

    if (res != LLE_SUCCESS) {
        return res;
    }

    /* Normalize whitespace if requested */
    const char *input_text = command_text;
    size_t input_length = command_length;
    char *normalized = NULL;

    if (engine->options.normalize_whitespace) {
        res = lle_reconstruction_engine_normalize_whitespace(
            engine, command_text, command_length, &normalized, &input_length);

        if (res == LLE_SUCCESS) {
            input_text = normalized;
        }
    }

    /* Apply indentation if requested */
    char *output_text = NULL;
    size_t output_length = 0;

    res = lle_reconstruction_engine_apply_indentation(
        engine, structure, input_text, input_length, &output_text,
        &output_length);

    if (res != LLE_SUCCESS) {
        lle_command_structure_destroy(structure);
        return res;
    }

    recon->text = output_text;
    recon->length = output_length;
    recon->indentation_applied = engine->options.apply_indentation;

    /* Count lines and build line offset array */
    size_t line_count = 1;
    for (size_t i = 0; i < output_length; i++) {
        if (output_text[i] == '\n') {
            line_count++;
        }
    }

    recon->line_count = line_count;
    recon->line_offsets = lle_pool_alloc(line_count * sizeof(size_t));

    if (recon->line_offsets) {
        size_t line_idx = 0;
        recon->line_offsets[line_idx++] = 0;

        for (size_t i = 0; i < output_length; i++) {
            if (output_text[i] == '\n' && line_idx < line_count) {
                recon->line_offsets[line_idx++] = i + 1;
            }
        }
    }

    lle_command_structure_destroy(structure);

    *result = recon;
    return LLE_SUCCESS;
}

/**
 * @brief Free a reconstructed command result
 * @param engine The reconstruction engine
 * @param result The result to free
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t
lle_reconstruction_engine_free_result(lle_reconstruction_engine_t *engine,
                                      lle_reconstructed_command_t *result) {
    if (!engine || !engine->active || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Memory pool owns all allocations, no explicit frees needed */

    return LLE_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Calculate the buffer size needed for indented output
 * @param text The original text
 * @param length Length of the text
 * @param structure Command structure with line info
 * @param options Reconstruction options
 * @return Estimated size needed for indented output
 */
static size_t
calculate_indented_size(const char *text, size_t length,
                        lle_command_structure_t *structure,
                        const lle_reconstruction_options_t *options) {
    (void)text; /* Length used instead of text scanning */
    /* Estimate: original size + (max_depth * spaces_per_level * line_count) */
    size_t line_count = structure->total_lines > 0 ? structure->total_lines : 1;
    size_t max_indent = structure->max_depth * options->spaces_per_level;

    return length + (max_indent * line_count) +
           line_count; /* +line_count for newlines */
}

/**
 * @brief Get the indentation spaces for a specific line
 * @param structure Command structure with indentation info
 * @param line_number The line number to get indentation for
 * @param options Reconstruction options
 * @return Number of spaces for indentation
 */
static size_t get_indent_for_line(lle_command_structure_t *structure,
                                  size_t line_number,
                                  const lle_reconstruction_options_t *options) {
    if (!structure->indentation ||
        line_number >= structure->indentation->line_count) {
        return 0;
    }

    size_t indent_spaces = structure->indentation->level_per_line[line_number];

    /* Convert to indentation levels */
    size_t indent_level =
        indent_spaces / (structure->indentation->spaces_per_level > 0
                             ? structure->indentation->spaces_per_level
                             : 2);

    return indent_level * options->spaces_per_level;
}

/**
 * @brief Apply indentation to a list of parsed lines
 * @param engine The reconstruction engine
 * @param first_line First line in the parsed line list
 * @param structure Command structure with indentation info
 * @param output Output buffer to write to
 * @param output_length Pointer to store actual output length
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t apply_indentation_to_lines(
    lle_reconstruction_engine_t *engine, lle_parsed_line_t *first_line,
    lle_command_structure_t *structure, char *output, size_t *output_length) {
    size_t out_pos = 0;
    lle_parsed_line_t *current_line = first_line;

    while (current_line) {
        /* Get indentation for this line */
        size_t indent_spaces = get_indent_for_line(
            structure, current_line->line_number, &engine->options);

        /* Add indentation */
        for (size_t i = 0; i < indent_spaces; i++) {
            output[out_pos++] = engine->options.indent_char;
        }

        /* Find start of non-whitespace content */
        size_t content_start = 0;
        while (content_start < current_line->length &&
               isspace(current_line->content[content_start])) {
            content_start++;
        }

        /* Copy line content (without leading whitespace) */
        size_t content_len = current_line->length - content_start;
        if (content_len > 0) {
            memcpy(&output[out_pos], &current_line->content[content_start],
                   content_len);
            out_pos += content_len;
        }

        /* Add newline if not last line or if preserve_line_breaks is enabled */
        if (current_line->next || engine->options.preserve_line_breaks) {
            output[out_pos++] = '\n';
        }

        current_line = current_line->next;
    }

    *output_length = out_pos;
    return LLE_SUCCESS;
}
