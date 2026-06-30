/**
 * @file fuzzy_match.h
 * @brief Unicode-aware fuzzy string matching library
 *
 * Provides fuzzy string matching algorithms with full Unicode support.
 * Used by both the shell's autocorrect system and LLE's completion/history
 * search.
 *
 * Features:
 * - Levenshtein (edit) distance
 * - Damerau-Levenshtein distance (with transpositions)
 * - Jaro-Winkler similarity
 * - Subsequence matching
 * - Combined weighted scoring
 * - Unicode NFC normalization support
 * - Case-insensitive matching with Unicode case folding
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef FUZZY_MATCH_H
#define FUZZY_MATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================
 */

/**
 * Fuzzy matching options
 */
typedef struct fuzzy_match_options {
    bool case_sensitive;    ///< Case-sensitive comparison (default: false)
    bool unicode_normalize; ///< Use NFC normalization (default: true)
    bool use_damerau;       ///< Use Damerau-Levenshtein instead of Levenshtein
    int max_distance; ///< Maximum edit distance to consider (0 = unlimited)
} fuzzy_match_options_t;

/**
 * Default options: case-insensitive, Unicode-normalized
 */
extern const fuzzy_match_options_t FUZZY_MATCH_DEFAULT;

/**
 * Strict options: case-sensitive, no Unicode normalization
 */
extern const fuzzy_match_options_t FUZZY_MATCH_STRICT;

/**
 * Fast options: case-insensitive, no Unicode normalization (byte-level)
 */
extern const fuzzy_match_options_t FUZZY_MATCH_FAST;

/* ============================================================================
 * SIMILARITY SCORE FUNCTIONS
 * ============================================================================
 */

/**
 * Calculate combined fuzzy similarity score between two strings
 *
 * Uses a weighted combination of multiple algorithms:
 * - Levenshtein/Damerau-Levenshtein distance (40%)
 * - Jaro-Winkler similarity (30%)
 * - Common prefix bonus (20%)
 * - Subsequence score (10%)
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Similarity score 0-100 (100 = identical)
 */
int fuzzy_match_score(const char *s1, const char *s2,
                      const fuzzy_match_options_t *options);

/**
 * Calculate similarity score with length parameters
 *
 * @param s1      First string (UTF-8)
 * @param len1    Length of first string in bytes
 * @param s2      Second string (UTF-8)
 * @param len2    Length of second string in bytes
 * @param options Matching options (NULL for defaults)
 * @return Similarity score 0-100
 */
int fuzzy_match_score_n(const char *s1, size_t len1, const char *s2,
                        size_t len2, const fuzzy_match_options_t *options);

/**
 * Check if two strings are a fuzzy match above a threshold
 *
 * @param s1        First string (UTF-8)
 * @param s2        Second string (UTF-8)
 * @param threshold Minimum score to consider a match (0-100)
 * @param options   Matching options (NULL for defaults)
 * @return true if score >= threshold
 */
bool fuzzy_match_is_match(const char *s1, const char *s2, int threshold,
                          const fuzzy_match_options_t *options);

/* ============================================================================
 * INDIVIDUAL ALGORITHM FUNCTIONS
 * ============================================================================
 */

/**
 * Calculate Levenshtein (edit) distance
 *
 * Counts minimum single-character edits (insertions, deletions, substitutions)
 * needed to transform s1 into s2.
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Edit distance (0 = identical, higher = more different)
 */
int fuzzy_levenshtein_distance(const char *s1, const char *s2,
                               const fuzzy_match_options_t *options);

/**
 * Calculate Damerau-Levenshtein distance
 *
 * Like Levenshtein but also considers transpositions (swapping adjacent chars)
 * as a single edit. Better for detecting typos like "teh" -> "the".
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Edit distance with transpositions
 */
int fuzzy_damerau_levenshtein_distance(const char *s1, const char *s2,
                                       const fuzzy_match_options_t *options);

/**
 * Calculate a typo-weighted Damerau-Levenshtein distance
 *
 * Like fuzzy_damerau_levenshtein_distance, but transposition costs slightly
 * less than insertion/deletion/substitution (9 vs 10), because an
 * adjacent-character swap is the most common typo. Used to RANK
 * spell-correction candidates so a transposition typo ("gti") prefers the real
 * word ("git", one transposition) over an unrelated single-substitution
 * neighbour ("gtr"), which a plain edit distance ties. The result scales op
 * costs by 10, so it is comparable only against other
 * fuzzy_weighted_edit_distance results, not a plain distance.
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Weighted edit distance (lower is closer)
 */
int fuzzy_weighted_edit_distance(const char *s1, const char *s2,
                                 const fuzzy_match_options_t *options);

/**
 * Calculate Jaro similarity
 *
 * Measures character matches within a sliding window, accounting for
 * transpositions. Good for short strings and typo detection.
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Similarity score 0-100
 */
int fuzzy_jaro_score(const char *s1, const char *s2,
                     const fuzzy_match_options_t *options);

/**
 * Calculate Jaro-Winkler similarity
 *
 * Jaro similarity with a bonus for matching prefixes.
 * Especially good for command names that often share prefixes.
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Similarity score 0-100
 */
int fuzzy_jaro_winkler_score(const char *s1, const char *s2,
                             const fuzzy_match_options_t *options);

/**
 * Calculate common prefix length
 *
 * @param s1      First string (UTF-8)
 * @param s2      Second string (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Length of common prefix in codepoints
 */
int fuzzy_common_prefix_length(const char *s1, const char *s2,
                               const fuzzy_match_options_t *options);

/**
 * Calculate subsequence match score
 *
 * Checks if characters of s1 appear in order in s2 (not necessarily adjacent).
 * Good for fuzzy completion like "gco" matching "git checkout".
 *
 * @param pattern Pattern string (UTF-8)
 * @param text    Text to search in (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return Score 0-100 based on match quality
 */
int fuzzy_subsequence_score(const char *pattern, const char *text,
                            const fuzzy_match_options_t *options);

/**
 * Check if pattern is a subsequence of text
 *
 * @param pattern Pattern string (UTF-8)
 * @param text    Text to search in (UTF-8)
 * @param options Matching options (NULL for defaults)
 * @return true if all characters of pattern appear in order in text
 */
bool fuzzy_is_subsequence(const char *pattern, const char *text,
                          const fuzzy_match_options_t *options);

/**
 * @brief Calculate fzy-style completion match score
 *
 * Optimized for "user typed a partial prefix or shorthand" rather than
 * "user typed a near-complete word with typos". This is the right scorer
 * for completion menus and type-to-filter narrowing; fuzzy_match_score()
 * is the right scorer for autocorrect ("did you mean") use cases.
 *
 * Algorithm: subsequence gate followed by dynamic-programming ranking
 * with positional bonuses, modeled on fzy:
 *
 *   - **Subsequence gate**: returns 0 if pattern is not a subsequence
 *     of text. No partial credit for near-misses.
 *   - **First-char bonus**: largest bonus when pattern[0] matches
 *     text[0] -- "ls<TAB>" should prefer `ls` over `tools`.
 *   - **Word-boundary bonus**: match at the start of a word
 *     (preceded by `/`, `.`, `-`, `_`, space, or any non-letter)
 *     scores well. `git c<TAB>` prefers `git checkout` over `gcc -o`.
 *   - **CamelCase boundary bonus**: match at a lowercase->uppercase
 *     transition scores well. `gc<TAB>` matches `GitCommit` strongly.
 *   - **Adjacency bonus**: consecutive pattern chars matched at
 *     consecutive text positions stack -- `gco<TAB>` prefers
 *     `git-co` (adjacent `co`) over `goc` (split).
 *   - **Gap penalty**: characters skipped between matches reduce
 *     the score. Leading gaps (before the first match) penalized
 *     less, since they're often unavoidable for tail-of-text matches.
 *
 * Empty pattern returns 100 (every candidate trivially matches).
 * Identical pattern/text returns 100 (exact match).
 *
 * @param pattern Pattern string the user typed (UTF-8). Subject to
 *                NFC normalization and case folding per @p options.
 * @param text    Candidate string (UTF-8). Same normalization.
 * @param options Matching options (NULL for FUZZY_MATCH_DEFAULT).
 *                @c case_sensitive @c false (default) makes matching
 *                case-insensitive while still detecting CamelCase
 *                boundaries from the original casing.
 * @return Score 0-100. 0 means pattern is not a subsequence of
 *         text. Higher scores indicate stronger matches.
 */
int fuzzy_completion_score(const char *pattern, const char *text,
                           const fuzzy_match_options_t *options);

/* ============================================================================
 * BATCH MATCHING (for completion/suggestion lists)
 * ============================================================================
 */

/**
 * Match result for batch operations
 */
typedef struct fuzzy_match_result {
    const char *text; ///< Matched text (not owned)
    int score;        ///< Similarity score 0-100
    int index;        ///< Original index in input array
} fuzzy_match_result_t;

/**
 * Find best matches from a list of candidates
 *
 * Scores all candidates against the pattern and returns the top matches
 * sorted by score (highest first).
 *
 * @param pattern     Pattern to match against (UTF-8)
 * @param candidates  Array of candidate strings
 * @param num_candidates Number of candidates
 * @param results     Output array for results (caller allocates)
 * @param max_results Maximum results to return
 * @param threshold   Minimum score to include (0 = include all)
 * @param options     Matching options (NULL for defaults)
 * @return Number of results found
 */
int fuzzy_match_best(const char *pattern, const char **candidates,
                     int num_candidates, fuzzy_match_result_t *results,
                     int max_results, int threshold,
                     const fuzzy_match_options_t *options);

/**
 * Filter candidates by fuzzy match
 *
 * Returns indices of candidates that match above the threshold.
 *
 * @param pattern       Pattern to match against (UTF-8)
 * @param candidates    Array of candidate strings
 * @param num_candidates Number of candidates
 * @param indices       Output array for matching indices (caller allocates)
 * @param max_indices   Maximum indices to return
 * @param threshold     Minimum score to include
 * @param options       Matching options (NULL for defaults)
 * @return Number of matching candidates
 */
int fuzzy_match_filter(const char *pattern, const char **candidates,
                       int num_candidates, int *indices, int max_indices,
                       int threshold, const fuzzy_match_options_t *options);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * Convert edit distance to similarity score
 *
 * @param distance Edit distance
 * @param max_len  Maximum length of the two strings
 * @return Similarity score 0-100
 */
int fuzzy_distance_to_score(int distance, int max_len);

/**
 * Get string length in Unicode codepoints
 *
 * @param s       UTF-8 string
 * @param options Matching options (affects normalization)
 * @return Length in codepoints
 */
int fuzzy_string_length(const char *s, const fuzzy_match_options_t *options);

#endif /// FUZZY_MATCH_H
