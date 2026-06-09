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

#include <stddef.h>

static lle_completion_filter_fn_t g_filter_fn = NULL;

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
        return true;
    }
    return g_filter_fn(prefix, candidate);
}
