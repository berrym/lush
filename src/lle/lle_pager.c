/**
 * @file lle_pager.c
 * @brief LLE pager public API implementation
 *
 * Top-level integration that wires the four pieces shipped in earlier
 * work (screen_line_index, pager_layer, display_controller pager
 * branch, pager_input) into a single consumer-facing entry point.
 *
 * See include/lle/lle_pager.h for the contract and the decision
 * tree, and docs/development/LLE_PAGER_DESIGN.md section 5 for the
 * architectural rationale.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/lle_pager.h"

#include "config.h"
#include "display/display_controller.h"
#include "display/pager_input.h"
#include "display/pager_layer.h"
#include "display_integration.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define PAGER_FRAME_BUFFER_SIZE 16384
#define PAGER_ESC_PEEK_USEC 50000 /// 50 ms

/// ============================================================================
/// I/O helpers
/// ============================================================================

/**
 * @brief Write all of `data` to fd, retrying on short writes / EINTR
 *
 * @return 0 on full write, -1 on hard error
 */
static int write_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        data += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/**
 * @brief Direct-write fallback used when pagination is not applicable
 *
 * Used for every non-paginated branch (non-tty, no display controller,
 * terminal size unavailable, content fits in one screen).
 */
static int direct_write(const char *content) {
    if (!content) {
        return 0;
    }
    size_t len = strlen(content);
    if (len == 0) {
        return 0;
    }
    if (write_all(STDOUT_FILENO, content, len) != 0) {
        return -1;
    }
    if (content[len - 1] != '\n') {
        if (write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
    }
    return 0;
}

/// ============================================================================
/// Frame rendering
/// ============================================================================

/**
 * @brief Render the current pager view to stdout via the display controller
 *
 * Uses display_controller_display_with_cursor with apply_terminal_control=true
 * so the controller's pager branch (active when attach_pager was called)
 * emits `\r\033[J` + the pager view body. The buffer is heap-free; the
 * 16 KiB stack frame is well under the 24-row * 80-col upper bound.
 */
static void render_frame(display_controller_t *dc) {
    char frame[PAGER_FRAME_BUFFER_SIZE];
    display_controller_error_t rc = display_controller_display_with_cursor(
        dc, "", "", 0, true, frame, sizeof(frame));
    if (rc != DISPLAY_CONTROLLER_SUCCESS) {
        return;
    }
    size_t len = strnlen(frame, sizeof(frame));
    (void)write_all(STDOUT_FILENO, frame, len);
}

/// ============================================================================
/// Key reading -- raw-mode byte stream + minimal CSI escape parsing
/// ============================================================================

/**
 * @brief Wait up to `usec` microseconds for data on STDIN
 *
 * Used to distinguish a bare Esc keypress from the lead byte of a
 * CSI escape sequence. Bare Esc arrives alone; CSI sequences arrive
 * as a burst that the terminal driver delivers within microseconds.
 *
 * @return 1 if data is ready, 0 on timeout, -1 on error
 */
static int stdin_has_data(int usec) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = usec};
    int r;
    do {
        r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    } while (r < 0 && errno == EINTR);
    return r > 0 ? 1 : 0;
}

/**
 * @brief Read one key from STDIN; translate CSI sequences to PAGER_KEY_*
 *
 * Returns:
 *   - ASCII byte 0..127 for regular keys
 *   - 27 (Esc) for a bare Esc (no following bytes within the peek window)
 *   - PAGER_KEY_UP / DOWN / LEFT / RIGHT / HOME / END / PAGE_UP / PAGE_DOWN
 *     for arrows / navigation
 *   - 0 for an unrecognized escape sequence (key source contract: any
 *     non-negative value is a key; the dispatch table treats 0 as
 *     unbound, which becomes PAGER_ACTION_NONE)
 *   - -1 for EOF / read error, which terminates the input loop
 */
static int read_pager_key(void *userdata) {
    display_controller_t *dc = (display_controller_t *)userdata;
    render_frame(dc);

    unsigned char ch;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &ch, 1);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) {
        return -1;
    }
    if (ch != 0x1b) {
        return (int)ch;
    }

    /// Esc lead byte: peek for a CSI continuation
    if (stdin_has_data(PAGER_ESC_PEEK_USEC) <= 0) {
        return 27;
    }

    unsigned char seq[8];
    ssize_t sn;
    do {
        sn = read(STDIN_FILENO, seq, sizeof(seq));
    } while (sn < 0 && errno == EINTR);
    if (sn <= 0) {
        return 27;
    }

    /// Only handle CSI (Esc [ ...); SS3 (Esc O ...) and other intro
    /// bytes are uncommon for the keys the pager cares about. Anything
    /// else collapses to "unbound key" so dispatch returns
    /// PAGER_ACTION_NONE rather than misfiring.
    if (seq[0] != '[') {
        return 27;
    }
    if (sn == 2) {
        switch (seq[1]) {
        case 'A':
            return PAGER_KEY_UP;
        case 'B':
            return PAGER_KEY_DOWN;
        case 'C':
            return PAGER_KEY_RIGHT;
        case 'D':
            return PAGER_KEY_LEFT;
        case 'H':
            return PAGER_KEY_HOME;
        case 'F':
            return PAGER_KEY_END;
        default:
            return 0;
        }
    }
    if (sn == 3 && seq[2] == '~') {
        switch (seq[1]) {
        case '1':
            return PAGER_KEY_HOME;
        case '4':
            return PAGER_KEY_END;
        case '5':
            return PAGER_KEY_PAGE_UP;
        case '6':
            return PAGER_KEY_PAGE_DOWN;
        default:
            return 0;
        }
    }
    return 0;
}

/// ============================================================================
/// Terminal raw-mode brackets
/// ============================================================================

/**
 * @brief Move STDIN into raw mode (no canonical, no echo)
 *
 * The caller already saved the prior termios in *saved. Returns 0 on
 * success, -1 on tcsetattr failure (in which case the caller should
 * fall back to a direct write).
 */
static int enter_raw_mode(struct termios *saved) {
    if (tcgetattr(STDIN_FILENO, saved) != 0) {
        return -1;
    }
    struct termios raw = *saved;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }
    return 0;
}

static void restore_termios(const struct termios *saved) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, saved);
}

/// ============================================================================
/// Public entry point
/// ============================================================================

int lle_pager_present(struct executor *executor, const char *content) {
    (void)executor; /// the central config registry is process-global

    if (!content || *content == '\0') {
        return 0;
    }

    /// Master switch: when the user has set display.lle.pager.enabled
    /// to false, every call short-circuits to a direct write -- the
    /// pager view is never activated regardless of content length.
    /// This is the escape hatch for users (or test harnesses) that
    /// want builtin output to stream through unchanged.
    if (!config.display_lle_pager_enabled) {
        return direct_write(content);
    }

    /// Non-tty fallback: stdout is a pipe, file, or otherwise not an
    /// interactive terminal. Pagination would be meaningless and
    /// hostile to consumers (less, grep, redirect-to-file, etc.).
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
        return direct_write(content);
    }

    display_controller_t *dc = display_integration_get_controller();
    if (!dc) {
        return direct_write(content);
    }

    /// Terminal dimensions via TIOCGWINSZ. base_terminal exposes the
    /// same data via an accessor, but ioctl on STDOUT is the
    /// authoritative source and avoids depending on the base_terminal
    /// having been refreshed since the most recent SIGWINCH.
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col <= 0 ||
        ws.ws_row <= 0) {
        return direct_write(content);
    }
    int term_width = (int)ws.ws_col;
    int term_rows = (int)ws.ws_row;

    /// The pagination threshold is the smaller of:
    ///   - terminal_rows - 1 (always leaves room for a status line)
    ///   - display.lle.pager.min_lines, when set to a positive value
    /// Content whose visual height does not exceed this threshold is
    /// printed directly. min_lines=0 (the default) means "use the
    /// natural terminal-rows threshold"; non-zero values let users
    /// force pagination earlier (e.g. min_lines=10 paginates anything
    /// over 10 rows even in a tall terminal).
    size_t view_rows = (term_rows > 1) ? (size_t)(term_rows - 1) : 1;
    if (config.display_lle_pager_min_lines > 0 &&
        (size_t)config.display_lle_pager_min_lines < view_rows) {
        view_rows = (size_t)config.display_lle_pager_min_lines;
    }

    pager_layer_t pager;
    size_t content_len = strlen(content);
    if (pager_layer_init(&pager, content, content_len, view_rows, term_width) !=
        0) {
        return direct_write(content);
    }

    /// Thread the central-config wrap_search value into the pager
    /// layer's runtime state. pager_layer_init defaults the field
    /// to true; the explicit assignment lets the user flip it via
    /// `display lle pager` or the lushrc.toml key without changing
    /// the layer's default for callers that bypass this entry
    /// point.
    pager.wrap_searches = config.display_lle_pager_wrap_search;

    /// Fits-in-viewport fallback. total_visual_rows accounts for
    /// wrapped lines, so a single long unwrapped line that exceeds
    /// the view width correctly forces pagination.
    if (pager.line_index.total_visual_rows <= view_rows) {
        pager_layer_destroy(&pager);
        return direct_write(content);
    }

    struct termios saved_termios;
    if (enter_raw_mode(&saved_termios) != 0) {
        pager_layer_destroy(&pager);
        return direct_write(content);
    }

    display_controller_attach_pager(dc, &pager);

    /// pager_run_input_loop sets / clears lle_in_pager() around the
    /// loop body. Each key-source call renders before reading, so
    /// the very first frame appears the moment we block on the
    /// first read().
    pager_run_input_loop(&pager, read_pager_key, dc);

    display_controller_detach_pager(dc);
    restore_termios(&saved_termios);

    /// Clear the pager view from the screen so the caller's next
    /// write (next prompt, next builtin output) lands on a clean
    /// surface. The bare \r\033[J pair is the same idiom the normal
    /// render cycle uses to clear stale wrapped content.
    (void)write_all(STDOUT_FILENO, "\r\033[J", 4);

    pager_layer_destroy(&pager);
    return 0;
}
