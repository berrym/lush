/**
 * @file powerline_renderer.c
 * @brief LLE Powerline Renderer Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Renders prompt segments as colored blocks with powerline arrow
 * separators. Called from the composer as an alternative rendering
 * path when the active theme has style = powerline.
 */

#include "lle/prompt/powerline.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/theme.h"

#include <string.h>

/* ============================================================================
 * Internal Types
 * ============================================================================
 */

/** Rendered segment with resolved colors */
typedef struct {
    char content[LLE_SEGMENT_OUTPUT_MAX];
    size_t content_len;
    lle_color_t fg;
    lle_color_t bg;
} powerline_segment_t;

/* Maximum visible segments in a single render */
#define POWERLINE_MAX_VISIBLE 32

/* ============================================================================
 * Default Color Mapping
 * ============================================================================
 */

/**
 * @brief Get the default background color for a segment
 *
 * Used when the theme does not provide a per-segment bg_color.
 */
static lle_color_t default_segment_bg(const char *name) {
    /* True color backgrounds — palette indices get remapped by terminal
     * colorschemes and produce unpredictable results. */
    if (strcmp(name, "user") == 0)
        return lle_color_rgb(68, 68, 68);      /* #444444 dark gray */
    if (strcmp(name, "host") == 0)
        return lle_color_rgb(68, 68, 68);      /* #444444 dark gray */
    if (strcmp(name, "directory") == 0)
        return lle_color_rgb(0, 95, 175);      /* #005FAF strong blue */
    if (strcmp(name, "git") == 0)
        return lle_color_rgb(135, 95, 175);    /* #875FAF medium purple */
    if (strcmp(name, "status") == 0)
        return lle_color_rgb(175, 0, 0);       /* #AF0000 strong red */
    if (strcmp(name, "jobs") == 0)
        return lle_color_rgb(175, 95, 0);      /* #AF5F00 orange */
    if (strcmp(name, "time") == 0)
        return lle_color_rgb(58, 58, 58);      /* #3A3A3A dim gray */
    if (strcmp(name, "shlvl") == 0)
        return lle_color_rgb(68, 68, 68);      /* #444444 dark gray */
    if (strcmp(name, "ssh") == 0)
        return lle_color_rgb(175, 95, 0);      /* #AF5F00 amber */
    if (strcmp(name, "cmd_duration") == 0)
        return lle_color_rgb(175, 95, 0);      /* #AF5F00 orange */
    if (strcmp(name, "virtualenv") == 0)
        return lle_color_rgb(0, 135, 0);       /* #008700 green */
    if (strcmp(name, "container") == 0)
        return lle_color_rgb(0, 135, 135);     /* #008787 teal */
    if (strcmp(name, "aws") == 0)
        return lle_color_rgb(175, 95, 0);      /* #AF5F00 orange */
    if (strcmp(name, "kubernetes") == 0)
        return lle_color_rgb(0, 95, 175);      /* #005FAF blue */
    /* Fallback: dark gray */
    return lle_color_rgb(68, 68, 68);          /* #444444 */
}

/**
 * @brief Resolve the fg/bg colors for a segment
 *
 * Checks per-segment config first, falls back to defaults.
 * Applies terminal color downgrade.
 */
static void resolve_segment_colors(const lle_theme_t *theme,
                                   const char *segment_name,
                                   const lle_prompt_context_t *ctx,
                                   lle_color_t *fg_out,
                                   lle_color_t *bg_out) {
    /* Default foreground: bold true-color white (palette 255 gets remapped
     * by dark terminal colorschemes and becomes unreadable) */
    lle_color_t fg = lle_color_rgb(255, 255, 255);
    fg.bold = true;
    lle_color_t bg = default_segment_bg(segment_name);

    /* Check theme text color */
    if (theme->colors.text.mode != LLE_COLOR_MODE_NONE) {
        fg = theme->colors.text;
    }

    /* Check per-segment config overrides */
    for (size_t i = 0; i < theme->segment_config_count; i++) {
        if (strcmp(theme->segment_configs[i].name, segment_name) == 0 &&
            theme->segment_configs[i].configured) {
            if (theme->segment_configs[i].fg_color_set) {
                fg = theme->segment_configs[i].fg_color;
            }
            if (theme->segment_configs[i].bg_color_set) {
                bg = theme->segment_configs[i].bg_color;
            }
            break;
        }
    }

    /* Downgrade colors based on terminal capability */
    *fg_out = lle_color_downgrade(&fg, ctx->has_true_color, ctx->has_256_color);
    *bg_out = lle_color_downgrade(&bg, ctx->has_true_color, ctx->has_256_color);
}

/* ============================================================================
 * Buffer Append Helpers
 * ============================================================================
 */

/** Append state for safe buffer building */
typedef struct {
    char *buf;
    size_t size;
    size_t pos;
} append_ctx_t;

static void buf_append(append_ctx_t *a, const char *str, size_t len) {
    if (a->pos + len >= a->size)
        return; /* Silently truncate */
    memcpy(a->buf + a->pos, str, len);
    a->pos += len;
    a->buf[a->pos] = '\0';
}

static void buf_append_str(append_ctx_t *a, const char *str) {
    buf_append(a, str, strlen(str));
}

static void buf_append_color_fg(append_ctx_t *a, const lle_color_t *color) {
    char ansi[32];
    size_t len = lle_color_to_ansi(color, true, ansi, sizeof(ansi));
    if (len > 0) {
        buf_append(a, ansi, len);
    }
}

static void buf_append_color_bg(append_ctx_t *a, const lle_color_t *color) {
    char ansi[32];
    size_t len = lle_color_to_ansi(color, false, ansi, sizeof(ansi));
    if (len > 0) {
        buf_append(a, ansi, len);
    }
}

static void buf_append_reset(append_ctx_t *a) {
    buf_append(a, "\033[0m", 4);
}

/* ============================================================================
 * ANSI Stripping
 * ============================================================================
 */

/**
 * @brief Strip ANSI escape sequences from a string
 *
 * Segment renderers embed their own ANSI color codes in content. The
 * powerline renderer provides its own fg/bg wrapping, so these inner
 * escape sequences must be removed to prevent color clobbering.
 *
 * Handles CSI sequences (ESC [ ... final_byte) which covers all color
 * and SGR sequences.
 */
static size_t strip_ansi(const char *src, char *dst, size_t dst_size) {
    size_t out = 0;

    for (size_t i = 0; src[i] != '\0'; i++) {
        if (src[i] == '\033' && src[i + 1] == '[') {
            /* Skip ESC [ ... until final byte (0x40-0x7E) */
            i += 2;
            while (src[i] != '\0' &&
                   !((unsigned char)src[i] >= 0x40 &&
                     (unsigned char)src[i] <= 0x7E)) {
                i++;
            }
            /* i now points to the final byte (or NUL); loop increment
             * will advance past it */
            continue;
        }

        if (out + 1 < dst_size) {
            dst[out++] = src[i];
        }
    }

    dst[out] = '\0';
    return out;
}

/* ============================================================================
 * Core Rendering
 * ============================================================================
 */

/**
 * @brief Collect visible segments with their rendered content and colors
 */
static size_t collect_visible_segments(const lle_theme_t *theme,
                                       lle_segment_registry_t *registry,
                                       const lle_prompt_context_t *ctx,
                                       powerline_segment_t *out,
                                       size_t out_capacity) {
    size_t count = 0;

    for (size_t i = 0; i < theme->enabled_segment_count && count < out_capacity;
         i++) {
        const char *name = theme->enabled_segments[i];

        /* Find segment in registry */
        const lle_prompt_segment_t *seg =
            lle_segment_registry_find(registry, name);
        if (!seg)
            continue;

        /* Check visibility */
        if (seg->is_visible && !seg->is_visible(seg, ctx))
            continue;

        /* Check per-segment show flag */
        for (size_t j = 0; j < theme->segment_config_count; j++) {
            if (strcmp(theme->segment_configs[j].name, name) == 0 &&
                theme->segment_configs[j].configured &&
                theme->segment_configs[j].show_set &&
                !theme->segment_configs[j].show) {
                goto next_segment;
            }
        }

        /* Render segment content */
        lle_segment_output_t output;
        memset(&output, 0, sizeof(output));
        if (seg->render(seg, ctx, theme, &output) != LLE_SUCCESS)
            continue;
        if (output.is_empty || output.content_len == 0)
            continue;

        /* Store with resolved colors, stripping embedded ANSI codes */
        powerline_segment_t *ps = &out[count];
        ps->content_len =
            strip_ansi(output.content, ps->content, sizeof(ps->content));
        resolve_segment_colors(theme, name, ctx, &ps->fg, &ps->bg);
        count++;

    next_segment:;
    }

    return count;
}

/**
 * @brief Render segments left-to-right (for PS1)
 *
 * Each segment: [bg][fg] content [separator with color transition]
 * Final segment gets a trailing separator fading to terminal default.
 */
static void render_left_to_right(const powerline_segment_t *segs,
                                 size_t count, const char *separator,
                                 append_ctx_t *a) {
    for (size_t i = 0; i < count; i++) {
        const powerline_segment_t *seg = &segs[i];

        /* Segment content: bg + fg + space + content + space */
        buf_append_color_bg(a, &seg->bg);
        buf_append_color_fg(a, &seg->fg);
        buf_append_str(a, " ");
        buf_append(a, seg->content, seg->content_len);
        buf_append_str(a, " ");

        /* Separator */
        if (i + 1 < count) {
            /* Between segments: fg=this.bg, bg=next.bg */
            const powerline_segment_t *next = &segs[i + 1];
            buf_append_color_fg(a, &seg->bg);
            buf_append_color_bg(a, &next->bg);
            buf_append_str(a, separator);
        } else {
            /* Final: reset, then fg=this.bg on default bg */
            buf_append_reset(a);
            buf_append_color_fg(a, &seg->bg);
            buf_append_str(a, separator);
            buf_append_reset(a);
        }
    }
}

/**
 * @brief Render segments right-to-left (for RPROMPT)
 *
 * Leading separator before each segment, content follows.
 * First segment gets a leading separator on terminal default bg.
 */
static void render_right_to_left(const powerline_segment_t *segs,
                                 size_t count, const char *separator,
                                 append_ctx_t *a) {
    for (size_t i = 0; i < count; i++) {
        const powerline_segment_t *seg = &segs[i];

        /* Separator before segment */
        if (i == 0) {
            /* First: fg=this.bg on default bg */
            buf_append_color_fg(a, &seg->bg);
            buf_append_str(a, separator);
        } else {
            /* Between: fg=this.bg, bg=prev.bg */
            const powerline_segment_t *prev = &segs[i - 1];
            buf_append_color_fg(a, &seg->bg);
            buf_append_color_bg(a, &prev->bg);
            buf_append_str(a, separator);
        }

        /* Segment content: bg + fg + space + content + space */
        buf_append_color_bg(a, &seg->bg);
        buf_append_color_fg(a, &seg->fg);
        buf_append_str(a, " ");
        buf_append(a, seg->content, seg->content_len);
        buf_append_str(a, " ");
    }

    buf_append_reset(a);
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

lle_result_t lle_powerline_render(const lle_theme_t *theme,
                                  lle_segment_registry_t *segments,
                                  const lle_prompt_context_t *context,
                                  lle_powerline_direction_t direction,
                                  char *output, size_t output_size) {
    if (!theme || !segments || !context || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    output[0] = '\0';

    if (theme->enabled_segment_count == 0) {
        return LLE_SUCCESS;
    }

    /* Collect visible segments */
    powerline_segment_t visible[POWERLINE_MAX_VISIBLE];
    size_t count =
        collect_visible_segments(theme, segments, context, visible,
                                POWERLINE_MAX_VISIBLE);

    if (count == 0) {
        return LLE_SUCCESS;
    }

    /* Get separator character from theme */
    const char *separator;
    if (direction == LLE_POWERLINE_LEFT_TO_RIGHT) {
        separator = theme->symbols.separator_left;
        if (!separator[0])
            separator = "\xee\x82\xb0"; /* U+E0B0  */
    } else {
        separator = theme->symbols.separator_right;
        if (!separator[0])
            separator = "\xee\x82\xb2"; /* U+E0B2  */
    }

    /* Assemble output */
    append_ctx_t a = {output, output_size, 0};

    if (direction == LLE_POWERLINE_LEFT_TO_RIGHT) {
        render_left_to_right(visible, count, separator, &a);
    } else {
        render_right_to_left(visible, count, separator, &a);
    }

    return LLE_SUCCESS;
}
