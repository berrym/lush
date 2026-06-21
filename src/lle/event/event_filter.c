/**
 * @file event_filter.c
 * @brief Event Filter System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Callback-based event filtering, hooks, and system state tracking.
 * Filters can pass, block, transform, or error on events.
 *
 * Spec 04: Event System
 */

#include "lle/event_system.h"
#include "lle/time_util.h"
#include <stdlib.h>
#include <string.h>

#define LLE_FILTER_INITIAL_CAPACITY 8

/* ============================================================================
 * Event Filter System Implementation
 * ============================================================================
 */

/**
 * @brief Initialize the event filter subsystem
 * @param system The event system to initialize filters for
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Allocates the filter system structure and initial filter array.
 * Safe to call multiple times; subsequent calls return success without changes.
 */
lle_result_t lle_event_filter_system_init(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Already initialized
    if (system->filter_system) {
        return LLE_SUCCESS;
    }

    /// Allocate filter system
    lle_event_filter_system_t *filter_sys =
        lle_pool_alloc(sizeof(lle_event_filter_system_t));
    if (!filter_sys) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(filter_sys, 0, sizeof(lle_event_filter_system_t));

    /// Allocate filter array
    filter_sys->filter_capacity = LLE_FILTER_INITIAL_CAPACITY;
    filter_sys->filters = lle_pool_alloc(sizeof(lle_event_filter_t *) *
                                         filter_sys->filter_capacity);
    if (!filter_sys->filters) {
        lle_pool_free(filter_sys);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(filter_sys->filters, 0,
           sizeof(lle_event_filter_t *) * filter_sys->filter_capacity);
    filter_sys->filter_count = 0;

    /// Initialize mutex
    pthread_mutex_init(&filter_sys->filter_mutex, NULL);

    system->filter_system = filter_sys;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy the event filter subsystem and free resources
 * @param system The event system to clean up filters for
 *
 * Frees all registered filters and the filter system structure.
 */
void lle_event_filter_system_destroy(lle_event_system_t *system) {
    if (!system || !system->filter_system) {
        return;
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    /// Destroy mutex
    pthread_mutex_destroy(&filter_sys->filter_mutex);

    /// Free all filters
    if (filter_sys->filters) {
        for (size_t i = 0; i < filter_sys->filter_count; i++) {
            if (filter_sys->filters[i]) {
                lle_pool_free(filter_sys->filters[i]);
            }
        }
        lle_pool_free(filter_sys->filters);
    }

    /// Free filter system
    lle_pool_free(filter_sys);

    system->filter_system = NULL;
}

/**
 * @brief Add an event filter to the system
 * @param system The event system to add the filter to
 * @param name Unique name for the filter (used for removal/management)
 * @param filter The filter callback function
 * @param user_data User context passed to the filter callback
 * @return LLE_SUCCESS on success, LLE_ERROR_ALREADY_INITIALIZED if name exists
 *
 * Filters are applied in registration order during event dispatch.
 * The filter array grows automatically when capacity is reached.
 */
lle_result_t lle_event_filter_add(lle_event_system_t *system, const char *name,
                                  lle_event_filter_fn filter, void *user_data) {
    if (!system || !name || !filter) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Initialize filter system if needed
    if (!system->filter_system) {
        lle_result_t result = lle_event_filter_system_init(system);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    /// Check if filter with this name already exists
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        if (strcmp(filter_sys->filters[i]->name, name) == 0) {
            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_ERROR_ALREADY_INITIALIZED; /* Filter with this name
                                                     already exists */
        }
    }

    /// Check capacity
    if (filter_sys->filter_count >= filter_sys->filter_capacity) {
        /// Need to grow array
        size_t new_capacity = filter_sys->filter_capacity * 2;
        lle_event_filter_t **new_array =
            lle_pool_alloc(sizeof(lle_event_filter_t *) * new_capacity);
        if (!new_array) {
            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        /// Copy existing filters
        memcpy(new_array, filter_sys->filters,
               sizeof(lle_event_filter_t *) * filter_sys->filter_count);
        memset(new_array + filter_sys->filter_count, 0,
               sizeof(lle_event_filter_t *) *
                   (new_capacity - filter_sys->filter_count));

        /// Free old array and update
        lle_pool_free(filter_sys->filters);
        filter_sys->filters = new_array;
        filter_sys->filter_capacity = new_capacity;
    }

    /// Allocate new filter
    lle_event_filter_t *new_filter = lle_pool_alloc(sizeof(lle_event_filter_t));
    if (!new_filter) {
        pthread_mutex_unlock(&filter_sys->filter_mutex);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize filter
    memset(new_filter, 0, sizeof(lle_event_filter_t));
    new_filter->filter = filter;
    new_filter->user_data = user_data;
    new_filter->enabled = true;
    strncpy(new_filter->name, name, sizeof(new_filter->name) - 1);
    new_filter->name[sizeof(new_filter->name) - 1] = '\0';

    /// Add to array
    filter_sys->filters[filter_sys->filter_count] = new_filter;
    filter_sys->filter_count++;

    pthread_mutex_unlock(&filter_sys->filter_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Remove an event filter by name
 * @param system The event system containing the filter
 * @param name The name of the filter to remove
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if filter not found
 */
lle_result_t lle_event_filter_remove(lle_event_system_t *system,
                                     const char *name) {
    if (!system || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->filter_system) {
        return LLE_ERROR_NOT_FOUND;
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    /// Find filter
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        if (strcmp(filter_sys->filters[i]->name, name) == 0) {
            /// Free filter
            lle_pool_free(filter_sys->filters[i]);

            /// Shift remaining filters down
            for (size_t j = i; j < filter_sys->filter_count - 1; j++) {
                filter_sys->filters[j] = filter_sys->filters[j + 1];
            }

            filter_sys->filters[filter_sys->filter_count - 1] = NULL;
            filter_sys->filter_count--;

            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&filter_sys->filter_mutex);
    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Enable a disabled event filter
 * @param system The event system containing the filter
 * @param name The name of the filter to enable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if filter not found
 */
lle_result_t lle_event_filter_enable(lle_event_system_t *system,
                                     const char *name) {
    if (!system || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->filter_system) {
        return LLE_ERROR_NOT_FOUND;
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    /// Find and enable filter
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        if (strcmp(filter_sys->filters[i]->name, name) == 0) {
            filter_sys->filters[i]->enabled = true;
            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&filter_sys->filter_mutex);
    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Disable an event filter without removing it
 * @param system The event system containing the filter
 * @param name The name of the filter to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if filter not found
 *
 * Disabled filters are skipped during event dispatch but retain their
 * statistics.
 */
lle_result_t lle_event_filter_disable(lle_event_system_t *system,
                                      const char *name) {
    if (!system || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->filter_system) {
        return LLE_ERROR_NOT_FOUND;
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    /// Find and disable filter
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        if (strcmp(filter_sys->filters[i]->name, name) == 0) {
            filter_sys->filters[i]->enabled = false;
            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&filter_sys->filter_mutex);
    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Apply all enabled filters to an event
 * @param system The event system with registered filters
 * @param event The event to filter
 * @return LLE_FILTER_PASS if all filters pass, LLE_FILTER_BLOCK if blocked
 *
 * Filters are applied in registration order. A BLOCK result stops
 * processing immediately. TRANSFORM and ERROR results continue to next filter.
 * Statistics are updated for each filter invocation.
 */
lle_filter_result_t lle_event_filter_apply(lle_event_system_t *system,
                                           lle_event_t *event) {
    if (!system || !event) {
        return LLE_FILTER_ERROR;
    }

    if (!system->filter_system) {
        return LLE_FILTER_PASS; /// No filters, pass through
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    filter_sys->total_events_filtered++;

    /// Apply each enabled filter in sequence
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        lle_event_filter_t *filter = filter_sys->filters[i];

        if (!filter->enabled) {
            continue;
        }

        /// Call filter function
        lle_filter_result_t result = filter->filter(event, filter->user_data);

        /// Update filter statistics
        filter->events_filtered++;

        switch (result) {
        case LLE_FILTER_PASS:
            filter->events_passed++;
            break;

        case LLE_FILTER_BLOCK:
            filter->events_blocked++;
            filter_sys->total_events_blocked++;
            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_FILTER_BLOCK; /// Stop processing, block event

        case LLE_FILTER_TRANSFORM:
            filter->events_transformed++;
            /// Continue to next filter with transformed event
            break;

        case LLE_FILTER_ERROR:
            filter->events_errored++;
            /// Continue to next filter on error (don't block)
            break;
        }
    }

    pthread_mutex_unlock(&filter_sys->filter_mutex);

    return LLE_FILTER_PASS; /// All filters passed or transformed
}

/**
 * @brief Get statistics for a specific filter
 * @param system The event system containing the filter
 * @param name The name of the filter to query
 * @param filtered Output: total events processed by filter (may be NULL)
 * @param passed Output: events that passed the filter (may be NULL)
 * @param blocked Output: events blocked by filter (may be NULL)
 * @param transformed Output: events transformed by filter (may be NULL)
 * @param errored Output: events that caused filter errors (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if filter not found
 */
lle_result_t lle_event_filter_get_stats(lle_event_system_t *system,
                                        const char *name, uint64_t *filtered,
                                        uint64_t *passed, uint64_t *blocked,
                                        uint64_t *transformed,
                                        uint64_t *errored) {
    if (!system || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->filter_system) {
        return LLE_ERROR_NOT_FOUND;
    }

    lle_event_filter_system_t *filter_sys = system->filter_system;

    pthread_mutex_lock(&filter_sys->filter_mutex);

    /// Find filter
    for (size_t i = 0; i < filter_sys->filter_count; i++) {
        if (strcmp(filter_sys->filters[i]->name, name) == 0) {
            lle_event_filter_t *filter = filter_sys->filters[i];

            if (filtered)
                *filtered = filter->events_filtered;
            if (passed)
                *passed = filter->events_passed;
            if (blocked)
                *blocked = filter->events_blocked;
            if (transformed)
                *transformed = filter->events_transformed;
            if (errored)
                *errored = filter->events_errored;

            pthread_mutex_unlock(&filter_sys->filter_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&filter_sys->filter_mutex);
    return LLE_ERROR_NOT_FOUND;
}

/* ============================================================================
 * Dispatch Hooks Implementation
 * ============================================================================
 */

/**
 * @brief Set a hook to be called before event dispatch
 * @param system The event system to configure
 * @param hook The callback function (NULL to remove hook)
 * @param user_data Context passed to the hook
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * The pre-dispatch hook can reject events by returning a non-success code.
 */
lle_result_t lle_event_set_pre_dispatch_hook(lle_event_system_t *system,
                                             lle_event_pre_dispatch_fn hook,
                                             void *user_data) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);
    system->pre_dispatch_hook = hook;
    system->pre_dispatch_data = user_data;
    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Set a hook to be called after event dispatch completes
 * @param system The event system to configure
 * @param hook The callback function (NULL to remove hook)
 * @param user_data Context passed to the hook
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * The post-dispatch hook receives the dispatch result for logging/monitoring.
 */
lle_result_t lle_event_set_post_dispatch_hook(lle_event_system_t *system,
                                              lle_event_post_dispatch_fn hook,
                                              void *user_data) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);
    system->post_dispatch_hook = hook;
    system->post_dispatch_data = user_data;
    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/* ============================================================================
 * System State Implementation
 * ============================================================================
 */

/**
 * @brief Set the current system state
 * @param system The event system to update
 * @param state The new state to transition to
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Records the previous state and timestamp of the state change.
 */
lle_result_t lle_event_system_set_state(lle_event_system_t *system,
                                        lle_system_state_t state) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    system->previous_state = system->current_state;
    system->current_state = state;
    system->state_changed_time = lle_get_current_time_microseconds();

    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get the current system state
 * @param system The event system to query
 * @return Current state, or LLE_STATE_ERROR if system is NULL
 */
lle_system_state_t lle_event_system_get_state(lle_event_system_t *system) {
    if (!system) {
        return LLE_STATE_ERROR;
    }

    pthread_mutex_lock(&system->system_mutex);
    lle_system_state_t state = system->current_state;
    pthread_mutex_unlock(&system->system_mutex);

    return state;
}

/**
 * @brief Get the previous system state before last transition
 * @param system The event system to query
 * @return Previous state, or LLE_STATE_ERROR if system is NULL
 */
lle_system_state_t
lle_event_system_get_previous_state(lle_event_system_t *system) {
    if (!system) {
        return LLE_STATE_ERROR;
    }

    pthread_mutex_lock(&system->system_mutex);
    lle_system_state_t state = system->previous_state;
    pthread_mutex_unlock(&system->system_mutex);

    return state;
}
