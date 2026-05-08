/**
 * @file parser.c
 * @brief Modern POSIX Shell Parser Implementation
 *
 * Clean recursive descent parser that properly handles POSIX shell grammar
 * with correct token boundary management and error handling.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "parser.h"

#include "executor.h"
#include "node.h"
#include "shell_mode.h"
#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static node_t *parse_command_list(parser_t *parser);
static node_t *parse_pipeline(parser_t *parser);
static node_t *parse_simple_command(parser_t *parser);

static node_t *parse_brace_group(parser_t *parser);
static node_t *parse_subshell(parser_t *parser);
static node_t *parse_if_statement(parser_t *parser);
static node_t *parse_while_statement(parser_t *parser);
static node_t *parse_until_statement(parser_t *parser);
static node_t *parse_for_statement(parser_t *parser);
static node_t *parse_case_statement(parser_t *parser);
static node_t *parse_function_definition(parser_t *parser);
static bool is_function_definition(parser_t *parser);
static node_t *parse_logical_expression(parser_t *parser);
static node_t *parse_redirection(parser_t *parser);
static bool is_redirection_token(token_type_t type);
static bool parse_trailing_redirections(parser_t *parser,
                                        node_t *compound_node);

// Forward declarations for extended language features (Phase 1)
static node_t *parse_arithmetic_command(parser_t *parser);
static node_t *parse_array_literal(parser_t *parser);

// Forward declarations for extended language features (Phase 2)
static node_t *parse_extended_test(parser_t *parser);

// Forward declarations for extended language features (Phase 3)
static node_t *parse_process_substitution(parser_t *parser);

// Forward declarations for extended language features (Phase 5)
static node_t *parse_select_statement(parser_t *parser);
static node_t *parse_time_command(parser_t *parser);
static node_t *parse_coproc(parser_t *parser);

// Forward declarations for extended language features (Phase 7: Zsh)
static node_t *parse_anonymous_function(parser_t *parser);

// Forward declarations for POSIX compliance
bool is_posix_mode_enabled(void);
static char *collect_heredoc_content(parser_t *parser, const char *delimiter,
                                     bool strip_tabs, bool expand_variables,
                                     source_location_t op_loc);
static void set_parser_error(parser_t *parser, const char *message);
static bool expect_token(parser_t *parser, token_type_t expected);

/**
 * @brief Create a new parser instance
 *
 * Allocates and initializes a parser with a tokenizer for the
 * given input string.
 *
 * @param input Shell command string to parse
 * @return New parser instance, or NULL on failure
 */
parser_t *parser_new(const char *input) {
    return parser_new_with_source(input, "<stdin>", 1);
}

parser_t *parser_new_with_source(const char *input, const char *source_name,
                                 size_t starting_line) {
    if (!input) {
        return NULL;
    }

    parser_t *parser = malloc(sizeof(parser_t));
    if (!parser) {
        return NULL;
    }

    parser->tokenizer = tokenizer_new_at(input, starting_line);
    if (!parser->tokenizer) {
        free(parser);
        return NULL;
    }

    parser->error_message = NULL;
    parser->has_error = false;

    /* Initialize structured error collection */
    parser->source_name = source_name ? source_name : "<stdin>";
    parser->error_collector =
        shell_error_collector_new(input, strlen(input), parser->source_name, 0);

    /* Initialize parser context stack */
    parser->context_depth = 0;
    for (size_t i = 0; i < PARSER_CONTEXT_MAX; i++) {
        parser->context_stack[i] = NULL;
    }

    /* Initialize recursion depth tracking */
    parser->recursion_depth = 0;

    return parser;
}

void parser_set_source_name(parser_t *parser, const char *source_name) {
    if (!parser) {
        return;
    }
    parser->source_name = source_name ? source_name : "<stdin>";

    /* Update error collector's source_name too if it exists */
    if (parser->error_collector) {
        parser->error_collector->source_name = parser->source_name;
    }
}

/**
 * @brief Free a parser instance
 *
 * Frees the parser and its associated tokenizer.
 *
 * @param parser Parser to free
 */
void parser_free(parser_t *parser) {
    if (!parser) {
        return;
    }

    tokenizer_free(parser->tokenizer);
    shell_error_collector_free(parser->error_collector);
    free(parser);
}

/**
 * @brief Check if parser has encountered an error
 *
 * @param parser Parser instance
 * @return true if an error has occurred
 */
bool parser_has_error(parser_t *parser) { return parser && parser->has_error; }

/**
 * @brief Get the parser error message
 *
 * @param parser Parser instance
 * @return Error message string, or "Invalid parser" if NULL
 */
const char *parser_error(parser_t *parser) {
    return parser ? parser->error_message : "Invalid parser";
}

/**
 * @brief Set parser error state with message
 *
 * @param parser Parser instance
 * @param message Error message describing the problem
 */
static void set_parser_error(parser_t *parser, const char *message) {
    if (parser) {
        parser->error_message = message;
        parser->has_error = true;
    }
}

/* ============================================================================
 * Structured Error Collection (Phase 2)
 * ============================================================================
 */

/**
 * @brief Convert a token to a source location
 *
 * @param token Token to extract location from
 * @param filename Source filename (or NULL for "<stdin>")
 * @return Source location structure
 */
source_location_t token_to_source_location(token_t *token,
                                           const char *filename) {
    if (!token) {
        return SOURCE_LOC_UNKNOWN;
    }

    return (source_location_t){.filename = filename ? filename : "<stdin>",
                               .line = token->line,
                               .column = token->column,
                               .offset = token->position,
                               .length = token->length};
}

/**
 * @brief Add a structured error to the parser's error collector
 *
 * @param parser Parser context
 * @param code Error code
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void parser_error_add(parser_t *parser, shell_error_code_t code,
                      const char *fmt, ...) {
    if (!parser) {
        return;
    }

    /* Get current token for location */
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    /* Create the error */
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        /* Fallback to legacy error system */
        set_parser_error(parser, "parse error");
        return;
    }

    /* Try to get source line for context display */
    if (parser->error_collector && loc.line > 0) {
        char *source_line =
            shell_error_collector_get_line(parser->error_collector, loc.line);
        if (source_line) {
            shell_error_set_source_line(
                error, source_line, loc.column > 0 ? loc.column - 1 : 0,
                loc.column > 0 ? loc.column - 1 + loc.length : loc.length);
            free(source_line);
        }
    }

    /* Add to collector if available */
    if (parser->error_collector) {
        shell_error_collector_add(parser->error_collector, error);
    } else {
        /* Fallback: just set legacy error and free */
        set_parser_error(parser,
                         error->message ? error->message : "parse error");
        shell_error_free(error);
    }

    /* Also set legacy error flag for compatibility */
    parser->has_error = true;
}

/**
 * @brief Display all collected parser errors
 *
 * @param parser Parser context
 * @param out Output stream
 * @param use_color Whether to use ANSI colors
 */
void parser_display_errors(parser_t *parser, FILE *out, bool use_color) {
    if (!parser || !parser->error_collector) {
        /* Fallback to legacy error display */
        if (parser && parser->error_message) {
            fprintf(out, "lush: %s\n", parser->error_message);
        }
        return;
    }

    shell_error_display_all(parser->error_collector, out, use_color);
}

/**
 * @brief Get the error collector from parser
 *
 * @param parser Parser context
 * @return Error collector or NULL
 */
shell_error_collector_t *parser_get_error_collector(parser_t *parser) {
    return parser ? parser->error_collector : NULL;
}

/* ============================================================================
 * Parser Context Stack
 * ============================================================================
 */

void parser_push_context(parser_t *parser, const char *context) {
    if (!parser || !context) {
        return;
    }
    if (parser->context_depth < PARSER_CONTEXT_MAX) {
        parser->context_stack[parser->context_depth++] = context;
    }
}

void parser_pop_context(parser_t *parser) {
    if (!parser || parser->context_depth == 0) {
        return;
    }
    parser->context_depth--;
    parser->context_stack[parser->context_depth] = NULL;
}

/* ============================================================================
 * Recursion Depth Tracking (Stack Overflow Protection)
 * ============================================================================
 */

/**
 * @brief Enter a recursive parsing operation
 *
 * Increments the recursion depth counter and checks against the maximum
 * allowed depth. If the limit is exceeded, sets a parser error and returns
 * false. The caller should abort parsing if this returns false.
 *
 * @param parser Parser context
 * @return true if depth is within limits, false if limit exceeded
 */
bool parser_enter_recursion(parser_t *parser) {
    if (!parser) {
        return false;
    }

    parser->recursion_depth++;

    if (parser->recursion_depth > PARSER_MAX_RECURSION_DEPTH) {
        parser_error_add_with_help(
            parser, SHELL_ERR_RESOURCE_LIMIT,
            "reduce nesting depth or simplify the script",
            "maximum parsing depth exceeded (%zu levels) - "
            "possible stack overflow attack or excessively nested code",
            PARSER_MAX_RECURSION_DEPTH);
        return false;
    }

    return true;
}

/**
 * @brief Exit a recursive parsing operation
 *
 * Decrements the recursion depth counter. Should be called when returning
 * from a recursive parsing function, regardless of success or failure.
 * Uses an assertion to catch underflow during development.
 *
 * @param parser Parser context
 */
void parser_exit_recursion(parser_t *parser) {
    if (!parser) {
        return;
    }

    /* Assert that we're not underflowing - indicates mismatched enter/exit */
    if (parser->recursion_depth == 0) {
        /* In debug builds this would be an assertion failure.
         * In release, we just prevent underflow. */
#ifndef NDEBUG
        fprintf(stderr, "PARSER BUG: recursion depth underflow\n");
#endif
        return;
    }

    parser->recursion_depth--;
}

/**
 * @brief Get current recursion depth
 *
 * Useful for debugging and testing.
 *
 * @param parser Parser context
 * @return Current recursion depth, or 0 if parser is NULL
 */
size_t parser_get_recursion_depth(parser_t *parser) {
    return parser ? parser->recursion_depth : 0;
}

/* Parser loop-progress guard.
 *
 * Defense against parser-loop livelock: every body-parsing loop must
 * consume tokens or it will spin forever, allocating per iteration. See
 * #82 for the original incident -- a 1.6 GB / 89% CPU interactive shell
 * stuck for 3 days inside parse_case_statement, with 100% of CPU samples
 * concentrated on one while-condition. The triggering input was lost
 * (typed interactively into a now-dead PID), so static analysis could
 * not pinpoint the offending no-advance path. This guard is the
 * defensive measure the issue itself recommended: track the start
 * position of the current token across iterations and abort with a
 * structured error if it does not advance for N consecutive iterations.
 *
 * The bound is small because every parse-body iteration MUST consume
 * at least one token; even one no-advance iteration is a bug. We allow
 * a small handful in case of edge-case interactions, but anything past
 * that is a livelock and we fail loudly rather than burn memory.
 */
#define PARSER_LOOP_MAX_NO_PROGRESS 16

typedef struct parser_loop_guard {
    size_t last_pos;    /* current token's start position last iteration */
    size_t no_progress; /* iterations seen at the same position */
} parser_loop_guard_t;

#define PARSER_LOOP_GUARD_INIT {.last_pos = SIZE_MAX, .no_progress = 0}

/* Check whether the parse loop is making forward progress.
 * Returns true if the loop should continue, false if the guard has
 * detected livelock (in which case a structured error has been added
 * to the parser and the caller must clean up and return NULL).
 *
 * @param parser    Parser context
 * @param guard     Per-loop guard state (stack-allocated)
 * @param loop_name Human-readable loop identifier for the error message
 */
static bool parser_loop_check_progress(parser_t *parser,
                                       parser_loop_guard_t *guard,
                                       const char *loop_name) {
    if (!parser || !parser->tokenizer || !guard) {
        return false;
    }
    size_t pos = parser->tokenizer->current
                     ? parser->tokenizer->current->position
                     : parser->tokenizer->position;
    if (pos == guard->last_pos) {
        guard->no_progress++;
        if (guard->no_progress > PARSER_LOOP_MAX_NO_PROGRESS) {
            parser_error_add_with_help(
                parser, SHELL_ERR_RESOURCE_LIMIT,
                "this is a parser bug -- please report with a reproducer",
                "parser loop '%s' made no progress for %d iterations "
                "(stuck on token at position %zu)",
                loop_name, PARSER_LOOP_MAX_NO_PROGRESS, pos);
            return false;
        }
    } else {
        guard->last_pos = pos;
        guard->no_progress = 0;
    }
    return true;
}

/**
 * @brief Add a structured error with context and help hint
 */
void parser_error_add_with_help_at(parser_t *parser, shell_error_code_t code,
                                   source_location_t loc, const char *help,
                                   const char *fmt, ...) {
    if (!parser) {
        return;
    }

    /* Create the error at the caller-supplied location */
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        /* Fallback to legacy error system */
        set_parser_error(parser, "parse error");
        return;
    }

    /* Try to get source line for context display */
    if (parser->error_collector && loc.line > 0) {
        char *source_line =
            shell_error_collector_get_line(parser->error_collector, loc.line);
        if (source_line) {
            shell_error_set_source_line(
                error, source_line, loc.column > 0 ? loc.column - 1 : 0,
                loc.column > 0 ? loc.column - 1 + loc.length : loc.length);
            free(source_line);
        }
    }

    /* Add parser context stack to error */
    for (size_t i = 0; i < parser->context_depth; i++) {
        shell_error_push_context(error, "%s", parser->context_stack[i]);
    }

    /* Add help suggestion if provided */
    if (help) {
        shell_error_set_suggestion(error, help);
    }

    /* Add to collector if available */
    if (parser->error_collector) {
        shell_error_collector_add(parser->error_collector, error);
    } else {
        /* Fallback: just set legacy error and free */
        set_parser_error(parser,
                         error->message ? error->message : "parse error");
        shell_error_free(error);
    }

    /* Also set legacy error flag for compatibility */
    parser->has_error = true;
}

void parser_error_add_with_help(parser_t *parser, shell_error_code_t code,
                                const char *help, const char *fmt, ...) {
    if (!parser) {
        return;
    }

    /* Default location: current token's position. Delegate the rest
     * to the _at variant so the two paths share one implementation. */
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    va_list args;
    va_start(args, fmt);
    /* Convert variadic args into a fixed string then forward — the
     * _at variant takes its own variadic. shell_error already handles
     * vsnprintf internally; here we just need to hand off the formatted
     * message. Simplest: format here, pass as a literal "%s" + buffer. */
    char message_buf[1024];
    vsnprintf(message_buf, sizeof(message_buf), fmt, args);
    va_end(args);

    parser_error_add_with_help_at(parser, code, loc, help, "%s", message_buf);
}

/**
 * @brief Expect and consume a specific token type
 *
 * Sets a parser error if the current token doesn't match expected type.
 *
 * @param parser Parser instance
 * @param expected Token type to match
 * @return true if token matched and was consumed
 */
static bool expect_token(parser_t *parser, token_type_t expected) {
    if (!tokenizer_match(parser->tokenizer, expected)) {
        token_t *current = tokenizer_current(parser->tokenizer);
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected '%s', got '%s'", token_type_name(expected),
                         current ? token_type_name(current->type) : "EOF");
        return false;
    }
    tokenizer_advance(parser->tokenizer);
    return true;
}

/**
 * @brief Expect and consume a specific token type with help hint
 *
 * Like expect_token but includes context and help message in error.
 *
 * @param parser Parser instance
 * @param expected Token type to match
 * @param help Help message for error (can be NULL)
 * @return true if token matched and was consumed
 */
static bool expect_token_with_help(parser_t *parser, token_type_t expected,
                                   const char *help) {
    if (!tokenizer_match(parser->tokenizer, expected)) {
        token_t *current = tokenizer_current(parser->tokenizer);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN, help, "expected '%s', got '%s'",
            token_type_name(expected),
            current ? token_type_name(current->type) : "EOF");
        return false;
    }
    tokenizer_advance(parser->tokenizer);
    return true;
}

/**
 * @brief Main parsing entry point
 *
 * Parses the entire input and returns an AST. Skips leading
 * whitespace, comments, and newlines.
 *
 * @param parser Parser instance
 * @return Root AST node, or NULL on empty/error
 */
node_t *parser_parse(parser_t *parser) {
    if (!parser) {
        return NULL;
    }

    // Skip initial whitespace and comments
    while (tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
           tokenizer_match(parser->tokenizer, TOK_COMMENT) ||
           tokenizer_match(parser->tokenizer, TOK_NEWLINE)) {
        tokenizer_advance(parser->tokenizer);
    }

    if (tokenizer_match(parser->tokenizer, TOK_EOF)) {
        return NULL; // Empty input
    }

    return parse_command_list(parser);
}

/**
 * @brief Parse a command line
 *
 * Entry point for parsing a sequence of commands.
 *
 * @param parser Parser instance
 * @return AST for the command line
 */
node_t *parser_parse_command_line(parser_t *parser) {
    return parse_command_list(parser);
}

/**
 * @brief Skip command separators
 *
 * Advances past semicolons, newlines, whitespace, and comments.
 *
 * @param parser Parser instance
 */
static void skip_separators(parser_t *parser) {
    while (tokenizer_match(parser->tokenizer, TOK_SEMICOLON) ||
           tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
           tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
           tokenizer_match(parser->tokenizer, TOK_COMMENT)) {
        tokenizer_advance(parser->tokenizer);
    }
}

/**
 * @brief Parse command body until terminator
 *
 * Parses multiple commands for control structure bodies (while, for, etc.)
 * until the specified terminator token is reached.
 *
 * @param parser Parser instance
 * @param terminator Token type that ends the body (e.g., TOK_DONE)
 * @return First command in linked list, or NULL on error
 */
static node_t *parse_command_body(parser_t *parser, token_type_t terminator) {
    node_t *first_command = NULL;
    node_t *current = NULL;
    parser_loop_guard_t guard = PARSER_LOOP_GUARD_INIT;

    while (!tokenizer_match(parser->tokenizer, terminator) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        if (!parser_loop_check_progress(parser, &guard, "parse_command_body")) {
            free_node_tree(first_command);
            return NULL;
        }

        // Skip separators between commands
        skip_separators(parser);

        // Check again for terminator after skipping separators
        if (tokenizer_match(parser->tokenizer, terminator) ||
            tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(first_command);
            return NULL;
        }

        if (!first_command) {
            first_command = command;
            current = command;
        } else {
            current->next_sibling = command;
            current = command;
        }
    }

    return first_command;
}

/**
 * @brief Parse if statement body
 *
 * Parses commands for if/elif bodies, stopping at else, elif, or fi.
 * Returns a NODE_COMMAND_LIST containing all commands.
 *
 * @param parser Parser instance
 * @return Command list node, or NULL on error
 */
static node_t *parse_if_body(parser_t *parser) {
    // Create a command list node to hold all commands
    node_t *command_list = new_node(NODE_COMMAND_LIST);
    if (!command_list) {
        return NULL;
    }

    parser_loop_guard_t guard = PARSER_LOOP_GUARD_INIT;
    while (!tokenizer_match(parser->tokenizer, TOK_ELSE) &&
           !tokenizer_match(parser->tokenizer, TOK_ELIF) &&
           !tokenizer_match(parser->tokenizer, TOK_FI) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        if (!parser_loop_check_progress(parser, &guard, "parse_if_body")) {
            free_node_tree(command_list);
            return NULL;
        }

        // Skip separators between commands
        skip_separators(parser);

        // Check again for terminators after skipping separators
        if (tokenizer_match(parser->tokenizer, TOK_ELSE) ||
            tokenizer_match(parser->tokenizer, TOK_ELIF) ||
            tokenizer_match(parser->tokenizer, TOK_FI) ||
            tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(command_list);
            return NULL;
        }

        // Add command as child of the command list
        add_child_node(command_list, command);

        // Skip separators after command
        skip_separators(parser);
    }

    return command_list;
}

/**
 * @brief Parse logical operators (&& and ||)
 *
 * Handles the and_or grammar level, creating NODE_LOGICAL_AND
 * or NODE_LOGICAL_OR nodes for compound commands.
 *
 * @param parser Parser instance
 * @return AST node for logical expression
 */
static node_t *parse_logical_expression(parser_t *parser) {
    /* Track recursion depth for stack overflow protection */
    if (!parser_enter_recursion(parser)) {
        return NULL;
    }

    node_t *left = parse_pipeline(parser);
    if (!left) {
        parser_exit_recursion(parser);
        return NULL;
    }

    while (tokenizer_match(parser->tokenizer, TOK_LOGICAL_AND) ||
           tokenizer_match(parser->tokenizer, TOK_LOGICAL_OR)) {

        token_type_t op_type = tokenizer_current(parser->tokenizer)->type;
        tokenizer_advance(parser->tokenizer); // consume operator

        // Skip whitespace after operator
        skip_separators(parser);

        node_t *right = parse_pipeline(parser);
        if (!right) {
            free_node_tree(left);
            parser_exit_recursion(parser);
            return NULL;
        }

        // Create logical operator node
        node_t *logical_node = new_node(
            op_type == TOK_LOGICAL_AND ? NODE_LOGICAL_AND : NODE_LOGICAL_OR);
        if (!logical_node) {
            free_node_tree(left);
            free_node_tree(right);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(logical_node, left);
        add_child_node(logical_node, right);
        left = logical_node;
    }

    parser_exit_recursion(parser);
    return left;
}

/**
 * @brief Parse a list of commands
 *
 * Parses commands separated by semicolons or newlines into
 * a sibling chain of AST nodes.
 *
 * @param parser Parser instance
 * @return First command in sibling chain, or NULL on error
 */
static node_t *parse_command_list(parser_t *parser) {
    node_t *first_command = NULL;
    node_t *current = NULL;

    parser_loop_guard_t guard = PARSER_LOOP_GUARD_INIT;
    while (!tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {
        if (!parser_loop_check_progress(parser, &guard, "parse_command_list")) {
            free_node_tree(first_command);
            return NULL;
        }

        // Skip separators, newlines, and comments
        while (tokenizer_match(parser->tokenizer, TOK_SEMICOLON) ||
               tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_COMMENT)) {
            tokenizer_advance(parser->tokenizer);
        }

        if (tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(first_command);
            return NULL;
        }

        if (!first_command) {
            first_command = command;
            current = command;
        } else {
            current->next_sibling = command;
            current = command;
        }

        // Check for end of command list
        if (tokenizer_match(parser->tokenizer, TOK_EOF) ||
            tokenizer_match(parser->tokenizer, TOK_DONE) ||
            tokenizer_match(parser->tokenizer, TOK_FI) ||
            tokenizer_match(parser->tokenizer, TOK_ELSE) ||
            tokenizer_match(parser->tokenizer, TOK_ELIF)) {
            break;
        }
    }

    return first_command;
}

/**
 * @brief Parse a pipeline
 *
 * Parses commands connected by | operators, creating NODE_PIPE
 * nodes. Also handles background execution (&).
 *
 * @param parser Parser instance
 * @return Pipeline AST node
 */
static node_t *parse_pipeline(parser_t *parser) {
    /* Track recursion depth for stack overflow protection */
    if (!parser_enter_recursion(parser)) {
        return NULL;
    }

    // Check for negation prefix (! pipeline)
    bool negate = false;
    token_t *current = tokenizer_current(parser->tokenizer);
    if (current && current->type == TOK_WORD && current->length == 1 &&
        current->text[0] == '!') {
        negate = true;
        tokenizer_advance(parser->tokenizer); // consume !
        // Skip whitespace after !
        while (tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }
    }

    node_t *left = parse_simple_command(parser);
    if (!left) {
        parser_exit_recursion(parser);
        return NULL;
    }

    if (tokenizer_match(parser->tokenizer, TOK_PIPE) ||
        tokenizer_match(parser->tokenizer, TOK_PIPE_STDERR)) {
        // Check if this is |& (pipe stderr too)
        bool pipe_stderr = tokenizer_match(parser->tokenizer, TOK_PIPE_STDERR);
        tokenizer_advance(parser->tokenizer); // consume | or |&

        // Skip newlines after pipe - allows multiline pipelines
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }

        node_t *right = parse_pipeline(parser);
        if (!right) {
            free_node_tree(left);
            parser_exit_recursion(parser);
            return NULL;
        }

        node_t *pipe_node = new_node(NODE_PIPE);
        if (!pipe_node) {
            free_node_tree(left);
            free_node_tree(right);
            parser_exit_recursion(parser);
            return NULL;
        }

        // Use val.sint to indicate if stderr should also be piped
        // 0 = normal pipe (stdout only), 1 = |& (stdout + stderr)
        pipe_node->val.sint = pipe_stderr ? 1 : 0;
        pipe_node->val_type = VAL_SINT;

        add_child_node(pipe_node, left);
        add_child_node(pipe_node, right);
        left = pipe_node;
    }

    // Check for background execution
    if (tokenizer_match(parser->tokenizer, TOK_AND)) {
        tokenizer_advance(parser->tokenizer); // consume &

        node_t *background_node = new_node(NODE_BACKGROUND);
        if (!background_node) {
            free_node_tree(left);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(background_node, left);
        left = background_node;
    }

    // Wrap in negation node if ! prefix was present
    if (negate) {
        node_t *negate_node = new_node(NODE_NEGATE);
        if (!negate_node) {
            free_node_tree(left);
            parser_exit_recursion(parser);
            return NULL;
        }
        add_child_node(negate_node, left);
        parser_exit_recursion(parser);
        return negate_node;
    }

    parser_exit_recursion(parser);
    return left;
}

/**
 * @brief Strip POSIX-unquoted backslash escapes from a word token's text.
 *
 * The tokenizer's word-context scanner keeps `\X` pairs in the token text
 * verbatim — escape interpretation is deferred. For tokens that did NOT
 * come from a quoted run, POSIX quote-removal says any `\X` (other than
 * `\<newline>`, which the tokenizer already eats as line continuation)
 * collapses to a literal X. Quote-removal happens after parameter
 * expansion in POSIX, but for backslashes that originated outside any
 * quote in the shell source we can collapse them here at parse time —
 * those backslashes never ride along inside variable values, so doing
 * it now does not corrupt later expansion. Tokens from `"..."` are NOT
 * touched: their backslashes (kept as `\$`, `\\`, etc. by the
 * double-quote scanner) follow double-quote escape rules and are
 * resolved later by the executor's expand_quoted_string.
 *
 * Returns a freshly malloc'd string the caller owns.
 */
static char *posix_unquoted_dequote(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\\' && i + 1 < len) {
            /* `\<newline>` was already removed by the tokenizer's word
             * scanner; no special case needed here. Any other `\X`
             * collapses to a literal X. */
            out[w++] = text[i + 1];
            i++;
            continue;
        }
        out[w++] = text[i];
    }
    out[w] = '\0';
    return out;
}

/**
 * @brief Try to consume one shell-style word argument from the current token.
 *
 * Tests whether the current token is "argument-like" (any of TOK_STRING,
 * TOK_EXPANDABLE_STRING, TOK_ARITH_EXP, TOK_COMMAND_SUB, TOK_BACKQUOTE,
 * word-like tokens, keyword tokens, TOK_VARIABLE, TOK_RBRACKET,
 * TOK_ASSIGN, TOK_GLOB, TOK_QUESTION, TOK_NOT_EQUAL). If so, runs the
 * adjacent-token concatenation loop (consecutive tokens with no
 * intervening whitespace fold into one logical argument), classifies
 * the result into the appropriate node type (NODE_STRING_LITERAL for
 * single-quoted, NODE_STRING_EXPANDABLE for double-quoted or any
 * multi-token concatenation, NODE_ARITH_EXP for arithmetic expansion,
 * NODE_COMMAND_SUB for $(...) / `...`, NODE_VAR otherwise), and adds
 * the resulting child node to `parent`. Advances the tokenizer past
 * every consumed token.
 *
 * On allocation failure, sets parser->has_error and returns false; the
 * caller is responsible for cleaning up `parent` and propagating.
 *
 * Used by parse_simple_command (regular command arguments) and
 * parse_anonymous_function (trailing positional args after `() { body }`).
 * Single source of truth for argument-collection semantics across the
 * parser; both call sites get identical acceptance, concatenation, and
 * classification behavior.
 *
 * @param parser Parser instance
 * @param parent Node to append the collected argument as a child of
 * @return true if an argument was consumed and added (caller can loop);
 *         false if the current token is not argument-like (caller stops)
 *         or allocation failed (parser->has_error is set)
 */
static bool collect_word_argument(parser_t *parser, node_t *parent) {
    token_t *arg_token = tokenizer_current(parser->tokenizer);
    if (!arg_token) {
        return false;
    }

    /* Acceptance test — same set parse_simple_command has used since the
     * unified-concatenation logic was introduced. */
    bool accepted =
        (arg_token->type == TOK_STRING ||
         arg_token->type == TOK_EXPANDABLE_STRING ||
         arg_token->type == TOK_ARITH_EXP ||
         arg_token->type == TOK_COMMAND_SUB ||
         arg_token->type == TOK_BACKQUOTE ||
         token_is_word_like(arg_token->type) ||
         token_is_keyword(arg_token->type) || arg_token->type == TOK_VARIABLE ||
         arg_token->type == TOK_RBRACKET || arg_token->type == TOK_ASSIGN ||
         arg_token->type == TOK_GLOB || arg_token->type == TOK_QUESTION ||
         arg_token->type == TOK_NOT_EQUAL);
    if (!accepted) {
        return false;
    }

    /* Adjacency-collection: gather consecutive arg-like tokens that have
     * no whitespace between them. `pre$VAR` becomes one logical arg
     * instead of two. */
    typedef struct {
        token_type_t type;
        char *text;
    } token_info_t;

    token_info_t *collected_tokens = NULL;
    int token_count = 0;
    size_t last_end_pos = arg_token->position + strlen(arg_token->text);

    while (arg_token &&
           (arg_token->type == TOK_STRING ||
            arg_token->type == TOK_EXPANDABLE_STRING ||
            arg_token->type == TOK_ARITH_EXP ||
            arg_token->type == TOK_COMMAND_SUB ||
            arg_token->type == TOK_BACKQUOTE ||
            token_is_word_like(arg_token->type) ||
            token_is_keyword(arg_token->type) ||
            arg_token->type == TOK_VARIABLE ||
            arg_token->type == TOK_RBRACKET || arg_token->type == TOK_ASSIGN ||
            arg_token->type == TOK_GLOB || arg_token->type == TOK_QUESTION ||
            arg_token->type == TOK_NOT_EQUAL)) {

        token_info_t *new_tokens =
            realloc(collected_tokens, (token_count + 1) * sizeof(token_info_t));
        if (!new_tokens) {
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text);
            }
            free(collected_tokens);
            parser->has_error = true;
            return false;
        }
        collected_tokens = new_tokens;

        collected_tokens[token_count].type = arg_token->type;
        collected_tokens[token_count].text = strdup(arg_token->text);
        token_count++;

        last_end_pos = arg_token->position + strlen(arg_token->text);
        tokenizer_advance(parser->tokenizer);
        token_t *next_token = tokenizer_current(parser->tokenizer);

        /* Stop collecting if next token has whitespace before it. */
        if (next_token && next_token->position != last_end_pos) {
            break;
        }
        arg_token = next_token;
    }

    /* Build a single arg node from collected tokens. */
    if (token_count == 1) {
        node_t *arg_node = NULL;
        switch (collected_tokens[0].type) {
        case TOK_STRING:
            arg_node = new_node(NODE_STRING_LITERAL);
            break;
        case TOK_EXPANDABLE_STRING:
            arg_node = new_node(NODE_STRING_EXPANDABLE);
            break;
        case TOK_ARITH_EXP:
            arg_node = new_node(NODE_ARITH_EXP);
            break;
        case TOK_COMMAND_SUB:
        case TOK_BACKQUOTE:
            arg_node = new_node(NODE_COMMAND_SUB);
            break;
        default:
            arg_node = new_node(NODE_VAR);
            break;
        }
        if (arg_node) {
            arg_node->val.str = strdup(collected_tokens[0].text);
            arg_node->val_type = VAL_STR;
            add_child_node(parent, arg_node);
        }
    } else if (token_count > 1) {
        /* Multi-token concatenation: build a single NODE_STRING_EXPANDABLE
         * with the concatenated text (matches the existing semantics in
         * parse_simple_command).
         *
         * Word-context tokens (TOK_WORD, keywords-as-words, etc.) carry
         * raw `\X` pairs because the tokenizer defers escape resolution.
         * For unquoted shell input, POSIX-unquoted quote-removal collapses
         * any such `\X` to literal X. Doing that here, before
         * concatenation, has two effects: (1) the `$DIR/a\ b` family of
         * inputs no longer ships backslashes through to argv, fixing #90
         * for the multi-token case; (2) any backslashes that survive into
         * the concatenated string came from a quoted segment
         * (TOK_EXPANDABLE_STRING kept its `\X` for the four DQ-meaningful
         * escapes), so the executor can apply double-quote rules safely
         * in `expand_quoted_string` without a quote-context flag.
         *
         * Tokens that already had escape semantics applied (or that are
         * sub-expressions evaluated later) pass through untouched:
         *   TOK_STRING               — single-quoted, no escapes ever
         *   TOK_EXPANDABLE_STRING    — double-quoted, DQ rules later
         *   TOK_ARITH_EXP            — `$((...))`, evaluated as arith
         *   TOK_COMMAND_SUB          — `$(...)`, evaluated as cmd
         *   TOK_BACKQUOTE            — `` `...` ``, evaluated as cmd
         *   TOK_VARIABLE             — `$VAR`, no escapes by construction
         */
        char **dequoted = malloc((size_t)token_count * sizeof(char *));
        if (!dequoted) {
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text);
            }
            free(collected_tokens);
            parser->has_error = true;
            return false;
        }
        size_t total_len = 0;
        bool ok = true;
        for (int i = 0; i < token_count; i++) {
            switch (collected_tokens[i].type) {
            case TOK_STRING:
            case TOK_EXPANDABLE_STRING:
            case TOK_ARITH_EXP:
            case TOK_COMMAND_SUB:
            case TOK_BACKQUOTE:
            case TOK_VARIABLE:
                dequoted[i] = strdup(collected_tokens[i].text);
                break;
            default:
                /* Word-like tokens: word-context backslashes collapse. */
                dequoted[i] = posix_unquoted_dequote(collected_tokens[i].text);
                break;
            }
            if (!dequoted[i]) {
                ok = false;
                break;
            }
            total_len += strlen(dequoted[i]);
        }
        if (!ok) {
            for (int i = 0; i < token_count; i++) {
                free(dequoted[i]);
            }
            free(dequoted);
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text);
            }
            free(collected_tokens);
            parser->has_error = true;
            return false;
        }
        char *concatenated = malloc(total_len + 1);
        if (concatenated) {
            concatenated[0] = '\0';
            for (int i = 0; i < token_count; i++) {
                strcat(concatenated, dequoted[i]);
            }
            node_t *arg_node = new_node(NODE_STRING_EXPANDABLE);
            if (arg_node) {
                arg_node->val.str = concatenated;
                arg_node->val_type = VAL_STR;
                add_child_node(parent, arg_node);
            } else {
                free(concatenated);
            }
        }
        for (int i = 0; i < token_count; i++) {
            free(dequoted[i]);
        }
        free(dequoted);
    }

    for (int i = 0; i < token_count; i++) {
        free(collected_tokens[i].text);
    }
    free(collected_tokens);
    return true;
}

/**
 * @brief Parse a simple command or control structure
 *
 * Dispatches to appropriate parser based on current token:
 * - Brace groups, subshells
 * - Control structures (if, while, for, case, function)
 * - Variable assignments
 * - Regular commands with arguments and redirections
 *
 * @param parser Parser instance
 * @return Command AST node
 */
static node_t *parse_simple_command(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current) {
        return NULL;
    }

    // Check for brace group
    if (current->type == TOK_LBRACE) {
        return parse_brace_group(parser);
    }

    // Check for arithmetic command (( expr ))
    if (current->type == TOK_DOUBLE_LPAREN &&
        shell_mode_allows(FEATURE_ARITH_COMMAND)) {
        return parse_arithmetic_command(parser);
    }

    // Check for extended test [[ expr ]]
    if (current->type == TOK_DOUBLE_LBRACKET &&
        shell_mode_allows(FEATURE_EXTENDED_TEST)) {
        return parse_extended_test(parser);
    }

    // Check for anonymous function () { body } or subshell
    if (current->type == TOK_LPAREN) {
        // Peek ahead to check for anonymous function syntax: () { ... }
        if (shell_mode_allows(FEATURE_ANONYMOUS_FUNCTIONS)) {
            token_t *next = tokenizer_peek(parser->tokenizer);
            if (next && next->type == TOK_RPAREN) {
                // Save position before advancing to check third token
                size_t saved_pos = current->position;
                size_t saved_line = parser->tokenizer->line;
                size_t saved_col = parser->tokenizer->column;

                tokenizer_advance(parser->tokenizer); // consume (
                tokenizer_advance(parser->tokenizer); // consume )
                token_t *after_paren = tokenizer_current(parser->tokenizer);

                if (after_paren && after_paren->type == TOK_LBRACE) {
                    // This is an anonymous function () { body }
                    return parse_anonymous_function(parser);
                }

                // Not anonymous function - restore tokenizer state
                parser->tokenizer->position = saved_pos;
                parser->tokenizer->line = saved_line;
                parser->tokenizer->column = saved_col;
                tokenizer_refresh_from_position(parser->tokenizer);
                current = tokenizer_current(parser->tokenizer);
            }
        }
        // Regular subshell
        return parse_subshell(parser);
    }

    // Check for control structures
    if (token_is_keyword(current->type)) {
        if (getenv("NEW_PARSER_DEBUG")) {
            printf("DEBUG: Found keyword token type %d (%s)\n", current->type,
                   token_type_name(current->type));
        }
        switch (current->type) {
        case TOK_IF:
            return parse_if_statement(parser);
        case TOK_WHILE:
            return parse_while_statement(parser);
        case TOK_UNTIL:
            return parse_until_statement(parser);
        case TOK_FOR:
            return parse_for_statement(parser);
        case TOK_CASE:
            return parse_case_statement(parser);
        case TOK_FUNCTION:
            return parse_function_definition(parser);
        case TOK_SELECT:
            return parse_select_statement(parser);
        case TOK_TIME:
            return parse_time_command(parser);
        case TOK_COPROC:
            return parse_coproc(parser);
        default:
            // Other keywords (like ESAC, FI, DONE, etc.) are handled by their
            // parent constructs - returning NULL here lets the parent detect
            // them
            return NULL;
        }
    }

    // Check for function definition (word followed by ())
    if (token_is_word_like(current->type) && is_function_definition(parser)) {
        return parse_function_definition(parser);
    }

    // Check for assignment (word followed by =) or array assignment
    if (token_is_word_like(current->type)) {
        token_t *next = tokenizer_peek(parser->tokenizer);

        // Check for array element assignment: arr[n]=value or arr[n]+=value
        // Tokenizer produces: arr[n] (WORD) + = (ASSIGN) + value (WORD)
        // or: arr[n] (WORD) + += (PLUS_ASSIGN) + value (WORD)
        if (shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
            const char *bracket = strchr(current->text, '[');
            const char *close_bracket = bracket ? strchr(bracket, ']') : NULL;

            // Check for arr[...] followed by = or +=
            if (bracket && close_bracket && next &&
                (next->type == TOK_ASSIGN || next->type == TOK_PLUS_ASSIGN)) {
                // This is an array element assignment
                // Token format: arr[n] = value (separate tokens)

                // Extract variable name (part before [)
                size_t var_len = bracket - current->text;
                char *var_name = malloc(var_len + 1);
                if (!var_name) {
                    return NULL;
                }
                strncpy(var_name, current->text, var_len);
                var_name[var_len] = '\0';

                // Extract subscript (part between [ and ])
                size_t sub_len = close_bracket - bracket - 1;
                char *subscript = malloc(sub_len + 1);
                if (!subscript) {
                    free(var_name);
                    return NULL;
                }
                strncpy(subscript, bracket + 1, sub_len);
                subscript[sub_len] = '\0';

                // Check if += or =
                bool is_append = (next->type == TOK_PLUS_ASSIGN);

                tokenizer_advance(parser->tokenizer); // consume arr[n]
                tokenizer_advance(parser->tokenizer); // consume = or +=

                // Get value token - must copy before advancing
                token_t *value_token = tokenizer_current(parser->tokenizer);
                char *value_str = NULL;
                if (value_token &&
                    (token_is_word_like(value_token->type) ||
                     value_token->type == TOK_VARIABLE ||
                     value_token->type == TOK_STRING ||
                     value_token->type == TOK_EXPANDABLE_STRING)) {
                    value_str = strdup(value_token->text);
                    tokenizer_advance(parser->tokenizer); // consume value
                }
                if (!value_str) {
                    value_str = strdup("");
                }

                // Create array assignment node
                node_t *assign_node = new_node(NODE_ARRAY_ASSIGN);
                if (!assign_node) {
                    free(var_name);
                    free(subscript);
                    return NULL;
                }

                assign_node->val.str = var_name; // Transfer ownership
                assign_node->val_type = VAL_STR;

                // Create subscript child node
                node_t *subscript_node = new_node(NODE_VAR);
                if (!subscript_node) {
                    free(subscript);
                    free_node_tree(assign_node);
                    return NULL;
                }
                subscript_node->val.str = subscript;
                subscript_node->val_type = VAL_STR;
                add_child_node(assign_node, subscript_node);

                // Create value child node
                node_t *value_node = new_node(NODE_VAR);
                if (!value_node) {
                    free_node_tree(assign_node);
                    return NULL;
                }

                if (is_append) {
                    // Encode append with "+=" prefix
                    size_t vlen = strlen(value_str);
                    char *append_val = malloc(vlen + 3);
                    if (append_val) {
                        strcpy(append_val, "+=");
                        strcat(append_val, value_str);
                        value_node->val.str = append_val;
                        free(value_str);
                    } else {
                        value_node->val.str = value_str; // Transfer ownership
                    }
                } else {
                    value_node->val.str = value_str; // Transfer ownership
                }
                value_node->val_type = VAL_STR;
                add_child_node(assign_node, value_node);

                return assign_node;
            }
        }

        if (next &&
            (next->type == TOK_ASSIGN || next->type == TOK_PLUS_ASSIGN)) {
            bool is_append = (next->type == TOK_PLUS_ASSIGN);

            // Save variable name BEFORE advancing tokenizer
            char *var_name = strdup(current->text);
            if (!var_name) {
                return NULL;
            }

            tokenizer_advance(parser->tokenizer); // consume variable name
            tokenizer_advance(parser->tokenizer); // consume '=' or '+='

            token_t *value = tokenizer_current(parser->tokenizer);

            // Check for array literal assignment: arr=(a b c) or arr+=(a b c)
            if (value && value->type == TOK_LPAREN &&
                shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                node_t *array_node = parse_array_literal(parser);
                if (!array_node) {
                    free(var_name);
                    return NULL;
                }
                // Create array assignment or append node
                node_t *assign_node =
                    new_node(is_append ? NODE_ARRAY_APPEND : NODE_ARRAY_ASSIGN);
                if (!assign_node) {
                    free(var_name);
                    free_node_tree(array_node);
                    return NULL;
                }
                assign_node->val.str = var_name; // Transfer ownership
                assign_node->val_type = VAL_STR;
                add_child_node(assign_node, array_node);
                return assign_node;
            }

            // Regular scalar assignment: variable=value
            node_t *command = new_node(NODE_COMMAND);
            if (!command) {
                free(var_name);
                return NULL;
            }

            // Collect all consecutive value tokens (handles ${A}_${B} etc.)
            // Value tokens include words, variables, command subs, etc.
            // Stop at whitespace, semicolon, newline, or other separators
            if (value &&
                (token_is_word_like(value->type) ||
                 value->type == TOK_VARIABLE || value->type == TOK_ARITH_EXP ||
                 value->type == TOK_COMMAND_SUB ||
                 value->type == TOK_BACKQUOTE)) {

                // Build complete value by concatenating adjacent tokens
                size_t value_capacity = 256;
                size_t value_len = 0;
                char *full_value = malloc(value_capacity);
                if (!full_value) {
                    free(var_name);
                    free_node_tree(command);
                    return NULL;
                }
                full_value[0] = '\0';

                while (value && (token_is_word_like(value->type) ||
                                 value->type == TOK_VARIABLE ||
                                 value->type == TOK_ARITH_EXP ||
                                 value->type == TOK_COMMAND_SUB ||
                                 value->type == TOK_BACKQUOTE)) {

                    // Check if this word is followed by = (making it a new
                    // assignment) If so, stop - this word belongs to the next
                    // assignment
                    if (token_is_word_like(value->type)) {
                        token_t *peek = tokenizer_peek(parser->tokenizer);
                        if (peek && (peek->type == TOK_ASSIGN ||
                                     peek->type == TOK_PLUS_ASSIGN)) {
                            break; // This word starts a new assignment
                        }
                    }

                    size_t token_len = strlen(value->text);
                    // For single-quoted strings, add 2 for surrounding quotes
                    size_t extra_len = (value->type == TOK_STRING) ? 2 : 0;

                    // Grow buffer if needed
                    if (value_len + token_len + extra_len + 1 >
                        value_capacity) {
                        value_capacity =
                            (value_len + token_len + extra_len + 1) * 2;
                        char *new_value = realloc(full_value, value_capacity);
                        if (!new_value) {
                            free(full_value);
                            free(var_name);
                            free_node_tree(command);
                            return NULL;
                        }
                        full_value = new_value;
                    }

                    // Add token with appropriate quoting
                    // Only single quotes need to be preserved to prevent
                    // expansion Double quotes: expand variables but don't keep
                    // quotes
                    if (value->type == TOK_STRING) {
                        // Single-quoted: preserve quotes to prevent expansion
                        strcat(full_value, "'");
                        strcat(full_value, value->text);
                        strcat(full_value, "'");
                        value_len += token_len + 2;
                    } else {
                        // All other types: just append the text
                        strcat(full_value, value->text);
                        value_len += token_len;
                    }

                    tokenizer_advance(parser->tokenizer); // consume this token
                    value = tokenizer_current(parser->tokenizer);
                }

                // Build final assignment string: var=value or var+=value
                size_t var_len = strlen(var_name);
                char *assignment =
                    malloc(var_len + (is_append ? 2 : 1) + value_len + 1);
                if (assignment) {
                    strcpy(assignment, var_name);
                    strcat(assignment, is_append ? "+=" : "=");
                    strcat(assignment, full_value);
                    command->val.str = assignment;
                    command->val_type = VAL_STR;
                }
                free(full_value);
            } else {
                // Assignment with empty value: variable=
                size_t var_len = strlen(var_name);
                char *assignment = malloc(var_len + 2);
                if (assignment) {
                    strcpy(assignment, var_name);
                    strcat(assignment, "=");
                    command->val.str = assignment;
                    command->val_type = VAL_STR;
                }
            }

            free(var_name);
            return command;
        }
    }

    // Parse regular command
    if (current->type == TOK_ERROR) {
        // Provide specific error message based on what's unclosed
        const char *text = current->text;
        if (text && text[0] == '$' && text[1] == '(') {
            if (text[2] == '(') {
                parser_push_context(parser,
                                    "parsing arithmetic expansion $((..))");
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNCLOSED_SUBST,
                    "arithmetic expansion requires closing '))'",
                    "unterminated arithmetic expansion $((");
                parser_pop_context(parser);
            } else {
                parser_push_context(parser,
                                    "parsing command substitution $(...)");
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNCLOSED_SUBST,
                    "command substitution requires closing ')'",
                    "unterminated command substitution $(");
                parser_pop_context(parser);
            }
        } else if (text && text[0] == '`') {
            parser_push_context(parser, "parsing backtick substitution `...`");
            parser_error_add_with_help(
                parser, SHELL_ERR_UNCLOSED_SUBST,
                "backtick substitution requires closing '`'",
                "unterminated backtick command substitution");
            parser_pop_context(parser);
        } else {
            parser_push_context(parser, "parsing quoted string");
            parser_error_add_with_help(parser, SHELL_ERR_UNCLOSED_QUOTE,
                                       "strings must be properly closed",
                                       "unterminated quoted string");
            parser_pop_context(parser);
        }
        return NULL;
    }

    if (!token_is_word_like(current->type) && current->type != TOK_LBRACKET) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected command name, got '%s'",
                         current->text ? current->text
                                       : token_type_name(current->type));
        return NULL;
    }

    /* Create command node with source location from current token */
    source_location_t cmd_loc =
        token_to_source_location(current, parser->source_name);
    node_t *command = new_node_at(NODE_COMMAND, cmd_loc);
    if (!command) {
        return NULL;
    }

    // Set command name
    command->val.str = strdup(current->text);
    command->val_type = VAL_STR;
    tokenizer_advance(parser->tokenizer);

    // Parse arguments and redirections
    while (!tokenizer_match(parser->tokenizer, TOK_EOF) &&
           !tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
           !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
           !tokenizer_match(parser->tokenizer, TOK_PIPE) &&
           !tokenizer_match(parser->tokenizer, TOK_AND) &&
           !tokenizer_match(parser->tokenizer, TOK_LOGICAL_AND) &&
           !tokenizer_match(parser->tokenizer, TOK_LOGICAL_OR)) {

        token_t *arg_token = tokenizer_current(parser->tokenizer);
        if (!arg_token) {
            break;
        }

        // Check for redirection tokens
        if (arg_token->type == TOK_REDIRECT_OUT ||
            arg_token->type == TOK_REDIRECT_IN ||
            arg_token->type == TOK_APPEND || arg_token->type == TOK_HEREDOC ||
            arg_token->type == TOK_HEREDOC_STRIP ||
            arg_token->type == TOK_HERESTRING ||
            arg_token->type == TOK_REDIRECT_ERR ||
            arg_token->type == TOK_REDIRECT_IN_FD ||
            arg_token->type == TOK_REDIRECT_BOTH ||
            arg_token->type == TOK_APPEND_ERR ||
            arg_token->type == TOK_REDIRECT_FD ||
            arg_token->type == TOK_REDIRECT_FD_ALLOC ||
            arg_token->type == TOK_REDIRECT_CLOBBER ||
            arg_token->type == TOK_APPEND_BOTH) {

            node_t *redir_node = parse_redirection(parser);
            if (!redir_node) {
                free_node_tree(command);
                return NULL;
            }

            add_child_node(command, redir_node);
            continue; // Continue to check for more redirections/arguments
        }
        // Handle process substitution <(cmd) and >(cmd)
        else if (arg_token->type == TOK_PROC_SUB_IN ||
                 arg_token->type == TOK_PROC_SUB_OUT) {
            node_t *proc_sub_node = parse_process_substitution(parser);
            if (!proc_sub_node) {
                free_node_tree(command);
                return NULL;
            }
            add_child_node(command, proc_sub_node);
            continue; // Continue to check for more arguments
        }
        // Handle array literal arguments: name=(...) or name+=(...)
        // This allows declare -A map=([key]=value) to work
        else if (token_is_word_like(arg_token->type) &&
                 shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
            // Peek ahead to see if this is word followed by = or +=
            // Check if next token (lookahead) is = or +=, and the one after is
            // (
            token_t *peek1 = tokenizer_peek(parser->tokenizer);

            if (peek1 &&
                (peek1->type == TOK_ASSIGN || peek1->type == TOK_PLUS_ASSIGN)) {
                // Check if = or += is immediately adjacent (no whitespace)
                size_t word_end = arg_token->position + strlen(arg_token->text);
                if (peek1->position == word_end) {
                    // They're adjacent. Now check if ( follows the =
                    // To check this, we need to look at what comes after peek1
                    size_t assign_end = peek1->position + strlen(peek1->text);
                    // Peek at the input directly to see if ( follows
                    if (assign_end < parser->tokenizer->input_length &&
                        parser->tokenizer->input[assign_end] == '(') {
                        // This is an array literal argument: name=(...) or
                        // name+=(...)
                        char *var_name = strdup(arg_token->text);
                        bool is_append = (peek1->type == TOK_PLUS_ASSIGN);

                        tokenizer_advance(
                            parser->tokenizer); // consume var name
                        tokenizer_advance(parser->tokenizer); // consume = or +=

                        // Now parse the array literal
                        node_t *array_node = parse_array_literal(parser);
                        if (!array_node) {
                            free(var_name);
                            free_node_tree(command);
                            return NULL;
                        }

                        // Build a string representation for the argument:
                        // name=(...) The builtin will parse this
                        size_t total_len = strlen(var_name) +
                                           (is_append ? 2 : 1) +
                                           2; // name + = or += + ()

                        // Count total length of elements
                        node_t *elem = array_node->first_child;
                        while (elem) {
                            if (elem->val.str) {
                                total_len += strlen(elem->val.str) +
                                             1; // element + space
                            }
                            elem = elem->next_sibling;
                        }

                        char *arg_str = malloc(total_len + 1);
                        if (arg_str) {
                            strcpy(arg_str, var_name);
                            strcat(arg_str, is_append ? "+=(" : "=(");
                            elem = array_node->first_child;
                            bool first = true;
                            while (elem) {
                                if (elem->val.str) {
                                    if (!first)
                                        strcat(arg_str, " ");
                                    strcat(arg_str, elem->val.str);
                                    first = false;
                                }
                                elem = elem->next_sibling;
                            }
                            strcat(arg_str, ")");

                            // Use NODE_STRING_LITERAL to prevent glob/brace
                            // expansion The array literal syntax [key]=value
                            // should be passed literally
                            node_t *arg_node = new_node(NODE_STRING_LITERAL);
                            if (arg_node) {
                                arg_node->val.str = arg_str;
                                arg_node->val_type = VAL_STR;
                                add_child_node(command, arg_node);
                            } else {
                                free(arg_str);
                            }
                        }

                        free(var_name);
                        free_node_tree(array_node);
                        continue; // Skip to next argument
                    }
                }
            }
            // Not an array literal, fall through to regular argument handling
        }
        // Handle all argument tokens via the shared helper. The helper
        // tests acceptance, runs the adjacency-concatenation loop, and
        // creates the appropriately-classified node attached to command.
        if (!collect_word_argument(parser, command)) {
            if (parser->has_error) {
                free_node_tree(command);
                return NULL;
            }
            break; // Not an arg-like token; stop parsing arguments.
        }
    }

    return command;
}

/**
 * @brief Parse a brace group { commands; }
 *
 * Creates NODE_BRACE_GROUP containing the enclosed commands.
 *
 * @param parser Parser instance
 * @return Brace group AST node
 */
static node_t *parse_brace_group(parser_t *parser) {
    /* Track recursion depth for stack overflow protection */
    if (!parser_enter_recursion(parser)) {
        return NULL;
    }

    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_LBRACE) {
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "brace groups execute commands in the current shell",
            "expected '{'");
        parser_exit_recursion(parser);
        return NULL;
    }

    /* Capture location for brace group */
    source_location_t brace_loc =
        token_to_source_location(current, parser->source_name);

    /* Push context for better error messages */
    parser_push_context(parser, "parsing brace group");

    // Create brace group node
    node_t *group_node = new_node_at(NODE_BRACE_GROUP, brace_loc);
    if (!group_node) {
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    // Consume '{'
    tokenizer_advance(parser->tokenizer);

    // Skip whitespace and newlines after '{'
    skip_separators(parser);

    // Parse commands until '}'
    parser_loop_guard_t guard = PARSER_LOOP_GUARD_INIT;
    while (!tokenizer_match(parser->tokenizer, TOK_RBRACE) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        if (!parser_loop_check_progress(parser, &guard, "parse_brace_group")) {
            free_node_tree(group_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(group_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(group_node, command);

        // Skip separators between commands
        skip_separators(parser);
    }

    // Expect '}'
    if (!expect_token_with_help(parser, TOK_RBRACE,
                                "brace group must end with '}'")) {
        free_node_tree(group_node);
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: { cmd; } >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, group_node)) {
        free_node_tree(group_node);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_exit_recursion(parser);
    return group_node;
}

/**
 * @brief Parse a subshell ( commands )
 *
 * Creates NODE_SUBSHELL containing the enclosed commands.
 *
 * @param parser Parser instance
 * @return Subshell AST node
 */
static node_t *parse_subshell(parser_t *parser) {
    /* Track recursion depth for stack overflow protection */
    if (!parser_enter_recursion(parser)) {
        return NULL;
    }

    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_LPAREN) {
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "subshells execute commands in a child process", "expected '('");
        parser_exit_recursion(parser);
        return NULL;
    }

    /* Capture location for subshell */
    source_location_t subshell_loc =
        token_to_source_location(current, parser->source_name);

    /* Push context for better error messages */
    parser_push_context(parser, "parsing subshell");

    // Create subshell node
    node_t *subshell_node = new_node_at(NODE_SUBSHELL, subshell_loc);
    if (!subshell_node) {
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    // Consume '('
    tokenizer_advance(parser->tokenizer);

    // Skip whitespace and newlines after '('
    skip_separators(parser);

    // Parse commands until ')'
    parser_loop_guard_t guard = PARSER_LOOP_GUARD_INIT;
    while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        if (!parser_loop_check_progress(parser, &guard, "parse_subshell")) {
            free_node_tree(subshell_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(subshell_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(subshell_node, command);

        // Skip separators between commands
        skip_separators(parser);
    }

    // Expect ')'
    if (!expect_token_with_help(parser, TOK_RPAREN,
                                "subshell must end with ')'")) {
        free_node_tree(subshell_node);
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: (cmd) >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, subshell_node)) {
        free_node_tree(subshell_node);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_exit_recursion(parser);
    return subshell_node;
}

/**
 * @brief Check if token is a redirection operator
 *
 * @param type Token type to check
 * @return true if token is a redirection operator
 */
static bool is_redirection_token(token_type_t type) {
    return type == TOK_REDIRECT_OUT || type == TOK_REDIRECT_IN ||
           type == TOK_APPEND || type == TOK_HEREDOC ||
           type == TOK_HEREDOC_STRIP || type == TOK_HERESTRING ||
           type == TOK_REDIRECT_ERR || type == TOK_REDIRECT_IN_FD ||
           type == TOK_REDIRECT_BOTH || type == TOK_APPEND_ERR ||
           type == TOK_REDIRECT_FD || type == TOK_REDIRECT_FD_ALLOC ||
           type == TOK_REDIRECT_CLOBBER || type == TOK_APPEND_BOTH;
}

/**
 * @brief Parse trailing redirections after a compound command
 *
 * Checks for and parses any redirection operators following a compound
 * command (brace group, subshell, if, while, for, etc.) and attaches
 * them as children of the compound command node.
 *
 * @param parser Parser instance
 * @param compound_node The compound command node to attach redirections to
 * @return true on success, false on error
 */
static bool parse_trailing_redirections(parser_t *parser,
                                        node_t *compound_node) {
    if (!parser || !compound_node) {
        return true; // Nothing to do
    }

    token_t *current = tokenizer_current(parser->tokenizer);

    while (current && is_redirection_token(current->type)) {
        node_t *redir_node = parse_redirection(parser);
        if (!redir_node) {
            return false; // Error parsing redirection
        }
        add_child_node(compound_node, redir_node);
        current = tokenizer_current(parser->tokenizer);
    }

    return true;
}

/**
 * @brief Parse a redirection operator and target
 *
 * Handles all redirection types: >, <, >>, <<, <<<, 2>, &>, etc.
 * For here-documents, collects content until delimiter.
 *
 * @param parser Parser instance
 * @return Redirection AST node with target as child
 */
static node_t *parse_redirection(parser_t *parser) {
    token_t *redir_token = tokenizer_current(parser->tokenizer);
    if (!redir_token) {
        return NULL;
    }

    /* Capture the operator's full source_location_t before any
     * tokenizer_advance — that call frees the current token, so
     * dereferencing redir_token later would be use-after-free.
     * source_location_t is the project's unified position primitive
     * (shell_error.h:33-39); using it here keeps the heredoc body
     * search consistent with how every other parser path tracks
     * positions, and lets the unterminated-heredoc error point at
     * the actual operator rather than at SOURCE_LOC_UNKNOWN. */
    source_location_t op_loc =
        token_to_source_location(redir_token, parser->source_name);

    node_type_t node_type;
    switch (redir_token->type) {
    case TOK_REDIRECT_OUT:
        node_type = NODE_REDIR_OUT;
        break;
    case TOK_REDIRECT_IN:
        node_type = NODE_REDIR_IN;
        break;
    case TOK_APPEND:
        node_type = NODE_REDIR_APPEND;
        break;
    case TOK_HEREDOC:
        node_type = NODE_REDIR_HEREDOC;
        break;
    case TOK_HEREDOC_STRIP:
        node_type = NODE_REDIR_HEREDOC_STRIP;
        break;
    case TOK_HERESTRING:
        node_type = NODE_REDIR_HERESTRING;
        break;
    case TOK_REDIRECT_ERR:
        node_type = NODE_REDIR_ERR;
        break;
    case TOK_REDIRECT_IN_FD:
        node_type = NODE_REDIR_IN_FD;
        break;
    case TOK_REDIRECT_BOTH:
        node_type = NODE_REDIR_BOTH;
        break;
    case TOK_APPEND_ERR:
        node_type = NODE_REDIR_ERR_APPEND;
        break;
    case TOK_REDIRECT_FD:
        node_type = NODE_REDIR_FD;
        break;
    case TOK_REDIRECT_FD_ALLOC:
        node_type = NODE_REDIR_FD_ALLOC;
        break;
    case TOK_REDIRECT_CLOBBER:
        node_type = NODE_REDIR_CLOBBER;
        break;
    case TOK_APPEND_BOTH:
        node_type = NODE_REDIR_BOTH_APPEND;
        break;
    default:
        parser_error_add(parser, SHELL_ERR_INVALID_REDIRECT,
                         "unknown redirection operator");
        return NULL;
    }

    /* Capture location for redirection */
    source_location_t redir_loc =
        token_to_source_location(redir_token, parser->source_name);
    node_t *redir_node = new_node_at(node_type, redir_loc);
    if (!redir_node) {
        return NULL;
    }

    // Store the redirection operator
    redir_node->val.str = strdup(redir_token->text);
    redir_node->val_type = VAL_STR;

    // Disable keyword recognition for redirection targets - filenames like
    // "in", "do", "done" etc. should be treated as words, not keywords
    tokenizer_enable_keywords(parser->tokenizer, false);
    tokenizer_refresh_lookahead(parser->tokenizer);
    tokenizer_advance(parser->tokenizer);

    // Parse the target (filename or here document content)
    token_t *target_token = tokenizer_current(parser->tokenizer);

    // Re-enable keyword recognition for subsequent tokens
    tokenizer_enable_keywords(parser->tokenizer, true);

    // For NODE_REDIR_FD, the target is embedded in the redirection token itself
    if (node_type == NODE_REDIR_FD) {
        // No separate target token needed for file descriptor redirections
        return redir_node;
    }

    // For NODE_REDIR_FD_ALLOC, check if target is embedded (>&- or >&N
    // patterns) or if we need a separate target file ({varname}> or
    // {varname}>>)
    if (node_type == NODE_REDIR_FD_ALLOC) {
        const char *redir_text = redir_node->val.str;
        size_t len = strlen(redir_text);
        // Check if ends with >&- or >&N or <&- or <&N (no target needed)
        if (len >= 2) {
            char last = redir_text[len - 1];
            char prev = redir_text[len - 2];
            if (prev == '&' && (last == '-' || isdigit(last))) {
                // {varname}>&- or {varname}>&N - no target needed
                return redir_node;
            }
        }
        // Otherwise fall through to parse target file
    }

    // Check for process substitution as redirection target: < <(cmd) or >
    // >(cmd) This is valid bash/zsh syntax for redirecting from/to process
    // substitution
    if (target_token && (target_token->type == TOK_PROC_SUB_IN ||
                         target_token->type == TOK_PROC_SUB_OUT)) {
        // Parse the process substitution and attach as child of redirection
        node_t *proc_sub_node = parse_process_substitution(parser);
        if (!proc_sub_node) {
            free_node_tree(redir_node);
            return NULL;
        }
        add_child_node(redir_node, proc_sub_node);
        return redir_node;
    }

    if (!target_token || !token_is_word_like(target_token->type)) {
        if (node_type == NODE_REDIR_HEREDOC ||
            node_type == NODE_REDIR_HEREDOC_STRIP) {
            // For here documents, the delimiter might be quoted or special
            if (target_token && (target_token->type == TOK_STRING ||
                                 target_token->type == TOK_EXPANDABLE_STRING ||
                                 token_is_word_like(target_token->type))) {
                // Valid here document delimiter
            } else {
                parser_error_add(parser, SHELL_ERR_HEREDOC_DELIMITER,
                                 "expected here-document delimiter");
                free_node_tree(redir_node);
                return NULL;
            }
        } else {
            parser_error_add(parser, SHELL_ERR_INVALID_REDIRECT,
                             "expected redirection target");
            free_node_tree(redir_node);
            return NULL;
        }
    }

    // For here documents, collect the content
    if (node_type == NODE_REDIR_HEREDOC ||
        node_type == NODE_REDIR_HEREDOC_STRIP) {
        // Store delimiter before advancing tokenizer
        char *delimiter = strdup(target_token->text);
        bool strip_tabs = (node_type == NODE_REDIR_HEREDOC_STRIP);

        // Check if delimiter is quoted (any quoted delimiter disables
        // expansion)
        bool expand_variables = true;
        if (target_token->type == TOK_STRING ||
            target_token->type == TOK_EXPANDABLE_STRING) {
            // Any quoted string - disable expansion per POSIX
            expand_variables = false;
        }
        // Only unquoted delimiters allow variable expansion

        // Advance past the delimiter token first
        tokenizer_advance(parser->tokenizer);

        // Collect the here document content (this will advance the tokenizer
        // further)
        char *content = collect_heredoc_content(parser, delimiter, strip_tabs,
                                                expand_variables, op_loc);
        if (!content) {
            free(delimiter);
            free_node_tree(redir_node);
            return NULL;
        }

        // Store delimiter in the redirection node value
        redir_node->val.str = delimiter; // Transfer ownership
        redir_node->val_type = VAL_STR;

        // Create content node with the collected content
        node_t *content_node = new_node(NODE_VAR);
        if (!content_node) {
            free(content);
            free_node_tree(redir_node);
            return NULL;
        }
        content_node->val.str = content; // Transfer ownership
        content_node->val_type = VAL_STR;
        add_child_node(redir_node, content_node);

        // Create a second child node to store the expand_variables flag
        node_t *expand_flag_node = new_node(NODE_VAR);
        if (!expand_flag_node) {
            free_node_tree(redir_node);
            return NULL;
        }
        expand_flag_node->val.str = strdup(expand_variables ? "1" : "0");
        expand_flag_node->val_type = VAL_STR;
        add_child_node(redir_node, expand_flag_node);

        return redir_node;
    } else {
        // Regular redirection - handle token concatenation for variables
        char *concatenated_target = NULL;
        size_t total_len = 0;
        size_t last_end_pos =
            target_token->position + strlen(target_token->text);

        // Collect all consecutive tokens without whitespace (like
        // /tmp/file_$VAR)
        token_t *current_token = target_token;
        while (current_token && (token_is_word_like(current_token->type) ||
                                 current_token->type == TOK_VARIABLE ||
                                 current_token->type == TOK_ARITH_EXP ||
                                 current_token->type == TOK_COMMAND_SUB ||
                                 current_token->type == TOK_BACKQUOTE)) {

            size_t token_len = strlen(current_token->text);
            char *new_target =
                realloc(concatenated_target, total_len + token_len + 1);
            if (!new_target) {
                free(concatenated_target);
                free_node_tree(redir_node);
                return NULL;
            }
            concatenated_target = new_target;

            strcpy(concatenated_target + total_len, current_token->text);
            total_len += token_len;
            last_end_pos = current_token->position + token_len;

            tokenizer_advance(parser->tokenizer);
            current_token = tokenizer_current(parser->tokenizer);

            // Check if the next token is adjacent (no whitespace between)
            if (current_token && current_token->position != last_end_pos) {
                break; // There's whitespace between tokens
            }
        }

        if (concatenated_target) {
            concatenated_target[total_len] = '\0';
        } else {
            // Fallback to single token
            concatenated_target = strdup(target_token->text);
            tokenizer_advance(parser->tokenizer);
        }

        node_t *target_node = new_node(NODE_VAR);
        if (!target_node) {
            free(concatenated_target);
            free_node_tree(redir_node);
            return NULL;
        }
        target_node->val.str = concatenated_target;
        target_node->val_type = VAL_STR;
        add_child_node(redir_node, target_node);

        return redir_node;
    }
}

/**
 * @brief Collect here-document content
 *
 * Scans input for lines until the delimiter is found alone on a line.
 * Handles <<- (strip leading tabs) variant. Updates tokenizer position
 * to after the here-document.
 *
 * @param parser Parser instance
 * @param delimiter End delimiter string
 * @param strip_tabs If true, strip leading tabs from each line
 * @param expand_variables If true, variables will be expanded during execution
 * @return Collected content string (caller must free)
 */
static char *collect_heredoc_content(parser_t *parser, const char *delimiter,
                                     bool strip_tabs, bool expand_variables,
                                     source_location_t op_loc) {
    (void)expand_variables; /* Expansion handled during execution phase */
    if (!parser || !delimiter) {
        return NULL;
    }

    tokenizer_t *tokenizer = parser->tokenizer;

    /* Strip outer quotes from the delimiter if present so body lines
     * compare against the user-visible terminator text (`'END'` →
     * `END`, `"EOF"` → `EOF`). Used only for the line-by-line
     * terminator match below; the delimiter SPEC in the input is not
     * re-parsed (see content_start computation). */
    const char *match_delimiter = delimiter;
    char *unquoted_delimiter = NULL;
    if ((delimiter[0] == '"' || delimiter[0] == '\'') &&
        strlen(delimiter) > 2 &&
        delimiter[strlen(delimiter) - 1] == delimiter[0]) {
        size_t delim_len = strlen(delimiter);
        unquoted_delimiter = malloc(delim_len - 1);
        if (unquoted_delimiter) {
            strncpy(unquoted_delimiter, delimiter + 1, delim_len - 2);
            unquoted_delimiter[delim_len - 2] = '\0';
            match_delimiter = unquoted_delimiter;
        }
    }

    /* The heredoc body always begins on the line AFTER the operator's
     * line. The parser captured the operator's source_location_t into
     * op_loc before any tokenizer_advance freed the operator token,
     * so op_loc.offset is a reliable absolute byte position of the
     * `<<` operator. Scan from just past `<<` to the next newline;
     * the byte after that newline is content_start.
     *
     * The previous implementation searched the entire input from byte
     * 0 for a literal `<< delimiter` substring match, which had two
     * unfixable defects:
     *
     *   - Issue #51: when two heredocs in the same input shared a
     *     delimiter, the search for the second always found the first
     *     and reset tokenizer->position backwards, causing the parser
     *     to re-parse the second heredoc forever. Anchoring the
     *     search at op_loc.offset (the previous fix) avoided one
     *     backward jump but kept the search.
     *
     *   - Issue #52: the search couldn't handle shell quote-
     *     concatenation in the delimiter spec (e.g. `<< ''ai`, where
     *     the parser correctly resolves the delimiter to "ai" but the
     *     input bytes show `''ai`). The literal-substring match
     *     failed, content_start defaulted to the initial value of 0,
     *     and body collection started at byte 0 of the input — finding
     *     spurious terminator lines and resetting tokenizer->position
     *     to inside the live parse, triggering O(N^2) command-list
     *     growth in the case-arm body parser.
     *
     * Both classes are eliminated by computing content_start from
     * op_loc.offset directly. The delimiter spec text in the input is
     * not used for content_start; only for the terminator-line match
     * below. */
    size_t content_start = op_loc.offset + 2; /* past `<<` */
    while (content_start < tokenizer->input_length &&
           tokenizer->input[content_start] != '\n') {
        content_start++;
    }
    if (content_start < tokenizer->input_length) {
        content_start++; /* skip the newline */
    }

    // Collect lines until we find the delimiter
    size_t content_size = 0;
    size_t content_capacity = 1024;
    char *content = malloc(content_capacity);
    if (!content) {
        return NULL;
    }
    content[0] = '\0';

    size_t line_start = content_start;
    bool found_delimiter_line = false;
    while (line_start < tokenizer->input_length) {
        // Find end of current line
        size_t line_end = line_start;
        while (line_end < tokenizer->input_length &&
               tokenizer->input[line_end] != '\n') {
            line_end++;
        }

        // Extract the line (without newline)
        size_t line_len = line_end - line_start;
        char *line = malloc(line_len + 1);
        if (!line) {
            free(content);
            return NULL;
        }
        strncpy(line, &tokenizer->input[line_start], line_len);
        line[line_len] = '\0';

        // Strip leading tabs if requested (<<- variant)
        const char *line_content = line;
        if (strip_tabs) {
            while (*line_content == '\t') {
                line_content++;
            }
        }

        // Check if this line matches the delimiter
        if (strcmp(line_content, match_delimiter) == 0) {
            // Found delimiter - stop collecting
            found_delimiter_line = true;
            free(line);
            break;
        }

        // Add line to content (with newline)
        size_t needed =
            content_size + line_len + 2; // +1 for newline, +1 for null
        if (needed > content_capacity) {
            content_capacity = needed * 2;
            char *new_content = realloc(content, content_capacity);
            if (!new_content) {
                free(line);
                free(content);
                return NULL;
            }
            content = new_content;
        }

        // Append the line (stripped if <<- variant) plus newline
        if (strip_tabs) {
            strcat(content, line_content);
        } else {
            strcat(content, line);
        }
        strcat(content, "\n");
        content_size = strlen(content);

        free(line);

        // Move to next line
        line_start = line_end + 1;
    }

    // EOF reached without finding the delimiter — issue #44.
    // Treat this as a parse error rather than silently accepting the
    // partial body. Bash warns at parse time; lush -n needs an error
    // because exit code is the only signal available to tooling.
    if (!found_delimiter_line) {
        /* Use the OPERATOR's source location (op_loc), not the parser's
         * current position (which by now is at end-of-input). Pointing
         * the diagnostic at the `<<` operator is far more useful than
         * pointing at EOF. parser_error_add_with_help_at() routes the
         * error through the same context-stack / source-line / legacy-
         * compatibility plumbing as parser_error_add_with_help, just
         * with an explicit location. */
        parser_error_add_with_help_at(
            parser, SHELL_ERR_UNEXPECTED_EOF, op_loc,
            "the delimiter must appear alone on a line; for <<- it may "
            "be preceded by tabs",
            "unterminated here-document: expected delimiter '%s' but "
            "reached end of input",
            match_delimiter);
        free(content);
        if (unquoted_delimiter) {
            free(unquoted_delimiter);
        }
        return NULL;
    }

    // Update tokenizer position to after the delimiter line
    tokenizer->position = line_start;

    // Update line and column tracking
    for (size_t i = content_start; i < tokenizer->position; i++) {
        if (tokenizer->input[i] == '\n') {
            tokenizer->line++;
            tokenizer->column = 1;
        } else {
            tokenizer->column++;
        }
    }

    // Refresh tokenizer cache from the updated position
    tokenizer_refresh_from_position(tokenizer);

    // Clean up temporary delimiter
    if (unquoted_delimiter) {
        free(unquoted_delimiter);
    }

    return content;
}

/**
 * @brief Parse an if statement
 *
 * Parses: if condition; then body [elif condition; then body]* [else body] fi
 *
 * @param parser Parser instance
 * @return If statement AST node
 */
static node_t *parse_if_statement(parser_t *parser) {
    /* Capture location before consuming 'if' token */
    token_t *if_token = tokenizer_current(parser->tokenizer);
    source_location_t if_loc =
        token_to_source_location(if_token, parser->source_name);

    if (!expect_token(parser, TOK_IF)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing if statement");

    node_t *if_node = new_node_at(NODE_IF, if_loc);
    if (!if_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse condition - parse until we hit 'then' or ';'
    node_t *condition = parse_logical_expression(parser);
    if (!condition) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(if_node, condition);

    // Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    // Now we should see 'then'
    if (!expect_token_with_help(
            parser, TOK_THEN, "'if' requires 'then' before the command body")) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'then' before parsing body
    skip_separators(parser);

    // Parse then body - parse until we hit 'else', 'elif', or 'fi'
    node_t *then_body = parse_if_body(parser);
    if (!then_body) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(if_node, then_body);

    // Handle optional semicolon before elif/else/fi
    if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
        tokenizer_advance(parser->tokenizer);
    }

    // Parse optional elif clauses
    // Skip separators before checking for elif
    skip_separators(parser);

    // Handle multiple elif clauses
    while (tokenizer_match(parser->tokenizer, TOK_ELIF)) {
        tokenizer_advance(parser->tokenizer);

        // Parse elif condition
        node_t *elif_condition = parse_logical_expression(parser);
        if (!elif_condition) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, elif_condition);

        // Skip separators before 'then'
        skip_separators(parser);

        // Expect 'then' after elif condition
        if (!expect_token_with_help(
                parser, TOK_THEN,
                "'elif' requires 'then' before the command body")) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Skip separators after 'then'
        skip_separators(parser);

        // Parse elif body
        node_t *elif_body = parse_if_body(parser);
        if (!elif_body) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, elif_body);

        // Handle optional semicolon after elif body
        if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
            tokenizer_advance(parser->tokenizer);
        }

        // Skip separators before next elif/else/fi
        skip_separators(parser);
    }

    // Handle optional else clause
    if (tokenizer_match(parser->tokenizer, TOK_ELSE)) {
        tokenizer_advance(parser->tokenizer);

        // Skip separators after 'else' before parsing body
        skip_separators(parser);

        node_t *else_body = parse_if_body(parser);
        if (!else_body) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, else_body);
    }

    // Skip separators before 'fi'
    skip_separators(parser);

    // No need for additional semicolon handling here since we handled it above

    if (!expect_token_with_help(parser, TOK_FI,
                                "'if' statement must end with 'fi'")) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: if ...; then ...; fi >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, if_node)) {
        free_node_tree(if_node);
        return NULL;
    }

    return if_node;
}

/**
 * @brief Parse a while statement
 *
 * Parses: while condition; do body done
 *
 * @param parser Parser instance
 * @return While loop AST node
 */
static node_t *parse_while_statement(parser_t *parser) {
    /* Capture location before consuming 'while' token */
    token_t *while_token = tokenizer_current(parser->tokenizer);
    source_location_t while_loc =
        token_to_source_location(while_token, parser->source_name);

    if (!expect_token(parser, TOK_WHILE)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing while loop");

    node_t *while_node = new_node_at(NODE_WHILE, while_loc);
    if (!while_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse condition as a full logical expression so that &&/|| chains
    // (e.g. `while [ $i -lt N ] && true; do ...`) parse like in if/elif.
    // The `do` terminator is unambiguous, so logical operators in the
    // condition introduce no parser ambiguity.
    node_t *condition = parse_logical_expression(parser);

    if (!condition) {
        free_node_tree(while_node);
        parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                   "'while' requires a condition command",
                                   "invalid while loop condition");
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(while_node, condition);

    // Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    // Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'while' requires 'do' before the loop body")) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'do' before parsing body
    skip_separators(parser);

    // Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(while_node, body);

    // Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'while' loop must end with 'done'")) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: while ...; do ...; done </input 2>&1
    if (!parse_trailing_redirections(parser, while_node)) {
        free_node_tree(while_node);
        return NULL;
    }

    return while_node;
}

/**
 * @brief Parse an until statement
 *
 * Parses: until condition; do body done
 *
 * @param parser Parser instance
 * @return Until loop AST node
 */
static node_t *parse_until_statement(parser_t *parser) {
    if (!expect_token(parser, TOK_UNTIL)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing until loop");

    node_t *until_node = new_node(NODE_UNTIL);
    if (!until_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse condition as a full logical expression — same rationale as
    // parse_while_statement: &&/|| in conditions are unambiguous because
    // `do` is the terminator.
    node_t *condition = parse_logical_expression(parser);

    if (!condition) {
        free_node_tree(until_node);
        parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                   "'until' requires a condition command",
                                   "invalid until loop condition");
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(until_node, condition);

    // Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    // Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'until' requires 'do' before the loop body")) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'do' before parsing body
    skip_separators(parser);

    // Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(until_node, body);

    // Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'until' loop must end with 'done'")) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: until ...; do ...; done </input 2>&1
    if (!parse_trailing_redirections(parser, until_node)) {
        free_node_tree(until_node);
        return NULL;
    }

    return until_node;
}

/**
 * @brief Parse a for statement
 *
 * Parses: for var in wordlist; do body done
 *
 * @param parser Parser instance
 * @return For loop AST node with variable name in val.str
 */
static node_t *parse_for_statement(parser_t *parser) {
    /* Capture location before consuming 'for' token */
    token_t *for_token = tokenizer_current(parser->tokenizer);
    source_location_t for_loc =
        token_to_source_location(for_token, parser->source_name);

    if (!expect_token(parser, TOK_FOR)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing for loop");

    // Check for C-style for loop: for ((init; test; update))
    if (tokenizer_match(parser->tokenizer, TOK_DOUBLE_LPAREN)) {
        tokenizer_advance(parser->tokenizer); // consume ((

        node_t *for_arith_node = new_node_at(NODE_FOR_ARITH, for_loc);
        if (!for_arith_node) {
            parser_pop_context(parser);
            return NULL;
        }

        // Parse the three arithmetic expressions separated by semicolons
        // Format: for ((init; test; update)); do body; done
        // Each expression is optional (can be empty)
        //
        // We extract raw input text to preserve operators like <= that
        // the tokenizer splits into separate tokens (< and =).

        const char *input = parser->tokenizer->input;
        char *init_expr = NULL;
        char *test_expr = NULL;
        char *update_expr = NULL;

        int paren_depth = 0; // Track nested parentheses

        // Get start position for init expression
        token_t *start_tok = tokenizer_current(parser->tokenizer);
        size_t expr_start = start_tok ? start_tok->position : 0;
        size_t expr_end = expr_start;

        // Parse init expression - find the first ; at depth 0
        while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
            token_t *tok = tokenizer_current(parser->tokenizer);

            // Track parentheses depth to handle nested (( )) in expressions
            if (tok->type == TOK_LPAREN || tok->type == TOK_DOUBLE_LPAREN) {
                paren_depth++;
            } else if (tok->type == TOK_RPAREN) {
                paren_depth--;
            } else if (tok->type == TOK_DOUBLE_RPAREN) {
                if (paren_depth > 0) {
                    paren_depth -= 2;
                } else {
                    // End of for (( )) - but we haven't seen all three
                    // expressions
                    parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                     "expected ';' in C-style for loop");
                    free_node_tree(for_arith_node);
                    parser_pop_context(parser);
                    return NULL;
                }
            }

            // Semicolon at depth 0 separates expressions
            if (tok->type == TOK_SEMICOLON && paren_depth == 0) {
                expr_end = tok->position;
                break;
            }

            tokenizer_advance(parser->tokenizer);
        }

        // Extract init expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            init_expr = malloc(len + 1);
            if (init_expr) {
                memcpy(init_expr, input + expr_start, len);
                init_expr[len] = '\0';
                // Trim leading/trailing whitespace
                char *p = init_expr;
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (p != init_expr)
                    memmove(init_expr, p, strlen(p) + 1);
                size_t l = strlen(init_expr);
                while (l > 0 &&
                       (init_expr[l - 1] == ' ' || init_expr[l - 1] == '\t')) {
                    init_expr[--l] = '\0';
                }
            }
        } else {
            init_expr = strdup("");
        }

        if (!tokenizer_consume(parser->tokenizer, TOK_SEMICOLON)) {
            parser_error_add(
                parser, SHELL_ERR_UNEXPECTED_TOKEN,
                "expected ';' after init expression in C-style for loop");
            free(init_expr);
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Parse test expression
        start_tok = tokenizer_current(parser->tokenizer);
        expr_start = start_tok ? start_tok->position : 0;
        expr_end = expr_start;
        paren_depth = 0;

        while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
            token_t *tok = tokenizer_current(parser->tokenizer);

            if (tok->type == TOK_LPAREN || tok->type == TOK_DOUBLE_LPAREN) {
                paren_depth++;
            } else if (tok->type == TOK_RPAREN) {
                paren_depth--;
            } else if (tok->type == TOK_DOUBLE_RPAREN) {
                if (paren_depth > 0) {
                    paren_depth -= 2;
                } else {
                    parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                     "expected ';' after test expression in "
                                     "C-style for loop");
                    free(init_expr);
                    free_node_tree(for_arith_node);
                    parser_pop_context(parser);
                    return NULL;
                }
            }

            if (tok->type == TOK_SEMICOLON && paren_depth == 0) {
                expr_end = tok->position;
                break;
            }

            tokenizer_advance(parser->tokenizer);
        }

        // Extract test expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            test_expr = malloc(len + 1);
            if (test_expr) {
                memcpy(test_expr, input + expr_start, len);
                test_expr[len] = '\0';
                // Trim whitespace
                char *p = test_expr;
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (p != test_expr)
                    memmove(test_expr, p, strlen(p) + 1);
                size_t l = strlen(test_expr);
                while (l > 0 &&
                       (test_expr[l - 1] == ' ' || test_expr[l - 1] == '\t')) {
                    test_expr[--l] = '\0';
                }
            }
        } else {
            test_expr = strdup("");
        }

        if (!tokenizer_consume(parser->tokenizer, TOK_SEMICOLON)) {
            parser_error_add(
                parser, SHELL_ERR_UNEXPECTED_TOKEN,
                "expected ';' after test expression in C-style for loop");
            free(init_expr);
            free(test_expr);
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Parse update expression (until )))
        start_tok = tokenizer_current(parser->tokenizer);
        expr_start = start_tok ? start_tok->position : 0;
        expr_end = expr_start;
        paren_depth = 0;

        while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
            token_t *tok = tokenizer_current(parser->tokenizer);

            if (tok->type == TOK_LPAREN || tok->type == TOK_DOUBLE_LPAREN) {
                paren_depth++;
            } else if (tok->type == TOK_RPAREN) {
                if (paren_depth > 0) {
                    paren_depth--;
                } else {
                    expr_end = tok->position;
                    break;
                }
            } else if (tok->type == TOK_DOUBLE_RPAREN) {
                if (paren_depth > 0) {
                    paren_depth -= 2;
                } else {
                    expr_end = tok->position;
                    break;
                }
            }

            tokenizer_advance(parser->tokenizer);
        }

        // Extract update expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            update_expr = malloc(len + 1);
            if (update_expr) {
                memcpy(update_expr, input + expr_start, len);
                update_expr[len] = '\0';
                // Trim whitespace
                char *p = update_expr;
                while (*p && (*p == ' ' || *p == '\t'))
                    p++;
                if (p != update_expr)
                    memmove(update_expr, p, strlen(p) + 1);
                size_t l = strlen(update_expr);
                while (l > 0 && (update_expr[l - 1] == ' ' ||
                                 update_expr[l - 1] == '\t')) {
                    update_expr[--l] = '\0';
                }
            }
        } else {
            update_expr = strdup("");
        }

        // Expect )) to close the arithmetic for
        if (!tokenizer_consume(parser->tokenizer, TOK_DOUBLE_RPAREN)) {
            parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                             "expected '))' to close C-style for loop");
            free(init_expr);
            free(test_expr);
            free(update_expr);
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Store expressions as child nodes
        // Child 0: init expression
        node_t *init_node = new_node(NODE_ARITH_EXP);
        if (init_node) {
            init_node->val.str = init_expr;
            init_node->val_type = VAL_STR;
            add_child_node(for_arith_node, init_node);
        } else {
            free(init_expr);
        }

        // Child 1: test expression
        node_t *test_node = new_node(NODE_ARITH_EXP);
        if (test_node) {
            test_node->val.str = test_expr;
            test_node->val_type = VAL_STR;
            add_child_node(for_arith_node, test_node);
        } else {
            free(test_expr);
        }

        // Child 2: update expression
        node_t *update_node = new_node(NODE_ARITH_EXP);
        if (update_node) {
            update_node->val.str = update_expr;
            update_node->val_type = VAL_STR;
            add_child_node(for_arith_node, update_node);
        } else {
            free(update_expr);
        }

        // Skip any separators (semicolons, newlines, whitespace)
        skip_separators(parser);

        // Expect 'do'
        if (!expect_token_with_help(
                parser, TOK_DO,
                "'for ((...))' requires 'do' before the loop body")) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Skip separators after 'do' before parsing body
        skip_separators(parser);

        // Parse loop body
        node_t *body = parse_command_body(parser, TOK_DONE);
        if (!body) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(for_arith_node, body); // Child 3: body

        // Skip separators before 'done'
        skip_separators(parser);

        if (!expect_token_with_help(
                parser, TOK_DONE, "'for ((...))' loop must end with 'done'")) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        parser_pop_context(parser);

        // Parse any trailing redirections
        if (!parse_trailing_redirections(parser, for_arith_node)) {
            free_node_tree(for_arith_node);
            return NULL;
        }

        return for_arith_node;
    }

    node_t *for_node = new_node_at(NODE_FOR, for_loc);
    if (!for_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse variable name (POSIX for-in loop)
    if (!tokenizer_match(parser->tokenizer, TOK_WORD)) {
        free_node_tree(for_node);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "syntax: for NAME [in WORDS...]; do COMMANDS; done\n       for "
            "((init; test; update)); do COMMANDS; done",
            "expected variable name or '((' after 'for'");
        parser_pop_context(parser);
        return NULL;
    }

    token_t *var_token = tokenizer_current(parser->tokenizer);
    for_node->val.str = strdup(var_token->text);
    for_node->val_type = VAL_STR;
    tokenizer_advance(parser->tokenizer);

    // Parse word list
    node_t *word_list = new_node(NODE_VAR); // Use as container
    if (!word_list) {
        free_node_tree(for_node);
        return NULL;
    }

    // POSIX: 'in' keyword is optional. If omitted, iterate over "$@"
    // Check if we have 'in' or if we're going directly to ';'/newline/'do'
    if (tokenizer_match(parser->tokenizer, TOK_IN)) {
        // Consume the 'in' token and parse word list
        tokenizer_advance(parser->tokenizer);
    } else if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON) ||
               tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_DO)) {
        // No 'in' clause - POSIX says iterate over positional parameters
        // Create a word list containing "$@"
        node_t *at_node = new_node(NODE_VAR);
        if (at_node) {
            at_node->val.str = strdup("\"$@\"");
            at_node->val_type = VAL_STR;
            add_child_node(word_list, at_node);
        }
        // Skip to where we expect 'do'
        goto skip_word_parsing;
    } else {
        // Unexpected token after variable name
        free_node_tree(for_node);
        free_node_tree(word_list);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "syntax: for NAME [in WORDS...]; do COMMANDS; done",
            "expected 'in', ';', or 'do' after variable name in for loop");
        parser_pop_context(parser);
        return NULL;
    }

    // Collect all words until ';', newline, or 'do'
    // In POSIX, words in the for-in list can contain '=' (e.g., name=value)
    // The tokenizer splits these, so we need to reassemble them here
    while (!tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
           !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
           !tokenizer_match(parser->tokenizer, TOK_DO) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        token_t *word_token = tokenizer_current(parser->tokenizer);

        // In for-in word lists, '=' can be a standalone word (e.g., for i in =
        // foo) Handle it specially before the normal word-like check
        if (word_token->type == TOK_ASSIGN) {
            node_t *word_node = new_node(NODE_VAR);
            if (!word_node) {
                free_node_tree(for_node);
                free_node_tree(word_list);
                return NULL;
            }
            word_node->val.str = strdup("=");
            word_node->val_type = VAL_STR;
            add_child_node(word_list, word_node);
            tokenizer_advance(parser->tokenizer);
            continue;
        }

        if (token_is_word_like(word_token->type) ||
            word_token->type == TOK_VARIABLE ||
            word_token->type == TOK_COMMAND_SUB ||
            word_token->type == TOK_ARITH_EXP ||
            word_token->type == TOK_BACKQUOTE) {

            node_t *word_node = NULL;

            // Create appropriate node type based on token type
            if (word_token->type == TOK_COMMAND_SUB) {
                word_node = new_node(NODE_COMMAND_SUB);
            } else if (word_token->type == TOK_ARITH_EXP) {
                word_node = new_node(NODE_ARITH_EXP);
            } else if (word_token->type == TOK_EXPANDABLE_STRING) {
                word_node = new_node(NODE_STRING_EXPANDABLE);
            } else if (word_token->type == TOK_STRING) {
                word_node = new_node(NODE_STRING_LITERAL);
            } else {
                word_node = new_node(NODE_VAR);
            }

            if (!word_node) {
                free_node_tree(for_node);
                free_node_tree(word_list);
                return NULL;
            }

            // Start building the word string - may need to combine with '=' and
            // more
            char *combined = strdup(word_token->text);
            if (!combined) {
                free_node_tree(word_node);
                free_node_tree(for_node);
                free_node_tree(word_list);
                return NULL;
            }
            // Track end position of current token for adjacency checks
            size_t current_end_pos = word_token->position + word_token->length;
            tokenizer_advance(parser->tokenizer);

            // Check for WORD=VALUE pattern: if next token is '=' followed by
            // word, combine them into a single word (POSIX: for i in a=b c=d;
            // do ...) Handle chained equals like a=b=c as a single word
            // IMPORTANT: Only combine if there's no whitespace between tokens
            while (tokenizer_match(parser->tokenizer, TOK_ASSIGN)) {
                // Check if '=' is adjacent to the previous token (no
                // whitespace)
                token_t *assign_token = tokenizer_current(parser->tokenizer);
                if (assign_token->position != current_end_pos) {
                    // There's whitespace before '=', don't combine
                    break;
                }
                size_t assign_end_pos =
                    assign_token->position + assign_token->length;
                tokenizer_advance(parser->tokenizer); // consume '='

                // Append '=' to combined string
                size_t len = strlen(combined);
                char *new_combined = realloc(combined, len + 2);
                if (!new_combined) {
                    free(combined);
                    free_node_tree(word_node);
                    free_node_tree(for_node);
                    free_node_tree(word_list);
                    return NULL;
                }
                combined = new_combined;
                combined[len] = '=';
                combined[len + 1] = '\0';

                // Check if there's a value IMMEDIATELY after '=' (no
                // whitespace)
                token_t *value_token = tokenizer_current(parser->tokenizer);
                bool is_adjacent = (value_token->position == assign_end_pos);

                if (is_adjacent && (token_is_word_like(value_token->type) ||
                                    value_token->type == TOK_VARIABLE ||
                                    value_token->type == TOK_NUMBER)) {
                    // Append the value (it's adjacent to '=', so part of same
                    // word)
                    size_t vlen = strlen(value_token->text);
                    new_combined =
                        realloc(combined, strlen(combined) + vlen + 1);
                    if (!new_combined) {
                        free(combined);
                        free_node_tree(word_node);
                        free_node_tree(for_node);
                        free_node_tree(word_list);
                        return NULL;
                    }
                    combined = new_combined;
                    strcat(combined, value_token->text);
                    // Update end position for next iteration's adjacency check
                    current_end_pos =
                        value_token->position + value_token->length;
                    tokenizer_advance(parser->tokenizer);
                    // Continue loop to check for more '=' (handles a=b=c)
                } else {
                    // No adjacent value after '=' - word ends with '=' (e.g.,
                    // empty=) Or there's whitespace before next token (separate
                    // word)
                    break;
                }
            }

            word_node->val.str = combined;
            word_node->val_type = VAL_STR;
            add_child_node(word_list, word_node);
        } else {
            break;
        }
    }

skip_word_parsing:
    add_child_node(for_node, word_list);

    // Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    // Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'for' requires 'do' before the loop body")) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'do' before parsing body
    skip_separators(parser);

    // Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(for_node, body);

    // Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'for' loop must end with 'done'")) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: for ...; do ...; done >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, for_node)) {
        free_node_tree(for_node);
        return NULL;
    }

    return for_node;
}

/**
 * @brief Parse a select statement
 *
 * Parses: select name [in word ...]; do commands; done
 * Similar to for loop but creates an interactive menu.
 *
 * @param parser Parser instance
 * @return Select statement AST node
 */
static node_t *parse_select_statement(parser_t *parser) {
    if (!expect_token(parser, TOK_SELECT)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing select statement");

    node_t *select_node = new_node(NODE_SELECT);
    if (!select_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse variable name
    if (!tokenizer_match(parser->tokenizer, TOK_WORD)) {
        free_node_tree(select_node);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "syntax: select NAME [in WORDS...]; do COMMANDS; done",
            "expected variable name after 'select'");
        parser_pop_context(parser);
        return NULL;
    }

    token_t *var_token = tokenizer_current(parser->tokenizer);
    select_node->val.str = strdup(var_token->text);
    select_node->val_type = VAL_STR;
    tokenizer_advance(parser->tokenizer);

    // Skip any whitespace
    while (tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
        tokenizer_advance(parser->tokenizer);
    }

    // Check for optional 'in' keyword
    if (tokenizer_match(parser->tokenizer, TOK_IN)) {
        tokenizer_advance(parser->tokenizer);

        // Parse word list
        node_t *word_list = new_node(NODE_VAR); // Use as container
        if (!word_list) {
            free_node_tree(select_node);
            return NULL;
        }

        // Collect all words until ';', newline, or 'do'
        while (!tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
               !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
               !tokenizer_match(parser->tokenizer, TOK_DO) &&
               !tokenizer_match(parser->tokenizer, TOK_EOF)) {

            token_t *word_token = tokenizer_current(parser->tokenizer);

            if (token_is_word_like(word_token->type) ||
                word_token->type == TOK_VARIABLE ||
                word_token->type == TOK_COMMAND_SUB ||
                word_token->type == TOK_ARITH_EXP ||
                word_token->type == TOK_BACKQUOTE) {

                node_t *word_node = NULL;

                // Create appropriate node type based on token type
                if (word_token->type == TOK_COMMAND_SUB) {
                    word_node = new_node(NODE_COMMAND_SUB);
                } else if (word_token->type == TOK_ARITH_EXP) {
                    word_node = new_node(NODE_ARITH_EXP);
                } else if (word_token->type == TOK_EXPANDABLE_STRING) {
                    word_node = new_node(NODE_STRING_EXPANDABLE);
                } else if (word_token->type == TOK_STRING) {
                    word_node = new_node(NODE_STRING_LITERAL);
                } else {
                    word_node = new_node(NODE_VAR);
                }

                if (!word_node) {
                    free_node_tree(select_node);
                    free_node_tree(word_list);
                    return NULL;
                }

                word_node->val.str = strdup(word_token->text);
                word_node->val_type = VAL_STR;
                add_child_node(word_list, word_node);
                tokenizer_advance(parser->tokenizer);
            } else {
                break;
            }
        }

        add_child_node(select_node, word_list);
    }

    // Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    // Now we should see 'do'
    if (!expect_token_with_help(
            parser, TOK_DO, "'select' requires 'do' before the command body")) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'do' before parsing body
    skip_separators(parser);

    // Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(select_node, body);

    // Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'select' statement must end with 'done'")) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: select ... done >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, select_node)) {
        free_node_tree(select_node);
        return NULL;
    }

    return select_node;
}

/**
 * @brief Parse a time command
 *
 * Parses: time [-p] pipeline
 * Times the execution of the pipeline.
 *
 * @param parser Parser instance
 * @return Time command AST node
 */
static node_t *parse_time_command(parser_t *parser) {
    if (!expect_token(parser, TOK_TIME)) {
        return NULL;
    }

    node_t *time_node = new_node(NODE_TIME);
    if (!time_node) {
        return NULL;
    }

    // Check for -p option (POSIX format)
    token_t *current = tokenizer_current(parser->tokenizer);
    if (current && token_is_word_like(current->type) &&
        strcmp(current->text, "-p") == 0) {
        time_node->val.sint = 1; // Flag for -p option
        time_node->val_type = VAL_SINT;
        tokenizer_advance(parser->tokenizer);
    } else {
        time_node->val.sint = 0;
        time_node->val_type = VAL_SINT;
    }

    // Parse the pipeline/command to time
    node_t *pipeline = parse_pipeline(parser);
    if (!pipeline) {
        free_node_tree(time_node);
        return NULL;
    }
    add_child_node(time_node, pipeline);

    return time_node;
}

/**
 * @brief Parse a coprocess command
 *
 * Parses: coproc [NAME] command
 * Creates a coprocess running in the background with bidirectional pipes.
 * If NAME is provided, file descriptors are stored in NAME array and
 * PID in NAME_PID variable. Otherwise uses COPROC and COPROC_PID.
 *
 * The first child node stores the command to execute.
 * The node's val.str stores the coprocess name (or NULL for default COPROC).
 *
 * @param parser Parser instance
 * @return Coproc AST node
 */
static node_t *parse_coproc(parser_t *parser) {
    if (!expect_token(parser, TOK_COPROC)) {
        return NULL;
    }

    // Check if feature is enabled
    if (!shell_mode_allows(FEATURE_COPROC)) {
        parser_error_add(parser, SHELL_ERR_FEATURE_DISABLED,
                         "coproc: feature not enabled in current shell mode");
        return NULL;
    }

    node_t *coproc_node = new_node(NODE_COPROC);
    if (!coproc_node) {
        return NULL;
    }

    // Skip any whitespace/newlines
    token_t *current = tokenizer_current(parser->tokenizer);
    while (current &&
           (current->type == TOK_NEWLINE || current->type == TOK_WHITESPACE)) {
        tokenizer_advance(parser->tokenizer);
        current = tokenizer_current(parser->tokenizer);
    }

    if (!current || current->type == TOK_EOF) {
        free_node_tree(coproc_node);
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected command after 'coproc'");
        return NULL;
    }

    // Check if first word is a NAME (simple identifier followed by command)
    // NAME must be a valid identifier and not a compound command starter
    char *coproc_name = NULL;

    if (token_is_word_like(current->type)) {
        // Peek ahead to see if this is a name or the start of a command
        token_t *next = tokenizer_peek(parser->tokenizer);

        // If next token is also word-like or a compound command starter,
        // then current token is the NAME
        if (next && (token_is_word_like(next->type) ||
                     next->type == TOK_LBRACE || next->type == TOK_LPAREN ||
                     next->type == TOK_WHILE || next->type == TOK_UNTIL ||
                     next->type == TOK_FOR || next->type == TOK_IF ||
                     next->type == TOK_CASE || next->type == TOK_SELECT)) {
            // Current is the NAME
            coproc_name = strdup(current->text);
            tokenizer_advance(parser->tokenizer);
        }
    }

    // Store name (NULL means use default "COPROC")
    coproc_node->val.str = coproc_name;
    coproc_node->val_type = VAL_STR;

    // Parse the command (can be simple command or compound command)
    node_t *command = parse_pipeline(parser);
    if (!command) {
        free_node_tree(coproc_node);
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected command after 'coproc'");
        return NULL;
    }

    add_child_node(coproc_node, command);

    return coproc_node;
}

/**
 * @brief Parse an anonymous function (Zsh-style)
 *
 * Parses: () { body }
 * Anonymous functions are immediately executed with no arguments.
 * The tokenizer has already consumed '(' and ')' when this is called,
 * and the current token is '{'.
 *
 * @param parser Parser instance
 * @return Anonymous function AST node
 */
static node_t *parse_anonymous_function(parser_t *parser) {
    // Current token should be '{' - we've already consumed ()
    node_t *anon_node = new_node(NODE_ANON_FUNCTION);
    if (!anon_node) {
        return NULL;
    }

    // Parse the brace group body
    node_t *body = parse_brace_group(parser);
    if (!body) {
        free_node_tree(anon_node);
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected '{' after '()' in anonymous function");
        return NULL;
    }

    add_child_node(anon_node, body);

    /* Collect trailing positional arguments after the closing '}'.
     * Zsh's anonymous-function form is `() { body } ARG1 ARG2 ...`,
     * where the args become $1, $2, ... within the body. Uses the
     * shared collect_word_argument helper so anon-function args have
     * the same acceptance, adjacency-concatenation, and node-type
     * classification semantics as regular command arguments. The
     * helper stops naturally at non-arg tokens (NEWLINE, SEMI, EOF,
     * AMP, PIPE, redirection tokens, etc.). */
    while (collect_word_argument(parser, anon_node)) {
        /* Loop until helper returns false (non-arg token or alloc failure). */
    }
    if (parser->has_error) {
        free_node_tree(anon_node);
        return NULL;
    }

    return anon_node;
}

/**
 * @brief Parse a case statement
 *
 * Parses: case word in pattern) commands ;; [pattern) commands ;;]* esac
 * Patterns can use | for alternation.
 *
 * @param parser Parser instance
 * @return Case statement AST node
 */
static node_t *parse_case_statement(parser_t *parser) {
    if (!expect_token(parser, TOK_CASE)) {
        return NULL;
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing case statement");

    node_t *case_node = new_node(NODE_CASE);
    if (!case_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Parse the word to test - collect multiple tokens until 'in'
    // This handles cases like: case $var in, case :$PATH: in, case "$foo" in
    char *case_word = NULL;
    size_t case_word_len = 0;

    while (!tokenizer_match(parser->tokenizer, TOK_IN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) &&
           !tokenizer_match(parser->tokenizer, TOK_NEWLINE)) {

        token_t *word_token = tokenizer_current(parser->tokenizer);

        // Accept word-like tokens, variables, strings, and colons for case
        // words
        if (token_is_word_like(word_token->type) ||
            word_token->type == TOK_VARIABLE ||
            word_token->type == TOK_STRING ||
            word_token->type == TOK_EXPANDABLE_STRING) {

            size_t token_len = strlen(word_token->text);
            char *new_word = realloc(case_word, case_word_len + token_len + 1);
            if (!new_word) {
                free(case_word);
                free_node_tree(case_node);
                parser_pop_context(parser);
                return NULL;
            }
            case_word = new_word;
            memcpy(case_word + case_word_len, word_token->text, token_len);
            case_word_len += token_len;
            case_word[case_word_len] = '\0';

            tokenizer_advance(parser->tokenizer);
        } else {
            break;
        }
    }

    if (!case_word || case_word_len == 0) {
        free(case_word);
        free_node_tree(case_node);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "syntax: case WORD in [PATTERN) COMMANDS ;;]... esac",
            "expected word after 'case'");
        parser_pop_context(parser);
        return NULL;
    }

    // Store the test word
    case_node->val.str = case_word;
    case_node->val_type = VAL_STR;

    // Skip separators
    skip_separators(parser);

    // Expect 'in' keyword
    if (!expect_token_with_help(
            parser, TOK_IN,
            "'case' requires 'in' keyword after the test word")) {
        free_node_tree(case_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after 'in'
    skip_separators(parser);

    // Parse case items until 'esac'
    parser_loop_guard_t items_guard = PARSER_LOOP_GUARD_INIT;
    while (!tokenizer_match(parser->tokenizer, TOK_ESAC) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        if (!parser_loop_check_progress(parser, &items_guard,
                                        "parse_case_statement items")) {
            free_node_tree(case_node);
            parser_pop_context(parser);
            return NULL;
        }

        // Parse pattern(s)
        node_t *case_item = new_node(NODE_CASE_ITEM);
        if (!case_item) {
            free_node_tree(case_node);
            return NULL;
        }

        // Terminator will be stored in pattern string prefix (0=break, 1=fall,
        // 2=cont)
        case_terminator_t terminator = CASE_TERM_BREAK;

        // Build pattern string (can be multiple patterns separated by |)
        char *pattern = NULL;
        size_t pattern_len = 0;

        do {
            // Build pattern from multiple tokens until ) or |
            char *single_pattern = NULL;
            size_t single_pattern_len = 0;

            // Collect tokens for a single pattern until ) or |
            while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
                   !tokenizer_match(parser->tokenizer, TOK_PIPE) &&
                   !tokenizer_match(parser->tokenizer, TOK_EOF) &&
                   !tokenizer_match(parser->tokenizer, TOK_ESAC)) {

                token_t *pattern_token = tokenizer_current(parser->tokenizer);

                // Accept word-like tokens, wildcards, brackets, variables, and
                // equals for patterns (equals needed for patterns like HOME=*)
                if (token_is_word_like(pattern_token->type) ||
                    pattern_token->type == TOK_MULTIPLY ||
                    pattern_token->type == TOK_QUESTION ||
                    pattern_token->type == TOK_GLOB ||
                    pattern_token->type == TOK_LBRACKET ||
                    pattern_token->type == TOK_RBRACKET ||
                    pattern_token->type == TOK_VARIABLE ||
                    pattern_token->type == TOK_ASSIGN) {

                    size_t token_len = strlen(pattern_token->text);
                    char *new_single_pattern = realloc(
                        single_pattern, single_pattern_len + token_len + 1);
                    if (!new_single_pattern) {
                        free(single_pattern);
                        free_node_tree(case_item);
                        free_node_tree(case_node);
                        return NULL;
                    }
                    single_pattern = new_single_pattern;
                    strcpy(single_pattern + single_pattern_len,
                           pattern_token->text);
                    single_pattern_len += token_len;

                    tokenizer_advance(parser->tokenizer);
                } else {
                    // Unexpected token in pattern
                    break;
                }
            }

            // If we didn't collect any pattern tokens, that's an error
            if (!single_pattern) {
                free_node_tree(case_item);
                free_node_tree(case_node);
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNEXPECTED_TOKEN,
                    "patterns can contain wildcards: *, ?, [...]",
                    "expected pattern in case statement");
                parser_pop_context(parser);
                return NULL;
            }

            // Append this single pattern to the overall pattern string
            if (pattern) {
                // Add | separator and new pattern
                char *new_pattern =
                    realloc(pattern, pattern_len + 1 + single_pattern_len + 1);
                if (!new_pattern) {
                    free(single_pattern);
                    free(pattern);
                    free_node_tree(case_item);
                    free_node_tree(case_node);
                    return NULL;
                }
                pattern = new_pattern;
                pattern[pattern_len] = '|';
                strcpy(pattern + pattern_len + 1, single_pattern);
                pattern_len += 1 + single_pattern_len;
            } else {
                // First pattern
                pattern = single_pattern;
                pattern_len = single_pattern_len;
                single_pattern = NULL; // Transfer ownership
            }

            free(single_pattern);

            // Check for | to continue with more patterns
        } while (tokenizer_match(parser->tokenizer, TOK_PIPE) &&
                 (tokenizer_advance(parser->tokenizer), true));

        // Store pattern in case item
        case_item->val.str = pattern;
        case_item->val_type = VAL_STR;

        // Expect )
        if (!tokenizer_match(parser->tokenizer, TOK_RPAREN)) {
            free_node_tree(case_item);
            free_node_tree(case_node);
            parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                       "case patterns must end with ')'",
                                       "expected ')' after case pattern");
            parser_pop_context(parser);
            return NULL;
        }
        tokenizer_advance(parser->tokenizer);

        // Skip only newlines/whitespace after ), NOT semicolons
        // (semicolons are case terminators that we need to detect)
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }

        // Parse commands until case terminator (;;, ;&, ;;&) or esac
        node_t *commands = NULL;
        parser_loop_guard_t body_guard = PARSER_LOOP_GUARD_INIT;
        while (!tokenizer_match(parser->tokenizer, TOK_ESAC) &&
               !tokenizer_match(parser->tokenizer, TOK_EOF)) {

            if (!parser_loop_check_progress(parser, &body_guard,
                                            "parse_case_statement body")) {
                free_node_tree(case_item);
                free_node_tree(case_node);
                if (commands) {
                    free_node_tree(commands);
                }
                parser_pop_context(parser);
                return NULL;
            }

            // Check for terminators before processing command
            if (tokenizer_match(parser->tokenizer, TOK_ESAC) ||
                tokenizer_match(parser->tokenizer, TOK_EOF)) {
                break;
            }

            // Skip newlines, whitespace, and comments, but NOT semicolons
            // (semicolons are case terminators that we need to detect)
            while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
                   tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
                   tokenizer_match(parser->tokenizer, TOK_COMMENT)) {
                tokenizer_advance(parser->tokenizer);
            }

            // Check for case terminators AFTER skipping separators
            // Order matters: check ;;& before ;& before ;;
            if (tokenizer_match(parser->tokenizer, TOK_CASE_CONTINUE)) {
                // ;;& - continue testing next patterns
                terminator = CASE_TERM_CONTINUE;
                tokenizer_advance(parser->tokenizer);
                break;
            }
            if (tokenizer_match(parser->tokenizer, TOK_CASE_FALLTHROUGH)) {
                // ;& - fall through to next item without testing
                terminator = CASE_TERM_FALLTHROUGH;
                tokenizer_advance(parser->tokenizer);
                break;
            }
            if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
                token_t *next = tokenizer_peek(parser->tokenizer);
                if (next && next->type == TOK_SEMICOLON) {
                    // ;; - break (default)
                    terminator = CASE_TERM_BREAK;
                    tokenizer_advance(parser->tokenizer); // First ;
                    tokenizer_advance(parser->tokenizer); // Second ;
                    break;
                }
                // Single semicolon - consume it and continue parsing commands
                tokenizer_advance(parser->tokenizer);
                continue;
            }

            // Use parse_logical_expression to handle && and || in case body
            node_t *command = parse_logical_expression(parser);
            if (!command) {
                break; // Can't parse more commands
            }
            if (!commands) {
                commands = command;
            } else {
                // Link commands as siblings
                node_t *last = commands;
                while (last->next_sibling) {
                    last = last->next_sibling;
                }
                last->next_sibling = command;
            }

            // Don't skip separators here - we need to detect terminators
            // explicitly
        }

        // Add commands as child of case item
        if (commands) {
            add_child_node(case_item, commands);
        }

        // Encode terminator in pattern string with prefix byte
        // Format: "<terminator_char><pattern>" where terminator_char is:
        // '0' = CASE_TERM_BREAK (;;)
        // '1' = CASE_TERM_FALLTHROUGH (;&)
        // '2' = CASE_TERM_CONTINUE (;;&)
        if (case_item->val.str) {
            size_t old_len = strlen(case_item->val.str);
            char *new_pattern = malloc(old_len + 2);
            if (new_pattern) {
                new_pattern[0] = '0' + (char)terminator;
                strcpy(new_pattern + 1, case_item->val.str);
                free(case_item->val.str);
                case_item->val.str = new_pattern;
            }
        }

        // Only skip non-semicolon separators (newlines, whitespace)
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }

        // Add case item to case statement
        add_child_node(case_node, case_item);
    }

    // Expect 'esac'
    if (!expect_token_with_help(parser, TOK_ESAC,
                                "'case' statement must end with 'esac'")) {
        free_node_tree(case_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Parse any trailing redirections: case ... esac >/dev/null 2>&1
    if (!parse_trailing_redirections(parser, case_node)) {
        free_node_tree(case_node);
        return NULL;
    }

    return case_node;
}

/**
 * @brief Check if current position is a function definition
 *
 * Looks for word() pattern indicating a function definition.
 *
 * @param parser Parser instance
 * @return true if function definition syntax detected
 */
static bool is_function_definition(parser_t *parser) {
    if (!parser || !parser->tokenizer) {
        return false;
    }

    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || !token_is_word_like(current->type)) {
        return false;
    }

    token_t *next = tokenizer_peek(parser->tokenizer);
    if (!next || next->type != TOK_LPAREN) {
        return false;
    }

    // We have word() - this looks like a function definition
    return true;
}

/**
 * @brief Validate function name for POSIX compliance
 *
 * Function names must start with letter/underscore and contain
 * only alphanumeric characters and underscores.
 *
 * @param name Function name to validate
 * @return true if name is valid POSIX function name
 */
static bool is_valid_posix_function_name(const char *name) {
    if (!name || !*name) {
        return false;
    }

    // First character must be letter or underscore
    if (!isalpha(*name) && *name != '_') {
        return false;
    }

    // Remaining characters must be alphanumeric or underscore
    for (const char *p = name + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return false;
        }
    }

    return true;
}

/**
 * @brief Parse a function definition
 *
 * Parses both forms:
 * - name() { commands; }
 * - function name() { commands; }
 *
 * Supports optional parameters with default values (non-POSIX extension).
 *
 * @param parser Parser instance
 * @return Function definition AST node
 */
static node_t *parse_function_definition(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    bool has_function_keyword = false;

    // Handle "function" keyword form
    if (current && current->type == TOK_FUNCTION) {
        has_function_keyword = true;
        tokenizer_advance(parser->tokenizer);
        current = tokenizer_current(parser->tokenizer);
    }

    /* Push context for better error messages */
    parser_push_context(parser, "parsing function definition");

    if (!current || !token_is_word_like(current->type)) {
        parser_error_add_with_help(
            parser, SHELL_ERR_INVALID_FUNCTION,
            "syntax: name() { commands; } or function name { commands; }",
            "expected function name");
        parser_pop_context(parser);
        return NULL;
    }

    // Create function node
    node_t *function_node = new_node(NODE_FUNCTION);
    if (!function_node) {
        parser_pop_context(parser);
        return NULL;
    }

    // Store function name
    function_node->val.str = strdup(current->text);
    function_node->val_type = VAL_STR;
    if (!function_node->val.str) {
        free_node_tree(function_node);
        return NULL;
    }

    // POSIX compliance: validate function name in posix mode
    if (is_posix_mode_enabled() &&
        !is_valid_posix_function_name(current->text)) {
        parser_error_add_with_help(
            parser, SHELL_ERR_INVALID_FUNCTION,
            "POSIX function names must start with a letter or underscore",
            "invalid function name in POSIX mode: '%s'", current->text);
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer);

    // ksh/bash style: "function name { }" - parentheses are optional
    // POSIX style: "name() { }" - parentheses required
    // Check if we have '(' or if we're going directly to '{'
    current = tokenizer_current(parser->tokenizer);
    if (has_function_keyword && current && current->type == TOK_LBRACE) {
        // "function name { }" form - skip parameter parsing, go to body
        goto parse_function_body;
    }

    // Expect '('
    if (!expect_token_with_help(
            parser, TOK_LPAREN,
            "POSIX functions require '()' after the function name")) {
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Parse parameters between ( and )
    function_param_t *params = NULL;
    function_param_t *last_param = NULL;
    int param_count = 0;
    (void)param_count; /* Reserved for parameter limit validation */

    // Check if we have parameters (not immediate ')')
    current = tokenizer_current(parser->tokenizer);
    while (current && current->type != TOK_RPAREN && current->type != TOK_EOF) {
        // Expect parameter name (word token)
        if (!token_is_word_like(current->type)) {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "function parameters must be valid identifiers",
                "expected parameter name");
            free_function_params(params);
            free_node_tree(function_node);
            parser_pop_context(parser);
            return NULL;
        }

        char *param_name = strdup(current->text);
        if (!param_name) {
            free_function_params(params);
            free_node_tree(function_node);
            return NULL;
        }

        tokenizer_advance(parser->tokenizer);
        current = tokenizer_current(parser->tokenizer);

        // Check for default value (= token)
        char *default_value = NULL;
        if (current && current->type == TOK_ASSIGN) {
            tokenizer_advance(parser->tokenizer); // Skip '='
            current = tokenizer_current(parser->tokenizer);

            if (!current || (!token_is_word_like(current->type) &&
                             current->type != TOK_STRING &&
                             current->type != TOK_EXPANDABLE_STRING)) {
                parser_error_add_with_help(
                    parser, SHELL_ERR_INVALID_FUNCTION,
                    "use name=value syntax for default parameter values",
                    "expected default value after '='");
                free(param_name);
                free_function_params(params);
                free_node_tree(function_node);
                parser_pop_context(parser);
                return NULL;
            }

            default_value = strdup(current->text);
            if (!default_value) {
                free(param_name);
                free_function_params(params);
                free_node_tree(function_node);
                return NULL;
            }

            tokenizer_advance(parser->tokenizer);
            current = tokenizer_current(parser->tokenizer);
        }

        // Create parameter structure
        function_param_t *param =
            create_function_param(param_name, default_value);
        if (!param) {
            free(param_name);
            free(default_value);
            free_function_params(params);
            free_node_tree(function_node);
            return NULL;
        }

        // Add to parameter list
        if (!params) {
            params = param;
        } else {
            last_param->next = param;
        }
        last_param = param;
        param_count++;

        free(param_name);
        free(default_value);

        // Check for comma or end
        if (current && current->type == TOK_RPAREN) {
            // End of parameters
            break;
        } else if (current && current->text &&
                   strcmp(current->text, ",") == 0) {
            tokenizer_advance(parser->tokenizer); // Skip comma
            current = tokenizer_current(parser->tokenizer);
            // Continue to next parameter
        } else {
            parser_error_add_with_help(parser, SHELL_ERR_INVALID_FUNCTION,
                                       "separate parameters with commas",
                                       "expected ',' or ')' after parameter");
            free_function_params(params);
            free_node_tree(function_node);
            parser_pop_context(parser);
            return NULL;
        }
    }

    // Expect ')'
    if (!expect_token_with_help(parser, TOK_RPAREN,
                                "function parameter list must end with ')'")) {
        free_function_params(params);
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Store parameters in the function node
    // We need a way to pass this to the executor
    // Create a special parameter info string to embed in the node
    if (params) {
        // Encode parameter info as JSON-like string for later parsing
        char *param_info = malloc(2048);
        if (param_info) {
            strcpy(param_info, "PARAMS{");
            function_param_t *p = params;
            bool first = true;
            while (p) {
                if (!first)
                    strcat(param_info, ",");
                strcat(param_info, p->name);
                if (p->default_value) {
                    strcat(param_info, "=");
                    strcat(param_info, p->default_value);
                }
                p = p->next;
                first = false;
            }
            strcat(param_info, "}");

            // Store in function node's string value temporarily
            if (function_node->val.str) {
                char *old_name = function_node->val.str;
                function_node->val.str =
                    malloc(strlen(old_name) + strlen(param_info) + 2);
                strcpy(function_node->val.str, old_name);
                strcat(function_node->val.str, "|");
                strcat(function_node->val.str, param_info);
                free(old_name);
            }
            free(param_info);
        }

        // Clean up params since we've encoded them
        free_function_params(params);
    }

parse_function_body:
    // Skip separators before the body
    skip_separators(parser);

    // The function body may be any compound command (POSIX function_body
    // is `compound_command`; bash/zsh extend this to (( )), [[ ]], select).
    // Brace-group bodies use the inline sibling-chain parsing below for AST
    // compactness. All other compound bodies are parsed by their respective
    // compound-command parsers and attached as a single child node.
    token_t *body_start = tokenizer_current(parser->tokenizer);
    if (body_start && body_start->type != TOK_LBRACE) {
        node_t *body = NULL;
        switch (body_start->type) {
        case TOK_LPAREN:
            body = parse_subshell(parser);
            break;
        case TOK_DOUBLE_LPAREN:
            body = parse_arithmetic_command(parser);
            break;
        case TOK_DOUBLE_LBRACKET:
            body = parse_extended_test(parser);
            break;
        case TOK_IF:
            body = parse_if_statement(parser);
            break;
        case TOK_WHILE:
            body = parse_while_statement(parser);
            break;
        case TOK_UNTIL:
            body = parse_until_statement(parser);
            break;
        case TOK_FOR:
            body = parse_for_statement(parser);
            break;
        case TOK_CASE:
            body = parse_case_statement(parser);
            break;
        case TOK_SELECT:
            body = parse_select_statement(parser);
            break;
        default:
            parser_error_add_with_help(
                parser, SHELL_ERR_UNEXPECTED_TOKEN,
                "function body must be a compound command",
                "expected '{', '(', '((', '[[', if, while, until, for, "
                "case, or select");
            free_node_tree(function_node);
            parser_pop_context(parser);
            return NULL;
        }
        if (!body) {
            free_node_tree(function_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(function_node, body);
        parser_pop_context(parser);

        // Trailing redirections after the body: `f() ( ... ) > out`.
        // The compound-body parsers (subshell, if, etc.) consume their own
        // trailing redirections, so any redirection that reaches here is
        // attached to the function definition itself.
        if (!parse_trailing_redirections(parser, function_node)) {
            free_node_tree(function_node);
            return NULL;
        }
        return function_node;
    }

    // Brace-group body — consume '{' and parse until '}'
    if (!expect_token_with_help(parser, TOK_LBRACE,
                                "function body must be enclosed in braces { } "
                                "or be a compound command")) {
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    // Skip separators after '{'
    skip_separators(parser);

    // Parse function body until '}'
    node_t *body = NULL;
    while (!tokenizer_match(parser->tokenizer, TOK_RBRACE) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            break; // Can't parse more commands
        }

        if (!body) {
            body = command;
        } else {
            // Link commands as siblings
            node_t *last = body;
            while (last->next_sibling) {
                last = last->next_sibling;
            }
            last->next_sibling = command;
        }

        // Skip separators after command
        skip_separators(parser);
    }

    // Add body as child of function
    if (body) {
        add_child_node(function_node, body);
    }

    // Expect '}'
    if (!expect_token_with_help(parser, TOK_RBRACE,
                                "function body must end with '}'")) {
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    // Trailing redirections after `}`: `f() { ...; } > out 2>&1`.
    // Without this, the redirection token would be left at command position
    // and the next statement would error with "expected command name".
    if (!parse_trailing_redirections(parser, function_node)) {
        free_node_tree(function_node);
        return NULL;
    }

    return function_node;
}

/**
 * @brief Parse an arithmetic command (( expr ))
 *
 * Parses the Bash-style arithmetic command that evaluates an expression
 * and returns success (0) if non-zero, failure (1) if zero.
 *
 * Grammar: (( arithmetic_expression ))
 *
 * @param parser Parser instance
 * @return Arithmetic command AST node with expression in val.str
 */
static node_t *parse_arithmetic_command(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_DOUBLE_LPAREN) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN, "expected '(('");
        return NULL;
    }

    /* Capture location for arithmetic command */
    source_location_t arith_loc =
        token_to_source_location(current, parser->source_name);

    // Consume ((
    tokenizer_advance(parser->tokenizer);

    // Create arithmetic command node
    node_t *arith_node = new_node_at(NODE_ARITH_CMD, arith_loc);
    if (!arith_node) {
        return NULL;
    }

    // Collect the arithmetic expression until ))
    // We need to handle nested parentheses within the expression
    char *expr = NULL;
    size_t expr_len = 0;
    size_t expr_capacity = 256;
    int paren_depth = 0;

    expr = malloc(expr_capacity);
    if (!expr) {
        free_node_tree(arith_node);
        return NULL;
    }
    expr[0] = '\0';

    // Parse tokens until we find )) at depth 0
    while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
        current = tokenizer_current(parser->tokenizer);
        if (!current) {
            break;
        }

        // Check for )) - end of arithmetic command
        if (current->type == TOK_DOUBLE_RPAREN && paren_depth == 0) {
            break;
        }

        // Track nested parentheses
        if (current->type == TOK_LPAREN) {
            paren_depth++;
        } else if (current->type == TOK_RPAREN) {
            if (paren_depth > 0) {
                paren_depth--;
            }
        }

        // Append token text to expression
        size_t token_len = strlen(current->text);
        if (expr_len + token_len + 2 > expr_capacity) {
            expr_capacity = (expr_len + token_len + 2) * 2;
            char *new_expr = realloc(expr, expr_capacity);
            if (!new_expr) {
                free(expr);
                free_node_tree(arith_node);
                return NULL;
            }
            expr = new_expr;
        }

        // Add space between tokens for readability (except at start)
        // But don't add space before operator characters that might form
        // multi-character operators (==, !=, <=, >=, &&, ||, +=, -=, *=, /=,
        // %=, ++, --, etc.)
        bool is_operator_char =
            (current->text[0] == '=' || current->text[0] == '!' ||
             current->text[0] == '<' || current->text[0] == '>' ||
             current->text[0] == '&' || current->text[0] == '|' ||
             current->text[0] == '+' || current->text[0] == '-' ||
             current->text[0] == '*' || current->text[0] == '/' ||
             current->text[0] == '%');
        bool prev_is_operator =
            (expr_len > 0 &&
             (expr[expr_len - 1] == '=' || expr[expr_len - 1] == '!' ||
              expr[expr_len - 1] == '<' || expr[expr_len - 1] == '>' ||
              expr[expr_len - 1] == '&' || expr[expr_len - 1] == '|' ||
              expr[expr_len - 1] == '+' || expr[expr_len - 1] == '-' ||
              expr[expr_len - 1] == '*' || expr[expr_len - 1] == '/' ||
              expr[expr_len - 1] == '%'));

        if (expr_len > 0 && expr[expr_len - 1] != ' ' &&
            current->type != TOK_RPAREN && expr[expr_len - 1] != '(' &&
            !(is_operator_char && prev_is_operator)) {
            expr[expr_len++] = ' ';
            expr[expr_len] = '\0';
        }

        strcat(expr, current->text);
        expr_len = strlen(expr);

        tokenizer_advance(parser->tokenizer);
    }

    // Expect ))
    if (!tokenizer_match(parser->tokenizer, TOK_DOUBLE_RPAREN)) {
        parser_error_add(parser, SHELL_ERR_UNCLOSED_SUBST, "expected '))'");
        free(expr);
        free_node_tree(arith_node);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer); // consume ))

    // Trim whitespace from expression
    while (expr_len > 0 && expr[expr_len - 1] == ' ') {
        expr[--expr_len] = '\0';
    }
    char *start = expr;
    while (*start == ' ') {
        start++;
    }
    if (start != expr) {
        memmove(expr, start, strlen(start) + 1);
    }

    arith_node->val.str = expr;
    arith_node->val_type = VAL_STR;

    return arith_node;
}

/**
 * @brief Parse an array literal (a b c)
 *
 * Parses the Bash-style array literal syntax that creates an indexed array.
 * Elements are separated by whitespace. Supports nested expansions.
 *
 * Grammar: ( [element ...] )
 *
 * @param parser Parser instance
 * @return Array literal AST node with elements as children
 */
static node_t *parse_array_literal(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_LPAREN) {
        parser_error_add(parser, SHELL_ERR_INVALID_ARRAY, "expected '('");
        return NULL;
    }

    /* Capture location for array literal */
    source_location_t array_loc =
        token_to_source_location(current, parser->source_name);

    // Consume (
    tokenizer_advance(parser->tokenizer);

    // Create array literal node
    node_t *array_node = new_node_at(NODE_ARRAY_LITERAL, array_loc);
    if (!array_node) {
        return NULL;
    }

    // Parse elements until )
    while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        current = tokenizer_current(parser->tokenizer);
        if (!current) {
            break;
        }

        // Skip whitespace
        if (current->type == TOK_WHITESPACE || current->type == TOK_NEWLINE) {
            tokenizer_advance(parser->tokenizer);
            continue;
        }

        // Check for [index]=value syntax
        if (current->type == TOK_LBRACKET) {
            // Parse indexed element [n]=value
            tokenizer_advance(parser->tokenizer); // consume [

            // Collect index expression
            char *index_str = NULL;
            size_t index_len = 0;

            while (!tokenizer_match(parser->tokenizer, TOK_RBRACKET) &&
                   !tokenizer_match(parser->tokenizer, TOK_EOF)) {
                token_t *idx_token = tokenizer_current(parser->tokenizer);
                if (!idx_token)
                    break;

                size_t tlen = strlen(idx_token->text);
                char *new_idx = realloc(index_str, index_len + tlen + 1);
                if (!new_idx) {
                    free(index_str);
                    free_node_tree(array_node);
                    return NULL;
                }
                index_str = new_idx;
                strcpy(index_str + index_len, idx_token->text);
                index_len += tlen;

                tokenizer_advance(parser->tokenizer);
            }

            if (!tokenizer_match(parser->tokenizer, TOK_RBRACKET)) {
                parser_error_add(parser, SHELL_ERR_INVALID_ARRAY,
                                 "expected ']' in array subscript");
                free(index_str);
                free_node_tree(array_node);
                return NULL;
            }
            tokenizer_advance(parser->tokenizer); // consume ]

            // Expect =
            if (!tokenizer_match(parser->tokenizer, TOK_ASSIGN)) {
                parser_error_add(parser, SHELL_ERR_INVALID_ARRAY,
                                 "expected '=' after array index");
                free(index_str);
                free_node_tree(array_node);
                return NULL;
            }
            tokenizer_advance(parser->tokenizer); // consume =

            // Get value
            current = tokenizer_current(parser->tokenizer);
            char *value_str = NULL;
            if (current &&
                (token_is_word_like(current->type) ||
                 current->type == TOK_VARIABLE || current->type == TOK_STRING ||
                 current->type == TOK_EXPANDABLE_STRING)) {
                value_str = strdup(current->text);
                tokenizer_advance(parser->tokenizer);
            } else {
                value_str = strdup(""); // Empty value
            }

            // Create element node with index:value format
            node_t *elem_node = new_node(NODE_VAR);
            if (!elem_node) {
                free(index_str);
                free(value_str);
                free_node_tree(array_node);
                return NULL;
            }

            // Store as "[index]=value" for later processing
            size_t total_len = 1 + (index_str ? strlen(index_str) : 0) + 2 +
                               (value_str ? strlen(value_str) : 0) + 1;
            char *combined = malloc(total_len);
            if (combined) {
                snprintf(combined, total_len, "[%s]=%s",
                         index_str ? index_str : "0",
                         value_str ? value_str : "");
                elem_node->val.str = combined;
                elem_node->val_type = VAL_STR;
            }

            free(index_str);
            free(value_str);
            add_child_node(array_node, elem_node);
        }
        // Regular element (no explicit index)
        else if (token_is_word_like(current->type) ||
                 current->type == TOK_VARIABLE || current->type == TOK_STRING ||
                 current->type == TOK_EXPANDABLE_STRING ||
                 current->type == TOK_ARITH_EXP ||
                 current->type == TOK_COMMAND_SUB) {

            node_t *elem_node = NULL;

            // Create appropriate node type
            switch (current->type) {
            case TOK_STRING:
                elem_node = new_node(NODE_STRING_LITERAL);
                break;
            case TOK_EXPANDABLE_STRING:
                elem_node = new_node(NODE_STRING_EXPANDABLE);
                break;
            case TOK_ARITH_EXP:
                elem_node = new_node(NODE_ARITH_EXP);
                break;
            case TOK_COMMAND_SUB:
                elem_node = new_node(NODE_COMMAND_SUB);
                break;
            default:
                elem_node = new_node(NODE_VAR);
                break;
            }

            if (!elem_node) {
                free_node_tree(array_node);
                return NULL;
            }

            elem_node->val.str = strdup(current->text);
            elem_node->val_type = VAL_STR;
            add_child_node(array_node, elem_node);

            tokenizer_advance(parser->tokenizer);
        } else {
            // Unknown token type in array literal - skip it
            tokenizer_advance(parser->tokenizer);
        }
    }

    // Expect )
    if (!tokenizer_match(parser->tokenizer, TOK_RPAREN)) {
        parser_error_add(parser, SHELL_ERR_INVALID_ARRAY,
                         "expected ')' to close array literal");
        free_node_tree(array_node);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer); // consume )

    return array_node;
}

/**
 * @brief Parse an extended test command [[ expression ]]
 *
 * Parses Bash/Zsh-style extended test expressions. These support:
 * - String comparisons: ==, !=, <, >
 * - Pattern matching: == with glob patterns (unquoted RHS)
 * - Regex matching: =~ with POSIX extended regex
 * - Logical operators: &&, ||, !
 * - Grouping: ( expression )
 * - File tests: -f, -d, -e, -r, -w, -x, etc.
 * - String tests: -z, -n
 *
 * Unlike [ ], extended tests:
 * - Don't perform word splitting on variables
 * - Don't perform glob expansion on arguments
 * - Support && and || directly (not -a and -o)
 * - Support < and > for string comparison without escaping
 *
 * Grammar: [[ conditional_expression ]]
 *
 * @param parser Parser instance
 * @return Extended test AST node with expression in val.str
 */
static node_t *parse_extended_test(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_DOUBLE_LBRACKET) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN, "expected '[['");
        return NULL;
    }

    // Capture source location BEFORE advancing (advance frees current token)
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    // Consume [[
    tokenizer_advance(parser->tokenizer);

    // Create extended test node with source location
    node_t *test_node = new_node_at(NODE_EXTENDED_TEST, loc);
    if (!test_node) {
        parser_error_add(parser, SHELL_ERR_OUT_OF_MEMORY,
                         "failed to create extended test node");
        return NULL;
    }

    // Collect the test expression until ]]
    // We need to handle nested (( )) for grouping within [[ ]]
    char *expr = NULL;
    size_t expr_len = 0;
    size_t expr_capacity = 256;
    int paren_depth = 0;
    bool in_regex = false; // Track if we're parsing a regex pattern after =~

    expr = malloc(expr_capacity);
    if (!expr) {
        free_node_tree(test_node);
        return NULL;
    }
    expr[0] = '\0';

    // Parse tokens until we find ]] at depth 0
    while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
        current = tokenizer_current(parser->tokenizer);
        if (!current) {
            break;
        }

        // Check for ]] - end of extended test
        if (current->type == TOK_DOUBLE_RBRACKET && paren_depth == 0) {
            break;
        }

        // Check for logical operators that end regex context
        // && and || are expression separators in [[ ]]
        if ((current->type == TOK_LOGICAL_AND ||
             current->type == TOK_LOGICAL_OR) &&
            paren_depth == 0) {
            in_regex = false;
        }

        // Track nested parentheses for grouping
        if (current->type == TOK_LPAREN) {
            paren_depth++;
        } else if (current->type == TOK_RPAREN) {
            if (paren_depth > 0) {
                paren_depth--;
            }
        }

        // Append token text to expression
        size_t token_len = strlen(current->text);
        if (expr_len + token_len + 2 > expr_capacity) {
            expr_capacity = (expr_len + token_len + 2) * 2;
            char *new_expr = realloc(expr, expr_capacity);
            if (!new_expr) {
                free(expr);
                free_node_tree(test_node);
                return NULL;
            }
            expr = new_expr;
        }

        // Determine if we should skip adding a space before this token
        bool skip_space = false;

        if (in_regex) {
            // In regex mode: don't add spaces between regex tokens
            // This keeps ^hello$ as one unit instead of ^ hello $
            skip_space = true;
        } else {
            // Normal mode: add spaces between tokens with some exceptions
            bool is_operator_char =
                (current->text[0] == '=' || current->text[0] == '!' ||
                 current->text[0] == '<' || current->text[0] == '>' ||
                 current->text[0] == '&' || current->text[0] == '|' ||
                 current->text[0] == '~');
            bool prev_is_operator =
                (expr_len > 0 &&
                 (expr[expr_len - 1] == '=' || expr[expr_len - 1] == '!' ||
                  expr[expr_len - 1] == '<' || expr[expr_len - 1] == '>' ||
                  expr[expr_len - 1] == '&' || expr[expr_len - 1] == '|'));

            // Don't add space:
            // - Before ) or after (
            // - After ) when followed by ( (for regex groups like )(
            // - Between consecutive operators
            skip_space = (current->type == TOK_RPAREN) ||
                         (expr_len > 0 && expr[expr_len - 1] == '(') ||
                         (expr_len > 0 && expr[expr_len - 1] == ')' &&
                          current->type == TOK_LPAREN) ||
                         (is_operator_char && prev_is_operator);
        }

        if (expr_len > 0 && expr[expr_len - 1] != ' ' && !skip_space) {
            expr[expr_len++] = ' ';
            expr[expr_len] = '\0';
        }

        strcat(expr, current->text);
        expr_len = strlen(expr);

        // Check if we just added the =~ operator - next tokens are regex
        if (current->type == TOK_REGEX_MATCH) {
            in_regex = true;
        }

        tokenizer_advance(parser->tokenizer);
    }

    // Expect ]]
    if (!tokenizer_match(parser->tokenizer, TOK_DOUBLE_RBRACKET)) {
        parser_error_add(parser, SHELL_ERR_UNCLOSED_CONTROL,
                         "expected ']]' to close extended test");
        free(expr);
        free_node_tree(test_node);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer); // consume ]]

    // Trim whitespace from expression
    while (expr_len > 0 && expr[expr_len - 1] == ' ') {
        expr[--expr_len] = '\0';
    }
    char *start = expr;
    while (*start == ' ') {
        start++;
    }
    if (start != expr) {
        memmove(expr, start, strlen(start) + 1);
    }

    test_node->val.str = expr;
    test_node->val_type = VAL_STR;

    return test_node;
}

/**
 * @brief Parse a process substitution <(cmd) or >(cmd)
 *
 * Process substitution allows a command's output or input to be used
 * as a filename. <(cmd) provides a filename that reads from cmd's stdout,
 * >(cmd) provides a filename that writes to cmd's stdin.
 *
 * Grammar: <( command_list ) | >( command_list )
 *
 * @param parser Parser instance
 * @return Process substitution AST node
 */
static node_t *parse_process_substitution(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current) {
        return NULL;
    }

    // Determine type based on token
    node_type_t node_type;
    const char *op_name;
    if (current->type == TOK_PROC_SUB_IN) {
        node_type = NODE_PROC_SUB_IN;
        op_name = "<(";
    } else if (current->type == TOK_PROC_SUB_OUT) {
        node_type = NODE_PROC_SUB_OUT;
        op_name = ">(";
    } else {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected '<(' or '>('");
        return NULL;
    }

    // Check if feature is enabled
    if (!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
        parser_error_add(parser, SHELL_ERR_FEATURE_DISABLED,
                         "process substitution not enabled");
        return NULL;
    }

    // Create the process substitution node with source location
    source_location_t loc =
        token_to_source_location(current, parser->source_name);
    node_t *proc_sub_node = new_node_at(node_type, loc);
    if (!proc_sub_node) {
        parser_error_add(parser, SHELL_ERR_OUT_OF_MEMORY,
                         "failed to create process substitution node");
        return NULL;
    }

    // Consume the <( or >( token
    tokenizer_advance(parser->tokenizer);

    // Skip whitespace after opening
    skip_separators(parser);

    // Parse commands until ')'
    while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; // End of input
            }
            free_node_tree(proc_sub_node);
            return NULL;
        }

        add_child_node(proc_sub_node, command);

        // Skip separators between commands
        skip_separators(parser);
    }

    // Expect ')'
    if (!expect_token(parser, TOK_RPAREN)) {
        free_node_tree(proc_sub_node);
        return NULL;
    }

    // Store the operator for debugging
    proc_sub_node->val.str = strdup(op_name);
    proc_sub_node->val_type = VAL_STR;

    return proc_sub_node;
}

/**
 * @brief Parse an array element assignment arr[index]=value
 *
 * Parses the Bash-style array element assignment.
 * Also handles += for append operations.
 *
 * Grammar: name[subscript]=value | name[subscript]+=value
 *
 * @param parser Parser instance
 * @param var_name Name of the array variable
 * @return Array assignment AST node
 */
