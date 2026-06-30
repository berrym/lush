/**
 * @file test_menu_filter.c
 * @brief Unit tests for the in-menu type-to-filter state machine.
 *
 * Exercises lle_completion_menu_state_t's filter fields end-to-end:
 *   - append_filter_char narrows the active view
 *   - pop_filter_char widens it
 *   - apply_filter against an empty filter swaps back to unfiltered
 *   - filter_active reports the active/inactive transition
 *   - the menu-filter function-pointer bridge is honored
 *
 * Uses a real lush memory pool and a real lle_completion_result_t so
 * the pool-allocation paths inside the new APIs are exercised. The
 * predicate is registered with a deterministic test stand-in
 * (case-insensitive ASCII strncmp prefix match) so test outcomes do
 * not depend on the central config state.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/completion/completion_menu_state.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/menu_filter.h"
#include "lle/memory_management.h"
#include "lush_memory_pool.h"
#include "test_framework.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

static lle_memory_pool_t *test_pool = NULL;

/// Test predicate: case-insensitive ASCII prefix match. Mirrors the
/// shape of the production predicate (NULL/empty prefix admits all,
/// NULL/empty candidate rejected) so the menu logic exercises the
/// same edge cases the production code will.
static bool test_filter_fn(const char *prefix, const char *candidate) {
    if (!candidate || candidate[0] == '\0') {
        return false;
    }
    if (!prefix || prefix[0] == '\0') {
        return true;
    }
    size_t i = 0;
    while (prefix[i] != '\0' && candidate[i] != '\0') {
        if (tolower((unsigned char)prefix[i]) !=
            tolower((unsigned char)candidate[i])) {
            return false;
        }
        i++;
    }
    /// Prefix consumed -> candidate matched.
    return prefix[i] == '\0';
}

static void global_setup(void) {
    lush_pool_init(NULL);
    lle_memory_pool_create_from_lush(&test_pool, global_memory_pool,
                                     LLE_POOL_BUFFER);
    lle_completion_set_filter_fn(test_filter_fn);
}

static void global_teardown(void) {
    lle_completion_set_filter_fn(NULL);
    if (test_pool) {
        lle_memory_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/// Build a six-candidate result populated with the same set the
/// compdef e2e tests use, so the chosen prefixes ("c", "ch", "co")
/// produce predictable narrowed views.
static lle_completion_result_t *make_test_result(void) {
    lle_completion_result_t *r = NULL;
    if (lle_completion_result_create(test_pool, 8, &r) != LLE_SUCCESS) {
        return NULL;
    }
    const char *names[] = {"checkout", "branch", "merge",
                           "clone",    "commit", "pull"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        (void)lle_completion_result_add(r, names[i], NULL,
                                        LLE_COMPLETION_TYPE_CUSTOM, 500);
    }
    return r;
}

static int find_in_result(const lle_completion_result_t *r, const char *text) {
    if (!r) {
        return -1;
    }
    for (size_t i = 0; i < r->count; i++) {
        if (r->items[i].text && strcmp(r->items[i].text, text) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ============================================================================
 * Filter lifecycle
 * ============================================================================
 */

TEST(filter_inactive_after_create) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    ASSERT_EQ(
        lle_completion_menu_state_create(test_pool, r, NULL, "git", &state),
        LLE_SUCCESS, "menu state created");
    ASSERT_NOT_NULL(state, "state allocated");
    ASSERT_FALSE(lle_completion_menu_filter_active(state),
                 "no filter active immediately after create");
    ASSERT_TRUE(state->result == state->unfiltered_result,
                "result points at unfiltered view");
    ASSERT_EQ(state->result->count, 6u, "all six candidates visible");
    lle_completion_menu_state_free(state);
}

TEST(filter_active_after_first_char) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "", &state);

    ASSERT_EQ(lle_completion_menu_append_filter_char(state, 'c'), LLE_SUCCESS,
              "append 'c' succeeded");
    ASSERT_TRUE(lle_completion_menu_filter_active(state),
                "filter is active after appending one char");
    /// With predicate strncmp("c", x), candidates starting with 'c'
    /// survive: checkout, clone, commit.
    ASSERT_EQ(state->result->count, 3u, "three c-prefix matches");
    ASSERT_TRUE(find_in_result(state->result, "checkout") >= 0,
                "checkout admitted");
    ASSERT_TRUE(find_in_result(state->result, "clone") >= 0, "clone admitted");
    ASSERT_TRUE(find_in_result(state->result, "commit") >= 0,
                "commit admitted");
    ASSERT_EQ(find_in_result(state->result, "branch"), -1, "branch dropped");
    lle_completion_menu_state_free(state);
}

TEST(filter_narrows_further_with_more_chars) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "", &state);

    lle_completion_menu_append_filter_char(state, 'c');
    lle_completion_menu_append_filter_char(state, 'h');
    /// "ch" prefix: only checkout matches.
    ASSERT_EQ(state->result->count, 1u, "ch narrows to one");
    ASSERT_TRUE(find_in_result(state->result, "checkout") >= 0,
                "checkout admitted at 'ch'");
    lle_completion_menu_state_free(state);
}

TEST(filter_pop_widens_view) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "", &state);

    lle_completion_menu_append_filter_char(state, 'c');
    lle_completion_menu_append_filter_char(state, 'h');
    ASSERT_EQ(state->result->count, 1u, "narrowed to one at 'ch'");

    lle_completion_menu_pop_filter_char(state);
    ASSERT_EQ(state->result->count, 3u, "pop widens back to three at 'c'");
    ASSERT_TRUE(lle_completion_menu_filter_active(state),
                "filter still active after one pop");

    lle_completion_menu_pop_filter_char(state);
    ASSERT_EQ(state->result->count, 6u, "second pop restores all six");
    ASSERT_FALSE(lle_completion_menu_filter_active(state),
                 "filter inactive after popping to empty");
    ASSERT_TRUE(state->result == state->unfiltered_result,
                "result reverts to unfiltered view after empty filter");
    lle_completion_menu_state_free(state);
}

TEST(filter_pop_on_empty_is_noop) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "", &state);

    /// Popping when filter is already empty must not crash or
    /// produce a spurious filter_active transition.
    ASSERT_EQ(lle_completion_menu_pop_filter_char(state), LLE_SUCCESS,
              "pop on empty returns SUCCESS");
    ASSERT_FALSE(lle_completion_menu_filter_active(state),
                 "filter remains inactive after no-op pop");
    ASSERT_EQ(state->result->count, 6u,
              "result count unchanged after no-op pop");
    lle_completion_menu_state_free(state);
}

TEST(filter_no_matches_keeps_filter_active_with_zero_results) {
    lle_completion_result_t *r = make_test_result();
    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "", &state);

    /// 'z' matches nothing in the candidate set; the menu's filter
    /// stays active so backspace can widen it back, but the visible
    /// result becomes empty.
    lle_completion_menu_append_filter_char(state, 'z');
    ASSERT_TRUE(lle_completion_menu_filter_active(state),
                "filter active even when no candidates match");
    ASSERT_EQ(state->result->count, 0u, "no matches in filtered view");
    lle_completion_menu_state_free(state);
}

TEST(filter_uses_original_prefix_when_provided) {
    /// Source candidates already pre-filtered by a 'c' prefix: the
    /// menu was opened with original_prefix='c'. Appending 'h' should
    /// build the combined prefix "ch" and narrow accordingly.
    lle_completion_result_t *r = NULL;
    lle_completion_result_create(test_pool, 4, &r);
    lle_completion_result_add(r, "checkout", NULL, LLE_COMPLETION_TYPE_CUSTOM,
                              500);
    lle_completion_result_add(r, "clone", NULL, LLE_COMPLETION_TYPE_CUSTOM,
                              500);
    lle_completion_result_add(r, "commit", NULL, LLE_COMPLETION_TYPE_CUSTOM,
                              500);

    lle_completion_menu_state_t *state = NULL;
    lle_completion_menu_state_create(test_pool, r, NULL, "c", &state);

    lle_completion_menu_append_filter_char(state, 'h');
    /// Combined prefix "ch" -> only checkout admitted.
    ASSERT_EQ(state->result->count, 1u, "ch narrows to checkout only");
    ASSERT_TRUE(find_in_result(state->result, "checkout") >= 0,
                "checkout admitted by combined prefix");
    lle_completion_menu_state_free(state);
}

/* ============================================================================
 * Bridge invoke
 * ============================================================================
 */

TEST(filter_invoke_prefix_match_when_no_fn_registered) {
    /// With no shell predicate (standalone liblle, early init) the invoker
    /// falls back to an NFC prefix match rather than admitting everything -- so
    /// the first-word sources that gate on it stay prefix-scoped instead of
    /// emitting the whole PATH / every builtin.
    lle_completion_set_filter_fn(NULL);
    ASSERT_TRUE(lle_completion_filter_invoke("ev", "everything"),
                "no predicate registered -> prefix match admits a prefix");
    ASSERT_FALSE(lle_completion_filter_invoke("anything", "everything"),
                 "no predicate registered -> a non-prefix is rejected");
    ASSERT_TRUE(lle_completion_filter_invoke("", "everything"),
                "empty prefix still admits all");
    /// Restore for downstream tests.
    lle_completion_set_filter_fn(test_filter_fn);
}

TEST(filter_invoke_rejects_null_candidate) {
    ASSERT_FALSE(lle_completion_filter_invoke("p", NULL),
                 "NULL candidate is rejected");
    ASSERT_FALSE(lle_completion_filter_invoke("p", ""),
                 "empty candidate is rejected");
}

TEST(filter_invoke_admits_when_prefix_empty) {
    ASSERT_TRUE(lle_completion_filter_invoke("", "candidate"),
                "empty prefix admits");
    ASSERT_TRUE(lle_completion_filter_invoke(NULL, "candidate"),
                "NULL prefix admits");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    global_setup();

    printf("Running menu type-to-filter tests\n");
    printf("=================================\n");

    printf("\nFilter lifecycle:\n");
    RUN_TEST(filter_inactive_after_create);
    RUN_TEST(filter_active_after_first_char);
    RUN_TEST(filter_narrows_further_with_more_chars);
    RUN_TEST(filter_pop_widens_view);
    RUN_TEST(filter_pop_on_empty_is_noop);
    RUN_TEST(filter_no_matches_keeps_filter_active_with_zero_results);
    RUN_TEST(filter_uses_original_prefix_when_provided);

    printf("\nBridge invoke:\n");
    RUN_TEST(filter_invoke_prefix_match_when_no_fn_registered);
    RUN_TEST(filter_invoke_rejects_null_candidate);
    RUN_TEST(filter_invoke_admits_when_prefix_empty);

    int rc = TEST_RESULT();
    global_teardown();
    return rc;
}
