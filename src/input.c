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
#include "input_continuation.h"
#include "lle/history.h"
#include "lle/lle_shell_integration.h"
#include "lush.h"
#include "shell_mode.h"
#include "symtable.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Single canonical analyzer: the multiline-input state and the
/// per-line analyzer live in input_continuation.c (see
/// input_continuation.h). This file used to carry a parallel
/// `input_state_t` + private `analyze_line`/`is_input_complete`
/// implementation, which had drifted ~200 lines from the shared one
/// (missing the herestring `<<<` guard, missing trailing `&&`/`||`
/// continuation, missing the context stack, etc.). The duplicate is
/// retired here; this file now only carries the reader plumbing
/// (getline loop, accumulator buffer, prompt selection) and dispatches
/// the state work to the shared API. Drives issue #151 / duplicate-
/// path audit.
static continuation_state_t global_state = {0};
static bool state_initialized = false;

/// ============================================================================
/// CONTINUATION PROMPT SELECTION
/// ============================================================================

/// Return the prompt string for a partially-read multiline construct.
/// Inspects the shared continuation_state_t to choose between
/// "quote>", "function>", "if>", "loop>", "case>", or the user's PS2
/// for the generic continuation case. Static because only the
/// LLE-driven readline path consults it; everything else asks the
/// shared API directly.
static const char *get_continuation_prompt(const continuation_state_t *state) {
    if (!state)
        return "> ";

    const char *ps2 = symtable_get_global_default("PS2", "> ");

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
    continuation_state_cleanup(&global_state);
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
        continuation_state_init(&global_state);
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

            /// Keep history-expansion verify behavior in step with the live
            /// feature (shopt/setopt histverify) before reading the line.
            /// The feature matrix is the source of truth; the engine flag is
            /// a pushed cache consulted by the readline accept path.
            lle_history_expansion_set_verify(
                shell_mode_allows(FEATURE_HIST_VERIFY));
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
                continuation_state_init(&global_state);
                return result;
            }
            return NULL;
        }

        /// Analyze this line to update state
        continuation_analyze_line(line, &global_state);

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
        if (continuation_is_complete(&global_state)) {
            char *result = accumulated_input;
            accumulated_input = NULL;
            accumulated_size = 0;
            accumulated_capacity = 0;

            /// Reset state for next input
            continuation_state_cleanup(&global_state);
            continuation_state_init(&global_state);

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
    continuation_state_t state = {0};
    continuation_state_init(&state);
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
        continuation_analyze_line(line, &state);

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
                continuation_state_cleanup(&state);
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
                continuation_state_cleanup(&state);
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
        if (!continuation_needs_continuation(&state)) {
            break;
        }
    }

    /// If we reach EOF while waiting for continuation, handle gracefully
    /// This prevents hanging on malformed input while preserving legitimate
    /// multiline support
    if (accumulated != NULL && continuation_needs_continuation(&state)) {
        /// We have partial input that needs continuation but hit EOF
        /// Check what type of continuation we're waiting for
        if (state.in_single_quote || state.in_double_quote) {
            /// Unterminated quotes in non-interactive mode should be syntax
            /// errors Don't wait indefinitely - return to parser for error
            /// handling
            free(line);
            if (lines_consumed) {
                *lines_consumed = source_lines;
            }
            continuation_state_cleanup(&state);
            return accumulated;
        } else if (!state.in_here_doc) {
            /// Other non-here-document continuations should also be handled as
            /// syntax errors on EOF
            free(line);
            if (lines_consumed) {
                *lines_consumed = source_lines;
            }
            continuation_state_cleanup(&state);
            return accumulated;
        }
        /// For here documents, continue normal processing (this is expected
        /// behavior)
    }

    free(line);
    if (lines_consumed) {
        *lines_consumed = source_lines;
    }
    continuation_state_cleanup(&state);
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