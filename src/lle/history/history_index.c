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
 * Uses the libhashtable ht_u64ptr wrapper: a uint64 key hashed directly with
 * the integer finalizer, mapping to a caller-owned entry pointer. The wrapper
 * copies the 8-byte key and stores the entry pointer as given; the history core
 * owns entry lifetime, so the table frees neither the entries nor (here) more.
 */

#include "ht.h"
#include "lle/error_handling.h"
#include "lle/history.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INDEX CREATION AND DESTRUCTION
 * ============================================================================
 */

/**
 * @brief Create hashtable index for fast ID lookup
 *
 * Creates an ht_u64ptr table mapping entry IDs to entry pointers. The wrapper
 * supplies the integer-finalizer hash and copies the 8-byte key internally.
 *
 * @param index Output pointer for created hashtable (must not be NULL)
 * @param initial_capacity Initial capacity hint for the hashtable
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index is NULL,
 *         LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t lle_history_index_create(ht_u64ptr_t **index,
                                      size_t initial_capacity) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    ht_u64ptr_t *ht = ht_u64ptr_create(
        &(ht_u64_options_t){.initial_capacity = initial_capacity});
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
void lle_history_index_destroy(ht_u64ptr_t *index) {
    if (index) {
        ht_u64ptr_destroy(index);
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
lle_result_t lle_history_index_insert(ht_u64ptr_t *index, uint64_t entry_id,
                                      lle_history_entry_t *entry) {
    if (!index || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Insert into hashtable (void return - assumes success)
    ht_u64ptr_insert(index, entry_id, entry);

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
lle_result_t lle_history_index_lookup(ht_u64ptr_t *index, uint64_t entry_id,
                                      lle_history_entry_t **entry) {
    if (!index || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Lookup in hashtable (returns NULL if not found)
    *entry = (lle_history_entry_t *)ht_u64ptr_get(index, entry_id);

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
lle_result_t lle_history_index_remove(ht_u64ptr_t *index, uint64_t entry_id) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Remove from hashtable (void return - assumes success)
    ht_u64ptr_remove(index, entry_id);

    return LLE_SUCCESS;
}

/**
 * @brief Clear all entries from index
 *
 * Removes every key, freeing the copied keys, and keeps the table allocated and
 * reusable. Entry pointers are stored as passthrough values, so the history
 * core (which owns the entries) is unaffected.
 *
 * @param index Hashtable index (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index is NULL
 */
lle_result_t lle_history_index_clear(ht_u64ptr_t *index) {
    if (!index) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// The typed wrappers are opaque aliases over the generic table, so the
    /// generic compound ops apply to the base pointer.
    ht_clear((ht_t *)index);

    return LLE_SUCCESS;
}

/**
 * @brief Get index size (number of entries)
 *
 * @param index Hashtable index (must not be NULL)
 * @param size Output pointer for size (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if index or size
 * is NULL
 */
lle_result_t lle_history_index_get_size(ht_u64ptr_t *index, size_t *size) {
    if (!index || !size) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *size = ht_size((const ht_t *)index);

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
