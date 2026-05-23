/**
 * @file history_buffer_integration.c
 * @brief History-Buffer Integration Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Phase 1-3: Complete Implementation
 * - Phase 1: Core Infrastructure (lifecycle, config, callbacks)
 * - Phase 2: Multiline Reconstruction Engine
 * - Phase 3: Interactive Editing System
 *
 * Critical Gap Spec: 22_history_buffer_integration_complete.md
 */

#include "lle/history_buffer_integration.h"
#include "lle/edit_cache.h"
#include "lle/edit_session_manager.h"
#include "lle/error_handling.h"
#include "lle/formatting_engine.h"
#include "lle/history_buffer_bridge.h"
#include "lle/memory_management.h"
#include "lle/multiline_parser.h"
#include "lle/reconstruction_engine.h"
#include "lle/structure_analyzer.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================
 */

// Phase 2, 3, and 4 components are now real implementations

struct lle_callback_registry {
    lle_history_edit_callbacks_t callbacks;
    bool has_callbacks;
};

/* ============================================================================
 * DEFAULT CONFIGURATION
 * ============================================================================
 */

static const lle_integration_config_t DEFAULT_CONFIG = {
    .enable_multiline_reconstruction = true,
    .preserve_original_formatting = true,
    .enable_structure_analysis = true,
    .enable_edit_caching = true,
    .max_cache_entries = 100,
    .max_reconstruction_depth = 10,
    .reconstruction_timeout_ms = 1000};

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize stub component (placeholder for Phase 2+)
 *
 * Allocates and zeroes memory for a component using the global pool.
 *
 * @param component Output pointer for allocated component (must not be NULL)
 * @param pool Memory pool (currently unused, uses global pool)
 * @param size Size of component to allocate in bytes
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if component is
 * NULL, LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
static lle_result_t init_stub_component(void **component,
                                        lle_memory_pool_t *pool, size_t size) {
    (void)pool; // Unused - uses lle_pool_alloc global pool

    if (!component) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    void *comp = lle_pool_alloc(size);
    if (!comp) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(comp, 0, size);
    *component = comp;

    return LLE_SUCCESS;
}

/* ============================================================================
 * SYSTEM LIFECYCLE FUNCTIONS
 * ============================================================================
 */

lle_result_t lle_history_buffer_integration_create(
    lle_history_buffer_integration_t **integration,
    lle_history_core_t *history_core, lle_memory_pool_t *memory_pool,
    lle_event_system_t *event_system) {
    lle_result_t result = LLE_SUCCESS;
    lle_history_buffer_integration_t *integ = NULL;

    // Validate parameters
    if (!integration || !history_core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // memory_pool can be NULL - will use global pool via lle_pool_alloc

    // Allocate integration system
    integ = lle_pool_alloc(sizeof(lle_history_buffer_integration_t));
    if (!integ) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(integ, 0, sizeof(lle_history_buffer_integration_t));

    // Set core references
    integ->history_core = history_core;
    integ->memory_pool = memory_pool;
    integ->event_system = event_system;

    // Allocate configuration
    integ->config = lle_pool_alloc(sizeof(lle_integration_config_t));
    if (!integ->config) {
        lle_pool_free(integ);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memcpy(integ->config, &DEFAULT_CONFIG, sizeof(lle_integration_config_t));

    // Allocate state
    integ->current_state = lle_pool_alloc(sizeof(lle_integration_state_t));
    if (!integ->current_state) {
        lle_pool_free(integ->config);
        lle_pool_free(integ);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memset(integ->current_state, 0, sizeof(lle_integration_state_t));
    integ->current_state->state = LLE_INTEGRATION_UNINITIALIZED;

    // Initialize callback registry
    result =
        init_stub_component((void **)&integ->callback_registry, memory_pool,
                            sizeof(struct lle_callback_registry));
    if (result != LLE_SUCCESS) {
        lle_pool_free(integ->current_state);
        lle_pool_free(integ->config);
        lle_pool_free(integ);
        return result;
    }

    // Initialize Phase 2 components - Multiline Reconstruction Engine
    result = lle_structure_analyzer_create(&integ->structure_analyzer,
                                           memory_pool, NULL);
    if (result != LLE_SUCCESS)
        goto cleanup;

    result = lle_multiline_parser_create(&integ->multiline_parser, memory_pool,
                                         integ->structure_analyzer, NULL);
    if (result != LLE_SUCCESS)
        goto cleanup;

    result = lle_reconstruction_engine_create(
        &integ->reconstruction, memory_pool, integ->structure_analyzer,
        integ->multiline_parser, NULL);
    if (result != LLE_SUCCESS)
        goto cleanup;

    result = lle_formatting_engine_create(&integ->formatter, memory_pool,
                                          integ->structure_analyzer, NULL);
    if (result != LLE_SUCCESS)
        goto cleanup;

    // Initialize Phase 3 components - Interactive Editing System
    result = lle_edit_session_manager_create(&integ->session_manager,
                                             memory_pool, history_core, NULL);
    if (result != LLE_SUCCESS)
        goto cleanup;

    // Initialize Phase 4 components - Performance Optimization
    lle_edit_cache_config_t cache_config;
    cache_config.max_entries = integ->config->max_cache_entries;
    cache_config.entry_ttl_ms = 300000; // 5 minutes
    cache_config.track_access = true;

    result =
        lle_edit_cache_create(&integ->edit_cache, memory_pool, &cache_config);
    if (result != LLE_SUCCESS)
        goto cleanup;

    // Initialize performance monitor if not provided
    if (!integ->perf_monitor) {
        /* Performance monitor should be created by caller, but we'll handle
         * gracefully */
        integ->perf_monitor = NULL;
    }

    // Initialize thread synchronization
    if (pthread_rwlock_init(&integ->integration_lock, NULL) != 0) {
        result = LLE_ERROR_INITIALIZATION_FAILED;
        goto cleanup;
    }

    // Set initial state
    integ->system_active = true;
    integ->session_counter = 0;
    integ->current_state->state = LLE_INTEGRATION_READY;

    *integration = integ;
    return LLE_SUCCESS;

cleanup:
    if (integ) {
        if (integ->edit_cache)
            lle_pool_free(integ->edit_cache);
        if (integ->formatter)
            lle_pool_free(integ->formatter);
        if (integ->structure_analyzer)
            lle_pool_free(integ->structure_analyzer);
        if (integ->multiline_parser)
            lle_pool_free(integ->multiline_parser);
        if (integ->session_manager)
            lle_pool_free(integ->session_manager);
        if (integ->reconstruction)
            lle_pool_free(integ->reconstruction);
        if (integ->callback_registry)
            lle_pool_free(integ->callback_registry);
        if (integ->current_state)
            lle_pool_free(integ->current_state);
        if (integ->config)
            lle_pool_free(integ->config);
        lle_pool_free(integ);
    }
    return result;
}

lle_result_t lle_history_buffer_integration_destroy(
    lle_history_buffer_integration_t *integration) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    // Mark as shutting down
    integration->system_active = false;
    if (integration->current_state) {
        integration->current_state->state = LLE_INTEGRATION_SHUTDOWN;
    }

    // Check for active sessions - should wait or fail?
    if (integration->current_state &&
        integration->current_state->active_sessions > 0) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE; /* Cannot destroy with active sessions
                                         */
    }

    // Free all components
    if (integration->edit_cache)
        lle_pool_free(integration->edit_cache);
    if (integration->formatter)
        lle_pool_free(integration->formatter);
    if (integration->structure_analyzer)
        lle_pool_free(integration->structure_analyzer);
    if (integration->multiline_parser)
        lle_pool_free(integration->multiline_parser);
    if (integration->session_manager)
        lle_pool_free(integration->session_manager);
    if (integration->reconstruction)
        lle_pool_free(integration->reconstruction);
    if (integration->callback_registry)
        lle_pool_free(integration->callback_registry);
    if (integration->current_state)
        lle_pool_free(integration->current_state);
    if (integration->config)
        lle_pool_free(integration->config);

    // Unlock and destroy lock
    pthread_rwlock_unlock(&integration->integration_lock);
    pthread_rwlock_destroy(&integration->integration_lock);

    // Free integration structure itself
    lle_pool_free(integration);

    return LLE_SUCCESS;
}

/* ============================================================================
 * CONFIGURATION FUNCTIONS
 * ============================================================================
 */

lle_result_t lle_history_buffer_integration_set_config(
    lle_history_buffer_integration_t *integration,
    const lle_integration_config_t *config) {
    if (!integration || !config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Validate configuration
    if (config->max_cache_entries == 0 || config->max_cache_entries > 10000) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (config->max_reconstruction_depth == 0 ||
        config->max_reconstruction_depth > 100) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (config->reconstruction_timeout_ms == 0 ||
        config->reconstruction_timeout_ms > 60000) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Apply configuration
    memcpy(integration->config, config, sizeof(lle_integration_config_t));

    // Note: Cache configuration is set at creation time
    // To update cache settings, destroy and recreate the cache

    pthread_rwlock_unlock(&integration->integration_lock);

    return LLE_SUCCESS;
}

lle_result_t lle_history_buffer_integration_get_config(
    lle_history_buffer_integration_t *integration,
    lle_integration_config_t *config) {
    if (!integration || !config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire read lock
    pthread_rwlock_rdlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Copy configuration
    memcpy(config, integration->config, sizeof(lle_integration_config_t));

    pthread_rwlock_unlock(&integration->integration_lock);

    return LLE_SUCCESS;
}

lle_result_t lle_history_buffer_integration_get_state(
    lle_history_buffer_integration_t *integration,
    lle_integration_state_t *state) {
    if (!integration || !state) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire read lock
    pthread_rwlock_rdlock(&integration->integration_lock);

    // Copy state
    memcpy(state, integration->current_state, sizeof(lle_integration_state_t));

    pthread_rwlock_unlock(&integration->integration_lock);

    return LLE_SUCCESS;
}

/* ============================================================================
 * CALLBACK REGISTRATION
 * ============================================================================
 */

lle_result_t lle_history_buffer_integration_register_callbacks(
    lle_history_buffer_integration_t *integration,
    const lle_history_edit_callbacks_t *callbacks) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    if (!integration->callback_registry) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    if (callbacks) {
        // Register callbacks
        memcpy(&integration->callback_registry->callbacks, callbacks,
               sizeof(lle_history_edit_callbacks_t));
        integration->callback_registry->has_callbacks = true;
    } else {
        // Clear callbacks
        memset(&integration->callback_registry->callbacks, 0,
               sizeof(lle_history_edit_callbacks_t));
        integration->callback_registry->has_callbacks = false;
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return LLE_SUCCESS;
}

lle_result_t lle_history_buffer_integration_unregister_callbacks(
    lle_history_buffer_integration_t *integration) {
    return lle_history_buffer_integration_register_callbacks(integration, NULL);
}

/* ============================================================================
 * PHASE 3 - INTERACTIVE EDITING FUNCTIONS
 * ============================================================================
 */

lle_result_t
lle_history_edit_entry(lle_history_buffer_integration_t *integration,
                       size_t entry_index, lle_buffer_t *buffer) {
    if (!integration || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Start edit session
    lle_edit_session_t *session = NULL;
    lle_result_t result = lle_edit_session_manager_start_session(
        integration->session_manager, entry_index, &session);

    if (result != LLE_SUCCESS) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return result;
    }

    // Phase 4: Try cache lookup first if enabled
    lle_edit_cache_entry_t *cached_entry = NULL;
    bool cache_hit = false;

    if (integration->config->enable_edit_caching && integration->edit_cache) {
        result = lle_edit_cache_lookup(integration->edit_cache, entry_index,
                                       &cached_entry);
        if (result == LLE_SUCCESS && cached_entry) {
            cache_hit = true;
            integration->current_state->cache_hits++;
        } else {
            integration->current_state->cache_misses++;
        }
    }

    // Load history entry to buffer (from cache or fresh)
    if (cache_hit) {
        // Use cached reconstructed text - load directly into buffer
        /* Note: For now we still do full reconstruction even on cache hit.
         * Future optimization: Implement buffer population from cached text.
         * This would require buffer API changes or direct buffer manipulation.
         */
        integration->current_state->cache_hits--; // Don't count as hit yet
        integration->current_state->cache_misses++;
        cache_hit = false; // Treat as miss for now
    }

    if (!cache_hit) {
        // Cache miss or caching disabled - do full reconstruction
        lle_history_buffer_bridge_t *bridge = NULL;
        result = lle_history_buffer_bridge_create(
            &bridge, integration->memory_pool, integration->history_core,
            integration->multiline_parser, integration->reconstruction);

        if (result == LLE_SUCCESS) {
            lle_transfer_result_t transfer_result;
            result = lle_history_buffer_bridge_load_to_buffer(
                bridge, entry_index, buffer, NULL, // Use default options
                &transfer_result);

            /* Phase 4: Cache insertion would happen here if we had the
             * reconstructed text available. For now, cache is prepared but
             * insertion requires getting the actual reconstructed text from
             * the buffer or reconstruction engine, which isn't exposed in
             * the current transfer_result structure. This is a known limitation
             * that can be addressed in future iterations. */

            lle_history_buffer_bridge_destroy(bridge);
        }
    }

    // Update integration state
    if (result == LLE_SUCCESS) {
        integration->current_state->active_sessions++;
        integration->current_state->state = LLE_INTEGRATION_BUSY;

        // Invoke callback if registered
        if (integration->callback_registry &&
            integration->callback_registry->has_callbacks &&
            integration->callback_registry->callbacks.on_edit_start) {
            // Get the entry for callback
            lle_history_entry_t *entry = NULL;
            lle_history_get_entry_by_index(integration->history_core,
                                           entry_index, &entry);
            integration->callback_registry->callbacks.on_edit_start(
                entry, integration->callback_registry->callbacks.user_data);
        }
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}

/* ============================================================================
 * PHASE 4 - PERFORMANCE MONITORING FUNCTIONS
 * ============================================================================
 */

lle_result_t lle_history_buffer_integration_get_cache_stats(
    lle_history_buffer_integration_t *integration,
    lle_edit_cache_stats_t *stats) {
    if (!integration || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_rdlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    lle_result_t result = LLE_SUCCESS;

    if (integration->edit_cache) {
        result = lle_edit_cache_get_stats(integration->edit_cache, stats);
    } else {
        // No cache available - return zeros
        memset(stats, 0, sizeof(lle_edit_cache_stats_t));
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}

lle_result_t lle_history_buffer_integration_clear_cache(
    lle_history_buffer_integration_t *integration) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    lle_result_t result = LLE_SUCCESS;

    if (integration->edit_cache) {
        result = lle_edit_cache_clear(integration->edit_cache);
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}

lle_result_t lle_history_buffer_integration_maintain_cache(
    lle_history_buffer_integration_t *integration, size_t *expired_count) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    lle_result_t result = LLE_SUCCESS;
    size_t evicted = 0;

    if (integration->edit_cache) {
        result =
            lle_edit_cache_evict_expired(integration->edit_cache, &evicted);
    }

    if (expired_count) {
        *expired_count = evicted;
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}

lle_result_t
lle_history_session_complete(lle_history_buffer_integration_t *integration,
                             lle_buffer_t *buffer) {
    if (!integration || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Get current session
    lle_edit_session_t *session = NULL;
    lle_result_t result = lle_edit_session_manager_get_current_session(
        integration->session_manager, &session);

    if (result != LLE_SUCCESS || !session) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Save buffer contents back to history
    lle_history_buffer_bridge_t *bridge = NULL;
    result = lle_history_buffer_bridge_create(
        &bridge, integration->memory_pool, integration->history_core,
        integration->multiline_parser, integration->reconstruction);

    if (result == LLE_SUCCESS) {
        lle_transfer_result_t transfer_result;
        result = lle_history_buffer_bridge_save_from_buffer(
            bridge, buffer, NULL, // Use default options
            &transfer_result);

        lle_history_buffer_bridge_destroy(bridge);
    }

    // Complete the session
    if (result == LLE_SUCCESS) {
        result = lle_edit_session_manager_complete_session(
            integration->session_manager, session);

        if (result == LLE_SUCCESS) {
            integration->current_state->active_sessions--;
            integration->current_state->total_edits++;

            // Phase 4: Invalidate cache entry since it was modified
            if (integration->config->enable_edit_caching &&
                integration->edit_cache) {
                lle_edit_cache_invalidate(integration->edit_cache,
                                          session->entry_index);
            }

            if (integration->current_state->active_sessions == 0) {
                integration->current_state->state = LLE_INTEGRATION_READY;
            }

            // Invoke callback if registered
            if (integration->callback_registry &&
                integration->callback_registry->has_callbacks &&
                integration->callback_registry->callbacks.on_edit_complete) {
                // Get the entry for callback
                lle_history_entry_t *entry = NULL;
                lle_history_get_entry_by_index(integration->history_core,
                                               session->entry_index, &entry);
                integration->callback_registry->callbacks.on_edit_complete(
                    entry, integration->callback_registry->callbacks.user_data);
            }
        }
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}

lle_result_t
lle_history_session_cancel(lle_history_buffer_integration_t *integration) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Acquire write lock
    pthread_rwlock_wrlock(&integration->integration_lock);

    if (!integration->system_active) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    // Get current session
    lle_edit_session_t *session = NULL;
    lle_result_t result = lle_edit_session_manager_get_current_session(
        integration->session_manager, &session);

    if (result != LLE_SUCCESS || !session) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return LLE_ERROR_INVALID_STATE;
    }

    size_t entry_index = session->entry_index;

    // Cancel the session (discards changes)
    result = lle_edit_session_manager_cancel_session(
        integration->session_manager, session);

    if (result == LLE_SUCCESS) {
        integration->current_state->active_sessions--;

        if (integration->current_state->active_sessions == 0) {
            integration->current_state->state = LLE_INTEGRATION_READY;
        }

        // Invoke callback if registered
        if (integration->callback_registry &&
            integration->callback_registry->has_callbacks &&
            integration->callback_registry->callbacks.on_edit_cancel) {
            // Get the entry for callback
            lle_history_entry_t *entry = NULL;
            lle_history_get_entry_by_index(integration->history_core,
                                           entry_index, &entry);
            integration->callback_registry->callbacks.on_edit_cancel(
                entry, integration->callback_registry->callbacks.user_data);
        }
    }

    pthread_rwlock_unlock(&integration->integration_lock);

    return result;
}
