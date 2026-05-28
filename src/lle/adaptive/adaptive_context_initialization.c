/**
 * @file adaptive_context_initialization.c
 * @brief Adaptive context initialization and lifecycle management
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 26 Phase 2 - Context Initialization
 *
 * Provides unified initialization and lifecycle management for adaptive
 * terminal integration contexts. Handles detection, controller selection,
 * and interface creation.
 *
 * Key Features:
 * - Automatic mode detection and controller selection
 * - Unified interface creation
 * - Complete lifecycle management
 * - Configuration recommendations
 * - Health monitoring and fallback
 */

#include "lle/adaptive_terminal_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations from controller implementations
extern lle_result_t
lle_initialize_native_controller(lle_adaptive_context_t *context,
                                 lush_memory_pool_t *memory_pool);
extern lle_result_t
lle_initialize_display_client_controller(lle_adaptive_context_t *context,
                                         lush_memory_pool_t *memory_pool);
extern lle_result_t
lle_initialize_multiplexer_controller(lle_adaptive_context_t *context,
                                      lush_memory_pool_t *memory_pool);
extern lle_result_t
lle_initialize_minimal_controller(lle_adaptive_context_t *context,
                                  lush_memory_pool_t *memory_pool);

extern void lle_cleanup_native_controller(lle_native_controller_t *native);
extern void
lle_cleanup_display_client_controller(lle_display_client_controller_t *client);
extern void
lle_cleanup_multiplexer_controller(lle_multiplexer_controller_t *mux);
extern void lle_cleanup_minimal_controller(lle_minimal_controller_t *minimal);

extern lle_result_t lle_native_read_line(lle_native_controller_t *native,
                                         const char *prompt, char **line);
extern lle_result_t
lle_display_client_read_line(lle_display_client_controller_t *client,
                             const char *prompt, char **line);
extern lle_result_t lle_multiplexer_read_line(lle_multiplexer_controller_t *mux,
                                              const char *prompt, char **line);
extern lle_result_t lle_minimal_read_line(lle_minimal_controller_t *minimal,
                                          const char *prompt, char **line);

/* ============================================================================
 * INTERNAL - MODE-SPECIFIC INTERFACE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Read line implementation - routes to appropriate controller
 * @param ctx Adaptive context
 * @param prompt Prompt string to display
 * @param line Pointer to store allocated line
 * @return LLE_SUCCESS on success, error code on failure
 */
static lle_result_t
lle_adaptive_interface_read_line(lle_adaptive_context_t *ctx,
                                 const char *prompt, char **line) {

    if (!ctx || !prompt || !line) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    switch (ctx->mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        return lle_native_read_line(ctx->controller.native, prompt, line);

    case LLE_ADAPTIVE_MODE_ENHANCED:
        return lle_display_client_read_line(ctx->controller.display_client,
                                            prompt, line);

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        return lle_multiplexer_read_line(ctx->controller.mux, prompt, line);

    case LLE_ADAPTIVE_MODE_MINIMAL:
        return lle_minimal_read_line(ctx->controller.minimal, prompt, line);

    case LLE_ADAPTIVE_MODE_NONE:
        // Non-interactive mode - no line editing available
        return LLE_ERROR_FEATURE_NOT_AVAILABLE;
    }

    // Should never reach here
    return LLE_ERROR_INVALID_STATE;
}

/**
 * @brief Process input implementation (placeholder)
 * @param ctx Adaptive context
 * @param input Input data to process
 * @param length Length of input data
 * @param events Pointer to store generated events
 * @return LLE_SUCCESS (input handled in read_line)
 */
static lle_result_t
lle_adaptive_interface_process_input(lle_adaptive_context_t *ctx,
                                     const char *input, size_t length,
                                     void **events) {
    LLE_UNUSED(ctx);
    LLE_UNUSED(input);
    LLE_UNUSED(length);
    LLE_UNUSED(events);

    // This would be fully implemented with input event system
    // For now, return success as input processing is handled in read_line
    return LLE_SUCCESS;
}

/**
 * @brief Update display implementation
 * @param ctx Adaptive context
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if ctx is NULL
 */
static lle_result_t
lle_adaptive_interface_update_display(lle_adaptive_context_t *ctx) {

    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Display updates are mode-specific and mostly handled automatically
    return LLE_SUCCESS;
}

/**
 * @brief Handle terminal resize implementation
 * @param ctx Adaptive context
 * @param new_width New terminal width
 * @param new_height New terminal height
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if ctx is NULL
 */
static lle_result_t
lle_adaptive_interface_handle_resize(lle_adaptive_context_t *ctx, int new_width,
                                     int new_height) {
    (void)new_width; // Reserved for dimension-aware resize handling
    (void)new_height;

    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Update dimensions based on mode
    // Native and multiplexer controllers track dimensions
    // Enhanced and minimal modes are less sensitive to resize

    return LLE_SUCCESS;
}

/**
 * @brief Set configuration implementation
 * @param ctx Adaptive context
 * @param config Configuration to apply
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if ctx is NULL
 */
static lle_result_t
lle_adaptive_interface_set_configuration(lle_adaptive_context_t *ctx,
                                         void *config) {
    (void)config; // Reserved for configuration application

    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Configuration changes would be applied here
    return LLE_SUCCESS;
}

/**
 * @brief Get status implementation
 * @param ctx Adaptive context
 * @param status Pointer to status structure to populate
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER on error
 */
static lle_result_t
lle_adaptive_interface_get_status(lle_adaptive_context_t *ctx, void *status) {

    if (!ctx || !status) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Status information would be populated here
    return LLE_SUCCESS;
}

/* ============================================================================
 * INTERNAL - INTERFACE FUNCTION ASSIGNMENT
 * ============================================================================
 */

/**
 * @brief Assign interface function pointers to implementation functions
 * @param interface Interface structure to populate
 */
static void
lle_assign_interface_functions(lle_adaptive_interface_t *interface) {
    interface->read_line = lle_adaptive_interface_read_line;
    interface->process_input = lle_adaptive_interface_process_input;
    interface->update_display = lle_adaptive_interface_update_display;
    interface->handle_resize = lle_adaptive_interface_handle_resize;
    interface->set_configuration = lle_adaptive_interface_set_configuration;
    interface->get_status = lle_adaptive_interface_get_status;
}

/* ============================================================================
 * PUBLIC API - ADAPTIVE CONTEXT INITIALIZATION
 * ============================================================================
 */

/**
 * @brief Initialize adaptive context with detected mode
 * @param context Pointer to store the created context
 * @param detection_result Terminal detection result
 * @param memory_pool Memory pool for allocations (can be NULL)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_initialize_adaptive_context(
    lle_adaptive_context_t **context,
    const lle_terminal_detection_result_t *detection_result,
    lush_memory_pool_t *memory_pool) {

    if (!context || !detection_result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Create context
    lle_adaptive_context_t *ctx = calloc(1, sizeof(lle_adaptive_context_t));
    if (!ctx) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Clone detection result
    ctx->detection_result = malloc(sizeof(lle_terminal_detection_result_t));
    if (!ctx->detection_result) {
        free(ctx);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    memcpy(ctx->detection_result, detection_result,
           sizeof(lle_terminal_detection_result_t));

    ctx->mode = detection_result->recommended_mode;
    ctx->memory_pool = memory_pool;
    ctx->healthy = true;
    ctx->error_count = 0;

    // Initialize mode-specific controller
    lle_result_t result;
    switch (ctx->mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        result = lle_initialize_native_controller(ctx, memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        result = lle_initialize_display_client_controller(ctx, memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        result = lle_initialize_multiplexer_controller(ctx, memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        result = lle_initialize_minimal_controller(ctx, memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_NONE:
        // Non-interactive mode - no controller to initialize
        free(ctx->detection_result);
        free(ctx);
        return LLE_ERROR_FEATURE_NOT_AVAILABLE;
    }

    // Should never reach here - all cases handled
    if (ctx->mode < LLE_ADAPTIVE_MODE_NONE ||
        ctx->mode > LLE_ADAPTIVE_MODE_MULTIPLEXED) {
        free(ctx->detection_result);
        free(ctx);
        return LLE_ERROR_INVALID_STATE;
    }

    if (result != LLE_SUCCESS) {
        free(ctx->detection_result);
        free(ctx);
        return result;
    }

    *context = ctx;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy adaptive context and cleanup resources
 * @param context Context to destroy (safe if NULL)
 */
void lle_adaptive_context_destroy(lle_adaptive_context_t *context) {
    if (!context) {
        return;
    }

    // Cleanup mode-specific controller
    switch (context->mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        lle_cleanup_native_controller(context->controller.native);
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        lle_cleanup_display_client_controller(
            context->controller.display_client);
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        lle_cleanup_multiplexer_controller(context->controller.mux);
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        lle_cleanup_minimal_controller(context->controller.minimal);
        break;

    default:
        break;
    }

    free(context->detection_result);
    free(context);
}

/* ============================================================================
 * PUBLIC API - ADAPTIVE INTERFACE CREATION
 * ============================================================================
 */

/**
 * @brief Create adaptive interface with automatic detection
 * @param interface Pointer to store the created interface
 * @param config Configuration options (can be NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_create_adaptive_interface(lle_adaptive_interface_t **interface,
                                           void *config) {
    (void)config; // Reserved for configuration-driven initialization

    if (!interface) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Perform terminal detection
    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t result =
        lle_detect_terminal_capabilities_optimized(&detection);
    if (result != LLE_SUCCESS) {
        return result;
    }

    // Create adaptive context
    lle_adaptive_context_t *context = NULL;
    result = lle_initialize_adaptive_context(&context, detection, NULL);

    if (result != LLE_SUCCESS) {
        /// Do NOT destroy detection - the optimized version returns a cached
        /// result that is managed internally. Destroying it could cause
        /// double-free if the cache was refreshed.
        return result;
    }

    // Detection result is cached by the detection system, don't free it

    // Create interface wrapper
    lle_adaptive_interface_t *iface =
        calloc(1, sizeof(lle_adaptive_interface_t));
    if (!iface) {
        lle_adaptive_context_destroy(context);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    iface->adaptive_context = context;

    // Assign function pointers
    lle_assign_interface_functions(iface);

    *interface = iface;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy adaptive interface and cleanup resources
 * @param interface Interface to destroy (safe if NULL)
 */
void lle_adaptive_interface_destroy(lle_adaptive_interface_t *interface) {
    if (!interface) {
        return;
    }

    lle_adaptive_context_destroy(interface->adaptive_context);
    free(interface);
}

/* ============================================================================
 * PUBLIC API - SHELL INTEGRATION
 * ============================================================================
 */

/**
 * @brief Determine if shell should be interactive based on context
 * @param forced_interactive True if user forced interactive mode
 * @param has_script_file True if executing a script file
 * @param stdin_mode True if reading commands from stdin
 * @return true if shell should be interactive, false otherwise
 */
bool lle_adaptive_should_shell_be_interactive(bool forced_interactive,
                                              bool has_script_file,
                                              bool stdin_mode) {

    // Script execution is never interactive
    if (has_script_file) {
        return false;
    }

    // Forced interactive always wins
    if (forced_interactive) {
        return true;
    }

    // Stdin mode typically disables interactive features
    if (stdin_mode) {
        return false;
    }

    /// CRITICAL: Check if stdin is a pipe or regular file BEFORE calling
    /// comprehensive detection. Piped input (echo "cmd" | lush) must
    /// NEVER be treated as interactive, even if stdout is a TTY.
    ///
    /// The comprehensive detection is designed for editor terminals
    /// (VS Code, Zed, Cursor) which have non-TTY stdin but should still
    /// be interactive. However, those cases are detected via terminal
    /// signature matching and environment variables, not just TTY status.
    ///
    /// A simple pipe has no such signatures, so we check for pipes first.
    struct stat stdin_stat;
    if (fstat(STDIN_FILENO, &stdin_stat) == 0) {
        // Pipes and FIFOs are never interactive
        if (S_ISFIFO(stdin_stat.st_mode)) {
            return false;
        }
        // Regular files (redirected input) are never interactive
        if (S_ISREG(stdin_stat.st_mode)) {
            return false;
        }
    }

    // Use enhanced detection for final decision
    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t result =
        lle_detect_terminal_capabilities_comprehensive(&detection);
    if (result != LLE_SUCCESS) {
        return false;
    }

    // Interactive if mode is not NONE
    // Also check force_interactive flag from signature
    bool interactive = (detection->recommended_mode != LLE_ADAPTIVE_MODE_NONE);
    if (!interactive && detection->matched_signature) {
        interactive = detection->matched_signature->force_interactive;
    }

    lle_terminal_detection_result_destroy(detection);
    return interactive;
}

/**
 * @brief Get configuration recommendations based on terminal detection
 * @param config Pointer to configuration structure to populate
 */
void lle_adaptive_get_recommended_config(
    lle_adaptive_config_recommendation_t *config) {

    if (!config) {
        return;
    }

    lle_terminal_detection_result_t *detection = NULL;
    lle_result_t result =
        lle_detect_terminal_capabilities_comprehensive(&detection);
    if (result != LLE_SUCCESS) {
        // Conservative defaults for detection failure
        memset(config, 0, sizeof(lle_adaptive_config_recommendation_t));
        config->enable_lle = false;
        config->enable_tab_completion = true;
        config->enable_history = true;
        config->color_support_level = 0;
        config->recommended_mode = LLE_ADAPTIVE_MODE_NONE;
        return;
    }

    // Configure based on detected mode and capabilities
    config->enable_lle =
        (detection->recommended_mode != LLE_ADAPTIVE_MODE_NONE);
    config->recommended_mode = detection->recommended_mode;

    switch (detection->recommended_mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
    case LLE_ADAPTIVE_MODE_ENHANCED:
        config->enable_syntax_highlighting = detection->supports_colors;
        config->enable_autosuggestions = true;
        config->enable_tab_completion = true;
        config->enable_history = true;
        config->enable_multiline_editing = true;
        config->enable_undo_redo = true;
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        config->enable_syntax_highlighting = detection->supports_colors;
        config->enable_autosuggestions = true;
        config->enable_tab_completion = true;
        config->enable_history = true;
        config->enable_multiline_editing =
            detection->supports_cursor_positioning;
        config->enable_undo_redo = detection->supports_cursor_positioning;
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        config->enable_syntax_highlighting = false;
        config->enable_autosuggestions = false;
        config->enable_tab_completion = true;
        config->enable_history = true;
        config->enable_multiline_editing = false;
        config->enable_undo_redo = false;
        break;

    case LLE_ADAPTIVE_MODE_NONE:
    default:
        config->enable_lle = false;
        config->enable_syntax_highlighting = false;
        config->enable_autosuggestions = false;
        config->enable_tab_completion = false;
        config->enable_history = false;
        config->enable_multiline_editing = false;
        config->enable_undo_redo = false;
        break;
    }

    // Set color support level
    if (detection->supports_truecolor) {
        config->color_support_level = 3;
    } else if (detection->supports_256_colors) {
        config->color_support_level = 2;
    } else if (detection->supports_colors) {
        config->color_support_level = 1;
    } else {
        config->color_support_level = 0;
    }

    lle_terminal_detection_result_destroy(detection);
}

/* ============================================================================
 * PUBLIC API - HEALTH MONITORING
 * ============================================================================
 */

/**
 * @brief Perform health check on adaptive context
 * @param context The context to check
 * @return true if context is healthy, false otherwise
 */
bool lle_adaptive_perform_health_check(lle_adaptive_context_t *context) {
    if (!context) {
        return false;
    }

    // Basic health check - more comprehensive checks would be added
    if (context->error_count > 100) {
        context->healthy = false;
        return false;
    }

    context->healthy = true;
    return true;
}

/**
 * @brief Try fallback mode if current mode fails
 * @param context The context to fallback
 * @return LLE_SUCCESS on successful fallback, error code on failure
 *
 * Implements graceful degradation hierarchy:
 * - NATIVE -> ENHANCED -> MINIMAL
 * - ENHANCED -> MINIMAL
 * - MULTIPLEXED -> NATIVE -> ENHANCED -> MINIMAL
 * - MINIMAL -> (no fallback available)
 */
lle_result_t lle_adaptive_try_fallback_mode(lle_adaptive_context_t *context) {
    if (!context) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Determine fallback mode based on current mode
    lle_adaptive_mode_t fallback_mode =
        LLE_ADAPTIVE_MODE_MINIMAL; // Default fallback
    lle_adaptive_mode_t original_mode = context->mode;

    switch (context->mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        fallback_mode = LLE_ADAPTIVE_MODE_ENHANCED;
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        fallback_mode = LLE_ADAPTIVE_MODE_MINIMAL;
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        // Multiplexer failure: try native first, then enhanced
        fallback_mode = LLE_ADAPTIVE_MODE_NATIVE;
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        // Already at minimal - no further fallback
        return LLE_ERROR_FEATURE_NOT_AVAILABLE;

    case LLE_ADAPTIVE_MODE_NONE:
        // Non-interactive - cannot fallback
        return LLE_ERROR_FEATURE_NOT_AVAILABLE;
    }

    // Cleanup current controller before switching
    switch (original_mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        lle_cleanup_native_controller(context->controller.native);
        context->controller.native = NULL;
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        lle_cleanup_display_client_controller(
            context->controller.display_client);
        context->controller.display_client = NULL;
        break;

    case LLE_ADAPTIVE_MODE_MULTIPLEXED:
        lle_cleanup_multiplexer_controller(context->controller.mux);
        context->controller.mux = NULL;
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        lle_cleanup_minimal_controller(context->controller.minimal);
        context->controller.minimal = NULL;
        break;

    default:
        break;
    }

    // Update mode
    context->mode = fallback_mode;

    // Initialize new controller
    lle_result_t result;
    switch (fallback_mode) {
    case LLE_ADAPTIVE_MODE_NATIVE:
        result =
            lle_initialize_native_controller(context, context->memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_ENHANCED:
        result = lle_initialize_display_client_controller(context,
                                                          context->memory_pool);
        break;

    case LLE_ADAPTIVE_MODE_MINIMAL:
        result =
            lle_initialize_minimal_controller(context, context->memory_pool);
        break;

    default:
        return LLE_ERROR_INVALID_STATE;
    }

    if (result != LLE_SUCCESS) {
        // Fallback initialization failed
        // If we're not already at minimal, try that as last resort
        if (fallback_mode != LLE_ADAPTIVE_MODE_MINIMAL) {
            context->mode = LLE_ADAPTIVE_MODE_MINIMAL;
            result = lle_initialize_minimal_controller(context,
                                                       context->memory_pool);
            if (result != LLE_SUCCESS) {
                // Even minimal failed - mark context as unhealthy
                context->healthy = false;
                return result;
            }
        } else {
            // Minimal failed - no options left
            context->healthy = false;
            return result;
        }
    }

    // Reset error count on successful fallback
    context->error_count = 0;
    context->healthy = true;

    return LLE_SUCCESS;
}

/* ============================================================================
 * UTILITY API
 * ============================================================================
 */

/* Note: lle_adaptive_mode_to_string and lle_capability_level_to_string
 * are implemented in adaptive_terminal_detection.c to avoid duplication */
