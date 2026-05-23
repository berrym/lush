/**
 * @file parser_state_machine.c
 * @brief Input Parser State Machine
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Manages parser state transitions for coordinating input processing across
 * multiple parsing components (UTF-8 processor, sequence parser, key detector,
 * mouse parser).
 *
 * States:
 * - NORMAL: Regular text input
 * - ESCAPE: Escape sequence started (ESC received)
 * - CSI: CSI sequence in progress (ESC[ received)
 * - OSC: OSC sequence in progress (ESC] received)
 * - DCS: DCS sequence in progress (ESC P received)
 * - UTF8_MULTIBYTE: UTF-8 multibyte sequence in progress
 * - KEY_SEQUENCE: Key sequence detection in progress
 * - MOUSE: Mouse event parsing in progress
 * - ERROR_RECOVERY: Error recovery state
 *
 * The state machine tracks state transitions and provides query functions.
 * Actual parser invocations are handled by the integration layer.
 *
 * Spec 06: Input Parsing - Phase 6
 */

#include "lle/error_handling.h"
#include "lle/input_parsing.h"
#include <string.h>
#include <time.h>

/**
 * @brief Get current time in microseconds
 *
 * @return Current monotonic time in microseconds
 */
static uint64_t get_current_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/**
 * @brief Initialize parser state machine
 *
 * @param state_machine Output pointer for created state machine
 * @param error_ctx Error context for error reporting
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_parser_state_machine_init(lle_parser_state_machine_t **state_machine,
                              lle_error_context_t *error_ctx,
                              lle_memory_pool_t *memory_pool) {
    if (!state_machine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_parser_state_machine_t *sm =
        lle_pool_alloc(sizeof(lle_parser_state_machine_t));
    if (!sm) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(sm, 0, sizeof(lle_parser_state_machine_t));

    sm->current_state = LLE_PARSER_STATE_NORMAL;
    sm->previous_state = LLE_PARSER_STATE_NORMAL;
    sm->state_transitions = 0;
    sm->state_change_time = get_current_time_us();
    sm->error_ctx = error_ctx;
    sm->error_recoveries = 0;
    sm->memory_pool = memory_pool;

    *state_machine = sm;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy parser state machine
 *
 * @param state_machine State machine to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_parser_state_machine_destroy(lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_pool_free(state_machine);
    return LLE_SUCCESS;
}

/**
 * @brief Transition to new state
 *
 * @param state_machine State machine to transition
 * @param new_state Target state
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_parser_state_machine_transition(lle_parser_state_machine_t *state_machine,
                                    lle_parser_state_t new_state) {
    if (!state_machine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (state_machine->current_state != new_state) {
        state_machine->previous_state = state_machine->current_state;
        state_machine->current_state = new_state;
        state_machine->state_transitions++;
        state_machine->state_change_time = get_current_time_us();
    }

    return LLE_SUCCESS;
}

/**
 * @brief Main processing function - analyzes input and determines state
 * transitions
 *
 * This is a lightweight function that examines the input data and determines
 * which parser state should handle it. The actual parsing is done by the
 * integration layer.
 *
 * @param state_machine State machine instance
 * @param parser_sys Parser system (reserved for future use)
 * @param data Input data to analyze
 * @param data_len Length of input data
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_parser_state_machine_process(lle_parser_state_machine_t *state_machine,
                                 lle_input_parser_system_t *parser_sys,
                                 const char *data, size_t data_len) {
    (void)parser_sys; // Reserved for parser-system-aware processing
    if (!state_machine || !data) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (data_len == 0) {
        return LLE_SUCCESS;
    }

    /* The process function provides basic state routing hints based on input.
     * The integration layer will do the actual parsing. */

    // Check current state and input to determine transitions
    switch (state_machine->current_state) {
    case LLE_PARSER_STATE_NORMAL:
        // Check if escape sequence is starting
        if (data[0] == '\x1B') {
            lle_parser_state_machine_transition(state_machine,
                                                LLE_PARSER_STATE_ESCAPE);
        }
        // Otherwise stay in NORMAL state for text processing
        break;

    case LLE_PARSER_STATE_ESCAPE:
        // Analyze second byte to determine sequence type
        if (data_len >= 2) {
            if (data[1] == '[') {
                // Could be CSI, mouse, or key sequence
                if (data_len >= 3) {
                    if (data[2] == 'M' || data[2] == '<') {
                        // Mouse sequence
                        lle_parser_state_machine_transition(
                            state_machine, LLE_PARSER_STATE_MOUSE);
                    } else {
                        // CSI sequence
                        lle_parser_state_machine_transition(
                            state_machine, LLE_PARSER_STATE_CSI);
                    }
                }
            } else if (data[1] == ']') {
                // OSC sequence
                lle_parser_state_machine_transition(state_machine,
                                                    LLE_PARSER_STATE_OSC);
            } else if (data[1] == 'P') {
                // DCS sequence
                lle_parser_state_machine_transition(state_machine,
                                                    LLE_PARSER_STATE_DCS);
            } else if (data[1] == 'O' || data[1] == 'N') {
                // SS3 or SS2 - key sequence
                lle_parser_state_machine_transition(
                    state_machine, LLE_PARSER_STATE_KEY_SEQUENCE);
            } else {
                // Other escape sequence - treat as key
                lle_parser_state_machine_transition(
                    state_machine, LLE_PARSER_STATE_KEY_SEQUENCE);
            }
        }
        break;

    case LLE_PARSER_STATE_CSI:
    case LLE_PARSER_STATE_OSC:
    case LLE_PARSER_STATE_DCS:
    case LLE_PARSER_STATE_KEY_SEQUENCE:
    case LLE_PARSER_STATE_MOUSE:
    case LLE_PARSER_STATE_UTF8_MULTIBYTE:
        /* These states are handled by the integration layer.
         * The state machine just tracks that we're in these states.
         * Integration layer will call transition() when parsing completes. */
        break;

    case LLE_PARSER_STATE_ERROR_RECOVERY:
        // Error recovery - transition back to normal
        lle_parser_state_machine_transition(state_machine,
                                            LLE_PARSER_STATE_NORMAL);
        state_machine->error_recoveries++;
        break;

    default:
        // Unknown state - recover
        lle_parser_state_machine_transition(state_machine,
                                            LLE_PARSER_STATE_ERROR_RECOVERY);
        state_machine->error_recoveries++;
        break;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get current state
 *
 * @param state_machine State machine to query
 * @return Current parser state
 */
lle_parser_state_t lle_parser_state_machine_get_state(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return LLE_PARSER_STATE_NORMAL;
    }
    return state_machine->current_state;
}

/**
 * @brief Get previous state
 *
 * @param state_machine State machine to query
 * @return Previous parser state
 */
lle_parser_state_t lle_parser_state_machine_get_previous_state(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return LLE_PARSER_STATE_NORMAL;
    }
    return state_machine->previous_state;
}

/**
 * @brief Get state transition count
 *
 * @param state_machine State machine to query
 * @return Total number of state transitions
 */
uint64_t lle_parser_state_machine_get_transitions(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return 0;
    }
    return state_machine->state_transitions;
}

/**
 * @brief Get error recovery count
 *
 * @param state_machine State machine to query
 * @return Total number of error recoveries
 */
uint32_t lle_parser_state_machine_get_error_recoveries(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return 0;
    }
    return state_machine->error_recoveries;
}

/**
 * @brief Get time since last state change (microseconds)
 *
 * @param state_machine State machine to query
 * @return Time in microseconds since last state change
 */
uint64_t lle_parser_state_machine_time_in_state(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return 0;
    }
    return get_current_time_us() - state_machine->state_change_time;
}

/**
 * @brief Check if state machine is in error recovery
 *
 * @param state_machine State machine to query
 * @return true if in error recovery state, false otherwise
 */
bool lle_parser_state_machine_is_error_state(
    const lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return false;
    }
    return state_machine->current_state == LLE_PARSER_STATE_ERROR_RECOVERY;
}

/**
 * @brief Reset state machine to initial state
 *
 * @param state_machine State machine to reset
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_parser_state_machine_reset(lle_parser_state_machine_t *state_machine) {
    if (!state_machine) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    state_machine->current_state = LLE_PARSER_STATE_NORMAL;
    state_machine->previous_state = LLE_PARSER_STATE_NORMAL;
    state_machine->state_transitions = 0;
    state_machine->state_change_time = get_current_time_us();
    state_machine->error_recoveries = 0;

    return LLE_SUCCESS;
}
