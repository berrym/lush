/**
 * @file event_handlers.c
 * @brief Event Handler Management and Dispatching
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Handler registration, unregistration, and event dispatching with filtering.
 * Implements Phase 1 core handlers and Phase 2C filtering/hooks.
 *
 * Spec 04: Event System - Phase 1 + Phase 2C
 */

#include "lle/event_system.h"
#include <stdlib.h>
#include <string.h>

/* Forward declaration for filter apply function (from event_filter.c) */
extern lle_filter_result_t lle_event_filter_apply(lle_event_system_t *system,
                                                  lle_event_t *event);

/**
 * @brief Register an event handler for a specific event type
 * @param system The event system to register with
 * @param type The event type to handle
 * @param handler The callback function to invoke
 * @param user_data User-provided context passed to handler
 * @param name Unique name for this handler (used for unregistration)
 * @return LLE_SUCCESS on success, or an error code on failure
 *
 * The handler array grows automatically when capacity is reached.
 */
lle_result_t lle_event_handler_register(lle_event_system_t *system,
                                        lle_event_kind_t type,
                                        lle_event_handler_fn handler,
                                        void *user_data, const char *name) {
    if (!system || !handler || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    /* Check if we need to grow handler array */
    if (system->handler_count >= system->handler_capacity) {
        size_t new_capacity = system->handler_capacity * 2;
        lle_event_handler_t **new_handlers =
            lle_pool_alloc(sizeof(lle_event_handler_t *) * new_capacity);

        if (!new_handlers) {
            pthread_mutex_unlock(&system->system_mutex);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        /* Copy existing handlers */
        memcpy(new_handlers, system->handlers,
               sizeof(lle_event_handler_t *) * system->handler_count);

        /* Free old array */
        lle_pool_free(system->handlers);

        system->handlers = new_handlers;
        system->handler_capacity = new_capacity;
    }

    /* Allocate new handler */
    lle_event_handler_t *h = lle_pool_alloc(sizeof(lle_event_handler_t));
    if (!h) {
        pthread_mutex_unlock(&system->system_mutex);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize handler */
    h->event_type = type;
    h->handler = handler;
    h->user_data = user_data;
    strncpy(h->name, name, sizeof(h->name) - 1);
    h->name[sizeof(h->name) - 1] = '\0';

    /* Add to array */
    system->handlers[system->handler_count++] = h;

    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Unregister a specific event handler by type and name
 * @param system The event system containing the handler
 * @param type The event type the handler was registered for
 * @param name The name of the handler to remove
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if handler not found
 */
lle_result_t lle_event_handler_unregister(lle_event_system_t *system,
                                          lle_event_kind_t type,
                                          const char *name) {
    if (!system || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    /* Find and remove handler */
    for (size_t i = 0; i < system->handler_count; i++) {
        lle_event_handler_t *h = system->handlers[i];

        if (h->event_type == type && strcmp(h->name, name) == 0) {
            /* Free handler */
            lle_pool_free(h);

            /* Shift remaining handlers down */
            for (size_t j = i; j < system->handler_count - 1; j++) {
                system->handlers[j] = system->handlers[j + 1];
            }

            system->handler_count--;

            pthread_mutex_unlock(&system->system_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&system->system_mutex);
    return LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Unregister all handlers for a specific event type
 * @param system The event system containing the handlers
 * @param type The event type to remove all handlers for
 * @return LLE_SUCCESS if at least one handler was removed, LLE_ERROR_NOT_FOUND otherwise
 */
lle_result_t lle_event_handler_unregister_all(lle_event_system_t *system,
                                              lle_event_kind_t type) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    size_t removed = 0;

    /* Remove all handlers for this type */
    for (size_t i = 0; i < system->handler_count;) {
        lle_event_handler_t *h = system->handlers[i];

        if (h->event_type == type) {
            /* Free handler */
            lle_pool_free(h);

            /* Shift remaining handlers down */
            for (size_t j = i; j < system->handler_count - 1; j++) {
                system->handlers[j] = system->handlers[j + 1];
            }

            system->handler_count--;
            removed++;
        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&system->system_mutex);

    return (removed > 0) ? LLE_SUCCESS : LLE_ERROR_NOT_FOUND;
}

/**
 * @brief Get the number of handlers registered for an event type
 * @param system The event system to query
 * @param type The event type to count handlers for
 * @return Number of registered handlers for the type
 */
size_t lle_event_handler_count(lle_event_system_t *system,
                               lle_event_kind_t type) {
    if (!system) {
        return 0;
    }

    pthread_mutex_lock(&system->system_mutex);

    size_t count = 0;
    for (size_t i = 0; i < system->handler_count; i++) {
        if (system->handlers[i]->event_type == type) {
            count++;
        }
    }

    pthread_mutex_unlock(&system->system_mutex);

    return count;
}

/**
 * @brief Dispatch an event to all registered handlers
 * @param system The event system to dispatch through
 * @param event The event to dispatch
 * @return LLE_SUCCESS on success, or last error from handlers
 *
 * Phase 2C enhancements:
 * - Applies event filters before dispatch (can block events)
 * - Calls pre-dispatch hook before handlers
 * - Updates system state to PROCESSING during dispatch
 * - Calls post-dispatch hook after handlers complete
 */
lle_result_t lle_event_dispatch(lle_event_system_t *system,
                                lle_event_t *event) {
    if (!system || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Phase 2C: Apply event filters */
    if (system->filter_system) {
        lle_filter_result_t filter_result =
            lle_event_filter_apply(system, event);
        if (filter_result == LLE_FILTER_BLOCK) {
            return LLE_SUCCESS; /* Event blocked by filter, but not an error */
        }
        /* FILTER_PASS, FILTER_TRANSFORM, or FILTER_ERROR: continue dispatch */
    }

    /* Phase 2C: Call pre-dispatch hook */
    if (system->pre_dispatch_hook) {
        lle_result_t hook_result =
            system->pre_dispatch_hook(event, system->pre_dispatch_data);
        if (hook_result != LLE_SUCCESS) {
            return hook_result; /* Hook rejected event */
        }
    }

    /* Copy-and-release pattern: snapshot matching handlers while holding
     * the mutex, then release the mutex before calling handlers. This
     * prevents deadlock if a handler touches the event system. */
    pthread_mutex_lock(&system->system_mutex);

    lle_system_state_t previous_state = system->current_state;
    system->current_state = LLE_STATE_PROCESSING;

    /* Snapshot matching handlers (stack-allocated, no malloc needed) */
    lle_event_handler_t *snapshot[64];
    size_t snap_count = 0;

    for (size_t i = 0; i < system->handler_count && snap_count < 64; i++) {
        if (system->handlers[i]->event_type == event->type) {
            snapshot[snap_count++] = system->handlers[i];
        }
    }

    pthread_mutex_unlock(&system->system_mutex);

    /* Dispatch WITHOUT holding lock - handlers can safely use event system */
    lle_result_t dispatch_result = LLE_SUCCESS;

    for (size_t i = 0; i < snap_count; i++) {
        lle_result_t result = snapshot[i]->handler(event, snapshot[i]->user_data);
        if (result != LLE_SUCCESS) {
            dispatch_result = result;
        }
    }

    /* Update statistics and restore state under lock */
    pthread_mutex_lock(&system->system_mutex);
    if (snap_count > 0) {
        system->events_dispatched++;
    }
    system->current_state = previous_state;
    pthread_mutex_unlock(&system->system_mutex);

    /* Phase 2C: Call post-dispatch hook */
    if (system->post_dispatch_hook) {
        system->post_dispatch_hook(event, dispatch_result,
                                   system->post_dispatch_data);
    }

    return dispatch_result;
}

/**
 * @brief Process a batch of events from the queue
 * @param system The event system to process
 * @param max_events Maximum number of events to process in this batch
 * @return LLE_SUCCESS on success, or last error encountered
 *
 * Dequeues and dispatches up to max_events events. Stops early if
 * the queue becomes empty or a fatal error occurs.
 */
lle_result_t lle_event_process_queue(lle_event_system_t *system,
                                     uint32_t max_events) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->active) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    uint32_t processed = 0;
    lle_result_t last_result = LLE_SUCCESS;

    while (processed < max_events) {
        /* Dequeue event */
        lle_event_t *event = NULL;
        lle_result_t result = lle_event_dequeue(system, &event);

        if (result == LLE_ERROR_QUEUE_EMPTY) {
            /* No more events */
            break;
        }

        if (result != LLE_SUCCESS) {
            last_result = result;
            break;
        }

        /* Dispatch event */
        result = lle_event_dispatch(system, event);
        if (result != LLE_SUCCESS) {
            last_result = result;
        }

        /* Destroy event */
        lle_event_destroy(system, event);

        processed++;
    }

    return last_result;
}

/**
 * @brief Process all events currently in the queue
 * @param system The event system to process
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Processes events in batches of 100 until the queue is empty.
 */
lle_result_t lle_event_process_all(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Process until queue is empty */
    while (!lle_event_queue_empty(system)) {
        lle_result_t result = lle_event_process_queue(system, 100);
        if (result != LLE_SUCCESS && result != LLE_ERROR_QUEUE_EMPTY) {
            return result;
        }
    }

    return LLE_SUCCESS;
}
