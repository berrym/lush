/**
 * @file terminal_input_processor.c
 * @brief Input Event Processing (Spec 02 Subsystem 5)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Processes input events from Unix terminal interface and validates them
 * before they are used to modify internal state.
 *
 * Key Responsibilities:
 * - Read input events from Unix interface
 * - Validate event data
 * - Track event sequence and timing
 * - Provide performance metrics
 *
 * Spec 02: Terminal Abstraction - Subsystem 5
 */

#include "lle/arena.h"
#include "lle/lle_shell_integration.h"
#include "lle/terminal_abstraction.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INPUT PROCESSOR OPERATIONS
 * ============================================================================
 */

/**
 * @brief Initialize input processor
 *
 * @param processor Output pointer for created processor
 * @param caps Terminal capabilities reference
 * @param unix_iface Unix interface for input reading
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_processor_init(lle_input_processor_t **processor,
                                      lle_terminal_capabilities_t *caps,
                                      lle_unix_interface_t *unix_iface) {
    if (!processor || !caps || !unix_iface) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_input_processor_t *proc = calloc(1, sizeof(lle_input_processor_t));
    if (!proc) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    proc->capabilities = caps;
    proc->unix_interface = unix_iface;

    // Initialize processing state
    proc->events_processed = 0;
    proc->next_sequence_number = 1;
    proc->total_processing_time_us = 0;

    /* Create event arena for per-event allocations.
     * Child of session arena if available. 1KB is enough for input events.
     * This arena is reset after each event is consumed, preventing the
     * per-keystroke memory leak that previously occurred with calloc(). */
    lle_arena_t *parent_arena = NULL;
    if (g_lle_integration && g_lle_integration->session_arena) {
        parent_arena = g_lle_integration->session_arena;
    }
    proc->event_arena = lle_arena_create(parent_arena, "event", 1024);
    /* Note: event_arena may be NULL if arena creation fails, which is handled
     * gracefully in read_next_event by falling back to calloc */

    *processor = proc;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy input processor
 *
 * @param processor Processor to destroy
 */
void lle_input_processor_destroy(lle_input_processor_t *processor) {
    if (!processor) {
        return;
    }

    // Destroy event arena if it exists
    if (processor->event_arena) {
        lle_arena_destroy(processor->event_arena);
        processor->event_arena = NULL;
    }

    free(processor);
}

/**
 * @brief Validate input event data
 *
 * Checks that the event structure is valid and contains reasonable data
 * based on the event type. Validates character byte counts, codepoint ranges,
 * key codes, and window geometry.
 *
 * @param event Event to validate
 * @return true if event is valid, false otherwise
 */
static bool validate_event(lle_input_event_t *event) {
    if (!event) {
        return false;
    }

    // Validate based on event type
    switch (event->type) {
    case LLE_INPUT_TYPE_CHARACTER:
        // Validate UTF-8 byte count
        if (event->data.character.byte_count == 0 ||
            event->data.character.byte_count > 8) {
            return false;
        }
        // Validate codepoint range
        if (event->data.character.codepoint > 0x10FFFF) {
            return false;
        }
        break;

    case LLE_INPUT_TYPE_SPECIAL_KEY:
        /* Validate key code - allow LLE_KEY_UNKNOWN if it has a valid keycode
         * (for Ctrl+letter) */
        if (event->data.special_key.key == LLE_KEY_UNKNOWN) {
            /* LLE_KEY_UNKNOWN is valid if keycode is set (e.g., Ctrl+A has
             * keycode='A') */
            if (event->data.special_key.keycode == 0) {
                return false; // Invalid: UNKNOWN key with no keycode
            }
        }
        break;

    case LLE_INPUT_TYPE_WINDOW_RESIZE:
        // Validate geometry
        if (event->data.resize.new_width == 0 ||
            event->data.resize.new_height == 0) {
            return false;
        }
        break;

    case LLE_INPUT_TYPE_SIGNAL:
    case LLE_INPUT_TYPE_TIMEOUT:
    case LLE_INPUT_TYPE_ERROR:
    case LLE_INPUT_TYPE_EOF:
        // These events are always valid
        break;

    default:
        return false;
    }

    return true;
}

/**
 * @brief Process input event (validate and track)
 *
 * @param processor Input processor instance
 * @param event Event to process
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_input_processor_process_event(lle_input_processor_t *processor,
                                               lle_input_event_t *event) {
    if (!processor || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    uint64_t processing_start = lle_get_current_time_microseconds();

    // Validate event
    if (!validate_event(event)) {
        return LLE_ERROR_INVALID_INPUT_EVENT;
    }

    // Assign sequence number
    event->sequence_number = processor->next_sequence_number++;

    // Update processing statistics
    processor->events_processed++;
    uint64_t processing_time =
        lle_get_current_time_microseconds() - processing_start;
    processor->total_processing_time_us += processing_time;

    return LLE_SUCCESS;
}

/**
 * @brief Read next input event from Unix interface
 *
 * Uses arena allocation for events to prevent per-keystroke memory leaks.
 * The event arena is reset at the start of each call, effectively freeing
 * the previous event. This is safe because events are only valid until the
 * next call to this function.
 *
 * @param processor Input processor instance
 * @param event Output pointer for read event
 * @param timeout_ms Timeout in milliseconds
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_input_processor_read_next_event(lle_input_processor_t *processor,
                                    lle_input_event_t **event,
                                    uint32_t timeout_ms) {
    if (!processor || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Reset event arena to reclaim memory from previous event.
     * This is the key to fixing the per-keystroke memory leak:
     * instead of allocating with calloc() and never freeing,
     * we reset the arena each iteration which is O(1). */
    if (processor->event_arena) {
        lle_arena_reset(processor->event_arena);
    }

    // Allocate event structure from arena (or fallback to calloc)
    lle_input_event_t *new_event = NULL;
    if (processor->event_arena) {
        new_event = lle_arena_calloc(processor->event_arena, 1,
                                     sizeof(lle_input_event_t));
    } else {
        /* Fallback if arena not available - still leaks but maintains
         * compatibility */
        new_event = calloc(1, sizeof(lle_input_event_t));
    }
    if (!new_event) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Read event from Unix interface
    lle_result_t result = lle_unix_interface_read_event(
        processor->unix_interface, new_event, timeout_ms);
    if (result != LLE_SUCCESS) {
        // No need to free - arena will be reset on next call
        return result;
    }

    // Process (validate and track) the event
    result = lle_input_processor_process_event(processor, new_event);
    if (result != LLE_SUCCESS) {
        // No need to free - arena will be reset on next call
        return result;
    }

    *event = new_event;
    return LLE_SUCCESS;
}
