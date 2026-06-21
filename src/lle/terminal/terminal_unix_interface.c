/**
 * @file terminal_unix_interface.c
 * @brief Unix Terminal Interface (Spec 02 Subsystem 6)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Minimal Unix terminal interface abstraction providing:
 * - Raw mode setup and teardown
 * - Terminal attribute saving and restoration
 * - Signal handling (SIGWINCH, SIGTSTP, SIGCONT, SIGINT, SIGTERM)
 * - Non-blocking input with timeout support
 * - UTF-8 character decoding
 * - Window resize event generation
 * - EOF and error detection
 *
 * Critical Principles:
 * - Always restore terminal state on exit
 * - Thread-safe state transitions
 * - Async-signal-safe signal handlers
 * - Idempotent operations (safe to call multiple times)
 *
 * Spec 02: Terminal Abstraction - Subsystem 6
 */

#include "lle/csi_decoder.h"
#include "lle/terminal_abstraction.h"
#include "lle/time_util.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * GLOBAL STATE FOR SIGNAL HANDLERS
 * ============================================================================
 *
 * Unfortunately, POSIX signal handlers require global state. We use a single
 * global pointer protected by mutex where possible.
 */

static lle_unix_interface_t *g_signal_interface = NULL;

/* ============================================================================
 * SIGNAL HANDLERS
 * ============================================================================
 *
 * Signal handlers must be async-signal-safe. Only a limited set of functions
 * are allowed: tcsetattr, signal, raise, write, etc. NO malloc, printf, etc.
 */

/**
 * @brief SIGWINCH handler - window size changed
 *
 * This is called when the terminal window is resized. We don't do much here
 * because we can't safely call complex functions in signal context.
 * Sets a flag that will be checked in the event loop.
 *
 * @param sig Signal number (unused, always SIGWINCH)
 */
static void handle_sigwinch(int sig) {
    (void)sig;

    /// Set flag to be checked in event loop (async-signal-safe)
    if (g_signal_interface) {
        g_signal_interface->sigwinch_received = true;
    }
}

/**
 * @brief SIGTSTP handler - suspend (Ctrl-Z)
 *
 * Before suspending, we must restore the terminal to its original state
 * so the user gets a normal shell prompt when backgrounded. After restoration,
 * the signal is re-raised with the default handler to actually suspend.
 *
 * @param sig Signal number (always SIGTSTP)
 */
static void handle_sigtstp(int sig) {
    if (!g_signal_interface)
        return;

    /// Exit raw mode before suspending (async-signal-safe)
    if (g_signal_interface->raw_mode_active) {
        tcsetattr(g_signal_interface->terminal_fd, TCSAFLUSH,
                  &g_signal_interface->original_termios);
    }

    /// Re-raise signal with default handler to actually suspend
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * @brief SIGCONT handler - resume after suspend
 *
 * When resumed from background, re-enter raw mode if we were in it
 * and re-install the SIGTSTP handler which was reset to default.
 *
 * @param sig Signal number (unused, always SIGCONT)
 */
static void handle_sigcont(int sig) {
    (void)sig;

    if (!g_signal_interface)
        return;

    /// Re-enter raw mode if we were in it (async-signal-safe)
    if (g_signal_interface->raw_mode_active) {
        tcsetattr(g_signal_interface->terminal_fd, TCSAFLUSH,
                  &g_signal_interface->raw_termios);
    }

    /// Re-install SIGTSTP handler (it was reset to default)
    signal(SIGTSTP, handle_sigtstp);
}

/*
 * NOTE: handle_exit_signal() removed - we no longer install SIGINT/SIGTERM
 * handlers. Lush's signal handlers (src/signals.c) now manage these signals
 * correctly.
 */

/* ============================================================================
 * CLEANUP ON EXIT
 * ============================================================================
 */

/**
 * @brief atexit handler - ensure terminal is restored even on abnormal exit
 *
 * This function is registered with atexit() to ensure the terminal is
 * restored to its original state even if the program exits unexpectedly.
 */
static void cleanup_on_exit(void) {
    if (g_signal_interface && g_signal_interface->raw_mode_active) {
        /// This is safe in atexit context
        tcsetattr(g_signal_interface->terminal_fd, TCSAFLUSH,
                  &g_signal_interface->original_termios);
    }
}

/**
 * @brief Register atexit cleanup handler (called once)
 *
 * Registers the cleanup_on_exit() function with atexit() if not already
 * registered. Uses a static flag to ensure single registration.
 */
static void register_cleanup(void) {
    static bool registered = false;

    if (!registered) {
        atexit(cleanup_on_exit);
        registered = true;
    }
}

/* ============================================================================
 * SIGNAL HANDLER INSTALLATION
 * ============================================================================
 */

/** Storage for original signal handlers (static lifetime) */
static struct sigaction original_sigwinch;
static struct sigaction original_sigtstp;
static struct sigaction original_sigcont;
/* Note: We don't install SIGINT/SIGTERM handlers anymore (lush handles them)
 */
static bool signals_installed = false;

/**
 * @brief Install all signal handlers
 *
 * Installs handlers for SIGWINCH, SIGTSTP, and SIGCONT signals.
 * Uses sigaction for reliable signal handling with SA_RESTART flag.
 * SIGINT and SIGTERM are left to lush's main signal handlers.
 *
 * @param interface Unix interface to associate with handlers
 * @return LLE_SUCCESS on success, LLE_ERROR_SYSTEM_CALL on sigaction failure
 */
static lle_result_t install_signal_handlers(lle_unix_interface_t *interface) {
    if (signals_installed) {
        return LLE_SUCCESS; /// Already installed
    }

    struct sigaction sa;

    /// SIGWINCH - window resize
    /// Block SIGTSTP and SIGCONT while handling SIGWINCH to prevent nesting
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTSTP);
    sigaddset(&sa.sa_mask, SIGCONT);
    sa.sa_flags = SA_RESTART; /// Restart interrupted system calls

    if (sigaction(SIGWINCH, &sa, &original_sigwinch) != 0) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    /// SIGTSTP - suspend (Ctrl-Z)
    /// Block SIGWINCH while handling SIGTSTP to prevent nesting
    sa.sa_handler = handle_sigtstp;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGWINCH);
    if (sigaction(SIGTSTP, &sa, &original_sigtstp) != 0) {
        sigaction(SIGWINCH, &original_sigwinch, NULL);
        return LLE_ERROR_SYSTEM_CALL;
    }

    /// SIGCONT - resume
    /// Block SIGWINCH while handling SIGCONT to prevent nesting
    sa.sa_handler = handle_sigcont;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGWINCH);
    if (sigaction(SIGCONT, &sa, &original_sigcont) != 0) {
        sigaction(SIGWINCH, &original_sigwinch, NULL);
        sigaction(SIGTSTP, &original_sigtstp, NULL);
        return LLE_ERROR_SYSTEM_CALL;
    }

    /// NOTE: We do NOT install SIGINT/SIGTERM handlers here.
    /// Lush's signal handlers (src/signals.c) manage these properly:
    /// - SIGINT: kills child process OR clears line (but never exits shell)
    /// - SIGTERM: handles graceful shutdown
    ///
    /// LLE previously installed handlers that would exit the shell on Ctrl+C,
    /// which is incorrect shell behavior. Now that ISIG is enabled in raw mode,
    /// Ctrl+C generates SIGINT which lush's handler will catch and handle
    /// correctly.

    /// Set global pointer for handlers
    g_signal_interface = interface;
    signals_installed = true;

    return LLE_SUCCESS;
}

/**
 * @brief Restore original signal handlers
 *
 * Restores the original signal handlers that were saved when
 * install_signal_handlers() was called. Only restores if the
 * provided interface is the one that originally installed them.
 *
 * @param interface Unix interface that installed the handlers
 */
static void restore_signal_handlers(lle_unix_interface_t *interface) {
    if (!signals_installed) {
        return;
    }

    /// Only restore if this was the interface that installed them
    if (g_signal_interface != interface) {
        return;
    }

    /// Restore signal handlers that we installed
    sigaction(SIGWINCH, &original_sigwinch, NULL);
    sigaction(SIGTSTP, &original_sigtstp, NULL);
    sigaction(SIGCONT, &original_sigcont, NULL);
    /// Note: We don't restore SIGINT/SIGTERM because we never installed them

    /// Clear global pointer
    g_signal_interface = NULL;
    signals_installed = false;
}

/* ============================================================================
 * PUBLIC API FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize Unix terminal interface
 *
 * @param interface Output pointer for created interface
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_interface_init(lle_unix_interface_t **interface) {
    if (!interface) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate interface structure
    lle_unix_interface_t *iface = calloc(1, sizeof(lle_unix_interface_t));
    if (!iface) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Set file descriptor (using STDIN for terminal operations)
    iface->terminal_fd = STDIN_FILENO;

    /// Save original terminal state (if this is a TTY)
    /// In non-TTY environments (tests, pipes), tcgetattr will fail - that's OK
    if (tcgetattr(iface->terminal_fd, &iface->original_termios) != 0) {
        /// Not a TTY - initialize with empty termios
        memset(&iface->original_termios, 0, sizeof(struct termios));
    }

    /// Initialize state
    iface->raw_mode_active = false;
    iface->size_changed = false;
    iface->sigwinch_received = false;
    iface->last_error = LLE_SUCCESS;

    /// Get initial window size
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        iface->current_width = ws.ws_col;
        iface->current_height = ws.ws_row;
    } else {
        iface->current_width = 80;
        iface->current_height = 24;
    }

    /// Install signal handlers
    lle_result_t result = install_signal_handlers(iface);
    if (result != LLE_SUCCESS) {
        free(iface);
        return result;
    }

    /// Register atexit cleanup
    register_cleanup();

    *interface = iface;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy Unix terminal interface
 *
 * @param interface Interface to destroy
 */
void lle_unix_interface_destroy(lle_unix_interface_t *interface) {
    if (!interface) {
        return;
    }

    /// Ensure we exit raw mode before cleanup
    if (interface->raw_mode_active) {
        lle_unix_interface_exit_raw_mode(interface);
    }

    /// Restore original signal handlers
    restore_signal_handlers(interface);

    /// Free structure
    free(interface);
}

/**
 * @brief Enter raw (non-canonical) mode
 *
 * @param interface Unix interface instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_unix_interface_enter_raw_mode(lle_unix_interface_t *interface) {
    if (!interface) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Already in raw mode? (idempotent)
    if (interface->raw_mode_active) {
        return LLE_SUCCESS;
    }

    /// Copy original settings
    interface->raw_termios = interface->original_termios;

    /// Modify for raw mode
    struct termios *raw = &interface->raw_termios;

    /// Input flags - disable special processing
    raw->c_iflag &= ~(BRKINT | /// No break signal
                      ICRNL |  /// Don't translate CR to NL
                      INPCK |  /// Disable parity checking
                      ISTRIP | /// Don't strip 8th bit
                      IXON);   /// Disable XON/XOFF flow control

    /// Output flags - KEEP output processing for proper display
    /// NOTE: Disabling OPOST causes display corruption - \n won't return to
    /// column 0
    /// We need raw INPUT mode, but output should remain processed for display

    /// Control flags - 8-bit characters
    raw->c_cflag |= (CS8); /// 8 bits per byte

    /// Local flags - disable canonical mode and echo, but KEEP signals enabled
    raw->c_lflag &= ~(ECHO |   /// No echo
                      ICANON | /// Non-canonical mode
                      IEXTEN); /// Disable extended input processing
    /// KEEP ISIG ENABLED - allow Ctrl-C to generate SIGINT for proper shell
    /// behavior
    /// This ensures lush's signal handler (src/signals.c) can manage child
    /// processes

    /// Control characters - non-blocking read
    raw->c_cc[VMIN] = 0;  /// Non-blocking: return immediately
    raw->c_cc[VTIME] = 0; /// No timeout

    /// Apply settings - TCSAFLUSH discards unread input
    if (tcsetattr(interface->terminal_fd, TCSAFLUSH, raw) != 0) {
        interface->last_error = LLE_ERROR_SYSTEM_CALL;
        return LLE_ERROR_SYSTEM_CALL;
    }

    interface->raw_mode_active = true;
    return LLE_SUCCESS;
}

/**
 * @brief Exit raw mode and restore original terminal state
 *
 * @param interface Unix interface instance
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_interface_exit_raw_mode(lle_unix_interface_t *interface) {
    if (!interface) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Not in raw mode? (idempotent)
    if (!interface->raw_mode_active) {
        return LLE_SUCCESS;
    }

    /// Restore original settings
    if (tcsetattr(interface->terminal_fd, TCSAFLUSH,
                  &interface->original_termios) != 0) {
        interface->last_error = LLE_ERROR_SYSTEM_CALL;
        return LLE_ERROR_SYSTEM_CALL;
    }

    interface->raw_mode_active = false;
    return LLE_SUCCESS;
}

/**
 * @brief Get current window size
 *
 * @param interface Unix interface instance
 * @param width Output for terminal width
 * @param height Output for terminal height
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_interface_get_window_size(lle_unix_interface_t *interface,
                                                size_t *width, size_t *height) {
    if (!interface || !width || !height) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    struct winsize ws;

    /// Try ioctl first
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
        ws.ws_row > 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;

        /// Update cached size
        interface->current_width = ws.ws_col;
        interface->current_height = ws.ws_row;

        return LLE_SUCCESS;
    }

    /// Fallback to environment variables
    const char *env_cols = getenv("COLUMNS");
    const char *env_lines = getenv("LINES");

    *width = (env_cols && *env_cols) ? (size_t)atoi(env_cols) : 80;
    *height = (env_lines && *env_lines) ? (size_t)atoi(env_lines) : 24;

    /// Update cached size
    interface->current_width = *width;
    interface->current_height = *height;

    return LLE_SUCCESS;
}

/* ============================================================================
 * UTF-8 DECODING HELPERS
 * ============================================================================
 */

/**
 * @brief Determine UTF-8 sequence length from first byte
 *
 * Analyzes the high bits of the first byte to determine how many
 * bytes are in the complete UTF-8 sequence.
 *
 * @param first_byte First byte of UTF-8 sequence
 * @return Expected sequence length (1-4), or -1 for invalid first byte
 */
static int get_utf8_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0x00)
        return 1; /// 0xxxxxxx - ASCII
    if ((first_byte & 0xE0) == 0xC0)
        return 2; /// 110xxxxx - 2 bytes
    if ((first_byte & 0xF0) == 0xE0)
        return 3; /// 1110xxxx - 3 bytes
    if ((first_byte & 0xF8) == 0xF0)
        return 4; /// 11110xxx - 4 bytes
    return -1;    /// Invalid first byte
}

/**
 * @brief Decode UTF-8 sequence to Unicode codepoint
 *
 * This decoder:
 * - Validates basic structure (continuation bytes)
 * - Returns replacement character (U+FFFD) for invalid sequences
 * - Does not detect overlong sequences (acceptable tradeoff)
 *
 * @param interface Unix interface for reading additional bytes from terminal
 * @param first_byte First byte already read from input
 * @param codepoint_out Output for decoded Unicode codepoint
 * @param utf8_bytes Output buffer for complete UTF-8 bytes (must be at least 8
 * bytes)
 * @param byte_count_out Output for number of bytes in sequence (1-4)
 * @return LLE_SUCCESS on success (always succeeds, uses replacement char on
 * error)
 */
static lle_result_t decode_utf8(lle_unix_interface_t *interface,
                                unsigned char first_byte,
                                uint32_t *codepoint_out, char *utf8_bytes,
                                uint8_t *byte_count_out) {
    int expected_bytes = get_utf8_length(first_byte);

    if (expected_bytes < 0) {
        /// Invalid first byte - use replacement character
        *codepoint_out = 0xFFFD;
        utf8_bytes[0] = (char)first_byte;
        *byte_count_out = 1;
        return LLE_SUCCESS;
    }

    utf8_bytes[0] = (char)first_byte;
    *byte_count_out = (uint8_t)expected_bytes;

    if (expected_bytes == 1) {
        /// ASCII - fast path
        *codepoint_out = first_byte;
        return LLE_SUCCESS;
    }

    /// Read additional bytes for multi-byte sequence
    for (int i = 1; i < expected_bytes; i++) {
        unsigned char byte;
        ssize_t n = read(interface->terminal_fd, &byte, 1);

        if (n <= 0) {
            /// Incomplete sequence - use replacement character
            *codepoint_out = 0xFFFD;
            return LLE_SUCCESS;
        }

        /// Validate continuation byte (10xxxxxx)
        if ((byte & 0xC0) != 0x80) {
            /// Invalid continuation - use replacement character
            *codepoint_out = 0xFFFD;
            return LLE_SUCCESS;
        }

        utf8_bytes[i] = (char)byte;
    }

    /// Decode to Unicode codepoint
    switch (expected_bytes) {
    case 2:
        *codepoint_out =
            ((first_byte & 0x1F) << 6) | ((unsigned char)utf8_bytes[1] & 0x3F);
        break;
    case 3:
        *codepoint_out = ((first_byte & 0x0F) << 12) |
                         (((unsigned char)utf8_bytes[1] & 0x3F) << 6) |
                         ((unsigned char)utf8_bytes[2] & 0x3F);
        break;
    case 4:
        *codepoint_out = ((first_byte & 0x07) << 18) |
                         (((unsigned char)utf8_bytes[1] & 0x3F) << 12) |
                         (((unsigned char)utf8_bytes[2] & 0x3F) << 6) |
                         ((unsigned char)utf8_bytes[3] & 0x3F);
        break;
    default:
        *codepoint_out = 0xFFFD;
        break;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * EVENT READING
 * ============================================================================
 */

/**
 * @brief Read input event from terminal with timeout support
 *
 * This implementation provides:
 * - Non-blocking input with configurable timeout
 * - UTF-8 character decoding
 * - Window resize event generation (from SIGWINCH)
 * - EOF detection
 * - Timeout events
 *
 * Higher-level parsing (escape sequences, special keys) is handled by
 * Spec 06 Input Parsing, which wraps this primitive interface.
 *
 * @param interface Unix interface instance
 * @param event Output for read event
 * @param timeout_ms Timeout in milliseconds (UINT32_MAX for infinite)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_unix_interface_read_event(lle_unix_interface_t *interface,
                                           lle_input_event_t *event,
                                           uint32_t timeout_ms) {
    if (!interface || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Clear event structure
    memset(event, 0, sizeof(lle_input_event_t));

    /// Check for pending SIGWINCH (resize event has priority)
    if (interface->sigwinch_received) {
        interface->sigwinch_received = false;

        size_t width, height;
        lle_result_t result =
            lle_unix_interface_get_window_size(interface, &width, &height);
        if (result != LLE_SUCCESS) {
            event->type = LLE_INPUT_TYPE_ERROR;
            event->timestamp = lle_get_current_time_microseconds();
            event->data.error.error_code = result;
            snprintf(event->data.error.error_message,
                     sizeof(event->data.error.error_message),
                     "Failed to get window size after SIGWINCH");
            return result;
        }

        event->type = LLE_INPUT_TYPE_WINDOW_RESIZE;
        event->timestamp = lle_get_current_time_microseconds();
        event->data.resize.new_width = width;
        event->data.resize.new_height = height;
        interface->size_changed = true;

        return LLE_SUCCESS;
    }

    /// Use select() for timeout support
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(interface->terminal_fd, &readfds);

    struct timeval tv;
    struct timeval *tv_ptr;

    /// Determine effective timeout
    uint32_t effective_timeout_ms = timeout_ms;

    /// The inline CSI reader below drains a complete ESC sequence in
    /// a single read_event call (its own short reads consume params +
    /// final byte), so there is no cross-call "accumulating sequence"
    /// state to shorten the outer select() timeout for.

    if (effective_timeout_ms == UINT32_MAX) {
        /// Infinite timeout - pass NULL to select()
        tv_ptr = NULL;
    } else {
        tv.tv_sec = (time_t)(effective_timeout_ms / 1000);
        tv.tv_usec = (suseconds_t)((effective_timeout_ms % 1000) * 1000);
        tv_ptr = &tv;
    }

    int ready =
        select(interface->terminal_fd + 1, &readfds, NULL, NULL, tv_ptr);

    if (ready == -1) {
        if (errno == EINTR) {
            /// Interrupted by signal - check for resize
            if (interface->sigwinch_received) {
                /// Recursively handle resize event
                return lle_unix_interface_read_event(interface, event,
                                                     timeout_ms);
            }
            /// Other signal - return timeout
            event->type = LLE_INPUT_TYPE_TIMEOUT;
            event->timestamp = lle_get_current_time_microseconds();
            return LLE_SUCCESS;
        }
        /// System call error
        event->type = LLE_INPUT_TYPE_ERROR;
        event->timestamp = lle_get_current_time_microseconds();
        event->data.error.error_code = LLE_ERROR_SYSTEM_CALL;
        snprintf(event->data.error.error_message,
                 sizeof(event->data.error.error_message), "select() failed: %s",
                 strerror(errno));
        return LLE_ERROR_SYSTEM_CALL;
    }

    if (ready == 0) {
        /// Timeout - no data available.
        event->type = LLE_INPUT_TYPE_TIMEOUT;
        event->timestamp = lle_get_current_time_microseconds();
        return LLE_SUCCESS;
    }

    /// Data available - read first byte
    unsigned char first_byte = 0;
    ssize_t bytes_read = read(interface->terminal_fd, &first_byte, 1);

    if (bytes_read == -1) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            /// Non-blocking read interrupted - treat as timeout
            event->type = LLE_INPUT_TYPE_TIMEOUT;
            event->timestamp = lle_get_current_time_microseconds();
            return LLE_SUCCESS;
        }
        /// Read error
        event->type = LLE_INPUT_TYPE_ERROR;
        event->timestamp = lle_get_current_time_microseconds();
        event->data.error.error_code = LLE_ERROR_SYSTEM_CALL;
        snprintf(event->data.error.error_message,
                 sizeof(event->data.error.error_message), "read() failed: %s",
                 strerror(errno));
        return LLE_ERROR_SYSTEM_CALL;
    }

    if (bytes_read == 0) {
        /// EOF - stdin closed
        event->type = LLE_INPUT_TYPE_EOF;
        event->timestamp = lle_get_current_time_microseconds();
        return LLE_SUCCESS;
    }

    /// Check for escape sequences (ESC = 0x1B = 27).
    /// All CSI / SS3 / Meta+key decoding is done by the inline reader
    /// below; it is the canonical live-path decoder. The earlier
    /// sequence_parser + key_detector fallback chain was retired in
    /// the #155 fix because it could not classify modifier-bearing
    /// nav keys (`ESC[5;2~` etc.) and silently mapped them to
    /// LLE_INPUT_TYPE_ERROR, terminating the shell.
    if (first_byte == 0x1B) {
        /// Read next byte with short timeout to detect escape sequences
        unsigned char second_byte;
        fd_set read_fds;
        struct timeval escape_timeout;

        FD_ZERO(&read_fds);
        FD_SET(interface->terminal_fd, &read_fds);
        escape_timeout.tv_sec = 0;
        escape_timeout.tv_usec =
            100000; /// 100ms timeout for ESC+key (Meta) sequences

        int ready = select(interface->terminal_fd + 1, &read_fds, NULL, NULL,
                           &escape_timeout);

        if (ready > 0) {
            ssize_t read2 = read(interface->terminal_fd, &second_byte, 1);

            if (read2 == 1 && second_byte == '[') {
                /// CSI sequence: ESC [ <params> <final>. Read parameter
                /// and intermediate bytes until the final byte (0x40-0x7E)
                /// so the ENTIRE sequence is consumed (otherwise the
                /// trailing `~` / `;2~` leaks into the line as literal
                /// text). The decoding from (params, final) to a
                /// SPECIAL_KEY is delegated to lle_csi_decode -- pure,
                /// unit-tested in tests/lle/unit/test_csi_decoder.c.
                char csi_params[16];
                size_t csi_len = 0;
                unsigned char final_byte = 0;
                bool have_final = false;
                while (csi_len < sizeof(csi_params) - 1) {
                    unsigned char b;
                    if (read(interface->terminal_fd, &b, 1) != 1) {
                        break; /// incomplete sequence; nothing leaks
                    }
                    if (b >= 0x40 && b <= 0x7E) {
                        final_byte = b;
                        have_final = true;
                        break;
                    }
                    csi_params[csi_len++] = (char)b;
                }

                event->type = LLE_INPUT_TYPE_SPECIAL_KEY;
                event->timestamp = lle_get_current_time_microseconds();
                event->data.special_key.key = LLE_KEY_UNKNOWN;
                event->data.special_key.keycode = 0;
                event->data.special_key.modifiers = 0;

                if (have_final) {
                    (void)lle_csi_decode(csi_params, csi_len, (char)final_byte,
                                         event);
                }

                /// Recognized or not, the whole CSI sequence has been
                /// drained. A mapped key dispatches; an unmapped one is
                /// a no-op special key (never inserted as text).
                return LLE_SUCCESS;
            } else if (read2 == 1 && second_byte == 'O') {
                /// SS3 sequence - alternate function keys
                unsigned char final_byte;
                ssize_t read3 = read(interface->terminal_fd, &final_byte, 1);

                if (read3 == 1) {
                    event->type = LLE_INPUT_TYPE_SPECIAL_KEY;
                    event->timestamp = lle_get_current_time_microseconds();
                    (void)lle_ss3_decode((char)final_byte, event);
                    return LLE_SUCCESS;
                }
            } else if (read2 == 1 && second_byte >= 0x20 &&
                       second_byte < 0x7F) {
                /// ESC + printable ASCII character = Meta/Alt + character
                /// This is how macOS Terminal and other terminals send Alt+key
                /// when the Option key is configured as Meta, or when user
                /// physically presses ESC then a letter (e.g., ESC f for
                /// Alt-f).
                ///
                /// Range 0x20-0x7E covers printable ASCII (space through
                /// tilde). This enables M-f (forward-word), M-b
                /// (backward-word), etc.
                event->type = LLE_INPUT_TYPE_SPECIAL_KEY;
                event->timestamp = lle_get_current_time_microseconds();
                event->data.special_key.key = LLE_KEY_UNKNOWN;
                event->data.special_key.modifiers = LLE_MOD_ALT;
                event->data.special_key.keycode = second_byte;
                return LLE_SUCCESS;
            }
        }

        /// If we get here, it's just a plain ESC key (no second byte within
        /// timeout) or an unrecognized sequence - return ESC as a regular
        /// character
    }

    /// Decode UTF-8 character
    uint32_t codepoint;
    char utf8_bytes[8] = {0};
    uint8_t byte_count;

    lle_result_t decode_result =
        decode_utf8(interface, first_byte, &codepoint, utf8_bytes, &byte_count);
    if (decode_result != LLE_SUCCESS) {
        event->type = LLE_INPUT_TYPE_ERROR;
        event->timestamp = lle_get_current_time_microseconds();
        event->data.error.error_code = decode_result;
        snprintf(event->data.error.error_message,
                 sizeof(event->data.error.error_message),
                 "UTF-8 decoding failed");
        return decode_result;
    }

    /// Check if this is a Ctrl+letter combination (0x01-0x1A = Ctrl-A through
    /// Ctrl-Z) According to spec, these should be SPECIAL_KEY events with
    /// keycode and modifiers EXCEPT for special control characters that have
    /// their own meaning:
    /// - 0x09 (Tab / Ctrl-I)
    /// - 0x0A (Newline / Ctrl-J)
    /// - 0x0D (Enter / Ctrl-M)
    if (codepoint >= 0x01 && codepoint <= 0x1A && codepoint != 0x09 &&
        codepoint != 0x0A && codepoint != 0x0D) {
        /// Ctrl+letter: Convert to SPECIAL_KEY event with keycode and
        /// LLE_MOD_CTRL
        event->type = LLE_INPUT_TYPE_SPECIAL_KEY;
        event->timestamp = lle_get_current_time_microseconds();
        event->data.special_key.key =
            LLE_KEY_UNKNOWN; /// Not a special key like arrow/F-key
        event->data.special_key.modifiers = LLE_MOD_CTRL;
        event->data.special_key.keycode =
            codepoint + 0x40; /// 0x01->0x41='A', 0x02->0x42='B', etc.
        return LLE_SUCCESS;
    }

    /// Populate character event
    event->type = LLE_INPUT_TYPE_CHARACTER;
    event->timestamp = lle_get_current_time_microseconds();
    event->data.character.codepoint = codepoint;
    memcpy(event->data.character.utf8_bytes, utf8_bytes, byte_count);
    event->data.character.byte_count = byte_count;

    return LLE_SUCCESS;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get current time in microseconds
 *
 * Uses CLOCK_MONOTONIC for reliable timing (not affected by system time
 * changes).
 *
 * @return Current time in microseconds
 */
uint64_t lle_get_current_time_microseconds(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL +
               (uint64_t)ts.tv_nsec / 1000ULL;
    }

    /// Fallback to gettimeofday if CLOCK_MONOTONIC fails
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}
