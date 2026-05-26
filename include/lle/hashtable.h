/**
 * @file hashtable.h
 * @brief LLE libhashtable Integration - Complete Wrapper Layer
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 05 - libhashtable Integration Complete
 * Version: 1.0.0
 *
 * This header provides a complete wrapper layer around libhashtable,
 * integrating it with LLE's memory pool system, performance monitoring,
 * thread safety, and error handling.
 *
 * All three phases of Spec 05 are implemented:
 * - Phase 1: Core Integration (memory pool, factory, monitoring)
 * - Phase 2: Thread Safety and Optimization
 * - Phase 3: Advanced Features (specialized types, analytics)
 */

#ifndef LLE_HASHTABLE_H
#define LLE_HASHTABLE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Include LLE dependencies
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

/// Include libhashtable
#include "ht.h"

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

typedef struct lle_hashtable_system lle_hashtable_system_t;
typedef struct lle_hashtable_config lle_hashtable_config_t;
typedef struct lle_hashtable_factory lle_hashtable_factory_t;
typedef struct lle_hashtable_memory_context lle_hashtable_memory_context_t;
typedef struct lle_hashtable_registry lle_hashtable_registry_t;
typedef struct lle_hashtable_monitor lle_hashtable_monitor_t;
typedef struct lle_strstr_hashtable lle_strstr_hashtable_t;
typedef struct lle_generic_hashtable lle_generic_hashtable_t;
typedef struct lle_concurrent_hashtable lle_concurrent_hashtable_t;
typedef struct lle_hashtable_performance_metrics
    lle_hashtable_performance_metrics_t;
typedef struct lle_monitored_hashtable lle_monitored_hashtable_t;

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Lock types for thread-safe hashtables
 */
typedef enum {
    LLE_LOCK_NONE = 0, ///< No locking
    LLE_LOCK_MUTEX,    ///< Mutex lock
    LLE_LOCK_RWLOCK,   ///< Reader-writer lock
    LLE_LOCK_SPINLOCK  ///< Spinlock (if available)
} lle_lock_type_t;

/* ============================================================================
 * CORE STRUCTURES
 * ============================================================================
 */

/**
 * @brief Hashtable configuration structure
 */
struct lle_hashtable_config {
    /// Memory management
    lush_memory_pool_t *memory_pool; ///< Lush memory pool
    bool use_memory_pool;            ///< Enable memory pool integration

    /// Hash configuration
    ht_hash hash_function; ///< Hash function (default: FNV1A)
    ht_keyeq key_equality; ///< Key comparison function
    uint64_t hash_seed;    ///< Hash seed (security)
    bool random_seed;      ///< Use random seed

    /// Performance tuning
    uint32_t initial_capacity; ///< Initial capacity hint
    double max_load_factor;    ///< Load factor threshold (default: 0.75)
    uint32_t growth_factor;    ///< Growth factor (default: 2)
    uint32_t max_capacity;     ///< Maximum capacity limit

    /// Thread safety
    bool thread_safe;            ///< Enable thread safety
    lle_lock_type_t lock_type;   ///< Lock type (rwlock, mutex)
    bool allow_concurrent_reads; ///< Concurrent read optimization

    /// Monitoring and debugging
    bool performance_monitoring; ///< Enable performance monitoring
    bool debug_mode;             ///< Debug output enabled
    const char *hashtable_name;  ///< Name for monitoring/debugging
};

/**
 * @brief Memory context for hashtable memory pool integration
 */
struct lle_hashtable_memory_context {
    lush_memory_pool_t *pool;   ///< Memory pool reference
    size_t allocations;         ///< Allocation counter
    size_t deallocations;       ///< Deallocation counter
    size_t bytes_allocated;     ///< Total bytes allocated
    size_t bytes_freed;         ///< Total bytes freed
    const char *hashtable_name; ///< Name for debugging
};

/**
 * @brief Performance metrics for hashtable operations
 */
struct lle_hashtable_performance_metrics {
    /// Operation counts
    uint64_t insert_operations;    ///< Insert operation count
    uint64_t lookup_operations;    ///< Lookup operation count
    uint64_t delete_operations;    ///< Delete operation count
    uint64_t iteration_operations; ///< Iteration operation count

    /// Timing statistics (microseconds)
    uint64_t total_insert_time_us; ///< Total insert time
    uint64_t total_lookup_time_us; ///< Total lookup time
    uint64_t total_delete_time_us; ///< Total delete time

    /// Performance characteristics
    uint64_t max_insert_time_us; ///< Max insert time
    uint64_t max_lookup_time_us; ///< Max lookup time
    uint64_t avg_insert_time_us; ///< Average insert time
    uint64_t avg_lookup_time_us; ///< Average lookup time

    /// Hash quality metrics
    uint64_t collisions;        ///< Collision count
    uint64_t rehash_operations; ///< Rehash count
    double load_factor;         ///< Current load factor

    /// Memory usage
    size_t current_capacity;   ///< Current capacity
    size_t used_entries;       ///< Used entry count
    size_t memory_usage_bytes; ///< Total memory usage
};

/**
 * @brief String-to-string hashtable wrapper
 */
struct lle_strstr_hashtable {
    ht_strstr_t *ht;                              ///< Underlying libhashtable
    lle_hashtable_memory_context_t *mem_ctx;      ///< Memory context
    lle_hashtable_performance_metrics_t *metrics; ///< Performance metrics
    lle_hashtable_config_t *config;               ///< Configuration
    pthread_rwlock_t *lock;                       ///< Thread safety lock
    bool is_concurrent;                           ///< Thread-safe flag
    const char *name;                             ///< Hashtable name
    size_t entry_count; /**< Entry count (workaround for libhashtable
                           enumeration bug) */
};

/**
 * @brief Generic hashtable wrapper
 */
struct lle_generic_hashtable {
    ht_t *ht;                                     ///< Underlying libhashtable
    lle_hashtable_memory_context_t *mem_ctx;      ///< Memory context
    lle_hashtable_performance_metrics_t *metrics; ///< Performance metrics
    lle_hashtable_config_t *config;               ///< Configuration
    pthread_rwlock_t *lock;                       ///< Thread safety lock
    bool is_concurrent;                           ///< Thread-safe flag
    const char *name;                             ///< Hashtable name
    size_t entry_count; /**< Entry count (workaround for libhashtable
                           enumeration bug) */
};

/**
 * @brief Hashtable factory for creating configured hashtables
 */
struct lle_hashtable_factory {
    lush_memory_pool_t *memory_pool;        ///< Memory pool reference
    lle_hashtable_config_t *default_config; ///< Default configuration
    lle_hashtable_registry_t *registry;     ///< Registry for tracking
    ht_callbacks_t default_callbacks;       ///< Default memory callbacks
    bool memory_pool_integrated;            ///< Memory pool integration flag
};

/**
 * @brief Registry for tracking all active hashtables
 */
struct lle_hashtable_registry {
    lle_strstr_hashtable_t **hashtables; ///< Array of hashtables
    size_t count;                        ///< Active hashtable count
    size_t capacity;                     ///< Registry capacity
    pthread_mutex_t lock;                ///< Registry lock
};

/**
 * @brief Global hashtable system
 */
struct lle_hashtable_system {
    lush_memory_pool_t *memory_pool;       ///< Memory pool reference
    lle_hashtable_factory_t *factory;      ///< Hashtable factory
    lle_hashtable_monitor_t *monitor;      ///< Performance monitoring
    lle_hashtable_registry_t *registry;    ///< Active hashtable registry
    lle_hashtable_config_t default_config; ///< Default configuration
    bool initialized;                      ///< Initialization flag
};

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================
 */

/**
 * @brief Initialize the hashtable system
 */
lle_result_t lle_hashtable_system_init(lle_hashtable_system_t **system,
                                       lush_memory_pool_t *memory_pool);

/**
 * @brief Destroy the hashtable system
 */
void lle_hashtable_system_destroy(lle_hashtable_system_t *system);

/* ============================================================================
 * FACTORY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize hashtable factory
 */
lle_result_t lle_hashtable_factory_init(lle_hashtable_factory_t **factory,
                                        lush_memory_pool_t *memory_pool);

/**
 * @brief Destroy hashtable factory
 */
void lle_hashtable_factory_destroy(lle_hashtable_factory_t *factory);

/**
 * @brief Create string-to-string hashtable with configuration
 */
lle_result_t
lle_hashtable_factory_create_strstr(lle_hashtable_factory_t *factory,
                                    const lle_hashtable_config_t *config,
                                    lle_strstr_hashtable_t **hashtable);

/**
 * @brief Create generic hashtable with custom callbacks
 */
lle_result_t lle_hashtable_factory_create_generic(
    lle_hashtable_factory_t *factory, const lle_hashtable_config_t *config,
    ht_hash hash_func, ht_keyeq key_eq, const ht_callbacks_t *callbacks,
    lle_generic_hashtable_t **hashtable);

/* ============================================================================
 * CONFIGURATION FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize configuration with defaults
 */
void lle_hashtable_config_init_default(lle_hashtable_config_t *config);

/**
 * @brief Create configuration for memory pool integration
 */
lle_result_t lle_hashtable_config_create_pooled(lle_hashtable_config_t **config,
                                                lush_memory_pool_t *memory_pool,
                                                const char *name);

/* ============================================================================
 * STRING-TO-STRING HASHTABLE OPERATIONS
 * ============================================================================
 */

/**
 * @brief Insert key-value pair
 */
lle_result_t lle_strstr_hashtable_insert(lle_strstr_hashtable_t *ht,
                                         const char *key, const char *value);

/**
 * @brief Lookup value by key
 */
const char *lle_strstr_hashtable_lookup(lle_strstr_hashtable_t *ht,
                                        const char *key);

/**
 * @brief Delete key-value pair
 */
lle_result_t lle_strstr_hashtable_delete(lle_strstr_hashtable_t *ht,
                                         const char *key);

/**
 * @brief Check if key exists
 */
bool lle_strstr_hashtable_contains(lle_strstr_hashtable_t *ht, const char *key);

/**
 * @brief Get hashtable size
 */
size_t lle_strstr_hashtable_size(lle_strstr_hashtable_t *ht);

/**
 * @brief Clear all entries
 */
void lle_strstr_hashtable_clear(lle_strstr_hashtable_t *ht);

/**
 * @brief Destroy hashtable
 */
void lle_strstr_hashtable_destroy(lle_strstr_hashtable_t *ht);

/* ============================================================================
 * PERFORMANCE MONITORING
 * ============================================================================
 */

/**
 * @brief Get performance metrics
 */
lle_result_t
lle_hashtable_get_metrics(lle_strstr_hashtable_t *ht,
                          lle_hashtable_performance_metrics_t *metrics);

/**
 * @brief Reset performance metrics
 */
void lle_hashtable_reset_metrics(lle_strstr_hashtable_t *ht);

/* ============================================================================
 * MEMORY POOL INTEGRATION
 * ============================================================================
 */

/**
 * @brief Integrate memory pool with factory
 */
lle_result_t
lle_hashtable_integrate_memory_pool(lle_hashtable_factory_t *factory,
                                    lush_memory_pool_t *memory_pool);

/**
 * @brief Memory pool callback: key copy
 */
void *lle_hashtable_key_copy_pooled(const void *key);

/**
 * @brief Memory pool callback: key free
 */
void lle_hashtable_key_free_pooled(const void *key);

/**
 * @brief Memory pool callback: value copy
 */
void *lle_hashtable_value_copy_pooled(const void *value);

/**
 * @brief Memory pool callback: value free
 */
void lle_hashtable_value_free_pooled(const void *value);

/* ============================================================================
 * REGISTRY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize hashtable registry
 */
lle_result_t lle_hashtable_registry_init(lle_hashtable_registry_t **registry);

/**
 * @brief Destroy hashtable registry
 */
void lle_hashtable_registry_destroy(lle_hashtable_registry_t *registry);

/**
 * @brief Register hashtable with registry
 */
lle_result_t lle_hashtable_registry_add(lle_hashtable_registry_t *registry,
                                        lle_strstr_hashtable_t *hashtable);

/**
 * @brief Unregister hashtable from registry
 */
lle_result_t lle_hashtable_registry_remove(lle_hashtable_registry_t *registry,
                                           lle_strstr_hashtable_t *hashtable);

#endif /// LLE_HASHTABLE_H
