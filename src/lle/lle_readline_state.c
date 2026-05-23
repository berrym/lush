/**
 * @file lle_readline_state.c
 * @brief LLE Readline State Machine Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements explicit state management for the readline input loop.
 * This provides guaranteed exit paths and replaces scattered flag checks
 * with centralized state transition logic.
 */

#include "lle/lle_readline_state.h"

#include <stddef.h>
#include <stdio.h>

/* Forward declare the readline context structure
 * The full definition is in lle_readline.c - we only need the state fields */
typedef struct readline_context {
    // We access these fields through the public API
    lle_readline_state_t state;
    lle_readline_state_t previous_state;
    // Other fields exist but we don't access them directly here
} readline_context_t;

/**
 * State names for debugging/logging
 */
static const char *const g_state_names[] = {
    [LLE_READLINE_STATE_IDLE] = "IDLE",
    [LLE_READLINE_STATE_EDITING] = "EDITING",
    [LLE_READLINE_STATE_COMPLETION] = "COMPLETION",
    [LLE_READLINE_STATE_SEARCH] = "SEARCH",
    [LLE_READLINE_STATE_MULTILINE] = "MULTILINE",
    [LLE_READLINE_STATE_QUOTED_INSERT] = "QUOTED_INSERT",
    [LLE_READLINE_STATE_DONE] = "DONE",
    [LLE_READLINE_STATE_ABORT] = "ABORT",
    [LLE_READLINE_STATE_EOF] = "EOF",
    [LLE_READLINE_STATE_TIMEOUT] = "TIMEOUT",
    [LLE_READLINE_STATE_ERROR] = "ERROR",
};

/**
 * @brief Get human-readable name for a readline state
 *
 * @param state The state to get the name for
 * @return String name of the state, or "INVALID" for unknown states
 */
const char *lle_readline_state_name(lle_readline_state_t state) {
    if (state >= LLE_READLINE_STATE_COUNT) {
        return "INVALID";
    }
    return g_state_names[state];
}

/**
 * @brief Validate a state transition
 *
 * For each (from_state, to_state) pair, indicates if transition is valid.
 * Terminal states can be reached from ANY state (escape hatch).
 * Normal state transitions are more restricted.
 *
 * @param from Current state
 * @param to Desired target state
 * @return true if transition is valid, false otherwise
 */
static bool is_valid_transition(lle_readline_state_t from,
                                lle_readline_state_t to) {
    // Terminal states can ALWAYS be reached - this is the escape hatch
    if (lle_readline_state_is_terminal(to)) {
        return true;
    }

    // Can't transition FROM a terminal state to a normal state
    if (lle_readline_state_is_terminal(from)) {
        return false;
    }

    // Normal state transitions
    switch (from) {
    case LLE_READLINE_STATE_IDLE:
        // From IDLE, can go to EDITING on first input
        return to == LLE_READLINE_STATE_EDITING;

    case LLE_READLINE_STATE_EDITING:
        // From EDITING, can enter any modal state
        return to == LLE_READLINE_STATE_COMPLETION ||
               to == LLE_READLINE_STATE_SEARCH ||
               to == LLE_READLINE_STATE_MULTILINE ||
               to == LLE_READLINE_STATE_QUOTED_INSERT;

    case LLE_READLINE_STATE_COMPLETION:
        // From COMPLETION, can go back to EDITING or enter other modes
        return to == LLE_READLINE_STATE_EDITING ||
               to == LLE_READLINE_STATE_SEARCH;

    case LLE_READLINE_STATE_SEARCH:
        // From SEARCH, can go back to EDITING
        return to == LLE_READLINE_STATE_EDITING;

    case LLE_READLINE_STATE_MULTILINE:
        // From MULTILINE, can go to EDITING or back to MULTILINE
        return to == LLE_READLINE_STATE_EDITING ||
               to == LLE_READLINE_STATE_COMPLETION;

    case LLE_READLINE_STATE_QUOTED_INSERT:
        // From QUOTED_INSERT, always go back to EDITING after one char
        return to == LLE_READLINE_STATE_EDITING;

    default:
        return false;
    }
}

/**
 * @brief Transition to a new readline state
 *
 * Validates the transition and updates the context state if valid.
 *
 * @param ctx Readline context containing current state
 * @param new_state Target state to transition to
 * @return LLE_SUCCESS on successful transition
 * @return LLE_ERROR_INVALID_PARAMETER if ctx is NULL or state invalid
 * @return LLE_ERROR_INVALID_STATE if transition is not allowed
 */
lle_result_t lle_readline_state_transition(struct readline_context *ctx,
                                           lle_readline_state_t new_state) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!lle_readline_state_is_valid(new_state)) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_readline_state_t current = ctx->state;

    // Validate the transition
    if (!is_valid_transition(current, new_state)) {
        // Log invalid transition attempt for debugging
#ifdef LLE_DEBUG
        fprintf(stderr, "lle: invalid state transition %s -> %s\n",
                lle_readline_state_name(current),
                lle_readline_state_name(new_state));
#endif
        return LLE_ERROR_INVALID_STATE;
    }

    // Perform the transition
    ctx->previous_state = current;
    ctx->state = new_state;

    return LLE_SUCCESS;
}

/**
 * @brief Force transition to ABORT state
 *
 * Emergency exit path - always succeeds regardless of current state.
 *
 * @param ctx Readline context to update
 */
void lle_readline_state_force_abort(struct readline_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->previous_state = ctx->state;
    ctx->state = LLE_READLINE_STATE_ABORT;
}

/**
 * @brief Force transition to EOF state
 *
 * Emergency exit path for end-of-file - always succeeds.
 *
 * @param ctx Readline context to update
 */
void lle_readline_state_force_eof(struct readline_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->previous_state = ctx->state;
    ctx->state = LLE_READLINE_STATE_EOF;
}

/**
 * @brief Force transition to TIMEOUT state
 *
 * Emergency exit path for timeout conditions - always succeeds.
 *
 * @param ctx Readline context to update
 */
void lle_readline_state_force_timeout(struct readline_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->previous_state = ctx->state;
    ctx->state = LLE_READLINE_STATE_TIMEOUT;
}

/**
 * @brief Force transition to ERROR state
 *
 * Emergency exit path for error conditions - always succeeds.
 *
 * @param ctx Readline context to update
 */
void lle_readline_state_force_error(struct readline_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->previous_state = ctx->state;
    ctx->state = LLE_READLINE_STATE_ERROR;
}

/**
 * @brief Reset state machine to initial IDLE state
 *
 * Resets both current and previous state to IDLE.
 *
 * @param ctx Readline context to reset
 */
void lle_readline_state_reset(struct readline_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->state = LLE_READLINE_STATE_IDLE;
    ctx->previous_state = LLE_READLINE_STATE_IDLE;
}

/**
 * @brief Get current readline state
 *
 * @param ctx Readline context to query
 * @return Current state, or LLE_READLINE_STATE_ERROR if ctx is NULL
 */
lle_readline_state_t
lle_readline_state_get(const struct readline_context *ctx) {
    if (!ctx) {
        return LLE_READLINE_STATE_ERROR;
    }
    return ctx->state;
}

/**
 * @brief Get previous readline state
 *
 * Useful for debugging and recovery logic.
 *
 * @param ctx Readline context to query
 * @return Previous state, or LLE_READLINE_STATE_ERROR if ctx is NULL
 */
lle_readline_state_t
lle_readline_state_get_previous(const struct readline_context *ctx) {
    if (!ctx) {
        return LLE_READLINE_STATE_ERROR;
    }
    return ctx->previous_state;
}
