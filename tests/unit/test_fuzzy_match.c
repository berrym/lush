/**
 * @file test_fuzzy_match.c
 * @brief Unit tests for fuzzy string matching library
 *
 * Tests the fuzzy matching module including:
 * - Levenshtein distance calculation
 * - Damerau-Levenshtein distance calculation
 * - Jaro and Jaro-Winkler similarity scores
 * - Common prefix length
 * - Subsequence matching
 * - Combined fuzzy match scoring
 * - Batch matching operations
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "fuzzy_match.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Test framework macros

/* ============================================================================
 * LEVENSHTEIN DISTANCE TESTS
 * ============================================================================
 */

TEST(levenshtein_identical) {
    int dist = fuzzy_levenshtein_distance("hello", "hello", NULL);
    ASSERT_EQ(dist, 0, "identical strings should have distance 0");
}

TEST(levenshtein_empty_strings) {
    int dist1 = fuzzy_levenshtein_distance("", "", NULL);
    ASSERT_EQ(dist1, 0, "two empty strings should have distance 0");

    int dist2 = fuzzy_levenshtein_distance("abc", "", NULL);
    ASSERT_EQ(dist2, 3, "string to empty should be string length");

    int dist3 = fuzzy_levenshtein_distance("", "abc", NULL);
    ASSERT_EQ(dist3, 3, "empty to string should be string length");
}

TEST(levenshtein_single_char_diff) {
    /// Substitution
    int dist1 = fuzzy_levenshtein_distance("cat", "bat", NULL);
    ASSERT_EQ(dist1, 1, "one substitution should be distance 1");

    /// Insertion
    int dist2 = fuzzy_levenshtein_distance("cat", "cart", NULL);
    ASSERT_EQ(dist2, 1, "one insertion should be distance 1");

    /// Deletion
    int dist3 = fuzzy_levenshtein_distance("cart", "cat", NULL);
    ASSERT_EQ(dist3, 1, "one deletion should be distance 1");
}

TEST(levenshtein_multiple_edits) {
    int dist = fuzzy_levenshtein_distance("kitten", "sitting", NULL);
    ASSERT_EQ(dist, 3, "kitten->sitting should be distance 3");
}

TEST(levenshtein_case_sensitive) {
    int dist_default = fuzzy_levenshtein_distance("Hello", "hello", NULL);
    ASSERT_EQ(dist_default, 0, "default is case insensitive");

    int dist_strict =
        fuzzy_levenshtein_distance("Hello", "hello", &FUZZY_MATCH_STRICT);
    ASSERT_EQ(dist_strict, 1, "strict mode is case sensitive");
}

/* ============================================================================
 * DAMERAU-LEVENSHTEIN DISTANCE TESTS
 * ============================================================================
 */

TEST(damerau_identical) {
    int dist = fuzzy_damerau_levenshtein_distance("hello", "hello", NULL);
    ASSERT_EQ(dist, 0, "identical strings should have distance 0");
}

TEST(damerau_transposition) {
    /// Transposition should count as 1 edit, not 2
    int dist = fuzzy_damerau_levenshtein_distance("ab", "ba", NULL);
    ASSERT_EQ(dist, 1, "simple transposition should be distance 1");
}

TEST(damerau_typo_correction) {
    /// Common typo: "teh" -> "the"
    int dist = fuzzy_damerau_levenshtein_distance("teh", "the", NULL);
    ASSERT_EQ(dist, 1, "transposition typo should be distance 1");
}

TEST(damerau_vs_levenshtein) {
    /// Damerau should give lower distance for transpositions
    int damerau = fuzzy_damerau_levenshtein_distance("ab", "ba", NULL);
    int levenshtein = fuzzy_levenshtein_distance("ab", "ba", NULL);

    ASSERT_LE(damerau, levenshtein,
              "Damerau should be <= Levenshtein for transpositions");
}

/* ============================================================================
 * JARO SCORE TESTS
 * ============================================================================
 */

TEST(jaro_identical) {
    int score = fuzzy_jaro_score("hello", "hello", NULL);
    ASSERT_EQ(score, 100, "identical strings should have score 100");
}

TEST(jaro_completely_different) {
    int score = fuzzy_jaro_score("abc", "xyz", NULL);
    ASSERT_EQ(score, 0, "completely different strings should have score 0");
}

TEST(jaro_partial_match) {
    int score = fuzzy_jaro_score("martha", "marhta", NULL);
    ASSERT_GE(score, 80, "similar strings should have high Jaro score");
}

TEST(jaro_empty_strings) {
    int score1 = fuzzy_jaro_score("", "", NULL);
    ASSERT_EQ(score1, 100, "two empty strings should have score 100");

    int score2 = fuzzy_jaro_score("abc", "", NULL);
    ASSERT_EQ(score2, 0, "one empty string should have score 0");
}

/* ============================================================================
 * JARO-WINKLER SCORE TESTS
 * ============================================================================
 */

TEST(jaro_winkler_identical) {
    int score = fuzzy_jaro_winkler_score("hello", "hello", NULL);
    ASSERT_EQ(score, 100, "identical strings should have score 100");
}

TEST(jaro_winkler_prefix_bonus) {
    /// Jaro-Winkler should give bonus for common prefix
    int jaro = fuzzy_jaro_score("prefix", "prefox", NULL);
    int jaro_winkler = fuzzy_jaro_winkler_score("prefix", "prefox", NULL);

    ASSERT_GE(jaro_winkler, jaro,
              "Jaro-Winkler should be >= Jaro for strings with common prefix");
}

TEST(jaro_winkler_command_names) {
    /// Good for command names like "gitcommit" vs "git-commit"
    int score = fuzzy_jaro_winkler_score("gitcommit", "git-commit", NULL);
    ASSERT_GE(score, 70, "similar command names should have high score");
}

/* ============================================================================
 * COMMON PREFIX LENGTH TESTS
 * ============================================================================
 */

TEST(common_prefix_identical) {
    int len = fuzzy_common_prefix_length("hello", "hello", NULL);
    ASSERT_EQ(len, 5, "identical strings should have full prefix length");
}

TEST(common_prefix_partial) {
    int len = fuzzy_common_prefix_length("prefix", "presto", NULL);
    ASSERT_EQ(len, 3, "common prefix should be 'pre' = 3");
}

TEST(common_prefix_none) {
    int len = fuzzy_common_prefix_length("abc", "xyz", NULL);
    ASSERT_EQ(len, 0, "no common prefix should return 0");
}

TEST(common_prefix_empty) {
    int len1 = fuzzy_common_prefix_length("", "hello", NULL);
    ASSERT_EQ(len1, 0, "empty string should have prefix 0");

    int len2 = fuzzy_common_prefix_length("hello", "", NULL);
    ASSERT_EQ(len2, 0, "empty string should have prefix 0");
}

TEST(common_prefix_case_sensitive) {
    int len_default = fuzzy_common_prefix_length("Hello", "hello", NULL);
    ASSERT_EQ(len_default, 5, "default is case insensitive");

    int len_strict =
        fuzzy_common_prefix_length("Hello", "hello", &FUZZY_MATCH_STRICT);
    ASSERT_EQ(len_strict, 0, "strict mode is case sensitive");
}

/* ============================================================================
 * SUBSEQUENCE MATCHING TESTS
 * ============================================================================
 */

TEST(is_subsequence_true) {
    bool result = fuzzy_is_subsequence("gco", "git checkout", NULL);
    ASSERT_TRUE(result, "gco is subsequence of 'git checkout'");
}

TEST(is_subsequence_false) {
    bool result = fuzzy_is_subsequence("xyz", "abc", NULL);
    ASSERT_FALSE(result, "xyz is not subsequence of abc");
}

TEST(is_subsequence_identical) {
    bool result = fuzzy_is_subsequence("hello", "hello", NULL);
    ASSERT_TRUE(result, "string is subsequence of itself");
}

TEST(is_subsequence_empty_pattern) {
    bool result = fuzzy_is_subsequence("", "hello", NULL);
    ASSERT_TRUE(result, "empty pattern is subsequence of any string");
}

TEST(is_subsequence_score) {
    int score1 = fuzzy_subsequence_score("gco", "git checkout", NULL);
    ASSERT_GE(score1, 50, "good subsequence match should have decent score");

    int score2 = fuzzy_subsequence_score("gc", "git checkout", NULL);
    ASSERT_GE(score2, 30, "partial subsequence should have some score");
}

/* ============================================================================
 * COMBINED MATCH SCORE TESTS
 * ============================================================================
 */

TEST(match_score_identical) {
    int score = fuzzy_match_score("hello", "hello", NULL);
    ASSERT_EQ(score, 100, "identical strings should have score 100");
}

TEST(match_score_similar) {
    int score = fuzzy_match_score("kitten", "sitting", NULL);
    ASSERT_GE(score, 40, "similar strings should have reasonable score");
    ASSERT_LE(score, 80, "similar strings shouldn't score too high");
}

TEST(match_score_different) {
    int score = fuzzy_match_score("abc", "xyz", NULL);
    ASSERT_LE(score, 30, "completely different strings should have low score");
}

TEST(match_score_empty) {
    int score1 = fuzzy_match_score("", "", NULL);
    ASSERT_EQ(score1, 100, "two empty strings should have score 100");

    int score2 = fuzzy_match_score("hello", "", NULL);
    ASSERT_EQ(score2, 0, "string vs empty should have score 0");
}

TEST(match_score_case_insensitive) {
    int score = fuzzy_match_score("HELLO", "hello", NULL);
    ASSERT_EQ(score, 100, "case should not matter by default");
}

TEST(match_score_case_sensitive) {
    int score = fuzzy_match_score("HELLO", "hello", &FUZZY_MATCH_STRICT);
    ASSERT_LE(score, 90, "strict mode should penalize case differences");
}

/* ============================================================================
 * IS MATCH THRESHOLD TESTS
 * ============================================================================
 */

TEST(is_match_above_threshold) {
    bool result = fuzzy_match_is_match("hello", "hello", 90, NULL);
    ASSERT_TRUE(result, "identical strings should pass any threshold");
}

TEST(is_match_below_threshold) {
    bool result = fuzzy_match_is_match("abc", "xyz", 50, NULL);
    ASSERT_FALSE(result, "completely different strings should fail threshold");
}

TEST(is_match_threshold_boundary) {
    /// Test with identical strings - should always pass
    bool result1 = fuzzy_match_is_match("hello", "hello", 95, NULL);
    ASSERT_TRUE(result1, "identical strings should pass high threshold");

    /// Test with slightly different strings
    bool result2 = fuzzy_match_is_match("hello", "hallo", 60, NULL);
    ASSERT_TRUE(result2, "similar strings should pass moderate threshold");
}

/* ============================================================================
 * BATCH MATCHING TESTS
 * ============================================================================
 */

TEST(match_best_basic) {
    const char *candidates[] = {"hello", "help", "world", "helm", "hero"};
    fuzzy_match_result_t results[3];

    int count = fuzzy_match_best("hel", candidates, 5, results, 3, 0, NULL);

    ASSERT_GE(count, 1, "should find at least one match");
    ASSERT_LE(count, 3, "should not exceed max_results");

    /// Results should be sorted by score (highest first)
    if (count >= 2) {
        ASSERT_GE(results[0].score, results[1].score,
                  "results should be sorted by score descending");
    }
}

TEST(match_best_with_threshold) {
    const char *candidates[] = {"hello", "world", "xyz"};
    fuzzy_match_result_t results[3];

    int count = fuzzy_match_best("hello", candidates, 3, results, 3, 80, NULL);

    /// Only "hello" should match with threshold 80
    ASSERT_GE(count, 1, "should find exact match");

    /// All results should be above threshold
    for (int i = 0; i < count; i++) {
        ASSERT_GE(results[i].score, 80,
                  "all results should be above threshold");
    }
}

TEST(match_filter_basic) {
    const char *candidates[] = {"hello", "help", "world", "helm"};
    int indices[4];

    int count = fuzzy_match_filter("hel", candidates, 4, indices, 4, 50, NULL);

    ASSERT_GE(count, 1, "should find at least one match");

    /// All indices should be valid
    for (int i = 0; i < count; i++) {
        ASSERT_GE(indices[i], 0, "index should be non-negative");
        ASSERT_LE(indices[i], 3, "index should be within bounds");
    }
}

/* ============================================================================
 * UTILITY FUNCTION TESTS
 * ============================================================================
 */

TEST(distance_to_score_zero) {
    int score = fuzzy_distance_to_score(0, 5);
    ASSERT_EQ(score, 100, "distance 0 should give score 100");
}

TEST(distance_to_score_full) {
    int score = fuzzy_distance_to_score(5, 5);
    ASSERT_EQ(score, 0, "distance = length should give score 0");
}

TEST(distance_to_score_half) {
    int score = fuzzy_distance_to_score(2, 4);
    ASSERT_EQ(score, 50, "distance = half length should give score 50");
}

TEST(string_length_ascii) {
    int len = fuzzy_string_length("hello", NULL);
    ASSERT_EQ(len, 5, "ASCII string length should be character count");
}

TEST(string_length_empty) {
    int len = fuzzy_string_length("", NULL);
    ASSERT_EQ(len, 0, "empty string length should be 0");
}

/* ============================================================================
 * OPTIONS PRESET TESTS
 * ============================================================================
 */

TEST(options_default) {
    ASSERT_FALSE(FUZZY_MATCH_DEFAULT.case_sensitive,
                 "default should be case insensitive");
    ASSERT_TRUE(FUZZY_MATCH_DEFAULT.unicode_normalize,
                "default should normalize unicode");
    ASSERT_TRUE(FUZZY_MATCH_DEFAULT.use_damerau,
                "default should use Damerau-Levenshtein");
}

TEST(options_strict) {
    ASSERT_TRUE(FUZZY_MATCH_STRICT.case_sensitive,
                "strict should be case sensitive");
    ASSERT_FALSE(FUZZY_MATCH_STRICT.unicode_normalize,
                 "strict should not normalize unicode");
    ASSERT_FALSE(FUZZY_MATCH_STRICT.use_damerau,
                 "strict should not use Damerau");
}

TEST(options_fast) {
    ASSERT_FALSE(FUZZY_MATCH_FAST.case_sensitive,
                 "fast should be case insensitive");
    ASSERT_FALSE(FUZZY_MATCH_FAST.unicode_normalize,
                 "fast should not normalize unicode");
    ASSERT_FALSE(FUZZY_MATCH_FAST.use_damerau, "fast should not use Damerau");
}

/* ============================================================================
 * COMPLETION SCORE TESTS (fzy-style)
 * ============================================================================
 */

TEST(completion_empty_pattern_matches_all) {
    /// Empty pattern = no-filter sentinel; every candidate gets 100.
    ASSERT_EQ(fuzzy_completion_score("", "git checkout", NULL), 100,
              "empty pattern returns 100");
    ASSERT_EQ(fuzzy_completion_score("", "", NULL), 100,
              "empty/empty returns 100");
}

TEST(completion_empty_text_zero) {
    ASSERT_EQ(fuzzy_completion_score("abc", "", NULL), 0,
              "empty candidate scores 0");
}

TEST(completion_exact_match_perfect) {
    ASSERT_EQ(fuzzy_completion_score("git", "git", NULL), 100,
              "identical pattern/text scores 100");
}

TEST(completion_case_insensitive_default) {
    /// Default options are case-insensitive; the same input scored
    /// against uppercase candidate should match.
    int s = fuzzy_completion_score("gco", "GIT CHECKOUT", NULL);
    ASSERT_TRUE(s > 0, "case-insensitive default scores a real match");
}

TEST(completion_non_subsequence_returns_zero) {
    /// `xyz` is not a subsequence of `git checkout`; the subsequence
    /// gate must return 0 (no partial credit for unrelated chars).
    ASSERT_EQ(fuzzy_completion_score("xyz", "git checkout", NULL), 0,
              "non-subsequence scores 0");
}

TEST(completion_subsequence_order_required) {
    /// `gco` IS a subsequence of `git checkout`; `cog` is NOT (c then
    /// o appear in order but g comes before c in the text). This is
    /// the core gate that fuzzy_match_score lacks.
    ASSERT_TRUE(fuzzy_completion_score("gco", "git checkout", NULL) > 0,
                "gco is a subsequence of 'git checkout'");
    ASSERT_EQ(fuzzy_completion_score("cog", "git checkout", NULL), 0,
              "cog is NOT a subsequence of 'git checkout'");
}

TEST(completion_first_char_bonus_outranks_mid_word) {
    /// Pattern matching at text[0] should outrank the same pattern
    /// matching only in the middle. `ls` vs `ls` at start vs `tools`
    /// (matches mid-word).
    int leading = fuzzy_completion_score("ls", "ls", NULL);
    int internal = fuzzy_completion_score("ls", "tools", NULL);
    ASSERT_TRUE(leading > internal, "first-char match outranks mid-word match");
}

TEST(completion_word_boundary_bonus_outranks_internal) {
    /// `c` matching after the space in `git checkout` (word boundary)
    /// should outrank `c` matching inside `gcc -o file` (the `c` in
    /// `gcc` is at position 2, inside the word).
    int boundary = fuzzy_completion_score("c", "git checkout", NULL);
    int internal = fuzzy_completion_score("c", "gcc -o file", NULL);
    ASSERT_TRUE(boundary > internal,
                "word-boundary match outranks mid-word match");
}

TEST(completion_diagnosis_case_keeps_candidates_above_zero) {
    /// The diagnosis case that motivated this scorer. User types
    /// `gco`; libfuzzy's autocorrect-tuned `fuzzy_match_score()`
    /// ranks both `git checkout` and `gcc -o file` BELOW the default
    /// threshold of 60 (edit-distance weight dominates), so both are
    /// dropped from the candidate list entirely. The completion
    /// scorer keeps both candidates positive so the user sees their
    /// options and can pick.
    ///
    /// Note: fzy-style scoring legitimately prefers `gcc -o file`
    /// (g+c adjacency at positions 0,1 earns the consecutive bonus)
    /// over `git checkout` (g and c scattered). That is fzy's
    /// behavior, not a bug. Acronym-matching ("first letter of each
    /// whitespace-separated token") is a separate scoring axis that
    /// would invert this ranking; it's tracked as a future
    /// enhancement on top of the base fzy algorithm.
    int checkout = fuzzy_completion_score("gco", "git checkout", NULL);
    int gcc_o = fuzzy_completion_score("gco", "gcc -o file", NULL);
    ASSERT_TRUE(checkout > 0,
                "git checkout matches (autocorrect blend would drop it)");
    ASSERT_TRUE(gcc_o > 0,
                "gcc -o file matches (autocorrect blend would drop it)");
}

TEST(completion_camel_case_boundary) {
    /// CamelCase boundary: `gc` matching `GitCommit` should score
    /// well even though the chars are mid-text -- the `C` is a
    /// camel-case word start.
    int camel = fuzzy_completion_score("gc", "GitCommit", NULL);
    int internal = fuzzy_completion_score("gc", "wagecmd", NULL);
    ASSERT_TRUE(camel > internal, "CamelCase boundary outranks mid-word match");
}

TEST(completion_adjacency_bonus) {
    /// Adjacent matches stack better than scattered ones. `co`
    /// adjacent in `coup` should outrank `co` scattered in `cargo`.
    int adjacent = fuzzy_completion_score("co", "coup", NULL);
    int scattered = fuzzy_completion_score("co", "cargo", NULL);
    ASSERT_TRUE(adjacent > scattered,
                "adjacent match outranks scattered match");
}

TEST(completion_threshold_distinguishes_strong_from_weak) {
    /// A strong match (prefix + adjacency) should land high; a weak
    /// match (scattered across many gaps) should land low. The
    /// configured completion.threshold (default 60) sits between them
    /// so users get meaningful filter behavior out of the box.
    int strong = fuzzy_completion_score("git", "git", NULL);
    int weak = fuzzy_completion_score("git", "gargantuan-inner-thing", NULL);
    ASSERT_TRUE(strong >= 60, "strong match scores >= 60");
    ASSERT_TRUE(weak < strong, "weak match scores below strong");
    ASSERT_TRUE(weak > 0, "weak match still positive (subsequence holds)");
}

TEST(completion_pattern_longer_than_text_zero) {
    /// Edge: pattern can never be a subsequence of shorter text.
    ASSERT_EQ(fuzzy_completion_score("hello world", "hi", NULL), 0,
              "pattern longer than text scores 0");
}

TEST(completion_unicode_nfc_normalization) {
    /// Default options normalize to NFC, so a precomposed and a
    /// decomposed form of the same string match identically.
    const char *precomposed = "caf\xc3\xa9"; // café (U+00E9)
    const char *decomposed = "cafe\xcc\x81"; // cafe + COMBINING ACUTE
    int s_pre = fuzzy_completion_score("caf", precomposed, NULL);
    int s_dec = fuzzy_completion_score("caf", decomposed, NULL);
    ASSERT_TRUE(s_pre > 0 && s_dec > 0,
                "both NFC forms produce positive matches");
    ASSERT_EQ(s_pre, s_dec, "NFC-equivalent forms score identically");
}

TEST(completion_case_sensitive_strict) {
    /// Strict options preserve case; `LS` should not subsequence-match
    /// `ls` under strict comparison.
    ASSERT_EQ(fuzzy_completion_score("LS", "ls", &FUZZY_MATCH_STRICT), 0,
              "strict case-sensitive blocks the cross-case match");
    ASSERT_TRUE(fuzzy_completion_score("LS", "ls", NULL) > 0,
                "default case-insensitive permits the match");
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Fuzzy Match tests...\n");

    /// Levenshtein distance tests
    printf("\n=== Levenshtein Distance Tests ===\n");
    RUN_TEST(levenshtein_identical);
    RUN_TEST(levenshtein_empty_strings);
    RUN_TEST(levenshtein_single_char_diff);
    RUN_TEST(levenshtein_multiple_edits);
    RUN_TEST(levenshtein_case_sensitive);

    /// Damerau-Levenshtein distance tests
    printf("\n=== Damerau-Levenshtein Distance Tests ===\n");
    RUN_TEST(damerau_identical);
    RUN_TEST(damerau_transposition);
    RUN_TEST(damerau_typo_correction);
    RUN_TEST(damerau_vs_levenshtein);

    /// Jaro score tests
    printf("\n=== Jaro Score Tests ===\n");
    RUN_TEST(jaro_identical);
    RUN_TEST(jaro_completely_different);
    RUN_TEST(jaro_partial_match);
    RUN_TEST(jaro_empty_strings);

    /// Jaro-Winkler score tests
    printf("\n=== Jaro-Winkler Score Tests ===\n");
    RUN_TEST(jaro_winkler_identical);
    RUN_TEST(jaro_winkler_prefix_bonus);
    RUN_TEST(jaro_winkler_command_names);

    /// Common prefix length tests
    printf("\n=== Common Prefix Length Tests ===\n");
    RUN_TEST(common_prefix_identical);
    RUN_TEST(common_prefix_partial);
    RUN_TEST(common_prefix_none);
    RUN_TEST(common_prefix_empty);
    RUN_TEST(common_prefix_case_sensitive);

    /// Subsequence matching tests
    printf("\n=== Subsequence Matching Tests ===\n");
    RUN_TEST(is_subsequence_true);
    RUN_TEST(is_subsequence_false);
    RUN_TEST(is_subsequence_identical);
    RUN_TEST(is_subsequence_empty_pattern);
    RUN_TEST(is_subsequence_score);

    /// Combined match score tests
    printf("\n=== Combined Match Score Tests ===\n");
    RUN_TEST(match_score_identical);
    RUN_TEST(match_score_similar);
    RUN_TEST(match_score_different);
    RUN_TEST(match_score_empty);
    RUN_TEST(match_score_case_insensitive);
    RUN_TEST(match_score_case_sensitive);

    /// Is match threshold tests
    printf("\n=== Is Match Threshold Tests ===\n");
    RUN_TEST(is_match_above_threshold);
    RUN_TEST(is_match_below_threshold);
    RUN_TEST(is_match_threshold_boundary);

    /// Batch matching tests
    printf("\n=== Batch Matching Tests ===\n");
    RUN_TEST(match_best_basic);
    RUN_TEST(match_best_with_threshold);
    RUN_TEST(match_filter_basic);

    /// Utility function tests
    printf("\n=== Utility Function Tests ===\n");
    RUN_TEST(distance_to_score_zero);
    RUN_TEST(distance_to_score_full);
    RUN_TEST(distance_to_score_half);
    RUN_TEST(string_length_ascii);
    RUN_TEST(string_length_empty);

    /// Options preset tests
    printf("\n=== Options Preset Tests ===\n");
    RUN_TEST(options_default);
    RUN_TEST(options_strict);
    RUN_TEST(options_fast);

    /// Completion score tests (fzy-style)
    printf("\n=== Completion Score Tests ===\n");
    RUN_TEST(completion_empty_pattern_matches_all);
    RUN_TEST(completion_empty_text_zero);
    RUN_TEST(completion_exact_match_perfect);
    RUN_TEST(completion_case_insensitive_default);
    RUN_TEST(completion_non_subsequence_returns_zero);
    RUN_TEST(completion_subsequence_order_required);
    RUN_TEST(completion_first_char_bonus_outranks_mid_word);
    RUN_TEST(completion_word_boundary_bonus_outranks_internal);
    RUN_TEST(completion_diagnosis_case_keeps_candidates_above_zero);
    RUN_TEST(completion_camel_case_boundary);
    RUN_TEST(completion_adjacency_bonus);
    RUN_TEST(completion_threshold_distinguishes_strong_from_weak);
    RUN_TEST(completion_pattern_longer_than_text_zero);
    RUN_TEST(completion_unicode_nfc_normalization);
    RUN_TEST(completion_case_sensitive_strict);

    return TEST_RESULT();
}
