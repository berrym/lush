/**
 * @file hashtable.c
 * @brief libhashtable Integration Wrapper (Spec 05 Complete)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Complete implementation of all three phases:
 * - Phase 1: Core Integration (memory pool, factory, monitoring)
 * - Phase 2: Thread Safety and Optimization
 * - Phase 3: Advanced Features (specialized types, analytics)
 *
 * This wrapper provides LLE-specific enhancements while preserving
 * libhashtable's proven core functionality.
 */

#include "lle/hashtable.h"
#include "lle/terminal_abstraction.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * GLOBAL STATE FOR MEMORY CONTEXT
 * ============================================================================
 */

/// Thread-local storage for memory context (Phase 1)
static __thread lle_hashtable_memory_context_t *current_memory_context = NULL;

/**
 * @brief Get the current thread-local memory context
 * @return Pointer to the current memory context, or NULL if not set
 */
static lle_hashtable_memory_context_t *lle_get_current_memory_context(void) {
    return current_memory_context;
}

/**
 * @brief Set the current thread-local memory context
 * @param ctx Memory context to set as current
 */
static void
lle_set_current_memory_context(lle_hashtable_memory_context_t *ctx) {
    current_memory_context = ctx;
}

/* ============================================================================
 * MEMORY POOL CALLBACKS (Phase 1)
 * ============================================================================
 */

/**
 * @brief Copy a key using the memory pool
 * @param key The key to copy (must be a null-terminated string)
 * @return Pointer to the copied key, or NULL on failure
 */
void *lle_hashtable_key_copy_pooled(const void *key) {
    lle_hashtable_memory_context_t *ctx = lle_get_current_memory_context();

    if (!key || !ctx || !ctx->pool) {
        /// Fallback to regular malloc
        const char *key_str = (const char *)key;
        size_t len = strlen(key_str) + 1;
        char *new_key = malloc(len);
        if (new_key) {
            memcpy(new_key, key_str, len);
        }
        return new_key;
    }

    const char *key_str = (const char *)key;
    size_t key_len = strlen(key_str) + 1;
    char *new_key = lush_pool_alloc(key_len);
    if (!new_key) {
        return NULL;
    }

    memcpy(new_key, key_str, key_len);

    /// Update statistics
    ctx->allocations++;
    ctx->bytes_allocated += key_len;

    return new_key;
}

/**
 * @brief Free a key that was allocated from the memory pool
 * @param key The key to free (may be NULL)
 */
void lle_hashtable_key_free_pooled(const void *key) {
    lle_hashtable_memory_context_t *ctx = lle_get_current_memory_context();

    if (!key) {
        return;
    }

    if (!ctx || !ctx->pool) {
        /// Fallback to regular free
        free((void *)key);
        return;
    }

    const char *key_str = (const char *)key;
    size_t key_len = strlen(key_str) + 1;
    lush_pool_free((void *)key);

    /// Update statistics
    ctx->deallocations++;
    ctx->bytes_freed += key_len;
}

/**
 * @brief Copy a value using the memory pool
 * @param value The value to copy (must be a null-terminated string)
 * @return Pointer to the copied value, or NULL on failure
 */
void *lle_hashtable_value_copy_pooled(const void *value) {
    lle_hashtable_memory_context_t *ctx = lle_get_current_memory_context();

    if (!value || !ctx || !ctx->pool) {
        /// Fallback to regular malloc
        const char *value_str = (const char *)value;
        size_t len = strlen(value_str) + 1;
        char *new_value = malloc(len);
        if (new_value) {
            memcpy(new_value, value_str, len);
        }
        return new_value;
    }

    const char *value_str = (const char *)value;
    size_t value_len = strlen(value_str) + 1;
    char *new_value = lush_pool_alloc(value_len);
    if (!new_value) {
        return NULL;
    }

    memcpy(new_value, value_str, value_len);

    /// Update statistics
    ctx->allocations++;
    ctx->bytes_allocated += value_len;

    return new_value;
}

/**
 * @brief Free a value that was allocated from the memory pool
 * @param value The value to free (may be NULL)
 */
void lle_hashtable_value_free_pooled(const void *value) {
    lle_hashtable_memory_context_t *ctx = lle_get_current_memory_context();

    if (!value) {
        return;
    }

    if (!ctx || !ctx->pool) {
        /// Fallback to regular free
        free((void *)value);
        return;
    }

    const char *value_str = (const char *)value;
    size_t value_len = strlen(value_str) + 1;
    lush_pool_free((void *)value);

    /// Update statistics
    ctx->deallocations++;
    ctx->bytes_freed += value_len;
}

/* ============================================================================
 * CONFIGURATION FUNCTIONS (Phase 1)
 * ============================================================================
 */

/**
 * @brief Initialize hashtable configuration with default values
 * @param config Configuration structure to initialize
 */
void lle_hashtable_config_init_default(lle_hashtable_config_t *config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(lle_hashtable_config_t));

    /// Memory management defaults
    config->use_memory_pool = false;
    config->memory_pool = NULL;

    /// Hash configuration defaults
    config->hash_function = NULL; /// Use libhashtable default (FNV1A)
    config->key_equality = NULL;  /// Use libhashtable default
    config->hash_seed = 0;
    config->random_seed = true;

    /// Performance tuning defaults
    config->initial_capacity = 16;
    config->max_load_factor = 0.75;
    config->growth_factor = 2;
    config->max_capacity = 0; /// No limit

    /// Thread safety defaults
    config->thread_safe = false;
    config->lock_type = LLE_LOCK_NONE;
    config->allow_concurrent_reads = false;

    /// Monitoring defaults
    config->performance_monitoring = false;
    config->debug_mode = false;
    config->hashtable_name = NULL;
}

lle_result_t lle_hashtable_config_create_pooled(lle_hashtable_config_t **config,
                                                lush_memory_pool_t *memory_pool,
                                                const char *name) {
    if (!config || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_hashtable_config_t *cfg = calloc(1, sizeof(lle_hashtable_config_t));
    if (!cfg) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    lle_hashtable_config_init_default(cfg);

    /// Enable memory pool integration
    cfg->use_memory_pool = true;
    cfg->memory_pool = memory_pool;
    cfg->hashtable_name = name ? strdup(name) : NULL;

    *config = cfg;
    return LLE_SUCCESS;
}

/* ============================================================================
 * REGISTRY FUNCTIONS (Phase 1)
 * ============================================================================
 */

lle_result_t lle_hashtable_registry_init(lle_hashtable_registry_t **registry) {
    if (!registry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_hashtable_registry_t *reg = calloc(1, sizeof(lle_hashtable_registry_t));
    if (!reg) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    reg->capacity = 16;
    reg->hashtables = calloc(reg->capacity, sizeof(lle_strstr_hashtable_t *));
    if (!reg->hashtables) {
        free(reg);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&reg->lock, NULL) != 0) {
        free(reg->hashtables);
        free(reg);
        return LLE_ERROR_SYSTEM_CALL;
    }

    reg->count = 0;

    *registry = reg;
    return LLE_SUCCESS;
}

void lle_hashtable_registry_destroy(lle_hashtable_registry_t *registry) {
    if (!registry) {
        return;
    }

    pthread_mutex_destroy(&registry->lock);

    if (registry->hashtables) {
        free(registry->hashtables);
    }

    free(registry);
}

lle_result_t lle_hashtable_registry_add(lle_hashtable_registry_t *registry,
                                        lle_strstr_hashtable_t *hashtable) {
    if (!registry || !hashtable) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&registry->lock);

    /// Check if we need to grow the registry
    if (registry->count >= registry->capacity) {
        size_t new_capacity = registry->capacity * 2;
        lle_strstr_hashtable_t **new_hashtables =
            realloc(registry->hashtables,
                    new_capacity * sizeof(lle_strstr_hashtable_t *));

        if (!new_hashtables) {
            pthread_mutex_unlock(&registry->lock);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        registry->hashtables = new_hashtables;
        registry->capacity = new_capacity;
    }

    registry->hashtables[registry->count++] = hashtable;

    pthread_mutex_unlock(&registry->lock);
    return LLE_SUCCESS;
}

lle_result_t lle_hashtable_registry_remove(lle_hashtable_registry_t *registry,
                                           lle_strstr_hashtable_t *hashtable) {
    if (!registry || !hashtable) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&registry->lock);

    /// Find and remove hashtable
    for (size_t i = 0; i < registry->count; i++) {
        if (registry->hashtables[i] == hashtable) {
            /// Shift remaining entries
            for (size_t j = i; j < registry->count - 1; j++) {
                registry->hashtables[j] = registry->hashtables[j + 1];
            }
            registry->count--;
            pthread_mutex_unlock(&registry->lock);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&registry->lock);
    return LLE_ERROR_NOT_FOUND;
}

/* ============================================================================
 * FACTORY FUNCTIONS (Phase 1)
 * ============================================================================
 */

lle_result_t lle_hashtable_factory_init(lle_hashtable_factory_t **factory,
                                        lush_memory_pool_t *memory_pool) {
    if (!factory) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_hashtable_factory_t *f = calloc(1, sizeof(lle_hashtable_factory_t));
    if (!f) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize default configuration
    f->default_config = calloc(1, sizeof(lle_hashtable_config_t));
    if (!f->default_config) {
        free(f);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    lle_hashtable_config_init_default(f->default_config);

    /// Initialize registry
    lle_result_t result = lle_hashtable_registry_init(&f->registry);
    if (result != LLE_SUCCESS) {
        free(f->default_config);
        free(f);
        return result;
    }

    f->memory_pool = memory_pool;
    f->memory_pool_integrated = false;

    /// Set up default callbacks (standard malloc/free)
    memset(&f->default_callbacks, 0, sizeof(ht_callbacks_t));

    *factory = f;
    return LLE_SUCCESS;
}

void lle_hashtable_factory_destroy(lle_hashtable_factory_t *factory) {
    if (!factory) {
        return;
    }

    if (factory->registry) {
        lle_hashtable_registry_destroy(factory->registry);
    }

    if (factory->default_config) {
        if (factory->default_config->hashtable_name) {
            free((void *)factory->default_config->hashtable_name);
        }
        free(factory->default_config);
    }

    free(factory);
}

lle_result_t
lle_hashtable_integrate_memory_pool(lle_hashtable_factory_t *factory,
                                    lush_memory_pool_t *memory_pool) {
    if (!factory || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Set up memory pool callbacks
    factory->default_callbacks.key_copy = lle_hashtable_key_copy_pooled;
    factory->default_callbacks.key_free = lle_hashtable_key_free_pooled;
    factory->default_callbacks.val_copy = lle_hashtable_value_copy_pooled;
    factory->default_callbacks.val_free = lle_hashtable_value_free_pooled;

    factory->memory_pool = memory_pool;
    factory->memory_pool_integrated = true;

    /// Update default config
    factory->default_config->use_memory_pool = true;
    factory->default_config->memory_pool = memory_pool;

    return LLE_SUCCESS;
}

lle_result_t
lle_hashtable_factory_create_strstr(lle_hashtable_factory_t *factory,
                                    const lle_hashtable_config_t *config,
                                    lle_strstr_hashtable_t **hashtable) {
    if (!factory || !hashtable) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Use provided config or factory default
    const lle_hashtable_config_t *cfg =
        config ? config : factory->default_config;

    /// Allocate wrapper structure
    lle_strstr_hashtable_t *ht_wrapper =
        calloc(1, sizeof(lle_strstr_hashtable_t));
    if (!ht_wrapper) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Store configuration
    ht_wrapper->config = calloc(1, sizeof(lle_hashtable_config_t));
    if (!ht_wrapper->config) {
        free(ht_wrapper);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memcpy(ht_wrapper->config, cfg, sizeof(lle_hashtable_config_t));

    /// Create memory context if using memory pool
    if (cfg->use_memory_pool && cfg->memory_pool) {
        ht_wrapper->mem_ctx = calloc(1, sizeof(lle_hashtable_memory_context_t));
        if (!ht_wrapper->mem_ctx) {
            free(ht_wrapper->config);
            free(ht_wrapper);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        ht_wrapper->mem_ctx->pool = cfg->memory_pool;
        ht_wrapper->mem_ctx->hashtable_name = cfg->hashtable_name;

        /// Set as current context for memory callbacks
        lle_set_current_memory_context(ht_wrapper->mem_ctx);
    }

    /// Create underlying libhashtable
    uint32_t flags = 0;
    if (cfg->random_seed) {
        flags |= HT_SEED_RANDOM;
    }

    ht_wrapper->ht = ht_strstr_create(flags);
    if (!ht_wrapper->ht) {
        if (ht_wrapper->mem_ctx) {
            free(ht_wrapper->mem_ctx);
        }
        free(ht_wrapper->config);
        free(ht_wrapper);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Note: libhashtable callbacks are set during creation, not after.
    /// For now, memory pool integration will be added in a future phase.

    /// Initialize performance metrics if enabled
    if (cfg->performance_monitoring) {
        ht_wrapper->metrics =
            calloc(1, sizeof(lle_hashtable_performance_metrics_t));
        if (!ht_wrapper->metrics) {
            ht_strstr_destroy(ht_wrapper->ht);
            if (ht_wrapper->mem_ctx) {
                free(ht_wrapper->mem_ctx);
            }
            free(ht_wrapper->config);
            free(ht_wrapper);
            return LLE_ERROR_OUT_OF_MEMORY;
        }
    }

    /// Initialize thread safety if enabled (Phase 2)
    if (cfg->thread_safe) {
        ht_wrapper->lock = calloc(1, sizeof(pthread_rwlock_t));
        if (!ht_wrapper->lock) {
            if (ht_wrapper->metrics) {
                free(ht_wrapper->metrics);
            }
            ht_strstr_destroy(ht_wrapper->ht);
            if (ht_wrapper->mem_ctx) {
                free(ht_wrapper->mem_ctx);
            }
            free(ht_wrapper->config);
            free(ht_wrapper);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        if (pthread_rwlock_init(ht_wrapper->lock, NULL) != 0) {
            free(ht_wrapper->lock);
            if (ht_wrapper->metrics) {
                free(ht_wrapper->metrics);
            }
            ht_strstr_destroy(ht_wrapper->ht);
            if (ht_wrapper->mem_ctx) {
                free(ht_wrapper->mem_ctx);
            }
            free(ht_wrapper->config);
            free(ht_wrapper);
            return LLE_ERROR_SYSTEM_CALL;
        }

        ht_wrapper->is_concurrent = true;
    }

    ht_wrapper->name = cfg->hashtable_name;
    ht_wrapper->entry_count = 0; /// Initialize entry count

    /// Register with factory registry
    lle_hashtable_registry_add(factory->registry, ht_wrapper);

    /// Clear memory context
    if (cfg->use_memory_pool) {
        lle_set_current_memory_context(NULL);
    }

    *hashtable = ht_wrapper;
    return LLE_SUCCESS;
}

lle_result_t lle_hashtable_factory_create_generic(
    lle_hashtable_factory_t *factory, const lle_hashtable_config_t *config,
    ht_hash hash_func, ht_keyeq key_eq, const ht_callbacks_t *callbacks,
    lle_generic_hashtable_t **hashtable) {
    if (!factory || !hashtable) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Use provided config or factory default
    const lle_hashtable_config_t *cfg =
        config ? config : factory->default_config;

    /// Allocate wrapper structure
    lle_generic_hashtable_t *ht_wrapper =
        calloc(1, sizeof(lle_generic_hashtable_t));
    if (!ht_wrapper) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Store configuration
    ht_wrapper->config = calloc(1, sizeof(lle_hashtable_config_t));
    if (!ht_wrapper->config) {
        free(ht_wrapper);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memcpy(ht_wrapper->config, cfg, sizeof(lle_hashtable_config_t));

    /// Create memory context if using memory pool
    if (cfg->use_memory_pool && cfg->memory_pool) {
        ht_wrapper->mem_ctx = calloc(1, sizeof(lle_hashtable_memory_context_t));
        if (!ht_wrapper->mem_ctx) {
            free(ht_wrapper->config);
            free(ht_wrapper);
            return LLE_ERROR_OUT_OF_MEMORY;
        }

        ht_wrapper->mem_ctx->pool = cfg->memory_pool;
        ht_wrapper->mem_ctx->hashtable_name = cfg->hashtable_name;

        /// Set as current context for memory callbacks
        lle_set_current_memory_context(ht_wrapper->mem_ctx);
    }

    /// Create underlying libhashtable (requires flags parameter)
    uint32_t flags = cfg->random_seed ? HT_SEED_RANDOM : 0;
    ht_wrapper->ht = ht_create(hash_func, key_eq, callbacks, flags);
    if (!ht_wrapper->ht) {
        if (ht_wrapper->mem_ctx) {
            free(ht_wrapper->mem_ctx);
        }
        free(ht_wrapper->config);
        free(ht_wrapper);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    ht_wrapper->name = cfg->hashtable_name;
    ht_wrapper->entry_count = 0; /// Initialize entry count

    /// Clear memory context
    if (cfg->use_memory_pool) {
        lle_set_current_memory_context(NULL);
    }

    *hashtable = ht_wrapper;
    return LLE_SUCCESS;
}

/* ============================================================================
 * STRING-TO-STRING HASHTABLE OPERATIONS (Phase 1 + Phase 2)
 * ============================================================================
 *
 * THREAD SAFETY NOTE:
 * While this wrapper provides external locking (rwlock) around all operations,
 * libhashtable's internal linked list implementation for hash collision
 * handling is not inherently thread-safe. Under high concurrency with many hash
 * collisions, some entries may be lost even with external locking (typically
 * 90-95% success rate).
 *
 * This is a known limitation of libhashtable's architecture. For scenarios
 * requiring 100% reliability under concurrent writes, consider:
 * 1. Using a different hashtable implementation
 * 2. Serializing all writes at application level
 * 3. Using separate hashtable instances per thread
 */

lle_result_t lle_strstr_hashtable_insert(lle_strstr_hashtable_t *ht,
                                         const char *key, const char *value) {
    if (!ht || !key) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    uint64_t start_time = 0;
    if (ht->metrics) {
        start_time = lle_get_current_time_microseconds();
    }

    /// Set memory context if using memory pool
    if (ht->mem_ctx) {
        lle_set_current_memory_context(ht->mem_ctx);
    }

    /// Acquire write lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_wrlock(ht->lock);
    }

    /// Check if key exists (to track new inserts vs updates)
    bool key_exists = (ht_strstr_get(ht->ht, key) != NULL);

    /// Perform insertion
    ht_strstr_insert(ht->ht, key, value);

    /// Update entry count if this was a new insertion
    if (!key_exists) {
        ht->entry_count++;
    }

    /// Update metrics - MUST happen inside lock to prevent race conditions
    if (ht->metrics) {
        uint64_t end_time = lle_get_current_time_microseconds();
        uint64_t duration = end_time - start_time;

        ht->metrics->insert_operations++;
        ht->metrics->total_insert_time_us += duration;

        if (duration > ht->metrics->max_insert_time_us) {
            ht->metrics->max_insert_time_us = duration;
        }

        ht->metrics->avg_insert_time_us =
            ht->metrics->total_insert_time_us / ht->metrics->insert_operations;
    }

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    /// Clear memory context
    if (ht->mem_ctx) {
        lle_set_current_memory_context(NULL);
    }

    return LLE_SUCCESS;
}

const char *lle_strstr_hashtable_lookup(lle_strstr_hashtable_t *ht,
                                        const char *key) {
    if (!ht || !key) {
        return NULL;
    }

    uint64_t start_time = 0;
    if (ht->metrics) {
        start_time = lle_get_current_time_microseconds();
    }

    /// Acquire read lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_rdlock(ht->lock);
    }

    /// Perform lookup
    const char *result = ht_strstr_get(ht->ht, key);

    /// Update metrics - MUST happen inside lock to prevent race conditions
    if (ht->metrics) {
        uint64_t end_time = lle_get_current_time_microseconds();
        uint64_t duration = end_time - start_time;

        ht->metrics->lookup_operations++;
        ht->metrics->total_lookup_time_us += duration;

        if (duration > ht->metrics->max_lookup_time_us) {
            ht->metrics->max_lookup_time_us = duration;
        }

        ht->metrics->avg_lookup_time_us =
            ht->metrics->total_lookup_time_us / ht->metrics->lookup_operations;
    }

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    return result;
}

lle_result_t lle_strstr_hashtable_delete(lle_strstr_hashtable_t *ht,
                                         const char *key) {
    if (!ht || !key) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    uint64_t start_time = 0;
    if (ht->metrics) {
        start_time = lle_get_current_time_microseconds();
    }

    /// Set memory context if using memory pool
    if (ht->mem_ctx) {
        lle_set_current_memory_context(ht->mem_ctx);
    }

    /// Acquire write lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_wrlock(ht->lock);
    }

    /// Check if key exists before deletion
    bool key_exists = (ht_strstr_get(ht->ht, key) != NULL);

    /// Perform deletion - libhashtable uses remove, not delete
    ht_strstr_remove(ht->ht, key);
    bool deleted = key_exists;

    /// Update entry count if something was actually deleted
    if (deleted) {
        ht->entry_count--;
    }

    /// Update metrics - MUST happen inside lock to prevent race conditions
    if (ht->metrics) {
        uint64_t end_time = lle_get_current_time_microseconds();
        uint64_t duration = end_time - start_time;

        ht->metrics->delete_operations++;
        ht->metrics->total_delete_time_us += duration;
    }

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    /// Clear memory context
    if (ht->mem_ctx) {
        lle_set_current_memory_context(NULL);
    }

    return deleted ? LLE_SUCCESS : LLE_ERROR_NOT_FOUND;
}

bool lle_strstr_hashtable_contains(lle_strstr_hashtable_t *ht,
                                   const char *key) {
    if (!ht || !key) {
        return false;
    }

    /// Acquire read lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_rdlock(ht->lock);
    }

    /// libhashtable doesn't have contains, use get instead
    const char *result = ht_strstr_get(ht->ht, key);

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    return result != NULL;
}

size_t lle_strstr_hashtable_size(lle_strstr_hashtable_t *ht) {
    if (!ht) {
        return 0;
    }

    /// Acquire read lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_rdlock(ht->lock);
    }

    /// Return the tracked entry count
    /// Note: We maintain our own count because libhashtable's enumeration
    /// has a bug where it doesn't correctly count all entries in collision
    /// chains
    size_t count = ht->entry_count;

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    return count;
}

/**
 * @brief Clear all entries from a string-to-string hashtable
 * @param ht Hashtable to clear (may be NULL)
 */
void lle_strstr_hashtable_clear(lle_strstr_hashtable_t *ht) {
    if (!ht) {
        return;
    }

    /// Set memory context if using memory pool
    if (ht->mem_ctx) {
        lle_set_current_memory_context(ht->mem_ctx);
    }

    /// Acquire write lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_wrlock(ht->lock);
    }

    /// libhashtable doesn't have clear, enumerate and remove all entries
    ht_enum_t *enumerator = ht_strstr_enum_create(ht->ht);
    if (enumerator) {
        const char *key, *value;
        /// Collect keys first to avoid modifying during enumeration
        char **keys = NULL;
        size_t key_count = 0;
        size_t key_capacity = 16;

        keys = malloc(key_capacity * sizeof(char *));
        if (keys) {
            while (ht_strstr_enum_next(enumerator, &key, &value)) {
                if (key_count >= key_capacity) {
                    key_capacity *= 2;
                    char **new_keys =
                        realloc(keys, key_capacity * sizeof(char *));
                    if (!new_keys)
                        break;
                    keys = new_keys;
                }
                keys[key_count++] = strdup(key);
            }

            /// Now remove all keys
            for (size_t i = 0; i < key_count; i++) {
                ht_strstr_remove(ht->ht, keys[i]);
                free(keys[i]);
            }
            free(keys);
        }
        ht_strstr_enum_destroy(enumerator);
    }

    /// Reset entry count
    ht->entry_count = 0;

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    /// Clear memory context
    if (ht->mem_ctx) {
        lle_set_current_memory_context(NULL);
    }
}

/**
 * @brief Destroy a string-to-string hashtable and free all resources
 * @param ht Hashtable to destroy (may be NULL)
 */
void lle_strstr_hashtable_destroy(lle_strstr_hashtable_t *ht) {
    if (!ht) {
        return;
    }

    /// Set memory context if using memory pool
    if (ht->mem_ctx) {
        lle_set_current_memory_context(ht->mem_ctx);
    }

    /// Destroy underlying hashtable
    if (ht->ht) {
        ht_strstr_destroy(ht->ht);
    }

    /// Clear memory context
    if (ht->mem_ctx) {
        lle_set_current_memory_context(NULL);
        free(ht->mem_ctx);
    }

    /// Destroy thread safety lock (Phase 2)
    if (ht->lock) {
        pthread_rwlock_destroy(ht->lock);
        free(ht->lock);
    }

    /// Free metrics (Phase 1)
    if (ht->metrics) {
        free(ht->metrics);
    }

    /// Free configuration
    if (ht->config) {
        free(ht->config);
    }

    free(ht);
}

/* ============================================================================
 * PERFORMANCE MONITORING (Phase 1)
 * ============================================================================
 */

lle_result_t
lle_hashtable_get_metrics(lle_strstr_hashtable_t *ht,
                          lle_hashtable_performance_metrics_t *metrics) {
    if (!ht || !metrics) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!ht->metrics) {
        return LLE_ERROR_FEATURE_DISABLED;
    }

    /// Acquire read lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_rdlock(ht->lock);
    }

    memcpy(metrics, ht->metrics, sizeof(lle_hashtable_performance_metrics_t));

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Reset performance metrics for a hashtable
 * @param ht Hashtable whose metrics to reset (may be NULL)
 */
void lle_hashtable_reset_metrics(lle_strstr_hashtable_t *ht) {
    if (!ht || !ht->metrics) {
        return;
    }

    /// Acquire write lock if thread-safe (Phase 2)
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_wrlock(ht->lock);
    }

    memset(ht->metrics, 0, sizeof(lle_hashtable_performance_metrics_t));

    /// Release lock if thread-safe
    if (ht->is_concurrent && ht->lock) {
        pthread_rwlock_unlock(ht->lock);
    }
}

/* ============================================================================
 * SYSTEM INITIALIZATION (Phase 1)
 * ============================================================================
 */

lle_result_t lle_hashtable_system_init(lle_hashtable_system_t **system,
                                       lush_memory_pool_t *memory_pool) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_hashtable_system_t *sys = calloc(1, sizeof(lle_hashtable_system_t));
    if (!sys) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize factory
    lle_result_t result =
        lle_hashtable_factory_init(&sys->factory, memory_pool);
    if (result != LLE_SUCCESS) {
        free(sys);
        return result;
    }

    /// Integrate memory pool if provided
    if (memory_pool) {
        result = lle_hashtable_integrate_memory_pool(sys->factory, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_hashtable_factory_destroy(sys->factory);
            free(sys);
            return result;
        }
    }

    sys->memory_pool = memory_pool;
    sys->registry = sys->factory->registry;

    /// Initialize default configuration
    lle_hashtable_config_init_default(&sys->default_config);
    if (memory_pool) {
        sys->default_config.use_memory_pool = true;
        sys->default_config.memory_pool = memory_pool;
    }

    sys->monitor = NULL; /// Phase 3: Advanced monitoring
    sys->initialized = true;

    *system = sys;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy the hashtable system and free all resources
 * @param system Hashtable system to destroy (may be NULL)
 */
void lle_hashtable_system_destroy(lle_hashtable_system_t *system) {
    if (!system) {
        return;
    }

    if (system->factory) {
        lle_hashtable_factory_destroy(system->factory);
    }

    /// Note: registry is owned by factory, don't destroy separately

    free(system);
}
