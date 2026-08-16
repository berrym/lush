/**
 * @file syntax_highlighting.h
 * @brief LLE Syntax Highlighting System
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides real-time syntax highlighting for shell command input with
 * theme integration. Core features:
 * - Shell-specific token classification (commands, builtins, keywords, etc.)
 * - Themeable color scheme
 * - Command/path existence validation for visual feedback
 * - Integration with LLE display system
 *
 * SPECIFICATION: docs/lle_specification/11_syntax_highlighting_complete.md
 */

#ifndef LLE_SYNTAX_HIGHLIGHTING_H
#define LLE_SYNTAX_HIGHLIGHTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/// TOKEN TYPES
/* ========================================================================== */

/**
 * @brief Shell-specific syntax token types
 */
typedef enum lle_syntax_token_type {
    LLE_TOKEN_UNKNOWN = 0, ///< Unknown/unclassified
    LLE_TOKEN_WHITESPACE,  ///< Whitespace

    /// Commands
    LLE_TOKEN_COMMAND_VALID,    ///< Valid external command
    LLE_TOKEN_COMMAND_INVALID,  ///< Non-existent command
    LLE_TOKEN_COMMAND_BUILTIN,  ///< Shell builtin (cd, echo, etc.)
    LLE_TOKEN_COMMAND_ALIAS,    ///< Defined alias
    LLE_TOKEN_COMMAND_FUNCTION, ///< Shell function

    /// Keywords
    LLE_TOKEN_KEYWORD, /**< Shell keyword (if, then, else, fi, for, while, do,
                          done, case, esac) */

    /// Assignment
    LLE_TOKEN_ASSIGNMENT, /**< Variable assignment (VAR=value, export VAR=val)
                           */

    /// Literals
    LLE_TOKEN_STRING_SINGLE,   ///< Single-quoted string
    LLE_TOKEN_STRING_DOUBLE,   ///< Double-quoted string
    LLE_TOKEN_STRING_BACKTICK, ///< Backtick command substitution
    LLE_TOKEN_NUMBER,          ///< Numeric literal

    /// Variables
    LLE_TOKEN_VARIABLE,         ///< Variable reference ($var, ${var})
    LLE_TOKEN_VARIABLE_SPECIAL, /**< Special variable ($?, $#, $@, $$, $!,
                                   $0-$9) */

    /* Paths -- two-axis classification.
     *
     * Shape (per spec S6.2): absolute (`/...`), relative (`./`, `../`,
     * bare with a slash), home (`~/...`).
     * Kind: regular file vs. directory, determined by stat() at
     * highlight time when validate_paths is on.
     *
     * The cross-product yields six (shape x kind) tokens; PATH_INVALID
     * is the seventh, reserved for paths that fail stat. Defaults
     * leave file-side coloring to fall through to ARGUMENT (no
     * highlight) and emphasize directories; themes can override every
     * shape x kind slot independently or use the kind-only fallbacks. */
    LLE_TOKEN_PATH_FILE_ABSOLUTE,
    LLE_TOKEN_PATH_FILE_RELATIVE,
    LLE_TOKEN_PATH_FILE_HOME,
    LLE_TOKEN_PATH_DIR_ABSOLUTE,
    LLE_TOKEN_PATH_DIR_RELATIVE,
    LLE_TOKEN_PATH_DIR_HOME,
    LLE_TOKEN_PATH_INVALID, ///< Non-existent path

    /// Operators
    LLE_TOKEN_PIPE,           ///< Pipe (|)
    LLE_TOKEN_REDIRECT,       ///< Redirection (>, <, >>, 2>, &>, etc.)
    LLE_TOKEN_BACKGROUND,     ///< Background (&)
    LLE_TOKEN_SEMICOLON,      ///< Command separator (;)
    LLE_TOKEN_AND,            ///< Logical AND (&&)
    LLE_TOKEN_OR,             ///< Logical OR (||)
    LLE_TOKEN_SUBSHELL_START, ///< Subshell start (
    LLE_TOKEN_SUBSHELL_END,   ///< Subshell end )
    LLE_TOKEN_BRACE_START,    ///< Brace group start {
    LLE_TOKEN_BRACE_END,      ///< Brace group end }

    /// Other
    LLE_TOKEN_COMMENT,  ///< Comment (# ...)
    LLE_TOKEN_OPTION,   ///< Command option (-v, --verbose)
    LLE_TOKEN_ARGUMENT, ///< Generic argument
    LLE_TOKEN_GLOB,     ///< Glob pattern (*, ?, [...])
    LLE_TOKEN_EXTGLOB, /**< Extended glob ?(pat), *(pat), +(pat), @(pat), !(pat)
                        */
    LLE_TOKEN_GLOB_QUAL, ///< Glob qualifier *(.), *(/)
    LLE_TOKEN_ESCAPE,    ///< Escape sequence (\x)

    /// Here-documents and here-strings
    LLE_TOKEN_HEREDOC_OP,      ///< Heredoc operator (<<, <<-, <<')
    LLE_TOKEN_HEREDOC_DELIM,   ///< Heredoc delimiter word
    LLE_TOKEN_HEREDOC_CONTENT, ///< Heredoc content (body)
    LLE_TOKEN_HERESTRING,      ///< Here-string (<<<)

    /// Process substitution
    LLE_TOKEN_PROCSUB_IN,  ///< Process substitution input <(...)
    LLE_TOKEN_PROCSUB_OUT, ///< Process substitution output >(...)

    /// ANSI-C quoting
    LLE_TOKEN_STRING_ANSIC, ///< ANSI-C quoted string $'...'

    /// Arithmetic expansion
    LLE_TOKEN_ARITHMETIC, ///< Arithmetic expansion $((...))

    /// Errors
    LLE_TOKEN_ERROR,             ///< Syntax error
    LLE_TOKEN_UNCLOSED_STRING,   ///< Unclosed quote
    LLE_TOKEN_UNCLOSED_SUBSHELL, ///< Unclosed subshell/brace

    LLE_TOKEN_TYPE_COUNT ///< Number of token types
} lle_syntax_token_type_t;

/* ========================================================================== */
/// COLOR SCHEME
/* ========================================================================== */

/**
 * @brief Syntax highlighting color scheme
 *
 * Colors are stored as RGB uint32_t (0x00RRGGBB format).
 * A value of 0 means "use default/inherited color".
 */
typedef struct lle_syntax_colors {
    /// Commands
    uint32_t command_valid;    ///< Valid external command (typically green)
    uint32_t command_invalid;  ///< Invalid command (typically red)
    uint32_t command_builtin;  ///< Shell builtin (typically cyan)
    uint32_t command_alias;    ///< Alias (typically cyan)
    uint32_t command_function; ///< Function (typically blue)

    /// Keywords
    uint32_t keyword; ///< Shell keywords (typically blue/magenta)

    /// Strings
    uint32_t string;        ///< Quoted strings (typically yellow)
    uint32_t string_escape; ///< Escape sequences in strings

    /// Variables
    uint32_t variable;         ///< Variables (typically magenta/purple)
    uint32_t variable_special; ///< Special variables

    /* Paths -- shape x kind grid with kind-only fallbacks.
     *
     * Resolution at color-application time:
     *   PATH_FILE_<SHAPE>  -> path_file_<shape> if non-zero
     *                      -> else path_file    if non-zero
     *                      -> else 0 (token demoted to ARGUMENT,
     *                                 i.e. default text -- no highlight)
     *   PATH_DIR_<SHAPE>   -> path_dir_<shape>  if non-zero
     *                      -> else path_dir     if non-zero
     *                      -> else 0 (token demoted to ARGUMENT)
     *   PATH_INVALID       -> path_invalid (no fallback chain)
     *
     * A value of 0 means "unset, fall through" -- consistent with the
     * struct's standing convention. Most themes will set only the
     * three base knobs (path_file, path_dir, path_invalid); the six
     * shape-specific slots exist for power-user themes. Defaults ship
     * file-side unset (so valid files stay unhighlighted), dir-side
     * pale-green-underlined, invalid red-underlined. */
    uint32_t path_file_absolute;
    uint32_t path_file_relative;
    uint32_t path_file_home;
    uint32_t path_dir_absolute;
    uint32_t path_dir_relative;
    uint32_t path_dir_home;
    uint32_t path_file;    ///< Kind-only fallback for any file path
    uint32_t path_dir;     ///< Kind-only fallback for any directory path
    uint32_t path_invalid; ///< Non-existent paths (no fallback)

    /// Operators
    uint32_t pipe;           ///< Pipe operator
    uint32_t redirect;       ///< Redirection operators
    uint32_t operator_other; ///< Other operators (&, ;, &&, ||)

    /// Assignment
    uint32_t assignment; ///< Variable assignment (VAR=value)

    /// Other
    uint32_t comment;   ///< Comments (typically dim/gray)
    uint32_t number;    ///< Numbers (typically cyan)
    uint32_t option;    ///< Options (typically white/default)
    uint32_t glob;      ///< Glob patterns
    uint32_t extglob;   ///< Extended glob patterns ?(pat), *(pat), etc.
    uint32_t glob_qual; ///< Glob qualifiers *(.), *(/)
    uint32_t argument;  ///< Generic arguments (default)

    /// Here-documents and here-strings
    uint32_t heredoc_op;      ///< Heredoc operator (<<)
    uint32_t heredoc_delim;   ///< Heredoc delimiter
    uint32_t heredoc_content; ///< Heredoc body content
    uint32_t herestring;      ///< Here-string (<<<word)

    /// Process substitution
    uint32_t procsub; ///< Process substitution <() and >()

    /// ANSI-C quoting
    uint32_t string_ansic; ///< ANSI-C string $'...'

    /// Arithmetic expansion
    uint32_t arithmetic; ///< Arithmetic $((...))

    /// Errors
    uint32_t error;    ///< Syntax errors (typically red bg)
    uint32_t error_fg; ///< Error foreground (typically white)

    /// Text attributes (bitmask)
    uint8_t keyword_bold;    ///< Bold keywords
    uint8_t command_bold;    ///< Bold valid commands
    uint8_t error_underline; ///< Underline errors
    uint8_t path_underline;  ///< Underline paths
    uint8_t comment_dim;     ///< Dim comments
} lle_syntax_colors_t;

/* ========================================================================== */
/// TOKEN
/* ========================================================================== */

/**
 * @brief A single syntax token
 */
typedef struct lle_syntax_token {
    lle_syntax_token_type_t type; ///< Token type
    size_t start;                 ///< Start position in input
    size_t end;                   ///< End position (exclusive)
    uint32_t color;               ///< Resolved color for this token
    uint8_t attributes;           ///< Text attributes (bold, underline, dim)
} lle_syntax_token_t;

/// Text attribute flags
#define LLE_ATTR_NONE 0x00
#define LLE_ATTR_BOLD 0x01
#define LLE_ATTR_DIM 0x02
#define LLE_ATTR_ITALIC 0x04
#define LLE_ATTR_UNDERLINE 0x08
#define LLE_ATTR_BLINK 0x10
#define LLE_ATTR_REVERSE 0x20

/* ========================================================================== */
/// HIGHLIGHTER CONTEXT
/* ========================================================================== */

/**
 * @brief Syntax highlighter context
 */
typedef struct lle_syntax_highlighter {
    /// Color scheme
    lle_syntax_colors_t colors;

    /// Token buffer
    lle_syntax_token_t *tokens;
    size_t token_count;
    size_t token_capacity;

    /// Configuration
    bool enabled;
    bool validate_commands; ///< Check if commands exist
    bool validate_paths;    ///< Check if paths exist
    bool highlight_errors;  ///< Highlight syntax errors

    /// Terminal capabilities
    int color_depth; ///< 0=none, 1=8, 2=256, 3=truecolor

    /* Caches for filesystem lookups -- opaque to keep the public ABI
     * insulated from the cache layout. The path cache is a separate
     * structure from the command cache because they age independently
     * (PATH changes invalidate one, cwd changes invalidate the other)
     * and because keying them in the same table would conflate the
     * two namespaces (`bash` the command vs. `bash` the file). */
    void *command_cache; ///< Opaque pointer to cmd_cache_t
    void *path_cache;    ///< Opaque pointer to path_cache_t
} lle_syntax_highlighter_t;

/* ========================================================================== */
/// API
/* ========================================================================== */

/**
 * @brief Create a syntax highlighter
 * @param highlighter Output pointer
 * @return 0 on success, error code on failure
 */
int lle_syntax_highlighter_create(lle_syntax_highlighter_t **highlighter);

/**
 * @brief Destroy a syntax highlighter
 * @param highlighter Highlighter to destroy
 */
void lle_syntax_highlighter_destroy(lle_syntax_highlighter_t *highlighter);

/**
 * @brief Set color scheme
 * @param highlighter Highlighter context
 * @param colors Color scheme to use (copied)
 */
void lle_syntax_highlighter_set_colors(lle_syntax_highlighter_t *highlighter,
                                       const lle_syntax_colors_t *colors);

/**
 * @brief Get default color scheme
 * @param colors Output color scheme
 */
void lle_syntax_colors_get_default(lle_syntax_colors_t *colors);

/**
 * @brief Tokenize and highlight a command line
 * @param highlighter Highlighter context
 * @param input Command line to tokenize
 * @param input_len Length of input
 * @return Number of tokens, or -1 on error
 */
int lle_syntax_highlight(lle_syntax_highlighter_t *highlighter,
                         const char *input, size_t input_len);

/**
 * @brief Get tokens from last highlight operation
 * @param highlighter Highlighter context
 * @param count Output: number of tokens
 * @return Pointer to token array (valid until next highlight call)
 */
const lle_syntax_token_t *
lle_syntax_get_tokens(lle_syntax_highlighter_t *highlighter, size_t *count);

/**
 * @brief Generate ANSI-colored output string
 * @param highlighter Highlighter context
 * @param input Original input string
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Length of output, or -1 on error
 */
int lle_syntax_render_ansi(lle_syntax_highlighter_t *highlighter,
                           const char *input, char *output, size_t output_size);

/**
 * @brief Convert RGB color to ANSI escape sequence
 * @param color RGB color (0x00RRGGBB)
 * @param attributes Text attributes
 * @param color_depth Terminal color depth (1=8, 2=256, 3=truecolor)
 * @param output Output buffer (should be at least 32 bytes)
 * @param output_size Size of output buffer
 * @return Length of escape sequence
 */
int lle_syntax_color_to_ansi(uint32_t color, uint8_t attributes,
                             int color_depth, char *output, size_t output_size);

/**
 * @brief Check if a command exists
 * @param highlighter Highlighter context (uses cache)
 * @param command Command name
 * @return Token type (COMMAND_VALID, COMMAND_BUILTIN, COMMAND_ALIAS, or
 * COMMAND_INVALID)
 */
lle_syntax_token_type_t
lle_syntax_check_command(lle_syntax_highlighter_t *highlighter,
                         const char *command);

/**
 * @brief Clear command existence cache
 * @param highlighter Highlighter context
 */
void lle_syntax_clear_cache(lle_syntax_highlighter_t *highlighter);

#ifdef __cplusplus
}
#endif

#endif /// LLE_SYNTAX_HIGHLIGHTING_H
