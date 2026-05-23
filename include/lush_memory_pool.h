/**
 * @file lush_memory_pool.h
 * @brief Memory pool system for display operations
 *
 * Provides enterprise-grade memory management with pooled allocation
 * optimized for display and rendering operations.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 * @license MIT
 */

#ifndef LUSH_MEMORY_POOL_H
#define LUSH_MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memory Pool System - Enterprise-Grade Memory Management
 *
 * Designed for high-frequency allocation patterns in display operations,
 * cache management, and composition engine. Provides automatic fallback
 * to malloc for robustness and comprehensive memory usage tracking.
 */

// Memory pool size categories
typedef enum {
    LUSH_POOL_SMALL = 0,  /**< 128B - state hashes, small strings, cache keys */
    LUSH_POOL_MEDIUM = 1, /**< 512B - prompts, short outputs, command strings */
    LUSH_POOL_LARGE =
        2, /**< 4KB - display outputs, compositions, multiline inputs */
    LUSH_POOL_XLARGE =
        3, /**< 16KB - tab completions, large buffers, complex outputs */
    LUSH_POOL_COUNT = 4
} lush_pool_size_t;

// Pool block structure for memory management
typedef struct lush_pool_block {
    void *memory;                 /**< Allocated memory block */
    size_t size;                  /**< Block size */
    bool in_use;                  /**< Allocation status */
    uint64_t allocation_time_us;  /**< Allocation timestamp for debugging */
    uint32_t allocation_id;       /**< Unique allocation ID for tracking */
    struct lush_pool_block *next; /**< Next block in free list */
    struct lush_pool_block *prev; /**< Previous block for efficient removal */
} lush_pool_block_t;

// Memory pool statistics for performance monitoring
typedef struct {
    uint64_t total_allocations;      /**< Total allocation requests */
    uint64_t pool_hits;              /**< Successful pool allocations */
    uint64_t pool_misses;            /**< Fallback to malloc count */
    uint64_t current_pool_usage;     /**< Current bytes allocated from pools */
    uint64_t peak_pool_usage;        /**< Maximum pool usage recorded */
    uint64_t malloc_fallbacks;       /**< Count of malloc fallback calls */
    uint64_t total_bytes_allocated;  /**< Total bytes allocated (pool + malloc)
                                      */
    double pool_hit_rate;            /**< Pool allocation success rate */
    uint64_t avg_allocation_time_ns; /**< Average allocation time */
    uint32_t active_allocations;     /**< Current active allocation count */
} lush_pool_stats_t;

// Individual pool configuration and state
typedef struct {
    size_t block_size;             /**< Size of each block in this pool */
    size_t initial_blocks;         /**< Initial number of blocks to pre-allocate
                                    */
    size_t max_blocks;             /**< Maximum blocks allowed (0 = unlimited)
                                    */
    size_t current_blocks;         /**< Current number of allocated blocks */
    size_t free_blocks;            /**< Number of available blocks */
    lush_pool_block_t *free_list;  /**< Linked list of free blocks */
    lush_pool_block_t *all_blocks; /**< Array of all blocks for cleanup */
    uint64_t pool_allocations;     /**< Allocations from this specific pool */
    uint64_t pool_deallocations;   /**< Deallocations to this specific pool */
} lush_pool_t;

// Main memory pool system
typedef struct {
    lush_pool_t pools[LUSH_POOL_COUNT]; /**< Individual size pools */
    lush_pool_stats_t stats;            /**< Performance statistics */
    bool initialized;                   /**< Initialization status */
    bool enable_statistics;             /**< Statistics collection toggle */
    bool enable_malloc_fallback;        /**< Automatic malloc fallback */
    uint32_t next_allocation_id;        /**< Unique ID counter */
    struct timespec init_time;          /**< Pool system initialization time */
} lush_memory_pool_system_t;

// Error codes for memory pool operations
typedef enum {
    LUSH_POOL_SUCCESS = 0,
    LUSH_POOL_ERROR_NOT_INITIALIZED,
    LUSH_POOL_ERROR_INVALID_SIZE,
    LUSH_POOL_ERROR_POOL_EXHAUSTED,
    LUSH_POOL_ERROR_INVALID_POINTER,
    LUSH_POOL_ERROR_MALLOC_FAILED,
    LUSH_POOL_ERROR_BLOCK_NOT_FOUND,
    LUSH_POOL_ERROR_DOUBLE_FREE,
    LUSH_POOL_ERROR_INITIALIZATION_FAILED
} lush_pool_error_t;

// Memory pool configuration structure
typedef struct {
    size_t small_pool_blocks;    /**< Number of 128B blocks (default: 64) */
    size_t medium_pool_blocks;   /**< Number of 512B blocks (default: 32) */
    size_t large_pool_blocks;    /**< Number of 4KB blocks (default: 16) */
    size_t xlarge_pool_blocks;   /**< Number of 16KB blocks (default: 8) */
    bool enable_statistics;      /**< Enable detailed statistics collection */
    bool enable_malloc_fallback; /**< Enable automatic malloc fallback */
    bool enable_debugging;       /**< Enable debug tracking and validation */
} lush_pool_config_t;

// Global memory pool system instance
extern lush_memory_pool_system_t *global_memory_pool;

/**
 * Core Memory Pool API
 */

/**
 * @brief Initialize the memory pool system with configuration
 * @param config Pointer to configuration structure, or NULL for defaults
 * @return LUSH_POOL_SUCCESS on success, error code on failure
 */
lush_pool_error_t lush_pool_init(const lush_pool_config_t *config);

/**
 * @brief Shutdown and cleanup the memory pool system
 */
void lush_pool_shutdown(void);

/**
 * @brief Allocate memory from appropriate pool or fallback to malloc
 * @param size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL on failure
 */
void *lush_pool_alloc(size_t size);

/**
 * @brief Free memory back to pool or call free() for malloc allocations
 * @param ptr Pointer to memory to free (NULL is safely ignored)
 */
void lush_pool_free(void *ptr);

/**
 * @brief Reallocate memory (may use malloc for complex resizing)
 * @param ptr Pointer to existing memory allocation (NULL allocates new memory)
 * @param new_size New size in bytes (0 frees the memory)
 * @return Pointer to reallocated memory, or NULL on failure
 */
void *lush_pool_realloc(void *ptr, size_t new_size);

/**
 * @brief Duplicate string using pool allocation
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char *lush_pool_strdup(const char *str);

/**
 * @brief Allocate and zero-initialize memory
 * @param count Number of elements to allocate
 * @param size Size of each element in bytes
 * @return Pointer to zero-initialized memory, or NULL on failure
 */
void *lush_pool_calloc(size_t count, size_t size);

/**
 * Pool Management and Information API
 */

/**
 * @brief Get current pool statistics
 * @return Copy of the current pool statistics structure
 */
lush_pool_stats_t lush_pool_get_stats(void);

/**
 * @brief Reset statistics counters
 */
void lush_pool_reset_stats(void);

/**
 * @brief Analyze malloc fallback patterns for optimization
 */
void lush_pool_analyze_fallback_patterns(void);

/**
 * @brief Get pool system status and health information
 * @return true if all pools have available capacity, false otherwise
 */
bool lush_pool_is_healthy(void);

/**
 * @brief Perform pool maintenance (defragmentation, cleanup)
 */
void lush_pool_maintenance(void);

/**
 * @brief Get recommended pool size for given allocation size
 * @param size The allocation size to find a pool for
 * @return Pool type that can accommodate the size
 */
lush_pool_size_t lush_pool_get_recommended_size(size_t size);

/**
 * @brief Check if pointer was allocated from pool system
 * @param ptr Pointer to check
 * @return true if pointer is from a pool, false otherwise
 */
bool lush_pool_is_pool_pointer(const void *ptr);

/**
 * Advanced Pool Operations
 */

/**
 * @brief Pre-allocate blocks in specific pool for performance
 * @param pool_type The pool type to pre-allocate blocks in
 * @param count Number of blocks to pre-allocate
 * @return LUSH_POOL_SUCCESS on success, error code on failure
 */
lush_pool_error_t lush_pool_preallocate(lush_pool_size_t pool_type,
                                        size_t count);

/**
 * @brief Get detailed information about specific pool
 * @param pool_type The pool type to query
 * @param block_size Pointer to store block size (may be NULL)
 * @param free_blocks Pointer to store free block count (may be NULL)
 * @param total_blocks Pointer to store total block count (may be NULL)
 */
void lush_pool_get_pool_info(lush_pool_size_t pool_type, size_t *block_size,
                             size_t *free_blocks, size_t *total_blocks);

/**
 * @brief Validate pool integrity (debug/testing)
 * @return true if pool integrity is valid, false otherwise
 */
bool lush_pool_validate_integrity(void);

/**
 * @brief Generate detailed pool status report for debugging
 */
void lush_pool_print_status_report(void);

/**
 * Integration with Performance Monitoring System
 */

/**
 * @brief Get pool memory usage for display performance reporting
 * @param pool_bytes Pointer to store current pool usage in bytes (may be NULL)
 * @param malloc_bytes Pointer to store malloc fallback usage in bytes (may be
 * NULL)
 * @param pool_efficiency Pointer to store pool hit rate percentage (may be
 * NULL)
 */
void lush_pool_get_memory_usage(uint64_t *pool_bytes, uint64_t *malloc_bytes,
                                double *pool_efficiency);

/**
 * @brief Check if pool system is meeting performance targets
 * @return true if performance targets are met, false otherwise
 */
bool lush_pool_meets_performance_targets(void);

/**
 * Configuration Helpers
 */

/**
 * @brief Get default pool configuration for typical usage
 * @return Default configuration structure
 */
lush_pool_config_t lush_pool_get_default_config(void);

/**
 * @brief Create optimized configuration for display-heavy workloads
 * @return Configuration structure optimized for display operations
 */
lush_pool_config_t lush_pool_get_display_optimized_config(void);

/**
 * @brief Create configuration for memory-constrained environments
 * @return Configuration structure with minimal memory footprint
 */
lush_pool_config_t lush_pool_get_minimal_config(void);

/**
 * Error Handling and Debugging
 */

/**
 * @brief Convert error code to human-readable string
 * @param error The error code to convert
 * @return Human-readable error description string
 */
const char *lush_pool_error_string(lush_pool_error_t error);

/**
 * @brief Enable/disable debug mode with detailed logging
 * @param enabled true to enable debug mode, false to disable
 */
void lush_pool_set_debug_mode(bool enabled);

/**
 * @brief Get last error that occurred in pool operations
 * @return Last error code from pool operations
 */
lush_pool_error_t lush_pool_get_last_error(void);

/**
 * Thread Safety Note:
 *
 * The memory pool system is designed to be thread-safe for future expansion.
 * All public API functions use appropriate synchronization mechanisms to
 * ensure safe concurrent access from multiple threads.
 */

/**
 * Usage Examples:
 *
 * // Initialize with default configuration
 * lush_pool_config_t config = lush_pool_get_default_config();
 * lush_pool_init(&config);
 *
 * // Allocate memory (automatically selects appropriate pool)
 * char *buffer = lush_pool_alloc(256);
 *
 * // Use the buffer...
 *
 * // Free memory (automatically returns to correct pool)
 * lush_pool_free(buffer);
 *
 * // Get statistics
 * lush_pool_stats_t stats = lush_pool_get_stats();
 * printf("Pool hit rate: %.2f%%\n", stats.pool_hit_rate);
 *
 * // Shutdown when done
 * lush_pool_shutdown();
 */

#ifdef __cplusplus
}
#endif

#endif // LUSH_MEMORY_POOL_H
