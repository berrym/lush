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

/// Project-wide config storage. Defined in src/config.c.
extern config_values_t config;

bool completion_filter_admits(const char *prefix, const char *candidate) {
    if (!candidate || candidate[0] == '\0') {
        return false;
    }
    if (!prefix || prefix[0] == '\0') {
        return true;
    }

    lle_unicode_compare_options_t cmp_opts = LLE_UNICODE_COMPARE_DEFAULT;
    cmp_opts.case_insensitive = !config.completion_case_sensitive;

    switch (config.completion_match_mode) {
    case COMPLETION_MATCH_SUBSTRING:
        return lle_unicode_contains_z(prefix, candidate, &cmp_opts);
    case COMPLETION_MATCH_FUZZY: {
        fuzzy_match_options_t fopts = FUZZY_MATCH_DEFAULT;
        fopts.case_sensitive = config.completion_case_sensitive;
        int score = fuzzy_completion_score(prefix, candidate, &fopts);
        return score >= config.completion_threshold;
    }
    case COMPLETION_MATCH_PREFIX:
    default:
        return lle_unicode_is_prefix_z(prefix, candidate, &cmp_opts);
    }
}

void completion_filter_bridge_init(void) {
    lle_completion_set_filter_fn(completion_filter_admits);
}
