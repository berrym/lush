/**
 * @file syntax_highlighting.c
 * @brief LLE Syntax Highlighting Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides real-time syntax highlighting for shell command input.
 * Core implementation focusing on:
 * - Shell lexer for tokenization
 * - Command/path existence validation
 * - Theme-based color application
 * - ANSI escape sequence generation
 *
 * SPECIFICATION: docs/lle_specification/11_syntax_highlighting_complete.md
 */

#include "lle/syntax_highlighting.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Include lush headers for command/alias/builtin checks */
#include "alias.h"
#include "builtins.h"
#include "lle/adaptive_terminal_integration.h"

/* Weak symbol for function lookup - overridden in full shell build */
__attribute__((weak)) bool lle_shell_function_exists(const char *name) {
    (void)name;
    return false;  /* Default: no functions in standalone LLE */
}

/* ========================================================================== */
/*                         DEFAULT COLOR SCHEME                               */
/* ========================================================================== */

/** @brief Default colors (vivid dark theme, derived from proven 256-color palette) */
static const lle_syntax_colors_t default_colors = {
    /* Commands */
    .command_valid = 0x005FFF00,    /* Bright lime green (256: 82) */
    .command_invalid = 0x00FF0000,  /* Bright red (256: 196) */
    .command_builtin = 0x0000FFFF,  /* Bright cyan (256: 51) */
    .command_alias = 0x0000FFFF,    /* Bright cyan (256: 51) */
    .command_function = 0x005F87FF, /* Steel blue (256: 69) */

    /* Keywords */
    .keyword = 0x005FAFFF, /* Sky blue (256: 75) */

    /* Strings */
    .string = 0x00FFAF00,        /* Amber (256: 214) */
    .string_escape = 0x00FF5F5F, /* Salmon (256: 203) */

    /* Variables */
    .variable = 0x00D787FF,         /* Light purple (256: 177) */
    .variable_special = 0x00FF87FF, /* Bright orchid */

    /* Paths */
    .path_valid = 0x0087D787,   /* Pale green (256: 114) */
    .path_invalid = 0x00FF0000, /* Bright red (256: 196) */

    /* Operators */
    .pipe = 0x005F87FF,           /* Steel blue (256: 69) */
    .redirect = 0x00FF5F5F,       /* Salmon (256: 203) */
    .operator_other = 0x00D0D0D0, /* Light gray (256: 252) */

    /* Assignment */
    .assignment = 0x00D787FF, /* Light purple (same as variable) */

    /* Other */
    .comment = 0x00808080,   /* Gray (256: 244) */
    .number = 0x00FFFFAF,    /* Light yellow (256: 229) */
    .option = 0x0087D7D7,    /* Muted cyan (256: 116) */
    .glob = 0x00FFAF00,      /* Amber (same as string) */
    .extglob = 0x00FFAF00,   /* Amber (same as string) */
    .glob_qual = 0x00D787FF, /* Light purple */
    .argument = 0x00D0D0D0,  /* Light gray (256: 252) */

    /* Here-documents and here-strings */
    .heredoc_op = 0x00FF5F5F,      /* Salmon (same as redirect) */
    .heredoc_delim = 0x00FFAF00,   /* Amber (same as string) */
    .heredoc_content = 0x00FFAF00, /* Amber (same as string) */
    .herestring = 0x00FFAF00,      /* Amber (same as string) */

    /* Process substitution */
    .procsub = 0x00FF5F5F, /* Salmon */

    /* ANSI-C quoting */
    .string_ansic = 0x00FFAF00, /* Amber (same as string) */

    /* Arithmetic expansion */
    .arithmetic = 0x0000FFFF, /* Bright cyan (256: 51) */

    /* Errors */
    .error = 0x00FF0000,    /* Bright red (256: 196) */
    .error_fg = 0x00FFFFFF, /* White */

    /* Attributes */
    .keyword_bold = 1,
    .command_bold = 1,
    .error_underline = 1,
    .path_underline = 1,
    .comment_dim = 1,
};

/* ========================================================================== */
/*                         COMMAND CACHE                                      */
/* ========================================================================== */

#define CMD_CACHE_SIZE 128
#define CMD_CACHE_TTL 30 /* seconds */

/**
 * @brief Command cache entry structure
 *
 * Stores a cached command lookup result with its type and timestamp
 * for cache expiration checking.
 */
typedef struct cmd_cache_entry {
    char *command;                  /**< @brief Command string (heap-allocated) */
    lle_syntax_token_type_t type;   /**< @brief Cached token type result */
    time_t timestamp;               /**< @brief Cache entry creation timestamp */
} cmd_cache_entry_t;

/**
 * @brief Command cache structure
 *
 * Hash-based cache for command existence lookups to avoid
 * repeated PATH searches for frequently used commands.
 */
typedef struct cmd_cache {
    cmd_cache_entry_t entries[CMD_CACHE_SIZE];  /**< @brief Cache entries array */
} cmd_cache_t;

/**
 * @brief Hash a string for cache lookup
 * @param str String to hash
 * @return Hash value modulo CMD_CACHE_SIZE
 */
static unsigned int hash_string(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % CMD_CACHE_SIZE;
}

/* ========================================================================== */
/*                         SHELL KEYWORDS                                     */
/* ========================================================================== */

/** @brief Shell keywords for syntax highlighting */
static const char *shell_keywords[] = {
    "if",     "then",  "else",  "elif", "fi",   "for",    "in",       "do",
    "done",   "while", "until", "case", "esac", "select", "function", "time",
    "coproc", "!",     "{",     "}",    "[[",   "]]",     NULL};

/** @brief Keywords that END blocks - these don't expect a command after them */
static const char *block_ending_keywords[] = {"fi", "done", "esac",
                                              "}",  "]]",   NULL};

/**
 * @brief Check if a word is a shell keyword
 * @param word Pointer to the word
 * @param len Length of the word
 * @return true if the word is a shell keyword
 */
static bool is_shell_keyword(const char *word, size_t len) {
    for (int i = 0; shell_keywords[i]; i++) {
        if (strlen(shell_keywords[i]) == len &&
            strncmp(word, shell_keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if a word is a block-ending keyword
 * @param word Pointer to the word
 * @param len Length of the word
 * @return true if the word ends a block (fi, done, esac, etc.)
 */
static bool is_block_ending_keyword(const char *word, size_t len) {
    for (int i = 0; block_ending_keywords[i]; i++) {
        if (strlen(block_ending_keywords[i]) == len &&
            strncmp(word, block_ending_keywords[i], len) == 0) {
            return true;
        }
    }
    return false;
}

/* ========================================================================== */
/*                         SPECIAL VARIABLES                                  */
/* ========================================================================== */

/** @brief Hook array variable names that get special highlighting */
static const char *hook_array_variables[] = {
    "precmd_functions",   /* Zsh: functions called before prompt */
    "preexec_functions",  /* Zsh: functions called before command exec */
    "chpwd_functions",    /* Zsh: functions called after directory change */
    "periodic_functions", /* Zsh: functions called periodically */
    "precmd",             /* Simple hook array (FEATURE_SIMPLE_HOOK_ARRAYS) */
    "preexec",            /* Simple hook array */
    "chpwd",              /* Simple hook array */
    "PROMPT_COMMAND",     /* Bash: command/array run before prompt */
    NULL
};

/**
 * @brief Check if a variable name is a hook array (special variable)
 * @param name Variable name (without $ prefix)
 * @param len Length of the variable name
 * @return true if the variable is a hook array
 */
static bool is_hook_array_variable(const char *name, size_t len) {
    for (int i = 0; hook_array_variables[i]; i++) {
        if (strlen(hook_array_variables[i]) == len &&
            strncmp(name, hook_array_variables[i], len) == 0) {
            return true;
        }
    }
    return false;
}

/* ========================================================================== */
/*                         LEXER HELPERS                                      */
/* ========================================================================== */

/**
 * @brief Check if a character is part of a word token
 * @param c Character to check
 * @return true if the character can be part of a word
 */
static bool is_word_char(char c) {
    unsigned char uc = (unsigned char)c;

    /* UTF-8 continuation bytes (10xxxxxx) and lead bytes (11xxxxxx) are part of
     * words. This ensures multi-byte UTF-8 characters like 'é' (0xC3 0xA9) are
     * not split. */
    if (uc >= 0x80) {
        return true; /* Any non-ASCII byte is part of a word */
    }

    return isalnum(uc) || c == '_' || c == '-' || c == '.' || c == '/' ||
           c == '~' || c == '+' || c == '@' || c == ':' || c == '=';
}

/**
 * @brief Check if a string starts with an option flag
 * @param s Pointer to string
 * @param remaining Remaining characters in string
 * @return true if string starts with - or --
 */
static bool is_option_start(const char *s, size_t remaining) {
    return remaining >= 2 && s[0] == '-' &&
           (isalnum((unsigned char)s[1]) || s[1] == '-');
}

/* is_variable_start - currently inlined in tokenizer, kept for future use */
#if 0
static bool is_variable_start(char c) {
    return c == '$';
}
#endif

/**
 * @brief Check if a character is a glob metacharacter
 * @param c Character to check
 * @return true if the character is *, ?, or [
 */
static bool is_glob_char(char c) { return c == '*' || c == '?' || c == '['; }

/**
 * @brief Check if a character introduces an extended glob pattern
 * @param c Character to check
 * @return true if the character is ?, *, +, @, or !
 *
 * Extended globs: ?(pat), *(pat), +(pat), @(pat), !(pat)
 */
static bool is_extglob_prefix(char c) {
    return c == '?' || c == '*' || c == '+' || c == '@' || c == '!';
}

/**
 * @brief Check if position starts an extended glob pattern
 * @param input Input string
 * @param pos Current position
 * @param len Total length
 * @return true if this is an extended glob like ?(, *(, +(, @(, !(
 */
static bool is_extglob_start(const char *input, size_t pos, size_t len) {
    if (pos + 1 >= len) return false;
    return is_extglob_prefix(input[pos]) && input[pos + 1] == '(';
}

/**
 * @brief Check if position starts a glob qualifier
 * @param input Input string
 * @param pos Current position
 * @param len Total length
 * @return true if this is a glob qualifier like *(.) or *(/)
 *
 * Zsh-style glob qualifiers: *(.) for files, *(/) for dirs, *(@) for symlinks
 */
static bool is_glob_qualifier(const char *input, size_t pos, size_t len) {
    /* Must have *( followed by single char and ) */
    if (pos + 3 >= len) return false;
    if (input[pos] != '*' || input[pos + 1] != '(') return false;
    /* Check for single-char qualifier: ., /, @, *, etc. */
    char qual = input[pos + 2];
    if (input[pos + 3] == ')' && 
        (qual == '.' || qual == '/' || qual == '@' || qual == '*' ||
         qual == 'r' || qual == 'w' || qual == 'x')) {
        return true;
    }
    return false;
}

/**
 * @brief Check if a word is a variable assignment (VAR=value pattern)
 *
 * Valid variable names start with letter or underscore, followed by
 * alphanumeric or underscore, then '='
 *
 * @param word Pointer to the word
 * @param len Length of the word
 * @return true if the word matches VAR=value pattern
 */
static bool is_assignment(const char *word, size_t len) {
    if (len < 2)
        return false; /* Minimum: "x=" */

    /* Find the '=' */
    const char *eq = memchr(word, '=', len);
    if (!eq)
        return false;

    /* Variable name must be before the '=' */
    size_t name_len = eq - word;
    if (name_len == 0)
        return false;

    /* First char must be letter or underscore */
    if (!isalpha((unsigned char)word[0]) && word[0] != '_') {
        return false;
    }

    /* Rest must be alphanumeric or underscore */
    for (size_t i = 1; i < name_len; i++) {
        if (!isalnum((unsigned char)word[i]) && word[i] != '_') {
            return false;
        }
    }

    return true;
}

/**
 * @brief Skip whitespace characters in input
 * @param input Input string
 * @param pos Current position
 * @param len Total length of input
 * @return New position after skipping whitespace
 */
static size_t skip_whitespace(const char *input, size_t pos, size_t len) {
    while (pos < len && isspace((unsigned char)input[pos])) {
        pos++;
    }
    return pos;
}

/* ========================================================================== */
/*                         COMMAND CHECKING                                   */
/* ========================================================================== */

/* Use is_builtin() from builtins.h (already included above) */
/* Use is_alias() from alias.h (already included above) */

/**
 * @brief Check if a path exists on the filesystem
 *
 * Forward declaration - implementation below tokenizer section.
 *
 * @param path Path to check
 * @return true if path exists, false otherwise
 */
static bool path_exists(const char *path);

/**
 * @brief Check if a command exists in PATH
 * @param command Command name to check
 * @return true if command is found and executable
 */
static bool command_exists_in_path(const char *command) {
    /* Check if command contains a path separator */
    if (strchr(command, '/')) {
        return access(command, X_OK) == 0;
    }

    const char *path_env = getenv("PATH");
    if (!path_env)
        return false;

    char *path_copy = strdup(path_env);
    if (!path_copy)
        return false;

    bool found = false;
    char *dir = strtok(path_copy, ":");

    while (dir && !found) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);
        if (access(full_path, X_OK) == 0) {
            found = true;
        }
        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return found;
}

lle_syntax_token_type_t
lle_syntax_check_command(lle_syntax_highlighter_t *highlighter,
                         const char *command) {
    if (!command || !*command) {
        return LLE_TOKEN_UNKNOWN;
    }

    /* Check cache first */
    if (highlighter && highlighter->command_cache) {
        cmd_cache_t *cache = (cmd_cache_t *)highlighter->command_cache;
        unsigned int idx = hash_string(command);
        cmd_cache_entry_t *entry = &cache->entries[idx];

        if (entry->command && strcmp(entry->command, command) == 0) {
            time_t now = time(NULL);
            if (now - entry->timestamp < CMD_CACHE_TTL) {
                return entry->type;
            }
        }
    }

    /* Check command type */
    lle_syntax_token_type_t type;

    if (is_builtin(command)) {
        type = LLE_TOKEN_COMMAND_BUILTIN;
    } else if (lookup_alias(command) != NULL) {
        type = LLE_TOKEN_COMMAND_ALIAS;
    } else if (lle_shell_function_exists(command)) {
        type = LLE_TOKEN_COMMAND_FUNCTION;
    } else if (command[0] == '/' || command[0] == '.') {
        /* Absolute or relative path - check if file exists */
        type = path_exists(command) ? LLE_TOKEN_COMMAND_VALID
                                    : LLE_TOKEN_COMMAND_INVALID;
    } else if (command[0] == '~') {
        /* Home directory path - expand and check */
        const char *home = getenv("HOME");
        if (home) {
            char expanded[4096];
            snprintf(expanded, sizeof(expanded), "%s%s", home, command + 1);
            type = path_exists(expanded) ? LLE_TOKEN_COMMAND_VALID
                                         : LLE_TOKEN_COMMAND_INVALID;
        } else {
            type = LLE_TOKEN_COMMAND_INVALID;
        }
    } else if (command[0] == '$' && strchr(command, '/')) {
        /* Variable path (e.g., $HOME/bin/script) - expand and check */
        char expanded[4096];
        const char *var_start = command + 1;
        const char *var_end = var_start;
        const char *rest = NULL;

        if (var_start[0] == '{') {
            /* ${VAR} format */
            var_start++;
            var_end = strchr(var_start, '}');
            if (var_end) {
                rest = var_end + 1;
            }
        } else {
            /* $VAR format */
            while (*var_end && (isalnum((unsigned char)*var_end) ||
                                *var_end == '_')) {
                var_end++;
            }
            rest = var_end;
        }

        if (var_end && var_end > var_start && rest) {
            size_t var_len = (size_t)(var_end - var_start);
            char var_name[256];
            if (var_len < sizeof(var_name)) {
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';
                const char *value = getenv(var_name);
                if (value) {
                    snprintf(expanded, sizeof(expanded), "%s%s", value, rest);
                    type = path_exists(expanded) ? LLE_TOKEN_COMMAND_VALID
                                                 : LLE_TOKEN_COMMAND_INVALID;
                } else {
                    type = LLE_TOKEN_COMMAND_INVALID;
                }
            } else {
                type = LLE_TOKEN_COMMAND_INVALID;
            }
        } else {
            type = LLE_TOKEN_COMMAND_INVALID;
        }
    } else if (command_exists_in_path(command)) {
        type = LLE_TOKEN_COMMAND_VALID;
    } else {
        type = LLE_TOKEN_COMMAND_INVALID;
    }

    /* Update cache */
    if (highlighter && highlighter->command_cache) {
        cmd_cache_t *cache = (cmd_cache_t *)highlighter->command_cache;
        unsigned int idx = hash_string(command);
        cmd_cache_entry_t *entry = &cache->entries[idx];

        free(entry->command);
        entry->command = strdup(command);
        entry->type = type;
        entry->timestamp = time(NULL);
    }

    return type;
}

/**
 * @brief Check if a path exists on the filesystem
 * @param path Path to check
 * @return true if path exists
 */
static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ========================================================================== */
/*                         TOKENIZER                                          */
/* ========================================================================== */

/**
 * @brief Ensure token array has sufficient capacity
 * @param h Highlighter instance
 * @param needed Minimum number of tokens needed
 * @return 0 on success, -1 on allocation failure
 */
static int ensure_token_capacity(lle_syntax_highlighter_t *h, size_t needed) {
    if (needed <= h->token_capacity)
        return 0;

    size_t new_cap = h->token_capacity ? h->token_capacity * 2 : 32;
    while (new_cap < needed)
        new_cap *= 2;

    lle_syntax_token_t *new_tokens =
        realloc(h->tokens, new_cap * sizeof(lle_syntax_token_t));
    if (!new_tokens)
        return -1;

    h->tokens = new_tokens;
    h->token_capacity = new_cap;
    return 0;
}

/**
 * @brief Add a token to the highlighter's token list
 * @param h Highlighter instance
 * @param type Token type
 * @param start Start byte offset in input
 * @param end End byte offset in input
 * @return 0 on success, -1 on allocation failure
 */
static int add_token(lle_syntax_highlighter_t *h, lle_syntax_token_type_t type,
                     size_t start, size_t end) {
    if (ensure_token_capacity(h, h->token_count + 1) < 0)
        return -1;

    lle_syntax_token_t *tok = &h->tokens[h->token_count++];
    tok->type = type;
    tok->start = start;
    tok->end = end;
    tok->color = 0;
    tok->attributes = LLE_ATTR_NONE;

    return 0;
}

/**
 * @brief Tokenize and highlight shell input
 *
 * Parses the input string and generates syntax tokens with appropriate
 * types and colors based on shell syntax rules.
 *
 * @param highlighter Highlighter instance
 * @param input Input string to highlight
 * @param input_len Length of input in bytes
 * @return Number of tokens generated, or -1 on error
 */
int lle_syntax_highlight(lle_syntax_highlighter_t *highlighter,
                         const char *input, size_t input_len) {
    if (!highlighter || !input)
        return -1;

    highlighter->token_count = 0;

    size_t pos = 0;
    bool expect_command = true;      /* Next word is a command */
    bool after_function_keyword = false; /* Previous token was 'function' keyword */

    while (pos < input_len) {
        /* Skip whitespace */
        size_t ws_start = pos;
        pos = skip_whitespace(input, pos, input_len);
        if (pos > ws_start) {
            add_token(highlighter, LLE_TOKEN_WHITESPACE, ws_start, pos);
            /* Check if whitespace contained a newline - new line = new command
             */
            for (size_t i = ws_start; i < pos; i++) {
                if (input[i] == '\n') {
                    expect_command = true;
                    break;
                }
            }
        }
        if (pos >= input_len)
            break;

        char c = input[pos];
        size_t token_start = pos;

        /* Comment */
        if (c == '#') {
            while (pos < input_len && input[pos] != '\n')
                pos++;
            add_token(highlighter, LLE_TOKEN_COMMENT, token_start, pos);
            continue;
        }

        /* Single-quoted string */
        if (c == '\'') {
            pos++;
            while (pos < input_len && input[pos] != '\'')
                pos++;
            if (pos < input_len)
                pos++; /* Skip closing quote */
            add_token(highlighter,
                      pos <= input_len && input[pos - 1] == '\''
                          ? LLE_TOKEN_STRING_SINGLE
                          : LLE_TOKEN_UNCLOSED_STRING,
                      token_start, pos);
            expect_command = false;
            continue;
        }

        /* Double-quoted string */
        if (c == '"') {
            pos++;
            while (pos < input_len && input[pos] != '"') {
                if (input[pos] == '\\' && pos + 1 < input_len)
                    pos++;
                pos++;
            }
            if (pos < input_len)
                pos++; /* Skip closing quote */
            add_token(highlighter,
                      pos <= input_len && input[pos - 1] == '"'
                          ? LLE_TOKEN_STRING_DOUBLE
                          : LLE_TOKEN_UNCLOSED_STRING,
                      token_start, pos);
            expect_command = false;
            continue;
        }

        /* Backtick command substitution */
        if (c == '`') {
            pos++;
            while (pos < input_len && input[pos] != '`') {
                if (input[pos] == '\\' && pos + 1 < input_len)
                    pos++;
                pos++;
            }
            if (pos < input_len)
                pos++;
            add_token(highlighter, LLE_TOKEN_STRING_BACKTICK, token_start, pos);
            expect_command = false;
            continue;
        }

        /* Variable and $-prefixed constructs */
        if (c == '$') {
            pos++;
            if (pos < input_len) {
                char next = input[pos];
                lle_syntax_token_type_t vtype = LLE_TOKEN_VARIABLE;

                /* ANSI-C quoting: $'...' */
                if (next == '\'') {
                    pos++; /* Skip opening quote */
                    while (pos < input_len && input[pos] != '\'') {
                        /* Handle escape sequences in ANSI-C strings */
                        if (input[pos] == '\\' && pos + 1 < input_len) {
                            pos++;
                        }
                        pos++;
                    }
                    if (pos < input_len)
                        pos++; /* Skip closing quote */
                    add_token(highlighter,
                              pos <= input_len && input[pos - 1] == '\''
                                  ? LLE_TOKEN_STRING_ANSIC
                                  : LLE_TOKEN_UNCLOSED_STRING,
                              token_start, pos);
                    expect_command = false;
                    continue;
                }
                /* Arithmetic expansion: $((...)) */
                else if (next == '(' && pos + 1 < input_len &&
                         input[pos + 1] == '(') {
                    pos += 2; /* Skip (( */
                    int depth = 1;
                    while (pos < input_len && depth > 0) {
                        if (pos + 1 < input_len && input[pos] == '(' &&
                            input[pos + 1] == '(') {
                            depth++;
                            pos++;
                        } else if (pos + 1 < input_len && input[pos] == ')' &&
                                   input[pos + 1] == ')') {
                            depth--;
                            pos++;
                        }
                        pos++;
                    }
                    add_token(highlighter, LLE_TOKEN_ARITHMETIC, token_start,
                              pos);
                    expect_command = false;
                    continue;
                }
                /* Special variables */
                else if (next == '?' || next == '#' || next == '@' ||
                         next == '*' || next == '$' || next == '!' ||
                         next == '-' || next == '_' ||
                         (next >= '0' && next <= '9')) {
                    pos++;
                    vtype = LLE_TOKEN_VARIABLE_SPECIAL;
                }
                /* ${...} brace expansion - extract variable name for hook check */
                else if (next == '{') {
                    pos++; /* Skip { */
                    size_t var_name_start = pos;
                    /* Scan to find end of variable name (before : or } or other modifier) */
                    while (pos < input_len && input[pos] != '}' &&
                           input[pos] != ':' && input[pos] != '#' &&
                           input[pos] != '%' && input[pos] != '/' &&
                           input[pos] != '[') {
                        pos++;
                    }
                    size_t var_name_len = pos - var_name_start;
                    /* Check if it's a hook array variable */
                    if (var_name_len > 0 &&
                        is_hook_array_variable(input + var_name_start, var_name_len)) {
                        vtype = LLE_TOKEN_VARIABLE_SPECIAL;
                    }
                    /* Continue to closing brace */
                    int depth = 1;
                    while (pos < input_len && depth > 0) {
                        if (input[pos] == '{')
                            depth++;
                        else if (input[pos] == '}')
                            depth--;
                        pos++;
                    }
                }
                /* $(...) command substitution */
                else if (next == '(') {
                    int depth = 1;
                    pos++;
                    while (pos < input_len && depth > 0) {
                        if (input[pos] == '(')
                            depth++;
                        else if (input[pos] == ')')
                            depth--;
                        pos++;
                    }
                }
                /* Simple $VAR */
                else if (isalpha((unsigned char)next) || next == '_') {
                    size_t var_name_start = pos;
                    pos++;
                    while (pos < input_len &&
                           (isalnum((unsigned char)input[pos]) ||
                            input[pos] == '_')) {
                        pos++;
                    }
                    /* Check if it's a hook array variable */
                    size_t var_name_len = pos - var_name_start;
                    if (is_hook_array_variable(input + var_name_start, var_name_len)) {
                        vtype = LLE_TOKEN_VARIABLE_SPECIAL;
                    }
                }

                add_token(highlighter, vtype, token_start, pos);
                expect_command = false;
                continue;
            }
        }

        /* Operators */
        if (c == '|') {
            pos++;
            if (pos < input_len && input[pos] == '|') {
                pos++;
                add_token(highlighter, LLE_TOKEN_OR, token_start, pos);
            } else {
                add_token(highlighter, LLE_TOKEN_PIPE, token_start, pos);
            }
            expect_command = true;
            continue;
        }

        if (c == '&') {
            pos++;
            if (pos < input_len && input[pos] == '&') {
                pos++;
                add_token(highlighter, LLE_TOKEN_AND, token_start, pos);
                expect_command = true;
            } else {
                add_token(highlighter, LLE_TOKEN_BACKGROUND, token_start, pos);
            }
            continue;
        }

        if (c == ';') {
            pos++;
            add_token(highlighter, LLE_TOKEN_SEMICOLON, token_start, pos);
            expect_command = true;
            continue;
        }

        if (c == '>' || c == '<') {
            /* Process substitution: >(...) or <(...)
             * Only emit the <( or >( operator token, then let normal
             * tokenization continue for the contents. This enables proper
             * syntax highlighting of commands inside process substitutions
             * like: cat <(cat <(echo nested))
             */
            if (pos + 1 < input_len && input[pos + 1] == '(') {
                lle_syntax_token_type_t pstype =
                    (c == '<') ? LLE_TOKEN_PROCSUB_IN : LLE_TOKEN_PROCSUB_OUT;
                pos += 2; /* Skip <( or >( */
                add_token(highlighter, pstype, token_start, pos);
                /* Next token is a command inside the process substitution */
                expect_command = true;
                continue;
            }

            /* Here-string: <<< */
            if (c == '<' && pos + 2 < input_len && input[pos + 1] == '<' &&
                input[pos + 2] == '<') {
                pos += 3; /* Skip <<< */
                add_token(highlighter, LLE_TOKEN_HERESTRING, token_start, pos);
                expect_command = false;
                continue;
            }

            /* Here-document: << or <<- (with optional quoting of delimiter) */
            if (c == '<' && pos + 1 < input_len && input[pos + 1] == '<' &&
                (pos + 2 >= input_len || input[pos + 2] != '<')) {
                pos += 2; /* Skip << */
                /* Check for <<- (strip leading tabs) */
                if (pos < input_len && input[pos] == '-') {
                    pos++;
                }
                add_token(highlighter, LLE_TOKEN_HEREDOC_OP, token_start, pos);
                /* Next token will be the delimiter (handled by word parsing) */
                expect_command = false;
                continue;
            }

            /* Regular redirect: >, >>, <, >&, <&, etc. */
            pos++;
            while (pos < input_len && (input[pos] == '>' || input[pos] == '&' ||
                                       isdigit((unsigned char)input[pos]))) {
                pos++;
            }
            add_token(highlighter, LLE_TOKEN_REDIRECT, token_start, pos);
            expect_command = false;
            continue;
        }

        if (c == '(') {
            /* Arithmetic command: (( expr )) */
            if (pos + 1 < input_len && input[pos + 1] == '(') {
                pos += 2; /* Skip (( */
                int depth = 1;
                while (pos < input_len && depth > 0) {
                    if (pos + 1 < input_len && input[pos] == '(' &&
                        input[pos + 1] == '(') {
                        depth++;
                        pos++;
                    } else if (pos + 1 < input_len && input[pos] == ')' &&
                               input[pos + 1] == ')') {
                        depth--;
                        pos++;
                    }
                    pos++;
                }
                add_token(highlighter, LLE_TOKEN_ARITHMETIC, token_start, pos);
                expect_command = false;
                continue;
            }
            /* Regular subshell */
            pos++;
            add_token(highlighter, LLE_TOKEN_SUBSHELL_START, token_start, pos);
            expect_command = true;
            continue;
        }

        if (c == ')') {
            pos++;
            add_token(highlighter, LLE_TOKEN_SUBSHELL_END, token_start, pos);
            expect_command = false;
            continue;
        }

        if (c == '{') {
            pos++;
            add_token(highlighter, LLE_TOKEN_BRACE_START, token_start, pos);
            expect_command = true;
            continue;
        }

        if (c == '}') {
            pos++;
            add_token(highlighter, LLE_TOKEN_BRACE_END, token_start, pos);
            expect_command = false;
            continue;
        }

        /* Glob qualifier: *(.) *(/) *(@) - must check before extglob */
        if (is_glob_qualifier(input, pos, input_len)) {
            pos += 4; /* Skip *(X) */
            add_token(highlighter, LLE_TOKEN_GLOB_QUAL, token_start, pos);
            expect_command = false;
            continue;
        }

        /* Extended glob: ?(pat), *(pat), +(pat), @(pat), !(pat) */
        if (is_extglob_start(input, pos, input_len)) {
            pos += 2; /* Skip ?( or *( etc. */
            int depth = 1;
            while (pos < input_len && depth > 0) {
                if (input[pos] == '(') depth++;
                else if (input[pos] == ')') depth--;
                pos++;
            }
            add_token(highlighter, LLE_TOKEN_EXTGLOB, token_start, pos);
            expect_command = false;
            continue;
        }

        /* Word (command, argument, path, option, etc.) */
        if (is_word_char(c) || c == '\\') {
            /* Scan the word */
            bool has_glob = false;
            bool has_slash = false;

            while (pos < input_len) {
                char ch = input[pos];
                if (ch == '\\' && pos + 1 < input_len) {
                    pos += 2;
                    continue;
                }
                if (ch == '\\') {
                    /* Trailing backslash at end of input - include it and stop */
                    pos++;
                    break;
                }
                if (is_glob_char(ch))
                    has_glob = true;
                if (ch == '/')
                    has_slash = true;
                if (!is_word_char(ch) && !is_glob_char(ch))
                    break;
                pos++;
            }

            size_t word_len = pos - token_start;

            /* Determine token type */
            lle_syntax_token_type_t type;

            if (expect_command) {
                /* Extract word for checking */
                char word[256];
                size_t copy_len =
                    word_len < sizeof(word) - 1 ? word_len : sizeof(word) - 1;
                memcpy(word, input + token_start, copy_len);
                word[copy_len] = '\0';

                /* Check if this is a function name after 'function' keyword
                 * e.g., "function foo" or "function foo()" */
                if (after_function_keyword) {
                    type = LLE_TOKEN_COMMAND_FUNCTION;
                    after_function_keyword = false;
                    expect_command = false;
                }
                /* Check for POSIX function definition: name() { ... }
                 * Look ahead for () after the word */
                else {
                    size_t lookahead = pos;
                    /* Skip optional whitespace between name and () */
                    while (lookahead < input_len &&
                           isspace((unsigned char)input[lookahead])) {
                        lookahead++;
                    }
                    /* Check for () */
                    bool is_posix_func_def = false;
                    if (lookahead + 1 < input_len &&
                        input[lookahead] == '(' && input[lookahead + 1] == ')') {
                        is_posix_func_def = true;
                    }

                    if (is_posix_func_def) {
                        type = LLE_TOKEN_COMMAND_FUNCTION;
                        expect_command = false;
                    }
                    /* Check for VAR=value assignment prefix */
                    else if (is_assignment(input + token_start, word_len)) {
                        /* Find the '=' to get the variable name length */
                        const char *eq = memchr(input + token_start, '=', word_len);
                        if (eq) {
                            size_t var_name_len = (size_t)(eq - (input + token_start));
                            /* Check for hook array assignment (special highlight) */
                            if (is_hook_array_variable(input + token_start, var_name_len)) {
                                type = LLE_TOKEN_VARIABLE_SPECIAL;
                            } else {
                                type = LLE_TOKEN_ASSIGNMENT;
                            }
                        } else {
                            type = LLE_TOKEN_ASSIGNMENT;
                        }
                        /* Keep expect_command = true because command follows */
                        /* e.g., "VAR=value command arg1 arg2" */
                    } else if (is_shell_keyword(input + token_start, word_len)) {
                        type = LLE_TOKEN_KEYWORD;
                        /* Check if this is the 'function' keyword */
                        if (word_len == 8 &&
                            strncmp(input + token_start, "function", 8) == 0) {
                            after_function_keyword = true;
                        }
                        /* Block-ending keywords (done, fi, esac, etc.) don't expect
                           a command after them. Block-starting keywords do. */
                        if (is_block_ending_keyword(input + token_start,
                                                    word_len)) {
                            expect_command = false;
                        } else {
                            expect_command = true;
                        }
                    } else if (highlighter->validate_commands) {
                        type = lle_syntax_check_command(highlighter, word);
                        expect_command = false;
                    } else {
                        type = LLE_TOKEN_COMMAND_VALID; /* Assume valid if not
                                                           checking */
                        expect_command = false;
                    }
                }
            } else {
                /* Not expecting command - it's an argument */
                if (is_option_start(input + token_start, word_len)) {
                    type = LLE_TOKEN_OPTION;
                } else if (has_glob) {
                    type = LLE_TOKEN_GLOB;
                } else if (has_slash && highlighter->validate_paths) {
                    /* Looks like a path - check if it exists */
                    char path[4096];
                    size_t copy_len = word_len < sizeof(path) - 1
                                          ? word_len
                                          : sizeof(path) - 1;
                    memcpy(path, input + token_start, copy_len);
                    path[copy_len] = '\0';

                    /* Expand ~ if present */
                    if (path[0] == '~') {
                        const char *home = getenv("HOME");
                        if (home) {
                            char expanded[4096];
                            snprintf(expanded, sizeof(expanded), "%s%s", home,
                                     path + 1);
                            type = path_exists(expanded)
                                       ? LLE_TOKEN_PATH_VALID
                                       : LLE_TOKEN_PATH_INVALID;
                        } else {
                            type = LLE_TOKEN_PATH_INVALID;
                        }
                    } else {
                        type = path_exists(path) ? LLE_TOKEN_PATH_VALID
                                                 : LLE_TOKEN_PATH_INVALID;
                    }
                } else if (has_slash) {
                    type =
                        LLE_TOKEN_ARGUMENT; /* Path-like but not validating */
                } else {
                    /* Check if it's a number */
                    bool is_num = true;
                    for (size_t i = token_start; i < pos && is_num; i++) {
                        if (!isdigit((unsigned char)input[i]) &&
                            input[i] != '.' && input[i] != '-' &&
                            input[i] != '+') {
                            is_num = false;
                        }
                    }
                    type = is_num ? LLE_TOKEN_NUMBER : LLE_TOKEN_ARGUMENT;
                }
            }

            add_token(highlighter, type, token_start, pos);
            continue;
        }

        /* Unknown character - skip it */
        pos++;
        add_token(highlighter, LLE_TOKEN_UNKNOWN, token_start, pos);
    }

    /* Apply colors to tokens */
    for (size_t i = 0; i < highlighter->token_count; i++) {
        lle_syntax_token_t *tok = &highlighter->tokens[i];
        const lle_syntax_colors_t *c = &highlighter->colors;

        switch (tok->type) {
        case LLE_TOKEN_COMMAND_VALID:
            tok->color = c->command_valid;
            if (c->command_bold)
                tok->attributes |= LLE_ATTR_BOLD;
            break;
        case LLE_TOKEN_COMMAND_INVALID:
            tok->color = c->command_invalid;
            break;
        case LLE_TOKEN_COMMAND_BUILTIN:
        case LLE_TOKEN_COMMAND_ALIAS:
            tok->color = c->command_builtin;
            if (c->command_bold)
                tok->attributes |= LLE_ATTR_BOLD;
            break;
        case LLE_TOKEN_COMMAND_FUNCTION:
            tok->color = c->command_function;
            break;
        case LLE_TOKEN_KEYWORD:
            tok->color = c->keyword;
            if (c->keyword_bold)
                tok->attributes |= LLE_ATTR_BOLD;
            break;
        case LLE_TOKEN_ASSIGNMENT:
            tok->color = c->assignment;
            break;
        case LLE_TOKEN_STRING_SINGLE:
        case LLE_TOKEN_STRING_DOUBLE:
        case LLE_TOKEN_STRING_BACKTICK:
            tok->color = c->string;
            break;
        case LLE_TOKEN_VARIABLE:
            tok->color = c->variable;
            break;
        case LLE_TOKEN_VARIABLE_SPECIAL:
            tok->color = c->variable_special;
            break;
        case LLE_TOKEN_PATH_VALID:
            tok->color = c->path_valid;
            if (c->path_underline)
                tok->attributes |= LLE_ATTR_UNDERLINE;
            break;
        case LLE_TOKEN_PATH_INVALID:
            tok->color = c->path_invalid;
            if (c->path_underline)
                tok->attributes |= LLE_ATTR_UNDERLINE;
            break;
        case LLE_TOKEN_PIPE:
            tok->color = c->pipe;
            break;
        case LLE_TOKEN_REDIRECT:
            tok->color = c->redirect;
            break;
        case LLE_TOKEN_AND:
        case LLE_TOKEN_OR:
        case LLE_TOKEN_BACKGROUND:
        case LLE_TOKEN_SEMICOLON:
        case LLE_TOKEN_SUBSHELL_START:
        case LLE_TOKEN_SUBSHELL_END:
        case LLE_TOKEN_BRACE_START:
        case LLE_TOKEN_BRACE_END:
            tok->color = c->operator_other;
            break;
        case LLE_TOKEN_COMMENT:
            tok->color = c->comment;
            if (c->comment_dim)
                tok->attributes |= LLE_ATTR_DIM;
            break;
        case LLE_TOKEN_NUMBER:
            tok->color = c->number;
            break;
        case LLE_TOKEN_OPTION:
            tok->color = c->option;
            break;
        case LLE_TOKEN_GLOB:
            tok->color = c->glob;
            break;
        case LLE_TOKEN_EXTGLOB:
            tok->color = c->extglob;
            break;
        case LLE_TOKEN_GLOB_QUAL:
            tok->color = c->glob_qual;
            break;
        case LLE_TOKEN_ARGUMENT:
            tok->color = c->argument;
            break;
        /* Here-documents and here-strings */
        case LLE_TOKEN_HEREDOC_OP:
            tok->color = c->heredoc_op;
            break;
        case LLE_TOKEN_HEREDOC_DELIM:
            tok->color = c->heredoc_delim;
            break;
        case LLE_TOKEN_HEREDOC_CONTENT:
            tok->color = c->heredoc_content;
            break;
        case LLE_TOKEN_HERESTRING:
            tok->color = c->herestring;
            break;
        /* Process substitution */
        case LLE_TOKEN_PROCSUB_IN:
        case LLE_TOKEN_PROCSUB_OUT:
            tok->color = c->procsub;
            break;
        /* ANSI-C quoting */
        case LLE_TOKEN_STRING_ANSIC:
            tok->color = c->string_ansic;
            break;
        /* Arithmetic expansion */
        case LLE_TOKEN_ARITHMETIC:
            tok->color = c->arithmetic;
            break;
        case LLE_TOKEN_ERROR:
        case LLE_TOKEN_UNCLOSED_STRING:
        case LLE_TOKEN_UNCLOSED_SUBSHELL:
            tok->color = c->error;
            if (c->error_underline)
                tok->attributes |= LLE_ATTR_UNDERLINE;
            break;
        default:
            tok->color = 0;
            break;
        }
    }

    return (int)highlighter->token_count;
}

/* ========================================================================== */
/*                         ANSI RENDERING                                     */
/* ========================================================================== */

/**
 * @brief Convert color and attributes to ANSI escape sequence
 *
 * Generates an ANSI escape sequence for the given color and text attributes.
 * Supports 8-color, 256-color, and truecolor modes.
 *
 * @param color RGB color value (0x00RRGGBB format)
 * @param attributes Text attributes (bold, dim, italic, underline)
 * @param color_depth Color depth (1=8 colors, 2=256 colors, 3=truecolor)
 * @param output Buffer to write escape sequence
 * @param output_size Size of output buffer
 * @return Number of bytes written, or -1 on error
 */
int lle_syntax_color_to_ansi(uint32_t color, uint8_t attributes,
                             int color_depth, char *output,
                             size_t output_size) {
    if (!output || output_size < 2)
        return -1;

    char *p = output;
    char *end = output + output_size - 1;

    /* Start escape sequence */
    p += snprintf(p, end - p, "\x1b[");

    /* Attributes */
    bool need_sep = false;
    if (attributes & LLE_ATTR_BOLD) {
        p += snprintf(p, end - p, "1");
        need_sep = true;
    }
    if (attributes & LLE_ATTR_DIM) {
        p += snprintf(p, end - p, "%s2", need_sep ? ";" : "");
        need_sep = true;
    }
    if (attributes & LLE_ATTR_ITALIC) {
        p += snprintf(p, end - p, "%s3", need_sep ? ";" : "");
        need_sep = true;
    }
    if (attributes & LLE_ATTR_UNDERLINE) {
        p += snprintf(p, end - p, "%s4", need_sep ? ";" : "");
        need_sep = true;
    }

    /* Color */
    if (color != 0 && color_depth > 0) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        if (color_depth >= 3) {
            /* Truecolor */
            p += snprintf(p, end - p, "%s38;2;%d;%d;%d", need_sep ? ";" : "", r,
                          g, b);
        } else if (color_depth == 2) {
            /* 256 color - convert RGB to 256 color index */
            int idx;
            if (r == g && g == b) {
                /* Grayscale */
                if (r < 8)
                    idx = 16;
                else if (r > 248)
                    idx = 231;
                else
                    idx = 232 + (r - 8) / 10;
            } else {
                /* Color cube */
                idx = 16 + (r / 51) * 36 + (g / 51) * 6 + (b / 51);
            }
            p += snprintf(p, end - p, "%s38;5;%d", need_sep ? ";" : "", idx);
        } else {
            /* 8 color - pick closest basic color */
            int basic = 0;
            if (r > 127)
                basic |= 1;
            if (g > 127)
                basic |= 2;
            if (b > 127)
                basic |= 4;
            /* Use bright if any component is very bright */
            if (r > 200 || g > 200 || b > 200) {
                p += snprintf(p, end - p, "%s9%d", need_sep ? ";" : "", basic);
            } else {
                p += snprintf(p, end - p, "%s3%d", need_sep ? ";" : "", basic);
            }
        }
    }

    p += snprintf(p, end - p, "m");

    return (int)(p - output);
}

/**
 * @brief Render highlighted input as ANSI-colored string
 *
 * Converts the tokenized input into a string with ANSI escape sequences
 * for terminal display.
 *
 * @param highlighter Highlighter instance with tokens
 * @param input Original input string
 * @param output Buffer to write ANSI-colored output
 * @param output_size Size of output buffer
 * @return Number of bytes written, or -1 on error
 */
int lle_syntax_render_ansi(lle_syntax_highlighter_t *highlighter,
                           const char *input, char *output,
                           size_t output_size) {
    if (!highlighter || !input || !output || output_size == 0)
        return -1;

    char *p = output;
    char *end = output + output_size - 1;

    for (size_t i = 0; i < highlighter->token_count && p < end; i++) {
        lle_syntax_token_t *tok = &highlighter->tokens[i];

        /* Skip whitespace and unknown tokens - just copy them */
        if (tok->type == LLE_TOKEN_WHITESPACE ||
            tok->type == LLE_TOKEN_UNKNOWN) {
            size_t len = tok->end - tok->start;
            if (p + len >= end)
                len = end - p;
            memcpy(p, input + tok->start, len);
            p += len;
            continue;
        }

        /* Apply color */
        if (tok->color != 0 || tok->attributes != 0) {
            char color_seq[64];
            int seq_len = lle_syntax_color_to_ansi(
                tok->color, tok->attributes, highlighter->color_depth,
                color_seq, sizeof(color_seq));
            if (seq_len > 0 && p + seq_len < end) {
                memcpy(p, color_seq, seq_len);
                p += seq_len;
            }
        }

        /* Copy token text */
        size_t len = tok->end - tok->start;
        if (p + len >= end)
            len = end - p;
        memcpy(p, input + tok->start, len);
        p += len;

        /* Reset after token */
        if (tok->color != 0 || tok->attributes != 0) {
            const char *reset = "\x1b[0m";
            size_t reset_len = 4;
            if (p + reset_len < end) {
                memcpy(p, reset, reset_len);
                p += reset_len;
            }
        }
    }

    *p = '\0';
    return (int)(p - output);
}

/* ========================================================================== */
/*                         PUBLIC API                                         */
/* ========================================================================== */

/**
 * @brief Create a new syntax highlighter instance
 * @param highlighter Output pointer to receive created highlighter
 * @return 0 on success, -1 on error
 */
int lle_syntax_highlighter_create(lle_syntax_highlighter_t **highlighter) {
    if (!highlighter)
        return -1;

    lle_syntax_highlighter_t *h = calloc(1, sizeof(lle_syntax_highlighter_t));
    if (!h)
        return -1;

    /* Set defaults */
    h->colors = default_colors;
    h->enabled = true;
    h->validate_commands = true;
    h->validate_paths = true;
    h->highlight_errors = true;
    /* Detect terminal color capabilities via cached detection */
    lle_terminal_detection_result_t *detection = NULL;
    if (lle_detect_terminal_capabilities_optimized(&detection) == LLE_SUCCESS
        && detection) {
        if (detection->supports_truecolor)
            h->color_depth = 3;
        else if (detection->supports_256_colors)
            h->color_depth = 2;
        else if (detection->supports_colors)
            h->color_depth = 1;
        else
            h->color_depth = 0;
    } else {
        h->color_depth = 3; /* Fallback: assume truecolor */
    }

    /* Create command cache */
    h->command_cache = calloc(1, sizeof(cmd_cache_t));

    *highlighter = h;
    return 0;
}

/**
 * @brief Destroy a syntax highlighter instance
 * @param highlighter Highlighter to destroy
 */
void lle_syntax_highlighter_destroy(lle_syntax_highlighter_t *highlighter) {
    if (!highlighter)
        return;

    /* Free command cache */
    if (highlighter->command_cache) {
        cmd_cache_t *cache = (cmd_cache_t *)highlighter->command_cache;
        for (int i = 0; i < CMD_CACHE_SIZE; i++) {
            free(cache->entries[i].command);
        }
        free(cache);
    }

    free(highlighter->tokens);
    free(highlighter);
}

/**
 * @brief Set custom color scheme for highlighter
 * @param highlighter Highlighter instance
 * @param colors Color scheme to apply
 */
void lle_syntax_highlighter_set_colors(lle_syntax_highlighter_t *highlighter,
                                       const lle_syntax_colors_t *colors) {
    if (!highlighter || !colors)
        return;
    
    /* Merge colors: only apply non-zero values from the source, preserving
     * defaults for unspecified colors. This allows themes to partially
     * override syntax colors without clearing unspecified ones to black. */
#define MERGE_COLOR(field) \
    if (colors->field != 0) highlighter->colors.field = colors->field
    
    MERGE_COLOR(command_valid);
    MERGE_COLOR(command_invalid);
    MERGE_COLOR(command_builtin);
    MERGE_COLOR(command_alias);
    MERGE_COLOR(command_function);
    MERGE_COLOR(keyword);
    MERGE_COLOR(string);
    MERGE_COLOR(string_escape);
    MERGE_COLOR(variable);
    MERGE_COLOR(variable_special);
    MERGE_COLOR(path_valid);
    MERGE_COLOR(path_invalid);
    MERGE_COLOR(pipe);
    MERGE_COLOR(redirect);
    MERGE_COLOR(operator_other);
    MERGE_COLOR(assignment);
    MERGE_COLOR(comment);
    MERGE_COLOR(number);
    MERGE_COLOR(option);
    MERGE_COLOR(glob);
    MERGE_COLOR(extglob);
    MERGE_COLOR(glob_qual);
    MERGE_COLOR(argument);
    MERGE_COLOR(heredoc_op);
    MERGE_COLOR(heredoc_delim);
    MERGE_COLOR(heredoc_content);
    MERGE_COLOR(herestring);
    MERGE_COLOR(procsub);
    MERGE_COLOR(string_ansic);
    MERGE_COLOR(arithmetic);
    MERGE_COLOR(error);
    MERGE_COLOR(error_fg);
    
#undef MERGE_COLOR
    
    /* Boolean attributes are always copied (they default to false) */
    highlighter->colors.keyword_bold = colors->keyword_bold;
    highlighter->colors.command_bold = colors->command_bold;
    highlighter->colors.error_underline = colors->error_underline;
    highlighter->colors.path_underline = colors->path_underline;
    highlighter->colors.comment_dim = colors->comment_dim;
}

/**
 * @brief Get the default color scheme
 * @param colors Output structure to receive default colors
 */
void lle_syntax_colors_get_default(lle_syntax_colors_t *colors) {
    if (!colors)
        return;
    *colors = default_colors;
}

/**
 * @brief Get the array of tokens from last highlight operation
 * @param highlighter Highlighter instance
 * @param count Output pointer to receive token count (may be NULL)
 * @return Pointer to token array, or NULL on error
 */
const lle_syntax_token_t *
lle_syntax_get_tokens(lle_syntax_highlighter_t *highlighter, size_t *count) {
    if (!highlighter) {
        if (count)
            *count = 0;
        return NULL;
    }
    if (count)
        *count = highlighter->token_count;
    return highlighter->tokens;
}

/**
 * @brief Clear the command existence cache
 *
 * Invalidates all cached command lookup results. Call this when PATH
 * changes or commands are installed/removed.
 *
 * @param highlighter Highlighter instance
 */
void lle_syntax_clear_cache(lle_syntax_highlighter_t *highlighter) {
    if (!highlighter || !highlighter->command_cache)
        return;

    cmd_cache_t *cache = (cmd_cache_t *)highlighter->command_cache;
    for (int i = 0; i < CMD_CACHE_SIZE; i++) {
        free(cache->entries[i].command);
        cache->entries[i].command = NULL;
        cache->entries[i].type = LLE_TOKEN_UNKNOWN;
        cache->entries[i].timestamp = 0;
    }
}
