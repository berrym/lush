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

#include "brace_match.h"
#include "debug.h"
#include "dequote.h"
#include "executor.h"
#include "identifier.h"
#include "lle/unicode_compare.h"
#include "node.h"
#include "shell_mode.h"
#include "tokenizer.h"

/// Lifted from executor.c (non-static). Used by parse_function_definition
/// to deep-copy a parsed body so zsh's `function NAME1 NAME2 { body }`
/// multi-name form can produce one FUNCTION node per name with each
/// owning its own independent body.
extern node_t *node_copy(node_t *node);

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Forward declarations
static node_t *parse_command_list(parser_t *parser);
static node_t *parse_pipeline(parser_t *parser);
static node_t *parse_simple_command(parser_t *parser);
static bool parse_command_suffix(parser_t *parser, node_t *command);
static char *parse_scalar_assignment_string(parser_t *parser,
                                            node_t **out_array_node);
static node_t *finish_assignment_or_prefix(parser_t *parser,
                                           node_t *first_assignment);

static node_t *parse_brace_group(parser_t *parser);
static node_t *parse_subshell(parser_t *parser);
static node_t *parse_if_statement(parser_t *parser);
static node_t *parse_while_statement(parser_t *parser);
static node_t *parse_until_statement(parser_t *parser);
static node_t *parse_repeat_statement(parser_t *parser);
static node_t *parse_for_statement(parser_t *parser);
static node_t *parse_case_statement(parser_t *parser);
static node_t *parse_function_definition(parser_t *parser);
static bool is_function_definition(parser_t *parser);
static node_t *parse_logical_expression(parser_t *parser);
static node_t *parse_and_or(parser_t *parser);
static node_t *parse_redirection(parser_t *parser);
static bool is_redirection_token(token_type_t type);
static bool parse_trailing_redirections(parser_t *parser,
                                        node_t *compound_node);

/// Forward declarations for extended language features
static node_t *parse_arithmetic_command(parser_t *parser);
static node_t *parse_array_literal(parser_t *parser);

/// Forward declarations for extended language features
static node_t *parse_extended_test(parser_t *parser);

/// Forward declarations for the extended-test [[ ]] conditional-expression
/// recursive-descent grammar (or -> and -> not -> primary).
static node_t *cond_or_expr(parser_t *parser);
static node_t *cond_and_expr(parser_t *parser);
static node_t *cond_not_expr(parser_t *parser);
static node_t *cond_primary(parser_t *parser);

/// Forward declarations for extended language features
static node_t *parse_process_substitution(parser_t *parser);

/// Forward declarations for extended language features
static node_t *parse_select_statement(parser_t *parser);
static node_t *parse_time_command(parser_t *parser);
static node_t *parse_coproc(parser_t *parser);

/// Forward declarations for extended language features (Zsh)
static node_t *parse_anonymous_function(parser_t *parser);

/// Typed-function form (`fn name(p: kind, ...) [-> kind] { body }`).
static node_t *parse_fn_declaration(parser_t *parser);
static node_t *parse_fn_call_expression(parser_t *parser);
static node_t *parse_let_fn_call(parser_t *parser);
static node_t *parse_fn_return_statement(parser_t *parser);
static bool is_valid_fn_kind_name(const char *text);
static bool is_let_fn_call_form(parser_t *parser);
static bool is_typed_fn_call_statement(parser_t *parser);

/// Forward declarations for POSIX compliance
bool is_posix_mode_enabled(void);
static bool collect_pending_heredocs(parser_t *parser);
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

    /// Initialize structured error collection
    parser->source_name = source_name ? source_name : "<stdin>";
    parser->error_collector =
        shell_error_collector_new(input, strlen(input), parser->source_name, 0);

    /// Initialize parser context stack
    parser->context_depth = 0;
    for (size_t i = 0; i < PARSER_CONTEXT_MAX; i++) {
        parser->context_stack[i] = NULL;
    }

    /// Initialize recursion depth tracking
    parser->recursion_depth = 0;

    /// No heredocs pending body collection yet
    parser->pending_heredoc_count = 0;

    /// Typed-function body parsing depth (0 outside any `fn` body).
    parser->fn_body_depth = 0;

    return parser;
}

void parser_set_source_name(parser_t *parser, const char *source_name) {
    if (!parser) {
        return;
    }
    parser->source_name = source_name ? source_name : "<stdin>";

    /// Update error collector's source_name too if it exists
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
 * Structured Error Collection
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

    /// Get current token for location
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    /// Create the error
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        /// Fallback to legacy error system
        set_parser_error(parser, "parse error");
        return;
    }

    /// Try to get source line for context display
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

    /// Add to collector if available
    if (parser->error_collector) {
        shell_error_collector_add(parser->error_collector, error);
    } else {
        /// Fallback: just set legacy error and free
        set_parser_error(parser,
                         error->message ? error->message : "parse error");
        shell_error_free(error);
    }

    /// Also set legacy error flag for compatibility
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
        /// Fallback to legacy error display
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

    /// Assert that we're not underflowing - indicates mismatched enter/exit
    if (parser->recursion_depth == 0) {
        /// In debug builds this would be an assertion failure.
        /// In release, we just prevent underflow.
#ifndef NDEBUG
        shell_error_t *err = shell_error_create(
            SHELL_ERR_ASSERTION, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "parser internal: recursion depth underflow");
        if (err) {
            shell_error_set_suggestion(
                err, "mismatched parser_enter_construct/parser_exit_construct "
                     "pair -- please report with a reproducer");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
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
    size_t last_pos;    ///< current token's start position last iteration
    size_t no_progress; ///< iterations seen at the same position
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

    /// Create the error at the caller-supplied location
    va_list args;
    va_start(args, fmt);
    shell_error_t *error =
        shell_error_createv(code, SHELL_SEVERITY_ERROR, loc, fmt, args);
    va_end(args);

    if (!error) {
        /// Fallback to legacy error system
        set_parser_error(parser, "parse error");
        return;
    }

    /// Try to get source line for context display
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

    /// Add parser context stack to error
    for (size_t i = 0; i < parser->context_depth; i++) {
        shell_error_push_context(error, "%s", parser->context_stack[i]);
    }

    /// Add help suggestion if provided
    if (help) {
        shell_error_set_suggestion(error, help);
    }

    /// Add to collector if available
    if (parser->error_collector) {
        shell_error_collector_add(parser->error_collector, error);
    } else {
        /// Fallback: just set legacy error and free
        set_parser_error(parser,
                         error->message ? error->message : "parse error");
        shell_error_free(error);
    }

    /// Also set legacy error flag for compatibility
    parser->has_error = true;
}

void parser_error_add_with_help(parser_t *parser, shell_error_code_t code,
                                const char *help, const char *fmt, ...) {
    if (!parser) {
        return;
    }

    /// Default location: current token's position. Delegate the rest
    /// to the _at variant so the two paths share one implementation.
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    va_list args;
    va_start(args, fmt);
    /// Convert variadic args into a fixed string then forward — the
    /// _at variant takes its own variadic. shell_error already handles
    /// vsnprintf internally; here we just need to hand off the formatted
    /// message. Simplest: format here, pass as a literal "%s" + buffer.
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

    /// Skip initial whitespace and comments
    while (tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
           tokenizer_match(parser->tokenizer, TOK_COMMENT) ||
           tokenizer_match(parser->tokenizer, TOK_NEWLINE)) {
        tokenizer_advance(parser->tokenizer);
    }

    if (tokenizer_match(parser->tokenizer, TOK_EOF)) {
        return NULL; /// Empty input
    }

    node_t *ast = parse_command_list(parser);

    /// A heredoc operator on the last line with no body and no
    /// line-terminating newline never reaches the skip_separators
    /// collection trigger. Flush here so the unterminated-heredoc
    /// error is still reported rather than silently dropped.
    if (parser->pending_heredoc_count > 0) {
        collect_pending_heredocs(parser);
    }

    return ast;
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
    node_t *ast = parse_command_list(parser);
    if (parser && parser->pending_heredoc_count > 0) {
        collect_pending_heredocs(parser);
    }
    return ast;
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
        /// A newline that follows a `<<delim` operator is the trigger to
        /// collect the deferred heredoc body/bodies. collect_pending_
        /// heredocs repositions the tokenizer past the last terminator,
        /// so do NOT also advance -- re-evaluate the loop condition
        /// against the freshly tokenized post-heredoc token.
        if (tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
            parser->pending_heredoc_count > 0) {
            collect_pending_heredocs(parser);
            continue;
        }
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

        /// Skip separators between commands
        skip_separators(parser);

        /// Check again for terminator after skipping separators
        if (tokenizer_match(parser->tokenizer, terminator) ||
            tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; /// End of input
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
    /// Create a command list node to hold all commands
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

        /// Skip separators between commands
        skip_separators(parser);

        /// Check again for terminators after skipping separators
        if (tokenizer_match(parser->tokenizer, TOK_ELSE) ||
            tokenizer_match(parser->tokenizer, TOK_ELIF) ||
            tokenizer_match(parser->tokenizer, TOK_FI) ||
            tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; /// End of input
            }
            free_node_tree(command_list);
            return NULL;
        }

        /// Add command as child of the command list
        add_child_node(command_list, command);

        /// Skip separators after command
        skip_separators(parser);
    }

    return command_list;
}

/**
 * @brief Parse an and-or list (&& and ||)
 *
 * Handles the and_or grammar level, creating NODE_LOGICAL_AND or
 * NODE_LOGICAL_OR nodes for compound commands. Does NOT consume a trailing
 * `&` -- that is a list separator, handled by parse_logical_expression in list
 * position. A condition (if/while/until) parses its and_or with this function
 * directly, so a trailing `&` there is left for the caller to reject rather
 * than silently backgrounding the condition.
 *
 * @param parser Parser instance
 * @return AST node for the and-or list
 */
static node_t *parse_and_or(parser_t *parser) {
    /// Track recursion depth for stack overflow protection
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
        tokenizer_advance(parser->tokenizer); /// consume operator

        /// Skip whitespace after operator
        skip_separators(parser);

        node_t *right = parse_pipeline(parser);
        if (!right) {
            /// A missing right operand is a syntax error. parse_simple_command
            /// returns NULL without recording one when the next token is a
            /// closing keyword (esac/fi/done/then/else/elif/do), so report it
            /// here. Beyond the diagnostic this sets has_error before the
            /// free below, which lets collect_pending_heredocs discard a
            /// here-document left pending on `left` instead of dereferencing
            /// the node this free releases.
            if (!parser->has_error) {
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNEXPECTED_TOKEN,
                    "a binary operator must be followed by a command",
                    "expected a command after '%s'",
                    op_type == TOK_LOGICAL_AND ? "&&" : "||");
            }
            free_node_tree(left);
            parser_exit_recursion(parser);
            return NULL;
        }

        /// Create logical operator node
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
 * @brief Parse an and-or list, optionally backgrounded by a trailing `&`
 *
 * A trailing `&` backgrounds the entire and-or list -- `a | b &` and
 * `a && b &` background the whole construct, not just the last pipeline stage
 * or operand (POSIX: `&` is a list separator terminating the preceding
 * and_or). This is the list-position entry point; conditions call parse_and_or
 * directly so a `&` after a condition is not consumed here.
 *
 * @param parser Parser instance
 * @return The and_or, wrapped in NODE_BACKGROUND when followed by `&`
 */
static node_t *parse_logical_expression(parser_t *parser) {
    node_t *node = parse_and_or(parser);
    if (!node) {
        return NULL;
    }

    if (tokenizer_match(parser->tokenizer, TOK_AND) ||
        tokenizer_match(parser->tokenizer, TOK_BACKGROUND_DISOWN)) {
        tokenizer_advance(parser->tokenizer); /// consume & / &| / &!

        node_t *background_node = new_node(NODE_BACKGROUND);
        if (!background_node) {
            free_node_tree(node);
            return NULL;
        }
        add_child_node(background_node, node);
        node = background_node;
    }

    return node;
}

/**
 * @brief Reject a trailing `&` on the condition of a compound command
 *
 * A `&` (`&`, `&|`, or `&!`) immediately after the condition of an
 * if/elif/while/until is a common mistake: `&` backgrounds a list element, but
 * a condition is not a list element, and a backgrounded condition would return
 * 0 asynchronously and break control flow (an `if` always takes the
 * then-branch, a `while` loops forever). This records a targeted structured
 * error -- naming the construct and how to fix it -- instead of the generic
 * missing-terminator diagnostic. Emitted at the `&` token's location.
 *
 * @param parser    Parser instance
 * @param construct Keyword for the message ("if" / "elif" / "while" / "until")
 * @return true if a `&` was present and an error was recorded (abort the parse)
 */
static bool reject_backgrounded_condition(parser_t *parser,
                                          const char *construct) {
    if (tokenizer_match(parser->tokenizer, TOK_AND) ||
        tokenizer_match(parser->tokenizer, TOK_BACKGROUND_DISOWN)) {
        parser_error_add_with_help(
            parser, SHELL_ERR_MALFORMED_CONSTRUCT,
            "remove the '&': a condition runs synchronously. To run the whole "
            "compound command in the background, put '&' after its closing "
            "keyword, e.g. `if ...; fi &`.",
            "the condition of '%s' cannot be backgrounded with '&'", construct);
        return true;
    }
    return false;
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

        /// Skip separators, newlines, comments -- and collect any
        /// deferred heredoc bodies triggered by a line-ending newline.
        skip_separators(parser);

        if (tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; /// End of input
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

        /// Check for end of command list
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
    /// Track recursion depth for stack overflow protection
    if (!parser_enter_recursion(parser)) {
        return NULL;
    }

    /// Check for negation prefix (! pipeline)
    bool negate = false;
    token_t *current = tokenizer_current(parser->tokenizer);
    if (current && current->type == TOK_WORD && current->length == 1 &&
        current->text[0] == '!') {
        negate = true;
        tokenizer_advance(parser->tokenizer); /// consume !
        /// Skip whitespace after !
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
        /// Check if this is |& (pipe stderr too)
        bool pipe_stderr = tokenizer_match(parser->tokenizer, TOK_PIPE_STDERR);
        tokenizer_advance(parser->tokenizer); /// consume | or |&

        /// Skip newlines, whitespace, and comments after a pipe -- this allows
        /// a multiline pipeline and a trailing comment before the next stage
        /// (`cmd | # note` then the stage on the following line).
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
               tokenizer_match(parser->tokenizer, TOK_COMMENT)) {
            /// `cmd <<EOF |` then newline: the heredoc body follows that
            /// newline. Collect it before consuming the newline.
            if (tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
                parser->pending_heredoc_count > 0) {
                collect_pending_heredocs(parser);
                continue;
            }
            tokenizer_advance(parser->tokenizer);
        }

        node_t *right = parse_pipeline(parser);
        if (!right) {
            /// A missing stage after a pipe is a syntax error that
            /// parse_simple_command leaves unreported for a closing keyword;
            /// record it so the diagnostic surfaces and has_error is set
            /// before the free (see the matching note in parse_and_or, which
            /// keeps a here-document pending on `left` from being collected
            /// against a freed node).
            if (!parser->has_error) {
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNEXPECTED_TOKEN,
                    "a pipe must be followed by a command",
                    "expected a command after '%s'", pipe_stderr ? "|&" : "|");
            }
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

        /// Use val.sint to indicate if stderr should also be piped
        /// 0 = normal pipe (stdout only), 1 = |& (stdout + stderr)
        pipe_node->val.sint = pipe_stderr ? 1 : 0;
        pipe_node->val_type = VAL_SINT;

        add_child_node(pipe_node, left);
        add_child_node(pipe_node, right);
        left = pipe_node;
    }

    /// A trailing `&` is NOT handled here: it backgrounds the whole and-or
    /// list, not the last pipeline stage. parse_logical_expression applies it
    /// above this level (otherwise the right-recursive pipe parse would bind
    /// `a | b &` as `a | (b &)`). The negation prefix, by contrast, is a
    /// genuine pipeline operator and stays here.

    /// Wrap in negation node if ! prefix was present
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
 * @brief Strip POSIX-unquoted backslash escapes from a word token's text,
 *        emitting a parallel per-character quote-provenance map.
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
 * Returns a freshly malloc'd string the caller owns, and via *out_prov a
 * parallel per-character quote map (ESCAPED for a de-escaped `\X` output
 * character, UNQUOTED for a bare one; same length as the dequoted string) used
 * to build a fused word's node_t.quote_prov for its bare word-token segments
 * (#498). Returns NULL and sets *out_prov to NULL on OOM.
 */
static char *posix_unquoted_dequote_prov(const char *text, char **out_prov) {
    if (out_prov) {
        *out_prov = NULL;
    }
    if (!text || !out_prov) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    char *prov = malloc(len + 1);
    if (!out || !prov) {
        free(out);
        free(prov);
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\\' && i + 1 < len) {
            out[w] = text[i + 1];
            prov[w] = QUOTE_PROV_ESCAPED;
            w++;
            i++;
            continue;
        }
        out[w] = text[i];
        prov[w] = QUOTE_PROV_UNQUOTED;
        w++;
    }
    out[w] = '\0';
    prov[w] = '\0';
    *out_prov = prov;
    return out;
}

/**
 * @brief Append a token's text to a growable buffer, re-encoding it from a
 *        per-character quote-provenance map into the smuggled-quote form the
 *        value expander consumes.
 *
 * The assignment value/word expanders (colon_segmented_tilde_expand,
 * expand_if_needed) reconstruct quote context by re-scanning embedded
 * backslashes: a `\X` is a POSIX-unquoted literal character. The parser must
 * therefore re-encode a dequoted token back into that form. Per whole token
 * this is easy (a single-quoted token holds every character literal; a
 * double-quoted token has `~`/`'` escaped). But the mixed-quote tokenizer fuses
 * a quoted segment with an adjacent unquoted run into ONE token (`"b":~/c`), so
 * a per-token rule mis-encodes the unquoted part. Driven by the per-character
 * map this re-encodes each character by its own quote context: an unquoted
 * character is verbatim (a bare `~` stays expandable); a backslash-escaped
 * literal and a double-quoted `~`/`'` are re-emitted `\X`; a single-quoted
 * character is escaped `\X` unless it is a newline. Backslash escaping is used
 * throughout rather than re-wrapping runs in `'...'`: the value expander's
 * single-quote-span scanner and its backslash rule interact badly when real
 * spans and `\'` escapes coexist in one string, so a fused word must smuggle
 * its literals as backslash escapes only.
 *
 * @return true on success, false on allocation failure (buffer left valid).
 */
static bool prov_reencode_append(char **buf, size_t *len, size_t *cap,
                                 const char *text, const char *prov, size_t n) {
    /// Worst case per input character is 2 output bytes (a backslash escape);
    /// reserve that plus a NUL. Grow once up front so the loop needs no bounds
    /// checks. Backslash escaping is used throughout rather than `'...'` spans:
    /// the value expander's single-quote-span scanner and its backslash rule
    /// interact badly when real spans and `\'` escapes are mixed in one string,
    /// so a fused mixed-quote word must smuggle its literals as backslash
    /// escapes only (this is also what the pre-fix per-token encoder produced).
    size_t need = *len + n * 2 + 1;
    if (need >= *cap) {
        size_t new_cap = (*cap > 0 ? *cap : 16);
        while (need >= new_cap) {
            new_cap *= 2;
        }
        char *grown = realloc(*buf, new_cap);
        if (!grown) {
            return false;
        }
        *buf = grown;
        *cap = new_cap;
    }
    for (size_t k = 0; k < n; k++) {
        char ch = text[k];
        char p = prov ? prov[k] : QUOTE_PROV_UNQUOTED;
        bool escape = false;
        switch (p) {
        case QUOTE_PROV_ESCAPED:
            /// Unquoted backslash-escaped literal: re-emit `\X` so the literal
            /// survives (a `\~` stays `~`, a `\'` stays a literal quote).
            escape = true;
            break;
        case QUOTE_PROV_DOUBLE:
            /// Double-quoted: `$`/`` ` ``/`$(...)` still expand (bare), but a
            /// `~` must not tilde-expand and a `'` must stay literal for the
            /// downstream single-quote scanner.
            escape = (ch == '~' || ch == '\'');
            break;
        case QUOTE_PROV_SINGLE:
            /// Single-quoted: fully literal. Backslash-escape EVERY character
            /// so the run always begins with `\`, which makes it immune to any
            /// first-character dispatch in the value expander -- `$name`, the
            /// `@`/`%` kind sigils (lush default), a leading `~` -- present or
            /// future. The downstream POSIX-unquoted-backslash rule strips each
            /// `\` back to the literal. The one exception is a newline: a
            /// `\<newline>` is a line continuation and would be removed, so a
            /// literal newline is left bare (a bare newline is not special).
            escape = (ch != '\n');
            break;
        default: /// QUOTE_PROV_UNQUOTED: genuine bare character, expands.
            escape = false;
            break;
        }
        if (escape) {
            (*buf)[(*len)++] = '\\';
        }
        (*buf)[(*len)++] = ch;
    }
    (*buf)[*len] = '\0';
    return true;
}

/// True when a collected token is a word-like token carrying a real
/// (non-escaped) `'` or `"` -- i.e. the tokenizer absorbed a quoted `[...]`
/// subscript into the word (#631 Phase 2c). A normal word token never contains
/// an unescaped quote (the word loop breaks at one), and the reader's quoted
/// tokens (TOK_STRING / TOK_EXPANDABLE_STRING / ...) already stripped their
/// delimiters, so this uniquely identifies the absorbed bracket word. Such a
/// word must be dequoted via lush_dequote_span and typed NODE_STRING_EXPANDABLE
/// so its quotes suppress globbing, exactly like any other quoted word.
static bool word_token_has_quote(token_type_t type, const char *text) {
    if (type == TOK_STRING || type == TOK_EXPANDABLE_STRING ||
        type == TOK_ARITH_EXP || type == TOK_COMMAND_SUB ||
        type == TOK_BACKQUOTE || type == TOK_VARIABLE) {
        return false;
    }
    if (!text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            return true;
        }
    }
    return false;
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

    /// Acceptance test routed through the canonical predicate so a
    /// leading-`[` glob like `[5].txt` is collected as a word here
    /// instead of being mis-parsed as the `[` test builtin (#154).
    if (!token_is_argument_word_token(arg_token->type)) {
        return false;
    }

    /// Adjacency-collection: gather consecutive arg-like tokens that have
    /// no whitespace between them. `pre$VAR` becomes one logical arg
    /// instead of two.
    typedef struct {
        token_type_t type;
        char *text;
        bool glob_qualified;
        /// Copy of the token's per-character quote provenance (strlen(text)
        /// bytes, NOT NUL-terminated), or NULL. Copied because the live token
        /// is freed as collection advances. Consumed by the magic_equal
        /// provenance builder to re-encode a fused mixed-quote word per
        /// character.
        char *quote_prov;
    } token_info_t;

    token_info_t *collected_tokens = NULL;
    int token_count = 0;
    /// Shell-word adjacency uses the consumed input span (end_position),
    /// not strlen(text): quoted strings strip their delimiters from text
    /// but their end_position covers the entire `"..."` span. POSIX 2.10.2
    /// word concatenation depends on this being correct.
    size_t last_end_pos = arg_token->end_position;

    while (arg_token && token_is_argument_word_token(arg_token->type)) {

        token_info_t *new_tokens =
            realloc(collected_tokens, (token_count + 1) * sizeof(token_info_t));
        if (!new_tokens) {
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text),
                    free(collected_tokens[i].quote_prov);
            }
            free(collected_tokens);
            parser->has_error = true;
            return false;
        }
        collected_tokens = new_tokens;

        collected_tokens[token_count].type = arg_token->type;
        collected_tokens[token_count].text = strdup(arg_token->text);
        collected_tokens[token_count].glob_qualified =
            arg_token->glob_qualified;
        /// Copy the quote-provenance map (strlen(text) bytes, parallel to the
        /// text; not NUL-terminated). The live token is freed as collection
        /// advances, so a borrow would dangle.
        collected_tokens[token_count].quote_prov = NULL;
        if (arg_token->quote_prov && arg_token->text) {
            size_t tl = strlen(arg_token->text);
            char *qp = malloc(tl > 0 ? tl : 1);
            if (qp) {
                memcpy(qp, arg_token->quote_prov, tl);
                collected_tokens[token_count].quote_prov = qp;
            }
        }
        token_count++;

        last_end_pos = arg_token->end_position;
        tokenizer_advance(parser->tokenizer);
        token_t *next_token = tokenizer_current(parser->tokenizer);

        /// Stop collecting if next token has whitespace before it.
        if (next_token && next_token->position != last_end_pos) {
            break;
        }
        arg_token = next_token;
    }

    /// Build a single arg node from collected tokens.
    if (token_count == 1) {
        /// A word token carrying a quote is an absorbed quoted `[...]`
        /// subscript
        /// (#631 Phase 2c): dequote it and type it EXPANDABLE so its quotes
        /// suppress globbing like any other quoted word.
        bool word_quote = word_token_has_quote(collected_tokens[0].type,
                                               collected_tokens[0].text);
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
            arg_node = new_node(word_quote ? NODE_STRING_EXPANDABLE : NODE_VAR);
            break;
        }
        if (arg_node) {
            if (word_quote) {
                /// Strip one level of quoting from the absorbed bracket word,
                /// keeping the provenance map so the expander protects
                /// single-quoted / escaped bytes.
                char *dtext = NULL, *dprov = NULL;
                dequote_flags_t f;
                if (lush_dequote_span(collected_tokens[0].text,
                                      strlen(collected_tokens[0].text), &dtext,
                                      &dprov, &f)) {
                    arg_node->val.str = dtext;
                    arg_node->quote_prov = dprov;
                } else {
                    arg_node->val.str = strdup(collected_tokens[0].text);
                }
            } else {
                arg_node->val.str = strdup(collected_tokens[0].text);
            }
            arg_node->val_type = VAL_STR;
            arg_node->glob_qualified = collected_tokens[0].glob_qualified;
            /// A fused mixed-quote word (`'$x'y`, `"b":~/c`) arrives as one
            /// TOK_EXPANDABLE_STRING with a per-character quote map; carry it
            /// onto the node so the word expander decides per character (#498).
            if (arg_node->type == NODE_STRING_EXPANDABLE && !word_quote &&
                collected_tokens[0].quote_prov && arg_node->val.str) {
                size_t qn = strlen(arg_node->val.str);
                char *qp = malloc(qn + 1);
                if (qp) {
                    memcpy(qp, collected_tokens[0].quote_prov, qn);
                    qp[qn] = '\0';
                    arg_node->quote_prov = qp;
                }
            }
            add_child_node(parent, arg_node);
        }
    } else if (token_count > 1) {
        /// Multi-token concatenation: build a single NODE_STRING_EXPANDABLE
        /// with the concatenated text (matches the existing semantics in
        /// parse_simple_command).
        ///
        /// Word-context tokens (TOK_WORD, keywords-as-words, etc.) carry
        /// raw `\X` pairs because the tokenizer defers escape resolution.
        /// For unquoted shell input, POSIX-unquoted quote-removal collapses
        /// any such `\X` to literal X. Doing that here, before
        /// concatenation, has two effects: (1) the `$DIR/a\ b` family of
        /// inputs no longer ships backslashes through to argv, fixing #90
        /// for the multi-token case; (2) any backslashes that survive into
        /// the concatenated string came from a quoted segment
        /// (TOK_EXPANDABLE_STRING kept its `\X` for the four DQ-meaningful
        /// escapes), so the executor can apply double-quote rules safely
        /// in `expand_quoted_string` without a quote-context flag.
        ///
        /// Tokens that already had escape semantics applied (or that are
        /// sub-expressions evaluated later) pass through untouched:
        ///   TOK_STRING               — single-quoted, no escapes ever
        ///   TOK_EXPANDABLE_STRING    — double-quoted, DQ rules later
        ///   TOK_ARITH_EXP            — `$((...))`, evaluated as arith
        ///   TOK_COMMAND_SUB          — `$(...)`, evaluated as cmd
        ///   TOK_BACKQUOTE            — `` `...` ``, evaluated as cmd
        ///   TOK_VARIABLE             — `$VAR`, no escapes by construction
        /// calloc (not malloc) so a mid-loop failure leaves the not-yet-filled
        /// tail NULL -- the cleanup loops free(dequoted[i]) over the full
        /// range.
        char **dequoted = calloc((size_t)token_count, sizeof(char *));
        if (!dequoted) {
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text),
                    free(collected_tokens[i].quote_prov);
            }
            free(collected_tokens);
            parser->has_error = true;
            return false;
        }
        /// Parallel per-character quote provenance for each dequoted segment,
        /// concatenated below into node_t.quote_prov so the word expander can
        /// decide per character in a fused mixed-quote word (#498).
        char **dequoted_prov = calloc((size_t)token_count, sizeof(char *));
        if (!dequoted_prov) {
            free(dequoted);
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text),
                    free(collected_tokens[i].quote_prov);
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
                if (dequoted[i]) {
                    size_t tl = strlen(dequoted[i]);
                    dequoted_prov[i] = malloc(tl + 1);
                    if (dequoted_prov[i]) {
                        if (collected_tokens[i].quote_prov) {
                            /// The token's map is parallel to its (unchanged)
                            /// text.
                            memcpy(dequoted_prov[i],
                                   collected_tokens[i].quote_prov, tl);
                        } else {
                            /// No per-character map: classify the whole segment
                            /// -- a single-/double-quoted reader token, or an
                            /// expansion (`$var`/`$(...)`/`$((...))`) that must
                            /// still expand, so UNQUOTED.
                            char cls = collected_tokens[i].type == TOK_STRING
                                           ? QUOTE_PROV_SINGLE
                                           : (collected_tokens[i].type ==
                                                      TOK_EXPANDABLE_STRING
                                                  ? QUOTE_PROV_DOUBLE
                                                  : QUOTE_PROV_UNQUOTED);
                            memset(dequoted_prov[i], cls, tl);
                        }
                        dequoted_prov[i][tl] = '\0';
                    }
                }
                break;
            default:
                if (word_token_has_quote(collected_tokens[i].type,
                                         collected_tokens[i].text)) {
                    /// Absorbed quoted `[...]` subscript word (#631 Phase 2c):
                    /// strip one quote level via the shared primitive, keeping
                    /// provenance so a fused segment protects/expands per byte.
                    dequote_flags_t f;
                    lush_dequote_span(collected_tokens[i].text,
                                      strlen(collected_tokens[i].text),
                                      &dequoted[i], &dequoted_prov[i], &f);
                } else {
                    /// Word-like tokens: word-context backslashes collapse; the
                    /// companion emits the parallel provenance (bare U,
                    /// de-escaped E).
                    dequoted[i] = posix_unquoted_dequote_prov(
                        collected_tokens[i].text, &dequoted_prov[i]);
                }
                break;
            }
            if (!dequoted[i] || !dequoted_prov[i]) {
                ok = false;
                break;
            }
            total_len += strlen(dequoted[i]);
        }
        if (!ok) {
            for (int i = 0; i < token_count; i++) {
                free(dequoted[i]);
                free(dequoted_prov[i]);
            }
            free(dequoted);
            free(dequoted_prov);
            for (int i = 0; i < token_count; i++) {
                free(collected_tokens[i].text),
                    free(collected_tokens[i].quote_prov);
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
            /// Choose the node type so the executor's glob / word-split
            /// gate fires correctly. NODE_STRING_EXPANDABLE means
            /// "this came from a double-quoted string -- no glob, no
            /// split". If every collected token was an unquoted bare
            /// word (no TOK_STRING / TOK_EXPANDABLE_STRING in the run)
            /// the concatenation is a regular shell word and must
            /// glob-expand: a leading `[5].txt` after the parser fix
            /// for #154 reaches here, and emitting it as
            /// NODE_STRING_EXPANDABLE would suppress the bracket-class
            /// glob.
            bool any_quoted = false;
            for (int i = 0; i < token_count; i++) {
                if (collected_tokens[i].type == TOK_STRING ||
                    collected_tokens[i].type == TOK_EXPANDABLE_STRING ||
                    word_token_has_quote(collected_tokens[i].type,
                                         collected_tokens[i].text)) {
                    any_quoted = true;
                    break;
                }
            }
            /// A fused glob qualifier on any collected segment flags the
            /// whole concatenation. If more segments follow the fused one
            /// (`"$f"(N)bar`) the `(...)` is no longer at the string end, so
            /// parse_glob_qualifier finds no qualifier and the value stays
            /// literal -- degraded, never dropped.
            bool any_glob_qualified = false;
            for (int i = 0; i < token_count; i++) {
                if (collected_tokens[i].glob_qualified) {
                    any_glob_qualified = true;
                    break;
                }
            }

            /// Concatenate the per-character quote provenance parallel to
            /// `concatenated` so the word expander can decide per character
            /// (#498). Only meaningful for a fused word that carries a quote
            /// (any_quoted -> NODE_STRING_EXPANDABLE); a fully-unquoted word
            /// keeps the untouched NODE_VAR expander path.
            char *node_quote_prov = NULL;
            if (any_quoted) {
                node_quote_prov = malloc(total_len + 1);
                if (node_quote_prov) {
                    size_t pw = 0;
                    for (int i = 0; i < token_count; i++) {
                        size_t dl = strlen(dequoted[i]);
                        memcpy(node_quote_prov + pw, dequoted_prov[i], dl);
                        pw += dl;
                    }
                    node_quote_prov[pw] = '\0';
                }
            }

            /// For an assignment-shaped word that has a quoted segment
            /// (`E=~/a:"~/b"`), also record the provenance value an assignment
            /// RHS carries, so the assignment-builtin / magic_equal tilde path
            /// (build_argv_from_ast) expands ONLY the unquoted tilde segments.
            /// Same per-token treatment as parse_scalar_assignment_string:
            /// single-quoted tokens re-wrapped '...', double-quoted tokens have
            /// `~` and `'` escaped as \~ / \', every other token appended
            /// verbatim (unquoted words keep a bare `~` to expand). val.str and
            /// all other consumers are untouched (#488). Only built when the
            /// word contains a `=` past its first byte; the executor validates
            /// the identifier before using it.
            char *magic_equal_prov = NULL;
            const char *eqp = any_quoted ? strchr(concatenated, '=') : NULL;
            if (eqp && eqp != concatenated) {
                size_t pcap = total_len * 2 + 4;
                magic_equal_prov = malloc(pcap);
                if (magic_equal_prov) {
                    size_t plen = 0;
                    for (int i = 0; i < token_count; i++) {
                        const char *t = collected_tokens[i].text;
                        size_t tl = t ? strlen(t) : 0;
                        /// When the token carries a per-character quote map
                        /// (any mixed-quote-reader token except ANSI-C-disabled
                        /// ones), re-encode per character so a fused quoted +
                        /// unquoted run (`"b":~/c`) escapes only the quoted
                        /// characters. The helper grows the buffer itself.
                        if (collected_tokens[i].quote_prov) {
                            if (!prov_reencode_append(
                                    &magic_equal_prov, &plen, &pcap, t,
                                    collected_tokens[i].quote_prov, tl)) {
                                free(magic_equal_prov);
                                magic_equal_prov = NULL;
                                break;
                            }
                            continue;
                        }
                        if (plen + tl * 2 + 3 > pcap) {
                            pcap = (plen + tl * 2 + 3) * 2;
                            char *pg = realloc(magic_equal_prov, pcap);
                            if (!pg) {
                                free(magic_equal_prov);
                                magic_equal_prov = NULL;
                                break;
                            }
                            magic_equal_prov = pg;
                        }
                        if (collected_tokens[i].type == TOK_STRING) {
                            magic_equal_prov[plen++] = '\'';
                            memcpy(magic_equal_prov + plen, t, tl);
                            plen += tl;
                            magic_equal_prov[plen++] = '\'';
                        } else if (collected_tokens[i].type ==
                                   TOK_EXPANDABLE_STRING) {
                            for (size_t k = 0; k < tl; k++) {
                                char ch = t[k];
                                if (ch == '\'' || ch == '~') {
                                    magic_equal_prov[plen++] = '\\';
                                }
                                magic_equal_prov[plen++] = ch;
                            }
                        } else {
                            memcpy(magic_equal_prov + plen, t, tl);
                            plen += tl;
                        }
                    }
                    if (magic_equal_prov) {
                        magic_equal_prov[plen] = '\0';
                    }
                }
            }

            node_t *arg_node =
                new_node(any_quoted ? NODE_STRING_EXPANDABLE : NODE_VAR);
            if (arg_node) {
                arg_node->val.str = concatenated;
                arg_node->val_type = VAL_STR;
                arg_node->glob_qualified = any_glob_qualified;
                arg_node->magic_equal_value = magic_equal_prov;
                arg_node->quote_prov = node_quote_prov;
                add_child_node(parent, arg_node);
            } else {
                free(concatenated);
                free(magic_equal_prov);
                free(node_quote_prov);
            }
        }
        for (int i = 0; i < token_count; i++) {
            free(dequoted[i]);
            free(dequoted_prov[i]);
        }
        free(dequoted);
        free(dequoted_prov);
    }

    for (int i = 0; i < token_count; i++) {
        free(collected_tokens[i].text);
        free(collected_tokens[i].quote_prov);
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

    /// Check for brace group
    if (current->type == TOK_LBRACE) {
        return parse_brace_group(parser);
    }

    /// Check for arithmetic command (( expr ))
    if (current->type == TOK_DOUBLE_LPAREN &&
        shell_mode_allows(FEATURE_ARITH_COMMAND)) {
        return parse_arithmetic_command(parser);
    }

    /// Check for extended test [[ expr ]]
    if (current->type == TOK_DOUBLE_LBRACKET &&
        shell_mode_allows(FEATURE_EXTENDED_TEST)) {
        return parse_extended_test(parser);
    }

    /// Check for anonymous function () { body } or subshell
    if (current->type == TOK_LPAREN) {
        /// Peek ahead to check for anonymous function syntax: () { ... }
        if (shell_mode_allows(FEATURE_ANONYMOUS_FUNCTIONS)) {
            token_t *next = tokenizer_peek(parser->tokenizer);
            if (next && next->type == TOK_RPAREN) {
                /// Save position before advancing to check third token
                size_t saved_pos = current->position;
                size_t saved_line = parser->tokenizer->line;
                size_t saved_col = parser->tokenizer->column;

                tokenizer_advance(parser->tokenizer); /// consume (
                tokenizer_advance(parser->tokenizer); /// consume )
                token_t *after_paren = tokenizer_current(parser->tokenizer);

                if (after_paren && after_paren->type == TOK_LBRACE) {
                    /// This is an anonymous function () { body }
                    return parse_anonymous_function(parser);
                }

                /// Not anonymous function - restore tokenizer state
                parser->tokenizer->position = saved_pos;
                parser->tokenizer->line = saved_line;
                parser->tokenizer->column = saved_col;
                tokenizer_refresh_from_position(parser->tokenizer);
                current = tokenizer_current(parser->tokenizer);
            }
        }
        /// Regular subshell
        return parse_subshell(parser);
    }

    /// Check for control structures
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
        case TOK_REPEAT:
            return parse_repeat_statement(parser);
        case TOK_FN:
            return parse_fn_declaration(parser);
        default:
            /// Other keywords (like ESAC, FI, DONE, etc.) are handled by their
            /// parent constructs - returning NULL here lets the parent detect
            /// them
            return NULL;
        }
    }

    /// Statement-position typed-fn call: `name(args)` or `name()` not
    /// followed by `{`. The peek-and-restore recognizer below
    /// distinguishes this from a POSIX function definition, which has
    /// the same `name(` prefix but requires `{` after `)`.
    ///
    /// The recognizer is checked BEFORE is_function_definition because
    /// both grammars accept `name(...)` at the head; the difference is
    /// what follows the closing paren.
    ///
    /// is_typed_fn_call_statement advances and restores the tokenizer;
    /// the restore frees the prior current+lookahead and re-tokenizes
    /// from the saved position. The outer `current` pointer becomes
    /// dangling, so it must be re-fetched whether the recognizer
    /// matched or not (mirrors the let-fn-call dispatch above).
    if (token_is_word_like(current->type) && current->text) {
        bool is_call = is_typed_fn_call_statement(parser);
        current = tokenizer_current(parser->tokenizer);
        if (is_call) {
            return parse_fn_call_expression(parser);
        }
    }

    /// Check for function definition (word followed by ())
    if (token_is_word_like(current->type) && is_function_definition(parser)) {
        return parse_function_definition(parser);
    }

    /// Typed `return EXPR` inside a typed-function body. Outside such a
    /// body, `return` remains a POSIX command and falls through to the
    /// builtin path below.
    if (parser->fn_body_depth > 0 && token_is_word_like(current->type) &&
        current->text && strcmp(current->text, "return") == 0) {
        return parse_fn_return_statement(parser);
    }

    /// Typed `let name = call(args)` capture. Recognized only when the
    /// `let` is followed by IDENT '=' IDENT '(' with the IDENT and '('
    /// adjacent (no whitespace between them). Anything else with `let`
    /// falls through to the existing arithmetic-let builtin path below.
    /// is_let_fn_call_form advances and restores the tokenizer; the
    /// outer `current` pointer is invalidated by the restore (refresh
    /// frees the prior tokens), so re-fetch it whether the recognizer
    /// matched or not.
    if (token_is_word_like(current->type) && current->text &&
        strcmp(current->text, "let") == 0) {
        bool is_call = is_let_fn_call_form(parser);
        current = tokenizer_current(parser->tokenizer);
        if (is_call) {
            return parse_let_fn_call(parser);
        }
    }

    /// Check for assignment (word followed by =) or array assignment
    if (token_is_word_like(current->type)) {
        token_t *next = tokenizer_peek(parser->tokenizer);

        /// Check for array element assignment: arr[n]=value or arr[n]+=value
        /// Tokenizer produces: arr[n] (WORD) + = (ASSIGN) + value (WORD)
        /// or: arr[n] (WORD) + += (PLUS_ASSIGN) + value (WORD)
        if (shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
            const char *bracket = strchr(current->text, '[');
            /// Delimit the subscript with the canonical span finder so a `]`
            /// inside quotes / a nested $(...) / ${...} does not close it
            /// early (#631). The interior is kept raw here; the executor
            /// canonicalizes it to the key/index.
            subscript_span_t span =
                bracket ? scan_subscript_bounds(bracket, strlen(bracket))
                        : (subscript_span_t){0, 0, false, false};
            const char *close_bracket =
                (bracket && span.is_valid) ? bracket + span.close : NULL;

            /// Check for arr[...] followed by = or +=
            if (bracket && close_bracket && next &&
                (next->type == TOK_ASSIGN || next->type == TOK_PLUS_ASSIGN)) {
                /// This is an array element assignment
                /// Token format: arr[n] = value (separate tokens)

                /// Extract variable name (part before [)
                size_t var_len = bracket - current->text;
                char *var_name = malloc(var_len + 1);
                if (!var_name) {
                    return NULL;
                }
                strncpy(var_name, current->text, var_len);
                var_name[var_len] = '\0';

                /// Extract subscript (part between [ and ])
                size_t sub_len = close_bracket - bracket - 1;
                char *subscript = malloc(sub_len + 1);
                if (!subscript) {
                    free(var_name);
                    return NULL;
                }
                strncpy(subscript, bracket + 1, sub_len);
                subscript[sub_len] = '\0';

                /// Check if += or =
                bool is_append = (next->type == TOK_PLUS_ASSIGN);

                tokenizer_advance(parser->tokenizer); /// consume arr[n]
                tokenizer_advance(parser->tokenizer); /// consume = or +=

                /// Get value token - must copy before advancing
                token_t *value_token = tokenizer_current(parser->tokenizer);
                char *value_str = NULL;
                if (value_token &&
                    (token_is_word_like(value_token->type) ||
                     value_token->type == TOK_VARIABLE ||
                     value_token->type == TOK_STRING ||
                     value_token->type == TOK_EXPANDABLE_STRING)) {
                    value_str = strdup(value_token->text);
                    tokenizer_advance(parser->tokenizer); /// consume value
                }
                if (!value_str) {
                    value_str = strdup("");
                }

                /// Create array assignment node
                node_t *assign_node = new_node(NODE_ARRAY_ASSIGN);
                if (!assign_node) {
                    free(var_name);
                    free(subscript);
                    return NULL;
                }

                assign_node->val.str = var_name; /// Transfer ownership
                assign_node->val_type = VAL_STR;

                /// Create subscript child node
                node_t *subscript_node = new_node(NODE_VAR);
                if (!subscript_node) {
                    free(subscript);
                    free_node_tree(assign_node);
                    return NULL;
                }
                subscript_node->val.str = subscript;
                subscript_node->val_type = VAL_STR;
                add_child_node(assign_node, subscript_node);

                /// Create value child node
                node_t *value_node = new_node(NODE_VAR);
                if (!value_node) {
                    free_node_tree(assign_node);
                    return NULL;
                }

                if (is_append) {
                    /// Encode append with "+=" prefix
                    size_t vlen = strlen(value_str);
                    char *append_val = malloc(vlen + 3);
                    if (append_val) {
                        strcpy(append_val, "+=");
                        strcat(append_val, value_str);
                        value_node->val.str = append_val;
                        free(value_str);
                    } else {
                        value_node->val.str = value_str; /// Transfer ownership
                    }
                } else {
                    value_node->val.str = value_str; /// Transfer ownership
                }
                value_node->val_type = VAL_STR;
                add_child_node(assign_node, value_node);

                return assign_node;
            }
        }

        if (next &&
            (next->type == TOK_ASSIGN || next->type == TOK_PLUS_ASSIGN)) {
            /// Capture the variable-name token's loc before
            /// parse_scalar_assignment_string advances past it; needed so
            /// the resulting NODE_COMMAND carries a real source location
            /// (otherwise expansion-subsystem errors inside the
            /// assignment's value -- e.g. ${var:?word} -- fall back to
            /// SOURCE_LOC_UNKNOWN at runtime).
            source_location_t assign_loc =
                token_to_source_location(current, parser->source_name);
            node_t *array_node = NULL;
            char *assignment =
                parse_scalar_assignment_string(parser, &array_node);
            if (array_node) {
                /// arr=(a b c) / arr+=(a b c) -- standalone array
                /// assignment (not a valid POSIX cmd_prefix word).
                return array_node;
            }
            if (!assignment) {
                return NULL; /// hard parse/allocation error
            }
            node_t *acmd = new_node_at(NODE_COMMAND, assign_loc);
            if (!acmd) {
                free(assignment);
                return NULL;
            }
            acmd->val.str = assignment;
            acmd->val_type = VAL_STR;
            /// Decide standalone-assignment vs cmd_prefix based on what
            /// follows on the same simple command.
            return finish_assignment_or_prefix(parser, acmd);
        }
    }

    /// Parse regular command
    if (current->type == TOK_ERROR) {
        /// Provide specific error message based on what's unclosed
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

    /// A command substitution or backtick may stand in command-name position
    /// (`$(echo echo) hi`, `` `echo echo` hi ``): its output is expanded first
    /// and becomes the command word (empty output -> null command, exit 0),
    /// exactly as in argument position. The value-kind command-name gate for
    /// named list/map words (SEMANTICS 3.9) is a separate, kind-scoped rule
    /// applied later at expansion; a scalar command substitution is unaffected.
    if (!token_is_word_like(current->type) && current->type != TOK_LBRACKET &&
        current->type != TOK_COMMAND_SUB && current->type != TOK_BACKQUOTE) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected command name, got '%s'",
                         current->text ? current->text
                                       : token_type_name(current->type));
        return NULL;
    }

    /// Create command node with source location from current token
    source_location_t cmd_loc =
        token_to_source_location(current, parser->source_name);
    node_t *command = new_node_at(NODE_COMMAND, cmd_loc);
    if (!command) {
        return NULL;
    }

    /// Set command name
    command->val.str = strdup(current->text);
    command->val_type = VAL_STR;
    /// Record whether the command-name word carried a quoted segment, so
    /// null-word removal can tell an unquoted empty name `$x` (drop -> null
    /// command, exit 0) from a quoted empty name `"$x"` / `''` (keep one
    /// empty word -> command not found). Arguments carry this in their own
    /// node type; the command name's token type is otherwise discarded here.
    command->name_quoted =
        (current->type == TOK_STRING || current->type == TOK_EXPANDABLE_STRING);
    tokenizer_advance(parser->tokenizer);

    if (!parse_command_suffix(parser, command)) {
        free_node_tree(command);
        return NULL;
    }

    return command;
}

/**
 * @brief Parse a command's cmd_suffix: arguments, redirections, process
 *        substitutions, and array-literal arguments.
 *
 * Shared by the regular-command path and the cmd_prefix path
 * (finish_assignment_or_prefix) so the POSIX
 * `cmd_prefix cmd_word cmd_suffix` grammar has exactly one suffix
 * implementation. On failure returns false WITHOUT freeing `command`
 * (the caller owns it and frees on false).
 *
 * @param parser  Parser instance
 * @param command Command node to attach arguments/redirections to
 * @return true on success, false on parse error
 */
static bool parse_command_suffix(parser_t *parser, node_t *command) {
    /// Parse arguments and redirections
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

        /// Check for redirection tokens via the canonical predicate so a
        /// new redirection token type added to is_redirection_token is
        /// picked up here automatically.
        if (is_redirection_token(arg_token->type)) {

            node_t *redir_node = parse_redirection(parser);
            if (!redir_node) {
                return false;
            }

            add_child_node(command, redir_node);
            continue; /// Continue to check for more redirections/arguments
        }
        /// Handle process substitution <(cmd) and >(cmd)
        else if (arg_token->type == TOK_PROC_SUB_IN ||
                 arg_token->type == TOK_PROC_SUB_OUT) {
            node_t *proc_sub_node = parse_process_substitution(parser);
            if (!proc_sub_node) {
                return false;
            }
            add_child_node(command, proc_sub_node);
            continue; /// Continue to check for more arguments
        }
        /// Handle array literal arguments: name=(...) or name+=(...)
        /// This allows declare -A map=([key]=value) to work
        else if (token_is_word_like(arg_token->type) &&
                 shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
            /// Peek ahead to see if this is word followed by = or +=
            /// Check if next token (lookahead) is = or +=, and the one after is
            /// (
            token_t *peek1 = tokenizer_peek(parser->tokenizer);

            if (peek1 &&
                (peek1->type == TOK_ASSIGN || peek1->type == TOK_PLUS_ASSIGN)) {
                /// Check if = or += is immediately adjacent (no whitespace)
                size_t word_end = arg_token->end_position;
                if (peek1->position == word_end) {
                    /// They're adjacent. Now check if ( follows the =
                    /// To check this, we need to look at what comes after peek1
                    size_t assign_end = peek1->end_position;
                    /// Peek at the input directly to see if ( follows
                    if (assign_end < parser->tokenizer->input_length &&
                        parser->tokenizer->input[assign_end] == '(') {
                        /// An array literal `name=(...)` is only meaningful as
                        /// an assignment or as an operand to an
                        /// assignment-aware builtin
                        /// (declare/local/typeset/export/readonly). As an
                        /// argument to any other command the unquoted `(` is a
                        /// syntax error -- bash, zsh, and POSIX sh all reject
                        /// it. lush rejects it too, with a diagnostic, rather
                        /// than silently accepting and flattening the word:
                        /// lush reports why a construct is invalid instead of
                        /// guessing an interpretation the writer did not
                        /// intend.
                        if (!is_assignment_builtin(command->val.str)) {
                            source_location_t loc = token_to_source_location(
                                arg_token, parser->source_name);
                            parser_error_add_with_help_at(
                                parser, SHELL_ERR_UNEXPECTED_TOKEN, loc,
                                "assign an array with `name=(...)`, declare "
                                "one "
                                "with `declare name=(...)`, or quote the word "
                                "to "
                                "pass it literally",
                                "unexpected `(`: an array literal is not valid "
                                "as an argument to `%s`",
                                command->val.str ? command->val.str
                                                 : "this command");
                            return false;
                        }
                        /// This is an array literal argument: name=(...) or
                        /// name+=(...)
                        char *var_name = strdup(arg_token->text);
                        bool is_append = (peek1->type == TOK_PLUS_ASSIGN);

                        tokenizer_advance(
                            parser->tokenizer); /// consume var name
                        tokenizer_advance(
                            parser->tokenizer); /// consume = or +=

                        /// Now parse the array literal
                        node_t *array_node = parse_array_literal(parser);
                        if (!array_node) {
                            free(var_name);
                            return false;
                        }

                        /// Build the argv element with a parser-internal
                        /// sentinel prefix: \x1F (ASCII Unit Separator)
                        /// followed by "name=(...)". The sentinel marks
                        /// this element as the unquoted array-literal
                        /// form so assignment-aware builtins (local,
                        /// declare, typeset, readonly, export) can tell
                        /// it apart from a quoted scalar `name="(...)"`
                        /// which, after quote-stripping in the regular
                        /// argument path, produces the same shape.
                        /// Consumer builtins strip the sentinel before
                        /// touching the name. Regular argv strings
                        /// never contain \x1F.
                        size_t total_len = 1 + /// sentinel
                                           strlen(var_name) +
                                           (is_append ? 2 : 1) +
                                           2; /// name + = or += + ()

                        /// Count total length of elements
                        node_t *elem = array_node->first_child;
                        while (elem) {
                            if (elem->val.str) {
                                total_len +=
                                    strlen(elem->val.str) +
                                    1; /// element + \x1F element separator
                            }
                            elem = elem->next_sibling;
                        }

                        char *arg_str = malloc(total_len + 1);
                        if (arg_str) {
                            arg_str[0] = '\x1F';
                            arg_str[1] = '\0';
                            strcat(arg_str, var_name);
                            strcat(arg_str, is_append ? "+=(" : "=(");
                            /// Join the already-parsed elements with the \x1F
                            /// element separator, not whitespace. The tokenizer
                            /// has stripped the quotes from each element, so a
                            /// quoted element that contained spaces (`"x y"`)
                            /// is now the bare value `x y`; joining with a
                            /// space would fuse it back into the neighbouring
                            /// elements and the consumer could not recover the
                            /// boundary.
                            /// \x1F is the same byte reserved for the outer
                            /// sentinel and never appears in real argv, so the
                            /// consumer (builtin_bind_array_literal) splits on
                            /// it to reproduce the exact element list.
                            elem = array_node->first_child;
                            bool first = true;
                            while (elem) {
                                if (elem->val.str) {
                                    if (!first) {
                                        strcat(arg_str, "\x1F");
                                    }
                                    strcat(arg_str, elem->val.str);
                                    first = false;
                                }
                                elem = elem->next_sibling;
                            }
                            strcat(arg_str, ")");

                            /// Use NODE_STRING_LITERAL to prevent glob/brace
                            /// expansion The array literal syntax [key]=value
                            /// should be passed literally
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
                        continue; /// Skip to next argument
                    }
                }
            }
            /// Not an array literal, fall through to regular argument handling
        }
        /// Handle all argument tokens via the shared helper. The helper
        /// tests acceptance, runs the adjacency-concatenation loop, and
        /// creates the appropriately-classified node attached to command.
        if (!collect_word_argument(parser, command)) {
            if (parser->has_error) {
                return false;
            }
            break; /// Not an arg-like token; stop parsing arguments.
        }
    }

    return true;
}

/**
 * @brief Parse `WORD (= | +=) value...` into a malloc'd assignment string.
 *
 * Precondition: the caller has verified the current token is word-like
 * and the lookahead token is TOK_ASSIGN / TOK_PLUS_ASSIGN. Consumes the
 * variable-name token, the assignment operator, and all adjacent value
 * tokens (applying the issue #98 / #102 quote-rewrap policy).
 *
 * If the value is an array literal `(...)`, no string is produced:
 * *out_array_node is set to the NODE_ARRAY_ASSIGN / NODE_ARRAY_APPEND
 * node and NULL is returned (the caller returns that node standalone).
 *
 * @param parser         Parser instance
 * @param out_array_node Out: set to an array node for arr=(...) forms
 * @return malloc'd "var=value" / "var+=value" string, or NULL (either a
 *         hard error, or an array literal -- distinguished by whether
 *         *out_array_node was set)
 */
static char *parse_scalar_assignment_string(parser_t *parser,
                                            node_t **out_array_node) {
    *out_array_node = NULL;

    token_t *current = tokenizer_current(parser->tokenizer);
    token_t *next = tokenizer_peek(parser->tokenizer);
    bool is_append = (next && next->type == TOK_PLUS_ASSIGN);

    /// Save variable name BEFORE advancing tokenizer
    char *var_name = strdup(current->text);
    if (!var_name) {
        return NULL;
    }

    /// Save end-of-operator position so the value-collection paths below
    /// can enforce the POSIX adjacency rule: `var= word` is an empty
    /// assignment followed by a separate word (the command word or a new
    /// prefix), not `var=word`. Without this check the parser greedily
    /// glues the next word-like token onto the empty value, mis-parsing
    /// `X= /bin/echo hi` as command `hi` with X="/bin/echo".
    size_t assign_op_end = next ? next->end_position : 0;

    tokenizer_advance(parser->tokenizer); /// consume variable name
    tokenizer_advance(parser->tokenizer); /// consume '=' or '+='

    token_t *value = tokenizer_current(parser->tokenizer);
    bool value_adjacent = (value && value->position == assign_op_end);

    /// Check for array literal assignment: arr=(a b c) or arr+=(a b c).
    /// `arr= (sub shell)` (whitespace before the paren) is not an array
    /// literal; fall through to the empty-assignment path.
    if (value_adjacent && value->type == TOK_LPAREN &&
        shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
        node_t *array_node = parse_array_literal(parser);
        if (!array_node) {
            free(var_name);
            return NULL;
        }
        node_t *assign_node =
            new_node(is_append ? NODE_ARRAY_APPEND : NODE_ARRAY_ASSIGN);
        if (!assign_node) {
            free(var_name);
            free_node_tree(array_node);
            return NULL;
        }
        assign_node->val.str = var_name; /// Transfer ownership
        assign_node->val_type = VAL_STR;
        add_child_node(assign_node, array_node);
        *out_array_node = assign_node;
        return NULL;
    }

    char *assignment = NULL;

    /// Collect all consecutive value tokens (handles ${A}_${B} etc.)
    /// Value tokens include words, variables, command subs, etc.
    /// Stop at whitespace, semicolon, newline, or other separators.
    /// The first value token must be positionally adjacent to '=' (no
    /// whitespace gap) or this is an empty-value assignment. TOK_ASSIGN
    /// and TOK_PLUS_ASSIGN are valid first tokens because POSIX
    /// ASSIGNMENT_WORD only delimits at the FIRST '='; `X===` has value
    /// `==`.
    if (value_adjacent && token_is_assignment_value_token(value->type)) {

        /// Build complete value by concatenating adjacent tokens
        size_t value_capacity = 256;
        size_t value_len = 0;
        char *full_value = malloc(value_capacity);
        if (!full_value) {
            free(var_name);
            return NULL;
        }
        full_value[0] = '\0';

        /// POSIX ASSIGNMENT_WORD: only the first '=' delimits name from
        /// value; subsequent '=' characters within the same shell word are
        /// part of the value. So TOK_ASSIGN and TOK_PLUS_ASSIGN are
        /// continuation tokens here. The adjacency check at the bottom of
        /// the loop is what separates a value from the next prefix word
        /// (a whitespace gap breaks the loop). A peek-ahead "this word
        /// starts a new assignment" check would mis-handle X=a=b by
        /// treating the inner '=' as a new operator.
        while (value && token_is_assignment_value_token(value->type)) {

            size_t token_len = strlen(value->text);
            /// Quote re-wrapping policy for assignment-value
            /// tokens. expand_if_needed inspects the value
            /// string for quote markers; the parser must
            /// preserve enough of the original quoting so
            /// that:
            ///   TOK_STRING (single-quoted '...'):
            ///     re-wrap with ' so expand_if_needed's
            ///     no-expansion path fires (issue #98).
            ///   TOK_STRING (ANSI-C $'...'):
            ///     tokenizer text already includes $' and ';
            ///     do NOT re-wrap.
            ///   TOK_EXPANDABLE_STRING ("..."):
            ///     text already had outer " stripped by the
            ///     tokenizer; pass through verbatim. The
            ///     embedded-quote bug (a="hello 'world'"
            ///     losing the quote) is solved at the
            ///     expand_if_needed layer: it only enters
            ///     the single-quote-handling block when
            ///     there is a matched pair of ' in the text
            ///     (issue #102 -- see expand_if_needed
            ///     comment near the strchr check).
            bool is_ansi_c = (value->type == TOK_STRING && token_len >= 2 &&
                              value->text[0] == '$' && value->text[1] == '\'');
            bool wrap_single = (value->type == TOK_STRING && !is_ansi_c);
            size_t extra_len = wrap_single ? 2 : 0;

            /// Grow buffer if needed
            if (value_len + token_len + extra_len + 1 > value_capacity) {
                value_capacity = (value_len + token_len + extra_len + 1) * 2;
                char *new_value = realloc(full_value, value_capacity);
                if (!new_value) {
                    free(full_value);
                    free(var_name);
                    return NULL;
                }
                full_value = new_value;
            }

            /// Add token with appropriate quoting
            /// Only single quotes need to be preserved to prevent
            /// expansion Double quotes: expand variables but don't keep
            /// quotes
            if (value->quote_prov) {
                /// The token carries a per-character quote map (a mixed-quote
                /// reader token that is not ANSI-C-disabled). Re-encode per
                /// character so a fused quoted + unquoted run (`~/a:"b":~/c`)
                /// escapes only the quoted `~`/`'` and leaves an unquoted `~`
                /// after a `:` bare to expand. Generalizes the per-token
                /// branches below; the helper grows the buffer itself.
                if (!prov_reencode_append(&full_value, &value_len,
                                          &value_capacity, value->text,
                                          value->quote_prov, token_len)) {
                    free(full_value);
                    free(var_name);
                    return NULL;
                }
            } else if (wrap_single) {
                /// Regular single-quoted: re-wrap with quotes to
                /// preserve no-expansion semantics through
                /// expand_if_needed.
                strcat(full_value, "'");
                strcat(full_value, value->text);
                strcat(full_value, "'");
                value_len += token_len + 2;
            } else if (value->type == TOK_EXPANDABLE_STRING) {
                /// Double-quoted content: the outer "..." was
                /// stripped by the tokenizer. Any embedded
                /// single quotes in this content were literal
                /// characters in the source, but
                /// expand_if_needed would later misinterpret
                /// them as POSIX single-quote openers and
                /// eat them (issue #102). Pre-escape literal
                /// single quotes as \' so the downstream
                /// POSIX-unquoted backslash rule preserves
                /// them as literal characters. Other chars
                /// pass through; double-quote-specific
                /// escapes like \$ \" \\ \` were already
                /// preserved by the tokenizer. Grow the
                /// buffer to account for the worst case (every
                /// char doubles).
                size_t worst = value_len + token_len * 2 + 1;
                if (worst >= value_capacity) {
                    value_capacity = worst * 2;
                    char *nv = realloc(full_value, value_capacity);
                    if (!nv) {
                        free(full_value);
                        free(var_name);
                        return NULL;
                    }
                    full_value = nv;
                }
                for (size_t k = 0; k < token_len; k++) {
                    char ch = value->text[k];
                    /// A `~` that was inside "..." must NOT undergo tilde
                    /// expansion (POSIX: tilde-prefixes are unquoted-only).
                    /// The value string has already lost its outer quotes, so
                    /// escape the `~` as \~ to record that it was quoted; the
                    /// assignment tilde pass skips \~ and the downstream
                    /// POSIX-unquoted backslash rule restores the literal `~`.
                    /// Literal single quotes are escaped for the same reason
                    /// (issue #102).
                    if (ch == '\'' || ch == '~') {
                        full_value[value_len++] = '\\';
                        full_value[value_len++] = ch;
                    } else {
                        full_value[value_len++] = ch;
                    }
                }
                full_value[value_len] = '\0';
            } else {
                /// All other types (including ANSI-C $'...'
                /// whose tokenizer text already carries the
                /// $' and ' markers): append the text
                /// verbatim.
                strcat(full_value, value->text);
                value_len += token_len;
            }

            /// Only adjacent tokens (no intervening whitespace) belong to
            /// the same assignment value: `a=foo"bar"` -> foobar, but
            /// `a=foo bar` -> value `foo` and `bar` is the command word /
            /// next prefix. Mirrors the adjacency rule in
            /// collect_word_argument. Uses end_position so quoted strings
            /// count their full `"..."` span, not just the stripped text.
            size_t last_end_pos = value->end_position;
            tokenizer_advance(parser->tokenizer); /// consume this token
            value = tokenizer_current(parser->tokenizer);
            if (value && value->position != last_end_pos) {
                break; /// whitespace gap: value ends here
            }
        }

        /// Build final assignment string: var=value or var+=value
        size_t var_len = strlen(var_name);
        assignment = malloc(var_len + (is_append ? 2 : 1) + value_len + 1);
        if (assignment) {
            strcpy(assignment, var_name);
            strcat(assignment, is_append ? "+=" : "=");
            strcat(assignment, full_value);
        }
        free(full_value);
    } else {
        /// Assignment with empty value: variable=
        size_t var_len = strlen(var_name);
        assignment = malloc(var_len + 2);
        if (assignment) {
            strcpy(assignment, var_name);
            strcat(assignment, "=");
        }
    }

    free(var_name);
    return assignment;
}

/**
 * @brief Decide standalone-assignment vs POSIX cmd_prefix.
 *
 * `first_assignment` is a NODE_COMMAND whose val.str is the first
 * "var=value" the caller already parsed. If the next token ends the
 * simple command, this is a lone assignment and the node is returned
 * unchanged (preserving the legacy representation the executor's
 * is_assignment() fast-path expects).
 *
 * Otherwise the simple command has a cmd_prefix: the assignment(s) and
 * any redirections are collected as NODE_ASSIGN / redirection children
 * of a NODE_COMMAND. If a command word follows it becomes the command
 * name (val.str) and cmd_suffix is parsed; otherwise val.str stays NULL
 * (a pure assignment+redirection command, e.g. `x=1 2>/dev/null`).
 *
 * @param parser           Parser instance
 * @param first_assignment NODE_COMMAND carrying the first var=value
 * @return the simple-command AST node, or NULL on error
 */
static node_t *finish_assignment_or_prefix(parser_t *parser,
                                           node_t *first_assignment) {
    token_t *cur = tokenizer_current(parser->tokenizer);
    bool ends_command =
        (!cur || cur->type == TOK_EOF || cur->type == TOK_SEMICOLON ||
         cur->type == TOK_NEWLINE || cur->type == TOK_PIPE ||
         cur->type == TOK_AND || cur->type == TOK_LOGICAL_AND ||
         cur->type == TOK_LOGICAL_OR);
    if (ends_command) {
        /// Lone assignment: keep the legacy NODE_COMMAND val.str shape.
        return first_assignment;
    }

    /// cmd_prefix form. Build a NODE_COMMAND whose children are the
    /// prefix assignments (NODE_ASSIGN), then redirections, and whose
    /// val.str is the command word (or NULL if there is no command).
    node_t *cmd = new_node(NODE_COMMAND);
    if (!cmd) {
        free_node_tree(first_assignment);
        return NULL;
    }
    cmd->val.str = NULL;
    cmd->val_type = VAL_STR;

    /// Convert the already-parsed first assignment into a NODE_ASSIGN
    /// child (move the string; discard the throwaway command shell).
    node_t *first = new_node(NODE_ASSIGN);
    if (!first) {
        free_node_tree(first_assignment);
        free_node_tree(cmd);
        return NULL;
    }
    first->val.str = first_assignment->val.str;
    first->val_type = VAL_STR;
    first_assignment->val.str = NULL;
    free_node_tree(first_assignment);
    add_child_node(cmd, first);

    /// Collect the rest of the cmd_prefix: further scalar assignments
    /// and redirections, in any order, until a command word or a
    /// command terminator.
    for (;;) {
        token_t *t = tokenizer_current(parser->tokenizer);
        if (!t || t->type == TOK_EOF || t->type == TOK_SEMICOLON ||
            t->type == TOK_NEWLINE || t->type == TOK_PIPE ||
            t->type == TOK_AND || t->type == TOK_LOGICAL_AND ||
            t->type == TOK_LOGICAL_OR) {
            break;
        }

        /// Redirection in the prefix (e.g. `x=1 2>/dev/null cmd`).
        /// Use the canonical predicate so a new redirection token type
        /// is recognized here automatically.
        if (is_redirection_token(t->type)) {
            node_t *redir = parse_redirection(parser);
            if (!redir) {
                free_node_tree(cmd);
                return NULL;
            }
            add_child_node(cmd, redir);
            continue;
        }

        /// Another scalar assignment in the prefix? Only plain `name=`
        /// (no `[` subscript) qualifies; array element / literal forms
        /// are not POSIX cmd_prefix words.
        token_t *p = tokenizer_peek(parser->tokenizer);
        if (token_is_word_like(t->type) && p &&
            (p->type == TOK_ASSIGN || p->type == TOK_PLUS_ASSIGN) && t->text &&
            !strchr(t->text, '[')) {
            node_t *arr = NULL;
            char *s = parse_scalar_assignment_string(parser, &arr);
            if (arr) {
                /// `a=(...)` is not valid as a cmd_prefix word.
                free_node_tree(arr);
                free_node_tree(cmd);
                parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                 "array assignment is not valid as a "
                                 "command prefix");
                return NULL;
            }
            if (!s) {
                free_node_tree(cmd);
                return NULL;
            }
            node_t *a = new_node(NODE_ASSIGN);
            if (!a) {
                free(s);
                free_node_tree(cmd);
                return NULL;
            }
            a->val.str = s;
            a->val_type = VAL_STR;
            add_child_node(cmd, a);
            continue;
        }

        /// Anything else ends the prefix: it must be the command word.
        break;
    }

    /// Optional command word + cmd_suffix. A command substitution or backtick
    /// may be the command word after a cmd_prefix (`A=1 $(echo echo) hi`),
    /// mirroring the command-name gate in parse_simple_command; without this
    /// the prefix would fall through to the pure-assignment path and persist
    /// (and export) instead of scoping to the command.
    token_t *w = tokenizer_current(parser->tokenizer);
    if (w && (token_is_word_like(w->type) || w->type == TOK_LBRACKET ||
              w->type == TOK_COMMAND_SUB || w->type == TOK_BACKQUOTE)) {
        source_location_t loc =
            token_to_source_location(w, parser->source_name);
        cmd->loc = loc;
        cmd->val.str = strdup(w->text);
        cmd->val_type = VAL_STR;
        tokenizer_advance(parser->tokenizer);
        if (!parse_command_suffix(parser, cmd)) {
            free_node_tree(cmd);
            return NULL;
        }
    }
    /// else: pure assignment(s)+redirection(s), no command word. val.str
    /// stays NULL; the executor applies the assignments persistently and
    /// performs the redirections (POSIX: `x=1 >f`).

    return cmd;
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
    /// Track recursion depth for stack overflow protection
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

    /// Capture location for brace group
    source_location_t brace_loc =
        token_to_source_location(current, parser->source_name);

    /// Push context for better error messages
    parser_push_context(parser, "parsing brace group");

    /// Create brace group node
    node_t *group_node = new_node_at(NODE_BRACE_GROUP, brace_loc);
    if (!group_node) {
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    /// Consume '{'
    tokenizer_advance(parser->tokenizer);

    /// Skip whitespace and newlines after '{'
    skip_separators(parser);

    /// Parse commands until '}'
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
                break; /// End of input
            }
            free_node_tree(group_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(group_node, command);

        /// Skip separators between commands
        skip_separators(parser);
    }

    /// Expect '}'
    if (!expect_token_with_help(parser, TOK_RBRACE,
                                "brace group must end with '}'")) {
        free_node_tree(group_node);
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: { cmd; } >/dev/null 2>&1
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
    /// Track recursion depth for stack overflow protection
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

    /// Capture location for subshell
    source_location_t subshell_loc =
        token_to_source_location(current, parser->source_name);

    /// Push context for better error messages
    parser_push_context(parser, "parsing subshell");

    /// Create subshell node
    node_t *subshell_node = new_node_at(NODE_SUBSHELL, subshell_loc);
    if (!subshell_node) {
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    /// Consume '('
    tokenizer_advance(parser->tokenizer);

    /// Skip whitespace and newlines after '('
    skip_separators(parser);

    /// Parse commands until ')'
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
                break; /// End of input
            }
            free_node_tree(subshell_node);
            parser_pop_context(parser);
            parser_exit_recursion(parser);
            return NULL;
        }

        add_child_node(subshell_node, command);

        /// Skip separators between commands
        skip_separators(parser);
    }

    /// Expect ')'
    if (!expect_token_with_help(parser, TOK_RPAREN,
                                "subshell must end with ')'")) {
        free_node_tree(subshell_node);
        parser_pop_context(parser);
        parser_exit_recursion(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: (cmd) >/dev/null 2>&1
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
        return true; /// Nothing to do
    }

    token_t *current = tokenizer_current(parser->tokenizer);

    while (current && is_redirection_token(current->type)) {
        node_t *redir_node = parse_redirection(parser);
        if (!redir_node) {
            return false; /// Error parsing redirection
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

    /// Capture the operator's full source_location_t before any
    /// tokenizer_advance — that call frees the current token, so
    /// dereferencing redir_token later would be use-after-free.
    /// source_location_t is the project's unified position primitive
    /// (shell_error.h:33-39); using it here keeps the heredoc body
    /// search consistent with how every other parser path tracks
    /// positions, and lets the unterminated-heredoc error point at
    /// the actual operator rather than at SOURCE_LOC_UNKNOWN.
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

    /// Capture location for redirection
    source_location_t redir_loc =
        token_to_source_location(redir_token, parser->source_name);
    node_t *redir_node = new_node_at(node_type, redir_loc);
    if (!redir_node) {
        return NULL;
    }

    /// Store the redirection operator
    redir_node->val.str = strdup(redir_token->text);
    redir_node->val_type = VAL_STR;

    /// Disable keyword recognition for redirection targets - filenames like
    /// "in", "do", "done" etc. should be treated as words, not keywords
    tokenizer_enable_keywords(parser->tokenizer, false);
    tokenizer_refresh_lookahead(parser->tokenizer);
    tokenizer_advance(parser->tokenizer);

    /// Parse the target (filename or here document content)
    token_t *target_token = tokenizer_current(parser->tokenizer);

    /// Re-enable keyword recognition for subsequent tokens
    tokenizer_enable_keywords(parser->tokenizer, true);

    /// For NODE_REDIR_FD, the target is embedded in the redirection token
    /// itself
    if (node_type == NODE_REDIR_FD) {
        /// No separate target token needed; the now-current token is the next
        /// command's first token, read while keywords were disabled. Refresh
        /// both current and lookahead so a following `fi`/`done`/`then` keyword
        /// is re-classified instead of arriving as TOK_WORD.
        tokenizer_refresh_current_and_lookahead(parser->tokenizer);
        return redir_node;
    }

    /// For NODE_REDIR_FD_ALLOC, check if target is embedded (>&- or >&N
    /// patterns) or if we need a separate target file ({varname}> or
    /// {varname}>>)
    if (node_type == NODE_REDIR_FD_ALLOC) {
        const char *redir_text = redir_node->val.str;
        size_t len = strlen(redir_text);
        /// Check if ends with >&- or >&N or <&- or <&N (no target needed)
        if (len >= 2) {
            char last = redir_text[len - 1];
            char prev = redir_text[len - 2];
            if (prev == '&' && (last == '-' || isdigit(last))) {
                /// Same keyword-misclassification fix as NODE_REDIR_FD above.
                tokenizer_refresh_current_and_lookahead(parser->tokenizer);
                return redir_node;
            }
        }
        /// Otherwise fall through to parse target file
    }

    /// Check for process substitution as redirection target: < <(cmd) or >
    /// >(cmd) This is valid bash/zsh syntax for redirecting from/to process
    /// substitution
    if (target_token && (target_token->type == TOK_PROC_SUB_IN ||
                         target_token->type == TOK_PROC_SUB_OUT)) {
        /// Parse the process substitution and attach as child of redirection
        node_t *proc_sub_node = parse_process_substitution(parser);
        if (!proc_sub_node) {
            free_node_tree(redir_node);
            return NULL;
        }
        add_child_node(redir_node, proc_sub_node);
        return redir_node;
    }

    bool is_heredoc_delim = (node_type == NODE_REDIR_HEREDOC ||
                             node_type == NODE_REDIR_HEREDOC_STRIP);
    /// A redirection or here-string operand accepts every word form a command
    /// argument does -- bare $(...), $((...)), and `...` in addition to the
    /// word-like set -- because the operand is expanded exactly like an
    /// argument (handle_redirection_node -> expand_arg_node). The broader
    /// tokens are already handled by the concatenation loop below; they just
    /// need to pass this leading-token gate. Here-document delimiters keep the
    /// narrower word/quoted-word set: a delimiter is matched literally, never
    /// expanded, so a command substitution is not a valid delimiter.
    bool target_acceptable =
        target_token &&
        (is_heredoc_delim ? (target_token->type == TOK_STRING ||
                             target_token->type == TOK_EXPANDABLE_STRING ||
                             token_is_word_like(target_token->type))
                          : (token_is_word_like(target_token->type) ||
                             target_token->type == TOK_ARITH_EXP ||
                             target_token->type == TOK_COMMAND_SUB ||
                             target_token->type == TOK_BACKQUOTE));
    if (!target_acceptable) {
        parser_error_add(parser,
                         is_heredoc_delim ? SHELL_ERR_HEREDOC_DELIMITER
                                          : SHELL_ERR_INVALID_REDIRECT,
                         is_heredoc_delim ? "expected here-document delimiter"
                                          : "expected redirection target");
        free_node_tree(redir_node);
        return NULL;
    }

    /// For here documents, DEFER body collection
    if (node_type == NODE_REDIR_HEREDOC ||
        node_type == NODE_REDIR_HEREDOC_STRIP) {
        /// Store delimiter before advancing tokenizer
        char *delimiter = strdup(target_token->text);
        bool strip_tabs = (node_type == NODE_REDIR_HEREDOC_STRIP);

        /// Check if delimiter is quoted (any quoted delimiter disables
        /// expansion)
        bool expand_variables = true;
        if (target_token->type == TOK_STRING ||
            target_token->type == TOK_EXPANDABLE_STRING) {
            /// Any quoted string - disable expansion per POSIX
            expand_variables = false;
        }
        /// Only unquoted delimiters allow variable expansion

        /// Advance past the delimiter token
        tokenizer_advance(parser->tokenizer);

        /// Store delimiter in the redirection node value. The
        /// operator text (`<<` / `<<-`) was strdup'd into val.str
        /// earlier as a default; for heredocs val.str holds the
        /// delimiter instead, so free the operator string before
        /// overwriting (caught by LeakSanitizer running fuzz_parser
        /// on a heredoc input -- 3 bytes leaked per parse).
        free(redir_node->val.str);
        redir_node->val.str = delimiter; /// Transfer ownership
        redir_node->val_type = VAL_STR;

        /// DEFERRED COLLECTION. The heredoc body physically begins on
        /// the line AFTER the operator -- collecting it now would mean
        /// jumping the tokenizer past everything that still follows
        /// `<<delim` on this line (`| wc`, a trailing `; cmd2`, etc.),
        /// losing those tokens. Instead queue the heredoc; the body is
        /// collected by collect_pending_heredocs() when the parser
        /// reaches the line-terminating newline. The content and
        /// expand-flag child nodes are attached there.
        if (parser->pending_heredoc_count >= PARSER_MAX_PENDING_HEREDOCS) {
            parser_error_add(parser, SHELL_ERR_HEREDOC_DELIMITER,
                             "too many here-documents on one line "
                             "(maximum %d)",
                             PARSER_MAX_PENDING_HEREDOCS);
            free_node_tree(redir_node);
            return NULL;
        }
        pending_heredoc_t *ph =
            &parser->pending_heredocs[parser->pending_heredoc_count++];
        ph->redir_node = redir_node;
        ph->delimiter = redir_node->val.str; /// borrowed; node owns it
        ph->strip_tabs = strip_tabs;
        ph->expand_variables = expand_variables;
        ph->op_loc = op_loc;

        return redir_node;
    } else {
        /// Regular redirection - handle token concatenation for variables
        char *concatenated_target = NULL;
        size_t total_len = 0;
        size_t last_end_pos = target_token->end_position;
        /// Track whether the collected run includes anything that
        /// needs expansion. If every collected token is a single-
        /// quoted TOK_STRING the result must be NODE_STRING_LITERAL
        /// so the downstream redirection-target handler skips
        /// expand_if_needed and writes the bytes verbatim. Any
        /// double-quoted, $VAR, arith, command-sub, backtick, or
        /// bare-word token means the result has to be expanded.
        bool any_needs_expansion = false;

        /// Collect all consecutive tokens without whitespace (like
        /// /tmp/file_$VAR)
        token_t *current_token = target_token;
        while (current_token && (token_is_word_like(current_token->type) ||
                                 current_token->type == TOK_VARIABLE ||
                                 current_token->type == TOK_ARITH_EXP ||
                                 current_token->type == TOK_COMMAND_SUB ||
                                 current_token->type == TOK_BACKQUOTE ||
                                 current_token->type == TOK_COMMA)) {

            if (current_token->type != TOK_STRING) {
                any_needs_expansion = true;
            }

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
            last_end_pos = current_token->end_position;

            tokenizer_advance(parser->tokenizer);
            current_token = tokenizer_current(parser->tokenizer);

            /// Check if the next token is adjacent (no whitespace between)
            if (current_token && current_token->position != last_end_pos) {
                break; /// There's whitespace between tokens
            }
        }

        if (concatenated_target) {
            concatenated_target[total_len] = '\0';
        } else {
            /// Fallback to single token
            concatenated_target = strdup(target_token->text);
            if (target_token->type != TOK_STRING) {
                any_needs_expansion = true;
            }
            tokenizer_advance(parser->tokenizer);
        }

        /// Single-quoted-only -> NODE_STRING_LITERAL (no expansion).
        /// Anything else falls through to NODE_VAR (existing default).
        node_t *target_node =
            new_node(any_needs_expansion ? NODE_VAR : NODE_STRING_LITERAL);
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
 * @brief Collect a single here-document body
 *
 * Scans input lines starting at @p body_start until the delimiter is
 * found alone on a line, accumulating the body text. Handles the <<-
 * (strip leading tabs) variant. Pure scan: does NOT touch the
 * tokenizer's position or token cache -- the caller
 * (collect_pending_heredocs) is responsible for repositioning the
 * tokenizer once every pending heredoc on the line is collected.
 *
 * @param parser    Parser instance (for error reporting)
 * @param delimiter End delimiter string (may carry surrounding quotes)
 * @param strip_tabs If true, strip leading tabs from each line (<<-)
 * @param body_start Absolute input byte offset where the body begins
 * @param op_loc     Source location of the `<<` operator (for errors)
 * @param body_end   OUT: input offset just past the terminator line's
 *                   newline (or input_length); set even on error
 * @return Collected content string (caller frees), or NULL on an
 *         unterminated heredoc (parser error already recorded)
 */
static char *collect_one_heredoc_body(parser_t *parser, const char *delimiter,
                                      bool strip_tabs, size_t body_start,
                                      source_location_t op_loc,
                                      size_t *body_end) {
    if (!parser || !delimiter || !body_end) {
        if (body_end) {
            *body_end = body_start;
        }
        return NULL;
    }

    tokenizer_t *tokenizer = parser->tokenizer;
    *body_end = body_start;

    /// Strip outer quotes from the delimiter if present so body lines
    /// compare against the user-visible terminator text (`'END'` →
    /// `END`, `"EOF"` → `EOF`). Used only for the line-by-line
    /// terminator match below; the delimiter SPEC in the input is not
    /// re-parsed (see content_start computation).
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

    /// The body begins exactly at body_start -- the caller
    /// (collect_pending_heredocs) computed it as the byte just past the
    /// line-terminating newline that followed the `<<delim` operator,
    /// or, for the second and later heredocs declared on the same line,
    /// the byte just past the previous heredoc's terminator. No
    /// scanning for the operator or the delimiter spec is needed here:
    /// deferral guarantees we are always positioned at a body start.
    (void)op_loc; /// retained only for the unterminated-heredoc error

    /// Collect lines until we find the delimiter
    size_t content_size = 0;
    size_t content_capacity = 1024;
    char *content = malloc(content_capacity);
    if (!content) {
        if (unquoted_delimiter) {
            free(unquoted_delimiter);
        }
        return NULL;
    }
    content[0] = '\0';

    size_t line_start = body_start;
    bool found_delimiter_line = false;
    while (line_start < tokenizer->input_length) {
        /// Find end of current line
        size_t line_end = line_start;
        while (line_end < tokenizer->input_length &&
               tokenizer->input[line_end] != '\n') {
            line_end++;
        }

        /// Extract the line (without newline)
        size_t line_len = line_end - line_start;
        char *line = malloc(line_len + 1);
        if (!line) {
            free(content);
            if (unquoted_delimiter) {
                free(unquoted_delimiter);
            }
            return NULL;
        }
        strncpy(line, &tokenizer->input[line_start], line_len);
        line[line_len] = '\0';

        /// Strip leading tabs if requested (<<- variant)
        const char *line_content = line;
        if (strip_tabs) {
            while (*line_content == '\t') {
                line_content++;
            }
        }

        /// Check if this line matches the delimiter. NFC-equivalent
        /// (see redirection.c::heredoc body-read for the same swap;
        /// the parser-time delimiter scan needs the same Unicode
        /// rule so on-script EOF\xc3\xa9 and on-stdin EOF\xcc\x81
        /// terminate one heredoc identically).
        if (lle_unicode_strings_equal(line_content, match_delimiter, NULL)) {
            /// Found the terminator. body_end is the byte just past the
            /// terminator line's newline -- the resume point for the
            /// tokenizer (or the start of the next heredoc's body, when
            /// several heredocs share a line). When the terminator line
            /// is the final line with no trailing newline, line_end is
            /// already input_length. Deferral collects the body during
            /// separator-skipping, so landing past the newline is
            /// correct: the parser is between statements and re-enters
            /// keyword-aware tokenization for whatever follows.
            found_delimiter_line = true;
            free(line);
            *body_end =
                (line_end < tokenizer->input_length) ? line_end + 1 : line_end;
            break;
        }

        /// Add line to content (with newline)
        size_t needed =
            content_size + line_len + 2; /// +1 for newline, +1 for null
        if (needed > content_capacity) {
            content_capacity = needed * 2;
            char *new_content = realloc(content, content_capacity);
            if (!new_content) {
                free(line);
                free(content);
                if (unquoted_delimiter) {
                    free(unquoted_delimiter);
                }
                return NULL;
            }
            content = new_content;
        }

        /// Append the line (stripped if <<- variant) plus newline
        if (strip_tabs) {
            strcat(content, line_content);
        } else {
            strcat(content, line);
        }
        strcat(content, "\n");
        content_size = strlen(content);

        free(line);

        /// Move to next line
        line_start = line_end + 1;
    }

    /// EOF reached without finding the delimiter — issue #44.
    /// Treat this as a parse error rather than silently accepting the
    /// partial body. Bash warns at parse time; lush -n needs an error
    /// because exit code is the only signal available to tooling.
    if (!found_delimiter_line) {
        /// Use the OPERATOR's source location (op_loc), not the parser's
        /// current position (which by now is at end-of-input). Pointing
        /// the diagnostic at the `<<` operator is far more useful than
        /// pointing at EOF. parser_error_add_with_help_at() routes the
        /// error through the same context-stack / source-line / legacy-
        /// compatibility plumbing as parser_error_add_with_help, just
        /// with an explicit location.
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
        /// All input consumed scanning for the missing terminator.
        *body_end = tokenizer->input_length;
        return NULL;
    }

    /// Clean up temporary delimiter
    if (unquoted_delimiter) {
        free(unquoted_delimiter);
    }

    return content;
}

/**
 * @brief Collect every here-document body pending on the current line
 *
 * Called when the parser reaches the newline that terminates a command
 * line on which one or more `<<delim` operators appeared. The bodies
 * follow that newline in declaration order; this drains the parser's
 * pending_heredocs queue, scans each body, attaches the collected
 * content (and an expand-flag sibling) to the corresponding
 * NODE_REDIR_HEREDOC node, then repositions the tokenizer past the
 * final terminator so normal parsing resumes after the last heredoc.
 *
 * Must be invoked with the current token being the line-terminating
 * NEWLINE (or EOF). The tokenizer's token cache is rebuilt from the
 * post-heredoc position before returning.
 *
 * @param parser Parser instance
 * @return true on success, false if any heredoc was unterminated
 */
static bool collect_pending_heredocs(parser_t *parser) {
    if (!parser || parser->pending_heredoc_count == 0) {
        return true;
    }

    /// Each pending entry borrows its redir_node (and the delimiter string the
    /// node owns) from the AST. A parse error recovers by freeing the partial
    /// AST via free_node_tree -- e.g. parse_case_statement discards the case
    /// subtree when a here-document appears inside a case whose 'esac' is
    /// missing -- which frees those borrowed nodes while the pending count is
    /// still set. The safety-net flush in parser_parse then walks the queue
    /// into freed memory. Once the parse has errored the queue is meaningless:
    /// drop it without dereferencing the freed nodes.
    if (parser->has_error) {
        parser->pending_heredoc_count = 0;
        return false;
    }

    tokenizer_t *tk = parser->tokenizer;
    token_t *cur = tokenizer_current(tk);

    /// The first body begins immediately after the line-terminating
    /// newline. If the line was not newline-terminated (heredoc op on
    /// the final line, no trailing '\n'), there is no body -- scanning
    /// from end-of-input makes collect_one_heredoc_body report the
    /// unterminated-heredoc error.
    size_t scan;
    size_t base_offset;
    size_t base_line;
    if (cur && cur->type == TOK_NEWLINE) {
        scan = cur->position + 1; /// past the '\n'
        base_line = cur->line + 1;
    } else if (cur) {
        scan = cur->position;
        base_line = cur->line;
    } else {
        scan = tk->position;
        base_line = tk->line;
    }
    base_offset = scan;

    bool ok = true;
    for (size_t i = 0; i < parser->pending_heredoc_count; i++) {
        pending_heredoc_t *ph = &parser->pending_heredocs[i];
        size_t body_end = scan;
        char *content = collect_one_heredoc_body(
            parser, ph->delimiter, ph->strip_tabs, scan, ph->op_loc, &body_end);
        scan = body_end;
        if (!content) {
            ok = false;
            break;
        }

        /// Body content child, then the expand-variables flag child --
        /// the layout the executor's heredoc handling expects.
        node_t *content_node = new_node(NODE_VAR);
        if (!content_node) {
            free(content);
            ok = false;
            break;
        }
        content_node->val.str = content;
        content_node->val_type = VAL_STR;
        add_child_node(ph->redir_node, content_node);

        node_t *expand_flag_node = new_node(NODE_VAR);
        if (!expand_flag_node) {
            ok = false;
            break;
        }
        expand_flag_node->val.str = strdup(ph->expand_variables ? "1" : "0");
        expand_flag_node->val_type = VAL_STR;
        add_child_node(ph->redir_node, expand_flag_node);
    }

    parser->pending_heredoc_count = 0;

    /// Reposition the tokenizer past the last terminator and rebuild
    /// its line/column counters by counting newlines across the span
    /// just consumed as heredoc bodies.
    tk->position = scan;
    size_t line = base_line;
    size_t column = 1;
    for (size_t p = base_offset; p < scan && p < tk->input_length; p++) {
        if (tk->input[p] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    tk->line = line;
    tk->column = column;
    tokenizer_refresh_from_position(tk);

    return ok;
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
    /// Capture location before consuming 'if' token
    token_t *if_token = tokenizer_current(parser->tokenizer);
    source_location_t if_loc =
        token_to_source_location(if_token, parser->source_name);

    if (!expect_token(parser, TOK_IF)) {
        return NULL;
    }

    /// Push context for better error messages
    parser_push_context(parser, "parsing if statement");

    node_t *if_node = new_node_at(NODE_IF, if_loc);
    if (!if_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse the condition as a bare and-or: a trailing `&` is a list
    /// separator, not valid after a condition, so parse_and_or leaves it for
    /// the `then`/`;` terminator rather than backgrounding the condition.
    node_t *condition = parse_and_or(parser);
    if (!condition) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(if_node, condition);

    if (reject_backgrounded_condition(parser, "if")) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    /// Now we should see 'then'
    if (!expect_token_with_help(
            parser, TOK_THEN, "'if' requires 'then' before the command body")) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'then' before parsing body
    skip_separators(parser);

    /// Parse then body - parse until we hit 'else', 'elif', or 'fi'
    node_t *then_body = parse_if_body(parser);
    if (!then_body) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(if_node, then_body);

    /// Handle optional semicolon before elif/else/fi
    if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
        tokenizer_advance(parser->tokenizer);
    }

    /// Parse optional elif clauses
    /// Skip separators before checking for elif
    skip_separators(parser);

    /// Handle multiple elif clauses
    while (tokenizer_match(parser->tokenizer, TOK_ELIF)) {
        tokenizer_advance(parser->tokenizer);

        /// Parse elif condition
        node_t *elif_condition = parse_and_or(parser);
        if (!elif_condition) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, elif_condition);

        if (reject_backgrounded_condition(parser, "elif")) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }

        /// Skip separators before 'then'
        skip_separators(parser);

        /// Expect 'then' after elif condition
        if (!expect_token_with_help(
                parser, TOK_THEN,
                "'elif' requires 'then' before the command body")) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }

        /// Skip separators after 'then'
        skip_separators(parser);

        /// Parse elif body
        node_t *elif_body = parse_if_body(parser);
        if (!elif_body) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, elif_body);

        /// Handle optional semicolon after elif body
        if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
            tokenizer_advance(parser->tokenizer);
        }

        /// Skip separators before next elif/else/fi
        skip_separators(parser);
    }

    /// Handle optional else clause
    if (tokenizer_match(parser->tokenizer, TOK_ELSE)) {
        tokenizer_advance(parser->tokenizer);

        /// Skip separators after 'else' before parsing body
        skip_separators(parser);

        node_t *else_body = parse_if_body(parser);
        if (!else_body) {
            free_node_tree(if_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(if_node, else_body);
    }

    /// Skip separators before 'fi'
    skip_separators(parser);

    /// No need for additional semicolon handling here since we handled it above

    if (!expect_token_with_help(parser, TOK_FI,
                                "'if' statement must end with 'fi'")) {
        free_node_tree(if_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: if ...; then ...; fi >/dev/null 2>&1
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
    /// Capture location before consuming 'while' token
    token_t *while_token = tokenizer_current(parser->tokenizer);
    source_location_t while_loc =
        token_to_source_location(while_token, parser->source_name);

    if (!expect_token(parser, TOK_WHILE)) {
        return NULL;
    }

    /// Push context for better error messages
    parser_push_context(parser, "parsing while loop");

    node_t *while_node = new_node_at(NODE_WHILE, while_loc);
    if (!while_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse condition as a full logical expression so that &&/|| chains
    /// (e.g. `while [ $i -lt N ] && true; do ...`) parse like in if/elif.
    /// The `do` terminator is unambiguous, so logical operators in the
    /// condition introduce no parser ambiguity. A bare and-or (parse_and_or)
    /// so a trailing `&` does not background the loop condition.
    node_t *condition = parse_and_or(parser);

    if (!condition) {
        free_node_tree(while_node);
        parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                   "'while' requires a condition command",
                                   "invalid while loop condition");
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(while_node, condition);

    if (reject_backgrounded_condition(parser, "while")) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    /// Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'while' requires 'do' before the loop body")) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'do' before parsing body
    skip_separators(parser);

    /// Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(while_node, body);

    /// Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'while' loop must end with 'done'")) {
        free_node_tree(while_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: while ...; do ...; done </input 2>&1
    if (!parse_trailing_redirections(parser, while_node)) {
        free_node_tree(while_node);
        return NULL;
    }

    return while_node;
}

/**
 * @brief Parse a zsh repeat loop
 *
 * Accepts both:
 *   repeat N; do BODY; done    (do/done form)
 *   repeat N { BODY }          (brace form)
 *
 * N is a single word (number or variable reference) -- not a full
 * arithmetic expression here, matching zsh's accepted syntax;
 * arithmetic count is supported via `repeat $((expr))`. The body
 * runs N times in a fresh loop scope. Issue #103.
 *
 * @param parser Parser instance
 * @return NODE_REPEAT AST node
 */
static node_t *parse_repeat_statement(parser_t *parser) {
    token_t *kw = tokenizer_current(parser->tokenizer);
    source_location_t kw_loc =
        token_to_source_location(kw, parser->source_name);

    if (!expect_token(parser, TOK_REPEAT)) {
        return NULL;
    }
    parser_push_context(parser, "parsing repeat loop");

    node_t *repeat_node = new_node_at(NODE_REPEAT, kw_loc);
    if (!repeat_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse count as a single word using the shared argument
    /// collector so $var / $((..)) / `(cmd)` all work.
    if (!collect_word_argument(parser, repeat_node)) {
        free_node_tree(repeat_node);
        parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                   "'repeat' requires a count expression",
                                   "invalid repeat loop count");
        parser_pop_context(parser);
        return NULL;
    }

    skip_separators(parser);

    /// Body: either `do ... done` or `{ ... }`.
    token_t *body_open = tokenizer_current(parser->tokenizer);
    node_t *body = NULL;

    if (body_open && body_open->type == TOK_DO) {
        tokenizer_advance(parser->tokenizer);
        skip_separators(parser);
        body = parse_command_body(parser, TOK_DONE);
        if (!body) {
            free_node_tree(repeat_node);
            parser_pop_context(parser);
            return NULL;
        }
        skip_separators(parser);
        if (!expect_token_with_help(parser, TOK_DONE,
                                    "'repeat ...; do' must end with 'done'")) {
            free_node_tree(body);
            free_node_tree(repeat_node);
            parser_pop_context(parser);
            return NULL;
        }
    } else if (body_open && body_open->type == TOK_LBRACE) {
        tokenizer_advance(parser->tokenizer);
        skip_separators(parser);
        body = parse_command_body(parser, TOK_RBRACE);
        if (!body) {
            free_node_tree(repeat_node);
            parser_pop_context(parser);
            return NULL;
        }
        skip_separators(parser);
        if (!expect_token_with_help(parser, TOK_RBRACE,
                                    "'repeat ... {' must end with '}'")) {
            free_node_tree(body);
            free_node_tree(repeat_node);
            parser_pop_context(parser);
            return NULL;
        }
    } else {
        free_node_tree(repeat_node);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "'repeat N' must be followed by '; do ... done' or '{ ... }'",
            "invalid repeat loop body");
        parser_pop_context(parser);
        return NULL;
    }

    add_child_node(repeat_node, body);

    parser_pop_context(parser);

    if (!parse_trailing_redirections(parser, repeat_node)) {
        free_node_tree(repeat_node);
        return NULL;
    }
    return repeat_node;
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

    /// Push context for better error messages
    parser_push_context(parser, "parsing until loop");

    node_t *until_node = new_node(NODE_UNTIL);
    if (!until_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse the condition as a bare and-or — same rationale as
    /// parse_while_statement: &&/|| in conditions are unambiguous because
    /// `do` is the terminator; parse_and_or also leaves a trailing `&` for the
    /// terminator rather than backgrounding the condition.
    node_t *condition = parse_and_or(parser);

    if (!condition) {
        free_node_tree(until_node);
        parser_error_add_with_help(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                   "'until' requires a condition command",
                                   "invalid until loop condition");
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(until_node, condition);

    if (reject_backgrounded_condition(parser, "until")) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    /// Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'until' requires 'do' before the loop body")) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'do' before parsing body
    skip_separators(parser);

    /// Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(until_node, body);

    /// Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'until' loop must end with 'done'")) {
        free_node_tree(until_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: until ...; do ...; done </input 2>&1
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
    /// Capture location before consuming 'for' token
    token_t *for_token = tokenizer_current(parser->tokenizer);
    source_location_t for_loc =
        token_to_source_location(for_token, parser->source_name);

    if (!expect_token(parser, TOK_FOR)) {
        return NULL;
    }

    /// Push context for better error messages
    parser_push_context(parser, "parsing for loop");

    /// Check for C-style for loop: for ((init; test; update))
    if (tokenizer_match(parser->tokenizer, TOK_DOUBLE_LPAREN)) {
        tokenizer_advance(parser->tokenizer); /// consume ((

        node_t *for_arith_node = new_node_at(NODE_FOR_ARITH, for_loc);
        if (!for_arith_node) {
            parser_pop_context(parser);
            return NULL;
        }

        /// Parse the three arithmetic expressions separated by semicolons
        /// Format: for ((init; test; update)); do body; done
        /// Each expression is optional (can be empty)
        ///
        /// We extract raw input text to preserve operators like <= that
        /// the tokenizer splits into separate tokens (< and =).

        const char *input = parser->tokenizer->input;
        char *init_expr = NULL;
        char *test_expr = NULL;
        char *update_expr = NULL;

        int paren_depth = 0; /// Track nested parentheses

        /// Get start position for init expression
        token_t *start_tok = tokenizer_current(parser->tokenizer);
        size_t expr_start = start_tok ? start_tok->position : 0;
        size_t expr_end = expr_start;

        /// Parse init expression - find the first ; at depth 0
        while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
            token_t *tok = tokenizer_current(parser->tokenizer);

            /// Track parentheses depth to handle nested (( )) in expressions
            if (tok->type == TOK_LPAREN || tok->type == TOK_DOUBLE_LPAREN) {
                paren_depth++;
            } else if (tok->type == TOK_RPAREN) {
                paren_depth--;
            } else if (tok->type == TOK_DOUBLE_RPAREN) {
                if (paren_depth > 0) {
                    paren_depth -= 2;
                } else {
                    /// End of for (( )) - but we haven't seen all three
                    /// expressions
                    parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                     "expected ';' in C-style for loop");
                    free_node_tree(for_arith_node);
                    parser_pop_context(parser);
                    return NULL;
                }
            }

            /// Semicolon at depth 0 separates expressions
            if (tok->type == TOK_SEMICOLON && paren_depth == 0) {
                expr_end = tok->position;
                break;
            }

            tokenizer_advance(parser->tokenizer);
        }

        /// Extract init expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            init_expr = malloc(len + 1);
            if (init_expr) {
                memcpy(init_expr, input + expr_start, len);
                init_expr[len] = '\0';
                /// Trim leading/trailing whitespace
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

        /// Parse test expression
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

        /// Extract test expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            test_expr = malloc(len + 1);
            if (test_expr) {
                memcpy(test_expr, input + expr_start, len);
                test_expr[len] = '\0';
                /// Trim whitespace
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

        /// Parse update expression (until )))
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

        /// Extract update expression from raw input
        if (expr_end > expr_start) {
            size_t len = expr_end - expr_start;
            update_expr = malloc(len + 1);
            if (update_expr) {
                memcpy(update_expr, input + expr_start, len);
                update_expr[len] = '\0';
                /// Trim whitespace
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

        /// Expect )) to close the arithmetic for
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

        /// Store expressions as child nodes
        /// Child 0: init expression
        node_t *init_node = new_node(NODE_ARITH_EXP);
        if (init_node) {
            init_node->val.str = init_expr;
            init_node->val_type = VAL_STR;
            add_child_node(for_arith_node, init_node);
        } else {
            free(init_expr);
        }

        /// Child 1: test expression
        node_t *test_node = new_node(NODE_ARITH_EXP);
        if (test_node) {
            test_node->val.str = test_expr;
            test_node->val_type = VAL_STR;
            add_child_node(for_arith_node, test_node);
        } else {
            free(test_expr);
        }

        /// Child 2: update expression
        node_t *update_node = new_node(NODE_ARITH_EXP);
        if (update_node) {
            update_node->val.str = update_expr;
            update_node->val_type = VAL_STR;
            add_child_node(for_arith_node, update_node);
        } else {
            free(update_expr);
        }

        /// Skip any separators (semicolons, newlines, whitespace)
        skip_separators(parser);

        /// Expect 'do'
        if (!expect_token_with_help(
                parser, TOK_DO,
                "'for ((...))' requires 'do' before the loop body")) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        /// Skip separators after 'do' before parsing body
        skip_separators(parser);

        /// Parse loop body
        node_t *body = parse_command_body(parser, TOK_DONE);
        if (!body) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(for_arith_node, body); /// Child 3: body

        /// Skip separators before 'done'
        skip_separators(parser);

        if (!expect_token_with_help(
                parser, TOK_DONE, "'for ((...))' loop must end with 'done'")) {
            free_node_tree(for_arith_node);
            parser_pop_context(parser);
            return NULL;
        }

        parser_pop_context(parser);

        /// Parse any trailing redirections
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

    /// Parse variable name (POSIX for-in loop)
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

    /// Parse word list
    node_t *word_list = new_node(NODE_VAR); /// Use as container
    if (!word_list) {
        free_node_tree(for_node);
        return NULL;
    }

    /// Zsh compact paren form: `for NAME (WORD ...) BODY` or
    /// `for NAME (WORD ...) { LIST }`. The paren introduces the iteration
    /// list (terminated by `)`), and the body is either a brace group or a
    /// single sublist (and-or list) terminated by `;` / newline.
    bool zsh_paren_form = false;
    if (tokenizer_match(parser->tokenizer, TOK_LPAREN)) {
        zsh_paren_form = true;
        tokenizer_advance(parser->tokenizer);
        while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
               !tokenizer_match(parser->tokenizer, TOK_EOF)) {
            token_t *word_token = tokenizer_current(parser->tokenizer);
            if (token_is_word_list_token(word_token->type)) {
                node_t *word_node = new_node(NODE_VAR);
                if (!word_node) {
                    free_node_tree(for_node);
                    free_node_tree(word_list);
                    return NULL;
                }
                word_node->val.str = strdup(word_token->text);
                word_node->val_type = VAL_STR;
                word_node->glob_qualified = word_token->glob_qualified;
                add_child_node(word_list, word_node);
                tokenizer_advance(parser->tokenizer);
            } else {
                tokenizer_advance(parser->tokenizer);
            }
        }
        if (!expect_token_with_help(parser, TOK_RPAREN,
                                    "expected ')' to close zsh `for NAME "
                                    "(LIST)` iteration list")) {
            free_node_tree(for_node);
            free_node_tree(word_list);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(for_node, word_list);
        skip_separators(parser);
        node_t *body = NULL;
        if (tokenizer_match(parser->tokenizer, TOK_LBRACE)) {
            body = parse_brace_group(parser);
        } else {
            body = parse_logical_expression(parser);
        }
        if (!body) {
            free_node_tree(for_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(for_node, body);
        parser_pop_context(parser);
        if (!parse_trailing_redirections(parser, for_node)) {
            free_node_tree(for_node);
            return NULL;
        }
        return for_node;
    }
    (void)zsh_paren_form;

    /// POSIX: 'in' keyword is optional. If omitted, iterate over "$@"
    /// Check if we have 'in' or if we're going directly to ';'/newline/'do'
    if (tokenizer_match(parser->tokenizer, TOK_IN)) {
        /// Consume the 'in' token and parse word list
        tokenizer_advance(parser->tokenizer);
    } else if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON) ||
               tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_DO)) {
        /// No 'in' clause - POSIX says iterate over positional parameters
        /// Create a word list containing "$@"
        node_t *at_node = new_node(NODE_VAR);
        if (at_node) {
            at_node->val.str = strdup("\"$@\"");
            at_node->val_type = VAL_STR;
            add_child_node(word_list, at_node);
        }
        /// Skip to where we expect 'do'
        goto skip_word_parsing;
    } else {
        /// Unexpected token after variable name
        free_node_tree(for_node);
        free_node_tree(word_list);
        parser_error_add_with_help(
            parser, SHELL_ERR_UNEXPECTED_TOKEN,
            "syntax: for NAME [in WORDS...]; do COMMANDS; done",
            "expected 'in', ';', or 'do' after variable name in for loop");
        parser_pop_context(parser);
        return NULL;
    }

    /// Collect all words until ';', newline, or 'do'
    /// In POSIX, words in the for-in list can contain '=' (e.g., name=value)
    /// The tokenizer splits these, so we need to reassemble them here
    while (!tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
           !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
           !tokenizer_match(parser->tokenizer, TOK_DO) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        token_t *word_token = tokenizer_current(parser->tokenizer);

        /// In for-in word lists, '=' can be a standalone word (e.g., for i in =
        /// foo) Handle it specially before the normal word-like check
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

        if (token_is_word_list_token(word_token->type)) {

            node_t *word_node = NULL;

            /// Create appropriate node type based on token type
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

            /// Start building the word string - may need to combine with '='
            /// and more. Capture the fused-qualifier flag before the advance
            /// below frees word_token.
            bool word_glob_qualified = word_token->glob_qualified;
            char *combined = strdup(word_token->text);
            if (!combined) {
                free_node_tree(word_node);
                free_node_tree(for_node);
                free_node_tree(word_list);
                return NULL;
            }
            /// Track end position of current token for adjacency checks.
            /// end_position is the consumed input span (includes stripped
            /// quote chars for "..." tokens); token->length is just the
            /// text byte count and would mis-track adjacency for quoted
            /// segments.
            size_t current_end_pos = word_token->end_position;
            tokenizer_advance(parser->tokenizer);

            /// General shell-word adjacency: any word-like, TOK_ASSIGN, or
            /// TOK_COMMA token that begins exactly where the previous token
            /// ended is part of the same word (POSIX 2.10.2 word
            /// concatenation). Handles `a=b` and `a=b=c` (assignment-like
            /// for-list words), `a,b,c` (comma-separated list element),
            /// `pre$VAR.txt` mixes, etc. A whitespace gap or a non-word
            /// token breaks the loop.
            for (;;) {
                token_t *next_tok = tokenizer_current(parser->tokenizer);
                if (!next_tok || next_tok->position != current_end_pos) {
                    break;
                }
                bool is_continuation =
                    token_is_word_list_token(next_tok->type) ||
                    next_tok->type == TOK_NUMBER ||
                    next_tok->type == TOK_ASSIGN ||
                    next_tok->type == TOK_PLUS_ASSIGN ||
                    next_tok->type == TOK_COMMA;
                if (!is_continuation) {
                    break;
                }
                size_t tlen = strlen(next_tok->text);
                char *new_combined =
                    realloc(combined, strlen(combined) + tlen + 1);
                if (!new_combined) {
                    free(combined);
                    free_node_tree(word_node);
                    free_node_tree(for_node);
                    free_node_tree(word_list);
                    return NULL;
                }
                combined = new_combined;
                strcat(combined, next_tok->text);
                word_glob_qualified =
                    word_glob_qualified || next_tok->glob_qualified;
                current_end_pos = next_tok->end_position;
                tokenizer_advance(parser->tokenizer);
            }

            word_node->val.str = combined;
            word_node->val_type = VAL_STR;
            word_node->glob_qualified = word_glob_qualified;
            add_child_node(word_list, word_node);
        } else {
            break;
        }
    }

skip_word_parsing:
    add_child_node(for_node, word_list);

    /// Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    /// Zsh short for-in form: `for NAME in WORDS<newline> sublist` (no
    /// `do`/`done`). If after the iteration list we land on anything other
    /// than `do`, treat the next sublist (and-or list) as the body and
    /// return.
    if (!tokenizer_match(parser->tokenizer, TOK_DO)) {
        node_t *body = parse_logical_expression(parser);
        if (!body) {
            free_node_tree(for_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(for_node, body);
        parser_pop_context(parser);
        if (!parse_trailing_redirections(parser, for_node)) {
            free_node_tree(for_node);
            return NULL;
        }
        return for_node;
    }

    /// Now we should see 'do'
    if (!expect_token_with_help(parser, TOK_DO,
                                "'for' requires 'do' before the loop body")) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'do' before parsing body
    skip_separators(parser);

    /// Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(for_node, body);

    /// Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'for' loop must end with 'done'")) {
        free_node_tree(for_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: for ...; do ...; done >/dev/null 2>&1
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

    /// Push context for better error messages
    parser_push_context(parser, "parsing select statement");

    node_t *select_node = new_node(NODE_SELECT);
    if (!select_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse variable name
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

    /// Skip any whitespace
    while (tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
        tokenizer_advance(parser->tokenizer);
    }

    /// Check for optional 'in' keyword
    if (tokenizer_match(parser->tokenizer, TOK_IN)) {
        tokenizer_advance(parser->tokenizer);

        /// Parse word list
        node_t *word_list = new_node(NODE_VAR); /// Use as container
        if (!word_list) {
            free_node_tree(select_node);
            return NULL;
        }

        /// Collect all words until ';', newline, or 'do'
        while (!tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
               !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
               !tokenizer_match(parser->tokenizer, TOK_DO) &&
               !tokenizer_match(parser->tokenizer, TOK_EOF)) {

            token_t *word_token = tokenizer_current(parser->tokenizer);

            if (token_is_word_list_token(word_token->type)) {

                node_t *word_node = NULL;

                /// Create appropriate node type based on token type
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
                word_node->glob_qualified = word_token->glob_qualified;
                add_child_node(word_list, word_node);
                tokenizer_advance(parser->tokenizer);
            } else {
                break;
            }
        }

        add_child_node(select_node, word_list);
    }

    /// Skip any separators (semicolons, newlines, whitespace)
    skip_separators(parser);

    /// Now we should see 'do'
    if (!expect_token_with_help(
            parser, TOK_DO, "'select' requires 'do' before the command body")) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'do' before parsing body
    skip_separators(parser);

    /// Parse body
    node_t *body = parse_command_body(parser, TOK_DONE);
    if (!body) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }
    add_child_node(select_node, body);

    /// Skip separators before 'done'
    skip_separators(parser);

    if (!expect_token_with_help(parser, TOK_DONE,
                                "'select' statement must end with 'done'")) {
        free_node_tree(select_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: select ... done >/dev/null 2>&1
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

    /// Check for -p option (POSIX format)
    token_t *current = tokenizer_current(parser->tokenizer);
    if (current && token_is_word_like(current->type) &&
        strcmp(current->text, "-p") == 0) {
        time_node->val.sint = 1; /// Flag for -p option
        time_node->val_type = VAL_SINT;
        tokenizer_advance(parser->tokenizer);
    } else {
        time_node->val.sint = 0;
        time_node->val_type = VAL_SINT;
    }

    /// Parse the pipeline/command to time
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

    /// Check if feature is enabled
    if (!shell_mode_allows(FEATURE_COPROC)) {
        parser_error_add(parser, SHELL_ERR_FEATURE_DISABLED,
                         "coproc: feature not enabled in current shell mode");
        return NULL;
    }

    node_t *coproc_node = new_node(NODE_COPROC);
    if (!coproc_node) {
        return NULL;
    }

    /// Skip any whitespace/newlines
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

    /// Check if first word is a NAME (simple identifier followed by command)
    /// NAME must be a valid identifier and not a compound command starter
    char *coproc_name = NULL;

    if (token_is_word_like(current->type)) {
        /// Peek ahead to see if this is a name or the start of a command
        token_t *next = tokenizer_peek(parser->tokenizer);

        /// If next token is also word-like or a compound command starter,
        /// then current token is the NAME
        if (next && (token_is_word_like(next->type) ||
                     next->type == TOK_LBRACE || next->type == TOK_LPAREN ||
                     next->type == TOK_WHILE || next->type == TOK_UNTIL ||
                     next->type == TOK_FOR || next->type == TOK_IF ||
                     next->type == TOK_CASE || next->type == TOK_SELECT)) {
            /// Current is the NAME
            coproc_name = strdup(current->text);
            tokenizer_advance(parser->tokenizer);
        }
    }

    /// Store name (NULL means use default "COPROC")
    coproc_node->val.str = coproc_name;
    coproc_node->val_type = VAL_STR;

    /// Parse the command (can be simple command or compound command)
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
    /// Current token should be '{' - we've already consumed ()
    node_t *anon_node = new_node(NODE_ANON_FUNCTION);
    if (!anon_node) {
        return NULL;
    }

    /// Parse the brace group body
    node_t *body = parse_brace_group(parser);
    if (!body) {
        free_node_tree(anon_node);
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected '{' after '()' in anonymous function");
        return NULL;
    }

    add_child_node(anon_node, body);

    /// Collect trailing positional arguments after the closing '}'.
    /// Zsh's anonymous-function form is `() { body } ARG1 ARG2 ...`,
    /// where the args become $1, $2, ... within the body. Uses the
    /// shared collect_word_argument helper so anon-function args have
    /// the same acceptance, adjacency-concatenation, and node-type
    /// classification semantics as regular command arguments. The
    /// helper stops naturally at non-arg tokens (NEWLINE, SEMI, EOF,
    /// AMP, PIPE, redirection tokens, etc.).
    while (collect_word_argument(parser, anon_node)) {
        /// Loop until helper returns false (non-arg token or alloc failure).
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

/// Append @p s to a growable case-pattern buffer. Returns false on allocation
/// failure (the caller frees @p *buf and unwinds).
static bool case_pat_append(char **buf, size_t *len, const char *s) {
    if (!s) {
        return true;
    }
    size_t sl = strlen(s);
    char *nb = realloc(*buf, *len + sl + 1);
    if (!nb) {
        return false;
    }
    *buf = nb;
    memcpy(*buf + *len, s, sl + 1);
    *len += sl;
    return true;
}

static node_t *parse_case_statement(parser_t *parser) {
    if (!expect_token(parser, TOK_CASE)) {
        return NULL;
    }

    /// Push context for better error messages
    parser_push_context(parser, "parsing case statement");

    node_t *case_node = new_node(NODE_CASE);
    if (!case_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse the word to test - collect multiple tokens until 'in'
    /// This handles cases like: case $var in, case :$PATH: in, case "$foo" in
    char *case_word = NULL;
    size_t case_word_len = 0;

    while (!tokenizer_match(parser->tokenizer, TOK_IN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) &&
           !tokenizer_match(parser->tokenizer, TOK_NEWLINE)) {

        token_t *word_token = tokenizer_current(parser->tokenizer);

        /// Accept word-like tokens, variables, strings, command
        /// substitutions, arithmetic expansions, and backticks for case
        /// words. POSIX 2.10.2 case_clause syntax allows any word, and a
        /// word is any sequence of expansions / characters; in practice
        /// real-world scripts (autoconf's gendocs.sh:425, configure
        /// scripts, init scripts) use `case $(cmd) in ...` and
        /// `case \`cmd\` in ...` heavily. Issue #202.
        if (token_is_word_like(word_token->type) ||
            word_token->type == TOK_VARIABLE ||
            word_token->type == TOK_STRING ||
            word_token->type == TOK_EXPANDABLE_STRING ||
            word_token->type == TOK_COMMAND_SUB ||
            word_token->type == TOK_ARITH_EXP ||
            word_token->type == TOK_BACKQUOTE) {

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

    /// Store the test word
    case_node->val.str = case_word;
    case_node->val_type = VAL_STR;

    /// Skip separators
    skip_separators(parser);

    /// Expect 'in' keyword
    if (!expect_token_with_help(
            parser, TOK_IN,
            "'case' requires 'in' keyword after the test word")) {
        free_node_tree(case_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after 'in'
    skip_separators(parser);

    /// Parse case items until 'esac'
    parser_loop_guard_t items_guard = PARSER_LOOP_GUARD_INIT;
    while (1) {
        /// Skip separators (newlines, comments, optional whitespace) between
        /// case items. Without this, a `;;` followed by a `# trailing
        /// comment` newline `esac` pattern (extremely common in real shell
        /// scripts -- see real_world/posix/100) errors with "expected
        /// pattern" when the loop body tries to parse the comment as a
        /// pattern. POSIX 2.10.2: case_item is `pattern_list ')' compound_list
        /// 'DSEMI'` followed by either another case_item or 'esac', with
        /// linebreaks / comments allowed in between.
        skip_separators(parser);

        if (tokenizer_match(parser->tokenizer, TOK_ESAC) ||
            tokenizer_match(parser->tokenizer, TOK_EOF)) {
            break;
        }

        if (!parser_loop_check_progress(parser, &items_guard,
                                        "parse_case_statement items")) {
            free_node_tree(case_node);
            parser_pop_context(parser);
            return NULL;
        }

        /// Parse pattern(s)
        node_t *case_item = new_node(NODE_CASE_ITEM);
        if (!case_item) {
            free_node_tree(case_node);
            return NULL;
        }

        /// POSIX 2.10.2 permits an optional `(` before the pattern list
        /// (`case_item: [(] pattern_list ')' ...`). Zsh scripts use it
        /// routinely (e.g. add-zsh-hook's `case $opt in (d) ... (D) ...`).
        /// Skip it so the pattern collector sees only the pattern tokens.
        if (tokenizer_match(parser->tokenizer, TOK_LPAREN)) {
            tokenizer_advance(parser->tokenizer);
        }

        /// Terminator will be stored in pattern string prefix (0=break, 1=fall,
        /// 2=cont)
        case_terminator_t terminator = CASE_TERM_BREAK;

        /// Build pattern string (can be multiple patterns separated by |)
        char *pattern = NULL;
        size_t pattern_len = 0;

        /// Set when a numeric-range alternative closed with a `>|` token (the
        /// lexer merges the range's `>` with a following alternation `|`); the
        /// `|` is already consumed, so the do-while must continue anyway.
        bool pipe_from_clobber = false;

        do {
            pipe_from_clobber = false;
            /// Build pattern from multiple tokens until ) or |
            char *single_pattern = NULL;
            size_t single_pattern_len = 0;

            /// Collect tokens for a single pattern until ) or |
            while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
                   !tokenizer_match(parser->tokenizer, TOK_PIPE) &&
                   !tokenizer_match(parser->tokenizer, TOK_EOF) &&
                   !tokenizer_match(parser->tokenizer, TOK_ESAC)) {

                token_t *pattern_token = tokenizer_current(parser->tokenizer);

                /// Accept word-like tokens, wildcards, brackets, variables,
                /// equals (patterns like HOME=*), and comma (patterns like
                /// a,b which bash and dash treat as a literal comma-bearing
                /// pattern). TOK_COMMA is a separate token at the lexer
                /// layer since 80647a09 -- here it concatenates into the
                /// pattern text.
                ///
                /// TOK_DOUBLE_LBRACKET / TOK_DOUBLE_RBRACKET arrive when
                /// the user writes a POSIX character class like
                /// `[[:space:]]` inside a case pattern: the tokenizer
                /// greedily lexes "[[" as the extended-test opener (and
                /// likewise "]]" as the closer), but the case-pattern
                /// context wants their literal text. Accept the tokens
                /// here and append "[[" / "]]" -- match_pattern handles
                /// the resulting `[[:class:]]` string correctly.
                ///
                /// zsh numeric-range case pattern <lo-hi> / <-> / <lo-> /
                /// <-hi> (#205). The lexer fragments `<...>` into a
                /// redirection-shaped run: `<` is REDIRECT_IN, the interior a
                /// WORD (e.g. "1-9", "100-", "-50") or, for the bare `<->`, an
                /// ARROW "->", and the closing `>` REDIRECT_OUT. `<` is never a
                /// redirect in pattern position, so reassemble the literal
                /// "<...>" text; execute_case range-tests it (a malformed or
                /// non-numeric `<...>` falls through to the glob matcher).
                if (pattern_token->type == TOK_REDIRECT_IN) {
                    bool ok = case_pat_append(&single_pattern,
                                              &single_pattern_len, "<");
                    tokenizer_advance(parser->tokenizer);
                    token_t *mid = tokenizer_current(parser->tokenizer);
                    if (ok && mid && mid->type == TOK_ARROW) {
                        /// "<->": the ARROW carries "->" (interior + closing).
                        ok = case_pat_append(&single_pattern,
                                             &single_pattern_len, mid->text);
                        tokenizer_advance(parser->tokenizer);
                    } else if (ok && mid && mid->text &&
                               token_is_word_like(mid->type)) {
                        /// "<1-9>" etc.: a word-like interior then the closing
                        /// `>`. Require word-like so a bare `<` on malformed
                        /// input does not swallow the following `)` or `|`.
                        ok = case_pat_append(&single_pattern,
                                             &single_pattern_len, mid->text);
                        tokenizer_advance(parser->tokenizer);
                        token_t *close = tokenizer_current(parser->tokenizer);
                        if (ok && close && close->type == TOK_REDIRECT_OUT) {
                            ok = case_pat_append(&single_pattern,
                                                 &single_pattern_len, ">");
                            tokenizer_advance(parser->tokenizer);
                        } else if (ok && close &&
                                   close->type == TOK_REDIRECT_CLOBBER) {
                            /// The lexer merged the range's closing `>` with a
                            /// following alternation `|` into one `>|` token.
                            /// Close the range and treat the `|` as the
                            /// alternative separator (the do-while continues,
                            /// the `|` being already consumed here).
                            ok = case_pat_append(&single_pattern,
                                                 &single_pattern_len, ">");
                            tokenizer_advance(parser->tokenizer);
                            pipe_from_clobber = true;
                        }
                    }
                    if (!ok) {
                        free(single_pattern);
                        free(pattern);
                        free_node_tree(case_item);
                        free_node_tree(case_node);
                        return NULL;
                    }
                    if (pipe_from_clobber) {
                        break;
                    }
                    continue;
                }
                if (token_is_word_like(pattern_token->type) ||
                    pattern_token->type == TOK_MULTIPLY ||
                    pattern_token->type == TOK_QUESTION ||
                    pattern_token->type == TOK_GLOB ||
                    pattern_token->type == TOK_LBRACKET ||
                    pattern_token->type == TOK_RBRACKET ||
                    pattern_token->type == TOK_DOUBLE_LBRACKET ||
                    pattern_token->type == TOK_DOUBLE_RBRACKET ||
                    pattern_token->type == TOK_VARIABLE ||
                    pattern_token->type == TOK_ASSIGN ||
                    pattern_token->type == TOK_COMMA) {

                    size_t token_len = strlen(pattern_token->text);
                    char *new_single_pattern = realloc(
                        single_pattern, single_pattern_len + token_len + 1);
                    if (!new_single_pattern) {
                        free(single_pattern);
                        free(pattern);
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
                    /// Unexpected token in pattern
                    break;
                }
            }

            /// If we didn't collect any pattern tokens, that's an error. Any
            /// pattern accumulated from earlier alternatives (e.g. the `a` in
            /// `a|`) is still a standalone allocation here -- not yet attached
            /// to case_item -- so release it too.
            if (!single_pattern) {
                free(pattern);
                free_node_tree(case_item);
                free_node_tree(case_node);
                parser_error_add_with_help(
                    parser, SHELL_ERR_UNEXPECTED_TOKEN,
                    "patterns can contain wildcards: *, ?, [...]",
                    "expected pattern in case statement");
                parser_pop_context(parser);
                return NULL;
            }

            /// Append this single pattern to the overall pattern string
            if (pattern) {
                /// Add | separator and new pattern
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
                /// First pattern
                pattern = single_pattern;
                pattern_len = single_pattern_len;
                single_pattern = NULL; /// Transfer ownership
            }

            free(single_pattern);

            /// Check for | to continue with more patterns. pipe_from_clobber
            /// covers a range alternative that ended in a merged `>|` token
            /// (its `|` is already consumed).
        } while (pipe_from_clobber ||
                 (tokenizer_match(parser->tokenizer, TOK_PIPE) &&
                  (tokenizer_advance(parser->tokenizer), true)));

        /// Store pattern in case item
        case_item->val.str = pattern;
        case_item->val_type = VAL_STR;

        /// Expect )
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

        /// Skip only newlines/whitespace after ), NOT semicolons
        /// (semicolons are case terminators that we need to detect)
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }

        /// Parse commands until case terminator (;;, ;&, ;;&) or esac
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

            /// Check for terminators before processing command
            if (tokenizer_match(parser->tokenizer, TOK_ESAC) ||
                tokenizer_match(parser->tokenizer, TOK_EOF)) {
                break;
            }

            /// Skip newlines, whitespace, and comments, but NOT semicolons
            /// (semicolons are case terminators that we need to detect)
            while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
                   tokenizer_match(parser->tokenizer, TOK_WHITESPACE) ||
                   tokenizer_match(parser->tokenizer, TOK_COMMENT)) {
                tokenizer_advance(parser->tokenizer);
            }

            /// Check for case terminators AFTER skipping separators
            /// Order matters: check ;;& before ;& before ;;
            if (tokenizer_match(parser->tokenizer, TOK_CASE_CONTINUE)) {
                /// ;;& - continue testing next patterns
                terminator = CASE_TERM_CONTINUE;
                tokenizer_advance(parser->tokenizer);
                break;
            }
            if (tokenizer_match(parser->tokenizer, TOK_CASE_FALLTHROUGH)) {
                /// ;& - fall through to next item without testing
                terminator = CASE_TERM_FALLTHROUGH;
                tokenizer_advance(parser->tokenizer);
                break;
            }
            if (tokenizer_match(parser->tokenizer, TOK_SEMICOLON)) {
                token_t *current_semi = tokenizer_current(parser->tokenizer);
                token_t *next = tokenizer_peek(parser->tokenizer);
                /// `;;` is the case_terminator only when the two
                /// semicolons are positionally adjacent. `echo b; ;;
                /// esac` is valid POSIX: a single `;` separator followed
                /// by the `;;` terminator. Without the adjacency check
                /// the parser would consume the first standalone `;`
                /// together with the first `;` of `;;`, leaving the
                /// trailing `;` to look like the start of a new
                /// case_item -- which then fails pattern parsing. bash
                /// and dash both require `;;` to be a single
                /// unspaced token.
                if (next && next->type == TOK_SEMICOLON && current_semi &&
                    next->position == current_semi->end_position) {
                    /// ;; - break (default)
                    terminator = CASE_TERM_BREAK;
                    tokenizer_advance(parser->tokenizer); /// First ;
                    tokenizer_advance(parser->tokenizer); /// Second ;
                    break;
                }
                /// Single semicolon - consume it and continue parsing commands
                tokenizer_advance(parser->tokenizer);
                continue;
            }

            /// Use parse_logical_expression to handle && and || in case body
            node_t *command = parse_logical_expression(parser);
            if (!command) {
                break; /// Can't parse more commands
            }
            if (!commands) {
                commands = command;
            } else {
                /// Link commands as siblings
                node_t *last = commands;
                while (last->next_sibling) {
                    last = last->next_sibling;
                }
                last->next_sibling = command;
            }

            /// Don't skip separators here - we need to detect terminators
            /// explicitly
        }

        /// Add commands as child of case item
        if (commands) {
            add_child_node(case_item, commands);
        }

        /// Encode terminator in pattern string with prefix byte
        /// Format: "<terminator_char><pattern>" where terminator_char is:
        /// '0' = CASE_TERM_BREAK (;;)
        /// '1' = CASE_TERM_FALLTHROUGH (;&)
        /// '2' = CASE_TERM_CONTINUE (;;&)
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

        /// Only skip non-semicolon separators (newlines, whitespace)
        while (tokenizer_match(parser->tokenizer, TOK_NEWLINE) ||
               tokenizer_match(parser->tokenizer, TOK_WHITESPACE)) {
            tokenizer_advance(parser->tokenizer);
        }

        /// Add case item to case statement
        add_child_node(case_node, case_item);
    }

    /// Expect 'esac'
    if (!expect_token_with_help(parser, TOK_ESAC,
                                "'case' statement must end with 'esac'")) {
        free_node_tree(case_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Parse any trailing redirections: case ... esac >/dev/null 2>&1
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

    /// We have word() - this looks like a function definition
    return true;
}

/**
 * @brief Validate function name in POSIX mode
 *
 * Delegates to lush_is_valid_identifier so the predicate is consistent
 * with declare/$var/${var}/arithmetic. The result is byte-identical to
 * the prior isalpha/isalnum check when FEATURE_UNICODE_IDENTIFIERS is
 * off -- which is the default in POSIX mode. A user who explicitly
 * opts in with `shopt -s unicode_identifiers` while in POSIX mode also
 * gets unicode-aware function names here.
 *
 * @param name Function name to validate
 * @return true if name is a valid identifier under the current mode
 */
static bool is_valid_posix_function_name(const char *name) {
    return lush_is_valid_identifier(name);
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
/**
 * @brief Wrap a single NODE_FUNCTION in a brace-group when extra names exist.
 *
 * For zsh's `function NAME1 NAME2 ... { body }` multi-name form: builds a
 * NODE_BRACE_GROUP containing N NODE_FUNCTION children, each carrying one
 * of the names and an independent deep-copied body. The original
 * function_node is reused as the first child (no body copy needed). The
 * extra_names array (heap-allocated strings) is consumed.
 *
 * Returns the new wrapper (caller should `return` it), or NULL on
 * allocation failure (the caller must still free function_node and the
 * extra_names array on error to keep the parser's error-path semantics).
 */
static node_t *wrap_multi_name_functions(node_t *function_node,
                                         char **extra_names,
                                         size_t extra_name_count) {
    if (extra_name_count == 0) {
        free(extra_names);
        return function_node;
    }
    node_t *wrapper = new_node(NODE_BRACE_GROUP);
    if (!wrapper) {
        return NULL;
    }
    add_child_node(wrapper, function_node);

    node_t *original_body = function_node->first_child;
    for (size_t i = 0; i < extra_name_count; i++) {
        node_t *fn = new_node(NODE_FUNCTION);
        if (!fn) {
            free_node_tree(wrapper);
            for (size_t j = i; j < extra_name_count; j++) {
                free(extra_names[j]);
            }
            free(extra_names);
            return NULL;
        }
        fn->val_type = VAL_STR;
        fn->val.str = extra_names[i]; /// ownership transferred
        extra_names[i] = NULL;
        if (original_body) {
            node_t *body_copy = node_copy(original_body);
            if (!body_copy) {
                free_node_tree(fn);
                free_node_tree(wrapper);
                for (size_t j = i + 1; j < extra_name_count; j++) {
                    free(extra_names[j]);
                }
                free(extra_names);
                return NULL;
            }
            add_child_node(fn, body_copy);
        }
        add_child_node(wrapper, fn);
    }
    free(extra_names);
    return wrapper;
}

static node_t *parse_function_definition(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    bool has_function_keyword = false;

    /// Handle "function" keyword form
    if (current && current->type == TOK_FUNCTION) {
        has_function_keyword = true;
        tokenizer_advance(parser->tokenizer);
        current = tokenizer_current(parser->tokenizer);
    }

    /// Push context for better error messages
    parser_push_context(parser, "parsing function definition");

    if (!current || !token_is_word_like(current->type)) {
        parser_error_add_with_help(
            parser, SHELL_ERR_INVALID_FUNCTION,
            "syntax: name() { commands; } or function name { commands; }",
            "expected function name");
        parser_pop_context(parser);
        return NULL;
    }

    /// Create function node
    node_t *function_node = new_node(NODE_FUNCTION);
    if (!function_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Store function name
    function_node->val.str = strdup(current->text);
    function_node->val_type = VAL_STR;
    if (!function_node->val.str) {
        free_node_tree(function_node);
        return NULL;
    }

    /// POSIX compliance: validate function name in posix mode
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

    /// ksh/bash style: "function name { }" - parentheses are optional
    /// POSIX style: "name() { }" - parentheses required
    /// zsh extension:  "function NAME1 NAME2 { body }" -- multi-name
    ///                 function definition: all listed names share one
    ///                 body. Collect additional names here so the body
    ///                 can be parsed once and then deep-copied per name
    ///                 below.
    char **extra_names = NULL;
    size_t extra_name_count = 0;
    current = tokenizer_current(parser->tokenizer);
    if (has_function_keyword) {
        while (current && token_is_word_like(current->type) &&
               current->type != TOK_LBRACE) {
            char **grown = realloc(extra_names, (extra_name_count + 1) *
                                                    sizeof(*extra_names));
            if (!grown) {
                for (size_t i = 0; i < extra_name_count; i++) {
                    free(extra_names[i]);
                }
                free(extra_names);
                free_node_tree(function_node);
                parser_pop_context(parser);
                return NULL;
            }
            extra_names = grown;
            extra_names[extra_name_count] = strdup(current->text);
            if (!extra_names[extra_name_count]) {
                for (size_t i = 0; i < extra_name_count; i++) {
                    free(extra_names[i]);
                }
                free(extra_names);
                free_node_tree(function_node);
                parser_pop_context(parser);
                return NULL;
            }
            extra_name_count++;
            tokenizer_advance(parser->tokenizer);
            current = tokenizer_current(parser->tokenizer);
        }
    }
    if (has_function_keyword && current && current->type == TOK_LBRACE) {
        /// "function name { }" form (with optional extra names already
        /// collected above) -- skip parameter parsing, go to body.
        goto parse_function_body;
    }

    /// Expect '('
    if (!expect_token_with_help(
            parser, TOK_LPAREN,
            "POSIX functions require '()' after the function name")) {
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Parse parameters between ( and )
    function_param_t *params = NULL;
    function_param_t *last_param = NULL;
    int param_count = 0;
    (void)param_count; /// Reserved for parameter limit validation

    /// Check if we have parameters (not immediate ')')
    current = tokenizer_current(parser->tokenizer);
    while (current && current->type != TOK_RPAREN && current->type != TOK_EOF) {
        /// Expect parameter name (word token)
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

        /// Check for default value (= token)
        char *default_value = NULL;
        if (current && current->type == TOK_ASSIGN) {
            tokenizer_advance(parser->tokenizer); /// Skip '='
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

        /// Create parameter structure
        function_param_t *param =
            create_function_param(param_name, default_value);
        if (!param) {
            free(param_name);
            free(default_value);
            free_function_params(params);
            free_node_tree(function_node);
            return NULL;
        }

        /// Add to parameter list
        if (!params) {
            params = param;
        } else {
            last_param->next = param;
        }
        last_param = param;
        param_count++;

        free(param_name);
        free(default_value);

        /// Check for comma or end
        if (current && current->type == TOK_RPAREN) {
            /// End of parameters
            break;
        } else if (current && current->type == TOK_COMMA) {
            tokenizer_advance(parser->tokenizer); /// Skip comma
            current = tokenizer_current(parser->tokenizer);
            /// Continue to next parameter
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

    /// Expect ')'
    if (!expect_token_with_help(parser, TOK_RPAREN,
                                "function parameter list must end with ')'")) {
        free_function_params(params);
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    /// Store parameters in the function node
    /// We need a way to pass this to the executor
    /// Create a special parameter info string to embed in the node
    if (params) {
        /// Encode parameter info as JSON-like string for later parsing
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

            /// Store in function node's string value temporarily
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

        /// Clean up params since we've encoded them
        free_function_params(params);
    }

parse_function_body:
    /// Skip separators before the body
    skip_separators(parser);

    /// The function body may be any compound command (POSIX function_body
    /// is `compound_command`; bash/zsh extend this to (( )), [[ ]], select).
    /// Brace-group bodies use the inline sibling-chain parsing below for AST
    /// compactness. All other compound bodies are parsed by their respective
    /// compound-command parsers and attached as a single child node.
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
            for (size_t i = 0; i < extra_name_count; i++) {
                free(extra_names[i]);
            }
            free(extra_names);
            parser_pop_context(parser);
            return NULL;
        }
        if (!body) {
            free_node_tree(function_node);
            for (size_t i = 0; i < extra_name_count; i++) {
                free(extra_names[i]);
            }
            free(extra_names);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(function_node, body);
        parser_pop_context(parser);

        /// Trailing redirections after the body: `f() ( ... ) > out`.
        /// The compound-body parsers (subshell, if, etc.) consume their own
        /// trailing redirections, so any redirection that reaches here is
        /// attached to the function definition itself.
        if (!parse_trailing_redirections(parser, function_node)) {
            free_node_tree(function_node);
            for (size_t i = 0; i < extra_name_count; i++) {
                free(extra_names[i]);
            }
            free(extra_names);
            return NULL;
        }
        return wrap_multi_name_functions(function_node, extra_names,
                                         extra_name_count);
    }

    /// Brace-group body — consume '{' and parse until '}'
    if (!expect_token_with_help(parser, TOK_LBRACE,
                                "function body must be enclosed in braces { } "
                                "or be a compound command")) {
        free_node_tree(function_node);
        for (size_t i = 0; i < extra_name_count; i++) {
            free(extra_names[i]);
        }
        free(extra_names);
        parser_pop_context(parser);
        return NULL;
    }

    /// Skip separators after '{'
    skip_separators(parser);

    /// Parse function body until '}'
    node_t *body = NULL;
    while (!tokenizer_match(parser->tokenizer, TOK_RBRACE) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            break; /// Can't parse more commands
        }

        if (!body) {
            body = command;
        } else {
            /// Link commands as siblings
            node_t *last = body;
            while (last->next_sibling) {
                last = last->next_sibling;
            }
            last->next_sibling = command;
        }

        /// Skip separators after command
        skip_separators(parser);
    }

    /// Add body as child of function
    if (body) {
        add_child_node(function_node, body);
    }

    /// Expect '}'
    if (!expect_token_with_help(parser, TOK_RBRACE,
                                "function body must end with '}'")) {
        free_node_tree(function_node);
        parser_pop_context(parser);
        return NULL;
    }

    parser_pop_context(parser);

    /// Trailing redirections after `}`: `f() { ...; } > out 2>&1`.
    /// Without this, the redirection token would be left at command position
    /// and the next statement would error with "expected command name".
    if (!parse_trailing_redirections(parser, function_node)) {
        free_node_tree(function_node);
        for (size_t i = 0; i < extra_name_count; i++) {
            free(extra_names[i]);
        }
        free(extra_names);
        return NULL;
    }

    return wrap_multi_name_functions(function_node, extra_names,
                                     extra_name_count);
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

    /// Capture location for arithmetic command
    source_location_t arith_loc =
        token_to_source_location(current, parser->source_name);

    /// Byte offset just past the `((`; the body is sliced from the raw
    /// source starting here (captured before the token is advanced past).
    size_t body_start = current->end_position;

    /// Consume ((
    tokenizer_advance(parser->tokenizer);

    /// Create arithmetic command node
    node_t *arith_node = new_node_at(NODE_ARITH_CMD, arith_loc);
    if (!arith_node) {
        return NULL;
    }

    /// Advance over the body tokens -- tracking nested parentheses -- only to
    /// locate the terminating )) at depth 0. The expression is NOT
    /// reassembled from individual token text: it is captured as the raw
    /// source span between (( and )), so operators, array subscripts, and
    /// spacing reach the arithmetic evaluator exactly as written --
    /// byte-identical to how the $(( )) expansion form is captured. The old
    /// token re-joiner reconstructed the string with heuristic spacing, which
    /// shredded multi-character operators (the `^=` fix, #563) and bracketed
    /// subscripts (`a[0]` -> `a [ 0 ]`, blocking #603); raw capture unifies the
    /// two arithmetic surfaces onto one evaluator input.
    int paren_depth = 0;
    while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
        current = tokenizer_current(parser->tokenizer);
        if (!current) {
            break;
        }

        /// Check for )) - end of arithmetic command (only at depth 0)
        if (current->type == TOK_DOUBLE_RPAREN && paren_depth == 0) {
            break;
        }

        /// Track nested parentheses. Inside the arithmetic context the
        /// tokenizer emits `((`/`))` as TOK_DOUBLE_LPAREN/TOK_DOUBLE_RPAREN, so
        /// a nested grouping like `((1+2))` must be balanced against the
        /// double-paren tokens as well as the single ones -- otherwise the
        /// first inner `))` is mistaken for the command terminator.
        if (current->type == TOK_LPAREN) {
            paren_depth++;
        } else if (current->type == TOK_DOUBLE_LPAREN) {
            paren_depth += 2;
        } else if (current->type == TOK_RPAREN) {
            if (paren_depth > 0) {
                paren_depth--;
            }
        } else if (current->type == TOK_DOUBLE_RPAREN) {
            /// A nested `))` at depth > 0 closes two levels.
            if (paren_depth > 0) {
                paren_depth -= (paren_depth >= 2) ? 2 : 1;
            }
        }

        tokenizer_advance(parser->tokenizer);
    }

    /// Expect ))
    current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_DOUBLE_RPAREN) {
        parser_error_add(parser, SHELL_ERR_UNCLOSED_SUBST, "expected '))'");
        free_node_tree(arith_node);
        return NULL;
    }
    size_t body_end = current->position;  /// byte offset of the ))
    tokenizer_advance(parser->tokenizer); /// consume ))

    /// Slice the raw body [body_start, body_end) and trim surrounding
    /// whitespace (including newlines from a multi-line command).
    const char *src = parser->tokenizer->input;
    if (body_end < body_start) {
        body_end = body_start;
    }
    const char *b = src + body_start;
    size_t raw_len = body_end - body_start;
    while (raw_len > 0 &&
           (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')) {
        b++;
        raw_len--;
    }
    while (raw_len > 0 && (b[raw_len - 1] == ' ' || b[raw_len - 1] == '\t' ||
                           b[raw_len - 1] == '\n' || b[raw_len - 1] == '\r')) {
        raw_len--;
    }
    char *expr = malloc(raw_len + 1);
    if (!expr) {
        free_node_tree(arith_node);
        return NULL;
    }
    memcpy(expr, b, raw_len);
    expr[raw_len] = '\0';

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

    /// Capture location for array literal
    source_location_t array_loc =
        token_to_source_location(current, parser->source_name);

    /// Consume (
    tokenizer_advance(parser->tokenizer);

    /// Create array literal node
    node_t *array_node = new_node_at(NODE_ARRAY_LITERAL, array_loc);
    if (!array_node) {
        return NULL;
    }

    /// Parse elements until )
    while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF)) {

        current = tokenizer_current(parser->tokenizer);
        if (!current) {
            break;
        }

        /// Skip whitespace
        if (current->type == TOK_WHITESPACE || current->type == TOK_NEWLINE) {
            tokenizer_advance(parser->tokenizer);
            continue;
        }

        /// Check for [index]=value syntax
        if (current->type == TOK_LBRACKET) {
            /// Parse indexed element [n]=value
            tokenizer_advance(parser->tokenizer); /// consume [

            /// Collect index expression
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
            tokenizer_advance(parser->tokenizer); /// consume ]

            /// Expect =
            if (!tokenizer_match(parser->tokenizer, TOK_ASSIGN)) {
                parser_error_add(parser, SHELL_ERR_INVALID_ARRAY,
                                 "expected '=' after array index");
                free(index_str);
                free_node_tree(array_node);
                return NULL;
            }
            tokenizer_advance(parser->tokenizer); /// consume =

            /// Get value
            current = tokenizer_current(parser->tokenizer);
            char *value_str = NULL;
            if (current &&
                (token_is_word_like(current->type) ||
                 current->type == TOK_VARIABLE || current->type == TOK_STRING ||
                 current->type == TOK_EXPANDABLE_STRING)) {
                value_str = strdup(current->text);
                tokenizer_advance(parser->tokenizer);
            } else {
                value_str = strdup(""); /// Empty value
            }

            /// Create element node with index:value format
            node_t *elem_node = new_node(NODE_VAR);
            if (!elem_node) {
                free(index_str);
                free(value_str);
                free_node_tree(array_node);
                return NULL;
            }

            /// Store as "[index]=value" for later processing
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
        /// Regular element (no explicit index)
        else if (token_is_word_like(current->type) ||
                 current->type == TOK_VARIABLE || current->type == TOK_STRING ||
                 current->type == TOK_EXPANDABLE_STRING ||
                 current->type == TOK_ARITH_EXP ||
                 current->type == TOK_COMMAND_SUB) {

            node_t *elem_node = NULL;

            /// Create appropriate node type
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
            elem_node->glob_qualified = current->glob_qualified;
            add_child_node(array_node, elem_node);

            tokenizer_advance(parser->tokenizer);
        } else {
            /// Unknown token type in array literal - skip it
            tokenizer_advance(parser->tokenizer);
        }
    }

    /// Expect )
    if (!tokenizer_match(parser->tokenizer, TOK_RPAREN)) {
        parser_error_add(parser, SHELL_ERR_INVALID_ARRAY,
                         "expected ')' to close array literal");
        free_node_tree(array_node);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer); /// consume )

    return array_node;
}

/* ============================================================================
 * Extended-test [[ ]] conditional-expression parser
 *
 * parse_extended_test builds a real conditional AST (NODE_COND_OR / _AND /
 * _NOT / _BINARY / _UNARY) instead of flattening tokens into a string that is
 * re-parsed at execution time. Operands are ordinary word-nodes built by
 * collect_word_argument (carrying per-character quote provenance); operators
 * are structure. The executor tree-walks the result.
 *
 * Grammar (recursive descent, bash/zsh consensus precedence):
 *
 *   or_expr  := and_expr ( '||' and_expr )*     // left-assoc, lowest
 *   and_expr := not_expr ( '&&' not_expr )*      // left-assoc, tighter
 *   not_expr := '!' not_expr | primary           // right-recursive
 *   primary  := '(' or_expr ')'                   // grouping (primary start)
 *             | unary_op word                     // -z/-n/.../-G  then operand
 *             | word binary_op word               // == = != =~ < > -eq..-nt..
 *             | word                              // truthiness
 *
 * The AST fixes two quirks of the former string flattener that both matched
 * bash and zsh: `a && b || c` now groups `(a && b) || c` (was `a && (b||c)`),
 * and `! ( expr )` now negates the group (was always false).
 * ============================================================================
 */

/// True when a word token's text is one of the binary word-operators
/// (arithmetic comparisons and file relations). Such a word is an operator at
/// the operator position, never a unary operator or a bare operand.
static bool cond_is_binary_word_op(const char *text) {
    static const char *const ops[] = {"-eq", "-ne", "-lt", "-le", "-gt",
                                      "-ge", "-nt", "-ot", "-ef"};
    if (!text) {
        return false;
    }
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (strcmp(text, ops[i]) == 0) {
            return true;
        }
    }
    return false;
}

/// True when the token at a PRIMARY position is a unary operator: a bare word
/// token whose text is '-' followed by an alphabetic character and which is
/// not one of the binary word-operators. A quoted operand (`"-z"`) is a string,
/// never an operator.
static bool cond_is_unary_op(token_t *tok) {
    if (!tok || tok->type != TOK_WORD || !tok->text) {
        return false;
    }
    if (tok->text[0] != '-' || !isalpha((unsigned char)tok->text[1])) {
        return false;
    }
    return !cond_is_binary_word_op(tok->text);
}

/// True when the token is a standalone '!' (negation), distinct from '!='
/// (TOK_NOT_EQUAL) and from extglob '!(...)' (which tokenizes as a word).
static bool cond_is_bang(token_t *tok) {
    return tok && tok->type == TOK_WORD && tok->text && tok->text[0] == '!' &&
           tok->text[1] == '\0';
}

/// Collect one fused word-node operand via collect_word_argument (the single
/// source of truth for word acceptance, adjacency concatenation, node-type
/// classification and quote provenance). Returns the detached child node, or
/// NULL if the current token does not begin a word or on allocation failure.
static node_t *cond_collect_operand(parser_t *parser) {
    node_t temp;
    memset(&temp, 0, sizeof(temp));
    if (!collect_word_argument(parser, &temp)) {
        return NULL;
    }
    node_t *child = temp.first_child;
    if (child) {
        child->prev_sibling = NULL;
        child->next_sibling = NULL;
    }
    return child;
}

/// Collect the raw text of a paren/regex pattern run for the RHS of ==/!=/=~.
/// At an RHS-operand position `(`, `)` and `|` are pattern content, not
/// grouping, so the run is gathered as raw concatenated token text tracking
/// paren depth. A top-level `)` (depth 0) belongs to an enclosing group and
/// stops the run; a whitespace gap at depth 0 also ends the operand. On return
/// *balanced is false if a `(` was left open (caller reports a parse error).
/// Consumes every token gathered. Returns a heap string (owner), or NULL on
/// allocation failure.
static char *cond_collect_paren_run(parser_t *parser, bool *balanced) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int depth = 0;
    size_t last_end = 0;
    bool first = true;

    for (;;) {
        token_t *tok = tokenizer_current(parser->tokenizer);
        if (!tok || tok->type == TOK_EOF) {
            break;
        }
        if (!first && depth == 0) {
            /// Top-level boundary: a whitespace gap or a structural terminator
            /// ends the operand; a `)` here closes an enclosing group.
            if (tok->position != last_end || tok->type == TOK_DOUBLE_RBRACKET ||
                tok->type == TOK_LOGICAL_AND || tok->type == TOK_LOGICAL_OR ||
                tok->type == TOK_RPAREN) {
                break;
            }
        }
        size_t tlen = strlen(tok->text);
        if (len + tlen + 1 > cap) {
            size_t ncap = (len + tlen + 1) * 2;
            char *nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                if (balanced) {
                    *balanced = false;
                }
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + len, tok->text, tlen);
        len += tlen;
        buf[len] = '\0';

        if (tok->type == TOK_LPAREN) {
            depth++;
        } else if (tok->type == TOK_RPAREN) {
            depth--;
        }
        last_end = tok->end_position;
        first = false;
        tokenizer_advance(parser->tokenizer);
    }

    if (!buf) {
        buf = strdup("");
    }
    if (balanced) {
        *balanced = (depth == 0);
    }
    return buf;
}

/// Build a bare NODE_VAR word-node from raw pattern text (quote_prov NULL: an
/// all-unquoted pattern). Frees `text` on failure. Returns NULL on error.
static node_t *cond_make_pattern_node(parser_t *parser, char *text) {
    node_t *node = new_node(NODE_VAR);
    if (!node) {
        free(text);
        parser->has_error = true;
        return NULL;
    }
    node->val.str = text;
    node->val_type = VAL_STR;
    return node;
}

/// Collect the RHS operand of a ==/!=/=~ comparison. A quoted operand
/// (`"@(a|b)"`, `"a b"`) goes through collect_word_argument so its quote
/// provenance survives. An unquoted operand whose contiguous run involves
/// parens or `|` is a pattern/regex whose parens are content, not grouping
/// (`(a|b)`, `^([a-z]+)([0-9]+)$`, `a|b`); it is gathered as raw text with no
/// quote provenance. `@(a|b)` and `[a-z]*` already fuse as ordinary words.
static node_t *cond_collect_rhs(parser_t *parser) {
    token_t *tok = tokenizer_current(parser->tokenizer);
    if (!tok) {
        return NULL;
    }
    /// Quoted RHS: ordinary collection preserves quote_prov.
    if (tok->type == TOK_STRING || tok->type == TOK_EXPANDABLE_STRING) {
        return cond_collect_operand(parser);
    }
    /// RHS begins with a bare `(`: the whole operand is a paren pattern/regex.
    if (tok->type == TOK_LPAREN) {
        bool balanced = true;
        char *text = cond_collect_paren_run(parser, &balanced);
        if (!text) {
            parser->has_error = true;
            return NULL;
        }
        if (!balanced) {
            parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                             "unbalanced '(' in extended-test pattern");
            free(text);
            return NULL;
        }
        return cond_make_pattern_node(parser, text);
    }
    /// Collect the leading word run.
    node_t *node = cond_collect_operand(parser);
    if (!node) {
        return NULL;
    }
    /// A contiguous `(` or `|` after the leading word means the operand is a
    /// pattern/regex whose parens/alternation are content (e.g. the leading
    /// `^` of `^([a-z]+)([0-9]+)$`). Append the raw run and drop quote
    /// provenance -- an all-unquoted pattern; NULL provenance means no quoting.
    tok = tokenizer_current(parser->tokenizer);
    if (tok && (tok->type == TOK_LPAREN || tok->type == TOK_PIPE)) {
        bool balanced = true;
        char *run = cond_collect_paren_run(parser, &balanced);
        if (!run) {
            free_node_tree(node);
            parser->has_error = true;
            return NULL;
        }
        if (!balanced) {
            parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                             "unbalanced '(' in extended-test pattern");
            free(run);
            free_node_tree(node);
            return NULL;
        }
        const char *base = node->val.str ? node->val.str : "";
        size_t nl = strlen(base) + strlen(run) + 1;
        char *combined = malloc(nl);
        if (!combined) {
            free(run);
            free_node_tree(node);
            parser->has_error = true;
            return NULL;
        }
        snprintf(combined, nl, "%s%s", base, run);
        free(run);
        free(node->val.str);
        node->val.str = combined;
        node->val_type = VAL_STR;
        node->type = NODE_VAR;
        free(node->quote_prov);
        node->quote_prov = NULL;
        free(node->magic_equal_value);
        node->magic_equal_value = NULL;
        node->glob_qualified = false;
    }
    return node;
}

/// Recognize and consume a binary operator at the operator position (after an
/// LHS operand). Returns the canonical operator spelling (heap, owner), or NULL
/// if the current token is not a binary operator. `=` and `==` both
/// canonicalize to "==".
static char *cond_binary_op(parser_t *parser) {
    token_t *tok = tokenizer_current(parser->tokenizer);
    if (!tok) {
        return NULL;
    }
    switch (tok->type) {
    case TOK_REGEX_MATCH:
        tokenizer_advance(parser->tokenizer);
        return strdup("=~");
    case TOK_NOT_EQUAL:
        tokenizer_advance(parser->tokenizer);
        return strdup("!=");
    case TOK_REDIRECT_IN:
        tokenizer_advance(parser->tokenizer);
        return strdup("<");
    case TOK_REDIRECT_OUT:
        tokenizer_advance(parser->tokenizer);
        return strdup(">");
    case TOK_ASSIGN:
        /// `==` arrives as two adjacent TOK_ASSIGN; a lone `=` aliases to `==`.
        tokenizer_advance(parser->tokenizer);
        if (tokenizer_match(parser->tokenizer, TOK_ASSIGN)) {
            tokenizer_advance(parser->tokenizer);
        }
        return strdup("==");
    default:
        if (tok->type == TOK_WORD && cond_is_binary_word_op(tok->text)) {
            char *op = strdup(tok->text);
            tokenizer_advance(parser->tokenizer);
            return op;
        }
        return NULL;
    }
}

static node_t *cond_primary(parser_t *parser) {
    token_t *tok = tokenizer_current(parser->tokenizer);
    if (!tok || tok->type == TOK_EOF || tok->type == TOK_DOUBLE_RBRACKET) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected operand in extended test");
        return NULL;
    }

    source_location_t loc = token_to_source_location(tok, parser->source_name);

    /// Grouping: '(' or_expr ')' -- only at a primary start. The group node is
    /// transparent (the inner subtree is returned directly).
    if (tok->type == TOK_LPAREN) {
        tokenizer_advance(parser->tokenizer);
        node_t *inner = cond_or_expr(parser);
        if (!inner) {
            return NULL;
        }
        if (!tokenizer_match(parser->tokenizer, TOK_RPAREN)) {
            parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                             "expected ')' to close group in extended test");
            free_node_tree(inner);
            return NULL;
        }
        tokenizer_advance(parser->tokenizer);
        return inner;
    }

    /// Unary operator: -z/-n/-v/-o/-e/-f/.../-G  then one operand.
    if (cond_is_unary_op(tok)) {
        char *op = strdup(tok->text);
        if (!op) {
            parser->has_error = true;
            return NULL;
        }
        tokenizer_advance(parser->tokenizer);
        node_t *operand = cond_collect_operand(parser);
        if (!operand) {
            parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                             "expected operand after unary operator '%s'", op);
            free(op);
            return NULL;
        }
        node_t *node = new_node_at(NODE_COND_UNARY, loc);
        if (!node) {
            free(op);
            free_node_tree(operand);
            parser->has_error = true;
            return NULL;
        }
        node->val.str = op;
        node->val_type = VAL_STR;
        add_child_node(node, operand);
        return node;
    }

    /// LHS operand.
    node_t *lhs = cond_collect_operand(parser);
    if (!lhs) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected operand in extended test");
        return NULL;
    }

    /// Binary comparison?
    char *op = cond_binary_op(parser);
    if (op) {
        node_t *rhs = cond_collect_rhs(parser);
        if (!rhs) {
            if (!parser->has_error) {
                parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                                 "expected operand after '%s'", op);
            }
            free(op);
            free_node_tree(lhs);
            return NULL;
        }
        node_t *node = new_node_at(NODE_COND_BINARY, loc);
        if (!node) {
            free(op);
            free_node_tree(lhs);
            free_node_tree(rhs);
            parser->has_error = true;
            return NULL;
        }
        node->val.str = op;
        node->val_type = VAL_STR;
        add_child_node(node, lhs);
        add_child_node(node, rhs);
        return node;
    }

    /// Bare word -- truthiness primary.
    return lhs;
}

static node_t *cond_not_expr(parser_t *parser) {
    token_t *tok = tokenizer_current(parser->tokenizer);
    if (cond_is_bang(tok)) {
        source_location_t loc =
            token_to_source_location(tok, parser->source_name);
        tokenizer_advance(parser->tokenizer);
        node_t *sub = cond_not_expr(parser);
        if (!sub) {
            return NULL;
        }
        node_t *node = new_node_at(NODE_COND_NOT, loc);
        if (!node) {
            free_node_tree(sub);
            parser->has_error = true;
            return NULL;
        }
        add_child_node(node, sub);
        return node;
    }
    return cond_primary(parser);
}

static node_t *cond_and_expr(parser_t *parser) {
    node_t *left = cond_not_expr(parser);
    if (!left) {
        return NULL;
    }
    while (tokenizer_match(parser->tokenizer, TOK_LOGICAL_AND)) {
        source_location_t loc = token_to_source_location(
            tokenizer_current(parser->tokenizer), parser->source_name);
        tokenizer_advance(parser->tokenizer);
        node_t *right = cond_not_expr(parser);
        if (!right) {
            free_node_tree(left);
            return NULL;
        }
        node_t *node = new_node_at(NODE_COND_AND, loc);
        if (!node) {
            free_node_tree(left);
            free_node_tree(right);
            parser->has_error = true;
            return NULL;
        }
        add_child_node(node, left);
        add_child_node(node, right);
        left = node;
    }
    return left;
}

static node_t *cond_or_expr(parser_t *parser) {
    node_t *left = cond_and_expr(parser);
    if (!left) {
        return NULL;
    }
    while (tokenizer_match(parser->tokenizer, TOK_LOGICAL_OR)) {
        source_location_t loc = token_to_source_location(
            tokenizer_current(parser->tokenizer), parser->source_name);
        tokenizer_advance(parser->tokenizer);
        node_t *right = cond_and_expr(parser);
        if (!right) {
            free_node_tree(left);
            return NULL;
        }
        node_t *node = new_node_at(NODE_COND_OR, loc);
        if (!node) {
            free_node_tree(left);
            free_node_tree(right);
            parser->has_error = true;
            return NULL;
        }
        add_child_node(node, left);
        add_child_node(node, right);
        left = node;
    }
    return left;
}

/**
 * @brief Parse an extended test command [[ expression ]]
 *
 * Parses Bash/Zsh-style extended test expressions into a conditional AST.
 * Supports string comparisons (== = != < >), pattern matching (== with
 * globs), regex matching (=~), logical operators (&& || !), grouping
 * ( expr ), and file/string unary tests (-f -d -e -z -n -v -o ...).
 *
 * Unlike [ ], extended tests do not word-split or glob-expand operands and
 * spell logical connectives &&/||/! directly.
 *
 * Grammar: [[ conditional_expression ]]. An empty `[[ ]]` yields a wrapper
 * node with zero children (evaluates false).
 *
 * @param parser Parser instance
 * @return NODE_EXTENDED_TEST wrapping the conditional expression tree, or NULL
 *         on a grammar violation (a parse error is reported).
 */
static node_t *parse_extended_test(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_DOUBLE_LBRACKET) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN, "expected '[['");
        return NULL;
    }

    /// Capture source location BEFORE advancing (advance frees current token).
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    /// Consume [[
    tokenizer_advance(parser->tokenizer);

    node_t *test_node = new_node_at(NODE_EXTENDED_TEST, loc);
    if (!test_node) {
        parser_error_add(parser, SHELL_ERR_OUT_OF_MEMORY,
                         "failed to create extended test node");
        return NULL;
    }

    /// Empty `[[ ]]` -> wrapper with zero children (evaluates false).
    if (tokenizer_match(parser->tokenizer, TOK_DOUBLE_RBRACKET)) {
        tokenizer_advance(parser->tokenizer);
        return test_node;
    }

    node_t *expr = cond_or_expr(parser);
    if (!expr) {
        free_node_tree(test_node);
        return NULL;
    }

    if (!tokenizer_match(parser->tokenizer, TOK_DOUBLE_RBRACKET)) {
        parser_error_add(parser, SHELL_ERR_UNCLOSED_CONTROL,
                         "expected ']]' to close extended test");
        free_node_tree(expr);
        free_node_tree(test_node);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer); /// consume ]]

    add_child_node(test_node, expr);
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

    /// Determine type based on token
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

    /// Check if feature is enabled
    if (!shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
        parser_error_add(parser, SHELL_ERR_FEATURE_DISABLED,
                         "process substitution not enabled");
        return NULL;
    }

    /// Create the process substitution node with source location
    source_location_t loc =
        token_to_source_location(current, parser->source_name);
    node_t *proc_sub_node = new_node_at(node_type, loc);
    if (!proc_sub_node) {
        parser_error_add(parser, SHELL_ERR_OUT_OF_MEMORY,
                         "failed to create process substitution node");
        return NULL;
    }

    /// Consume the <( or >( token
    tokenizer_advance(parser->tokenizer);

    /// Skip whitespace after opening
    skip_separators(parser);

    /// Parse commands until ')'
    while (!tokenizer_match(parser->tokenizer, TOK_RPAREN) &&
           !tokenizer_match(parser->tokenizer, TOK_EOF) && !parser->has_error) {

        node_t *command = parse_logical_expression(parser);
        if (!command) {
            if (!parser->has_error) {
                break; /// End of input
            }
            free_node_tree(proc_sub_node);
            return NULL;
        }

        add_child_node(proc_sub_node, command);

        /// Skip separators between commands
        skip_separators(parser);
    }

    /// Expect ')'
    if (!expect_token(parser, TOK_RPAREN)) {
        free_node_tree(proc_sub_node);
        return NULL;
    }

    /// Store the operator for debugging
    proc_sub_node->val.str = strdup(op_name);
    proc_sub_node->val_type = VAL_STR;

    return proc_sub_node;
}

/**
 * @brief Check whether the given text names a valid typed-function kind.
 *
 * The three admitted kinds are `scalar`, `list`, and `map` -- the
 * same first-class value kinds the executor and symtable use. No
 * `any` or polymorphic kind: helpers that want to be untyped use
 * the POSIX function form instead.
 */
static bool is_valid_fn_kind_name(const char *text) {
    if (!text) {
        return false;
    }
    return (strcmp(text, "scalar") == 0) || (strcmp(text, "list") == 0) ||
           (strcmp(text, "map") == 0);
}

/**
 * @brief Parse a typed-function declaration.
 *
 * Grammar:
 *     fn IDENT ( [param [, param]*] ) [-> kind] compound_command
 *     param := IDENT ":" kind
 *     kind  := "scalar" | "list" | "map"
 *
 * AST representation (node.h NODE_FN_DECL):
 *   val.str = "name\x1f<return_kind>\x1fp1:kind1\x1fp2:kind2..."
 *   first_child = body (a brace group / compound command).
 *
 * Empty parameter list and absent return kind are both legal. An
 * absent return kind is encoded as an empty string between the two
 * \x1F separators; void functions have no `return EXPR` (caught at
 * resolve time in a later phase).
 *
 * @param parser Parser instance positioned at TOK_FN.
 * @return New NODE_FN_DECL node, or NULL on error (parser->has_error set).
 */
static node_t *parse_fn_declaration(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_FN) {
        parser_error_add(parser, SHELL_ERR_UNEXPECTED_TOKEN,
                         "expected 'fn' keyword");
        return NULL;
    }

    /// Capture the source location of the 'fn' keyword for diagnostics.
    source_location_t fn_loc =
        token_to_source_location(current, parser->source_name);

    parser_push_context(parser, "parsing typed-function declaration");

    /// Consume 'fn'.
    tokenizer_advance(parser->tokenizer);
    current = tokenizer_current(parser->tokenizer);

    /// Function name.
    if (!current || !token_is_word_like(current->type)) {
        parser_error_add_with_help(
            parser, SHELL_ERR_INVALID_FUNCTION,
            "syntax: fn name(p: kind, ...) [-> kind] { body }",
            "expected function name after 'fn'");
        parser_pop_context(parser);
        return NULL;
    }

    char *name = strdup(current->text);
    if (!name) {
        parser_pop_context(parser);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer);

    /// Opening paren is mandatory.
    if (!expect_token_with_help(
            parser, TOK_LPAREN,
            "typed functions require '(' after the function name")) {
        free(name);
        parser_pop_context(parser);
        return NULL;
    }

    /// Buffer the encoded parameter signature.
    char param_buf[2048];
    param_buf[0] = '\0';
    size_t param_len = 0;
    int param_count = 0;

    current = tokenizer_current(parser->tokenizer);
    while (current && current->type != TOK_RPAREN && current->type != TOK_EOF) {
        /// Parameter forms accepted (the tokenizer treats ':' as a word
        /// char, so the surface form `name: kind` may arrive in a few
        /// shapes depending on whitespace):
        ///   1. one TOK_WORD `name:kind`           (no whitespace anywhere)
        ///   2. TOK_WORD `name:` then TOK_WORD `kind`  (space after ':')
        ///   3. TOK_WORD `name` then TOK_WORD `:kind`  (space before ':')
        ///   4. TOK_WORD `name` then TOK_WORD `:` then TOK_WORD `kind`
        ///
        /// In every case we extract the colon-delimited (name, kind)
        /// pair by string-splitting around the first ':'.
        if (!token_is_word_like(current->type)) {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "each parameter must be 'name: kind' where kind is "
                "scalar, list, or map",
                "expected parameter name");
            free(name);
            parser_pop_context(parser);
            return NULL;
        }

        /// Gather up to three consecutive word tokens forming the
        /// parameter and slice them around the first ':'.
        char composite[256];
        composite[0] = '\0';
        size_t composite_len = 0;
        for (int i = 0; i < 3; i++) {
            if (!current || !token_is_word_like(current->type)) {
                break;
            }
            size_t tl = strlen(current->text);
            if (composite_len + tl + 1 >= sizeof(composite)) {
                break;
            }
            memcpy(composite + composite_len, current->text, tl);
            composite_len += tl;
            composite[composite_len] = '\0';
            tokenizer_advance(parser->tokenizer);
            /// Stop once we have a ':' AND non-empty text after it.
            const char *colon = strchr(composite, ':');
            if (colon && colon[1] != '\0') {
                break;
            }
            current = tokenizer_current(parser->tokenizer);
            /// If the next token is TOK_COMMA or TOK_RPAREN, we've run
            /// out of words for this parameter -- stop collecting.
            if (!current || current->type == TOK_COMMA ||
                current->type == TOK_RPAREN) {
                break;
            }
        }

        const char *colon = strchr(composite, ':');
        if (!colon || colon == composite || colon[1] == '\0') {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "annotate each parameter with its kind: name: scalar / "
                "list / map",
                "expected 'name: kind' in parameter list (got '%s')",
                composite);
            free(name);
            parser_pop_context(parser);
            return NULL;
        }
        size_t p_name_len = (size_t)(colon - composite);
        char *p_name = malloc(p_name_len + 1);
        if (!p_name) {
            free(name);
            parser_pop_context(parser);
            return NULL;
        }
        memcpy(p_name, composite, p_name_len);
        p_name[p_name_len] = '\0';
        const char *p_kind = colon + 1;

        if (!is_valid_fn_kind_name(p_kind)) {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "valid kinds are: scalar, list, map (no 'any' in the "
                "initial form)",
                "unknown parameter kind '%s' for parameter '%s'", p_kind,
                p_name);
            free(p_name);
            free(name);
            parser_pop_context(parser);
            return NULL;
        }

        int written =
            snprintf(param_buf + param_len, sizeof(param_buf) - param_len,
                     "\x1f%s:%s", p_name, p_kind);
        if (written < 0 || (size_t)written >= sizeof(param_buf) - param_len) {
            parser_error_add(parser, SHELL_ERR_INVALID_FUNCTION,
                             "typed-function signature too large");
            free(p_name);
            free(name);
            parser_pop_context(parser);
            return NULL;
        }
        param_len += (size_t)written;
        param_count++;
        free(p_name);

        /// Optional comma between parameters.
        current = tokenizer_current(parser->tokenizer);
        if (current && current->type == TOK_COMMA) {
            tokenizer_advance(parser->tokenizer);
            current = tokenizer_current(parser->tokenizer);
        }
    }

    /// Closing paren.
    if (!expect_token_with_help(parser, TOK_RPAREN,
                                "typed functions require ')' to close "
                                "the parameter list")) {
        free(name);
        parser_pop_context(parser);
        return NULL;
    }

    /// Optional return-kind annotation: -> kind. The tokenizer emits
    /// TOK_ARROW only when '-' and '>' are adjacent (no whitespace),
    /// matching the design's adjacency requirement. The return-kind
    /// text must be copied here -- tokenizer_advance frees the token
    /// it pointed at, and the encoded signature is built much later
    /// after the body is parsed.
    char *return_kind = NULL;
    current = tokenizer_current(parser->tokenizer);
    if (current && current->type == TOK_ARROW) {
        tokenizer_advance(parser->tokenizer);
        current = tokenizer_current(parser->tokenizer);
        if (!current || !token_is_word_like(current->type) ||
            !is_valid_fn_kind_name(current->text)) {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "valid return kinds are: scalar, list, map (no 'any')",
                "expected return kind after '->'");
            free(name);
            parser_pop_context(parser);
            return NULL;
        }
        return_kind = strdup(current->text);
        if (!return_kind) {
            free(name);
            parser_pop_context(parser);
            return NULL;
        }
        tokenizer_advance(parser->tokenizer);
    }

    /// Body: must be a brace group. Skip newlines between signature
    /// and body.
    while (tokenizer_match(parser->tokenizer, TOK_NEWLINE)) {
        tokenizer_advance(parser->tokenizer);
    }
    current = tokenizer_current(parser->tokenizer);
    if (!current || current->type != TOK_LBRACE) {
        parser_error_add_with_help(
            parser, SHELL_ERR_INVALID_FUNCTION,
            "typed-function bodies must be enclosed in '{ ... }'",
            "expected '{' to begin the function body");
        free(name);
        parser_pop_context(parser);
        return NULL;
    }

    /// Track fn-body depth so `return expression` inside the body is
    /// recognized as the typed return statement.
    parser->fn_body_depth++;
    node_t *body = parse_brace_group(parser);
    parser->fn_body_depth--;
    if (!body) {
        free(name);
        free(return_kind);
        parser_pop_context(parser);
        return NULL;
    }

    /// Build the encoded val.str: "name\x1f<return_kind>\x1f<params>".
    /// param_buf already has a leading \x1F per entry; the second
    /// \x1F here separates return_kind from the first param.
    const char *rk = return_kind ? return_kind : "";
    size_t encoded_size = strlen(name) + 1 + strlen(rk) + 1 + param_len + 1;
    char *encoded = malloc(encoded_size);
    if (!encoded) {
        free_node_tree(body);
        free(name);
        free(return_kind);
        parser_pop_context(parser);
        return NULL;
    }
    int n = snprintf(encoded, encoded_size, "%s\x1f%s%s", name, rk, param_buf);
    if (n < 0 || (size_t)n >= encoded_size) {
        free(encoded);
        free_node_tree(body);
        free(name);
        free(return_kind);
        parser_pop_context(parser);
        return NULL;
    }
    free(name);
    free(return_kind);

    node_t *decl = new_node_at(NODE_FN_DECL, fn_loc);
    if (!decl) {
        free(encoded);
        free_node_tree(body);
        parser_pop_context(parser);
        return NULL;
    }
    decl->val.str = encoded;
    decl->val_type = VAL_STR;
    add_child_node(decl, body);

    (void)param_count; /// reserved for static-check coupling
    parser_pop_context(parser);
    return decl;
}

/**
 * @brief Recognize the let-fn-call form by peek-ahead.
 *
 * Returns true iff the token stream beginning at the current `let`
 * matches `let IDENT = IDENT (` with the IDENT and the `(` adjacent
 * in the input (no whitespace between them).
 *
 * Does not consume tokens: the recognizer saves the tokenizer state,
 * walks five tokens forward, and restores. The parser handler then
 * re-walks the same tokens to build the AST.
 *
 * Anything that does not match falls through to the existing
 * arithmetic-let builtin path.
 */
static bool is_let_fn_call_form(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || !current->text || strcmp(current->text, "let") != 0) {
        return false;
    }

    size_t saved_pos = current->position;
    size_t saved_line = parser->tokenizer->line;
    size_t saved_col = parser->tokenizer->column;

    bool matched = false;

    /// Consume `let`.
    tokenizer_advance(parser->tokenizer);
    token_t *lhs = tokenizer_current(parser->tokenizer);
    if (!lhs || !token_is_word_like(lhs->type)) {
        goto restore;
    }

    /// Consume LHS identifier; expect '='.
    tokenizer_advance(parser->tokenizer);
    token_t *eq = tokenizer_current(parser->tokenizer);
    if (!eq || eq->type != TOK_ASSIGN) {
        goto restore;
    }

    /// Consume '='; expect a call-target identifier adjacent to '('.
    tokenizer_advance(parser->tokenizer);
    token_t *callee = tokenizer_current(parser->tokenizer);
    if (!callee || !token_is_word_like(callee->type)) {
        goto restore;
    }
    size_t callee_end = callee->end_position;

    tokenizer_advance(parser->tokenizer);
    token_t *lparen = tokenizer_current(parser->tokenizer);
    if (!lparen || lparen->type != TOK_LPAREN) {
        goto restore;
    }
    if (lparen->position != callee_end) {
        /// `let x = foo (...)` with whitespace before '(' falls through
        /// to the arithmetic-let path. The space-before-paren failure
        /// mode is intentional per the design.
        goto restore;
    }

    matched = true;

restore:
    parser->tokenizer->position = saved_pos;
    parser->tokenizer->line = saved_line;
    parser->tokenizer->column = saved_col;
    tokenizer_refresh_from_position(parser->tokenizer);
    return matched;
}

/**
 * @brief Parse a typed-function call expression `callee(args)`.
 *
 * The parser is positioned at the callee identifier. Each argument is
 * collected as a single word token (a full expression-argument grammar
 * arrives with the executor pass; arguments here are parsed as words /
 * strings / variable references, which is what the typed-call site
 * binds to parameters). Arguments are separated by ',' tokens; trailing
 * commas are tolerated.
 *
 * @return NODE_FN_CALL with the callee name in val.str and arguments
 *         as children, or NULL on error.
 */
static node_t *parse_fn_call_expression(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || !token_is_word_like(current->type)) {
        parser_error_add(parser, SHELL_ERR_INVALID_FUNCTION,
                         "expected function name in call expression");
        return NULL;
    }

    source_location_t call_loc =
        token_to_source_location(current, parser->source_name);
    char *callee = strdup(current->text);
    if (!callee) {
        return NULL;
    }
    tokenizer_advance(parser->tokenizer);

    if (!expect_token_with_help(
            parser, TOK_LPAREN,
            "typed-function call requires '(' immediately after the "
            "function name")) {
        free(callee);
        return NULL;
    }

    node_t *call_node = new_node_at(NODE_FN_CALL, call_loc);
    if (!call_node) {
        free(callee);
        return NULL;
    }
    call_node->val.str = callee;
    call_node->val_type = VAL_STR;

    /// Collect arguments until ')'.
    current = tokenizer_current(parser->tokenizer);
    while (current && current->type != TOK_RPAREN && current->type != TOK_EOF) {
        if (!token_is_word_like(current->type) && current->type != TOK_STRING &&
            current->type != TOK_EXPANDABLE_STRING &&
            current->type != TOK_VARIABLE) {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "arguments to a typed-function call must be words, "
                "strings, or variable references",
                "unexpected token in argument list");
            free_node_tree(call_node);
            return NULL;
        }

        source_location_t arg_loc =
            token_to_source_location(current, parser->source_name);
        /// Map token type -> AST node type so the executor can dispatch
        /// on kind without re-tokenizing. NODE_VAR for bare $name refs
        /// (so list/map kinds survive the boundary), NODE_STRING_LITERAL
        /// for single-quoted strings (no expansion), NODE_STRING_EXPANDABLE
        /// for double-quoted strings (full expansion), NODE_COMMAND text
        /// for plain words.
        node_type_t arg_type = NODE_COMMAND;
        if (current->type == TOK_VARIABLE) {
            arg_type = NODE_VAR;
        } else if (current->type == TOK_STRING) {
            arg_type = NODE_STRING_LITERAL;
        } else if (current->type == TOK_EXPANDABLE_STRING) {
            arg_type = NODE_STRING_EXPANDABLE;
        }
        node_t *arg = new_node_at(arg_type, arg_loc);
        if (!arg) {
            free_node_tree(call_node);
            return NULL;
        }
        arg->val.str = strdup(current->text);
        arg->val_type = VAL_STR;
        if (!arg->val.str) {
            free_node_tree(arg);
            free_node_tree(call_node);
            return NULL;
        }
        add_child_node(call_node, arg);
        tokenizer_advance(parser->tokenizer);

        /// Optional comma between arguments.
        current = tokenizer_current(parser->tokenizer);
        if (current && current->type == TOK_COMMA) {
            tokenizer_advance(parser->tokenizer);
            current = tokenizer_current(parser->tokenizer);
        }
    }

    if (!expect_token_with_help(
            parser, TOK_RPAREN,
            "typed-function call requires ')' to close the argument "
            "list")) {
        free_node_tree(call_node);
        return NULL;
    }

    return call_node;
}

/**
 * @brief Parse `let NAME = callee(args)` capture form.
 *
 * Recognized only when `is_let_fn_call_form` returned true. Builds a
 * NODE_LET_FN whose val.str is the LHS variable name, with a single
 * child NODE_FN_CALL carrying the call expression. Existing
 * arithmetic-let forms (`let x=5+3`, `let "x += 1"`) take a different
 * dispatch path and continue to reach the bin_let builtin unchanged.
 */
static node_t *parse_let_fn_call(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t let_loc =
        token_to_source_location(current, parser->source_name);

    parser_push_context(parser, "parsing 'let' typed-function call");

    /// Consume `let`.
    tokenizer_advance(parser->tokenizer);

    /// LHS identifier.
    current = tokenizer_current(parser->tokenizer);
    if (!current || !token_is_word_like(current->type)) {
        parser_error_add_with_help(parser, SHELL_ERR_INVALID_FUNCTION,
                                   "form: let NAME = callee(args)",
                                   "expected variable name after 'let'");
        parser_pop_context(parser);
        return NULL;
    }
    char *lhs = strdup(current->text);
    if (!lhs) {
        parser_pop_context(parser);
        return NULL;
    }
    tokenizer_advance(parser->tokenizer);

    /// '='.
    if (!expect_token_with_help(parser, TOK_ASSIGN,
                                "let-fn-call requires '=' between the "
                                "name and the call expression")) {
        free(lhs);
        parser_pop_context(parser);
        return NULL;
    }

    /// Call expression.
    node_t *call = parse_fn_call_expression(parser);
    if (!call) {
        free(lhs);
        parser_pop_context(parser);
        return NULL;
    }

    node_t *let_node = new_node_at(NODE_LET_FN, let_loc);
    if (!let_node) {
        free_node_tree(call);
        free(lhs);
        parser_pop_context(parser);
        return NULL;
    }
    let_node->val.str = lhs;
    let_node->val_type = VAL_STR;
    add_child_node(let_node, call);

    parser_pop_context(parser);
    return let_node;
}

/**
 * @brief Parse `return [expression]` inside a typed-function body.
 *
 * The dispatch path only reaches this handler when
 * `parser->fn_body_depth > 0`. Builds a NODE_FN_RETURN whose
 * first_child is the optional return-value expression (NULL for a
 * void return), captured as a single word / string / variable / call
 * expression. A full expression grammar arrives with the executor
 * pass; this surface accepts what the typed-return runtime will
 * consume.
 */
static node_t *parse_fn_return_statement(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    source_location_t loc =
        token_to_source_location(current, parser->source_name);

    parser_push_context(parser, "parsing typed-function return");

    /// Consume `return`.
    tokenizer_advance(parser->tokenizer);

    node_t *ret_node = new_node_at(NODE_FN_RETURN, loc);
    if (!ret_node) {
        parser_pop_context(parser);
        return NULL;
    }

    /// Optional expression. A bare `return` followed by a statement
    /// terminator is a void return.
    current = tokenizer_current(parser->tokenizer);
    if (current && current->type != TOK_NEWLINE &&
        current->type != TOK_SEMICOLON && current->type != TOK_EOF &&
        current->type != TOK_RBRACE && current->type != TOK_AND) {

        node_t *expr = NULL;
        token_t *next = tokenizer_peek(parser->tokenizer);
        /// A typed-function call as the return expression -- IDENT
        /// immediately followed by '(' (adjacent, no whitespace).
        if (token_is_word_like(current->type) && next &&
            next->type == TOK_LPAREN &&
            next->position == current->end_position) {
            expr = parse_fn_call_expression(parser);
        } else if (token_is_word_like(current->type) ||
                   current->type == TOK_STRING ||
                   current->type == TOK_EXPANDABLE_STRING ||
                   current->type == TOK_VARIABLE) {
            /// Single word / string / variable expression. Map token
            /// type -> AST node type so the executor sees the kind
            /// directly (NODE_VAR for $name, NODE_STRING_LITERAL for
            /// single-quoted, NODE_STRING_EXPANDABLE for double-quoted,
            /// NODE_COMMAND for bare words).
            source_location_t expr_loc =
                token_to_source_location(current, parser->source_name);
            node_type_t expr_type = NODE_COMMAND;
            if (current->type == TOK_VARIABLE) {
                expr_type = NODE_VAR;
            } else if (current->type == TOK_STRING) {
                expr_type = NODE_STRING_LITERAL;
            } else if (current->type == TOK_EXPANDABLE_STRING) {
                expr_type = NODE_STRING_EXPANDABLE;
            }
            expr = new_node_at(expr_type, expr_loc);
            if (expr) {
                expr->val.str = strdup(current->text);
                expr->val_type = VAL_STR;
                if (!expr->val.str) {
                    free_node_tree(expr);
                    expr = NULL;
                }
            }
            if (expr) {
                tokenizer_advance(parser->tokenizer);
            }
        } else {
            parser_error_add_with_help(
                parser, SHELL_ERR_INVALID_FUNCTION,
                "supported forms: return; return WORD; return STRING; "
                "return $VAR; return callee(args)",
                "unexpected token in typed return expression");
            free_node_tree(ret_node);
            parser_pop_context(parser);
            return NULL;
        }

        if (!expr) {
            free_node_tree(ret_node);
            parser_pop_context(parser);
            return NULL;
        }
        add_child_node(ret_node, expr);
    }

    parser_pop_context(parser);
    return ret_node;
}

/**
 * @brief Recognize a statement-position typed-fn call.
 *
 * Both POSIX function definition and typed-fn call begin with
 * `IDENT(`; the discriminator must work even when the recognizer
 * cannot see past end-of-statement (lush feeds the parser one
 * statement at a time, so a `name()` followed by a newline and a
 * `{` body on the next line never has the `{` in the recognizer's
 * window).
 *
 * Discriminator: argument presence between `(` and `)`.
 *   `name()`            -- empty parens -- POSIX function definition
 *   `name(arg, ...)`    -- one or more arguments -- typed-fn call
 *
 * Discarding the return value of a zero-arg typed-fn call at
 * statement position is rare (`let _ = name()` is the explicit
 * form); POSIX `name() { ... }` is common. Disambiguating by
 * argument presence is unambiguous and avoids the look-past-EOS
 * problem.
 *
 * Does not consume tokens: the caller re-walks via
 * parse_fn_call_expression after a true return.
 */
static bool is_typed_fn_call_statement(parser_t *parser) {
    token_t *current = tokenizer_current(parser->tokenizer);
    if (!current || !token_is_word_like(current->type)) {
        return false;
    }
    /// Must be followed by an LPAREN adjacent to the identifier.
    token_t *next = tokenizer_peek(parser->tokenizer);
    if (!next || next->type != TOK_LPAREN) {
        return false;
    }
    if (next->position != current->end_position) {
        /// `name (` with whitespace -- existing POSIX-form path
        /// recognizes it as a function header; keep that behavior.
        return false;
    }

    size_t saved_pos = current->position;
    size_t saved_line = parser->tokenizer->line;
    size_t saved_col = parser->tokenizer->column;

    bool matched = false;
    bool saw_argument = false;

    /// Consume the IDENT.
    tokenizer_advance(parser->tokenizer);
    /// Consume the LPAREN.
    tokenizer_advance(parser->tokenizer);

    /// Walk past everything until the matching RPAREN. Nested parens
    /// (e.g. a nested call as an argument) bump the depth so we don't
    /// exit at the wrong close. Any non-trivial token between `(` and
    /// `)` at depth 1 counts as an argument; zero non-trivial tokens
    /// means empty parens (`name()` -- POSIX function definition).
    int depth = 1;
    while (depth > 0) {
        token_t *t = tokenizer_current(parser->tokenizer);
        if (!t || t->type == TOK_EOF) {
            goto restore;
        }
        if (t->type == TOK_LPAREN) {
            depth++;
            saw_argument = true;
        } else if (t->type == TOK_RPAREN) {
            depth--;
            if (depth == 0) {
                tokenizer_advance(parser->tokenizer);
                break;
            }
        } else if (t->type != TOK_WHITESPACE && t->type != TOK_NEWLINE &&
                   t->type != TOK_COMMENT) {
            saw_argument = true;
        }
        tokenizer_advance(parser->tokenizer);
    }

    if (g_debug_context) {
        debug_trace_printf(g_debug_context,
                           "typed-fn-call recognizer: saw_argument=%d\n",
                           saw_argument ? 1 : 0);
    }
    matched = saw_argument;

restore:
    parser->tokenizer->position = saved_pos;
    parser->tokenizer->line = saved_line;
    parser->tokenizer->column = saved_col;
    tokenizer_refresh_from_position(parser->tokenizer);
    return matched;
}
