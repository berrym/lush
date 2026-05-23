/**
 * @file event_queue.c
 * @brief LLE Event Queue Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Simple circular buffer queue with thread safety.
 * Provides the core queueing mechanism for the event system.
 *
 * Spec 04: Event System - Phase 1
 */

#include "lle/event_system.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize an event queue
 * @param queue Output pointer to receive initialized queue
 * @param capacity Maximum number of events the queue can hold
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_queue_init(lle_event_queue_t **queue, size_t capacity) {
    if (!queue || capacity == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate queue structure
    lle_event_queue_t *q = lle_pool_alloc(sizeof(lle_event_queue_t));
    if (!q) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(q, 0, sizeof(lle_event_queue_t));

    // Allocate event array
    q->events = lle_pool_alloc(sizeof(lle_event_t *) * capacity);
    if (!q->events) {
        lle_pool_free(q);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(q->events, 0, sizeof(lle_event_t *) * capacity);

    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;

    // Initialize mutex
    pthread_mutex_init(&q->mutex, NULL);

    *queue = q;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy an event queue and free its resources
 * @param queue The queue to destroy, may be NULL
 *
 * Note: Events should be dequeued and destroyed separately before
 * calling this function, or destroyed by the event system.
 */
void lle_event_queue_destroy(lle_event_queue_t *queue) {
    if (!queue) {
        return;
    }

    /* Note: We don't destroy events here - they should be dequeued first
     * or destroyed by the event system */

    // Free event array
    if (queue->events) {
        lle_pool_free(queue->events);
    }

    // Destroy mutex
    pthread_mutex_destroy(&queue->mutex);

    // Free queue structure
    lle_pool_free(queue);
}

/**
 * @brief Add an event to the queue
 * @param system The event system containing the queue
 * @param event The event to enqueue
 * @return LLE_SUCCESS on success, LLE_ERROR_QUEUE_FULL if queue is at capacity,
 *         or LLE_ERROR_INVALID_PARAMETER if parameters are invalid
 *
 * In Phase 2, critical priority events are routed to a separate priority queue.
 * Events dropped due to full queue are tracked in system statistics.
 */
lle_result_t lle_event_enqueue(lle_event_system_t *system, lle_event_t *event) {
    if (!system || !system->queue || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Phase 2: Route to priority queue for CRITICAL priority events only
    lle_event_queue_t *queue;
    if (system->use_priority_queue &&
        event->priority == LLE_PRIORITY_CRITICAL) {
        queue = system->priority_queue;
        __atomic_fetch_add(&system->priority_events_queued, 1,
                           __ATOMIC_SEQ_CST);
    } else {
        queue = system->queue;
    }

    pthread_mutex_lock(&queue->mutex);

    // Check if queue is full
    if (queue->count >= queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        __atomic_fetch_add(&system->events_dropped, 1, __ATOMIC_SEQ_CST);
        return LLE_ERROR_QUEUE_FULL;
    }

    // Add event to tail
    queue->events[queue->tail] = event;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    // Phase 2: Mark event as queued and update statistics
    event->flags |= LLE_EVENT_FLAG_QUEUED;
    __atomic_fetch_add(&system->events_by_priority[event->priority], 1,
                       __ATOMIC_SEQ_CST);

    pthread_mutex_unlock(&queue->mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Remove and return the next event from the queue
 * @param system The event system containing the queue
 * @param event Output pointer to receive the dequeued event
 * @return LLE_SUCCESS on success, LLE_ERROR_QUEUE_EMPTY if no events available,
 *         or LLE_ERROR_INVALID_PARAMETER if parameters are invalid
 *
 * In Phase 2, priority queue is checked first before the main queue.
 * The event's queued flag is cleared upon dequeue.
 */
lle_result_t lle_event_dequeue(lle_event_system_t *system,
                               lle_event_t **event) {
    if (!system || !system->queue || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Phase 2: Check priority queue first if enabled
    lle_event_queue_t *queue;
    bool from_priority_queue = false;

    if (system->use_priority_queue && system->priority_queue) {
        pthread_mutex_lock(&system->priority_queue->mutex);
        if (system->priority_queue->count > 0) {
            queue = system->priority_queue;
            from_priority_queue = true;
        } else {
            pthread_mutex_unlock(&system->priority_queue->mutex);
            queue = system->queue;
            pthread_mutex_lock(&queue->mutex);
        }
    } else {
        queue = system->queue;
        pthread_mutex_lock(&queue->mutex);
    }

    // Check if queue is empty
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        *event = NULL;
        return LLE_ERROR_QUEUE_EMPTY;
    }

    // Remove event from head
    *event = queue->events[queue->head];
    queue->events[queue->head] = NULL;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    // Phase 2: Update event flags and statistics
    if (*event) {
        (*event)->flags &= ~LLE_EVENT_FLAG_QUEUED; // Clear queued flag
        if (from_priority_queue) {
            __atomic_fetch_add(&system->priority_events_processed, 1,
                               __ATOMIC_SEQ_CST);
        }
    }

    pthread_mutex_unlock(&queue->mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get the total number of events in the queue(s)
 * @param system The event system to query
 * @return Total count of queued events (main + priority queue in Phase 2)
 */
size_t lle_event_queue_size(lle_event_system_t *system) {
    if (!system || !system->queue) {
        return 0;
    }

    size_t count = 0;

    // Count events in main queue
    pthread_mutex_lock(&system->queue->mutex);
    count += system->queue->count;
    pthread_mutex_unlock(&system->queue->mutex);

    // Phase 2: Count events in priority queue if enabled
    if (system->use_priority_queue && system->priority_queue) {
        pthread_mutex_lock(&system->priority_queue->mutex);
        count += system->priority_queue->count;
        pthread_mutex_unlock(&system->priority_queue->mutex);
    }

    return count;
}

/**
 * @brief Check if all queues are empty
 * @param system The event system to query
 * @return true if no events are queued, false otherwise
 */
bool lle_event_queue_empty(lle_event_system_t *system) {
    return lle_event_queue_size(system) == 0;
}

/**
 * @brief Check if the queue system is full
 * @param system The event system to query
 * @return true if no more events can be queued, false otherwise
 *
 * In Phase 2, both main and priority queues must be full for this to return
 * true.
 */
bool lle_event_queue_full(lle_event_system_t *system) {
    if (!system || !system->queue) {
        return false;
    }

    bool main_full = false;
    bool priority_full = false;

    // Check main queue
    pthread_mutex_lock(&system->queue->mutex);
    main_full = (system->queue->count >= system->queue->capacity);
    pthread_mutex_unlock(&system->queue->mutex);

    // Phase 2: Check priority queue if enabled
    if (system->use_priority_queue && system->priority_queue) {
        pthread_mutex_lock(&system->priority_queue->mutex);
        priority_full =
            (system->priority_queue->count >= system->priority_queue->capacity);
        pthread_mutex_unlock(&system->priority_queue->mutex);

        // Both queues must be full for system to be considered full
        return main_full && priority_full;
    }

    return main_full;
}
