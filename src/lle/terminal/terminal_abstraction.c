/**
 * @file terminal_abstraction.c
 * @brief Main Terminal Abstraction (Spec 02)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Main initialization and destruction for LLE Terminal Abstraction system.
 * Coordinates all 8 subsystems:
 *
 * 1. Terminal Capabilities (terminal_capabilities.c)
 * 2. Internal State Authority Model (terminal_internal_state.c)
 * 3. Display Content Generation (terminal_display_generator.c)
 * 4. Lush Display Integration (terminal_lush_client.c)
 * 5. Input Event Processing (terminal_input_processor.c)
 * 6. Unix Terminal Interface (terminal_unix_interface.c)
 * 7. Error Handling (terminal_error_handler.c)
 * 8. Performance Monitoring (terminal_perf_monitor.c)
 *
 * Spec 02: Terminal Abstraction Complete Specification
 */

#include "lle/terminal_abstraction.h"
#include "lle/memory_management.h"
#include <stdlib.h>

/* ============================================================================
 * MAIN TERMINAL ABSTRACTION LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Initialize terminal abstraction system
 *
 * This is the main entry point for LLE terminal abstraction. It initializes
 * all 8 subsystems in the correct order with proper error handling.
 *
 * @param abstraction Output pointer for created abstraction
 * @param lush_display Lush display context
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_terminal_abstraction_init(lle_terminal_abstraction_t **abstraction,
                              lush_display_context_t *lush_display) {
    if (!abstraction) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_terminal_abstraction_t *abs = NULL;
    lle_result_t result = LLE_SUCCESS;

    /* Step 1: Allocate main structure */
    abs = calloc(1, sizeof(lle_terminal_abstraction_t));
    if (!abs) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Step 2: Initialize Unix interface for basic terminal access */
    result = lle_unix_interface_init(&abs->unix_interface);
    if (result != LLE_SUCCESS) {
        free(abs);
        return result;
    }

    /* Step 3: One-time capability detection (NO terminal queries) */
    result = lle_capabilities_detect_environment(&abs->capabilities,
                                                 abs->unix_interface);
    if (result != LLE_SUCCESS) {
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 3.5: Initialize sequence parser now that capabilities are available
     */
    result = lle_unix_interface_init_sequence_parser(
        abs->unix_interface, abs->capabilities,
        (lle_memory_pool_t *)global_memory_pool);
    if (result != LLE_SUCCESS) {
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 4: Initialize internal state authority model */
    result = lle_internal_state_init(&abs->internal_state, abs->capabilities);
    if (result != LLE_SUCCESS) {
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 5: Initialize display content generation system */
    result = lle_display_generator_init(&abs->display_generator,
                                        abs->capabilities, abs->internal_state);
    if (result != LLE_SUCCESS) {
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 6: Initialize as Lush display layer client */
    result = lle_lush_display_client_init(&abs->display_client, lush_display,
                                          abs->capabilities);
    if (result != LLE_SUCCESS) {
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 7: Initialize input processing system */
    result = lle_input_processor_init(&abs->input_processor, abs->capabilities,
                                      abs->unix_interface);
    if (result != LLE_SUCCESS) {
        lle_lush_display_client_destroy(abs->display_client);
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }

    /* Step 8: Initialize error handling and performance monitoring */
    /* NOTE: When Spec 16 Phase 2 and Spec 14 Phase 1 are implemented,
     * we will call initialization functions here:
     * - lle_error_context_init(&abs->error_ctx)
     * - lle_performance_monitor_init(&abs->perf_monitor)
     *
     * For now, set to NULL to indicate not yet initialized.
     */
    abs->error_ctx = NULL;
    abs->perf_monitor = NULL;

    *abstraction = abs;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy terminal abstraction system
 *
 * Cleans up all subsystems in reverse order of initialization.
 *
 * @param abstraction Abstraction to destroy
 */
void lle_terminal_abstraction_destroy(lle_terminal_abstraction_t *abstraction) {
    if (!abstraction) {
        return;
    }

    /* Destroy in reverse order of initialization */

    /* Step 8: Destroy performance monitoring and error handling */
    /* NOTE: When Spec 16 Phase 2 and Spec 14 Phase 1 are implemented,
     * we will call destroy functions here. For now, they are NULL.
     */
    abstraction->perf_monitor = NULL;
    abstraction->error_ctx = NULL;

    /* Step 7: Destroy input processing system */
    if (abstraction->input_processor) {
        lle_input_processor_destroy(abstraction->input_processor);
    }

    /* Step 6: Destroy Lush display client */
    if (abstraction->display_client) {
        lle_lush_display_client_destroy(abstraction->display_client);
    }

    /* Step 5: Destroy display generator */
    if (abstraction->display_generator) {
        lle_display_generator_destroy(abstraction->display_generator);
    }

    /* Step 4: Destroy internal state */
    if (abstraction->internal_state) {
        lle_internal_state_destroy(abstraction->internal_state);
    }

    /* Step 3: Destroy capabilities */
    if (abstraction->capabilities) {
        lle_capabilities_destroy(abstraction->capabilities);
    }

    /* Step 2: Destroy Unix interface (ensures terminal restored) */
    if (abstraction->unix_interface) {
        lle_unix_interface_destroy(abstraction->unix_interface);
    }

    /* Step 1: Free main structure */
    free(abstraction);
}
