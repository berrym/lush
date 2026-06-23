/**
 * @file error_handling.h
 * @brief LLE Error Handling System - Type Definitions and Function Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 16 - Error Handling Complete Specification
 * Version: 1.0.0
 *
 * This header contains ALL type definitions and function declarations for the
 * LLE error handling system. NO implementations are included here.
 *
 * Layer 0: Type Definitions Only
 * Layer 1: Implementations in src/lle/error_handling.c (separate file)
 */

#ifndef LLE_ERROR_HANDLING_H
#define LLE_ERROR_HANDLING_H

/* Portable macro for marking intentionally unused
 * functions/variables/parameters. Use for spec-compliant code that is
 * implemented but not yet wired up. */
#ifdef __GNUC__
#define LLE_MAYBE_UNUSED __attribute__((unused))
#else
#define LLE_MAYBE_UNUSED
#endif

/// Convenience macro for unused parameters in function bodies
#define LLE_UNUSED(x) (void)(x)

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/// Forward declarations for circular dependencies
struct lle_memory_pool;
struct lle_performance_monitor;

/* ============================================================================
 * PRIMARY RESULT TYPE
 * ============================================================================
 */

/**
 * @brief Primary result type for all LLE operations
 *
 * Hierarchical error code system with 50+ specific error types organized
 * by category. Used as return type for virtually every LLE function.
 */
typedef enum {
    /// Success codes (0-999)
    LLE_SUCCESS = 0,           ///< Operation completed successfully
    LLE_SUCCESS_WITH_WARNINGS, ///< Success with non-critical issues

    /// Input validation errors (1000-1099)
    LLE_ERROR_INVALID_PARAMETER = 1000, ///< Invalid function parameter
    LLE_ERROR_NULL_POINTER,             ///< Null pointer passed
    LLE_ERROR_BUFFER_OVERFLOW,          ///< Buffer size exceeded
    LLE_ERROR_BUFFER_UNDERFLOW,         ///< Buffer size insufficient
    LLE_ERROR_INVALID_STATE,            ///< Component in invalid state
    LLE_ERROR_INVALID_RANGE,            ///< Value outside valid range
    LLE_ERROR_INVALID_FORMAT,           ///< Data format validation failed
    LLE_ERROR_INVALID_ENCODING,         ///< Text encoding validation failed

    /// Memory management errors (1100-1199)
    LLE_ERROR_OUT_OF_MEMORY = 1100,  ///< Memory allocation failed
    LLE_ERROR_MEMORY_CORRUPTION,     ///< Memory corruption detected
    LLE_ERROR_MEMORY_POOL_EXHAUSTED, ///< Specific pool exhausted
    LLE_ERROR_MEMORY_LEAK_DETECTED,  ///< Memory leak detection triggered
    LLE_ERROR_DOUBLE_FREE_DETECTED,  ///< Double free attempt detected
    LLE_ERROR_USE_AFTER_FREE,        ///< Use after free detected
    LLE_ERROR_MEMORY_ALIGNMENT,      ///< Memory alignment requirements violated
    LLE_ERROR_MEMORY_PROTECTION,     ///< Memory protection violation

    /// System integration errors (1200-1299)
    LLE_ERROR_SYSTEM_CALL = 1200,   ///< System call failed
    LLE_ERROR_IO_ERROR,             ///< I/O operation failed
    LLE_ERROR_TIMEOUT,              ///< Operation timed out
    LLE_ERROR_INTERRUPT,            ///< Operation interrupted
    LLE_ERROR_PERMISSION_DENIED,    ///< Permission denied
    LLE_ERROR_RESOURCE_UNAVAILABLE, ///< System resource unavailable
    LLE_ERROR_DEVICE_ERROR,         ///< Device or driver error
    LLE_ERROR_NETWORK_ERROR,        ///< Network operation failed

    /// Component-specific errors (1300-1399)
    LLE_ERROR_BUFFER_COMPONENT = 1300,  ///< Buffer management error
    LLE_ERROR_EVENT_SYSTEM,             ///< Event system error
    LLE_ERROR_TERMINAL_ABSTRACTION,     ///< Terminal abstraction error
    LLE_ERROR_INPUT_PARSING,            ///< Input parsing error
    LLE_ERROR_HISTORY_SYSTEM,           ///< History management error
    LLE_ERROR_AUTOSUGGESTIONS,          ///< Autosuggestions error
    LLE_ERROR_SYNTAX_HIGHLIGHTING,      ///< Syntax highlighting error
    LLE_ERROR_COMPLETION_SYSTEM,        ///< Tab completion error
    LLE_ERROR_DISPLAY_INTEGRATION,      ///< Display integration error
    LLE_ERROR_DISPLAY_SUBMISSION,       ///< Display submission error
    LLE_ERROR_PERFORMANCE_MONITORING,   ///< Performance monitoring error
    LLE_ERROR_NO_UNDO_AVAILABLE,        ///< No undo operation available
    LLE_ERROR_NO_REDO_AVAILABLE,        ///< No redo operation available
    LLE_ERROR_OPERATION_IN_PROGRESS,    ///< Operation already in progress
    LLE_ERROR_NO_OPERATION_IN_PROGRESS, ///< No operation in progress
    LLE_ERROR_INVALID_INPUT_EVENT,      ///< Invalid input event data

    /// Feature and extensibility errors (1400-1499)
    LLE_ERROR_FEATURE_DISABLED = 1400,  ///< Required feature disabled
    LLE_ERROR_FEATURE_NOT_AVAILABLE,    ///< Feature not available
    LLE_ERROR_PLUGIN_LOAD_FAILED,       ///< Plugin loading failed
    LLE_ERROR_PLUGIN_INIT_FAILED,       ///< Plugin initialization failed
    LLE_ERROR_PLUGIN_VALIDATION_FAILED, ///< Plugin validation failed
    LLE_ERROR_DEPENDENCY_MISSING,       ///< Required dependency missing
    LLE_ERROR_VERSION_MISMATCH,         ///< Version compatibility error
    LLE_ERROR_API_MISMATCH,             ///< API compatibility error
    LLE_ERROR_CONFIGURATION_INVALID,    ///< Configuration validation failed
    LLE_ERROR_CONFIGURATION_MISSING,    ///< Required configuration missing

    /// Performance and resource errors (1500-1599)
    LLE_ERROR_PERFORMANCE_DEGRADED = 1500, ///< Performance below threshold
    LLE_ERROR_RESOURCE_EXHAUSTED,          ///< Resource limit exceeded
    LLE_ERROR_QUEUE_FULL,                  ///< Event queue full
    LLE_ERROR_QUEUE_EMPTY,                 ///< Event queue empty
    LLE_ERROR_NOT_FOUND,                   ///< Item not found
    LLE_ERROR_ALREADY_INITIALIZED,         ///< Already initialized
    LLE_ERROR_NOT_INITIALIZED,             ///< Not initialized
    LLE_ERROR_CACHE_MISS,                  ///< Critical cache miss
    LLE_ERROR_CACHE_CORRUPTED,             ///< Cache corruption detected
    LLE_ERROR_THROTTLING_ACTIVE,           ///< Resource throttling active
    LLE_ERROR_MONITORING_FAILURE,          ///< Performance monitoring failure
    LLE_ERROR_OPTIMIZATION_FAILED,         ///< Optimization attempt failed
    LLE_ERROR_ALREADY_EXISTS,              ///< Item already exists
    LLE_ERROR_DISABLED,                    ///< Feature/widget is disabled

    /// Critical system errors (1600-1699)
    LLE_ERROR_INITIALIZATION_FAILED = 1600, ///< System initialization failed
    LLE_ERROR_SHUTDOWN_FAILED,              ///< System shutdown failed
    LLE_ERROR_STATE_CORRUPTION,             ///< Internal state corrupted
    LLE_ERROR_INVARIANT_VIOLATION,          ///< Internal invariant violated
    LLE_ERROR_ASSERTION_FAILED,             ///< Assertion failure
    LLE_ERROR_FATAL_INTERNAL,               ///< Fatal internal error
    LLE_ERROR_RECOVERY_FAILED,              ///< Error recovery failed
    LLE_ERROR_DEGRADATION_LIMIT_REACHED     ///< Maximum degradation reached
} lle_result_t;

/* ============================================================================
 * ERROR SEVERITY CLASSIFICATION
 * ============================================================================
 */

/**
 * @brief Error severity levels for classification and reporting
 */
typedef enum {
    LLE_SEVERITY_INFO,     ///< Informational, no action needed
    LLE_SEVERITY_WARNING,  ///< Warning, monitoring recommended
    LLE_SEVERITY_MINOR,    ///< Minor error, degraded functionality
    LLE_SEVERITY_MAJOR,    ///< Major error, significant impact
    LLE_SEVERITY_CRITICAL, ///< Critical error, immediate attention
    LLE_SEVERITY_FATAL     ///< Fatal error, system shutdown required
} lle_error_severity_t;

/* ============================================================================
 * ERROR CONTEXT STRUCTURE
 * ============================================================================
 */

/**
 * @brief Comprehensive error context for detailed error reporting
 *
 * Contains complete information about an error including source location,
 * execution context, system state, error chain, recovery information,
 * and performance impact.
 */
typedef struct lle_error_context {
    /// Primary error information
    lle_result_t error_code;       ///< Primary error code
    const char *error_message;     ///< Human-readable error message
    const char *technical_details; ///< Technical details for debugging

    /// Source location information
    const char *function_name;  ///< Function where error occurred
    const char *file_name;      ///< Source file name
    int line_number;            ///< Line number in source
    const char *component_name; ///< LLE component name

    /// Execution context
    uint64_t thread_id;         ///< Thread identifier
    uint64_t timestamp_ns;      ///< Error timestamp (nanoseconds)
    uint64_t operation_id;      ///< Unique operation identifier
    const char *operation_name; ///< Operation being performed

    /// System state information
    size_t memory_usage_bytes;      ///< Current memory usage
    size_t memory_pool_utilization; ///< Memory pool utilization percentage
    uint32_t active_components;     ///< Bitmask of active components
    uint32_t system_load_factor;    ///< Current system load (0-100)

    /// Error chain and causality
    struct lle_error_context *root_cause;      ///< Root cause error
    struct lle_error_context *immediate_cause; ///< Immediate cause error
    uint32_t error_chain_depth;                ///< Depth in error chain

    /// Recovery and handling information
    uint32_t recovery_attempts;      ///< Number of recovery attempts made
    uint32_t degradation_level;      ///< Current system degradation level
    bool auto_recovery_possible;     ///< Whether auto-recovery is possible
    bool user_intervention_required; ///< Whether user intervention needed

    /// Performance impact
    uint64_t performance_impact_ns; ///< Performance impact measurement
    bool critical_path_affected;    ///< Whether critical path affected

    /// Custom context data
    void *context_data;       ///< Component-specific context data
    size_t context_data_size; ///< Size of context data
    void (*context_data_cleanup)(
        void *data); ///< Cleanup function for context data
} lle_error_context_t;

/* ============================================================================
 * ATOMIC ERROR STATISTICS
 * ============================================================================
 */

/**
 * @brief Atomic error statistics counters
 *
 * Lock-free atomic counters for error statistics accessible from multiple
 * threads without contention.
 */
typedef struct lle_error_atomic_counters {
    _Atomic uint64_t total_errors_handled;  ///< Total errors handled
    _Atomic uint64_t critical_errors_count; ///< Critical errors count
    _Atomic uint64_t warnings_count;        ///< Warnings count
    _Atomic uint64_t recoveries_successful; ///< Successful recoveries
    _Atomic uint64_t recoveries_failed;     ///< Failed recoveries
    _Atomic uint32_t active_error_contexts; ///< Active error contexts
    _Atomic uint32_t
        preallocated_contexts_used;          ///< Pre-allocated contexts in use
    _Atomic uint64_t total_recovery_time_ns; ///< Total recovery time
    _Atomic uint64_t max_recovery_time_ns;   ///< Maximum recovery time
    _Atomic uint32_t concurrent_errors;      ///< Concurrent errors
} lle_error_atomic_counters_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ============================================================================
 * All implementations are in src/lle/error_handling.c (Layer 1)
 */

/// Error Severity
/**
 * @brief Determine the severity of an error using its code and context
 * @param error_code Error code to classify
 * @param context Optional error context for refinement
 * @return Severity level
 */
lle_error_severity_t
lle_determine_error_severity(lle_result_t error_code,
                             const lle_error_context_t *context);

/// Error Counter Snapshot

/**
 * @brief Read-only snapshot of the global error counters
 *
 * Plain (non-atomic) copy of the live atomic counters, taken with acquire
 * ordering. Exposes only counters that are meaningful today; recovery-time
 * counters are reserved for the future recovery milestone and are not
 * surfaced here.
 */
typedef struct lle_error_counter_snapshot {
    uint64_t total_errors_handled;  ///< Total faults reported
    uint64_t critical_errors_count; ///< Critical/major severity faults
    uint64_t warnings_count;        ///< Warning/minor severity faults
    uint32_t active_error_contexts; ///< Error contexts currently checked out
    uint32_t concurrent_errors;     ///< Faults being processed concurrently
} lle_error_counter_snapshot_t;

/**
 * @brief Copy the live error counters into a caller-provided snapshot
 * @param out Destination snapshot; ignored if NULL
 */
void lle_error_get_counter_snapshot(lle_error_counter_snapshot_t *out);

/// Fault Router and Lifecycle Seam

/**
 * @brief A single fault captured at the report site
 *
 * Plain fields only -- no shell types -- so the router stays decoupled from
 * the shell's structured-error renderer. The shell-side sinks (registered via
 * lle_fault_set_user_sink / _set_dev_sink) translate this into whatever the
 * user and developer channels need.
 */
typedef struct lle_fault {
    lle_result_t code;             ///< The LLE result code that was raised
    lle_error_severity_t severity; ///< Severity derived from the code
    const char *component;         ///< Subsystem name, e.g. "history"
    const char *detail;            ///< Short human phrase, e.g. "index alloc"
    const char *function;          ///< Function where the fault was raised
    const char *file;              ///< Source file
    int line;                      ///< Source line
} lle_fault_t;

/**
 * @brief Disposition of a fault after the lifecycle dispatch
 *
 * Today the dispatch only ever surfaces. The reserved values mark where the
 * future recovery/degradation milestone will return without reshaping the
 * call path or any LLE_FAULT() site.
 */
typedef enum lle_fault_disposition {
    LLE_FAULT_SURFACED, ///< Reported through the channels (the only path today)
    LLE_FAULT_RECOVERED, ///< Reserved: a future strategy handled it
    LLE_FAULT_DEGRADED,  ///< Reserved: a future strategy reduced functionality
} lle_fault_disposition_t;

/**
 * @brief Output adapter for a fault channel
 *
 * Installed by the shell to bridge faults to the user-visible renderer or the
 * developer trace channel. The router owns the lle_fault_t; sinks must not
 * retain the pointer beyond the call.
 */
typedef void (*lle_fault_sink_t)(const lle_fault_t *fault);

/**
 * @brief Install the user-visible fault sink (shell structured-error renderer)
 * @param fn Sink callback, or NULL to detach
 */
void lle_fault_set_user_sink(lle_fault_sink_t fn);

/**
 * @brief Install the developer-diagnostic fault sink (trace channel)
 * @param fn Sink callback, or NULL to detach
 */
void lle_fault_set_dev_sink(lle_fault_sink_t fn);

/**
 * @brief Dispatch a fault through the lifecycle: account, then route
 *
 * Bumps the atomic counters, invokes the developer sink for every fault, and
 * the user sink for faults at or above major severity. The single seam where
 * the future recovery/degradation milestone will be wired.
 *
 * @param fault The fault to dispatch (must be non-NULL)
 * @return The disposition; today always LLE_FAULT_SURFACED
 */
lle_fault_disposition_t lle_handle_fault_lifecycle(const lle_fault_t *fault);

/**
 * @brief Report a genuine fault and return its code unchanged
 *
 * Builds an lle_fault_t (deriving severity from the code) and dispatches it
 * through lle_handle_fault_lifecycle. Returns @p code so call sites remain a
 * single expression. Use the LLE_FAULT macro rather than calling directly.
 *
 * @return @p code, unchanged
 */
lle_result_t lle_fault_report(lle_result_t code, const char *component,
                              const char *detail, const char *function,
                              const char *file, int line);

/**
 * @brief Report a genuine fault at the current source location
 *
 * Use only at genuine-fault sites (allocation/corruption/syscall failures),
 * not at validation guards or expected-not-found returns.
 */
#define LLE_FAULT(code, component, detail)                                     \
    lle_fault_report((code), (component), (detail), __func__, __FILE__,        \
                     __LINE__)

/**
 * @brief Generate a technical details string for an error code
 * @param error_code Error code to describe
 * @return Pointer to a technical details string (static or pool-owned)
 */
const char *lle_generate_technical_details(lle_result_t error_code);

/**
 * @brief Get the symbolic name of an error code (e.g.
 * "LLE_ERROR_OUT_OF_MEMORY")
 * @param error_code Error code to name
 * @return Pointer to a static string; never NULL
 */
const char *lle_error_code_to_string(lle_result_t error_code);

/// Timing Functions
/**
 * @brief Get a fast monotonic timestamp in nanoseconds
 * @return Timestamp in nanoseconds
 */
uint64_t lle_get_fast_timestamp_ns(void);

/**
 * @brief Get the current thread identifier from a thread-local cache
 * @return Thread identifier
 */
uint64_t lle_get_thread_id_cached(void);

/**
 * @brief Get the current thread identifier
 * @return Thread identifier
 */
uint64_t lle_get_thread_id(void);

/**
 * @brief Get a monotonic timestamp in nanoseconds
 * @return Timestamp in nanoseconds
 */
uint64_t lle_get_timestamp_ns(void);

/// System State
/**
 * @brief Get the identifier of the current in-flight operation
 * @return Current operation identifier
 */
uint64_t lle_get_current_operation_id(void);

/**
 * @brief Get the name of the current in-flight operation
 * @return Pointer to the current operation name
 */
const char *lle_get_current_operation_name(void);

/**
 * @brief Get the bitmask of currently active LLE components
 * @return Bitmask of active components
 */
uint32_t lle_get_active_components_mask(void);

/**
 * @brief Calculate the current system load factor
 * @return System load factor (0-100)
 */
uint32_t lle_calculate_system_load(void);

/**
 * @brief Measure the current error-handling performance impact
 * @return Performance impact measurement in nanoseconds
 */
uint64_t lle_measure_current_performance_impact(void);

/**
 * @brief Check whether the critical execution path is currently active
 * @return true if the critical path is active, false otherwise
 */
bool lle_is_critical_path_active(void);

#endif /// LLE_ERROR_HANDLING_H
