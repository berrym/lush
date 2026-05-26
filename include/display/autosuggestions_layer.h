/**
 * @file autosuggestions_layer.h
 * @brief Autosuggestions layer (Layer 7) - Fish-like autosuggestions
 *
 * Provides Fish-like autosuggestions integrated with the layered display
 * architecture. Eliminates display corruption and provides enterprise-grade
 * autosuggestion capabilities.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Seamless integration with layered display architecture
 * - Fish-like autosuggestion experience with professional appearance
 * - Intelligent caching and performance optimization
 * - Safe display operations coordinated with other layers
 * - Enterprise-ready configuration and monitoring
 * - Zero display corruption through proper layer coordination
 *
 * Design Principles:
 * - Full integration with existing layered display system
 * - Professional appearance with configurable styling
 * - High-performance suggestion generation and caching
 * - Safe display operations with proper layer coordination
 * - Comprehensive error handling and recovery mechanisms
 * - Cross-platform compatibility with terminal capability detection
 *
 * Strategic Innovation:
 * This layer completes the autosuggestions perfection initiative by providing
 * enterprise-grade Fish-like autosuggestions that integrate flawlessly with
 * the sophisticated layered display architecture.
 */

#ifndef AUTOSUGGESTIONS_LAYER_H
#define AUTOSUGGESTIONS_LAYER_H

#include "layer_events.h"
#include "terminal_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ============================================================================
/// CONSTANTS AND CONFIGURATION
/// ============================================================================

#define AUTOSUGGESTIONS_LAYER_VERSION_MAJOR 1
#define AUTOSUGGESTIONS_LAYER_VERSION_MINOR 3
#define AUTOSUGGESTIONS_LAYER_VERSION_PATCH 0

/// Layer identification
#define LAYER_ID_AUTOSUGGESTIONS 7

/// Performance and caching limits
#define AUTOSUGGESTIONS_LAYER_MAX_SUGGESTION_LENGTH 256
#define AUTOSUGGESTIONS_LAYER_MAX_CACHE_ENTRIES 64
#define AUTOSUGGESTIONS_LAYER_CACHE_TTL_MS 30000
#define AUTOSUGGESTIONS_LAYER_MAX_GENERATION_TIME_MS 50

/// Display configuration
#define AUTOSUGGESTIONS_LAYER_DEFAULT_COLOR_TYPE TERMINAL_COLOR_BRIGHT_BLACK
#define AUTOSUGGESTIONS_LAYER_MIN_TERMINAL_WIDTH 40
#define AUTOSUGGESTIONS_LAYER_MAX_DISPLAY_WIDTH 120

/// Performance monitoring
#define AUTOSUGGESTIONS_LAYER_METRICS_HISTORY_SIZE 100
#define AUTOSUGGESTIONS_LAYER_PERFORMANCE_THRESHOLD_MS 10

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Error codes for autosuggestions layer operations
 */
typedef enum {
    AUTOSUGGESTIONS_LAYER_SUCCESS = 0, /// Operation completed successfully
    AUTOSUGGESTIONS_LAYER_ERROR_INVALID_PARAM, /// Invalid parameter provided
    AUTOSUGGESTIONS_LAYER_ERROR_NULL_POINTER,  /// NULL pointer passed
    AUTOSUGGESTIONS_LAYER_ERROR_MEMORY_ALLOCATION,  /// Memory allocation failed
    AUTOSUGGESTIONS_LAYER_ERROR_NOT_INITIALIZED,    /// Layer not initialized
    AUTOSUGGESTIONS_LAYER_ERROR_TERMINAL_TOO_SMALL, /// Terminal too small
    AUTOSUGGESTIONS_LAYER_ERROR_GENERATION_FAILED,  /// Suggestion generation
                                                    /// failed
    AUTOSUGGESTIONS_LAYER_ERROR_DISPLAY_FAILED,     /// Display operation failed
    AUTOSUGGESTIONS_LAYER_ERROR_CACHE_FULL,         /// Cache is full
    AUTOSUGGESTIONS_LAYER_ERROR_UNSUPPORTED_TERMINAL, /// Terminal doesn't
                                                      /// support required
                                                      /// features
    AUTOSUGGESTIONS_LAYER_ERROR_LAYER_CONFLICT, /// Conflict with other layers
    AUTOSUGGESTIONS_LAYER_ERROR_EVENT_FAILED    /// Event handling failed
} autosuggestions_layer_error_t;

/**
 * Autosuggestion display style configuration
 */
typedef struct {
    terminal_color_t suggestion_color;       /// Color for suggestion text
    terminal_style_flags_t suggestion_style; /// Style flags (dim, italic, etc.)
    bool enable_color;                       /// Whether to use color
    bool enable_styling;                     /// Whether to apply styling
    int display_delay_ms;                    /// Delay before showing suggestion
    int fade_timeout_ms;          /// Timeout before fading suggestion
    bool show_in_multiline;       /// Show suggestions in multiline input
    bool show_for_short_commands; /// Show for commands < 3 chars
} autosuggestions_display_config_t;

/**
 * Autosuggestion cache entry
 */
typedef struct {
    char *input_text;             /// Input that generated suggestion
    char *suggestion_text;        /// Generated suggestion
    int suggestion_score;         /// Quality score of suggestion
    uint64_t generation_time_ns;  /// Time to generate suggestion
    uint64_t last_used_timestamp; /// Last time this entry was used
    uint32_t use_count;           /// Number of times used
    bool valid;                   /// Whether entry is valid
} autosuggestions_cache_entry_t;

/**
 * Performance metrics for autosuggestions layer
 */
typedef struct {
    uint64_t total_suggestions_requested; /// Total suggestions requested
    uint64_t suggestions_generated;       /// Suggestions successfully generated
    uint64_t suggestions_displayed;       /// Suggestions actually displayed
    uint64_t suggestions_accepted;        /// Suggestions accepted by user
    uint64_t cache_hits;                  /// Cache hits
    uint64_t cache_misses;                /// Cache misses
    double avg_generation_time_ms;        /// Average generation time
    double avg_display_time_ms;           /// Average display time
    double cache_hit_rate;                /// Cache hit rate percentage
    uint64_t display_errors;              /// Display errors encountered
    uint64_t layer_conflicts;             /// Conflicts with other layers
    uint64_t last_performance_update;     /// Last performance update timestamp
} autosuggestions_performance_metrics_t;

/**
 * Autosuggestions layer state
 */
typedef struct {
    /// Layer identification and initialization
    layer_id_t layer_id; /// Layer ID in the system
    bool initialized;    /// Whether layer is initialized
    bool enabled;        /// Whether autosuggestions are enabled
    autosuggestions_layer_error_t last_error; /// Last error encountered

    /// Display configuration
    autosuggestions_display_config_t display_config; /// Display configuration
    terminal_capabilities_t terminal_caps;           /// Terminal capabilities

    /// Current state
    char *current_input;           /// Current input line
    char *current_suggestion;      /// Current suggestion
    int cursor_position;           /// Current cursor position
    bool suggestion_displayed;     /// Whether suggestion is displayed
    uint64_t suggestion_timestamp; /// When suggestion was generated

    /// Caching system
    autosuggestions_cache_entry_t
        cache[AUTOSUGGESTIONS_LAYER_MAX_CACHE_ENTRIES];
    int cache_size;       /// Number of cache entries
    int cache_next_index; /// Next cache index to use

    /// Performance monitoring
    autosuggestions_performance_metrics_t metrics; /// Performance metrics

    /// Layer integration
    layer_event_system_t *event_system;   /// Event system reference
    terminal_control_t *terminal_control; /// Terminal control reference

    /// Thread safety and coordination
    bool in_display_operation; /// Prevent recursive display calls
    bool needs_refresh;        /// Whether display needs refresh
} autosuggestions_layer_t;

/**
 * Autosuggestion context for generation
 */
typedef struct {
    const char *input_line;        /// Current input line
    int cursor_position;           /// Cursor position in line
    int line_length;               /// Length of input line
    bool is_multiline_context;     /// Whether in multiline input
    const char *current_directory; /// Current working directory
    const char *shell_context;     /// Additional shell context
} autosuggestions_context_t;

/// ============================================================================
/// CORE LAYER MANAGEMENT
/// ============================================================================

/**
 * Create and initialize autosuggestions layer
 *
 * @param event_system Event system for layer coordination
 * @param terminal_control Terminal control layer reference
 * @return Pointer to initialized layer, or NULL on failure
 */
autosuggestions_layer_t *
autosuggestions_layer_create(layer_event_system_t *event_system,
                             terminal_control_t *terminal_control);

/**
 * Initialize autosuggestions layer
 *
 * @param layer Layer to initialize
 * @param config Display configuration (NULL for defaults)
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_init(autosuggestions_layer_t *layer,
                           const autosuggestions_display_config_t *config);

/**
 * Cleanup and destroy autosuggestions layer
 *
 * @param layer Layer to destroy (pointer will be set to NULL)
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_destroy(autosuggestions_layer_t **layer);

/**
 * Enable or disable autosuggestions layer
 *
 * @param layer Layer to configure
 * @param enabled Whether to enable autosuggestions
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_set_enabled(autosuggestions_layer_t *layer, bool enabled);

/**
 * Check if autosuggestions layer is enabled
 *
 * @param layer Layer to check
 * @return true if enabled, false otherwise
 */
bool autosuggestions_layer_is_enabled(const autosuggestions_layer_t *layer);

/// ============================================================================
/// SUGGESTION GENERATION AND DISPLAY
/// ============================================================================

/**
 * Update autosuggestions for current input
 *
 * This function is called when the input line changes and generates/displays
 * appropriate suggestions using the layered display system.
 *
 * @param layer Autosuggestions layer
 * @param context Input context for suggestion generation
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_update(autosuggestions_layer_t *layer,
                             const autosuggestions_context_t *context);

/**
 * Clear current autosuggestion display
 *
 * @param layer Autosuggestions layer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_clear(autosuggestions_layer_t *layer);

/**
 * Accept current autosuggestion
 *
 * @param layer Autosuggestions layer
 * @param accepted_text Buffer to store accepted suggestion text
 * @param buffer_size Size of accepted_text buffer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_accept(autosuggestions_layer_t *layer,
                             char *accepted_text, size_t buffer_size);

/**
 * Get current suggestion text
 *
 * @param layer Autosuggestions layer
 * @return Current suggestion text, or NULL if no suggestion
 */
const char *autosuggestions_layer_get_current_suggestion(
    const autosuggestions_layer_t *layer);

/**
 * Set suggestion text directly (bypassing internal generation)
 *
 * This function allows external code (like lle_readline) to set the suggestion
 * text directly, bypassing the internal lush_get_suggestion() generation.
 * This is used when the suggestion should come from LLE history rather than
 * the legacy GNU readline history.
 *
 * @param layer Autosuggestions layer
 * @param suggestion Suggestion text to display (the part to append after
 * cursor)
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_set_suggestion(autosuggestions_layer_t *layer,
                                     const char *suggestion);

/**
 * Check if a suggestion is currently displayed
 *
 * @param layer Autosuggestions layer
 * @return true if suggestion is displayed, false otherwise
 */
bool autosuggestions_layer_has_suggestion(const autosuggestions_layer_t *layer);

/// ============================================================================
/// CONFIGURATION AND STYLING
/// ============================================================================

/**
 * Set display configuration
 *
 * @param layer Autosuggestions layer
 * @param config New display configuration
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t autosuggestions_layer_set_display_config(
    autosuggestions_layer_t *layer,
    const autosuggestions_display_config_t *config);

/**
 * Get current display configuration
 *
 * @param layer Autosuggestions layer
 * @param config Buffer to store current configuration
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t autosuggestions_layer_get_display_config(
    const autosuggestions_layer_t *layer,
    autosuggestions_display_config_t *config);

/**
 * Create default display configuration
 *
 * @param config Configuration structure to initialize with defaults
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t autosuggestions_layer_create_default_config(
    autosuggestions_display_config_t *config);

/**
 * Set suggestion color
 *
 * @param layer Autosuggestions layer
 * @param color New color for suggestions
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_set_color(autosuggestions_layer_t *layer,
                                terminal_color_t color);

/**
 * Set suggestion style
 *
 * @param layer Autosuggestions layer
 * @param style New style flags for suggestions
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_set_style(autosuggestions_layer_t *layer,
                                terminal_style_flags_t style);

/// ============================================================================
/// CACHE MANAGEMENT
/// ============================================================================

/**
 * Clear suggestion cache
 *
 * @param layer Autosuggestions layer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_clear_cache(autosuggestions_layer_t *layer);

/**
 * Get cache statistics
 *
 * @param layer Autosuggestions layer
 * @param hit_rate Pointer to store cache hit rate (0.0 to 1.0)
 * @param cache_size Pointer to store current cache size
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_get_cache_stats(const autosuggestions_layer_t *layer,
                                      double *hit_rate, int *cache_size);

/**
 * Preload suggestion cache with common patterns
 *
 * @param layer Autosuggestions layer
 * @param patterns Array of common input patterns to cache
 * @param pattern_count Number of patterns in array
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_preload_cache(autosuggestions_layer_t *layer,
                                    const char **patterns, int pattern_count);

/// ============================================================================
/// PERFORMANCE MONITORING
/// ============================================================================

/**
 * Get performance metrics
 *
 * @param layer Autosuggestions layer
 * @param metrics Buffer to store performance metrics
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t autosuggestions_layer_get_metrics(
    const autosuggestions_layer_t *layer,
    autosuggestions_performance_metrics_t *metrics);

/**
 * Reset performance metrics
 *
 * @param layer Autosuggestions layer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_reset_metrics(autosuggestions_layer_t *layer);

/**
 * Print performance diagnostics
 *
 * @param layer Autosuggestions layer
 * @param output File stream to write diagnostics to
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_print_diagnostics(const autosuggestions_layer_t *layer,
                                        FILE *output);

/**
 * Check if performance is within acceptable thresholds
 *
 * @param layer Autosuggestions layer
 * @return true if performance is acceptable, false otherwise
 */
bool autosuggestions_layer_performance_ok(const autosuggestions_layer_t *layer);

/// ============================================================================
/// LAYER EVENT INTEGRATION
/// ============================================================================

/**
 * Handle layer events
 *
 * This function is called by the event system when layer events are received.
 *
 * @param event Event to handle
 * @param user_data User data (autosuggestions layer)
 * @return LAYER_EVENTS_SUCCESS if event was handled, error code otherwise
 */
layer_events_error_t
autosuggestions_layer_handle_event(const layer_event_t *event, void *user_data);

/**
 * Publish autosuggestion change event
 *
 * @param layer Autosuggestions layer
 * @param old_suggestion Previous suggestion text (NULL if none)
 * @param new_suggestion New suggestion text (NULL if cleared)
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_publish_change(autosuggestions_layer_t *layer,
                                     const char *old_suggestion,
                                     const char *new_suggestion);

/**
 * Subscribe to relevant layer events
 *
 * @param layer Autosuggestions layer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_subscribe_events(autosuggestions_layer_t *layer);

/**
 * Unsubscribe from layer events
 *
 * @param layer Autosuggestions layer
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_unsubscribe_events(autosuggestions_layer_t *layer);

/// ============================================================================
/// ERROR HANDLING AND UTILITIES
/// ============================================================================

/**
 * Get last error from layer
 *
 * @param layer Autosuggestions layer
 * @return Last error code
 */
autosuggestions_layer_error_t
autosuggestions_layer_get_last_error(const autosuggestions_layer_t *layer);

/**
 * Get human-readable error message
 *
 * @param error Error code to convert
 * @return Static string describing the error
 */
const char *
autosuggestions_layer_error_string(autosuggestions_layer_error_t error);

/**
 * Check if terminal supports autosuggestions display
 *
 * @param layer Autosuggestions layer
 * @return true if terminal supports autosuggestions, false otherwise
 */
bool autosuggestions_layer_terminal_supported(
    const autosuggestions_layer_t *layer);

/**
 * Get layer version information
 *
 * @param major Pointer to store major version
 * @param minor Pointer to store minor version
 * @param patch Pointer to store patch version
 */
void autosuggestions_layer_get_version(int *major, int *minor, int *patch);

/// ============================================================================
/// INTEGRATION HELPERS
/// ============================================================================

/**
 * Create autosuggestion context from readline state
 *
 * @param context Context structure to populate
 * @param line_buffer Current readline buffer
 * @param cursor_pos Current cursor position
 * @param line_end End of line position
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_create_context_from_readline(
    autosuggestions_context_t *context, const char *line_buffer, int cursor_pos,
    int line_end);

/**
 * Check if autosuggestions should be shown for current input
 *
 * @param layer Autosuggestions layer
 * @param context Input context
 * @return true if suggestions should be shown, false otherwise
 */
bool autosuggestions_layer_should_suggest(
    const autosuggestions_layer_t *layer,
    const autosuggestions_context_t *context);

/**
 * Coordinate with composition engine for display
 *
 * @param layer Autosuggestions layer
 * @param buffer Buffer to write suggestion display data
 * @param buffer_size Size of buffer
 * @param bytes_written Pointer to store number of bytes written
 * @return AUTOSUGGESTIONS_LAYER_SUCCESS on success, error code otherwise
 */
autosuggestions_layer_error_t
autosuggestions_layer_compose_display(autosuggestions_layer_t *layer,
                                      char *buffer, size_t buffer_size,
                                      size_t *bytes_written);

#ifdef __cplusplus
}
#endif

#endif /// AUTOSUGGESTIONS_LAYER_H
