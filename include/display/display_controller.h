/**
 * @file display_controller.h
 * @brief Display controller - High-level display management system
 *
 * Provides high-level coordination of all display layers, system-wide
 * performance monitoring, and intelligent caching for optimal shell
 * integration.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - High-level coordination of all display layers
 * - System-wide performance monitoring and optimization
 * - Display state caching with intelligent diff algorithms
 * - Adaptive performance tuning and resource management
 * - Enterprise-grade configuration management
 * - Preparation for seamless shell integration
 *
 * Design Principles:
 * - Centralized coordination without layer intrusion
 * - Performance optimization across entire display system
 * - Intelligent caching with minimal memory overhead
 * - Adaptive tuning based on real-time performance metrics
 * - Enterprise-ready configuration and monitoring
 * - Seamless integration preparation for existing shell functions
 *
 * Strategic Innovation:
 * This controller completes the revolutionary layered display architecture by
 * providing the high-level coordination needed for enterprise deployment and
 * seamless integration with existing shell functionality.
 */

#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "../lle/completion/completion_menu_state.h"
#include "../lle/notification.h"
#include "autosuggestions_layer.h"

/// Symbol compatibility mode for display rendering
typedef enum {
    SYMBOL_MODE_UNICODE = 0,   ///< Full Unicode symbols
    SYMBOL_MODE_ASCII = 1,     ///< ASCII-only fallback
    SYMBOL_MODE_NERD_FONT = 2, ///< Nerd Font enhanced symbols
    SYMBOL_MODE_AUTO = 3       ///< Auto-detect terminal capability
} symbol_compatibility_t;

#define THEME_NAME_MAX 64
#include "composition_engine.h"
#include "layer_events.h"
#include "terminal_control.h"

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

#define DISPLAY_CONTROLLER_VERSION_MAJOR 1
#define DISPLAY_CONTROLLER_VERSION_MINOR 0
#define DISPLAY_CONTROLLER_VERSION_PATCH 0

/// Performance and caching limits
#define DISPLAY_CONTROLLER_MAX_CACHE_SIZE 32768
#define DISPLAY_CONTROLLER_MAX_DIFF_SIZE 4096
#define DISPLAY_CONTROLLER_DEFAULT_CACHE_TTL_MS 10000
#define DISPLAY_CONTROLLER_PERFORMANCE_HISTORY_SIZE 100

/// Optimization thresholds
#define DISPLAY_CONTROLLER_PERFORMANCE_THRESHOLD_MS 10
#define DISPLAY_CONTROLLER_CACHE_HIT_RATE_THRESHOLD 0.8
#define DISPLAY_CONTROLLER_MEMORY_THRESHOLD_MB 5

/// Configuration defaults
#define DISPLAY_CONTROLLER_DEFAULT_OPTIMIZATION_LEVEL 2
#define DISPLAY_CONTROLLER_DEFAULT_MONITORING_INTERVAL_MS 1000

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Display controller error codes.
 */
typedef enum {
    DISPLAY_CONTROLLER_SUCCESS = 0,
    DISPLAY_CONTROLLER_ERROR_INVALID_PARAM,
    DISPLAY_CONTROLLER_ERROR_NULL_POINTER,
    DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION,
    DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED,
    DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED,
    DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED,
    DISPLAY_CONTROLLER_ERROR_CACHE_FULL,
    DISPLAY_CONTROLLER_ERROR_PERFORMANCE_DEGRADED,
    DISPLAY_CONTROLLER_ERROR_CONFIGURATION_INVALID,
    DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL,
    DISPLAY_CONTROLLER_ERROR_SYSTEM_RESOURCE,
    DISPLAY_CONTROLLER_ERROR_INTEGRATION_FAILED
} display_controller_error_t;

/**
 * Display optimization levels.
 */
typedef enum {
    DISPLAY_OPTIMIZATION_DISABLED = 0,   /// No optimization
    DISPLAY_OPTIMIZATION_BASIC = 1,      /// Basic caching only
    DISPLAY_OPTIMIZATION_STANDARD = 2,   /// Standard optimization (default)
    DISPLAY_OPTIMIZATION_AGGRESSIVE = 3, /// Aggressive optimization
    DISPLAY_OPTIMIZATION_MAXIMUM = 4     /// Maximum performance mode
} display_optimization_level_t;

/**
 * Display state change types for diff algorithms.
 */
typedef enum {
    DISPLAY_STATE_UNCHANGED = 0,
    DISPLAY_STATE_PROMPT_CHANGED,
    DISPLAY_STATE_COMMAND_CHANGED,
    DISPLAY_STATE_COMPOSITION_CHANGED,
    DISPLAY_STATE_TERMINAL_CHANGED,
    DISPLAY_STATE_FULL_REFRESH_NEEDED
} display_state_change_t;

/**
 * System-wide performance metrics.
 */
typedef struct {
    /// Overall system performance
    uint64_t total_display_operations; /// Total display operations
    uint64_t avg_display_time_ns;      /// Average total display time
    uint64_t max_display_time_ns;      /// Maximum display time
    uint64_t min_display_time_ns;      /// Minimum display time

    /// Layer-specific performance
    uint64_t prompt_layer_time_ns;     /// Time in prompt layer
    uint64_t command_layer_time_ns;    /// Time in command layer
    uint64_t composition_time_ns;      /// Time in composition
    uint64_t terminal_control_time_ns; /// Time in terminal control

    /// Caching performance
    uint64_t cache_hits;             /// Display cache hits
    uint64_t cache_misses;           /// Display cache misses
    uint64_t cache_invalidations;    /// Cache invalidation operations
    double cache_hit_rate;           /// Cache hit rate percentage
    size_t cache_memory_usage_bytes; /// Cache memory usage

    /// Optimization metrics
    uint64_t optimization_saves_ns;   /// Time saved by optimizations
    uint64_t diff_operations;         /// Number of diff operations
    uint64_t full_refresh_operations; /// Number of full refreshes

    /// System health
    bool performance_within_threshold; /// Performance is acceptable
    bool memory_within_threshold;      /// Memory usage is acceptable
    bool optimization_effective;       /// Optimizations are helping
} display_controller_performance_t;

/**
 * Display state cache entry.
 */
typedef struct {
    char *display_content;    /// Cached display content
    size_t content_length;    /// Content length
    char *state_hash;         /// State hash for validation
    struct timeval timestamp; /// Cache entry timestamp
    uint32_t access_count;    /// Access frequency counter
    bool is_valid;            /// Cache entry validity
} display_cache_entry_t;

/**
 * Display state diff information.
 */
typedef struct {
    display_state_change_t change_type; /// Type of change detected
    size_t change_start_pos;            /// Start position of change
    size_t change_length;               /// Length of changed content
    char *diff_content;                 /// Differential content
    bool requires_full_refresh;         /// Full refresh needed flag
} display_state_diff_t;

/**
 * Display controller configuration.
 */
typedef struct {
    /// Performance configuration
    display_optimization_level_t optimization_level; /// Optimization level
    uint32_t cache_ttl_ms;                           /// Cache time-to-live
    uint32_t performance_monitor_interval_ms;        /// Monitoring interval
    uint32_t max_cache_entries;                      /// Maximum cache entries

    /// Feature toggles
    bool enable_caching;                /// Enable display caching
    bool enable_diff_algorithms;        /// Enable diff algorithms
    bool enable_performance_monitoring; /// Enable performance monitoring
    bool enable_adaptive_optimization;  /// Enable adaptive optimization
    bool enable_integration_mode;       /// Enable shell integration mode

    /// Threshold configuration
    uint32_t performance_threshold_ms; /// Performance threshold
    double cache_hit_rate_threshold;   /// Cache hit rate threshold
    uint32_t memory_threshold_mb;      /// Memory usage threshold

    /// Debug and diagnostics
    bool enable_debug_logging;         /// Enable debug logging
    bool enable_performance_profiling; /// Enable detailed profiling
    char *log_file_path;               /// Log file path (optional)
} display_controller_config_t;

/**
 * Display controller state structure.
 *
 * This structure represents the complete state of the display controller,
 * including all managed layers, performance metrics, caching state, and
 * configuration. It serves as the central coordination point for the entire
 * layered display architecture.
 */
typedef struct {
    /// Core layer management
    composition_engine_t *compositor;   /// Composition engine instance
    terminal_control_t *terminal_ctrl;  /// Terminal control context
    layer_event_system_t *event_system; /// Event system instance

    /// Display state management
    char *last_display_state;   /// Last complete display state
    size_t last_display_length; /// Length of last display
    char *current_state_hash;   /// Current state hash
    bool display_cache_valid;   /// Display cache validity

    /// Performance monitoring
    display_controller_performance_t performance; /// Performance metrics
    struct timeval last_performance_update; /// Last performance update time
    uint64_t performance_history
        [DISPLAY_CONTROLLER_PERFORMANCE_HISTORY_SIZE]; /// Performance history
    size_t performance_history_index; /// History circular buffer index

    /// Caching system
    display_cache_entry_t *cache_entries; /// Cache entries array
    size_t cache_count;                   /// Current cache count
    size_t cache_capacity;                /// Cache capacity
    struct timeval last_cache_cleanup;    /// Last cache cleanup time

    /// Configuration and optimization
    display_controller_config_t config; /// Controller configuration
    display_optimization_level_t
        current_optimization;           /// Current optimization level
    bool adaptive_optimization_enabled; /// Adaptive optimization state

    /// State tracking
    bool is_initialized;                /// Initialization state
    bool integration_mode_active;       /// Shell integration mode
    struct timeval initialization_time; /// Initialization timestamp
    uint32_t operation_sequence_number; /// Operation sequence counter

    /// Theme context integration
    char current_theme_name[THEME_NAME_MAX]; /// Current active theme name
    symbol_compatibility_t
        current_symbol_mode;        /// Current symbol compatibility mode
    bool theme_context_initialized; /// Theme context initialization state

    /// Completion menu integration (LLE Spec 12 - Proper Architecture)
    lle_completion_menu_state_t
        *active_completion_menu;  /// Active completion menu (NULL if none)
    bool completion_menu_visible; /// Menu visibility state
    bool menu_state_changed;      /// Flag: menu state changed, needs redraw

    /// Autosuggestions integration (Fish-style ghost text)
    autosuggestions_layer_t
        *autosuggestions_layer;   /// Autosuggestions layer (NULL if disabled)
    bool autosuggestions_enabled; /// Whether autosuggestions are active

    /// Notification integration (transient hints below command line)
    /// We store a COPY of the notification, not a pointer, because the source
    /// may be on the stack and get overwritten by intermediate function calls
    lle_notification_state_t notification_copy; /// Copy of notification data
    bool notification_visible; /// Notification visibility state
    bool
        notification_state_changed; /// Flag: notification changed, needs redraw
} display_controller_t;

/// ============================================================================
/// CORE API FUNCTIONS
/// ============================================================================

/**
 * Create a new display controller instance.
 *
 * Allocates and initializes a new display controller structure with default
 * configuration. The controller must be initialized with
 * display_controller_init() before use.
 *
 * @return Pointer to new display controller instance, or NULL on failure
 */
display_controller_t *display_controller_create(void);

/**
 * Initialize the display controller.
 *
 * Initializes the display controller with the provided configuration and sets
 * up all managed layers, performance monitoring, and caching systems. This
 * function must be called before any display operations.
 *
 * @param controller The display controller to initialize
 * @param config Configuration for the controller (NULL for defaults)
 * @param event_system Event system for layer coordination
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_init(display_controller_t *controller,
                        const display_controller_config_t *init_config,
                        layer_event_system_t *event_system);

/**
 * Perform a complete display operation.
 *
 * Coordinates all display layers to produce a complete display output with
 * intelligent caching, diff algorithms, and performance optimization. This
 * is the main high-level function for display operations.
 *
 * @param controller The display controller
 * @param prompt_text Prompt text to display (NULL to use current)
 * @param command_text Command text for syntax highlighting (NULL for none)
 * @param output Buffer to receive complete display output
 * @param output_size Size of output buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_display(display_controller_t *controller,
                           const char *prompt_text, const char *command_text,
                           char *output, size_t output_size);

/**
 * Perform display operation with cursor position tracking and optional terminal
 * control.
 *
 * This function extends display_controller_display() with cursor position
 * tracking and optional terminal control sequence wrapping for LLE integration.
 * It uses the composition engine's incremental cursor tracking (proven approach
 * from Replxx/Fish/ZLE) to calculate cursor screen position, then optionally
 * wraps the output with terminal control sequences for line clearing and cursor
 * positioning.
 *
 * When terminal control wrapping is enabled, the output includes:
 * - Line clear sequence (\r\033[J)
 * - Composed content (prompt + command with syntax highlighting)
 * - Cursor positioning sequence (\033[row;colH)
 *
 * This makes the output "terminal-ready" for LLE, which maintains architectural
 * purity by having NO terminal knowledge.
 *
 * @param controller The display controller
 * @param prompt_text Prompt text to display (NULL to use current)
 * @param command_text Command text for syntax highlighting (NULL for none)
 * @param cursor_byte_offset Cursor position as byte offset in command_text
 * (0-based)
 * @param apply_terminal_control If true, wrap output with terminal control
 * sequences
 * @param output Buffer to receive display output (terminal-ready if wrapping
 * enabled)
 * @param output_size Size of output buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 *
 * @note For GNU Readline: Pass apply_terminal_control=false (handles own
 * terminal control)
 * @note For LLE: Pass apply_terminal_control=true (gets terminal-ready output)
 * @see docs/development/TERMINAL_CONTROL_WRAPPING_DESIGN.md
 */
display_controller_error_t display_controller_display_with_cursor(
    display_controller_t *controller, const char *prompt_text,
    const char *command_text, size_t cursor_byte_offset,
    bool apply_terminal_control, char *output, size_t output_size);

/**
 * Update display with intelligent diff algorithms.
 *
 * Performs an intelligent update of the display using diff algorithms to
 * minimize output and improve performance. Uses caching and state tracking
 * to determine the minimal changes needed.
 *
 * @param controller The display controller
 * @param new_prompt_text New prompt text (NULL to keep current)
 * @param new_command_text New command text (NULL to keep current)
 * @param diff_output Buffer to receive differential output
 * @param output_size Size of output buffer
 * @param change_info Information about detected changes (optional)
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_update(
    display_controller_t *controller, const char *new_prompt_text,
    const char *new_command_text, char *diff_output, size_t output_size,
    display_state_diff_t *change_info);

/**
 * Force a complete display refresh.
 *
 * Forces a complete refresh of the display, bypassing caches and diff
 * algorithms. Useful for recovery from display corruption or when cache
 * invalidation is needed.
 *
 * @param controller The display controller
 * @param output Buffer to receive complete display output
 * @param output_size Size of output buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_refresh(display_controller_t *controller, char *output,
                           size_t output_size);

/**
 * Clean up display controller resources.
 *
 * Cleans up internal resources, clears caches, and shuts down all managed
 * layers. The controller can be reused after calling display_controller_init()
 * again.
 *
 * @param controller The display controller to clean up
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_cleanup(display_controller_t *controller);

/**
 * Clear the terminal screen.
 *
 * Clears the entire terminal screen and repositions the cursor to the home
 * position. This function goes through the terminal_control layer for proper
 * abstraction.
 *
 * @param controller The display controller instance
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_clear_screen(display_controller_t *controller);

/**
 * Destroy display controller instance.
 *
 * Completely destroys the display controller instance, freeing all memory and
 * resources. The controller pointer should not be used after this call.
 *
 * @param controller The display controller to destroy (can be NULL)
 */
void display_controller_destroy(display_controller_t *controller);

/// ============================================================================
/// COMPLETION MENU INTEGRATION (LLE Spec 12 - Proper Architecture)
/// ============================================================================

/**
 * Set active completion menu for display composition.
 *
 * Associates a completion menu with the display controller. The menu will be
 * composed with the command output during rendering. The menu state is NOT
 * owned by the display controller - caller retains ownership and is responsible
 * for lifecycle management.
 *
 * This follows proper architectural layering where display_controller composes
 * multiple display elements (prompt, command, menu) rather than having menu
 * baked into command text.
 *
 * @param controller The display controller
 * @param menu_state Completion menu state (NULL to clear)
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_completion_menu(display_controller_t *controller,
                                       lle_completion_menu_state_t *menu_state);

/**
 * Clear active completion menu.
 *
 * Removes the completion menu from display composition. The next display
 * update will show only prompt and command without menu.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_clear_completion_menu(display_controller_t *controller);

/**
 * Check if completion menu is currently visible.
 *
 * @param controller The display controller
 * @return true if menu is visible, false otherwise
 */
bool display_controller_has_completion_menu(
    const display_controller_t *controller);

/**
 * Get active completion menu state.
 *
 * Returns a pointer to the currently active completion menu, or NULL if
 * no menu is active. The returned pointer is NOT owned by caller.
 *
 * @param controller The display controller
 * @return Pointer to active menu state, or NULL if no menu active
 */
lle_completion_menu_state_t *
display_controller_get_completion_menu(const display_controller_t *controller);

/**
 * Check if menu state changed and clear the flag.
 *
 * This is used by command_layer to determine if it should bypass its early
 * return optimization even when command text hasn't changed. When the menu
 * state changes (menu shown/hidden), we need a redraw even if command is same.
 *
 * @param controller The display controller
 * @return true if menu state changed since last check, false otherwise
 */
bool display_controller_check_and_clear_menu_changed(
    display_controller_t *controller);

/// ============================================================================
/// AUTOSUGGESTIONS INTEGRATION (Fish-style Ghost Text)
/// ============================================================================

/**
 * Update autosuggestion state based on current buffer content.
 *
 * Called after every buffer modification to regenerate suggestions.
 * Clears the suggestion if cursor is not at end of buffer or if
 * completion menu is visible.
 *
 * @param controller The display controller
 * @param buffer_content Current buffer text (may be NULL for empty)
 * @param cursor_position Cursor byte offset in buffer
 * @param buffer_length Total buffer length in bytes
 *
 * @deprecated Use display_controller_set_autosuggestion() instead for LLE
 * history
 */
void display_controller_update_autosuggestion(display_controller_t *controller,
                                              const char *buffer_content,
                                              size_t cursor_position,
                                              size_t buffer_length);

/**
 * Set autosuggestion text directly (for LLE history integration).
 *
 * This function allows lle_readline to set the suggestion text directly
 * after searching LLE history, bypassing the legacy GNU readline history.
 *
 * @param controller The display controller
 * @param suggestion The suggestion text to display (text to append after
 * cursor) Pass NULL or empty string to clear the suggestion
 */
void display_controller_set_autosuggestion(display_controller_t *controller,
                                           const char *suggestion);

/**
 * Get current autosuggestion text.
 *
 * @param controller The display controller
 * @return Current suggestion text or NULL if none available
 *
 * @note Returns internal pointer - do not free or modify
 */
const char *
display_controller_get_autosuggestion(const display_controller_t *controller);

/**
 * Accept current autosuggestion.
 *
 * Retrieves and clears the current suggestion. The caller should
 * insert the returned text into the buffer.
 *
 * @param controller The display controller
 * @param accepted_text Buffer to receive accepted text
 * @param buffer_size Size of accepted_text buffer
 * @return true if suggestion was accepted, false if none available
 *
 * @note Clears the current suggestion after acceptance
 */
bool display_controller_accept_autosuggestion(display_controller_t *controller,
                                              char *accepted_text,
                                              size_t buffer_size);

/**
 * Check if an autosuggestion is currently available.
 *
 * @param controller The display controller
 * @return true if a suggestion is available, false otherwise
 */
bool display_controller_has_autosuggestion(
    const display_controller_t *controller);

/**
 * Clear current autosuggestion.
 *
 * @param controller The display controller
 */
void display_controller_clear_autosuggestion(display_controller_t *controller);

/**
 * Enable or disable autosuggestions.
 *
 * @param controller The display controller
 * @param enabled Whether to enable autosuggestions
 */
void display_controller_set_autosuggestions_enabled(
    display_controller_t *controller, bool enabled);

/// ============================================================================
/// NOTIFICATION INTEGRATION (Transient Hints)
/// ============================================================================

/**
 * Set active notification for display composition.
 *
 * Associates a notification with the display controller. The notification will
 * be composed with the command output during rendering, appearing below the
 * command line (and below completion menu if present).
 *
 * The notification state is NOT owned by the display controller - caller
 * retains ownership and is responsible for lifecycle management.
 *
 * @param controller The display controller
 * @param notification Notification state (NULL to clear)
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_set_notification(
    display_controller_t *controller,
    const lle_notification_state_t *notification);

/**
 * Clear active notification.
 *
 * Removes the notification from display composition. The next display
 * update will show without the notification.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_clear_notification(display_controller_t *controller);

/**
 * Check if notification is currently visible.
 *
 * @param controller The display controller
 * @return true if notification is visible, false otherwise
 */
bool display_controller_has_notification(
    const display_controller_t *controller);

/**
 * Check if notification state changed and clear the flag.
 *
 * This is used to determine if a redraw is needed due to notification
 * state changes.
 *
 * @param controller The display controller
 * @return true if notification state changed since last check, false otherwise
 */
bool display_controller_check_and_clear_notification_changed(
    display_controller_t *controller);

/// ============================================================================
/// PERFORMANCE AND MONITORING FUNCTIONS
/// ============================================================================

/**
 * Get system-wide performance metrics.
 *
 * Retrieves comprehensive performance metrics for the entire display system,
 * including all layers, caching effectiveness, and optimization impact.
 *
 * @param controller The display controller
 * @param performance Buffer to receive performance metrics
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_get_performance(
    const display_controller_t *controller,
    display_controller_performance_t *performance);

/**
 * Update performance monitoring.
 *
 * Updates performance monitoring with current metrics and triggers adaptive
 * optimization if enabled. This function is called automatically during
 * display operations but can be called manually for real-time monitoring.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_update_performance_monitoring(
    display_controller_t *controller);

/**
 * Reset performance metrics.
 *
 * Resets all performance metrics and history to baseline values. Useful for
 * performance testing and benchmarking.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_reset_performance_metrics(display_controller_t *controller);

/// ============================================================================
/// CACHING AND OPTIMIZATION FUNCTIONS
/// ============================================================================

/**
 * Configure optimization level.
 *
 * Sets the optimization level for the display controller, affecting caching
 * aggressiveness, diff algorithm complexity, and performance monitoring detail.
 *
 * @param controller The display controller
 * @param level Optimization level to set
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_optimization_level(display_controller_t *controller,
                                          display_optimization_level_t level);

/**
 * Enable or disable adaptive optimization.
 *
 * Controls whether the display controller automatically adjusts optimization
 * based on real-time performance metrics and system conditions.
 *
 * @param controller The display controller
 * @param enable True to enable adaptive optimization, false to disable
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_adaptive_optimization(display_controller_t *controller,
                                             bool enable);

/**
 * Clear display cache.
 *
 * Clears all cached display states and forces fresh composition on next
 * display operation. Useful for troubleshooting or memory management.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_clear_cache(display_controller_t *controller);

/**
 * Validate cache integrity.
 *
 * Validates the integrity of the display cache, checking for corruption,
 * expired entries, and cache effectiveness metrics.
 *
 * @param controller The display controller
 * @param valid_entries Number of valid cache entries (optional)
 * @param expired_entries Number of expired cache entries (optional)
 * @param corruption_detected Whether cache corruption was detected (optional)
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_validate_cache(
    display_controller_t *controller, size_t *valid_entries,
    size_t *expired_entries, bool *corruption_detected);

/**
 * Optimize cache performance.
 *
 * Performs cache optimization including cleanup of expired entries,
 * defragmentation, and access pattern analysis for improved performance.
 *
 * @param controller The display controller
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_optimize_cache(display_controller_t *controller);

/// ============================================================================
/// CONFIGURATION AND STATE FUNCTIONS
/// ============================================================================

/**
 * Get current configuration.
 *
 * Retrieves the current configuration of the display controller, including
 * all settings, thresholds, and feature toggles.
 *
 * @param controller The display controller
 * @param config Buffer to receive current configuration
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_get_config(const display_controller_t *controller,
                              display_controller_config_t *config);

/**
 * Update configuration.
 *
 * Updates the display controller configuration with new settings. Changes
 * take effect immediately and may trigger cache invalidation or optimization
 * level changes.
 *
 * @param controller The display controller
 * @param config New configuration to apply
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_config(display_controller_t *controller,
                              const display_controller_config_t *config);

/**
 * Enable or disable integration mode.
 *
 * Controls integration mode for seamless operation with existing shell
 * functions. When enabled, the controller optimizes for shell integration
 * and provides compatibility interfaces.
 *
 * @param controller The display controller
 * @param enable True to enable integration mode, false to disable
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_integration_mode(display_controller_t *controller,
                                        bool enable);

/**
 * Check if controller is initialized.
 *
 * Checks whether the display controller has been properly initialized and
 * is ready for display operations.
 *
 * @param controller The display controller
 * @return True if initialized, false otherwise
 */
bool display_controller_is_initialized(const display_controller_t *controller);

/**
 * Set theme context for the display controller.
 *
 * Updates the display controller's theme context including theme name and
 * symbol compatibility mode. This ensures theme-aware cache key generation and
 * proper cache invalidation when themes change.
 *
 * @param controller The display controller instance
 * @param theme_name The name of the currently active theme
 * @param symbol_mode The current symbol compatibility mode
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_set_theme_context(display_controller_t *controller,
                                     const char *theme_name,
                                     symbol_compatibility_t symbol_mode);

/**
 * Get controller version information.
 *
 * Retrieves version information for the display controller including version
 * numbers and build information.
 *
 * @param controller The display controller
 * @param version_buffer Buffer to receive version string
 * @param buffer_size Size of version buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_get_version(const display_controller_t *controller,
                               char *version_buffer, size_t buffer_size);

/// ============================================================================
/// INTEGRATION PREPARATION FUNCTIONS
/// ============================================================================

/**
 * Prepare for shell integration.
 *
 * Prepares the display controller for integration with existing shell functions
 * by setting up compatibility interfaces and optimization profiles suitable
 * for shell operation.
 *
 * @param controller The display controller
 * @param shell_config Shell-specific configuration (optional)
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_prepare_shell_integration(display_controller_t *controller,
                                             const void *shell_config);

/**
 * Get integration interface.
 *
 * Retrieves the integration interface structure that provides compatibility
 * functions for seamless integration with existing shell display functions.
 *
 * @param controller The display controller
 * @param interface_buffer Buffer to receive interface structure
 * @param buffer_size Size of interface buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_get_integration_interface(
    const display_controller_t *controller, void *interface_buffer,
    size_t buffer_size);

/// ============================================================================
/// UTILITY AND DIAGNOSTIC FUNCTIONS
/// ============================================================================

/**
 * Get error description string.
 *
 * Converts a display controller error code to a human-readable description.
 *
 * @param error The error code to describe
 * @return String description of error (never NULL)
 */
const char *display_controller_error_string(display_controller_error_t error);

/**
 * Generate diagnostic report.
 *
 * Generates a comprehensive diagnostic report including performance metrics,
 * cache statistics, configuration settings, and system health information.
 *
 * @param controller The display controller
 * @param report_buffer Buffer to receive diagnostic report
 * @param buffer_size Size of report buffer
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t display_controller_generate_diagnostic_report(
    const display_controller_t *controller, char *report_buffer,
    size_t buffer_size);

/**
 * Create default configuration.
 *
 * Creates a default configuration structure with recommended settings for
 * typical shell operation. Can be used as a starting point for customization.
 *
 * @param config Buffer to receive default configuration
 * @return DISPLAY_CONTROLLER_SUCCESS on success, error code on failure
 */
display_controller_error_t
display_controller_create_default_config(display_controller_config_t *config);

/**
 * Get terminal control instance from display controller.
 *
 * Provides access to the terminal control layer managed by the display
 * controller. This is used by other layers that need direct terminal
 * interaction capabilities.
 *
 * @param controller The display controller instance
 * @return Pointer to terminal control instance, or NULL if not
 * available/initialized
 *
 * @note The returned pointer is owned by the display controller and should not
 * be freed
 * @note Returns NULL if controller is NULL or not properly initialized
 */
terminal_control_t *
display_controller_get_terminal_control(const display_controller_t *controller);

/**
 * Get event system instance from display controller.
 *
 * Provides access to the layer event system managed by the display controller.
 * This is used by layers that need to participate in the event-driven
 * architecture.
 *
 * @param controller The display controller instance
 * @return Pointer to event system instance, or NULL if not
 * available/initialized
 *
 * @note The returned pointer is owned by the display controller and should not
 * be freed
 * @note Returns NULL if controller is NULL or not properly initialized
 */
layer_event_system_t *
display_controller_get_event_system(const display_controller_t *controller);

/**
 * Reset prompt display state - call when starting a new input session.
 *
 * This resets internal state tracking that prevents redundant prompt redraws.
 * Should be called before each readline/input session to ensure the prompt
 * is drawn fresh for the new command.
 */
void dc_reset_prompt_display_state(void);

/**
 * @brief Finalize input and prepare for command output
 *
 * Called when user presses Enter to accept input. Moves cursor to next line
 * so command output appears below the input, then resets display state.
 *
 * This ensures proper separation between input and output, and clears any
 * accumulated state from wrapped lines or cursor movements during editing.
 */
void dc_finalize_input(void);

/**
 * @brief Get prompt display metrics for transient prompt support
 *
 * Returns the number of lines the prompt occupies and the total display
 * lines (prompt + command). Used by the transient prompt system to know
 * how many lines to clear and replace.
 *
 * @param prompt_lines    Output: number of lines the prompt occupies (1+)
 * @param total_lines     Output: total display lines including command
 * @param command_col     Output: column where command starts on last prompt
 * line
 */
void dc_get_prompt_metrics(int *prompt_lines, int *total_lines,
                           int *command_col);

/**
 * @brief Apply transient prompt replacement through display system
 *
 * Replaces the current prompt with a transient (simplified) prompt while
 * preserving the command text. This is called from the LINE_ACCEPTED hook,
 * before dc_finalize_input(), to simplify prompts in scrollback history.
 *
 * The function:
 * 1. Re-renders the screen buffer with the transient prompt + same command
 * 2. Uses the display diff system to output minimal escape sequences
 * 3. Properly handles multi-line prompts collapsing to single line
 *
 * This approach respects the LLE architecture by going through the screen
 * buffer rather than writing directly to the terminal.
 *
 * @param transient_prompt  The simplified prompt string (e.g., "❯ ")
 * @param command_text      The command text to preserve
 * @return true on success, false on failure
 */
bool dc_apply_transient_prompt(const char *transient_prompt,
                               const char *command_text);

#ifdef __cplusplus
}
#endif

#endif /// DISPLAY_CONTROLLER_H
