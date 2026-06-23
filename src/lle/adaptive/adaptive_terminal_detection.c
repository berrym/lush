/**
 * @file adaptive_terminal_detection.c
 * @brief Comprehensive terminal capability detection system
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 - Adaptive Terminal Integration
 *
 * Comprehensive terminal capability detection with multi-tier approach:
 * 1. Environment variable analysis
 * 2. Terminal signature matching
 * 3. Safe runtime capability probing
 * 4. Fallback mode determination
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/time_util.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Performance monitoring
static lle_detection_performance_stats_t detection_stats = {0};

// Detection cache
static lle_terminal_detection_result_t *cached_result = NULL;
static uint64_t cache_timestamp_us = 0;
#define CACHE_TTL_US 30000000 // 30 seconds

/* ============================================================================
 * INTERNAL UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Simple wildcard pattern matching
 * @param pattern Pattern to match (supports * wildcard)
 * @param string String to match against
 * @return true if pattern matches string, false otherwise
 */
static bool pattern_match(const char *pattern, const char *string) {
    if (!pattern || !string) {
        return false;
    }

    // Simple wildcard matching implementation
    const char *p = pattern;
    const char *s = string;
    const char *star = NULL;
    const char *ss = NULL;

    while (*s) {
        if (*p == '*') {
            // Remember position of * for backtracking
            star = p++;
            ss = s;
        } else if (*p == *s || (*p >= 'A' && *p <= 'Z' && *p + 32 == *s) ||
                   (*p >= 'a' && *p <= 'z' && *p - 32 == *s)) {
            // Match (case-insensitive)
            p++;
            s++;
        } else if (star) {
            /// Backtrack to *
            p = star + 1;
            s = ++ss;
        } else {
            return false;
        }
    }

    // Skip trailing * in pattern
    while (*p == '*') {
        p++;
    }

    return *p == '\0';
}

/**
 * @brief Safe string copy with guaranteed null termination
 * @param dest Destination buffer
 * @param src Source string (can be NULL)
 * @param size Size of destination buffer
 */
static void safe_strncpy(char *dest, const char *src, size_t size) {
    if (!dest || size == 0) {
        return;
    }

    if (!src) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/* ============================================================================
 * ENVIRONMENT ANALYSIS
 * ============================================================================
 */

/**
 * @brief Analyze environment variables for terminal information
 * @param detection Detection result structure to populate
 * @return LLE_SUCCESS on success
 */
static lle_result_t
analyze_environment_variables(lle_terminal_detection_result_t *detection) {
    const char *term = getenv("TERM");
    const char *term_program = getenv("TERM_PROGRAM");
    const char *colorterm = getenv("COLORTERM");

    // Copy environment values
    safe_strncpy(detection->term_name, term ? term : "",
                 sizeof(detection->term_name));
    safe_strncpy(detection->term_program, term_program ? term_program : "",
                 sizeof(detection->term_program));
    safe_strncpy(detection->colorterm, colorterm ? colorterm : "",
                 sizeof(detection->colorterm));

    // Basic capability inference from environment
    detection->supports_truecolor =
        (colorterm && (strcmp(colorterm, "truecolor") == 0 ||
                       strcmp(colorterm, "24bit") == 0));
    detection->supports_256_colors = (term && strstr(term, "256"));
    detection->supports_colors =
        (term && (strstr(term, "color") || strstr(term, "256"))) ||
        detection->supports_truecolor;
    detection->supports_unicode =
        (colorterm != NULL); // COLORTERM usually implies UTF-8

    // Detect terminal multiplexer
    const char *tmux = getenv("TMUX");
    const char *sty = getenv("STY");

    if (tmux && tmux[0] != '\0') {
        detection->multiplexer_type = LLE_MUX_TYPE_TMUX;
    } else if ((term && strstr(term, "screen")) || (sty && sty[0] != '\0')) {
        detection->multiplexer_type = LLE_MUX_TYPE_SCREEN;
    } else {
        detection->multiplexer_type = LLE_MUX_TYPE_NONE;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * TERMINAL SIGNATURE MATCHING
 * ============================================================================
 */

/**
 * @brief Match terminal against known signature database
 * @param detection Detection result with environment info
 * @return Pointer to matching signature, or NULL if no match
 */
const lle_terminal_signature_t *
lle_match_terminal_signature(const lle_terminal_detection_result_t *detection) {

    if (!detection) {
        return NULL;
    }

    size_t count = 0;
    const lle_terminal_signature_t *signatures =
        lle_get_terminal_signature_database(&count);

    // Iterate through signatures in priority order
    for (size_t i = 0; i < count; i++) {
        const lle_terminal_signature_t *sig = &signatures[i];
        bool match = true;

        // Check TERM_PROGRAM pattern
        if (sig->term_program_pattern) {
            if (detection->term_program[0] == '\0' ||
                !pattern_match(sig->term_program_pattern,
                               detection->term_program)) {
                match = false;
            }
        }

        // Check TERM pattern
        if (match && sig->term_pattern) {
            if (detection->term_name[0] == '\0' ||
                !pattern_match(sig->term_pattern, detection->term_name)) {
                match = false;
            }
        }

        // Check additional environment variable
        if (match && sig->env_var_check) {
            const char *env_val = getenv(sig->env_var_check);
            if (!env_val || env_val[0] == '\0') {
                match = false;
            }
        }

        if (match) {
            return sig;
        }
    }

    return NULL; // No match found
}

/* ============================================================================
 * SAFE CAPABILITY PROBING
 * ============================================================================
 */

/**
 * @brief Probe a single terminal capability with timeout protection
 * @param query Escape sequence query to send
 * @param timeout_ms Timeout in milliseconds
 * @return true if terminal responded, false on timeout or error
 */
static bool probe_capability_with_timeout(const char *query, int timeout_ms) {
    // Only probe if stdout is a TTY
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }

    // Write query sequence
    if (write(STDOUT_FILENO, query, strlen(query)) < 0) {
        return false;
    }

    // Wait for response with timeout
    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

    if (result > 0) {
        // Data available - read and discard response
        char buffer[256];
        ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
        return bytes_read > 0;
    }

    return false; // Timeout or error
}

/**
 * @brief Safe terminal capability probing with timeout protection
 * @param detection Detection result to populate with probing results
 * @return LLE_SUCCESS on success, error code on failure
 *
 * Performs progressive capability probing including cursor positioning,
 * bracketed paste mode, and mouse support detection.
 */
lle_result_t lle_probe_terminal_capabilities_safe(
    lle_terminal_detection_result_t *detection) {

    /// Cannot probe without stdout TTY - mark as unsuccessful but not an error
    if (!detection->stdout_is_tty) {
        detection->probing_successful = false;
        /// Set all probe flags to false - this is complete behavior for non-TTY
        detection->supports_cursor_positioning = false;
        detection->supports_cursor_queries = false;
        detection->supports_bracketed_paste = false;
        detection->supports_mouse = false;
        return LLE_SUCCESS; // Successfully determined we cannot probe
    }

    // Save terminal state
    struct termios saved_termios;
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        detection->probing_successful = false;
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    // Set raw mode for accurate probing
    struct termios raw_termios = saved_termios;
    raw_termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_termios.c_oflag &= ~(OPOST);
    raw_termios.c_cflag |= (CS8);
    raw_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw_termios.c_cc[VMIN] = 0;
    raw_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
        detection->probing_successful = false;
        return LLE_ERROR_TERMINAL_ABSTRACTION;
    }

    // Progressive capability probing with timeout protection

    // Test cursor positioning (DSR - Device Status Report)
    detection->supports_cursor_positioning =
        probe_capability_with_timeout("\x1b[6n", 100);

    // If basic cursor support works, we likely have ANSI capability
    if (detection->supports_cursor_positioning) {
        detection->supports_cursor_queries = true;
    }

    // Test bracketed paste mode
    detection->supports_bracketed_paste =
        probe_capability_with_timeout("\x1b[?2004h", 25);

    // Test mouse support
    detection->supports_mouse =
        probe_capability_with_timeout("\x1b[?1000h", 50);

    // Restore terminal state
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);

    detection->probing_successful = true;
    return LLE_SUCCESS;
}

/* ============================================================================
 * MODE DETERMINATION
 * ============================================================================
 */

/**
 * @brief Determine fallback mode based on TTY status
 * @param detection Detection result with TTY information
 * @return Appropriate adaptive mode for the environment
 */
static lle_adaptive_mode_t
determine_fallback_mode(const lle_terminal_detection_result_t *detection) {

    // No TTY at all - non-interactive
    if (!detection->stdin_is_tty && !detection->stdout_is_tty) {
        return LLE_ADAPTIVE_MODE_NONE;
    }

    // Both stdin and stdout are TTY - native mode
    if (detection->stdin_is_tty && detection->stdout_is_tty) {
        return LLE_ADAPTIVE_MODE_NATIVE;
    }

    // Only stdout is TTY - enhanced mode (editor terminal pattern)
    if (!detection->stdin_is_tty && detection->stdout_is_tty) {
        return LLE_ADAPTIVE_MODE_ENHANCED;
    }

    // Capable output but no TTY - minimal mode
    return LLE_ADAPTIVE_MODE_MINIMAL;
}

/**
 * @brief Validate and adjust recommended mode for compatibility
 * @param detection Detection result with mode recommendation
 * @return Validated and possibly adjusted mode
 */
static lle_adaptive_mode_t
validate_and_adjust_mode(const lle_terminal_detection_result_t *detection) {

    lle_adaptive_mode_t mode = detection->recommended_mode;

    // Validate mode is compatible with TTY status
    switch (mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        // Native mode requires stdin TTY
        if (!detection->stdin_is_tty) {
            mode = LLE_ADAPTIVE_MODE_ENHANCED;
        }
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        // Enhanced mode requires stdout TTY
        if (!detection->stdout_is_tty) {
            mode = LLE_ADAPTIVE_MODE_MINIMAL;
        }
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        // Multiplexed mode requires both TTYs
        if (!detection->stdin_is_tty || !detection->stdout_is_tty) {
            mode = LLE_ADAPTIVE_MODE_ENHANCED;
        }
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
    case LLE_ADAPTIVE_MODE_NONE:
        // No validation needed
        break;
    }

    return mode;
}

/* ============================================================================
 * PUBLIC API - MAIN DETECTION
 * ============================================================================
 */

/**
 * @brief Perform comprehensive terminal capability detection
 * @param result Pointer to store allocated detection result
 * @return LLE_SUCCESS on success, error code on failure
 *
 * Performs full multi-tier detection including environment analysis,
 * signature matching, and runtime capability probing.
 */
lle_result_t lle_detect_terminal_capabilities_comprehensive(
    lle_terminal_detection_result_t **result) {

    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate detection result using standard malloc, not the pool.
    /// This is intentional: the detection result is cached as a singleton
    /// and may outlive pool shutdown. Using the pool would cause double-free
    /// or use-after-free when the pool is cleaned up while cached_result
    /// still holds a reference.
    lle_terminal_detection_result_t *detection =
        calloc(1, sizeof(lle_terminal_detection_result_t));
    if (!detection) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "adaptive",
                         "adaptive terminal detection allocation failed");
    }

    uint64_t start_time = lle_get_current_time_microseconds();

    // Step 1: Basic TTY status detection
    detection->stdin_is_tty = isatty(STDIN_FILENO);
    detection->stdout_is_tty = isatty(STDOUT_FILENO);
    detection->stderr_is_tty = isatty(STDERR_FILENO);

    // Get terminal dimensions
    lle_get_terminal_size(&detection->terminal_cols, &detection->terminal_rows);

    // Step 2: Environment variable analysis
    lle_result_t env_result = analyze_environment_variables(detection);
    if (env_result != LLE_SUCCESS) {
        // Continue with conservative defaults
        detection->capability_level = LLE_CAPABILITY_BASIC;
    }

    // Step 3: Terminal signature matching
    detection->matched_signature = lle_match_terminal_signature(detection);
    if (detection->matched_signature) {
        detection->capability_level =
            detection->matched_signature->capability_level;
        detection->recommended_mode =
            detection->matched_signature->preferred_mode;
        detection->detection_confidence_high = true;
    } else {
        // Step 4: Runtime capability probing (for unknown terminals)
        lle_result_t probe_result =
            lle_probe_terminal_capabilities_safe(detection);
        detection->probing_successful = (probe_result == LLE_SUCCESS);
        detection->detection_confidence_high = detection->probing_successful;

        // Step 5: Fallback mode determination
        detection->recommended_mode = determine_fallback_mode(detection);

        // Infer capability level from probing results
        if (detection->supports_truecolor) {
            detection->capability_level = LLE_CAPABILITY_PREMIUM;
        } else if (detection->supports_256_colors) {
            detection->capability_level = LLE_CAPABILITY_FULL;
        } else if (detection->supports_colors) {
            detection->capability_level = LLE_CAPABILITY_STANDARD;
        } else {
            detection->capability_level = LLE_CAPABILITY_BASIC;
        }
    }

    // Step 6: Final mode validation and adjustment
    detection->recommended_mode = validate_and_adjust_mode(detection);

    detection->detection_time_us =
        lle_get_current_time_microseconds() - start_time;

    // Update statistics
    detection_stats.total_detections++;
    detection_stats.avg_detection_time_us =
        ((detection_stats.avg_detection_time_us *
          (detection_stats.total_detections - 1)) +
         detection->detection_time_us) /
        detection_stats.total_detections;
    if (detection->detection_time_us > detection_stats.max_detection_time_us) {
        detection_stats.max_detection_time_us = detection->detection_time_us;
    }

    *result = detection;
    return LLE_SUCCESS;
}

/**
 * @brief Optimized detection with result caching
 * @param result Pointer to store detection result (may be cached)
 * @return LLE_SUCCESS on success, error code on failure
 *
 * Returns cached result if available and not expired, otherwise
 * performs full detection and caches the result.
 */
lle_result_t lle_detect_terminal_capabilities_optimized(
    lle_terminal_detection_result_t **result) {

    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    uint64_t current_time = lle_get_current_time_microseconds();

    // Check cache validity
    if (cached_result && (current_time - cache_timestamp_us) < CACHE_TTL_US) {
        // Return cached result
        *result = cached_result;
        detection_stats.cache_hits++;
        return LLE_SUCCESS;
    }

    // Cache miss - perform full detection
    detection_stats.cache_misses++;

    lle_result_t res = lle_detect_terminal_capabilities_comprehensive(result);
    if (res == LLE_SUCCESS) {
        // Update cache
        if (cached_result) {
            free(cached_result);
        }
        cached_result = *result;
        cache_timestamp_us = current_time;
    }

    return res;
}

/**
 * @brief Free a detection result
 * @param result Detection result to free (safe if NULL or cached)
 */
void lle_terminal_detection_result_destroy(
    lle_terminal_detection_result_t *result) {
    if (result && result != cached_result) {
        free(result);
    }
}

/* ============================================================================
 * PUBLIC API - PERFORMANCE MONITORING
 * ============================================================================
 */

/**
 * @brief Get detection performance statistics
 * @param stats Pointer to statistics structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if stats is NULL
 */
lle_result_t
lle_adaptive_get_detection_stats(lle_detection_performance_stats_t *stats) {

    if (!stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *stats = detection_stats;
    return LLE_SUCCESS;
}

/**
 * @brief Reset detection performance statistics to zero
 */
void lle_adaptive_reset_detection_stats(void) {
    memset(&detection_stats, 0, sizeof(detection_stats));
}

/* ============================================================================
 * PUBLIC API - UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Convert adaptive mode to human-readable string
 * @param mode The adaptive mode value
 * @return Static string describing the mode
 */
const char *lle_adaptive_mode_to_string(lle_adaptive_mode_t mode) {
    switch (mode) {
    case LLE_ADAPTIVE_MODE_NONE:
        return "none";
    case LLE_ADAPTIVE_MODE_MINIMAL:
        return "minimal";
    case LLE_ADAPTIVE_MODE_ENHANCED:
        return "enhanced";
    case LLE_ADAPTIVE_MODE_NATIVE:
        return "native";
    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        return "multiplexed";
    default:
        return "unknown";
    }
}

/**
 * @brief Convert capability level to human-readable string
 * @param level The capability level value
 * @return Static string describing the capability level
 */
const char *lle_capability_level_to_string(lle_capability_level_t level) {
    switch (level) {
    case LLE_CAPABILITY_NONE:
        return "none";
    case LLE_CAPABILITY_BASIC:
        return "basic";
    case LLE_CAPABILITY_STANDARD:
        return "standard";
    case LLE_CAPABILITY_FULL:
        return "full";
    case LLE_CAPABILITY_PREMIUM:
        return "premium";
    default:
        return "unknown";
    }
}

/* ============================================================================
 * PUBLIC API - TERMINAL TYPE DETECTION HELPERS
 * ============================================================================
 */

/**
 * @brief Check if running in iTerm2 terminal
 * @param detection Detection result (can be NULL to check environment directly)
 * @return true if running in iTerm2, false otherwise
 */
bool lle_is_iterm2(const lle_terminal_detection_result_t *detection) {
    if (detection) {
        return (detection->term_program[0] != '\0' &&
                strstr(detection->term_program, "iTerm") != NULL);
    }

    // Check environment directly
    const char *term_program = getenv("TERM_PROGRAM");
    return (term_program && strstr(term_program, "iTerm") != NULL);
}

/**
 * @brief Check if running inside tmux multiplexer
 * @param detection Detection result (can be NULL to check environment directly)
 * @return true if running inside tmux, false otherwise
 */
bool lle_is_tmux(const lle_terminal_detection_result_t *detection) {
    if (detection) {
        return (detection->multiplexer_type == LLE_MUX_TYPE_TMUX);
    }

    // Check environment directly
    const char *tmux = getenv("TMUX");
    return (tmux != NULL && tmux[0] != '\0');
}

/**
 * @brief Check if running inside GNU screen multiplexer
 * @param detection Detection result (can be NULL to check environment directly)
 * @return true if running inside screen, false otherwise
 */
bool lle_is_screen(const lle_terminal_detection_result_t *detection) {
    if (detection) {
        return (detection->multiplexer_type == LLE_MUX_TYPE_SCREEN);
    }

    // Check environment directly
    const char *term = getenv("TERM");
    const char *sty = getenv("STY");

    return ((term && strstr(term, "screen") != NULL) ||
            (sty != NULL && sty[0] != '\0'));
}

/**
 * @brief Check if running inside any terminal multiplexer
 * @param detection Detection result (can be NULL to check environment directly)
 * @return true if running inside a multiplexer, false otherwise
 */
bool lle_is_multiplexed(const lle_terminal_detection_result_t *detection) {
    if (detection) {
        return (detection->multiplexer_type != LLE_MUX_TYPE_NONE);
    }

    // Check for common multiplexers
    return lle_is_tmux(NULL) || lle_is_screen(NULL);
}

/**
 * @brief Get terminal type string from TERM environment variable
 * @param detection Detection result (can be NULL to check environment directly)
 * @return Terminal type string or NULL if not set
 */
const char *
lle_get_terminal_type(const lle_terminal_detection_result_t *detection) {
    if (detection && detection->term_name[0] != '\0') {
        return detection->term_name;
    }

    return getenv("TERM");
}

/**
 * @brief Get current terminal dimensions
 * @param cols Pointer to store column count (can be NULL)
 * @param rows Pointer to store row count (can be NULL)
 * @return LLE_SUCCESS on success
 */
lle_result_t lle_get_terminal_size(int *cols, int *rows) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (cols) {
            *cols = ws.ws_col > 0 ? ws.ws_col : 80;
        }
        if (rows) {
            *rows = ws.ws_row > 0 ? ws.ws_row : 24;
        }
        return LLE_SUCCESS;
    }

    // Fallback to environment variables
    const char *columns = getenv("COLUMNS");
    const char *lines = getenv("LINES");

    if (cols) {
        *cols = columns ? atoi(columns) : 80;
        if (*cols <= 0) {
            *cols = 80;
        }
    }
    if (rows) {
        *rows = lines ? atoi(lines) : 24;
        if (*rows <= 0) {
            *rows = 24;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Check if stdout is a TTY
 * @return true if stdout is a TTY, false otherwise
 */
bool lle_is_tty(void) { return isatty(STDOUT_FILENO) != 0; }

/**
 * @brief Reset terminal to clean state
 *
 * Resets all attributes, shows cursor, and outputs a newline.
 * Safe to call even if stdout is not a TTY.
 */
void lle_terminal_reset(void) {
    if (!isatty(STDOUT_FILENO)) {
        return;
    }

    /// Reset all attributes, show cursor, move to new line.
    /// Best-effort terminal cleanup; nothing useful to do on failure.
    static const char reset_seq[] = "\x1b[0m\x1b[?25h\n";
    (void)!write(STDOUT_FILENO, reset_seq, sizeof(reset_seq) - 1);
}

/**
 * @brief Clean up terminal detection cache
 *
 * Frees the cached terminal detection result. Should be called
 * during shell shutdown to ensure proper cleanup.
 */
void lle_terminal_detection_cache_cleanup(void) {
    if (cached_result) {
        free(cached_result);
        cached_result = NULL;
    }
    cache_timestamp_us = 0;
}
