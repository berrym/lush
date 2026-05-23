/**
 * @file fuzzy_match.c
 * @brief Unicode-Aware Fuzzy String Matching Implementation
 *
 * Implements multiple fuzzy matching algorithms with Unicode support.
 * Uses NFC normalization from lle/unicode_compare.h for consistent matching.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 */

#include "fuzzy_match.h"
#include "lle/unicode_compare.h"
#include "lle/utf8_support.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CONSTANTS AND DEFAULT OPTIONS
 * ============================================================================
 */

const fuzzy_match_options_t FUZZY_MATCH_DEFAULT = {
    .case_sensitive = false,
    .unicode_normalize = true,
    .use_damerau = true,
    .max_distance = 0 // unlimited
};

const fuzzy_match_options_t FUZZY_MATCH_STRICT = {.case_sensitive = true,
                                                  .unicode_normalize = false,
                                                  .use_damerau = false,
                                                  .max_distance = 0};

const fuzzy_match_options_t FUZZY_MATCH_FAST = {.case_sensitive = false,
                                                .unicode_normalize = false,
                                                .use_damerau = false,
                                                .max_distance = 0};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

// Buffer size for normalized strings
#define NORM_BUFFER_SIZE 4096

// Maximum codepoints to process (prevent DoS on huge strings)
#define MAX_CODEPOINTS 1024

/**
 * @brief Internal structure for holding decoded codepoints
 */
typedef struct {
    uint32_t codepoints[MAX_CODEPOINTS];
    int length;
} codepoint_array_t;

/**
 * @brief Decode UTF-8 string to codepoint array with optional normalization
 * @param str Input UTF-8 string to decode
 * @param out Output codepoint array
 * @param opts Fuzzy match options for normalization and case folding
 * @return Number of codepoints decoded, or -1 on error
 */
static int decode_to_codepoints(const char *str, codepoint_array_t *out,
                                const fuzzy_match_options_t *opts) {
    if (!str || !out) {
        return -1;
    }

    out->length = 0;

    const char *input = str;
    size_t input_len = strlen(str);

    // Normalize if requested
    char norm_buf[NORM_BUFFER_SIZE];
    if (opts && opts->unicode_normalize) {
        size_t norm_len;
        if (lle_unicode_normalize_nfc(str, input_len, norm_buf,
                                      NORM_BUFFER_SIZE, &norm_len) == 0) {
            input = norm_buf;
            input_len = norm_len;
        }
        // If normalization fails, use original string
    }

    // Decode to codepoints
    const char *ptr = input;
    const char *end = input + input_len;

    while (ptr < end && out->length < MAX_CODEPOINTS) {
        uint32_t cp;
        int len = lle_utf8_decode_codepoint(ptr, end - ptr, &cp);
        if (len <= 0) {
            // Invalid UTF-8, try to skip one byte
            ptr++;
            continue;
        }

        // Case folding if case-insensitive
        if (opts && !opts->case_sensitive) {
            // Simple ASCII case folding - for full Unicode would need ICU
            if (cp >= 'A' && cp <= 'Z') {
                cp = cp - 'A' + 'a';
            }
            // Latin-1 Supplement uppercase
            else if (cp >= 0x00C0 && cp <= 0x00D6) {
                cp += 0x20;
            } else if (cp >= 0x00D8 && cp <= 0x00DE) {
                cp += 0x20;
            }
        }

        out->codepoints[out->length++] = cp;
        ptr += len;
    }

    return out->length;
}

/**
 * @brief Return minimum of two integers
 * @param a First integer
 * @param b Second integer
 * @return The smaller of a and b
 */
static inline int min2(int a, int b) { return (a < b) ? a : b; }

/**
 * @brief Return minimum of three integers
 * @param a First integer
 * @param b Second integer
 * @param c Third integer
 * @return The smallest of a, b, and c
 */
static inline int min3(int a, int b, int c) {
    int min_ab = (a < b) ? a : b;
    return (min_ab < c) ? min_ab : c;
}

/**
 * @brief Return maximum of two integers
 * @param a First integer
 * @param b Second integer
 * @return The larger of a and b
 */
static inline int max2(int a, int b) { return (a > b) ? a : b; }

/* ============================================================================
 * LEVENSHTEIN DISTANCE
 * ============================================================================
 */

/**
 * @brief Calculate Levenshtein distance between two codepoint arrays
 * @param s1 First codepoint array
 * @param s2 Second codepoint array
 * @param max_dist Maximum distance threshold (0 for unlimited)
 * @return Edit distance between the arrays
 */
static int levenshtein_codepoints(const codepoint_array_t *s1,
                                  const codepoint_array_t *s2, int max_dist) {
    int len1 = s1->length;
    int len2 = s2->length;

    // Early exit for empty strings
    if (len1 == 0)
        return len2;
    if (len2 == 0)
        return len1;

    // Early exit if length difference exceeds max distance
    if (max_dist > 0 && abs(len1 - len2) > max_dist) {
        return max_dist + 1;
    }

    // Use two-row optimization for memory efficiency
    int *prev_row = malloc((len2 + 1) * sizeof(int));
    int *curr_row = malloc((len2 + 1) * sizeof(int));

    if (!prev_row || !curr_row) {
        free(prev_row);
        free(curr_row);
        return len1 + len2; // Worst case on allocation failure
    }

    // Initialize first row
    for (int j = 0; j <= len2; j++) {
        prev_row[j] = j;
    }

    // Fill matrix row by row
    for (int i = 1; i <= len1; i++) {
        curr_row[0] = i;

        int row_min = curr_row[0]; // Track minimum in row for early exit

        for (int j = 1; j <= len2; j++) {
            int cost = (s1->codepoints[i - 1] == s2->codepoints[j - 1]) ? 0 : 1;

            curr_row[j] = min3(prev_row[j] + 1,       // deletion
                               curr_row[j - 1] + 1,   // insertion
                               prev_row[j - 1] + cost // substitution
            );

            if (curr_row[j] < row_min) {
                row_min = curr_row[j];
            }
        }

        // Early exit if minimum in row exceeds max distance
        if (max_dist > 0 && row_min > max_dist) {
            free(prev_row);
            free(curr_row);
            return max_dist + 1;
        }

        // Swap rows
        int *temp = prev_row;
        prev_row = curr_row;
        curr_row = temp;
    }

    int result = prev_row[len2];

    free(prev_row);
    free(curr_row);

    return result;
}

/**
 * @brief Calculate Levenshtein edit distance between two strings
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Edit distance between the strings
 */
int fuzzy_levenshtein_distance(const char *s1, const char *s2,
                               const fuzzy_match_options_t *options) {
    if (!s1 && !s2)
        return 0;
    if (!s1)
        return strlen(s2);
    if (!s2)
        return strlen(s1);

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp1, cp2;
    if (decode_to_codepoints(s1, &cp1, opts) < 0 ||
        decode_to_codepoints(s2, &cp2, opts) < 0) {
        // Fallback to byte-level comparison on decode failure
        return abs((int)strlen(s1) - (int)strlen(s2));
    }

    return levenshtein_codepoints(&cp1, &cp2, opts->max_distance);
}

/* ============================================================================
 * DAMERAU-LEVENSHTEIN DISTANCE
 * ============================================================================
 */

/**
 * @brief Calculate Damerau-Levenshtein distance between two codepoint arrays
 *
 * Includes transpositions as a single edit operation.
 *
 * @param s1 First codepoint array
 * @param s2 Second codepoint array
 * @param max_dist Maximum distance threshold (0 for unlimited)
 * @return Edit distance between the arrays
 */
static int damerau_levenshtein_codepoints(const codepoint_array_t *s1,
                                          const codepoint_array_t *s2,
                                          int max_dist) {
    int len1 = s1->length;
    int len2 = s2->length;

    if (len1 == 0)
        return len2;
    if (len2 == 0)
        return len1;

    if (max_dist > 0 && abs(len1 - len2) > max_dist) {
        return max_dist + 1;
    }

    /* Need full matrix for Damerau-Levenshtein (transpositions need
     * d[i-2][j-2]) */
    int **d = malloc((len1 + 1) * sizeof(int *));
    if (!d)
        return len1 + len2;

    for (int i = 0; i <= len1; i++) {
        d[i] = malloc((size_t)(len2 + 1) * sizeof(int));
        if (!d[i]) {
            for (int j = 0; j < i; j++)
                free(d[j]);
            free(d);
            return len1 + len2;
        }
    }

    // Initialize
    for (int i = 0; i <= len1; i++)
        d[i][0] = i;
    for (int j = 0; j <= len2; j++)
        d[0][j] = j;

    /* Fill matrix. The result lives in d[len1][len2]; track it directly
     * in a local so gcc -Wmaybe-uninitialized can see the write
     * dominates the read. The early returns above already guarantee
     * len1 >= 1 && len2 >= 1, but gcc's data-flow only watches scalars,
     * not memory cells. */
    int result = 0;
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1->codepoints[i - 1] == s2->codepoints[j - 1]) ? 0 : 1;

            d[i][j] = min3(d[i - 1][j] + 1,       // deletion
                           d[i][j - 1] + 1,       // insertion
                           d[i - 1][j - 1] + cost // substitution
            );

            // Check for transposition
            if (i > 1 && j > 1 &&
                s1->codepoints[i - 1] == s2->codepoints[j - 2] &&
                s1->codepoints[i - 2] == s2->codepoints[j - 1]) {
                d[i][j] = min2(d[i][j], d[i - 2][j - 2] + cost);
            }

            if (i == len1 && j == len2) {
                result = d[i][j];
            }
        }
    }

    // Cleanup
    for (int i = 0; i <= len1; i++)
        free(d[i]);
    free(d);

    return result;
}

/**
 * @brief Calculate Damerau-Levenshtein edit distance between two strings
 *
 * Includes transpositions as a single edit operation.
 *
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Edit distance between the strings
 */
int fuzzy_damerau_levenshtein_distance(const char *s1, const char *s2,
                                       const fuzzy_match_options_t *options) {
    if (!s1 && !s2)
        return 0;
    if (!s1)
        return strlen(s2);
    if (!s2)
        return strlen(s1);

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp1, cp2;
    if (decode_to_codepoints(s1, &cp1, opts) < 0 ||
        decode_to_codepoints(s2, &cp2, opts) < 0) {
        return abs((int)strlen(s1) - (int)strlen(s2));
    }

    return damerau_levenshtein_codepoints(&cp1, &cp2, opts->max_distance);
}

/* ============================================================================
 * JARO AND JARO-WINKLER SIMILARITY
 * ============================================================================
 */

/**
 * @brief Calculate Jaro similarity between two codepoint arrays
 * @param s1 First codepoint array
 * @param s2 Second codepoint array
 * @return Jaro similarity score between 0.0 and 1.0
 */
static double jaro_codepoints(const codepoint_array_t *s1,
                              const codepoint_array_t *s2) {
    int len1 = s1->length;
    int len2 = s2->length;

    if (len1 == 0 && len2 == 0)
        return 1.0;
    if (len1 == 0 || len2 == 0)
        return 0.0;

    // Calculate match window
    int match_window = max2(len1, len2) / 2 - 1;
    if (match_window < 0)
        match_window = 0;

    // Track matches
    bool *s1_matches = calloc(len1, sizeof(bool));
    bool *s2_matches = calloc(len2, sizeof(bool));

    if (!s1_matches || !s2_matches) {
        free(s1_matches);
        free(s2_matches);
        return 0.0;
    }

    int matches = 0;

    // Find matches within window
    for (int i = 0; i < len1; i++) {
        int start = (i - match_window > 0) ? i - match_window : 0;
        int end = (i + match_window < len2) ? i + match_window : len2 - 1;

        for (int j = start; j <= end; j++) {
            if (s2_matches[j] || s1->codepoints[i] != s2->codepoints[j]) {
                continue;
            }
            s1_matches[i] = s2_matches[j] = true;
            matches++;
            break;
        }
    }

    if (matches == 0) {
        free(s1_matches);
        free(s2_matches);
        return 0.0;
    }

    // Count transpositions
    int transpositions = 0;
    int k = 0;
    for (int i = 0; i < len1; i++) {
        if (!s1_matches[i])
            continue;
        while (!s2_matches[k])
            k++;
        if (s1->codepoints[i] != s2->codepoints[k]) {
            transpositions++;
        }
        k++;
    }

    free(s1_matches);
    free(s2_matches);

    double jaro = ((double)matches / len1 + (double)matches / len2 +
                   (double)(matches - transpositions / 2) / matches) /
                  3.0;

    return jaro;
}

/**
 * @brief Calculate Jaro similarity score between two strings
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Similarity score from 0 to 100
 */
int fuzzy_jaro_score(const char *s1, const char *s2,
                     const fuzzy_match_options_t *options) {
    if (!s1 && !s2)
        return 100;
    if (!s1 || !s2)
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp1, cp2;
    if (decode_to_codepoints(s1, &cp1, opts) < 0 ||
        decode_to_codepoints(s2, &cp2, opts) < 0) {
        return 0;
    }

    double jaro = jaro_codepoints(&cp1, &cp2);
    return (int)(jaro * 100);
}

/**
 * @brief Calculate Jaro-Winkler similarity score between two strings
 *
 * Applies a prefix bonus to the Jaro similarity score.
 *
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Similarity score from 0 to 100
 */
int fuzzy_jaro_winkler_score(const char *s1, const char *s2,
                             const fuzzy_match_options_t *options) {
    if (!s1 && !s2)
        return 100;
    if (!s1 || !s2)
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp1, cp2;
    if (decode_to_codepoints(s1, &cp1, opts) < 0 ||
        decode_to_codepoints(s2, &cp2, opts) < 0) {
        return 0;
    }

    double jaro = jaro_codepoints(&cp1, &cp2);

    // Calculate common prefix (max 4 chars for Winkler bonus)
    int prefix_len = 0;
    int max_prefix = min2(min2(cp1.length, cp2.length), 4);

    for (int i = 0; i < max_prefix; i++) {
        if (cp1.codepoints[i] == cp2.codepoints[i]) {
            prefix_len++;
        } else {
            break;
        }
    }

    // Jaro-Winkler formula: jaro + (prefix_len * 0.1 * (1 - jaro))
    double jaro_winkler = jaro + (0.1 * prefix_len * (1.0 - jaro));

    return (int)(jaro_winkler * 100);
}

/* ============================================================================
 * COMMON PREFIX AND SUBSEQUENCE
 * ============================================================================
 */

/**
 * @brief Calculate the length of the common prefix between two strings
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Length of common prefix in codepoints
 */
int fuzzy_common_prefix_length(const char *s1, const char *s2,
                               const fuzzy_match_options_t *options) {
    if (!s1 || !s2)
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp1, cp2;
    if (decode_to_codepoints(s1, &cp1, opts) < 0 ||
        decode_to_codepoints(s2, &cp2, opts) < 0) {
        return 0;
    }

    int prefix_len = 0;
    int max_len = min2(cp1.length, cp2.length);

    for (int i = 0; i < max_len; i++) {
        if (cp1.codepoints[i] == cp2.codepoints[i]) {
            prefix_len++;
        } else {
            break;
        }
    }

    return prefix_len;
}

/**
 * @brief Calculate subsequence match score between pattern and text
 * @param pattern Pattern string to match
 * @param text Text string to search in
 * @param options Fuzzy match options (NULL for defaults)
 * @return Score from 0 to 100 based on how much of pattern matches as
 * subsequence
 */
int fuzzy_subsequence_score(const char *pattern, const char *text,
                            const fuzzy_match_options_t *options) {
    if (!pattern || !text)
        return 0;
    if (*pattern == '\0')
        return 100;
    if (*text == '\0')
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t pat, txt;
    if (decode_to_codepoints(pattern, &pat, opts) < 0 ||
        decode_to_codepoints(text, &txt, opts) < 0) {
        return 0;
    }

    if (pat.length == 0)
        return 100;
    if (txt.length == 0)
        return 0;

    int matches = 0;
    int txt_idx = 0;

    for (int p = 0; p < pat.length && txt_idx < txt.length; p++) {
        for (; txt_idx < txt.length; txt_idx++) {
            if (pat.codepoints[p] == txt.codepoints[txt_idx]) {
                matches++;
                txt_idx++;
                break;
            }
        }
    }

    return (matches * 100) / pat.length;
}

/**
 * @brief Check if pattern is a subsequence of text
 * @param pattern Pattern string to match
 * @param text Text string to search in
 * @param options Fuzzy match options (NULL for defaults)
 * @return true if pattern is a subsequence of text, false otherwise
 */
bool fuzzy_is_subsequence(const char *pattern, const char *text,
                          const fuzzy_match_options_t *options) {
    if (!pattern || *pattern == '\0')
        return true;
    if (!text || *text == '\0')
        return false;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t pat, txt;
    if (decode_to_codepoints(pattern, &pat, opts) < 0 ||
        decode_to_codepoints(text, &txt, opts) < 0) {
        return false;
    }

    int txt_idx = 0;
    for (int p = 0; p < pat.length; p++) {
        bool found = false;
        for (; txt_idx < txt.length; txt_idx++) {
            if (pat.codepoints[p] == txt.codepoints[txt_idx]) {
                found = true;
                txt_idx++;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

/* ============================================================================
 * COMBINED SCORE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Calculate combined fuzzy match score between two strings
 *
 * Uses weighted combination of edit distance, Jaro-Winkler, prefix, and
 * subsequence scores for comprehensive matching.
 *
 * @param s1 First string
 * @param s2 Second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Combined similarity score from 0 to 100
 */
int fuzzy_match_score(const char *s1, const char *s2,
                      const fuzzy_match_options_t *options) {
    if (!s1 && !s2)
        return 100;
    if (!s1 || !s2)
        return 0;
    if (*s1 == '\0' && *s2 == '\0')
        return 100;
    if (*s1 == '\0' || *s2 == '\0')
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    // Check for exact match first
    lle_unicode_compare_options_t cmp_opts = {
        .normalize = opts->unicode_normalize,
        .case_insensitive = !opts->case_sensitive,
        .trim_whitespace = false};

    if (lle_unicode_strings_equal(s1, s2, &cmp_opts)) {
        return 100;
    }

    // Calculate individual scores
    int edit_distance;
    if (opts->use_damerau) {
        edit_distance = fuzzy_damerau_levenshtein_distance(s1, s2, opts);
    } else {
        edit_distance = fuzzy_levenshtein_distance(s1, s2, opts);
    }

    int len1 = fuzzy_string_length(s1, opts);
    int len2 = fuzzy_string_length(s2, opts);
    int max_len = max2(len1, len2);

    int levenshtein_score = fuzzy_distance_to_score(edit_distance, max_len);
    int jaro_score = fuzzy_jaro_winkler_score(s1, s2, opts);

    int prefix_len = fuzzy_common_prefix_length(s1, s2, opts);
    int avg_len = (len1 + len2) / 2;
    int prefix_score = (avg_len > 0) ? (prefix_len * 100) / avg_len : 0;
    if (prefix_score > 100)
        prefix_score = 100;

    int subseq_score = fuzzy_subsequence_score(s1, s2, opts);

    /* Weighted combination:
     * - Edit distance: 40% (most reliable for typos)
     * - Jaro-Winkler: 30% (good for short strings, prefix aware)
     * - Prefix: 20% (important for command matching)
     * - Subsequence: 10% (good for abbreviations)
     */
    int final_score = (levenshtein_score * 4 + jaro_score * 3 +
                       prefix_score * 2 + subseq_score * 1) /
                      10;

    if (final_score > 100)
        final_score = 100;
    if (final_score < 0)
        final_score = 0;

    return final_score;
}

/**
 * @brief Calculate fuzzy match score for length-specified strings
 * @param s1 First string
 * @param len1 Length of first string
 * @param s2 Second string
 * @param len2 Length of second string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Combined similarity score from 0 to 100
 */
int fuzzy_match_score_n(const char *s1, size_t len1, const char *s2,
                        size_t len2, const fuzzy_match_options_t *options) {
    // Create null-terminated copies
    char *copy1 = malloc(len1 + 1);
    char *copy2 = malloc(len2 + 1);

    if (!copy1 || !copy2) {
        free(copy1);
        free(copy2);
        return 0;
    }

    memcpy(copy1, s1, len1);
    copy1[len1] = '\0';
    memcpy(copy2, s2, len2);
    copy2[len2] = '\0';

    int result = fuzzy_match_score(copy1, copy2, options);

    free(copy1);
    free(copy2);

    return result;
}

/**
 * @brief Check if two strings match above a threshold
 * @param s1 First string
 * @param s2 Second string
 * @param threshold Minimum score to consider a match (0-100)
 * @param options Fuzzy match options (NULL for defaults)
 * @return true if match score is at or above threshold, false otherwise
 */
bool fuzzy_match_is_match(const char *s1, const char *s2, int threshold,
                          const fuzzy_match_options_t *options) {
    return fuzzy_match_score(s1, s2, options) >= threshold;
}

/* ============================================================================
 * BATCH MATCHING
 * ============================================================================
 */

/**
 * @brief Comparison function for sorting results by score (descending)
 * @param a First result to compare
 * @param b Second result to compare
 * @return Negative if a > b, positive if a < b, zero if equal
 */
static int compare_results(const void *a, const void *b) {
    const fuzzy_match_result_t *ra = (const fuzzy_match_result_t *)a;
    const fuzzy_match_result_t *rb = (const fuzzy_match_result_t *)b;
    return rb->score - ra->score; // Descending
}

/**
 * @brief Find best matches for a pattern among candidates
 * @param pattern Pattern string to match against
 * @param candidates Array of candidate strings
 * @param num_candidates Number of candidates in array
 * @param results Output array for results (sorted by score descending)
 * @param max_results Maximum number of results to return
 * @param threshold Minimum score to include in results (0-100)
 * @param options Fuzzy match options (NULL for defaults)
 * @return Number of results returned
 */
int fuzzy_match_best(const char *pattern, const char **candidates,
                     int num_candidates, fuzzy_match_result_t *results,
                     int max_results, int threshold,
                     const fuzzy_match_options_t *options) {
    if (!pattern || !candidates || !results || max_results <= 0) {
        return 0;
    }

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    // Score all candidates
    fuzzy_match_result_t *all_results =
        malloc(num_candidates * sizeof(fuzzy_match_result_t));
    if (!all_results)
        return 0;

    int count = 0;
    for (int i = 0; i < num_candidates; i++) {
        if (!candidates[i])
            continue;

        int score = fuzzy_match_score(pattern, candidates[i], opts);
        if (score >= threshold) {
            all_results[count].text = candidates[i];
            all_results[count].score = score;
            all_results[count].index = i;
            count++;
        }
    }

    // Sort by score (descending)
    qsort(all_results, count, sizeof(fuzzy_match_result_t), compare_results);

    // Copy top results
    int result_count = (count < max_results) ? count : max_results;
    memcpy(results, all_results, result_count * sizeof(fuzzy_match_result_t));

    free(all_results);
    return result_count;
}

/**
 * @brief Filter candidates that match pattern above threshold
 * @param pattern Pattern string to match against
 * @param candidates Array of candidate strings
 * @param num_candidates Number of candidates in array
 * @param indices Output array for indices of matching candidates
 * @param max_indices Maximum number of indices to return
 * @param threshold Minimum score to include (0-100)
 * @param options Fuzzy match options (NULL for defaults)
 * @return Number of matching candidates found
 */
int fuzzy_match_filter(const char *pattern, const char **candidates,
                       int num_candidates, int *indices, int max_indices,
                       int threshold, const fuzzy_match_options_t *options) {
    if (!pattern || !candidates || !indices || max_indices <= 0) {
        return 0;
    }

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    int count = 0;
    for (int i = 0; i < num_candidates && count < max_indices; i++) {
        if (!candidates[i])
            continue;

        int score = fuzzy_match_score(pattern, candidates[i], opts);
        if (score >= threshold) {
            indices[count++] = i;
        }
    }

    return count;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Convert edit distance to similarity score
 * @param distance Edit distance value
 * @param max_len Maximum string length for normalization
 * @return Similarity score from 0 to 100
 */
int fuzzy_distance_to_score(int distance, int max_len) {
    if (max_len <= 0)
        return 100;
    if (distance <= 0)
        return 100;
    if (distance >= max_len)
        return 0;

    return ((max_len - distance) * 100) / max_len;
}

/**
 * @brief Get Unicode-aware string length in codepoints
 * @param s Input string
 * @param options Fuzzy match options (NULL for defaults)
 * @return Length in codepoints, or byte length on decode failure
 */
int fuzzy_string_length(const char *s, const fuzzy_match_options_t *options) {
    if (!s)
        return 0;

    const fuzzy_match_options_t *opts =
        options ? options : &FUZZY_MATCH_DEFAULT;

    codepoint_array_t cp;
    if (decode_to_codepoints(s, &cp, opts) < 0) {
        return strlen(s); // Fallback to byte length
    }

    return cp.length;
}
