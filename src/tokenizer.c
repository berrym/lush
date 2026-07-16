/**
 * @file tokenizer.c
 * @brief Shell Command Tokenizer Implementation
 *
 * Lexical analyzer for POSIX shell syntax. Handles tokenization of
 * shell commands including operators, keywords, quoting, and escapes.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "tokenizer.h"
#include "brace_match.h"
#include "identifier.h"
#include "lle/utf8_support.h"
#include "shell_mode.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool lush_dollar_paren_is_arithmetic(const char *content, size_t remaining) {
    if (!content) {
        return true;
    }
    size_t s = 0;
    int depth = 2; /// the two outer `(` of `$((`
    while (s < remaining && depth > 0) {
        char sc = content[s];
        if (sc == '$' && s + 1 < remaining && content[s + 1] == '{') {
            /// `${...}` parameter expansion is legal inside arithmetic
            /// (`$((${a}+1))`). Its braces are not the anonymous-function
            /// body braces this guard rejects, so skip the whole span.
            /// Brace-balanced to tolerate nested `${...}` forms.
            s += 2; /// past "${"
            int bdepth = 1;
            while (s < remaining && bdepth > 0) {
                if (content[s] == '{') {
                    bdepth++;
                } else if (content[s] == '}') {
                    bdepth--;
                }
                s++;
            }
            continue;
        }
        if (sc == '(') {
            depth++;
        } else if (sc == ')') {
            depth--;
            if (depth == 0) {
                return true; /// matched `))` -- arithmetic
            }
        } else if (sc == '{' || sc == '}' || sc == ';' || sc == '\n') {
            return false; /// bare brace / separator -- command substitution
        }
        s++;
    }
    /// Ran out of input without a disqualifier: treat as arithmetic; the
    /// caller's extraction handles an unterminated `$((`.
    return true;
}

/// Keyword lookup table
static const struct {
    const char *text;
    token_type_t type;
} keywords[] = {
    {      "if",       TOK_IF},
    {    "then",     TOK_THEN},
    {    "else",     TOK_ELSE},
    {    "elif",     TOK_ELIF},
    {      "fi",       TOK_FI},
    {   "while",    TOK_WHILE},
    {      "do",       TOK_DO},
    {    "done",     TOK_DONE},
    {     "for",      TOK_FOR},
    {      "in",       TOK_IN},
    {    "case",     TOK_CASE},
    {    "esac",     TOK_ESAC},
    {   "until",    TOK_UNTIL},
    {"function", TOK_FUNCTION},
    {  "select",   TOK_SELECT},
    {    "time",     TOK_TIME},
    {  "coproc",   TOK_COPROC},
    {  "repeat",   TOK_REPEAT},
    {      "fn",       TOK_FN},
    {      NULL,     TOK_WORD}  /// Sentinel
};

/// Helper functions
static token_t *token_new(token_type_t type, const char *text, size_t length,
                          size_t line, size_t column, size_t position);
static void token_free(token_t *token);
static token_t *tokenize_next(tokenizer_t *tokenizer);
static token_t *tokenize_next_inner(tokenizer_t *tokenizer);
static token_type_t classify_word(const char *text, size_t length,
                                  bool enable_keywords);
static bool is_operator_char(char c);
static bool is_word_char(char c);
static void skip_whitespace(tokenizer_t *tokenizer);

/**
 * @brief Create a new tokenizer instance
 *
 * Initializes a tokenizer for the given input string and
 * pre-tokenizes the first two tokens (current and lookahead).
 *
 * @param input Shell command string to tokenize
 * @return New tokenizer instance, or NULL on failure
 */
tokenizer_t *tokenizer_new(const char *input) {
    return tokenizer_new_at(input, 1);
}

tokenizer_t *tokenizer_new_at(const char *input, size_t starting_line) {
    if (!input) {
        return NULL;
    }
    if (starting_line == 0) {
        starting_line = 1; /// defensive: 0 is unknown; 1-based per source
    }

    tokenizer_t *tokenizer = malloc(sizeof(tokenizer_t));
    if (!tokenizer) {
        return NULL;
    }

    tokenizer->input = input;
    tokenizer->input_length = strlen(input);
    tokenizer->position = 0;
    tokenizer->line = starting_line;
    tokenizer->column = 1;
    tokenizer->current = NULL;
    tokenizer->lookahead = NULL;
    tokenizer->enable_keywords = true;
    tokenizer->arith_cmd_depth = 0;

    /// Initialize by getting the first two tokens. The line number must
    /// be set BEFORE tokenize_next, since lookahead pre-tokenizes too.
    tokenizer->current = tokenize_next(tokenizer);
    tokenizer->lookahead = tokenize_next(tokenizer);

    return tokenizer;
}

/**
 * @brief Free a tokenizer instance
 *
 * Frees the tokenizer and any associated tokens.
 *
 * @param tokenizer Tokenizer to free
 */
void tokenizer_free(tokenizer_t *tokenizer) {
    if (!tokenizer) {
        return;
    }

    if (tokenizer->current) {
        token_free(tokenizer->current);
    }
    if (tokenizer->lookahead) {
        token_free(tokenizer->lookahead);
    }

    free(tokenizer);
}

/**
 * @brief Get the current token
 *
 * @param tokenizer Tokenizer instance
 * @return Current token, or NULL if tokenizer is NULL
 */
token_t *tokenizer_current(tokenizer_t *tokenizer) {
    return tokenizer ? tokenizer->current : NULL;
}

/**
 * @brief Peek at the next token without consuming
 *
 * @param tokenizer Tokenizer instance
 * @return Lookahead token, or NULL if tokenizer is NULL
 */
token_t *tokenizer_peek(tokenizer_t *tokenizer) {
    return tokenizer ? tokenizer->lookahead : NULL;
}

/**
 * @brief Advance to the next token
 *
 * Frees the current token, moves lookahead to current,
 * and tokenizes the next lookahead.
 *
 * @param tokenizer Tokenizer instance
 */
void tokenizer_advance(tokenizer_t *tokenizer) {
    if (!tokenizer) {
        return;
    }

    /// Free the current token
    if (tokenizer->current) {
        token_free(tokenizer->current);
    }

    /// Move lookahead to current
    tokenizer->current = tokenizer->lookahead;

    /// Get new lookahead
    tokenizer->lookahead = tokenize_next(tokenizer);
}

/**
 * @brief Check if current token matches expected type
 *
 * @param tokenizer Tokenizer instance
 * @param type Token type to match
 * @return true if current token matches type
 */
bool tokenizer_match(tokenizer_t *tokenizer, token_type_t type) {
    if (!tokenizer || !tokenizer->current) {
        return false;
    }
    return tokenizer->current->type == type;
}

/**
 * @brief Consume a token if it matches expected type
 *
 * Advances past the token if it matches, otherwise leaves
 * the tokenizer state unchanged.
 *
 * @param tokenizer Tokenizer instance
 * @param type Token type to match and consume
 * @return true if token was matched and consumed
 */
bool tokenizer_consume(tokenizer_t *tokenizer, token_type_t type) {
    if (tokenizer_match(tokenizer, type)) {
        tokenizer_advance(tokenizer);
        return true;
    }
    return false;
}

/**
 * @brief Get human-readable name for token type
 *
 * Returns a string representation of the token type
 * for debugging and error messages.
 *
 * @param type Token type
 * @return String name of token type
 */
const char *token_type_name(token_type_t type) {
    switch (type) {
    case TOK_EOF:
        return "EOF";
    case TOK_WORD:
        return "WORD";
    case TOK_STRING:
        return "STRING";
    case TOK_EXPANDABLE_STRING:
        return "EXPANDABLE_STRING";
    case TOK_NUMBER:
        return "NUMBER";
    case TOK_VARIABLE:
        return "VARIABLE";
    case TOK_SEMICOLON:
        return "SEMICOLON";
    case TOK_PIPE:
        return "PIPE";
    case TOK_AND:
        return "AND";
    case TOK_BACKGROUND_DISOWN:
        return "BACKGROUND_DISOWN";
    case TOK_LOGICAL_AND:
        return "LOGICAL_AND";
    case TOK_LOGICAL_OR:
        return "LOGICAL_OR";
    case TOK_REDIRECT_IN:
        return "REDIRECT_IN";
    case TOK_REDIRECT_OUT:
        return "REDIRECT_OUT";
    case TOK_APPEND:
        return "APPEND";
    case TOK_HEREDOC:
        return "HEREDOC";
    case TOK_HEREDOC_STRIP:
        return "HEREDOC_STRIP";
    case TOK_HERESTRING:
        return "HERESTRING";
    case TOK_REDIRECT_ERR:
        return "REDIRECT_ERR";
    case TOK_REDIRECT_IN_FD:
        return "REDIRECT_IN_FD";
    case TOK_REDIRECT_BOTH:
        return "REDIRECT_BOTH";
    case TOK_APPEND_ERR:
        return "APPEND_ERR";
    case TOK_REDIRECT_FD:
        return "REDIRECT_FD";
    case TOK_REDIRECT_FD_ALLOC:
        return "REDIRECT_FD_ALLOC";
    case TOK_REDIRECT_CLOBBER:
        return "REDIRECT_CLOBBER";
    case TOK_ASSIGN:
        return "ASSIGN";
    case TOK_COMMA:
        return "COMMA";
    case TOK_NOT_EQUAL:
        return "NOT_EQUAL";
    case TOK_PLUS:
        return "PLUS";
    case TOK_MINUS:
        return "MINUS";
    case TOK_MULTIPLY:
        return "MULTIPLY";
    case TOK_DIVIDE:
        return "DIVIDE";
    case TOK_MODULO:
        return "MODULO";
    case TOK_GLOB:
        return "GLOB";
    case TOK_QUESTION:
        return "QUESTION";
    case TOK_COMMAND_SUB:
        return "COMMAND_SUB";
    case TOK_ARITH_EXP:
        return "ARITH_EXP";
    case TOK_BACKQUOTE:
        return "BACKQUOTE";
    case TOK_LPAREN:
        return "LPAREN";
    case TOK_RPAREN:
        return "RPAREN";
    case TOK_DOUBLE_LPAREN:
        return "DOUBLE_LPAREN";
    case TOK_DOUBLE_RPAREN:
        return "DOUBLE_RPAREN";
    case TOK_LBRACE:
        return "LBRACE";
    case TOK_RBRACE:
        return "RBRACE";
    case TOK_LBRACKET:
        return "LBRACKET";
    case TOK_RBRACKET:
        return "RBRACKET";
    case TOK_DOUBLE_LBRACKET:
        return "DOUBLE_LBRACKET";
    case TOK_DOUBLE_RBRACKET:
        return "DOUBLE_RBRACKET";
    case TOK_PLUS_ASSIGN:
        return "PLUS_ASSIGN";
    case TOK_REGEX_MATCH:
        return "REGEX_MATCH";
    case TOK_PROC_SUB_IN:
        return "PROC_SUB_IN";
    case TOK_PROC_SUB_OUT:
        return "PROC_SUB_OUT";
    case TOK_PIPE_STDERR:
        return "PIPE_STDERR";
    case TOK_APPEND_BOTH:
        return "APPEND_BOTH";
    case TOK_COPROC:
        return "COPROC";
    case TOK_CASE_FALLTHROUGH:
        return "CASE_FALLTHROUGH";
    case TOK_CASE_CONTINUE:
        return "CASE_CONTINUE";
    case TOK_SELECT:
        return "SELECT";
    case TOK_TIME:
        return "TIME";
    case TOK_REPEAT:
        return "REPEAT";
    case TOK_IF:
        return "IF";
    case TOK_THEN:
        return "THEN";
    case TOK_ELSE:
        return "ELSE";
    case TOK_ELIF:
        return "ELIF";
    case TOK_FI:
        return "FI";
    case TOK_WHILE:
        return "WHILE";
    case TOK_DO:
        return "DO";
    case TOK_DONE:
        return "DONE";
    case TOK_FOR:
        return "FOR";
    case TOK_IN:
        return "IN";
    case TOK_CASE:
        return "CASE";
    case TOK_ESAC:
        return "ESAC";
    case TOK_UNTIL:
        return "UNTIL";
    case TOK_FUNCTION:
        return "FUNCTION";
    case TOK_FN:
        return "FN";
    case TOK_ARROW:
        return "ARROW";
    case TOK_NEWLINE:
        return "NEWLINE";
    case TOK_WHITESPACE:
        return "WHITESPACE";
    case TOK_COMMENT:
        return "COMMENT";
    case TOK_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Check if token type is a shell keyword
 *
 * Keywords include: if, then, else, elif, fi, while, do, done,
 * for, in, case, esac, until, function, fn, select, time, coproc,
 * repeat.
 *
 * @param type Token type to check
 * @return true if token is a keyword
 */
bool token_is_keyword(token_type_t type) {
    return type >= TOK_IF && type <= TOK_FN;
}

/**
 * @brief Check if token type is an operator
 *
 * Operators include: ;, |, &, &&, ||, <, >, >>, <<, =, etc.
 *
 * @param type Token type to check
 * @return true if token is an operator
 */
bool token_is_operator(token_type_t type) {
    return (type >= TOK_SEMICOLON && type <= TOK_REDIRECT_CLOBBER) ||
           (type >= TOK_ASSIGN && type <= TOK_BACKQUOTE);
}

/**
 * @brief Check if token can be part of a command/argument
 *
 * Word-like tokens include: WORD, STRING, EXPANDABLE_STRING,
 * NUMBER, and VARIABLE.
 *
 * @param type Token type to check
 * @return true if token is word-like
 */
bool token_is_word_like(token_type_t type) {
    return type == TOK_WORD || type == TOK_STRING ||
           type == TOK_EXPANDABLE_STRING || type == TOK_NUMBER ||
           type == TOK_VARIABLE;
}

bool token_is_argument_word_token(token_type_t type) {
    return type == TOK_STRING || type == TOK_EXPANDABLE_STRING ||
           type == TOK_ARITH_EXP || type == TOK_COMMAND_SUB ||
           type == TOK_BACKQUOTE || token_is_word_like(type) ||
           token_is_keyword(type) || type == TOK_VARIABLE ||
           type == TOK_LBRACKET || type == TOK_RBRACKET || type == TOK_ASSIGN ||
           type == TOK_COMMA || type == TOK_GLOB || type == TOK_QUESTION ||
           type == TOK_NOT_EQUAL;
}

bool token_is_assignment_value_token(token_type_t type) {
    return token_is_word_like(type) || type == TOK_VARIABLE ||
           type == TOK_ARITH_EXP || type == TOK_COMMAND_SUB ||
           type == TOK_BACKQUOTE || type == TOK_ASSIGN ||
           type == TOK_PLUS_ASSIGN || type == TOK_COMMA ||
           type == TOK_LBRACKET || type == TOK_RBRACKET;
}

bool token_is_word_list_token(token_type_t type) {
    return token_is_word_like(type) || type == TOK_VARIABLE ||
           type == TOK_COMMAND_SUB || type == TOK_ARITH_EXP ||
           type == TOK_BACKQUOTE || type == TOK_LBRACKET ||
           type == TOK_RBRACKET;
}

/**
 * @brief Enable or disable keyword recognition
 *
 * When disabled, keywords like 'if', 'while' are treated as
 * regular words. Useful in certain parsing contexts.
 *
 * @param tokenizer Tokenizer instance
 * @param enable true to enable keyword recognition
 */
void tokenizer_enable_keywords(tokenizer_t *tokenizer, bool enable) {
    if (tokenizer) {
        tokenizer->enable_keywords = enable;
    }
}

/**
 * @brief Refresh the lookahead token with current settings
 *
 * Re-tokenizes the lookahead token using current tokenizer settings
 * (e.g., enable_keywords). Use this when keyword recognition context
 * changes and the lookahead needs to be re-classified.
 *
 * @param tokenizer Tokenizer instance
 */
void tokenizer_refresh_lookahead(tokenizer_t *tokenizer) {
    if (!tokenizer || !tokenizer->lookahead) {
        return;
    }

    /// Save the position before the lookahead token
    size_t saved_position = tokenizer->lookahead->position;
    size_t saved_line = tokenizer->lookahead->line;
    size_t saved_column = tokenizer->lookahead->column;

    /// Free the lookahead
    token_free(tokenizer->lookahead);

    /// Restore position to where lookahead started
    tokenizer->position = saved_position;
    tokenizer->line = saved_line;
    tokenizer->column = saved_column;

    /// Re-tokenize with current settings
    tokenizer->lookahead = tokenize_next(tokenizer);
}

/**
 * @brief Refresh both current and lookahead tokens with current settings
 *
 * Re-tokenizes current and lookahead together. Use this after a tokenizer
 * setting change (e.g., `enable_keywords` flipping back on) when *both*
 * already-buffered tokens may be misclassified. Refreshing only the
 * lookahead leaves the current token stale, which can cause keywords
 * like `fi`/`done` to be returned as TOK_WORD to the parser.
 */
void tokenizer_refresh_current_and_lookahead(tokenizer_t *tokenizer) {
    if (!tokenizer || !tokenizer->current) {
        return;
    }

    size_t saved_position = tokenizer->current->position;
    size_t saved_line = tokenizer->current->line;
    size_t saved_column = tokenizer->current->column;

    token_free(tokenizer->current);
    tokenizer->current = NULL;
    if (tokenizer->lookahead) {
        token_free(tokenizer->lookahead);
        tokenizer->lookahead = NULL;
    }

    tokenizer->position = saved_position;
    tokenizer->line = saved_line;
    tokenizer->column = saved_column;

    tokenizer->current = tokenize_next(tokenizer);
    tokenizer->lookahead = tokenize_next(tokenizer);
}

/**
 * @brief Refresh tokenizer from current position
 *
 * Discards current and lookahead tokens and re-tokenizes
 * from the current input position. Used for error recovery
 * and after manually advancing the position (e.g., here-documents).
 *
 * @param tokenizer Tokenizer instance
 */
void tokenizer_refresh_from_position(tokenizer_t *tokenizer) {
    if (!tokenizer) {
        return;
    }

    /// Free existing tokens
    if (tokenizer->current) {
        token_free(tokenizer->current);
        tokenizer->current = NULL;
    }
    if (tokenizer->lookahead) {
        token_free(tokenizer->lookahead);
        tokenizer->lookahead = NULL;
    }

    /// Re-tokenize from current position
    tokenizer->current = tokenize_next(tokenizer);
    tokenizer->lookahead = tokenize_next(tokenizer);
}

/* ========== Helper Functions ========== */

/**
 * @brief Create a new token
 *
 * Allocates and initializes a token with the given properties.
 *
 * @param type Token type
 * @param text Token text (will be copied)
 * @param length Length of text
 * @param line Source line number
 * @param column Source column number
 * @param position Byte offset in input
 * @return New token, or NULL on failure
 */
static token_t *token_new(token_type_t type, const char *text, size_t length,
                          size_t line, size_t column, size_t position) {
    token_t *token = malloc(sizeof(token_t));
    if (!token) {
        return NULL;
    }

    token->type = type;
    token->length = length;
    token->line = line;
    token->column = column;
    token->position = position;
    /// end_position is provisional here; tokenize_next() stamps the
    /// authoritative post-token input position over this. The default is
    /// the conservative position + length so consumers that bypass the
    /// wrapper still get a usable value for unquoted tokens.
    token->end_position = position + length;
    token->next = NULL;
    token->glob_qualified = false;

    if (text && length > 0) {
        token->text = malloc(length + 1);
        if (!token->text) {
            free(token);
            return NULL;
        }
        memcpy(token->text, text, length);
        token->text[length] = '\0';
    } else {
        /// Always allocate text, even for empty strings
        token->text = malloc(1);
        if (!token->text) {
            free(token);
            return NULL;
        }
        token->text[0] = '\0';
    }

    return token;
}

/**
 * @brief Free a token
 *
 * @param token Token to free
 */
static void token_free(token_t *token) {
    if (!token) {
        return;
    }

    if (token->text) {
        free(token->text);
    }
    free(token);
}

/**
 * @brief Classify a word as keyword or regular word
 *
 * Checks the word against the keyword lookup table.
 *
 * @param text Word text
 * @param length Word length
 * @param enable_keywords If false, always returns TOK_WORD
 * @return Token type (keyword type or TOK_WORD)
 */
static token_type_t classify_word(const char *text, size_t length,
                                  bool enable_keywords) {
    if (!enable_keywords || !text || length == 0) {
        return TOK_WORD;
    }

    /// Check against keyword table
    for (size_t i = 0; keywords[i].text; i++) {
        if (strlen(keywords[i].text) == length &&
            strncmp(keywords[i].text, text, length) == 0) {
            return keywords[i].type;
        }
    }

    return TOK_WORD;
}

/**
 * @brief Check if character can start/be part of an operator
 *
 * @param c Character to check
 * @return true if character is an operator character
 */
static bool is_operator_char(char c) {
    return strchr(";|&<>=+*%?(){}[]#!,", c) != NULL;
}

/**
 * @brief Check if ASCII character can be part of a word
 *
 * Word characters include alphanumerics and: _.-/~:@*?[]+%
 *
 * @param c Character to check
 * @return true if character can be part of a word
 */
static bool is_word_char(char c) {
    /// ',' is intentionally NOT a word char: it is emitted as TOK_COMMA
    /// (parser-level adjacency reassembles `noatime,noexec` into a single
    /// shell word via collect_word_argument). Tokenizer-level brace
    /// expansion scans `,` directly from the input buffer (not via this
    /// helper), so `{a,b,c}` continues to tokenize as one TOK_WORD.
    return isalnum(c) || strchr("_.-/~:@*?[]+%!^#", c) != NULL;
}

/**
 * @brief Check if Unicode codepoint can be part of a word
 *
 * For ASCII (< 0x80), uses traditional shell word character logic.
 * For non-ASCII, all Unicode codepoints are valid word characters,
 * allowing filenames with international characters.
 *
 * @param codepoint Unicode codepoint
 * @return true if codepoint can be part of a word
 */
static bool is_word_codepoint(uint32_t codepoint) {
    /// ASCII range: Use traditional shell word character logic.
    /// ',' is excluded -- see is_word_char() for rationale.
    if (codepoint < 0x80) {
        char c = (char)codepoint;
        return isalnum(c) || strchr("_.-/~:@*?[]+%!^#", c) != NULL;
    }

    /// Non-ASCII UTF-8: All non-ASCII codepoints are valid word characters
    /// This includes:
    /// - Latin Extended (accented characters like é, ñ, ü)
    /// - CJK (Chinese, Japanese, Korean)
    /// - Emoji
    /// - All other Unicode scripts
    ///
    /// We explicitly exclude:
    /// - ASCII control characters (0x00-0x1F, already < 0x80)
    /// - Invalid codepoints (handled by UTF-8 decoder)
    ///
    /// Shell metacharacters (quotes, pipes, etc.) are all ASCII (< 0x80),
    /// so they're handled by the ASCII logic above.
    return true;
}

/**
 * @brief Skip whitespace characters
 *
 * Advances past spaces and tabs, but not newlines
 * (which are significant in shell syntax).
 *
 * @param tokenizer Tokenizer instance
 */
static void skip_whitespace(tokenizer_t *tokenizer) {
    while (tokenizer->position < tokenizer->input_length) {
        char c = tokenizer->input[tokenizer->position];
        if (c == ' ' || c == '\t') {
            tokenizer->position++;
            tokenizer->column++;
        } else {
            break;
        }
    }
}

/**
 * @brief Tokenize the next token from input
 *
 * Main tokenization function that handles:
 * - Newlines and comments
 * - Single and double quoted strings
 * - Variable references ($var, ${var}, $(cmd), $((expr)))
 * - Backtick command substitution
 * - All operators and redirections
 * - Keywords and words (UTF-8 aware)
 * - Numbered file descriptor redirections
 *
 * @param tokenizer Tokenizer instance
 * @return Next token, or TOK_EOF at end of input
 */
/* Public wrapper: drives the giant token-recognition switch via
 * tokenize_next_inner() and then stamps the authoritative end_position
 * (== tokenizer->position immediately after consumption) on the result.
 * Every adjacency check in the parser reads this field; centralising
 * the stamping here means none of the 80+ token_new() call sites have
 * to know about it. POSIX ASSIGNMENT_WORD and word concatenation rely
 * on this being correct for quoted strings, command substitutions,
 * expansions, and other tokens whose stored text length differs from
 * the consumed input span. */
static token_t *tokenize_next(tokenizer_t *tokenizer) {
    token_t *tok = tokenize_next_inner(tokenizer);
    if (tok && tokenizer) {
        tok->end_position = tokenizer->position;
    }
    return tok;
}

static token_t *tokenize_next_inner(tokenizer_t *tokenizer) {
    if (!tokenizer || tokenizer->position >= tokenizer->input_length) {
        return token_new(TOK_EOF, NULL, 0, tokenizer ? tokenizer->line : 1,
                         tokenizer ? tokenizer->column : 1,
                         tokenizer ? tokenizer->position : 0);
    }

    skip_whitespace(tokenizer);

    if (tokenizer->position >= tokenizer->input_length) {
        return token_new(TOK_EOF, NULL, 0, tokenizer->line, tokenizer->column,
                         tokenizer->position);
    }

    size_t start_pos = tokenizer->position;
    size_t start_line = tokenizer->line;
    size_t start_column = tokenizer->column;
    char c = tokenizer->input[tokenizer->position];

    /// Handle newlines
    if (c == '\n') {
        tokenizer->position++;
        tokenizer->line++;
        tokenizer->column = 1;
        return token_new(TOK_NEWLINE, "\n", 1, start_line, start_column,
                         start_pos);
    }

    /// Handle comments
    if (c == '#') {
        size_t start = tokenizer->position;
        while (tokenizer->position < tokenizer->input_length &&
               tokenizer->input[tokenizer->position] != '\n') {
            tokenizer->position++;
            tokenizer->column++;
        }
        size_t length = tokenizer->position - start;
        return token_new(TOK_COMMENT, &tokenizer->input[start], length,
                         start_line, start_column, start_pos);
    }

    /// Handle quoted strings (with adjacent quote concatenation support)
    if (c == '\'' || c == '"') {
        /// Build a combined token for adjacent quoted segments
        /// We need to extract content from each segment and combine them
        size_t result_capacity = 256;
        char *result = malloc(result_capacity);
        if (!result) {
            return token_new(TOK_ERROR, &tokenizer->input[start_pos], 1,
                             start_line, start_column, start_pos);
        }
        size_t result_len = 0;
        bool has_expandable = false;
        bool glob_qualified = false;

    parse_next_segment:;
        char quote_char = tokenizer->input[tokenizer->position];
        tokenizer->position++; /// Skip opening quote
        tokenizer->column++;

        /// For double-quoted strings, we need to build the result character by
        /// character to properly handle backslash-newline line continuation
        /// (which should be removed entirely per POSIX).
        /// For single-quoted strings, we can still do bulk copy since no
        /// escape processing happens inside single quotes.

        if (quote_char == '\'') {
            /// Single-quoted string: bulk copy (no escape processing)
            size_t segment_start = tokenizer->position;
            while (tokenizer->position < tokenizer->input_length) {
                char curr = tokenizer->input[tokenizer->position];
                if (curr == '\'') {
                    /// Found closing quote - copy segment content
                    size_t segment_len = tokenizer->position - segment_start;
                    while (result_len + segment_len + 1 >= result_capacity) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return token_new(
                                TOK_ERROR, &tokenizer->input[start_pos], 1,
                                start_line, start_column, start_pos);
                        }
                        result = new_result;
                    }
                    memcpy(&result[result_len],
                           &tokenizer->input[segment_start], segment_len);
                    result_len += segment_len;

                    tokenizer->position++; /// Skip closing quote
                    tokenizer->column++;
                    goto check_adjacent;
                } else if (curr == '\n') {
                    tokenizer->line++;
                    tokenizer->column = 1;
                    tokenizer->position++;
                } else {
                    tokenizer->column++;
                    tokenizer->position++;
                }
            }
            /// Unterminated single-quoted string -- free the segment
            /// builder buffer before returning, otherwise every input
            /// with an unbalanced quote leaks 256+ bytes (caught via
            /// LeakSanitizer running fuzz_parser; see corresponding
            /// fix at the unterminated-double-quote site below).
            free(result);
            return token_new(TOK_ERROR, &tokenizer->input[start_pos],
                             tokenizer->position - start_pos, start_line,
                             start_column, start_pos);
        }

        /// Double-quoted string: character by character to handle line
        /// continuation
        has_expandable = true;
        while (tokenizer->position < tokenizer->input_length) {
            char curr = tokenizer->input[tokenizer->position];

            if (curr == '"') {
                /// Found closing quote
                tokenizer->position++; /// Skip closing quote
                tokenizer->column++;
                goto check_adjacent;
            } else if (curr == '$' &&
                       tokenizer->position + 1 < tokenizer->input_length &&
                       tokenizer->input[tokenizer->position + 1] == '(') {
                /// Handle command substitution inside double quotes - copy
                /// verbatim
                size_t subst_start = tokenizer->position;
                tokenizer->position += 2; /// Skip $(
                tokenizer->column += 2;
                int paren_depth = 1;

                while (tokenizer->position < tokenizer->input_length &&
                       paren_depth > 0) {
                    char sub_curr = tokenizer->input[tokenizer->position];
                    if (sub_curr == '(') {
                        paren_depth++;
                    } else if (sub_curr == ')') {
                        paren_depth--;
                    } else if (sub_curr == '"' || sub_curr == '\'') {
                        char sub_quote = sub_curr;
                        tokenizer->position++;
                        tokenizer->column++;
                        while (tokenizer->position < tokenizer->input_length) {
                            char nested_curr =
                                tokenizer->input[tokenizer->position];
                            if (nested_curr == sub_quote) {
                                break;
                            } else if (nested_curr == '\\' &&
                                       tokenizer->position + 1 <
                                           tokenizer->input_length) {
                                tokenizer->position++;
                                tokenizer->column++;
                            }
                            if (nested_curr == '\n') {
                                tokenizer->line++;
                                tokenizer->column = 1;
                            } else {
                                tokenizer->column++;
                            }
                            tokenizer->position++;
                        }
                    } else if (sub_curr == '\\' &&
                               tokenizer->position + 1 <
                                   tokenizer->input_length) {
                        tokenizer->position++;
                        tokenizer->column++;
                    }

                    if (sub_curr == '\n') {
                        tokenizer->line++;
                        tokenizer->column = 1;
                    } else {
                        tokenizer->column++;
                    }
                    tokenizer->position++;
                }
                /// Copy the entire command substitution to result
                size_t subst_len = tokenizer->position - subst_start;
                while (result_len + subst_len + 1 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                memcpy(&result[result_len], &tokenizer->input[subst_start],
                       subst_len);
                result_len += subst_len;
            } else if (curr == '`') {
                /// Handle backtick command substitution - copy verbatim
                size_t subst_start = tokenizer->position;
                tokenizer->position++; /// Skip opening backtick
                tokenizer->column++;

                while (tokenizer->position < tokenizer->input_length) {
                    char sub_curr = tokenizer->input[tokenizer->position];
                    if (sub_curr == '`') {
                        tokenizer->position++;
                        tokenizer->column++;
                        break;
                    } else if (sub_curr == '\\' &&
                               tokenizer->position + 1 <
                                   tokenizer->input_length) {
                        tokenizer->position++;
                        tokenizer->column++;
                    }

                    if (sub_curr == '\n') {
                        tokenizer->line++;
                        tokenizer->column = 1;
                    } else {
                        tokenizer->column++;
                    }
                    tokenizer->position++;
                }
                /// Copy the entire backtick substitution to result
                size_t subst_len = tokenizer->position - subst_start;
                while (result_len + subst_len + 1 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                memcpy(&result[result_len], &tokenizer->input[subst_start],
                       subst_len);
                result_len += subst_len;
            } else if (curr == '\\' &&
                       tokenizer->position + 1 < tokenizer->input_length) {
                /// Handle backslash escapes in double quotes
                char escaped = tokenizer->input[tokenizer->position + 1];
                if (escaped == '\n') {
                    /// Line continuation: skip both backslash and newline
                    /// entirely
                    tokenizer->position += 2;
                    tokenizer->line++;
                    tokenizer->column = 1;
                    /// Don't add anything to result - this is line continuation
                } else if (escaped == '$' || escaped == '`' || escaped == '"' ||
                           escaped == '\\' || escaped == '\n') {
                    /// These are the only characters that backslash quotes in
                    /// double quotes Add just the escaped character (not the
                    /// backslash)
                    while (result_len + 2 >= result_capacity) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return token_new(
                                TOK_ERROR, &tokenizer->input[start_pos], 1,
                                start_line, start_column, start_pos);
                        }
                        result = new_result;
                    }
                    /// Keep backslash + escaped char for later escape
                    /// processing
                    result[result_len++] = '\\';
                    result[result_len++] = escaped;
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                } else {
                    /// Backslash followed by other chars: keep both literally
                    while (result_len + 2 >= result_capacity) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return token_new(
                                TOK_ERROR, &tokenizer->input[start_pos], 1,
                                start_line, start_column, start_pos);
                        }
                        result = new_result;
                    }
                    result[result_len++] = '\\';
                    result[result_len++] = escaped;
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                }
            } else if (curr == '\n') {
                /// Literal newline in double quotes (not escaped)
                while (result_len + 1 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                result[result_len++] = curr;
                tokenizer->line++;
                tokenizer->column = 1;
                tokenizer->position++;
            } else {
                /// Regular character - add to result
                while (result_len + 1 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                result[result_len++] = curr;
                tokenizer->column++;
                tokenizer->position++;
            }
        }

        /// Unterminated double-quoted string -- free the segment
        /// builder buffer before returning, otherwise every input
        /// with an unbalanced double quote leaks 256+ bytes.
        free(result);
        return token_new(TOK_ERROR, &tokenizer->input[start_pos],
                         tokenizer->position - start_pos, start_line,
                         start_column, start_pos);

    check_adjacent:
        /// Check for adjacent quote - continue if another quote follows
        if (tokenizer->position < tokenizer->input_length) {
            char next = tokenizer->input[tokenizer->position];
            if (next == '\'' || next == '"') {
                /// Adjacent quote - continue parsing
                goto parse_next_segment;
            }
            /// Check for adjacent $' (ANSI-C quoting)
            if (next == '$' &&
                tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '\'') {
                tokenizer->position++; /// Skip $
                tokenizer->column++;
                /// Add marker for ANSI-C quote in result
                while (result_len + 2 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                result[result_len++] = '$';
                goto parse_next_segment;
            }
            /// Check for adjacent unquoted word characters
            if (is_word_char(next) || next == '\\' || next == '$') {
                /// Adjacent word - scan until whitespace or special char
                while (tokenizer->position < tokenizer->input_length) {
                    char wc = tokenizer->input[tokenizer->position];
                    if (wc == '\'' || wc == '"') {
                        /// Another quote - handle it
                        goto parse_next_segment;
                    }
                    if (wc == '$' &&
                        tokenizer->position + 1 < tokenizer->input_length &&
                        tokenizer->input[tokenizer->position + 1] == '\'') {
                        tokenizer->position++;
                        tokenizer->column++;
                        result[result_len++] = '$';
                        goto parse_next_segment;
                    }
                    if (!is_word_char(wc) && wc != '\\' && wc != '$') {
                        break;
                    }
                    /// Add character to result
                    if (result_len + 1 >= result_capacity) {
                        result_capacity *= 2;
                        char *new_result = realloc(result, result_capacity);
                        if (!new_result) {
                            free(result);
                            return token_new(
                                TOK_ERROR, &tokenizer->input[start_pos], 1,
                                start_line, start_column, start_pos);
                        }
                        result = new_result;
                    }
                    if (wc == '\\' &&
                        tokenizer->position + 1 < tokenizer->input_length) {
                        char esc_char =
                            tokenizer->input[tokenizer->position + 1];
                        if (esc_char == '\n') {
                            /// Line continuation outside quotes - skip both
                            tokenizer->position += 2;
                            tokenizer->line++;
                            tokenizer->column = 1;
                            continue;
                        }
                        /// Escape sequence
                        tokenizer->position++;
                        tokenizer->column++;
                        result[result_len++] =
                            tokenizer->input[tokenizer->position];
                    } else {
                        result[result_len++] = wc;
                    }
                    tokenizer->position++;
                    tokenizer->column++;
                    has_expandable =
                        true; /// Unquoted content may have expansions
                }
            }
        }

        /// Fuse a trailing zsh glob-qualifier group onto the quoted string:
        /// `"$f"(N)`, `'$f'(.)`, `"$f"(Nm-1)`. Gated on FEATURE_GLOB_QUALIFIERS
        /// so a mode without glob qualifiers (posix/bash) tokenizes
        /// `"x"(cmd)` exactly as before -- a quoted word then a subshell --
        /// rather than swallowing the group into a literal. The alphabet check
        /// is deliberately coarse: a balanced, whitespace-free, non-nested run
        /// drawn from the qualifier ALPHABET (type/permission letters plus the
        /// `m` time qualifier's unit/comparison/count chars). It is a
        /// character-set test, not the full qualifier grammar:
        /// parse_glob_qualifier is the single grammar authority and rejects a
        /// malformed run (`"$f"(5)`, `"$f"(mh)`), after which the value stays
        /// literal rather than being dropped. An ordinary adjacent
        /// subshell/grouping (a char outside the alphabet) falls through
        /// untouched. The parens are appended to the text and the token is
        /// flagged so the executor applies the qualifier to the expanded
        /// value; keeping the parens outside the quotes distinct from
        /// `"$f(N)"` (literal parens) rides on the flag, not the text.
        if (tokenizer->position < tokenizer->input_length &&
            tokenizer->input[tokenizer->position] == '(' &&
            shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
            size_t scan = tokenizer->position + 1;
            while (scan < tokenizer->input_length &&
                   tokenizer->input[scan] != ')') {
                if (!strchr(".@/*rwNDmMhs+-,0123456789",
                            tokenizer->input[scan])) {
                    break;
                }
                scan++;
            }
            /// Require a non-empty interior and a closing ')'.
            if (scan < tokenizer->input_length &&
                tokenizer->input[scan] == ')' &&
                scan > tokenizer->position + 1) {
                size_t group_len = scan - tokenizer->position + 1;
                while (result_len + group_len + 1 >= result_capacity) {
                    result_capacity *= 2;
                    char *new_result = realloc(result, result_capacity);
                    if (!new_result) {
                        free(result);
                        return token_new(TOK_ERROR,
                                         &tokenizer->input[start_pos], 1,
                                         start_line, start_column, start_pos);
                    }
                    result = new_result;
                }
                memcpy(result + result_len,
                       &tokenizer->input[tokenizer->position], group_len);
                result_len += group_len;
                tokenizer->position = scan + 1;
                tokenizer->column += group_len;
                glob_qualified = true;
            }
        }

        /// No more adjacent content - return the complete token
        result[result_len] = '\0';
        token_type_t type = has_expandable ? TOK_EXPANDABLE_STRING : TOK_STRING;
        token_t *tok = token_new(type, result, result_len, start_line,
                                 start_column, start_pos);
        if (tok) {
            tok->glob_qualified = glob_qualified;
        }
        free(result);
        return tok;
    }

    /// Handle variable references ($var, ${var}, $(cmd), $((expr)))
    if (c == '$') {
        size_t start = tokenizer->position;
        tokenizer->position++;
        tokenizer->column++;

        if (tokenizer->position < tokenizer->input_length) {
            char next = tokenizer->input[tokenizer->position];

            if (next == '(') {
                /// Check for $(( (arithmetic) or $( (command substitution)
                tokenizer->position++;
                tokenizer->column++;

                if (tokenizer->position < tokenizer->input_length &&
                    tokenizer->input[tokenizer->position] == '(') {
                    /// `$((` is ambiguous: it could begin arithmetic
                    /// expansion `$((expr))` OR command substitution of
                    /// an anonymous function `$(() { body; } args)`.
                    /// Arithmetic content cannot contain braces, semicolons,
                    /// or newlines at the top level, so if a lookahead
                    /// scan finds any of those before the matched `))`,
                    /// the input is really `$(` followed by a `(` -- back
                    /// up the second `(` and reparse as command sub.
                    /// Issue #99.
                    /// position is at the second `(`; content begins after it.
                    bool looks_arith = lush_dollar_paren_is_arithmetic(
                        &tokenizer->input[tokenizer->position + 1],
                        tokenizer->input_length - (tokenizer->position + 1));

                    if (looks_arith) {
                        /// Arithmetic expansion $((expr))
                        tokenizer->position++;
                        tokenizer->column++;

                        int paren_count = 2;
                        while (tokenizer->position < tokenizer->input_length &&
                               paren_count > 0) {
                            char curr = tokenizer->input[tokenizer->position];
                            if (curr == '(') {
                                paren_count++;
                            } else if (curr == ')') {
                                paren_count--;
                            } else if (curr == '\n') {
                                tokenizer->line++;
                                tokenizer->column = 0;
                            }
                            tokenizer->position++;
                            tokenizer->column++;
                        }

                        /// Check for unclosed arithmetic expansion
                        if (paren_count > 0) {
                            size_t length = tokenizer->position - start;
                            return token_new(
                                TOK_ERROR, &tokenizer->input[start], length,
                                start_line, start_column, start_pos);
                        }

                        size_t length = tokenizer->position - start;
                        return token_new(TOK_ARITH_EXP,
                                         &tokenizer->input[start], length,
                                         start_line, start_column, start_pos);
                    }
                    /// Fall through to command-sub handling below.
                    /// Position is currently at the second `(` of `$((`,
                    /// which is the start of the inner subshell or
                    /// anonymous function body that the command-sub
                    /// paren-counter will track.
                }
                {
                    /// Command substitution $(cmd). Use the canonical
                    /// quote-aware matcher so a quoted `)` inside the command
                    /// (e.g. `$(echo ")")`) does not mis-terminate it -- the
                    /// hand-rolled paren counter here stopped at the first `)`,
                    /// even one inside quotes, so the rest of the word (a
                    /// dangling quote) then failed to parse (#486). The opener
                    /// is the `(` at start + 1; a `$((`-that-is-really-`$(` (an
                    /// anonymous function) has fallen through to here with the
                    /// same opener.
                    size_t close_off = 0;
                    bool matched = lush_find_matching_brace(
                        &tokenizer->input[start + 1],
                        tokenizer->input_length - (start + 1), &close_off);
                    /// One past the matching `)` (offset is relative to the
                    /// `(`); on no match consume to end and report the error.
                    size_t end_pos = matched ? start + 1 + close_off + 1
                                             : tokenizer->input_length;
                    /// Advance across the substitution, keeping the line/column
                    /// counters in step for diagnostics.
                    while (tokenizer->position < end_pos) {
                        if (tokenizer->input[tokenizer->position] == '\n') {
                            tokenizer->line++;
                            tokenizer->column = 0;
                        } else {
                            tokenizer->column++;
                        }
                        tokenizer->position++;
                    }

                    size_t length = tokenizer->position - start;
                    if (!matched) {
                        /// Unclosed command substitution.
                        return token_new(TOK_ERROR, &tokenizer->input[start],
                                         length, start_line, start_column,
                                         start_pos);
                    }
                    return token_new(TOK_COMMAND_SUB, &tokenizer->input[start],
                                     length, start_line, start_column,
                                     start_pos);
                }
            } else if (next == '\'') {
                /// ANSI-C quoting $'...'
                tokenizer->position++; /// Skip the '
                tokenizer->column++;

                while (tokenizer->position < tokenizer->input_length) {
                    char curr = tokenizer->input[tokenizer->position];

                    if (curr == '\'') {
                        /// End of ANSI-C string
                        tokenizer->position++;
                        tokenizer->column++;
                        break;
                    } else if (curr == '\\' && tokenizer->position + 1 <
                                                   tokenizer->input_length) {
                        /// Escape sequence - skip both chars
                        tokenizer->position += 2;
                        tokenizer->column += 2;
                    } else if (curr == '\n') {
                        tokenizer->line++;
                        tokenizer->column = 0;
                        tokenizer->position++;
                    } else {
                        tokenizer->position++;
                        tokenizer->column++;
                    }
                }

                size_t length = tokenizer->position - start;
                return token_new(TOK_STRING, &tokenizer->input[start], length,
                                 start_line, start_column, start_pos);
            } else if (next == '{') {
                /// Parameter expansion ${var} with proper nested brace handling
                tokenizer->position++;
                tokenizer->column++;

                int brace_count = 1; /// We've seen the opening brace
                while (tokenizer->position < tokenizer->input_length &&
                       brace_count > 0) {
                    char curr = tokenizer->input[tokenizer->position];

                    if (curr == '{') {
                        brace_count++;
                    } else if (curr == '}') {
                        brace_count--;
                    } else if (curr == '\n') {
                        tokenizer->line++;
                        tokenizer->column = 0;
                        tokenizer->position++;
                        continue;
                    }

                    tokenizer->position++;
                    tokenizer->column++;
                }

                size_t length = tokenizer->position - start;
                return token_new(TOK_VARIABLE, &tokenizer->input[start], length,
                                 start_line, start_column, start_pos);
            } else if (next == '+' &&
                       tokenizer->position + 1 < tokenizer->input_length &&
                       lush_ident_match_start(
                           &tokenizer->input[tokenizer->position + 1],
                           tokenizer->input_length - tokenizer->position - 1) >
                           0) {
                /// zsh `$+NAME` (and `$+NAME[SUBSCRIPT]`) is the unbraced
                /// shorthand for `${+NAME}` is-set test. Consume `+`,
                /// the identifier, and any `[...]` subscript so the
                /// expansion path receives one TOK_VARIABLE token rather
                /// than `$` + word + `+NAME[...]`.
                tokenizer->position++; /// consume +
                tokenizer->column++;
                while (tokenizer->position < tokenizer->input_length) {
                    size_t n = lush_ident_match_continue(
                        &tokenizer->input[tokenizer->position],
                        tokenizer->input_length - tokenizer->position);
                    if (n == 0) {
                        break;
                    }
                    tokenizer->position += n;
                    tokenizer->column += n;
                }
                if (tokenizer->position < tokenizer->input_length &&
                    tokenizer->input[tokenizer->position] == '[') {
                    int depth = 0;
                    while (tokenizer->position < tokenizer->input_length) {
                        char curr = tokenizer->input[tokenizer->position];
                        if (curr == '[') {
                            depth++;
                        } else if (curr == ']') {
                            depth--;
                            tokenizer->position++;
                            tokenizer->column++;
                            if (depth == 0) {
                                break;
                            }
                            continue;
                        }
                        tokenizer->position++;
                        tokenizer->column++;
                    }
                }
                size_t length = tokenizer->position - start;
                return token_new(TOK_VARIABLE, &tokenizer->input[start], length,
                                 start_line, start_column, start_pos);
            } else if (lush_ident_match_start(
                           &tokenizer->input[tokenizer->position],
                           tokenizer->input_length - tokenizer->position) > 0 ||
                       next == '?' || next == '$' || next == '!' ||
                       next == '@' || next == '*' || next == '#' ||
                       next == '-') {
                /// Simple variable $var, $?, $$, $!, $-, etc.
                /// $var goes through the lush identifier predicate so a
                /// non-ASCII identifier ($café, $Σ, $имя) is accepted
                /// under FEATURE_UNICODE_IDENTIFIERS. Special-form
                /// single-char variants ($?, $@, etc.) keep their
                /// fast-path branch.

                /// For special single-character variables, only advance by one
                if (next == '?' || next == '$' || next == '!' || next == '@' ||
                    next == '*' || next == '#' || next == '-') {
                    tokenizer
                        ->position++; /// Just one character for special vars
                    tokenizer->column++;
                } else {
                    /// For regular variables, continue until non-identifier
                    /// character. lush_ident_match_continue returns the
                    /// byte length consumed (1 for ASCII, 2-4 for multi-
                    /// byte UTF-8), so we advance by that count.
                    while (tokenizer->position < tokenizer->input_length) {
                        size_t n = lush_ident_match_continue(
                            &tokenizer->input[tokenizer->position],
                            tokenizer->input_length - tokenizer->position);
                        if (n == 0) {
                            break;
                        }
                        tokenizer->position += n;
                        tokenizer->column += n;
                    }

                    /// Zsh bare-form subscript: $var[N] / $var[N,M] in
                    /// unquoted context. Without this, the tokenizer
                    /// emits TOK_VARIABLE($var) + TOK_LBRACKET([) + ...
                    /// and the parser dispatches `[1]` as the `[` test
                    /// builtin (issue #58). When FEATURE_ZSH_BARE_SUBSCRIPT
                    /// is enabled, absorb the entire [...] into the
                    /// TOK_VARIABLE so parse_parameter_expansion sees
                    /// `$var[N]` as one unit -- same shape as the brace
                    /// form ${var[N]} and the quoted bare form "$var[N]"
                    /// which both already work.
                    ///
                    /// The subscript is scanned by balanced-bracket
                    /// counter so nested brackets in expressions like
                    /// $arr[$other[i]] or $a[$((b[c]+1))] absorb
                    /// correctly. A bracket without a matching close is
                    /// left for the parser to surface as a normal error;
                    /// we don't advance position in that case. The flag
                    /// is curated true in zsh+lush modes, false in
                    /// posix+bash (see src/shell_mode.c).
                    if (tokenizer->position < tokenizer->input_length &&
                        tokenizer->input[tokenizer->position] == '[' &&
                        shell_mode_allows(FEATURE_ZSH_BARE_SUBSCRIPT)) {
                        size_t scan = tokenizer->position;
                        size_t scan_col = tokenizer->column;
                        size_t scan_line = tokenizer->line;
                        int depth = 0;
                        bool closed = false;
                        while (scan < tokenizer->input_length) {
                            char sc = tokenizer->input[scan];
                            if (sc == '[') {
                                depth++;
                            } else if (sc == ']') {
                                depth--;
                                if (depth == 0) {
                                    scan++;
                                    scan_col++;
                                    closed = true;
                                    break;
                                }
                            } else if (sc == '\n') {
                                scan_line++;
                                scan_col = 0;
                            }
                            scan++;
                            scan_col++;
                        }
                        if (closed) {
                            tokenizer->position = scan;
                            tokenizer->column = scan_col;
                            tokenizer->line = scan_line;
                        }
                    }
                }

                size_t length = tokenizer->position - start;
                return token_new(TOK_VARIABLE, &tokenizer->input[start], length,
                                 start_line, start_column, start_pos);
            }
        }

        /// Just a plain $ - treat as word
        size_t length = tokenizer->position - start;
        return token_new(TOK_WORD, &tokenizer->input[start], length, start_line,
                         start_column, start_pos);
    }

    /// Handle backtick command substitution
    if (c == '`') {
        size_t start = tokenizer->position;
        tokenizer->position++;
        tokenizer->column++;

        while (tokenizer->position < tokenizer->input_length &&
               tokenizer->input[tokenizer->position] != '`') {
            if (tokenizer->input[tokenizer->position] == '\n') {
                tokenizer->line++;
                tokenizer->column = 0;
            }
            tokenizer->position++;
            tokenizer->column++;
        }

        if (tokenizer->position < tokenizer->input_length) {
            tokenizer->position++; /// Skip closing backtick
            tokenizer->column++;
        } else {
            /// Unclosed backtick - return error token
            size_t length = tokenizer->position - start;
            return token_new(TOK_ERROR, &tokenizer->input[start], length,
                             start_line, start_column, start_pos);
        }

        size_t length = tokenizer->position - start;
        return token_new(TOK_BACKQUOTE, &tokenizer->input[start], length,
                         start_line, start_column, start_pos);
    }

    /// Handle operators
    if (is_operator_char(c)) {
        switch (c) {
        case ';':
            /// Check for ;;& (case continue - test next pattern)
            if (tokenizer->position + 2 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == ';' &&
                tokenizer->input[tokenizer->position + 2] == '&') {
                tokenizer->position += 3;
                tokenizer->column += 3;
                return token_new(TOK_CASE_CONTINUE, ";;&", 3, start_line,
                                 start_column, start_pos);
            }
            /// Check for ;& (case fall-through - execute next without test)
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '&') {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_CASE_FALLTHROUGH, ";&", 2, start_line,
                                 start_column, start_pos);
            }
            /// Regular semicolon
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_SEMICOLON, ";", 1, start_line, start_column,
                             start_pos);

        case '|':
            if (tokenizer->position + 1 < tokenizer->input_length) {
                char next = tokenizer->input[tokenizer->position + 1];
                if (next == '|') {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_LOGICAL_OR, "||", 2, start_line,
                                     start_column, start_pos);
                }
                /// Pipe stderr |& (shorthand for 2>&1 |)
                if (next == '&' &&
                    shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_PIPE_STDERR, "|&", 2, start_line,
                                     start_column, start_pos);
                }
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_PIPE, "|", 1, start_line, start_column,
                             start_pos);

        case '&':
            if (tokenizer->position + 1 < tokenizer->input_length) {
                char next = tokenizer->input[tokenizer->position + 1];
                if (next == '&') {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_LOGICAL_AND, "&&", 2, start_line,
                                     start_column, start_pos);
                } else if (next == '|' || next == '!') {
                    /// zsh `&|` and `&!` -- background-and-disown. Treated
                    /// as plain background for execution semantics; the
                    /// disown bookkeeping is an optimization the corpus
                    /// does not observe.
                    char text[3] = {'&', next, '\0'};
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_BACKGROUND_DISOWN, text, 2, start_line,
                                     start_column, start_pos);
                } else if (next == '>') {
                    /// Check for &>> (append both stdout and stderr)
                    if (tokenizer->position + 2 < tokenizer->input_length &&
                        tokenizer->input[tokenizer->position + 2] == '>' &&
                        shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
                        tokenizer->position += 3;
                        tokenizer->column += 3;
                        return token_new(TOK_APPEND_BOTH, "&>>", 3, start_line,
                                         start_column, start_pos);
                    }
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_REDIRECT_BOTH, "&>", 2, start_line,
                                     start_column, start_pos);
                }
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_AND, "&", 1, start_line, start_column,
                             start_pos);

        case '<':
            if (tokenizer->position + 1 < tokenizer->input_length) {
                char next = tokenizer->input[tokenizer->position + 1];
                /// Process substitution <( - check before heredoc
                if (next == '(' &&
                    shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_PROC_SUB_IN, "<(", 2, start_line,
                                     start_column, start_pos);
                }
                if (next == '<') {
                    if (tokenizer->position + 2 < tokenizer->input_length &&
                        tokenizer->input[tokenizer->position + 2] == '<') {
                        tokenizer->position += 3;
                        tokenizer->column += 3;
                        return token_new(TOK_HERESTRING, "<<<", 3, start_line,
                                         start_column, start_pos);
                    } else if (tokenizer->position + 2 <
                                   tokenizer->input_length &&
                               tokenizer->input[tokenizer->position + 2] ==
                                   '-') {
                        tokenizer->position += 3;
                        tokenizer->column += 3;
                        return token_new(TOK_HEREDOC_STRIP, "<<-", 3,
                                         start_line, start_column, start_pos);
                    } else {
                        tokenizer->position += 2;
                        tokenizer->column += 2;
                        return token_new(TOK_HEREDOC, "<<", 2, start_line,
                                         start_column, start_pos);
                    }
                }
                /// Handle <&N, <&-, <&$VAR patterns (input fd duplication)
                if (next == '&' &&
                    tokenizer->position + 2 < tokenizer->input_length) {
                    char fd_char = tokenizer->input[tokenizer->position + 2];
                    /// Handle <&N (dup input from fd N) or <&- (close stdin)
                    if (isdigit(fd_char) || fd_char == '-') {
                        tokenizer->position += 3;
                        tokenizer->column += 3;
                        return token_new(TOK_REDIRECT_FD,
                                         &tokenizer->input[start_pos], 3,
                                         start_line, start_column, start_pos);
                    }
                    /// Handle <&$VAR or <&${VAR} patterns
                    if (fd_char == '$') {
                        size_t fd_start = tokenizer->position + 2;
                        size_t fd_pos = fd_start + 1; /// Skip $
                        /// Handle ${...} brace form
                        if (fd_pos < tokenizer->input_length &&
                            tokenizer->input[fd_pos] == '{') {
                            fd_pos++; /// Skip {
                            int brace_depth = 1;
                            while (fd_pos < tokenizer->input_length &&
                                   brace_depth > 0) {
                                if (tokenizer->input[fd_pos] == '{')
                                    brace_depth++;
                                else if (tokenizer->input[fd_pos] == '}')
                                    brace_depth--;
                                fd_pos++;
                            }
                        } else {
                            /// Simple $VAR form - scan alphanumeric/underscore
                            while (fd_pos < tokenizer->input_length &&
                                   (isalnum(tokenizer->input[fd_pos]) ||
                                    tokenizer->input[fd_pos] == '_')) {
                                fd_pos++;
                            }
                        }
                        size_t length = fd_pos - start_pos;
                        tokenizer->position = fd_pos;
                        tokenizer->column += length;
                        return token_new(TOK_REDIRECT_FD,
                                         &tokenizer->input[start_pos], length,
                                         start_line, start_column, start_pos);
                    }
                }
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_REDIRECT_IN, "<", 1, start_line, start_column,
                             start_pos);

        case '>':
            if (tokenizer->position + 1 < tokenizer->input_length) {
                char next = tokenizer->input[tokenizer->position + 1];
                /// Process substitution >(
                if (next == '(' &&
                    shell_mode_allows(FEATURE_PROCESS_SUBSTITUTION)) {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_PROC_SUB_OUT, ">(", 2, start_line,
                                     start_column, start_pos);
                }
                if (next == '>') {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_APPEND, ">>", 2, start_line,
                                     start_column, start_pos);
                }
                if (next == '|') {
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    return token_new(TOK_REDIRECT_CLOBBER, ">|", 2, start_line,
                                     start_column, start_pos);
                }
                if (next == '&' &&
                    tokenizer->position + 2 < tokenizer->input_length) {
                    char fd_char = tokenizer->input[tokenizer->position + 2];
                    /// Handle >&N pattern (redirect stdout to file descriptor
                    /// N) Also handle >&- (close fd) and >&$VAR (variable
                    /// expansion)
                    if (isdigit(fd_char) || fd_char == '-') {
                        tokenizer->position += 3;
                        tokenizer->column += 3;
                        return token_new(TOK_REDIRECT_FD,
                                         &tokenizer->input[start_pos], 3,
                                         start_line, start_column, start_pos);
                    }
                    /// Handle >&$VAR or >&${VAR} patterns
                    if (fd_char == '$') {
                        size_t fd_start = tokenizer->position + 2;
                        size_t fd_pos = fd_start + 1; /// Skip $
                        /// Handle ${...} brace form
                        if (fd_pos < tokenizer->input_length &&
                            tokenizer->input[fd_pos] == '{') {
                            fd_pos++; /// Skip {
                            int brace_depth = 1;
                            while (fd_pos < tokenizer->input_length &&
                                   brace_depth > 0) {
                                if (tokenizer->input[fd_pos] == '{')
                                    brace_depth++;
                                else if (tokenizer->input[fd_pos] == '}')
                                    brace_depth--;
                                fd_pos++;
                            }
                        } else {
                            /// Simple $VAR form - scan alphanumeric/underscore
                            while (fd_pos < tokenizer->input_length &&
                                   (isalnum(tokenizer->input[fd_pos]) ||
                                    tokenizer->input[fd_pos] == '_')) {
                                fd_pos++;
                            }
                        }
                        size_t length = fd_pos - start_pos;
                        tokenizer->position = fd_pos;
                        tokenizer->column += length;
                        return token_new(TOK_REDIRECT_FD,
                                         &tokenizer->input[start_pos], length,
                                         start_line, start_column, start_pos);
                    }
                }
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_REDIRECT_OUT, ">", 1, start_line, start_column,
                             start_pos);

        case '=':
            /// `=~` is the regex-match operator (inside [[ ]]) ONLY when it
            /// stands alone -- at input start or preceded by whitespace, as in
            /// `[[ str =~ re ]]`. When the `=` immediately follows a word
            /// character it is an assignment `=` and the `~` is a separate
            /// tilde (e.g. `x=~/path`, or `cmd foo=~/path`); tokenizing that as
            /// the regex operator broke tilde-expanded assignments entirely.
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '~' &&
                shell_mode_allows(FEATURE_REGEX_MATCH) &&
                (start_pos == 0 || tokenizer->input[start_pos - 1] == ' ' ||
                 tokenizer->input[start_pos - 1] == '\t' ||
                 tokenizer->input[start_pos - 1] == '\n' ||
                 tokenizer->input[start_pos - 1] == '\r')) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_REGEX_MATCH, "=~", 2, start_line,
                                 start_column, start_pos);
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_ASSIGN, "=", 1, start_line, start_column,
                             start_pos);

        case '!':
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '=') {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_NOT_EQUAL, "!=", 2, start_line,
                                 start_column, start_pos);
            }
            /// Check for extglob !(pattern)
            if (shell_mode_allows(FEATURE_EXTENDED_GLOB) &&
                tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '(') {
                /// Fall through to word tokenization which handles extglob
                break;
            }
            /// Standalone ! character (for test negation)
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_WORD, "!", 1, start_line, start_column,
                             start_pos);

        case '+':
            /// Check for += (append/add assignment)
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '=' &&
                shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_PLUS_ASSIGN, "+=", 2, start_line,
                                 start_column, start_pos);
            }
            /// Let + be handled as part of words (e.g., date +%Y)
            /// Fall through to word tokenization
            break;

            /// case '-' is not reachable here -- '-' is a word char (not
            /// an operator char) and is consumed by the word tokenizer
            /// below. The '->' arrow is handled before the word path so
            /// it does not get absorbed into a word.

            /// case '*':
            ///     tokenizer->position++;
            ///     tokenizer->column++;
            ///     return token_new(TOK_MULTIPLY, "*", 1, start_line,
            ///     start_column,
            ///                      start_pos);

        case '%':
            /// Let % be handled as part of words (e.g., +%Y format specifiers)
            /// Fall through to word tokenization
            break;

        case ',':
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_COMMA, ",", 1, start_line, start_column,
                             start_pos);

            /// case '?':
            ///     tokenizer->position++;
            ///     tokenizer->column++;
            ///     return token_new(TOK_QUESTION, "?", 1, start_line,
            ///     start_column,
            ///                      start_pos);

        case '(':
            /// Check for (( arithmetic command
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '(' &&
                shell_mode_allows(FEATURE_ARITH_COMMAND)) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                tokenizer->arith_cmd_depth++; /// Track arithmetic context
                return token_new(TOK_DOUBLE_LPAREN, "((", 2, start_line,
                                 start_column, start_pos);
            }
            /// Check for zsh-style glob alternation: (a|b)suffix
            /// This is a word, not a subshell, when:
            /// 1. Extended glob is enabled
            /// 2. There's a | inside the parens
            /// 3. After ), there's more word-like content (not
            /// whitespace/EOF/operator)
            if (shell_mode_allows(FEATURE_EXTENDED_GLOB)) {
                size_t scan_pos = tokenizer->position + 1;
                bool has_pipe = false;
                bool found_close = false;
                int paren_depth = 1;

                /// Scan to find matching ) and check for |
                while (scan_pos < tokenizer->input_length && paren_depth > 0) {
                    char sc = tokenizer->input[scan_pos];
                    if (sc == '(') {
                        paren_depth++;
                    } else if (sc == ')') {
                        paren_depth--;
                        if (paren_depth == 0) {
                            found_close = true;
                        }
                    } else if (sc == '|' && paren_depth == 1) {
                        has_pipe = true;
                    }
                    scan_pos++;
                }

                /// If we found (a|b) pattern, check what follows
                if (found_close && has_pipe) {
                    /// Check if there's word-like content after )
                    if (scan_pos < tokenizer->input_length) {
                        char next = tokenizer->input[scan_pos];
                        /// If followed by alphanumeric, dot, or other word
                        /// chars, it's glob alternation
                        if (isalnum(next) || next == '.' || next == '_' ||
                            next == '-' || next == '*' || next == '?') {
                            /// Treat entire (a|b)suffix as a word token
                            size_t word_start = tokenizer->position;
                            /// Skip past the closing paren we found
                            tokenizer->position = scan_pos;
                            tokenizer->column += (scan_pos - word_start);

                            /// Continue scanning word chars after )
                            while (tokenizer->position <
                                   tokenizer->input_length) {
                                char wc = tokenizer->input[tokenizer->position];
                                if (isalnum(wc) || is_word_char(wc)) {
                                    tokenizer->position++;
                                    tokenizer->column++;
                                } else {
                                    break;
                                }
                            }

                            size_t word_len = tokenizer->position - word_start;
                            /// token_new copies the text, so pass input
                            /// directly
                            return token_new(
                                TOK_WORD, &tokenizer->input[word_start],
                                word_len, start_line, start_column, start_pos);
                        }
                    }
                }
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_LPAREN, "(", 1, start_line, start_column,
                             start_pos);

        case ')':
            /// Check for )) arithmetic command end - only when inside (( ))
            /// Without this check, nested process substitutions like
            /// cat <(cat <(echo nested)) would fail because )) gets
            /// tokenized as TOK_DOUBLE_RPAREN instead of two TOK_RPAREN
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == ')' &&
                tokenizer->arith_cmd_depth > 0 &&
                shell_mode_allows(FEATURE_ARITH_COMMAND)) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                tokenizer->arith_cmd_depth--; /// Leaving arithmetic context
                return token_new(TOK_DOUBLE_RPAREN, "))", 2, start_line,
                                 start_column, start_pos);
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_RPAREN, ")", 1, start_line, start_column,
                             start_pos);

        case '{':
            /// Check for {varname} fd allocation syntax (bash 4.1+/zsh)
            /// Pattern: {identifier}> or {identifier}< for fd allocation
            if (tokenizer->position + 2 < tokenizer->input_length) {
                size_t scan = tokenizer->position + 1; /// After '{'
                /// Scan for valid identifier; honors
                /// FEATURE_UNICODE_IDENTIFIERS via lush_ident_match_start /
                /// _continue.
                size_t start_n = lush_ident_match_start(
                    &tokenizer->input[scan], tokenizer->input_length - scan);
                if (start_n > 0) {
                    scan += start_n;
                    while (scan < tokenizer->input_length) {
                        size_t n = lush_ident_match_continue(
                            &tokenizer->input[scan],
                            tokenizer->input_length - scan);
                        if (n == 0) {
                            break;
                        }
                        scan += n;
                    }
                    /// Check for closing } followed by redirection operator
                    if (scan < tokenizer->input_length &&
                        tokenizer->input[scan] == '}') {
                        size_t after_brace = scan + 1;
                        if (after_brace < tokenizer->input_length) {
                            char next_ch = tokenizer->input[after_brace];
                            /// Must be followed by > or < for fd allocation
                            if (next_ch == '>' || next_ch == '<') {
                                /// This is {varname}> or {varname}< fd
                                /// allocation Include the entire pattern with
                                /// redirection op
                                size_t tok_end = after_brace + 1;
                                /// Check for >> or >& or <& variants
                                if (tok_end < tokenizer->input_length) {
                                    char op2 = tokenizer->input[tok_end];
                                    if (op2 == '>' || op2 == '&' ||
                                        op2 == '<') {
                                        tok_end++;
                                        /// Check for >&- pattern
                                        if (tok_end < tokenizer->input_length &&
                                            tokenizer->input[tok_end] == '-') {
                                            tok_end++;
                                        }
                                    }
                                }
                                size_t length = tok_end - tokenizer->position;
                                token_t *tok = token_new(
                                    TOK_REDIRECT_FD_ALLOC,
                                    &tokenizer->input[tokenizer->position],
                                    length, start_line, start_column,
                                    start_pos);
                                tokenizer->position = tok_end;
                                tokenizer->column += length;
                                return tok;
                            }
                        }
                    }
                }
            }
            /// Check if this is brace expansion: {a,b,c} or {1..10}
            /// Or a literal brace word: {solo} or {}
            /// Command groups require whitespace after {: { cmd; }
            if (shell_mode_allows(FEATURE_BRACE_EXPANSION)) {
                size_t scan_pos = tokenizer->position + 1;
                bool has_comma = false;
                bool has_dotdot = false;
                bool found_close = false;
                bool has_whitespace_after_open = false;
                int brace_depth = 1;

                /// Lookahead to find matching } and check for , or ..
                while (scan_pos < tokenizer->input_length && brace_depth > 0) {
                    char sc = tokenizer->input[scan_pos];

                    /// If we hit whitespace/newline right after {, it's a
                    /// command group
                    if (scan_pos == tokenizer->position + 1 &&
                        (sc == ' ' || sc == '\t' || sc == '\n')) {
                        has_whitespace_after_open = true;
                        break;
                    }

                    if (sc == '{') {
                        brace_depth++;
                    } else if (sc == '}') {
                        brace_depth--;
                        if (brace_depth == 0) {
                            found_close = true;
                        }
                    } else if (sc == ',' && brace_depth == 1) {
                        has_comma = true;
                    } else if (sc == '.' &&
                               scan_pos + 1 < tokenizer->input_length &&
                               tokenizer->input[scan_pos + 1] == '.' &&
                               brace_depth == 1) {
                        has_dotdot = true;
                        scan_pos++; /// skip second dot
                    }
                    scan_pos++;
                }

                /// If no whitespace after { and we found matching }, treat as
                /// word This covers: {a,b,c}, {1..10}, {solo}, {} Note:
                /// has_comma/has_dotdot tracked for potential future validation
                (void)has_comma;
                (void)has_dotdot;
                if (!has_whitespace_after_open && found_close) {
                    /// Also consume suffix: word characters AND additional
                    /// brace patterns This handles {a,b,c}_suffix and
                    /// {1..2}{a..b} Cartesian products
                    while (scan_pos < tokenizer->input_length) {
                        char sc = tokenizer->input[scan_pos];
                        /// Check for word characters that can be part of suffix
                        if (isalnum((unsigned char)sc) ||
                            strchr("_.-/~:@*?+%!", sc) != NULL) {
                            scan_pos++;
                        } else if ((unsigned char)sc >= 0x80) {
                            /// Skip the rest of this UTF-8 sequence. Use the
                            /// shared LLE primitive so the byte-pattern logic
                            /// lives in exactly one place (issue #50). The
                            /// primitive reports what the lead byte CLAIMS;
                            /// the caller is responsible for clamping against
                            /// the input length so a partial sequence at the
                            /// end of the buffer cannot walk past it.
                            int seq_len =
                                lle_utf8_sequence_length((unsigned char)sc);
                            if (seq_len <= 0) {
                                seq_len = 1;
                            }
                            scan_pos += (size_t)seq_len;
                            if (scan_pos > tokenizer->input_length) {
                                scan_pos = tokenizer->input_length;
                            }
                        } else if (sc == '{') {
                            /// Another brace pattern - scan for its closing }
                            /// to support Cartesian products like {1..2}{a..b}
                            size_t brace_scan = scan_pos + 1;
                            int depth = 1;
                            bool valid_brace = false;
                            while (brace_scan < tokenizer->input_length &&
                                   depth > 0) {
                                char bc = tokenizer->input[brace_scan];
                                if (bc == '{')
                                    depth++;
                                else if (bc == '}') {
                                    depth--;
                                    if (depth == 0)
                                        valid_brace = true;
                                }
                                brace_scan++;
                            }
                            if (valid_brace) {
                                scan_pos =
                                    brace_scan; /// Include this brace group
                            } else {
                                break; /// Malformed brace, stop
                            }
                        } else {
                            break;
                        }
                    }

                    size_t total_len = scan_pos - tokenizer->position;

                    token_t *tok = token_new(
                        TOK_WORD, &tokenizer->input[tokenizer->position],
                        total_len, start_line, start_column, start_pos);
                    tokenizer->position = scan_pos;
                    tokenizer->column += total_len;
                    return tok;
                }
            }
            /// Not a brace expansion - return as command group brace
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_LBRACE, "{", 1, start_line, start_column,
                             start_pos);

        case '}':
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_RBRACE, "}", 1, start_line, start_column,
                             start_pos);

        case '[':
            /// Check for [[ extended test
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == '[' &&
                shell_mode_allows(FEATURE_EXTENDED_TEST)) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_DOUBLE_LBRACKET, "[[", 2, start_line,
                                 start_column, start_pos);
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_LBRACKET, "[", 1, start_line, start_column,
                             start_pos);

        case ']':
            /// Check for ]] extended test end
            if (tokenizer->position + 1 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position + 1] == ']' &&
                shell_mode_allows(FEATURE_EXTENDED_TEST)) {
                tokenizer->position += 2;
                tokenizer->column += 2;
                return token_new(TOK_DOUBLE_RBRACKET, "]]", 2, start_line,
                                 start_column, start_pos);
            }
            tokenizer->position++;
            tokenizer->column++;
            return token_new(TOK_RBRACKET, "]", 1, start_line, start_column,
                             start_pos);
        }
    }

    /// Handle numbered file descriptor redirections first
    if (isdigit(c)) {
        size_t num_start = tokenizer->position;
        /// Read the number
        while (tokenizer->position < tokenizer->input_length &&
               isdigit(tokenizer->input[tokenizer->position])) {
            tokenizer->position++;
            tokenizer->column++;
        }

        /// Check if followed by >, >>, or >&
        if (tokenizer->position < tokenizer->input_length) {
            if (tokenizer->input[tokenizer->position] == '>') {
                if (tokenizer->position + 1 < tokenizer->input_length &&
                    tokenizer->input[tokenizer->position + 1] == '>') {
                    /// Handle N>> (append to file descriptor N)
                    tokenizer->position += 2;
                    tokenizer->column += 2;
                    size_t length = tokenizer->position - num_start;
                    return token_new(TOK_APPEND_ERR,
                                     &tokenizer->input[num_start], length,
                                     start_line, start_column, start_pos);
                } else if (tokenizer->position + 1 < tokenizer->input_length &&
                           tokenizer->input[tokenizer->position + 1] == '&') {
                    /// Check for N>&M, N>&-, or N>&$VAR patterns
                    if (tokenizer->position + 2 < tokenizer->input_length) {
                        char fd_char =
                            tokenizer->input[tokenizer->position + 2];
                        if (isdigit(fd_char) || fd_char == '-') {
                            tokenizer->position += 3; /// Skip >&M or >&-
                            tokenizer->column += 3;
                            size_t length = tokenizer->position - num_start;
                            return token_new(
                                TOK_REDIRECT_FD, &tokenizer->input[num_start],
                                length, start_line, start_column, start_pos);
                        }
                        /// Handle N>&$VAR or N>&${VAR} patterns
                        if (fd_char == '$') {
                            size_t fd_pos =
                                tokenizer->position + 3; /// After >&$
                            /// Handle ${...} brace form
                            if (fd_pos < tokenizer->input_length &&
                                tokenizer->input[fd_pos] == '{') {
                                fd_pos++; /// Skip {
                                int brace_depth = 1;
                                while (fd_pos < tokenizer->input_length &&
                                       brace_depth > 0) {
                                    if (tokenizer->input[fd_pos] == '{')
                                        brace_depth++;
                                    else if (tokenizer->input[fd_pos] == '}')
                                        brace_depth--;
                                    fd_pos++;
                                }
                            } else {
                                /// Simple $VAR form - scan
                                /// alphanumeric/underscore
                                while (fd_pos < tokenizer->input_length &&
                                       (isalnum(tokenizer->input[fd_pos]) ||
                                        tokenizer->input[fd_pos] == '_')) {
                                    fd_pos++;
                                }
                            }
                            size_t length = fd_pos - num_start;
                            tokenizer->position = fd_pos;
                            tokenizer->column += length;
                            return token_new(
                                TOK_REDIRECT_FD, &tokenizer->input[num_start],
                                length, start_line, start_column, start_pos);
                        }
                    }
                } else {
                    /// Handle N> (redirect file descriptor N)
                    tokenizer->position++;
                    tokenizer->column++;
                    size_t length = tokenizer->position - num_start;
                    return token_new(TOK_REDIRECT_ERR,
                                     &tokenizer->input[num_start], length,
                                     start_line, start_column, start_pos);
                }
            } else if (tokenizer->input[tokenizer->position] == '<') {
                /// Check for N<& patterns first
                if (tokenizer->position + 1 < tokenizer->input_length &&
                    tokenizer->input[tokenizer->position + 1] == '&') {
                    /// Handle N<&M, N<&-, or N<&$VAR patterns (input fd
                    /// duplication)
                    if (tokenizer->position + 2 < tokenizer->input_length) {
                        char fd_char =
                            tokenizer->input[tokenizer->position + 2];
                        if (isdigit(fd_char) || fd_char == '-') {
                            tokenizer->position += 3; /// Skip <&M or <&-
                            tokenizer->column += 3;
                            size_t length = tokenizer->position - num_start;
                            return token_new(
                                TOK_REDIRECT_FD, &tokenizer->input[num_start],
                                length, start_line, start_column, start_pos);
                        }
                        /// Handle N<&$VAR or N<&${VAR} patterns
                        if (fd_char == '$') {
                            size_t fd_pos =
                                tokenizer->position + 3; /// After <&$
                            /// Handle ${...} brace form
                            if (fd_pos < tokenizer->input_length &&
                                tokenizer->input[fd_pos] == '{') {
                                fd_pos++; /// Skip {
                                int brace_depth = 1;
                                while (fd_pos < tokenizer->input_length &&
                                       brace_depth > 0) {
                                    if (tokenizer->input[fd_pos] == '{')
                                        brace_depth++;
                                    else if (tokenizer->input[fd_pos] == '}')
                                        brace_depth--;
                                    fd_pos++;
                                }
                            } else {
                                /// Simple $VAR form - scan
                                /// alphanumeric/underscore
                                while (fd_pos < tokenizer->input_length &&
                                       (isalnum(tokenizer->input[fd_pos]) ||
                                        tokenizer->input[fd_pos] == '_')) {
                                    fd_pos++;
                                }
                            }
                            size_t length = fd_pos - num_start;
                            tokenizer->position = fd_pos;
                            tokenizer->column += length;
                            return token_new(
                                TOK_REDIRECT_FD, &tokenizer->input[num_start],
                                length, start_line, start_column, start_pos);
                        }
                    }
                } else {
                    /// Handle N< (redirect input to file descriptor N)
                    tokenizer->position++;
                    tokenizer->column++;
                    size_t length = tokenizer->position - num_start;
                    return token_new(TOK_REDIRECT_IN_FD,
                                     &tokenizer->input[num_start], length,
                                     start_line, start_column, start_pos);
                }
            }
        }

        /// Not a redirection, reset position and treat as regular number
        tokenizer->position = num_start;
        tokenizer->column = start_column;
    }

    /// Typed-function return-kind arrow '->'. Match only when '-' and
    /// '>' are adjacent (no whitespace); the design specifies adjacent
    /// recognition so '-' followed by ' >' (with space) parses as a
    /// word '-' followed by the redirection operator, unchanged.
    if (c == '-' && tokenizer->position + 1 < tokenizer->input_length &&
        tokenizer->input[tokenizer->position + 1] == '>') {
        tokenizer->position += 2;
        tokenizer->column += 2;
        return token_new(TOK_ARROW, "->", 2, start_line, start_column,
                         start_pos);
    }

    /// Kind-sigil dispatch: when FEATURE_KIND_SIGILS is enabled and a `@` or
    /// `%` sits at the start of a fresh token followed immediately by a valid
    /// identifier-start character ([A-Za-z_]), tokenize it as a TOK_VARIABLE
    /// carrying the sigil prefix.  Other uses of `@` and `%` (mid-word
    /// `user@host`, extglob `@(foo|bar)`, git refs `@{-1}`, arithmetic `%`,
    /// job specifiers `%1`) all fail this regex check and fall through to the
    /// normal word/operator paths -- exactly the disambiguation the spec
    /// settled on.  Disabled in posix/bash/zsh modes so those profiles keep
    /// the historical word-character reading of `@` and `%`.
    if ((c == '@' || c == '%') && shell_mode_allows(FEATURE_KIND_SIGILS) &&
        tokenizer->position + 1 < tokenizer->input_length) {
        size_t start_n = lush_ident_match_start(
            &tokenizer->input[tokenizer->position + 1],
            tokenizer->input_length - tokenizer->position - 1);
        if (start_n > 0) {
            size_t sigil_start = tokenizer->position;
            size_t sigil_line = tokenizer->line;
            size_t sigil_col = tokenizer->column;
            tokenizer->position += 1 + start_n; /// sigil + first name codepoint
            tokenizer->column += 1 + start_n;
            while (tokenizer->position < tokenizer->input_length) {
                size_t n = lush_ident_match_continue(
                    &tokenizer->input[tokenizer->position],
                    tokenizer->input_length - tokenizer->position);
                if (n == 0) {
                    break;
                }
                tokenizer->position += n;
                tokenizer->column += n;
            }
            size_t length = tokenizer->position - sigil_start;
            return token_new(TOK_VARIABLE, &tokenizer->input[sigil_start],
                             length, sigil_line, sigil_col, sigil_start);
        }
    }

    /// Handle words and numbers (UTF-8 aware)
    /// First, try to decode the current character as UTF-8
    uint32_t codepoint;
    int char_len = lle_utf8_decode_codepoint(
        &tokenizer->input[tokenizer->position],
        tokenizer->input_length - tokenizer->position, &codepoint);

    /// Check if this could be the start of a word
    /// (either ASCII word char or non-ASCII UTF-8)
    /// Also handle backslash-escaped characters as word starters
    bool could_be_word = false;
    if (char_len > 0) {
        could_be_word = is_word_codepoint(codepoint);
        /// Backslash followed by another char starts a word (escape sequence)
        if (!could_be_word && codepoint == '\\' &&
            tokenizer->position + 1 < tokenizer->input_length) {
            could_be_word = true;
        }
    } else if (isalnum(c) || is_word_char(c)) {
        /// Fallback for byte-level check (ASCII)
        could_be_word = true;
    }

    if (could_be_word) {
        size_t start = tokenizer->position;
        bool is_numeric = (char_len == 1 && isdigit(c));

        /// Scan word character by character (UTF-8 aware)
        while (tokenizer->position < tokenizer->input_length) {
            /// Try to decode UTF-8 codepoint at current position
            uint32_t curr_codepoint;
            int curr_char_len = lle_utf8_decode_codepoint(
                &tokenizer->input[tokenizer->position],
                tokenizer->input_length - tokenizer->position, &curr_codepoint);

            if (curr_char_len > 0) {
                /// Valid UTF-8 character - check if it's a word character
                if (is_word_codepoint(curr_codepoint)) {
                    /// Special case: stop at ] ONLY inside array literals
                    /// For arr[n]=value, we want arr[n] as ONE token (element
                    /// assignment) For [idx]=value inside =(...), we want idx
                    /// as separate token The key distinction: if word started
                    /// right after [, break at ] Otherwise, keep ] as part of
                    /// the word
                    if (curr_codepoint == ']' &&
                        shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                        /// Only break if we started immediately after a [
                        /// (i.e., we're inside an array literal parsing
                        /// [idx]=value)
                        if (start > 0 && tokenizer->input[start - 1] == '[') {
                            /// Don't consume ], let it be tokenized separately
                            break;
                        }
                        /// Otherwise, include ] in word (for arr[n]=value
                        /// syntax)
                    }

                    /// Special case: stop at + if followed by = (for +=
                    /// operator) This allows arr+=(c d) to be parsed as arr
                    /// followed by +=
                    if (curr_codepoint == '+' &&
                        shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                        size_t next_pos = tokenizer->position + 1;
                        if (next_pos < tokenizer->input_length &&
                            tokenizer->input[next_pos] == '=') {
                            /// Stop here, don't include the +
                            break;
                        }
                    }

                    /// Check if still numeric (only single-byte ASCII digits
                    /// count)
                    if (curr_char_len > 1 ||
                        !isdigit(tokenizer->input[tokenizer->position])) {
                        is_numeric = false;
                    }

                    /// Advance by the UTF-8 character length
                    tokenizer->position += curr_char_len;
                    tokenizer->column++; /// One visual column per character
                } else if (curr_codepoint == '\\' &&
                           tokenizer->position + 1 < tokenizer->input_length) {
                    /// Backslash escape in word context
                    /// Include backslash + next character as part of word
                    tokenizer->position++; /// Skip backslash
                    tokenizer->column++;

                    char next = tokenizer->input[tokenizer->position];
                    if (next == '\n') {
                        /// Line continuation - skip the newline and continue
                        tokenizer->line++;
                        tokenizer->column = 1;
                        tokenizer->position++;
                    } else {
                        /// Include escaped character in word
                        tokenizer->position++;
                        tokenizer->column++;
                    }
                    is_numeric = false;
                    continue; /// Keep scanning for more word characters
                } else if (curr_codepoint == '(' &&
                           tokenizer->position > start) {
                    char prev = tokenizer->input[tokenizer->position - 1];

                    /// Check for array literal: var=(...)
                    if (prev == '=' &&
                        shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                        /// Scan to find matching closing paren
                        size_t scan_pos = tokenizer->position + 1;
                        int paren_depth = 1;
                        bool found_close = false;

                        while (scan_pos < tokenizer->input_length &&
                               paren_depth > 0) {
                            char sc = tokenizer->input[scan_pos];
                            if (sc == '(') {
                                paren_depth++;
                            } else if (sc == ')') {
                                paren_depth--;
                                if (paren_depth == 0) {
                                    found_close = true;
                                }
                            } else if (sc == '\\' &&
                                       scan_pos + 1 < tokenizer->input_length) {
                                scan_pos++; /// Skip escaped char
                            }
                            scan_pos++;
                        }

                        if (found_close) {
                            /// Include the array literal in the word
                            is_numeric = false;
                            size_t old_pos = tokenizer->position;
                            tokenizer->position = scan_pos;
                            tokenizer->column += (scan_pos - old_pos);
                            /// Continue scanning for more word chars
                            continue;
                        }
                    }
                    /// Check for extglob pattern: @(...), ?(...), *(...),
                    /// +(...), !(...)
                    else if (shell_mode_allows(FEATURE_EXTENDED_GLOB) &&
                             (prev == '@' || prev == '?' || prev == '*' ||
                              prev == '+' || prev == '!')) {
                        /// Scan to find matching closing paren
                        size_t scan_pos = tokenizer->position + 1;
                        int paren_depth = 1;
                        bool found_close = false;

                        while (scan_pos < tokenizer->input_length &&
                               paren_depth > 0) {
                            char sc = tokenizer->input[scan_pos];
                            if (sc == '(') {
                                paren_depth++;
                            } else if (sc == ')') {
                                paren_depth--;
                                if (paren_depth == 0) {
                                    found_close = true;
                                }
                            } else if (sc == '\\' &&
                                       scan_pos + 1 < tokenizer->input_length) {
                                scan_pos++; /// Skip escaped char
                            }
                            scan_pos++;
                        }

                        if (found_close) {
                            /// Include the extglob pattern in the word
                            is_numeric = false;
                            size_t old_pos = tokenizer->position;
                            tokenizer->position = scan_pos;
                            tokenizer->column += (scan_pos - old_pos);
                            /// Continue scanning for more word chars
                            continue;
                        }
                    }
                    /// Check for a trailing zsh glob qualifier:
                    /// `*.txt(N)`, `foo(.@)`, etc. A '(' that follows a
                    /// word character and encloses ONLY glob-qualifier
                    /// characters, with the matching ')' ending the
                    /// word, is part of the glob word -- not a subshell.
                    /// (`*(.N)` is already absorbed by the extglob
                    /// branch above via prev=='*'; this handles the
                    /// prefixed-pattern form the extglob branch misses.)
                    else if (shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
                        size_t scan_pos = tokenizer->position + 1;
                        bool only_qual = true;
                        bool found_close = false;
                        while (scan_pos < tokenizer->input_length) {
                            char sc = tokenizer->input[scan_pos];
                            if (sc == ')') {
                                found_close = true;
                                break;
                            }
                            if (!strchr("./@*rwND,", sc)) {
                                only_qual = false;
                                break;
                            }
                            scan_pos++;
                        }
                        if (found_close && only_qual &&
                            scan_pos > tokenizer->position + 1) {
                            /// The qualifier must end the word: after
                            /// ')' expect whitespace, EOF, or a token
                            /// boundary (incl. the ')' of an enclosing
                            /// array literal).
                            size_t after = scan_pos + 1;
                            char ac = (after < tokenizer->input_length)
                                          ? tokenizer->input[after]
                                          : '\0';
                            if (ac == '\0' || isspace((unsigned char)ac) ||
                                strchr(")|&;<>", ac)) {
                                is_numeric = false;
                                size_t old_pos = tokenizer->position;
                                tokenizer->position = after;
                                tokenizer->column += (after - old_pos);
                                continue;
                            }
                        }
                    }
                    /// Not an extglob pattern or array literal - end the word
                    /// here
                    break;
                } else if (curr_codepoint == '$') {
                    /// Check for ANSI-C quoting $'...' first - include as part
                    /// of word
                    if (tokenizer->position + 1 < tokenizer->input_length &&
                        tokenizer->input[tokenizer->position + 1] == '\'') {
                        /// Scan forward to find closing quote
                        size_t scan_pos = tokenizer->position + 2;
                        while (scan_pos < tokenizer->input_length) {
                            char sc = tokenizer->input[scan_pos];
                            if (sc == '\'') {
                                /// Found closing quote - include $'...' in word
                                scan_pos++;
                                is_numeric = false;
                                size_t old_pos = tokenizer->position;
                                tokenizer->position = scan_pos;
                                tokenizer->column += (scan_pos - old_pos);
                                continue; /// Continue scanning word
                            } else if (sc == '\\' &&
                                       scan_pos + 1 < tokenizer->input_length) {
                                scan_pos += 2; /// Skip escaped char
                            } else {
                                scan_pos++;
                            }
                        }
                        /// Unterminated ANSI-C quote - fall through to break
                    }

                    /// Check if we're inside brackets (array subscript like
                    /// arr[$i])
                    if (!shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
                        /// No array support - end word here
                        break;
                    }
                    /// Scan backwards to see if there's an unmatched '['
                    bool in_brackets = false;
                    for (size_t i = tokenizer->position; i > start; i--) {
                        if (tokenizer->input[i - 1] == '[') {
                            in_brackets = true;
                            break;
                        } else if (tokenizer->input[i - 1] == ']') {
                            break; /// Found a closing bracket first, not in
                                   /// brackets
                        }
                    }

                    if (in_brackets) {
                        /// Scan forward to find the closing bracket, including
                        /// the $ variable
                        size_t scan_pos = tokenizer->position + 1;

                        /// Skip past the variable name after $
                        /// Handle ${...}, $(...), $((...)), or simple $var
                        if (scan_pos < tokenizer->input_length) {
                            char nc = tokenizer->input[scan_pos];
                            if (nc == '{') {
                                /// ${...} - find matching }
                                int brace_depth = 1;
                                scan_pos++;
                                while (scan_pos < tokenizer->input_length &&
                                       brace_depth > 0) {
                                    if (tokenizer->input[scan_pos] == '{')
                                        brace_depth++;
                                    else if (tokenizer->input[scan_pos] == '}')
                                        brace_depth--;
                                    scan_pos++;
                                }
                            } else if (nc == '(') {
                                /// $(...) or $((...)) - find matching )
                                int paren_depth = 1;
                                scan_pos++;
                                while (scan_pos < tokenizer->input_length &&
                                       paren_depth > 0) {
                                    if (tokenizer->input[scan_pos] == '(')
                                        paren_depth++;
                                    else if (tokenizer->input[scan_pos] == ')')
                                        paren_depth--;
                                    scan_pos++;
                                }
                            } else if (nc == '?' || nc == '$' || nc == '!' ||
                                       nc == '@' || nc == '*' || nc == '#' ||
                                       isdigit((unsigned char)nc) ||
                                       lush_ident_match_start(
                                           &tokenizer->input[scan_pos],
                                           tokenizer->input_length - scan_pos) >
                                           0) {
                                /// Simple variable $var or special $?, $$, etc.
                                if (nc == '?' || nc == '$' || nc == '!' ||
                                    nc == '@' || nc == '*' || nc == '#') {
                                    scan_pos++; /// Single char special var
                                } else {
                                    /// Regular var - scan identifier codepoints
                                    while (scan_pos < tokenizer->input_length) {
                                        size_t n = lush_ident_match_continue(
                                            &tokenizer->input[scan_pos],
                                            tokenizer->input_length - scan_pos);
                                        if (n == 0) {
                                            break;
                                        }
                                        scan_pos += n;
                                    }
                                }
                            }
                        }

                        /// Now look for the closing ]
                        if (scan_pos < tokenizer->input_length &&
                            tokenizer->input[scan_pos] == ']') {
                            /// Include everything up to and including ]
                            scan_pos++;
                            is_numeric = false;
                            size_t old_pos = tokenizer->position;
                            tokenizer->position = scan_pos;
                            tokenizer->column += (scan_pos - old_pos);
                            /// Stop here - don't continue scanning
                            /// This allows = or += to be tokenized separately
                            break;
                        }
                    }
                    /// Not in array subscript context - end word here
                    break;
                } else if (curr_codepoint == '{' &&
                           shell_mode_allows(FEATURE_BRACE_EXPANSION)) {
                    /// Check if this is a brace expansion pattern embedded in a
                    /// word e.g., file{1..3}.txt or name{a,b,c}.log
                    size_t brace_start = tokenizer->position;
                    size_t scan_pos = brace_start + 1;
                    bool has_comma = false;
                    bool has_dotdot = false;
                    bool found_close = false;
                    int brace_depth = 1;

                    while (scan_pos < tokenizer->input_length &&
                           brace_depth > 0) {
                        char sc = tokenizer->input[scan_pos];

                        /// If whitespace right after {, not a brace expansion
                        if (scan_pos == brace_start + 1 &&
                            (sc == ' ' || sc == '\t' || sc == '\n')) {
                            break;
                        }

                        if (sc == '{') {
                            brace_depth++;
                        } else if (sc == '}') {
                            brace_depth--;
                            if (brace_depth == 0) {
                                found_close = true;
                            }
                        } else if (sc == ',' && brace_depth == 1) {
                            has_comma = true;
                        } else if (sc == '.' &&
                                   scan_pos + 1 < tokenizer->input_length &&
                                   tokenizer->input[scan_pos + 1] == '.' &&
                                   brace_depth == 1) {
                            has_dotdot = true;
                            scan_pos++; /// skip second dot
                        }
                        scan_pos++;
                    }

                    if (found_close && (has_comma || has_dotdot)) {
                        /// Valid brace expansion - include it in the word
                        is_numeric = false;
                        size_t brace_len = scan_pos - brace_start;
                        tokenizer->position = scan_pos;
                        tokenizer->column += brace_len;
                        /// Continue scanning for suffix (e.g., .txt after })
                    } else {
                        /// Not a brace expansion - end the word here
                        break;
                    }
                } else {
                    /// Not a word character - end of word
                    break;
                }
            } else {
                /// Invalid UTF-8 sequence - treat as end of word
                break;
            }
        }

        /// Check for glob qualifier suffix: (.) (/) (@) (*) (r) (w)
        /// Only if FEATURE_GLOB_QUALIFIERS is enabled and word contains glob
        /// chars
        if (shell_mode_allows(FEATURE_GLOB_QUALIFIERS)) {
            size_t word_len = tokenizer->position - start;
            bool has_glob_char = false;
            for (size_t i = 0; i < word_len; i++) {
                if (tokenizer->input[start + i] == '*' ||
                    tokenizer->input[start + i] == '?' ||
                    tokenizer->input[start + i] == '[') {
                    has_glob_char = true;
                    break;
                }
            }

            if (has_glob_char &&
                tokenizer->position + 2 < tokenizer->input_length &&
                tokenizer->input[tokenizer->position] == '(') {
                char qual = tokenizer->input[tokenizer->position + 1];
                if ((qual == '.' || qual == '/' || qual == '@' || qual == '*' ||
                     qual == 'r' || qual == 'w') &&
                    tokenizer->input[tokenizer->position + 2] == ')') {
                    /// Include the glob qualifier in the word token
                    tokenizer->position += 3;
                    tokenizer->column += 3;
                }
            }
        }

        size_t length = tokenizer->position - start;
        token_type_t type =
            is_numeric ? TOK_NUMBER
                       : classify_word(&tokenizer->input[start], length,
                                       tokenizer->enable_keywords);

        return token_new(type, &tokenizer->input[start], length, start_line,
                         start_column, start_pos);
    }

    /// Unknown character - treat as error
    tokenizer->position++;
    tokenizer->column++;
    return token_new(TOK_ERROR, &tokenizer->input[start_pos], 1, start_line,
                     start_column, start_pos);
}
