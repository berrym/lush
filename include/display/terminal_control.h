/**
 * @file terminal_control.h
 * @brief Terminal control layer (Layer 2) - ANSI sequences and capabilities
 *
 * Provides ANSI sequence management, terminal capability detection, and
 * low-level terminal control operations for the display system.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - ANSI escape sequence generation and management
 * - Terminal capability detection and validation
 * - Cursor positioning and movement
 * - Color management and validation
 * - Screen clearing and manipulation
 * - Performance optimization for terminal operations
 *
 * Design Principles:
 * - Efficient ANSI sequence generation
 * - Comprehensive capability detection
 * - Cross-platform terminal compatibility
 * - Performance-optimized operations
 * - Caching for frequently used sequences
 */

#ifndef TERMINAL_CONTROL_H
#define TERMINAL_CONTROL_H

#include "base_terminal.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

#define TERMINAL_CONTROL_VERSION_MAJOR 1
#define TERMINAL_CONTROL_VERSION_MINOR 0
#define TERMINAL_CONTROL_VERSION_PATCH 0

#define TERMINAL_CONTROL_MAX_SEQUENCE_LENGTH 64
#define TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH 64
#define TERMINAL_CONTROL_CAPABILITY_CACHE_SIZE 32

// Common ANSI color codes
#define TERMINAL_COLOR_BLACK 0
#define TERMINAL_COLOR_RED 1
#define TERMINAL_COLOR_GREEN 2
#define TERMINAL_COLOR_YELLOW 3
#define TERMINAL_COLOR_BLUE 4
#define TERMINAL_COLOR_MAGENTA 5
#define TERMINAL_COLOR_CYAN 6
#define TERMINAL_COLOR_WHITE 7

// Bright color variants (8-15)
#define TERMINAL_COLOR_BRIGHT_BLACK 8
#define TERMINAL_COLOR_BRIGHT_RED 9
#define TERMINAL_COLOR_BRIGHT_GREEN 10
#define TERMINAL_COLOR_BRIGHT_YELLOW 11
#define TERMINAL_COLOR_BRIGHT_BLUE 12
#define TERMINAL_COLOR_BRIGHT_MAGENTA 13
#define TERMINAL_COLOR_BRIGHT_CYAN 14
#define TERMINAL_COLOR_BRIGHT_WHITE 15

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

/**
 * Error codes for terminal control operations
 */
typedef enum {
    TERMINAL_CONTROL_SUCCESS = 0,         // Operation completed successfully
    TERMINAL_CONTROL_ERROR_INVALID_PARAM, // Invalid parameter provided
    TERMINAL_CONTROL_ERROR_MEMORY_ALLOCATION,     // Memory allocation failed
    TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION,  // Capability detection failed
    TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG,     // Generated sequence too long
    TERMINAL_CONTROL_ERROR_UNSUPPORTED_OPERATION, // Operation not supported by
                                                  // terminal
    TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE, // Color value out of valid range
    TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE, // Cursor position out of
                                                  // range
    TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY     // Terminal not properly
                                                  // initialized
} terminal_control_error_t;

/**
 * Terminal capability flags
 */
typedef enum {
    TERMINAL_CAP_NONE = 0,                      // No special capabilities
    TERMINAL_CAP_COLOR_8 = (1 << 0),            // 8-color support
    TERMINAL_CAP_COLOR_16 = (1 << 1),           // 16-color support
    TERMINAL_CAP_COLOR_256 = (1 << 2),          // 256-color support
    TERMINAL_CAP_COLOR_TRUECOLOR = (1 << 3),    // 24-bit RGB color support
    TERMINAL_CAP_CURSOR_POSITIONING = (1 << 4), // Cursor positioning support
    TERMINAL_CAP_CURSOR_VISIBILITY = (1 << 5),  // Cursor visibility control
    TERMINAL_CAP_UNICODE = (1 << 6),            // Unicode character support
    TERMINAL_CAP_BOLD = (1 << 7),               // Bold text support
    TERMINAL_CAP_ITALIC = (1 << 8),             // Italic text support
    TERMINAL_CAP_UNDERLINE = (1 << 9),          // Underline text support
    TERMINAL_CAP_REVERSE = (1 << 10),           // Reverse video support
    TERMINAL_CAP_STRIKETHROUGH = (1 << 11),     // Strikethrough text support
    TERMINAL_CAP_ALTERNATE_SCREEN = (1 << 12),  // Alternate screen buffer
    TERMINAL_CAP_MOUSE_REPORTING = (1 << 13),   // Mouse event reporting
    TERMINAL_CAP_BRACKETED_PASTE = (1 << 14),   // Bracketed paste mode
    TERMINAL_CAP_WINDOW_TITLE = (1 << 15)       // Window title setting
} terminal_capability_flags_t;

/**
 * Text style attributes
 */
typedef enum {
    TERMINAL_STYLE_NONE = 0,
    TERMINAL_STYLE_BOLD = (1 << 0),
    TERMINAL_STYLE_ITALIC = (1 << 1),
    TERMINAL_STYLE_UNDERLINE = (1 << 2),
    TERMINAL_STYLE_REVERSE = (1 << 3),
    TERMINAL_STYLE_STRIKETHROUGH = (1 << 4)
} terminal_style_flags_t;

/**
 * Color specification structure
 */
typedef struct {
    enum {
        TERMINAL_COLOR_TYPE_DEFAULT, // Use terminal default
        TERMINAL_COLOR_TYPE_BASIC,   // Basic 8/16 colors (0-15)
        TERMINAL_COLOR_TYPE_256,     // 256-color palette (0-255)
        TERMINAL_COLOR_TYPE_RGB      // 24-bit RGB color
    } type;

    union {
        uint8_t basic;   // Basic color index (0-15)
        uint8_t palette; // 256-color palette index
        struct {
            uint8_t r, g, b; // RGB values (0-255)
        } rgb;
    } value;
} terminal_color_t;

/**
 * Terminal capabilities structure
 */
typedef struct {
    int terminal_width;                // Current terminal width in columns
    int terminal_height;               // Current terminal height in rows
    terminal_capability_flags_t flags; // Supported capability flags
    char terminal_name
        [TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH]; // Terminal name from $TERM

    // Detailed capability information
    int max_colors;                  // Maximum number of colors supported
    bool cursor_positioning_support; // Can position cursor arbitrarily
    bool unicode_support;            // Supports Unicode characters
    bool mouse_support;              // Supports mouse reporting

    // Performance optimization data
    bool sequence_caching_enabled; // Whether to cache generated sequences
    uint32_t cache_hit_count;      // Number of cache hits
    uint32_t cache_miss_count;     // Number of cache misses
} terminal_capabilities_t;

/**
 * Cursor position structure
 */
typedef struct {
    int row;    // Row position (1-based)
    int column; // Column position (1-based)
} terminal_position_t;

/**
 * ANSI sequence cache entry
 */
typedef struct {
    char sequence[TERMINAL_CONTROL_MAX_SEQUENCE_LENGTH]; // Cached sequence
    uint32_t hash;      // Hash of the parameters
    uint64_t last_used; // Timestamp of last use
    bool valid;         // Whether this entry is valid
} sequence_cache_entry_t;

/**
 * Terminal control instance structure
 */
typedef struct {
    base_terminal_t *base_terminal;       // Reference to base terminal
    terminal_capabilities_t capabilities; // Terminal capabilities
    bool initialized;                    // Whether control layer is initialized
    terminal_control_error_t last_error; // Last error that occurred

    // Sequence caching for performance
    sequence_cache_entry_t
        sequence_cache[TERMINAL_CONTROL_CAPABILITY_CACHE_SIZE];
    uint32_t cache_next_index; // Next cache slot to use

    // Current state
    terminal_position_t cursor_position;  // Last known cursor position
    terminal_color_t current_fg_color;    // Current foreground color
    terminal_color_t current_bg_color;    // Current background color
    terminal_style_flags_t current_style; // Current text style

    // Performance metrics
    uint64_t sequences_generated;      // Total sequences generated
    uint64_t sequences_cached;         // Sequences served from cache
    uint64_t total_generation_time_ns; // Time spent generating sequences
} terminal_control_t;

// ============================================================================
// LIFECYCLE FUNCTIONS
// ============================================================================

/**
 * Create a new terminal control instance
 *
 * Creates and initializes a new terminal control instance associated with
 * the given base terminal. The terminal control layer provides ANSI sequence
 * management and capability detection on top of the base terminal.
 *
 * @param base_terminal Pointer to initialized base terminal instance
 * @return Pointer to new terminal_control_t instance, or NULL on failure
 *
 * @note The base terminal must be initialized before creating control layer
 * @note The returned pointer must be freed with terminal_control_destroy()
 */
terminal_control_t *terminal_control_create(base_terminal_t *base_terminal);

/**
 * Initialize the terminal control system
 *
 * Performs complete initialization of the terminal control layer including:
 * - Detection of terminal capabilities
 * - Cache initialization
 * - Initial state setup
 * - Performance metrics initialization
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Must be called before any other terminal control operations
 */
terminal_control_error_t terminal_control_init(terminal_control_t *control);

/**
 * Clean up terminal control resources
 *
 * Cleans up all resources used by the terminal control layer and resets
 * the terminal to a clean state.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Safe to call multiple times
 * @note Does not free the control structure itself
 */
terminal_control_error_t terminal_control_cleanup(terminal_control_t *control);

/**
 * Destroy terminal control instance
 *
 * Performs cleanup and frees the terminal control structure. After calling
 * this function, the control pointer is invalid and should not be used.
 *
 * @param control Pointer to terminal_control_t instance
 *
 * @note Automatically calls terminal_control_cleanup() if needed
 * @note Safe to call with NULL pointer
 */
void terminal_control_destroy(terminal_control_t *control);

// ============================================================================
// CAPABILITY DETECTION FUNCTIONS
// ============================================================================

/**
 * Detect terminal capabilities
 *
 * Performs comprehensive detection of terminal capabilities including
 * color support, cursor positioning, Unicode support, and other features.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Results are cached in the capabilities structure
 * @note May take some time on first call due to terminal queries
 */
terminal_control_error_t
terminal_control_detect_capabilities(terminal_control_t *control);

/**
 * Get current terminal capabilities
 *
 * Returns a copy of the current terminal capabilities structure.
 *
 * @param control Pointer to terminal_control_t instance
 * @return Copy of terminal capabilities structure
 *
 * @note Returns empty capabilities if control is NULL or uninitialized
 */
terminal_capabilities_t
terminal_control_get_capabilities(terminal_control_t *control);

/**
 * Check if terminal supports specific capability
 *
 * Checks whether the terminal supports a specific capability flag.
 *
 * @param control Pointer to terminal_control_t instance
 * @param capability Capability flag to check
 * @return true if capability is supported, false otherwise
 */
bool terminal_control_has_capability(terminal_control_t *control,
                                     terminal_capability_flags_t capability);

/**
 * Update terminal size
 *
 * Updates the cached terminal size information. This should be called
 * when the terminal is resized.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_update_size(terminal_control_t *control);

// ============================================================================
// CURSOR CONTROL FUNCTIONS
// ============================================================================

/**
 * Move cursor to specified position
 *
 * Generates and sends ANSI sequence to move cursor to the specified position.
 * Position is 1-based (top-left corner is 1,1).
 *
 * @param control Pointer to terminal_control_t instance
 * @param row Target row (1-based)
 * @param column Target column (1-based)
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Position is validated against current terminal size
 * @note Updates internal cursor position tracking
 */
terminal_control_error_t
terminal_control_move_cursor(terminal_control_t *control, int row, int column);

/**
 * Move cursor by relative offset
 *
 * Moves cursor by the specified offset from current position.
 *
 * @param control Pointer to terminal_control_t instance
 * @param row_offset Rows to move (positive = down, negative = up)
 * @param column_offset Columns to move (positive = right, negative = left)
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_move_cursor_relative(terminal_control_t *control,
                                      int row_offset, int column_offset);

/**
 * Get current cursor position
 *
 * Returns the last known cursor position. Note that this may not reflect
 * the actual terminal cursor position if it was moved by other means.
 *
 * @param control Pointer to terminal_control_t instance
 * @return Current cursor position structure
 */
terminal_position_t
terminal_control_get_cursor_position(terminal_control_t *control);

/**
 * Show or hide cursor
 *
 * Controls cursor visibility if supported by the terminal.
 *
 * @param control Pointer to terminal_control_t instance
 * @param visible true to show cursor, false to hide
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_set_cursor_visible(terminal_control_t *control, bool visible);

/**
 * Save current cursor position
 *
 * Saves the current cursor position to terminal's internal stack.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_save_cursor(terminal_control_t *control);

/**
 * Restore saved cursor position
 *
 * Restores cursor position from terminal's internal stack.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_restore_cursor(terminal_control_t *control);

// ============================================================================
// SCREEN CONTROL FUNCTIONS
// ============================================================================

/**
 * Clear entire screen
 *
 * Clears the entire terminal screen and moves cursor to top-left.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_screen(terminal_control_t *control);

/**
 * Clear from cursor to end of line
 *
 * Clears from current cursor position to end of current line.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_to_end_of_line(terminal_control_t *control);

/**
 * Clear from cursor to beginning of line
 *
 * Clears from beginning of current line to cursor position.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_to_beginning_of_line(terminal_control_t *control);

/**
 * Clear entire current line
 *
 * Clears the entire current line without moving cursor.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_line(terminal_control_t *control);

/**
 * Clear from cursor to end of screen
 *
 * Clears from cursor position to end of screen.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_to_end_of_screen(terminal_control_t *control);

// ============================================================================
// COLOR AND STYLE FUNCTIONS
// ============================================================================

/**
 * Set foreground color
 *
 * Sets the foreground (text) color using the specified color specification.
 *
 * @param control Pointer to terminal_control_t instance
 * @param color Color specification
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Color type is validated against terminal capabilities
 */
terminal_control_error_t
terminal_control_set_foreground_color(terminal_control_t *control,
                                      terminal_color_t color);

/**
 * Set background color
 *
 * Sets the background color using the specified color specification.
 *
 * @param control Pointer to terminal_control_t instance
 * @param color Color specification
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_set_background_color(terminal_control_t *control,
                                      terminal_color_t color);

/**
 * Set text style attributes
 *
 * Sets text style attributes (bold, italic, underline, etc.).
 *
 * @param control Pointer to terminal_control_t instance
 * @param style Style flags to apply
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 *
 * @note Style flags are validated against terminal capabilities
 */
terminal_control_error_t
terminal_control_set_style(terminal_control_t *control,
                           terminal_style_flags_t style);

/**
 * Reset all colors and styles
 *
 * Resets all colors and text styles to terminal defaults.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_reset_formatting(terminal_control_t *control);

// ============================================================================
// SEQUENCE GENERATION FUNCTIONS
// ============================================================================

/**
 * Generate ANSI sequence for cursor movement
 *
 * Generates an ANSI escape sequence for moving cursor to specified position.
 *
 * @param control Pointer to terminal_control_t instance
 * @param row Target row (1-based)
 * @param column Target column (1-based)
 * @param buffer Buffer to store generated sequence
 * @param buffer_size Size of buffer
 * @return Number of bytes written to buffer, or -1 on error
 */
ssize_t terminal_control_generate_cursor_sequence(terminal_control_t *control,
                                                  int row, int column,
                                                  char *buffer,
                                                  size_t buffer_size);

/**
 * Generate ANSI sequence for color setting
 *
 * Generates an ANSI escape sequence for setting foreground or background color.
 *
 * @param control Pointer to terminal_control_t instance
 * @param color Color specification
 * @param is_background true for background color, false for foreground
 * @param buffer Buffer to store generated sequence
 * @param buffer_size Size of buffer
 * @return Number of bytes written to buffer, or -1 on error
 */
ssize_t terminal_control_generate_color_sequence(terminal_control_t *control,
                                                 terminal_color_t color,
                                                 bool is_background,
                                                 char *buffer,
                                                 size_t buffer_size);

/**
 * Generate ANSI sequence for style setting
 *
 * Generates an ANSI escape sequence for setting text style attributes.
 *
 * @param control Pointer to terminal_control_t instance
 * @param style Style flags
 * @param buffer Buffer to store generated sequence
 * @param buffer_size Size of buffer
 * @return Number of bytes written to buffer, or -1 on error
 */
ssize_t terminal_control_generate_style_sequence(terminal_control_t *control,
                                                 terminal_style_flags_t style,
                                                 char *buffer,
                                                 size_t buffer_size);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Create color specification from RGB values
 *
 * Creates a terminal_color_t structure from RGB values, automatically
 * selecting the best color type based on terminal capabilities.
 *
 * @param control Pointer to terminal_control_t instance
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Color specification structure
 */
terminal_color_t terminal_control_color_from_rgb(terminal_control_t *control,
                                                 uint8_t r, uint8_t g,
                                                 uint8_t b);

/**
 * Create color specification from basic color index
 *
 * Creates a terminal_color_t structure for a basic color (0-15).
 *
 * @param color_index Basic color index (0-15)
 * @return Color specification structure
 */
terminal_color_t terminal_control_color_from_basic(uint8_t color_index);

/**
 * Create color specification for default color
 *
 * Creates a terminal_color_t structure for terminal default color.
 *
 * @return Default color specification structure
 */
terminal_color_t terminal_control_color_default(void);

/**
 * Validate color against terminal capabilities
 *
 * Checks if the specified color can be displayed by the terminal.
 *
 * @param control Pointer to terminal_control_t instance
 * @param color Color specification to validate
 * @return true if color is supported, false otherwise
 */
bool terminal_control_validate_color(terminal_control_t *control,
                                     terminal_color_t color);

// ============================================================================
// PERFORMANCE AND DIAGNOSTICS
// ============================================================================

/**
 * Get performance metrics
 *
 * Returns performance statistics for the terminal control layer.
 *
 * @param control Pointer to terminal_control_t instance
 * @param sequences_generated Pointer to store total sequences generated
 * @param cache_hit_rate Pointer to store cache hit rate (0.0-1.0)
 * @param avg_generation_time_ns Pointer to store average generation time
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t terminal_control_get_performance_metrics(
    terminal_control_t *control, uint64_t *sequences_generated,
    double *cache_hit_rate, uint64_t *avg_generation_time_ns);

/**
 * Clear performance metrics
 *
 * Resets all performance counters to zero.
 *
 * @param control Pointer to terminal_control_t instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_clear_metrics(terminal_control_t *control);

/**
 * Enable or disable sequence caching
 *
 * Controls whether generated ANSI sequences are cached for performance.
 *
 * @param control Pointer to terminal_control_t instance
 * @param enabled true to enable caching, false to disable
 * @return TERMINAL_CONTROL_SUCCESS on success, error code on failure
 */
terminal_control_error_t
terminal_control_set_caching_enabled(terminal_control_t *control, bool enabled);

// ============================================================================
// ERROR HANDLING
// ============================================================================

/**
 * Get last error code
 *
 * Returns the last error that occurred on this terminal control instance.
 *
 * @param control Pointer to terminal_control_t instance
 * @return Last error code, or TERMINAL_CONTROL_ERROR_INVALID_PARAM if control
 * is NULL
 */
terminal_control_error_t
terminal_control_get_last_error(terminal_control_t *control);

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
const char *terminal_control_error_string(terminal_control_error_t error);

// ============================================================================
// VERSION INFORMATION
// ============================================================================

/**
 * Get terminal control layer version
 *
 * Returns version information for the terminal control layer implementation.
 *
 * @param major Pointer to store major version number
 * @param minor Pointer to store minor version number
 * @param patch Pointer to store patch version number
 */
static inline void terminal_control_get_version(int *major, int *minor,
                                                int *patch) {
    if (major) {
        *major = TERMINAL_CONTROL_VERSION_MAJOR;
    }
    if (minor) {
        *minor = TERMINAL_CONTROL_VERSION_MINOR;
    }
    if (patch) {
        *patch = TERMINAL_CONTROL_VERSION_PATCH;
    }
}

#ifdef __cplusplus
}
#endif

#endif // TERMINAL_CONTROL_H

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/*
 * Basic usage example:
 *
 * ```c
 * #include "terminal_control.h"
 *
 * int main() {
 *     // Create base terminal
 *     base_terminal_t *base = base_terminal_create();
 *     base_terminal_init(base);
 *
 *     // Create terminal control layer
 *     terminal_control_t *control = terminal_control_create(base);
 *     if (!control) return 1;
 *
 *     if (terminal_control_init(control) != TERMINAL_CONTROL_SUCCESS) {
 *         terminal_control_destroy(control);
 *         return 1;
 *     }
 *
 *     // Use terminal control features
 *     terminal_control_clear_screen(control);
 *     terminal_control_move_cursor(control, 1, 1);
 *
 *     // Set colors
 *     terminal_color_t red =
 * terminal_control_color_from_basic(TERMINAL_COLOR_RED);
 *     terminal_control_set_foreground_color(control, red);
 *
 *     // Write colored text (through base terminal)
 *     base_terminal_write(base, "Hello, colorful world!\n", 23);
 *
 *     // Reset formatting
 *     terminal_control_reset_formatting(control);
 *
 *     // Cleanup
 *     terminal_control_destroy(control);
 *     base_terminal_destroy(base);
 *     return 0;
 * }
 * ```
 *
 * Advanced color example:
 *
 * ```c
 * // RGB color (if supported)
 * terminal_color_t purple = terminal_control_color_from_rgb(control, 128, 0,
 * 128); if (terminal_control_validate_color(control, purple)) {
 *     terminal_control_set_background_color(control, purple);
 * }
 *
 * // Style combination
 * terminal_style_flags_t style = TERMINAL_STYLE_BOLD |
 * TERMINAL_STYLE_UNDERLINE; terminal_control_set_style(control, style);
 * ```
 */
