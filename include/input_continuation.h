/**
 * @file input_continuation.h
 * @brief Shared multiline input continuation system
 *
 * Provides shared multiline parsing functionality for both the Line Editing
 * Engine (LLE) and the main input system. Handles shell construct detection,
 * quote tracking, bracket counting, and continuation line analysis.
 *
 * This is shared infrastructure used by multiple components of Lush.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef INPUT_CONTINUATION_H
#define INPUT_CONTINUATION_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Context types for the context stack
 *
 * Used to track nested shell constructs for proper prompt display.
 */
typedef enum {
    CONTEXT_NONE = 0,   /**< No special context */
    CONTEXT_IF,         /**< Inside if statement */
    CONTEXT_WHILE,      /**< Inside while loop */
    CONTEXT_FOR,        /**< Inside for loop */
    CONTEXT_UNTIL,      /**< Inside until loop */
    CONTEXT_CASE,       /**< Inside case statement */
    CONTEXT_FUNCTION,   /**< Inside function definition */
    CONTEXT_BRACE_GROUP /**< Inside brace group { } */
} continuation_context_type_t;

/**
 * @brief Maximum nesting depth for context stack
 */
#define CONTINUATION_MAX_CONTEXT_DEPTH 32

/**
 * @brief Continuation state structure
 *
 * Tracks the parsing state for multiline input constructs including:
 * - Quote tracking (single, double, backtick)
 * - Bracket/brace/parenthesis counting
 * - Control structure detection (if/then/fi, case, loops)
 * - Here document handling
 * - Function definitions
 * - Context stack for proper nested construct tracking
 */
typedef struct {
    /* Quote tracking */
    int quote_count;        /**< Single quote count */
    int double_quote_count; /**< Double quote count */
    int backtick_count;     /**< Backtick count */
    bool in_single_quote;   /**< Currently inside single quotes */
    bool in_double_quote;   /**< Currently inside double quotes */
    bool in_backtick;       /**< Currently inside backticks */

    /* Bracket/brace/parenthesis tracking */
    int paren_count;   /**< Parenthesis nesting depth */
    int brace_count;   /**< Brace nesting depth */
    int bracket_count; /**< Bracket nesting depth */

    /* Escape and continuation */
    bool escaped;          /**< Previous character was backslash */
    bool has_continuation; /**< Line ends with backslash */

    /* Here document handling */
    bool in_here_doc;         /**< Currently in here document */
    char *here_doc_delimiter; /**< Here document delimiter string */

    /* Command substitution and arithmetic */
    bool in_command_substitution; /**< Inside $(...) */
    bool in_arithmetic;           /**< Inside $((...)) */

    /* Control structure tracking (legacy flags - kept for compatibility) */
    bool in_function_definition; /**< Inside function definition */
    bool saw_posix_func_parens;  /**< Saw name() pattern, waiting for { */
    bool in_case_statement;      /**< Inside case statement */
    bool in_if_statement;        /**< Inside if statement */
    bool in_while_loop;          /**< Inside while loop */
    bool in_for_loop;            /**< Inside for loop */
    bool in_until_loop;          /**< Inside until loop */
    int compound_command_depth;  /**< Nesting depth of compound commands */

    /* Context stack for nested construct tracking */
    /** @brief Stack of nested contexts for proper prompt switching */
    continuation_context_type_t context_stack[CONTINUATION_MAX_CONTEXT_DEPTH];
    int context_stack_depth; /**< Current depth of context stack */
} continuation_state_t;

/**
 * @brief Initialize a continuation state structure
 *
 * Sets all fields to their initial values (zeros/false).
 *
 * @param state Pointer to state structure to initialize
 */
void continuation_state_init(continuation_state_t *state);

/**
 * @brief Clean up a continuation state structure
 *
 * Frees any dynamically allocated memory (e.g., here_doc_delimiter)
 * and resets the structure to initial state.
 *
 * @param state Pointer to state structure to clean up
 */
void continuation_state_cleanup(continuation_state_t *state);

/**
 * @brief Analyze a line and update the continuation state
 *
 * Parses the line for shell constructs, quotes, brackets, control
 * keywords, and updates the state accordingly. This is the core
 * parsing function.
 *
 * @param line The line to analyze (null-terminated string)
 * @param state Pointer to state structure to update
 */
void continuation_analyze_line(const char *line, continuation_state_t *state);

/**
 * @brief Check if the current input is complete
 *
 * Returns true if all constructs are closed and the input does not
 * require continuation. Returns false if the input needs more lines.
 *
 * @param state Pointer to state structure to check
 * @return true if input is complete, false if continuation is needed
 */
bool continuation_is_complete(const continuation_state_t *state);

/**
 * @brief Check if the current state needs continuation
 *
 * Similar to continuation_is_complete but returns the opposite value.
 * This is a convenience function.
 *
 * @param state Pointer to state structure to check
 * @return true if continuation is needed, false if input is complete
 */
bool continuation_needs_continuation(const continuation_state_t *state);

/**
 * @brief Get an appropriate continuation prompt string
 *
 * Returns a prompt string based on the current parsing state.
 * The returned string is a static or symbol table string and should
 * not be freed by the caller.
 *
 * @param state Pointer to state structure
 * @return Prompt string (e.g., "> ", "quote> ", "if> ", etc.)
 */
const char *continuation_get_prompt(const continuation_state_t *state);

/**
 * @brief Check if a word is a shell control keyword
 *
 * Returns true for keywords like "if", "then", "while", "do", etc.
 *
 * @param word The word to check (null-terminated string)
 * @return true if word is a control keyword, false otherwise
 */
bool continuation_is_control_keyword(const char *word);

/**
 * @brief Check if a line is a control structure terminator
 *
 * Returns true for terminators like "fi", "done", "esac", "}".
 *
 * @param line The line to check (null-terminated string)
 * @return true if line is a terminator, false otherwise
 */
bool continuation_is_terminator(const char *line);

#endif /* INPUT_CONTINUATION_H */
