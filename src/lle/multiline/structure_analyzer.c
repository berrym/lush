/**
 * @file structure_analyzer.c
 * @brief Shell command structure analysis and keyword detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration
 *
 * This module analyzes shell command structure, detecting keywords,
 * tracking nesting levels, and determining command completeness.
 * It supports POSIX shell constructs including loops, conditionals,
 * case statements, and function definitions.
 */

#include "lle/structure_analyzer.h"
#include "lle/command_structure.h"
#include "lle/memory_management.h"
#include <ctype.h>
#include <string.h>

// Default configuration values
#define DEFAULT_MAX_NESTING_DEPTH 50
#define DEFAULT_MAX_COMMAND_LENGTH 1048576 // 1MB

// Keyword definitions
typedef struct keyword_definition {
    const char *text;
    size_t length;
    lle_keyword_type_t type;
    bool requires_command_start;
} keyword_definition_t;

static const keyword_definition_t SHELL_KEYWORDS[] = {
    {     "for", 3,      LLE_KEYWORD_FOR,  true},
    {  "select", 6,   LLE_KEYWORD_SELECT,  true},
    {    "case", 4,     LLE_KEYWORD_CASE,  true},
    {      "if", 2,       LLE_KEYWORD_IF,  true},
    {   "while", 5,    LLE_KEYWORD_WHILE,  true},
    {   "until", 5,    LLE_KEYWORD_UNTIL,  true},
    {"function", 8, LLE_KEYWORD_FUNCTION,  true},
    {      "do", 2,       LLE_KEYWORD_DO, false},
    {    "done", 4,     LLE_KEYWORD_DONE, false},
    {    "then", 4,     LLE_KEYWORD_THEN, false},
    {      "fi", 2,       LLE_KEYWORD_FI, false},
    {    "esac", 4,     LLE_KEYWORD_ESAC, false},
    {    "elif", 4,     LLE_KEYWORD_ELIF, false},
    {    "else", 4,     LLE_KEYWORD_ELSE, false},
    {      NULL, 0,     LLE_KEYWORD_NONE, false}
};

// Structure analyzer implementation
struct lle_structure_analyzer {
    lle_memory_pool_t *memory_pool;
    lle_analyzer_config_t config;
    lle_analysis_context_t context;
    bool active;
};

// Forward declarations for internal functions
static bool is_word_boundary(char c);
static bool is_keyword_at_position(const char *text, size_t pos,
                                   size_t text_len,
                                   const keyword_definition_t *keyword,
                                   bool *at_start);
static void update_context_for_char(lle_analysis_context_t *ctx, char c);
static lle_result_t build_construct_tree(lle_structure_analyzer_t *analyzer,
                                         lle_command_structure_t *structure);
static lle_construct_type_t
determine_construct_type(lle_keyword_type_t first_keyword);

/* ============================================================================
 * PUBLIC API - CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Get default structure analyzer configuration
 * @param config Pointer to configuration structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if config is NULL
 */
lle_result_t
lle_structure_analyzer_get_default_config(lle_analyzer_config_t *config) {
    if (!config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    config->max_nesting_depth = DEFAULT_MAX_NESTING_DEPTH;
    config->detect_incomplete = true;
    config->validate_syntax = true;
    config->track_indentation = true;
    config->max_command_length = DEFAULT_MAX_COMMAND_LENGTH;
    memset(config->reserved, 0, sizeof(config->reserved));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new structure analyzer instance
 * @param analyzer Pointer to store the created analyzer
 * @param memory_pool Memory pool for allocations (can be NULL for global pool)
 * @param config Analyzer configuration (can be NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_create(lle_structure_analyzer_t **analyzer,
                              lle_memory_pool_t *memory_pool,
                              const lle_analyzer_config_t *config) {
    if (!analyzer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // memory_pool can be NULL - will use global pool

    lle_structure_analyzer_t *new_analyzer =
        lle_pool_alloc(sizeof(lle_structure_analyzer_t));
    if (!new_analyzer) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline structure allocation failed");
    }

    new_analyzer->memory_pool = memory_pool; // Can be NULL

    if (config) {
        memcpy(&new_analyzer->config, config, sizeof(lle_analyzer_config_t));
    } else {
        lle_structure_analyzer_get_default_config(&new_analyzer->config);
    }

    memset(&new_analyzer->context, 0, sizeof(lle_analysis_context_t));
    new_analyzer->active = true;

    *analyzer = new_analyzer;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a structure analyzer and release resources
 * @param analyzer The analyzer to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if analyzer is
 * NULL
 */
lle_result_t
lle_structure_analyzer_destroy(lle_structure_analyzer_t *analyzer) {
    if (!analyzer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    analyzer->active = false;
    // Memory pool owns all allocations, no explicit frees needed

    return LLE_SUCCESS;
}

/**
 * @brief Reset the analyzer state for a new analysis session
 * @param analyzer The analyzer to reset
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
lle_result_t lle_structure_analyzer_reset(lle_structure_analyzer_t *analyzer) {
    if (!analyzer || !analyzer->active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(&analyzer->context, 0, sizeof(lle_analysis_context_t));

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - ANALYSIS OPERATIONS
 * ============================================================================
 */

/**
 * @brief Detect shell keywords in command text
 * @param analyzer The structure analyzer
 * @param command_text The command text to analyze
 * @param command_length Length of the command text
 * @param keywords Pointer to store array of keyword matches
 * @param keyword_count Pointer to store number of keywords found
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_detect_keywords(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_keyword_match_t **keywords,
    size_t *keyword_count) {
    if (!analyzer || !analyzer->active || !command_text || !keywords ||
        !keyword_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (command_length > analyzer->config.max_command_length) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Reset context for new analysis
    memset(&analyzer->context, 0, sizeof(lle_analysis_context_t));

    // Count keywords first pass
    size_t count = 0;
    for (size_t i = 0; i < command_length; i++) {
        update_context_for_char(&analyzer->context, command_text[i]);

        // Skip if we're inside quotes or comments
        if (analyzer->context.in_quoted_string ||
            analyzer->context.in_comment) {
            continue;
        }

        // Check each keyword definition
        for (const keyword_definition_t *kw = SHELL_KEYWORDS; kw->text != NULL;
             kw++) {
            bool at_start = false;
            if (is_keyword_at_position(command_text, i, command_length, kw,
                                       &at_start)) {
                if (!kw->requires_command_start || at_start) {
                    count++;
                    i += kw->length - 1; // Skip past keyword
                    break;
                }
            }
        }
    }

    if (count == 0) {
        *keywords = NULL;
        *keyword_count = 0;
        return LLE_SUCCESS;
    }

    // Allocate keyword array
    lle_keyword_match_t *kw_array =
        lle_pool_alloc(count * sizeof(lle_keyword_match_t));
    if (!kw_array) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline structure allocation failed");
    }

    // Second pass: extract keywords
    memset(&analyzer->context, 0, sizeof(lle_analysis_context_t));
    size_t kw_idx = 0;

    for (size_t i = 0; i < command_length; i++) {
        char c = command_text[i];

        // Track line numbers
        if (c == '\n') {
            analyzer->context.current_line++;
        }

        update_context_for_char(&analyzer->context, c);

        // Skip if we're inside quotes or comments
        if (analyzer->context.in_quoted_string ||
            analyzer->context.in_comment) {
            continue;
        }

        // Check each keyword definition
        for (const keyword_definition_t *kw = SHELL_KEYWORDS; kw->text != NULL;
             kw++) {
            bool at_start = false;
            if (is_keyword_at_position(command_text, i, command_length, kw,
                                       &at_start)) {
                if (!kw->requires_command_start || at_start) {
                    kw_array[kw_idx].type = kw->type;
                    kw_array[kw_idx].start_offset = i;
                    kw_array[kw_idx].length = kw->length;
                    kw_array[kw_idx].line_number =
                        analyzer->context.current_line;
                    kw_array[kw_idx].is_command_start = at_start;
                    memset(kw_array[kw_idx].reserved, 0,
                           sizeof(kw_array[kw_idx].reserved));

                    kw_idx++;
                    i += kw->length - 1; // Skip past keyword
                    break;
                }
            }
        }
    }

    *keywords = kw_array;
    *keyword_count = count;

    return LLE_SUCCESS;
}

/**
 * @brief Detect the primary construct type of a command
 * @param analyzer The structure analyzer
 * @param command_text The command text to analyze
 * @param command_length Length of the command text
 * @param construct_type Pointer to store the detected construct type
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_detect_type(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_construct_type_t *construct_type) {
    if (!analyzer || !analyzer->active || !command_text || !construct_type) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_keyword_match_t *keywords = NULL;
    size_t keyword_count = 0;

    lle_result_t result = lle_structure_analyzer_detect_keywords(
        analyzer, command_text, command_length, &keywords, &keyword_count);

    if (result != LLE_SUCCESS) {
        return result;
    }

    if (keyword_count == 0) {
        *construct_type = LLE_CONSTRUCT_SIMPLE;
        return LLE_SUCCESS;
    }

    // Determine type from first keyword
    *construct_type = determine_construct_type(keywords[0].type);

    return LLE_SUCCESS;
}

/**
 * @brief Check if a command is syntactically complete
 * @param analyzer The structure analyzer
 * @param command_text The command text to analyze
 * @param command_length Length of the command text
 * @param is_complete Pointer to store completion status
 * @param missing_keyword Pointer to store missing keyword type (can be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_check_complete(lle_structure_analyzer_t *analyzer,
                                      const char *command_text,
                                      size_t command_length, bool *is_complete,
                                      lle_keyword_type_t *missing_keyword) {
    if (!analyzer || !analyzer->active || !command_text || !is_complete) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_keyword_match_t *keywords = NULL;
    size_t keyword_count = 0;

    lle_result_t result = lle_structure_analyzer_detect_keywords(
        analyzer, command_text, command_length, &keywords, &keyword_count);

    if (result != LLE_SUCCESS) {
        return result;
    }

    if (keyword_count == 0) {
        *is_complete = true;
        if (missing_keyword) {
            *missing_keyword = LLE_KEYWORD_NONE;
        }
        return LLE_SUCCESS;
    }

    // Stack-based matching of opening/closing keywords
    typedef struct {
        lle_keyword_type_t opening;
        lle_keyword_type_t closing;
    } keyword_pair_t;

    static const keyword_pair_t pairs[] = {
        {   LLE_KEYWORD_FOR, LLE_KEYWORD_DONE},
        { LLE_KEYWORD_WHILE, LLE_KEYWORD_DONE},
        { LLE_KEYWORD_UNTIL, LLE_KEYWORD_DONE},
        {LLE_KEYWORD_SELECT, LLE_KEYWORD_DONE},
        {    LLE_KEYWORD_IF,   LLE_KEYWORD_FI},
        {  LLE_KEYWORD_CASE, LLE_KEYWORD_ESAC},
        {  LLE_KEYWORD_NONE, LLE_KEYWORD_NONE}
    };

    // Simple stack to track unclosed keywords
    lle_keyword_type_t stack[DEFAULT_MAX_NESTING_DEPTH];
    size_t stack_size = 0;

    for (size_t i = 0; i < keyword_count; i++) {
        lle_keyword_type_t kw_type = keywords[i].type;

        // Check if it's an opening keyword
        bool is_opening = false;
        lle_keyword_type_t expected_closing = LLE_KEYWORD_NONE;

        for (const keyword_pair_t *pair = pairs;
             pair->opening != LLE_KEYWORD_NONE; pair++) {
            if (kw_type == pair->opening) {
                is_opening = true;
                expected_closing = pair->closing;
                break;
            }
        }

        if (is_opening) {
            if (stack_size >= DEFAULT_MAX_NESTING_DEPTH) {
                return LLE_ERROR_INVALID_STATE;
            }
            stack[stack_size++] = expected_closing;
        } else {
            // Check if it's a closing keyword
            bool is_closing = false;
            for (const keyword_pair_t *pair = pairs;
                 pair->opening != LLE_KEYWORD_NONE; pair++) {
                if (kw_type == pair->closing) {
                    is_closing = true;
                    break;
                }
            }

            if (is_closing) {
                if (stack_size == 0) {
                    // Closing without opening
                    *is_complete = false;
                    if (missing_keyword) {
                        *missing_keyword = LLE_KEYWORD_NONE;
                    }
                    return LLE_SUCCESS;
                }

                if (stack[stack_size - 1] != kw_type) {
                    // Mismatched closing
                    *is_complete = false;
                    if (missing_keyword) {
                        *missing_keyword = stack[stack_size - 1];
                    }
                    return LLE_SUCCESS;
                }

                stack_size--;
            }
        }
    }

    *is_complete = (stack_size == 0);
    if (missing_keyword) {
        *missing_keyword =
            (stack_size > 0) ? stack[stack_size - 1] : LLE_KEYWORD_NONE;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Calculate indentation information for a command
 * @param analyzer The structure analyzer
 * @param command_text The command text to analyze
 * @param command_length Length of the command text
 * @param indent_info Pointer to store indentation info
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_structure_analyzer_calculate_indentation(
    lle_structure_analyzer_t *analyzer, const char *command_text,
    size_t command_length, lle_indentation_info_t **indent_info) {
    if (!analyzer || !analyzer->active || !command_text || !indent_info) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!analyzer->config.track_indentation) {
        return LLE_ERROR_INVALID_STATE;
    }

    lle_indentation_info_t *info =
        lle_pool_alloc(sizeof(lle_indentation_info_t));
    if (!info) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline structure allocation failed");
    }

    memset(info, 0, sizeof(lle_indentation_info_t));

    // Count lines
    size_t line_count = 1;
    for (size_t i = 0; i < command_length; i++) {
        if (command_text[i] == '\n') {
            line_count++;
        }
    }

    info->line_count = line_count;
    info->spaces_per_level = 2; // Default to 2 spaces
    info->use_tabs = false;
    info->indent_char = ' ';

    // Allocate line indentation array
    info->level_per_line = lle_pool_alloc(line_count * sizeof(size_t));
    if (!info->level_per_line) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "multiline",
                         "multiline structure allocation failed");
    }

    // Calculate indentation for each line
    size_t current_line = 0;
    size_t line_start = 0;

    for (size_t i = 0; i <= command_length; i++) {
        if (i == command_length || command_text[i] == '\n') {
            // Calculate indentation for this line
            size_t indent = 0;
            for (size_t j = line_start; j < i; j++) {
                if (command_text[j] == ' ') {
                    indent++;
                } else if (command_text[j] == '\t') {
                    indent += 4; // Tab = 4 spaces
                    info->use_tabs = true;
                } else {
                    break; // Non-whitespace found
                }
            }

            info->level_per_line[current_line] = indent;
            current_line++;
            line_start = i + 1;
        }
    }

    *indent_info = info;
    return LLE_SUCCESS;
}

/**
 * @brief Perform full structural analysis of a command
 * @param analyzer The structure analyzer
 * @param command_text The command text to analyze
 * @param command_length Length of the command text
 * @param structure Pointer to store the command structure
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_structure_analyzer_analyze(lle_structure_analyzer_t *analyzer,
                               const char *command_text, size_t command_length,
                               lle_command_structure_t **structure) {
    if (!analyzer || !analyzer->active || !command_text || !structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Create command structure
    lle_result_t result =
        lle_command_structure_create(structure, analyzer->memory_pool);
    if (result != LLE_SUCCESS) {
        return result;
    }

    // Detect keywords
    lle_keyword_match_t *keywords = NULL;
    size_t keyword_count = 0;

    result = lle_structure_analyzer_detect_keywords(
        analyzer, command_text, command_length, &keywords, &keyword_count);

    if (result != LLE_SUCCESS) {
        lle_command_structure_destroy(*structure);
        *structure = NULL;
        return result;
    }

    // Add keywords to structure
    for (size_t i = 0; i < keyword_count; i++) {
        result = lle_command_structure_add_keyword(
            *structure, keywords[i].type, keywords[i].start_offset,
            keywords[i].line_number, 0 // Indent level calculated separately
        );

        if (result != LLE_SUCCESS) {
            lle_command_structure_destroy(*structure);
            *structure = NULL;
            return result;
        }
    }

    // Determine primary construct type
    if (keyword_count > 0) {
        (*structure)->primary_type = determine_construct_type(keywords[0].type);
    } else {
        (*structure)->primary_type = LLE_CONSTRUCT_SIMPLE;
    }

    // Check completeness
    bool is_complete = false;
    result = lle_structure_analyzer_check_complete(
        analyzer, command_text, command_length, &is_complete, NULL);

    if (result == LLE_SUCCESS) {
        (*structure)->is_complete = is_complete;
    }

    // Calculate indentation if enabled
    if (analyzer->config.track_indentation) {
        lle_indentation_info_t *indent_info = NULL;
        result = lle_structure_analyzer_calculate_indentation(
            analyzer, command_text, command_length, &indent_info);

        if (result == LLE_SUCCESS) {
            (*structure)->indentation = indent_info;
            (*structure)->total_lines = indent_info->line_count;
        }
    }

    // Build construct tree
    result = build_construct_tree(analyzer, *structure);
    if (result != LLE_SUCCESS) {
        lle_command_structure_destroy(*structure);
        *structure = NULL;
        return result;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Check if a character is a word boundary
 * @param c The character to check
 * @return true if the character is a word boundary, false otherwise
 */
static bool is_word_boundary(char c) {
    return isspace(c) || c == ';' || c == '|' || c == '&' || c == '(' ||
           c == ')' || c == '{' || c == '}' || c == '<' || c == '>' ||
           c == '\0';
}

/**
 * @brief Check if a keyword exists at a specific position in text
 * @param text The text to search
 * @param pos Position to check
 * @param text_len Total length of text
 * @param keyword The keyword definition to match
 * @param at_start Pointer to store whether keyword is at command start
 * @return true if keyword found at position, false otherwise
 */
static bool is_keyword_at_position(const char *text, size_t pos,
                                   size_t text_len,
                                   const keyword_definition_t *keyword,
                                   bool *at_start) {
    // Check if we have enough characters
    if (pos + keyword->length > text_len) {
        return false;
    }

    // Check if keyword matches
    if (strncmp(&text[pos], keyword->text, keyword->length) != 0) {
        return false;
    }

    // Check word boundaries
    if (pos > 0 && !is_word_boundary(text[pos - 1])) {
        return false;
    }

    if (pos + keyword->length < text_len &&
        !is_word_boundary(text[pos + keyword->length])) {
        return false;
    }

    // Check if at command/line start
    if (at_start) {
        *at_start = (pos == 0);
        if (!*at_start) {
            // Check if we're at the start of a line
            for (size_t i = pos; i > 0; i--) {
                if (text[i - 1] == '\n') {
                    *at_start = true;
                    break;
                }
                if (!isspace(text[i - 1])) {
                    break;
                }
            }
        }
    }

    return true;
}

/**
 * @brief Update analysis context state for a character
 * @param ctx The analysis context to update
 * @param c The character being processed
 *
 * Tracks quoting state, comments, and escape sequences.
 */
static void update_context_for_char(lle_analysis_context_t *ctx, char c) {
    if (ctx->last_was_escape) {
        ctx->last_was_escape = false;
        return;
    }

    if (c == '\\') {
        ctx->last_was_escape = true;
        return;
    }

    if (ctx->in_comment) {
        if (c == '\n') {
            ctx->in_comment = false;
        }
        return;
    }

    if (!ctx->in_quoted_string && c == '#') {
        ctx->in_comment = true;
        return;
    }

    if (c == '"' || c == '\'' || c == '`') {
        if (!ctx->in_quoted_string) {
            ctx->in_quoted_string = true;
            ctx->quote_char = c;
        } else if (c == ctx->quote_char) {
            ctx->in_quoted_string = false;
            ctx->quote_char = '\0';
        }
    }
}

/**
 * @brief Determine construct type from the first keyword
 * @param first_keyword The first keyword found in the command
 * @return The corresponding construct type
 */
static lle_construct_type_t
determine_construct_type(lle_keyword_type_t first_keyword) {
    switch (first_keyword) {
    case LLE_KEYWORD_FOR:
        return LLE_CONSTRUCT_FOR_LOOP;
    case LLE_KEYWORD_WHILE:
        return LLE_CONSTRUCT_WHILE_LOOP;
    case LLE_KEYWORD_UNTIL:
        return LLE_CONSTRUCT_UNTIL_LOOP;
    case LLE_KEYWORD_IF:
        return LLE_CONSTRUCT_IF_STATEMENT;
    case LLE_KEYWORD_CASE:
        return LLE_CONSTRUCT_CASE_STATEMENT;
    case LLE_KEYWORD_FUNCTION:
        return LLE_CONSTRUCT_FUNCTION;
    case LLE_KEYWORD_SELECT:
        return LLE_CONSTRUCT_SELECT;
    default:
        return LLE_CONSTRUCT_SIMPLE;
    }
}

/**
 * @brief Build a nested construct tree from keyword list
 * @param analyzer The structure analyzer
 * @param structure The command structure to populate
 * @return LLE_SUCCESS on success, error code on failure
 *
 * This function builds the nested construct tree from the keyword list.
 * Currently returns success - tree building will be enhanced in next iteration.
 */
static lle_result_t build_construct_tree(lle_structure_analyzer_t *analyzer,
                                         lle_command_structure_t *structure) {
    /// This function builds the nested construct tree from the keyword list
    /// Currently returns success - tree building will be enhanced in next
    /// iteration

    if (!analyzer || !structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Count keywords to determine max depth
    size_t keyword_count = 0;
    lle_result_t result = lle_command_structure_count_keywords(
        structure, LLE_KEYWORD_NONE, &keyword_count);

    if (result != LLE_SUCCESS) {
        return result;
    }

    structure->keyword_count = keyword_count;
    structure->max_depth = 0; // Will be calculated during tree building

    // Tree building implementation placeholder for future enhancement
    /// For now, we have the flat keyword list which is sufficient for basic
    /// analysis

    return LLE_SUCCESS;
}
