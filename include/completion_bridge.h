/**
 * @file completion_bridge.h
 * @brief Shell-side facade for the LLE completion-result accumulator.
 *
 * compadd appends candidates to executor->active_comp_result while a
 * compdef-bound user function is running. The underlying type is
 * lle_completion_result_t (owned by liblle, defined in
 * include/lle/completion/completion_types.h). Exposing the LLE header
 * to bin_compadd.c would invert the shell/LLE dependency direction
 * (liblle is the dependent layer; the shell is the host). This header
 * gives the shell side a typed, dependency-free API; the LLE bridge
 * (src/lle/completion/, separate change) provides the implementations
 * by casting executor->active_comp_result back to the real type.
 *
 * Lifecycle: the LLE completion source, before invoking the function
 * bound by compdef, sets executor->active_comp_result to the active
 * lle_completion_result_t and clears it after the call. compadd reads
 * via this API and errors when there is no active result.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_COMPLETION_BRIDGE_H
#define LUSH_COMPLETION_BRIDGE_H

#include <stdbool.h>

/// Forward declaration -- the full executor_t lives in executor.h.
struct executor;

/**
 * @brief Test whether a completion-result accumulator is currently
 *        active on the given executor.
 *
 * @param e Executor (may be NULL; returns false).
 * @return true if executor->active_comp_result is non-NULL.
 */
bool completion_bridge_active(struct executor *e);

/**
 * @brief Append one candidate (with optional description) to the
 *        active completion-result accumulator.
 *
 * No-op and returns -1 when no result is active (compadd is expected
 * to check completion_bridge_active() first and surface a builtin
 * error). The candidate / description strings are copied internally.
 *
 * When executor->active_comp_prefix is non-empty, candidates that do
 * not NFC-prefix-match the prefix are silently skipped and the call
 * returns 0 (a deliberately filtered candidate is not a failure). The
 * filter uses lle_unicode_is_prefix_z so normalization-form
 * differences in either side do not mismatch.
 *
 * @param e Executor whose active_comp_result is the target.
 * @param candidate Candidate text. Must be non-NULL, non-empty.
 * @param description Description text. NULL for no description.
 * @return 0 on success or prefix-filtered skip, -1 on failure (no
 *         active result, OOM, or invalid input).
 */
int completion_bridge_add(struct executor *e, const char *candidate,
                          const char *description);

#endif /* LUSH_COMPLETION_BRIDGE_H */
