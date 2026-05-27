/**
 * @file pager_layer.c
 * @brief Pager display layer -- data and scroll math
 *
 * Implements the contract declared in include/display/pager_layer.h.
 * Owns the line index over a borrowed content blob plus view / mode
 * state. No terminal I/O, no rendering, no input handling -- those
 * land in the display-controller pager branch and the input loop
 * module respectively.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/pager_layer.h"

#include <stdlib.h>
#include <string.h>

static void pager_layer_clear(pager_layer_t *pager) {
    pager->content = NULL;
    pager->content_byte_length = 0;
    memset(&pager->line_index, 0, sizeof(pager->line_index));
    pager->terminal_width = 0;
    pager->view_rows = 0;
    pager->top_line = 0;
    pager->mode = PAGER_MODE_VIEW;
    pager->active = false;
    pager->search_pattern = NULL;
    pager->current_match_line = (size_t)-1;
    pager->wrap_searches = true;
}

int pager_layer_init(pager_layer_t *pager, const char *content,
                     size_t content_len, size_t view_rows, int term_width) {
    if (!pager) {
        return -1;
    }
    pager_layer_clear(pager);

    pager->content = content;
    pager->content_byte_length = content_len;
    pager->view_rows = view_rows;
    pager->terminal_width = term_width > 0 ? term_width : 1;
    pager->mode = PAGER_MODE_VIEW;
    pager->active = true;
    pager->current_match_line = (size_t)-1;
    pager->wrap_searches = true;

    if (screen_line_index_build(&pager->line_index, content, content_len,
                                pager->terminal_width) != 0) {
        pager_layer_clear(pager);
        return -1;
    }
    pager->top_line = 0;
    return 0;
}

void pager_layer_destroy(pager_layer_t *pager) {
    if (!pager) {
        return;
    }
    screen_line_index_free(&pager->line_index);
    free(pager->search_pattern);
    pager_layer_clear(pager);
}

/* ============================================================================
 * Scroll math
 * ============================================================================
 */

/**
 * @brief Largest valid top_line value
 *
 * If content is empty, the only valid value is 0. Otherwise we permit
 * top_line up to line_count - 1 so even the last line alone remains
 * addressable. pager_layer_scroll_bottom picks the more user-friendly
 * "bottom anchor" position (window's worth of content visible) on
 * top of this bound.
 */
static size_t pager_layer_max_top(const pager_layer_t *pager) {
    if (!pager || pager->line_index.count == 0) {
        return 0;
    }
    return pager->line_index.count - 1;
}

static void clamp_top(pager_layer_t *pager) {
    size_t max_top = pager_layer_max_top(pager);
    if (pager->top_line > max_top) {
        pager->top_line = max_top;
    }
}

void pager_layer_scroll_lines(pager_layer_t *pager, long delta) {
    if (!pager) {
        return;
    }
    if (delta < 0) {
        size_t mag = (size_t)(-delta);
        if (mag >= pager->top_line) {
            pager->top_line = 0;
        } else {
            pager->top_line -= mag;
        }
    } else {
        pager->top_line += (size_t)delta;
        clamp_top(pager);
    }
}

void pager_layer_scroll_pages(pager_layer_t *pager, long delta) {
    if (!pager) {
        return;
    }
    /// Use view_rows as the page stride (one screenful of logical
    /// lines, simplifying assumption documented in the header).
    long stride = (long)pager->view_rows;
    if (stride <= 0) {
        stride = 1;
    }
    pager_layer_scroll_lines(pager, delta * stride);
}

void pager_layer_scroll_top(pager_layer_t *pager) {
    if (!pager) {
        return;
    }
    pager->top_line = 0;
}

void pager_layer_scroll_bottom(pager_layer_t *pager) {
    if (!pager || pager->line_index.count == 0) {
        if (pager) {
            pager->top_line = 0;
        }
        return;
    }
    /// Walk backward from the end summing visual_height until adding
    /// the next entry would exceed view_rows. The top_line value we
    /// settle on is the largest where the visible window still fits.
    size_t accum = 0;
    size_t i = pager->line_index.count;
    while (i > 0) {
        size_t prev = i - 1;
        size_t h = pager->line_index.entries[prev].visual_height;
        if (accum + h > pager->view_rows) {
            break;
        }
        accum += h;
        i = prev;
    }
    pager->top_line = i;
    clamp_top(pager);
}

size_t pager_layer_visible_line_count(const pager_layer_t *pager) {
    if (!pager || pager->line_index.count == 0 || pager->view_rows == 0) {
        return 0;
    }
    size_t accum = 0;
    size_t count = 0;
    for (size_t i = pager->top_line; i < pager->line_index.count; i++) {
        size_t h = pager->line_index.entries[i].visual_height;
        if (accum + h > pager->view_rows) {
            break;
        }
        accum += h;
        count++;
    }
    /// Even when no full line fits (a single line is taller than the
    /// view), report 1 so the caller has something to draw against
    /// rather than rendering an empty page.
    if (count == 0 && pager->top_line < pager->line_index.count) {
        count = 1;
    }
    return count;
}

/* ============================================================================
 * Resize
 * ============================================================================
 */

void pager_layer_resize(pager_layer_t *pager, int term_width,
                        size_t view_rows) {
    if (!pager) {
        return;
    }
    if (term_width <= 0) {
        term_width = 1;
    }
    pager->terminal_width = term_width;
    pager->view_rows = view_rows;
    if (pager->content) {
        screen_line_index_rewidth(&pager->line_index, pager->content,
                                  pager->content_byte_length, term_width);
    }
    clamp_top(pager);
}

/* ============================================================================
 * Mode and search state
 * ============================================================================
 */

void pager_layer_set_mode(pager_layer_t *pager, pager_mode_t mode) {
    if (!pager) {
        return;
    }
    pager->mode = mode;
}

int pager_layer_set_search(pager_layer_t *pager, const char *pattern) {
    if (!pager) {
        return -1;
    }
    free(pager->search_pattern);
    pager->search_pattern = NULL;
    pager->current_match_line = (size_t)-1;
    if (!pattern) {
        return 0;
    }
    pager->search_pattern = strdup(pattern);
    if (!pager->search_pattern) {
        return -1;
    }
    return 0;
}

void pager_layer_clear_search(pager_layer_t *pager) {
    (void)pager_layer_set_search(pager, NULL);
}

int pager_layer_search_append_byte(pager_layer_t *pager, unsigned char byte) {
    if (!pager) {
        return -1;
    }
    size_t cur_len = pager->search_pattern ? strlen(pager->search_pattern) : 0;
    char *bigger = realloc(pager->search_pattern, cur_len + 2);
    if (!bigger) {
        return -1;
    }
    bigger[cur_len] = (char)byte;
    bigger[cur_len + 1] = '\0';
    pager->search_pattern = bigger;
    return 0;
}

void pager_layer_search_backspace(pager_layer_t *pager) {
    if (!pager || !pager->search_pattern) {
        return;
    }
    size_t len = strlen(pager->search_pattern);
    if (len == 0) {
        return;
    }
    pager->search_pattern[len - 1] = '\0';
}

/**
 * @brief Substring match within a content slice
 *
 * Byte-wise scan; the slice is not NUL-terminated and may contain
 * embedded NULs (rare for shell output but possible). Equivalent to
 * memmem() but spelled out so platforms without it (uncommon, but
 * the rule for portable code in this project is "no GNU
 * extensions") still build.
 */
static bool slice_contains(const char *hay, size_t hay_len, const char *needle,
                           size_t needle_len) {
    if (needle_len == 0 || needle_len > hay_len) {
        return needle_len == 0;
    }
    size_t last_start = hay_len - needle_len;
    for (size_t i = 0; i <= last_start; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

size_t pager_layer_search_advance(const pager_layer_t *pager, size_t from_line,
                                  pager_search_direction_t direction) {
    if (!pager || !pager->search_pattern || pager->search_pattern[0] == '\0') {
        return PAGER_SEARCH_NO_MATCH;
    }
    size_t n = pager->line_index.count;
    if (n == 0) {
        return PAGER_SEARCH_NO_MATCH;
    }
    size_t pat_len = strlen(pager->search_pattern);
    bool forward = (direction == PAGER_SEARCH_FORWARD);
    bool wrap = pager->wrap_searches;

    /// Walk lines in the requested direction. The candidate set is
    /// at most n lines and visits each at most once; wrap_searches
    /// is the only knob that lets the scan cross the content
    /// boundary.
    size_t scanned = 0;
    size_t idx = from_line;
    while (scanned < n) {
        if (forward) {
            if (idx + 1 < n) {
                idx++;
            } else if (wrap) {
                idx = 0;
            } else {
                break;
            }
        } else {
            if (idx > 0) {
                idx--;
            } else if (wrap) {
                idx = n - 1;
            } else {
                break;
            }
        }
        const screen_line_index_entry_t *e = &pager->line_index.entries[idx];
        if (slice_contains(pager->content + e->byte_offset, e->byte_length,
                           pager->search_pattern, pat_len)) {
            return idx;
        }
        scanned++;
        if (idx == from_line) {
            break; /// completed a full wrap without finding
        }
    }
    return PAGER_SEARCH_NO_MATCH;
}
