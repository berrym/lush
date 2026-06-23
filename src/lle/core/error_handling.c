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
