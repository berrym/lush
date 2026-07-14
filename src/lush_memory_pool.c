/**
 * @file lush_memory_pool.c
 * @brief Memory pool system for display operations
 *
 * Enterprise-grade memory management optimized for display operations:
 * - Pre-allocated memory pools by size class (small, medium, large, xlarge)
 * - Thread-safe allocation and deallocation
 * - Automatic malloc fallback for oversized requests
 * - Performance statistics and debugging support
 * - Pool health monitoring
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 */

#include "lush_memory_pool.h"
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/// Global memory pool system instance
lush_memory_pool_system_t *global_memory_pool = NULL;

/// Thread safety mutex for pool operations
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

/// Debug and error tracking
static bool debug_mode = false;
static lush_pool_error_t last_error = LUSH_POOL_SUCCESS;

/// Fallback size tracking for analysis
static size_t fallback_sizes[100];
static int fallback_count = 0;

/// Malloc fallback pointer tracking for proper cleanup
/// Uses two lockstep dynamic arrays that grow as needed: one for the pointers,
/// one for their requested sizes. The sizes are the authoritative usable
/// capacity of each fallback allocation, consulted by lush_pool_realloc so it
/// never over-reads a smaller original buffer when growing.
#define INITIAL_FALLBACK_CAPACITY 256
static void **malloc_fallback_ptrs = NULL;
static size_t *malloc_fallback_sizes = NULL;
static size_t malloc_fallback_count = 0;
static size_t malloc_fallback_capacity = 0;

/**
 * @brief Track a malloc fallback allocation for later cleanup
 * @param ptr Pointer to track
 * @param size Requested allocation size in bytes (usable capacity)
 * @return true on success, false on failure
 */
static bool track_malloc_fallback(void *ptr, size_t size) {
    if (!ptr)
        return false;

    /// Initialize arrays on first use
    if (!malloc_fallback_ptrs) {
        malloc_fallback_ptrs =
            malloc(INITIAL_FALLBACK_CAPACITY * sizeof(void *));
        if (!malloc_fallback_ptrs)
            return false;
        malloc_fallback_sizes =
            malloc(INITIAL_FALLBACK_CAPACITY * sizeof(size_t));
        if (!malloc_fallback_sizes) {
            free(malloc_fallback_ptrs);
            malloc_fallback_ptrs = NULL;
            return false;
        }
        malloc_fallback_capacity = INITIAL_FALLBACK_CAPACITY;
        malloc_fallback_count = 0;
    }

    /// Grow arrays if needed
    if (malloc_fallback_count >= malloc_fallback_capacity) {
        size_t new_capacity = malloc_fallback_capacity * 2;
        void **new_ptrs =
            realloc(malloc_fallback_ptrs, new_capacity * sizeof(void *));
        if (!new_ptrs)
            return false;
        malloc_fallback_ptrs = new_ptrs;
        size_t *new_sizes =
            realloc(malloc_fallback_sizes, new_capacity * sizeof(size_t));
        if (!new_sizes)
            return false;
        malloc_fallback_sizes = new_sizes;
        malloc_fallback_capacity = new_capacity;
    }

    malloc_fallback_sizes[malloc_fallback_count] = size;
    malloc_fallback_ptrs[malloc_fallback_count++] = ptr;
    return true;
}

/**
 * @brief Untrack a malloc fallback allocation (called when freed before
 * shutdown)
 * @param ptr Pointer to untrack
 * @return true if found and removed, false if not found
 */
static bool untrack_malloc_fallback(void *ptr) {
    if (!ptr || !malloc_fallback_ptrs)
        return false;

    for (size_t i = 0; i < malloc_fallback_count; i++) {
        if (malloc_fallback_ptrs[i] == ptr) {
            /// Move last element to this position (order doesn't matter);
            /// keep the size array in lockstep with the pointer array
            malloc_fallback_count--;
            malloc_fallback_ptrs[i] =
                malloc_fallback_ptrs[malloc_fallback_count];
            malloc_fallback_sizes[i] =
                malloc_fallback_sizes[malloc_fallback_count];
            return true;
        }
    }
    return false;
}

/**
 * @brief Look up the tracked size of a malloc fallback allocation
 * @param ptr Pointer to look up
 * @param size_out Receives the requested allocation size when found
 * @return true if ptr is a tracked fallback allocation, false otherwise
 */
static bool malloc_fallback_size(void *ptr, size_t *size_out) {
    if (!ptr || !malloc_fallback_ptrs)
        return false;

    for (size_t i = 0; i < malloc_fallback_count; i++) {
        if (malloc_fallback_ptrs[i] == ptr) {
            *size_out = malloc_fallback_sizes[i];
            return true;
        }
    }
    return false;
}

/// @brief Free all tracked malloc fallback allocations during shutdown
static void free_all_malloc_fallbacks(void) {
    if (!malloc_fallback_ptrs)
        return;

    for (size_t i = 0; i < malloc_fallback_count; i++) {
        if (malloc_fallback_ptrs[i]) {
            free(malloc_fallback_ptrs[i]);
        }
    }

    free(malloc_fallback_ptrs);
    free(malloc_fallback_sizes);
    malloc_fallback_ptrs = NULL;
    malloc_fallback_sizes = NULL;
    malloc_fallback_count = 0;
    malloc_fallback_capacity = 0;
}

/// Track whether pool was ever initialized (vs never used)
/// This distinguishes "pool shutdown" from "pool never started"
static bool pool_was_ever_initialized = false;

/// Pool size definitions (optimized for display operations)
static const size_t POOL_SIZES[LUSH_POOL_COUNT] = {
    128,  /// SMALL: state hashes, cache keys
    512,  /// MEDIUM: prompts, short outputs
    4096, /// LARGE: display compositions, multiline inputs
    16384 /// XLARGE: tab completions, complex outputs
};

/// Default pool block counts (optimized for typical usage)
static const size_t DEFAULT_BLOCK_COUNTS[LUSH_POOL_COUNT] = {
    512, /// SMALL: High frequency allocations (4x increase - analysis shows
         /// 100% fallbacks here)
    64,  /// MEDIUM: Moderate frequency (doubled from 32)
    32,  /// LARGE: Lower frequency, larger impact (doubled from 16)
    16   /// XLARGE: Infrequent but critical (doubled from 8)
};

/// Performance monitoring macros
#define POOL_DEBUG(fmt, ...)                                                   \
    do {                                                                       \
        if (debug_mode) {                                                      \
            fprintf(stderr, "[POOL] " fmt "\n", ##__VA_ARGS__);                \
        }                                                                      \
    } while (0)

#define POOL_ERROR(fmt, ...)                                                   \
    do {                                                                       \
        fprintf(stderr, "[POOL ERROR] " fmt "\n", ##__VA_ARGS__);              \
    } while (0)

/**
 * @brief Get current timestamp in microseconds
 * @return Current timestamp in microseconds since epoch
 */
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

/**
 * @brief Get current timestamp in nanoseconds
 * @return Current timestamp in nanoseconds from monotonic clock
 */
static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Set last error with debug information
 * @param error The error code to set as the last error
 */
static void set_last_error(lush_pool_error_t error) {
    last_error = error;
    if (error != LUSH_POOL_SUCCESS && debug_mode) {
        POOL_ERROR("Operation failed: %s", lush_pool_error_string(error));
    }
}

/**
 * @brief Initialize a single memory pool
 * @param pool Pointer to the pool structure to initialize
 * @param block_size Size of each block in the pool
 * @param initial_blocks Number of blocks to pre-allocate
 * @return LUSH_POOL_SUCCESS on success, error code on failure
 */
static lush_pool_error_t init_single_pool(lush_pool_t *pool, size_t block_size,
                                          size_t initial_blocks) {
    if (!pool || block_size == 0 || initial_blocks == 0) {
        return LUSH_POOL_ERROR_INVALID_SIZE;
    }

    /// Initialize pool structure
    memset(pool, 0, sizeof(lush_pool_t));
    pool->block_size = block_size;
    pool->initial_blocks = initial_blocks;
    pool->max_blocks = initial_blocks * 4; /// Allow growth up to 4x initial

    /// Allocate block management array
    pool->all_blocks = calloc(pool->max_blocks, sizeof(lush_pool_block_t));
    if (!pool->all_blocks) {
        POOL_ERROR(
            "Failed to allocate block management array for pool (size=%zu)",
            block_size);
        return LUSH_POOL_ERROR_MALLOC_FAILED;
    }

    /// Pre-allocate initial blocks
    for (size_t i = 0; i < initial_blocks; i++) {
        lush_pool_block_t *block = &pool->all_blocks[i];

        /// Allocate memory for this block
        block->memory = malloc(block_size);
        if (!block->memory) {
            POOL_ERROR("Failed to allocate memory block %zu (size=%zu)", i,
                       block_size);
            /// Cleanup previously allocated blocks
            for (size_t j = 0; j < i; j++) {
                free(pool->all_blocks[j].memory);
            }
            free(pool->all_blocks);
            return LUSH_POOL_ERROR_MALLOC_FAILED;
        }

        /// Initialize block metadata
        block->size = block_size;
        block->in_use = false;
        block->allocation_time_us = 0;
        block->allocation_id = 0;

        /// Add to free list
        block->next = pool->free_list;
        block->prev = NULL;
        if (pool->free_list) {
            pool->free_list->prev = block;
        }
        pool->free_list = block;
    }

    pool->current_blocks = initial_blocks;
    pool->free_blocks = initial_blocks;

    POOL_DEBUG("Initialized pool: block_size=%zu, initial_blocks=%zu",
               block_size, initial_blocks);
    return LUSH_POOL_SUCCESS;
}

/**
 * @brief Cleanup a single memory pool
 * @param pool Pointer to the pool structure to cleanup
 */
static void cleanup_single_pool(lush_pool_t *pool) {
    if (!pool || !pool->all_blocks) {
        return;
    }

    /// Free all allocated memory blocks
    for (size_t i = 0; i < pool->current_blocks; i++) {
        if (pool->all_blocks[i].memory) {
            free(pool->all_blocks[i].memory);
        }
    }

    /// Free block management array
    free(pool->all_blocks);

    /// Clear pool structure
    memset(pool, 0, sizeof(lush_pool_t));

    POOL_DEBUG("Cleaned up pool");
}

/**
 * @brief Find the appropriate pool for a given size
 * @param size The allocation size to find a pool for
 * @return Pool type that can accommodate the size, or LUSH_POOL_COUNT if too
 * large
 */
static lush_pool_size_t find_pool_for_size(size_t size) {
    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        if (size <= POOL_SIZES[i]) {
            return (lush_pool_size_t)i;
        }
    }
    /// Size is larger than any pool - will use malloc fallback
    return LUSH_POOL_COUNT; /// Invalid pool index indicates malloc fallback
}

/**
 * @brief Allocate a block from specific pool
 * @param pool Pointer to the pool to allocate from
 * @return Pointer to allocated memory, or NULL if pool is exhausted
 */
static void *allocate_from_pool(lush_pool_t *pool) {
    if (!pool || !pool->free_list) {
        return NULL;
    }

    /// Take first block from free list
    lush_pool_block_t *block = pool->free_list;

    /// Remove from free list
    pool->free_list = block->next;
    if (pool->free_list) {
        pool->free_list->prev = NULL;
    }

    /// Mark as in use
    block->in_use = true;
    block->allocation_time_us = get_timestamp_us();
    block->allocation_id = ++global_memory_pool->next_allocation_id;

    /// Update pool statistics
    pool->free_blocks--;
    pool->pool_allocations++;

    POOL_DEBUG("Allocated block: size=%zu, id=%u, ptr=%p", block->size,
               block->allocation_id, block->memory);

    return block->memory;
}

/**
 * @brief Return a block to specific pool
 * @param pool Pointer to the pool to return the block to
 * @param ptr Pointer to the memory block to return
 * @return true if block was successfully returned, false otherwise
 */
static bool return_to_pool(lush_pool_t *pool, void *ptr) {
    if (!pool || !ptr) {
        return false;
    }

    /// Find the block that contains this pointer
    lush_pool_block_t *block = NULL;
    for (size_t i = 0; i < pool->current_blocks; i++) {
        if (pool->all_blocks[i].memory == ptr) {
            block = &pool->all_blocks[i];
            break;
        }
    }

    if (!block || !block->in_use) {
        return false; /// Not found or double free
    }

    /// Mark as free
    block->in_use = false;
    block->allocation_time_us = 0;
    block->allocation_id = 0;

    /// Add back to free list
    block->next = pool->free_list;
    block->prev = NULL;
    if (pool->free_list) {
        pool->free_list->prev = block;
    }
    pool->free_list = block;

    /// Update statistics
    pool->free_blocks++;
    pool->pool_deallocations++;

    POOL_DEBUG("Returned block to pool: size=%zu, ptr=%p", block->size, ptr);
    return true;
}

/**
 * @brief Update global statistics after an allocation
 * @param pool_hit Whether the allocation came from a pool (true) or malloc
 * (false)
 * @param size Size of the allocation in bytes
 * @param allocation_time_ns Time taken for the allocation in nanoseconds
 */
static void update_stats(bool pool_hit, size_t size,
                         uint64_t allocation_time_ns) {
    if (!global_memory_pool->enable_statistics) {
        return;
    }

    lush_pool_stats_t *stats = &global_memory_pool->stats;

    stats->total_allocations++;
    stats->total_bytes_allocated += size;
    stats->active_allocations++;

    if (pool_hit) {
        stats->pool_hits++;
        stats->current_pool_usage += size;
        if (stats->current_pool_usage > stats->peak_pool_usage) {
            stats->peak_pool_usage = stats->current_pool_usage;
        }
    } else {
        stats->pool_misses++;
        stats->malloc_fallbacks++;
    }

    /// Update hit rate
    stats->pool_hit_rate =
        (double)stats->pool_hits / stats->total_allocations * 100.0;

    /// Update average allocation time
    if (stats->total_allocations == 1) {
        stats->avg_allocation_time_ns = allocation_time_ns;
    } else {
        stats->avg_allocation_time_ns =
            (stats->avg_allocation_time_ns * (stats->total_allocations - 1) +
             allocation_time_ns) /
            stats->total_allocations;
    }
}

/**
 * Public API Implementation
 */

/**
 * @brief Initialize the memory pool system with configuration
 * @param config Pointer to configuration structure, or NULL for defaults
 * @return LUSH_POOL_SUCCESS on success, error code on failure
 */
lush_pool_error_t lush_pool_init(const lush_pool_config_t *config) {
    pthread_mutex_lock(&pool_mutex);

    /// Check if already initialized
    if (global_memory_pool) {
        pthread_mutex_unlock(&pool_mutex);
        set_last_error(LUSH_POOL_SUCCESS);
        return LUSH_POOL_SUCCESS; /// Allow multiple inits
    }

    /// Allocate global pool system
    global_memory_pool = calloc(1, sizeof(lush_memory_pool_system_t));
    if (!global_memory_pool) {
        pthread_mutex_unlock(&pool_mutex);
        set_last_error(LUSH_POOL_ERROR_MALLOC_FAILED);
        return LUSH_POOL_ERROR_MALLOC_FAILED;
    }

    /// Use provided config or defaults
    lush_pool_config_t default_config = lush_pool_get_default_config();
    if (!config) {
        config = &default_config;
    }

    /// Set system configuration
    global_memory_pool->enable_statistics = config->enable_statistics;
    global_memory_pool->enable_malloc_fallback = config->enable_malloc_fallback;
    debug_mode = config->enable_debugging;

    /// Initialize timestamp
    clock_gettime(CLOCK_MONOTONIC, &global_memory_pool->init_time);

    /// Initialize individual pools
    size_t block_counts[LUSH_POOL_COUNT] = {
        config->small_pool_blocks, config->medium_pool_blocks,
        config->large_pool_blocks, config->xlarge_pool_blocks};

    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        lush_pool_error_t result = init_single_pool(
            &global_memory_pool->pools[i], POOL_SIZES[i], block_counts[i]);

        if (result != LUSH_POOL_SUCCESS) {
            POOL_ERROR("Failed to initialize pool %d", i);
            /// Cleanup already initialized pools
            for (int j = 0; j < i; j++) {
                cleanup_single_pool(&global_memory_pool->pools[j]);
            }
            free(global_memory_pool);
            global_memory_pool = NULL;
            pthread_mutex_unlock(&pool_mutex);
            set_last_error(result);
            return result;
        }
    }

    global_memory_pool->initialized = true;
    pool_was_ever_initialized = true;

    pthread_mutex_unlock(&pool_mutex);

    POOL_DEBUG("Memory pool system initialized successfully");
    POOL_DEBUG("Pools: Small=%zu, Medium=%zu, Large=%zu, XLarge=%zu",
               block_counts[0], block_counts[1], block_counts[2],
               block_counts[3]);

    set_last_error(LUSH_POOL_SUCCESS);
    return LUSH_POOL_SUCCESS;
}

/**
 * @brief Shutdown and cleanup the memory pool system
 */
void lush_pool_shutdown(void) {
    pthread_mutex_lock(&pool_mutex);

    if (!global_memory_pool) {
        pthread_mutex_unlock(&pool_mutex);
        return;
    }

    POOL_DEBUG("Shutting down memory pool system");

    /// Print final statistics if enabled
    if (global_memory_pool->enable_statistics && debug_mode) {
        lush_pool_print_status_report();
    }

    /// Free all tracked malloc fallback allocations first
    free_all_malloc_fallbacks();

    /// Cleanup all pools
    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        cleanup_single_pool(&global_memory_pool->pools[i]);
    }

    /// Free global pool system
    free(global_memory_pool);
    global_memory_pool = NULL;

    pthread_mutex_unlock(&pool_mutex);

    POOL_DEBUG("Memory pool system shutdown complete");
}

/**
 * @brief Allocate memory from appropriate pool or fallback to malloc
 * @param size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL on failure
 */
void *lush_pool_alloc(size_t size) {
    if (size == 0) {
        set_last_error(LUSH_POOL_ERROR_INVALID_SIZE);
        return NULL;
    }

    uint64_t start_time = get_timestamp_ns();
    void *result = NULL;
    bool pool_hit = false;

    pthread_mutex_lock(&pool_mutex);

    if (!global_memory_pool || !global_memory_pool->initialized) {
        /// Fallback to malloc if pool not initialized. Track the allocation and
        /// its size like any other fallback so lush_pool_realloc can recover
        /// the original size and shutdown can reclaim it.
        result = malloc(size);
        if (result) {
            track_malloc_fallback(result, size);
            if (global_memory_pool) {
                update_stats(false, size, get_timestamp_ns() - start_time);
            }
        }
        pthread_mutex_unlock(&pool_mutex);
        set_last_error(result ? LUSH_POOL_SUCCESS
                              : LUSH_POOL_ERROR_MALLOC_FAILED);
        return result;
    }

    /// Find appropriate pool
    lush_pool_size_t pool_type = find_pool_for_size(size);

    if (pool_type < LUSH_POOL_COUNT) {
        /// Try to allocate from pool
        result = allocate_from_pool(&global_memory_pool->pools[pool_type]);
        if (result) {
            pool_hit = true;
            POOL_DEBUG("Pool allocation: size=%zu, pool=%d", size, pool_type);
        }
    }

    /// Fallback to malloc if pool allocation failed or size too large
    if (!result && global_memory_pool->enable_malloc_fallback) {
        result = malloc(size);

        /// Track fallback sizes for optimization analysis
        if (fallback_count < 100) {
            fallback_sizes[fallback_count++] = size;
        }

        /// Track pointer for cleanup during shutdown
        if (result) {
            track_malloc_fallback(result, size);
        }

        POOL_DEBUG("Malloc fallback: size=%zu (total fallbacks: %d)", size,
                   fallback_count);
    }

    /// Update statistics
    if (result) {
        update_stats(pool_hit, size, get_timestamp_ns() - start_time);
    }

    pthread_mutex_unlock(&pool_mutex);

    set_last_error(result ? LUSH_POOL_SUCCESS : LUSH_POOL_ERROR_MALLOC_FAILED);
    return result;
}

/**
 * @brief Free memory back to pool or call free() for malloc allocations
 * @param ptr Pointer to memory to free (NULL is safely ignored)
 */
void lush_pool_free(void *ptr) {
    if (!ptr) {
        return;
    }

    pthread_mutex_lock(&pool_mutex);

    /// Handle case where pool is NULL
    if (!global_memory_pool) {
        if (pool_was_ever_initialized) {
            /// Pool was shut down - memory was already freed during shutdown.
            /// Do NOT call free() here as it would cause double-free.
            pthread_mutex_unlock(&pool_mutex);
            return;
        } else {
            /// Pool was never initialized - this memory came from a pre-init
            /// malloc fallback. Untrack it (still under the lock) before
            /// freeing so a later init+shutdown cannot double-free a stale
            /// entry.
            untrack_malloc_fallback(ptr);
            pthread_mutex_unlock(&pool_mutex);
            free(ptr);
            return;
        }
    }

    bool returned_to_pool = false;

    if (global_memory_pool->initialized) {
        /// Try to return to appropriate pool
        for (int i = 0; i < LUSH_POOL_COUNT; i++) {
            if (return_to_pool(&global_memory_pool->pools[i], ptr)) {
                returned_to_pool = true;

                /// Update statistics
                if (global_memory_pool->enable_statistics) {
                    global_memory_pool->stats.active_allocations--;
                    global_memory_pool->stats.current_pool_usage -=
                        POOL_SIZES[i];
                }

                POOL_DEBUG("Freed to pool: size=%zu, pool=%d", POOL_SIZES[i],
                           i);
                break;
            }
        }
    }

    pthread_mutex_unlock(&pool_mutex);

    /// If not returned to pool, use standard free
    if (!returned_to_pool) {
        /// Untrack from malloc fallback list before freeing
        pthread_mutex_lock(&pool_mutex);
        untrack_malloc_fallback(ptr);
        pthread_mutex_unlock(&pool_mutex);

        uintptr_t ptr_addr =
            (uintptr_t)ptr; /// Save address as integer before free
        free(ptr);
        POOL_DEBUG("Standard free: ptr=%#" PRIxPTR, ptr_addr);

        /// Update statistics for malloc fallback free
        if (global_memory_pool && global_memory_pool->enable_statistics) {
            pthread_mutex_lock(&pool_mutex);
            global_memory_pool->stats.active_allocations--;
            pthread_mutex_unlock(&pool_mutex);
        }
    }

    set_last_error(LUSH_POOL_SUCCESS);
}

/**
 * @brief Usable capacity in bytes of a live allocation owned by the pool
 * @param ptr Pointer previously returned by lush_pool_alloc
 * @return Backing capacity in bytes, or 0 if ptr is not owned by the pool
 *
 * For a pooled allocation this is the fixed block size; for a malloc fallback
 * it is the tracked requested size. The caller must hold pool_mutex.
 */
static size_t usable_capacity_for_ptr(void *ptr) {
    if (!ptr) {
        return 0;
    }

    if (global_memory_pool && global_memory_pool->initialized) {
        for (int i = 0; i < LUSH_POOL_COUNT; i++) {
            lush_pool_t *pool = &global_memory_pool->pools[i];
            for (size_t j = 0; j < pool->current_blocks; j++) {
                if (pool->all_blocks[j].memory == ptr &&
                    pool->all_blocks[j].in_use) {
                    return pool->all_blocks[j].size;
                }
            }
        }
    }

    size_t fallback = 0;
    if (malloc_fallback_size(ptr, &fallback)) {
        return fallback;
    }

    return 0;
}

/**
 * @brief Reallocate memory (may use malloc for complex resizing)
 * @param ptr Pointer to existing memory allocation (NULL allocates new memory)
 * @param new_size New size in bytes (0 frees the memory)
 * @return Pointer to reallocated memory, or NULL on failure
 */
void *lush_pool_realloc(void *ptr, size_t new_size) {
    if (new_size == 0) {
        lush_pool_free(ptr);
        return NULL;
    }

    if (!ptr) {
        return lush_pool_alloc(new_size);
    }

    /// The pool does not record the caller's original requested size, but the
    /// usable capacity of the backing allocation (fixed block size, or the
    /// tracked malloc-fallback size) is a safe upper bound on what may be read
    /// from ptr. Copying at most that many bytes preserves the caller's data
    /// without over-reading a smaller original buffer when growing.
    pthread_mutex_lock(&pool_mutex);
    size_t old_capacity = usable_capacity_for_ptr(ptr);
    pthread_mutex_unlock(&pool_mutex);

    void *new_ptr = lush_pool_alloc(new_size);
    if (new_ptr) {
        size_t copy_size = (new_size < old_capacity) ? new_size : old_capacity;
        if (copy_size > 0) {
            memcpy(new_ptr, ptr, copy_size);
        }
        lush_pool_free(ptr);
    }

    return new_ptr;
}

/**
 * @brief Duplicate string using pool allocation
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char *lush_pool_strdup(const char *str) {
    if (!str) {
        set_last_error(LUSH_POOL_ERROR_INVALID_POINTER);
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *dup = (char *)lush_pool_alloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }

    return dup;
}

/**
 * @brief Allocate and zero-initialize memory
 * @param count Number of elements to allocate
 * @param size Size of each element in bytes
 * @return Pointer to zero-initialized memory, or NULL on failure
 */
void *lush_pool_calloc(size_t count, size_t size) {
    size_t total_size = count * size;
    void *ptr = lush_pool_alloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

/**
 * @brief Get current pool statistics
 * @return Copy of the current pool statistics structure
 */
lush_pool_stats_t lush_pool_get_stats(void) {
    lush_pool_stats_t stats = {0};

    if (!global_memory_pool) {
        return stats;
    }

    pthread_mutex_lock(&pool_mutex);
    stats = global_memory_pool->stats;
    pthread_mutex_unlock(&pool_mutex);

    return stats;
}

/**
 * @brief Reset statistics counters
 */
void lush_pool_reset_stats(void) {
    if (!global_memory_pool) {
        return;
    }

    pthread_mutex_lock(&pool_mutex);
    memset(&global_memory_pool->stats, 0, sizeof(lush_pool_stats_t));
    pthread_mutex_unlock(&pool_mutex);

    POOL_DEBUG("Pool statistics reset");
}

/**
 * @brief Check if pool system is healthy
 * @return true if all pools have available capacity, false otherwise
 */
bool lush_pool_is_healthy(void) {
    if (!global_memory_pool || !global_memory_pool->initialized) {
        return false;
    }

    pthread_mutex_lock(&pool_mutex);

    /// Check each pool has some free blocks available
    bool healthy = true;
    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        if (global_memory_pool->pools[i].free_blocks == 0 &&
            global_memory_pool->pools[i].current_blocks >=
                global_memory_pool->pools[i].max_blocks) {
            healthy = false;
            break;
        }
    }

    pthread_mutex_unlock(&pool_mutex);
    return healthy;
}

/**
 * @brief Get recommended pool size for given allocation size
 * @param size The allocation size to find a pool for
 * @return Pool type that can accommodate the size
 */
lush_pool_size_t lush_pool_get_recommended_size(size_t size) {
    return find_pool_for_size(size);
}

/**
 * @brief Analyze malloc fallback patterns for optimization
 */
void lush_pool_analyze_fallback_patterns(void) {
    if (fallback_count == 0) {
        printf("No malloc fallbacks recorded\n");
        return;
    }

    printf("=== Memory Pool Fallback Analysis ===\n");
    printf("Total fallbacks: %d\n", fallback_count);

    /// Count fallbacks by size ranges
    int small_misses = 0, medium_misses = 0, large_misses = 0,
        xlarge_misses = 0, oversized = 0;

    for (int i = 0; i < fallback_count; i++) {
        size_t size = fallback_sizes[i];
        if (size <= 128)
            small_misses++;
        else if (size <= 512)
            medium_misses++;
        else if (size <= 4096)
            large_misses++;
        else if (size <= 16384)
            xlarge_misses++;
        else
            oversized++;
    }

    printf("Fallback breakdown:\n");
    printf("  ≤ 128B (SMALL pool):  %d fallbacks\n", small_misses);
    printf("  ≤ 512B (MEDIUM pool): %d fallbacks\n", medium_misses);
    printf("  ≤ 4KB (LARGE pool):   %d fallbacks\n", large_misses);
    printf("  ≤ 16KB (XLARGE pool): %d fallbacks\n", xlarge_misses);
    printf("  > 16KB (oversized):   %d fallbacks\n", oversized);

    /// Show pool status
    if (global_memory_pool && global_memory_pool->initialized) {
        printf("Pool Status:\n");
        for (int i = 0; i < LUSH_POOL_COUNT; i++) {
            lush_pool_t *pool = &global_memory_pool->pools[i];
            printf("  Pool %d (%zuB): %zu/%zu blocks free\n", i, POOL_SIZES[i],
                   pool->free_blocks, pool->current_blocks);
        }
    }

    /// Show actual sizes for first 20 fallbacks
    printf("First %d fallback sizes: ",
           (fallback_count < 20) ? fallback_count : 20);
    for (int i = 0; i < fallback_count && i < 20; i++) {
        printf("%zu ", fallback_sizes[i]);
    }
    printf("\n=====================================\n");
}

/**
 * @brief Check if pointer was allocated from pool system
 * @param ptr Pointer to check
 * @return true if pointer is from a pool, false otherwise
 */
bool lush_pool_is_pool_pointer(const void *ptr) {
    if (!ptr || !global_memory_pool) {
        return false;
    }

    pthread_mutex_lock(&pool_mutex);

    bool is_pool_ptr = false;
    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        lush_pool_t *pool = &global_memory_pool->pools[i];
        for (size_t j = 0; j < pool->current_blocks; j++) {
            if (pool->all_blocks[j].memory == ptr) {
                is_pool_ptr = true;
                goto cleanup;
            }
        }
    }

cleanup:
    pthread_mutex_unlock(&pool_mutex);
    return is_pool_ptr;
}

/**
 * @brief Generate detailed pool status report for debugging
 */
void lush_pool_print_status_report(void) {
    if (!global_memory_pool) {
        printf("Memory pool system not initialized\n");
        return;
    }

    pthread_mutex_lock(&pool_mutex);

    printf("\n=== Lush Memory Pool Status Report ===\n");

    lush_pool_stats_t *stats = &global_memory_pool->stats;
    printf("Overall Statistics:\n");
    printf("  Total allocations: %" PRIu64 "\n", stats->total_allocations);
    printf("  Pool hits: %" PRIu64 " (%.2f%%)\n", stats->pool_hits,
           stats->pool_hit_rate);
    printf("  Malloc fallbacks: %" PRIu64 "\n", stats->malloc_fallbacks);
    printf("  Active allocations: %u\n", stats->active_allocations);
    printf("  Pool memory usage: %" PRIu64 " bytes (peak: %" PRIu64 " bytes)\n",
           stats->current_pool_usage, stats->peak_pool_usage);
    printf("  Average allocation time: %" PRIu64 " ns\n",
           stats->avg_allocation_time_ns);

    printf("\nIndividual Pool Status:\n");
    const char *pool_names[] = {"Small", "Medium", "Large", "XLarge"};
    for (int i = 0; i < LUSH_POOL_COUNT; i++) {
        lush_pool_t *pool = &global_memory_pool->pools[i];
        printf("  %s Pool (%zu bytes): %zu/%zu blocks free, %" PRIu64
               " allocs, %" PRIu64 " deallocs\n",
               pool_names[i], pool->block_size, pool->free_blocks,
               pool->current_blocks, pool->pool_allocations,
               pool->pool_deallocations);
    }

    printf("========================================\n\n");

    pthread_mutex_unlock(&pool_mutex);
}

/**
 * @brief Get default pool configuration for typical usage
 * @return Default configuration structure
 */
lush_pool_config_t lush_pool_get_default_config(void) {
    lush_pool_config_t config = {
        .small_pool_blocks = DEFAULT_BLOCK_COUNTS[LUSH_POOL_SMALL],
        .medium_pool_blocks = DEFAULT_BLOCK_COUNTS[LUSH_POOL_MEDIUM],
        .large_pool_blocks = DEFAULT_BLOCK_COUNTS[LUSH_POOL_LARGE],
        .xlarge_pool_blocks = DEFAULT_BLOCK_COUNTS[LUSH_POOL_XLARGE],
        .enable_statistics = true,
        .enable_malloc_fallback = true,
        .enable_debugging = false};
    return config;
}

/**
 * @brief Create optimized configuration for display-heavy workloads
 * @return Configuration structure optimized for display operations
 */
lush_pool_config_t lush_pool_get_display_optimized_config(void) {
    lush_pool_config_t config = lush_pool_get_default_config();

    /// Optimize for display operations based on fallback analysis
    config.small_pool_blocks =
        512; /// Analysis shows 100% fallbacks are small allocations
    config.medium_pool_blocks = 64; /// More medium blocks for prompts
    config.large_pool_blocks = 32;  /// More large blocks for compositions
    config.xlarge_pool_blocks = 16; /// More XL blocks for complex outputs

    return config;
}

/**
 * @brief Convert error code to human-readable string
 * @param error The error code to convert
 * @return Human-readable error description string
 */
const char *lush_pool_error_string(lush_pool_error_t error) {
    switch (error) {
    case LUSH_POOL_SUCCESS:
        return "Success";
    case LUSH_POOL_ERROR_NOT_INITIALIZED:
        return "Pool system not initialized";
    case LUSH_POOL_ERROR_INVALID_SIZE:
        return "Invalid allocation size";
    case LUSH_POOL_ERROR_POOL_EXHAUSTED:
        return "Pool exhausted";
    case LUSH_POOL_ERROR_INVALID_POINTER:
        return "Invalid pointer";
    case LUSH_POOL_ERROR_MALLOC_FAILED:
        return "Malloc allocation failed";
    case LUSH_POOL_ERROR_BLOCK_NOT_FOUND:
        return "Block not found";
    case LUSH_POOL_ERROR_DOUBLE_FREE:
        return "Double free detected";
    case LUSH_POOL_ERROR_INITIALIZATION_FAILED:
        return "Initialization failed";
    default:
        return "Unknown error";
    }
}

/**
 * @brief Enable/disable debug mode with detailed logging
 * @param enabled true to enable debug mode, false to disable
 */
void lush_pool_set_debug_mode(bool enabled) {
    debug_mode = enabled;
    POOL_DEBUG("Debug mode %s", enabled ? "enabled" : "disabled");
}

/**
 * @brief Get last error that occurred in pool operations
 * @return Last error code from pool operations
 */
lush_pool_error_t lush_pool_get_last_error(void) { return last_error; }

/**
 * @brief Get pool memory usage for display performance reporting
 * @param pool_bytes Pointer to store current pool usage in bytes (may be NULL)
 * @param malloc_bytes Pointer to store malloc fallback usage in bytes (may be
 * NULL)
 * @param pool_efficiency Pointer to store pool hit rate percentage (may be
 * NULL)
 */
void lush_pool_get_memory_usage(uint64_t *pool_bytes, uint64_t *malloc_bytes,
                                double *pool_efficiency) {
    if (!global_memory_pool) {
        if (pool_bytes)
            *pool_bytes = 0;
        if (malloc_bytes)
            *malloc_bytes = 0;
        if (pool_efficiency)
            *pool_efficiency = 0.0;
        return;
    }

    pthread_mutex_lock(&pool_mutex);

    if (pool_bytes) {
        *pool_bytes = global_memory_pool->stats.current_pool_usage;
    }

    if (malloc_bytes) {
        *malloc_bytes = global_memory_pool->stats.total_bytes_allocated -
                        global_memory_pool->stats.current_pool_usage;
    }

    if (pool_efficiency) {
        *pool_efficiency = global_memory_pool->stats.pool_hit_rate;
    }

    pthread_mutex_unlock(&pool_mutex);
}

/**
 * @brief Check if pool system is meeting performance targets
 * @return true if performance targets are met, false otherwise
 */
bool lush_pool_meets_performance_targets(void) {
    if (!global_memory_pool) {
        return false;
    }

    pthread_mutex_lock(&pool_mutex);

    /// Performance targets:
    /// - Pool hit rate > 80%
    /// - Average allocation time < 1000 ns
    /// - System healthy (pools not exhausted)

    bool meets_targets =
        (global_memory_pool->stats.pool_hit_rate > 80.0) &&
        (global_memory_pool->stats.avg_allocation_time_ns < 1000) &&
        lush_pool_is_healthy();

    pthread_mutex_unlock(&pool_mutex);
    return meets_targets;
}