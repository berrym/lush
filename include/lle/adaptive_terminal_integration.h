/**
 * @file adaptive_terminal_integration.h
 * @brief Adaptive Terminal Integration System
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides universal terminal compatibility through detection and control
 * separation. Enables LLE to work optimally in any environment from
 * traditional TTY terminals to modern editor interfaces and AI assistants.
 *
 * Key Innovation: Detection and Control Separation
 * - Interactive capability detection operates independently
 * - Control method selection based on detected capabilities
 * - Universal compatibility with graceful degradation
 *
 * Specification:
 * docs/lle_specification/critical_gaps/26_adaptive_terminal_integration_complete.md
 * Date: 2025-11-02
 */

#ifndef LLE_ADAPTIVE_TERMINAL_INTEGRATION_H
#define LLE_ADAPTIVE_TERMINAL_INTEGRATION_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * INTEGRATION MODES
 * ============================================================================
 */

/**
 * Adaptive mode determines how LLE interfaces with the terminal environment.
 * Separate from terminal type - mode is selected based on capabilities.
 */
typedef enum {
    LLE_ADAPTIVE_MODE_NONE = 0,   ///< Non-interactive (scripts, pipes)
    LLE_ADAPTIVE_MODE_MINIMAL,    ///< Basic line editing, no terminal control
    LLE_ADAPTIVE_MODE_ENHANCED,   /**< Editor terminals, display client approach
                                   */
    LLE_ADAPTIVE_MODE_NATIVE,     ///< Traditional TTY, full terminal control
    LLE_ADAPTIVE_MODE_MULTIPLEXED ///< Terminal multiplexers (tmux/screen)
} lle_adaptive_mode_t;

/**
 * Capability level indicates feature richness available in the environment.
 */
typedef enum {
    LLE_CAPABILITY_NONE = 0, ///< No interactive capabilities
    LLE_CAPABILITY_BASIC,    ///< Basic text I/O only
    LLE_CAPABILITY_STANDARD, ///< Colors, basic formatting
    LLE_CAPABILITY_FULL,     ///< 256 colors, cursor control
    LLE_CAPABILITY_PREMIUM   ///< Truecolor, advanced features
} lle_capability_level_t;

/* ============================================================================
 * TERMINAL SIGNATURE DATABASE
 * ============================================================================
 */

/**
 * Terminal signature for pattern-based identification.
 * Describes known terminal characteristics and preferred handling.
 */
typedef struct {
    const char *name;                        ///< Terminal identifier
    const char *term_program_pattern;        ///< TERM_PROGRAM pattern
    const char *term_pattern;                ///< TERM variable pattern
    const char *env_var_check;               ///< Additional env var
    lle_capability_level_t capability_level; ///< Expected capabilities
    lle_adaptive_mode_t preferred_mode;      ///< Preferred mode
    bool force_interactive;                  ///< Override stdin check
    bool requires_special_handling;          ///< Mode-specific code
} lle_terminal_signature_t;

/* ============================================================================
 * MULTIPLEXER TYPES
 * ============================================================================
 */

/**
 * Terminal multiplexer type for special handling.
 */
typedef enum {
    LLE_MUX_TYPE_NONE = 0,
    LLE_MUX_TYPE_TMUX,
    LLE_MUX_TYPE_SCREEN,
    LLE_MUX_TYPE_OTHER
} lle_multiplexer_type_t;

/* ============================================================================
 * DETECTION SYSTEM
 * ============================================================================
 */

/**
 * Comprehensive terminal detection result.
 * Contains all information needed for mode selection and initialization.
 */
typedef struct {
    /// Basic terminal status
    bool stdin_is_tty;
    bool stdout_is_tty;
    bool stderr_is_tty;

    /// Environment analysis results
    char term_name[64];
    char term_program[64];
    char colorterm[32];

    /// Detected capabilities
    bool supports_colors;
    bool supports_256_colors;
    bool supports_truecolor;
    bool supports_cursor_queries;
    bool supports_cursor_positioning;
    bool supports_mouse;
    bool supports_bracketed_paste;
    bool supports_unicode;

    /// Terminal dimensions
    int terminal_cols; ///< Terminal width in columns
    int terminal_rows; ///< Terminal height in rows

    /// Multiplexer detection
    lle_multiplexer_type_t multiplexer_type;

    /// Terminal classification
    const lle_terminal_signature_t *matched_signature;
    lle_capability_level_t capability_level;
    lle_adaptive_mode_t recommended_mode;
    bool detection_confidence_high;

    /// Timing and performance
    uint64_t detection_time_us;
    bool probing_successful;
} lle_terminal_detection_result_t;

/**
 * Detection performance statistics.
 */
typedef struct {
    uint64_t total_detections;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t avg_detection_time_us;
    uint64_t max_detection_time_us;
    uint64_t probe_timeouts;
} lle_detection_performance_stats_t;

/* ============================================================================
 * MODE-SPECIFIC CONTROLLERS (Forward Declarations)
 * ============================================================================
 */

/// Opaque types - defined in implementation files
typedef struct lle_native_controller_t lle_native_controller_t;
typedef struct lle_display_client_controller_t lle_display_client_controller_t;
typedef struct lle_multiplexer_controller_t lle_multiplexer_controller_t;
typedef struct lle_minimal_controller_t lle_minimal_controller_t;
typedef struct lle_enhanced_input_processor_t lle_enhanced_input_processor_t;
typedef struct lle_display_content_generator_t lle_display_content_generator_t;
typedef struct lle_multiplexer_adapter_t lle_multiplexer_adapter_t;

/* ============================================================================
 * ADAPTIVE CONTEXT
 * ============================================================================
 */

/**
 * Main adaptive integration context.
 * Contains detected mode and appropriate controller implementation.
 */
typedef struct {
    lle_adaptive_mode_t mode;
    lle_terminal_detection_result_t *detection_result;

    /// Control method implementations (mode-specific)
    union {
        lle_native_controller_t *native;
        lle_display_client_controller_t *display_client;
        lle_multiplexer_controller_t *mux;
        lle_minimal_controller_t *minimal;
    } controller;

    /// Common systems (available in all modes)
    void *buffer;          ///< lle_buffer_t - avoid circular dependency
    void *history;         ///< lle_history_core_t
    void *completion;      ///< lle_completion_system_t
    void *input_processor; ///< lle_input_processor_t

    /// Integration with Lush systems
    lush_memory_pool_t *memory_pool;
    void *display_context; ///< lush_display_context_t

    /// Performance monitoring
    lle_performance_monitor_t *performance_monitor;

    /// Health status
    bool healthy;
    uint32_t error_count;
    uint64_t last_health_check_us;
} lle_adaptive_context_t;

/* ============================================================================
 * ADAPTIVE INTERFACE
 * ============================================================================
 */

/**
 * Unified LLE interface - provides consistent API across all modes.
 */
typedef struct {
    lle_adaptive_context_t *adaptive_context;

    /// Unified operation interface
    lle_result_t (*read_line)(lle_adaptive_context_t *ctx, const char *prompt,
                              char **line);

    lle_result_t (*process_input)(lle_adaptive_context_t *ctx,
                                  const char *input, size_t length,
                                  void **events);

    lle_result_t (*update_display)(lle_adaptive_context_t *ctx);

    lle_result_t (*handle_resize)(lle_adaptive_context_t *ctx, int new_width,
                                  int new_height);

    lle_result_t (*set_configuration)(lle_adaptive_context_t *ctx,
                                      void *config);

    lle_result_t (*get_status)(lle_adaptive_context_t *ctx, void *status);
} lle_adaptive_interface_t;

/* ============================================================================
 * CONFIGURATION RECOMMENDATIONS
 * ============================================================================
 */

/**
 * Configuration recommendations based on detected capabilities.
 */
typedef struct {
    bool enable_lle;
    bool enable_syntax_highlighting;
    bool enable_autosuggestions;
    bool enable_tab_completion;
    bool enable_history;
    bool enable_multiline_editing;
    bool enable_undo_redo;
    int color_support_level; ///< 0=none, 1=basic, 2=256, 3=true
    lle_adaptive_mode_t recommended_mode;
} lle_adaptive_config_recommendation_t;

/* ============================================================================
 * ERROR CODES
 * ============================================================================
 */

/**
 * Adaptive integration specific error codes.
 */
typedef enum {
    LLE_ADAPTIVE_ERROR_DETECTION_FAILED = 0x3000,
    LLE_ADAPTIVE_ERROR_MODE_INITIALIZATION_FAILED,
    LLE_ADAPTIVE_ERROR_CONTROLLER_MISMATCH,
    LLE_ADAPTIVE_ERROR_CAPABILITY_MISMATCH,
    LLE_ADAPTIVE_ERROR_FALLBACK_FAILED,
    LLE_ADAPTIVE_ERROR_CONTEXT_CORRUPTION,
    LLE_ADAPTIVE_ERROR_MODE_SWITCHING_FAILED
} lle_adaptive_error_t;

/* ============================================================================
 * CORE DETECTION API
 * ============================================================================
 */

/**
 * Perform comprehensive terminal capability detection.
 *
 * Detects terminal type, capabilities, and recommends integration mode.
 * Uses multi-tier detection: environment analysis, signature matching,
 * and safe runtime probing.
 *
 * @param result Output detection result (caller must free)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_detect_terminal_capabilities_comprehensive(
    lle_terminal_detection_result_t **result);

/**
 * Optimized detection with caching (30-second TTL).
 *
 * @param result Output detection result (caller must free)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_detect_terminal_capabilities_optimized(
    lle_terminal_detection_result_t **result);

/**
 * Free detection result.
 *
 * @param result Detection result to free
 */
void lle_terminal_detection_result_destroy(
    lle_terminal_detection_result_t *result);

/**
 * Safe terminal capability probing with timeout protection.
 *
 * Only probes if stdout is TTY. Uses progressive testing with
 * appropriate timeouts for each capability.
 *
 * @param detection Detection result to populate
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_probe_terminal_capabilities_safe(
    lle_terminal_detection_result_t *detection);

/**
 * Match terminal signature from database.
 *
 * @param detection Detection result with environment data
 * @return Matched signature or NULL if no match
 */
const lle_terminal_signature_t *
lle_match_terminal_signature(const lle_terminal_detection_result_t *detection);

/**
 * Get terminal signature database.
 *
 * @param count Output signature count
 * @return Array of signatures
 */
const lle_terminal_signature_t *
lle_get_terminal_signature_database(size_t *count);

/* ============================================================================
 * INITIALIZATION API
 * ============================================================================
 */

/**
 * Create adaptive interface with automatic detection.
 *
 * Performs capability detection and initializes appropriate controller.
 *
 * @param interface Output interface (caller must destroy)
 * @param config Optional configuration (NULL for defaults)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_create_adaptive_interface(lle_adaptive_interface_t **interface,
                                           void *config);

/**
 * Destroy adaptive interface and free resources.
 *
 * @param interface Interface to destroy
 */
void lle_adaptive_interface_destroy(lle_adaptive_interface_t *interface);

/**
 * Initialize adaptive context with detected mode.
 *
 * @param context Output context (caller must destroy)
 * @param detection_result Detection result to use
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_initialize_adaptive_context(
    lle_adaptive_context_t **context,
    const lle_terminal_detection_result_t *detection_result,
    lush_memory_pool_t *memory_pool);

/**
 * Destroy adaptive context and free resources.
 *
 * @param context Context to destroy
 */
void lle_adaptive_context_destroy(lle_adaptive_context_t *context);

/* ============================================================================
 * SHELL INTEGRATION API
 * ============================================================================
 */

/**
 * Determine if shell should be interactive.
 *
 * Drop-in replacement for traditional shell interactive detection.
 * Considers adaptive detection results including force_interactive flags.
 *
 * @param forced_interactive Force interactive mode (-i flag)
 * @param has_script_file Has script file argument
 * @param stdin_mode Stdin redirection mode
 * @return true if shell should be interactive
 */
bool lle_adaptive_should_shell_be_interactive(bool forced_interactive,
                                              bool has_script_file,
                                              bool stdin_mode);

/**
 * Get configuration recommendations based on detected capabilities.
 *
 * @param config Output configuration recommendations
 */
void lle_adaptive_get_recommended_config(
    lle_adaptive_config_recommendation_t *config);

/* ============================================================================
 * PERFORMANCE MONITORING API
 * ============================================================================
 */

/**
 * Get detection performance statistics.
 *
 * @param stats Output statistics
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_adaptive_get_detection_stats(lle_detection_performance_stats_t *stats);

/**
 * Reset detection performance statistics.
 */
void lle_adaptive_reset_detection_stats(void);

/* ============================================================================
 * HEALTH MONITORING API
 * ============================================================================
 */

/**
 * Perform health check on adaptive context.
 *
 * @param context Context to check
 * @return true if healthy
 */
bool lle_adaptive_perform_health_check(lle_adaptive_context_t *context);

/**
 * Pure mode-degradation mapping: the mode @p mode degrades to when its
 * controller fails. NATIVE -> ENHANCED -> MINIMAL, MULTIPLEXED -> NATIVE,
 * and the terminal modes MINIMAL and NONE map to themselves (no further
 * fallback). Has no side effects and needs no context, so the degradation
 * hierarchy can be verified directly.
 *
 * @param mode Current adaptive mode
 * @return The next mode in the degradation chain, or @p mode if terminal
 */
lle_adaptive_mode_t lle_adaptive_fallback_mode_for(lle_adaptive_mode_t mode);

/**
 * Try fallback mode if current mode fails.
 *
 * @param context Context to update
 * @return LLE_SUCCESS if fallback succeeded
 */
lle_result_t lle_adaptive_try_fallback_mode(lle_adaptive_context_t *context);

/* ============================================================================
 * UTILITY API
 * ============================================================================
 */

/**
 * Get human-readable mode name.
 *
 * @param mode Integration mode
 * @return Mode name string (static, do not free)
 */
const char *lle_adaptive_mode_to_string(lle_adaptive_mode_t mode);

/**
 * Get human-readable capability level name.
 *
 * @param level Capability level
 * @return Level name string (static, do not free)
 */
const char *lle_capability_level_to_string(lle_capability_level_t level);

/* ============================================================================
 * TERMINAL TYPE DETECTION HELPERS
 * ============================================================================
 */

/**
 * Check if running in iTerm2.
 *
 * @param detection Detection result (NULL to perform fresh detection)
 * @return true if iTerm2 detected
 */
bool lle_is_iterm2(const lle_terminal_detection_result_t *detection);

/**
 * Check if running inside tmux.
 *
 * @param detection Detection result (NULL to perform fresh detection)
 * @return true if tmux detected
 */
bool lle_is_tmux(const lle_terminal_detection_result_t *detection);

/**
 * Check if running inside GNU screen.
 *
 * @param detection Detection result (NULL to perform fresh detection)
 * @return true if screen detected
 */
bool lle_is_screen(const lle_terminal_detection_result_t *detection);

/**
 * Check if running inside any terminal multiplexer.
 *
 * @param detection Detection result (NULL to perform fresh detection)
 * @return true if any multiplexer detected
 */
bool lle_is_multiplexed(const lle_terminal_detection_result_t *detection);

/**
 * Get terminal type string (e.g., "xterm-256color").
 *
 * @param detection Detection result
 * @return Terminal type string or NULL if unknown
 */
const char *
lle_get_terminal_type(const lle_terminal_detection_result_t *detection);

/**
 * Get current terminal dimensions.
 *
 * @param cols Output column count (may be NULL)
 * @param rows Output row count (may be NULL)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_get_terminal_size(int *cols, int *rows);

/**
 * Check if stdout is a TTY.
 *
 * @return true if stdout is a TTY
 */
bool lle_is_tty(void);

/**
 * Reset terminal to clean state.
 *
 * Resets formatting, shows cursor, and outputs newline.
 * Safe to call even if terminal is not a TTY.
 */
void lle_terminal_reset(void);

/**
 * Clean up terminal detection cache.
 *
 * Frees the cached terminal detection result. Should be called
 * during shell shutdown to ensure proper cleanup.
 */
void lle_terminal_detection_cache_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /// LLE_ADAPTIVE_TERMINAL_INTEGRATION_H
