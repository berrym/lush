/**
 * @file completion_bridge.c
 * @brief Shell-side typed access to the LLE completion-result accumulator.
 *
 * See include/completion_bridge.h for the contract.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "completion_bridge.h"

#include "completion_filter.h"
#include "executor.h"
#include "lle/completion/completion_types.h"

bool completion_bridge_active(struct executor *e) {
    return e != NULL && e->active_comp_result != NULL;
}

int completion_bridge_add(struct executor *e, const char *candidate,
                          const char *description) {
    if (!e || !e->active_comp_result || !candidate || candidate[0] == '\0') {
        return -1;
    }
    /// Filtering here means every compadd code path (positional,
    /// -a, -k) and any future emit path benefits without per-site
    /// duplication; the user's bound function can emit its entire
    /// candidate set blindly. completion_filter_admits dispatches on
    /// completion.match_mode (prefix / substring / fuzzy) so the
    /// active configuration choice applies uniformly across the
    /// engine; switching modes never produces divergent ranking
    /// between the pre-emit filter and other consumers.
    if (e->active_comp_prefix &&
        !completion_filter_admits(e->active_comp_prefix, candidate)) {
        return 0;
    }
    lle_completion_result_t *r =
        (lle_completion_result_t *)e->active_comp_result;
    /* Type CUSTOM marks compdef-emitted candidates so the menu renderer
     * can distinguish them from engine-built sources (files, builtins,
     * etc.). Relevance 500 is the mid-band default; compdef bindings
     * are domain-specific and should not outrank or be outranked by
     * generic file/command completion on score alone. */
    lle_result_t rc;
    if (description && description[0] != '\0') {
        rc = lle_completion_result_add_with_description(
            r, candidate, NULL, LLE_COMPLETION_TYPE_CUSTOM, 500, description);
    } else {
        rc = lle_completion_result_add(r, candidate, NULL,
                                       LLE_COMPLETION_TYPE_CUSTOM, 500);
    }
    return rc == LLE_SUCCESS ? 0 : -1;
}
