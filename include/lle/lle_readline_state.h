/**
 * @file lle_readline_state.h
 * @brief LLE Readline State Machine
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides explicit state management for the readline input loop with
 * guaranteed exit paths. This replaces implicit flag-based state tracking
 * with a formal state machine that ensures Ctrl+C and Ctrl+G can always
 * exit from any state.
 *
 * State Machine Design:
 * - Normal states: IDLE, EDITING, COMPLETION, SEARCH, MULTILINE, QUOTED_INSERT
 * - Terminal states: DONE, ABORT, EOF, TIMEOUT, ERROR
 * - INVARIANT: Transitions to terminal states ALWAYS succeed
 * - INVARIANT: Ctrl+C/Ctrl+G can exit from ANY non-terminal state
 *
 * @see src/lle/lle_readline_state.c for implementation
 * @see src/lle/lle_readline.c for integration
 */

#ifndef LLE_READLINE_STATE_H
#define LLE_READLINE_STATE_H

#include "lle/error_handling.h"

#include <stdbool.h>

/**
 * @brief Readline state machine states
 *
 * States are ordered such that terminal states come last, allowing
 * a simple >= comparison to check if readline should exit.
 */
typedef enum {
    /* ========== Normal (Non-Terminal) States ========== */

    /** Initial state before any input received */
    LLE_READLINE_STATE_IDLE = 0,

    /** Normal text editing mode (default operating state) */
    LLE_READLINE_STATE_EDITING,

    /** Completion menu is visible and accepting navigation */
    LLE_READLINE_STATE_COMPLETION,

    /** Interactive history search active (Ctrl+R) */
    LLE_READLINE_STATE_SEARCH,

    /** Multiline input mode (incomplete command, awaiting more input) */
    LLE_READLINE_STATE_MULTILINE,

    /** Quoted insert mode (Ctrl+V - next char inserted literally) */
    LLE_READLINE_STATE_QUOTED_INSERT,

    /* ========== Terminal States (Readline Exits) ========== */

    /** Line accepted successfully (Enter pressed, input complete) */
    LLE_READLINE_STATE_DONE,

    /** Line aborted by user (Ctrl+G or Ctrl+C) */
    LLE_READLINE_STATE_ABORT,

    /** EOF received (Ctrl+D on empty line) */
    LLE_READLINE_STATE_EOF,

    /** Watchdog timeout - no input for too long */
    LLE_READLINE_STATE_TIMEOUT,

    /** Unrecoverable error occurred */
    LLE_READLINE_STATE_ERROR,

    /** Sentinel for iteration/validation */
    LLE_READLINE_STATE_COUNT
} lle_readline_state_t;

/**
 * @brief First terminal state (for range checking)
 */
#define LLE_READLINE_STATE_FIRST_TERMINAL LLE_READLINE_STATE_DONE

/**
 * @brief Check if state is a terminal state (readline should exit)
 *
 * Terminal states indicate that the readline loop should exit and
 * return control to the caller.
 *
 * @param state State to check
 * @return true if state is terminal, false otherwise
 */
static inline bool lle_readline_state_is_terminal(lle_readline_state_t state) {
    return state >= LLE_READLINE_STATE_FIRST_TERMINAL;
}

/**
 * @brief Check if state is a normal (non-terminal) state
 *
 * @param state State to check
 * @return true if state is normal, false if terminal
 */
static inline bool lle_readline_state_is_normal(lle_readline_state_t state) {
    return state < LLE_READLINE_STATE_FIRST_TERMINAL;
}

/**
 * @brief Check if state allows text input
 *
 * Some states like COMPLETION and SEARCH have special input handling
 * but still allow transitioning back to EDITING.
 *
 * @param state State to check
 * @return true if text input is accepted
 */
static inline bool
lle_readline_state_accepts_input(lle_readline_state_t state) {
    switch (state) {
    case LLE_READLINE_STATE_EDITING:
    case LLE_READLINE_STATE_COMPLETION:
    case LLE_READLINE_STATE_SEARCH:
    case LLE_READLINE_STATE_MULTILINE:
    case LLE_READLINE_STATE_QUOTED_INSERT:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Get human-readable state name for debugging/logging
 *
 * @param state State to describe
 * @return Static string with state name (never NULL)
 */
const char *lle_readline_state_name(lle_readline_state_t state);

/**
 * @brief Validate that a state value is within valid range
 *
 * @param state State to validate
 * @return true if valid, false if out of range
 */
static inline bool lle_readline_state_is_valid(lle_readline_state_t state) {
    return state >= LLE_READLINE_STATE_IDLE && state < LLE_READLINE_STATE_COUNT;
}

// Forward declaration for readline context
struct readline_context;

/**
 * @brief Transition to a new state with validation
 *
 * Performs the state transition and any necessary cleanup/setup for
 * the new state. Validates that the transition is legal.
 *
 * INVARIANT: Transitions to terminal states (ABORT, EOF, TIMEOUT, ERROR)
 * ALWAYS succeed regardless of current state. This is the escape hatch.
 *
 * @param ctx      Readline context (contains current state)
 * @param new_state Target state
 * @return LLE_SUCCESS on successful transition
 * @return LLE_ERROR_INVALID_STATE if transition is not allowed
 */
lle_result_t lle_readline_state_transition(struct readline_context *ctx,
                                           lle_readline_state_t new_state);

/**
 * @brief Force transition to abort state
 *
 * This function NEVER fails. It is the escape hatch for Ctrl+C and Ctrl+G.
 * Clears all modal states (completion menu, search, etc.) and sets the
 * state to ABORT.
 *
 * @param ctx Readline context
 */
void lle_readline_state_force_abort(struct readline_context *ctx);

/**
 * @brief Force transition to EOF state
 *
 * This function NEVER fails. Used when Ctrl+D is pressed on empty line.
 *
 * @param ctx Readline context
 */
void lle_readline_state_force_eof(struct readline_context *ctx);

/**
 * @brief Force transition to timeout state
 *
 * This function NEVER fails. Used when watchdog timer expires.
 *
 * @param ctx Readline context
 */
void lle_readline_state_force_timeout(struct readline_context *ctx);

/**
 * @brief Force transition to error state
 *
 * This function NEVER fails. Used on unrecoverable errors.
 *
 * @param ctx Readline context
 */
void lle_readline_state_force_error(struct readline_context *ctx);

/**
 * @brief Reset state machine to initial state
 *
 * Called at the start of each readline() call to ensure clean state.
 *
 * @param ctx Readline context
 */
void lle_readline_state_reset(struct readline_context *ctx);

/**
 * @brief Get the current state from context
 *
 * @param ctx Readline context
 * @return Current state
 */
lle_readline_state_t lle_readline_state_get(const struct readline_context *ctx);

/**
 * @brief Get the previous state (for debugging/logging)
 *
 * @param ctx Readline context
 * @return Previous state before last transition
 */
lle_readline_state_t
lle_readline_state_get_previous(const struct readline_context *ctx);

#endif // LLE_READLINE_STATE_H
