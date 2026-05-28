/**
 * @file command_layer.h
 * @brief Command layer - Real-time syntax highlighting system
 *
 * Provides real-time syntax highlighting for command input that works
 * universally with any prompt structure.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Real-time syntax highlighting for shell commands
 * - Universal compatibility with any prompt structure
 * - Integration with existing lush syntax highlighting functions
 * - High-performance command input processing (<5ms updates)
 * - Memory-safe command text management
 * - Intelligent cursor position tracking
 * - Event-driven communication with prompt layer
 *
 * Design Principles:
 * - Independent from prompt structure and layout
 * - No interference with prompt display or positioning
 * - Leverages existing proven syntax highlighting logic
 * - Performance-optimized for real-time updates
 * - Memory-safe with proper resource management
 * - Event-driven coordination with other layers
 *
 * Strategic Innovation:
 * This layer completes the revolutionary combination of professional themes
 * with real-time syntax highlighting by providing command input processing
 * that works universally with the prompt layer's universal compatibility.
 */

#ifndef COMMAND_LAYER_H
#define COMMAND_LAYER_H

#include "layer_events.h"
#include "lle/error_handling.h"
#include "lle/syntax_highlighting.h"
#include "prompt_layer.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

/// Forward declaration for LLE types (no longer needed in command_layer)
/// Completion menu state moved to display_controller (proper architecture)

#ifdef __cplusplus
extern "C" {
#endif

/// ============================================================================
/// CONSTANTS AND CONFIGURATION
/// ============================================================================

#define COMMAND_LAYER_VERSION_MAJOR 1
#define COMMAND_LAYER_VERSION_MINOR 0
#define COMMAND_LAYER_VERSION_PATCH 0

/// Command content limits
#define COMMAND_LAYER_MAX_COMMAND_SIZE 8192
#define COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE 16384
#define COMMAND_LAYER_MAX_TOKENS 512

/// Performance targets
#define COMMAND_LAYER_TARGET_UPDATE_TIME_MS 5
#define COMMAND_LAYER_CACHE_EXPIRY_MS 50

/// Cache configuration
#define COMMAND_LAYER_CACHE_SIZE 64
#define COMMAND_LAYER_METRICS_HISTORY_SIZE 32

/// Syntax highlighting configuration
#define COMMAND_LAYER_MAX_HIGHLIGHT_REGIONS 256
#define COMMAND_LAYER_COLOR_RESET "\033[0m"
#define COMMAND_LAYER_MAX_COLOR_CODE_SIZE 32

/// ============================================================================
/// TYPE DEFINITIONS
/// ============================================================================

/**
 * Error codes for command layer operations
 */
typedef enum {
    COMMAND_LAYER_SUCCESS = 0,              /// Operation completed successfully
    COMMAND_LAYER_ERROR_INVALID_PARAM,      /// Invalid parameter provided
    COMMAND_LAYER_ERROR_NULL_POINTER,       /// NULL pointer passed
    COMMAND_LAYER_ERROR_MEMORY_ALLOCATION,  /// Memory allocation failed
    COMMAND_LAYER_ERROR_BUFFER_TOO_SMALL,   /// Output buffer insufficient
    COMMAND_LAYER_ERROR_COMMAND_TOO_LARGE,  /// Command exceeds limits
    COMMAND_LAYER_ERROR_INVALID_CURSOR_POS, /// Cursor position invalid
    COMMAND_LAYER_ERROR_CACHE_FULL,         /// Cache is full
    COMMAND_LAYER_ERROR_SYNTAX_ERROR,       /// Syntax highlighting error
    COMMAND_LAYER_ERROR_NOT_INITIALIZED,    /// Layer not initialized
    COMMAND_LAYER_ERROR_EVENT_SYSTEM,       /// Event system error
    COMMAND_LAYER_ERROR_PERFORMANCE_LIMIT   /// Performance limit exceeded
} command_layer_error_t;

/**
 * Syntax highlighting token types
 */
typedef enum {
    COMMAND_TOKEN_NONE = 0, /// No token / whitespace
    COMMAND_TOKEN_COMMAND,  /// Command name
    COMMAND_TOKEN_ARGUMENT, /// Command argument
    COMMAND_TOKEN_OPTION,   /// Command option (--flag)
    COMMAND_TOKEN_STRING,   /// Quoted string
    COMMAND_TOKEN_VARIABLE, /// Variable ($var)
    COMMAND_TOKEN_REDIRECT, /// Redirection (>, <, >>)
    COMMAND_TOKEN_PIPE,     /// Pipe (|)
    COMMAND_TOKEN_KEYWORD,  /// Shell keywords (if, for, etc)
    COMMAND_TOKEN_OPERATOR, /// Operators (&&, ||, ;)
    COMMAND_TOKEN_PATH,     /// File paths
    COMMAND_TOKEN_NUMBER,   /// Numeric values
    COMMAND_TOKEN_COMMENT,  /// Comments (#)
    COMMAND_TOKEN_ERROR     /// Syntax errors
} command_token_type_t;

/**
 * Syntax highlighting color scheme
 */
typedef struct {
    char command_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];  /// Command names
    char argument_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE]; /// Arguments
    char option_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];   /// Options/flags
    char string_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];   /// Strings
    char variable_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE]; /// Variables
    char redirect_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE]; /// Redirections
    char pipe_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];     /// Pipes
    char keyword_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];  /// Keywords
    char operator_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE]; /// Operators
    char path_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];     /// Paths
    char number_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];   /// Numbers
    char comment_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];  /// Comments
    char error_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];    /// Errors
    char reset_color[COMMAND_LAYER_MAX_COLOR_CODE_SIZE];    /// Reset
} command_color_scheme_t;

/**
 * Syntax highlighting region
 */
typedef struct {
    size_t start;                    /// Start position in command
    size_t length;                   /// Length of region
    command_token_type_t token_type; /// Type of token
    char
        color_code[COMMAND_LAYER_MAX_COLOR_CODE_SIZE]; /// Color for this region
} command_highlight_region_t;

/**
 * Command metrics and positioning information
 */
typedef struct {
    size_t command_length;        /// Length of command text
    size_t cursor_position;       /// Cursor position in command
    size_t visual_length;         /// Visual length (with colors)
    size_t token_count;           /// Number of tokens identified
    size_t error_count;           /// Number of syntax errors
    int estimated_display_column; /// Where command will display
    int estimated_display_row;    /// Row for command display
    bool is_multiline_command;    /// Command spans multiple lines
    bool has_syntax_errors;       /// Command has syntax errors
} command_metrics_t;

/**
 * Command layer performance statistics
 */
typedef struct {
    uint64_t update_count;             /// Number of command updates
    uint64_t cache_hits;               /// Cache hit count
    uint64_t cache_misses;             /// Cache miss count
    uint64_t syntax_highlight_time_ns; /// Time for syntax highlighting
    uint64_t avg_update_time_ns;       /// Average update time
    uint64_t max_update_time_ns;       /// Maximum update time
    uint64_t min_update_time_ns;       /// Minimum update time
    uint64_t total_processing_time_ns; /// Total processing time
} command_performance_t;

/**
 * Command cache entry
 */
typedef struct {
    char command_text[COMMAND_LAYER_MAX_COMMAND_SIZE]; /// Cached command
    char highlighted_text[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE]; /// Cached output
    command_metrics_t metrics; /// Cached metrics
    uint64_t timestamp_ns;     /// Cache timestamp
    uint32_t hash;             /// Command hash for validation
    bool is_valid;             /// Cache entry validity
} command_cache_entry_t;

/**
 * Syntax highlighting configuration
 */
typedef struct {
    bool enabled;                        /// Syntax highlighting enabled
    bool use_colors;                     /// Use color output
    bool highlight_errors;               /// Highlight syntax errors
    bool cache_enabled;                  /// Enable result caching
    command_color_scheme_t color_scheme; /// Color configuration
    uint32_t cache_expiry_ms;            /// Cache expiry time
    uint32_t max_update_time_ms;         /// Maximum update time
} command_syntax_config_t;

/**
 * Command layer state structure
 */
typedef struct command_layer_s {
    /// Layer identification and state
    uint32_t magic;    /// Magic number for validation
    bool initialized;  /// Initialization state
    bool needs_redraw; /// Redraw needed flag

    /// Command content management
    char command_text[COMMAND_LAYER_MAX_COMMAND_SIZE]; /// Current command
    char highlighted_text[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE]; /// Highlighted
                                                               /// output
    size_t cursor_position; /// Current cursor position (byte offset)

    /// Cursor screen coordinates (calculated by LLE using incremental tracking)
    size_t cursor_screen_row;          /// Cursor row on screen (0-based)
    size_t cursor_screen_column;       /// Cursor column on screen (0-based)
    bool cursor_screen_position_valid; /// True if screen position is valid

    /// Completion menu state (tracked as extension of command layer)
    /// The menu is conceptually part of the command - it shows completions FOR
    /// the command being typed. By tracking it here, screen_buffer can include
    /// menu rows in cursor calculations, fixing the display bug.
    bool completion_menu_visible;        /// Menu is currently displayed
    char *completion_menu_content;       /// Rendered menu text (with ANSI)
    size_t completion_menu_content_size; /// Size of allocated buffer
    int completion_menu_lines;           /// Number of lines menu occupies
    int completion_menu_selected_index;  /// Currently selected item index

    /// Syntax highlighting state
    command_highlight_region_t
        highlight_regions[COMMAND_LAYER_MAX_HIGHLIGHT_REGIONS];
    size_t region_count;                   /// Number of highlight regions
    command_syntax_config_t syntax_config; /// Highlighting configuration
    lle_syntax_highlighter_t
        *spec_highlighter; /// Spec-compliant highlighter (Spec 11)

    /// Metrics and positioning
    command_metrics_t metrics;         /// Command metrics
    command_performance_t performance; /// Performance statistics

    /// Caching system
    command_cache_entry_t cache[COMMAND_LAYER_CACHE_SIZE]; /// Result cache
    size_t cache_size;           /// Current cache size
    uint64_t cache_access_count; /// Cache access counter

    /// Event system integration
    layer_event_system_t *event_system; /// Event system reference
    uint32_t event_subscription_id;     /// Event subscription ID

    /// Integration with prompt layer
    prompt_layer_t *prompt_layer;    /// Associated prompt layer
    bool prompt_integration_enabled; /// Prompt coordination enabled

    /// Performance monitoring
    struct timespec last_update_time; /// Last update timestamp
    uint64_t update_sequence_number;  /// Update sequence tracking
} command_layer_t;

/// ============================================================================
/// CORE API FUNCTIONS
/// ============================================================================

/**
 * Get command layer version information
 */
const char *command_layer_get_version(void);

/**
 * Create a new command layer instance
 *
 * @return New command layer instance, or NULL on failure
 */
command_layer_t *command_layer_create(void);

/**
 * Initialize command layer with event system
 *
 * @param layer Command layer instance
 * @param events Event system for layer communication
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_init(command_layer_t *layer,
                                         layer_event_system_t *events);

/**
 * Set command text for syntax highlighting
 *
 * @param layer Command layer instance
 * @param command_text Command text to highlight
 * @param cursor_pos Cursor position within command
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_set_command(command_layer_t *layer,
                                                const char *command_text,
                                                size_t cursor_pos);

/**
 * Get highlighted command text with colors
 *
 * @param layer Command layer instance
 * @param output Buffer for highlighted text
 * @param output_size Size of output buffer
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_get_highlighted_text(command_layer_t *layer,
                                                         char *output,
                                                         size_t output_size);

/**
 * Update cursor position in command
 *
 * @param layer Command layer instance
 * @param cursor_pos New cursor position
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_set_cursor_position(command_layer_t *layer,
                                                        size_t cursor_pos);

/**
 * Get command metrics and positioning information
 *
 * @param layer Command layer instance
 * @param metrics Output buffer for metrics
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_get_metrics(command_layer_t *layer,
                                                command_metrics_t *metrics);

/**
 * Force update of syntax highlighting
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_update(command_layer_t *layer);

/**
 * Clear command content
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_clear(command_layer_t *layer);

/// ============================================================================
/// COMPLETION MENU INTEGRATION
///
/// The completion menu is tracked as an extension of the command layer because
/// it is conceptually part of the command input - showing completions FOR the
/// command being typed. By tracking the menu here, screen_buffer can include
/// menu rows in cursor position calculations, ensuring correct cursor placement
/// regardless of terminal scrolling.
/// ============================================================================

/**
 * Set completion menu content
 *
 * Sets the rendered menu content to be displayed below the command.
 * The menu becomes part of the command layer's output and is tracked
 * for proper cursor positioning.
 *
 * @param layer Command layer instance
 * @param menu_content Rendered menu text with ANSI codes (will be copied)
 * @param num_lines Number of lines the menu occupies
 * @param selected_index Currently selected item index
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_set_completion_menu(command_layer_t *layer,
                                  const char *menu_content, int num_lines,
                                  int selected_index);

/**
 * Clear completion menu
 *
 * Removes the completion menu from the command layer display.
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_clear_completion_menu(command_layer_t *layer);

/**
 * Check if completion menu is visible
 *
 * @param layer Command layer instance
 * @return true if menu is visible, false otherwise
 */
bool command_layer_is_menu_visible(const command_layer_t *layer);

/**
 * Get completion menu content
 *
 * @param layer Command layer instance
 * @return Menu content string, or NULL if no menu is visible
 */
const char *command_layer_get_menu_content(const command_layer_t *layer);

/**
 * Get completion menu line count
 *
 * @param layer Command layer instance
 * @return Number of lines the menu occupies, or 0 if no menu
 */
int command_layer_get_menu_lines(const command_layer_t *layer);

/**
 * Update completion menu selection
 *
 * Updates just the selected index without re-rendering the full menu.
 *
 * @param layer Command layer instance
 * @param selected_index New selected item index
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_set_menu_selection(command_layer_t *layer,
                                                       int selected_index);

/// ============================================================================
/// LIFECYCLE MANAGEMENT
/// ============================================================================

/**
 * Cleanup command layer resources
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_cleanup(command_layer_t *layer);

/**
 * Destroy command layer instance
 *
 * @param layer Command layer instance
 */
void command_layer_destroy(command_layer_t *layer);

/// ============================================================================
/// SYNTAX HIGHLIGHTING CONFIGURATION
/// ============================================================================

/**
 * Enable or disable syntax highlighting
 *
 * @param layer Command layer instance
 * @param enabled Enable syntax highlighting
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_set_syntax_enabled(command_layer_t *layer,
                                                       bool enabled);

/**
 * Check if syntax highlighting is enabled
 *
 * @param layer Command layer instance
 * @return true if enabled, false otherwise
 */
bool command_layer_is_syntax_enabled(command_layer_t *layer);

/**
 * Set syntax highlighting color scheme
 *
 * @param layer Command layer instance
 * @param color_scheme Color scheme configuration
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_set_color_scheme(command_layer_t *layer,
                               const command_color_scheme_t *color_scheme);

/**
 * Get current color scheme
 *
 * @param layer Command layer instance
 * @param color_scheme Output buffer for color scheme
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_get_color_scheme(command_layer_t *layer,
                               command_color_scheme_t *color_scheme);

/**
 * Set syntax highlighting configuration
 *
 * @param layer Command layer instance
 * @param config Syntax highlighting configuration
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_set_syntax_config(command_layer_t *layer,
                                const command_syntax_config_t *config);

/// ============================================================================
/// PROMPT LAYER INTEGRATION
/// ============================================================================

/**
 * Set associated prompt layer for coordination
 *
 * @param layer Command layer instance
 * @param prompt_layer Associated prompt layer
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_set_prompt_layer(command_layer_t *layer,
                               prompt_layer_t *prompt_layer);

/**
 * Enable or disable prompt layer integration
 *
 * @param layer Command layer instance
 * @param enabled Enable prompt integration
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_set_prompt_integration(command_layer_t *layer, bool enabled);

/**
 * Get command display position relative to prompt
 *
 * @param layer Command layer instance
 * @param column Output for display column
 * @param row Output for display row
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_get_display_position(command_layer_t *layer,
                                                         int *column, int *row);

/// ============================================================================
/// PERFORMANCE AND MONITORING
/// ============================================================================

/**
 * Get performance statistics
 *
 * @param layer Command layer instance
 * @param performance Output buffer for performance stats
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_get_performance(command_layer_t *layer,
                              command_performance_t *performance);

/**
 * Reset performance statistics
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_reset_performance(command_layer_t *layer);

/**
 * Enable or disable result caching
 *
 * @param layer Command layer instance
 * @param enabled Enable caching
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_set_cache_enabled(command_layer_t *layer,
                                                      bool enabled);

/**
 * Clear syntax highlighting cache
 *
 * @param layer Command layer instance
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_clear_cache(command_layer_t *layer);

/// ============================================================================
/// VALIDATION AND DEBUGGING
/// ============================================================================

/**
 * Validate command layer instance
 *
 * @param layer Command layer instance
 * @return true if valid, false otherwise
 */
bool command_layer_validate(command_layer_t *layer);

/**
 * Get detailed layer status for debugging
 *
 * @param layer Command layer instance
 * @param status_buffer Output buffer for status information
 * @param buffer_size Size of status buffer
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t command_layer_get_debug_info(command_layer_t *layer,
                                                   char *status_buffer,
                                                   size_t buffer_size);

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * Create default syntax highlighting configuration
 *
 * @param config Output buffer for default configuration
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_create_default_config(command_syntax_config_t *config);

/**
 * Create default color scheme
 *
 * @param color_scheme Output buffer for default color scheme
 * @return COMMAND_LAYER_SUCCESS on success, error code on failure
 */
command_layer_error_t
command_layer_create_default_colors(command_color_scheme_t *color_scheme);

/**
 * Get error message for error code
 *
 * @param error Error code
 * @return Human-readable error message
 */
const char *command_layer_get_error_message(command_layer_error_t error);

/**
 * Apply LLE theme syntax colors to command layer's spec highlighter
 *
 * @param lle_theme LLE theme with syntax colors
 * @param cmd_layer Command layer to apply colors to
 * @return LLE_SUCCESS on success, error code on failure
 */
struct lle_theme;
typedef struct lle_theme lle_theme_t;
lle_result_t command_layer_apply_theme_colors(const lle_theme_t *lle_theme,
                                              command_layer_t *cmd_layer);

#ifdef __cplusplus
}
#endif

#endif /// COMMAND_LAYER_H
