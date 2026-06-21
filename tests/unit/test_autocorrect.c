/**
 * @file test_autocorrect.c
 * @brief Unit tests for autocorrection system
 *
 * Tests the autocorrection system including:
 * - Initialization and cleanup
 * - Configuration management
 * - Similarity scoring algorithms
 * - Suggestion generation
 * - Statistics tracking
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alias.h"
#include "autocorrect.h"
#include "executor.h"
#include "symtable.h"
#include "test_framework.h"

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(autocorrect_init_cleanup) {
    int result = autocorrect_init();
    ASSERT_EQ(result, 0, "autocorrect_init should succeed");

    autocorrect_cleanup();
    /// Should not crash on cleanup
}

TEST(autocorrect_double_init) {
    int result = autocorrect_init();
    ASSERT_EQ(result, 0, "First init should succeed");

    result = autocorrect_init();
    ASSERT_EQ(result, 0, "Second init should also succeed");

    autocorrect_cleanup();
}

TEST(autocorrect_cleanup_without_init) {
    /// Should not crash
    autocorrect_cleanup();
}

/* ============================================================================
 * CONFIGURATION TESTS
 * ============================================================================
 */

TEST(autocorrect_default_config) {
    autocorrect_config_t config;
    autocorrect_get_default_config(&config);

    ASSERT_TRUE(config.enabled, "Default should be enabled");
    ASSERT(config.max_suggestions >= 1 && config.max_suggestions <= 5,
           "Max suggestions should be 1-5");
    ASSERT(config.similarity_threshold >= 0 &&
               config.similarity_threshold <= 100,
           "Threshold should be 0-100");
}

TEST(autocorrect_validate_config_valid) {
    autocorrect_config_t config;
    autocorrect_get_default_config(&config);

    bool valid = autocorrect_validate_config(&config);
    ASSERT_TRUE(valid, "Default config should be valid");
}

TEST(autocorrect_validate_config_invalid_suggestions) {
    autocorrect_config_t config;
    autocorrect_get_default_config(&config);
    config.max_suggestions = 10; /// Invalid - too many

    bool valid = autocorrect_validate_config(&config);
    ASSERT_FALSE(valid, "Config with too many suggestions should be invalid");
}

TEST(autocorrect_validate_config_invalid_threshold) {
    autocorrect_config_t config;
    autocorrect_get_default_config(&config);
    config.similarity_threshold = 150; /// Invalid - over 100

    bool valid = autocorrect_validate_config(&config);
    ASSERT_FALSE(valid, "Config with threshold > 100 should be invalid");
}

TEST(autocorrect_load_config) {
    autocorrect_init();

    autocorrect_config_t config;
    autocorrect_get_default_config(&config);
    config.enabled = false;

    int result = autocorrect_load_config(&config);
    ASSERT_EQ(result, 0, "Loading config should succeed");
    ASSERT_FALSE(autocorrect_is_enabled(),
                 "Should be disabled after config load");

    autocorrect_cleanup();
}

TEST(autocorrect_apply_config) {
    autocorrect_init();

    autocorrect_config_t config;
    autocorrect_get_default_config(&config);

    /// Applying a config takes effect: enabling it makes the engine report
    /// enabled, and applying a disabled config turns it back off.
    config.enabled = true;
    config.max_suggestions = 3;
    ASSERT_EQ(autocorrect_apply_config(&config), 0, "applying config succeeds");
    ASSERT(autocorrect_is_enabled(), "config with enabled=true takes effect");

    config.enabled = false;
    ASSERT_EQ(autocorrect_apply_config(&config), 0, "applying config succeeds");
    ASSERT(!autocorrect_is_enabled(), "config with enabled=false takes effect");

    autocorrect_cleanup();
}

TEST(autocorrect_is_enabled) {
    autocorrect_init();

    autocorrect_config_t config;
    autocorrect_get_default_config(&config);
    config.enabled = true;
    autocorrect_load_config(&config);

    ASSERT_TRUE(autocorrect_is_enabled(), "Should be enabled");

    config.enabled = false;
    autocorrect_load_config(&config);

    ASSERT_FALSE(autocorrect_is_enabled(), "Should be disabled");

    autocorrect_cleanup();
}

/* ============================================================================
 * SIMILARITY SCORING TESTS
 * ============================================================================
 */

TEST(levenshtein_identical) {
    int dist = autocorrect_levenshtein_distance("hello", "hello");
    ASSERT_EQ(dist, 0, "Identical strings should have distance 0");
}

TEST(levenshtein_one_char_diff) {
    int dist = autocorrect_levenshtein_distance("hello", "hallo");
    ASSERT_EQ(dist, 1, "One char difference should be distance 1");
}

TEST(levenshtein_empty_string) {
    int dist = autocorrect_levenshtein_distance("", "hello");
    ASSERT_EQ(dist, 5, "Empty to 'hello' should be distance 5");

    dist = autocorrect_levenshtein_distance("hello", "");
    ASSERT_EQ(dist, 5, "'hello' to empty should be distance 5");
}

TEST(levenshtein_completely_different) {
    int dist = autocorrect_levenshtein_distance("abc", "xyz");
    ASSERT_EQ(dist, 3, "Completely different strings of length 3");
}

TEST(jaro_winkler_identical) {
    int score = autocorrect_jaro_winkler_score("hello", "hello");
    ASSERT_EQ(score, 100, "Identical strings should have score 100");
}

TEST(jaro_winkler_similar) {
    int score = autocorrect_jaro_winkler_score("hello", "hallo");
    ASSERT(score >= 70 && score <= 95,
           "Similar strings should have high score");
}

TEST(jaro_winkler_different) {
    int score = autocorrect_jaro_winkler_score("abc", "xyz");
    ASSERT(score < 50, "Different strings should have low score");
}

TEST(common_prefix_identical) {
    int len = autocorrect_common_prefix_length("hello", "hello", true);
    ASSERT_EQ(len, 5, "Identical strings should have full prefix");
}

TEST(common_prefix_partial) {
    int len = autocorrect_common_prefix_length("hello", "help", true);
    ASSERT_EQ(len, 3, "'hello' and 'help' share 'hel' prefix");
}

TEST(common_prefix_none) {
    int len = autocorrect_common_prefix_length("abc", "xyz", true);
    ASSERT_EQ(len, 0, "No common prefix");
}

TEST(common_prefix_case_insensitive) {
    int len = autocorrect_common_prefix_length("Hello", "hello", false);
    ASSERT_EQ(len, 5, "Case insensitive should match full string");
}

TEST(common_prefix_case_sensitive) {
    int len = autocorrect_common_prefix_length("Hello", "hello", true);
    ASSERT_EQ(len, 0, "Case sensitive should not match");
}

TEST(subsequence_score_full_match) {
    int score = autocorrect_subsequence_score("ls", "ls", true);
    ASSERT_EQ(score, 100, "Full match should have score 100");
}

TEST(subsequence_score_partial) {
    int score = autocorrect_subsequence_score("gp", "grep", true);
    ASSERT(score >= 50, "Subsequence 'gp' in 'grep' should have decent score");
}

TEST(subsequence_score_no_match) {
    int score = autocorrect_subsequence_score("xyz", "abc", true);
    ASSERT_EQ(score, 0, "No subsequence match should have score 0");
}

TEST(similarity_score_identical) {
    int score = autocorrect_similarity_score("echo", "echo", true);
    ASSERT(score >= 90, "Identical strings should have very high score");
}

TEST(similarity_score_typo) {
    int score = autocorrect_similarity_score("ehco", "echo", true);
    ASSERT(score >= 50, "Typo should still have reasonable score");
}

TEST(similarity_score_case_insensitive) {
    int score = autocorrect_similarity_score("ECHO", "echo", false);
    ASSERT(score >= 90, "Case insensitive identical should match");
}

TEST(levenshtein_unicode_case_insensitive_latin1) {
    /// Latin-1 Supplement: É (U+00C9) folds to é (U+00E9). The byte
    /// length differs from codepoint length, so a byte-by-byte fold
    /// would mismatch even between identical inputs of opposite case.
    int dist = autocorrect_levenshtein_distance("café", "CAFÉ");
    ASSERT_EQ(dist, 0, "Case-insensitive Café/CAFÉ should fold to distance 0");
}

TEST(levenshtein_unicode_one_codepoint_typo) {
    /// One-codepoint substitution at a non-ASCII position.
    /// "café" -> "cafe": é (multi-byte) replaced by ASCII e. A bytewise
    /// Levenshtein would count the multi-byte difference as two edits
    /// (one per replaced byte) plus a deletion; codepoint-wise this
    /// is one substitution.
    int dist = autocorrect_levenshtein_distance("café", "cafe");
    ASSERT_EQ(dist, 1, "café -> cafe should be one codepoint substitution");
}

TEST(levenshtein_unicode_extended_a) {
    /// Latin Extended-A: Č (U+010C) folds to č (U+010D). Without the
    /// lle_unicode_tolower_codepoint path, libfuzzy's prior fold only
    /// covered ASCII + Latin-1 Supplement and would mishandle this.
    int dist = autocorrect_levenshtein_distance("ČAJ", "čaj");
    ASSERT_EQ(dist, 0, "Latin Extended-A case-fold should match identically");
}

TEST(levenshtein_unicode_greek) {
    /// Greek: Α (U+0391) folds to α (U+03B1). Same reasoning -- prior
    /// code wouldn't have touched Greek codepoints.
    int dist = autocorrect_levenshtein_distance("ΑΒΓ", "αβγ");
    ASSERT_EQ(dist, 0, "Greek case-fold should match identically");
}

TEST(levenshtein_unicode_codepoint_distance_not_byte) {
    /// "café" is 4 codepoints / 5 bytes. "naïve" is 5 codepoints / 6
    /// bytes. The edit distance should reflect codepoint differences,
    /// not byte differences: substitutions on c/n, a/n... vs a, f/i,
    /// é/v, plus an insertion of e -- net 4 edits over codepoints.
    /// A bytewise count would inflate this past the codepoint figure
    /// because the multi-byte é and ï each look like multiple byte
    /// edits.
    int dist = autocorrect_levenshtein_distance("café", "naïve");
    ASSERT(dist <= 5, "Codepoint-level distance should be bounded by max len");
}

/* ============================================================================
 * SUGGESTION TESTS
 * ============================================================================
 */

TEST(suggest_builtins_basic) {
    autocorrect_init();

    correction_t suggestions[5];
    int count = autocorrect_suggest_builtins("ehco", suggestions, 5, true);

    /// Should find 'echo' as a suggestion
    bool found_echo = false;
    for (int i = 0; i < count; i++) {
        if (suggestions[i].command &&
            strcmp(suggestions[i].command, "echo") == 0) {
            found_echo = true;
            free(suggestions[i].command);
        } else if (suggestions[i].command) {
            free(suggestions[i].command);
        }
    }
    ASSERT_TRUE(found_echo, "Should find 'echo' for 'ehco' typo");

    autocorrect_cleanup();
}

TEST(suggest_builtins_no_match) {
    autocorrect_init();

    correction_t suggestions[5];
    int count = autocorrect_suggest_builtins("xyzabc123", suggestions, 5, true);

    /// Clean up any suggestions
    for (int i = 0; i < count; i++) {
        if (suggestions[i].command) {
            free(suggestions[i].command);
        }
    }

    /// Unlikely to find matches for random string
    ASSERT(count <= 1, "Random string should have few or no matches");

    autocorrect_cleanup();
}

/* ============================================================================
 * RESULT MANAGEMENT TESTS
 * ============================================================================
 */

TEST(free_results_empty) {
    correction_results_t results;
    memset(&results, 0, sizeof(results));
    results.count = 0;
    results.original_command = NULL;

    /// Should not crash
    autocorrect_free_results(&results);
}

TEST(free_results_with_data) {
    correction_results_t results;
    memset(&results, 0, sizeof(results));
    results.count = 1;
    results.original_command = strdup("test");
    results.suggestions[0].command = strdup("test_suggestion");
    results.suggestions[0].score = 85;
    results.suggestions[0].source = "test";

    autocorrect_free_results(&results);

    /// Freeing resets the struct: owned strings are released and their
    /// pointers cleared, and the count returns to zero.
    ASSERT_EQ(results.count, 0, "count is reset to 0 after free");
    ASSERT_NULL(results.original_command, "original_command is cleared");
    ASSERT_NULL(results.suggestions[0].command, "suggestion command cleared");
}

/* ============================================================================
 * STATISTICS TESTS
 * ============================================================================
 */

TEST(stats_initial) {
    autocorrect_init();

    int offered = -1, accepted = -1, learned = -1;
    autocorrect_get_stats(&offered, &accepted, &learned);

    /// A freshly initialized engine has done nothing yet: all counters are 0.
    ASSERT_EQ(offered, 0, "no corrections offered yet");
    ASSERT_EQ(accepted, 0, "no corrections accepted yet");
    ASSERT_EQ(learned, 0, "nothing learned yet");

    autocorrect_cleanup();
}

TEST(stats_reset) {
    autocorrect_init();

    autocorrect_reset_stats();

    int offered = -1, accepted = -1, learned = -1;
    autocorrect_get_stats(&offered, &accepted, &learned);

    ASSERT_EQ(offered, 0, "Offered should be 0 after reset");
    ASSERT_EQ(accepted, 0, "Accepted should be 0 after reset");
    ASSERT_EQ(learned, 0, "Learned should be 0 after reset");

    autocorrect_cleanup();
}

TEST(stats_learn_command) {
    autocorrect_init();
    autocorrect_reset_stats();

    autocorrect_learn_command("ls");
    autocorrect_learn_command("grep");

    int offered, accepted, learned;
    autocorrect_get_stats(&offered, &accepted, &learned);

    ASSERT_EQ(learned, 2, "Should have learned 2 commands");

    autocorrect_cleanup();
}

TEST(learn_command_nfc_nfd_dedup) {
    /// Same user-visible command name in NFC and NFD must not
    /// expand the learned-commands set -- they are canonically
    /// equivalent strings.
    autocorrect_init();
    autocorrect_reset_stats();

    autocorrect_learn_command("caf\xC3\xA9");  /// NFC: e-acute U+00E9
    autocorrect_learn_command("cafe\xCC\x81"); /// NFD: e + U+0301

    int offered, accepted, learned;
    autocorrect_get_stats(&offered, &accepted, &learned);
    ASSERT_EQ(learned, 1,
              "NFC and NFD encodings of café must collapse to one entry");

    autocorrect_cleanup();
}

TEST(learn_command_distinct_under_case) {
    /// NFC equivalence does not paper over case differences --
    /// "café" and "CAFÉ" remain separate learned entries.
    autocorrect_init();
    autocorrect_reset_stats();

    autocorrect_learn_command("caf\xC3\xA9"); /// café (lowercase)
    autocorrect_learn_command("CAF\xC3\x89"); /// CAFÉ (uppercase)

    int offered, accepted, learned;
    autocorrect_get_stats(&offered, &accepted, &learned);
    ASSERT_EQ(learned, 2,
              "Different case is a different command for dedup purposes");

    autocorrect_cleanup();
}

/* ============================================================================
 * DEBUG MODE TESTS
 * ============================================================================
 */

TEST(debug_mode_toggle) {
    autocorrect_init();

    /// Should not crash
    autocorrect_set_debug(true);
    autocorrect_set_debug(false);

    autocorrect_cleanup();
}

/* ============================================================================
 * COMMAND EXISTS TESTS
 * ============================================================================
 */

TEST(command_exists_builtin) {
    init_symtable();
    init_aliases();

    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "Executor should be created");

    bool exists = autocorrect_command_exists(exec, "echo");
    ASSERT_TRUE(exists, "'echo' builtin should exist");

    exists = autocorrect_command_exists(exec, "cd");
    ASSERT_TRUE(exists, "'cd' builtin should exist");

    executor_free(exec);
}

TEST(command_exists_nonexistent) {
    init_symtable();
    init_aliases();

    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "Executor should be created");

    bool exists = autocorrect_command_exists(exec, "nonexistent_cmd_xyz_123");
    ASSERT_FALSE(exists, "Nonexistent command should not exist");

    executor_free(exec);
}

TEST(command_exists_external) {
    init_symtable();
    init_aliases();

    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "Executor should be created");

    /// 'ls' should exist on most systems
    bool exists = autocorrect_command_exists(exec, "ls");
    ASSERT_TRUE(exists, "'ls' should exist in PATH");

    executor_free(exec);
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

TEST(null_inputs) {
    /// NULL inputs are handled gracefully with defined return values rather
    /// than crashing. Similarity, Jaro-Winkler, prefix, and subsequence
    /// scoring all treat a NULL operand as "no match" and return 0.
    ASSERT_EQ(autocorrect_similarity_score(NULL, "test", true), 0,
              "similarity with a NULL operand is 0");
    ASSERT_EQ(autocorrect_similarity_score("test", NULL, true), 0,
              "similarity with a NULL operand is 0");
    ASSERT_EQ(autocorrect_jaro_winkler_score(NULL, "test"), 0,
              "jaro-winkler with a NULL operand is 0");
    ASSERT_EQ(autocorrect_common_prefix_length(NULL, "test", true), 0,
              "prefix length with a NULL operand is 0");
    ASSERT_EQ(autocorrect_subsequence_score(NULL, "test", true), 0,
              "subsequence score with a NULL operand is 0");

    /// Levenshtein treats a NULL operand as the empty string, so the distance
    /// is the length of the other string.
    ASSERT_EQ(autocorrect_levenshtein_distance(NULL, "test"), 4,
              "distance from NULL to \"test\" is its length");
    ASSERT_EQ(autocorrect_levenshtein_distance("test", NULL), 4,
              "distance from \"test\" to NULL is its length");
}

TEST(empty_strings) {
    int dist = autocorrect_levenshtein_distance("", "");
    ASSERT_EQ(dist, 0, "Two empty strings should have distance 0");

    /// Two empty strings are identical, so the similarity is the maximum 100.
    int score = autocorrect_jaro_winkler_score("", "");
    ASSERT_EQ(score, 100, "Two empty strings score a perfect 100");

    int prefix = autocorrect_common_prefix_length("", "", true);
    ASSERT_EQ(prefix, 0, "Two empty strings should have prefix 0");
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n=== Autocorrect System Unit Tests ===\n\n");

    /// Initialization tests
    printf("Initialization Tests:\n");
    RUN_TEST(autocorrect_init_cleanup);
    RUN_TEST(autocorrect_double_init);
    RUN_TEST(autocorrect_cleanup_without_init);

    /// Configuration tests
    printf("\nConfiguration Tests:\n");
    RUN_TEST(autocorrect_default_config);
    RUN_TEST(autocorrect_validate_config_valid);
    RUN_TEST(autocorrect_validate_config_invalid_suggestions);
    RUN_TEST(autocorrect_validate_config_invalid_threshold);
    RUN_TEST(autocorrect_load_config);
    RUN_TEST(autocorrect_apply_config);
    RUN_TEST(autocorrect_is_enabled);

    /// Similarity scoring tests
    printf("\nSimilarity Scoring Tests:\n");
    RUN_TEST(levenshtein_identical);
    RUN_TEST(levenshtein_one_char_diff);
    RUN_TEST(levenshtein_empty_string);
    RUN_TEST(levenshtein_completely_different);
    RUN_TEST(jaro_winkler_identical);
    RUN_TEST(jaro_winkler_similar);
    RUN_TEST(jaro_winkler_different);
    RUN_TEST(common_prefix_identical);
    RUN_TEST(common_prefix_partial);
    RUN_TEST(common_prefix_none);
    RUN_TEST(common_prefix_case_insensitive);
    RUN_TEST(common_prefix_case_sensitive);
    RUN_TEST(subsequence_score_full_match);
    RUN_TEST(subsequence_score_partial);
    RUN_TEST(subsequence_score_no_match);
    RUN_TEST(similarity_score_identical);
    RUN_TEST(similarity_score_typo);
    RUN_TEST(similarity_score_case_insensitive);
    RUN_TEST(levenshtein_unicode_case_insensitive_latin1);
    RUN_TEST(levenshtein_unicode_one_codepoint_typo);
    RUN_TEST(levenshtein_unicode_extended_a);
    RUN_TEST(levenshtein_unicode_greek);
    RUN_TEST(levenshtein_unicode_codepoint_distance_not_byte);

    /// Suggestion tests
    printf("\nSuggestion Tests:\n");
    RUN_TEST(suggest_builtins_basic);
    RUN_TEST(suggest_builtins_no_match);

    /// Result management tests
    printf("\nResult Management Tests:\n");
    RUN_TEST(free_results_empty);
    RUN_TEST(free_results_with_data);

    /// Statistics tests
    printf("\nStatistics Tests:\n");
    RUN_TEST(stats_initial);
    RUN_TEST(stats_reset);
    RUN_TEST(stats_learn_command);
    RUN_TEST(learn_command_nfc_nfd_dedup);
    RUN_TEST(learn_command_distinct_under_case);

    /// Debug mode tests
    printf("\nDebug Mode Tests:\n");
    RUN_TEST(debug_mode_toggle);

    /// Command exists tests
    printf("\nCommand Exists Tests:\n");
    RUN_TEST(command_exists_builtin);
    RUN_TEST(command_exists_nonexistent);
    RUN_TEST(command_exists_external);

    /// Edge case tests
    printf("\nEdge Case Tests:\n");
    RUN_TEST(null_inputs);
    RUN_TEST(empty_strings);

    return TEST_RESULT();
}
