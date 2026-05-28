/**
 * @file terminal_capabilities.c
 * @brief Terminal Capability Detection (Spec 02 Phase 1)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Detects terminal type, features, and capabilities at initialization time
 * through environment variables, terminfo queries, and terminal type matching.
 *
 * Critical Principle: ONE-TIME DETECTION at startup. NO runtime terminal
 * queries.
 *
 * Spec 02: Terminal Abstraction - Phase 1
 */

#include "lle/terminal_abstraction.h"
#include <curses.h> /// ncurses (provides OK constant and setupterm)
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <term.h> /// terminfo functions
#include <unistd.h>

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Detect terminal type from TERM environment variable
 *
 * @param term_env TERM environment variable value
 * @return Detected terminal type enum
 */
static lle_terminal_type_t detect_terminal_type(const char *term_env) {
    if (!term_env) {
        return LLE_TERMINAL_GENERIC;
    }

    /// Check for specific terminal types (order matters)

    /// Modern terminals first
    if (strstr(term_env, "alacritty"))
        return LLE_TERMINAL_ALACRITTY;
    if (strstr(term_env, "kitty"))
        return LLE_TERMINAL_KITTY;
    if (strstr(term_env, "iterm"))
        return LLE_TERMINAL_ITERM2;

    /// Terminal multiplexers
    if (strstr(term_env, "tmux"))
        return LLE_TERMINAL_TMUX;
    if (strstr(term_env, "screen"))
        return LLE_TERMINAL_SCREEN;

    /// Traditional terminals
    if (strstr(term_env, "xterm"))
        return LLE_TERMINAL_XTERM;
    if (strstr(term_env, "rxvt"))
        return LLE_TERMINAL_RXVT;
    if (strstr(term_env, "konsole"))
        return LLE_TERMINAL_KONSOLE;
    if (strstr(term_env, "gnome"))
        return LLE_TERMINAL_GNOME_TERMINAL;

    /// Console
    if (strstr(term_env, "linux"))
        return LLE_TERMINAL_LINUX_CONSOLE;

    /// macOS
    if (strstr(term_env, "nsterm"))
        return LLE_TERMINAL_DARWIN_TERMINAL;

    return LLE_TERMINAL_GENERIC;
}

/**
 * @brief Detect if running in a TTY
 *
 * @return true if stdin and stdout are TTYs, false otherwise
 */
static bool detect_is_tty(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

/**
 * @brief Get terminal program name from environment
 *
 * @return Terminal program name or "unknown"
 */
static const char *detect_terminal_program(void) {
    /// Check common environment variables
    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program)
        return term_program;

    const char *colorterm = getenv("COLORTERM");
    if (colorterm)
        return colorterm;

    return "unknown";
}

/**
 * @brief Query terminfo for boolean capability
 *
 * @param capability_name Terminfo capability name
 * @return true if capability is supported, false otherwise
 */
static bool query_terminfo_flag(const char *capability_name) {
    int result = tigetflag((char *)capability_name);
    return (result == 1);
}

/**
 * @brief Query terminfo for numeric capability
 *
 * @param capability_name Terminfo capability name
 * @return Capability value or 0 if not available
 */
static int query_terminfo_num(const char *capability_name) {
    int result = tigetnum((char *)capability_name);
    return (result >= 0) ? result : 0;
}

/**
 * @brief Detect color capabilities from terminfo and environment
 *
 * @param caps Capabilities structure to populate
 */
static void detect_color_capabilities(lle_terminal_capabilities_t *caps) {
    /// Initialize terminfo
    int err;
    if (setupterm(NULL, STDOUT_FILENO, &err) != OK) {
        /// Terminfo unavailable - use conservative defaults
        caps->supports_ansi_colors = false;
        caps->supports_256_colors = false;
        caps->supports_truecolor = false;
        caps->detected_color_depth = 0;
        return;
    }

    /// Query color capability from terminfo
    int colors = query_terminfo_num("colors");

    if (colors >= 256) {
        caps->supports_ansi_colors = true;
        caps->supports_256_colors = true;
        caps->detected_color_depth = 8; /// 8-bit color
    } else if (colors >= 8) {
        caps->supports_ansi_colors = true;
        caps->supports_256_colors = false;
        caps->detected_color_depth = 4; /// 4-bit color (16 colors)
    } else {
        caps->supports_ansi_colors = false;
        caps->supports_256_colors = false;
        caps->detected_color_depth = 0;
    }

    /// Check for truecolor via environment (not in standard terminfo)
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 ||
                      strcmp(colorterm, "24bit") == 0)) {
        caps->supports_truecolor = true;
        caps->detected_color_depth = 24; /// 24-bit truecolor
    } else {
        caps->supports_truecolor = false;
    }

    /// Some modern terminals support truecolor even without COLORTERM
    if (!caps->supports_truecolor) {
        if (caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
            caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
            caps->terminal_type_enum == LLE_TERMINAL_ITERM2) {
            caps->supports_truecolor = true;
            caps->detected_color_depth = 24;
        }
    }
}

/**
 * @brief Detect text attributes from terminfo
 *
 * @param caps Capabilities structure to populate
 */
static void detect_text_attributes(lle_terminal_capabilities_t *caps) {
    /// Query terminfo for text attributes
    caps->supports_bold = query_terminfo_flag("bold");
    caps->supports_italic = query_terminfo_flag("sitm"); /// enter italic mode
    caps->supports_underline =
        query_terminfo_flag("smul"); /// enter underline mode
    caps->supports_reverse = query_terminfo_flag("rev"); /// reverse video
    caps->supports_dim = query_terminfo_flag("dim");     /// dim/half-bright

    /// Strikethrough not in standard terminfo - check by terminal type
    caps->supports_strikethrough = false;
    if (caps->terminal_type_enum == LLE_TERMINAL_XTERM ||
        caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_ITERM2 ||
        caps->terminal_type_enum == LLE_TERMINAL_GNOME_TERMINAL) {
        caps->supports_strikethrough = true;
    }
}

/**
 * @brief Detect advanced terminal features
 *
 * @param caps Capabilities structure to populate
 */
static void detect_advanced_features(lle_terminal_capabilities_t *caps) {
    /// Mouse reporting - most modern terminals support it
    caps->supports_mouse_reporting = false;
    if (caps->terminal_type_enum == LLE_TERMINAL_XTERM ||
        caps->terminal_type_enum == LLE_TERMINAL_RXVT ||
        caps->terminal_type_enum == LLE_TERMINAL_GNOME_TERMINAL ||
        caps->terminal_type_enum == LLE_TERMINAL_ITERM2 ||
        caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KONSOLE) {
        caps->supports_mouse_reporting = true;
    }

    /// Bracketed paste mode
    caps->supports_bracketed_paste = false;
    if (caps->terminal_type_enum == LLE_TERMINAL_XTERM ||
        caps->terminal_type_enum == LLE_TERMINAL_RXVT ||
        caps->terminal_type_enum == LLE_TERMINAL_GNOME_TERMINAL ||
        caps->terminal_type_enum == LLE_TERMINAL_ITERM2 ||
        caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KONSOLE) {
        caps->supports_bracketed_paste = true;
    }

    /// Focus events (FocusIn/FocusOut escape sequences)
    caps->supports_focus_events = false;
    if (caps->terminal_type_enum == LLE_TERMINAL_XTERM ||
        caps->terminal_type_enum == LLE_TERMINAL_ITERM2 ||
        caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY) {
        caps->supports_focus_events = true;
    }

    /// Synchronized output (DEC mode 2026) - reduces flicker
    caps->supports_synchronized_output = false;
    if (caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY) {
        caps->supports_synchronized_output = true;
    }

    /// Unicode support - assume yes for all modern terminals
    caps->supports_unicode = true;
    if (caps->terminal_type_enum == LLE_TERMINAL_LINUX_CONSOLE) {
        /// Linux console has limited Unicode support
        caps->supports_unicode = false;
    }
}

/**
 * @brief Detect terminal window size using ioctl
 *
 * @param caps Capabilities structure to populate
 */
static void detect_terminal_geometry(lle_terminal_capabilities_t *caps) {
    struct winsize ws;

    /// Try ioctl first
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
        ws.ws_row > 0) {
        caps->terminal_width = ws.ws_col;
        caps->terminal_height = ws.ws_row;
    } else {
        /// Fallback to environment variables
        const char *env_cols = getenv("COLUMNS");
        const char *env_lines = getenv("LINES");

        caps->terminal_width = (env_cols && *env_cols)
                                   ? (size_t)atoi(env_cols)
                                   : 80; /// Default 80 columns
        caps->terminal_height = (env_lines && *env_lines)
                                    ? (size_t)atoi(env_lines)
                                    : 24; /// Default 24 lines
    }

    /// Sanity checks - ensure reasonable minimums
    if (caps->terminal_width < 20)
        caps->terminal_width = 80;
    if (caps->terminal_height < 5)
        caps->terminal_height = 24;
}

/**
 * @brief Estimate terminal latency and performance characteristics
 *
 * @param caps Capabilities structure to populate
 */
static void
detect_performance_characteristics(lle_terminal_capabilities_t *caps) {
    /// Estimated round-trip latency in milliseconds
    switch (caps->terminal_type_enum) {
    case LLE_TERMINAL_ALACRITTY:
    case LLE_TERMINAL_KITTY:
        /// GPU-accelerated terminals - very fast
        caps->estimated_latency_ms = 5;
        caps->supports_fast_updates = true;
        break;

    case LLE_TERMINAL_ITERM2:
    case LLE_TERMINAL_GNOME_TERMINAL:
        /// Modern terminals - fast
        caps->estimated_latency_ms = 10;
        caps->supports_fast_updates = true;
        break;

    case LLE_TERMINAL_XTERM:
    case LLE_TERMINAL_RXVT:
    case LLE_TERMINAL_KONSOLE:
        /// Traditional terminals - moderate
        caps->estimated_latency_ms = 15;
        caps->supports_fast_updates = true;
        break;

    case LLE_TERMINAL_SCREEN:
    case LLE_TERMINAL_TMUX:
        /// Terminal multiplexers - slower (additional layer)
        caps->estimated_latency_ms = 20;
        caps->supports_fast_updates = false;
        break;

    case LLE_TERMINAL_LINUX_CONSOLE:
        /// Console - slower
        caps->estimated_latency_ms = 30;
        caps->supports_fast_updates = false;
        break;

    default:
        /// Conservative default
        caps->estimated_latency_ms = 15;
        caps->supports_fast_updates = true;
        break;
    }
}

/**
 * @brief Set optimization flags based on terminal capabilities
 *
 * @param caps Capabilities structure to populate
 */
static void set_optimization_flags(lle_terminal_capabilities_t *caps) {
    caps->optimizations = LLE_OPT_NONE;

    /// Fast cursor positioning for GPU-accelerated terminals
    if (caps->terminal_type_enum == LLE_TERMINAL_ALACRITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_KITTY ||
        caps->terminal_type_enum == LLE_TERMINAL_ITERM2) {
        caps->optimizations |= LLE_OPT_FAST_CURSOR;
    }

    /// Batch updates for slower terminals and multiplexers
    if (caps->terminal_type_enum == LLE_TERMINAL_SCREEN ||
        caps->terminal_type_enum == LLE_TERMINAL_TMUX ||
        caps->terminal_type_enum == LLE_TERMINAL_LINUX_CONSOLE) {
        caps->optimizations |= LLE_OPT_BATCH_UPDATES;
    }

    /// Incremental drawing for fast terminals
    if (caps->supports_fast_updates) {
        caps->optimizations |= LLE_OPT_INCREMENTAL_DRAW;
    }

    /// Unicode awareness
    if (caps->supports_unicode) {
        caps->optimizations |= LLE_OPT_UNICODE_AWARE;
    }
}

/* ============================================================================
 * PUBLIC API FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Main capability detection entry point
 *
 * Detects all terminal capabilities at initialization time. This is called
 * ONCE at startup and the results are cached for the lifetime of the program.
 *
 * @param caps Output pointer for created capabilities structure
 * @param unix_iface Unix interface (may be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_capabilities_detect_environment(lle_terminal_capabilities_t **caps,
                                    lle_unix_interface_t *unix_iface) {
    if (!caps) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Note: unix_iface may be NULL - not required for capability detection
    (void)unix_iface; /// Unused in Phase 1

    /// Allocate capabilities structure
    lle_terminal_capabilities_t *c =
        calloc(1, sizeof(lle_terminal_capabilities_t));
    if (!c) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Already zeroed by calloc

    /// Detect TTY status
    c->is_tty = detect_is_tty();

    /// Get environment variables
    const char *term_env = getenv("TERM");
    c->terminal_type = term_env ? strdup(term_env) : strdup("unknown");
    if (!c->terminal_type) {
        lle_pool_free(c);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    const char *term_program = detect_terminal_program();
    c->terminal_program = strdup(term_program);
    if (!c->terminal_program) {
        free((char *)c->terminal_type);
        lle_pool_free(c);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Detect terminal type enum
    c->terminal_type_enum = detect_terminal_type(term_env);

    /// Detect all capabilities
    detect_color_capabilities(c);
    detect_text_attributes(c);
    detect_advanced_features(c);
    detect_terminal_geometry(c);
    detect_performance_characteristics(c);
    set_optimization_flags(c);

    *caps = c;
    return LLE_SUCCESS;
}

/**
 * @brief Cleanup capabilities structure
 *
 * @param caps Capabilities structure to destroy
 */
void lle_capabilities_destroy(lle_terminal_capabilities_t *caps) {
    if (!caps) {
        return;
    }

    /// Free string allocations
    if (caps->terminal_type) {
        free((char *)caps->terminal_type);
    }
    if (caps->terminal_program) {
        free((char *)caps->terminal_program);
    }

    /// Cleanup ncurses terminfo data allocated by setupterm()
    if (cur_term) {
        del_curterm(cur_term);
    }

    /// Free structure itself
    free(caps);
}

/**
 * @brief Update terminal geometry (called after SIGWINCH)
 *
 * This is the ONLY function that queries terminal state at runtime.
 * It's called in response to window resize signals.
 *
 * @param caps Capabilities structure to update
 * @param width New width (0 to auto-detect)
 * @param height New height (0 to auto-detect)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_capabilities_update_geometry(lle_terminal_capabilities_t *caps,
                                              size_t width, size_t height) {
    if (!caps) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If width/height provided, use them directly (from signal handler)
    if (width > 0 && height > 0) {
        caps->terminal_width = width;
        caps->terminal_height = height;
    } else {
        /// Otherwise, re-detect geometry via ioctl
        detect_terminal_geometry(caps);
    }

    /// Sanity checks
    if (caps->terminal_width < 20)
        caps->terminal_width = 80;
    if (caps->terminal_height < 5)
        caps->terminal_height = 24;

    return LLE_SUCCESS;
}
