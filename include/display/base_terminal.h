/**
 * @file base_terminal.h
 * @brief Base terminal layer (Layer 1) - Foundation terminal abstraction
 *
 * Provides low-level terminal abstraction and raw I/O operations that all
 * higher display layers depend on. Cross-platform terminal support with
 * raw and canonical mode handling.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 *
 * Key Features:
 * - Cross-platform terminal abstraction
 * - Raw and canonical terminal mode support
 * - Non-blocking I/O operations
 * - Terminal capability detection
 * - Performance monitoring and metrics
 * - Comprehensive error handling
 *
 * Design Principles:
 * - Universal compatibility across Unix-like systems
 * - Thread-safe operations where applicable
 * - Memory-safe resource management
 * - Comprehensive error reporting
 * - Performance monitoring capabilities
 */

#ifndef BASE_TERMINAL_H
#define BASE_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// PORTABILITY MACROS
// ============================================================================

/* Macro to suppress unused warnings for spec-compliant but not-yet-wired code
 */
#ifndef MAYBE_UNUSED
#ifdef __GNUC__
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif
#endif

// Macro to explicitly mark a parameter/variable as intentionally unused
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

#define BASE_TERMINAL_VERSION_MAJOR 1
#define BASE_TERMINAL_VERSION_MINOR 0
#define BASE_TERMINAL_VERSION_PATCH 0

#define BASE_TERMINAL_MAX_TERMINAL_NAME_LENGTH 64
#define BASE_TERMINAL_DEFAULT_TIMEOUT_MS 100

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

/**
 * Error codes for base terminal operations
 */
typedef enum {
    BASE_TERMINAL_SUCCESS = 0,             // Operation completed successfully
    BASE_TERMINAL_ERROR_INVALID_PARAM,     // Invalid parameter provided
    BASE_TERMINAL_ERROR_MEMORY_ALLOCATION, // Memory allocation failed
    BASE_TERMINAL_ERROR_TERMIOS_FAILED, // Terminal attributes operation failed
    BASE_TERMINAL_ERROR_TERMINAL_DETECTION, // Terminal type detection failed
    BASE_TERMINAL_ERROR_SIGNAL_HANDLER,     // Signal handler setup failed
    BASE_TERMINAL_ERROR_READ_FAILED,        // Terminal read operation failed
    BASE_TERMINAL_ERROR_WRITE_FAILED,       // Terminal write operation failed
    BASE_TERMINAL_ERROR_FLUSH_FAILED,       // Terminal flush operation failed
    BASE_TERMINAL_ERROR_IOCTL_FAILED,       // Terminal ioctl operation failed
    BASE_TERMINAL_ERROR_SELECT_FAILED,      // Select operation failed
    BASE_TERMINAL_ERROR_NOT_INITIALIZED     // Terminal not initialized
} base_terminal_error_t;

/**
 * Performance metrics for terminal operations
 */
typedef struct {
    uint64_t total_reads;         // Total number of read operations
    uint64_t total_writes;        // Total number of write operations
    uint64_t total_bytes_read;    // Total bytes read from terminal
    uint64_t total_bytes_written; // Total bytes written to terminal
    uint64_t
        initialization_time_ns;  // Time taken for initialization (nanoseconds)
    uint64_t last_read_time_ns;  // Timestamp of last read operation
    uint64_t last_write_time_ns; // Timestamp of last write operation
} base_terminal_metrics_t;

/**
 * Base terminal instance structure
 *
 * Contains all state and configuration for a terminal instance.
 * This structure should be treated as opaque by higher layers.
 */
typedef struct {
    // File descriptors
    int input_fd;  // Input file descriptor (usually STDIN)
    int output_fd; // Output file descriptor (usually STDOUT)
    int error_fd;  // Error file descriptor (usually STDERR)

    // Terminal state
    struct termios original_termios; // Original terminal attributes
    struct termios current_termios;  // Current terminal attributes
    bool raw_mode_enabled;           // True if raw mode is active
    bool initialized;                // True if terminal is initialized

    // Terminal information
    char *terminal_type; // Terminal type string (from $TERM)
    int terminal_width;  // Current terminal width
    int terminal_height; // Current terminal height

    // Error handling
    base_terminal_error_t last_error; // Last error that occurred

    // Performance metrics
    base_terminal_metrics_t metrics; // Performance tracking data

    // Internal state (implementation specific)
    void *private_data; // Private implementation data
} base_terminal_t;

// ============================================================================
// LIFECYCLE FUNCTIONS
// ============================================================================

/**
 * Create a new base terminal instance
 *
 * Allocates and initializes a new base_terminal_t structure with default
 * values. The terminal is not yet initialized for use - call
 * base_terminal_init() to complete initialization.
 *
 * @return Pointer to new base_terminal_t instance, or NULL on failure
 *
 * @note The returned pointer must be freed with base_terminal_destroy()
 */
base_terminal_t *base_terminal_create(void);

/**
 * Initialize the base terminal system
 *
 * Performs complete initialization of the terminal including:
 * - Validation of terminal file descriptors
 * - Saving original terminal attributes
 * - Detection of terminal type and capabilities
 * - Setup of signal handlers
 * - Configuration of initial terminal modes
 *
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 *
 * @note Must be called before any other terminal operations
 * @note Terminal starts in canonical mode - use base_terminal_set_raw_mode() to
 * change
 */
base_terminal_error_t base_terminal_init(base_terminal_t *terminal);

/**
 * Clean up and restore terminal state
 *
 * Restores the terminal to its original state and cleans up all resources:
 * - Restores original terminal attributes
 * - Restores original signal handlers
 * - Frees allocated memory
 * - Resets internal state
 *
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 *
 * @note Safe to call multiple times
 * @note Does not free the terminal structure itself - use
 * base_terminal_destroy()
 */
base_terminal_error_t base_terminal_cleanup(base_terminal_t *terminal);

/**
 * Destroy base terminal instance
 *
 * Performs cleanup and frees the terminal structure. After calling this
 * function, the terminal pointer is invalid and should not be used.
 *
 * @param terminal Pointer to base_terminal_t instance
 *
 * @note Automatically calls base_terminal_cleanup() if needed
 * @note Safe to call with NULL pointer
 */
void base_terminal_destroy(base_terminal_t *terminal);

// ============================================================================
// TERMINAL MODE FUNCTIONS
// ============================================================================

/**
 * Enable or disable raw terminal mode
 *
 * Raw mode disables line buffering and special character processing,
 * providing direct access to keystrokes. This is typically used for
 * interactive applications that need immediate response to user input.
 *
 * Raw mode changes:
 * - Disables canonical input processing (ICANON)
 * - Disables echo (ECHO)
 * - Disables signal generation (ISIG)
 * - Disables flow control (IXON)
 * - Sets minimum read to 0 characters
 * - Sets read timeout to 0.1 seconds
 *
 * @param terminal Pointer to base_terminal_t instance
 * @param enable True to enable raw mode, false to restore canonical mode
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 *
 * @note Terminal must be initialized before calling this function
 * @note Canonical mode is restored automatically during cleanup
 */
base_terminal_error_t base_terminal_set_raw_mode(base_terminal_t *terminal,
                                                 bool enable);

// ============================================================================
// INPUT/OUTPUT FUNCTIONS
// ============================================================================

/**
 * Read data from terminal input
 *
 * Reads available data from the terminal input. In canonical mode, this
 * will read complete lines. In raw mode, this will read individual
 * characters as they become available.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @param buffer Buffer to store read data
 * @param max_len Maximum number of bytes to read
 * @return Number of bytes read, or -1 on error
 *
 * @note In raw mode, may return 0 if no data is available
 * @note Buffer is not null-terminated - caller must handle this
 * @note Updates performance metrics on successful reads
 */
ssize_t base_terminal_read(base_terminal_t *terminal, char *buffer,
                           size_t max_len);

/**
 * Write data to terminal output
 *
 * Writes data to the terminal output. Handles partial writes and retries
 * automatically for robust operation.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @param data Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or -1 on error
 *
 * @note May perform multiple write() calls to handle partial writes
 * @note Updates performance metrics on successful writes
 * @note Does not automatically flush - use base_terminal_flush() if needed
 */
ssize_t base_terminal_write(base_terminal_t *terminal, const char *data,
                            size_t len);

/**
 * Flush terminal output buffer
 *
 * Forces any buffered output to be written to the terminal immediately.
 * This is important for ensuring that output appears promptly, especially
 * for interactive applications.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 *
 * @note Automatically called during cleanup
 * @note May block briefly while flushing buffers
 */
base_terminal_error_t base_terminal_flush(base_terminal_t *terminal);

// ============================================================================
// TERMINAL INFORMATION FUNCTIONS
// ============================================================================

/**
 * Get current terminal size
 *
 * Retrieves the current terminal dimensions in characters. This information
 * is updated automatically when the terminal is resized.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @param width Pointer to store terminal width (columns)
 * @param height Pointer to store terminal height (rows)
 * @return BASE_TERMINAL_SUCCESS on success, error code on failure
 *
 * @note Values are updated in real-time when terminal is resized
 * @note Returns cached values for performance - updated on SIGWINCH
 */
base_terminal_error_t base_terminal_get_size(base_terminal_t *terminal,
                                             int *width, int *height);

/**
 * Check if data is available for reading
 *
 * Uses select() to check if input data is available without blocking.
 * This is useful for implementing non-blocking input loops.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @param timeout_ms Timeout in milliseconds (0 for immediate return)
 * @return 1 if data available, 0 if no data, -1 on error
 *
 * @note timeout_ms of 0 provides immediate return (polling)
 * @note timeout_ms < 0 blocks indefinitely until data is available
 */
int base_terminal_data_available(base_terminal_t *terminal, int timeout_ms);

// ============================================================================
// PERFORMANCE AND DIAGNOSTICS
// ============================================================================

/**
 * Get current performance metrics
 *
 * Returns a copy of the current performance metrics for monitoring
 * and optimization purposes.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @return Copy of current performance metrics
 *
 * @note Returns zeroed metrics if terminal is NULL or uninitialized
 * @note Metrics are updated in real-time during operations
 */
base_terminal_metrics_t base_terminal_get_metrics(base_terminal_t *terminal);

/**
 * Get high-resolution timestamp in nanoseconds
 *
 * Provides a monotonic timestamp for performance measurement and
 * timing operations.
 *
 * @return Current timestamp in nanoseconds since an arbitrary epoch
 *
 * @note Uses CLOCK_MONOTONIC for consistent timing
 * @note Returns 0 if high-resolution timing is not available
 */
uint64_t base_terminal_get_timestamp_ns(void);

// ============================================================================
// ERROR HANDLING
// ============================================================================

/**
 * Get last error code
 *
 * Returns the last error that occurred on this terminal instance.
 * Error codes are preserved until the next operation or until
 * explicitly cleared.
 *
 * @param terminal Pointer to base_terminal_t instance
 * @return Last error code, or BASE_TERMINAL_ERROR_INVALID_PARAM if terminal is
 * NULL
 *
 * @note Error state is per-terminal instance
 * @note Successful operations do not clear error state
 */
base_terminal_error_t base_terminal_get_last_error(base_terminal_t *terminal);

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
const char *base_terminal_error_string(base_terminal_error_t error);

// ============================================================================
// VERSION INFORMATION
// ============================================================================

/**
 * Get base terminal layer version
 *
 * Returns version information for the base terminal layer implementation.
 *
 * @param major Pointer to store major version number
 * @param minor Pointer to store minor version number
 * @param patch Pointer to store patch version number
 */
static inline void base_terminal_get_version(int *major, int *minor,
                                             int *patch) {
    if (major) {
        *major = BASE_TERMINAL_VERSION_MAJOR;
    }
    if (minor) {
        *minor = BASE_TERMINAL_VERSION_MINOR;
    }
    if (patch) {
        *patch = BASE_TERMINAL_VERSION_PATCH;
    }
}

#ifdef __cplusplus
}
#endif

#endif // BASE_TERMINAL_H

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/*
 * Basic usage example:
 *
 * ```c
 * #include "base_terminal.h"
 *
 * int main() {
 *     // Create and initialize terminal
 *     base_terminal_t *terminal = base_terminal_create();
 *     if (!terminal) return 1;
 *
 *     if (base_terminal_init(terminal) != BASE_TERMINAL_SUCCESS) {
 *         base_terminal_destroy(terminal);
 *         return 1;
 *     }
 *
 *     // Enable raw mode for character-by-character input
 *     base_terminal_set_raw_mode(terminal, true);
 *
 *     // Read a single character
 *     char ch;
 *     if (base_terminal_read(terminal, &ch, 1) == 1) {
 *         printf("You pressed: %c\n", ch);
 *     }
 *
 *     // Write some output
 *     const char *message = "Hello, terminal!\n";
 *     base_terminal_write(terminal, message, strlen(message));
 *     base_terminal_flush(terminal);
 *
 *     // Get terminal size
 *     int width, height;
 *     if (base_terminal_get_size(terminal, &width, &height) ==
 * BASE_TERMINAL_SUCCESS) { printf("Terminal size: %dx%d\n", width, height);
 *     }
 *
 *     // Check performance metrics
 *     base_terminal_metrics_t metrics = base_terminal_get_metrics(terminal);
 *     printf("Total reads: %lu, Total writes: %lu\n",
 *            metrics.total_reads, metrics.total_writes);
 *
 *     // Cleanup
 *     base_terminal_destroy(terminal);
 *     return 0;
 * }
 * ```
 *
 * Non-blocking input example:
 *
 * ```c
 * // Check for input without blocking
 * while (running) {
 *     if (base_terminal_data_available(terminal, 0) > 0) {
 *         char buffer[256];
 *         ssize_t bytes = base_terminal_read(terminal, buffer, sizeof(buffer));
 *         if (bytes > 0) {
 *             // Process input
 *             process_input(buffer, bytes);
 *         }
 *     }
 *
 *     // Do other work
 *     perform_background_tasks();
 * }
 * ```
 */
