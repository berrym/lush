/**
 * @file completion_filter.h
 * @brief Shell-side predicate for completion candidate filtering.
 *
 * Looks up the active match mode, case sensitivity, and fuzzy
 * threshold from the central config registry and dispatches to the
 * configured predicate:
 *
 *   - completion.match_mode = prefix
 *     -> lle_unicode_is_prefix_z (NFC-aware prefix)
 *   - completion.match_mode = substring
 *     -> lle_unicode_contains_z (NFC-aware substring)
 *   - completion.match_mode = fuzzy
 *     -> fuzzy_completion_score; admitted when score >=
 *        completion.threshold
 *
 * Consumed in two places so the bridge pre-emit filter (compdef
 * candidate emission) and the in-menu type-to-filter pass share the
 * exact same predicate -- the UX never diverges between "type then
 * TAB" and "TAB then narrow."
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_COMPLETION_FILTER_H
#define LUSH_COMPLETION_FILTER_H

#include <stdbool.h>

/**
 * @brief Test whether a candidate passes the active completion filter.
 *
 * Empty or NULL @p prefix accepts every candidate (no filter active).
 * NULL or empty @p candidate is rejected.
 *
 * The threshold is honored only when the active mode is fuzzy; in
 * prefix and substring modes the predicate is binary.
 *
 * @param prefix    Current word prefix typed by the user (UTF-8). May
 *                  be NULL or empty.
 * @param candidate Candidate string under test (UTF-8). Must be
 *                  non-NULL, non-empty.
 * @return true if the candidate is admitted by the configured filter.
 */
bool completion_filter_admits(const char *prefix, const char *candidate);

/**
 * @brief Register the shell-side filter with the LLE menu/bridge.
 *
 * Calls lle_completion_set_filter_fn(completion_filter_admits) so the
 * in-menu type-to-filter path and the compdef bridge invoke the same
 * predicate. Idempotent; safe to call multiple times during init.
 */
void completion_filter_bridge_init(void);

#endif /* LUSH_COMPLETION_FILTER_H */
