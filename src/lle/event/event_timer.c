/**
 * @file event_timer.c
 * @brief Timer Event Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements timer event support for the LLE event system.
 *
 * Features:
 * - One-shot timers (fire once)
 * - Repeating timers (fire at intervals)
 * - Timer enable/disable (without destroying)
 * - Timer statistics
 * - Thread-safe operations
 *
 * Design:
 * - Simple array-based storage (optimal for small N)
 * - Sorted by trigger_time_us for efficient processing
 * - Timers own their events (deep copy on creation)
 * - Manual processing via lle_event_timer_process()
 *
 * Spec 04: Event System
 */

#include "lle/event_system.h"
#include "lle/time_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Initial capacity for timer array
#define TIMER_INITIAL_CAPACITY 16

/**
 * @brief Find timer index by ID (internal helper)
 * @param ts The timer system to search
 * @param timer_id The timer ID to find
 * @return Index of timer, or -1 if not found
 * @note Must be called with timer mutex held
 */
static int find_timer_index(lle_timer_system_t *ts, uint64_t timer_id) {
    for (size_t i = 0; i < ts->timer_count; i++) {
        if (ts->timers[i]->timer_id == timer_id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Insert timer in sorted position by trigger time (internal helper)
 * @param ts The timer system to insert into
 * @param timer The timer to insert
 * @return LLE_SUCCESS on success, LLE_ERROR_OUT_OF_MEMORY if allocation fails
 *
 * Maintains array sorted by trigger_time_us for efficient processing.
 * Grows the array automatically when capacity is reached.
 */
static lle_result_t insert_timer_sorted(lle_timer_system_t *ts,
                                        lle_timer_event_t *timer) {
    /// Grow array if needed
    if (ts->timer_count >= ts->timer_capacity) {
        size_t new_capacity = ts->timer_capacity * 2;
        lle_timer_event_t **new_array =
            realloc(ts->timers, new_capacity * sizeof(lle_timer_event_t *));
        if (!new_array) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                             "event timer allocation failed");
        }
        ts->timers = new_array;
        ts->timer_capacity = new_capacity;
    }

    /// Find insertion point (keep sorted by trigger_time_us)
    size_t insert_pos = ts->timer_count;
    for (size_t i = 0; i < ts->timer_count; i++) {
        if (timer->trigger_time_us < ts->timers[i]->trigger_time_us) {
            insert_pos = i;
            break;
        }
    }

    /// Shift elements to make room
    if (insert_pos < ts->timer_count) {
        memmove(&ts->timers[insert_pos + 1], &ts->timers[insert_pos],
                (ts->timer_count - insert_pos) * sizeof(lle_timer_event_t *));
    }

    /// Insert timer
    ts->timers[insert_pos] = timer;
    ts->timer_count++;

    return LLE_SUCCESS;
}

/**
 * @brief Remove timer at specific index (internal helper)
 * @param ts The timer system to remove from
 * @param index The index of the timer to remove
 * @note Must be called with timer mutex held. Frees the timer and its event.
 */
static void remove_timer_at_index(lle_timer_system_t *ts, size_t index) {
    if (index >= ts->timer_count) {
        return;
    }

    /// Free the timer and its event
    if (ts->timers[index]->event) {
        /// Note: We need the event system to properly destroy the event,
        /// but we don't have it here. For now, just free the event data.
        if (ts->timers[index]->event->data) {
            free(ts->timers[index]->event->data);
        }
        free(ts->timers[index]->event);
    }
    free(ts->timers[index]);

    /// Shift remaining timers down
    if (index < ts->timer_count - 1) {
        memmove(&ts->timers[index], &ts->timers[index + 1],
                (ts->timer_count - index - 1) * sizeof(lle_timer_event_t *));
    }

    ts->timer_count--;
}

/**
 * @brief Initialize the timer subsystem
 * @param system The event system to initialize timers for
 * @return LLE_SUCCESS on success, LLE_ERROR_ALREADY_INITIALIZED if already
 * init, or other error code on failure
 */
lle_result_t lle_event_timer_system_init(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Already initialized?
    if (system->timer_system) {
        return LLE_ERROR_ALREADY_INITIALIZED;
    }

    /// Allocate timer system
    lle_timer_system_t *ts = calloc(1, sizeof(lle_timer_system_t));
    if (!ts) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    /// Allocate timer array
    ts->timers = calloc(TIMER_INITIAL_CAPACITY, sizeof(lle_timer_event_t *));
    if (!ts->timers) {
        free(ts);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    ts->timer_count = 0;
    ts->timer_capacity = TIMER_INITIAL_CAPACITY;
    ts->next_timer_id = 1;
    ts->total_timers_created = 0;
    ts->total_timers_fired = 0;
    ts->total_timers_cancelled = 0;

    /// Initialize mutex
    if (pthread_mutex_init(&ts->timer_mutex, NULL) != 0) {
        free(ts->timers);
        free(ts);
        return LLE_FAULT(LLE_ERROR_SYSTEM_CALL, "event",
                         "timer mutex init failed");
    }

    system->timer_system = ts;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy the timer subsystem and all timers
 * @param system The event system to clean up timers for
 *
 * Cancels all pending timers and frees all associated resources.
 */
void lle_event_timer_system_destroy(lle_event_system_t *system) {
    if (!system || !system->timer_system) {
        return;
    }

    lle_timer_system_t *ts = system->timer_system;

    /// Destroy all timers
    pthread_mutex_lock(&ts->timer_mutex);
    for (size_t i = 0; i < ts->timer_count; i++) {
        if (ts->timers[i]->event) {
            if (ts->timers[i]->event->data) {
                free(ts->timers[i]->event->data);
            }
            free(ts->timers[i]->event);
        }
        free(ts->timers[i]);
    }
    free(ts->timers);
    pthread_mutex_unlock(&ts->timer_mutex);

    /// Destroy mutex
    pthread_mutex_destroy(&ts->timer_mutex);

    /// Free timer system
    free(ts);
    system->timer_system = NULL;
}

/**
 * @brief Create a one-shot timer that fires once after a delay
 * @param system The event system to add the timer to
 * @param event The event to dispatch when the timer fires (deep copied)
 * @param delay_us Delay in microseconds before the timer fires
 * @param timer_id_out Output to receive the timer ID (may be NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * The event is deep copied; the caller retains ownership of the original.
 * The timer is automatically removed after it fires.
 */
lle_result_t lle_event_timer_add_oneshot(lle_event_system_t *system,
                                         lle_event_t *event, uint64_t delay_us,
                                         uint64_t *timer_id_out) {
    if (!system || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize timer system if needed
    if (!system->timer_system) {
        lle_result_t result = lle_event_timer_system_init(system);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    lle_timer_system_t *ts = system->timer_system;

    /// Allocate timer
    lle_timer_event_t *timer = calloc(1, sizeof(lle_timer_event_t));
    if (!timer) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    /// Clone the event (deep copy)
    timer->event = calloc(1, sizeof(lle_event_t));
    if (!timer->event) {
        free(timer);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    /// Copy event structure
    memcpy(timer->event, event, sizeof(lle_event_t));

    /// Deep copy event data if present
    if (event->data && event->data_size > 0) {
        timer->event->data = malloc(event->data_size);
        if (!timer->event->data) {
            free(timer->event);
            free(timer);
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                             "event timer allocation failed");
        }
        memcpy(timer->event->data, event->data, event->data_size);
    } else {
        timer->event->data = NULL;
    }

    /// Reset queue linkage (timer event is not in queue)
    timer->event->next = NULL;
    timer->event->prev = NULL;

    /// Set up timer
    pthread_mutex_lock(&ts->timer_mutex);

    timer->timer_id = ts->next_timer_id++;
    timer->trigger_time_us = lle_get_current_time_microseconds() + delay_us;
    timer->interval_us = 0;
    timer->repeating = false;
    timer->enabled = true;
    timer->fire_count = 0;

    /// Insert in sorted order
    lle_result_t result = insert_timer_sorted(ts, timer);
    if (result != LLE_SUCCESS) {
        pthread_mutex_unlock(&ts->timer_mutex);
        if (timer->event->data) {
            free(timer->event->data);
        }
        free(timer->event);
        free(timer);
        return result;
    }

    ts->total_timers_created++;

    if (timer_id_out) {
        *timer_id_out = timer->timer_id;
    }

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Create a repeating timer that fires at regular intervals
 * @param system The event system to add the timer to
 * @param event The event to dispatch each time the timer fires (deep copied)
 * @param initial_delay_us Initial delay before first fire in microseconds
 * @param interval_us Interval between subsequent fires in microseconds
 * @param timer_id_out Output to receive the timer ID (may be NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * The event is deep copied; the caller retains ownership of the original.
 * The timer continues to fire until cancelled or disabled.
 */
lle_result_t lle_event_timer_add_repeating(lle_event_system_t *system,
                                           lle_event_t *event,
                                           uint64_t initial_delay_us,
                                           uint64_t interval_us,
                                           uint64_t *timer_id_out) {
    if (!system || !event || interval_us == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize timer system if needed
    if (!system->timer_system) {
        lle_result_t result = lle_event_timer_system_init(system);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    lle_timer_system_t *ts = system->timer_system;

    /// Allocate timer
    lle_timer_event_t *timer = calloc(1, sizeof(lle_timer_event_t));
    if (!timer) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    /// Clone the event (deep copy)
    timer->event = calloc(1, sizeof(lle_event_t));
    if (!timer->event) {
        free(timer);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                         "event timer allocation failed");
    }

    /// Copy event structure
    memcpy(timer->event, event, sizeof(lle_event_t));

    /// Deep copy event data if present
    if (event->data && event->data_size > 0) {
        timer->event->data = malloc(event->data_size);
        if (!timer->event->data) {
            free(timer->event);
            free(timer);
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "event",
                             "event timer allocation failed");
        }
        memcpy(timer->event->data, event->data, event->data_size);
    } else {
        timer->event->data = NULL;
    }

    /// Reset queue linkage
    timer->event->next = NULL;
    timer->event->prev = NULL;

    /// Set up timer
    pthread_mutex_lock(&ts->timer_mutex);

    timer->timer_id = ts->next_timer_id++;
    timer->trigger_time_us =
        lle_get_current_time_microseconds() + initial_delay_us;
    timer->interval_us = interval_us;
    timer->repeating = true;
    timer->enabled = true;
    timer->fire_count = 0;

    /// Insert in sorted order
    lle_result_t result = insert_timer_sorted(ts, timer);
    if (result != LLE_SUCCESS) {
        pthread_mutex_unlock(&ts->timer_mutex);
        if (timer->event->data) {
            free(timer->event->data);
        }
        free(timer->event);
        free(timer);
        return result;
    }

    ts->total_timers_created++;

    if (timer_id_out) {
        *timer_id_out = timer->timer_id;
    }

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Cancel and remove a timer
 * @param system The event system containing the timer
 * @param timer_id The ID of the timer to cancel
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if timer not found
 *
 * The timer is removed and its resources freed. It will not fire again.
 */
lle_result_t lle_event_timer_cancel(lle_event_system_t *system,
                                    uint64_t timer_id) {
    if (!system || !system->timer_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_timer_system_t *ts = system->timer_system;

    pthread_mutex_lock(&ts->timer_mutex);

    int index = find_timer_index(ts, timer_id);
    if (index < 0) {
        pthread_mutex_unlock(&ts->timer_mutex);
        return LLE_ERROR_NOT_FOUND;
    }

    remove_timer_at_index(ts, (size_t)index);
    ts->total_timers_cancelled++;

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Enable a disabled timer
 * @param system The event system containing the timer
 * @param timer_id The ID of the timer to enable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if timer not found
 */
lle_result_t lle_event_timer_enable(lle_event_system_t *system,
                                    uint64_t timer_id) {
    if (!system || !system->timer_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_timer_system_t *ts = system->timer_system;

    pthread_mutex_lock(&ts->timer_mutex);

    int index = find_timer_index(ts, timer_id);
    if (index < 0) {
        pthread_mutex_unlock(&ts->timer_mutex);
        return LLE_ERROR_NOT_FOUND;
    }

    ts->timers[index]->enabled = true;

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Disable a timer without removing it
 * @param system The event system containing the timer
 * @param timer_id The ID of the timer to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if timer not found
 *
 * Disabled timers are skipped during processing but retain their state.
 */
lle_result_t lle_event_timer_disable(lle_event_system_t *system,
                                     uint64_t timer_id) {
    if (!system || !system->timer_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_timer_system_t *ts = system->timer_system;

    pthread_mutex_lock(&ts->timer_mutex);

    int index = find_timer_index(ts, timer_id);
    if (index < 0) {
        pthread_mutex_unlock(&ts->timer_mutex);
        return LLE_ERROR_NOT_FOUND;
    }

    ts->timers[index]->enabled = false;

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Get information about a timer
 * @param system The event system containing the timer
 * @param timer_id The ID of the timer to query
 * @param next_fire_time_us Output: next scheduled fire time (may be NULL)
 * @param fire_count Output: number of times timer has fired (may be NULL)
 * @param is_repeating Output: whether timer is repeating (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if timer not found
 */
lle_result_t lle_event_timer_get_info(lle_event_system_t *system,
                                      uint64_t timer_id,
                                      uint64_t *next_fire_time_us,
                                      uint64_t *fire_count,
                                      bool *is_repeating) {
    if (!system || !system->timer_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_timer_system_t *ts = system->timer_system;

    pthread_mutex_lock(&ts->timer_mutex);

    int index = find_timer_index(ts, timer_id);
    if (index < 0) {
        pthread_mutex_unlock(&ts->timer_mutex);
        return LLE_ERROR_NOT_FOUND;
    }

    lle_timer_event_t *timer = ts->timers[index];

    if (next_fire_time_us) {
        *next_fire_time_us = timer->trigger_time_us;
    }
    if (fire_count) {
        *fire_count = timer->fire_count;
    }
    if (is_repeating) {
        *is_repeating = timer->repeating;
    }

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Process all timers that are ready to fire
 * @param system The event system to process timers for
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Checks all timers against the current time and dispatches events
 * for any that have reached their trigger time. Repeating timers
 * are rescheduled; one-shot timers are removed after firing.
 *
 * This function should be called periodically (e.g., in the main loop).
 */
lle_result_t lle_event_timer_process(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// No timer system? Nothing to do
    if (!system->timer_system) {
        return LLE_SUCCESS;
    }

    lle_timer_system_t *ts = system->timer_system;
    uint64_t current_time = lle_get_current_time_microseconds();

    pthread_mutex_lock(&ts->timer_mutex);

    /// Process timers in order (array is sorted by trigger_time_us)
    size_t i = 0;
    while (i < ts->timer_count) {
        lle_timer_event_t *timer = ts->timers[i];

        /// If this timer is not ready, none of the rest are either
        if (timer->trigger_time_us > current_time) {
            break;
        }

        /// Skip disabled timers
        if (!timer->enabled) {
            i++;
            continue;
        }

        /// Clone the event for dispatch
        lle_event_t *event_to_dispatch = calloc(1, sizeof(lle_event_t));
        if (event_to_dispatch) {
            memcpy(event_to_dispatch, timer->event, sizeof(lle_event_t));

            /// Clone event data if present
            if (timer->event->data && timer->event->data_size > 0) {
                event_to_dispatch->data = malloc(timer->event->data_size);
                if (event_to_dispatch->data) {
                    memcpy(event_to_dispatch->data, timer->event->data,
                           timer->event->data_size);
                } else {
                    /// Failed to allocate data, just use NULL
                    event_to_dispatch->data = NULL;
                    event_to_dispatch->data_size = 0;
                }
            } else {
                event_to_dispatch->data = NULL;
            }

            /// Reset queue linkage
            event_to_dispatch->next = NULL;
            event_to_dispatch->prev = NULL;

            /// Update timer
            timer->fire_count++;
            ts->total_timers_fired++;

            /// Unlock mutex before dispatching (avoid deadlock)
            pthread_mutex_unlock(&ts->timer_mutex);

            /// Dispatch the event
            lle_event_dispatch(system, event_to_dispatch);

            /// Free the dispatched event (dispatch doesn't take ownership)
            if (event_to_dispatch->data) {
                free(event_to_dispatch->data);
            }
            free(event_to_dispatch);

            /// Re-lock mutex
            pthread_mutex_lock(&ts->timer_mutex);

            /// Timer array may have changed during dispatch - find timer again
            int index = find_timer_index(ts, timer->timer_id);
            if (index < 0) {
                /// Timer was cancelled during dispatch
                continue;
            }

            /// Update i to reflect current position
            i = (size_t)index;
            timer = ts->timers[i];
        }

        /// Handle repeating vs one-shot
        if (timer->repeating) {
            /// Update trigger time
            timer->trigger_time_us += timer->interval_us;

            /// Remove from current position
            lle_timer_event_t *timer_copy = timer;
            if (i < ts->timer_count - 1) {
                memmove(&ts->timers[i], &ts->timers[i + 1],
                        (ts->timer_count - i - 1) *
                            sizeof(lle_timer_event_t *));
            }
            ts->timer_count--;

            /// Re-insert in sorted position
            insert_timer_sorted(ts, timer_copy);

            /// Don't increment i - we removed this element
        } else {
            /// One-shot timer - remove it
            remove_timer_at_index(ts, i);
            /// Don't increment i - we removed this element
        }
    }

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Get timer system statistics
 * @param system The event system to query
 * @param created Output: total timers created (may be NULL)
 * @param fired Output: total timer fire events (may be NULL)
 * @param cancelled Output: total timers cancelled (may be NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Returns zeros if the timer system has not been initialized.
 */
lle_result_t lle_event_timer_get_stats(lle_event_system_t *system,
                                       uint64_t *created, uint64_t *fired,
                                       uint64_t *cancelled) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// No timer system? Return zeros
    if (!system->timer_system) {
        if (created)
            *created = 0;
        if (fired)
            *fired = 0;
        if (cancelled)
            *cancelled = 0;
        return LLE_SUCCESS;
    }

    lle_timer_system_t *ts = system->timer_system;

    pthread_mutex_lock(&ts->timer_mutex);

    if (created) {
        *created = ts->total_timers_created;
    }
    if (fired) {
        *fired = ts->total_timers_fired;
    }
    if (cancelled) {
        *cancelled = ts->total_timers_cancelled;
    }

    pthread_mutex_unlock(&ts->timer_mutex);
    return LLE_SUCCESS;
}
