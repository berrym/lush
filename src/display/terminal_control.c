/**
 * @file terminal_control.c
 * @brief Terminal Control Layer - ANSI Sequences and Capabilities
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Manages ANSI escape sequences and terminal capabilities.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * TERMINAL CONTROL LAYER (Layer 2)
 *
 * This layer provides ANSI sequence management, terminal capability detection,
 * and low-level terminal control operations. It builds on the base terminal
 * layer to provide high-level terminal manipulation capabilities.
 *
 * Key Responsibilities:
 * - ANSI escape sequence generation and management
 * - Terminal capability detection and validation
 * - Cursor positioning and movement
 * - Color management and validation
 * - Screen clearing and manipulation
 * - Performance optimization through sequence caching
 *
 * Design Principles:
 * - Efficient ANSI sequence generation with caching
 * - Comprehensive capability detection
 * - Cross-platform terminal compatibility
 * - Performance-optimized operations
 * - Safe fallbacks for unsupported operations
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "terminal_control.h"

/// ============================================================================
/// CONSTANTS AND CONFIGURATION
/// ============================================================================

/// Common ANSI escape sequences
#define ESC "\033"
#define CSI "\033["

/// Terminal capability test sequences
#define TERMINAL_QUERY_DEVICE_ATTRIBUTES CSI "c"
#define TERMINAL_QUERY_CURSOR_POSITION CSI "6n"
#define TERMINAL_TEST_256_COLOR CSI "38;5;196m"
#define TERMINAL_TEST_TRUECOLOR CSI "38;2;255;0;0m"

/// Maximum time to wait for terminal responses (milliseconds)
#define CAPABILITY_DETECTION_TIMEOUT_MS 100

/// Hash function for sequence caching
#define HASH_MULTIPLIER 31
#define HASH_SEED 0x12345678

/// ============================================================================
/// STATIC VARIABLES
/// ============================================================================

static bool terminal_control_initialized = false;

/// ============================================================================
/// FORWARD DECLARATIONS
/// ============================================================================

static terminal_control_error_t
detect_color_support(terminal_control_t *control);
static terminal_control_error_t
detect_cursor_capabilities(terminal_control_t *control);
static terminal_control_error_t
detect_unicode_support(terminal_control_t *control);
static terminal_control_error_t
detect_style_support(terminal_control_t *control);

static uint32_t calculate_sequence_hash(const char *format, ...);
static sequence_cache_entry_t *find_cached_sequence(terminal_control_t *control,
                                                    uint32_t hash);
static void cache_sequence(terminal_control_t *control, uint32_t hash,
                           const char *sequence);

static ssize_t send_sequence(terminal_control_t *control, const char *sequence);
static ssize_t generate_sequence(char *buffer, size_t buffer_size,
                                 const char *format, ...);

static bool validate_position(terminal_control_t *control, int row, int column);
static uint8_t rgb_to_256_color(uint8_t r, uint8_t g, uint8_t b);

/// ============================================================================
/// LIFECYCLE FUNCTIONS
/// ============================================================================

/**
 * @brief Create a new terminal control instance
 * @param base_terminal Base terminal layer to build upon
 * @return Pointer to new terminal control instance, or NULL on failure
 */
terminal_control_t *terminal_control_create(base_terminal_t *base_terminal) {
    if (!base_terminal) {
        return NULL;
    }

    terminal_control_t *control = calloc(1, sizeof(terminal_control_t));
    if (!control) {
        return NULL;
    }

    /// Initialize with default values
    control->base_terminal = base_terminal;
    control->initialized = false;
    control->last_error = TERMINAL_CONTROL_SUCCESS;

    /// Initialize capabilities with defaults
    control->capabilities.terminal_width = 80;
    control->capabilities.terminal_height = 24;
    control->capabilities.flags = TERMINAL_CAP_NONE;
    control->capabilities.max_colors = 8;
    control->capabilities.cursor_positioning_support = false;
    control->capabilities.unicode_support = false;
    control->capabilities.mouse_support = false;
    control->capabilities.sequence_caching_enabled = true;

    /// Initialize cursor position
    control->cursor_position.row = 1;
    control->cursor_position.column = 1;

    /// Initialize colors to default
    control->current_fg_color = terminal_control_color_default();
    control->current_bg_color = terminal_control_color_default();
    control->current_style = TERMINAL_STYLE_NONE;

    /// Initialize cache
    memset(control->sequence_cache, 0, sizeof(control->sequence_cache));
    control->cache_next_index = 0;

    /// Initialize performance metrics
    control->sequences_generated = 0;
    control->sequences_cached = 0;
    control->total_generation_time_ns = 0;

    return control;
}

/**
 * @brief Initialize the terminal control system
 * @param control Terminal control instance to initialize
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t terminal_control_init(terminal_control_t *control) {
    if (!control || !control->base_terminal) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (control->initialized) {
        return TERMINAL_CONTROL_SUCCESS; /// Already initialized
    }

    uint64_t start_time = base_terminal_get_timestamp_ns();

    /// Detect terminal capabilities
    terminal_control_error_t result =
        terminal_control_detect_capabilities(control);
    if (result != TERMINAL_CONTROL_SUCCESS) {
        control->last_error = result;
        return result;
    }

    /// Update terminal size
    result = terminal_control_update_size(control);
    if (result != TERMINAL_CONTROL_SUCCESS) {
        /// Non-fatal - use defaults
        control->capabilities.terminal_width = 80;
        control->capabilities.terminal_height = 24;
    }

    control->initialized = true;
    terminal_control_initialized = true;

    uint64_t end_time = base_terminal_get_timestamp_ns();
    control->total_generation_time_ns += (end_time - start_time);

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Clean up terminal control resources
 * @param control Terminal control instance to cleanup
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t terminal_control_cleanup(terminal_control_t *control) {
    if (!control) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (!control->initialized) {
        return TERMINAL_CONTROL_SUCCESS; /// Nothing to clean up
    }

    /// Reset terminal formatting
    terminal_control_reset_formatting(control);

    /// Clear cache
    memset(control->sequence_cache, 0, sizeof(control->sequence_cache));
    control->cache_next_index = 0;

    control->initialized = false;

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Destroy terminal control instance and free resources
 * @param control Terminal control instance to destroy
 */
void terminal_control_destroy(terminal_control_t *control) {
    if (!control) {
        return;
    }

    terminal_control_cleanup(control);

    /// Destroy base terminal if we own it
    if (control->base_terminal) {
        base_terminal_destroy(control->base_terminal);
        control->base_terminal = NULL;
    }

    free(control);
}

/// ============================================================================
/// CAPABILITY DETECTION FUNCTIONS
/// ============================================================================

/**
 * @brief Detect terminal capabilities
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_detect_capabilities(terminal_control_t *control) {
    if (!control || !control->base_terminal) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    /// Get terminal name from environment
    const char *term_env = getenv("TERM");
    if (term_env) {
        strncpy(control->capabilities.terminal_name, term_env,
                TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH - 1);
        control->capabilities
            .terminal_name[TERMINAL_CONTROL_MAX_TERMINAL_NAME_LENGTH - 1] =
            '\0';
    } else {
        strcpy(control->capabilities.terminal_name, "unknown");
    }

    /// Detect various capabilities
    detect_color_support(control);
    detect_cursor_capabilities(control);
    detect_unicode_support(control);
    detect_style_support(control);

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Get current terminal capabilities
 * @param control Terminal control instance
 * @return Terminal capabilities structure (empty if control invalid)
 */
terminal_capabilities_t
terminal_control_get_capabilities(terminal_control_t *control) {
    terminal_capabilities_t empty_capabilities = {0};

    if (!control || !control->initialized) {
        return empty_capabilities;
    }

    return control->capabilities;
}

/**
 * @brief Check if terminal supports specific capability
 * @param control Terminal control instance
 * @param capability Capability flag to check
 * @return true if capability is supported, false otherwise
 */
bool terminal_control_has_capability(terminal_control_t *control,
                                     terminal_capability_flags_t capability) {
    if (!control || !control->initialized) {
        return false;
    }

    return (control->capabilities.flags & capability) != 0;
}

/**
 * @brief Update terminal size from system
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_update_size(terminal_control_t *control) {
    if (!control || !control->base_terminal) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    int width, height;
    base_terminal_error_t result =
        base_terminal_get_size(control->base_terminal, &width, &height);

    if (result == BASE_TERMINAL_SUCCESS) {
        control->capabilities.terminal_width = width;
        control->capabilities.terminal_height = height;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        control->last_error = TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION;
        return TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION;
    }
}

/// ============================================================================
/// CURSOR CONTROL FUNCTIONS
/// ============================================================================

/**
 * @brief Move cursor to specified position
 * @param control Terminal control instance
 * @param row Target row (1-based)
 * @param column Target column (1-based)
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_move_cursor(terminal_control_t *control, int row, int column) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (!validate_position(control, row, column)) {
        control->last_error = TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE;
        return TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE;
    }

    char sequence[32];
    ssize_t len = generate_sequence(sequence, sizeof(sequence), CSI "%d;%dH",
                                    row, column);

    if (len < 0) {
        control->last_error = TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG;
        return TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG;
    }

    ssize_t result = send_sequence(control, sequence);
    if (result > 0) {
        control->cursor_position.row = row;
        control->cursor_position.column = column;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        control->last_error = TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/**
 * @brief Move cursor by relative offset from current position
 * @param control Terminal control instance
 * @param row_offset Row offset to move (positive = down)
 * @param column_offset Column offset to move (positive = right)
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_move_cursor_relative(terminal_control_t *control,
                                      int row_offset, int column_offset) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    int new_row = control->cursor_position.row + row_offset;
    int new_column = control->cursor_position.column + column_offset;

    return terminal_control_move_cursor(control, new_row, new_column);
}

/**
 * @brief Get current cursor position
 * @param control Terminal control instance
 * @return Current cursor position (row=-1, column=-1 if invalid)
 */
terminal_position_t
terminal_control_get_cursor_position(terminal_control_t *control) {
    terminal_position_t invalid_position = {-1, -1};

    if (!control || !control->initialized) {
        return invalid_position;
    }

    return control->cursor_position;
}

/**
 * @brief Show or hide cursor
 * @param control Terminal control instance
 * @param visible true to show cursor, false to hide
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_set_cursor_visible(terminal_control_t *control, bool visible) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (!terminal_control_has_capability(control,
                                         TERMINAL_CAP_CURSOR_VISIBILITY)) {
        return TERMINAL_CONTROL_ERROR_UNSUPPORTED_OPERATION;
    }

    const char *sequence = visible ? CSI "?25h" : CSI "?25l";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/**
 * @brief Save current cursor position to terminal stack
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_save_cursor(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = ESC "7";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/**
 * @brief Restore saved cursor position from terminal stack
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_restore_cursor(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = ESC "8";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/// ============================================================================
/// SCREEN CONTROL FUNCTIONS
/// ============================================================================

/**
 * @brief Clear entire screen and move cursor to home position
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_screen(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "2J" CSI "H";
    ssize_t result = send_sequence(control, sequence);

    if (result > 0) {
        control->cursor_position.row = 1;
        control->cursor_position.column = 1;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/**
 * @brief Clear from cursor to end of line
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_to_end_of_line(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "K";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/**
 * @brief Clear from cursor to beginning of line
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_to_beginning_of_line(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "1K";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/**
 * @brief Clear entire current line
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_line(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "2K";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/**
 * @brief Clear from cursor to end of screen
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_to_end_of_screen(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "J";
    ssize_t result = send_sequence(control, sequence);

    return (result > 0) ? TERMINAL_CONTROL_SUCCESS
                        : TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
}

/// ============================================================================
/// COLOR AND STYLE FUNCTIONS
/// ============================================================================

/**
 * @brief Set foreground color for subsequent text output
 * @param control Terminal control instance
 * @param color Color specification to apply
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_set_foreground_color(terminal_control_t *control,
                                      terminal_color_t color) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (!terminal_control_validate_color(control, color)) {
        return TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE;
    }

    char sequence[64];
    ssize_t len = terminal_control_generate_color_sequence(
        control, color, false, sequence, sizeof(sequence));

    if (len < 0) {
        return TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG;
    }

    ssize_t result = send_sequence(control, sequence);
    if (result > 0) {
        control->current_fg_color = color;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/**
 * @brief Set background color for subsequent text output
 * @param control Terminal control instance
 * @param color Color specification to apply
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_set_background_color(terminal_control_t *control,
                                      terminal_color_t color) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (!terminal_control_validate_color(control, color)) {
        return TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE;
    }

    char sequence[64];
    ssize_t len = terminal_control_generate_color_sequence(
        control, color, true, sequence, sizeof(sequence));

    if (len < 0) {
        return TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG;
    }

    ssize_t result = send_sequence(control, sequence);
    if (result > 0) {
        control->current_bg_color = color;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/**
 * @brief Set text style attributes (bold, italic, underline, etc.)
 * @param control Terminal control instance
 * @param style Style flags to apply
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_set_style(terminal_control_t *control,
                           terminal_style_flags_t style) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    char sequence[64];
    ssize_t len = terminal_control_generate_style_sequence(
        control, style, sequence, sizeof(sequence));

    if (len < 0) {
        return TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG;
    }

    ssize_t result = send_sequence(control, sequence);
    if (result > 0) {
        control->current_style = style;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/**
 * @brief Reset all colors and styles to terminal defaults
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_reset_formatting(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    const char *sequence = CSI "0m";
    ssize_t result = send_sequence(control, sequence);

    if (result > 0) {
        control->current_fg_color = terminal_control_color_default();
        control->current_bg_color = terminal_control_color_default();
        control->current_style = TERMINAL_STYLE_NONE;
        return TERMINAL_CONTROL_SUCCESS;
    } else {
        return TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY;
    }
}

/// ============================================================================
/// SEQUENCE GENERATION FUNCTIONS
/// ============================================================================

/**
 * @brief Generate ANSI sequence for cursor movement
 * @param control Terminal control instance
 * @param row Target row position (1-based)
 * @param column Target column position (1-based)
 * @param buffer Output buffer for generated sequence
 * @param buffer_size Size of output buffer
 * @return Length of generated sequence, or -1 on error
 */
ssize_t terminal_control_generate_cursor_sequence(terminal_control_t *control,
                                                  int row, int column,
                                                  char *buffer,
                                                  size_t buffer_size) {
    if (!control || !buffer || buffer_size == 0) {
        return -1;
    }

    if (!validate_position(control, row, column)) {
        return -1;
    }

    return generate_sequence(buffer, buffer_size, CSI "%d;%dH", row, column);
}

/**
 * @brief Generate ANSI sequence for color setting
 * @param control Terminal control instance
 * @param color Color specification to generate sequence for
 * @param is_background true for background color, false for foreground
 * @param buffer Output buffer for generated sequence
 * @param buffer_size Size of output buffer
 * @return Length of generated sequence, or -1 on error
 */
ssize_t terminal_control_generate_color_sequence(terminal_control_t *control,
                                                 terminal_color_t color,
                                                 bool is_background,
                                                 char *buffer,
                                                 size_t buffer_size) {
    if (!control || !buffer || buffer_size == 0) {
        return -1;
    }

    int base_code = is_background ? 40 : 30;

    switch (color.type) {
    case TERMINAL_COLOR_TYPE_DEFAULT:
        return generate_sequence(buffer, buffer_size, CSI "%dm",
                                 is_background ? 49 : 39);

    case TERMINAL_COLOR_TYPE_BASIC:
        if (color.value.basic < 8) {
            return generate_sequence(buffer, buffer_size, CSI "%dm",
                                     base_code + color.value.basic);
        } else if (color.value.basic < 16) {
            return generate_sequence(buffer, buffer_size, CSI "%dm",
                                     base_code + 60 + (color.value.basic - 8));
        }
        return -1;

    case TERMINAL_COLOR_TYPE_256:
        return generate_sequence(buffer, buffer_size, CSI "%d;5;%dm",
                                 is_background ? 48 : 38, color.value.palette);

    case TERMINAL_COLOR_TYPE_RGB:
        return generate_sequence(buffer, buffer_size, CSI "%d;2;%d;%d;%dm",
                                 is_background ? 48 : 38, color.value.rgb.r,
                                 color.value.rgb.g, color.value.rgb.b);

    default:
        return -1;
    }
}

/**
 * @brief Generate ANSI sequence for style setting
 * @param control Terminal control instance
 * @param style Style flags to generate sequence for
 * @param buffer Output buffer for generated sequence
 * @param buffer_size Size of output buffer
 * @return Length of generated sequence, or -1 on error
 */
ssize_t terminal_control_generate_style_sequence(terminal_control_t *control,
                                                 terminal_style_flags_t style,
                                                 char *buffer,
                                                 size_t buffer_size) {
    if (!control || !buffer || buffer_size == 0) {
        return -1;
    }

    if (style == TERMINAL_STYLE_NONE) {
        return generate_sequence(buffer, buffer_size, CSI "0m");
    }

    char codes[32];
    int pos = 0;

    if (style & TERMINAL_STYLE_BOLD) {
        pos += snprintf(codes + pos, sizeof(codes) - pos, "1;");
    }
    if (style & TERMINAL_STYLE_ITALIC) {
        pos += snprintf(codes + pos, sizeof(codes) - pos, "3;");
    }
    if (style & TERMINAL_STYLE_UNDERLINE) {
        pos += snprintf(codes + pos, sizeof(codes) - pos, "4;");
    }
    if (style & TERMINAL_STYLE_REVERSE) {
        pos += snprintf(codes + pos, sizeof(codes) - pos, "7;");
    }
    if (style & TERMINAL_STYLE_STRIKETHROUGH) {
        pos += snprintf(codes + pos, sizeof(codes) - pos, "9;");
    }

    /// Remove trailing semicolon
    if (pos > 0 && codes[pos - 1] == ';') {
        codes[pos - 1] = '\0';
    }

    return generate_sequence(buffer, buffer_size, CSI "%sm", codes);
}

/// ============================================================================
/// UTILITY FUNCTIONS
/// ============================================================================

/**
 * @brief Create color specification from RGB values
 * @param control Terminal control instance (used for capability detection)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Color specification appropriate for terminal capabilities
 */
terminal_color_t terminal_control_color_from_rgb(terminal_control_t *control,
                                                 uint8_t r, uint8_t g,
                                                 uint8_t b) {
    terminal_color_t color;

    if (control && terminal_control_has_capability(
                       control, TERMINAL_CAP_COLOR_TRUECOLOR)) {
        color.type = TERMINAL_COLOR_TYPE_RGB;
        color.value.rgb.r = r;
        color.value.rgb.g = g;
        color.value.rgb.b = b;
    } else if (control && terminal_control_has_capability(
                              control, TERMINAL_CAP_COLOR_256)) {
        color.type = TERMINAL_COLOR_TYPE_256;
        color.value.palette = rgb_to_256_color(r, g, b);
    } else {
        /// Fallback to basic colors
        color.type = TERMINAL_COLOR_TYPE_BASIC;
        /// Simple RGB to basic color mapping
        if (r > 127)
            color.value.basic =
                (g > 127)
                    ? ((b > 127) ? TERMINAL_COLOR_WHITE : TERMINAL_COLOR_YELLOW)
                    : ((b > 127) ? TERMINAL_COLOR_MAGENTA : TERMINAL_COLOR_RED);
        else
            color.value.basic =
                (g > 127)
                    ? ((b > 127) ? TERMINAL_COLOR_CYAN : TERMINAL_COLOR_GREEN)
                    : ((b > 127) ? TERMINAL_COLOR_BLUE : TERMINAL_COLOR_BLACK);
    }

    return color;
}

/**
 * @brief Create color specification from basic color index
 * @param color_index Basic color index (0-15)
 * @return Color specification for basic color
 */
terminal_color_t terminal_control_color_from_basic(uint8_t color_index) {
    terminal_color_t color;
    color.type = TERMINAL_COLOR_TYPE_BASIC;
    color.value.basic = color_index;
    return color;
}

/**
 * @brief Create color specification for default color
 * @return Color specification for terminal default color
 */
terminal_color_t terminal_control_color_default(void) {
    terminal_color_t color;
    color.type = TERMINAL_COLOR_TYPE_DEFAULT;
    return color;
}

/**
 * @brief Validate color against terminal capabilities
 * @param control Terminal control instance
 * @param color Color specification to validate
 * @return true if color is valid for terminal, false otherwise
 */
bool terminal_control_validate_color(terminal_control_t *control,
                                     terminal_color_t color) {
    if (!control) {
        return false;
    }

    switch (color.type) {
    case TERMINAL_COLOR_TYPE_DEFAULT:
        return true;

    case TERMINAL_COLOR_TYPE_BASIC:
        return color.value.basic < 16;

    case TERMINAL_COLOR_TYPE_256:
        /// palette is uint8_t, so always < 256 - just check capability
        return terminal_control_has_capability(control, TERMINAL_CAP_COLOR_256);

    case TERMINAL_COLOR_TYPE_RGB:
        return terminal_control_has_capability(control,
                                               TERMINAL_CAP_COLOR_TRUECOLOR);

    default:
        return false;
    }
}

/// ============================================================================
/// ERROR HANDLING
/// ============================================================================

/**
 * @brief Get last error code
 * @param control Terminal control instance
 * @return Last error code, or TERMINAL_CONTROL_ERROR_INVALID_PARAM if NULL
 */
terminal_control_error_t
terminal_control_get_last_error(terminal_control_t *control) {
    if (!control) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    return control->last_error;
}

/**
 * @brief Get error description string
 * @param error Error code to get description for
 * @return Human-readable error description string
 */
const char *terminal_control_error_string(terminal_control_error_t error) {
    switch (error) {
    case TERMINAL_CONTROL_SUCCESS:
        return "Success";
    case TERMINAL_CONTROL_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case TERMINAL_CONTROL_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case TERMINAL_CONTROL_ERROR_CAPABILITY_DETECTION:
        return "Capability detection failed";
    case TERMINAL_CONTROL_ERROR_SEQUENCE_TOO_LONG:
        return "Generated sequence too long";
    case TERMINAL_CONTROL_ERROR_UNSUPPORTED_OPERATION:
        return "Operation not supported by terminal";
    case TERMINAL_CONTROL_ERROR_COLOR_OUT_OF_RANGE:
        return "Color value out of valid range";
    case TERMINAL_CONTROL_ERROR_POSITION_OUT_OF_RANGE:
        return "Cursor position out of range";
    case TERMINAL_CONTROL_ERROR_TERMINAL_NOT_READY:
        return "Terminal not properly initialized";
    default:
        return "Unknown error";
    }
}

/// ============================================================================
/// STATIC HELPER FUNCTIONS
/// ============================================================================

/**
 * @brief Detect color support capabilities from terminal environment
 * @param control Terminal control instance to update
 * @return TERMINAL_CONTROL_SUCCESS on success
 */
static terminal_control_error_t
detect_color_support(terminal_control_t *control) {
    /// Basic color support detection based on terminal name
    const char *term_name = control->capabilities.terminal_name;

    /// Most terminals support at least 8 colors
    control->capabilities.flags |= TERMINAL_CAP_COLOR_8;
    control->capabilities.max_colors = 8;

    /// Check for extended color support
    if (strstr(term_name, "256color") || strstr(term_name, "xterm-256") ||
        strstr(term_name, "screen-256")) {
        control->capabilities.flags |=
            TERMINAL_CAP_COLOR_16 | TERMINAL_CAP_COLOR_256;
        control->capabilities.max_colors = 256;
    } else if (strstr(term_name, "xterm") || strstr(term_name, "screen") ||
               strstr(term_name, "tmux")) {
        control->capabilities.flags |= TERMINAL_CAP_COLOR_16;
        control->capabilities.max_colors = 16;
    }

    /// Check for truecolor support
    if (getenv("COLORTERM")) {
        const char *colorterm = getenv("COLORTERM");
        if (strcmp(colorterm, "truecolor") == 0 ||
            strcmp(colorterm, "24bit") == 0) {
            control->capabilities.flags |= TERMINAL_CAP_COLOR_TRUECOLOR;
            control->capabilities.max_colors = 16777216; /// 24-bit
        }
    }

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Detect cursor positioning capabilities
 * @param control Terminal control instance to update
 * @return TERMINAL_CONTROL_SUCCESS on success
 */
static terminal_control_error_t
detect_cursor_capabilities(terminal_control_t *control) {
    /// Most modern terminals support cursor positioning
    control->capabilities.flags |=
        TERMINAL_CAP_CURSOR_POSITIONING | TERMINAL_CAP_CURSOR_VISIBILITY;
    control->capabilities.cursor_positioning_support = true;

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Detect Unicode support from locale environment
 * @param control Terminal control instance to update
 * @return TERMINAL_CONTROL_SUCCESS on success
 */
static terminal_control_error_t
detect_unicode_support(terminal_control_t *control) {
    /// Check locale and terminal capabilities
    const char *lang = getenv("LANG");
    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");

    if ((lang && strstr(lang, "UTF-8")) ||
        (lc_all && strstr(lc_all, "UTF-8")) ||
        (lc_ctype && strstr(lc_ctype, "UTF-8"))) {
        control->capabilities.flags |= TERMINAL_CAP_UNICODE;
        control->capabilities.unicode_support = true;
    }

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Detect text style support based on terminal type
 * @param control Terminal control instance to update
 * @return TERMINAL_CONTROL_SUCCESS on success
 */
static terminal_control_error_t
detect_style_support(terminal_control_t *control) {
    /// Most terminals support basic styles
    control->capabilities.flags |=
        TERMINAL_CAP_BOLD | TERMINAL_CAP_UNDERLINE | TERMINAL_CAP_REVERSE;

    /// Check for advanced style support
    const char *term_name = control->capabilities.terminal_name;
    if (strstr(term_name, "xterm") || strstr(term_name, "gnome") ||
        strstr(term_name, "konsole") || strstr(term_name, "iterm")) {
        control->capabilities.flags |=
            TERMINAL_CAP_ITALIC | TERMINAL_CAP_STRIKETHROUGH;
    }

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Calculate hash for sequence caching
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return 32-bit hash value for the formatted sequence
 */
MAYBE_UNUSED
static uint32_t calculate_sequence_hash(const char *format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    uint32_t hash = HASH_SEED;
    for (const char *p = buffer; *p; p++) {
        hash = hash * HASH_MULTIPLIER + (uint32_t)*p;
    }
    return hash;
}

/**
 * @brief Find cached sequence by hash
 * @param control Terminal control instance containing cache
 * @param hash Hash value to search for
 * @return Pointer to cache entry, or NULL if not found
 */
MAYBE_UNUSED
static sequence_cache_entry_t *find_cached_sequence(terminal_control_t *control,
                                                    uint32_t hash) {
    for (int i = 0; i < TERMINAL_CONTROL_CAPABILITY_CACHE_SIZE; i++) {
        if (control->sequence_cache[i].valid &&
            control->sequence_cache[i].hash == hash) {
            return &control->sequence_cache[i];
        }
    }
    return NULL;
}

/**
 * @brief Cache a sequence for future reuse
 * @param control Terminal control instance containing cache
 * @param hash Hash value for the sequence
 * @param sequence ANSI sequence string to cache
 */
MAYBE_UNUSED
static void cache_sequence(terminal_control_t *control, uint32_t hash,
                           const char *sequence) {
    if (!control->capabilities.sequence_caching_enabled) {
        return;
    }

    sequence_cache_entry_t *entry =
        &control->sequence_cache[control->cache_next_index];

    entry->hash = hash;
    strncpy(entry->sequence, sequence, sizeof(entry->sequence) - 1);
    entry->sequence[sizeof(entry->sequence) - 1] = '\0';
    entry->last_used = base_terminal_get_timestamp_ns();
    entry->valid = true;

    control->cache_next_index = (control->cache_next_index + 1) %
                                TERMINAL_CONTROL_CAPABILITY_CACHE_SIZE;
}

/**
 * @brief Send ANSI sequence to terminal
 * @param control Terminal control instance
 * @param sequence ANSI sequence string to send
 * @return Number of bytes written, or -1 on error
 */
static ssize_t send_sequence(terminal_control_t *control,
                             const char *sequence) {
    if (!control || !control->base_terminal || !sequence) {
        return -1;
    }

    size_t len = strlen(sequence);
    return base_terminal_write(control->base_terminal, sequence, len);
}

/**
 * @brief Generate formatted sequence into buffer
 * @param buffer Output buffer for generated sequence
 * @param buffer_size Size of output buffer
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return Length of generated sequence, or -1 on error
 */
static ssize_t generate_sequence(char *buffer, size_t buffer_size,
                                 const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, buffer_size, format, args);
    va_end(args);

    if (result < 0 || (size_t)result >= buffer_size) {
        return -1;
    }

    return result;
}

/**
 * @brief Validate cursor position against terminal dimensions
 * @param control Terminal control instance
 * @param row Row position to validate (1-based)
 * @param column Column position to validate (1-based)
 * @return true if position is valid, false otherwise
 */
static bool validate_position(terminal_control_t *control, int row,
                              int column) {
    if (!control) {
        return false;
    }

    return (row >= 1 && column >= 1 &&
            row <= control->capabilities.terminal_height &&
            column <= control->capabilities.terminal_width);
}

/**
 * @brief Convert RGB to 256-color palette index
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 256-color palette index (16-255)
 */
static uint8_t rgb_to_256_color(uint8_t r, uint8_t g, uint8_t b) {
    /// Simple RGB to 256-color conversion
    /// Use the 216-color cube (colors 16-231)
    if (r == g && g == b) {
        /// Grayscale (colors 232-255)
        if (r < 8)
            return 16;
        if (r > 248)
            return 231;
        return 232 + (r - 8) / 10;
    }

    /// Color cube: 16 + 36*r + 6*g + b
    uint8_t r_index = r * 5 / 255;
    uint8_t g_index = g * 5 / 255;
    uint8_t b_index = b * 5 / 255;

    return 16 + 36 * r_index + 6 * g_index + b_index;
}

/// ============================================================================
/// PERFORMANCE AND DIAGNOSTICS FUNCTIONS
/// ============================================================================

/**
 * @brief Get performance metrics for terminal control operations
 * @param control Terminal control instance
 * @param sequences_generated Output: number of sequences generated
 * @param cache_hit_rate Output: cache hit rate (0.0-1.0)
 * @param avg_generation_time_ns Output: average generation time in nanoseconds
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t terminal_control_get_performance_metrics(
    terminal_control_t *control, uint64_t *sequences_generated,
    double *cache_hit_rate, uint64_t *avg_generation_time_ns) {

    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    if (sequences_generated) {
        *sequences_generated = control->sequences_generated;
    }

    if (cache_hit_rate) {
        uint64_t total =
            control->sequences_generated + control->sequences_cached;
        *cache_hit_rate =
            total > 0 ? (double)control->sequences_cached / total : 0.0;
    }

    if (avg_generation_time_ns) {
        *avg_generation_time_ns = control->sequences_generated > 0
                                      ? control->total_generation_time_ns /
                                            control->sequences_generated
                                      : 0;
    }

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Clear performance metrics and reset counters
 * @param control Terminal control instance
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_clear_metrics(terminal_control_t *control) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    control->sequences_generated = 0;
    control->sequences_cached = 0;
    control->total_generation_time_ns = 0;
    control->capabilities.cache_hit_count = 0;
    control->capabilities.cache_miss_count = 0;

    return TERMINAL_CONTROL_SUCCESS;
}

/**
 * @brief Enable or disable sequence caching
 * @param control Terminal control instance
 * @param enabled true to enable caching, false to disable
 * @return TERMINAL_CONTROL_SUCCESS on success, error code otherwise
 */
terminal_control_error_t
terminal_control_set_caching_enabled(terminal_control_t *control,
                                     bool enabled) {
    if (!control || !control->initialized) {
        return TERMINAL_CONTROL_ERROR_INVALID_PARAM;
    }

    control->capabilities.sequence_caching_enabled = enabled;

    if (!enabled) {
        /// Clear cache when disabling
        memset(control->sequence_cache, 0, sizeof(control->sequence_cache));
        control->cache_next_index = 0;
    }

    return TERMINAL_CONTROL_SUCCESS;
}