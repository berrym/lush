/**
 * @file base_terminal.c
 * @brief Base Terminal Layer - Foundation Terminal Abstraction
 *
 * Part of the Lush Shell Layered Display Architecture.
 * Provides low-level terminal access and capability detection.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * ============================================================================
 *
 * BASE TERMINAL LAYER (Layer 1)
 *
 * This is the foundation layer of the Lush Display System. It provides
 * low-level terminal abstraction and raw I/O operations that all higher
 * layers depend on.
 *
 * Key Responsibilities:
 * - Terminal initialization and cleanup
 * - Raw input/output operations
 * - Terminal mode management (raw, canonical)
 * - Cross-platform terminal compatibility
 * - Error handling and recovery
 *
 * Design Principles:
 * - Universal compatibility across all Unix-like systems
 * - Safe initialization and cleanup with proper error handling
 * - Non-blocking I/O operations where appropriate
 * - Comprehensive error reporting and recovery
 * - Memory-safe operations with proper resource management
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#elif __APPLE__ || __FreeBSD__ || __OpenBSD__ || __NetBSD__
#include <sys/event.h>
#endif

#include "base_terminal.h"

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

#define BASE_TERMINAL_READ_BUFFER_SIZE 4096
#define BASE_TERMINAL_WRITE_BUFFER_SIZE 8192
#define BASE_TERMINAL_MAX_RETRY_COUNT 3
#define BASE_TERMINAL_TIMEOUT_MS 100

// Terminal capability detection strings
#define TERMINAL_CAPABILITY_REQUEST "\033[c"
#define TERMINAL_SIZE_REQUEST "\033[18t"

// ============================================================================
// STATIC VARIABLES
// ============================================================================

static bool base_terminal_initialized = false;
static base_terminal_t *global_terminal = NULL;
static struct sigaction original_sigwinch_handler;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static base_terminal_error_t detect_terminal_type(base_terminal_t *terminal);
static base_terminal_error_t setup_signal_handlers(base_terminal_t *terminal);
static base_terminal_error_t restore_signal_handlers(base_terminal_t *terminal);
static void sigwinch_handler(int sig);
static base_terminal_error_t validate_terminal_fds(base_terminal_t *terminal);
static base_terminal_error_t
configure_terminal_modes(base_terminal_t *terminal);

// ============================================================================
// IMPLEMENTATION
// ============================================================================

/**
 * @brief Create a new base terminal instance
 * @return Pointer to new base_terminal_t instance, or NULL on failure
 */
base_terminal_t *base_terminal_create(void) {
    base_terminal_t *terminal = calloc(1, sizeof(base_terminal_t));
    if (!terminal) {
        return NULL;
    }

    // Initialize with default values
    terminal->input_fd = STDIN_FILENO;
    terminal->output_fd = STDOUT_FILENO;
    terminal->error_fd = STDERR_FILENO;
    terminal->raw_mode_enabled = false;
    terminal->initialized = false;
    terminal->terminal_type = NULL;
    terminal->last_error = BASE_TERMINAL_SUCCESS;

    // Initialize performance metrics
    terminal->metrics.total_reads = 0;
    terminal->metrics.total_writes = 0;
    terminal->metrics.total_bytes_read = 0;
    terminal->metrics.total_bytes_written = 0;
    terminal->metrics.initialization_time_ns = 0;

    return terminal;
}

/**
 * @brief Initialize the base terminal system
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
base_terminal_error_t base_terminal_init(base_terminal_t *terminal) {
    if (!terminal) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    if (terminal->initialized) {
        return BASE_TERMINAL_SUCCESS; // Already initialized
    }

    uint64_t start_time = base_terminal_get_timestamp_ns();

    // Validate terminal file descriptors
    base_terminal_error_t result = validate_terminal_fds(terminal);
    if (result != BASE_TERMINAL_SUCCESS) {
        terminal->last_error = result;
        return result;
    }

    // Save original terminal attributes
    if (tcgetattr(terminal->input_fd, &terminal->original_termios) != 0) {
        terminal->last_error = BASE_TERMINAL_ERROR_TERMIOS_FAILED;
        return BASE_TERMINAL_ERROR_TERMIOS_FAILED;
    }

    // Copy original attributes to current
    terminal->current_termios = terminal->original_termios;

    // Detect terminal type and capabilities
    result = detect_terminal_type(terminal);
    if (result != BASE_TERMINAL_SUCCESS) {
        terminal->last_error = result;
        return result;
    }

    // Setup signal handlers for window size changes
    result = setup_signal_handlers(terminal);
    if (result != BASE_TERMINAL_SUCCESS) {
        terminal->last_error = result;
        return result;
    }

    // Configure initial terminal modes
    result = configure_terminal_modes(terminal);
    if (result != BASE_TERMINAL_SUCCESS) {
        terminal->last_error = result;
        return result;
    }

    terminal->initialized = true;
    base_terminal_initialized = true;
    global_terminal = terminal;

    // Record initialization time
    terminal->metrics.initialization_time_ns =
        base_terminal_get_timestamp_ns() - start_time;

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Enable or disable raw terminal mode
 * @param terminal Pointer to base_terminal_t instance
 * @param enable True to enable raw mode, false to disable
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
base_terminal_error_t base_terminal_set_raw_mode(base_terminal_t *terminal,
                                                 bool enable) {
    if (!terminal || !terminal->initialized) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    if (terminal->raw_mode_enabled == enable) {
        return BASE_TERMINAL_SUCCESS; // Already in desired mode
    }

    if (enable) {
        // Configure for raw mode
        terminal->current_termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        terminal->current_termios.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP);
        terminal->current_termios.c_cflag |= CS8;
        terminal->current_termios.c_oflag &= ~OPOST;

        // Set non-blocking read with minimal timeout
        terminal->current_termios.c_cc[VMIN] = 0;
        terminal->current_termios.c_cc[VTIME] = 1;
    } else {
        // Restore canonical mode
        terminal->current_termios = terminal->original_termios;
    }

    if (tcsetattr(terminal->input_fd, TCSAFLUSH, &terminal->current_termios) !=
        0) {
        terminal->last_error = BASE_TERMINAL_ERROR_TERMIOS_FAILED;
        return BASE_TERMINAL_ERROR_TERMIOS_FAILED;
    }

    terminal->raw_mode_enabled = enable;
    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Read data from terminal input
 * @param terminal Pointer to base_terminal_t instance
 * @param buffer Buffer to store read data
 * @param max_len Maximum number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t base_terminal_read(base_terminal_t *terminal, char *buffer,
                           size_t max_len) {
    if (!terminal || !terminal->initialized || !buffer || max_len == 0) {
        if (terminal)
            terminal->last_error = BASE_TERMINAL_ERROR_INVALID_PARAM;
        return -1;
    }

    ssize_t bytes_read = read(terminal->input_fd, buffer, max_len);

    if (bytes_read > 0) {
        terminal->metrics.total_reads++;
        terminal->metrics.total_bytes_read += bytes_read;
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        terminal->last_error = BASE_TERMINAL_ERROR_READ_FAILED;
    }

    return bytes_read;
}

/**
 * @brief Write data to terminal output
 * @param terminal Pointer to base_terminal_t instance
 * @param data Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t base_terminal_write(base_terminal_t *terminal, const char *data,
                            size_t len) {
    if (!terminal || !terminal->initialized || !data || len == 0) {
        if (terminal)
            terminal->last_error = BASE_TERMINAL_ERROR_INVALID_PARAM;
        return -1;
    }

    ssize_t total_written = 0;
    size_t remaining = len;
    int retry_count = 0;

    while (remaining > 0 && retry_count < BASE_TERMINAL_MAX_RETRY_COUNT) {
        ssize_t bytes_written =
            write(terminal->output_fd, data + total_written, remaining);

        if (bytes_written > 0) {
            total_written += bytes_written;
            remaining -= bytes_written;
            retry_count = 0; // Reset retry count on successful write
        } else if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                retry_count++;
                usleep(1000); // Brief delay before retry
                continue;
            } else {
                terminal->last_error = BASE_TERMINAL_ERROR_WRITE_FAILED;
                return -1;
            }
        }
    }

    if (total_written > 0) {
        terminal->metrics.total_writes++;
        terminal->metrics.total_bytes_written += total_written;
    }

    return total_written;
}

/**
 * @brief Flush terminal output buffer
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
base_terminal_error_t base_terminal_flush(base_terminal_t *terminal) {
    if (!terminal || !terminal->initialized) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    // Use fflush for stdout/stderr, fsync for other file descriptors
    if (terminal->output_fd == STDOUT_FILENO) {
        if (fflush(stdout) != 0) {
            terminal->last_error = BASE_TERMINAL_ERROR_FLUSH_FAILED;
            return BASE_TERMINAL_ERROR_FLUSH_FAILED;
        }
    } else if (terminal->output_fd == STDERR_FILENO) {
        if (fflush(stderr) != 0) {
            terminal->last_error = BASE_TERMINAL_ERROR_FLUSH_FAILED;
            return BASE_TERMINAL_ERROR_FLUSH_FAILED;
        }
    } else {
        // For other file descriptors, use fsync
        if (fsync(terminal->output_fd) != 0) {
            terminal->last_error = BASE_TERMINAL_ERROR_FLUSH_FAILED;
            return BASE_TERMINAL_ERROR_FLUSH_FAILED;
        }
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Get current terminal size
 * @param terminal Pointer to base_terminal_t instance
 * @param width Pointer to store terminal width
 * @param height Pointer to store terminal height
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
base_terminal_error_t base_terminal_get_size(base_terminal_t *terminal,
                                             int *width, int *height) {
    if (!terminal || !terminal->initialized || !width || !height) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    struct winsize ws;
    if (ioctl(terminal->output_fd, TIOCGWINSZ, &ws) == -1) {
        terminal->last_error = BASE_TERMINAL_ERROR_IOCTL_FAILED;
        return BASE_TERMINAL_ERROR_IOCTL_FAILED;
    }

    *width = ws.ws_col;
    *height = ws.ws_row;

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Check if data is available for reading
 * @param terminal Pointer to base_terminal_t instance
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking, negative for
 * infinite)
 * @return 1 if data available, 0 if no data, -1 on error
 */
int base_terminal_data_available(base_terminal_t *terminal, int timeout_ms) {
    if (!terminal || !terminal->initialized) {
        if (terminal)
            terminal->last_error = BASE_TERMINAL_ERROR_INVALID_PARAM;
        return -1;
    }

    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(terminal->input_fd, &readfds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    // timeout_ms = 0: non-blocking (immediate return)
    // timeout_ms > 0: wait for specified time
    // timeout_ms < 0: block indefinitely
    int result = select(terminal->input_fd + 1, &readfds, NULL, NULL,
                        timeout_ms < 0 ? NULL : &timeout);

    if (result == -1) {
        terminal->last_error = BASE_TERMINAL_ERROR_SELECT_FAILED;
        return -1;
    }

    return result;
}

/**
 * @brief Get current performance metrics
 * @param terminal Pointer to base_terminal_t instance
 * @return Copy of current performance metrics
 */
base_terminal_metrics_t base_terminal_get_metrics(base_terminal_t *terminal) {
    base_terminal_metrics_t empty_metrics = {0};

    if (!terminal || !terminal->initialized) {
        return empty_metrics;
    }

    return terminal->metrics;
}

/**
 * @brief Get last error code
 * @param terminal Pointer to base_terminal_t instance
 * @return Last error code
 */
base_terminal_error_t base_terminal_get_last_error(base_terminal_t *terminal) {
    if (!terminal) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    return terminal->last_error;
}

/**
 * @brief Get error description string
 * @param error Error code
 * @return Human-readable error description
 */
const char *base_terminal_error_string(base_terminal_error_t error) {
    switch (error) {
    case BASE_TERMINAL_SUCCESS:
        return "Success";
    case BASE_TERMINAL_ERROR_INVALID_PARAM:
        return "Invalid parameter";
    case BASE_TERMINAL_ERROR_MEMORY_ALLOCATION:
        return "Memory allocation failed";
    case BASE_TERMINAL_ERROR_TERMIOS_FAILED:
        return "Terminal attributes operation failed";
    case BASE_TERMINAL_ERROR_TERMINAL_DETECTION:
        return "Terminal type detection failed";
    case BASE_TERMINAL_ERROR_SIGNAL_HANDLER:
        return "Signal handler setup failed";
    case BASE_TERMINAL_ERROR_READ_FAILED:
        return "Terminal read operation failed";
    case BASE_TERMINAL_ERROR_WRITE_FAILED:
        return "Terminal write operation failed";
    case BASE_TERMINAL_ERROR_FLUSH_FAILED:
        return "Terminal flush operation failed";
    case BASE_TERMINAL_ERROR_IOCTL_FAILED:
        return "Terminal ioctl operation failed";
    case BASE_TERMINAL_ERROR_SELECT_FAILED:
        return "Select operation failed";
    case BASE_TERMINAL_ERROR_NOT_INITIALIZED:
        return "Terminal not initialized";
    default:
        return "Unknown error";
    }
}

/**
 * @brief Clean up and restore terminal state
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
base_terminal_error_t base_terminal_cleanup(base_terminal_t *terminal) {
    if (!terminal) {
        return BASE_TERMINAL_ERROR_INVALID_PARAM;
    }

    if (!terminal->initialized) {
        return BASE_TERMINAL_SUCCESS; // Nothing to clean up
    }

    // Restore original terminal attributes
    if (tcsetattr(terminal->input_fd, TCSAFLUSH, &terminal->original_termios) !=
        0) {
        terminal->last_error = BASE_TERMINAL_ERROR_TERMIOS_FAILED;
        // Continue cleanup despite error
    }

    // Restore signal handlers
    restore_signal_handlers(terminal);

    // Clean up terminal type string
    if (terminal->terminal_type) {
        free(terminal->terminal_type);
        terminal->terminal_type = NULL;
    }

    terminal->initialized = false;
    terminal->raw_mode_enabled = false;

    if (global_terminal == terminal) {
        global_terminal = NULL;
        base_terminal_initialized = false;
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Destroy base terminal instance and free resources
 * @param terminal Pointer to base_terminal_t instance
 */
void base_terminal_destroy(base_terminal_t *terminal) {
    if (!terminal) {
        return;
    }

    base_terminal_cleanup(terminal);
    free(terminal);
}

/**
 * @brief Get high-resolution timestamp in nanoseconds
 * @return Current monotonic timestamp in nanoseconds, or 0 on failure
 */
uint64_t base_terminal_get_timestamp_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    return 0;
}

// ============================================================================
// STATIC HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Detect terminal type and capabilities
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
static base_terminal_error_t detect_terminal_type(base_terminal_t *terminal) {
    const char *term_env = getenv("TERM");
    if (term_env) {
        terminal->terminal_type = strdup(term_env);
        if (!terminal->terminal_type) {
            return BASE_TERMINAL_ERROR_MEMORY_ALLOCATION;
        }
    } else {
        terminal->terminal_type = strdup("unknown");
        if (!terminal->terminal_type) {
            return BASE_TERMINAL_ERROR_MEMORY_ALLOCATION;
        }
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Setup signal handlers for terminal events
 * @param terminal Pointer to base_terminal_t instance (unused)
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
static base_terminal_error_t setup_signal_handlers(base_terminal_t *terminal) {
    (void)terminal; // Unused parameter
    struct sigaction sa;
    sa.sa_handler = sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGWINCH, &sa, &original_sigwinch_handler) == -1) {
        return BASE_TERMINAL_ERROR_SIGNAL_HANDLER;
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Restore original signal handlers
 * @param terminal Pointer to base_terminal_t instance (unused)
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
static base_terminal_error_t
restore_signal_handlers(base_terminal_t *terminal) {
    (void)terminal; // Unused parameter
    if (sigaction(SIGWINCH, &original_sigwinch_handler, NULL) == -1) {
        return BASE_TERMINAL_ERROR_SIGNAL_HANDLER;
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Signal handler for window size changes
 * @param sig Signal number (unused)
 */
static void sigwinch_handler(int sig) {
    (void)sig; // Unused parameter

    // Just set a flag - actual handling will be done in main thread
    if (global_terminal) {
        // Could set a flag here for window size change notification
        // For now, we'll handle this through polling in higher layers
    }
}

/**
 * @brief Validate terminal file descriptors
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
static base_terminal_error_t validate_terminal_fds(base_terminal_t *terminal) {
    // Check if file descriptors are valid
    if (terminal->input_fd < 0 || terminal->output_fd < 0) {
        return BASE_TERMINAL_ERROR_TERMINAL_DETECTION;
    }

    // For interactive terminals, both input and output should be TTYs
    // For testing or non-interactive use, allow non-TTY file descriptors
    bool input_is_tty = isatty(terminal->input_fd);
    bool output_is_tty = isatty(terminal->output_fd);

    // If we're dealing with standard streams, be more permissive
    if ((terminal->input_fd == STDIN_FILENO &&
         terminal->output_fd == STDOUT_FILENO)) {
        // Allow operation even if not TTYs (for testing/scripting)
        return BASE_TERMINAL_SUCCESS;
    }

    // For other file descriptors, require TTY status
    if (!input_is_tty || !output_is_tty) {
        return BASE_TERMINAL_ERROR_TERMINAL_DETECTION;
    }

    return BASE_TERMINAL_SUCCESS;
}

/**
 * @brief Configure initial terminal modes
 * @param terminal Pointer to base_terminal_t instance (unused)
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 */
static base_terminal_error_t
configure_terminal_modes(base_terminal_t *terminal) {
    (void)terminal; // Unused parameter
    // Start in canonical mode (normal terminal behavior)
    // Raw mode can be enabled later if needed
    return BASE_TERMINAL_SUCCESS;
}