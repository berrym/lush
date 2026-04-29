/**
 * @file input.h
 * @brief Shell input handling and line reading
 *
 * Provides core input functions for reading commands from various sources
 * including interactive terminals, files, and pipes. Supports multiline
 * input with continuation prompts.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdio.h>

/**
 * @brief Free all input-related buffers
 *
 * Releases memory allocated for input processing buffers.
 * Should be called during shell cleanup.
 */
void free_input_buffers(void);

/**
 * @brief Read a line of input from a file stream
 *
 * Reads a single line of input from the specified file stream.
 * Does not handle multiline continuation.
 *
 * @param in Input file stream to read from
 * @return Allocated string containing the input line, or NULL on EOF/error
 */
char *get_input(FILE *in);

/**
 * @brief Read a line of input using the line editor
 *
 * Reads input interactively using LLE (Lush Line Editor) with
 * command-line editing, history, and completion support.
 *
 * @return Allocated string containing the input line, or NULL on EOF/error
 */
char *ln_gets(void);

/**
 * @brief Read complete input with multiline continuation handling
 *
 * Reads input from the specified file stream, handling multiline
 * continuation for incomplete shell constructs (quotes, brackets,
 * control structures).
 *
 * @param in Input file stream to read from
 * @return Allocated string containing the complete input, or NULL on EOF/error
 */
char *get_input_complete(FILE *in);

/**
 * @brief Read complete shell input batch with source-line accounting
 *
 * Same as get_input_complete() but additionally reports how many source
 * lines from the input stream were consumed by this batch (including
 * lines that were joined via backslash continuation, since those still
 * advance the file's cumulative line counter even though they don't
 * appear as newlines in the returned string).
 *
 * @param in              Input file stream to read from
 * @param lines_consumed  Out-pointer set to the number of source lines
 *                        consumed by this batch (1 minimum on success);
 *                        may be NULL if the caller does not need the count
 * @return Allocated string containing the complete input, or NULL on
 *         EOF/error
 */
char *get_input_complete_counted(FILE *in, size_t *lines_consumed);

/**
 * @brief Read unified input from file stream
 *
 * Primary input function that handles both interactive and non-interactive
 * input with proper multiline continuation detection.
 *
 * @param in Input file stream to read from
 * @return Allocated string containing the input, or NULL on EOF/error
 */
char *get_unified_input(FILE *in);

/**
 * @brief Read unified input with source-line accounting
 *
 * Same as get_unified_input() but reports how many source lines from
 * the underlying stream were consumed by this batch. For interactive
 * mode the count is set to 1 (each readline batch is its own logical
 * input); for non-interactive mode the count reflects the actual
 * number of source lines in the underlying file.
 *
 * @param in              Input file stream (or NULL for stdin)
 * @param lines_consumed  Out-pointer for source-line count; may be NULL
 * @return Allocated string containing the input, or NULL on EOF/error
 */
char *get_unified_input_at(FILE *in, size_t *lines_consumed);

/**
 * @brief Read complete input using line editor with continuation
 *
 * @deprecated Use get_unified_input() instead
 *
 * Legacy function that reads input interactively with multiline
 * continuation support.
 *
 * @return Allocated string containing the complete input, or NULL on EOF/error
 */
char *ln_gets_complete(void);

/**
 * @brief Get the current continuation prompt string
 *
 * Returns the appropriate prompt to display for multiline input
 * continuation based on the current parsing context.
 *
 * @return Continuation prompt string (e.g., "> ", "quote> ")
 */
const char *lush_get_current_continuation_prompt(void);

#endif
