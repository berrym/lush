/**
 * @file debug_view.c
 * @brief User-facing debugger output rendered through the screen buffer
 *
 * The debugger's interactive output -- breakpoint banners, variable
 * inspection tables, stack traces -- renders through this module so it
 * has a single, coherent visual identity instead of inline ad-hoc
 * formatting.
 *
 * Architectural contract: LLE is a *client* of lush's layered display
 * system. The screen buffer (src/display/screen_buffer.c) is the
 * authority on per-line UTF-8 width, wrap, ANSI handling, and per-line
 * left-prefix attachment. Debugger output flows through it exactly as
 * the REPL's command-and-prompt rendering does, so the same width and
 * wrap guarantees apply to debug lines as to interactive prompts.
 *
 * Two output shapes:
 *
 *   - Streaming lines: a thin left gutter ("|" / U+2502) marks each
 *     debug line so it stands apart from script output on a shared
 *     terminal. Used for breakpoint banners, step indicators, ad-hoc
 *     notes.
 *
 *   - Framed blocks: a "begin_frame(title) ... end_frame()" pair
 *     opens and closes a bordered region with the same gutter on every
 *     interior line. Used for variable inspection ("Local Variables",
 *     "Arrays", per-variable type+value tables). The frame opens and
 *     closes *inside* the gutter -- the gutter is unbroken on the
 *     left, and the frame corners sit just to its right.
 *
 * Glyphs degrade to ASCII when the terminal does not advertise UTF-8.
 * The choice is made once on first emit and cached -- the answer is a
 * one-time property of the process environment.
 *
 * Output goes to ctx->debug_output (stderr by default; redirectable
 * via `debug log <file>`), the same channel debug_printf uses. The
 * debugger UI fires mid-execution and is not part of the REPL render
 * cycle, but it still uses the screen buffer for the gutter prefix +
 * width-aware rendering. display_controller is the sole stdout writer
 * for the REPL; the debug channel is independent and may be stderr or
 * a redirected file, so this module emits its rendered output directly
 * to ctx->debug_output rather than through display_controller.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "display/screen_buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ============================================================================
 * Glyph set selection
 * ============================================================================
 */

typedef struct {
    const char *gutter;          ///< per-line left marker, e.g. "| "
    const char *frame_corner_tl; ///< top-left, e.g. "+-"
    const char *frame_corner_bl; ///< bottom-left, e.g. "+-"
    const char *frame_horiz;     ///< horizontal fill, "-"
    const char *frame_open;      ///< between title and trailing fill, " "
} debug_view_glyphs_t;

static const debug_view_glyphs_t glyphs_utf8 = {
    .gutter = "\xe2\x94\x82 ", /// U+2502 BOX DRAWINGS LIGHT VERTICAL + space
    .frame_corner_tl = "\xe2\x94\x8c\xe2\x94\x80", /// U+250C U+2500
    .frame_corner_bl = "\xe2\x94\x94\xe2\x94\x80", /// U+2514 U+2500
    .frame_horiz = "\xe2\x94\x80",                 /// U+2500
    .frame_open = " ",
};

static const debug_view_glyphs_t glyphs_ascii = {
    .gutter = "| ",
    .frame_corner_tl = "+-",
    .frame_corner_bl = "+-",
    .frame_horiz = "-",
    .frame_open = " ",
};

/// Lazy-resolved once per process: the locale + TERM at start determine
/// which glyph set the debugger uses for the rest of the run.
static const debug_view_glyphs_t *g_glyphs = NULL;

/**
 * @brief Does the locale string advertise a UTF-8 encoding?
 *
 * Recognizes "UTF-8", "utf-8", "UTF8", "utf8" as substrings -- the
 * spellings POSIX and shell tooling actually use.
 *
 * @param locale LANG / LC_CTYPE / LC_ALL value (may be NULL/empty).
 * @return true if the string indicates a UTF-8 locale.
 */
static bool env_indicates_utf8_locale(const char *locale) {
    if (!locale || !*locale) {
        return false;
    }
    return strstr(locale, "UTF-8") != NULL || strstr(locale, "utf-8") != NULL ||
           strstr(locale, "UTF8") != NULL || strstr(locale, "utf8") != NULL;
}

/**
 * @brief Pick the right glyph set for the active terminal
 *
 * Matches LLE's terminal_capabilities heuristic: assume UTF-8 unless
 * the locale doesn't advertise it or TERM is the linux framebuffer
 * console (which has limited UTF-8 support).
 *
 * @return Pointer to one of the static glyph_sets (UTF-8 or ASCII).
 */
static const debug_view_glyphs_t *resolve_glyphs(void) {
    /// The linux framebuffer console renders box-drawing characters
    /// unreliably; match LLE's terminal_capabilities.c which treats it
    /// as the one common terminal lacking solid Unicode support.
    const char *term = getenv("TERM");
    if (term && strcmp(term, "linux") == 0) {
        return &glyphs_ascii;
    }

    /// If the locale does not advertise UTF-8, fall back to ASCII --
    /// the gutter and frame glyphs are multi-byte UTF-8 and a non-UTF-8
    /// locale risks misrendering.
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) {
        locale = getenv("LC_CTYPE");
    }
    if (!locale || !*locale) {
        locale = getenv("LANG");
    }
    if (!env_indicates_utf8_locale(locale)) {
        return &glyphs_ascii;
    }

    return &glyphs_utf8;
}

/**
 * @brief Return the cached glyph set, resolving on first use.
 *
 * @return The active glyph set for this process.
 */
static const debug_view_glyphs_t *view_glyphs(void) {
    if (!g_glyphs) {
        g_glyphs = resolve_glyphs();
    }
    return g_glyphs;
}

/// Inner width of a frame, in display columns. Fixed to 76 -- wide
/// enough to comfortably hold variable-state tables, narrow enough to
/// fit on a standard 80-column terminal alongside the gutter.
#define DEBUG_VIEW_FRAME_WIDTH 76

/* ============================================================================
 * Screen-buffer-backed emission
 * ============================================================================
 */

/**
 * @brief Detect the current terminal width
 *
 * Probes the controlling tty via TIOCGWINSZ. Falls back to 80 when the
 * channel is not a terminal (the debug-log redirect case) or the
 * ioctl is unavailable.
 *
 * @return Terminal width in columns; 80 on any failure.
 */
static int detect_terminal_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    return 80;
}

/**
 * @brief Render a multi-line text block through the screen buffer
 *
 * Emits the block to ctx->debug_output with a uniform left gutter on
 * every line. Routes through screen_buffer's add_text_rows +
 * set_line_prefix + render_multiline_with_prefixes pipeline so the
 * same width/wrap/ANSI handling that the REPL prompt enjoys applies
 * to debug output too.
 *
 * @param ctx        Debug context (must have debug_output set).
 * @param block_text Newline-separated lines, no trailing newline
 *                   required. Each line becomes one screen-buffer row.
 */
static void emit_block_with_gutter(debug_context_t *ctx,
                                   const char *block_text) {
    if (!ctx || !ctx->enabled || !ctx->debug_output || !block_text) {
        return;
    }

    screen_buffer_t *sb = calloc(1, sizeof(screen_buffer_t));
    if (!sb) {
        /// Fall back to a direct write so a malloc failure doesn't
        /// swallow the diagnostic the user is waiting for.
        fputs(view_glyphs()->gutter, ctx->debug_output);
        fputs(block_text, ctx->debug_output);
        fputc('\n', ctx->debug_output);
        fflush(ctx->debug_output);
        return;
    }

    screen_buffer_init(sb, detect_terminal_width());

    int rows_added = screen_buffer_add_text_rows(sb, 0, block_text);
    if (rows_added <= 0) {
        /// add_text_rows declined the input (overflow, etc.). Same
        /// pragmatic fall-through as the malloc-failed branch above.
        fputs(view_glyphs()->gutter, ctx->debug_output);
        fputs(block_text, ctx->debug_output);
        fputc('\n', ctx->debug_output);
        fflush(ctx->debug_output);
        screen_buffer_cleanup(sb);
        free(sb);
        return;
    }

    const char *gutter = view_glyphs()->gutter;
    for (int i = 0; i < rows_added; i++) {
        screen_buffer_set_line_prefix(sb, i, gutter);
    }

    /// SCREEN_BUFFER_MAX_ROWS * (gutter + MAX_COLS * 4 UTF-8 bytes).
    /// Heap-alloc to stay off the stack; 100*512*4 + slack ~= 220KB.
    size_t out_cap =
        (size_t)rows_added * (size_t)(SCREEN_BUFFER_MAX_COLS * 4 + 32);
    char *out = malloc(out_cap);
    if (out && screen_buffer_render_multiline_with_prefixes(sb, 0, rows_added,
                                                            out, out_cap)) {
        fputs(out, ctx->debug_output);
        fputc('\n', ctx->debug_output);
        fflush(ctx->debug_output);
    } else {
        /// Render declined; emit raw with gutter so the message lands.
        for (int i = 0; i < rows_added; i++) {
            fputs(gutter, ctx->debug_output);
        }
        fputs(block_text, ctx->debug_output);
        fputc('\n', ctx->debug_output);
        fflush(ctx->debug_output);
    }

    free(out);
    screen_buffer_cleanup(sb);
    free(sb);
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

/**
 * @brief Emit one user-facing debugger line with a left gutter
 *
 * Implementation of the public API declared in debug.h. Formats the
 * printf-style input into a heap buffer, then renders it through the
 * screen buffer with the gutter as the per-line prefix.
 *
 * @param ctx Debug context (must have debug_output set).
 * @param format printf-style format string.
 * @param ... Format arguments.
 */
void debug_view_emit_line(debug_context_t *ctx, const char *format, ...) {
    if (!ctx || !ctx->enabled || !ctx->debug_output || !format) {
        return;
    }

    va_list args;
    va_start(args, format);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    if (needed < 0) {
        va_end(args);
        return;
    }

    size_t cap = (size_t)needed + 1;
    char *line = malloc(cap);
    if (!line) {
        va_end(args);
        return;
    }
    vsnprintf(line, cap, format, args);
    va_end(args);

    /// Strip a trailing newline if the caller embedded one -- the
    /// screen-buffer emit path adds its own.
    if (needed > 0 && line[needed - 1] == '\n') {
        line[needed - 1] = '\0';
    }

    emit_block_with_gutter(ctx, line);
    free(line);
}

/**
 * @brief Build the top-border row "<corner>- [title] <fill>..."
 *
 * The line is emitted through emit_block_with_gutter so the gutter
 * sits to its left in the rendered output; this helper produces the
 * frame content itself (no gutter included).
 *
 * @param dest Output buffer.
 * @param dest_size Output buffer capacity.
 * @param title Title to embed; may be NULL/"".
 */
static void format_top_border(char *dest, size_t dest_size, const char *title) {
    const debug_view_glyphs_t *g = view_glyphs();
    size_t pos = 0;

    pos +=
        (size_t)snprintf(dest + pos, dest_size - pos, "%s", g->frame_corner_tl);
    int fill = DEBUG_VIEW_FRAME_WIDTH;

    if (title && *title) {
        pos += (size_t)snprintf(dest + pos, dest_size - pos, "%s[%s] ",
                                g->frame_open, title);
        size_t title_visible = strlen(title);
        fill -= (int)title_visible + 4;
        if (fill < 4) {
            fill = 4;
        }
    } else {
        pos +=
            (size_t)snprintf(dest + pos, dest_size - pos, "%s", g->frame_horiz);
        fill -= 1;
    }

    for (int i = 0; i < fill && pos + 4 < dest_size; i++) {
        pos +=
            (size_t)snprintf(dest + pos, dest_size - pos, "%s", g->frame_horiz);
    }
    if (pos < dest_size) {
        dest[pos] = '\0';
    } else {
        dest[dest_size - 1] = '\0';
    }
}

/**
 * @brief Open a framed block with an optional bracketed title
 *
 * The top-border row is built into a buffer and emitted through the
 * screen-buffer path so the same gutter sits to its left as on every
 * interior line. Pair with debug_view_end_frame.
 *
 * @param ctx Debug context.
 * @param title Title to embed, or NULL/"" for an unlabeled frame.
 */
void debug_view_begin_frame(debug_context_t *ctx, const char *title) {
    if (!ctx || !ctx->enabled || !ctx->debug_output) {
        return;
    }

    /// The frame border itself is bounded by DEBUG_VIEW_FRAME_WIDTH
    /// display columns; with UTF-8 box characters at up to 3 bytes
    /// each plus title and slack, 512 bytes is comfortable.
    char border[512];
    format_top_border(border, sizeof(border), title);
    emit_block_with_gutter(ctx, border);
}

/**
 * @brief Close a framed block opened with debug_view_begin_frame
 *
 * @param ctx Debug context.
 */
void debug_view_end_frame(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled || !ctx->debug_output) {
        return;
    }

    const debug_view_glyphs_t *g = view_glyphs();
    char border[512];
    size_t pos = 0;

    pos += (size_t)snprintf(border + pos, sizeof(border) - pos, "%s",
                            g->frame_corner_bl);
    for (int i = 0; i < DEBUG_VIEW_FRAME_WIDTH - 1 && pos + 4 < sizeof(border);
         i++) {
        pos += (size_t)snprintf(border + pos, sizeof(border) - pos, "%s",
                                g->frame_horiz);
    }
    if (pos < sizeof(border)) {
        border[pos] = '\0';
    } else {
        border[sizeof(border) - 1] = '\0';
    }

    emit_block_with_gutter(ctx, border);
}

/**
 * @brief Test seam: clear the cached glyph set
 *
 * The next call to view_glyphs() will re-resolve from the current
 * environment. Not part of the public debug.h API; used by
 * test_debug_trace's view tests so each case starts from a known
 * state.
 */
void debug_view_reset_glyph_cache(void) { g_glyphs = NULL; }

/**
 * @brief Test seam: force the ASCII glyph set regardless of env
 */
void debug_view_force_ascii_for_tests(void) { g_glyphs = &glyphs_ascii; }

/**
 * @brief Test seam: force the UTF-8 glyph set regardless of env
 */
void debug_view_force_utf8_for_tests(void) { g_glyphs = &glyphs_utf8; }
