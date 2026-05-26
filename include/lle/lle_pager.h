/**
 * @file lle_pager.h
 * @brief LLE pager public API
 *
 * Single entry point for paginated output. Consumers (builtins,
 * debugger, completion overflow) call lle_pager_present to display
 * potentially long text; the function decides at call time whether
 * pagination is needed and either writes the content directly or
 * activates the pager view with its own input loop.
 *
 * Non-tty stdout, content shorter than one screen, or any failure to
 * initialise the pager all fall back to a direct write to stdout, so
 * the function is safe to call unconditionally.
 *
 * Wires together the four pieces shipped in earlier work:
 *   - screen_line_index (visual-row math)
 *   - pager_layer       (state + scroll)
 *   - display_controller pager branch (rendering)
 *   - pager_input       (key dispatch + loop)
 *
 * Future steps in the design (see docs/development/LLE_PAGER_DESIGN.md)
 * add config gating, builtin migrations, and debugger integration on
 * top of this entry point. The executor parameter is reserved for
 * those steps; step 5 does not yet consult it.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LLE_PAGER_H
#define LLE_PAGER_H

#ifdef __cplusplus
extern "C" {
#endif

struct executor;

/**
 * @brief Present content, paginating when interactive and overflowing
 *
 * Decision tree at call time:
 *   - `content` is NULL or empty            -> succeed without writing
 *   - stdout is not a tty                   -> direct write, return 0
 *   - display controller unavailable        -> direct write, return 0
 *   - terminal size query fails             -> direct write, return 0
 *   - content fits in `rows - 1` visual rows -> direct write, return 0
 *   - otherwise                             -> pager view, blocks until user
 * quits
 *
 * On the pager path, the terminal is placed into raw mode for the
 * duration of the pager view (saved + restored via tcgetattr /
 * tcsetattr) so single-key navigation works without ENTER.
 *
 * When the user exits the pager (q, Esc, Ctrl-C, Ctrl-G, or EOF on
 * stdin), the function clears the pager view, restores terminal
 * settings, and returns. The caller's next render (the next readline
 * prompt, the next builtin output, etc.) repaints the screen.
 *
 * The pager owns no persistent state across calls; lifetimes of
 * pager_layer + screen_line_index are scoped to this function.
 *
 * @param executor Executor context (reserved for future config use)
 * @param content  Text to display. The pager reads through it; it
 *                 must remain valid (and unchanged) until this call
 *                 returns. A NULL or empty string is a no-op success.
 * @return 0 on success (including the direct-write fallbacks),
 *         non-zero only if the pager path could not initialise its
 *         own state -- in which case stdout is left untouched.
 */
int lle_pager_present(struct executor *executor, const char *content);

#ifdef __cplusplus
}
#endif

#endif /// LLE_PAGER_H
