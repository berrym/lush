/**
 * @file kill_ring.c
 * @brief Kill Ring System Implementation
 *
 * Implements GNU Readline compatible kill/yank operations for cut/paste.
 * The kill ring is a circular buffer that stores killed (cut) text for
 * later yanking (pasting), supporting both append and prepend operations.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md
 * Implementation Plan:
 * docs/lle_specification/critical_gaps/25_IMPLEMENTATION_PLAN.md
 */

#include "lle/kill_ring.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================
 */

/**
 * Kill ring entry - stores a single killed text string
 */
typedef struct {
    char *text;     /**< Killed text (heap allocated) */
    size_t length;  /**< Length of text (excluding null terminator) */
    bool allocated; /**< True if entry is in use */
} lle_kill_entry_t;

/**
 * Kill ring structure - circular buffer of kill entries
 */
struct lle_kill_ring {
    lle_kill_entry_t *entries; /**< Array of kill entries */
    size_t capacity;           /**< Maximum number of entries */
    size_t count;              /**< Current number of entries */
    size_t head;               /**< Index of most recent kill */
    size_t yank_position;      /**< Current position for yank-pop */
    bool last_was_yank;        /**< True if last operation was yank */
    bool last_was_kill;        /**< True if last operation was kill */
    lle_memory_pool_t *pool;   /**< Memory pool for allocations */
    pthread_mutex_t lock;      /**< Thread safety lock */
};

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Allocate string from memory pool or malloc
 * @param ring Kill ring instance containing memory pool reference
 * @param str Source string to duplicate
 * @param len Length of string to allocate (excluding null terminator)
 * @return Pointer to allocated string copy, or NULL on failure
 */
static char *kill_ring_strdup(lle_kill_ring_t *ring, const char *str,
                              size_t len) {
    char *copy;

    if (ring->pool != NULL) {
        copy = (char *)lle_pool_allocate_fast(ring->pool, len + 1);
    } else {
        copy = (char *)malloc(len + 1);
    }

    if (copy != NULL) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }

    return copy;
}

/**
 * @brief Free string to memory pool or malloc
 * @param ring Kill ring instance containing memory pool reference
 * @param str String to free (may be NULL)
 */
static void kill_ring_free_string(lle_kill_ring_t *ring, char *str) {
    if (str == NULL) {
        return;
    }

    if (ring->pool != NULL) {
        lle_pool_free_fast(ring->pool, str);
    } else {
        free(str);
    }
}

/**
 * @brief Free a kill entry's text and reset entry state
 * @param ring Kill ring instance containing memory pool reference
 * @param entry Kill entry to free and reset
 */
static void free_entry(lle_kill_ring_t *ring, lle_kill_entry_t *entry) {
    if (entry->text != NULL) {
        kill_ring_free_string(ring, entry->text);
        entry->text = NULL;
    }
    entry->length = 0;
    entry->allocated = false;
}

/**
 * @brief Get entry at circular buffer position
 * @param position Logical position in the ring
 * @param capacity Total capacity of the ring buffer
 * @return Physical index in the circular buffer array
 */
static size_t circular_index(size_t position, size_t capacity) {
    return position % capacity;
}

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create a new kill ring instance
 * @param ring Pointer to store the created kill ring
 * @param max_entries Maximum number of entries (0 for default, clamped to max)
 * @param pool Memory pool for allocations (NULL to use malloc/free)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_ring_create(lle_kill_ring_t **ring, size_t max_entries,
                                  lle_memory_pool_t *pool) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    // Use default size if 0
    if (max_entries == 0) {
        max_entries = LLE_KILL_RING_DEFAULT_SIZE;
    }

    // Clamp to maximum
    if (max_entries > LLE_KILL_RING_MAX_SIZE) {
        max_entries = LLE_KILL_RING_MAX_SIZE;
    }

    // Allocate ring structure
    lle_kill_ring_t *new_ring;
    if (pool != NULL) {
        new_ring = (lle_kill_ring_t *)lle_pool_allocate_fast(
            pool, sizeof(lle_kill_ring_t));
    } else {
        new_ring = (lle_kill_ring_t *)malloc(sizeof(lle_kill_ring_t));
    }

    if (new_ring == NULL) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Allocate entries array
    if (pool != NULL) {
        new_ring->entries = (lle_kill_entry_t *)lle_pool_allocate_fast(
            pool, sizeof(lle_kill_entry_t) * max_entries);
    } else {
        new_ring->entries =
            (lle_kill_entry_t *)malloc(sizeof(lle_kill_entry_t) * max_entries);
    }

    if (new_ring->entries == NULL) {
        if (pool != NULL) {
            lle_pool_free_fast(pool, new_ring);
        } else {
            free(new_ring);
        }
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize entries
    memset(new_ring->entries, 0, sizeof(lle_kill_entry_t) * max_entries);

    // Initialize ring state
    new_ring->capacity = max_entries;
    new_ring->count = 0;
    new_ring->head = 0;
    new_ring->yank_position = 0;
    new_ring->last_was_yank = false;
    new_ring->last_was_kill = false;
    new_ring->pool = pool;

    // Initialize thread safety
    if (pthread_mutex_init(&new_ring->lock, NULL) != 0) {
        if (pool != NULL) {
            lle_pool_free_fast(pool, new_ring->entries);
            lle_pool_free_fast(pool, new_ring);
        } else {
            free(new_ring->entries);
            free(new_ring);
        }
        return LLE_ERROR_INITIALIZATION_FAILED;
    }

    *ring = new_ring;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a kill ring and free all resources
 * @param ring Kill ring to destroy
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if ring is NULL
 */
lle_result_t lle_kill_ring_destroy(lle_kill_ring_t *ring) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    // Free all entries
    for (size_t i = 0; i < ring->capacity; i++) {
        if (ring->entries[i].allocated) {
            free_entry(ring, &ring->entries[i]);
        }
    }

    // Free entries array
    if (ring->pool != NULL) {
        lle_pool_free_fast(ring->pool, ring->entries);
    } else {
        free(ring->entries);
    }

    pthread_mutex_unlock(&ring->lock);
    pthread_mutex_destroy(&ring->lock);

    // Free ring structure
    if (ring->pool != NULL) {
        lle_pool_free_fast(ring->pool, ring);
    } else {
        free(ring);
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * KILL OPERATIONS
 * ============================================================================
 */

/**
 * @brief Add text to the kill ring
 * @param ring Kill ring instance
 * @param text Text to add (must not be NULL or empty)
 * @param append If true and last operation was kill, append to current entry
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_ring_add(lle_kill_ring_t *ring, const char *text,
                               bool append) {
    if (ring == NULL || text == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    size_t text_len = strlen(text);
    if (text_len == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&ring->lock);

    // If append mode and ring not empty and last operation was kill
    if (append && ring->count > 0 && ring->last_was_kill) {
        // Append to current head entry
        lle_kill_entry_t *entry = &ring->entries[ring->head];

        // Reallocate with additional space
        size_t new_len = entry->length + text_len;
        char *new_text = kill_ring_strdup(ring, "", new_len);

        if (new_text == NULL) {
            pthread_mutex_unlock(&ring->lock);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        // Copy old text + new text
        memcpy(new_text, entry->text, entry->length);
        memcpy(new_text + entry->length, text, text_len);
        new_text[new_len] = '\0';

        // Free old text and update entry
        kill_ring_free_string(ring, entry->text);
        entry->text = new_text;
        entry->length = new_len;

    } else {
        // Create new entry

        // Move head forward (circular)
        ring->head = circular_index(ring->head + 1, ring->capacity);

        // If ring full, free oldest entry
        lle_kill_entry_t *entry = &ring->entries[ring->head];
        if (entry->allocated) {
            free_entry(ring, entry);
        } else if (ring->count < ring->capacity) {
            ring->count++;
        }

        // Allocate and store new text
        entry->text = kill_ring_strdup(ring, text, text_len);
        if (entry->text == NULL) {
            pthread_mutex_unlock(&ring->lock);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        entry->length = text_len;
        entry->allocated = true;
    }

    // Reset yank state, set kill state
    ring->last_was_yank = false;
    ring->last_was_kill = true;
    ring->yank_position = ring->head;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Prepend text to the current kill ring entry
 * @param ring Kill ring instance
 * @param text Text to prepend (must not be NULL or empty)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_ring_prepend(lle_kill_ring_t *ring, const char *text) {
    if (ring == NULL || text == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    size_t text_len = strlen(text);
    if (text_len == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&ring->lock);

    // If ring empty, just add as new entry
    if (ring->count == 0) {
        pthread_mutex_unlock(&ring->lock);
        return lle_kill_ring_add(ring, text, false);
    }

    // Prepend to current head entry
    lle_kill_entry_t *entry = &ring->entries[ring->head];

    // Reallocate with additional space
    size_t new_len = text_len + entry->length;
    char *new_text = kill_ring_strdup(ring, "", new_len);

    if (new_text == NULL) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Copy new text + old text
    memcpy(new_text, text, text_len);
    memcpy(new_text + text_len, entry->text, entry->length);
    new_text[new_len] = '\0';

    // Free old text and update entry
    kill_ring_free_string(ring, entry->text);
    entry->text = new_text;
    entry->length = new_len;

    // Reset yank state, set kill state
    ring->last_was_yank = false;
    ring->last_was_kill = true;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/* ============================================================================
 * YANK OPERATIONS
 * ============================================================================
 */

/**
 * @brief Get the current (most recent) kill ring entry for yanking
 * @param ring Kill ring instance
 * @param text_out Pointer to store the text (not a copy, do not free)
 * @return LLE_SUCCESS on success, LLE_ERROR_QUEUE_EMPTY if ring is empty
 */
lle_result_t lle_kill_ring_get_current(lle_kill_ring_t *ring,
                                       const char **text_out) {
    if (ring == NULL || text_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    if (ring->count == 0) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_QUEUE_EMPTY;
    }

    // Return most recent kill (head)
    lle_kill_entry_t *entry = &ring->entries[ring->head];
    *text_out = entry->text;

    // Set yank state
    ring->last_was_yank = true;
    ring->last_was_kill = false;
    ring->yank_position = ring->head;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Get the previous kill ring entry (yank-pop operation)
 * @param ring Kill ring instance
 * @param text_out Pointer to store the text (not a copy, do not free)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_STATE if not after yank
 */
lle_result_t lle_kill_ring_yank_pop(lle_kill_ring_t *ring,
                                    const char **text_out) {
    if (ring == NULL || text_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    // Must be after yank operation
    if (!ring->last_was_yank) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_INVALID_STATE;
    }

    if (ring->count == 0) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_QUEUE_EMPTY;
    }

    // Move backwards through ring (circular)
    if (ring->yank_position == 0) {
        ring->yank_position = ring->capacity - 1;
    } else {
        ring->yank_position--;
    }

    // Find next allocated entry going backwards
    size_t attempts = 0;
    while (!ring->entries[ring->yank_position].allocated &&
           attempts < ring->capacity) {
        if (ring->yank_position == 0) {
            ring->yank_position = ring->capacity - 1;
        } else {
            ring->yank_position--;
        }
        attempts++;
    }

    // Should always find one if count > 0
    if (!ring->entries[ring->yank_position].allocated) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_STATE_CORRUPTION;
    }

    *text_out = ring->entries[ring->yank_position].text;

    // Maintain yank state
    ring->last_was_yank = true;
    ring->last_was_kill = false;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/* ============================================================================
 * STATE MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Clear all entries from the kill ring
 * @param ring Kill ring instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if ring is NULL
 */
lle_result_t lle_kill_ring_clear(lle_kill_ring_t *ring) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    // Free all entries
    for (size_t i = 0; i < ring->capacity; i++) {
        if (ring->entries[i].allocated) {
            free_entry(ring, &ring->entries[i]);
        }
    }

    // Reset state
    ring->count = 0;
    ring->head = 0;
    ring->yank_position = 0;
    ring->last_was_yank = false;
    ring->last_was_kill = false;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Reset the yank state flag
 * @param ring Kill ring instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if ring is NULL
 */
lle_result_t lle_kill_ring_reset_yank_state(lle_kill_ring_t *ring) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    ring->last_was_yank = false;
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Set the last-was-kill state flag
 * @param ring Kill ring instance
 * @param was_kill True if last operation was a kill
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if ring is NULL
 */
lle_result_t lle_kill_ring_set_last_was_kill(lle_kill_ring_t *ring,
                                             bool was_kill) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    ring->last_was_kill = was_kill;
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get the number of entries in the kill ring
 * @param ring Kill ring instance
 * @param count_out Pointer to store the count
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_kill_ring_get_count(lle_kill_ring_t *ring, size_t *count_out) {
    if (ring == NULL || count_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    *count_out = ring->count;
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Check if the kill ring is empty
 * @param ring Kill ring instance
 * @param empty_out Pointer to store the result (true if empty)
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_kill_ring_is_empty(lle_kill_ring_t *ring, bool *empty_out) {
    if (ring == NULL || empty_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    *empty_out = (ring->count == 0);
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Check if the last operation was a yank
 * @param ring Kill ring instance
 * @param was_yank_out Pointer to store the result
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_kill_ring_was_last_yank(lle_kill_ring_t *ring,
                                         bool *was_yank_out) {
    if (ring == NULL || was_yank_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    *was_yank_out = ring->last_was_yank;
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Get the maximum capacity of the kill ring
 * @param ring Kill ring instance
 * @param capacity_out Pointer to store the capacity
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER on NULL argument
 */
lle_result_t lle_kill_ring_get_capacity(lle_kill_ring_t *ring,
                                        size_t *capacity_out) {
    if (ring == NULL || capacity_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);
    *capacity_out = ring->capacity;
    pthread_mutex_unlock(&ring->lock);

    return LLE_SUCCESS;
}

/* ============================================================================
 * DEBUGGING/INTROSPECTION
 * ============================================================================
 */

#ifdef LLE_DEBUG

#include <stdio.h>

/**
 * @brief Get entry at a specific index (debug only)
 * @param ring Kill ring instance
 * @param index Logical index (0 = most recent)
 * @param text_out Pointer to store the text
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_kill_ring_get_entry_at_index(lle_kill_ring_t *ring,
                                              size_t index,
                                              const char **text_out) {
    if (ring == NULL || text_out == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    if (index >= ring->count) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_OUT_OF_BOUNDS;
    }

    // Convert logical index to circular buffer position
    size_t pos = circular_index(ring->head - index, ring->capacity);

    if (!ring->entries[pos].allocated) {
        pthread_mutex_unlock(&ring->lock);
        return LLE_ERROR_INTERNAL;
    }

    *text_out = ring->entries[pos].text;

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Dump kill ring contents to stdout (debug only)
 * @param ring Kill ring instance
 * @return LLE_SUCCESS on success, LLE_ERROR_NULL_POINTER if ring is NULL
 */
lle_result_t lle_kill_ring_dump(lle_kill_ring_t *ring) {
    if (ring == NULL) {
        return LLE_ERROR_NULL_POINTER;
    }

    pthread_mutex_lock(&ring->lock);

    printf("Kill Ring Dump:\n");
    printf("  Capacity: %zu\n", ring->capacity);
    printf("  Count: %zu\n", ring->count);
    printf("  Head: %zu\n", ring->head);
    printf("  Yank Position: %zu\n", ring->yank_position);
    printf("  Last Was Yank: %s\n", ring->last_was_yank ? "true" : "false");
    printf("  Last Was Kill: %s\n", ring->last_was_kill ? "true" : "false");
    printf("\nEntries:\n");

    for (size_t i = 0; i < ring->capacity; i++) {
        if (ring->entries[i].allocated) {
            printf("  [%zu] (len=%zu): \"%s\"\n", i, ring->entries[i].length,
                   ring->entries[i].text);
        } else {
            printf("  [%zu] (empty)\n", i);
        }
    }

    pthread_mutex_unlock(&ring->lock);
    return LLE_SUCCESS;
}

#endif // LLE_DEBUG
