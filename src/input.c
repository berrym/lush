/**
 * @file input.c
 * @brief Lush Input System using LLE (Lush Line Editor)
 *
 * This module provides unified input handling for both interactive and
 * non-interactive modes, using LLE for interactive line editing.
 *
 * UTF-8 Support:
 * This module uses LLE's UTF-8 support to properly handle multi-byte
 * characters. While shell syntax characters (quotes, brackets, etc.) are
 * all ASCII, we must properly skip over UTF-8 multi-byte sequences to
 * avoid misinterpreting continuation bytes as syntax characters.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "input.h"
#include "errors.h"
#include "init.h"
#include "lle/lle_shell_integration.h"
#include "lle/utf8_support.h"
#include "lush.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// ============================================================================
/// UTF-8 HELPER (uses LLE's UTF-8 support)
/// ============================================================================

/**
 * @brief Get the byte length of a UTF-8 character at the given position
 *
 * Determines the number of bytes in a UTF-8 character sequence starting
 * at the given position. Uses LLE's UTF-8 support for sequence detection
 * and validation.
 *
 * @param p Pointer to the start of a UTF-8 character sequence
 * @return Number of bytes in the character (1-4), or 0 if p is NULL/empty,
 *         or 1 for invalid sequences (safe fallback)
 */
static int utf8_char_len(const char *p) {
    if (!p || !*p)
        return 0;

    unsigned char c = (unsigned char)*p;

    /// ASCII (0x00-0x7F) - single byte
    if (c < 0x80)
        return 1;

    /// Use LLE's UTF-8 sequence length detection
    int len = lle_utf8_sequence_length(c);

    /// Validate the sequence
    if (len > 1 && lle_utf8_is_valid_sequence(p, len)) {
        return len;
    }

    /// Invalid sequence - skip one byte
    return 1;
}

/// Input state for multiline parsing
typedef struct {
    int quote_count;
    int double_quote_count;
    int backtick_count;
    int paren_count;
    int brace_count;
    int bracket_count;
    bool in_single_quote;
    bool in_double_quote;
    bool in_backtick;
    bool escaped;
    bool in_here_doc;
    char *here_doc_delimiter;
    bool has_continuation;
    bool in_command_substitution;
    bool in_arithmetic;
    bool in_function_definition;
    bool saw_posix_func_parens; ///< Saw name() pattern, waiting for {
    bool in_case_statement;
    bool in_if_statement;
    bool in_while_loop;
    bool in_for_loop;
    bool in_until_loop;
    int compound_command_depth;
} input_state_t;

/// Global state
static input_state_t global_state = {0};
static bool state_initialized = false;

/// Forward declarations
static void init_input_state(input_state_t *state);
static void cleanup_input_state(input_state_t *state);
static void analyze_line(const char *line, input_state_t *state);
static bool is_input_complete(input_state_t *state);
static const char *get_continuation_prompt(input_state_t *state);
static bool is_control_keyword(const char *word);
static bool is_terminator(const char *line);

/// Public function to get current continuation prompt
const char *lush_get_current_continuation_prompt(void);
static char *convert_multiline_for_history(const char *input);

/// ============================================================================
/// INPUT STATE MANAGEMENT
/// ============================================================================

/**
 * @brief Initialize an input state structure to default values
 *
 * Zeros out all fields and sets the here document delimiter to NULL.
 * Must be called before first use of an input_state_t structure.
 *
 * @param state Pointer to the input state structure to initialize
 */
static void init_input_state(input_state_t *state) {
    memset(state, 0, sizeof(input_state_t));
    state->here_doc_delimiter = NULL;
}

/**
 * @brief Clean up an input state structure and free allocated memory
 *
 * Frees the here document delimiter if allocated and zeros the structure.
 * Safe to call on an already-cleaned state.
 *
 * @param state Pointer to the input state structure to clean up
 */
static void cleanup_input_state(input_state_t *state) {
    if (state->here_doc_delimiter) {
        free(state->here_doc_delimiter);
        state->here_doc_delimiter = NULL;
    }
    memset(state, 0, sizeof(input_state_t));
}

/// ============================================================================
/// MULTILINE INPUT ANALYSIS
/// ============================================================================

/**
 * @brief Check if a word is a shell control flow keyword
 *
 * Identifies shell reserved words that affect control flow and require
 * special handling for multiline input parsing (if, then, else, fi, etc.).
 *
 * @param word The word to check
 * @return true if word is a control keyword, false otherwise
 */
static bool is_control_keyword(const char *word) {
    const char *keywords[] = {"if",   "then", "else",  "elif",     "fi",
                              "case", "esac", "while", "until",    "do",
                              "done", "for",  "in",    "function", "select",
                              "{",    "}"};

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strcmp(word, keywords[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if a line contains a compound command terminator
 *
 * Checks if the line (after skipping whitespace) starts with a terminator
 * keyword like fi, done, esac, or closing brace.
 *
 * @param line The line to check
 * @return true if line starts with a terminator, false otherwise
 */
MAYBE_UNUSED
static bool is_terminator(const char *line) {
    /// Skip whitespace
    while (*line && isspace(*line))
        line++;

    /// Check for terminators
    return (strncmp(line, "fi", 2) == 0 || strncmp(line, "done", 4) == 0 ||
            strncmp(line, "esac", 4) == 0 || strcmp(line, "}") == 0);
}

/**
 * @brief Analyze a line of input to update parsing state
 *
 * Processes a line character by character to track quotes, parentheses,
 * braces, brackets, here documents, escape sequences, and control keywords.
 * Updates the input state structure to reflect what constructs are currently
 * open and whether continuation input is needed.
 *
 * @param line The line of input to analyze
 * @param state The input state structure to update
 */
static void analyze_line(const char *line, input_state_t *state) {
    if (!line || !state)
        return;

    const char *p = line;
    char word[256] = {0};
    int word_pos = 0;
    (void)word_pos; /// Reserved for word boundary tracking
    bool at_word_start = true;
    (void)at_word_start; /// Reserved for word start detection

    while (*p) {
        unsigned char uc = (unsigned char)*p;
        char c = *p;

        /// UTF-8 multi-byte sequence handling:
        /// If this is a non-ASCII byte (high bit set), it's part of a UTF-8
        /// multi-byte character. Skip the entire sequence since shell syntax
        /// characters are all ASCII. This prevents misinterpreting UTF-8
        /// continuation bytes as shell metacharacters.
        if (uc >= 0x80) {
            int char_len = utf8_char_len(p);

            /// If we're collecting a word, flush it first (UTF-8 chars break
            /// words for keyword detection purposes - keywords are ASCII only)
            if (word_pos > 0) {
                word[word_pos] = '\0';
                /// Keywords are ASCII-only, so no need to check here
                word_pos = 0;
                memset(word, 0, sizeof(word));
            }

            /// Skip the entire UTF-8 sequence
            p += (char_len > 0) ? char_len : 1;
            at_word_start = true;
            continue;
        }

        /// Handle escape sequences
        if (state->escaped) {
            state->escaped = false;
            p++;
            continue;
        }

        if (c == '\\') {
            state->escaped = true;
            p++;
            continue;
        }

        /// Handle quotes
        if (c == '\'' && !state->in_double_quote && !state->in_backtick) {
            state->in_single_quote = !state->in_single_quote;
            if (state->in_single_quote) {
                state->quote_count++;
            }
        } else if (c == '"' && !state->in_single_quote) {
            state->in_double_quote = !state->in_double_quote;
            if (state->in_double_quote) {
                state->double_quote_count++;
            }
        } else if (c == '`' && !state->in_single_quote) {
            state->in_backtick = !state->in_backtick;
            if (state->in_backtick) {
                state->backtick_count++;
            }
        }

        /// Skip if we're in quotes
        if (state->in_single_quote || state->in_double_quote ||
            state->in_backtick) {
            p++;
            continue;
        }

        /// Handle parentheses, braces, brackets
        if (c == '(') {
            state->paren_count++;
            /// Check for POSIX function definition: name() or name ()
            /// We just accumulated a word and now see '(' followed by ')'
            if (word_pos > 0 && *(p + 1) == ')') {
                /// This is name() pattern - mark that we're in a POSIX func def
                state->saw_posix_func_parens = true;
            }
        } else if (c == ')') {
            state->paren_count--;
        } else if (c == '{') {
            state->brace_count++;
        } else if (c == '}') {
            state->brace_count--;
        } else if (c == '[') {
            state->bracket_count++;
        } else if (c == ']') {
            state->bracket_count--;
        }

        /// Handle here document detection
        if (c == '<' && *(p + 1) == '<' && !state->in_here_doc) {
            /// Found <<, look for delimiter
            const char *delim_start = p + 2;

            /// Skip optional '-' for <<-
            if (*delim_start == '-') {
                delim_start++;
            }

            /// Skip whitespace
            while (*delim_start == ' ' || *delim_start == '\t') {
                delim_start++;
            }

            /// Extract delimiter (up to end of line or whitespace)
            const char *delim_end = delim_start;
            while (*delim_end && *delim_end != '\n' && *delim_end != ' ' &&
                   *delim_end != '\t') {
                delim_end++;
            }

            if (delim_end > delim_start) {
                /// Handle quoted delimiters - strip surrounding quotes
                const char *actual_delim_start = delim_start;
                const char *actual_delim_end = delim_end;

                /// Check for single or double quotes
                if ((*delim_start == '\'' || *delim_start == '"') &&
                    delim_end > delim_start + 1 &&
                    *(delim_end - 1) == *delim_start) {
                    /// Strip quotes
                    actual_delim_start++;
                    actual_delim_end--;
                }

                /// Found a delimiter, enter here document mode
                state->in_here_doc = true;
                if (state->here_doc_delimiter) {
                    free(state->here_doc_delimiter);
                }
                size_t delim_len = actual_delim_end - actual_delim_start;
                state->here_doc_delimiter = malloc(delim_len + 1);
                if (state->here_doc_delimiter) {
                    strncpy(state->here_doc_delimiter, actual_delim_start,
                            delim_len);
                    state->here_doc_delimiter[delim_len] = '\0';
                }
            }
        }

        /// Check if current line is a here document delimiter (ends here doc)
        if (state->in_here_doc && state->here_doc_delimiter) {
            /// Check if this entire line matches the delimiter
            const char *line_start = line;
            while (*line_start == ' ' || *line_start == '\t') {
                line_start++; /// Skip leading whitespace
            }

            if (strncmp(line_start, state->here_doc_delimiter,
                        strlen(state->here_doc_delimiter)) == 0) {
                /// Check if delimiter is followed by end of line or whitespace
                const char *after_delim =
                    line_start + strlen(state->here_doc_delimiter);
                if (*after_delim == '\0' || *after_delim == '\n' ||
                    *after_delim == ' ' || *after_delim == '\t') {
                    /// This line is the delimiter, end here document
                    state->in_here_doc = false;
                    free(state->here_doc_delimiter);
                    state->here_doc_delimiter = NULL;
                }
            }
        }

        /// Collect words for keyword analysis
        if (isalnum(c) || c == '_') {
            if (word_pos < (int)sizeof(word) - 1) {
                word[word_pos++] = c;
            }
            at_word_start = false;
        } else if (c == '{' || c == '}') {
            /// Handle { and } as single-character keywords
            if (word_pos > 0) {
                /// Process any accumulated word first
                word[word_pos] = '\0';

                /// Check for control keywords
                if (is_control_keyword(word)) {

                    if (strcmp(word, "if") == 0) {
                        state->in_if_statement = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "while") == 0) {
                        state->in_while_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "for") == 0) {
                        state->in_for_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "until") == 0) {
                        state->in_until_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "case") == 0) {
                        state->in_case_statement = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "function") == 0) {
                        state->in_function_definition = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "fi") == 0) {
                        state->in_if_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    } else if (strcmp(word, "done") == 0) {
                        state->in_while_loop = false;
                        state->in_for_loop = false;
                        state->in_until_loop = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    } else if (strcmp(word, "esac") == 0) {
                        state->in_case_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    }
                }

                word_pos = 0;
                memset(word, 0, sizeof(word));
            }

            /// Now handle the { or } character as a single-character keyword
            if (c == '{') {
                /// Check if this is a POSIX function definition: name() { ... }
                /// saw_posix_func_parens is set when we saw the () pattern
                if (state->saw_posix_func_parens) {
                    /// This is a POSIX-style function definition
                    state->in_function_definition = true;
                    state->compound_command_depth++;
                    state->saw_posix_func_parens = false;
                } else if (!state->in_function_definition) {
                    /// Regular brace group (not a function)
                    state->compound_command_depth++;
                }
                /// If already in_function_definition (from 'function' keyword),
                /// don't increment again - the keyword handler already did
            } else if (c == '}') {
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                if (state->compound_command_depth == 0) {
                    state->in_function_definition = false;
                    state->saw_posix_func_parens = false;
                }
            }
        } else {
            if (word_pos > 0) {
                word[word_pos] = '\0';

                /// Check for control keywords
                if (is_control_keyword(word)) {

                    if (strcmp(word, "if") == 0) {
                        state->in_if_statement = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "while") == 0) {
                        state->in_while_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "for") == 0) {
                        state->in_for_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "until") == 0) {
                        state->in_until_loop = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "case") == 0) {
                        state->in_case_statement = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "function") == 0) {
                        state->in_function_definition = true;
                        state->compound_command_depth++;
                    } else if (strcmp(word, "{") == 0) {
                        state->compound_command_depth++;
                    } else if (strcmp(word, "fi") == 0) {
                        state->in_if_statement = false;
                        state->has_continuation =
                            false; /// Clear continuation flag when closing if
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    } else if (strcmp(word, "done") == 0) {
                        state->in_while_loop = false;
                        state->in_for_loop = false;
                        state->in_until_loop = false;
                        state->has_continuation =
                            false; /// Clear continuation flag when closing loop
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    } else if (strcmp(word, "esac") == 0) {
                        state->in_case_statement = false;
                        state->has_continuation =
                            false; /// Clear continuation flag when closing case
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                    } else if (strcmp(word, "}") == 0) {
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        if (state->compound_command_depth == 0) {
                            state->in_function_definition = false;
                        }
                    }
                }

                word_pos = 0;
                memset(word, 0, sizeof(word));
            }
        }

        /// Check for line continuation
        if (c == '\\' && *(p + 1) == '\0') {
            state->has_continuation = true;
        }

        p++;
    }

    /// Handle remaining word
    /// Trailing logical/pipe operator forces line continuation. A line
    /// ending in `&&`, `||`, or `|` (after trailing whitespace, with the
    /// operator not inside quotes) has its statement obviously not
    /// complete -- the next command appears on the following line. Bash
    /// and zsh both keep reading; without this check, lush treats the
    /// line as one statement and the parser sees the operator with
    /// nothing on its right.
    if (!state->in_single_quote && !state->in_double_quote &&
        !state->in_backtick && !state->in_here_doc) {
        const char *trim_end = line + strlen(line);
        while (trim_end > line &&
               (*(trim_end - 1) == ' ' || *(trim_end - 1) == '\t')) {
            trim_end--;
        }
        size_t trim_len = (size_t)(trim_end - line);
        if (trim_len >= 2 && ((trim_end[-2] == '&' && trim_end[-1] == '&') ||
                              (trim_end[-2] == '|' && trim_end[-1] == '|'))) {
            state->has_continuation = true;
        } else if (trim_len >= 1 && trim_end[-1] == '|' &&
                   (trim_len < 2 || trim_end[-2] != '|')) {
            /// Bare `|` at end of line (not part of `||`); pipeline
            /// continues on next line.
            state->has_continuation = true;
        }
    }

    /// Check final word at end of line if any
    if (word_pos > 0) {
        word[word_pos] = '\0';

        if (is_control_keyword(word)) {
            /// Handle keywords found at end of line
            if (strcmp(word, "then") == 0 || strcmp(word, "do") == 0) {
                state->has_continuation = true;
            } else if (strcmp(word, "done") == 0) {
                state->in_while_loop = false;
                state->in_for_loop = false;
                state->in_until_loop = false;
                state->has_continuation =
                    false; /// Clear continuation flag when closing loop
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
            } else if (strcmp(word, "esac") == 0) {
                state->in_case_statement = false;
                state->has_continuation =
                    false; /// Clear continuation flag when closing case
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
            } else if (strcmp(word, "}") == 0) {
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                if (state->compound_command_depth == 0) {
                    state->in_function_definition = false;
                }
            } else if (strcmp(word, "fi") == 0) {
                state->in_if_statement = false;
                state->has_continuation =
                    false; /// Clear continuation flag when closing if
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
            }
        }
    }
}

/**
 * @brief Check if the current input is syntactically complete
 *
 * Examines the input state to determine if all opened constructs
 * (quotes, parentheses, compound commands, etc.) have been closed.
 *
 * @param state The input state to check
 * @return true if input is complete, false if more input is needed
 */
static bool is_input_complete(input_state_t *state) {
    if (!state)
        return true;

    /// Check for unmatched quotes
    if (state->in_single_quote || state->in_double_quote ||
        state->in_backtick) {
        return false;
    }

    /// Check for unmatched parentheses, braces, brackets
    if (state->paren_count > 0 || state->brace_count > 0 ||
        state->bracket_count > 0) {
        return false;
    }

    /// Check for incomplete compound commands
    if (state->compound_command_depth > 0) {
        return false;
    }

    /// Check for line continuation
    if (state->has_continuation) {
        return false;
    }

    /// Check for incomplete control structures
    if (state->in_if_statement || state->in_while_loop || state->in_for_loop ||
        state->in_until_loop || state->in_case_statement ||
        state->in_function_definition) {
        return false;
    }

    /// Check for here documents
    if (state->in_here_doc) {
        return false;
    }

    return true;
}

/**
 * @brief Get the appropriate continuation prompt for the current state
 *
 * Returns a context-specific prompt based on what construct is currently
 * being entered (quote, function, if, loop, case, etc.). Falls back to
 * the PS2 environment variable or "> " if not set.
 *
 * @param state The current input state
 * @return Prompt string to display
 */
static const char *get_continuation_prompt(input_state_t *state) {
    if (!state)
        return "> ";

    /// Use PS2 from symbol table, with fallback
    const char *ps2 = symtable_get_global_default("PS2", "> ");

    /// Could customize based on state if desired
    if (state->in_single_quote || state->in_double_quote) {
        return "quote> ";
    } else if (state->in_function_definition) {
        return "function> ";
    } else if (state->in_if_statement) {
        return "if> ";
    } else if (state->in_while_loop || state->in_for_loop ||
               state->in_until_loop) {
        return "loop> ";
    } else if (state->in_case_statement) {
        return "case> ";
    }

    return ps2;
}

/**
 * @brief Get the current continuation prompt for multiline input
 *
 * Returns the appropriate prompt string based on the current input state.
 * If not in multiline mode, returns the primary prompt. Otherwise returns
 * a context-specific continuation prompt (quote, function, if, loop, case).
 *
 * @return Prompt string to display (static string, do not free)
 */
const char *lush_get_current_continuation_prompt(void) {
    if (!state_initialized) {
        return "$ "; /// Return primary prompt if not in multiline mode
    }

    /// Check for any active multiline state indicators
    bool in_multiline =
        (global_state.in_single_quote || global_state.in_double_quote ||
         global_state.in_backtick || global_state.paren_count > 0 ||
         global_state.brace_count > 0 || global_state.bracket_count > 0 ||
         global_state.in_if_statement || global_state.in_while_loop ||
         global_state.in_for_loop || global_state.in_until_loop ||
         global_state.in_case_statement ||
         global_state.in_function_definition ||
         global_state.compound_command_depth > 0);

    if (!in_multiline) {
        return "$ "; /// Return primary prompt
    }

    /// Return appropriate continuation prompt
    return get_continuation_prompt(&global_state);
}

/**
 * @brief Convert multiline input to single line for history storage
 *
 * Replaces newline characters with spaces to create a single-line
 * representation suitable for storing in command history.
 *
 * @param input The multiline input string
 * @return Allocated single-line string, or NULL on failure
 */
MAYBE_UNUSED
static char *convert_multiline_for_history(const char *input) {
    if (!input)
        return NULL;

    size_t len = strlen(input);
    char *converted = malloc(len + 1);
    if (!converted)
        return NULL;

    char *dst = converted;
    const char *src = input;

    while (*src) {
        if (*src == '\n') {
            *dst++ = ' ';
        } else {
            *dst++ = *src;
        }
        src++;
    }

    *dst = '\0';
    return converted;
}

/// ============================================================================
/// PUBLIC INPUT FUNCTIONS
/// ============================================================================

/**
 * @brief Free all input buffers and reset input state
 *
 * Cleans up the global input state and releases any allocated memory.
 * Should be called when input processing is complete or on error cleanup.
 */
void free_input_buffers(void) {
    cleanup_input_state(&global_state);
    state_initialized = false;
}

/**
 * @brief Read a single line of input from a file stream
 *
 * Reads one line from the specified file stream (or stdin if NULL).
 * Used for non-interactive input where multiline handling is not needed.
 * The trailing newline is removed from the returned string.
 *
 * @param in File stream to read from, or NULL for stdin
 * @return Allocated line string, or NULL on EOF or error
 */
char *get_input(FILE *in) {
    /// For non-interactive input, read a line directly (single line only)
    if (!in)
        in = stdin;

    char *line = NULL;
    size_t len = 0;
    ssize_t read = getline(&line, &len, in);

    if (read == -1) {
        free(line);
        return NULL;
    }

    /// Remove trailing newline
    if (read > 0 && line[read - 1] == '\n') {
        line[read - 1] = '\0';
    }

    return line;
}

/**
 * @brief Check if current state requires additional input lines
 *
 * Determines if the input state indicates an incomplete construct
 * that requires continuation input (open quotes, compound commands,
 * here documents, unmatched brackets, etc.).
 *
 * @param state Pointer to the input state to check
 * @return true if more input is needed, false if complete
 */
static bool needs_continuation(input_state_t *state) {
    if (!state)
        return false;

    /// Need continuation if we're in any compound structure
    if (state->compound_command_depth > 0) {
        return true;
    }

    /// Need continuation if we're in any specific construct
    if (state->in_function_definition || state->in_case_statement ||
        state->in_if_statement || state->in_while_loop || state->in_for_loop ||
        state->in_until_loop) {
        return true;
    }

    /// POSIX function header `name()` seen but body brace not yet opened.
    /// The function body may start on a following line; keep reading
    /// until the `{` arrives (analyze_line clears this flag when it does).
    /// Without this, `name()\n{ body }` is split into two statements and
    /// the parser sees `name()` followed by EOF.
    if (state->saw_posix_func_parens) {
        return true;
    }

    /// Need continuation if we have pending quotes or escapes
    if (state->in_single_quote || state->in_double_quote || state->escaped ||
        state->has_continuation) {
        return true;
    }

    /// Need continuation if we're in a here document
    if (state->in_here_doc) {
        return true;
    }

    /// Need continuation if we have unmatched brackets
    if (state->paren_count > 0 || state->bracket_count > 0 ||
        state->brace_count > 0) {
        return true;
    }

    return false;
}

/**
 * @brief Read a complete command from interactive input
 *
 * Reads input lines using GNU readline until a syntactically complete
 * command is obtained. Handles multiline constructs like compound commands,
 * here documents, and quoted strings with appropriate continuation prompts.
 * For non-interactive shells, delegates to get_input().
 *
 * @return Allocated command string, or NULL on EOF or error
 */
char *ln_gets(void) {
    if (!is_interactive_shell()) {
        return get_input(stdin);
    }

    /// Initialize state if needed
    if (!state_initialized) {
        init_input_state(&global_state);
        state_initialized = true;
    }

    static char *accumulated_input = NULL;
    static size_t accumulated_size = 0;
    static size_t accumulated_capacity = 0;

    char *line = NULL;
    bool first_line = (accumulated_size == 0);

    while (true) {
        errno = 0;

        /// Get appropriate prompt
        const char *prompt;
        if (first_line) {
            prompt = NULL; /// Let readline system generate themed prompt
        } else {
            prompt = get_continuation_prompt(&global_state);
        }

        /// Get line using readline
        line = lush_readline_with_prompt(prompt);

        /// Print verbose output if -v is enabled and we got a line
        if (line && shell_opts.verbose) {
            fprintf(stderr, "%s\n", line);
            fflush(stderr);
        }

        if (!line) {
            /// EOF or error
            if (accumulated_input && *accumulated_input) {
                /// Return accumulated input and reset
                char *result = strdup(accumulated_input);
                free(accumulated_input);
                accumulated_input = NULL;
                accumulated_size = 0;
                accumulated_capacity = 0;
                init_input_state(&global_state);
                return result;
            }
            return NULL;
        }

        /// Analyze this line to update state
        analyze_line(line, &global_state);

        /// Handle accumulation
        size_t line_len = strlen(line);
        size_t needed_size =
            accumulated_size + line_len + 2; /// +1 for newline, +1 for null

        if (needed_size > accumulated_capacity) {
            size_t new_capacity =
                accumulated_capacity ? accumulated_capacity * 2 : 1024;
            while (new_capacity < needed_size) {
                new_capacity *= 2;
            }

            char *tmp = realloc(accumulated_input, new_capacity);
            if (!tmp) {
                error_syscall("error: realloc in ln_gets");
                free(accumulated_input);
                accumulated_input = NULL;
                accumulated_size = 0;
                accumulated_capacity = 0;
                free(line);
                return NULL;
            }
            accumulated_input = tmp;
            accumulated_capacity = new_capacity;
        }

        if (accumulated_size == 0) {
            /// First line
            strcpy(accumulated_input, line);
            accumulated_size = line_len;
        } else {
            /// Append with newline
            strcat(accumulated_input, "\n");
            strcat(accumulated_input, line);
            accumulated_size += line_len + 1;
        }

        /// Free individual line (readline allocates it)
        free(line);
        line = NULL;

        /// Check if input is complete
        if (is_input_complete(&global_state)) {
            char *result = accumulated_input;
            accumulated_input = NULL;
            accumulated_size = 0;
            accumulated_capacity = 0;

            /// Reset state for next input
            cleanup_input_state(&global_state);
            init_input_state(&global_state);

            /// Note: History is handled by readline system automatically
            return result;
        }

        first_line = false;
    }
}

/**
 * @brief Read a complete command from a file stream with multiline support
 *
 * Reads lines from the specified file stream, accumulating them until
 * a syntactically complete command is obtained. Handles compound commands,
 * here documents, and other multiline constructs in non-interactive mode.
 *
 * @param in File stream to read from, or NULL for stdin
 * @return Allocated complete command string, or NULL on EOF or error
 */
char *get_input_complete(FILE *in) {
    return get_input_complete_counted(in, NULL);
}

char *get_input_complete_counted(FILE *in, size_t *lines_consumed) {
    if (lines_consumed) {
        *lines_consumed = 0;
    }

    /// For non-interactive mode, accumulate lines for complete constructs
    if (!in)
        in = stdin;

    char *accumulated = NULL;
    size_t accumulated_len = 0;
    input_state_t state = {0};
    /// Count of source lines this batch consumed. Includes lines joined
    /// via backslash continuation since they still advance the file's
    /// cumulative line counter even though they don't appear as
    /// newlines in the returned string.
    size_t source_lines = 0;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, in)) != -1) {
        source_lines++;
        /// Remove trailing newline for analysis
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
            read--;
        }

        /// Print verbose output if -v is enabled
        if (shell_opts.verbose) {
            fprintf(stderr, "%s\n", line);
            fflush(stderr);
        }

        /// Analyze this line to update state
        analyze_line(line, &state);

        /// Check if previous line had backslash continuation
        /// If so, we need to join lines without newline and remove the
        /// backslash
        bool had_backslash_continuation = false;
        if (accumulated_len > 0 && accumulated[accumulated_len - 1] == '\\') {
            /// Remove trailing backslash from accumulated
            accumulated[accumulated_len - 1] = '\0';
            accumulated_len--;
            had_backslash_continuation = true;
        }

        /// Accumulate the line
        if (accumulated == NULL) {
            accumulated =
                malloc(read + 2); /// +2 for newline and null terminator
            if (!accumulated) {
                free(line);
                return NULL;
            }
            strcpy(accumulated, line);
            accumulated_len = read;
        } else {
            size_t new_len = accumulated_len + read +
                             2; /// +2 for newline and null terminator
            char *new_accumulated = realloc(accumulated, new_len);
            if (!new_accumulated) {
                free(accumulated);
                free(line);
                return NULL;
            }
            accumulated = new_accumulated;
            /// Only add newline if previous line didn't have backslash
            /// continuation
            if (!had_backslash_continuation) {
                strcat(accumulated, "\n");
                accumulated_len++;
            }
            strcat(accumulated, line);
            accumulated_len += read;
        }

        /// Check if we have a complete construct
        if (!needs_continuation(&state)) {
            break;
        }
    }

    /// If we reach EOF while waiting for continuation, handle gracefully
    /// This prevents hanging on malformed input while preserving legitimate
    /// multiline support
    if (accumulated != NULL && needs_continuation(&state)) {
        /// We have partial input that needs continuation but hit EOF
        /// Check what type of continuation we're waiting for
        if (state.in_single_quote || state.in_double_quote) {
            /// Unterminated quotes in non-interactive mode should be syntax
            /// errors Don't wait indefinitely - return to parser for error
            /// handling
            free(line);
            return accumulated;
        } else if (!state.in_here_doc) {
            /// Other non-here-document continuations should also be handled as
            /// syntax errors on EOF
            free(line);
            return accumulated;
        }
        /// For here documents, continue normal processing (this is expected
        /// behavior)
    }

    free(line);
    if (lines_consumed) {
        *lines_consumed = source_lines;
    }
    return accumulated;
}

/**
 * @brief Unified input function for both interactive and non-interactive modes
 *
 * Main entry point for reading shell input. Automatically selects the
 * appropriate input method based on whether the shell is interactive.
 * Both modes support multiline constructs.
 *
 * @param in File stream for non-interactive mode, or NULL for stdin
 * @return Allocated complete command string, or NULL on EOF or error
 */
char *get_unified_input(FILE *in) { return get_unified_input_at(in, NULL); }

char *get_unified_input_at(FILE *in, size_t *lines_consumed) {
    if (is_interactive_shell()) {
        /// Each readline batch is one logical input; for source-location
        /// tracking purposes treat it as a single line of an implicit
        /// <stdin> source. The cumulative file-line counter in the main
        /// loop stays at 1 for interactive mode regardless of this
        /// value, so even multi-line constructs entered interactively
        /// always display as line-1 relative — matching bash.
        if (lines_consumed) {
            *lines_consumed = 1;
        }
        return ln_gets();
    } else {
        /// Non-interactive mode - use file input with multiline support for
        /// here documents. Counts actual source lines for cumulative tracking.
        return get_input_complete_counted(in, lines_consumed);
    }
}

/**
 * @brief Legacy compatibility wrapper for ln_gets
 *
 * Provides backward compatibility with code that used the old
 * ln_gets_complete() function name.
 *
 * @return Allocated complete command string, or NULL on EOF or error
 */
char *ln_gets_complete(void) { return ln_gets(); }