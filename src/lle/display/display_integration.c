/**
 * @file display_integration.c
 * @brief LLE Display Integration - Implementation (Layer 1)
 *
 * SPECIFICATION: docs/lle_specification/08_display_integration_complete.md
 * IMPLEMENTATION PLAN: docs/lle_implementation/SPEC_08_IMPLEMENTATION_PLAN.md
 *
 * This file implements the Spec 08 display integration layer that connects
 * LLE's buffer system with Lush's layered display architecture.
 *
 * ZERO-TOLERANCE COMPLIANCE:
 * - Complete implementations only
 * - 100% spec-compliant implementations
 * - Complete error handling for all functions
 */

#include "lle/display_integration.h"
#include <stdlib.h>

/* ========================================================================== */
/*                         GLOBAL STATE                                       */
/* ========================================================================== */

/**
 * @brief Global display integration singleton instance
 *
 * This is initialized once during LLE startup and provides the bridge
 * between LLE and Lush display systems.
 */
static lle_display_integration_t *global_display_integration = NULL;

/* ========================================================================== */
/*                    CORE INTEGRATION FUNCTIONS                              */
/* ========================================================================== */

/**
 * @brief Get the global display integration instance
 *
 * @return Pointer to global display integration, or NULL if not initialized
 */
lle_display_integration_t *lle_display_integration_get_global(void) {
    return global_display_integration;
}

/**
 * @brief Initialize the display integration system
 *
 * Creates and initializes the global display integration instance that connects
 * LLE's editing system with Lush's display layers.
 *
 * @param integration Output pointer for created integration instance
 * @param editor LLE editor instance (opaque)
 * @param lush_display Lush display controller
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_display_integration_init(lle_display_integration_t **integration,
                             void *editor, display_controller_t *lush_display,
                             lle_memory_pool_t *memory_pool) {
    if (!integration || !lush_display) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Allocate main integration structure */
    lle_display_integration_t *integ =
        calloc(1, sizeof(lle_display_integration_t));
    if (!integ) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Store Lush display controller reference */
    integ->lush_display = lush_display;
    integ->memory_pool = memory_pool;
    integ->integration_active = true;
    integ->api_version = 1;
    integ->frame_counter = 0;

    /* Initialize integration lock */
    if (pthread_rwlock_init(&integ->integration_lock, NULL) != 0) {
        free(integ);
        return LLE_ERROR_SYSTEM_CALL;
    }

    /* Initialize display bridge */
    lle_result_t result = lle_display_bridge_init(
        &integ->display_bridge, editor, lush_display, memory_pool);

    if (result != LLE_SUCCESS) {
        pthread_rwlock_destroy(&integ->integration_lock);
        free(integ);
        return result;
    }

    /* Initialize render controller */
    result = lle_render_controller_init(&integ->render_controller,
                                        integ->display_bridge, memory_pool);

    if (result != LLE_SUCCESS) {
        lle_display_bridge_cleanup(integ->display_bridge);
        pthread_rwlock_destroy(&integ->integration_lock);
        free(integ);
        return result;
    }

    /* Allocate display metrics */
    integ->perf_metrics = calloc(1, sizeof(lle_display_metrics_t));
    if (!integ->perf_metrics) {
        lle_render_controller_cleanup(integ->render_controller);
        lle_display_bridge_cleanup(integ->display_bridge);
        pthread_rwlock_destroy(&integ->integration_lock);
        free(integ);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate display config */
    integ->config = calloc(1, sizeof(lle_display_config_t));
    if (!integ->config) {
        free(integ->perf_metrics);
        lle_render_controller_cleanup(integ->render_controller);
        lle_display_bridge_cleanup(integ->display_bridge);
        pthread_rwlock_destroy(&integ->integration_lock);
        free(integ);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Set default config */
    integ->config->enable_syntax_highlighting = true;
    integ->config->enable_caching = false; /* Disabled for now */
    integ->config->enable_performance_monitoring = false;

    /* Allocate display state */
    integ->current_state = calloc(1, sizeof(lle_display_state_t));
    if (!integ->current_state) {
        free(integ->config);
        free(integ->perf_metrics);
        lle_render_controller_cleanup(integ->render_controller);
        lle_display_bridge_cleanup(integ->display_bridge);
        pthread_rwlock_destroy(&integ->integration_lock);
        free(integ);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Set as global singleton */
    global_display_integration = integ;
    *integration = integ;

    return LLE_SUCCESS;
}

/**
 * @brief Cleanup the display integration system
 *
 * @param integration Display integration instance to cleanup
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_display_integration_cleanup(lle_display_integration_t *integration) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Clear global singleton if this is it */
    if (global_display_integration == integration) {
        global_display_integration = NULL;
    }

    /* Cleanup render controller */
    if (integration->render_controller) {
        lle_render_controller_cleanup(integration->render_controller);
    }

    /* Cleanup display bridge */
    if (integration->display_bridge) {
        lle_display_bridge_cleanup(integration->display_bridge);
    }

    /* Free allocated structures */
    free(integration->current_state);
    free(integration->config);
    free(integration->perf_metrics);

    /* Destroy lock */
    pthread_rwlock_destroy(&integration->integration_lock);

    /* Free main structure */
    free(integration);

    return LLE_SUCCESS;
}

/* ========================================================================== */
/*                      DISPLAY BRIDGE FUNCTIONS                              */
/* ========================================================================== */

/* NOTE: Display bridge init, cleanup, and send_output are implemented in
 * display_bridge.c to avoid duplication. */

/* NOTE: Render controller functions (lle_render_controller_init,
 * lle_render_controller_cleanup, lle_render_buffer_content,
 * lle_render_output_free) are implemented in render_controller.c - the
 * spec-compliant implementation with full sub-component support (buffer
 * renderer, cursor renderer, cache, etc.) */
