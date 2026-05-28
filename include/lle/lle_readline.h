/**
 * @file lle_readline.h
 * @brief LLE Readline Function - Public API
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This header declares lle_readline(), the main entry point for interactive
 * line editing in LLE. This function replaces GNU readline's readline() when
 * LLE is enabled.
 *
 * Usage:
 *   char *line = lle_readline("prompt> ");
 *   if (line != NULL) {
 *       /// Process line
 *       free(line);  /// Caller must free
 *   }
 */

#ifndef LLE_READLINE_H
#define LLE_READLINE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a line of input from the user with line editing
 *
 * This function orchestrates all LLE subsystems to provide interactive line
 * editing similar to GNU readline. It:
 * - Displays the prompt
 * - Reads user input character by character
 * - Supports line editing operations (insert, delete, cursor movement)
 * - Handles special keys (arrows, Home, End, etc.)
 * - Supports multiline editing for incomplete commands
 * - Returns the completed line when user presses Enter
 *
 * The function enters raw terminal mode during execution and restores the
 * terminal state before returning.
 *
 * @param prompt The prompt string to display (e.g., "$ ", "> ")
 *               May be NULL for no prompt.
 *
 * @return Newly allocated string containing the user's input line.
 *         The caller is responsible for freeing this memory with free().
 *         Returns NULL on:
 *         - EOF (Ctrl-D on empty line)
 *         - Interrupt (Ctrl-C)
 *         - Error (LLE system not initialized, terminal error, etc.)
 *
 * @note This function is NOT reentrant. Only one readline operation
 *       should be active at a time.
 *
 * @note The LLE system must be initialized with lle_system_initialize()
 *       before calling this function.
 */
char *lle_readline(const char *prompt);

/**
 * @brief Read a line like lle_readline(), recording nothing to history
 *
 * Identical to lle_readline() except the returned line is never added
 * to the in-memory history nor written to the history file. Intended
 * for transient prompts whose input must not pollute the user's shell
 * history -- for example the debugger's interactive break-prompt.
 *
 * @param prompt The prompt string to display (may be NULL).
 * @return Newly allocated input line (caller frees with free()), or
 *         NULL on EOF / interrupt / error -- same contract as
 *         lle_readline().
 *
 * @note Not reentrant. Shares lle_readline()'s single-active-call
 *       contract.
 */
char *lle_readline_no_history(const char *prompt);

/**
 * @brief Is the currently-active prompt the debugger's break-prompt?
 *
 * Returns true between the call to lle_readline_no_history() and its
 * return. Completion sources consult this to switch from shell
 * completions to debug-command completions at the (lush-debug) prompt
 * -- see src/lle/completion/source_manager.c.
 *
 * Defined in lle_debug_prompt_state.c (separated from lle_readline.c
 * so that completion-source .o files do not chain lle_readline.c.o's
 * shell-symbol dependencies into LLE-isolated test binaries).
 *
 * @return true if a debug-prompt read is in progress, false otherwise.
 */
bool lle_in_debug_prompt(void);

/**
 * @brief Set the debug-prompt-active flag
 *
 * Called by lle_readline_no_history() to bracket the in-flight read.
 * Internal to the LLE subsystem; external callers should use
 * lle_readline_no_history() rather than touching the flag directly.
 *
 * @param active true on entry, false on return.
 */
void lle_set_debug_prompt_active(bool active);

#ifdef __cplusplus
}
#endif

#endif /// LLE_READLINE_H
