/**
 * @file debug_view.c
 * @brief User-facing debugger output: gutters, frames, terminal-aware glyphs
 *
 * The debugger's interactive output -- breakpoint banners, variable
 * inspection tables, stack traces -- renders through this module so it
 * has a single, coherent visual identity instead of inline ad-hoc
 * formatting.
 *
 * Two output shapes:
 *
 *   - Streaming lines: a thin left gutter ("|" or U+2502) marks each
 *     debug line so it stands apart from script output on a shared
 *     terminal. Used for breakpoint banners, step indicators, ad-hoc
 *     notes.
 *
 *   - Framed blocks: a "begin_frame(title) ... end_frame()" pair
 *     opens and closes a bordered region with the same gutter on every
 *     interior line. Used for variable inspection ("Local Variables",
 *     "Arrays", per-variable type+value tables).
 *
 * Glyphs degrade to ASCII when the terminal does not advertise UTF-8.
 * The choice is made once on first emit and cached -- the answer is a
 * one-time property of the process environment.
 *
 * Output goes to ctx->debug_output (stderr by default; redirectable
 * via `debug log <file>`), the same channel debug_printf uses. The
 * debugger UI is not part of the REPL display_controller render cycle
 * -- it fires mid-execution and is stream-oriented -- so no
 * display_controller plumbing is required.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Glyph set selection
 * ============================================================================
 */

typedef struct {
    const char *gutter;       /* per-line left marker, e.g. "│ " or "| " */
    const char *frame_corner_tl; /* top-left, e.g. "┌─" or "+-"           */
    const char *frame_corner_bl; /* bottom-left, e.g. "└─" or "+-"        */
    const char *frame_horiz;     /* horizontal fill, "─" or "-"            */
    const char *frame_open;      /* between title and trailing fill, " "   */
} debug_view_glyphs_t;

static const debug_view_glyphs_t glyphs_utf8 = {
    .gutter = "│ ",
    .frame_corner_tl = "┌─",
    .frame_corner_bl = "└─",
    .frame_horiz = "─",
    .frame_open = " ",
};

static const debug_view_glyphs_t glyphs_ascii = {
    .gutter = "| ",
    .frame_corner_tl = "+-",
    .frame_corner_bl = "+-",
    .frame_horiz = "-",
    .frame_open = " ",
};

/* Lazy-resolve once: the locale + TERM at process start determine
 * which set the debugger uses for the rest of the run. */
static const debug_view_glyphs_t *g_glyphs = NULL;

static bool env_indicates_utf8_locale(const char *locale) {
    if (!locale || !*locale) {
        return false;
    }
    return strstr(locale, "UTF-8") != NULL || strstr(locale, "utf-8") != NULL ||
           strstr(locale, "UTF8") != NULL || strstr(locale, "utf8") != NULL;
}

static const debug_view_glyphs_t *resolve_glyphs(void) {
    /* The linux framebuffer console renders box-drawing characters
     * unreliably; match LLE's terminal_capabilities.c which treats it
     * as the one common terminal lacking solid Unicode support. */
    const char *term = getenv("TERM");
    if (term && strcmp(term, "linux") == 0) {
        return &glyphs_ascii;
    }

    /* If the locale does not advertise UTF-8, fall back to ASCII --
     * the gutter and frame glyphs are multi-byte UTF-8 and a non-UTF-8
     * locale risks misrendering. */
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

static const debug_view_glyphs_t *view_glyphs(void) {
    if (!g_glyphs) {
        g_glyphs = resolve_glyphs();
    }
    return g_glyphs;
}

/* Inner width of a frame, in display columns. Fixed to 76 -- wide
 * enough to comfortably hold variable-state tables, narrow enough to
 * fit on a standard 80-column terminal alongside the gutter. */
#define DEBUG_VIEW_FRAME_WIDTH 76

/* ============================================================================
 * Output primitives
 * ============================================================================
 */

void debug_view_emit_line(debug_context_t *ctx, const char *format, ...) {
    if (!ctx || !ctx->enabled || !ctx->debug_output || !format) {
        return;
    }

    fputs(view_glyphs()->gutter, ctx->debug_output);

    va_list args;
    va_start(args, format);
    vfprintf(ctx->debug_output, format, args);
    va_end(args);

    /* Ensure each line is terminated. Callers passing a newline-
     * terminated format string will produce a single newline; callers
     * omitting one still get a well-formed line. */
    fputc('\n', ctx->debug_output);
    fflush(ctx->debug_output);
}

void debug_view_begin_frame(debug_context_t *ctx, const char *title) {
    if (!ctx || !ctx->enabled || !ctx->debug_output) {
        return;
    }

    const debug_view_glyphs_t *g = view_glyphs();
    fputs(g->frame_corner_tl, ctx->debug_output);

    /* When a title is supplied, frame it: "<corner>- [title] <fill>". */
    int fill = DEBUG_VIEW_FRAME_WIDTH;
    if (title && *title) {
        size_t title_visible = strlen(title); /* ASCII title assumed */
        /* "<corner>- [" + title + "] " consumes 4 + title chars of fill. */
        fputs(g->frame_open, ctx->debug_output);
        fputc('[', ctx->debug_output);
        fputs(title, ctx->debug_output);
        fputs("] ", ctx->debug_output);
        fill -= (int)title_visible + 4;
        if (fill < 4) {
            fill = 4;
        }
    } else {
        fputs(g->frame_horiz, ctx->debug_output);
        fill -= 1;
    }

    for (int i = 0; i < fill; i++) {
        fputs(g->frame_horiz, ctx->debug_output);
    }
    fputc('\n', ctx->debug_output);
    fflush(ctx->debug_output);
}

void debug_view_end_frame(debug_context_t *ctx) {
    if (!ctx || !ctx->enabled || !ctx->debug_output) {
        return;
    }

    const debug_view_glyphs_t *g = view_glyphs();
    fputs(g->frame_corner_bl, ctx->debug_output);
    for (int i = 0; i < DEBUG_VIEW_FRAME_WIDTH - 1; i++) {
        fputs(g->frame_horiz, ctx->debug_output);
    }
    fputc('\n', ctx->debug_output);
    fflush(ctx->debug_output);
}

/* Used by tests to deterministically force a glyph set regardless of
 * the process environment. NOT part of the public debug.h API. */
void debug_view_reset_glyph_cache(void) { g_glyphs = NULL; }

void debug_view_force_ascii_for_tests(void) { g_glyphs = &glyphs_ascii; }

void debug_view_force_utf8_for_tests(void) { g_glyphs = &glyphs_utf8; }
