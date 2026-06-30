/**
 * @file menu_filter.c
 * @brief Function-pointer dispatch for the shell completion predicate.
 *
 * See include/lle/completion/menu_filter.h for the contract.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/completion/menu_filter.h"

#include "lle/unicode_compare.h"

#include <stddef.h>

static lle_completion_filter_fn_t g_filter_fn = NULL;
static lle_completion_score_fn_t g_score_fn = NULL;

void lle_completion_set_filter_fn(lle_completion_filter_fn_t fn) {
    g_filter_fn = fn;
}

bool lle_completion_filter_invoke(const char *prefix, const char *candidate) {
    if (!candidate || candidate[0] == '\0') {
        return false;
    }
    if (!prefix || prefix[0] == '\0') {
        return true;
    }
    if (!g_filter_fn) {
        /// No shell predicate wired (standalone liblle, early init): keep the
        /// historical prefix scoping rather than admitting every candidate.
        return lle_unicode_is_prefix_z(prefix, candidate,
                                       &LLE_UNICODE_COMPARE_DEFAULT);
    }
    return g_filter_fn(prefix, candidate);
}

void lle_completion_set_score_fn(lle_completion_score_fn_t fn) {
    g_score_fn = fn;
}

int lle_completion_filter_score_invoke(const char *prefix,
                                       const char *candidate) {
    if (!g_score_fn || !prefix || !candidate || prefix[0] == '\0' ||
        candidate[0] == '\0') {
        return 0;
    }
    return g_score_fn(prefix, candidate);
}
