/**
 * @file event_system.h
 * @brief Event System Header (Phase 1 + Phase 2A + Phase 2B + Phase 2C + Phase
 * 2D)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Event-driven architecture for LLE.
 *
 * Phase 1 (Complete):
 * - Basic event types (input, terminal, buffer, display, system)
 * - FIFO event queue
 * - Handler registration and dispatch
 * - Thread-safe queue operations
 *
 * Phase 2A (Complete):
 * - Expanded event types (70+ event types across 12 categories)
 * - Priority-based event processing (5 priority levels)
 * - Event priority and source tracking
 * - Dual queue system (priority queue for CRITICAL events)
 * - Event metadata (flags, processing times, handler counts)
 * - Typed event data structures (key, mouse, resize, paste, etc.)
 *
 * Phase 2B (Complete):
 * - Enhanced statistics (per-type counters, cycle timing, queue depth)
 * - Processing configuration (limits, auto-processing, detailed stats)
 * - Processing state control (running, stopped, paused)
 * - Statistics query API
 * - Configuration API
 *
 * Phase 2C (Complete):
 * - Event filtering (callback-based, add/remove/enable/disable filters)
 * - Filter statistics (per-filter counters for passed/blocked/transformed)
 * - Pre/post dispatch hooks
 * - System state tracking (idle, processing, paused, error, etc.)
 * - State management API
 *
 * Phase 2D (Complete):
 * - Timer events (one-shot and repeating timers)
 * - Timer management API
 * - Timer processing and scheduling
 *
 * Spec 04: Event System - Complete Specification
 */

#ifndef LLE_EVENT_SYSTEM_H
#define LLE_EVENT_SYSTEM_H

#include "error_handling.h"
#include "memory_management.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
typedef struct lle_event lle_event_t;
typedef struct lle_event_queue lle_event_queue_t;
typedef struct lle_event_handler lle_event_handler_t;
typedef struct lle_event_system lle_event_system_t;

/*
 * Event Types (Phase 1 + Phase 2 - Complete)
 * Note: Using 'kind' terminology to avoid conflict with memory pool event_type
 */
typedef enum {
    // Terminal Input Events (0x1000 - 0x1FFF) - Priority: HIGH
    LLE_EVENT_KEY_PRESS = 0x1000, /**< Single key press */
    LLE_EVENT_KEY_RELEASE,        /**< Key release (if supported) */
    LLE_EVENT_KEY_SEQUENCE,       /**< Multi-key sequence (escape sequences) */
    LLE_EVENT_KEY_CHORD,          /**< Simultaneous key combination */

    // Mouse Events (0x2000 - 0x2FFF) - Priority: MEDIUM
    LLE_EVENT_MOUSE_PRESS = 0x2000, /**< Mouse button press */
    LLE_EVENT_MOUSE_RELEASE,        /**< Mouse button release */
    LLE_EVENT_MOUSE_MOVE,           /**< Mouse movement */
    LLE_EVENT_MOUSE_WHEEL,          /**< Mouse wheel scroll */
    LLE_EVENT_MOUSE_DRAG,           /**< Mouse drag operation */

    // Terminal State Events (0x3000 - 0x3FFF) - Priority: CRITICAL
    LLE_EVENT_TERMINAL_RESIZE = 0x3000, /**< Terminal size change */
    LLE_EVENT_FOCUS_IN,                 /**< Terminal gained focus */
    LLE_EVENT_FOCUS_OUT,                /**< Terminal lost focus */
    LLE_EVENT_TERMINAL_DISCONNECT,      /**< Terminal disconnected */

    // Paste Events (0x4000 - 0x4FFF) - Priority: HIGH
    LLE_EVENT_PASTE_START = 0x4000, /**< Bracketed paste start */
    LLE_EVENT_PASTE_DATA,           /**< Paste data chunk */
    LLE_EVENT_PASTE_END,            /**< Bracketed paste end */
    LLE_EVENT_PASTE_TIMEOUT,        /**< Paste operation timeout */

    // Buffer Events (0x5000 - 0x5FFF) - Priority: MEDIUM
    LLE_EVENT_BUFFER_CHANGED = 0x5000, /**< Buffer content changed */
    LLE_EVENT_CURSOR_MOVED,            /**< Cursor position changed */
    LLE_EVENT_SELECTION_CHANGED,       /**< Text selection changed */
    LLE_EVENT_BUFFER_VALIDATION,       /**< Buffer validation request */

    // History Events (0x6000 - 0x6FFF) - Priority: LOW
    LLE_EVENT_HISTORY_CHANGED = 0x6000, /**< History state changed */
    LLE_EVENT_HISTORY_SEARCH,           /**< History search request */
    LLE_EVENT_HISTORY_NAVIGATE,         /**< History navigation */

    // Completion Events (0x7000 - 0x7FFF) - Priority: MEDIUM
    LLE_EVENT_COMPLETION_REQUESTED = 0x7000, /**< Tab completion requested */
    LLE_EVENT_COMPLETION_UPDATED,            /**< Completion list updated */
    LLE_EVENT_COMPLETION_ACCEPTED,           /**< Completion accepted */
    LLE_EVENT_COMPLETION_CANCELLED,          /**< Completion cancelled */

    // Suggestion Events (0x8000 - 0x8FFF) - Priority: LOW
    LLE_EVENT_SUGGESTION_UPDATED = 0x8000, /**< Autosuggestion updated */
    LLE_EVENT_SUGGESTION_ACCEPTED,         /**< Suggestion accepted */
    LLE_EVENT_SUGGESTION_DISMISSED,        /**< Suggestion dismissed */

    // System Events (0x9000 - 0x9FFF) - Priority: CRITICAL
    LLE_EVENT_SYSTEM_ERROR = 0x9000, /**< System error occurred */
    LLE_EVENT_SYSTEM_WARNING,        /**< System warning */
    LLE_EVENT_SYSTEM_SHUTDOWN,       /**< System shutdown request */
    LLE_EVENT_MEMORY_PRESSURE,       /**< Memory pressure warning */

    // Timer Events (0xA000 - 0xAFFF) - Priority: LOW
    LLE_EVENT_TIMER_EXPIRED = 0xA000, /**< Timer expired */
    LLE_EVENT_TIMEOUT,                /**< General timeout */
    LLE_EVENT_PERIODIC_UPDATE,        /**< Periodic update trigger */

    // Plugin Events (0xB000 - 0xBFFF) - Priority: CONFIGURABLE
    LLE_EVENT_PLUGIN_CUSTOM = 0xB000, /**< Plugin-defined event */
    LLE_EVENT_PLUGIN_MESSAGE,         /**< Inter-plugin message */
    LLE_EVENT_PLUGIN_ERROR,           /**< Plugin error */

    // Display Integration Events (0xC000 - 0xCFFF) - Priority: HIGH
    LLE_EVENT_DISPLAY_UPDATE = 0xC000, /**< Display update required */
    LLE_EVENT_DISPLAY_REFRESH,         /**< Display refresh request */
    LLE_EVENT_DISPLAY_INVALIDATE,      /**< Display invalidation */

    // Shell Lifecycle Events (0xD000 - 0xDFFF) - Priority: HIGH
    LLE_EVENT_DIRECTORY_CHANGED = 0xD000, /**< Working directory changed (cd) */
    LLE_EVENT_PRE_COMMAND,                /**< Before command execution */
    LLE_EVENT_POST_COMMAND,               /**< After command execution */
    LLE_EVENT_COMMAND_NOT_FOUND,          /**< Command not found */
    LLE_EVENT_PROMPT_DISPLAY,             /**< Prompt about to be displayed */

    // Testing and Debug Events (0xF000 - 0xFFFF) - Priority: LOWEST
    LLE_EVENT_DEBUG_MARKER = 0xF000, /**< Debug marker event */
    LLE_EVENT_TEST_TRIGGER,          /**< Test trigger event */
    LLE_EVENT_PROFILING_SAMPLE,      /**< Profiling sample */
} lle_event_kind_t;

/*
 * Event Priority Levels (Phase 2)
 */
typedef enum {
    LLE_PRIORITY_CRITICAL =
        0, /**< System-critical events (terminal state, errors) */
    LLE_PRIORITY_HIGH = 1, /**< High-priority user events (key input, paste) */
    LLE_PRIORITY_MEDIUM = 2, /**< Normal priority events (buffer, completion) */
    LLE_PRIORITY_LOW =
        3, /**< Low priority events (history, suggestions, timers) */
    LLE_PRIORITY_LOWEST = 4, /**< Lowest priority events (debug, profiling) */
    LLE_PRIORITY_COUNT = 5   // Total priority levels
} lle_event_priority_t;

/*
 * Event Source Identification (Phase 2)
 */
typedef enum {
    LLE_EVENT_SOURCE_TERMINAL,   /**< Terminal input */
    LLE_EVENT_SOURCE_INTERNAL,   /**< Internal system event */
    LLE_EVENT_SOURCE_PLUGIN,     /**< Plugin-generated event */
    LLE_EVENT_SOURCE_TIMER,      /**< Timer-generated event */
    LLE_EVENT_SOURCE_DISPLAY,    /**< Display system event */
    LLE_EVENT_SOURCE_BUFFER,     /**< Buffer system event */
    LLE_EVENT_SOURCE_HISTORY,    /**< History system event */
    LLE_EVENT_SOURCE_COMPLETION, /**< Completion system event */
    LLE_EVENT_SOURCE_SHELL, /**< Shell lifecycle event (cd, command exec) */
    LLE_EVENT_SOURCE_TEST,  /**< Test system event */
} lle_event_source_t;

/*
 * Event Flags (Phase 2)
 */
typedef enum {
    LLE_EVENT_FLAG_NONE = 0,             /**< No flags */
    LLE_EVENT_FLAG_HANDLED = (1 << 0),   /**< Event has been handled */
    LLE_EVENT_FLAG_CANCELLED = (1 << 1), /**< Event processing cancelled */
    LLE_EVENT_FLAG_SYNTHETIC = (1 << 2), /**< Synthetically generated event */
    LLE_EVENT_FLAG_PROPAGATE =
        (1 << 3), /**< Continue propagating to other handlers */
    LLE_EVENT_FLAG_QUEUED = (1 << 4),     /**< Event is currently queued */
    LLE_EVENT_FLAG_PROCESSING = (1 << 5), /**< Event is being processed */
} lle_event_flags_t;

/*
 * Key Event Data (Phase 2)
 */
typedef struct {
    uint32_t key_code;  /**< Key code */
    uint32_t modifiers; /**< Modifier keys (Ctrl, Alt, Shift) */
    char utf8_char[8];  /**< UTF-8 character */
    bool is_special;    /**< Special key (arrow, function key) */
} lle_key_event_data_t;

/*
 * Mouse Event Data (Phase 2)
 */
typedef struct {
    uint32_t button;     /**< Mouse button (1=left, 2=middle, 3=right) */
    uint32_t x;          /**< X coordinate */
    uint32_t y;          /**< Y coordinate */
    int32_t wheel_delta; /**< Wheel scroll delta */
    uint32_t modifiers;  /**< Modifier keys */
} lle_mouse_event_data_t;

/*
 * Terminal Resize Event Data (Phase 2)
 */
typedef struct {
    uint32_t old_width;  /**< Previous terminal width */
    uint32_t old_height; /**< Previous terminal height */
    uint32_t new_width;  /**< New terminal width */
    uint32_t new_height; /**< New terminal height */
} lle_resize_event_data_t;

/*
 * Paste Event Data (Phase 2)
 */
typedef struct {
    const char *data;  /**< Paste data */
    size_t length;     /**< Data length */
    bool is_bracketed; /**< Bracketed paste mode */
} lle_paste_event_data_t;

/*
 * Buffer Event Data (Phase 2)
 */
typedef struct {
    size_t old_cursor_pos; /**< Previous cursor position */
    size_t new_cursor_pos; /**< New cursor position */
    size_t old_length;     /**< Previous buffer length */
    size_t new_length;     /**< New buffer length */
} lle_buffer_event_data_t;

/*
 * Error Event Data (Phase 2)
 */
typedef struct {
    lle_result_t error_code; /**< Error code */
    char message[256];       /**< Error message */
    const char *source_file; /**< Source file */
    int source_line;         /**< Source line */
} lle_error_event_data_t;

/*
 * Timer Event Data (Phase 2)
 */
typedef struct {
    uint64_t timer_id;    /**< Timer identifier */
    uint64_t interval_us; /**< Timer interval (microseconds) */
    bool is_periodic;     /**< Periodic timer */
} lle_timer_event_data_t;

/*
 * Custom Event Data (Phase 2)
 */
typedef struct {
    char event_name[64];     /**< Custom event name */
    void *custom_data;       /**< Custom data pointer */
    size_t custom_data_size; /**< Custom data size */
} lle_custom_event_data_t;

/*
 * Shell Lifecycle Event Data
 * Used for directory changes, command execution events, etc.
 */
typedef struct {
    char old_directory[4096]; /**< Previous working directory */
    char new_directory[4096]; /**< New working directory (for DIRECTORY_CHANGED)
                               */
    char command[4096];   /**< Command being executed (for PRE/POST_COMMAND) */
    int exit_code;        /**< Command exit code (for POST_COMMAND) */
    uint64_t duration_us; /**< Command duration in microseconds */
} lle_shell_event_data_t;

/*
 * Event Data Union (Phase 2)
 */
typedef union {
    lle_key_event_data_t key;       /**< Key event data */
    lle_mouse_event_data_t mouse;   /**< Mouse event data */
    lle_resize_event_data_t resize; /**< Resize event data */
    lle_paste_event_data_t paste;   /**< Paste event data */
    lle_buffer_event_data_t buffer; /**< Buffer event data */
    lle_error_event_data_t error;   /**< Error event data */
    lle_timer_event_data_t timer;   /**< Timer event data */
    lle_custom_event_data_t custom; /**< Custom event data */
    lle_shell_event_data_t shell;   /**< Shell lifecycle event data */
} lle_event_data_union_t;

/*
 * Event Structure (Phase 1 + Phase 2)
 */
struct lle_event {
    // Core event identification (Phase 1)
    lle_event_kind_t type;    /**< Event kind/type */
    uint64_t sequence_number; /**< Global sequence number */
    uint64_t timestamp;       /**< Event timestamp (microseconds) */

    // Phase 2: Event metadata
    lle_event_source_t source;     /**< Event source */
    lle_event_priority_t priority; /**< Event priority */
    lle_event_flags_t flags;       /**< Event flags */

    // Phase 2: Event processing state
    uint32_t handler_count;         /**< Number of handlers invoked */
    uint64_t processing_start_time; /**< Processing start timestamp */
    uint64_t processing_end_time;   /**< Processing end timestamp */

    // Event data (Phase 1 - preserved for backward compatibility)
    void *data;       /**< Generic event-specific data */
    size_t data_size; /**< Size of data */

    // Phase 2: Typed event data union
    lle_event_data_union_t event_data; /**< Typed event data */

    // Queue linkage (Phase 1)
    struct lle_event *next; /**< Next event in queue */

    // Phase 2: Doubly-linked list support
    struct lle_event *prev; /**< Previous event in queue */
};

/*
 * Event Handler Function Type
 */
typedef lle_result_t (*lle_event_handler_fn)(lle_event_t *event,
                                             void *user_data);

/*
 * Event Handler Structure
 */
struct lle_event_handler {
    lle_event_kind_t event_type;  /**< Event kind this handler handles */
    lle_event_handler_fn handler; /**< Handler function */
    void *user_data;              /**< User data passed to handler */
    char name[64];                /**< Handler name (for debugging) */
};

/*
 * Event Queue Structure (Phase 1 - circular buffer)
 */
struct lle_event_queue {
    lle_event_t **events;  /**< Array of event pointers */
    size_t capacity;       /**< Queue capacity */
    size_t head;           /**< Read position */
    size_t tail;           /**< Write position */
    size_t count;          /**< Current event count */
    pthread_mutex_t mutex; /**< Thread safety */
};

/*
 * Per-Event-Type Statistics (Phase 2B)
 */
typedef struct {
    lle_event_kind_t event_type;    /**< Event type */
    uint64_t count;                 /**< Total events of this type */
    uint64_t total_processing_time; /**< Total microseconds spent */
    uint64_t min_processing_time;   /**< Minimum processing time */
    uint64_t max_processing_time;   /**< Maximum processing time */
    uint64_t last_processed_time;   /**< Last processing timestamp */
} lle_event_type_stats_t;

/*
 * Enhanced Statistics Structure (Phase 2B)
 */
typedef struct {
    // Cycle statistics
    uint64_t cycles_completed;
    uint64_t total_cycle_time;
    uint64_t min_cycle_time;
    uint64_t max_cycle_time;

    // Per-type statistics
    lle_event_type_stats_t *type_stats;
    size_t type_stats_count;
    size_t type_stats_capacity;

    // Queue depth tracking
    uint64_t max_queue_depth_seen;
    uint64_t max_priority_queue_depth_seen;

    // Processing limits tracking
    uint64_t cycles_hit_time_limit;
    uint64_t cycles_hit_event_limit;

    // Thread safety
    pthread_mutex_t stats_mutex;
} lle_event_enhanced_stats_t;

/*
 * Processing State (Phase 2B)
 */
typedef enum {
    LLE_PROCESSING_STOPPED, /**< Processing stopped */
    LLE_PROCESSING_RUNNING, /**< Processing running */
    LLE_PROCESSING_PAUSED   // Processing paused
} lle_processing_state_t;

/*
 * Processing Configuration (Phase 2B)
 */
typedef struct {
    uint32_t max_events_per_cycle; /**< Maximum events per processing cycle */
    uint64_t cycle_time_limit_us;  /**< Time limit per cycle (microseconds) */
    bool auto_process;          /**< Automatically process when events queued */
    bool record_detailed_stats; /**< Enable detailed statistics */
} lle_event_processing_config_t;

/*
 * Filter Result (Phase 2C)
 */
typedef enum {
    LLE_FILTER_PASS,      /**< Pass event through */
    LLE_FILTER_BLOCK,     /**< Block event (don't dispatch) */
    LLE_FILTER_TRANSFORM, /**< Event was transformed */
    LLE_FILTER_ERROR      // Error in filter
} lle_filter_result_t;

/*
 * Event Filter Callback (Phase 2C)
 */
typedef lle_filter_result_t (*lle_event_filter_fn)(lle_event_t *event,
                                                   void *user_data);

/*
 * Event Filter (Phase 2C)
 */
typedef struct {
    lle_event_filter_fn filter; /**< Filter function */
    void *user_data;            /**< User data for filter */
    char name[64];              /**< Filter name (for debugging) */
    bool enabled;               /**< Filter enabled/disabled */

    // Filter statistics
    uint64_t events_filtered;    /**< Total events checked */
    uint64_t events_passed;      /**< Events passed */
    uint64_t events_blocked;     /**< Events blocked */
    uint64_t events_transformed; /**< Events transformed */
    uint64_t events_errored;     /**< Filter errors */
} lle_event_filter_t;

/*
 * Event Filter System (Phase 2C)
 */
typedef struct {
    lle_event_filter_t **filters; /**< Array of filters */
    size_t filter_count;          /**< Number of filters */
    size_t filter_capacity;       /**< Array capacity */
    pthread_mutex_t filter_mutex; /**< Thread safety */

    // Global filter statistics
    uint64_t total_events_filtered;
    uint64_t total_events_blocked;
} lle_event_filter_system_t;

/*
 * Pre-Dispatch Hook (Phase 2C)
 * Return LLE_SUCCESS to continue, error to skip dispatch
 */
typedef lle_result_t (*lle_event_pre_dispatch_fn)(lle_event_t *event,
                                                  void *user_data);

/*
 * Post-Dispatch Hook (Phase 2C)
 */
typedef void (*lle_event_post_dispatch_fn)(lle_event_t *event,
                                           lle_result_t dispatch_result,
                                           void *user_data);

/*
 * System State (Phase 2C)
 */
typedef enum {
    LLE_STATE_INITIALIZING,  /**< System initializing */
    LLE_STATE_IDLE,          /**< No events processing */
    LLE_STATE_PROCESSING,    /**< Processing events */
    LLE_STATE_PAUSED,        /**< Processing paused */
    LLE_STATE_SHUTTING_DOWN, /**< System shutting down */
    LLE_STATE_ERROR          // Error state
} lle_system_state_t;

/*
 * Timer Event (Phase 2D)
 */
typedef struct lle_timer_event {
    uint64_t timer_id;        /**< Unique timer identifier */
    lle_event_t *event;       /**< Event to dispatch when timer fires */
    uint64_t trigger_time_us; /**< When to fire (absolute timestamp) */
    uint64_t interval_us;     /**< Repeat interval (0 = one-shot) */
    bool repeating;           /**< Is this a repeating timer? */
    bool enabled;             /**< Is timer currently enabled? */
    uint64_t fire_count;      /**< How many times has it fired? */
} lle_timer_event_t;

/*
 * Timer System (Phase 2D)
 */
typedef struct {
    lle_timer_event_t **timers;  /**< Array of timer pointers */
    size_t timer_count;          /**< Current number of timers */
    size_t timer_capacity;       /**< Capacity of array */
    uint64_t next_timer_id;      /**< Next ID to assign */
    pthread_mutex_t timer_mutex; /**< Thread safety */

    // Timer statistics
    uint64_t total_timers_created;
    uint64_t total_timers_fired;
    uint64_t total_timers_cancelled;
} lle_timer_system_t;

/*
 * Event System Structure (Phase 1 + Phase 2A + Phase 2B + Phase 2C + Phase 2D)
 */
struct lle_event_system {
    // Phase 1: Single queue
    lle_event_queue_t *queue; /**< Main event queue (FIFO for Phase 1) */

    // Phase 2A: Priority queue support
    lle_event_queue_t *priority_queue; /**< Priority queue (CRITICAL events) */
    bool use_priority_queue;           /**< Enable priority queue processing */

    // Handler management
    lle_event_handler_t **handlers; /**< Handler array */
    size_t handler_count;           /**< Number of handlers */
    size_t handler_capacity;        /**< Handler array capacity */

    // Memory management
    lle_memory_pool_t *event_pool; /**< Memory pool for events */
    pthread_mutex_t system_mutex;  /**< System-wide mutex */

    // Event tracking
    uint64_t sequence_counter; /**< Event sequence counter */
    bool active;               /**< System active flag */

    // Phase 1 Statistics
    uint64_t events_created;    /**< Total events created */
    uint64_t events_dispatched; /**< Total events dispatched */
    uint64_t events_dropped;    /**< Events dropped (queue full) */

    // Phase 2A Statistics
    uint64_t priority_events_queued;    /**< Priority events queued */
    uint64_t priority_events_processed; /**< Priority events processed */
    uint64_t events_by_priority[LLE_PRIORITY_COUNT]; /**< Events per priority
                                                        level */

    // Phase 2B: Enhanced statistics and processing control
    lle_event_enhanced_stats_t
        *enhanced_stats; /**< Enhanced statistics (optional) */
    lle_event_processing_config_t
        processing_config;                   /**< Processing configuration */
    lle_processing_state_t processing_state; /**< Current processing state */

    // Phase 2C: Event filtering and hooks
    lle_event_filter_system_t
        *filter_system; /**< Event filter system (optional) */
    lle_event_pre_dispatch_fn pre_dispatch_hook; /**< Pre-dispatch callback */
    void *pre_dispatch_data;                     /**< Pre-dispatch user data */
    lle_event_post_dispatch_fn
        post_dispatch_hook;   /**< Post-dispatch callback */
    void *post_dispatch_data; /**< Post-dispatch user data */

    // Phase 2C: System state tracking
    lle_system_state_t current_state;  /**< Current system state */
    lle_system_state_t previous_state; /**< Previous state (for recovery) */
    uint64_t state_changed_time;       /**< When state last changed */

    // Phase 2D: Timer events
    lle_timer_system_t *timer_system; /**< Timer system (optional) */
};

/* ============================================================================
 * Event System Lifecycle Functions
 * ============================================================================
 */

/*
 * Initialize event system
 */
/**
 * @brief Initialize event system
 * @param system Output: newly created event system
 * @param pool Memory pool used to allocate events
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_system_init(lle_event_system_t **system,
                                   lle_memory_pool_t *pool);

/*
 * Destroy event system
 */
/**
 * @brief Destroy event system and release all associated resources
 * @param system Event system to destroy
 */
void lle_event_system_destroy(lle_event_system_t *system);

/*
 * Start event system (begin processing)
 */
/**
 * @brief Start event system (begin processing)
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_system_start(lle_event_system_t *system);

/*
 * Stop event system (stop processing)
 */
/**
 * @brief Stop event system (stop processing)
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_system_stop(lle_event_system_t *system);

/* ============================================================================
 * Event Creation and Destruction
 * ============================================================================
 */

/*
 * Create event
 */
/**
 * @brief Create event
 * @param system Event system
 * @param type Event kind/type
 * @param data Generic event-specific data (may be NULL)
 * @param data_size Size of data in bytes
 * @param event Output: newly created event
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_create(lle_event_system_t *system, lle_event_kind_t type,
                              void *data, size_t data_size,
                              lle_event_t **event);

/*
 * Destroy event
 */
/**
 * @brief Destroy event and return its memory to the pool
 * @param system Event system
 * @param event Event to destroy
 */
void lle_event_destroy(lle_event_system_t *system, lle_event_t *event);

/*
 * Clone event
 */
/**
 * @brief Clone event
 * @param system Event system
 * @param source Source event to clone
 * @param dest Output: newly created clone
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_clone(lle_event_system_t *system, lle_event_t *source,
                             lle_event_t **dest);

/* ============================================================================
 * Event Queue Operations
 * ============================================================================
 */

/*
 * Initialize event queue
 */
/**
 * @brief Initialize event queue
 * @param queue Output: newly created queue
 * @param capacity Maximum number of events the queue can hold
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_queue_init(lle_event_queue_t **queue, size_t capacity);

/*
 * Destroy event queue
 */
/**
 * @brief Destroy event queue and release associated resources
 * @param queue Queue to destroy
 */
void lle_event_queue_destroy(lle_event_queue_t *queue);

/*
 * Enqueue event
 */
/**
 * @brief Enqueue event for later processing
 * @param system Event system
 * @param event Event to enqueue
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enqueue(lle_event_system_t *system, lle_event_t *event);

/*
 * Dequeue event
 */
/**
 * @brief Dequeue next event from the queue
 * @param system Event system
 * @param event Output: dequeued event
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_dequeue(lle_event_system_t *system, lle_event_t **event);

/*
 * Get queue size
 */
/**
 * @brief Get current queue size (number of queued events)
 * @param system Event system
 * @return Number of events currently in the queue
 */
size_t lle_event_queue_size(lle_event_system_t *system);

/*
 * Check if queue is empty
 */
/**
 * @brief Check if queue is empty
 * @param system Event system
 * @return true if the queue contains no events
 */
bool lle_event_queue_empty(lle_event_system_t *system);

/*
 * Check if queue is full
 */
/**
 * @brief Check if queue is full
 * @param system Event system
 * @return true if the queue is at capacity
 */
bool lle_event_queue_full(lle_event_system_t *system);

/* ============================================================================
 * Handler Management
 * ============================================================================
 */

/*
 * Register event handler
 */
/**
 * @brief Register event handler for a specific event type
 * @param system Event system
 * @param type Event kind to register the handler for
 * @param handler Handler function
 * @param user_data User data passed to the handler on each dispatch
 * @param name Handler name (for debugging)
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_handler_register(lle_event_system_t *system,
                                        lle_event_kind_t type,
                                        lle_event_handler_fn handler,
                                        void *user_data, const char *name);

/*
 * Unregister specific handler
 */
/**
 * @brief Unregister a specific handler by name
 * @param system Event system
 * @param type Event kind the handler was registered for
 * @param name Handler name to unregister
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_handler_unregister(lle_event_system_t *system,
                                          lle_event_kind_t type,
                                          const char *name);

/*
 * Unregister all handlers for event type
 */
/**
 * @brief Unregister all handlers for a given event type
 * @param system Event system
 * @param type Event kind to clear handlers for
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_handler_unregister_all(lle_event_system_t *system,
                                              lle_event_kind_t type);

/*
 * Get handler count for event type
 */
/**
 * @brief Get handler count for a given event type
 * @param system Event system
 * @param type Event kind to query
 * @return Number of handlers registered for this event type
 */
size_t lle_event_handler_count(lle_event_system_t *system,
                               lle_event_kind_t type);

/* ============================================================================
 * Event Processing
 * ============================================================================
 */

/*
 * Dispatch event to all registered handlers
 */
/**
 * @brief Dispatch event to all registered handlers
 * @param system Event system
 * @param event Event to dispatch
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_dispatch(lle_event_system_t *system, lle_event_t *event);

/*
 * Process events from queue (up to max_events)
 */
/**
 * @brief Process events from queue up to a maximum count
 * @param system Event system
 * @param max_events Maximum number of events to process in this call
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_process_queue(lle_event_system_t *system,
                                     uint32_t max_events);

/*
 * Process all events in queue
 */
/**
 * @brief Process all events currently in the queue
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_process_all(lle_event_system_t *system);

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/*
 * Get current timestamp in microseconds
 */
/**
 * @brief Get current timestamp in microseconds
 * @return Monotonic timestamp in microseconds
 */
uint64_t lle_event_get_timestamp_us(void);

/*
 * Get event type name (for debugging)
 */
/**
 * @brief Get event type name for debugging
 * @param type Event kind
 * @return Static string naming the event type
 */
const char *lle_event_type_name(lle_event_kind_t type);

/*
 * Get event system statistics
 */
/**
 * @brief Get event system statistics
 * @param system Event system
 * @param created Output: total events created
 * @param dispatched Output: total events dispatched
 * @param dropped Output: total events dropped (queue full)
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_system_get_stats(lle_event_system_t *system,
                                        uint64_t *created, uint64_t *dispatched,
                                        uint64_t *dropped);

/* ============================================================================
 * Phase 2B: Enhanced Statistics API
 * ============================================================================
 */

/*
 * Initialize enhanced statistics (optional - called automatically if
 * config.record_detailed_stats is true)
 */
/**
 * @brief Initialize enhanced statistics
 *
 * Optional: called automatically if config.record_detailed_stats is true.
 *
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enhanced_stats_init(lle_event_system_t *system);

/*
 * Destroy enhanced statistics
 */
/**
 * @brief Destroy enhanced statistics and release resources
 * @param system Event system
 */
void lle_event_enhanced_stats_destroy(lle_event_system_t *system);

/*
 * Get per-type statistics for a specific event type
 */
/**
 * @brief Get per-type statistics for a specific event type
 * @param system Event system
 * @param type Event kind to query
 * @param stats Output: statistics for this event type
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enhanced_stats_get_type(lle_event_system_t *system,
                                               lle_event_kind_t type,
                                               lle_event_type_stats_t *stats);

/*
 * Get all type statistics (returns array)
 */
/**
 * @brief Get all per-type statistics as an array
 * @param system Event system
 * @param stats Output: array of per-type statistics
 * @param count Output: number of entries in the array
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enhanced_stats_get_all_types(
    lle_event_system_t *system, lle_event_type_stats_t **stats, size_t *count);

/*
 * Get cycle statistics
 */
/**
 * @brief Get processing cycle statistics
 * @param system Event system
 * @param cycles Output: total completed cycles
 * @param total_time Output: total cycle processing time
 * @param min_time Output: minimum cycle processing time
 * @param max_time Output: maximum cycle processing time
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enhanced_stats_get_cycles(lle_event_system_t *system,
                                                 uint64_t *cycles,
                                                 uint64_t *total_time,
                                                 uint64_t *min_time,
                                                 uint64_t *max_time);

/*
 * Get queue depth statistics
 */
/**
 * @brief Get peak queue depth statistics
 * @param system Event system
 * @param max_main_depth Output: peak depth of the main queue
 * @param max_priority_depth Output: peak depth of the priority queue
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_event_enhanced_stats_get_queue_depth(lle_event_system_t *system,
                                         uint64_t *max_main_depth,
                                         uint64_t *max_priority_depth);

/*
 * Reset all enhanced statistics
 */
/**
 * @brief Reset all enhanced statistics to zero
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_enhanced_stats_reset(lle_event_system_t *system);

/* ============================================================================
 * Phase 2B: Processing Configuration API
 * ============================================================================
 */

/*
 * Set processing configuration
 */
/**
 * @brief Set processing configuration
 * @param system Event system
 * @param config Configuration to apply
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_event_processing_set_config(lle_event_system_t *system,
                                const lle_event_processing_config_t *config);

/*
 * Get processing configuration
 */
/**
 * @brief Get processing configuration
 * @param system Event system
 * @param config Output: current configuration
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_event_processing_get_config(lle_event_system_t *system,
                                lle_event_processing_config_t *config);

/*
 * Set processing state
 */
/**
 * @brief Set processing state (running, stopped, paused)
 * @param system Event system
 * @param state New processing state
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_processing_set_state(lle_event_system_t *system,
                                            lle_processing_state_t state);

/*
 * Get processing state
 */
/**
 * @brief Get current processing state
 * @param system Event system
 * @return Current processing state
 */
lle_processing_state_t
lle_event_processing_get_state(lle_event_system_t *system);

/* ============================================================================
 * Phase 2C: Event Filter API
 * ============================================================================
 */

/*
 * Initialize event filter system (optional - created on demand)
 */
/**
 * @brief Initialize event filter system
 *
 * Optional: filter system is created on demand when a filter is added.
 *
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_system_init(lle_event_system_t *system);

/*
 * Destroy event filter system
 */
/**
 * @brief Destroy event filter system and release resources
 * @param system Event system
 */
void lle_event_filter_system_destroy(lle_event_system_t *system);

/*
 * Add event filter
 */
/**
 * @brief Add an event filter
 * @param system Event system
 * @param name Filter name (for later lookup and debugging)
 * @param filter Filter callback function
 * @param user_data User data passed to the filter on each invocation
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_add(lle_event_system_t *system, const char *name,
                                  lle_event_filter_fn filter, void *user_data);

/*
 * Remove event filter by name
 */
/**
 * @brief Remove an event filter by name
 * @param system Event system
 * @param name Filter name to remove
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_remove(lle_event_system_t *system,
                                     const char *name);

/*
 * Enable event filter by name
 */
/**
 * @brief Enable a previously-added event filter by name
 * @param system Event system
 * @param name Filter name to enable
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_enable(lle_event_system_t *system,
                                     const char *name);

/*
 * Disable event filter by name
 */
/**
 * @brief Disable an event filter by name (without removing it)
 * @param system Event system
 * @param name Filter name to disable
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_disable(lle_event_system_t *system,
                                      const char *name);

/*
 * Get filter statistics
 */
/**
 * @brief Get per-filter statistics
 * @param system Event system
 * @param name Filter name to query
 * @param filtered Output: total events checked
 * @param passed Output: events passed
 * @param blocked Output: events blocked
 * @param transformed Output: events transformed
 * @param errored Output: filter errors
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_filter_get_stats(lle_event_system_t *system,
                                        const char *name, uint64_t *filtered,
                                        uint64_t *passed, uint64_t *blocked,
                                        uint64_t *transformed,
                                        uint64_t *errored);

/* ============================================================================
 * Phase 2C: Dispatch Hooks API
 * ============================================================================
 */

/*
 * Set pre-dispatch hook
 */
/**
 * @brief Set pre-dispatch hook
 * @param system Event system
 * @param hook Pre-dispatch callback
 * @param user_data User data passed to the hook
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_set_pre_dispatch_hook(lle_event_system_t *system,
                                             lle_event_pre_dispatch_fn hook,
                                             void *user_data);

/*
 * Set post-dispatch hook
 */
/**
 * @brief Set post-dispatch hook
 * @param system Event system
 * @param hook Post-dispatch callback
 * @param user_data User data passed to the hook
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_set_post_dispatch_hook(lle_event_system_t *system,
                                              lle_event_post_dispatch_fn hook,
                                              void *user_data);

/* ============================================================================
 * Phase 2C: System State API
 * ============================================================================
 */

/*
 * Set system state
 */
/**
 * @brief Set system state
 * @param system Event system
 * @param state New system state
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_system_set_state(lle_event_system_t *system,
                                        lle_system_state_t state);

/*
 * Get current system state
 */
/**
 * @brief Get current system state
 * @param system Event system
 * @return Current system state
 */
lle_system_state_t lle_event_system_get_state(lle_event_system_t *system);

/*
 * Get previous system state
 */
/**
 * @brief Get previous system state (for recovery)
 * @param system Event system
 * @return Previous system state
 */
lle_system_state_t
lle_event_system_get_previous_state(lle_event_system_t *system);

/* ============================================================================
 * Phase 2D: Timer Events API
 * ============================================================================
 */

/*
 * Initialize timer system (called internally - can also be called explicitly)
 */
/**
 * @brief Initialize timer system
 *
 * Called internally during event-system init; may also be called explicitly.
 *
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_system_init(lle_event_system_t *system);

/*
 * Destroy timer system (called internally during system destroy)
 */
/**
 * @brief Destroy timer system and release resources
 *
 * Called internally during event-system destroy.
 *
 * @param system Event system
 */
void lle_event_timer_system_destroy(lle_event_system_t *system);

/*
 * Create a one-shot timer (fires once)
 *
 * @param system       Event system
 * @param event        Event to dispatch when timer fires (will be cloned)
 * @param delay_us     Delay before firing (microseconds)
 * @param timer_id_out Output: timer ID for later reference
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Create a one-shot timer (fires once)
 * @param system Event system
 * @param event Event to dispatch when timer fires (will be cloned)
 * @param delay_us Delay before firing (microseconds)
 * @param timer_id_out Output: timer ID for later reference
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_add_oneshot(lle_event_system_t *system,
                                         lle_event_t *event, uint64_t delay_us,
                                         uint64_t *timer_id_out);

/*
 * Create a repeating timer
 *
 * @param system           Event system
 * @param event            Event to dispatch when timer fires (will be cloned)
 * @param initial_delay_us Initial delay before first fire (microseconds)
 * @param interval_us      Repeat interval (microseconds)
 * @param timer_id_out     Output: timer ID for later reference
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Create a repeating timer
 * @param system Event system
 * @param event Event to dispatch when timer fires (will be cloned)
 * @param initial_delay_us Initial delay before first fire (microseconds)
 * @param interval_us Repeat interval (microseconds)
 * @param timer_id_out Output: timer ID for later reference
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_add_repeating(lle_event_system_t *system,
                                           lle_event_t *event,
                                           uint64_t initial_delay_us,
                                           uint64_t interval_us,
                                           uint64_t *timer_id_out);

/*
 * Cancel a timer (removes and destroys it)
 *
 * @param system   Event system
 * @param timer_id Timer ID to cancel
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Cancel a timer (removes and destroys it)
 * @param system Event system
 * @param timer_id Timer ID to cancel
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_cancel(lle_event_system_t *system,
                                    uint64_t timer_id);

/*
 * Enable a timer (without destroying it)
 *
 * @param system   Event system
 * @param timer_id Timer ID to enable
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Enable a timer (without destroying it)
 * @param system Event system
 * @param timer_id Timer ID to enable
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_enable(lle_event_system_t *system,
                                    uint64_t timer_id);

/*
 * Disable a timer (without destroying it)
 *
 * @param system   Event system
 * @param timer_id Timer ID to disable
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Disable a timer (without destroying it)
 * @param system Event system
 * @param timer_id Timer ID to disable
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_disable(lle_event_system_t *system,
                                     uint64_t timer_id);

/*
 * Get timer information
 *
 * @param system           Event system
 * @param timer_id         Timer ID to query
 * @param next_fire_time_us Output: when timer will next fire (absolute
 * timestamp)
 * @param fire_count       Output: how many times timer has fired
 * @param is_repeating     Output: is this a repeating timer?
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Get timer information
 * @param system Event system
 * @param timer_id Timer ID to query
 * @param next_fire_time_us Output: when timer will next fire (absolute
 * timestamp)
 * @param fire_count Output: how many times timer has fired
 * @param is_repeating Output: is this a repeating timer?
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_get_info(lle_event_system_t *system,
                                      uint64_t timer_id,
                                      uint64_t *next_fire_time_us,
                                      uint64_t *fire_count, bool *is_repeating);

/*
 * Process all timers that are ready to fire
 * Call this periodically (e.g., in main event loop)
 *
 * @param system Event system
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Process all timers that are ready to fire
 *
 * Call this periodically (e.g., in the main event loop).
 *
 * @param system Event system
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_process(lle_event_system_t *system);

/*
 * Get timer system statistics
 *
 * @param system   Event system
 * @param created  Output: total timers created
 * @param fired    Output: total times timers have fired
 * @param cancelled Output: total timers cancelled
 * @return LLE_SUCCESS or error code
 */
/**
 * @brief Get timer system statistics
 * @param system Event system
 * @param created Output: total timers created
 * @param fired Output: total times timers have fired
 * @param cancelled Output: total timers cancelled
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_event_timer_get_stats(lle_event_system_t *system,
                                       uint64_t *created, uint64_t *fired,
                                       uint64_t *cancelled);

/* ============================================================================
 * Shell Lifecycle Events API
 * ============================================================================
 */

/**
 * Fire a directory changed event
 *
 * Called when the shell's working directory changes (e.g., via cd builtin).
 * This triggers cache invalidation for directory-dependent data like git
 * status.
 *
 * @param system      Event system
 * @param old_dir     Previous working directory (may be NULL)
 * @param new_dir     New working directory (must not be NULL)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_event_fire_directory_changed(lle_event_system_t *system,
                                              const char *old_dir,
                                              const char *new_dir);

/**
 * Fire a pre-command event
 *
 * Called just before a command is executed. Allows prompt system to record
 * state for transient prompt and command timing.
 *
 * @param system      Event system
 * @param command     Command about to be executed
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_event_fire_pre_command(lle_event_system_t *system,
                                        const char *command);

/**
 * Fire a post-command event
 *
 * Called after a command completes execution. Provides exit code and duration
 * for prompt status display and history annotation.
 *
 * @param system      Event system
 * @param command     Command that was executed
 * @param exit_code   Command exit code
 * @param duration_us Command execution duration in microseconds
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_event_fire_post_command(lle_event_system_t *system,
                                         const char *command, int exit_code,
                                         uint64_t duration_us);

/**
 * Fire a prompt display event
 *
 * Called just before the prompt is displayed. Allows prompt system to
 * regenerate prompt content if needed.
 *
 * @param system      Event system
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_event_fire_prompt_display(lle_event_system_t *system);

#endif // LLE_EVENT_SYSTEM_H
