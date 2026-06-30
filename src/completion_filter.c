/**
 * @file completion_filter.c
 * @brief Implementation of completion_filter_admits.
 *
 * See include/completion_filter.h for the contract.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "completion_filter.h"

#include "config.h"
#include "fuzzy_match.h"
#include "lle/completion/menu_filter.h"
#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"

#include <string.h>

/// Project-wide config storage. Defined in src/config.c.
extern config_values_t config;

/// Below completion.fuzzy_min_chars typed characters, fuzzy/substring fall back
/// to prefix matching. A 1-char fuzzy query matches almost everything (noise)
/// and is the worst performance amplifier -- it would fuzzy-score and stat
/// every PATH entry. A floor of 0 or 1 disables the guard.
static bool prefix_below_fuzzy_floor(const char *prefix) {
    int floor = config.completion_fuzzy_min_chars;
    if (floor <= 1) {
        return false;
    }
    return (int)lle_utf8_count_codepoints(prefix, strlen(prefix)) < floor;
}

/// The mode to apply for @p prefix: the configured mode, except a short prefix
/// stays prefix-scoped (the floor) so fuzzy/substring never widen on 1 char.
static completion_match_mode_t effective_mode(const char *prefix) {
    completion_match_mode_t mode = config.completion_match_mode;
    if (mode != COMPLETION_MATCH_PREFIX && prefix_below_fuzzy_floor(prefix)) {
        return COMPLETION_MATCH_PREFIX;
    }
    return mode;
}

bool completion_filter_admits(const char *prefix, const char *candidate) {
    if (!candidate || candidate[0] == '\0') {
        return false;
    }
    if (!prefix || prefix[0] == '\0') {
        return true;
    }

    lle_unicode_compare_options_t cmp_opts = LLE_UNICODE_COMPARE_DEFAULT;
    cmp_opts.case_insensitive = !config.completion_case_sensitive;

    switch (effective_mode(prefix)) {
    case COMPLETION_MATCH_SUBSTRING:
        return lle_unicode_contains_z(prefix, candidate, &cmp_opts);
    case COMPLETION_MATCH_FUZZY: {
        fuzzy_match_options_t fopts = FUZZY_MATCH_DEFAULT;
        fopts.case_sensitive = config.completion_case_sensitive;
        int score = fuzzy_completion_score(prefix, candidate, &fopts);
        /// A genuine subsequence match floors at 1; a non-subsequence scores 0.
        /// Require score > 0 so a threshold of 0 still rejects non-matches --
        /// otherwise 0 >= 0 would admit the whole universe AND (since the
        /// rescore pass only ranks score > 0 items) leave those non-matches at
        /// their high static relevance, sorting them above the real fuzzy hits.
        return score > 0 && score >= config.completion_threshold;
    }
    case COMPLETION_MATCH_PREFIX:
    default:
        return lle_unicode_is_prefix_z(prefix, candidate, &cmp_opts);
    }
}

int completion_filter_score(const char *prefix, const char *candidate) {
    if (!prefix || !candidate || prefix[0] == '\0' || candidate[0] == '\0') {
        return 0;
    }
    /// Only fuzzy mode ranks; prefix/substring (and the short-prefix floor)
    /// keep the source's static order, so report 0 ("do not rank").
    if (effective_mode(prefix) != COMPLETION_MATCH_FUZZY) {
        return 0;
    }
    fuzzy_match_options_t fopts = FUZZY_MATCH_DEFAULT;
    fopts.case_sensitive = config.completion_case_sensitive;
    return fuzzy_completion_score(prefix, candidate, &fopts);
}

void completion_filter_bridge_init(void) {
    lle_completion_set_filter_fn(completion_filter_admits);
    lle_completion_set_score_fn(completion_filter_score);
}
