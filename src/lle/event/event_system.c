/**
 * @file event_system.c
 * @brief LLE Event System Core Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Core event system lifecycle and event creation/destruction.
 * Implements the foundation of the LLE event-driven architecture.
 *
 * Spec 04: Event System - Phase 1
 */

#include "lle/event_system.h"
#include "lle/time_util.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Default event queue capacity */
#define LLE_EVENT_QUEUE_DEFAULT_CAPACITY 1024
/** @brief Initial handler array capacity */
#define LLE_EVENT_HANDLER_INITIAL_CAPACITY 32

/**
 * @brief Get human-readable name for an event type
 * @param type Event type enumeration value
 * @return Static string containing event type name
 */
const char *lle_event_type_name(lle_event_kind_t type) {
    switch (type) {
    /// Terminal Input Events
    case LLE_EVENT_KEY_PRESS:
        return "KEY_PRESS";
    case LLE_EVENT_KEY_RELEASE:
        return "KEY_RELEASE";
    case LLE_EVENT_KEY_SEQUENCE:
        return "KEY_SEQUENCE";
    case LLE_EVENT_KEY_CHORD:
        return "KEY_CHORD";

    /// Mouse Events
    case LLE_EVENT_MOUSE_PRESS:
        return "MOUSE_PRESS";
    case LLE_EVENT_MOUSE_RELEASE:
        return "MOUSE_RELEASE";
    case LLE_EVENT_MOUSE_MOVE:
        return "MOUSE_MOVE";
    case LLE_EVENT_MOUSE_WHEEL:
        return "MOUSE_WHEEL";
    case LLE_EVENT_MOUSE_DRAG:
        return "MOUSE_DRAG";

    /// Terminal State Events
    case LLE_EVENT_TERMINAL_RESIZE:
        return "TERMINAL_RESIZE";
    case LLE_EVENT_FOCUS_IN:
        return "FOCUS_IN";
    case LLE_EVENT_FOCUS_OUT:
        return "FOCUS_OUT";
    case LLE_EVENT_TERMINAL_DISCONNECT:
        return "TERMINAL_DISCONNECT";

    /// Paste Events
    case LLE_EVENT_PASTE_START:
        return "PASTE_START";
    case LLE_EVENT_PASTE_DATA:
        return "PASTE_DATA";
    case LLE_EVENT_PASTE_END:
        return "PASTE_END";
    case LLE_EVENT_PASTE_TIMEOUT:
        return "PASTE_TIMEOUT";

    /// Buffer Events
    case LLE_EVENT_BUFFER_CHANGED:
        return "BUFFER_CHANGED";
    case LLE_EVENT_CURSOR_MOVED:
        return "CURSOR_MOVED";
    case LLE_EVENT_SELECTION_CHANGED:
        return "SELECTION_CHANGED";
    case LLE_EVENT_BUFFER_VALIDATION:
        return "BUFFER_VALIDATION";

    /// History Events
    case LLE_EVENT_HISTORY_CHANGED:
        return "HISTORY_CHANGED";
    case LLE_EVENT_HISTORY_SEARCH:
        return "HISTORY_SEARCH";
    case LLE_EVENT_HISTORY_NAVIGATE:
        return "HISTORY_NAVIGATE";

    /// Completion Events
    case LLE_EVENT_COMPLETION_REQUESTED:
        return "COMPLETION_REQUESTED";
    case LLE_EVENT_COMPLETION_UPDATED:
        return "COMPLETION_UPDATED";
    case LLE_EVENT_COMPLETION_ACCEPTED:
        return "COMPLETION_ACCEPTED";
    case LLE_EVENT_COMPLETION_CANCELLED:
        return "COMPLETION_CANCELLED";

    /// Suggestion Events
    case LLE_EVENT_SUGGESTION_UPDATED:
        return "SUGGESTION_UPDATED";
    case LLE_EVENT_SUGGESTION_ACCEPTED:
        return "SUGGESTION_ACCEPTED";
    case LLE_EVENT_SUGGESTION_DISMISSED:
        return "SUGGESTION_DISMISSED";

    /// System Events
    case LLE_EVENT_SYSTEM_ERROR:
        return "SYSTEM_ERROR";
    case LLE_EVENT_SYSTEM_WARNING:
        return "SYSTEM_WARNING";
    case LLE_EVENT_SYSTEM_SHUTDOWN:
        return "SYSTEM_SHUTDOWN";
    case LLE_EVENT_MEMORY_PRESSURE:
        return "MEMORY_PRESSURE";

    /// Timer Events
    case LLE_EVENT_TIMER_EXPIRED:
        return "TIMER_EXPIRED";
    case LLE_EVENT_TIMEOUT:
        return "TIMEOUT";
    case LLE_EVENT_PERIODIC_UPDATE:
        return "PERIODIC_UPDATE";

    /// Plugin Events
    case LLE_EVENT_PLUGIN_CUSTOM:
        return "PLUGIN_CUSTOM";
    case LLE_EVENT_PLUGIN_MESSAGE:
        return "PLUGIN_MESSAGE";
    case LLE_EVENT_PLUGIN_ERROR:
        return "PLUGIN_ERROR";

    /// Display Events
    case LLE_EVENT_DISPLAY_UPDATE:
        return "DISPLAY_UPDATE";
    case LLE_EVENT_DISPLAY_REFRESH:
        return "DISPLAY_REFRESH";
    case LLE_EVENT_DISPLAY_INVALIDATE:
        return "DISPLAY_INVALIDATE";

    /// Shell Lifecycle Events
    case LLE_EVENT_DIRECTORY_CHANGED:
        return "DIRECTORY_CHANGED";
    case LLE_EVENT_PRE_COMMAND:
        return "PRE_COMMAND";
    case LLE_EVENT_POST_COMMAND:
        return "POST_COMMAND";
    case LLE_EVENT_COMMAND_NOT_FOUND:
        return "COMMAND_NOT_FOUND";
    case LLE_EVENT_PROMPT_DISPLAY:
        return "PROMPT_DISPLAY";

    /// Debug/Test Events
    case LLE_EVENT_DEBUG_MARKER:
        return "DEBUG_MARKER";
    case LLE_EVENT_TEST_TRIGGER:
        return "TEST_TRIGGER";
    case LLE_EVENT_PROFILING_SAMPLE:
        return "PROFILING_SAMPLE";

    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Initialize the event system
 *
 * Creates and initializes an event system with queues, handler arrays,
 * and all supporting subsystems (timer, filter, stats).
 *
 * @param system Output pointer to receive initialized event system
 * @param pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_system_init(lle_event_system_t **system,
                                   lle_memory_pool_t *pool) {
    if (!system || !pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate system structure
    lle_event_system_t *sys = lle_pool_alloc(sizeof(lle_event_system_t));
    if (!sys) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(sys, 0, sizeof(lle_event_system_t));

    /// Initialize main queue
    lle_result_t result =
        lle_event_queue_init(&sys->queue, LLE_EVENT_QUEUE_DEFAULT_CAPACITY);
    if (result != LLE_SUCCESS) {
        lle_pool_free(sys);
        return result;
    }

    /// Phase 2: Initialize priority queue
    result = lle_event_queue_init(&sys->priority_queue,
                                  LLE_EVENT_QUEUE_DEFAULT_CAPACITY / 2);
    if (result != LLE_SUCCESS) {
        lle_event_queue_destroy(sys->queue);
        lle_pool_free(sys);
        return result;
    }

    /// Initialize handler array
    sys->handler_capacity = LLE_EVENT_HANDLER_INITIAL_CAPACITY;
    sys->handlers =
        lle_pool_alloc(sizeof(lle_event_handler_t *) * sys->handler_capacity);
    if (!sys->handlers) {
        lle_event_queue_destroy(sys->priority_queue);
        lle_event_queue_destroy(sys->queue);
        lle_pool_free(sys);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    sys->handler_count = 0;
    sys->event_pool = pool;
    sys->sequence_counter = 1; /// Start at 1 so first event gets sequence 1
    sys->active = true; /// System is active after successful initialization

    /// Phase 1 statistics
    sys->events_created = 0;
    sys->events_dispatched = 0;
    sys->events_dropped = 0;

    /// Phase 2A configuration and statistics
    sys->use_priority_queue = true; /// Enable priority queue by default
    sys->priority_events_queued = 0;
    sys->priority_events_processed = 0;
    for (int i = 0; i < LLE_PRIORITY_COUNT; i++) {
        sys->events_by_priority[i] = 0;
    }

    /// Phase 2B: Initialize processing configuration with defaults
    sys->processing_config.max_events_per_cycle =
        100; /// Default: 100 events per cycle
    sys->processing_config.cycle_time_limit_us =
        10000;                                   /// Default: 10ms per cycle
    sys->processing_config.auto_process = false; /// Default: manual processing
    sys->processing_config.record_detailed_stats =
        false; /// Default: disabled (opt-in)

    /// Phase 2B: Initialize processing state
    sys->processing_state = LLE_PROCESSING_RUNNING; /// Default: running

    /// Phase 2B: Enhanced statistics starts NULL (created on demand)
    sys->enhanced_stats = NULL;

    /// Phase 2C: Initialize event filtering and hooks
    sys->filter_system = NULL; /// Filter system created on demand
    sys->pre_dispatch_hook = NULL;
    sys->pre_dispatch_data = NULL;
    sys->post_dispatch_hook = NULL;
    sys->post_dispatch_data = NULL;

    /// Phase 2C: Initialize system state tracking
    sys->current_state = LLE_STATE_INITIALIZING;
    sys->previous_state = LLE_STATE_INITIALIZING;
    sys->state_changed_time = lle_get_current_time_microseconds();

    /// Phase 2D: Timer system starts NULL (created on demand)
    sys->timer_system = NULL;

    /// Initialize mutex
    pthread_mutex_init(&sys->system_mutex, NULL);

    /// Phase 2C: Transition to IDLE state after successful initialization
    sys->current_state = LLE_STATE_IDLE;
    sys->state_changed_time = lle_get_current_time_microseconds();

    *system = sys;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy the event system and release all resources
 *
 * Cleans up all subsystems (timer, filter, stats), queues, handlers,
 * and frees all allocated memory.
 *
 * @param system Event system to destroy
 */
void lle_event_system_destroy(lle_event_system_t *system) {
    if (!system) {
        return;
    }

    /// Phase 2C: Set state to shutting down
    system->current_state = LLE_STATE_SHUTTING_DOWN;

    /// Stop system if active
    if (system->active) {
        lle_event_system_stop(system);
    }

    /// Phase 2D: Destroy timer system if allocated
    if (system->timer_system) {
        lle_event_timer_system_destroy(system);
    }

    /// Phase 2C: Destroy filter system if allocated
    if (system->filter_system) {
        lle_event_filter_system_destroy(system);
    }

    /// Phase 2B: Destroy enhanced statistics if allocated
    if (system->enhanced_stats) {
        lle_event_enhanced_stats_destroy(system);
    }

    /// Destroy main queue
    if (system->queue) {
        lle_event_queue_destroy(system->queue);
    }

    /// Phase 2A: Destroy priority queue
    if (system->priority_queue) {
        lle_event_queue_destroy(system->priority_queue);
    }

    /// Free handlers
    if (system->handlers) {
        for (size_t i = 0; i < system->handler_count; i++) {
            if (system->handlers[i]) {
                lle_pool_free(system->handlers[i]);
            }
        }
        lle_pool_free(system->handlers);
    }

    /// Destroy mutex
    pthread_mutex_destroy(&system->system_mutex);

    /// Free system structure
    lle_pool_free(system);
}

/**
 * @brief Start the event system
 * @param system Event system to start
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_system_start(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    if (system->active) {
        pthread_mutex_unlock(&system->system_mutex);
        return LLE_ERROR_ALREADY_INITIALIZED;
    }

    system->active = true;

    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Stop the event system
 * @param system Event system to stop
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_system_stop(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    if (!system->active) {
        pthread_mutex_unlock(&system->system_mutex);
        return LLE_SUCCESS;
    }

    system->active = false;

    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get default priority for an event type
 * @param type Event type
 * @return Appropriate priority level for the event type
 */
static lle_event_priority_t
lle_event_get_priority_for_type(lle_event_kind_t type) {
    /// Determine priority based on event type category
    if (type >= 0x1000 && type < 0x2000) {
        /// Terminal Input Events - HIGH
        return LLE_PRIORITY_HIGH;
    } else if (type >= 0x2000 && type < 0x3000) {
        /// Mouse Events - MEDIUM
        return LLE_PRIORITY_MEDIUM;
    } else if (type >= 0x3000 && type < 0x4000) {
        /// Terminal State Events - CRITICAL
        return LLE_PRIORITY_CRITICAL;
    } else if (type >= 0x4000 && type < 0x5000) {
        /// Paste Events - HIGH
        return LLE_PRIORITY_HIGH;
    } else if (type >= 0x5000 && type < 0x6000) {
        /// Buffer Events - MEDIUM
        return LLE_PRIORITY_MEDIUM;
    } else if (type >= 0x6000 && type < 0x7000) {
        /// History Events - LOW
        return LLE_PRIORITY_LOW;
    } else if (type >= 0x7000 && type < 0x8000) {
        /// Completion Events - MEDIUM
        return LLE_PRIORITY_MEDIUM;
    } else if (type >= 0x8000 && type < 0x9000) {
        /// Suggestion Events - LOW
        return LLE_PRIORITY_LOW;
    } else if (type >= 0x9000 && type < 0xA000) {
        /// System Events - CRITICAL
        return LLE_PRIORITY_CRITICAL;
    } else if (type >= 0xA000 && type < 0xB000) {
        /// Timer Events - LOW
        return LLE_PRIORITY_LOW;
    } else if (type >= 0xB000 && type < 0xC000) {
        /// Plugin Events - MEDIUM (configurable)
        return LLE_PRIORITY_MEDIUM;
    } else if (type >= 0xC000 && type < 0xD000) {
        /// Display Events - HIGH
        return LLE_PRIORITY_HIGH;
    } else if (type >= 0xF000 && type <= 0xFFFF) {
        /// Debug/Test Events - LOWEST
        return LLE_PRIORITY_LOWEST;
    }

    /// Default to MEDIUM priority
    return LLE_PRIORITY_MEDIUM;
}

/**
 * @brief Create a new event
 *
 * Allocates and initializes an event with the specified type and data.
 * The event data is deep-copied if provided.
 *
 * @param system Event system
 * @param type Event type
 * @param data Event data (copied if not NULL)
 * @param data_size Size of event data
 * @param event Output pointer to receive created event
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_create(lle_event_system_t *system, lle_event_kind_t type,
                              void *data, size_t data_size,
                              lle_event_t **event) {
    if (!system || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate event structure
    lle_event_t *evt = lle_pool_alloc(sizeof(lle_event_t));
    if (!evt) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(evt, 0, sizeof(lle_event_t));

    /// Allocate and copy data if provided
    if (data && data_size > 0) {
        evt->data = lle_pool_alloc(data_size);
        if (!evt->data) {
            lle_pool_free(evt);
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        memcpy(evt->data, data, data_size);
        evt->data_size = data_size;
    }

    /// Set Phase 1 event fields
    evt->type = type;
    evt->sequence_number =
        __atomic_fetch_add(&system->sequence_counter, 1, __ATOMIC_SEQ_CST);
    evt->timestamp = lle_get_current_time_microseconds();
    evt->next = NULL;

    /// Set Phase 2 event fields
    evt->source = LLE_EVENT_SOURCE_INTERNAL; /// Default source
    evt->priority = lle_event_get_priority_for_type(type);
    evt->flags = LLE_EVENT_FLAG_NONE;
    evt->handler_count = 0;
    evt->processing_start_time = 0;
    evt->processing_end_time = 0;
    evt->prev = NULL;

    /// Update statistics
    __atomic_fetch_add(&system->events_created, 1, __ATOMIC_SEQ_CST);

    *event = evt;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy an event and free its resources
 * @param system Event system (unused, reserved for future use)
 * @param event Event to destroy
 */
void lle_event_destroy(lle_event_system_t *system, lle_event_t *event) {
    if (!event) {
        return;
    }

    /// Free event data
    if (event->data) {
        lle_pool_free(event->data);
    }

    /// Free event structure
    lle_pool_free(event);

    (void)system; /// Unused for now
}

/**
 * @brief Clone an existing event
 * @param system Event system
 * @param source Event to clone
 * @param dest Output pointer to receive cloned event
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_clone(lle_event_system_t *system, lle_event_t *source,
                             lle_event_t **dest) {
    if (!system || !source || !dest) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Create new event with same type and data
    return lle_event_create(system, source->type, source->data,
                            source->data_size, dest);
}

/**
 * @brief Get event system statistics
 * @param system Event system
 * @param created Output for events created count (may be NULL)
 * @param dispatched Output for events dispatched count (may be NULL)
 * @param dropped Output for events dropped count (may be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_system_get_stats(lle_event_system_t *system,
                                        uint64_t *created, uint64_t *dispatched,
                                        uint64_t *dropped) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (created) {
        *created = __atomic_load_n(&system->events_created, __ATOMIC_SEQ_CST);
    }
    if (dispatched) {
        *dispatched =
            __atomic_load_n(&system->events_dispatched, __ATOMIC_SEQ_CST);
    }
    if (dropped) {
        *dropped = __atomic_load_n(&system->events_dropped, __ATOMIC_SEQ_CST);
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * Shell Lifecycle Events Implementation
 * ============================================================================
 */

/**
 * @brief Fire a directory changed event
 *
 * This is the core event for fixing stale git prompts (Issue #16).
 * When the working directory changes, this event allows handlers to
 * invalidate cached data that depends on the current directory.
 *
 * @param system Event system
 * @param old_dir Previous directory path (may be NULL)
 * @param new_dir New directory path
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_fire_directory_changed(lle_event_system_t *system,
                                              const char *old_dir,
                                              const char *new_dir) {
    if (!system || !new_dir) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_DIRECTORY_CHANGED, NULL, 0, &event);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Populate shell event data
    event->source = LLE_EVENT_SOURCE_SHELL;
    event->priority = LLE_PRIORITY_HIGH;

    if (old_dir) {
        strncpy(event->event_data.shell.old_directory, old_dir,
                sizeof(event->event_data.shell.old_directory) - 1);
        event->event_data.shell
            .old_directory[sizeof(event->event_data.shell.old_directory) - 1] =
            '\0';
    } else {
        event->event_data.shell.old_directory[0] = '\0';
    }

    strncpy(event->event_data.shell.new_directory, new_dir,
            sizeof(event->event_data.shell.new_directory) - 1);
    event->event_data.shell
        .new_directory[sizeof(event->event_data.shell.new_directory) - 1] =
        '\0';

    /// Enqueue and dispatch immediately for prompt responsiveness
    result = lle_event_enqueue(system, event);
    if (result != LLE_SUCCESS) {
        lle_event_destroy(system, event);
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Fire a pre-command event
 *
 * Called before command execution to allow the prompt system to record
 * the current prompt position for transient prompt replacement.
 *
 * @param system Event system
 * @param command Command about to be executed (may be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_fire_pre_command(lle_event_system_t *system,
                                        const char *command) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_PRE_COMMAND, NULL, 0, &event);
    if (result != LLE_SUCCESS) {
        return result;
    }

    event->source = LLE_EVENT_SOURCE_SHELL;
    event->priority = LLE_PRIORITY_HIGH;

    if (command) {
        strncpy(event->event_data.shell.command, command,
                sizeof(event->event_data.shell.command) - 1);
        event->event_data.shell
            .command[sizeof(event->event_data.shell.command) - 1] = '\0';
    } else {
        event->event_data.shell.command[0] = '\0';
    }

    result = lle_event_enqueue(system, event);
    if (result != LLE_SUCCESS) {
        lle_event_destroy(system, event);
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Fire a post-command event
 *
 * Called after command execution with exit code and duration.
 * Used for prompt status display and history enrichment.
 *
 * @param system Event system
 * @param command Command that was executed (may be NULL)
 * @param exit_code Command exit code
 * @param duration_us Command execution duration in microseconds
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_fire_post_command(lle_event_system_t *system,
                                         const char *command, int exit_code,
                                         uint64_t duration_us) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_POST_COMMAND, NULL, 0, &event);
    if (result != LLE_SUCCESS) {
        return result;
    }

    event->source = LLE_EVENT_SOURCE_SHELL;
    event->priority = LLE_PRIORITY_HIGH;

    if (command) {
        strncpy(event->event_data.shell.command, command,
                sizeof(event->event_data.shell.command) - 1);
        event->event_data.shell
            .command[sizeof(event->event_data.shell.command) - 1] = '\0';
    } else {
        event->event_data.shell.command[0] = '\0';
    }

    event->event_data.shell.exit_code = exit_code;
    event->event_data.shell.duration_us = duration_us;

    result = lle_event_enqueue(system, event);
    if (result != LLE_SUCCESS) {
        lle_event_destroy(system, event);
        return result;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Fire a prompt display event
 *
 * Called just before the prompt is rendered. Allows the prompt system
 * to check if regeneration is needed and apply any pending updates.
 *
 * @param system Event system
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_event_fire_prompt_display(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_PROMPT_DISPLAY, NULL, 0, &event);
    if (result != LLE_SUCCESS) {
        return result;
    }

    event->source = LLE_EVENT_SOURCE_SHELL;
    event->priority = LLE_PRIORITY_HIGH;

    result = lle_event_enqueue(system, event);
    if (result != LLE_SUCCESS) {
        lle_event_destroy(system, event);
        return result;
    }

    return LLE_SUCCESS;
}
