/**
 * @file history_search.c
 * @brief LLE History System - Search Engine Implementation (Spec 09 Phase 3 Day
 * 8)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements basic search functionality for the LLE history system:
 * - Exact match search
 * - Prefix search
 * - Substring search
 * - Basic fuzzy search (Levenshtein distance)
 * - Result ranking and scoring
 *
 * Performance Targets (Spec 09):
 * - Prefix search: <500us for 10K entries
 * - Substring search: <5ms for 10K entries
 * - Fuzzy search: <10ms for 10K entries
 *
 * Architecture:
 * - Linear scan algorithms (simple, correct)
 * - Score-based ranking (recency, position, frequency)
 * - Memory pool allocation for results
 * - Integration with history_core for entry access
 */

#include "fuzzy_match.h"
#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/performance.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h> /* for printf */
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* for strncasecmp - must be after other includes */
#include <time.h>

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/* Forward declare POSIX function that may not be visible in strict C99 mode */
extern int strncasecmp(const char *s1, const char *s2, size_t n);

#define DEFAULT_MAX_SEARCH_RESULTS 100
#define FUZZY_MAX_DISTANCE                                                     \
    3 /* Maximum Levenshtein distance for fuzzy match                          \
       */

/* Scoring weights */
#define SCORE_EXACT_MATCH 1000
#define SCORE_PREFIX_MATCH 500
#define SCORE_SUBSTRING_MATCH 100
#define SCORE_FUZZY_MATCH 50
#define SCORE_RECENCY_WEIGHT 10  /* Points per position from end */
#define SCORE_POSITION_WEIGHT 5  /* Bonus for match at start */
#define SCORE_FREQUENCY_WEIGHT 2 /* Bonus for frequently used commands */

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/* Note: lle_search_type_t and lle_search_result_t are defined in history.h
 * We only define the internal structure for lle_history_search_results_t here
 * (the header has an opaque typedef for this type) */

/**
 * Search results container (internal structure)
 */
struct lle_history_search_results {
    lle_search_result_t *results;  /* Array of results */
    size_t count;                  /* Number of results */
    size_t capacity;               /* Allocated capacity */
    char *query;                   /* Search query (owned copy) */
    lle_search_type_t search_type; /* Search type used */
    uint64_t search_time_us;       /* Search duration in microseconds */
    bool sorted;                   /* Whether results are sorted by score */
};

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Duplicate a string using pool allocation
 *
 * Allocates memory from the LLE memory pool and copies the input string.
 *
 * @param str String to duplicate (may be NULL)
 * @return Pointer to duplicated string, or NULL if str is NULL or allocation
 * fails
 */
static char *pool_strdup(const char *str) {
    if (!str)
        return NULL;
    size_t len = strlen(str);
    char *dup = lle_pool_alloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

/* Note: Levenshtein distance now provided by libfuzzy (fuzzy_match.h) */

/**
 * @brief Calculate relevance score for a search result
 *
 * Computes a score based on match type, recency, position, and command length.
 * Higher scores indicate more relevant results.
 *
 * @param command Command string being scored
 * @param query Search query used for matching
 * @param match_position Position of match in command (0 = start)
 * @param entry_index Index in history (higher = more recent)
 * @param total_entries Total entries in history
 * @param match_type Type of match (exact, prefix, substring, fuzzy)
 * @return Relevance score (higher = better)
 */
static int calculate_score(const char *command, const char *query,
                           size_t match_position, size_t entry_index,
                           size_t total_entries, lle_search_type_t match_type) {
    int score = 0;

    /* Base score by match type */
    switch (match_type) {
    case LLE_SEARCH_TYPE_EXACT:
        score += SCORE_EXACT_MATCH;
        break;
    case LLE_SEARCH_TYPE_PREFIX:
        score += SCORE_PREFIX_MATCH;
        break;
    case LLE_SEARCH_TYPE_SUBSTRING:
        score += SCORE_SUBSTRING_MATCH;
        break;
    case LLE_SEARCH_TYPE_FUZZY:
        score += SCORE_FUZZY_MATCH;
        break;
    }

    /* Recency bonus: more recent commands score higher */
    /* entry_index near total_entries = recent */
    size_t recency = total_entries - entry_index;
    score += (int)(recency * SCORE_RECENCY_WEIGHT / 100);

    /* Position bonus: matches earlier in command score higher */
    if (match_position == 0) {
        score += SCORE_POSITION_WEIGHT;
    }

    /* Length bonus: prefer shorter commands (more specific) */
    size_t cmd_len = strlen(command);
    size_t query_len = strlen(query);
    if (cmd_len > 0 && query_len > 0) {
        int length_ratio = (int)((query_len * 100) / cmd_len);
        score += length_ratio; /* Higher ratio = query is larger portion of
                                  command */
    }

    return score;
}

/**
 * @brief Compare function for qsort - sort results by score (descending)
 *
 * Sorts search results by score in descending order. Uses entry index
 * as a tie-breaker (more recent entries win).
 *
 * @param a First search result to compare
 * @param b Second search result to compare
 * @return Negative if a > b, positive if a < b, zero if equal
 */
static int compare_results_by_score(const void *a, const void *b) {
    const lle_search_result_t *r1 = (const lle_search_result_t *)a;
    const lle_search_result_t *r2 = (const lle_search_result_t *)b;

    /* Sort by score (descending) */
    if (r1->score > r2->score)
        return -1;
    if (r1->score < r2->score)
        return 1;

    /* Tie-breaker: more recent entry wins */
    if (r1->entry_index > r2->entry_index)
        return -1;
    if (r1->entry_index < r2->entry_index)
        return 1;

    return 0;
}

/**
 * @brief Case-insensitive substring search
 *
 * Finds the first occurrence of needle in haystack, ignoring case.
 *
 * @param haystack String to search in (may be NULL)
 * @param needle String to search for (may be NULL)
 * @return Pointer to first match in haystack, or NULL if not found or NULL
 * inputs
 */
static const char *stristr(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return haystack;

    size_t needle_len = strlen(needle);

    for (const char *p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return p;
        }
    }

    return NULL;
}

/**
 * @brief Case-insensitive prefix match
 *
 * Checks if a string starts with the given prefix, ignoring case.
 *
 * @param str String to check (may be NULL)
 * @param prefix Prefix to match (may be NULL)
 * @return true if str starts with prefix (case-insensitive), false otherwise or
 * on NULL inputs
 */
static bool str_starts_with_i(const char *str, const char *prefix) {
    if (!str || !prefix)
        return false;
    return strncasecmp(str, prefix, strlen(prefix)) == 0;
}

/* ============================================================================
 * PUBLIC API - SEARCH RESULTS MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create search results container
 *
 * Allocates a new search results container with the specified capacity.
 * Uses memory pool allocation.
 *
 * @param max_results Maximum number of results to store (0 = default of 100)
 * @return Pointer to new search results container, or NULL on allocation
 * failure
 */
lle_history_search_results_t *
lle_history_search_results_create(size_t max_results) {
    if (max_results == 0) {
        max_results = DEFAULT_MAX_SEARCH_RESULTS;
    }

    lle_history_search_results_t *results =
        (lle_history_search_results_t *)lle_pool_alloc(
            sizeof(lle_history_search_results_t));

    if (!results) {
        /* Failed to allocate search results */
        return NULL;
    }

    results->results = (lle_search_result_t *)lle_pool_alloc(
        max_results * sizeof(lle_search_result_t));

    if (!results->results) {
        lle_pool_free(results);
        /* Failed to allocate search results array */
        return NULL;
    }

    results->count = 0;
    results->capacity = max_results;
    results->query = NULL;
    results->search_type = LLE_SEARCH_TYPE_EXACT;
    results->search_time_us = 0;
    results->sorted = false;

    return results;
}

/**
 * @brief Destroy search results and free memory
 *
 * Frees all memory associated with the search results container.
 * Safe to call with NULL.
 *
 * @param results Search results container to destroy (may be NULL)
 */
void lle_history_search_results_destroy(lle_history_search_results_t *results) {
    if (!results)
        return;

    if (results->query) {
        lle_pool_free(results->query);
    }

    if (results->results) {
        lle_pool_free(results->results);
    }

    lle_pool_free(results);
}

/**
 * @brief Add result to search results (if not full)
 *
 * Appends a new search result to the container if capacity allows.
 * Marks the results as unsorted after adding.
 *
 * @param results Search results container (must not be NULL)
 * @param entry_id Unique ID of the history entry
 * @param entry_index Index of entry in history
 * @param command Command string (reference, not copied)
 * @param timestamp Entry timestamp
 * @param score Relevance score
 * @param match_position Position of match in command
 * @param match_type Type of match performed
 * @return true if result was added, false if container is NULL or full
 */
static bool add_search_result(lle_history_search_results_t *results,
                              uint64_t entry_id, size_t entry_index,
                              const char *command, uint64_t timestamp,
                              int score, size_t match_position,
                              lle_search_type_t match_type) {
    if (!results || results->count >= results->capacity) {
        return false;
    }

    lle_search_result_t *result = &results->results[results->count];
    result->entry_id = entry_id;
    result->entry_index = entry_index;
    result->command = command; /* Reference, not copy */
    result->timestamp = timestamp;
    result->score = score;
    result->match_position = match_position;
    result->match_type = match_type;

    results->count++;
    results->sorted = false; /* Adding invalidates sort */

    return true;
}

/**
 * @brief Sort search results by score (descending)
 *
 * Sorts the results array by score in descending order.
 * Does nothing if already sorted, empty, or NULL.
 *
 * @param results Search results container to sort (may be NULL)
 */
void lle_history_search_results_sort(lle_history_search_results_t *results) {
    if (!results || results->count == 0 || results->sorted) {
        return;
    }

    qsort(results->results, results->count, sizeof(lle_search_result_t),
          compare_results_by_score);

    results->sorted = true;
}

/* ============================================================================
 * PUBLIC API - SEARCH OPERATIONS
 * ============================================================================
 */

/**
 * @brief Search history for exact command match
 *
 * Finds history entries that exactly match the query string.
 * Results are sorted by relevance score.
 *
 * @param history_core History core engine (must not be NULL)
 * @param query Search query string (must not be NULL)
 * @param max_results Maximum results to return (0 = default of 100)
 * @return Search results container, or NULL on failure or invalid parameters
 */
lle_history_search_results_t *
lle_history_search_exact(lle_history_core_t *history_core, const char *query,
                         size_t max_results) {
    if (!history_core || !query) {
        return NULL;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Create results container */
    lle_history_search_results_t *results =
        lle_history_search_results_create(max_results);
    if (!results) {
        return NULL;
    }

    /* Store query */
    results->query = pool_strdup(query);
    results->search_type = LLE_SEARCH_TYPE_EXACT;

    /* Get total entry count */
    size_t total_entries = 0;
    if (lle_history_get_entry_count(history_core, &total_entries) !=
        LLE_SUCCESS) {
        lle_history_search_results_destroy(results);
        return NULL;
    }

    /* Search backward through history (most recent first) */
    for (size_t i = total_entries; i > 0; i--) {
        size_t index = i - 1;

        /* Get entry */
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history_core, index, &entry) !=
                LLE_SUCCESS ||
            !entry) {
            continue;
        }
        if (!entry || !entry->command) {
            continue;
        }

        /* Check for exact match */
        if (strcmp(entry->command, query) == 0) {
            int score = calculate_score(
                entry->command, query, 0, /* Exact match = position 0 */
                index, total_entries, LLE_SEARCH_TYPE_EXACT);

            add_search_result(results, entry->entry_id, index, entry->command,
                              entry->timestamp, score, 0,
                              LLE_SEARCH_TYPE_EXACT);

            /* Check if we've hit max results */
            if (results->count >= results->capacity) {
                break;
            }
        }
    }

    /* Sort results by score */
    lle_history_search_results_sort(results);

    /* Record search time */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    results->search_time_us =
        (uint64_t)((end_time.tv_sec - start_time.tv_sec) * 1000000 +
                   (end_time.tv_nsec - start_time.tv_nsec) / 1000);

    return results;
}

/**
 * @brief Search history for commands starting with prefix
 *
 * Finds history entries that begin with the prefix string (case-insensitive).
 * Results are sorted by relevance score.
 *
 * @param history_core History core engine (must not be NULL)
 * @param prefix Prefix string to search for (must not be NULL)
 * @param max_results Maximum results to return (0 = default of 100)
 * @return Search results container, or NULL on failure or invalid parameters
 */
lle_history_search_results_t *
lle_history_search_prefix(lle_history_core_t *history_core, const char *prefix,
                          size_t max_results) {
    if (!history_core || !prefix) {
        return NULL;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Create results container */
    lle_history_search_results_t *results =
        lle_history_search_results_create(max_results);
    if (!results) {
        return NULL;
    }

    /* Store query */
    results->query = pool_strdup(prefix);
    results->search_type = LLE_SEARCH_TYPE_PREFIX;

    /* Get total entry count */
    size_t total_entries = 0;
    if (lle_history_get_entry_count(history_core, &total_entries) !=
        LLE_SUCCESS) {
        lle_history_search_results_destroy(results);
        return NULL;
    }

    /* Search backward through history (most recent first) */
    for (size_t i = total_entries; i > 0; i--) {
        size_t index = i - 1;

        /* Get entry */
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history_core, index, &entry) !=
                LLE_SUCCESS ||
            !entry) {
            continue;
        }
        if (!entry->command) {
            continue;
        }

        /* Check for prefix match (case-insensitive) */
        if (str_starts_with_i(entry->command, prefix)) {
            int score = calculate_score(
                entry->command, prefix, 0, /* Prefix match = position 0 */
                index, total_entries, LLE_SEARCH_TYPE_PREFIX);

            add_search_result(results, entry->entry_id, index, entry->command,
                              entry->timestamp, score, 0,
                              LLE_SEARCH_TYPE_PREFIX);

            /* Check if we've hit max results */
            if (results->count >= results->capacity) {
                break;
            }
        }
    }

    /* Sort results by score */
    lle_history_search_results_sort(results);

    /* Record search time */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    results->search_time_us =
        (uint64_t)((end_time.tv_sec - start_time.tv_sec) * 1000000 +
                   (end_time.tv_nsec - start_time.tv_nsec) / 1000);

    return results;
}

/**
 * @brief Search history for commands containing substring
 *
 * Finds history entries that contain the substring (case-insensitive).
 * Results are sorted by relevance score.
 *
 * @param history_core History core engine (must not be NULL)
 * @param substring Substring to search for (must not be NULL)
 * @param max_results Maximum results to return (0 = default of 100)
 * @return Search results container, or NULL on failure or invalid parameters
 */
lle_history_search_results_t *
lle_history_search_substring(lle_history_core_t *history_core,
                             const char *substring, size_t max_results) {
    if (!history_core || !substring) {
        return NULL;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Create results container */
    lle_history_search_results_t *results =
        lle_history_search_results_create(max_results);
    if (!results) {
        return NULL;
    }

    /* Store query */
    results->query = pool_strdup(substring);
    results->search_type = LLE_SEARCH_TYPE_SUBSTRING;

    /* Get total entry count */
    size_t total_entries = 0;
    if (lle_history_get_entry_count(history_core, &total_entries) !=
        LLE_SUCCESS) {
        lle_history_search_results_destroy(results);
        return NULL;
    }

    /* Search backward through history (most recent first) */
    for (size_t i = total_entries; i > 0; i--) {
        size_t index = i - 1;

        /* Get entry */
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history_core, index, &entry) !=
                LLE_SUCCESS ||
            !entry) {
            continue;
        }
        if (!entry->command) {
            continue;
        }

        /* Check for substring match (case-insensitive) */
        const char *match_pos = stristr(entry->command, substring);
        if (match_pos) {
            size_t position = (size_t)(match_pos - entry->command);

            int score =
                calculate_score(entry->command, substring, position, index,
                                total_entries, LLE_SEARCH_TYPE_SUBSTRING);

            add_search_result(results, entry->entry_id, index, entry->command,
                              entry->timestamp, score, position,
                              LLE_SEARCH_TYPE_SUBSTRING);

            /* Check if we've hit max results */
            if (results->count >= results->capacity) {
                break;
            }
        }
    }

    /* Sort results by score */
    lle_history_search_results_sort(results);

    /* Record search time */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    results->search_time_us =
        (uint64_t)((end_time.tv_sec - start_time.tv_sec) * 1000000 +
                   (end_time.tv_nsec - start_time.tv_nsec) / 1000);

    return results;
}

/**
 * @brief Search history for commands with fuzzy matching (Levenshtein distance)
 *
 * Finds history entries that approximately match the query using
 * Levenshtein distance (via libfuzzy). Matches within FUZZY_MAX_DISTANCE
 * are included. Results are sorted by relevance score.
 *
 * @param history_core History core engine (must not be NULL)
 * @param query Query string (must not be NULL)
 * @param max_results Maximum results to return (0 = default of 100)
 * @return Search results container, or NULL on failure or invalid parameters
 */
lle_history_search_results_t *
lle_history_search_fuzzy(lle_history_core_t *history_core, const char *query,
                         size_t max_results) {
    if (!history_core || !query) {
        return NULL;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Create results container */
    lle_history_search_results_t *results =
        lle_history_search_results_create(max_results);
    if (!results) {
        return NULL;
    }

    /* Store query */
    results->query = pool_strdup(query);
    results->search_type = LLE_SEARCH_TYPE_FUZZY;

    /* Get total entry count */
    size_t total_entries = 0;
    if (lle_history_get_entry_count(history_core, &total_entries) !=
        LLE_SUCCESS) {
        lle_history_search_results_destroy(results);
        return NULL;
    }

    /* Search backward through history (most recent first) */
    for (size_t i = total_entries; i > 0; i--) {
        size_t index = i - 1;

        /* Get entry */
        lle_history_entry_t *entry = NULL;
        if (lle_history_get_entry_by_index(history_core, index, &entry) !=
                LLE_SUCCESS ||
            !entry) {
            continue;
        }
        if (!entry->command) {
            continue;
        }

        /* Calculate Levenshtein distance using libfuzzy (Unicode-aware) */
        fuzzy_match_options_t opts = FUZZY_MATCH_DEFAULT;
        opts.case_sensitive = false;
        int distance = fuzzy_levenshtein_distance(entry->command, query, &opts);

        /* Accept if within fuzzy threshold */
        if (distance >= 0 && distance <= FUZZY_MAX_DISTANCE) {
            /* Lower distance = higher score */
            int score = calculate_score(entry->command, query, 0, index,
                                        total_entries, LLE_SEARCH_TYPE_FUZZY);

            /* Adjust score based on distance (closer = better) */
            score -= distance * 50;

            add_search_result(results, entry->entry_id, index, entry->command,
                              entry->timestamp, score, 0,
                              LLE_SEARCH_TYPE_FUZZY);

            /* Check if we've hit max results */
            if (results->count >= results->capacity) {
                break;
            }
        }
    }

    /* Sort results by score */
    lle_history_search_results_sort(results);

    /* Record search time */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    results->search_time_us =
        (uint64_t)((end_time.tv_sec - start_time.tv_sec) * 1000000 +
                   (end_time.tv_nsec - start_time.tv_nsec) / 1000);

    return results;
}

/* ============================================================================
 * PUBLIC API - SEARCH UTILITIES
 * ============================================================================
 */

/**
 * @brief Get number of results in search results
 *
 * Returns the count of results stored in the container.
 *
 * @param results Search results container (may be NULL)
 * @return Number of results, or 0 if results is NULL
 */
size_t lle_history_search_results_get_count(
    const lle_history_search_results_t *results) {
    return results ? results->count : 0;
}

/**
 * @brief Get specific result from search results
 *
 * Returns a pointer to the result at the specified index.
 *
 * @param results Search results container (may be NULL)
 * @param index Index of result to retrieve (0-based)
 * @return Pointer to search result, or NULL if results is NULL or index out of
 * bounds
 */
const lle_search_result_t *
lle_history_search_results_get(const lle_history_search_results_t *results,
                               size_t index) {
    if (!results || index >= results->count) {
        return NULL;
    }

    return &results->results[index];
}

/**
 * @brief Get search duration in microseconds
 *
 * Returns how long the search operation took to complete.
 *
 * @param results Search results container (may be NULL)
 * @return Search duration in microseconds, or 0 if results is NULL
 */
uint64_t lle_history_search_results_get_time_us(
    const lle_history_search_results_t *results) {
    return results ? results->search_time_us : 0;
}

/**
 * @brief Print search results (for debugging)
 *
 * Outputs the search results to stdout in a human-readable format,
 * including query, type, count, timing, and individual results.
 *
 * @param results Search results container to print (may be NULL)
 */
void lle_history_search_results_print(
    const lle_history_search_results_t *results) {
    if (!results) {
        printf("Search results: NULL\n");
        return;
    }

    printf("Search Results:\n");
    printf("  Query: '%s'\n", results->query ? results->query : "(null)");
    printf("  Type: %s\n",
           results->search_type == LLE_SEARCH_TYPE_EXACT       ? "exact"
           : results->search_type == LLE_SEARCH_TYPE_PREFIX    ? "prefix"
           : results->search_type == LLE_SEARCH_TYPE_SUBSTRING ? "substring"
                                                               : "fuzzy");
    printf("  Count: %zu / %zu\n", results->count, results->capacity);
    printf("  Time: %" PRIu64 " μs\n", results->search_time_us);
    printf("  Results:\n");

    for (size_t i = 0; i < results->count; i++) {
        const lle_search_result_t *r = &results->results[i];
        printf("    [%zu] score=%d pos=%zu cmd=\"%s\"\n", i, r->score,
               r->match_position, r->command);
    }
}
