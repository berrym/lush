/**
 * @file pager_layer.h
 * @brief Display L3 layer for paginated content
 *
 * Sibling to prompt_layer / command_layer / autosuggestions_layer.
 * Owns a const-char content blob, a screen_line_index over it, the
 * view state (top_line, view_rows), and the modal state (view /
 * search / help). This module is data and math only -- no terminal
 * I/O, no input handling, no rendering. The display controller will
 * compose a screen_buffer view from a slice of the layer's line
 * index; input dispatch lives in the input loop module.
 *
 * Scroll semantics: navigation is by logical line. A "line down"
 * advances top_line by one entry in the line index; a "page down"
 * advances by view_rows entries. Visual-row-precise scrolling (so
 * that mid-wrapped-line positions are addressable) is a later
 * refinement; the simpler logical-line stride is what less, more,
 * and vim use by default and is sufficient for the common case.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef PAGER_LAYER_H
#define PAGER_LAYER_H

#include "display/screen_buffer.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pager modal state
 */
typedef enum {
    PAGER_MODE_VIEW = 0, ///< Default; navigation keys active
    PAGER_MODE_SEARCH,   ///< Search prompt at status line
    PAGER_MODE_HELP,     ///< Help overlay
} pager_mode_t;

/**
 * @brief Pager display layer
 *
 * Owned by the display controller while pagination is active. The
 * `content` pointer is borrowed -- the layer does not free it on
 * destroy; the caller (typically `lle_pager_present`) owns content
 * lifetime. The line index, however, is owned by the layer and is
 * freed by pager_layer_destroy.
 */
typedef struct pager_layer {
    /// Source content (borrowed; caller owns)
    const char *content;
    size_t content_byte_length;

    /// Line index over content (owned)
    screen_line_index_t line_index;

    /// Geometry
    int terminal_width;
    size_t view_rows; ///< Visible rows for content (excluding status line)

    /// View state
    size_t top_line; ///< Index of first visible line in line_index.entries

    /// Modal state
    pager_mode_t mode;
    bool active; ///< True between init and destroy

    /// Search state (storage only; the search engine lands later)
    char *search_pattern;      ///< NULL when no search active
    size_t current_match_line; ///< Index into line_index.entries; SIZE_MAX = no
                               ///< match
    bool wrap_searches;        ///< Wrap to top on no-match (config)
} pager_layer_t;

/**
 * @brief Initialise a pager layer over `content`
 *
 * Builds the line index from `content` (length `content_len`) for the
 * given terminal width, sets view_rows for the visible-content area,
 * and parks the view at the top in VIEW mode with no active search.
 * The layer borrows `content`; the caller must keep it alive until
 * pager_layer_destroy returns.
 *
 * @param pager        Layer to initialise (must point to writable memory)
 * @param content      Content blob (caller owns; must outlive layer)
 * @param content_len  Length of content in bytes
 * @param view_rows    Rows available for content (typically terminal_rows - 1)
 * @param term_width   Terminal width in columns
 * @return 0 on success, -1 on OOM (layer is left in a safe destroyed state)
 */
int pager_layer_init(pager_layer_t *pager, const char *content,
                     size_t content_len, size_t view_rows, int term_width);

/**
 * @brief Tear down a pager layer
 *
 * Frees the owned line index and search pattern; clears the struct
 * to a safe-zero state. Does not free the content blob (borrowed).
 * Safe on already-destroyed / zero-initialised structs.
 *
 * @param pager Layer to destroy
 */
void pager_layer_destroy(pager_layer_t *pager);

/* ============================================================================
 * Scroll math (logical-line stride)
 * ============================================================================
 */

/**
 * @brief Scroll the view by `delta` logical lines (positive = down)
 *
 * Clamps top_line to a valid range:
 *   - never below 0
 *   - never above the largest top_line such that at least one line
 *     remains visible (i.e. top_line <= line_count - 1 when content
 *     is non-empty; or 0 when content is empty).
 *
 * @param pager Layer to scroll
 * @param delta Number of logical lines to move (positive: down)
 */
void pager_layer_scroll_lines(pager_layer_t *pager, long delta);

/**
 * @brief Scroll the view by `delta` pages (positive = down)
 *
 * One page is view_rows logical lines. Clamps via pager_layer_scroll_lines.
 *
 * @param pager Layer to scroll
 * @param delta Number of pages to move
 */
void pager_layer_scroll_pages(pager_layer_t *pager, long delta);

/**
 * @brief Park the view at the top of content
 *
 * @param pager Layer to scroll
 */
void pager_layer_scroll_top(pager_layer_t *pager);

/**
 * @brief Park the view so the last line of content is visible
 *
 * Positions top_line at the largest value such that the visible
 * window's visual_height sum stays within view_rows.
 *
 * @param pager Layer to scroll
 */
void pager_layer_scroll_bottom(pager_layer_t *pager);

/**
 * @brief Number of logical lines whose visual rows fit in the view
 *
 * Sums visual_height of entries from top_line until adding the next
 * entry would exceed view_rows, returning the count of entries
 * included. Useful for the render path to know how many lines to
 * draw and for scroll_bottom to compute the correct anchor.
 *
 * @param pager Layer to query
 * @return Number of visible logical lines starting at top_line
 */
size_t pager_layer_visible_line_count(const pager_layer_t *pager);

/* ============================================================================
 * Resize
 * ============================================================================
 */

/**
 * @brief Apply a new terminal width and view-rows count
 *
 * Recomputes visual_height per line in the index for the new width.
 * top_line is clamped to a valid range against the new line count
 * (which is unchanged by width changes, but view_rows changes can
 * make the previous bottom-anchored position invalid).
 *
 * @param pager      Layer to resize
 * @param term_width New terminal width
 * @param view_rows  New view-rows count
 */
void pager_layer_resize(pager_layer_t *pager, int term_width, size_t view_rows);

/* ============================================================================
 * Mode and search state
 * ============================================================================
 */

/**
 * @brief Transition to a new modal state
 *
 * Pure state mutation; no side effects on view or search state.
 *
 * @param pager Layer to mutate
 * @param mode  New mode
 */
void pager_layer_set_mode(pager_layer_t *pager, pager_mode_t mode);

/**
 * @brief Store a search pattern (the search engine lands later)
 *
 * Replaces any prior pattern; `pattern == NULL` clears the search.
 * The layer makes its own copy of the pattern string. Resets
 * current_match_line to SIZE_MAX.
 *
 * @param pager   Layer to mutate
 * @param pattern Search pattern (NUL-terminated) or NULL to clear
 * @return 0 on success, -1 on OOM
 */
int pager_layer_set_search(pager_layer_t *pager, const char *pattern);

/**
 * @brief Clear the active search state
 *
 * Equivalent to `pager_layer_set_search(pager, NULL)`.
 *
 * @param pager Layer to mutate
 */
void pager_layer_clear_search(pager_layer_t *pager);

#ifdef __cplusplus
}
#endif

#endif /// PAGER_LAYER_H
