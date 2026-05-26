/**
 * @file layer_events.h
 * @brief Layer events system - Inter-layer communication protocol
 *
 * Provides event-driven communication between display layers using a
 * publisher/subscriber pattern for loose coupling. Manages event queues
 * and processing for the layered display architecture.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Publisher/Subscriber pattern for loose coupling
 * - Event queue management and processing
 * - Performance-optimized event handling
 * - Memory-safe event data management
 * - Event debugging and monitoring
 * - Priority-based event processing
 *
 * Design Principles:
 * - No direct layer dependencies
 * - Asynchronous event processing
 * - Type-safe event handling
 * - Performance-optimized delivery
 * - Comprehensive event debugging
 */

#ifndef LAYER_EVENTS_H
#define LAYER_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ============================================================================
/// CONSTANTS AND CONFIGURATION
/// ============================================================================

#define LAYER_EVENTS_VERSION_MAJOR 1
#define LAYER_EVENTS_VERSION_MINOR 0
#define LAYER_EVENTS_VERSION_PATCH 0

#define LAYER_EVENTS_MAX_SUBSCRIBERS 32
#define LAYER_EVENTS_MAX_EVENT_QUEUE_SIZE 256
#define LAYER_EVENTS_MAX_EVENT_DATA_SIZE 1024
#define LAYER_EVENTS_MAX_EVENT_TYPE_NAME 32

/// Event processing timeouts
#define LAYER_EVENTS_DEFAULT_TIMEOUT_MS 50
#define LAYER_EVENTS_CRITICAL_TIMEOUT_MS 10

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Error codes for layer events operations
 */
typedef enum {
    LAYER_EVENTS_SUCCESS = 0,             /// Operation completed successfully
    LAYER_EVENTS_ERROR_INVALID_PARAM,     /// Invalid parameter provided
    LAYER_EVENTS_ERROR_MEMORY_ALLOCATION, /// Memory allocation failed
    LAYER_EVENTS_ERROR_QUEUE_FULL,        /// Event queue is full
    LAYER_EVENTS_ERROR_QUEUE_EMPTY,       /// Event queue is empty
    LAYER_EVENTS_ERROR_SUBSCRIBER_NOT_FOUND, /// Subscriber not found
    LAYER_EVENTS_ERROR_MAX_SUBSCRIBERS,      /// Maximum subscribers reached
    LAYER_EVENTS_ERROR_EVENT_TOO_LARGE,      /// Event data too large
    LAYER_EVENTS_ERROR_TIMEOUT,              /// Event processing timeout
    LAYER_EVENTS_ERROR_NOT_INITIALIZED       /// Events system not initialized
} layer_events_error_t;

/**
 * Event types for layer communication
 */
typedef enum {
    LAYER_EVENT_NONE = 0, /// No event / invalid

    /// Content change events
    LAYER_EVENT_CONTENT_CHANGED, /// Layer content has changed
    LAYER_EVENT_TEXT_UPDATED,    /// Text content updated
    LAYER_EVENT_STYLE_UPDATED,   /// Text styling updated

    /// Layout and positioning events
    LAYER_EVENT_SIZE_CHANGED,     /// Layer or terminal size changed
    LAYER_EVENT_POSITION_CHANGED, /// Layer position changed
    LAYER_EVENT_CURSOR_MOVED,     /// Cursor position changed

    /// Display and rendering events
    LAYER_EVENT_REDRAW_NEEDED,      /// Layer needs redraw
    LAYER_EVENT_REFRESH_REQUESTED,  /// Full refresh requested
    LAYER_EVENT_VISIBILITY_CHANGED, /// Layer visibility changed

    /// Theme and appearance events
    LAYER_EVENT_THEME_CHANGED,        /// Color theme changed
    LAYER_EVENT_COLOR_SCHEME_UPDATED, /// Color scheme updated
    LAYER_EVENT_FONT_CHANGED,         /// Font or font size changed

    /// User interaction events
    LAYER_EVENT_INPUT_RECEIVED, /// User input received
    LAYER_EVENT_FOCUS_GAINED,   /// Layer gained focus
    LAYER_EVENT_FOCUS_LOST,     /// Layer lost focus

    /// System events
    LAYER_EVENT_TERMINAL_RESIZE,         /// Terminal was resized
    LAYER_EVENT_INITIALIZATION_COMPLETE, /// Layer initialization done
    LAYER_EVENT_CLEANUP_REQUESTED,       /// Layer cleanup requested
    LAYER_EVENT_ERROR_OCCURRED,          /// Error occurred in layer

    /// Performance and debugging events
    LAYER_EVENT_PERFORMANCE_WARNING, /// Performance issue detected
    LAYER_EVENT_DEBUG_INFO,          /// Debug information available

    /// Custom events (for future extensibility)
    LAYER_EVENT_CUSTOM_START = 1000 /// Start of custom event range
} layer_event_type_t;

/**
 * Event priority levels
 */
typedef enum {
    LAYER_EVENT_PRIORITY_LOW = 0,     /// Low priority, can be delayed
    LAYER_EVENT_PRIORITY_NORMAL = 1,  /// Normal priority
    LAYER_EVENT_PRIORITY_HIGH = 2,    /// High priority, process quickly
    LAYER_EVENT_PRIORITY_CRITICAL = 3 /// Critical, process immediately
} layer_event_priority_t;

/**
 * Event processing flags
 */
typedef enum {
    LAYER_EVENT_FLAG_NONE = 0,              /// No special flags
    LAYER_EVENT_FLAG_ASYNC = (1 << 0),      /// Process asynchronously
    LAYER_EVENT_FLAG_BROADCAST = (1 << 1),  /// Broadcast to all subscribers
    LAYER_EVENT_FLAG_ONCE = (1 << 2),       /// Process only once
    LAYER_EVENT_FLAG_PERSISTENT = (1 << 3), /// Keep event data after processing
    LAYER_EVENT_FLAG_NO_QUEUE = (1 << 4)    /// Process immediately, don't queue
} layer_event_flags_t;

/**
 * Layer identifier for event source/target
 */
typedef enum {
    LAYER_ID_UNKNOWN = 0,
    LAYER_ID_BASE_TERMINAL = 1,
    LAYER_ID_TERMINAL_CONTROL = 2,
    LAYER_ID_PROMPT_LAYER = 3,
    LAYER_ID_COMMAND_LAYER = 4,
    LAYER_ID_COMPOSITION_ENGINE = 5,
    LAYER_ID_DISPLAY_CONTROLLER = 6,
    LAYER_ID_AUTOSUGGESTIONS = 7, /// Fish-like autosuggestions layer
    LAYER_ID_EXTERNAL = 100       /// External components
} layer_id_t;

/**
 * Event data structure for different event types
 */
typedef union {
    /// Content change data
    struct {
        const char *new_content;
        size_t content_length;
        bool needs_reflow;
    } content_changed;

    /// Size change data
    struct {
        int old_width, old_height;
        int new_width, new_height;
    } size_changed;

    /// Position change data
    struct {
        int old_row, old_column;
        int new_row, new_column;
    } position_changed;

    /// Theme change data
    struct {
        const char *theme_name;
        uint32_t color_count;
        void *color_palette;
    } theme_changed;

    /// Error data
    struct {
        int error_code;
        const char *error_message;
        const char *context;
    } error_occurred;

    /// Performance warning data
    struct {
        const char *warning_type;
        uint64_t metric_value;
        uint64_t threshold_value;
    } performance_warning;

    /// Generic data for custom events
    struct {
        void *data;
        size_t data_size;
        void (*cleanup_func)(void *data);
    } custom_data;
} layer_event_data_t;

/**
 * Layer event structure
 */
typedef struct {
    layer_event_type_t type;         /// Event type
    layer_id_t source_layer;         /// Layer that generated the event
    layer_id_t target_layer;         /// Target layer (0 for broadcast)
    layer_event_priority_t priority; /// Event priority
    layer_event_flags_t flags;       /// Processing flags

    uint64_t timestamp;       /// Event creation timestamp
    uint32_t sequence_number; /// Global sequence number
    uint32_t event_id;        /// Unique event identifier

    layer_event_data_t data; /// Event-specific data

    /// Internal processing data
    uint32_t processing_attempts;  /// Number of processing attempts
    uint64_t last_processing_time; /// Last processing attempt time
    bool processed;                /// Whether event was processed
} layer_event_t;

/**
 * Event callback function type
 */
typedef layer_events_error_t (*layer_event_callback_t)(
    const layer_event_t *event, void *user_data);

/**
 * Event subscriber information
 */
typedef struct {
    layer_event_type_t event_type;       /// Type of events to receive
    layer_id_t subscriber_id;            /// ID of subscribing layer
    layer_event_callback_t callback;     /// Callback function
    void *user_data;                     /// User data for callback
    layer_event_priority_t min_priority; /// Minimum priority to process
    bool active;                         /// Whether subscription is active
    uint64_t events_received;            /// Number of events received
    uint64_t events_processed;           /// Number of events processed
    uint64_t last_event_time;            /// Timestamp of last event
} layer_event_subscriber_t;

/**
 * Event statistics structure
 */
typedef struct {
    uint64_t events_published;         /// Total events published
    uint64_t events_processed;         /// Total events processed
    uint64_t events_dropped;           /// Events dropped due to queue full
    uint64_t events_failed;            /// Events that failed processing
    uint64_t queue_overflows;          /// Number of queue overflow events
    uint64_t total_processing_time_ns; /// Total time spent processing
    uint32_t current_queue_size;       /// Current number of queued events
    uint32_t max_queue_size_reached;   /// Maximum queue size reached
    uint32_t active_subscribers;       /// Number of active subscribers
} layer_event_stats_t;

/**
 * Event system configuration
 */
typedef struct {
    uint32_t max_queue_size;            /// Maximum events in queue
    uint32_t max_subscribers;           /// Maximum number of subscribers
    uint32_t processing_timeout_ms;     /// Processing timeout in milliseconds
    bool enable_debugging;              /// Enable debug logging
    bool enable_performance_monitoring; /// Enable performance monitoring
    bool auto_cleanup_processed_events; /// Auto-cleanup processed events
} layer_events_config_t;

/**
 * Event system instance
 */
typedef struct layer_event_system layer_event_system_t;

/// ============================================================================
/// LIFECYCLE FUNCTIONS
/// ============================================================================

/**
 * Create a new layer events system
 *
 * Creates and initializes a new event system instance with the specified
 * configuration. The event system manages event queues, subscribers, and
 * processing.
 *
 * @param config Configuration for the event system, or NULL for defaults
 * @return Pointer to new layer_event_system_t instance, or NULL on failure
 *
 * @note The returned pointer must be freed with layer_events_destroy()
 */
layer_event_system_t *layer_events_create(const layer_events_config_t *config);

/**
 * Initialize the layer events system
 *
 * Performs complete initialization of the event system including queue
 * setup, subscriber management initialization, and performance monitoring.
 *
 * @param events Pointer to layer_event_system_t instance
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 *
 * @note Must be called before any other event system operations
 */
layer_events_error_t layer_events_init(layer_event_system_t *events);

/**
 * Clean up event system resources
 *
 * Cleans up all resources used by the event system including pending
 * events, subscriber lists, and allocated memory.
 *
 * @param events Pointer to layer_event_system_t instance
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 *
 * @note Safe to call multiple times
 * @note Does not free the events structure itself
 */
layer_events_error_t layer_events_cleanup(layer_event_system_t *events);

/**
 * Destroy event system instance
 *
 * Performs cleanup and frees the event system structure. After calling
 * this function, the events pointer is invalid and should not be used.
 *
 * @param events Pointer to layer_event_system_t instance
 *
 * @note Automatically calls layer_events_cleanup() if needed
 * @note Safe to call with NULL pointer
 */
void layer_events_destroy(layer_event_system_t *events);

/// ============================================================================
/// EVENT PUBLISHING FUNCTIONS
/// ============================================================================

/**
 * Publish an event to the system
 *
 * Publishes an event to all interested subscribers. The event is queued
 * for processing unless NO_QUEUE flag is set.
 *
 * @param events Pointer to event system instance
 * @param event Event to publish
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 *
 * @note Event data is copied, original can be freed after call
 * @note Processing may be asynchronous depending on event flags
 */
layer_events_error_t layer_events_publish(layer_event_system_t *events,
                                          const layer_event_t *event);

/**
 * Publish a simple event with minimal data
 *
 * Convenience function for publishing common events without setting up
 * a full event structure.
 *
 * @param events Pointer to event system instance
 * @param type Event type
 * @param source_layer Source layer ID
 * @param target_layer Target layer ID (0 for broadcast)
 * @param priority Event priority
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
layer_events_publish_simple(layer_event_system_t *events,
                            layer_event_type_t type, layer_id_t source_layer,
                            layer_id_t target_layer,
                            layer_event_priority_t priority);

/**
 * Publish a content changed event
 *
 * Convenience function for publishing content change events.
 *
 * @param events Pointer to event system instance
 * @param source_layer Source layer ID
 * @param content New content string
 * @param content_length Length of content
 * @param needs_reflow Whether content change requires reflow
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t layer_events_publish_content_changed(
    layer_event_system_t *events, layer_id_t source_layer, const char *content,
    size_t content_length, bool needs_reflow);

/**
 * Publish a size changed event
 *
 * Convenience function for publishing size change events.
 *
 * @param events Pointer to event system instance
 * @param source_layer Source layer ID
 * @param old_width Previous width
 * @param old_height Previous height
 * @param new_width New width
 * @param new_height New height
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t layer_events_publish_size_changed(
    layer_event_system_t *events, layer_id_t source_layer, int old_width,
    int old_height, int new_width, int new_height);

/// ============================================================================
/// EVENT SUBSCRIPTION FUNCTIONS
/// ============================================================================

/**
 * Subscribe to events of a specific type
 *
 * Registers a callback function to receive events of the specified type.
 * The callback will be called for each matching event.
 *
 * @param events Pointer to event system instance
 * @param event_type Type of events to subscribe to
 * @param subscriber_id ID of the subscribing layer
 * @param callback Callback function to handle events
 * @param user_data User data to pass to callback
 * @param min_priority Minimum priority of events to receive
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 *
 * @note Callback may be called from any thread
 * @note User data pointer must remain valid until unsubscribed
 */
layer_events_error_t
layer_events_subscribe(layer_event_system_t *events,
                       layer_event_type_t event_type, layer_id_t subscriber_id,
                       layer_event_callback_t callback, void *user_data,
                       layer_event_priority_t min_priority);

/**
 * Unsubscribe from events
 *
 * Removes a previous subscription for the specified event type and subscriber.
 *
 * @param events Pointer to event system instance
 * @param event_type Type of events to unsubscribe from
 * @param subscriber_id ID of the subscribing layer
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t layer_events_unsubscribe(layer_event_system_t *events,
                                              layer_event_type_t event_type,
                                              layer_id_t subscriber_id);

/**
 * Unsubscribe from all events
 *
 * Removes all subscriptions for the specified subscriber.
 *
 * @param events Pointer to event system instance
 * @param subscriber_id ID of the subscribing layer
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t layer_events_unsubscribe_all(layer_event_system_t *events,
                                                  layer_id_t subscriber_id);

/// ============================================================================
/// EVENT PROCESSING FUNCTIONS
/// ============================================================================

/**
 * Process pending events
 *
 * Processes all pending events in the queue. Events are processed in
 * priority order, with higher priority events processed first.
 *
 * @param events Pointer to event system instance
 * @param max_events Maximum number of events to process (0 for all)
 * @param timeout_ms Maximum time to spend processing (0 for no limit)
 * @return Number of events processed, or -1 on error
 *
 * @note This function may block for up to timeout_ms milliseconds
 * @note Critical priority events are always processed first
 */
int layer_events_process_pending(layer_event_system_t *events,
                                 uint32_t max_events, uint32_t timeout_ms);

/**
 * Process events of specific priority
 *
 * Processes only events of the specified priority level.
 *
 * @param events Pointer to event system instance
 * @param priority Priority level to process
 * @param max_events Maximum number of events to process
 * @return Number of events processed, or -1 on error
 */
int layer_events_process_priority(layer_event_system_t *events,
                                  layer_event_priority_t priority,
                                  uint32_t max_events);

/**
 * Check if there are pending events
 *
 * Checks whether there are any events waiting to be processed.
 *
 * @param events Pointer to event system instance
 * @return true if events are pending, false otherwise
 */
bool layer_events_has_pending(layer_event_system_t *events);

/**
 * Get number of pending events
 *
 * Returns the current number of events in the processing queue.
 *
 * @param events Pointer to event system instance
 * @return Number of pending events, or 0 if system is NULL
 */
uint32_t layer_events_get_pending_count(layer_event_system_t *events);

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * Get event type name
 *
 * Returns a human-readable string for the event type.
 *
 * @param event_type Event type to convert
 * @return Static string describing the event type
 *
 * @note Returned string is static and should not be freed
 */
const char *layer_events_get_type_name(layer_event_type_t event_type);

/**
 * Get layer name
 *
 * Returns a human-readable string for the layer ID.
 *
 * @param layer_id Layer ID to convert
 * @return Static string describing the layer
 *
 * @note Returned string is static and should not be freed
 */
const char *layer_events_get_layer_name(layer_id_t layer_id);

/**
 * Create default configuration
 *
 * Creates a default configuration structure for the event system.
 *
 * @return Default configuration structure
 */
layer_events_config_t layer_events_create_default_config(void);

/**
 * Validate event structure
 *
 * Validates that an event structure contains valid data.
 *
 * @param event Event to validate
 * @return true if event is valid, false otherwise
 */
bool layer_events_validate_event(const layer_event_t *event);

/// ============================================================================
/// PERFORMANCE AND DIAGNOSTICS
/// ============================================================================

/**
 * Get event system statistics
 *
 * Returns performance and usage statistics for the event system.
 *
 * @param events Pointer to event system instance
 * @return Event system statistics structure
 *
 * @note Returns empty statistics if events is NULL
 */
layer_event_stats_t layer_events_get_statistics(layer_event_system_t *events);

/**
 * Clear event system statistics
 *
 * Resets all performance counters to zero.
 *
 * @param events Pointer to event system instance
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
layer_events_clear_statistics(layer_event_system_t *events);

/**
 * Enable or disable debug logging
 *
 * Controls whether the event system logs debug information.
 *
 * @param events Pointer to event system instance
 * @param enabled true to enable debug logging, false to disable
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
layer_events_set_debug_enabled(layer_event_system_t *events, bool enabled);

/**
 * Dump event system state
 *
 * Prints detailed information about the current event system state
 * including pending events, subscribers, and statistics.
 *
 * @param events Pointer to event system instance
 * @param include_queue_contents Whether to include pending event details
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t layer_events_dump_state(layer_event_system_t *events,
                                             bool include_queue_contents);

/// ============================================================================
/// ERROR HANDLING
/// ============================================================================

/**
 * Get error description string
 *
 * Converts an error code to a human-readable description string.
 *
 * @param error Error code to convert
 * @return Static string describing the error
 *
 * @note Returned string is static and should not be freed
 * @note Always returns a valid string, even for unknown error codes
 */
const char *layer_events_error_string(layer_events_error_t error);

/// ============================================================================
/// VERSION INFORMATION
/// ============================================================================

/**
 * Get layer events system version
 *
 * Returns version information for the layer events system implementation.
 *
 * @param major Pointer to store major version number
 * @param minor Pointer to store minor version number
 * @param patch Pointer to store patch version number
 */
static inline void layer_events_get_version(int *major, int *minor,
                                            int *patch) {
    if (major) {
        *major = LAYER_EVENTS_VERSION_MAJOR;
    }
    if (minor) {
        *minor = LAYER_EVENTS_VERSION_MINOR;
    }
    if (patch) {
        *patch = LAYER_EVENTS_VERSION_PATCH;
    }
}

#ifdef __cplusplus
}
#endif

#endif /// LAYER_EVENTS_H

/// ============================================================================
/// USAGE EXAMPLES
/// ============================================================================

/*
 * Basic usage example:
 *
 * ```c
 * #include "layer_events.h"
 *
 * /// Event callback function
 * layer_events_error_t handle_content_change(const layer_event_t *event, void
 * *user_data) { printf("Content changed: %s\n",
 * event->data.content_changed.new_content); return LAYER_EVENTS_SUCCESS;
 * }
 *
 * int main() {
 *     /// Create event system
 *     layer_event_system_t *events = layer_events_create(NULL);
 *     layer_events_init(events);
 *
 *     /// Subscribe to content change events
 *     layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
 *                            LAYER_ID_COMMAND_LAYER, handle_content_change,
 *                            NULL, LAYER_EVENT_PRIORITY_NORMAL);
 *
 *     /// Publish a content change event
 *     layer_events_publish_content_changed(events, LAYER_ID_PROMPT_LAYER,
 *                                          "New prompt text", 15, false);
 *
 *     /// Process events
 *     layer_events_process_pending(events, 0, 100);
 *
 *     /// Cleanup
 *     layer_events_destroy(events);
 *     return 0;
 * }
 * ```
 *
 * Advanced event handling:
 *
 * ```c
 * /// Create custom event
 * layer_event_t custom_event = {
 *     .type = LAYER_EVENT_CUSTOM_START + 1,
 *     .source_layer = LAYER_ID_DISPLAY_CONTROLLER,
 *     .target_layer = LAYER_ID_COMMAND_LAYER,
 *     .priority = LAYER_EVENT_PRIORITY_HIGH,
 *     .flags = LAYER_EVENT_FLAG_ASYNC | LAYER_EVENT_FLAG_ONCE
 * };
 *
 * /// Set custom data
 * custom_event.data.custom_data.data = my_data;
 * custom_event.data.custom_data.data_size = sizeof(my_data);
 * custom_event.data.custom_data.cleanup_func = my_cleanup_function;
 *
 * /// Publish custom event
 * layer_events_publish(events, &custom_event);
 * ```
 */
