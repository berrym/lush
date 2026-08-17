/**
 * @file prompt_layer.h
 * @brief Prompt layer - Universal prompt rendering system
 *
 * Provides universal prompt rendering that works with any prompt structure
 * without requiring parsing or modification. Integrates with the theme system.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Universal prompt structure compatibility
 * - Integration with existing theme system (6 professional themes)
 * - Event-driven communication with foundation layers
 * - High-performance rendering with intelligent caching
 * - Memory-safe prompt content management
 * - Real-time theme switching support
 *
 * Design Principles:
 * - No prompt structure parsing required
 * - Works with any prompt format (simple, complex, ASCII art)
 * - Preserves existing prompt generation functionality
 * - Performance-optimized (<5ms rendering time)
 * - Memory-safe with proper resource management
 *
 * Strategic Innovation:
 * This layer enables the revolutionary combination of professional themes
 * with real-time syntax highlighting by providing universal prompt
 * compatibility without interfering with command input processing.
 */

#ifndef PROMPT_LAYER_H
#define PROMPT_LAYER_H

#include "layer_events.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/// ============================================================================
/// CONSTANTS AND CONFIGURATION
/// ============================================================================

#define PROMPT_LAYER_VERSION_MAJOR 1
#define PROMPT_LAYER_VERSION_MINOR 0
#define PROMPT_LAYER_VERSION_PATCH 0

/// Prompt content limits
#define PROMPT_LAYER_MAX_CONTENT_SIZE 4096
#define PROMPT_LAYER_MAX_LINES 32
#define PROMPT_LAYER_MAX_LINE_WIDTH 512

/// Performance targets
#define PROMPT_LAYER_TARGET_RENDER_TIME_MS 5
#define PROMPT_LAYER_CACHE_EXPIRY_MS 100

/// Cache configuration
#define PROMPT_LAYER_CACHE_SIZE 32
#define PROMPT_LAYER_METRICS_HISTORY_SIZE 16

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Error codes for prompt layer operations
 */
typedef enum {
    PROMPT_LAYER_SUCCESS = 0,               /// Operation completed successfully
    PROMPT_LAYER_ERROR_INVALID_PARAM,       /// Invalid parameter provided
    PROMPT_LAYER_ERROR_NULL_POINTER,        /// NULL pointer passed
    PROMPT_LAYER_ERROR_MEMORY_ALLOCATION,   /// Memory allocation failed
    PROMPT_LAYER_ERROR_BUFFER_TOO_SMALL,    /// Output buffer insufficient
    PROMPT_LAYER_ERROR_CONTENT_TOO_LARGE,   /// Content exceeds limits
    PROMPT_LAYER_ERROR_THEME_NOT_AVAILABLE, /// Theme system unavailable
    PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE, /// Event communication failed
    PROMPT_LAYER_ERROR_RENDERING_FAILURE,    /// Prompt rendering failed
    PROMPT_LAYER_ERROR_INVALID_STATE,        /// Layer in invalid state
    PROMPT_LAYER_ERROR_PERFORMANCE_TIMEOUT,  /// Operation exceeded time limit
    PROMPT_LAYER_ERROR_UNKNOWN               /// Unknown error occurred
} prompt_layer_error_t;

/**
 * Prompt content structure and metrics
 */
typedef struct {
    int line_count;               /// Number of lines in prompt
    int max_line_width;           /// Width of longest line
    int total_visual_width;       /// Total visual width (with colors)
    int estimated_command_column; /// Best guess for command start column
    int estimated_command_row;    /// Best guess for command start row
    bool has_ansi_sequences;      /// Contains ANSI color codes
    bool is_multiline;            /// Spans multiple lines
    bool has_unicode;             /// Contains Unicode characters
} prompt_metrics_t;

/**
 * Prompt cache entry for performance optimization
 */
typedef struct {
    char *raw_content;         /// Original prompt content
    char *rendered_content;    /// Theme-rendered content
    char *theme_name;          /// Theme used for rendering
    prompt_metrics_t metrics;  /// Cached metrics
    uint64_t content_hash;     /// Content hash for validation
    uint64_t theme_hash;       /// Theme hash for validation
    uint64_t creation_time_ns; /// Cache entry creation time (nanoseconds)
    bool is_valid;             /// Cache validity flag
} prompt_cache_entry_t;

/**
 * Performance monitoring and statistics
 */
typedef struct {
    uint64_t render_count;         /// Total render operations
    uint64_t cache_hits;           /// Cache hit count
    uint64_t cache_misses;         /// Cache miss count
    uint64_t theme_switches;       /// Theme change count
    uint64_t total_render_time_ns; /// Total rendering time (nanoseconds)
    uint64_t max_render_time_ns;   /// Maximum render time
    uint64_t min_render_time_ns;   /// Minimum render time
    uint64_t avg_render_time_ns;   /// Average render time
    uint64_t recent_render_times[PROMPT_LAYER_METRICS_HISTORY_SIZE]; /// Recent
                                                                     /// timing
                                                                     /// history
    int recent_times_index;          /// Index for circular buffer
    uint64_t last_render_time_ns;    /// Last render timestamp (nanoseconds)
    uint64_t last_metrics_update_ns; /// Last metrics update time (nanoseconds)
} prompt_performance_t;

/**
 * Theme integration context
 */
typedef struct {
    char *current_theme_name;     /// Currently active theme name
    uint64_t theme_hash;          /// Current theme hash
    bool theme_available;         /// Theme system availability
    bool theme_supports_prompt;   /// Theme has prompt support
    uint64_t last_theme_check_ns; /// Last theme validation time (nanoseconds)
    void *theme_context;          /// Theme-specific context data
} prompt_theme_context_t;

/**
 * Event communication state
 */
typedef struct {
    layer_event_system_t *events; /// Event system reference
    bool events_initialized;      /// Event system ready
    uint32_t subscription_ids[8]; /// Event subscription IDs
    int subscription_count;       /// Number of active subscriptions
    uint64_t events_received;     /// Total events received
    uint64_t events_processed;    /// Total events processed
    uint64_t last_event_time_ns;  /// Last event timestamp (nanoseconds)
} prompt_events_context_t;

/**
 * Main prompt layer structure
 */
typedef struct {
    /// Content management
    char *raw_content;                /// Original prompt content
    char *rendered_content;           /// Final rendered content
    prompt_metrics_t current_metrics; /// Current prompt metrics
    bool content_dirty;               /// Content needs re-rendering
    bool metrics_dirty;               /// Metrics need recalculation

    /// Theme integration
    prompt_theme_context_t theme_context; /// Theme integration state

    /// Performance and caching
    prompt_cache_entry_t cache[PROMPT_LAYER_CACHE_SIZE]; /// Render cache
    int cache_next_index;             /// Next cache slot to use
    prompt_performance_t performance; /// Performance metrics

    /// Event communication
    prompt_events_context_t events_context; /// Event system integration

    /// Layer state
    bool initialized;             /// Layer initialization state
    bool enabled;                 /// Layer enabled/disabled
    uint64_t creation_time_ns;    /// Layer creation timestamp (nanoseconds)
    uint64_t last_update_time_ns; /// Last update timestamp (nanoseconds)

    /// Memory management
    size_t allocated_size; /// Total allocated memory
    uint32_t magic_header; /// Memory corruption detection
    uint32_t magic_footer; /// Memory corruption detection
} prompt_layer_t;

/// ============================================================================
/// LIFECYCLE FUNCTIONS
/// ============================================================================

/**
 * Create a new prompt layer instance
 *
 * Allocates and initializes a new prompt_layer_t structure with default
 * values. The layer is not yet ready for use - call prompt_layer_init()
 * to complete initialization.
 *
 * @return Pointer to new prompt_layer_t instance, or NULL on failure
 *
 * @note The returned pointer must be freed with prompt_layer_destroy()
 * @note Layer starts in disabled state until properly initialized
 */
prompt_layer_t *prompt_layer_create(void);

/**
 * Initialize prompt layer with event system integration
 *
 * Completes initialization of the prompt layer, including:
 * - Event system integration and subscription setup
 * - Theme system integration and validation
 * - Performance monitoring initialization
 * - Cache system preparation
 *
 * @param layer Pointer to prompt_layer_t instance
 * @param events Pointer to layer events system
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Layer must be created before calling this function
 * @note Events system must be initialized before calling this function
 * @note Automatically enables the layer on successful initialization
 */
prompt_layer_error_t prompt_layer_init(prompt_layer_t *layer,
                                       layer_event_system_t *events);

/**
 * Cleanup prompt layer resources
 *
 * Releases all resources used by the prompt layer, including:
 * - Event system subscriptions and communication
 * - Theme system integration cleanup
 * - Cache memory and content cleanup
 * - Performance monitoring cleanup
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Safe to call multiple times
 * @note Does not free the layer structure itself - use prompt_layer_destroy()
 * @note Automatically disables the layer during cleanup
 */
prompt_layer_error_t prompt_layer_cleanup(prompt_layer_t *layer);

/**
 * Destroy prompt layer instance
 *
 * Performs cleanup and frees the layer structure. After calling this
 * function, the layer pointer is invalid and should not be used.
 *
 * @param layer Pointer to prompt_layer_t instance
 *
 * @note Automatically calls prompt_layer_cleanup() if needed
 * @note Safe to call with NULL pointer
 * @note Validates memory integrity before destruction
 */
void prompt_layer_destroy(prompt_layer_t *layer);

/// ============================================================================
/// CONTENT MANAGEMENT FUNCTIONS
/// ============================================================================

/**
 * Set prompt content for rendering
 *
 * Sets the raw prompt content that will be rendered with the current theme.
 * The content can be any format - simple text, complex multi-line prompts,
 * or even ASCII art. No parsing or structural analysis is performed.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @param content Raw prompt content to render
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Content is copied internally - caller retains ownership
 * @note Triggers cache invalidation and re-rendering
 * @note Supports any prompt structure without modification
 * @note Content length is limited by PROMPT_LAYER_MAX_CONTENT_SIZE
 */
prompt_layer_error_t prompt_layer_set_content(prompt_layer_t *layer,
                                              const char *content);

/**
 * Get current rendered prompt content
 *
 * Returns the current rendered prompt content with theme colors and
 * formatting applied. The content is ready for direct terminal output.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @param output Buffer to store rendered content
 * @param output_size Size of output buffer
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Automatically renders content if cache is invalid
 * @note Output includes ANSI color codes and formatting
 * @note Buffer must be large enough for rendered content
 */
prompt_layer_error_t prompt_layer_get_rendered_content(prompt_layer_t *layer,
                                                       char *output,
                                                       size_t output_size);

/**
 * Get prompt metrics and positioning information
 *
 * Returns detailed metrics about the current prompt, including size,
 * line count, and estimated cursor positioning for command input.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @param metrics Pointer to structure to receive metrics
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Metrics are calculated automatically during rendering
 * @note Provides best-effort cursor position estimation
 * @note Updates performance statistics during calculation
 */
prompt_layer_error_t prompt_layer_get_metrics(prompt_layer_t *layer,
                                              prompt_metrics_t *metrics);

/// ============================================================================
/// THEME INTEGRATION FUNCTIONS
/// ============================================================================

/**
 * Update theme integration and refresh content
 *
 * Updates the layer's theme integration, validates theme availability,
 * and triggers content re-rendering with the new theme if needed.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Automatically detects theme changes and updates accordingly
 * @note Invalidates cache when theme changes are detected
 * @note Integrates with existing Lush theme system
 * @note Supports all 6 built-in themes (dark, light, minimal, colorful,
 * classic, corporate)
 */
prompt_layer_error_t prompt_layer_update_theme(prompt_layer_t *layer);

/**
 * Force re-rendering with current theme
 *
 * Forces immediate re-rendering of prompt content with the current theme,
 * bypassing cache and performance optimizations. Useful for theme debugging
 * or when cache corruption is suspected.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Clears all cache entries before rendering
 * @note Updates performance metrics with forced render timing
 * @note Should be used sparingly due to performance impact
 */
prompt_layer_error_t prompt_layer_force_render(prompt_layer_t *layer);

/// ============================================================================
/// PERFORMANCE AND MONITORING FUNCTIONS
/// ============================================================================

/**
 * Get current performance statistics
 *
 * Returns detailed performance metrics for the prompt layer, including
 * render times, cache hit rates, and resource usage statistics.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @param performance Pointer to structure to receive performance data
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Performance data is updated continuously during operation
 * @note Includes historical timing data for trend analysis
 * @note Useful for performance optimization and debugging
 */
prompt_layer_error_t
prompt_layer_get_performance(prompt_layer_t *layer,
                             prompt_performance_t *performance);

/**
 * Reset performance statistics
 *
 * Resets all performance counters and timing statistics to initial values.
 * Useful for performance testing and benchmarking specific operations.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Preserves cache contents but resets cache hit/miss counters
 * @note Does not affect layer functionality or content
 */
prompt_layer_error_t prompt_layer_reset_performance(prompt_layer_t *layer);

/**
 * Optimize layer performance and cleanup resources
 *
 * Performs maintenance operations to optimize layer performance:
 * - Cache cleanup and reorganization
 * - Memory defragmentation
 * - Performance statistics analysis
 * - Resource usage optimization
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Safe to call during normal operation
 * @note May cause temporary performance impact during optimization
 * @note Automatically called periodically by the layer
 */
prompt_layer_error_t prompt_layer_optimize(prompt_layer_t *layer);

/// ============================================================================
/// EVENT HANDLING FUNCTIONS
/// ============================================================================

/**
 * Process pending events for the layer
 *
 * Processes all pending events relevant to the prompt layer, including:
 * - Theme change notifications
 * - Terminal size change events
 * - Content refresh requests
 * - Performance monitoring events
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Should be called regularly to maintain responsiveness
 * @note Automatically handles event prioritization
 * @note Updates layer state based on processed events
 */
prompt_layer_error_t prompt_layer_process_events(prompt_layer_t *layer);

/// ============================================================================
/// UTILITY AND DIAGNOSTIC FUNCTIONS
/// ============================================================================

/**
 * Validate layer integrity and state
 *
 * Performs comprehensive validation of the layer's internal state,
 * memory integrity, and configuration consistency.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS if valid, error code if issues detected
 *
 * @note Checks memory corruption markers
 * @note Validates all internal pointers and structures
 * @note Useful for debugging and health monitoring
 */
prompt_layer_error_t prompt_layer_validate(prompt_layer_t *layer);

/**
 * Get human-readable error description
 *
 * Returns a descriptive string for the given error code, useful for
 * logging and debugging purposes.
 *
 * @param error Error code to describe
 * @return String description of the error
 *
 * @note Returned string is statically allocated and does not need to be freed
 * @note Safe to call with any error code value
 */
const char *prompt_layer_error_string(prompt_layer_error_t error);

/**
 * Get layer version information
 *
 * Returns version information for the prompt layer implementation.
 *
 * @param major Pointer to receive major version number
 * @param minor Pointer to receive minor version number
 * @param patch Pointer to receive patch version number
 *
 * @note All parameters are optional (can be NULL)
 */
void prompt_layer_get_version(int *major, int *minor, int *patch);

/// ============================================================================
/// INTEGRATION HELPERS
/// ============================================================================

/**
 * Generate prompt using existing Lush prompt system
 *
 * Convenience function that integrates with the existing Lush prompt
 * generation system (src/prompt.c) to create content for the layer.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS on success, error code on failure
 *
 * @note Uses existing build_prompt() and theme_generate_primary_prompt()
 * functions
 * @note Automatically sets generated content in the layer
 * @note Preserves all existing prompt generation functionality
 */
prompt_layer_error_t prompt_layer_generate_from_lush(prompt_layer_t *layer);

/**
 * Test prompt layer with sample content
 *
 * Testing helper that validates layer functionality with various
 * prompt formats and theme configurations.
 *
 * @param layer Pointer to prompt_layer_t instance
 * @return PROMPT_LAYER_SUCCESS if all tests pass, error code on failure
 *
 * @note Tests simple, complex, and multiline prompt formats
 * @note Validates performance requirements
 * @note Useful for integration testing and validation
 */
prompt_layer_error_t prompt_layer_run_tests(prompt_layer_t *layer);

#ifdef __cplusplus
}
#endif

#endif /// PROMPT_LAYER_H

/*
 * ============================================================================
 * USAGE EXAMPLES
 * ============================================================================
 *
 * Basic Usage:
 * ```c
 * #include "display/prompt_layer.h"
 * #include "display/layer_events.h"
 *
 * /// Create and initialize
 * layer_events_t *events = layer_events_create();
 * layer_events_init(events);
 *
 * prompt_layer_t *layer = prompt_layer_create();
 * prompt_layer_init(layer, events);
 *
 * /// Generate and render prompt
 * prompt_layer_generate_from_lush(layer);
 *
 * char output[4096];
 * prompt_layer_get_rendered_content(layer, output, sizeof(output));
 * printf("%s", output);
 *
 * /// Cleanup
 * prompt_layer_destroy(layer);
 * layer_events_destroy(events);
 * ```
 *
 * Theme Integration:
 * ```c
 * /// Theme changes are handled automatically through events
 * prompt_layer_update_theme(layer);  /// Manual theme refresh if needed
 *
 * /// Get performance metrics
 * prompt_performance_t perf;
 * prompt_layer_get_performance(layer, &perf);
 * printf("Average render time: %lu ns\n", perf.avg_render_time_ns);
 * ```
 *
 * Custom Content:
 * ```c
 * /// Works with any prompt format
 * prompt_layer_set_content(layer, "$ ");                    /// Simple
 * prompt_layer_set_content(layer, "[user@host ~/path]$ ");  /// Complex
 * prompt_layer_set_content(layer, "U+250C-[user@host]-[~/path]\n+-$ "); ///
 * Multi-line
 *
 * /// Get positioning information
 * prompt_metrics_t metrics;
 * prompt_layer_get_metrics(layer, &metrics);
 * printf("Command starts at column %d, row %d\n",
 *        metrics.estimated_command_column,
 *        metrics.estimated_command_row);
 * ```
 */
