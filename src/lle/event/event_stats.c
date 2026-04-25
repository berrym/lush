/**
 * @file event_stats.c
 * @brief Event System Enhanced Statistics
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Enhanced statistics tracking and processing configuration.
 * Provides per-event-type statistics, cycle timing, and queue depth metrics.
 *
 * Spec 04: Event System - Phase 2B
 */

#include "lle/event_system.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LLE_ENHANCED_STATS_INITIAL_CAPACITY 32

/* ============================================================================
 * Phase 2B: Enhanced Statistics Implementation
 * ============================================================================
 */

/**
 * @brief Initialize the enhanced statistics subsystem
 * @param system The event system to initialize stats for
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Allocates per-type statistics array and initializes cycle tracking.
 * Safe to call multiple times; subsequent calls return success.
 */
lle_result_t lle_event_enhanced_stats_init(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Already initialized */
    if (system->enhanced_stats) {
        return LLE_SUCCESS;
    }

    /* Allocate enhanced stats structure */
    lle_event_enhanced_stats_t *stats =
        lle_pool_alloc(sizeof(lle_event_enhanced_stats_t));
    if (!stats) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(stats, 0, sizeof(lle_event_enhanced_stats_t));

    /* Allocate per-type statistics array */
    stats->type_stats_capacity = LLE_ENHANCED_STATS_INITIAL_CAPACITY;
    stats->type_stats = lle_pool_alloc(sizeof(lle_event_type_stats_t) *
                                       stats->type_stats_capacity);
    if (!stats->type_stats) {
        lle_pool_free(stats);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(stats->type_stats, 0,
           sizeof(lle_event_type_stats_t) * stats->type_stats_capacity);
    stats->type_stats_count = 0;

    /* Initialize cycle stats with sentinel values */
    stats->min_cycle_time = UINT64_MAX;
    stats->max_cycle_time = 0;

    /* Initialize mutex */
    pthread_mutex_init(&stats->stats_mutex, NULL);

    system->enhanced_stats = stats;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy the enhanced statistics subsystem
 * @param system The event system to clean up stats for
 *
 * Frees all statistics structures and resets the pointer to NULL.
 */
void lle_event_enhanced_stats_destroy(lle_event_system_t *system) {
    if (!system || !system->enhanced_stats) {
        return;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    /* Destroy mutex */
    pthread_mutex_destroy(&stats->stats_mutex);

    /* Free type stats array */
    if (stats->type_stats) {
        lle_pool_free(stats->type_stats);
    }

    /* Free stats structure */
    lle_pool_free(stats);

    system->enhanced_stats = NULL;
}

/**
 * @brief Get statistics for a specific event type
 * @param system The event system to query
 * @param type The event type to get statistics for
 * @param stats_out Output structure to receive the statistics
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Returns zeroed statistics if the type has not been tracked yet.
 */
lle_result_t
lle_event_enhanced_stats_get_type(lle_event_system_t *system,
                                  lle_event_kind_t type,
                                  lle_event_type_stats_t *stats_out) {
    if (!system || !stats_out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->enhanced_stats) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    pthread_mutex_lock(&stats->stats_mutex);

    /* Search for type */
    for (size_t i = 0; i < stats->type_stats_count; i++) {
        if (stats->type_stats[i].event_type == type) {
            memcpy(stats_out, &stats->type_stats[i],
                   sizeof(lle_event_type_stats_t));
            pthread_mutex_unlock(&stats->stats_mutex);
            return LLE_SUCCESS;
        }
    }

    pthread_mutex_unlock(&stats->stats_mutex);

    /* Type not found - return zeros */
    memset(stats_out, 0, sizeof(lle_event_type_stats_t));
    stats_out->event_type = type;
    stats_out->min_processing_time = 0; /* Override sentinel */

    return LLE_SUCCESS;
}

/**
 * @brief Get statistics for all tracked event types
 * @param system The event system to query
 * @param stats_out Output pointer to receive the stats array (not copied)
 * @param count_out Output to receive the number of entries
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Returns a pointer to the internal array; do not free or modify.
 */
lle_result_t
lle_event_enhanced_stats_get_all_types(lle_event_system_t *system,
                                       lle_event_type_stats_t **stats_out,
                                       size_t *count_out) {
    if (!system || !stats_out || !count_out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->enhanced_stats) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    pthread_mutex_lock(&stats->stats_mutex);

    *stats_out = stats->type_stats;
    *count_out = stats->type_stats_count;

    pthread_mutex_unlock(&stats->stats_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get event processing cycle statistics
 * @param system The event system to query
 * @param cycles Output: number of processing cycles completed (may be NULL)
 * @param total_time Output: total time spent in cycles in microseconds (may be
 * NULL)
 * @param min_time Output: minimum cycle time in microseconds (may be NULL)
 * @param max_time Output: maximum cycle time in microseconds (may be NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_event_enhanced_stats_get_cycles(lle_event_system_t *system,
                                                 uint64_t *cycles,
                                                 uint64_t *total_time,
                                                 uint64_t *min_time,
                                                 uint64_t *max_time) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->enhanced_stats) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    pthread_mutex_lock(&stats->stats_mutex);

    if (cycles) {
        *cycles = stats->cycles_completed;
    }
    if (total_time) {
        *total_time = stats->total_cycle_time;
    }
    if (min_time) {
        *min_time =
            (stats->min_cycle_time == UINT64_MAX) ? 0 : stats->min_cycle_time;
    }
    if (max_time) {
        *max_time = stats->max_cycle_time;
    }

    pthread_mutex_unlock(&stats->stats_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get maximum observed queue depths
 * @param system The event system to query
 * @param max_main_depth Output: maximum main queue depth seen (may be NULL)
 * @param max_priority_depth Output: maximum priority queue depth seen (may be
 * NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t
lle_event_enhanced_stats_get_queue_depth(lle_event_system_t *system,
                                         uint64_t *max_main_depth,
                                         uint64_t *max_priority_depth) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->enhanced_stats) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    pthread_mutex_lock(&stats->stats_mutex);

    if (max_main_depth) {
        *max_main_depth = stats->max_queue_depth_seen;
    }
    if (max_priority_depth) {
        *max_priority_depth = stats->max_priority_queue_depth_seen;
    }

    pthread_mutex_unlock(&stats->stats_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Reset all enhanced statistics to initial state
 * @param system The event system to reset stats for
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * Clears all cycle stats, per-type stats, and queue depth tracking.
 */
lle_result_t lle_event_enhanced_stats_reset(lle_event_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!system->enhanced_stats) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_event_enhanced_stats_t *stats = system->enhanced_stats;

    pthread_mutex_lock(&stats->stats_mutex);

    /* Reset cycle stats */
    stats->cycles_completed = 0;
    stats->total_cycle_time = 0;
    stats->min_cycle_time = UINT64_MAX;
    stats->max_cycle_time = 0;

    /* Reset type stats */
    memset(stats->type_stats, 0,
           sizeof(lle_event_type_stats_t) * stats->type_stats_capacity);
    stats->type_stats_count = 0;

    /* Reset queue depth */
    stats->max_queue_depth_seen = 0;
    stats->max_priority_queue_depth_seen = 0;

    /* Reset limit tracking */
    stats->cycles_hit_time_limit = 0;
    stats->cycles_hit_event_limit = 0;

    pthread_mutex_unlock(&stats->stats_mutex);

    return LLE_SUCCESS;
}

/* ============================================================================
 * Phase 2B: Processing Configuration Implementation
 * ============================================================================
 */

/**
 * @brief Set the event processing configuration
 * @param system The event system to configure
 * @param config The configuration to apply
 * @return LLE_SUCCESS on success, or error code on failure
 *
 * If detailed stats are enabled and not yet initialized, initializes them.
 */
lle_result_t
lle_event_processing_set_config(lle_event_system_t *system,
                                const lle_event_processing_config_t *config) {
    if (!system || !config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);

    /* If enabling detailed stats and not yet initialized, initialize now */
    if (config->record_detailed_stats && !system->enhanced_stats) {
        lle_result_t result = lle_event_enhanced_stats_init(system);
        if (result != LLE_SUCCESS) {
            pthread_mutex_unlock(&system->system_mutex);
            return result;
        }
    }

    /* Copy configuration */
    system->processing_config = *config;

    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get the current event processing configuration
 * @param system The event system to query
 * @param config Output structure to receive the configuration
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t
lle_event_processing_get_config(lle_event_system_t *system,
                                lle_event_processing_config_t *config) {
    if (!system || !config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);
    *config = system->processing_config;
    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Set the event processing state
 * @param system The event system to update
 * @param state The new processing state
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_event_processing_set_state(lle_event_system_t *system,
                                            lle_processing_state_t state) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&system->system_mutex);
    system->processing_state = state;
    pthread_mutex_unlock(&system->system_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Get the current event processing state
 * @param system The event system to query
 * @return Current processing state, or LLE_PROCESSING_STOPPED if system is NULL
 */
lle_processing_state_t
lle_event_processing_get_state(lle_event_system_t *system) {
    if (!system) {
        return LLE_PROCESSING_STOPPED;
    }

    pthread_mutex_lock(&system->system_mutex);
    lle_processing_state_t state = system->processing_state;
    pthread_mutex_unlock(&system->system_mutex);

    return state;
}
