/**
 * @file input_continuation.c
 * @brief Shared Multiline Input Continuation System for Lush Shell
 *
 * This module provides shared multiline parsing functionality for both the
 * Line Editing Engine (LLE) and the main input system. It tracks the parsing
 * state across multiple lines to determine when a complete syntactic unit
 * has been entered.
 *
 * Features:
 * - Quote tracking (single, double, backtick)
 * - Bracket/brace/parenthesis balancing
 * - Here document detection and termination
 * - Control flow keyword tracking (if/then/fi, while/do/done, etc.)
 * - Context stack for proper nested construct handling
 * - Line continuation (backslash at end of line)
 *
 * UTF-8 Support:
 * This module uses LLE's UTF-8 support to properly handle multi-byte
 * characters. While shell syntax characters (quotes, brackets, etc.) are
 * all ASCII, we must properly skip over UTF-8 multi-byte sequences to
 * avoid misinterpreting continuation bytes as syntax characters.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "input_continuation.h"
#include "lle/utf8_support.h"
#include "symtable.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

/// ============================================================================
/// STATE MANAGEMENT
/// ============================================================================

/**
 * @brief Initialize a continuation state structure to default values
 *
 * Zeros out all fields in the continuation state and sets the here document
 * delimiter to NULL. Must be called before first use of a continuation_state_t.
 *
 * @param state Pointer to the continuation state structure to initialize
 */
void continuation_state_init(continuation_state_t *state) {
    if (!state)
        return;
    memset(state, 0, sizeof(continuation_state_t));
    state->here_doc_delimiter = NULL;
}

/**
 * @brief Clean up a continuation state structure and free allocated memory
 *
 * Frees the here document delimiter if allocated and zeros the structure.
 * Safe to call on an already-cleaned or uninitialized state.
 *
 * @param state Pointer to the continuation state structure to clean up
 */
void continuation_state_cleanup(continuation_state_t *state) {
    if (!state)
        return;

    if (state->here_doc_delimiter) {
        free(state->here_doc_delimiter);
        state->here_doc_delimiter = NULL;
    }
    memset(state, 0, sizeof(continuation_state_t));
}

/// ============================================================================
/// CONTEXT STACK MANAGEMENT
/// ============================================================================

/**
 * @brief Push a context onto the context stack
 *
 * Adds a new context type to track nested constructs. Used when entering
 * compound commands like if, while, for, case, or brace groups.
 *
 * @param state The continuation state containing the context stack
 * @param ctx The context type to push
 */
static void context_stack_push(continuation_state_t *state,
                               continuation_context_type_t ctx) {
    if (!state)
        return;
    if (state->context_stack_depth >= CONTINUATION_MAX_CONTEXT_DEPTH)
        return;

    state->context_stack[state->context_stack_depth++] = ctx;
}

/**
 * @brief Pop a context from the stack if it matches the expected type
 *
 * Removes the top context from the stack if it matches the expected type.
 * For loop contexts (for/while/until), any loop type matches since they
 * all terminate with 'done'.
 *
 * @param state The continuation state containing the context stack
 * @param expected The expected context type to pop
 * @return true if context was popped, false if stack empty or mismatch
 */
static bool context_stack_pop(continuation_state_t *state,
                              continuation_context_type_t expected) {
    if (!state || state->context_stack_depth <= 0)
        return false;

    /// Check if top matches expected (for proper nesting validation)
    continuation_context_type_t top =
        state->context_stack[state->context_stack_depth - 1];
    if (top == expected) {
        state->context_stack_depth--;
        return true;
    }

    /// For loops, accept any loop type (for/while/until all end with 'done')
    if (expected == CONTEXT_FOR || expected == CONTEXT_WHILE ||
        expected == CONTEXT_UNTIL) {
        if (top == CONTEXT_FOR || top == CONTEXT_WHILE ||
            top == CONTEXT_UNTIL) {
            state->context_stack_depth--;
            return true;
        }
    }

    return false;
}

/**
 * @brief Pop any loop context from the stack
 *
 * Used when processing the 'done' keyword which terminates for, while,
 * and until loops. Pops if the top context is any loop type.
 *
 * @param state The continuation state containing the context stack
 * @return true if a loop context was popped, false otherwise
 */
static bool context_stack_pop_loop(continuation_state_t *state) {
    if (!state || state->context_stack_depth <= 0)
        return false;

    continuation_context_type_t top =
        state->context_stack[state->context_stack_depth - 1];
    if (top == CONTEXT_FOR || top == CONTEXT_WHILE || top == CONTEXT_UNTIL) {
        state->context_stack_depth--;
        return true;
    }
    return false;
}

/**
 * @brief Get the current (topmost) context from the stack
 *
 * Returns the context type at the top of the stack without removing it.
 * Used to determine the current nesting context for prompt generation.
 *
 * @param state The continuation state containing the context stack
 * @return The topmost context type, or CONTEXT_NONE if stack is empty
 */
static continuation_context_type_t
context_stack_top(const continuation_state_t *state) {
    if (!state || state->context_stack_depth <= 0)
        return CONTEXT_NONE;
    return state->context_stack[state->context_stack_depth - 1];
}

/// ============================================================================
/// KEYWORD DETECTION
/// ============================================================================

/**
 * @brief Check if a word is a shell control flow keyword
 *
 * Identifies shell reserved words that affect control flow and require
 * special handling for multiline input parsing (if, then, else, fi,
 * while, for, case, function, etc.).
 *
 * @param word The word to check
 * @return true if word is a control keyword, false otherwise
 */
bool continuation_is_control_keyword(const char *word) {
    if (!word)
        return false;

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
bool continuation_is_terminator(const char *line) {
    if (!line)
        return false;

    /// Skip whitespace
    while (*line && isspace(*line))
        line++;

    /// Check for terminators
    return (strncmp(line, "fi", 2) == 0 || strncmp(line, "done", 4) == 0 ||
            strncmp(line, "esac", 4) == 0 || strcmp(line, "}") == 0);
}

/// ============================================================================
/// LINE ANALYSIS
/// ============================================================================

/**
 * @brief Analyze a line of input to update continuation parsing state
 *
 * Processes a line character by character to track quotes, parentheses,
 * braces, brackets, here documents, escape sequences, and control keywords.
 * Updates the continuation state structure to reflect what constructs are
 * currently open and whether continuation input is needed.
 *
 * @param line The line of input to analyze
 * @param state The continuation state structure to update
 */
void continuation_analyze_line(const char *line, continuation_state_t *state) {
    if (!line || !state)
        return;

    const char *p = line;
    char word[256] = {0};
    int word_pos = 0;

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

        /// Handle here document detection (but not herestring <<<)
        if (c == '<' && *(p + 1) == '<' && *(p + 2) != '<' &&
            !state->in_here_doc) {
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
        } else if (c == '{' || c == '}') {
            /// Handle { and } as single-character keywords
            if (word_pos > 0) {
                /// Process any accumulated word first
                word[word_pos] = '\0';

                /// Check for control keywords
                if (continuation_is_control_keyword(word)) {

                    if (strcmp(word, "if") == 0) {
                        state->in_if_statement = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_IF);
                    } else if (strcmp(word, "while") == 0) {
                        state->in_while_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_WHILE);
                    } else if (strcmp(word, "for") == 0) {
                        state->in_for_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_FOR);
                    } else if (strcmp(word, "until") == 0) {
                        state->in_until_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_UNTIL);
                    } else if (strcmp(word, "case") == 0) {
                        state->in_case_statement = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_CASE);
                    } else if (strcmp(word, "function") == 0) {
                        state->in_function_definition = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_FUNCTION);
                    } else if (strcmp(word, "fi") == 0) {
                        state->in_if_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop(state, CONTEXT_IF);
                    } else if (strcmp(word, "done") == 0) {
                        state->in_while_loop = false;
                        state->in_for_loop = false;
                        state->in_until_loop = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop_loop(state);
                    } else if (strcmp(word, "esac") == 0) {
                        state->in_case_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop(state, CONTEXT_CASE);
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
                    context_stack_push(state, CONTEXT_FUNCTION);
                    state->saw_posix_func_parens = false;
                } else if (!state->in_function_definition) {
                    /// Regular brace group (not a function)
                    state->compound_command_depth++;
                    context_stack_push(state, CONTEXT_BRACE_GROUP);
                }
                /// If already in_function_definition (from 'function' keyword),
                /// don't push again - the keyword handler already did
            } else if (c == '}') {
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                context_stack_pop(state, CONTEXT_BRACE_GROUP);
                /// Also try popping function context
                if (state->context_stack_depth > 0 &&
                    context_stack_top(state) == CONTEXT_FUNCTION) {
                    context_stack_pop(state, CONTEXT_FUNCTION);
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
                if (continuation_is_control_keyword(word)) {

                    if (strcmp(word, "if") == 0) {
                        state->in_if_statement = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_IF);
                    } else if (strcmp(word, "while") == 0) {
                        state->in_while_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_WHILE);
                    } else if (strcmp(word, "for") == 0) {
                        state->in_for_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_FOR);
                    } else if (strcmp(word, "until") == 0) {
                        state->in_until_loop = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_UNTIL);
                    } else if (strcmp(word, "case") == 0) {
                        state->in_case_statement = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_CASE);
                    } else if (strcmp(word, "function") == 0) {
                        state->in_function_definition = true;
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_FUNCTION);
                    } else if (strcmp(word, "{") == 0) {
                        state->compound_command_depth++;
                        context_stack_push(state, CONTEXT_BRACE_GROUP);
                    } else if (strcmp(word, "fi") == 0) {
                        state->in_if_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop(state, CONTEXT_IF);
                    } else if (strcmp(word, "done") == 0) {
                        state->in_while_loop = false;
                        state->in_for_loop = false;
                        state->in_until_loop = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop_loop(state);
                    } else if (strcmp(word, "esac") == 0) {
                        state->in_case_statement = false;
                        state->has_continuation = false;
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop(state, CONTEXT_CASE);
                    } else if (strcmp(word, "}") == 0) {
                        if (state->compound_command_depth > 0) {
                            state->compound_command_depth--;
                        }
                        context_stack_pop(state, CONTEXT_BRACE_GROUP);
                        if (state->compound_command_depth == 0) {
                            state->in_function_definition = false;
                        }
                    }
                }

                word_pos = 0;
                memset(word, 0, sizeof(word));
            }
        }

        p++;
    }

    /// Check if line ends with unescaped backslash (line continuation)
    /// Must check after loop completes to handle escaped flag correctly
    if (state->escaped) {
        state->has_continuation = true;
        state->escaped = false; /// Reset for next line
    }

    /// Trailing logical/pipe operator forces line continuation. A line
    /// ending in `&&`, `||`, or a bare `|` (after trailing whitespace,
    /// with the operator not inside quotes) has its statement
    /// obviously not complete -- the next command appears on the
    /// following line. Bash and zsh both keep reading; without this
    /// check, an `&&`-at-EOL is treated as one statement and the
    /// parser sees the operator with nothing on its right.
    ///
    /// The local analyzer in src/input.c already does this for the
    /// file/stdin reader path; this shared analyzer is used by the
    /// executor's per-batch splitter (issue #151) and by the LLE
    /// interactive multi-line driver. The two analyzers should
    /// eventually collapse onto a single canonical implementation
    /// (tracked: duplicate-path audit).
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

    /// Handle remaining word at end of line
    if (word_pos > 0) {
        word[word_pos] = '\0';

        if (continuation_is_control_keyword(word)) {
            /// Handle keywords found at end of line
            if (strcmp(word, "then") == 0 || strcmp(word, "do") == 0) {
                state->has_continuation = true;
            } else if (strcmp(word, "done") == 0) {
                state->in_while_loop = false;
                state->in_for_loop = false;
                state->in_until_loop = false;
                state->has_continuation = false;
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                context_stack_pop_loop(state);
            } else if (strcmp(word, "esac") == 0) {
                state->in_case_statement = false;
                state->has_continuation = false;
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                context_stack_pop(state, CONTEXT_CASE);
            } else if (strcmp(word, "}") == 0) {
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                context_stack_pop(state, CONTEXT_BRACE_GROUP);
                if (state->compound_command_depth == 0) {
                    state->in_function_definition = false;
                }
            } else if (strcmp(word, "fi") == 0) {
                state->in_if_statement = false;
                state->has_continuation = false;
                if (state->compound_command_depth > 0) {
                    state->compound_command_depth--;
                }
                context_stack_pop(state, CONTEXT_IF);
            }
        }
    }
}

/// ============================================================================
/// COMPLETION CHECKING
/// ============================================================================

/**
 * @brief Check if the current input is syntactically complete
 *
 * Examines the continuation state to determine if all opened constructs
 * (quotes, parentheses, compound commands, here documents, etc.) have
 * been closed and no continuation is needed.
 *
 * @param state The continuation state to check
 * @return true if input is complete, false if more input is needed
 */
bool continuation_is_complete(const continuation_state_t *state) {
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

    /// POSIX function header `name()` seen but body brace not yet
    /// opened. The function body may start on a following line; keep
    /// reading until the `{` arrives (analyze_line clears this flag
    /// when it does). Without this, `name()\n{ body }` is split into
    /// two statements and the parser sees `name()` followed by EOF.
    /// Ported from input.c's local needs_continuation as part of the
    /// dual-analyzer unification.
    if (state->saw_posix_func_parens) {
        return false;
    }

    return true;
}

/**
 * @brief Check if continuation input is needed
 *
 * Convenience function that returns the inverse of continuation_is_complete().
 * Returns true if the input is incomplete and more lines are needed.
 *
 * @param state The continuation state to check
 * @return true if more input is needed, false if input is complete
 */
bool continuation_needs_continuation(const continuation_state_t *state) {
    return !continuation_is_complete(state);
}

/// ============================================================================
/// PROMPT GENERATION
/// ============================================================================

/**
 * @brief Get the appropriate continuation prompt for the current state
 *
 * Returns a context-specific prompt based on what construct is currently
 * being entered (quote, function, if, while, for, until, case, brace group).
 * Uses the context stack for proper nested construct tracking. Falls back
 * to the PS2 environment variable or "> " if not set.
 *
 * @param state The current continuation state
 * @return Prompt string to display (static string, do not free)
 */
const char *continuation_get_prompt(const continuation_state_t *state) {
    if (!state)
        return "> ";

    /// Use PS2 from symbol table, with fallback
    const char *ps2 = symtable_get_global_default("PS2", "> ");

    /// Quote state takes highest priority (not tracked in context stack)
    if (state->in_single_quote || state->in_double_quote) {
        return "quote> ";
    }

    /// Use context stack for proper nested construct tracking
    /// The top of the stack represents the innermost active construct
    continuation_context_type_t current_ctx = context_stack_top(state);

    switch (current_ctx) {
    case CONTEXT_IF:
        return "if> ";
    case CONTEXT_WHILE:
        return "while> ";
    case CONTEXT_FOR:
        return "for> ";
    case CONTEXT_UNTIL:
        return "until> ";
    case CONTEXT_CASE:
        return "case> ";
    case CONTEXT_FUNCTION:
        return "func> ";
    case CONTEXT_BRACE_GROUP:
        return "brace> ";
    case CONTEXT_NONE:
    default:
        break;
    }

    /// Fallback to legacy boolean flags for backwards compatibility
    /// (in case context stack isn't being used consistently)
    if (state->in_function_definition) {
        return "func> ";
    } else if (state->in_if_statement) {
        return "if> ";
    } else if (state->in_while_loop) {
        return "while> ";
    } else if (state->in_for_loop) {
        return "for> ";
    } else if (state->in_until_loop) {
        return "until> ";
    } else if (state->in_case_statement) {
        return "case> ";
    }

    return ps2;
}
