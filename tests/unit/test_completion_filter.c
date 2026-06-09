/**
 * @file test_completion_filter.c
 * @brief Unit tests for completion_filter_admits.
 *
 * Exercises the canonical filter predicate across all three
 * completion.match_mode values. The bridge filter and (subsequent
 * commit) the in-menu type-to-filter both consume this function;
 * regressions here are visible as candidate ranking drift in the
 * completion menu.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "completion_filter.h"
#include "config.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern config_values_t config;
extern void init_symtable(void);

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Snapshot + restore guards so each test gets the configuration it
/// expects without persisting changes across tests. Match-mode and
/// case-sensitivity are the two knobs the filter consults; threshold
/// matters only for fuzzy mode but is restored too for completeness.
static completion_match_mode_t saved_mode;
static bool saved_case_sensitive;
static int saved_threshold;

static void config_snapshot(void) {
    saved_mode = config.completion_match_mode;
    saved_case_sensitive = config.completion_case_sensitive;
    saved_threshold = config.completion_threshold;
}

static void config_restore(void) {
    config.completion_match_mode = saved_mode;
    config.completion_case_sensitive = saved_case_sensitive;
    config.completion_threshold = saved_threshold;
}

/* ============================================================================
 * Empty / null edge cases (mode-independent)
 * ============================================================================
 */

TEST(filter_empty_prefix_admits_everything) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_TRUE(completion_filter_admits("", "anything"),
                "empty prefix accepts any candidate");
    ASSERT_TRUE(completion_filter_admits(NULL, "anything"),
                "NULL prefix accepts any candidate");
    config_restore();
}

TEST(filter_null_or_empty_candidate_rejected) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_FALSE(completion_filter_admits("prefix", NULL),
                 "NULL candidate is rejected");
    ASSERT_FALSE(completion_filter_admits("prefix", ""),
                 "empty candidate is rejected");
    config_restore();
}

/* ============================================================================
 * Prefix mode
 * ============================================================================
 */

TEST(filter_prefix_mode_admits_prefix_matches) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    config.completion_case_sensitive = false;
    ASSERT_TRUE(completion_filter_admits("git", "git checkout"),
                "prefix mode admits a prefix match");
    ASSERT_TRUE(completion_filter_admits("c", "checkout"),
                "single-char prefix matches");
    ASSERT_FALSE(completion_filter_admits("checkout", "git checkout"),
                 "prefix mode rejects substring-only matches");
    config_restore();
}

TEST(filter_prefix_mode_case_insensitive_by_default) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    config.completion_case_sensitive = false;
    ASSERT_TRUE(completion_filter_admits("GIT", "git checkout"),
                "case-insensitive prefix mode admits cross-case match");
    config_restore();
}

TEST(filter_prefix_mode_case_sensitive_when_configured) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    config.completion_case_sensitive = true;
    ASSERT_FALSE(completion_filter_admits("GIT", "git checkout"),
                 "case-sensitive prefix mode rejects cross-case match");
    ASSERT_TRUE(completion_filter_admits("git", "git checkout"),
                "case-sensitive prefix mode still admits exact-case match");
    config_restore();
}

/* ============================================================================
 * Substring mode
 * ============================================================================
 */

TEST(filter_substring_mode_admits_internal_match) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;
    ASSERT_TRUE(completion_filter_admits("check", "git checkout"),
                "substring mode admits internal match");
    ASSERT_TRUE(completion_filter_admits("git", "git checkout"),
                "substring mode admits a prefix match too");
    ASSERT_TRUE(completion_filter_admits("out", "git checkout"),
                "substring mode admits a suffix match too");
    ASSERT_FALSE(completion_filter_admits("xyz", "git checkout"),
                 "substring mode rejects non-occurrence");
    config_restore();
}

TEST(filter_substring_mode_case_insensitive_by_default) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;
    config.completion_case_sensitive = false;
    ASSERT_TRUE(completion_filter_admits("CHECK", "git checkout"),
                "case-insensitive substring admits cross-case match");
    config_restore();
}

/* ============================================================================
 * Fuzzy mode
 * ============================================================================
 */

TEST(filter_fuzzy_mode_admits_subsequence) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_threshold = 1; /// floor so any subsequence wins
    ASSERT_TRUE(completion_filter_admits("gco", "git checkout"),
                "fuzzy mode admits a subsequence match");
    ASSERT_TRUE(completion_filter_admits("gco", "gcc -o file"),
                "fuzzy mode admits any subsequence match, not just one");
    config_restore();
}

TEST(filter_fuzzy_mode_rejects_non_subsequence) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_threshold = 1;
    ASSERT_FALSE(completion_filter_admits("xyz", "git checkout"),
                 "fuzzy mode rejects non-subsequence (subsequence gate)");
    config_restore();
}

TEST(filter_fuzzy_mode_honors_threshold) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    /// 99 forces only near-exact matches through. The earlier
    /// completion_score tests show "gco" against "git checkout"
    /// lands well below 90; threshold 99 must reject.
    config.completion_threshold = 99;
    ASSERT_FALSE(completion_filter_admits("gco", "git checkout"),
                 "threshold 99 gates a mid-strength fuzzy match");
    /// Exact match should still pass at threshold 99 (score 100).
    ASSERT_TRUE(completion_filter_admits("git", "git"),
                "exact match passes any non-100 threshold");
    config_restore();
}

/* ============================================================================
 * Mode independence: changing mode without restart changes behavior
 * ============================================================================
 */

TEST(filter_changing_mode_changes_admission) {
    config_snapshot();
    config.completion_threshold = 1;
    config.completion_case_sensitive = false;

    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_FALSE(completion_filter_admits("check", "git checkout"),
                 "prefix mode rejects mid-word match");

    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;
    ASSERT_TRUE(completion_filter_admits("check", "git checkout"),
                "substring mode admits the same input after switch");

    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    ASSERT_TRUE(completion_filter_admits("check", "git checkout"),
                "fuzzy mode admits the same input after switch");
    config_restore();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /// The filter reads the central config, which assumes
    /// config_init has run and the symtable manager is online.
    init_symtable();
    config_init();

    printf("Running completion_filter tests\n");
    printf("===============================\n");

    printf("\nEdge cases:\n");
    RUN_TEST(filter_empty_prefix_admits_everything);
    RUN_TEST(filter_null_or_empty_candidate_rejected);

    printf("\nPrefix mode:\n");
    RUN_TEST(filter_prefix_mode_admits_prefix_matches);
    RUN_TEST(filter_prefix_mode_case_insensitive_by_default);
    RUN_TEST(filter_prefix_mode_case_sensitive_when_configured);

    printf("\nSubstring mode:\n");
    RUN_TEST(filter_substring_mode_admits_internal_match);
    RUN_TEST(filter_substring_mode_case_insensitive_by_default);

    printf("\nFuzzy mode:\n");
    RUN_TEST(filter_fuzzy_mode_admits_subsequence);
    RUN_TEST(filter_fuzzy_mode_rejects_non_subsequence);
    RUN_TEST(filter_fuzzy_mode_honors_threshold);

    printf("\nMode switching:\n");
    RUN_TEST(filter_changing_mode_changes_admission);

    return TEST_RESULT();
}
