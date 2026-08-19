/**
 * @file test_history_search_results.c
 * @brief Functional tests for the LLE history search-results container
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * History search-results tests (Spec 09)
 *
 * Comprehensive test suite for the LLE History Search Engine:
 * - Exact match search
 * - Prefix search
 * - Substring search
 * - Fuzzy search (Levenshtein distance)
 * - Result management and ranking
 * - Performance characteristics
 *
 * API verified from include/lle/history.h on 2025-11-02
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/// Test memory mock provided by test_memory_mock.c

/// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Helper macros
#define TEST_START(name)                                                       \
    do {                                                                       \
        tests_run++;                                                           \
        printf("\n[TEST %d] %s...\n", tests_run, name);                        \
    } while (0)

#define TEST_PASS()                                                            \
    do {                                                                       \
        tests_passed++;                                                        \
        printf("  \xe2\x9c\x93 PASS\n");                                       \
    } while (0)

#define TEST_FAIL(msg)                                                         \
    do {                                                                       \
        tests_failed++;                                                        \
        printf("  \xe2\x9c\x97 FAIL: %s\n", msg);                              \
    } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s\n", msg);              \
            printf("    Expected: %ld, Got: %ld\n", (long)(expected),          \
                   (long)(actual));                                            \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

/// Is this translation unit built under AddressSanitizer?
///
/// The `defined(__has_feature) && __has_feature(...)` one-liner does NOT work:
/// a preprocessor that lacks __has_feature substitutes the unknown identifier
/// with 0 and then fails on the following `(` -- `defined()` does not
/// short-circuit the PARSE. GCC rejected exactly that with "missing binary
/// operator before token" while clang accepted it, so the one-liner built
/// clean on macOS and broke the Linux jobs. The nested form is the portable
/// spelling: the inner directive is only ever reached where __has_feature
/// exists.
#if defined(__SANITIZE_ADDRESS__)
#define LUSH_TEST_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LUSH_TEST_UNDER_ASAN 1
#endif
#endif
#ifndef LUSH_TEST_UNDER_ASAN
#define LUSH_TEST_UNDER_ASAN 0
#endif

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s\n", msg);              \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg)                                              \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s (got NULL)\n", msg);   \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NULL(ptr, msg)                                                  \
    do {                                                                       \
        if ((ptr) != NULL) {                                                   \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s (expected NULL)\n",    \
                   msg);                                                       \
            TEST_FAIL(msg);                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

/* ============================================================================
 * SEARCH RESULT MANAGEMENT TESTS
 * ============================================================================
 */

void test_search_results_create_destroy(void) {
    TEST_START("Search Results Create/Destroy");

    /// Create results container
    lle_history_search_results_t *results =
        lle_history_search_results_create(100);
    ASSERT_NOT_NULL(results, "Results creation should succeed");

    /// Verify initial state
    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Initial count should be 0");

    /// Destroy
    lle_history_search_results_destroy(results);

    TEST_PASS();
}

void test_search_results_create_default(void) {
    TEST_START("Search Results Create with Default Size");

    /// Create with default size (0 = use default)
    lle_history_search_results_t *results =
        lle_history_search_results_create(0);
    ASSERT_NOT_NULL(results,
                    "Results creation with default size should succeed");

    /// Should be able to use it
    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Initial count should be 0");

    lle_history_search_results_destroy(results);

    TEST_PASS();
}

/* ============================================================================
 * EXACT MATCH SEARCH TESTS
 * ============================================================================
 */

void test_exact_match_search(void) {
    TEST_START("Exact Match Search");

    /// Create history core
    lle_history_core_t *core = NULL;
    lle_result_t result = lle_history_core_create(&core, NULL, NULL);
    ASSERT_EQ(result, LLE_SUCCESS, "Core creation should succeed");

    /// Add some test entries
    uint64_t id1, id2, id3;
    lle_history_add_entry(core, "ls -la", 0, &id1);
    lle_history_add_entry(core, "cd /tmp", 0, &id2);
    lle_history_add_entry(core, "ls -la", 0, &id3); /// Duplicate

    /// Search for exact match
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "ls -la", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    /// Should find both matches
    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 2, "Should find 2 exact matches");

    /// Verify results contain the correct command
    const lle_search_result_t *r = lle_history_search_results_get(results, 0);
    ASSERT_NOT_NULL(r, "Should get first result");
    ASSERT_TRUE(strcmp(r->command, "ls -la") == 0, "Result should match query");

    /// Check search time was recorded
    uint64_t search_time = lle_history_search_results_get_time_us(results);
    ASSERT_TRUE(search_time > 0, "Search time should be recorded");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_exact_match_nfc_nfd_equivalence(void) {
    TEST_START("Exact Match Search - NFC/NFD Equivalence");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Entry stored in NFC form: "cafe-acute" = caf + e-acute (U+00E9, bytes C3
    /// A9)
    lle_history_add_entry(core, "caf\xC3\xA9", 0, NULL);
    /// Unrelated entry for noise.
    lle_history_add_entry(core, "echo hello", 0, NULL);

    /// Query in NFD form: "cafe-acute" = caf + e + combining acute (U+0301, CC
    /// 81) Must match the NFC-stored entry under NFC equivalence.
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "cafe\xCC\x81", 10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "NFD query must find NFC-stored entry");

    lle_history_search_results_destroy(results);

    /// And the reverse direction: NFD-stored entry, NFC query.
    lle_history_add_entry(core, "cafe\xCC\x81 deuxieme", 0, NULL);
    results = lle_history_search_exact(core, "caf\xC3\xA9 deuxieme", 10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "NFC query must find NFD-stored entry");
    lle_history_search_results_destroy(results);

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_prefix_search_matches_exact_on_normalization(void) {
    TEST_START("Prefix Search - normalization and cluster boundaries");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Entry stored precomposed; query typed decomposed. Same visible text.
    /// Exact search has always matched this; prefix search did not, and prefix
    /// search is what interactive recall uses (issue #775).
    lle_history_add_entry(core, "caf\xC3\xA9 alpha", 0, NULL);
    lle_history_search_results_t *r =
        lle_history_search_prefix(core, "cafe\xCC\x81", 10);
    ASSERT_NOT_NULL(r, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(r), 1,
              "decomposed query must find the precomposed entry");
    lle_history_search_results_destroy(r);

    /// A query that would end BETWEEN a base and its combining mark is half a
    /// character, and does not match -- the rule completion filtering and the
    /// substitution search both use.
    lle_history_add_entry(core, "cafe\xCC\x81 beta", 0, NULL);
    r = lle_history_search_prefix(core, "cafe", 10);
    ASSERT_NOT_NULL(r, "Search should return a result set");
    ASSERT_EQ(lle_history_search_results_get_count(r), 0,
              "a prefix must not match inside a character");
    lle_history_search_results_destroy(r);

    /// Ending ON a boundary matches both spellings, since they normalize
    /// to the same text.
    r = lle_history_search_prefix(core, "caf", 10);
    ASSERT_NOT_NULL(r, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(r), 2,
              "a boundary-aligned prefix matches both stored spellings");
    lle_history_search_results_destroy(r);

    lle_history_core_destroy(core);
    TEST_PASS();
}

void test_exact_match_no_results(void) {
    TEST_START("Exact Match Search - No Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "ls -la", 0, NULL);
    lle_history_add_entry(core, "cd /tmp", 0, NULL);

    /// Search for non-existent command
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "nonexistent", 10);
    ASSERT_NOT_NULL(results, "Search should return results container");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Should find no matches");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_exact_match_case_sensitive(void) {
    TEST_START("Exact Match Search - Case Sensitive");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "ls -la", 0, NULL);
    lle_history_add_entry(core, "LS -LA", 0, NULL);

    /// Exact match is case-sensitive
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "ls -la", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 1, "Should find only exact case match");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * PREFIX SEARCH TESTS
 * ============================================================================
 */

void test_prefix_search(void) {
    TEST_START("Prefix Search");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add entries with common prefixes
    lle_history_add_entry(core, "git status", 0, NULL);
    lle_history_add_entry(core, "git commit", 0, NULL);
    lle_history_add_entry(core, "git push", 0, NULL);
    lle_history_add_entry(core, "ls -la", 0, NULL);

    /// Search for "git" prefix
    lle_history_search_results_t *results =
        lle_history_search_prefix(core, "git", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 3, "Should find 3 commands starting with 'git'");

    /// Verify all results start with prefix
    for (size_t i = 0; i < count; i++) {
        const lle_search_result_t *r =
            lle_history_search_results_get(results, i);
        ASSERT_NOT_NULL(r, "Should get result");
        ASSERT_TRUE(strncmp(r->command, "git", 3) == 0 ||
                        strncmp(r->command, "Git", 3) == 0,
                    "Result should start with 'git' (case-insensitive)");
    }

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_prefix_search_case_insensitive(void) {
    TEST_START("Prefix Search - Case Insensitive");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "Git status", 0, NULL);
    lle_history_add_entry(core, "GIT commit", 0, NULL);
    lle_history_add_entry(core, "git push", 0, NULL);

    /// Search with lowercase prefix should match all cases
    lle_history_search_results_t *results =
        lle_history_search_prefix(core, "git", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 3, "Should find all case variations");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_prefix_search_empty_results(void) {
    TEST_START("Prefix Search - No Matches");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "ls -la", 0, NULL);
    lle_history_add_entry(core, "cd /tmp", 0, NULL);

    lle_history_search_results_t *results =
        lle_history_search_prefix(core, "git", 10);
    ASSERT_NOT_NULL(results, "Search should return results container");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Should find no matches");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * SUBSTRING SEARCH TESTS
 * ============================================================================
 */

void test_substring_search(void) {
    TEST_START("Substring Search");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add entries with keyword in different positions
    lle_history_add_entry(core, "docker ps -a", 0, NULL);
    lle_history_add_entry(core, "docker-compose up", 0, NULL);
    lle_history_add_entry(core, "ls -la /var/lib/docker", 0, NULL);
    lle_history_add_entry(core, "git status", 0, NULL);

    /// Search for "docker" substring
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "docker", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 3, "Should find 3 commands containing 'docker'");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_substring_search_case_insensitive(void) {
    TEST_START("Substring Search - Case Insensitive");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "DOCKER ps", 0, NULL);
    lle_history_add_entry(core, "Docker-compose", 0, NULL);
    lle_history_add_entry(core, "docker logs", 0, NULL);

    /// Lowercase search should match all cases
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "docker", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 3, "Should find all case variations");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_substring_search_case_insensitive_unicode(void) {
    TEST_START("Substring Search - Case Insensitive Unicode");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Mixed-case entries with non-ASCII letters that the previous
    /// strncasecmp-based search could not fold (only A-Z -> a-z).
    lle_history_add_entry(core, "echo Caf\xc3\xa9", 0, NULL);
    lle_history_add_entry(core, "echo NA\xc3\x8fVE", 0, NULL);
    lle_history_add_entry(core, "echo \xc3\x85ngstr\xc3\xb6m", 0, NULL);
    lle_history_add_entry(core,
                          "echo stra\xc3\x9f"
                          "e",
                          0, NULL);

    /// Lowercase needle must match the uppercase Unicode entries.
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "caf\xc3\xa9", 10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "Should find 'echo Caf\xc3\xa9' via 'caf\xc3\xa9'");
    lle_history_search_results_destroy(results);

    results = lle_history_search_substring(core, "na\xc3\xafve", 10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "Should find 'echo NA\xc3\x8fVE' via 'na\xc3\xafve'");
    lle_history_search_results_destroy(results);

    results = lle_history_search_substring(core, "\xc3\xa5ngstr\xc3\xb6m", 10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "Should find 'echo \xc3\x85ngstr\xc3\xb6m' via "
              "'\xc3\xa5ngstr\xc3\xb6m'");
    lle_history_search_results_destroy(results);

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_prefix_search_case_insensitive_unicode(void) {
    TEST_START("Prefix Search - Case Insensitive Unicode");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core,
                          "\xc3\x9c"
                          "ber alle Berge",
                          0, NULL);
    lle_history_add_entry(core,
                          "\xc3\x9c"
                          "BER LASTIG",
                          0, NULL);
    lle_history_add_entry(core,
                          "\xc3\xbc"
                          "ber die Br\xc3\xbc"
                          "cke",
                          0, NULL);
    lle_history_add_entry(core, "git status", 0, NULL);

    /// Lowercase 'u-umlaut' prefix must match all three Unicode entries.
    lle_history_search_results_t *results = lle_history_search_prefix(core,
                                                                      "\xc3\xbc"
                                                                      "ber",
                                                                      10);
    ASSERT_NOT_NULL(results, "Search should return results");
    ASSERT_EQ(lle_history_search_results_get_count(results), 3,
              "Should find all 3 '\xc3\x9c"
              "ber' / '\xc3\x9c"
              "BER' / '\xc3\xbc"
              "ber' "
              "prefix variants");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_substring_search_partial_match(void) {
    TEST_START("Substring Search - Partial Word Match");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "systemctl status", 0, NULL);
    lle_history_add_entry(core, "system info", 0, NULL);

    /// Search for partial word
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "stem", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 2, "Should find both 'system' matches");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * FUZZY SEARCH TESTS
 * ============================================================================
 */

void test_fuzzy_search_exact(void) {
    TEST_START("Fuzzy Search - Exact Match");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "git status", 0, NULL);
    lle_history_add_entry(core, "git commit", 0, NULL);

    /// Exact match should have highest score
    lle_history_search_results_t *results =
        lle_history_search_fuzzy(core, "git status", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_TRUE(count >= 1, "Should find at least exact match");

    /// First result should be exact match
    const lle_search_result_t *r = lle_history_search_results_get(results, 0);
    ASSERT_NOT_NULL(r, "Should get first result");
    ASSERT_TRUE(strcmp(r->command, "git status") == 0,
                "First result should be exact match");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_fuzzy_search_typo(void) {
    TEST_START("Fuzzy Search - Single Character Typo");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "git status", 0, NULL);
    lle_history_add_entry(core, "ls -la", 0, NULL);

    /// Search with typo (statuz instead of status)
    lle_history_search_results_t *results =
        lle_history_search_fuzzy(core, "git statuz", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_TRUE(count >= 1, "Should find fuzzy match");

    /// Should find "git status"
    const lle_search_result_t *r = lle_history_search_results_get(results, 0);
    ASSERT_NOT_NULL(r, "Should get result");
    ASSERT_TRUE(strcmp(r->command, "git status") == 0,
                "Should match despite typo");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_fuzzy_search_distance_limit(void) {
    TEST_START("Fuzzy Search - Distance Limit (max 3)");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "git", 0, NULL);

    /// 4 character difference should not match (distance > 3)
    lle_history_search_results_t *results =
        lle_history_search_fuzzy(core, "gitxxxx", 10);
    ASSERT_NOT_NULL(results, "Search should return results container");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Should not match with distance > 3");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * RESULT RANKING TESTS
 * ============================================================================
 */

void test_result_ranking_by_recency(void) {
    TEST_START("Result Ranking - Recency");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add same command multiple times
    lle_history_add_entry(core, "ls -la", 0, NULL); /// Older
    lle_history_add_entry(core, "cd /tmp", 0, NULL);
    lle_history_add_entry(core, "ls -la", 0, NULL); /// More recent

    /// Search should find both, with more recent first
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "ls -la", 10);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 2, "Should find 2 matches");

    /// More recent should have higher score
    const lle_search_result_t *r0 = lle_history_search_results_get(results, 0);
    const lle_search_result_t *r1 = lle_history_search_results_get(results, 1);
    ASSERT_NOT_NULL(r0, "Should get first result");
    ASSERT_NOT_NULL(r1, "Should get second result");

    /// First result should be more recent (higher index)
    ASSERT_TRUE(r0->entry_index > r1->entry_index,
                "First result should be more recent");
    ASSERT_TRUE(r0->score >= r1->score,
                "More recent should have higher or equal score");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_result_max_limit(void) {
    TEST_START("Result Limiting - Max Results");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add many matching entries
    for (int i = 0; i < 20; i++) {
        lle_history_add_entry(core, "ls -la", 0, NULL);
    }

    /// Limit to 5 results
    lle_history_search_results_t *results =
        lle_history_search_exact(core, "ls -la", 5);
    ASSERT_NOT_NULL(results, "Search should return results");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 5, "20 matching entries cap at the requested limit of 5");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

void test_search_empty_history(void) {
    TEST_START("Search on Empty History");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Search empty history
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "test", 10);
    ASSERT_NOT_NULL(results, "Search should return results container");

    size_t count = lle_history_search_results_get_count(results);
    ASSERT_EQ(count, 0, "Should find no results in empty history");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_search_null_parameters(void) {
    TEST_START("Search with NULL Parameters");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// NULL core should return NULL
    lle_history_search_results_t *results1 =
        lle_history_search_exact(NULL, "test", 10);
    ASSERT_NULL(results1, "Search with NULL core should return NULL");

    /// NULL query should return NULL
    lle_history_search_results_t *results2 =
        lle_history_search_exact(core, NULL, 10);
    ASSERT_NULL(results2, "Search with NULL query should return NULL");

    lle_history_core_destroy(core);

    TEST_PASS();
}

void test_search_empty_query(void) {
    TEST_START("Search with Empty Query");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    lle_history_add_entry(core, "ls -la", 0, NULL);

    /// An empty substring query matches every entry, so the single stored
    /// command is returned.
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "", 10);
    ASSERT_NOT_NULL(results, "Empty query should return a results container");
    ASSERT_EQ(lle_history_search_results_get_count(results), 1,
              "Empty query matches all entries");

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    TEST_PASS();
}

/* ============================================================================
 * PERFORMANCE TESTS
 * ============================================================================
 */

void test_search_performance_large_history(void) {
    TEST_START("Search Performance - 1000 Entries");

    lle_history_core_t *core = NULL;
    lle_history_core_create(&core, NULL, NULL);

    /// Add 1000 entries
    char cmd[64];
    for (int i = 0; i < 1000; i++) {
        snprintf(cmd, sizeof(cmd), "command_%d", i);
        lle_history_add_entry(core, cmd, 0, NULL);
    }

    /// Add target command
    lle_history_add_entry(core, "target_command", 0, NULL);

    /// Search should complete reasonably fast
    lle_history_search_results_t *results =
        lle_history_search_substring(core, "target", 10);
    ASSERT_NOT_NULL(results, "Search should succeed");

    /// Read everything out, then RELEASE, then assert.
    ///
    /// ASSERT_TRUE returns from the function, so an assertion placed before the
    /// destroy calls skips them -- and this test holds 1001 entries plus an
    /// index. Under the leak-enforcing sanitizer job that turned ONE failed
    /// assertion into 294KB of leaks across 4234 allocations and a SIGABRT, so
    /// the report named a leak where the real defect was the assertion above
    /// it. Cleanup must not be conditional on the test passing.
    uint64_t time_us = lle_history_search_results_get_time_us(results);
    size_t count = lle_history_search_results_get_count(results);

    lle_history_search_results_destroy(results);
    lle_history_core_destroy(core);

    printf("  Search time: %" PRIu64 " \xce\xbcs\n", time_us);

    ASSERT_TRUE(count >= 1, "Should find target command");

    /// The wall-clock bound is informational under a sanitizer build. ASan
    /// slows execution by an order of magnitude and the CI runners are shared,
    /// so a 50ms threshold there measures the machine rather than the search --
    /// exactly the kind of assertion that fails for reasons unrelated to the
    /// code under test. A normal build is still held to the bound, which is
    /// where the number means something.
#if LUSH_TEST_UNDER_ASAN
    if (time_us >= 50000) {
        printf("  (over 50ms; not asserted under sanitizers)\n");
    }
#else
    ASSERT_TRUE(time_us < 50000, "Search should complete in < 50ms");
#endif

    TEST_PASS();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("=======================================================\n");
    printf("  LLE HISTORY SEARCH ENGINE TESTS\n");
    printf("=======================================================\n");

    printf("\n--- SEARCH RESULT MANAGEMENT ---\n");
    test_search_results_create_destroy();
    test_search_results_create_default();

    printf("\n--- EXACT MATCH SEARCH ---\n");
    test_exact_match_search();
    test_exact_match_nfc_nfd_equivalence();
    test_prefix_search_matches_exact_on_normalization();
    test_exact_match_no_results();
    test_exact_match_case_sensitive();

    printf("\n--- PREFIX SEARCH ---\n");
    test_prefix_search();
    test_prefix_search_case_insensitive();
    test_prefix_search_case_insensitive_unicode();
    test_prefix_search_empty_results();

    printf("\n--- SUBSTRING SEARCH ---\n");
    test_substring_search();
    test_substring_search_case_insensitive();
    test_substring_search_case_insensitive_unicode();
    test_substring_search_partial_match();

    printf("\n--- FUZZY SEARCH ---\n");
    test_fuzzy_search_exact();
    test_fuzzy_search_typo();
    test_fuzzy_search_distance_limit();

    printf("\n--- RESULT RANKING ---\n");
    test_result_ranking_by_recency();
    test_result_max_limit();

    printf("\n--- EDGE CASES ---\n");
    test_search_empty_history();
    test_search_null_parameters();
    test_search_empty_query();

    printf("\n--- PERFORMANCE TESTS ---\n");
    test_search_performance_large_history();

    printf("\n=======================================================\n");
    printf("  TEST RESULTS\n");
    printf("=======================================================\n");
    printf("Total Tests:  %d\n", tests_run);
    printf("Passed:       %d \xe2\x9c\x93\n", tests_passed);
    printf("Failed:       %d \xe2\x9c\x97\n", tests_failed);
    printf("Success Rate: %.1f%%\n",
           tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    printf("=======================================================\n");

    return (tests_failed == 0) ? 0 : 1;
}
