/**
 * @file menu_filter.h
 * @brief LLE-side bridge to the shell's completion filter predicate.
 *
 * The in-menu type-to-filter pass and the bridge pre-emit filter share
 * a single predicate (completion_filter_admits in
 * src/completion_filter.c) that consults completion.match_mode and
 * dispatches to lle_unicode_is_prefix_z / lle_unicode_contains_z /
 * fuzzy_completion_score. The predicate is shell-side; this header
 * exposes a function-pointer registration so menu logic inside liblle
 * can invoke it without taking a hard dependency on shell-side code.
 *
 * Shell init registers the predicate at startup via
 * completion_filter_bridge_init() (see src/completion_filter.c). When
 * no predicate is registered (test stubs, early init), the invoker
 * admits every candidate so menu code can run without special-casing
 * the unset state.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LLE_COMPLETION_MENU_FILTER_H
#define LLE_COMPLETION_MENU_FILTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Predicate signature.
 *
 * Returns true if @p candidate is admitted under the active completion
 * configuration for @p prefix. The shell-side implementation reads
 * completion.match_mode, completion.case_sensitive, and
 * completion.threshold each call so runtime changes take effect
 * without re-registration.
 */
typedef bool (*lle_completion_filter_fn_t)(const char *prefix,
                                           const char *candidate);

/**
 * @brief Register the predicate the menu and bridge will consult.
 *
 * Passing NULL clears the registration and reverts every subsequent
 * lle_completion_filter_invoke call to admitting all candidates.
 * Idempotent; safe to call multiple times.
 */
void lle_completion_set_filter_fn(lle_completion_filter_fn_t fn);

/**
 * @brief Apply the registered predicate to a (prefix, candidate) pair.
 *
 * Edge cases are uniform across modes:
 *   - NULL or empty @p candidate returns false.
 *   - NULL or empty @p prefix returns true (no filter active).
 *   - No predicate registered returns true (open-by-default).
 */
bool lle_completion_filter_invoke(const char *prefix, const char *candidate);

#ifdef __cplusplus
}
#endif

#endif /* LLE_COMPLETION_MENU_FILTER_H */
