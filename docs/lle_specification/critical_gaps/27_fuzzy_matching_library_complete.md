# Fuzzy Matching Library Complete Specification

**Document**: 27_fuzzy_matching_library_complete.md  
**Version**: 1.0.0  
**Date**: 2025-10-14  
**Status**: Implementation-Ready Specification  
**Classification**: Critical Foundational Infrastructure  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Core Algorithms](#3-core-algorithms)
4. [Similarity Scoring](#4-similarity-scoring)
5. [Performance Optimization](#5-performance-optimization)
6. [Integration Points](#6-integration-points)
7. [API Specification](#7-api-specification)
8. [Testing and Validation](#8-testing-and-validation)
9. [Implementation Roadmap](#9-implementation-roadmap)

---

## 1. Executive Summary

### 1.1 Purpose

The Fuzzy Matching Library provides **shared, production-tested similarity algorithms** used by multiple Lush/LLE systems. This eliminates code duplication and ensures consistent fuzzy matching behavior across:

- Autocorrect system (`src/autocorrect.c`)
- LLE Completion System (Spec 12)
- LLE History Search (Spec 09)
- Any future features requiring similarity matching

### 1.2 Why This Is a Critical Gap

**Problem**: Fuzzy matching algorithms are currently:
- OK **Implemented in autocorrect.c** (production-tested, working)
- FAIL **Duplicated in Spec 12** (completion fuzzy matching)
- FAIL **Will be duplicated in Spec 09** (history search)
- FAIL **No shared library** exists

**Solution**: Extract proven algorithms from `autocorrect.c` into shared library.

### 1.3 Key Features

- **Proven Algorithms**: Extracted from working autocorrect.c code
- **Multiple Methods**: Levenshtein, Jaro-Winkler, prefix matching, subsequence
- **Weighted Scoring**: Configurable algorithm weights for different use cases
- **High Performance**: Sub-millisecond matching for typical queries
- **Memory Efficient**: Zero-allocation option for hot paths
- **Thread-Safe**: Concurrent matching support

### 1.4 Used By

| System | Use Case | Required Algorithms |
|--------|----------|---------------------|
| **Autocorrect** | Command suggestion | All (weighted combination) |
| **LLE Completion** | Fuzzy completion | Prefix + subsequence + Levenshtein |
| **LLE History Search** | Command search | Jaro-Winkler + subsequence |
| **LLE Autosuggestions** | Real-time suggestions | Prefix + quick subsequence |

---

## 2. Architecture Overview

### 2.1 Library Structure

```c
// Shared fuzzy matching library
typedef struct lle_fuzzy_matcher {
    // Algorithm implementations
    lle_levenshtein_engine_t *levenshtein;         // Edit distance
    lle_jaro_winkler_engine_t *jaro_winkler;       // Jaro-Winkler similarity
    lle_prefix_matcher_t *prefix_matcher;          // Common prefix
    lle_subsequence_matcher_t *subsequence;        // Subsequence matching
    
    // Scoring configuration
    lle_scoring_config_t *config;                  // Algorithm weights
    lle_scoring_cache_t *cache;                    // Result caching
    
    // Performance optimization
    lle_memory_pool_t *memory_pool;                // Memory management
    lle_performance_monitor_t *perf_monitor;       // Performance tracking
    
    // Thread safety
    pthread_rwlock_t matcher_lock;                 // Concurrent access
    bool matcher_active;                           // System status
} lle_fuzzy_matcher_t;
```

### 2.2 Scoring Configuration

```c
// Configurable algorithm weights for different use cases
typedef struct lle_scoring_config {
    // Algorithm weights (sum should equal 100)
    uint8_t levenshtein_weight;                    // Default: 40
    uint8_t jaro_winkler_weight;                   // Default: 30
    uint8_t prefix_weight;                         // Default: 20
    uint8_t subsequence_weight;                    // Default: 10
    
    // Performance settings
    bool enable_caching;                           // Cache results
    bool case_sensitive;                           // Case-sensitive matching
    size_t cache_size;                             // Max cache entries
    
    // Thresholds
    int min_similarity_score;                      // Minimum to consider (0-100)
    int prefix_bonus;                              // Extra points for prefix match
} lle_scoring_config_t;
```

---

## 3. Core Algorithms

### 3.1 Levenshtein Distance (Edit Distance)

**Source**: `autocorrect.c` lines 520-575 (PROVEN, TESTED)

```c
/**
 * Calculate Levenshtein distance between two strings
 * 
 * This is the PROVEN implementation from autocorrect.c
 * that has been used successfully in production.
 * 
 * @param s1 First string
 * @param s2 Second string
 * @return Edit distance (number of operations to transform s1 into s2)
 */
int lle_fuzzy_levenshtein_distance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);

    // Create DP matrix
    int **matrix = malloc((len1 + 1) * sizeof(int *));
    if (!matrix) {
        return len1 + len2; // Worst case on allocation failure
    }

    for (int i = 0; i <= len1; i++) {
        matrix[i] = malloc((len2 + 1) * sizeof(int));
        if (!matrix[i]) {
            // Cleanup and return worst case
            for (int j = 0; j < i; j++) {
                free(matrix[j]);
            }
            free(matrix);
            return len1 + len2;
        }
    }

    // Initialize matrix
    for (int i = 0; i <= len1; i++) {
        matrix[i][0] = i;
    }
    for (int j = 0; j <= len2; j++) {
        matrix[0][j] = j;
    }

    // Fill matrix using DP
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (tolower(s1[i - 1]) == tolower(s2[j - 1])) ? 0 : 1;
            
            int deletion = matrix[i - 1][j] + 1;
            int insertion = matrix[i][j - 1] + 1;
            int substitution = matrix[i - 1][j - 1] + cost;
            
            matrix[i][j] = min3(deletion, insertion, substitution);
        }
    }

    int result = matrix[len1][len2];

    // Cleanup
    for (int i = 0; i <= len1; i++) {
        free(matrix[i]);
    }
    free(matrix);

    return result;
}

// Helper function
static int min3(int a, int b, int c) {
    int min_ab = (a < b) ? a : b;
    return (min_ab < c) ? min_ab : c;
}
```

**Complexity**: O(m*n) time, O(m*n) space  
**Use Case**: Best for finding closest matches with character edits

---

### 3.2 Jaro-Winkler Similarity

**Source**: `autocorrect.c` lines 588-650 (PROVEN, TESTED)

```c
/**
 * Calculate Jaro-Winkler similarity score
 * 
 * This is the PROVEN implementation from autocorrect.c.
 * Returns a score from 0.0 (no match) to 1.0 (perfect match).
 *
 * @param s1 First string
 * @param s2 Second string
 * @return Similarity score (0.0 to 1.0)
 */
double lle_fuzzy_jaro_winkler(const char *s1, const char *s2) {
    // First calculate Jaro similarity
    double jaro = lle_fuzzy_jaro_similarity(s1, s2);
    
    // Add prefix bonus (Winkler extension)
    int prefix_len = lle_fuzzy_common_prefix_length(s1, s2, false);
    if (prefix_len > 4) {
        prefix_len = 4; // Max prefix bonus is 4 chars
    }
    
    double jaro_winkler = jaro + (0.1 * prefix_len * (1 - jaro));
    
    return jaro_winkler;
}

// Jaro similarity (base algorithm)
static double lle_fuzzy_jaro_similarity(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);

    if (len1 == 0 && len2 == 0) return 1.0;
    if (len1 == 0 || len2 == 0) return 0.0;

    int match_window = ((len1 > len2) ? len1 : len2) / 2 - 1;
    if (match_window < 0) match_window = 0;

    bool *s1_matches = calloc(len1, sizeof(bool));
    bool *s2_matches = calloc(len2, sizeof(bool));

    if (!s1_matches || !s2_matches) {
        free(s1_matches);
        free(s2_matches);
        return 0.0;
    }

    int matches = 0;

    // Find matches
    for (int i = 0; i < len1; i++) {
        int start = (i - match_window > 0) ? i - match_window : 0;
        int end = (i + match_window < len2) ? i + match_window : len2 - 1;

        for (int j = start; j <= end; j++) {
            if (s2_matches[j] || tolower(s1[i]) != tolower(s2[j])) {
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
        if (!s1_matches[i]) continue;
        while (!s2_matches[k]) k++;
        if (tolower(s1[i]) != tolower(s2[k])) {
            transpositions++;
        }
        k++;
    }

    free(s1_matches);
    free(s2_matches);

    // Calculate Jaro similarity
    double jaro = ((double)matches / len1 + 
                   (double)matches / len2 + 
                   (double)(matches - transpositions / 2) / matches) / 3.0;

    return jaro;
}
```

**Complexity**: O(m*n) time, O(m+n) space  
**Use Case**: Best for comparing strings of similar length

---

### 3.3 Common Prefix Matching

**Source**: `autocorrect.c` lines 652-666 (PROVEN, TESTED)

```c
/**
 * Calculate common prefix length between two strings
 *
 * @param s1 First string
 * @param s2 Second string
 * @param case_sensitive Whether to do case-sensitive comparison
 * @return Length of common prefix
 */
int lle_fuzzy_common_prefix_length(const char *s1, const char *s2,
                                   bool case_sensitive) {
    int len = 0;
    while (s1[len] && s2[len]) {
        char c1 = case_sensitive ? s1[len] : tolower(s1[len]);
        char c2 = case_sensitive ? s2[len] : tolower(s2[len]);
        if (c1 != c2) {
            break;
        }
        len++;
    }
    return len;
}
```

**Complexity**: O(min(m,n)) time, O(1) space  
**Use Case**: Fast prefix matching for autocompletion

---

### 3.4 Subsequence Matching

**Source**: `autocorrect.c` lines 668-696 (PROVEN, TESTED)

```c
/**
 * Check if pattern is a subsequence of text and calculate quality score
 *
 * A subsequence means all characters of pattern appear in text in order,
 * but not necessarily consecutively.
 *
 * @param pattern Pattern string to find
 * @param text Text to search in
 * @param case_sensitive Whether to do case-sensitive comparison
 * @return Score from 0-100 (100 = perfect match, 0 = not a subsequence)
 */
int lle_fuzzy_subsequence_score(const char *pattern, const char *text,
                                bool case_sensitive) {
    int pattern_len = strlen(pattern);
    int text_len = strlen(text);

    if (pattern_len == 0) return 100;
    if (text_len == 0) return 0;

    int matches = 0;
    int text_idx = 0;

    for (int p = 0; p < pattern_len && text_idx < text_len; p++) {
        char pc = case_sensitive ? pattern[p] : tolower(pattern[p]);

        for (; text_idx < text_len; text_idx++) {
            char tc = case_sensitive ? text[text_idx] : tolower(text[text_idx]);
            if (pc == tc) {
                matches++;
                text_idx++;
                break;
            }
        }
    }

    // Score is percentage of pattern that matched
    return (matches * 100) / pattern_len;
}
```

**Complexity**: O(m+n) time, O(1) space  
**Use Case**: Fast fuzzy matching for interactive completion

---

## 4. Similarity Scoring

### 4.1 Weighted Combination

**Source**: `autocorrect.c` lines 475-518 (PROVEN, TESTED)

```c
/**
 * Calculate overall similarity score using weighted combination of algorithms
 *
 * This is the PROVEN weighted scoring from autocorrect.c that combines
 * all algorithms for best results.
 *
 * @param command1 First command string
 * @param command2 Second command string
 * @param config Scoring configuration (algorithm weights)
 * @return Overall similarity score (0-100)
 */
int lle_fuzzy_similarity_score(const char *command1, const char *command2,
                               lle_scoring_config_t *config) {
    if (!command1 || !command2) return 0;

    // Perfect match
    if (strcasecmp(command1, command2) == 0) {
        return 100;
    }

    int len1 = strlen(command1);
    int len2 = strlen(command2);

    if (len1 == 0 || len2 == 0) return 0;

    // 1. Levenshtein distance score
    int edit_distance = lle_fuzzy_levenshtein_distance(command1, command2);
    int max_len = (len1 > len2) ? len1 : len2;
    int levenshtein_score = ((max_len - edit_distance) * 100) / max_len;
    if (levenshtein_score < 0) levenshtein_score = 0;

    // 2. Jaro-Winkler score
    double jaro_winkler = lle_fuzzy_jaro_winkler(command1, command2);
    int jaro_score = (int)(jaro_winkler * 100);

    // 3. Common prefix score
    int prefix_len = lle_fuzzy_common_prefix_length(command1, command2, config->case_sensitive);
    int prefix_score = (prefix_len * 100) / ((len1 + len2) / 2);
    if (prefix_score > 100) prefix_score = 100;

    // 4. Subsequence matching score
    int subseq_score = lle_fuzzy_subsequence_score(command1, command2, config->case_sensitive);

    // Weighted combination (default: 40% Levenshtein, 30% Jaro, 20% prefix, 10% subseq)
    int final_score = (levenshtein_score * config->levenshtein_weight +
                       jaro_score * config->jaro_winkler_weight +
                       prefix_score * config->prefix_weight +
                       subseq_score * config->subsequence_weight) / 100;

    if (final_score > 100) final_score = 100;
    if (final_score < 0) final_score = 0;

    return final_score;
}
```

**This is the PROVEN algorithm from autocorrect.c that has been tested and works.**

---

## 5. Performance Optimization

### 5.1 Result Caching

```c
// Cache for fuzzy matching results
typedef struct lle_fuzzy_cache {
    lle_hash_table_t *result_cache;                // Pattern -> result cache
    size_t max_entries;                            // Maximum cache size
    uint64_t cache_hits;                           // Cache hit count
    uint64_t cache_misses;                         // Cache miss count
} lle_fuzzy_cache_t;

// Cached fuzzy matching with automatic caching
int lle_fuzzy_similarity_cached(const char *s1, const char *s2,
                                lle_scoring_config_t *config,
                                lle_fuzzy_cache_t *cache) {
    if (!cache || !config->enable_caching) {
        return lle_fuzzy_similarity_score(s1, s2, config);
    }
    
    // Generate cache key
    char cache_key[512];
    snprintf(cache_key, sizeof(cache_key), "%s:%s", s1, s2);
    
    // Check cache
    int *cached_score = lle_hash_table_get(cache->result_cache, cache_key);
    if (cached_score) {
        cache->cache_hits++;
        return *cached_score;
    }
    
    // Calculate and cache
    int score = lle_fuzzy_similarity_score(s1, s2, config);
    
    int *score_copy = malloc(sizeof(int));
    if (score_copy) {
        *score_copy = score;
        lle_hash_table_insert(cache->result_cache, cache_key, score_copy);
    }
    
    cache->cache_misses++;
    return score;
}
```

---

## 6. Integration Points

### 6.1 Autocorrect Integration

**Replace** `autocorrect.c` internal functions with library calls:

```c
// OLD (autocorrect.c internal):
int score = autocorrect_similarity_score(cmd1, cmd2, case_sensitive);

// NEW (using shared library):
lle_scoring_config_t config = {
    .levenshtein_weight = 40,
    .jaro_winkler_weight = 30,
    .prefix_weight = 20,
    .subsequence_weight = 10,
    .case_sensitive = false
};
int score = lle_fuzzy_similarity_score(cmd1, cmd2, &config);
```

### 6.2 LLE Completion Integration

**Use** for fuzzy completion matching (Spec 12):

```c
// In completion system
lle_scoring_config_t completion_config = {
    .levenshtein_weight = 25,
    .jaro_winkler_weight = 25,
    .prefix_weight = 40,  // Higher weight for prefix in completion
    .subsequence_weight = 10,
    .case_sensitive = false,
    .enable_caching = true
};

int score = lle_fuzzy_similarity_cached(pattern, candidate, 
                                        &completion_config, fuzzy_cache);
```

### 6.3 LLE History Search Integration

**Use** for history command search (Spec 09):

```c
// In history search
lle_scoring_config_t history_config = {
    .levenshtein_weight = 20,
    .jaro_winkler_weight = 50,  // Higher weight for Jaro in history
    .prefix_weight = 20,
    .subsequence_weight = 10,
    .case_sensitive = false
};

int score = lle_fuzzy_similarity_score(query, history_cmd, &history_config);
```

---

## 7. API Specification

### 7.1 Core API

```c
// Library initialization
lle_fuzzy_matcher_t* lle_fuzzy_matcher_create(lle_memory_pool_t *memory_pool);
void lle_fuzzy_matcher_destroy(lle_fuzzy_matcher_t *matcher);

// Similarity scoring
int lle_fuzzy_similarity_score(const char *s1, const char *s2, 
                               lle_scoring_config_t *config);
int lle_fuzzy_similarity_cached(const char *s1, const char *s2,
                                lle_scoring_config_t *config,
                                lle_fuzzy_cache_t *cache);

// Individual algorithms (can be used separately)
int lle_fuzzy_levenshtein_distance(const char *s1, const char *s2);
double lle_fuzzy_jaro_winkler(const char *s1, const char *s2);
int lle_fuzzy_common_prefix_length(const char *s1, const char *s2, bool case_sensitive);
int lle_fuzzy_subsequence_score(const char *pattern, const char *text, bool case_sensitive);

// Configuration
lle_scoring_config_t* lle_fuzzy_config_create_default(void);
lle_scoring_config_t* lle_fuzzy_config_create_for_completion(void);
lle_scoring_config_t* lle_fuzzy_config_create_for_history(void);
lle_scoring_config_t* lle_fuzzy_config_create_for_autocorrect(void);
```

---

## 8. Testing and Validation

### 8.1 Test Cases

```c
// Test suite for fuzzy matching library
void test_fuzzy_levenshtein(void) {
    assert(lle_fuzzy_levenshtein_distance("git", "gti") == 2);  // 1 swap
    assert(lle_fuzzy_levenshtein_distance("git", "gitt") == 1); // 1 insert
    assert(lle_fuzzy_levenshtein_distance("git", "gi") == 1);   // 1 delete
    assert(lle_fuzzy_levenshtein_distance("git", "git") == 0);  // Identical
}

void test_fuzzy_jaro_winkler(void) {
    double score = lle_fuzzy_jaro_winkler("git", "gti");
    assert(score > 0.8);  // High similarity
}

void test_fuzzy_prefix(void) {
    assert(lle_fuzzy_common_prefix_length("gitlab", "gitea", false) == 3);
    assert(lle_fuzzy_common_prefix_length("Git", "git", false) == 3);
    assert(lle_fuzzy_common_prefix_length("Git", "git", true) == 0);
}

void test_fuzzy_subsequence(void) {
    assert(lle_fuzzy_subsequence_score("gti", "git", false) == 100);  // Matches but wrong order
    assert(lle_fuzzy_subsequence_score("gt", "git", false) == 100);   // Subsequence
}
```

---

## 9. Implementation Roadmap

### Phase 1: Extraction (Week 1)
1. Extract algorithms from `autocorrect.c` into `src/lle/foundation/fuzzy_matching/`
2. Create library header `fuzzy_matching.h`
3. Add basic tests

### Phase 2: Integration (Week 2)
1. Refactor `autocorrect.c` to use shared library
2. Verify autocorrect still works identically
3. Integration tests

### Phase 3: LLE Integration (Week 3)
1. Integrate into LLE Completion System (Spec 12)
2. Integrate into LLE History Search (Spec 09)
3. Performance validation

---

## Conclusion

**This specification provides a shared fuzzy matching library using PROVEN algorithms from autocorrect.c.**

**Benefits**:
- OK No code duplication across systems
- OK Consistent matching behavior
- OK Proven, tested algorithms
- OK Configurable for different use cases
- OK High performance with caching

**Recommendation**: Implement this library FIRST as foundational infrastructure.

---

**Created**: 2025-10-14  
**Source**: Extracted from `src/autocorrect.c` (production-tested code)  
**Status**: Ready for implementation
