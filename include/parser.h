/**
 * @file parser.h
 * @brief POSIX shell parser using recursive descent
 *
 * Implements a proper recursive descent parser for POSIX shell grammar.
 * Handles control structures, commands, pipelines, and proper token
 * boundary management.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef PARSER_H
#define PARSER_H

#include "node.h"
#include "shell_error.h"
#include "tokenizer.h"

/** Maximum depth of parser context stack */
#define PARSER_CONTEXT_MAX 16

/**
 * Maximum recursion depth for parsing nested constructs.
 * Prevents stack overflow on deeply nested or malicious input.
 * This limit applies to all recursive parsing operations including:
 * - Nested control structures (if, while, for, case)
 * - Subshells and brace groups
 * - Command substitutions
 * - Pipelines and logical expressions
 *
 * A depth of 256 allows for complex real-world scripts while
 * protecting against stack overflow attacks.
 */
#define PARSER_MAX_RECURSION_DEPTH 256

/** Parser state */
typedef struct parser {
    tokenizer_t *tokenizer;
    const char *error_message;
    bool has_error;

    /* Structured error collection (Phase 2 error management) */
    shell_error_collector_t *error_collector;
    const char *source_name; /* Script name for error display */

    /* Parser context stack for context-aware error messages */
    const char *context_stack[PARSER_CONTEXT_MAX];
    size_t context_depth;

    /* Recursion depth tracking for stack overflow protection */
    size_t recursion_depth;
} parser_t;

/* ============================================================================
 * Parser Lifecycle
 * ============================================================================
 */

/**
 * @brief Create a new parser for input string
 *
 * @param input Shell command string to parse
 * @return New parser instance or NULL on failure
 */
parser_t *parser_new(const char *input);

/**
 * @brief Create a new parser with source name for error reporting
 *
 * @param input Shell command string to parse
 * @param source_name Source filename (e.g., "script.sh" or "<stdin>")
 * @return New parser instance or NULL on failure
 */
parser_t *parser_new_with_source(const char *input, const char *source_name);

/**
 * @brief Set the source name for error reporting
 *
 * Updates the source name used in error messages. The parser does not
 * take ownership of the string; caller must ensure it remains valid.
 *
 * @param parser Parser context
 * @param source_name Source filename (e.g., "script.sh")
 */
void parser_set_source_name(parser_t *parser, const char *source_name);

/**
 * @brief Free a parser and associated resources
 *
 * @param parser Parser to free
 */
void parser_free(parser_t *parser);

/* ============================================================================
 * Parsing Functions
 * ============================================================================
 */

/**
 * @brief Parse input into an AST
 *
 * @param parser Parser context
 * @return Root AST node or NULL on error
 */
node_t *parser_parse(parser_t *parser);

/**
 * @brief Parse a complete command line
 *
 * @param parser Parser context
 * @return Root AST node or NULL on error
 */
node_t *parser_parse_command_line(parser_t *parser);

/* ============================================================================
 * Error Handling
 * ============================================================================
 */

/**
 * @brief Check if parser has an error
 *
 * @param parser Parser context
 * @return True if a parse error occurred
 */
bool parser_has_error(parser_t *parser);

/**
 * @brief Get the parser error message
 *
 * @param parser Parser context
 * @return Error message string or NULL
 */
const char *parser_error(parser_t *parser);

/* ============================================================================
 * Structured Error Collection (Phase 2)
 * ============================================================================
 */

/**
 * @brief Convert a token to a source location
 *
 * Creates a source_location_t from token position information.
 *
 * @param token Token to extract location from
 * @param filename Source filename (or NULL for default)
 * @return Source location structure
 */
source_location_t token_to_source_location(token_t *token,
                                           const char *filename);

/**
 * @brief Add a structured error to the parser's error collector
 *
 * Creates and adds a structured error with full source location information.
 * Falls back to the legacy error system if collector is not initialized.
 *
 * @param parser Parser context
 * @param code Error code from shell_error_code_t
 * @param fmt Printf-style format string for error message
 * @param ... Format arguments
 */
void parser_error_add(parser_t *parser, shell_error_code_t code,
                      const char *fmt, ...);

/**
 * @brief Display all collected parser errors
 *
 * Outputs all errors from the collector with source context.
 *
 * @param parser Parser context
 * @param out Output stream (typically stderr)
 * @param use_color Whether to use ANSI color codes
 */
void parser_display_errors(parser_t *parser, FILE *out, bool use_color);

/**
 * @brief Get the error collector from parser
 *
 * @param parser Parser context
 * @return Error collector or NULL if not initialized
 */
shell_error_collector_t *parser_get_error_collector(parser_t *parser);

/* ============================================================================
 * Parser Context Stack (for context-aware error messages)
 * ============================================================================
 */

/**
 * @brief Push a parsing context onto the stack
 *
 * Used to track what construct is currently being parsed for better error
 * messages. Context strings should be static or persist for parser lifetime.
 *
 * @param parser Parser context
 * @param context Context description (e.g., "parsing if statement")
 */
void parser_push_context(parser_t *parser, const char *context);

/**
 * @brief Pop a parsing context from the stack
 *
 * @param parser Parser context
 */
void parser_pop_context(parser_t *parser);

/**
 * @brief Add a structured error with context and help hint
 *
 * Creates and adds a structured error with full source location,
 * parser context stack, and an optional help suggestion.
 *
 * @param parser Parser context
 * @param code Error code from shell_error_code_t
 * @param help Optional help message (can be NULL)
 * @param fmt Printf-style format string for error message
 * @param ... Format arguments
 */
void parser_error_add_with_help(parser_t *parser, shell_error_code_t code,
                                const char *help, const char *fmt, ...);

/* ============================================================================
 * Recursion Depth Tracking (Stack Overflow Protection)
 * ============================================================================
 */

/**
 * @brief Enter a recursive parsing operation
 *
 * Increments the recursion depth counter and checks against the maximum
 * allowed depth. If the limit is exceeded, sets a parser error.
 *
 * @param parser Parser context
 * @return true if depth is within limits, false if limit exceeded
 */
bool parser_enter_recursion(parser_t *parser);

/**
 * @brief Exit a recursive parsing operation
 *
 * Decrements the recursion depth counter. Should be called when returning
 * from a recursive parsing function, regardless of success or failure.
 *
 * @param parser Parser context
 */
void parser_exit_recursion(parser_t *parser);

/**
 * @brief Get current recursion depth
 *
 * @param parser Parser context
 * @return Current recursion depth
 */
size_t parser_get_recursion_depth(parser_t *parser);

#endif // PARSER_H
