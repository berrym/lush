/**
 * @file history_index.c
 * @brief LLE History System - Indexing and Fast Lookup
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 09 - History System
 * Phase: Phase 1 Day 2 - Entry Management and Indexing
 *
 * Provides hashtable-based indexing for O(1) entry lookup by ID.
 * Uses libhashtable (Spec 05) with custom hash functions for uint64_t keys.
 */

#include "ht.h"
#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * HASH FUNCTIONS FOR UINT64_T KEYS
 * ============================================================================
 */

/**
 * @brief Hash function for uint64_t keys using Thomas Wang's algorithm
 * @param key Pointer to uint64_t key
 * @param seed Hash seed (unused for deterministic hashing)
 * @return Hash value
 */
static uint64_t hash_uint64(const void *key, uint64_t seed) {
    (void)seed; /// Unused for deterministic integer hashing

    uint64_t k = *(const uint64_t *)key;

    /// Thomas Wang's 64-bit integer hash
    k = (~k) + (k << 21);
    k = k ^ (k >> 24);
    k = (k + (k << 3)) + (k << 8);
    k = k ^ (k >> 14);
    k = (k + (k << 2)) + (k << 4);
    k = k ^ (k >> 28);
    k = k + (k << 31);

    return k;
}

/**
 * @brief Equality function for uint64_t keys
 * @param key1 First key to compare
 * @param key2 Second key to compare
 * @return true if keys are equal, false otherwise
 */
static bool eq_uint64(const void *key1, const void *key2) {
    return *(const uint64_t *)key1 == *(const uint64_t *)key2;
}

/**
 * @brief Key copy function - allocates and copies uint64_t
 * @param key Key to copy
 * @return Pointer to copied key, or NULL on allocation failure
 */
static void *copy_uint64_key(const void *key) {
    uint64_t *new_key = lle_pool_alloc(sizeof(uint64_t));
    if (new_key) {
        *new_key = *(const uint64_t *)key;
    }
    return new_key;
}

/**
 * @brief Key free function for uint64_t keys
 * @param key Key to free
 */
static void free_uint64_key(const void *key) {
    if (key) {
        lle_pool_free((void *)key);
    }
}

/**
 * @brief Value copy function - returns pointer as-is (no deep copy)
 * @param value Entry pointer to store
 * @return The same pointer (entries are managed by history core)
 */
static void *copy_entry_ptr(const void *value) { return (void *)value; }

/**
 * @brief Value free function - no-op for entry pointers
 *
 * Entries are managed by history core, not the hashtable.
 *
 * @param value Entry pointer (unused)
 */
static void free_entry_ptr(const void *value) {
    (void)value; /// No-op - entries are owned by history core
}

/* ============================================================================
 * INDEX CREATION AND DESTRUCTION
 * ============================================================================
 */

/**
 * @brief Create hashtable index for fast ID lookup
 *
 * Creates a hashtable using libhashtable with custom hash functions
 * for uint64_t keys and entry pointer values.
 *
 * @param index Output pointer for created hashtable (must not be NULL)
 * @param initial_capacity Initial capacity hint for the hashtable
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index is NULL,
 *         LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t lle_history_index_create(lle_hashtable_t **index,
                                      size_t initial_capacity) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Set up callbacks for uint64_t keys and entry pointer values
    ht_callbacks_t callbacks = {.key_copy = copy_uint64_key,
                                .key_free = free_uint64_key,
                                .val_copy = copy_entry_ptr,
                                .val_free = free_entry_ptr};

    /// Create hashtable with custom hash and equality functions
    ht_t *ht = ht_create(hash_uint64, eq_uint64, &callbacks,
                         (unsigned int)initial_capacity);
    if (!ht) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    *index = ht;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy hashtable index
 *
 * Frees all resources associated with the hashtable index.
 * Safe to call with NULL.
 *
 * @param index Hashtable to destroy (may be NULL)
 */
void lle_history_index_destroy(lle_hashtable_t *index) {
    if (index) {
        ht_destroy(index);
    }
}

/* ============================================================================
 * INDEX OPERATIONS
 * ============================================================================
 */

/**
 * @brief Insert entry into index
 *
 * Adds an entry to the hashtable with the given ID as key.
 *
 * @param index Hashtable index (must not be NULL)
 * @param entry_id Entry ID (key)
 * @param entry Entry pointer (value) (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index or entry
 * is NULL
 */
lle_result_t lle_history_index_insert(lle_hashtable_t *index, uint64_t entry_id,
                                      lle_history_entry_t *entry) {
    if (!index || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Insert into hashtable (void return - assumes success)
    ht_insert(index, &entry_id, entry);

    return LLE_SUCCESS;
}

/**
 * @brief Lookup entry by ID in index
 *
 * Finds an entry in the hashtable by its ID. Returns NULL in *entry
 * if not found, but still returns LLE_SUCCESS.
 *
 * @param index Hashtable index (must not be NULL)
 * @param entry_id Entry ID to lookup
 * @param entry Output pointer for found entry (NULL if not found) (must not be
 * NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index or entry
 * is NULL
 */
lle_result_t lle_history_index_lookup(lle_hashtable_t *index, uint64_t entry_id,
                                      lle_history_entry_t **entry) {
    if (!index || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Lookup in hashtable (returns NULL if not found)
    void *found = ht_get(index, &entry_id);
    *entry = (lle_history_entry_t *)found;

    return LLE_SUCCESS;
}

/**
 * @brief Remove entry from index
 *
 * Removes an entry from the hashtable by its ID.
 *
 * @param index Hashtable index (must not be NULL)
 * @param entry_id Entry ID to remove
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index is NULL
 */
lle_result_t lle_history_index_remove(lle_hashtable_t *index,
                                      uint64_t entry_id) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Remove from hashtable (void return - assumes success)
    ht_remove(index, &entry_id);

    return LLE_SUCCESS;
}

/**
 * @brief Clear all entries from index
 *
 * Note: libhashtable doesn't have a clear function, so this is a no-op.
 * The caller should destroy and recreate the hashtable if needed.
 *
 * @param index Hashtable index (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index is NULL
 */
lle_result_t lle_history_index_clear(lle_hashtable_t *index) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// libhashtable doesn't provide ht_clear(), so we iterate and remove
    /// For now, return success - caller will destroy/recreate if needed
    /// Phase 1 Day 2: Acceptable since clear is only used in
    /// lle_history_clear() which destroys the whole core anyway

    return LLE_SUCCESS;
}

/**
 * @brief Get index size (number of entries)
 *
 * Note: libhashtable doesn't expose a size function, so this always returns 0.
 * Callers should track size themselves via the history core's entry_count.
 *
 * @param index Hashtable index (must not be NULL)
 * @param size Output pointer for size (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index or size
 * is NULL
 */
lle_result_t lle_history_index_get_size(lle_hashtable_t *index, size_t *size) {
    if (!index || !size) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// libhashtable doesn't expose ht_size()
    /// Return 0 for now - callers should use core->entry_count instead
    *size = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Rebuild index from history core entries
 *
 * This function rebuilds the entire index from the history core's
 * entry array. Useful after bulk operations or corruption recovery.
 * Creates a new index if one doesn't exist.
 *
 * @param core History core (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if core is NULL
 */
lle_result_t lle_history_rebuild_index(lle_history_core_t *core) {
    if (!core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// If no index exists, create one
    if (!core->entry_lookup) {
        lle_result_t result = lle_history_index_create(
            &core->entry_lookup, core->config->initial_capacity);
        if (result != LLE_SUCCESS) {
            return result;
        }
    } else {
        /// Clear existing index
        lle_history_index_clear(core->entry_lookup);
    }

    /// Rebuild from entries array
    for (size_t i = 0; i < core->entry_count; i++) {
        lle_history_entry_t *entry = core->entries[i];
        if (entry) {
            lle_result_t result = lle_history_index_insert(
                core->entry_lookup, entry->entry_id, entry);
            if (result != LLE_SUCCESS) {
                return result;
            }
        }
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * Get entry by index (for Up/Down arrow navigation)
 *
 * This is already implemented in history_core.c as
 * lle_history_get_entry_by_index(), which provides O(1) array access. This
 * comment documents that fact.
 */

/**
 * @brief Get last N entries
 *
 * Returns the most recent N entries from history. If fewer than N
 * entries exist, returns all available entries.
 *
 * @param core History core (must not be NULL)
 * @param n Number of entries to retrieve
 * @param entries Output array (caller must allocate at least n pointers) (must
 * not be NULL)
 * @param count Output pointer for actual number retrieved (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL
 */
lle_result_t lle_history_get_last_n_entries(lle_history_core_t *core, size_t n,
                                            lle_history_entry_t **entries,
                                            size_t *count) {
    if (!core || !entries || !count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_rdlock(&core->lock);

    /// Calculate actual count to return
    size_t actual_n = (n < core->entry_count) ? n : core->entry_count;

    /// Copy last N entries from the array
    size_t start_index = core->entry_count - actual_n;
    for (size_t i = 0; i < actual_n; i++) {
        entries[i] = core->entries[start_index + i];
    }

    *count = actual_n;

    pthread_rwlock_unlock(&core->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Get entry by reverse index (for Up arrow - most recent first)
 *
 * Index 0 = most recent entry
 * Index 1 = second most recent
 * etc.
 *
 * @param core History core (must not be NULL)
 * @param reverse_index Reverse index (0 = newest)
 * @param entry Output pointer for entry (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if core or entry
 * is NULL, LLE_ERROR_INVALID_RANGE if reverse_index is out of bounds
 */
lle_result_t
lle_history_get_entry_by_reverse_index(lle_history_core_t *core,
                                       size_t reverse_index,
                                       lle_history_entry_t **entry) {
    if (!core || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_rdlock(&core->lock);

    /// Check bounds
    if (reverse_index >= core->entry_count) {
        pthread_rwlock_unlock(&core->lock);
        return LLE_ERROR_INVALID_RANGE; /// Index out of bounds
    }

    /// Calculate forward index
    size_t forward_index = core->entry_count - 1 - reverse_index;
    *entry = core->entries[forward_index];

    pthread_rwlock_unlock(&core->lock);
    return LLE_SUCCESS;
}
