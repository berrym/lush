/**
 * @file error_handling.c
 * @brief LLE Error Handling System Core Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 16 - Error Handling Complete Specification
 * 1 - Core Error Handling
 * Version: 1.0.0
 *
 * This file contains the core error handling:
 * - Error code conversion and severity classification
 * - The fault router and lifecycle dispatch seam
 * - Atomic error statistics
 * - Thread-local operation context and timing utilities
 *
 * The speculative recovery, degradation, state-machine, circuit-breaker,
 * forensic, and error-injection subsystems were removed: they were never
 * reachable from any live path. The fault router and its shell-side sinks are
 * the live error path.
 */

#include "lle/error_handling.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/// macOS compatibility
#ifdef __APPLE__
#include <pthread.h>
/// macOS doesn't have CLOCK_MONOTONIC_COARSE, use CLOCK_MONOTONIC instead
#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif
#endif

/* ============================================================================
 * GLOBAL STATE AND PRE-ALLOCATED STRUCTURES
 * ============================================================================
 */

/// Pre-allocated error contexts for zero-allocation critical paths
#define LLE_PREALLOCATED_ERROR_CONTEXTS 100
#define LLE_ERROR_STRING_POOL_SIZE 256
#define LLE_ERROR_POOL_BLOCKS 1000

static struct {
    lle_error_context_t contexts[LLE_PREALLOCATED_ERROR_CONTEXTS];
    bool in_use[LLE_PREALLOCATED_ERROR_CONTEXTS];
    pthread_mutex_t allocation_mutex;
    uint32_t allocation_hint;
} g_preallocated_errors = {.allocation_mutex = PTHREAD_MUTEX_INITIALIZER,
                           .allocation_hint = 0};

/// Static fallback error context for emergency situations
static lle_error_context_t g_static_error_context = {0};

/// Global error reporting system
static lle_error_reporting_system_t *g_error_reporting_system = NULL;

/// Initialization guard for the global error reporting system. Paired with
/// g_error_reporting_system so use-before-init and double-init are no-ops.
static bool g_error_system_initialized = false;

/// Global atomic error counters
static lle_error_atomic_counters_t g_error_atomic_counters = {0};

/// Thread-local storage for current operation context
static __thread uint64_t tls_current_operation_id = 0;
static __thread const char *tls_current_operation_name = NULL;
static __thread uint64_t tls_cached_thread_id = 0;
static __thread bool tls_thread_id_cached = false;

/// Simple memory pool for error contexts
static struct {
    void *error_context_pool;
    void *error_string_pool;
    pthread_mutex_t pool_mutex;
    bool initialized;
} g_error_memory_pools = {.pool_mutex = PTHREAD_MUTEX_INITIALIZER,
                          .initialized = false};

/* ============================================================================
 * TIMING AND SYSTEM STATE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get high-resolution timestamp in nanoseconds
 */
uint64_t lle_get_timestamp_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Fast timestamp using CLOCK_MONOTONIC_COARSE for performance
 */
uint64_t lle_get_fast_timestamp_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Get current thread ID
 */
uint64_t lle_get_thread_id(void) {
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
#else
    return (uint64_t)syscall(SYS_gettid);
#endif
}

/**
 * @brief Get cached thread ID for performance
 */
uint64_t lle_get_thread_id_cached(void) {
    if (!tls_thread_id_cached) {
        tls_cached_thread_id = lle_get_thread_id();
        tls_thread_id_cached = true;
    }
    return tls_cached_thread_id;
}

/**
 * @brief Get current operation ID from thread-local storage
 */
uint64_t lle_get_current_operation_id(void) { return tls_current_operation_id; }

/**
 * @brief Get current operation name from thread-local storage
 */
const char *lle_get_current_operation_name(void) {
    return tls_current_operation_name ? tls_current_operation_name : "unknown";
}

/**
 * @brief Get bitmask of currently active components
 */
uint32_t lle_get_active_components_mask(void) {
    /// This would integrate with actual component tracking system
    /// For now, return a placeholder indicating basic components active
    return 0x0001; /// Bit 0: Core system active
}

/**
 * @brief Calculate current system load factor (0-100)
 */
uint32_t lle_calculate_system_load(void) {
    /// System load calculation based on concurrent error activity
    uint32_t concurrent = atomic_load_explicit(
        &g_error_atomic_counters.concurrent_errors, memory_order_relaxed);

    /// Simple heuristic: 10% per concurrent error, capped at 100%
    uint32_t load = concurrent * 10;
    return (load > 100) ? 100 : load;
}

/**
 * @brief Measure current performance impact in nanoseconds
 */
uint64_t lle_measure_current_performance_impact(void) {
    /// Returns estimated performance impact based on error handling activity
    uint32_t concurrent = atomic_load_explicit(
        &g_error_atomic_counters.concurrent_errors, memory_order_relaxed);

    /// Each concurrent error adds ~10μs estimated impact
    return concurrent * 10000ULL;
}

/**
 * @brief Check if critical path is currently active
 */
bool lle_is_critical_path_active(void) {
    /// This would integrate with actual performance monitoring
    /// For now, assume critical path during user input processing
    return tls_current_operation_name != NULL &&
           strstr(tls_current_operation_name, "input") != NULL;
}

/* ============================================================================
 * MEMORY POOL MANAGEMENT FOR ERROR HANDLING
 * ============================================================================
 */

/**
 * @brief Initialize error-specific memory pools
 */
lle_result_t lle_init_error_memory_pools(void) {
    pthread_mutex_lock(&g_error_memory_pools.pool_mutex);

    if (g_error_memory_pools.initialized) {
        pthread_mutex_unlock(&g_error_memory_pools.pool_mutex);
        return LLE_SUCCESS;
    }

    /// Use simple malloc-based allocation for now
    /// A future revision will integrate with the Document 15 memory pools
    g_error_memory_pools.error_context_pool =
        malloc(sizeof(lle_error_context_t) * LLE_ERROR_POOL_BLOCKS);

    if (!g_error_memory_pools.error_context_pool) {
        pthread_mutex_unlock(&g_error_memory_pools.pool_mutex);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    g_error_memory_pools.error_string_pool =
        malloc(LLE_ERROR_STRING_POOL_SIZE * 256);

    if (!g_error_memory_pools.error_string_pool) {
        free(g_error_memory_pools.error_context_pool);
        g_error_memory_pools.error_context_pool = NULL;
        pthread_mutex_unlock(&g_error_memory_pools.pool_mutex);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    g_error_memory_pools.initialized = true;
    pthread_mutex_unlock(&g_error_memory_pools.pool_mutex);

    return LLE_SUCCESS;
}

/**
 * @brief Allocate memory from error pool
 */
void *lle_error_pool_alloc(size_t size) {
    /// Simple allocation for now
    /// A future revision will use proper pool allocation
    return malloc(size);
}

/**
 * @brief Duplicate string in error string pool
 */
char *lle_error_string_pool_strdup(const char *str) {
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}

/* ============================================================================
 * ERROR CODE TO STRING CONVERSION
 * ============================================================================
 */

/**
 * @brief Generate technical details string for error code
 */
const char *lle_generate_technical_details(lle_result_t error_code) {
    /// Error code category and range information
    if (error_code == LLE_SUCCESS) {
        return "Operation completed successfully";
    }

    if (error_code >= LLE_ERROR_INVALID_PARAMETER &&
        error_code < LLE_ERROR_OUT_OF_MEMORY) {
        return "Input validation error - check function parameters and state";
    }

    if (error_code >= LLE_ERROR_OUT_OF_MEMORY &&
        error_code < LLE_ERROR_SYSTEM_CALL) {
        return "Memory management error - check memory usage and pool "
               "availability";
    }

    if (error_code >= LLE_ERROR_SYSTEM_CALL &&
        error_code < LLE_ERROR_BUFFER_COMPONENT) {
        return "System integration error - check system resources and "
               "permissions";
    }

    if (error_code >= LLE_ERROR_BUFFER_COMPONENT &&
        error_code < LLE_ERROR_FEATURE_DISABLED) {
        return "Component-specific error - check component state and "
               "dependencies";
    }

    if (error_code >= LLE_ERROR_FEATURE_DISABLED &&
        error_code < LLE_ERROR_PERFORMANCE_DEGRADED) {
        return "Feature availability error - check feature configuration and "
               "dependencies";
    }

    if (error_code >= LLE_ERROR_PERFORMANCE_DEGRADED &&
        error_code < LLE_ERROR_INITIALIZATION_FAILED) {
        return "Performance/resource error - check system load and resource "
               "limits";
    }

    if (error_code >= LLE_ERROR_INITIALIZATION_FAILED) {
        return "Critical system error - immediate attention required";
    }

    return "Unknown error code";
}

/// @brief Get human-readable error name from error code
const char *lle_error_code_to_string(lle_result_t error_code) {
    switch (error_code) {
    case LLE_SUCCESS:
        return "LLE_SUCCESS";
    case LLE_SUCCESS_WITH_WARNINGS:
        return "LLE_SUCCESS_WITH_WARNINGS";

    /// Input validation errors
    case LLE_ERROR_INVALID_PARAMETER:
        return "LLE_ERROR_INVALID_PARAMETER";
    case LLE_ERROR_NULL_POINTER:
        return "LLE_ERROR_NULL_POINTER";
    case LLE_ERROR_BUFFER_OVERFLOW:
        return "LLE_ERROR_BUFFER_OVERFLOW";
    case LLE_ERROR_BUFFER_UNDERFLOW:
        return "LLE_ERROR_BUFFER_UNDERFLOW";
    case LLE_ERROR_INVALID_STATE:
        return "LLE_ERROR_INVALID_STATE";
    case LLE_ERROR_INVALID_RANGE:
        return "LLE_ERROR_INVALID_RANGE";
    case LLE_ERROR_INVALID_FORMAT:
        return "LLE_ERROR_INVALID_FORMAT";
    case LLE_ERROR_INVALID_ENCODING:
        return "LLE_ERROR_INVALID_ENCODING";

    /// Memory errors
    case LLE_ERROR_OUT_OF_MEMORY:
        return "LLE_ERROR_OUT_OF_MEMORY";
    case LLE_ERROR_MEMORY_CORRUPTION:
        return "LLE_ERROR_MEMORY_CORRUPTION";
    case LLE_ERROR_MEMORY_POOL_EXHAUSTED:
        return "LLE_ERROR_MEMORY_POOL_EXHAUSTED";
    case LLE_ERROR_MEMORY_LEAK_DETECTED:
        return "LLE_ERROR_MEMORY_LEAK_DETECTED";
    case LLE_ERROR_DOUBLE_FREE_DETECTED:
        return "LLE_ERROR_DOUBLE_FREE_DETECTED";
    case LLE_ERROR_USE_AFTER_FREE:
        return "LLE_ERROR_USE_AFTER_FREE";
    case LLE_ERROR_MEMORY_ALIGNMENT:
        return "LLE_ERROR_MEMORY_ALIGNMENT";
    case LLE_ERROR_MEMORY_PROTECTION:
        return "LLE_ERROR_MEMORY_PROTECTION";

    /// System errors
    case LLE_ERROR_SYSTEM_CALL:
        return "LLE_ERROR_SYSTEM_CALL";
    case LLE_ERROR_IO_ERROR:
        return "LLE_ERROR_IO_ERROR";
    case LLE_ERROR_TIMEOUT:
        return "LLE_ERROR_TIMEOUT";
    case LLE_ERROR_INTERRUPT:
        return "LLE_ERROR_INTERRUPT";
    case LLE_ERROR_PERMISSION_DENIED:
        return "LLE_ERROR_PERMISSION_DENIED";
    case LLE_ERROR_RESOURCE_UNAVAILABLE:
        return "LLE_ERROR_RESOURCE_UNAVAILABLE";
    case LLE_ERROR_DEVICE_ERROR:
        return "LLE_ERROR_DEVICE_ERROR";
    case LLE_ERROR_NETWORK_ERROR:
        return "LLE_ERROR_NETWORK_ERROR";

    /// Component errors
    case LLE_ERROR_BUFFER_COMPONENT:
        return "LLE_ERROR_BUFFER_COMPONENT";
    case LLE_ERROR_EVENT_SYSTEM:
        return "LLE_ERROR_EVENT_SYSTEM";
    case LLE_ERROR_TERMINAL_ABSTRACTION:
        return "LLE_ERROR_TERMINAL_ABSTRACTION";
    case LLE_ERROR_INPUT_PARSING:
        return "LLE_ERROR_INPUT_PARSING";
    case LLE_ERROR_HISTORY_SYSTEM:
        return "LLE_ERROR_HISTORY_SYSTEM";
    case LLE_ERROR_AUTOSUGGESTIONS:
        return "LLE_ERROR_AUTOSUGGESTIONS";
    case LLE_ERROR_SYNTAX_HIGHLIGHTING:
        return "LLE_ERROR_SYNTAX_HIGHLIGHTING";
    case LLE_ERROR_COMPLETION_SYSTEM:
        return "LLE_ERROR_COMPLETION_SYSTEM";
    case LLE_ERROR_DISPLAY_INTEGRATION:
        return "LLE_ERROR_DISPLAY_INTEGRATION";
    case LLE_ERROR_PERFORMANCE_MONITORING:
        return "LLE_ERROR_PERFORMANCE_MONITORING";

    /// Feature errors
    case LLE_ERROR_FEATURE_DISABLED:
        return "LLE_ERROR_FEATURE_DISABLED";
    case LLE_ERROR_FEATURE_NOT_AVAILABLE:
        return "LLE_ERROR_FEATURE_NOT_AVAILABLE";
    case LLE_ERROR_PLUGIN_LOAD_FAILED:
        return "LLE_ERROR_PLUGIN_LOAD_FAILED";
    case LLE_ERROR_PLUGIN_INIT_FAILED:
        return "LLE_ERROR_PLUGIN_INIT_FAILED";
    case LLE_ERROR_PLUGIN_VALIDATION_FAILED:
        return "LLE_ERROR_PLUGIN_VALIDATION_FAILED";
    case LLE_ERROR_DEPENDENCY_MISSING:
        return "LLE_ERROR_DEPENDENCY_MISSING";
    case LLE_ERROR_VERSION_MISMATCH:
        return "LLE_ERROR_VERSION_MISMATCH";
    case LLE_ERROR_API_MISMATCH:
        return "LLE_ERROR_API_MISMATCH";
    case LLE_ERROR_CONFIGURATION_INVALID:
        return "LLE_ERROR_CONFIGURATION_INVALID";
    case LLE_ERROR_CONFIGURATION_MISSING:
        return "LLE_ERROR_CONFIGURATION_MISSING";

    /// Performance errors
    case LLE_ERROR_PERFORMANCE_DEGRADED:
        return "LLE_ERROR_PERFORMANCE_DEGRADED";
    case LLE_ERROR_RESOURCE_EXHAUSTED:
        return "LLE_ERROR_RESOURCE_EXHAUSTED";
    case LLE_ERROR_QUEUE_FULL:
        return "LLE_ERROR_QUEUE_FULL";
    case LLE_ERROR_CACHE_MISS:
        return "LLE_ERROR_CACHE_MISS";
    case LLE_ERROR_CACHE_CORRUPTED:
        return "LLE_ERROR_CACHE_CORRUPTED";
    case LLE_ERROR_THROTTLING_ACTIVE:
        return "LLE_ERROR_THROTTLING_ACTIVE";
    case LLE_ERROR_MONITORING_FAILURE:
        return "LLE_ERROR_MONITORING_FAILURE";
    case LLE_ERROR_OPTIMIZATION_FAILED:
        return "LLE_ERROR_OPTIMIZATION_FAILED";

    /// Critical errors
    case LLE_ERROR_INITIALIZATION_FAILED:
        return "LLE_ERROR_INITIALIZATION_FAILED";
    case LLE_ERROR_SHUTDOWN_FAILED:
        return "LLE_ERROR_SHUTDOWN_FAILED";
    case LLE_ERROR_STATE_CORRUPTION:
        return "LLE_ERROR_STATE_CORRUPTION";
    case LLE_ERROR_INVARIANT_VIOLATION:
        return "LLE_ERROR_INVARIANT_VIOLATION";
    case LLE_ERROR_ASSERTION_FAILED:
        return "LLE_ERROR_ASSERTION_FAILED";
    case LLE_ERROR_FATAL_INTERNAL:
        return "LLE_ERROR_FATAL_INTERNAL";
    case LLE_ERROR_RECOVERY_FAILED:
        return "LLE_ERROR_RECOVERY_FAILED";
    case LLE_ERROR_DEGRADATION_LIMIT_REACHED:
        return "LLE_ERROR_DEGRADATION_LIMIT_REACHED";

    default:
        return "UNKNOWN_ERROR";
    }
}

/// @brief Get severity level name as string
static const char *lle_severity_to_string(lle_error_severity_t severity) {
    switch (severity) {
    case LLE_SEVERITY_INFO:
        return "INFO";
    case LLE_SEVERITY_WARNING:
        return "WARNING";
    case LLE_SEVERITY_MINOR:
        return "MINOR";
    case LLE_SEVERITY_MAJOR:
        return "MAJOR";
    case LLE_SEVERITY_CRITICAL:
        return "CRITICAL";
    case LLE_SEVERITY_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

/* ============================================================================
 * ERROR SEVERITY DETERMINATION
 * ============================================================================
 */

/**
 * @brief Determine error severity based on error code and context
 */
lle_error_severity_t
lle_determine_error_severity(lle_result_t error_code,
                             const lle_error_context_t *context) {
    /// Memory errors are generally critical
    if (error_code >= LLE_ERROR_OUT_OF_MEMORY &&
        error_code < LLE_ERROR_SYSTEM_CALL) {
        if (error_code == LLE_ERROR_MEMORY_CORRUPTION ||
            error_code == LLE_ERROR_USE_AFTER_FREE) {
            return LLE_SEVERITY_FATAL;
        }
        return LLE_SEVERITY_CRITICAL;
    }

    /// System call errors depend on context
    if (error_code >= LLE_ERROR_SYSTEM_CALL &&
        error_code < LLE_ERROR_BUFFER_COMPONENT) {
        if (context && context->critical_path_affected) {
            return LLE_SEVERITY_CRITICAL;
        }
        return LLE_SEVERITY_MAJOR;
    }

    /// Component errors are generally recoverable
    if (error_code >= LLE_ERROR_BUFFER_COMPONENT &&
        error_code < LLE_ERROR_FEATURE_DISABLED) {
        return LLE_SEVERITY_MAJOR;
    }

    /// Feature errors are usually minor
    if (error_code >= LLE_ERROR_FEATURE_DISABLED &&
        error_code < LLE_ERROR_PERFORMANCE_DEGRADED) {
        return LLE_SEVERITY_MINOR;
    }

    /// Performance errors are warnings unless severe
    if (error_code >= LLE_ERROR_PERFORMANCE_DEGRADED &&
        error_code < LLE_ERROR_INITIALIZATION_FAILED) {
        if (context &&
            context->performance_impact_ns > 1000000) { /// > 1ms impact
            return LLE_SEVERITY_MAJOR;
        }
        return LLE_SEVERITY_WARNING;
    }

    /// Critical system errors
    if (error_code >= LLE_ERROR_INITIALIZATION_FAILED) {
        return LLE_SEVERITY_CRITICAL;
    }

    return LLE_SEVERITY_INFO;
}

/**
 * @brief Fast severity determination without context (for critical paths)
 */
lle_error_severity_t lle_fast_determine_severity(lle_result_t error_code) {
    return lle_determine_error_severity(error_code, NULL);
}

/* ============================================================================
 * SYSTEM STATE CAPTURE
 * ============================================================================
 */

/// @brief Capture current system state into error context
static void lle_capture_system_state(lle_error_context_t *ctx) {
    if (!ctx)
        return;

    /// Memory usage tracking
    ctx->memory_usage_bytes =
        0; /// Integrates with memory system when available
    ctx->memory_pool_utilization = 0;

    /// Active components bitmask
    ctx->active_components = lle_get_active_components_mask();

    /// System load factor
    ctx->system_load_factor = lle_calculate_system_load();

    /// Performance impact measurement
    ctx->performance_impact_ns = lle_measure_current_performance_impact();
    ctx->critical_path_affected = lle_is_critical_path_active();
}

/* ============================================================================
 * ERROR CONTEXT CREATION AND MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create comprehensive error context with full state capture
 */
lle_error_context_t *lle_create_error_context(lle_result_t error_code,
                                              const char *message,
                                              const char *function,
                                              const char *file, int line,
                                              const char *component) {
    /// Allocate from error pool
    lle_error_context_t *ctx =
        lle_error_pool_alloc(sizeof(lle_error_context_t));
    if (!ctx) {
        /// Fallback to static error context for critical memory situations
        return &g_static_error_context;
    }

    /// Zero-initialize structure
    memset(ctx, 0, sizeof(lle_error_context_t));

    /// Populate basic error information
    ctx->error_code = error_code;
    ctx->error_message = lle_error_string_pool_strdup(message);
    ctx->technical_details = lle_generate_technical_details(error_code);

    /// Source location information
    ctx->function_name = function;
    ctx->file_name = file;
    ctx->line_number = line;
    ctx->component_name = component;

    /// Execution context
    ctx->thread_id = lle_get_thread_id();
    ctx->timestamp_ns = lle_get_timestamp_ns();
    ctx->operation_id = lle_get_current_operation_id();
    ctx->operation_name = lle_get_current_operation_name();

    /// System state capture
    lle_capture_system_state(ctx);

    /// Error chain - initially NULL
    ctx->root_cause = NULL;
    ctx->immediate_cause = NULL;
    ctx->error_chain_depth = 0;

    /// Recovery information - initially unset
    ctx->recovery_attempts = 0;
    ctx->degradation_level = 0;
    ctx->auto_recovery_possible = true; /// Optimistic default
    ctx->user_intervention_required = false;

    /// Custom context data - initially NULL
    ctx->context_data = NULL;
    ctx->context_data_size = 0;
    ctx->context_data_cleanup = NULL;

    return ctx;
}

/**
 * @brief Destroy error context and free associated resources
 *
 * Properly frees an error context created by lle_create_error_context.
 * Handles the static fallback context safely (does not free it).
 *
 * @param ctx Error context to destroy (may be NULL)
 */
void lle_error_context_destroy(lle_error_context_t *ctx) {
    if (!ctx) {
        return;
    }

    /// Don't free the static fallback context
    if (ctx == &g_static_error_context) {
        return;
    }

    /// Free dynamically allocated error message
    if (ctx->error_message) {
        free((void *)ctx->error_message);
    }

    /// Free the context structure itself
    free(ctx);
}

/**
 * @brief Fast error context allocation for critical paths (zero-allocation)
 */
lle_error_context_t *lle_allocate_fast_error_context(void) {
    pthread_mutex_lock(&g_preallocated_errors.allocation_mutex);

    /// Start from hint position for better cache locality
    for (uint32_t i = 0; i < LLE_PREALLOCATED_ERROR_CONTEXTS; i++) {
        uint32_t index = (g_preallocated_errors.allocation_hint + i) %
                         LLE_PREALLOCATED_ERROR_CONTEXTS;

        if (!g_preallocated_errors.in_use[index]) {
            g_preallocated_errors.in_use[index] = true;
            g_preallocated_errors.allocation_hint =
                (index + 1) % LLE_PREALLOCATED_ERROR_CONTEXTS;

            lle_error_context_t *ctx = &g_preallocated_errors.contexts[index];
            memset(ctx, 0, sizeof(lle_error_context_t));

            pthread_mutex_unlock(&g_preallocated_errors.allocation_mutex);

            /// Update atomic counter
            atomic_fetch_add_explicit(
                &g_error_atomic_counters.preallocated_contexts_used, 1,
                memory_order_relaxed);

            return ctx;
        }
    }

    pthread_mutex_unlock(&g_preallocated_errors.allocation_mutex);
    return NULL; /// All preallocated contexts in use
}

/**
 * @brief Release fast error context back to pool
 */
void lle_release_fast_error_context(lle_error_context_t *ctx) {
    if (!ctx)
        return;

    /// Check if this is from preallocated pool
    ptrdiff_t offset = ctx - g_preallocated_errors.contexts;
    if (offset >= 0 && offset < LLE_PREALLOCATED_ERROR_CONTEXTS) {
        pthread_mutex_lock(&g_preallocated_errors.allocation_mutex);
        g_preallocated_errors.in_use[offset] = false;
        pthread_mutex_unlock(&g_preallocated_errors.allocation_mutex);

        /// Update atomic counter
        atomic_fetch_sub_explicit(
            &g_error_atomic_counters.preallocated_contexts_used, 1,
            memory_order_relaxed);
    }
}

/**
 * @brief Initialize memory-safe error context
 */
void lle_init_memory_safe_error_context(lle_memory_safe_error_context_t *ctx) {
    if (!ctx)
        return;

    memset(ctx, 0, sizeof(lle_memory_safe_error_context_t));

    /// Set magic values for corruption detection
    ctx->magic_header = 0xDEADBEEF;
    ctx->magic_footer = 0xCAFEBABE;

    /// Initialize resource tracking
    ctx->allocated_resources = NULL;
    ctx->allocated_count = 0;
    ctx->allocated_capacity = 0;
    ctx->cleanup_functions = NULL;
    ctx->cleanup_count = 0;
}

/**
 * @brief Cleanup memory-safe error context and release all resources
 */
void lle_cleanup_memory_safe_error_context(
    lle_memory_safe_error_context_t *ctx) {
    if (!ctx)
        return;

    /// Verify magic values
    if (ctx->magic_header != 0xDEADBEEF || ctx->magic_footer != 0xCAFEBABE) {
        /// Memory corruption detected
        return;
    }

    /// Execute cleanup functions
    for (size_t i = 0; i < ctx->cleanup_count; i++) {
        if (ctx->cleanup_functions[i] && i < ctx->allocated_count) {
            ctx->cleanup_functions[i](ctx->allocated_resources[i]);
        }
    }

    /// Free tracking arrays
    free(ctx->allocated_resources);
    free(ctx->cleanup_functions);

    /// Cleanup base context custom data
    if (ctx->base_context.context_data &&
        ctx->base_context.context_data_cleanup) {
        ctx->base_context.context_data_cleanup(ctx->base_context.context_data);
    }

    /// Clear magic values
    ctx->magic_header = 0;
    ctx->magic_footer = 0;
}

/* ============================================================================
 * ERROR REPORTING
 * ============================================================================
 */

/**
 * @brief Format error context to console output
 */
void lle_report_error_to_console(const lle_error_context_t *context) {
    if (!context)
        return;

    lle_error_severity_t severity =
        lle_determine_error_severity(context->error_code, context);

    /// Color codes for different severity levels
    const char *color_reset = "\033[0m";
    const char *color;

    switch (severity) {
    case LLE_SEVERITY_FATAL:
    case LLE_SEVERITY_CRITICAL:
        color = "\033[1;31m"; /// Bold Red
        break;
    case LLE_SEVERITY_MAJOR:
        color = "\033[0;31m"; /// Red
        break;
    case LLE_SEVERITY_WARNING:
        color = "\033[0;33m"; /// Yellow
        break;
    case LLE_SEVERITY_MINOR:
        color = "\033[0;36m"; /// Cyan
        break;
    default:
        color = "\033[0;37m"; /// White
        break;
    }

    fprintf(stderr, "\n%s[LLE %s]%s %s (%s)\n", color,
            lle_severity_to_string(severity), color_reset,
            context->error_message ? context->error_message : "Unknown error",
            lle_error_code_to_string(context->error_code));

    fprintf(stderr, "  Location: %s:%d in %s()\n",
            context->file_name ? context->file_name : "unknown",
            context->line_number,
            context->function_name ? context->function_name : "unknown");

    fprintf(stderr, "  Component: %s\n",
            context->component_name ? context->component_name : "unknown");

    if (context->technical_details) {
        fprintf(stderr, "  Details: %s\n", context->technical_details);
    }

    fprintf(stderr, "  Thread: 0x%lx, Time: %lu ns\n",
            (unsigned long)context->thread_id,
            (unsigned long)context->timestamp_ns);

    if (context->critical_path_affected) {
        fprintf(stderr, "  WARNING: Critical path affected!\n");
    }

    fprintf(stderr, "\n");
}

/**
 * @brief Write error to log file
 */
void lle_report_error_to_log_file(lle_error_reporting_system_t *system,
                                  const lle_error_context_t *context) {
    if (!system || !context || !system->log_file)
        return;

    lle_error_severity_t severity =
        lle_determine_error_severity(context->error_code, context);

    /// Format: [timestamp] [severity] [component] message
    fprintf(system->log_file, "[%lu] [%s] [%s] %s (%s)\n",
            (unsigned long)context->timestamp_ns,
            lle_severity_to_string(severity),
            context->component_name ? context->component_name : "unknown",
            context->error_message ? context->error_message : "Unknown error",
            lle_error_code_to_string(context->error_code));

    fprintf(system->log_file, "    at %s:%d in %s()\n",
            context->file_name ? context->file_name : "unknown",
            context->line_number,
            context->function_name ? context->function_name : "unknown");

    if (context->technical_details) {
        fprintf(system->log_file, "    %s\n", context->technical_details);
    }

    fflush(system->log_file);
}

/**
 * @brief Write error to system log (syslog)
 */
void lle_report_error_to_system_log(const lle_error_context_t *context) {
    if (!context)
        return;

    lle_error_severity_t severity =
        lle_determine_error_severity(context->error_code, context);

    /// Map LLE severity to syslog priority
    int priority;
    switch (severity) {
    case LLE_SEVERITY_FATAL:
    case LLE_SEVERITY_CRITICAL:
        priority = LOG_CRIT;
        break;
    case LLE_SEVERITY_MAJOR:
        priority = LOG_ERR;
        break;
    case LLE_SEVERITY_WARNING:
        priority = LOG_WARNING;
        break;
    case LLE_SEVERITY_MINOR:
        priority = LOG_NOTICE;
        break;
    default:
        priority = LOG_INFO;
        break;
    }

    syslog(priority, "[%s] %s (%s) at %s:%d",
           context->component_name ? context->component_name : "LLE",
           context->error_message ? context->error_message : "Unknown error",
           lle_error_code_to_string(context->error_code),
           context->file_name ? context->file_name : "unknown",
           context->line_number);
}

/**
 * @brief Check if error should be suppressed (flood control)
 */
bool lle_should_suppress_error(lle_error_reporting_system_t *system,
                               const lle_error_context_t *context) {
    (void)system;

    /// Flood control logic
    /// Never suppress critical or fatal errors
    lle_error_severity_t severity =
        lle_determine_error_severity(context->error_code, context);

    if (severity >= LLE_SEVERITY_CRITICAL) {
        return false;
    }

    /// A future revision will implement the full suppression table
    return false;
}

/**
 * @brief Report error through all configured channels
 */
lle_result_t lle_error_reporting_system_init(void) {
    if (g_error_system_initialized) {
        return LLE_SUCCESS;
    }

    lle_error_reporting_system_t *system = calloc(1, sizeof(*system));
    if (!system) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Reporting channels start disabled; the singleton accumulates
    /// statistics and owns the reporting mutex. Channels are configured by
    /// later wiring, not at construction.
    if (pthread_mutex_init(&system->reporting_mutex, NULL) != 0) {
        free(system);
        return LLE_ERROR_SYSTEM_CALL;
    }

    g_error_reporting_system = system;
    g_error_system_initialized = true;
    return LLE_SUCCESS;
}

void lle_error_reporting_system_shutdown(void) {
    if (!g_error_system_initialized) {
        return;
    }

    lle_error_reporting_system_t *system = g_error_reporting_system;
    if (system) {
        if (system->log_file) {
            fclose(system->log_file);
            system->log_file = NULL;
        }
        pthread_mutex_destroy(&system->reporting_mutex);
        free(system);
    }

    g_error_reporting_system = NULL;
    g_error_system_initialized = false;
}

bool lle_error_reporting_system_is_initialized(void) {
    return g_error_system_initialized;
}

void lle_error_get_counter_snapshot(lle_error_counter_snapshot_t *out) {
    if (!out) {
        return;
    }

    out->total_errors_handled = atomic_load_explicit(
        &g_error_atomic_counters.total_errors_handled, memory_order_acquire);
    out->critical_errors_count = atomic_load_explicit(
        &g_error_atomic_counters.critical_errors_count, memory_order_acquire);
    out->warnings_count = atomic_load_explicit(
        &g_error_atomic_counters.warnings_count, memory_order_acquire);
    out->active_error_contexts = atomic_load_explicit(
        &g_error_atomic_counters.active_error_contexts, memory_order_acquire);
    out->concurrent_errors = atomic_load_explicit(
        &g_error_atomic_counters.concurrent_errors, memory_order_acquire);
}

/// Fault channel sinks, installed by the shell at startup. NULL until then;
/// the router stays silent (no per-test stubs needed for standalone liblle).
static lle_fault_sink_t g_user_fault_sink = NULL;
static lle_fault_sink_t g_dev_fault_sink = NULL;

/// Faults at or above this severity reach the user-visible channel; below it
/// they are developer-diagnostic only. Major maps to the shell's ERROR level.
#define LLE_FAULT_USER_THRESHOLD LLE_SEVERITY_MAJOR

void lle_fault_set_user_sink(lle_fault_sink_t fn) { g_user_fault_sink = fn; }

void lle_fault_set_dev_sink(lle_fault_sink_t fn) { g_dev_fault_sink = fn; }

lle_fault_disposition_t lle_handle_fault_lifecycle(const lle_fault_t *fault) {
    if (!fault) {
        return LLE_FAULT_SURFACED;
    }

    /// Account: bump the always-available atomic counters. Mirrors the
    /// existing lle_report_error accounting: critical counts truly-critical
    /// faults; the major user-channel threshold is a separate concern below.
    atomic_fetch_add_explicit(&g_error_atomic_counters.total_errors_handled, 1,
                              memory_order_relaxed);
    if (fault->severity >= LLE_SEVERITY_CRITICAL) {
        atomic_fetch_add_explicit(
            &g_error_atomic_counters.critical_errors_count, 1,
            memory_order_relaxed);
    } else if (fault->severity <= LLE_SEVERITY_WARNING) {
        atomic_fetch_add_explicit(&g_error_atomic_counters.warnings_count, 1,
                                  memory_order_relaxed);
    }

    /// Route: the developer channel sees every fault; the user channel sees
    /// only faults the user can act on. Future recovery/degradation strategies
    /// will be consulted here, before surfacing, and may return a different
    /// disposition.
    if (g_dev_fault_sink) {
        g_dev_fault_sink(fault);
    }
    if (fault->severity >= LLE_FAULT_USER_THRESHOLD && g_user_fault_sink) {
        g_user_fault_sink(fault);
    }

    return LLE_FAULT_SURFACED;
}

lle_result_t lle_fault_report(lle_result_t code, const char *component,
                              const char *detail, const char *function,
                              const char *file, int line) {
    lle_fault_t fault = {
        .code = code,
        .severity = lle_determine_error_severity(code, NULL),
        .component = component,
        .detail = detail,
        .function = function,
        .file = file,
        .line = line,
    };
    lle_handle_fault_lifecycle(&fault);
    return code;
}

lle_result_t lle_report_error(const lle_error_context_t *context) {
    if (!context) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Update atomic statistics
    lle_error_severity_t severity =
        lle_determine_error_severity(context->error_code, context);

    atomic_fetch_add_explicit(&g_error_atomic_counters.total_errors_handled, 1,
                              memory_order_relaxed);

    if (severity >= LLE_SEVERITY_CRITICAL) {
        atomic_fetch_add_explicit(
            &g_error_atomic_counters.critical_errors_count, 1,
            memory_order_relaxed);
    } else if (severity <= LLE_SEVERITY_WARNING) {
        atomic_fetch_add_explicit(&g_error_atomic_counters.warnings_count, 1,
                                  memory_order_relaxed);
    }

    uint64_t reporting_start = lle_get_timestamp_ns();

    /// Console reporting - always enabled for now
    lle_report_error_to_console(context);

    /// Log file reporting if system is configured
    if (g_error_reporting_system) {
        if (g_error_reporting_system->config.log_file_reporting_enabled &&
            severity >=
                g_error_reporting_system->config.min_log_file_severity) {
            lle_report_error_to_log_file(g_error_reporting_system, context);
        }

        /// System log reporting
        if (g_error_reporting_system->config.system_log_reporting_enabled &&
            severity >=
                g_error_reporting_system->config.min_system_log_severity) {
            lle_report_error_to_system_log(context);
        }

        /// Callback reporting
        if (g_error_reporting_system->config.callback_reporting_enabled &&
            g_error_reporting_system->config.error_callback &&
            severity >=
                g_error_reporting_system->config.min_callback_severity) {
            g_error_reporting_system->config.error_callback(
                context, g_error_reporting_system->config.callback_user_data);
        }

        /// Update reporting statistics
        uint64_t reporting_end = lle_get_timestamp_ns();
        uint64_t latency = reporting_end - reporting_start;

        g_error_reporting_system->total_errors_reported++;
        g_error_reporting_system->errors_by_severity[severity]++;

        g_error_reporting_system->avg_reporting_latency_ns =
            (g_error_reporting_system->avg_reporting_latency_ns + latency) / 2;

        if (latency > g_error_reporting_system->max_reporting_latency_ns) {
            g_error_reporting_system->max_reporting_latency_ns = latency;
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Fast critical error reporting for critical paths
 */
void lle_fast_report_critical_error(const lle_error_context_t *ctx) {
    if (!ctx)
        return;

    /// Minimal console output for critical paths
    fprintf(stderr, "[LLE CRITICAL] %s (%d) in %s\n",
            ctx->error_message ? ctx->error_message : "Critical error",
            ctx->error_code,
            ctx->component_name ? ctx->component_name : "unknown");
}

/* ============================================================================
 * CRITICAL PATH ERROR HANDLING
 * ============================================================================
 */

/**
 * @brief Handle errors on critical path with minimal overhead
 */
lle_result_t lle_handle_critical_path_error(lle_result_t error_code,
                                            const char *component) {
    /// Use pre-allocated context to avoid memory allocation
    lle_error_context_t *ctx = lle_allocate_fast_error_context();
    if (!ctx) {
        /// Fallback to static context if no pre-allocated available
        ctx = &g_static_error_context;
    }

    /// Minimal context population for performance
    ctx->error_code = error_code;
    ctx->timestamp_ns = lle_get_fast_timestamp_ns();
    ctx->component_name = component;
    ctx->thread_id = lle_get_thread_id_cached();
    ctx->critical_path_affected = true;

    /// Fast severity determination
    lle_error_severity_t severity = lle_fast_determine_severity(error_code);

    /// Fast reporting (minimal) for critical errors
    if (severity >= LLE_SEVERITY_CRITICAL) {
        lle_fast_report_critical_error(ctx);
    }

    /// Update statistics
    atomic_fetch_add_explicit(&g_error_atomic_counters.total_errors_handled, 1,
                              memory_order_relaxed);

    /// Release context if it was pre-allocated
    if (ctx != &g_static_error_context) {
        lle_release_fast_error_context(ctx);
    }

    return error_code; /// Return original error for propagation
}

/* ============================================================================
 * ATOMIC OPERATIONS FOR ERROR STATISTICS
 * ============================================================================
 */

/**
 * @brief Increment atomic error counter with relaxed ordering
 */
void lle_error_increment_counter(_Atomic uint64_t *counter) {
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

/**
 * @brief Read atomic error counter with acquire ordering
 */
uint64_t lle_error_read_counter(_Atomic uint64_t *counter) {
    return atomic_load_explicit(counter, memory_order_acquire);
}

/**
 * @brief Update maximum time atomically with compare-exchange
 */
void lle_error_update_max_time(_Atomic uint64_t *max_time, uint64_t new_time) {
    uint64_t current_max = atomic_load_explicit(max_time, memory_order_relaxed);
    while (new_time > current_max) {
        if (atomic_compare_exchange_weak_explicit(
                max_time, &current_max, new_time, memory_order_release,
                memory_order_relaxed)) {
            break;
        }
    }
}

/**
 * @brief Try to acquire error context atomically
 */
bool lle_error_try_acquire_context_atomic(void) {
    uint32_t current_count = atomic_load_explicit(
        &g_error_atomic_counters.active_error_contexts, memory_order_acquire);

    if (current_count >= LLE_PREALLOCATED_ERROR_CONTEXTS) {
        return false;
    }

    uint32_t expected = current_count;
    return atomic_compare_exchange_strong_explicit(
        &g_error_atomic_counters.active_error_contexts, &expected,
        current_count + 1, memory_order_acq_rel, memory_order_acquire);
}

/**
 * @brief Release error context atomically
 */
void lle_error_release_context_atomic(void) {
    atomic_fetch_sub_explicit(&g_error_atomic_counters.active_error_contexts, 1,
                              memory_order_release);
}

/**
 * @brief Update error statistics atomically (lock-free)
 */
lle_result_t lle_error_update_statistics_lockfree(lle_result_t error_code,
                                                  lle_error_severity_t severity,
                                                  uint64_t recovery_time_ns,
                                                  bool recovery_successful) {
    (void)error_code; /// Error code is encoded in severity for now

    /// Update total errors counter
    lle_error_increment_counter(&g_error_atomic_counters.total_errors_handled);

    /// Update severity-specific counters
    switch (severity) {
    case LLE_SEVERITY_CRITICAL:
    case LLE_SEVERITY_MAJOR:
        lle_error_increment_counter(
            &g_error_atomic_counters.critical_errors_count);
        break;
    case LLE_SEVERITY_WARNING:
    case LLE_SEVERITY_MINOR:
        lle_error_increment_counter(&g_error_atomic_counters.warnings_count);
        break;
    default:
        break;
    }

    /// Update recovery statistics
    if (recovery_time_ns > 0) {
        if (recovery_successful) {
            lle_error_increment_counter(
                &g_error_atomic_counters.recoveries_successful);
        } else {
            lle_error_increment_counter(
                &g_error_atomic_counters.recoveries_failed);
        }

        /// Update recovery time statistics
        atomic_fetch_add_explicit(
            &g_error_atomic_counters.total_recovery_time_ns, recovery_time_ns,
            memory_order_relaxed);
        lle_error_update_max_time(&g_error_atomic_counters.max_recovery_time_ns,
                                  recovery_time_ns);
    }

    return LLE_SUCCESS;
}
