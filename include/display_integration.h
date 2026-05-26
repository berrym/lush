/**
 * @file display_integration.h
 * @brief Display controller integration wrapper for shell display functions
 *
 * Defines the API for integrating the layered display controller with existing
 * shell display functions. Provides seamless function replacement with backward
 * compatibility, configuration management, and performance monitoring.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Seamless function replacement with zero regression
 * - Configuration-based enable/disable of layered display
 * - Performance monitoring integration with shell diagnostics
 * - Enterprise deployment readiness with configuration management
 * - Graceful fallback to existing display functions
 * - Resource management and memory safety
 * - Professional safety infrastructure with comprehensive error handling
 * - Incremental integration with fallback tracking and diagnostics
 *
 * Integration Functions:
 * - display_integration_redisplay() replaces lush_safe_redisplay()
 * - display_integration_prompt_update() replaces lush_prompt_update()
 * - display_integration_clear_screen() replaces lush_clear_screen()
 *
 * Strategic Benefits:
 * - Zero regression deployment of layered display architecture
 * - Enterprise-grade configuration and monitoring capabilities
 * - Production-ready performance optimization and caching
 * - Seamless user experience with enhanced display capabilities
 */

#ifndef DISPLAY_INTEGRATION_H
#define DISPLAY_INTEGRATION_H

#include "display/display_controller.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND CONFIGURATION
 * ============================================================================
 */

#define DISPLAY_INTEGRATION_VERSION_MAJOR 1
#define DISPLAY_INTEGRATION_VERSION_MINOR 0
#define DISPLAY_INTEGRATION_VERSION_PATCH 0

/// Buffer and limit constants
#define DISPLAY_INTEGRATION_MAX_OUTPUT_SIZE 16384
#define DISPLAY_INTEGRATION_MAX_CONFIG_STRING 256

/// Default configuration values
#define DISPLAY_INTEGRATION_DEFAULT_PERFORMANCE_THRESHOLD_MS 20
#define DISPLAY_INTEGRATION_DEFAULT_CACHE_HIT_RATE_THRESHOLD 0.8
#define DISPLAY_INTEGRATION_DEFAULT_OPTIMIZATION_LEVEL                         \
    DISPLAY_OPTIMIZATION_BALANCED

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * Fallback reason enumeration for safety infrastructure.
 * Tracks why layered display operations fall back to standard functions.
 */
typedef enum {
    INTEGRATION_FALLBACK_NONE = 0, ///< No fallback - layered display succeeded
    INTEGRATION_FALLBACK_CONTROLLER_NULL,  ///< Display controller is null
    INTEGRATION_FALLBACK_CONTROLLER_ERROR, ///< Display controller returned
                                           ///< error
    INTEGRATION_FALLBACK_BUFFER_ERROR,     ///< Buffer allocation or size error
    INTEGRATION_FALLBACK_TIMEOUT, ///< Operation exceeded timeout threshold
    /**
     * User explicitly disabled layered display
     */
    INTEGRATION_FALLBACK_USER_REQUEST,
    INTEGRATION_FALLBACK_SAFETY_CHECK, ///< Safety check failed
    INTEGRATION_FALLBACK_MEMORY_ERROR, ///< Memory allocation failure
    /**
     * System not properly initialized
     */
    INTEGRATION_FALLBACK_INITIALIZATION_ERROR,
    INTEGRATION_FALLBACK_RECURSION_PROTECTION ///< Recursion protection
                                              ///< triggered
} integration_fallback_reason_t;

/**
 * Display integration health status enumeration.
 */
typedef enum {
    /**
     * Operating perfectly with layered display
     */
    DISPLAY_INTEGRATION_HEALTH_EXCELLENT = 0,
    DISPLAY_INTEGRATION_HEALTH_GOOD = 1, ///< Operating well with minor issues
    /**
     * Performance issues or high fallback rate
     */
    DISPLAY_INTEGRATION_HEALTH_DEGRADED = 2,
    DISPLAY_INTEGRATION_HEALTH_ERROR = 3, ///< Error in layered display system
    /**
     * Layered display intentionally disabled
     */
    DISPLAY_INTEGRATION_HEALTH_DISABLED = 4,
    /**
     * Integration system not initialized
     */
    DISPLAY_INTEGRATION_HEALTH_NOT_INITIALIZED = 5
} display_integration_health_t;

/**
 * Display integration configuration structure.
 * Controls all aspects of layered display integration behavior.
 */
typedef struct {
    /// Core feature enables - v1.3.0: Layered display is now exclusive system
    bool enable_caching; ///< Enable display caching for performance
    bool enable_performance_monitoring; ///< Enable performance tracking and
                                        ///< metrics

    /// Optimization settings
    display_optimization_level_t
        optimization_level; ///< Performance optimization level
    uint32_t
        performance_threshold_ms; ///< Performance threshold for health checks
    double cache_hit_rate_threshold; ///< Cache hit rate threshold for health

    /// Behavior settings
    bool fallback_on_error; ///< Fallback to original functions on error
    bool debug_mode;        ///< Enable debug output and logging
    size_t max_output_size; ///< Maximum size for display output buffers

    /// Enterprise deployment settings
    char deployment_mode
        [DISPLAY_INTEGRATION_MAX_CONFIG_STRING]; ///< Deployment mode identifier
    char environment_name
        [DISPLAY_INTEGRATION_MAX_CONFIG_STRING]; ///< Environment
                                                 ///< name
    bool enable_enterprise_logging; ///< Enable enterprise-grade logging
    bool strict_compatibility_mode; ///< Strict backward compatibility mode
} display_integration_config_t;

/**
 * Display integration performance statistics.
 * Tracks usage patterns and performance metrics for monitoring and
 * optimization.
 */
typedef struct {
    /// Usage statistics
    time_t init_time;               ///< Time when integration was initialized
    uint64_t total_display_calls;   ///< Total number of display function calls
    uint64_t layered_display_calls; ///< Calls handled by layered display
    uint64_t fallback_calls; ///< Calls that fell back to original functions

    /// Performance metrics
    uint64_t avg_layered_display_time_ns; ///< Average time for layered display
                                          ///< operations
    double cache_hit_rate;                ///< Current cache hit rate (0.0-1.0)
    size_t memory_usage_bytes; ///< Current memory usage of integration system

    /// Error tracking
    uint64_t layered_display_errors; ///< Number of errors in layered display
    uint64_t fallback_triggers;      ///< Number of times fallback was triggered
    time_t last_error_time;          ///< Time of last error

    /// Health indicators
    bool performance_within_threshold; ///< Performance meeting threshold
                                       ///< requirements
    bool cache_efficiency_good;        ///< Cache performing efficiently
    bool memory_usage_acceptable; ///< Memory usage within acceptable limits

    /// v1.3.0 Safety Infrastructure Statistics
    uint64_t safety_checks_performed; ///< Number of safety checks performed
    uint64_t fallback_events[10];     ///< Count of each fallback reason type
    time_t last_fallback_time;        ///< Time of last fallback event
    integration_fallback_reason_t
        last_fallback_reason; ///< Reason for last fallback
} display_integration_stats_t;

/**
 * Enhanced Performance Monitoring Metrics.
 * Provides detailed metrics for cache efficiency and display timing validation.
 */
typedef struct {
    /// Cache Performance Targets
    uint64_t cache_operations_total; ///< Total cache operations performed
    uint64_t cache_hits_global;      ///< Global cache hits across all systems
    uint64_t cache_misses_global;    ///< Global cache misses across all systems
    double cache_hit_rate_current;   ///< Current cache hit rate percentage
    double cache_hit_rate_target;    ///< Target cache hit rate (>75% dev, >90%
                                     ///< release)
    bool cache_target_achieved;      ///< Whether cache target is being met

    /// Display Timing Targets
    uint64_t
        display_operations_measured; ///< Number of display operations measured
    uint64_t display_time_total_ns;  ///< Total display time in nanoseconds
    uint64_t display_time_min_ns;    ///< Minimum display time recorded
    uint64_t display_time_max_ns;    ///< Maximum display time recorded
    double display_time_avg_ms;      ///< Average display time in milliseconds
    double display_time_target_ms;   ///< Target display time (<50ms release)
    bool display_timing_target_achieved; ///< Whether timing target is being met

    /// Performance Trend Analysis
    uint64_t measurements_window[60]; ///< Rolling window of last 60
                                      ///< measurements (ns)
    uint32_t measurements_index;      ///< Current index in measurements window
    double performance_trend;         ///< Performance trend indicator

    /// Real-time Monitoring
    time_t last_measurement_time; ///< Time of last performance measurement
    bool monitoring_active;       ///< Whether real-time monitoring is active
    uint32_t measurement_frequency_hz; ///< Measurement frequency for monitoring

    /// Baseline Establishment
    bool baseline_established;          ///< Whether performance baseline is set
    double baseline_cache_hit_rate;     ///< Baseline cache hit rate
    double baseline_display_time_ms;    ///< Baseline display time
    time_t baseline_establishment_time; ///< When baseline was established
} display_perf_metrics_t;

/* ============================================================================
 * INITIALIZATION AND CLEANUP
 * ============================================================================
 */

/**
 * Initialize the display integration system.
 * Sets up the display controller and prepares for shell integration.
 *
 * This function must be called during shell startup, before any display
 * operations. It initializes the layered display controller if enabled
 * and prepares the integration wrapper system.
 *
 * @param init_config Configuration for display integration (NULL for defaults)
 * @return true on success, false on failure
 */
bool display_integration_init(const display_integration_config_t *init_config);

/**
 * Cleanup the display integration system.
 * Releases all resources and resets state.
 *
 * This function should be called during shell shutdown to ensure
 * proper cleanup of all display integration resources.
 */
void display_integration_cleanup(void);

/* ============================================================================
 * CONFIGURATION MANAGEMENT
 * ============================================================================
 */

/**
 * Create default configuration for display integration.
 * Initializes configuration structure with sensible defaults for most
 * environments.
 *
 * @param config Configuration structure to initialize
 */
void display_integration_create_default_config(
    display_integration_config_t *config);

/**
 * Update display integration configuration.
 * Applies new configuration settings and reconfigures the system as needed.
 *
 * This function can be called at runtime to change integration behavior,
 * including enabling/disabling layered display or changing optimization levels.
 *
 * @param config New configuration to apply
 * @return true on success, false on failure
 */
bool display_integration_set_config(const display_integration_config_t *config);

/**
 * Get current display integration configuration.
 * Retrieves the currently active configuration settings.
 *
 * @param config Configuration structure to fill
 * @return true on success, false on failure
 */
bool display_integration_get_config(display_integration_config_t *config);

/* ============================================================================
 * MAIN INTEGRATION FUNCTIONS (Shell Function Replacements)
 * ============================================================================
 */

/**
 * Integrated display function - replacement for lush_safe_redisplay().
 *
 * This function provides coordinated display using the layered architecture
 * when enabled, with graceful fallback to the existing display function.
 * It handles prompt rendering, syntax highlighting, and display composition
 * through the display controller system.
 *
 * Usage: Replace all calls to lush_safe_redisplay() with this function.
 * Behavior: Seamless operation with enhanced capabilities when layered display
 *          is enabled, identical behavior when disabled.
 */
void display_integration_redisplay(void);

/**
 * Integrated prompt update function - replacement for lush_prompt_update().
 *
 * This function provides coordinated prompt updates using the layered
 * architecture when enabled, with graceful fallback to the existing
 * prompt update function. It ensures proper prompt rendering and
 * coordination with other display layers.
 *
 * Usage: Replace all calls to lush_prompt_update() with this function.
 * Behavior: Enhanced prompt update coordination when layered display is
 *          enabled, identical behavior when disabled.
 */
void display_integration_prompt_update(void);

/**
 * Integrated clear screen function - replacement for lush_clear_screen().
 *
 * This function provides coordinated screen clearing using the layered
 * architecture when enabled, with graceful fallback to the existing
 * clear screen function. It ensures proper screen clearing and display
 * state reset across all layers.
 *
 * Usage: Replace all calls to lush_clear_screen() with this function.
 * Behavior: Coordinated screen clearing when layered display is enabled,
 *          identical behavior when disabled.
 */
void display_integration_clear_screen(void);

/**
 * Get enhanced prompt using layered display system.
 * Provides enhanced prompt generation with visual enhancements.
 *
 * @param enhanced_prompt Pointer to store the generated enhanced prompt
 * @return true on success, false on failure
 */
bool display_integration_get_enhanced_prompt(char **enhanced_prompt);

/**
 * Generate the current prompt string using LLE prompt composer.
 *
 * @return Newly allocated prompt string, or NULL on error
 */
char *lush_generate_prompt(void);

/**
 * Update display after command execution completion.
 * This function is called from the main shell loop after each command
 * execution to ensure the layered display system handles post-command
 * prompt rendering and caching optimization.
 *
 * @param executed_command The command that was just executed (for cache
 * analysis)
 *
 * Usage: Call after parse_and_execute() in main shell loop.
 * Behavior: Uses layered display for post-command rendering when enabled,
 *          no-op when disabled. Analyzes command for cache optimization.
 */
void display_integration_post_command_update(const char *executed_command);

/* ============================================================================
 * PERFORMANCE MONITORING AND DIAGNOSTICS
 * ============================================================================
 */

/**
 * Get display integration performance statistics.
 * Retrieves comprehensive performance and usage statistics for monitoring
 * and optimization purposes.
 *
 * @param stats Statistics structure to fill
 * @return true on success, false on failure
 */
bool display_integration_get_stats(display_integration_stats_t *stats);

/**
 * Reset display integration performance statistics.
 * Clears all performance counters and statistics, useful for benchmarking
 * or periodic monitoring resets.
 */
void display_integration_reset_stats(void);

/**
 * Check if layered display is currently enabled and working.
 *
 * @return true if layered display is active and functional, false otherwise
 */
bool display_integration_is_layered_active(void);

/**
 * Get the global display controller instance.
 * Returns the display controller for integration with other components like
 * LLE.
 *
 * @return pointer to display controller, or NULL if not initialized
 */
display_controller_t *display_integration_get_controller(void);

/**
 * Get display integration health status.
 * Provides a comprehensive health assessment of the integration system
 * based on performance metrics, error rates, and system status.
 *
 * @return health status enum indicating system health
 */
display_integration_health_t display_integration_get_health(void);

/**
 * Get human-readable health status string.
 * Converts health status enum to descriptive string for user display.
 *
 * @param health Health status enum
 * @return String description of health status
 */
const char *
display_integration_health_string(display_integration_health_t health);

/* ============================================================================
 * v1.3.0 SAFETY INFRASTRUCTURE
 * ============================================================================
 */

/**
 * Perform comprehensive safety check for layered display operation.
 *
 * This function validates that all prerequisites are met for a safe
 * layered display operation, including controller state, memory availability,
 * and system health. It provides detailed fallback reasoning for diagnostics.
 *
 * @param function_name Name of the calling function for logging
 * @param fallback_reason Output parameter for fallback reason if check fails
 * @return true if safe to proceed with layered display, false if should
 * fallback
 */
bool safe_layered_display_attempt(
    const char *function_name, integration_fallback_reason_t *fallback_reason);

/**
 * Log a fallback event for diagnostics and monitoring.
 *
 * Records fallback events for analysis and troubleshooting. In debug mode,
 * provides detailed logging. In production, maintains statistics only.
 *
 * @param function_name Name of the function that fell back
 * @param reason Reason for the fallback
 */
void log_fallback_event(const char *function_name,
                        integration_fallback_reason_t reason);

/**
 * Log a display controller error with context.
 *
 * Records display controller errors with context information for debugging
 * and system monitoring. Integrates with enterprise logging when enabled.
 *
 * @param function_name Name of the function where error occurred
 * @param error Display controller error code
 */
void log_controller_error(const char *function_name,
                          display_controller_error_t error);

/**
 * Get human-readable string for fallback reason.
 *
 * Converts fallback reason enum to descriptive string for logging and
 * diagnostic output. Used in debug mode and system diagnostics.
 *
 * @param reason Fallback reason enum value
 * @return Human-readable description string
 */
const char *
integration_fallback_reason_string(integration_fallback_reason_t reason);

/**
 * Print comprehensive diagnostic information about display integration.
 * Outputs detailed information about integration status, performance,
 * configuration, and health for debugging and monitoring purposes.
 */
void display_integration_print_diagnostics(void);

/* ============================================================================
 * ENTERPRISE FEATURES AND DEPLOYMENT
 * ============================================================================
 */

/**
 * Enable enterprise deployment mode with specific configuration.
 * Configures the integration system for enterprise deployment with
 * appropriate logging, monitoring, and compatibility settings.
 *
 * @param deployment_name Name identifier for the deployment
 * @param environment_name Environment identifier (dev/staging/prod)
 * @param strict_compatibility Enable strict backward compatibility mode
 * @return true on success, false on failure
 */
bool display_integration_enable_enterprise_mode(const char *deployment_name,
                                                const char *environment_name,
                                                bool strict_compatibility);

/**
 * Get integration system version information.
 * Returns version string for compatibility and support purposes.
 *
 * @return Version string in "major.minor.patch" format
 */
const char *display_integration_get_version(void);

/**
 * Check compatibility with shell version.
 * Verifies that the integration system is compatible with the current
 * shell version and configuration.
 *
 * @param shell_version Shell version string to check
 * @return true if compatible, false if incompatible
 */
bool display_integration_check_compatibility(const char *shell_version);

/**
 * Update autosuggestions using layered display system.
 * Replaces direct terminal control with coordinated layer display.
 *
 * @param line_buffer Current readline buffer
 * @param cursor_pos Current cursor position
 * @param line_end End of line position
 * @return true on success, false on failure
 */
bool display_integration_update_autosuggestions(const char *line_buffer,
                                                int cursor_pos, int line_end);

/**
 * Clear autosuggestions display using layered system.
 *
 * @return true on success, false on failure
 */
bool display_integration_clear_autosuggestions(void);

/**
 * Initialize autosuggestions layer integration.
 *
 * @return true on success, false on failure
 */
bool display_integration_init_autosuggestions(void);

/**
 * Cleanup autosuggestions layer integration.
 */
void display_integration_cleanup_autosuggestions(void);

/* ============================================================================
 * PERFORMANCE MONITORING
 * ============================================================================
 */

/**
 * Initialize performance monitoring system.
 * Sets up enhanced metrics collection for cache efficiency and display timing.
 *
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_init(void);

/**
 * Get current performance metrics.
 *
 * @param metrics Metrics structure to fill
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_get_metrics(
    display_perf_metrics_t *metrics);

/**
 * Record a display operation for timing analysis.
 *
 * @param operation_time_ns Operation time in nanoseconds
 * @return true on success, false on failure
 */
bool display_integration_record_display_timing(uint64_t operation_time_ns);

/**
 * Record cache operation for cache efficiency analysis.
 *
 * @param was_hit true if cache hit, false if cache miss
 * @return true on success, false on failure
 */
bool display_integration_record_cache_operation(bool was_hit);

/**
 * Establish performance baseline for monitoring.
 *
 * @return true on success, false on failure
 */
bool display_integration_establish_baseline(void);

/**
 * Check if performance targets are being met.
 *
 * @param cache_target_met Output: whether cache hit rate target is met
 * @param timing_target_met Output: whether display timing target is met
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_check_targets(bool *cache_target_met,
                                                    bool *timing_target_met);

/**
 * Generate performance report.
 *
 * @param detailed true for detailed report, false for summary
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_report(bool detailed);

/**
 * Record cache operation for specific layer (for detailed analysis).
 *
 * @param layer_name Layer name ("display_controller", "composition_engine",
 * etc.)
 * @param hit Whether the cache operation was a hit (true) or miss (false)
 */
void display_integration_record_layer_cache_operation(const char *layer_name,
                                                      bool hit);

/**
 * Print detailed layer-specific cache performance report.
 */
void display_integration_print_layer_cache_report(void);

/**
 * Reset layer-specific cache statistics.
 */
void display_integration_reset_layer_cache_stats(void);

/**
 * Reset performance metrics.
 *
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_reset(void);

/**
 * Enable/disable real-time performance monitoring.
 *
 * @param enable true to enable, false to disable
 * @param frequency_hz Monitoring frequency in Hz (1-60)
 * @return true on success, false on failure
 */
bool display_integration_perf_monitor_set_active(bool enable,
                                                 uint32_t frequency_hz);

/* ============================================================================
 * DISPLAY INTEGRATION MACROS
 * ============================================================================
 */

/**
 * Convenience macro for safe function replacement.
 * Provides compile-time switching between original and integrated functions.
 */
#ifdef DISPLAY_INTEGRATION_ENABLED
#define LUSH_SAFE_REDISPLAY() display_integration_redisplay()
#define LUSH_PROMPT_UPDATE() display_integration_prompt_update()
#define LUSH_CLEAR_SCREEN() display_integration_clear_screen()
#else
#define LUSH_SAFE_REDISPLAY() lush_safe_redisplay()
#define LUSH_PROMPT_UPDATE() lush_prompt_update()
#define LUSH_CLEAR_SCREEN() lush_clear_screen()
#endif

/**
 * Quick health check macro for conditional behavior.
 */
#define DISPLAY_INTEGRATION_IS_HEALTHY()                                       \
    (display_integration_get_health() <= DISPLAY_INTEGRATION_HEALTH_GOOD)

/**
 * Debug output macro (only active in debug mode).
 */
#define DISPLAY_INTEGRATION_DEBUG(fmt, ...)                                    \
    do {                                                                       \
        display_integration_config_t config;                                   \
        if (display_integration_get_config(&config) && config.debug_mode) {    \
            fprintf(stderr, "display_integration: " fmt "\n", ##__VA_ARGS__);  \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /// DISPLAY_INTEGRATION_H
