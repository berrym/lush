/**
 * @file tokenizer.h
 * @brief POSIX shell tokenizer for recursive descent parsing
 *
 * Provides clean token classification with lookahead support and proper
 * token boundary handling for the recursive descent parser.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

/// Simple, clean token classification for parser
typedef enum {
    /// Basic token types
    TOK_EOF,    ///< End of input
    TOK_WORD,   ///< Regular word (command, argument, variable name)
    TOK_STRING, ///< Quoted string ('...' - literal)
    /**
     * Double-quoted string ("...") - needs variable expansion
     */
    TOK_EXPANDABLE_STRING,
    TOK_NUMBER,   ///< Numeric literal
    TOK_VARIABLE, ///< Variable reference ($var, ${var}, etc.)

    /// Operators and separators
    TOK_SEMICOLON,         ///< ;
    TOK_PIPE,              ///< |
    TOK_AND,               ///< &
    TOK_BACKGROUND_DISOWN, ///< `&|` and `&!` -- zsh background-and-disown.
                           ///< Lush treats the disown bookkeeping as an
                           ///< optimization; observable script semantics
                           ///< match plain `&`.
    TOK_LOGICAL_AND,       ///< &&
    TOK_LOGICAL_OR,        ///< ||
    TOK_REDIRECT_IN,       ///< <
    TOK_REDIRECT_OUT,      ///< >
    TOK_APPEND,            ///< >>
    TOK_HEREDOC,           ///< <<
    TOK_HEREDOC_STRIP,     ///< <<-
    TOK_HERESTRING,        ///< <<<
    TOK_REDIRECT_ERR,      ///< N> (any fd output, e.g., 2>, 3>)
    TOK_REDIRECT_IN_FD,    ///< N< (any fd input, e.g., 3<, 4<)
    TOK_REDIRECT_BOTH,     ///< &>
    TOK_APPEND_ERR,        ///< 2>>
    TOK_REDIRECT_FD,       ///< &1, &2, etc.
    TOK_REDIRECT_FD_ALLOC, ///< {varname}> - fd allocation (bash 4.1+/zsh)
    TOK_REDIRECT_CLOBBER,  ///< >|
    TOK_ASSIGN,            ///< =
    /**
     * , (parameter separator for lush function parameter lists; an
     * adjacency token elsewhere -- collect_word_argument and the
     * assignment-value collector accept it so unquoted noatime,noexec
     * still concatenates into a single shell word)
     */
    TOK_COMMA,
    TOK_NOT_EQUAL,   ///< !=
    TOK_ARROW,       ///< -> (typed-function return-kind annotation)
    TOK_PLUS,        ///< +
    TOK_MINUS,       ///< -
    TOK_MULTIPLY,    ///< *
    TOK_DIVIDE,      ///< /
    TOK_MODULO,      ///< %
    TOK_GLOB,        ///< * (when used for globbing)
    TOK_QUESTION,    ///< ?
    TOK_COMMAND_SUB, ///< $(...)
    TOK_ARITH_EXP,   ///< $((...))
    TOK_BACKQUOTE,   ///< `

    /// Delimiters
    TOK_LPAREN,          ///< (
    TOK_RPAREN,          ///< )
    TOK_DOUBLE_LPAREN,   ///< (( - arithmetic command start
    TOK_DOUBLE_RPAREN,   ///< )) - arithmetic command end
    TOK_LBRACE,          ///< {
    TOK_RBRACE,          ///< }
    TOK_LBRACKET,        ///< [
    TOK_RBRACKET,        ///< ]
    TOK_DOUBLE_LBRACKET, ///< [[ - extended test start
    TOK_DOUBLE_RBRACKET, ///< ]] - extended test end

    /// Extended operators (Phase 1-2)
    TOK_PLUS_ASSIGN, ///< += - append to array or add to integer
    TOK_REGEX_MATCH, ///< =~ - regex match operator in [[ ]]

    /// Process substitution and extended pipes (Phase 3)
    TOK_PROC_SUB_IN,  ///< <( - process substitution input
    TOK_PROC_SUB_OUT, ///< >( - process substitution output
    TOK_PIPE_STDERR,  ///< |& - pipe both stdout and stderr
    TOK_APPEND_BOTH,  ///< &>> - append both stdout and stderr

    /// Control flow extensions (Phase 5)
    TOK_CASE_FALLTHROUGH, ///< ;& - case fall-through (execute next without
                          ///< test)
    TOK_CASE_CONTINUE,    ///< ;;& - case continue (test next pattern)

    /// Keywords (recognized contextually)
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_ELIF,
    TOK_FI,
    TOK_WHILE,
    TOK_DO,
    TOK_DONE,
    TOK_FOR,
    TOK_IN,
    TOK_CASE,
    TOK_ESAC,
    TOK_UNTIL,
    TOK_FUNCTION,
    TOK_SELECT, ///< select keyword for select loop
    TOK_TIME,   ///< time keyword for timing pipelines
    TOK_COPROC, ///< coproc keyword for coprocesses
    TOK_REPEAT, ///< zsh repeat keyword (#103)
    TOK_FN,     ///< Typed-function declaration keyword (`fn ...`)

    /// Special
    TOK_NEWLINE,    ///< \n (significant in shell)
    TOK_WHITESPACE, ///< Spaces, tabs (usually ignored)
    TOK_COMMENT,    ///< # comment
    TOK_ERROR       ///< Invalid token
} token_type_t;

/// Token structure for parser
typedef struct token {
    token_type_t type;
    /**
     * Token text (null-terminated, may differ from raw input: quoted
     * strings strip surrounding quotes, expansions store inner
     * expression).
     */
    char *text;
    /**
     * strlen(text) -- byte count of the stored textual content (NOT
     * the consumed input span; use end_position for that).
     */
    size_t length;
    size_t line;     ///< Line number (1-based)
    size_t column;   ///< Column number (1-based)
    size_t position; ///< Byte offset where this token starts in input.
    /**
     * Byte offset immediately AFTER the token's consumed input span.
     * For unquoted tokens this equals position + length; for quoted
     * strings, expansions, and command substitutions it includes the
     * consumed delimiters/escapes. The shell-word adjacency rule
     * (POSIX 2.10.2 ASSIGNMENT_WORD and word concatenation) uses this
     * field: two tokens are part of the same shell word iff
     * token2.position == token1.end_position.
     */
    size_t end_position;
    struct token *next; ///< For token stream
} token_t;

/// Tokenizer state for parser
typedef struct tokenizer {
    const char *input;    ///< Input string
    size_t input_length;  ///< Input length
    size_t position;      ///< Current position
    size_t line;          ///< Current line (1-based)
    size_t column;        ///< Current column (1-based)
    token_t *current;     ///< Current token
    token_t *lookahead;   ///< Next token (for lookahead)
    bool enable_keywords; ///< Whether to recognize keywords (context-sensitive)
    int arith_cmd_depth;  ///< Nesting depth of (( )) arithmetic commands
} tokenizer_t;

/* ============================================================================
 * Tokenizer Lifecycle
 * ============================================================================
 */

/**
 * @brief Create a new tokenizer for input string
 *
 * Equivalent to tokenizer_new_at(input, 1). Use tokenizer_new_at when
 * the input is a slice from a larger source file and the first character
 * of `input` corresponds to a line other than 1, so that error reporting
 * and AST source-location tracking carry the correct file line.
 *
 * @param input Shell command string to tokenize
 * @return New tokenizer instance or NULL on failure
 */
tokenizer_t *tokenizer_new(const char *input);

/**
 * @brief Create a new tokenizer with an explicit starting line number
 *
 * The starting_line value seeds tokenizer->line so that tokens produced
 * for `input` carry source line numbers relative to the original source
 * file rather than the per-statement slice the shell main loop hands to
 * the parser. The tokenizer still increments the line counter on each
 * newline within `input` as normal.
 *
 * @param input         Shell command string to tokenize
 * @param starting_line Line number of the first character of `input`
 *                      within the source file (1-based; pass 1 if input
 *                      is the entire source or origin is unknown)
 * @return New tokenizer instance or NULL on failure
 */
tokenizer_t *tokenizer_new_at(const char *input, size_t starting_line);

/**
 * @brief Free a tokenizer and associated resources
 *
 * @param tokenizer Tokenizer to free
 */
void tokenizer_free(tokenizer_t *tokenizer);

/* ============================================================================
 * Token Operations
 * ============================================================================
 */

/**
 * @brief Get the current token
 *
 * @param tokenizer Tokenizer context
 * @return Current token or NULL if at end
 */
token_t *tokenizer_current(tokenizer_t *tokenizer);

/**
 * @brief Peek at the next token without consuming
 *
 * @param tokenizer Tokenizer context
 * @return Next token (lookahead) or NULL
 */
token_t *tokenizer_peek(tokenizer_t *tokenizer);

/**
 * @brief Advance to the next token
 *
 * @param tokenizer Tokenizer context
 */
void tokenizer_advance(tokenizer_t *tokenizer);

/**
 * @brief Check if current token matches type
 *
 * @param tokenizer Tokenizer context
 * @param type Token type to match
 * @return True if current token matches type
 */
bool tokenizer_match(tokenizer_t *tokenizer, token_type_t type);

/**
 * @brief Match and consume a token of specified type
 *
 * @param tokenizer Tokenizer context
 * @param type Token type to consume
 * @return True if token was consumed, false otherwise
 */
bool tokenizer_consume(tokenizer_t *tokenizer, token_type_t type);

/* ============================================================================
 * Token Utilities
 * ============================================================================
 */

/**
 * @brief Get human-readable name for token type
 *
 * @param type Token type
 * @return String name of token type
 */
const char *token_type_name(token_type_t type);

/**
 * @brief Check if token type is a keyword
 *
 * @param type Token type to check
 * @return True if type is a keyword token
 */
bool token_is_keyword(token_type_t type);

/**
 * @brief Check if token type is an operator
 *
 * @param type Token type to check
 * @return True if type is an operator token
 */
bool token_is_operator(token_type_t type);

/**
 * @brief Check if token type is word-like
 *
 * @param type Token type to check
 * @return True if type represents a word or string
 */
bool token_is_word_like(token_type_t type);

/* ============================================================================
 * Tokenizer Control
 * ============================================================================
 */

/**
 * @brief Enable or disable keyword recognition
 *
 * @param tokenizer Tokenizer context
 * @param enable True to recognize keywords, false for all words
 */
void tokenizer_enable_keywords(tokenizer_t *tokenizer, bool enable);

/**
 * @brief Refresh the lookahead token with current settings
 *
 * Re-tokenizes the lookahead token using current tokenizer settings.
 * Use when keyword recognition context changes before advancing.
 *
 * @param tokenizer Tokenizer context
 */
void tokenizer_refresh_lookahead(tokenizer_t *tokenizer);

/**
 * @brief Refresh both current and lookahead tokens
 *
 * Re-tokenizes current and lookahead together using current tokenizer
 * settings. Use after a setting flip (e.g., re-enabling keywords) when
 * both already-buffered tokens may be misclassified.
 *
 * @param tokenizer Tokenizer context
 */
void tokenizer_refresh_current_and_lookahead(tokenizer_t *tokenizer);

/**
 * @brief Refresh tokenizer state from current position
 *
 * @param tokenizer Tokenizer context
 */
void tokenizer_refresh_from_position(tokenizer_t *tokenizer);

#endif /// TOKENIZER_H
