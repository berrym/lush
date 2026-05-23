/**
 * @file edit_cache.h
 * @brief LRU cache for frequently edited history entries
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 4
 * Provides caching of reconstructed command text for performance optimization.
 */

#ifndef LLE_EDIT_CACHE_H
#define LLE_EDIT_CACHE_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct lle_edit_cache lle_edit_cache_t;
typedef struct lle_edit_cache_entry lle_edit_cache_entry_t;

/**
 * @brief Cache entry data structure
 */
typedef struct lle_edit_cache_entry {
    size_t history_index;          /**< Entry identification - history index */
    uint64_t entry_id;             /**< Entry identification - unique ID */
    char *original_text;           /**< Original command text */
    size_t original_length;        /**< Length of original text */
    char *reconstructed_text;      /**< Reconstructed text */
    size_t reconstructed_length;   /**< Length of reconstructed text */
    struct timespec cached_at;     /**< When entry was cached */
    struct timespec last_accessed; /**< Last access time */
    uint64_t access_count;         /**< Number of accesses */
    struct lle_edit_cache_entry *prev; /**< Previous entry in LRU chain */
    struct lle_edit_cache_entry *next; /**< Next entry in LRU chain */
    void *reserved[2];                 /**< Reserved for future use */
} lle_edit_cache_entry_t;

/**
 * @brief Cache configuration structure
 */
typedef struct lle_edit_cache_config {
    size_t max_entries;    /**< Maximum cache entries */
    uint32_t entry_ttl_ms; /**< Cache entry TTL (milliseconds, 0 = no expiry) */
    bool track_access;     /**< Enable access tracking */
    void *reserved[4];     /**< Reserved for future use */
} lle_edit_cache_config_t;

/**
 * @brief Cache statistics structure
 */
typedef struct lle_edit_cache_stats {
    uint64_t hits;          /**< Cache hits */
    uint64_t misses;        /**< Cache misses */
    size_t current_entries; /**< Current number of entries */
    size_t max_entries;     /**< Maximum allowed entries */
    uint64_t evictions;     /**< Number of evictions */
    uint64_t expirations;   /**< Number of expirations */
    void *reserved[2];      /**< Reserved for future use */
} lle_edit_cache_stats_t;

/**
 * @brief Create an edit cache
 * @param cache Output parameter for created cache
 * @param memory_pool Memory pool for allocations
 * @param config Configuration (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_create(lle_edit_cache_t **cache,
                                   lle_memory_pool_t *memory_pool,
                                   const lle_edit_cache_config_t *config);

/**
 * @brief Destroy an edit cache
 * @param cache Cache to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_destroy(lle_edit_cache_t *cache);

/**
 * @brief Look up an entry in the cache
 * @param cache Edit cache
 * @param history_index History entry index to look up
 * @param entry Output parameter for cache entry (NULL if not found)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_lookup(lle_edit_cache_t *cache,
                                   size_t history_index,
                                   lle_edit_cache_entry_t **entry);

/**
 * @brief Insert an entry into the cache
 * @param cache Edit cache
 * @param history_index History entry index
 * @param entry_id History entry ID
 * @param original_text Original command text
 * @param original_length Length of original text
 * @param reconstructed_text Reconstructed text (can be NULL)
 * @param reconstructed_length Length of reconstructed text
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_insert(lle_edit_cache_t *cache,
                                   size_t history_index, uint64_t entry_id,
                                   const char *original_text,
                                   size_t original_length,
                                   const char *reconstructed_text,
                                   size_t reconstructed_length);

/**
 * @brief Invalidate a specific cache entry
 * @param cache Edit cache
 * @param history_index History entry index to invalidate
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_invalidate(lle_edit_cache_t *cache,
                                       size_t history_index);

/**
 * @brief Clear all cache entries
 * @param cache Edit cache
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_clear(lle_edit_cache_t *cache);

/**
 * @brief Get cache statistics
 * @param cache Edit cache
 * @param stats Output parameter for statistics
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_get_stats(lle_edit_cache_t *cache,
                                      lle_edit_cache_stats_t *stats);

/**
 * @brief Evict expired entries
 * @param cache Edit cache
 * @param expired_count Output parameter for number of entries evicted
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_evict_expired(lle_edit_cache_t *cache,
                                          size_t *expired_count);

/**
 * @brief Get default cache configuration
 * @param config Configuration structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_cache_get_default_config(lle_edit_cache_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // LLE_EDIT_CACHE_H
