/**
 * @file composition_engine.h
 * @brief Composition engine - Intelligent layer combination system
 *
 * Intelligently combines prompt and command layers without interference,
 * enabling universal prompt compatibility with real-time syntax highlighting.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Intelligent combination of prompt and command layers
 * - Universal prompt structure analysis (non-invasive)
 * - Smart positioning and conflict resolution
 * - High-performance composition with intelligent caching
 * - Memory-safe composition management
 * - Event-driven coordination between layers
 *
 * Design Principles:
 * - No modification of existing layer outputs
 * - Universal compatibility with any prompt structure
 * - Intelligent positioning without hardcoded assumptions
 * - Performance-optimized composition algorithms
 * - Memory-safe with proper resource management
 * - Event-driven communication with all layers
 *
 * Strategic Innovation:
 * This engine completes the revolutionary shell display technology by enabling
 * the universal combination of professional themes with real-time syntax
 * highlighting, making Lush the first shell to achieve this capability.
 */

#ifndef COMPOSITION_ENGINE_H
#define COMPOSITION_ENGINE_H

#include "command_layer.h"
#include "layer_events.h"
#include "prompt_layer.h"
#include "screen_buffer.h"

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

#define COMPOSITION_ENGINE_VERSION_MAJOR 1
#define COMPOSITION_ENGINE_VERSION_MINOR 0
#define COMPOSITION_ENGINE_VERSION_PATCH 0

/// Composition content limits
#define COMPOSITION_ENGINE_MAX_OUTPUT_SIZE                                     \
    65536 /// Increased for complex prompts
#define COMPOSITION_ENGINE_MAX_LINES 64
#define COMPOSITION_ENGINE_MAX_LINE_WIDTH 1024

/// Performance targets
#define COMPOSITION_ENGINE_TARGET_COMPOSE_TIME_MS 5
#define COMPOSITION_ENGINE_CACHE_EXPIRY_MS 50

/// Cache configuration
#define COMPOSITION_ENGINE_CACHE_SIZE 32
#define COMPOSITION_ENGINE_METRICS_HISTORY_SIZE 32

/// Composition algorithm configuration
#define COMPOSITION_ENGINE_MAX_PROMPT_LINES 32
#define COMPOSITION_ENGINE_MAX_COMMAND_LINES 8
#define COMPOSITION_ENGINE_ANALYSIS_BUFFER_SIZE 2048

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Error codes for composition engine operations
 */
typedef enum {
    COMPOSITION_ENGINE_SUCCESS = 0,         /// Operation completed successfully
    COMPOSITION_ENGINE_ERROR_INVALID_PARAM, /// Invalid parameter provided
    COMPOSITION_ENGINE_ERROR_NULL_POINTER,  /// NULL pointer passed
    COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION,  /// Memory allocation failed
    COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL,   /// Output buffer insufficient
    COMPOSITION_ENGINE_ERROR_CONTENT_TOO_LARGE,  /// Content exceeds limits
    COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY,    /// Layer not ready for
                                                 /// composition
    COMPOSITION_ENGINE_ERROR_ANALYSIS_FAILED,    /// Prompt structure analysis
                                                 /// failed
    COMPOSITION_ENGINE_ERROR_COMPOSITION_FAILED, /// Composition algorithm
                                                 /// failed
    COMPOSITION_ENGINE_ERROR_CACHE_INVALID,      /// Cache state is invalid
    COMPOSITION_ENGINE_ERROR_EVENT_FAILED,       /// Event handling failed
    COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED     /// Engine not initialized
} composition_engine_error_t;

/**
 * Composition strategy for different prompt types
 */
typedef enum {
    COMPOSITION_STRATEGY_SIMPLE = 0, /// Simple single-line prompts
    COMPOSITION_STRATEGY_MULTILINE,  /// Multi-line prompts
    COMPOSITION_STRATEGY_COMPLEX,    /// Complex structured prompts
    COMPOSITION_STRATEGY_ASCII_ART,  /// ASCII art prompts
    COMPOSITION_STRATEGY_ADAPTIVE,   /// Adaptive strategy selection
    COMPOSITION_STRATEGY_COUNT       /// Number of strategies
} composition_strategy_t;

/**
 * Prompt structure analysis results
 */
typedef struct {
    size_t line_count;       /// Number of prompt lines
    size_t max_line_width;   /// Maximum line width
    size_t last_line_length; /// Length of last line
    size_t cursor_column;    /// Cursor column position
    size_t cursor_line;      /// Cursor line position
    bool has_trailing_space; /// Prompt ends with space
    bool is_multiline;       /// Multi-line prompt
    bool has_ansi_sequences; /// Contains ANSI codes
    bool is_ascii_art;       /// ASCII art style prompt
    composition_strategy_t
        recommended_strategy;    /// Recommended composition strategy
    char last_line_content[512]; /// Content of last line
} composition_analysis_t;

/**
 * Composition positioning information
 */
typedef struct {
    size_t prompt_start_line;      /// Prompt starting line
    size_t prompt_end_line;        /// Prompt ending line
    size_t command_start_line;     /// Command starting line
    size_t command_start_column;   /// Command starting column
    size_t total_lines;            /// Total composition lines
    size_t total_width;            /// Total composition width
    bool command_on_same_line;     /// Command on same line as prompt
    bool needs_cursor_positioning; /// Requires cursor repositioning
} composition_positioning_t;

/**
 * Composition performance metrics
 */
typedef struct {
    uint64_t composition_count;       /// Number of compositions performed
    uint64_t cache_hits;              /// Cache hit count
    uint64_t cache_misses;            /// Cache miss count
    uint64_t avg_composition_time_ns; /// Average composition time
    uint64_t max_composition_time_ns; /// Maximum composition time
    uint64_t min_composition_time_ns; /// Minimum composition time
    uint64_t analysis_time_ns;        /// Time spent on analysis
    uint64_t combination_time_ns;     /// Time spent on combination
    double cache_hit_rate;            /// Cache hit rate percentage
} composition_performance_t;

/**
 * Composition cache entry
 */
typedef struct {
    char prompt_hash[32];                         /// Prompt content hash
    char command_hash[32];                        /// Command content hash
    char *cached_output;                          /// Cached composition output
    composition_analysis_t cached_analysis;       /// Cached analysis
    composition_positioning_t cached_positioning; /// Cached positioning
    struct timeval timestamp;                     /// Cache entry timestamp
    bool valid;                                   /// Cache entry validity
} composition_cache_entry_t;

/**
 * Composition engine main structure
 */
typedef struct {
    /// Layer references
    prompt_layer_t *prompt_layer;       /// Prompt layer instance
    command_layer_t *command_layer;     /// Command layer instance
    layer_event_system_t *event_system; /// Event system instance
    screen_buffer_t *screen_buffer;     /// Screen buffer for rendering

    /// Continuation prompts handled via screen_buffer line prefixes
    /// in display_controller.c using screen_buffer_render_with_continuation()
    bool continuation_prompts_enabled; /// Enable continuation prompts

    /// Current composition state
    char *composed_output;                         /// Current composed output
    size_t composed_output_size;                   /// Size of composed output
    composition_analysis_t current_analysis;       /// Current prompt analysis
    composition_positioning_t current_positioning; /// Current positioning
    composition_strategy_t current_strategy; /// Current composition strategy

    /// Performance and caching
    composition_cache_entry_t
        cache[COMPOSITION_ENGINE_CACHE_SIZE]; /// Composition cache
    composition_performance_t performance;    /// Performance metrics
    bool composition_cache_valid;             /// Cache validity flag
    struct timeval last_composition_time;     /// Last composition timestamp

    /// Configuration
    bool intelligent_positioning; /// Enable intelligent positioning
    bool adaptive_strategy;       /// Enable adaptive strategy selection
    bool performance_monitoring;  /// Enable performance monitoring
    size_t max_cache_age_ms;      /// Maximum cache age in milliseconds

    /// Event handling
    bool event_subscription_active; /// Event subscription status
    uint32_t event_subscriber_id;   /// Event subscriber ID

    /// Internal state
    bool initialized;        /// Initialization status
    char version_string[32]; /// Version information
} composition_engine_t;

/// ============================================================================
/// CORE API FUNCTIONS
/// ============================================================================

/**
 * Create a new composition engine instance.
 *
 * Allocates and initializes a new composition engine structure with default
 * configuration. The engine must be initialized with composition_engine_init()
 * before use.
 *
 * @return Pointer to new composition engine instance, or NULL on failure
 */
composition_engine_t *composition_engine_create(void);

/**
 * Initialize the composition engine.
 *
 * Initializes the composition engine with the specified layers and event
 * system. Sets up event subscriptions, initializes caching, and prepares the
 * engine for composition operations.
 *
 * @param engine The composition engine to initialize
 * @param prompt_layer The prompt layer to compose with
 * @param command_layer The command layer to compose with
 * @param event_system The event system for layer communication
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t composition_engine_init(
    composition_engine_t *engine, prompt_layer_t *prompt_layer,
    command_layer_t *command_layer, layer_event_system_t *event_system);

/**
 * Perform intelligent composition of prompt and command layers.
 *
 * Analyzes the current prompt structure, determines optimal composition
 * strategy, and combines prompt and command layers without interference. Uses
 * intelligent caching to optimize performance.
 *
 * @param engine The composition engine
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_compose(composition_engine_t *engine);

/**
 * Get the composed output from the engine.
 *
 * Returns the current composed output combining prompt and command layers.
 * The output includes proper ANSI sequences and positioning for display.
 *
 * @param engine The composition engine
 * @param output Buffer to store composed output
 * @param output_size Size of output buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_get_output(const composition_engine_t *engine, char *output,
                              size_t output_size);

/**
 * Get the current prompt analysis results.
 *
 * Returns detailed analysis of the current prompt structure, including
 * line count, positioning information, and recommended composition strategy.
 *
 * @param engine The composition engine
 * @param analysis Pointer to store analysis results
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_get_analysis(const composition_engine_t *engine,
                                composition_analysis_t *analysis);

/**
 * Get the current composition positioning information.
 *
 * Returns detailed positioning information for the current composition,
 * including line and column positions for prompt and command elements.
 *
 * @param engine The composition engine
 * @param positioning Pointer to store positioning results
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_get_positioning(const composition_engine_t *engine,
                                   composition_positioning_t *positioning);

/**
 * Get composition performance metrics.
 *
 * Returns detailed performance metrics including composition times,
 * cache statistics, and efficiency measurements.
 *
 * @param engine The composition engine
 * @param performance Pointer to store performance metrics
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_get_performance(const composition_engine_t *engine,
                                   composition_performance_t *performance);

/**
 * Clean up composition engine resources.
 *
 * Cleans up internal resources, clears caches, and unsubscribes from events.
 * The engine can be reused after calling composition_engine_init() again.
 *
 * @param engine The composition engine to clean up
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_cleanup(composition_engine_t *engine);

/**
 * Destroy a composition engine instance.
 *
 * Performs cleanup and frees all memory associated with the composition engine.
 * The engine pointer becomes invalid after this call.
 *
 * @param engine The composition engine to destroy (can be NULL)
 */
void composition_engine_destroy(composition_engine_t *engine);

/// ============================================================================
/// CONFIGURATION AND CONTROL FUNCTIONS
/// ============================================================================

/**
 * Set composition strategy.
 *
 * Sets the composition strategy to use for combining layers. Use
 * COMPOSITION_STRATEGY_ADAPTIVE for automatic strategy selection.
 *
 * @param engine The composition engine
 * @param strategy The composition strategy to use
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_set_strategy(composition_engine_t *engine,
                                composition_strategy_t strategy);

/**
 * Enable or disable intelligent positioning.
 *
 * Controls whether the engine uses intelligent positioning algorithms
 * to determine optimal command placement relative to prompt structure.
 *
 * @param engine The composition engine
 * @param enable True to enable intelligent positioning, false to disable
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_set_intelligent_positioning(composition_engine_t *engine,
                                               bool enable);

/**
 * Enable or disable performance monitoring.
 *
 * Controls whether the engine collects detailed performance metrics
 * during composition operations.
 *
 * @param engine The composition engine
 * @param enable True to enable monitoring, false to disable
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_set_performance_monitoring(composition_engine_t *engine,
                                              bool enable);

/**
 * Set maximum cache age.
 *
 * Sets the maximum age for cache entries in milliseconds. Older entries
 * will be automatically expired and regenerated.
 *
 * @param engine The composition engine
 * @param max_age_ms Maximum cache age in milliseconds
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_set_cache_max_age(composition_engine_t *engine,
                                     size_t max_age_ms);

/**
 * Clear composition cache.
 *
 * Clears all cached composition results, forcing regeneration on next
 * composition operation.
 *
 * @param engine The composition engine
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_clear_cache(composition_engine_t *engine);

/// ============================================================================
/// CONTINUATION PROMPT SUPPORT (Phase 4)
/// ============================================================================

/**
 * Set screen buffer.
 *
 * Associates a screen buffer with the composition engine for multiline
 * rendering with continuation prompts.
 *
 * @param engine The composition engine
 * @param buffer The screen buffer instance
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_set_screen_buffer(composition_engine_t *engine,
                                     screen_buffer_t *buffer);

/// ============================================================================
/// ANALYSIS AND DEBUGGING FUNCTIONS
/// ============================================================================

/**
 * Analyze prompt structure.
 *
 * Performs detailed analysis of the current prompt structure without
 * performing composition. Useful for debugging and optimization.
 *
 * @param engine The composition engine
 * @param analysis Pointer to store analysis results
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_analyze_prompt(composition_engine_t *engine,
                                  composition_analysis_t *analysis);

/**
 * Calculate optimal positioning.
 *
 * Calculates optimal positioning for command layer relative to prompt
 * structure without performing composition.
 *
 * @param engine The composition engine
 * @param analysis Prompt analysis to base positioning on
 * @param positioning Pointer to store positioning results
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t composition_engine_calculate_positioning(
    composition_engine_t *engine, const composition_analysis_t *analysis,
    composition_positioning_t *positioning);

/**
 * Validate composition cache.
 *
 * Validates all cache entries and removes expired or invalid entries.
 * Returns cache statistics after validation.
 *
 * @param engine The composition engine
 * @param valid_entries Pointer to store count of valid entries (can be NULL)
 * @param expired_entries Pointer to store count of expired entries (can be
 * NULL)
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_validate_cache(composition_engine_t *engine,
                                  size_t *valid_entries,
                                  size_t *expired_entries);

/**
 * Get composition engine version.
 *
 * Returns the version string for the composition engine.
 *
 * @param engine The composition engine
 * @param version_buffer Buffer to store version string
 * @param buffer_size Size of version buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_get_version(const composition_engine_t *engine,
                               char *version_buffer, size_t buffer_size);

/**
 * Check if composition engine is initialized.
 *
 * Returns whether the composition engine has been properly initialized
 * and is ready for composition operations.
 *
 * @param engine The composition engine to check
 * @return True if initialized, false otherwise
 */
bool composition_engine_is_initialized(const composition_engine_t *engine);

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * Convert error code to string.
 *
 * Returns a human-readable string description of the given error code.
 *
 * @param error The error code to convert
 * @return String description of error (never NULL)
 */
const char *composition_engine_error_string(composition_engine_error_t error);

/**
 * Convert composition strategy to string.
 *
 * Returns a human-readable string description of the given composition
 * strategy.
 *
 * @param strategy The composition strategy to convert
 * @return String description of strategy (never NULL)
 */
const char *composition_engine_strategy_string(composition_strategy_t strategy);

/**
 * Calculate composition hash.
 *
 * Calculates a hash value for the current prompt and command content
 * for use in caching and change detection.
 *
 * @param engine The composition engine
 * @param hash_buffer Buffer to store hash string
 * @param buffer_size Size of hash buffer
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 */
composition_engine_error_t
composition_engine_calculate_hash(const composition_engine_t *engine,
                                  char *hash_buffer, size_t buffer_size);

/// ============================================================================
/// EVENT HANDLING FUNCTIONS (Internal)
/// ============================================================================

/**
 * Handle layer content changed event.
 *
 * Internal function to handle layer content change events from prompt
 * or command layers. Invalidates cache and triggers recomposition.
 *
 * @param event The layer event
 * @param user_data The composition engine (cast from void*)
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
composition_engine_handle_content_changed(const layer_event_t *event,
                                          void *user_data);

/**
 * Handle theme changed event.
 *
 * Internal function to handle theme change events. Invalidates cache
 * and triggers recomposition with new theme.
 *
 * @param event The layer event
 * @param user_data The composition engine (cast from void*)
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
composition_engine_handle_theme_changed(const layer_event_t *event,
                                        void *user_data);

/**
 * Handle terminal resize event.
 *
 * Internal function to handle terminal resize events. Recalculates
 * positioning and triggers recomposition.
 *
 * @param event The layer event
 * @param user_data The composition engine (cast from void*)
 * @return LAYER_EVENTS_SUCCESS on success, error code on failure
 */
layer_events_error_t
composition_engine_handle_terminal_resize(const layer_event_t *event,
                                          void *user_data);

/// ============================================================================
/// CURSOR TRACKING API (For LLE Terminal Control Wrapping)
/// ============================================================================

/**
 * Composition result with cursor position tracking.
 *
 * This structure extends the basic composition output with cursor position
 * information calculated using incremental tracking during composition.
 * This is required for LLE to get terminal-ready output with correct cursor
 * positioning that handles line wrapping, UTF-8, ANSI codes, and tabs.
 */
typedef struct {
    char composed_output[COMPOSITION_ENGINE_MAX_OUTPUT_SIZE]; /// Composed
                                                              /// content
    size_t cursor_screen_row;    /// Cursor row (0-based)
    size_t cursor_screen_column; /// Cursor column (0-based)
    bool cursor_found;           /// Cursor position was calculated
    size_t terminal_width;       /// Terminal width used for wrapping
} composition_with_cursor_t;

/**
 * Compose layers with cursor position tracking.
 *
 * This function performs composition while tracking the screen position of
 * a cursor at a specific byte offset in the command buffer. It uses incremental
 * cursor tracking (the proven approach from Replxx/Fish/ZLE) to handle:
 * - Multi-byte UTF-8 characters (correct visual width)
 * - Wide characters (CJK - 2 columns)
 * - ANSI escape sequences (0 columns)
 * - Tab expansion (to next multiple of 8)
 * - Line wrapping (when content exceeds terminal width)
 *
 * The cursor position is calculated by walking through the command buffer
 * character-by-character during composition, tracking the visual (x, y)
 * position. When the byte offset matches cursor_byte_offset, the current
 * position is recorded.
 *
 * This is the architecturally correct approach for LLE integration:
 * - LLE passes cursor as byte offset in its buffer
 * - Composition engine calculates screen position during rendering
 * - Display controller uses pre-calculated position for terminal sequences
 * - LLE never needs terminal knowledge (maintains architectural purity)
 *
 * @param engine The composition engine
 * @param cursor_byte_offset Cursor position as byte offset in command text
 * (0-based)
 * @param terminal_width Terminal width for line wrapping calculation
 * @param result Output structure with composed content and cursor position
 * @return COMPOSITION_ENGINE_SUCCESS on success, error code on failure
 *
 * @note This is the CORRECT approach researched from modern line editors
 * @note Do NOT use division/modulo formulas (breaks on UTF-8/ANSI/tabs/wide
 * chars)
 * @see docs/development/TERMINAL_CONTROL_WRAPPING_DESIGN.md
 * @see docs/development/MODERN_EDITOR_WRAPPING_RESEARCH.md
 */
composition_engine_error_t composition_engine_compose_with_cursor(
    composition_engine_t *engine, size_t cursor_byte_offset, int terminal_width,
    composition_with_cursor_t *result);

#ifdef __cplusplus
}
#endif

#endif /// COMPOSITION_ENGINE_H
