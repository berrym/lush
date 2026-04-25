/**
 * @file render_cache.c
 * @brief LLE Render Cache Implementation (Layer 1)
 *
 * Implements the render caching system for LLE Display Integration using
 * libhashtable as the exclusive hashtable solution per Spec 05.
 *
 * SPECIFICATION: docs/lle_specification/08_display_integration_complete.md
 * IMPLEMENTATION PLAN: docs/lle_implementation/SPEC_08_IMPLEMENTATION_PLAN.md
 * LIBHASHTABLE SPEC:
 * docs/lle_specification/05_libhashtable_integration_complete.md
 *
 * COMPLIANCE:
 * - Uses libhashtable (ht_strstr_t) as exclusive hashtable solution
 * - Thread-safe operations with pthread_rwlock
 * - Full memory pool integration
 * - Comprehensive error handling
 * - Cache metrics tracking
 *
 * Week 4 Day 4-5: Simple Caching
 */

#include "lle/display_integration.h"
#include "lle/error_handling.h"
#include "lle/hashtable.h"
#include "lle/memory_management.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================== */
/*                      LRU CACHE POLICY CONSTANTS                            */
/* ========================================================================== */

#define LLE_CACHE_DEFAULT_MAX_ENTRIES 1000 /* Default maximum cache entries */
#define LLE_CACHE_EVICTION_BATCH_SIZE                                          \
    100 /* Evict in batches for efficiency                                     \
         */

/* ========================================================================== */
/*                      CACHE ENTRY SERIALIZATION                             */
/* ========================================================================== */

/**
 * @brief Serialize cache entry to string for storage in libhashtable
 *
 * Format: "data_size:timestamp:last_access:access_count:valid|<data>"
 *
 * @param entry Cache entry to serialize
 * @return Serialized string (caller must free)
 */
static char *serialize_cache_entry(const lle_cached_entry_t *entry) {
    if (!entry || !entry->data) {
        return NULL;
    }

    /* Calculate required size: header + data */
    size_t header_size = 128; /* Space for metadata */
    size_t total_size = header_size + entry->data_size;

    char *serialized = lle_pool_alloc(total_size);
    if (!serialized) {
        return NULL;
    }

    /* Write metadata header */
    int header_len =
        snprintf(serialized, header_size, "%zu:%" PRIu64 ":%" PRIu64 ":%u:%d|",
                 entry->data_size, entry->timestamp, entry->last_access,
                 entry->access_count, entry->valid ? 1 : 0);

    if (header_len < 0 || header_len >= (int)header_size) {
        lle_pool_free(serialized);
        return NULL;
    }

    /* Append data */
    memcpy(serialized + header_len, entry->data, entry->data_size);

    return serialized;
}

/**
 * @brief Deserialize cache entry from string stored in libhashtable
 *
 * @param serialized Serialized string
 * @param entry Output entry structure (caller must allocate)
 * @return LLE_SUCCESS or error code
 */
static lle_result_t deserialize_cache_entry(const char *serialized,
                                            lle_cached_entry_t *entry) {
    if (!serialized || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Parse metadata header */
    uint64_t timestamp, last_access;
    unsigned int access_count;
    int valid;
    size_t data_size;

    int fields =
        sscanf(serialized, "%zu:%" SCNu64 ":%" SCNu64 ":%u:%d|", &data_size,
               &timestamp, &last_access, &access_count, &valid);

    if (fields != 5) {
        return LLE_ERROR_INVALID_FORMAT;
    }

    /* Find data start (after '|') */
    const char *data_start = strchr(serialized, '|');
    if (!data_start) {
        return LLE_ERROR_INVALID_FORMAT;
    }
    data_start++; /* Skip '|' */

    /* Allocate and copy data */
    entry->data = lle_pool_alloc(data_size);
    if (!entry->data) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memcpy(entry->data, data_start, data_size);

    /* Set metadata */
    entry->data_size = data_size;
    entry->timestamp = timestamp;
    entry->last_access = last_access;
    entry->access_count = access_count;
    entry->valid = (valid != 0);
    entry->next = NULL;

    return LLE_SUCCESS;
}

/* ========================================================================== */
/*                      LRU CACHE POLICY IMPLEMENTATION                       */
/* ========================================================================== */

/**
 * @brief Initialize LRU cache policy
 *
 * Creates an LRU eviction policy for the cache.
 *
 * @param policy Output pointer to receive initialized policy
 * @param max_entries Maximum number of cache entries before eviction
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t lle_cache_policy_init(lle_display_cache_policy_t **policy,
                                          size_t max_entries,
                                          lle_memory_pool_t *memory_pool) {
    if (!policy || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_display_cache_policy_t *p =
        lle_pool_alloc(sizeof(struct lle_cache_policy_t));
    if (!p) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(p, 0, sizeof(struct lle_cache_policy_t));
    p->lru_head = NULL;
    p->lru_tail = NULL;
    p->max_entries =
        max_entries > 0 ? max_entries : LLE_CACHE_DEFAULT_MAX_ENTRIES;
    p->eviction_count = 0;

    *policy = p;
    return LLE_SUCCESS;
}

/**
 * @brief Clean up LRU cache policy
 *
 * @param policy Policy to clean up
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t
lle_cache_policy_cleanup(lle_display_cache_policy_t *policy) {
    if (!policy) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* LRU list entries are owned by cache entries, not the policy */
    lle_pool_free(policy);
    return LLE_SUCCESS;
}

/**
 * @brief Add entry to LRU list (most recently used position)
 *
 * @param policy LRU policy
 * @param entry Cache entry to add
 */
static void lle_lru_add_entry(lle_display_cache_policy_t *policy,
                              lle_cached_entry_t *entry) {
    if (!policy || !entry) {
        return;
    }

    /* Add to head (most recently used) */
    entry->next = policy->lru_head;
    policy->lru_head = entry;

    /* Update tail if this is the first entry */
    if (!policy->lru_tail) {
        policy->lru_tail = entry;
    }
}

/**
 * @brief Remove entry from LRU list
 *
 * @param policy LRU policy
 * @param entry Cache entry to remove
 */
static void lle_lru_remove_entry(lle_display_cache_policy_t *policy,
                                 lle_cached_entry_t *entry) {
    if (!policy || !entry) {
        return;
    }

    /* Find and remove entry from list */
    lle_cached_entry_t *prev = NULL;
    lle_cached_entry_t *curr = policy->lru_head;

    while (curr) {
        if (curr == entry) {
            if (prev) {
                prev->next = curr->next;
            } else {
                policy->lru_head = curr->next;
            }

            if (curr == policy->lru_tail) {
                policy->lru_tail = prev;
            }

            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief Move entry to head of LRU list (mark as recently used)
 *
 * @param policy LRU policy
 * @param entry Cache entry to move
 */
LLE_MAYBE_UNUSED
static void lle_lru_touch_entry(lle_display_cache_policy_t *policy,
                                lle_cached_entry_t *entry) {
    if (!policy || !entry) {
        return;
    }

    /* Remove from current position and re-add to head */
    lle_lru_remove_entry(policy, entry);
    lle_lru_add_entry(policy, entry);
}

/**
 * @brief Get least recently used entry for eviction
 *
 * @param policy LRU policy
 * @return Least recently used entry, or NULL if list is empty
 */
LLE_MAYBE_UNUSED
static lle_cached_entry_t *
lle_lru_get_eviction_candidate(lle_display_cache_policy_t *policy) {
    if (!policy) {
        return NULL;
    }

    /* LRU entry is at the tail */
    return policy->lru_tail;
}

/**
 * @brief Calculate cache hit rate
 *
 * @param metrics Cache metrics
 * @return Hit rate as percentage (0.0 - 100.0)
 */
static double lle_calculate_hit_rate(lle_cache_metrics_t *metrics) {
    if (!metrics) {
        return 0.0;
    }

    uint64_t total = metrics->cache_hits + metrics->cache_misses;
    if (total == 0) {
        return 0.0;
    }

    return (double)metrics->cache_hits * 100.0 / (double)total;
}

/* ========================================================================== */
/*                      DISPLAY CACHE IMPLEMENTATION                          */
/* ========================================================================== */

/**
 * @brief Initialize display cache
 *
 * Creates a display cache using libhashtable as the storage backend.
 *
 * @param cache Output pointer to receive initialized cache
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_display_cache_init(lle_display_cache_t **cache,
                                    lle_memory_pool_t *memory_pool) {
    /* Step 1: Validate parameters */
    if (!cache || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Step 2: Allocate cache structure */
    lle_display_cache_t *c = lle_pool_alloc(sizeof(lle_display_cache_t));
    if (!c) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memset(c, 0, sizeof(lle_display_cache_t));

    /* Step 3: Store memory pool reference (cast from LLE to Lush type) */
    c->memory_pool = (lush_memory_pool_t *)memory_pool;

    /* Step 4: Create LLE hashtable wrapper with memory pool integration (Spec
     * 05) */
    lle_hashtable_config_t config;
    lle_hashtable_config_init_default(&config);
    config.use_memory_pool = true;
    config.memory_pool = (lush_memory_pool_t *)memory_pool;
    config.random_seed = true;
    config.thread_safe = false; /* render_cache has its own rwlock */
    config.performance_monitoring = true;
    config.hashtable_name = "render_cache";

    /* Use factory pattern to create hashtable */
    lle_hashtable_factory_t *factory = NULL;
    lle_result_t factory_result =
        lle_hashtable_factory_init(&factory, (lush_memory_pool_t *)memory_pool);
    if (factory_result != LLE_SUCCESS) {
        lle_pool_free(c);
        return factory_result;
    }

    factory_result =
        lle_hashtable_factory_create_strstr(factory, &config, &c->cache_table);
    lle_hashtable_factory_destroy(factory);

    if (factory_result != LLE_SUCCESS) {
        lle_pool_free(c);
        return factory_result;
    }

    /* Step 5: Allocate cache metrics */
    c->metrics = lle_pool_alloc(sizeof(lle_cache_metrics_t));
    if (!c->metrics) {
        lle_strstr_hashtable_destroy(c->cache_table);
        lle_pool_free(c);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memset(c->metrics, 0, sizeof(lle_cache_metrics_t));

    /* Step 6: Initialize LRU cache policy */
    lle_result_t result = lle_cache_policy_init(
        &c->policy, LLE_CACHE_DEFAULT_MAX_ENTRIES, memory_pool);
    if (result != LLE_SUCCESS) {
        lle_pool_free(c->metrics);
        lle_strstr_hashtable_destroy(c->cache_table);
        lle_pool_free(c);
        return result;
    }

    /* Step 7: Initialize read-write lock */
    if (pthread_rwlock_init(&c->cache_lock, NULL) != 0) {
        lle_cache_policy_cleanup(c->policy);
        lle_pool_free(c->metrics);
        lle_strstr_hashtable_destroy(c->cache_table);
        lle_pool_free(c);
        return LLE_ERROR_INITIALIZATION_FAILED;
    }

    /* Step 8: Return initialized cache */
    *cache = c;
    return LLE_SUCCESS;
}

/**
 * @brief Clean up display cache
 *
 * @param cache Cache to clean up
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_display_cache_cleanup(lle_display_cache_t *cache) {
    if (!cache) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Destroy LLE hashtable wrapper (frees all entries) */
    if (cache->cache_table) {
        lle_strstr_hashtable_destroy(cache->cache_table);
    }

    /* Destroy lock */
    pthread_rwlock_destroy(&cache->cache_lock);

    /* Clean up LRU policy */
    if (cache->policy) {
        lle_cache_policy_cleanup(cache->policy);
    }

    /* Free metrics */
    if (cache->metrics) {
        lle_pool_free(cache->metrics);
    }

    /* Free cache structure */
    lle_pool_free(cache);

    return LLE_SUCCESS;
}

/**
 * @brief Store entry in cache
 *
 * Stores or updates a cache entry using libhashtable.
 *
 * @param cache Display cache
 * @param key Cache key (uint64_t converted to string)
 * @param data Data to cache
 * @param data_size Size of data
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_display_cache_store(lle_display_cache_t *cache, uint64_t key,
                                     const void *data, size_t data_size) {
    /* Step 1: Validate parameters */
    if (!cache || !data || data_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Step 2: Convert key to string */
    char key_str[32];
    snprintf(key_str, sizeof(key_str), "%" PRIu64, key);

    /* Step 3: Create cache entry */
    lle_cached_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.data = (void *)data;
    entry.data_size = data_size;

    /* Set timestamps */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    entry.timestamp =
        (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    entry.last_access = entry.timestamp;
    entry.access_count = 0;
    entry.valid = true;

    /* Step 4: Serialize entry */
    char *serialized = serialize_cache_entry(&entry);
    if (!serialized) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Step 5: Acquire write lock */
    pthread_rwlock_wrlock(&cache->cache_lock);

    /* Step 6: Insert into libhashtable */
    lle_strstr_hashtable_insert(cache->cache_table, key_str, serialized);

    /* Step 7: Release lock */
    pthread_rwlock_unlock(&cache->cache_lock);

    /* Free serialized string (libhashtable makes a copy) */
    lle_pool_free(serialized);

    return LLE_SUCCESS;
}

/**
 * @brief Lookup entry in cache
 *
 * Retrieves a cache entry from libhashtable.
 *
 * @param cache Display cache
 * @param key Cache key
 * @param data Output pointer for cached data
 * @param data_size Output pointer for data size
 * @return LLE_SUCCESS on hit, LLE_ERROR_CACHE_MISS on miss
 */
lle_result_t lle_display_cache_lookup(lle_display_cache_t *cache, uint64_t key,
                                      void **data, size_t *data_size) {
    /* Step 1: Validate parameters */
    if (!cache || !data || !data_size) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Step 2: Convert key to string */
    char key_str[32];
    snprintf(key_str, sizeof(key_str), "%" PRIu64, key);

    /* Step 3: Acquire read lock */
    pthread_rwlock_rdlock(&cache->cache_lock);

    /* Step 4: Lookup in libhashtable */
    const char *serialized =
        lle_strstr_hashtable_lookup(cache->cache_table, key_str);

    if (!serialized) {
        /* Cache miss */
        cache->metrics->cache_misses++;
        cache->metrics->hit_rate = lle_calculate_hit_rate(cache->metrics);
        pthread_rwlock_unlock(&cache->cache_lock);
        return LLE_ERROR_CACHE_MISS;
    }

    /* Step 5: Deserialize entry */
    lle_cached_entry_t entry;
    lle_result_t result = deserialize_cache_entry(serialized, &entry);

    if (result != LLE_SUCCESS) {
        cache->metrics->cache_misses++;
        pthread_rwlock_unlock(&cache->cache_lock);
        return result;
    }

    /* Step 6: Return data */
    *data = entry.data;
    *data_size = entry.data_size;

    /* Step 7: Update metrics */
    cache->metrics->cache_hits++;
    cache->metrics->hit_rate = lle_calculate_hit_rate(cache->metrics);

    /* Step 8: Release lock */
    pthread_rwlock_unlock(&cache->cache_lock);

    return LLE_SUCCESS;
}

/**
 * @brief Invalidate specific cache entry
 *
 * Removes a specific entry from the cache.
 *
 * @param cache Display cache
 * @param key Cache key to invalidate
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_display_cache_invalidate(lle_display_cache_t *cache,
                                          uint64_t key) {
    if (!cache) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Convert key to string */
    char key_str[32];
    snprintf(key_str, sizeof(key_str), "%" PRIu64, key);

    /* Acquire write lock */
    pthread_rwlock_wrlock(&cache->cache_lock);

    /* Remove from libhashtable */
    lle_strstr_hashtable_delete(cache->cache_table, key_str);

    /* Update metrics */
    cache->metrics->evictions++;

    /* Release lock */
    pthread_rwlock_unlock(&cache->cache_lock);

    return LLE_SUCCESS;
}

/**
 * @brief Invalidate all cache entries
 *
 * Clears the entire cache.
 *
 * @param cache Display cache
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_display_cache_invalidate_all(lle_display_cache_t *cache) {
    if (!cache) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Acquire write lock */
    pthread_rwlock_wrlock(&cache->cache_lock);

    /* Destroy and recreate libhashtable to clear all entries */
    if (cache->cache_table) {
        lle_strstr_hashtable_destroy(cache->cache_table);
    }

    /* Recreate hashtable using LLE wrapper */
    lle_hashtable_config_t config;
    lle_hashtable_config_init_default(&config);
    config.use_memory_pool = true;
    config.memory_pool = cache->memory_pool;
    config.random_seed = true;
    config.thread_safe = false;
    config.performance_monitoring = true;
    config.hashtable_name = "render_cache";

    lle_hashtable_factory_t *factory = NULL;
    if (lle_hashtable_factory_init(&factory, cache->memory_pool) ==
        LLE_SUCCESS) {
        lle_hashtable_factory_create_strstr(factory, &config,
                                            &cache->cache_table);
        lle_hashtable_factory_destroy(factory);
    }
    if (!cache->cache_table) {
        pthread_rwlock_unlock(&cache->cache_lock);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Reset metrics but preserve historical data */
    uint64_t total_evictions = cache->metrics->evictions;
    cache->metrics->evictions = total_evictions + cache->metrics->cache_hits;

    /* Release lock */
    pthread_rwlock_unlock(&cache->cache_lock);

    return LLE_SUCCESS;
}

/* ========================================================================== */
/*                      RENDER CACHE IMPLEMENTATION                           */
/* ========================================================================== */

/**
 * @brief Initialize render cache
 *
 * Creates render cache with base cache implementation.
 *
 * @param cache Output pointer to receive initialized cache
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_render_cache_init(lle_render_cache_t **cache,
                                   lle_memory_pool_t *memory_pool) {
    /* Step 1: Validate parameters */
    if (!cache || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Step 2: Allocate render cache structure */
    lle_render_cache_t *rc = lle_pool_alloc(sizeof(lle_render_cache_t));
    if (!rc) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memset(rc, 0, sizeof(lle_render_cache_t));

    /* Step 3: Initialize base cache using libhashtable */
    lle_result_t result = lle_display_cache_init(&rc->base_cache, memory_pool);
    if (result != LLE_SUCCESS) {
        lle_pool_free(rc);
        return result;
    }

    /* Step 4: Configure render cache */
    rc->max_render_size = 0; /* No limit for now */
    rc->cache_ttl_ms = 5000; /* 5 second TTL */

    /* Step 5: Return initialized cache */
    *cache = rc;
    return LLE_SUCCESS;
}

/**
 * @brief Clean up render cache
 *
 * @param cache Cache to clean up
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_render_cache_cleanup(lle_render_cache_t *cache) {
    if (!cache) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Clean up base cache */
    if (cache->base_cache) {
        lle_display_cache_cleanup(cache->base_cache);
    }

    /* Free render cache structure */
    lle_pool_free(cache);

    return LLE_SUCCESS;
}

/**
 * @brief Compute cache key from buffer and cursor
 *
 * Generates a simple cache key by combining buffer and cursor hashes.
 *
 * @param buffer Buffer state
 * @param cursor Cursor position
 * @return Combined cache key
 */
uint64_t lle_compute_cache_key(lle_buffer_t *buffer,
                               lle_cursor_position_t *cursor) {
    if (!buffer || !cursor) {
        return 0;
    }

    /* Simple combination: XOR of buffer content hash and cursor position */
    uint64_t buffer_hash = (uint64_t)buffer->length;
    uint64_t cursor_hash =
        ((uint64_t)cursor->line_number << 32) | cursor->column_offset;

    return buffer_hash ^ cursor_hash;
}
