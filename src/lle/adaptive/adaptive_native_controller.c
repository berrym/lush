/**
 * @file adaptive_native_controller.c
 * @brief Native terminal controller for traditional TTY environments
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 - Native Terminal Controller
 *
 * Implements the native controller for traditional TTY terminals.
 * Provides full terminal control with raw mode and comprehensive
 * terminal abstraction integration.
 *
 * Key Features:
 * - Full raw mode terminal control
 * - Integration with research-validated terminal abstraction
 * - Capability-based optimization
 * - Complete cursor control and formatting
 * - Performance monitoring and statistics
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ============================================================================
 * CONTROLLER-SPECIFIC STRUCTURES
 * ============================================================================
 */

/**
 * Controller-specific capabilities (separate from terminal_abstraction.h).
 */
typedef struct {
    bool has_colors;
    bool has_256_colors;
    bool has_truecolor;
    bool has_cursor_positioning;
    bool has_cursor_queries;
    bool has_mouse_support;
    bool has_bracketed_paste;
    bool has_unicode;
} lle_native_capabilities_t;

/**
 * Controller optimization flags (separate from terminal_abstraction.h).
 */
typedef enum {
    LLE_NATIVE_OPT_NONE = 0,
    LLE_NATIVE_OPT_FAST_CURSOR_QUERIES = (1 << 0),
    LLE_NATIVE_OPT_EXTENDED_COLOR_CACHE = (1 << 1),
    LLE_NATIVE_OPT_ADVANCED_SEQUENCES = (1 << 2),
    LLE_NATIVE_OPT_MOUSE_TRACKING = (1 << 3),
    LLE_NATIVE_OPT_BRACKETED_PASTE = (1 << 4)
} lle_native_optimization_flags_t;

/**
 * Terminal performance statistics.
 */
typedef struct {
    uint64_t escape_sequences_sent;
    uint64_t cursor_moves_performed;
    uint64_t screen_updates;
    uint64_t total_bytes_written;
    uint64_t avg_update_time_us;
    uint64_t raw_mode_toggles;
} lle_terminal_performance_stats_t;

/**
 * Terminal state tracking.
 */
typedef struct {
    int cursor_row;
    int cursor_col;
    int terminal_width;
    int terminal_height;
    bool raw_mode_active;
    bool alternate_screen_active;
    int current_fg_color;
    int current_bg_color;
    bool bold_active;
    bool italic_active;
    bool underline_active;
} lle_terminal_state_t;

/**
 * Native terminal controller structure.
 */
struct lle_native_controller_t {
    // Terminal state tracking
    lle_terminal_state_t *terminal_state;

    // Raw mode management
    struct termios original_termios;
    struct termios raw_termios;
    bool raw_mode_active;
    bool termios_saved;

    // Capability-specific optimization
    lle_native_capabilities_t *capabilities;
    lle_native_optimization_flags_t optimization_flags;

    // Output buffering
    char *output_buffer;
    size_t buffer_capacity;
    size_t buffer_used;

    // Performance monitoring
    lle_terminal_performance_stats_t *perf_stats;

    // Memory management
    lush_memory_pool_t *memory_pool;
};

/* ============================================================================
 * TERMINAL STATE IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create and initialize terminal state tracking structure.
 *
 * Allocates terminal state and queries current terminal dimensions.
 * Falls back to 80x24 if dimensions cannot be determined.
 *
 * @return Pointer to created terminal state, or NULL on allocation failure.
 */
static lle_terminal_state_t *lle_terminal_state_create(void) {
    lle_terminal_state_t *state = calloc(1, sizeof(lle_terminal_state_t));
    if (!state) {
        return NULL;
    }

    // Get terminal dimensions
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        state->terminal_width = ws.ws_col;
        state->terminal_height = ws.ws_row;
    } else {
        state->terminal_width = 80;
        state->terminal_height = 24;
    }

    state->cursor_row = 0;
    state->cursor_col = 0;
    state->current_fg_color = -1;
    state->current_bg_color = -1;

    return state;
}

/**
 * @brief Destroy a terminal state structure and free its resources.
 *
 * @param state The terminal state to destroy, or NULL for no-op.
 */
static void lle_terminal_state_destroy(lle_terminal_state_t *state) {
    free(state);
}

/**
 * @brief Update terminal dimensions from the TTY.
 *
 * Queries the terminal for current width and height using ioctl.
 *
 * @param state The terminal state to update.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t
lle_terminal_state_update_dimensions(lle_terminal_state_t *state) {

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    state->terminal_width = ws.ws_col;
    state->terminal_height = ws.ws_row;

    return LLE_SUCCESS;
}

/* ============================================================================
 * PERFORMANCE STATISTICS IMPLEMENTATION
 * ============================================================================
 */

/**
 * @brief Create a performance statistics tracking structure.
 *
 * @return Pointer to created statistics structure, or NULL on allocation
 * failure.
 */
static lle_terminal_performance_stats_t *
lle_terminal_performance_stats_create(void) {
    return calloc(1, sizeof(lle_terminal_performance_stats_t));
}

/**
 * @brief Destroy a performance statistics structure.
 *
 * @param stats The statistics structure to destroy, or NULL for no-op.
 */
static void lle_terminal_performance_stats_destroy(
    lle_terminal_performance_stats_t *stats) {
    free(stats);
}

/* ============================================================================
 * RAW MODE MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Enter raw terminal mode for character-by-character input.
 *
 * Saves the original terminal settings and configures raw mode
 * with disabled echo, canonical mode, and signal handling.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_native_enter_raw_mode(lle_native_controller_t *native) {
    if (native->raw_mode_active) {
        return LLE_SUCCESS;
    }

    // Save original terminal settings
    if (!native->termios_saved) {
        if (tcgetattr(STDIN_FILENO, &native->original_termios) != 0) {
            return LLE_ERROR_TERMINAL_ABSTRACTION;
        }
        native->termios_saved = true;
    }

    // Configure raw mode
    native->raw_termios = native->original_termios;
    native->raw_termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    native->raw_termios.c_oflag &= ~(OPOST);
    native->raw_termios.c_cflag |= (CS8);
    native->raw_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    native->raw_termios.c_cc[VMIN] = 1;
    native->raw_termios.c_cc[VTIME] = 0;

    // Apply raw mode
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &native->raw_termios) != 0) {
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    native->raw_mode_active = true;
    native->perf_stats->raw_mode_toggles++;

    return LLE_SUCCESS;
}

/**
 * @brief Exit raw terminal mode and restore original settings.
 *
 * Restores the terminal to its original configuration saved
 * when entering raw mode.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_native_exit_raw_mode(lle_native_controller_t *native) {
    if (!native->raw_mode_active || !native->termios_saved) {
        return LLE_SUCCESS;
    }

    // Restore original terminal settings
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &native->original_termios) != 0) {
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    native->raw_mode_active = false;
    native->perf_stats->raw_mode_toggles++;

    return LLE_SUCCESS;
}

/* ============================================================================
 * OUTPUT BUFFER MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Append data to the output buffer.
 *
 * Grows the buffer as needed to accommodate the new data.
 *
 * @param native The native controller.
 * @param data The data to append.
 * @param length The length of the data in bytes.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_native_buffer_append(lle_native_controller_t *native,
                                             const char *data, size_t length) {

    // Ensure capacity
    size_t needed = native->buffer_used + length;
    if (needed >= native->buffer_capacity) {
        size_t new_capacity = native->buffer_capacity;
        while (new_capacity < needed + 1) {
            new_capacity *= 2;
        }
        char *new_buffer = realloc(native->output_buffer, new_capacity);
        if (!new_buffer) {
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                             "adaptive native controller allocation failed");
        }
        native->output_buffer = new_buffer;
        native->buffer_capacity = new_capacity;
    }

    memcpy(native->output_buffer + native->buffer_used, data, length);
    native->buffer_used += length;
    native->output_buffer[native->buffer_used] = '\0';

    return LLE_SUCCESS;
}

/**
 * @brief Flush the output buffer to stdout.
 *
 * Writes all buffered content to stdout and updates byte statistics.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_native_buffer_flush(lle_native_controller_t *native) {
    if (native->buffer_used == 0) {
        return LLE_SUCCESS;
    }

    ssize_t written =
        write(STDOUT_FILENO, native->output_buffer, native->buffer_used);
    if (written < 0) {
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    native->perf_stats->total_bytes_written += written;
    native->buffer_used = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Clear the output buffer without flushing.
 *
 * @param native The native controller.
 */
static void lle_native_buffer_clear(lle_native_controller_t *native) {
    native->buffer_used = 0;
}

/* ============================================================================
 * TERMINAL CONTROL SEQUENCES
 * ============================================================================
 */

/**
 * @brief Move the cursor to a specific position.
 *
 * Appends a cursor positioning escape sequence to the output buffer.
 *
 * @param native The native controller.
 * @param row The target row (0-indexed).
 * @param col The target column (0-indexed).
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t lle_native_move_cursor(lle_native_controller_t *native,
                                           int row, int col) {

    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\x1b[%d;%dH", row + 1, col + 1);
    if (len < 0 || len >= (int)sizeof(seq)) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = lle_native_buffer_append(native, seq, len);
    if (result == LLE_SUCCESS) {
        native->terminal_state->cursor_row = row;
        native->terminal_state->cursor_col = col;
        native->perf_stats->cursor_moves_performed++;
        native->perf_stats->escape_sequences_sent++;
    }

    return result;
}

/**
 * @brief Clear the entire screen and move cursor to home position.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t lle_native_clear_screen(lle_native_controller_t *native) {
    const char *seq = "\x1b[2J\x1b[H";
    lle_result_t result = lle_native_buffer_append(native, seq, strlen(seq));
    if (result == LLE_SUCCESS) {
        native->terminal_state->cursor_row = 0;
        native->terminal_state->cursor_col = 0;
        native->perf_stats->escape_sequences_sent += 2;
    }
    return result;
}

/**
 * @brief Clear from cursor position to end of current line.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t lle_native_clear_to_eol(lle_native_controller_t *native) {
    const char *seq = "\x1b[K";
    lle_result_t result = lle_native_buffer_append(native, seq, strlen(seq));
    if (result == LLE_SUCCESS) {
        native->perf_stats->escape_sequences_sent++;
    }
    return result;
}

/**
 * @brief Set the foreground color using 256-color mode.
 *
 * Optimizes by skipping if the color hasn't changed.
 *
 * @param native The native controller.
 * @param color The 256-color palette index.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t lle_native_set_fg_color(lle_native_controller_t *native,
                                            int color) {

    if (color == native->terminal_state->current_fg_color) {
        return LLE_SUCCESS;
    }

    char seq[32];
    int len = snprintf(seq, sizeof(seq), "\x1b[38;5;%dm", color);
    if (len < 0 || len >= (int)sizeof(seq)) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = lle_native_buffer_append(native, seq, len);
    if (result == LLE_SUCCESS) {
        native->terminal_state->current_fg_color = color;
        native->perf_stats->escape_sequences_sent++;
    }

    return result;
}

/**
 * @brief Reset all text formatting to terminal defaults.
 *
 * Clears colors, bold, italic, underline, and other attributes.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t
lle_native_reset_formatting(lle_native_controller_t *native) {
    const char *seq = "\x1b[0m";
    lle_result_t result = lle_native_buffer_append(native, seq, strlen(seq));
    if (result == LLE_SUCCESS) {
        native->terminal_state->current_fg_color = -1;
        native->terminal_state->current_bg_color = -1;
        native->terminal_state->bold_active = false;
        native->terminal_state->italic_active = false;
        native->terminal_state->underline_active = false;
        native->perf_stats->escape_sequences_sent++;
    }
    return result;
}

/**
 * @brief Show the terminal cursor.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t lle_native_show_cursor(lle_native_controller_t *native) {
    const char *seq = "\x1b[?25h";
    lle_result_t result = lle_native_buffer_append(native, seq, strlen(seq));
    if (result == LLE_SUCCESS) {
        native->perf_stats->escape_sequences_sent++;
    }
    return result;
}

/**
 * @brief Hide the terminal cursor.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
LLE_MAYBE_UNUSED
static lle_result_t lle_native_hide_cursor(lle_native_controller_t *native) {
    const char *seq = "\x1b[?25l";
    lle_result_t result = lle_native_buffer_append(native, seq, strlen(seq));
    if (result == LLE_SUCCESS) {
        native->perf_stats->escape_sequences_sent++;
    }
    return result;
}

/* ============================================================================
 * CAPABILITY-BASED OPTIMIZATION
 * ============================================================================
 */

/**
 * @brief Apply capability-based optimizations to the native controller.
 *
 * Sets optimization flags based on detected terminal capabilities
 * such as cursor queries, color depth, and advanced features.
 *
 * @param native The native controller.
 * @param detection The terminal detection results.
 * @return LLE_SUCCESS on success.
 */
static lle_result_t lle_apply_capability_optimizations(
    lle_native_controller_t *native,
    const lle_terminal_detection_result_t *detection) {

    native->optimization_flags = LLE_NATIVE_OPT_NONE;

    if (detection->supports_cursor_queries) {
        native->optimization_flags |= LLE_NATIVE_OPT_FAST_CURSOR_QUERIES;
    }

    if (detection->supports_256_colors) {
        native->optimization_flags |= LLE_NATIVE_OPT_EXTENDED_COLOR_CACHE;
    }

    if (detection->capability_level >= LLE_CAPABILITY_FULL) {
        native->optimization_flags |= LLE_NATIVE_OPT_ADVANCED_SEQUENCES;
    }

    if (detection->supports_mouse) {
        native->optimization_flags |= LLE_NATIVE_OPT_MOUSE_TRACKING;
    }

    if (detection->supports_bracketed_paste) {
        native->optimization_flags |= LLE_NATIVE_OPT_BRACKETED_PASTE;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Calculate the optimal output buffer size based on capability level.
 *
 * Premium terminals get larger buffers, minimal terminals get smaller ones.
 *
 * @param detection The terminal detection results.
 * @return The recommended buffer size in bytes.
 */
static size_t lle_calculate_optimal_buffer_size(
    const lle_terminal_detection_result_t *detection) {

    // Base size: 16KB
    size_t base_size = 16384;

    // Adjust based on capability level
    switch (detection->capability_level) {
    case LLE_CAPABILITY_PREMIUM:
        return base_size * 2;
    case LLE_CAPABILITY_FULL:
        return base_size;
    case LLE_CAPABILITY_STANDARD:
        return base_size / 2;
    default:
        return base_size / 4;
    }
}

/* ============================================================================
 * NATIVE CONTROLLER API
 * ============================================================================
 */

/**
 * @brief Initialize the native terminal controller for full TTY environments.
 *
 * Creates and configures a native controller with full terminal control
 * including raw mode, cursor positioning, colors, and performance monitoring.
 *
 * @param context The adaptive context to initialize the controller in.
 * @param memory_pool Memory pool for allocations.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_initialize_native_controller(lle_adaptive_context_t *context,
                                              lush_memory_pool_t *memory_pool) {

    lle_native_controller_t *native =
        calloc(1, sizeof(lle_native_controller_t));
    if (!native) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    // Create terminal state
    native->terminal_state = lle_terminal_state_create();
    if (!native->terminal_state) {
        free(native);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    // Create capabilities structure
    native->capabilities = calloc(1, sizeof(lle_native_capabilities_t));
    if (!native->capabilities) {
        lle_terminal_state_destroy(native->terminal_state);
        free(native);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    // Configure capabilities from detection
    native->capabilities->has_colors =
        context->detection_result->supports_colors;
    native->capabilities->has_256_colors =
        context->detection_result->supports_256_colors;
    native->capabilities->has_truecolor =
        context->detection_result->supports_truecolor;
    native->capabilities->has_cursor_positioning =
        context->detection_result->supports_cursor_positioning;
    native->capabilities->has_cursor_queries =
        context->detection_result->supports_cursor_queries;
    native->capabilities->has_mouse_support =
        context->detection_result->supports_mouse;
    native->capabilities->has_bracketed_paste =
        context->detection_result->supports_bracketed_paste;
    native->capabilities->has_unicode =
        context->detection_result->supports_unicode;

    // Apply capability optimizations
    lle_result_t result =
        lle_apply_capability_optimizations(native, context->detection_result);
    if (result != LLE_SUCCESS) {
        free(native->capabilities);
        lle_terminal_state_destroy(native->terminal_state);
        free(native);
        return result;
    }

    // Create output buffer
    native->buffer_capacity =
        lle_calculate_optimal_buffer_size(context->detection_result);
    native->output_buffer = malloc(native->buffer_capacity);
    if (!native->output_buffer) {
        free(native->capabilities);
        lle_terminal_state_destroy(native->terminal_state);
        free(native);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    // Create performance statistics
    native->perf_stats = lle_terminal_performance_stats_create();
    if (!native->perf_stats) {
        free(native->output_buffer);
        free(native->capabilities);
        lle_terminal_state_destroy(native->terminal_state);
        free(native);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    native->memory_pool = memory_pool;
    context->controller.native = native;

    return LLE_SUCCESS;
}

/**
 * @brief Clean up and destroy a native controller.
 *
 * Restores terminal state if in raw mode and releases all resources.
 *
 * @param native The native controller to destroy, or NULL for no-op.
 */
void lle_cleanup_native_controller(lle_native_controller_t *native) {
    if (!native) {
        return;
    }

    // Restore terminal state if needed
    if (native->raw_mode_active) {
        lle_native_exit_raw_mode(native);
    }

    lle_terminal_performance_stats_destroy(native->perf_stats);
    free(native->output_buffer);
    free(native->capabilities);
    lle_terminal_state_destroy(native->terminal_state);
    free(native);
}

/**
 * @brief Read a line of input using the native controller.
 *
 * Enters raw mode, displays a colored prompt, reads input, and
 * returns the line. Currently uses fgets for simplicity.
 *
 * @param native The native controller.
 * @param prompt The prompt string to display.
 * @param line Output pointer to receive the allocated input line.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_native_read_line(lle_native_controller_t *native,
                                  const char *prompt, char **line) {

    if (!native || !prompt || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Enter raw mode
    lle_result_t result = lle_native_enter_raw_mode(native);
    if (result != LLE_SUCCESS) {
        return result;
    }

    // Display prompt with color
    lle_native_buffer_clear(native);
    if (native->capabilities->has_colors) {
        lle_native_set_fg_color(native, 2); // Green
    }
    lle_native_buffer_append(native, prompt, strlen(prompt));
    if (native->capabilities->has_colors) {
        lle_native_reset_formatting(native);
    }
    lle_native_buffer_flush(native);

    /// Read line using simple fgets for now (full implementation would use raw
    /// input)
    lle_native_exit_raw_mode(native);

    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), stdin)) {
        if (feof(stdin)) {
            return LLE_FAULT(
                LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                "adaptive native controller allocation failed"); // EOF treated
                                                                 // as error
        }
        return LLE_ERROR_INPUT_PARSING;
    }

    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    *line = strdup(buffer);
    if (!*line) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive native controller allocation failed");
    }

    return LLE_SUCCESS;
}

/**
 * @brief Update the native controller's display output.
 *
 * Flushes the output buffer to the terminal.
 *
 * @param native The native controller.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_native_update_display(lle_native_controller_t *native) {
    if (!native) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    native->perf_stats->screen_updates++;
    return lle_native_buffer_flush(native);
}

/**
 * @brief Handle terminal resize events.
 *
 * Updates the stored terminal dimensions when the terminal is resized.
 *
 * @param native The native controller.
 * @param new_width The new terminal width in columns.
 * @param new_height The new terminal height in rows.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_native_handle_resize(lle_native_controller_t *native,
                                      int new_width, int new_height) {

    if (!native) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    native->terminal_state->terminal_width = new_width;
    native->terminal_state->terminal_height = new_height;

    return LLE_SUCCESS;
}

/**
 * @brief Get native controller performance statistics.
 *
 * Copies the performance statistics to the provided structure.
 *
 * @param native The native controller.
 * @param stats Output structure to receive the statistics.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_native_get_stats(const lle_native_controller_t *native,
                                  lle_terminal_performance_stats_t *stats) {

    if (!native || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *stats = *native->perf_stats;
    return LLE_SUCCESS;
}
