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
static int saved_fuzzy_min_chars;

static void config_snapshot(void) {
    saved_mode = config.completion_match_mode;
    saved_case_sensitive = config.completion_case_sensitive;
    saved_threshold = config.completion_threshold;
    saved_fuzzy_min_chars = config.completion_fuzzy_min_chars;
    /// Default to no floor unless a test sets one, so the existing mode tests
    /// (single-char prefixes in fuzzy mode) are unaffected.
    config.completion_fuzzy_min_chars = 0;
}

static void config_restore(void) {
    config.completion_match_mode = saved_mode;
    config.completion_case_sensitive = saved_case_sensitive;
    config.completion_threshold = saved_threshold;
    config.completion_fuzzy_min_chars = saved_fuzzy_min_chars;
}

/* ============================================================================
 * Empty / null edge cases (mode-independent)
 * ============================================================================
 */

TEST(filter_matches_across_normalization_forms) {
    /// A user types the text they see; the candidate came from the filesystem,
    /// which may store the other normalization form. All three match modes must
    /// answer the same question the same way regardless of spelling.
    ///
    /// \xc3\xa9 is precomposed e-acute; e + \xcc\x81 is the decomposed pair.
    /// Both render identically, so a user cannot tell which they typed.
    config_snapshot();
    const char *nfc = "caf\xc3\xa9-file";
    const char *nfd = "cafe\xcc\x81-file";

    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_TRUE(completion_filter_admits("cafe\xcc\x81", nfc),
                "prefix: decomposed query must match a precomposed candidate");
    ASSERT_TRUE(completion_filter_admits("caf\xc3\xa9", nfd),
                "prefix: precomposed query must match a decomposed candidate");

    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;
    ASSERT_TRUE(
        completion_filter_admits("cafe\xcc\x81", nfc),
        "substring: decomposed query must match a precomposed candidate");

    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_threshold = 0;
    config.completion_fuzzy_min_chars = 0;
    ASSERT_TRUE(completion_filter_admits("cafe\xcc\x81", nfc),
                "fuzzy: decomposed query must match a precomposed candidate");
    ASSERT_TRUE(completion_filter_admits("caf\xc3\xa9", nfd),
                "fuzzy: precomposed query must match a decomposed candidate");
    config_restore();
}

TEST(filter_prefix_does_not_end_inside_a_character) {
    /// "cafe" is a prefix of the decomposed candidate's BYTES, but it would end
    /// between the base and its combining mark -- half a character. The prefix
    /// test refuses, which is the same rule pattern matching settled on: a
    /// literal matches exact text, and a cluster's base is not the cluster.
    ///
    /// This is the behavior history prefix search does NOT have (issue #775);
    /// completion is the precedent it should follow.
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_TRUE(!completion_filter_admits("cafe", "cafe\xcc\x81-file"),
                "prefix must not match a candidate mid-cluster");
    ASSERT_TRUE(completion_filter_admits("caf", "cafe\xcc\x81-file"),
                "a prefix ending ON a boundary still matches");
    config_restore();
}

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

/* ============================================================================
 * First-word widening: fuzzy admits a strong non-prefix candidate (the gap
 * this fix closes -- the engine sources now route through this predicate).
 * ============================================================================
 */

TEST(filter_fuzzy_widens_beyond_prefix) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_threshold = 60;
    config.completion_case_sensitive = false;

    /// "gco" is not a prefix of "git-config" but is a strong fuzzy match --
    /// exactly the candidate prefix matching misses and fuzzy now surfaces.
    ASSERT_TRUE(completion_filter_admits("gco", "git-config"),
                "fuzzy admits a strong non-prefix subsequence");
    ASSERT_FALSE(completion_filter_admits("gco", "zoo"),
                 "fuzzy still rejects a non-subsequence");

    /// Prefix mode would NOT admit it, proving the mode is what widens.
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_FALSE(completion_filter_admits("gco", "git-config"),
                 "prefix mode rejects the non-prefix candidate");
    config_restore();
}

TEST(filter_fuzzy_threshold_zero_still_rejects_non_match) {
    config_snapshot();
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_case_sensitive = false;
    config.completion_threshold =
        0; /// maximally permissive, but still a filter

    /// A genuine subsequence match floors at score 1, a non-subsequence scores
    /// 0. At threshold 0 a naive `score >= threshold` would admit every
    /// non-match (0 >= 0) and -- since the rescore pass only ranks score > 0
    /// items -- leave that junk at its high static relevance, sorting it above
    /// the real fuzzy hits. The filter must reject a non-subsequence at any
    /// threshold.
    ASSERT_FALSE(completion_filter_admits("gco", "zoo"),
                 "threshold 0 still rejects a non-subsequence");
    ASSERT_TRUE(completion_filter_admits("gco", "git-config"),
                "threshold 0 admits a genuine fuzzy match");
    config_restore();
}

/* ============================================================================
 * Short-prefix floor: below completion.fuzzy_min_chars, substring/fuzzy fall
 * back to prefix matching so 1-char input does not widen.
 * ============================================================================
 */

TEST(filter_short_prefix_floor_falls_back_to_prefix) {
    config_snapshot();
    config.completion_case_sensitive = false;
    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;

    /// With a floor of 2, a 1-char prefix is prefix-scoped: "a" inside "cat"
    /// is a substring but not a prefix, so the floor rejects it.
    config.completion_fuzzy_min_chars = 2;
    ASSERT_FALSE(completion_filter_admits("a", "cat"),
                 "1-char prefix below the floor stays prefix-scoped");
    ASSERT_TRUE(completion_filter_admits("c", "cat"),
                "a genuine 1-char prefix still admits");

    /// Two characters is at the floor, so substring widening resumes.
    ASSERT_TRUE(completion_filter_admits("at", "cat"),
                "2-char prefix at the floor widens (substring)");

    /// Disabling the floor (0/1) lets the 1-char substring through again.
    config.completion_fuzzy_min_chars = 1;
    ASSERT_TRUE(completion_filter_admits("a", "cat"),
                "floor of 1 disables the guard -- 1-char substring widens");
    config_restore();
}

/* ============================================================================
 * Ranking score: fuzzy mode reports a match score; other modes report 0
 * ("keep static order"), as does a below-floor prefix.
 * ============================================================================
 */

TEST(filter_score_ranks_only_in_fuzzy) {
    config_snapshot();
    config.completion_case_sensitive = false;
    config.completion_threshold = 60;
    config.completion_fuzzy_min_chars = 2;

    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    ASSERT_TRUE(completion_filter_score("gco", "gconfig") > 0,
                "fuzzy mode scores a match for ranking");
    /// A stronger match outscores a weaker one (drives best-first ordering).
    ASSERT_TRUE(completion_filter_score("gco", "gconfig") >
                    completion_filter_score("gco", "git-cleanup-tool-old"),
                "a closer fuzzy match scores higher");

    /// Prefix/substring do not rank: 0 keeps the source's static order.
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    ASSERT_EQ(completion_filter_score("gco", "gconfig"), 0,
              "prefix mode does not rank");
    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;
    ASSERT_EQ(completion_filter_score("gco", "gconfig"), 0,
              "substring mode does not rank");

    /// Below the floor, fuzzy does not rank either (it acts as prefix).
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    ASSERT_EQ(completion_filter_score("g", "gconfig"), 0,
              "a below-floor prefix does not rank");
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
    RUN_TEST(filter_matches_across_normalization_forms);
    RUN_TEST(filter_prefix_does_not_end_inside_a_character);
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
    RUN_TEST(filter_fuzzy_widens_beyond_prefix);
    RUN_TEST(filter_fuzzy_threshold_zero_still_rejects_non_match);
    RUN_TEST(filter_short_prefix_floor_falls_back_to_prefix);
    RUN_TEST(filter_score_ranks_only_in_fuzzy);

    return TEST_RESULT();
}
