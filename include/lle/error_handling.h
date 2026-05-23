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

/* Convenience macro for unused parameters in function bodies */
#define LLE_UNUSED(x) (void)(x)

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Forward declarations for circular dependencies */
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
    /* Success codes (0-999) */
    LLE_SUCCESS = 0,           /**< Operation completed successfully */
    LLE_SUCCESS_WITH_WARNINGS, /**< Success with non-critical issues */

    /* Input validation errors (1000-1099) */
    LLE_ERROR_INVALID_PARAMETER = 1000, /**< Invalid function parameter */
    LLE_ERROR_NULL_POINTER,             /**< Null pointer passed */
    LLE_ERROR_BUFFER_OVERFLOW,          /**< Buffer size exceeded */
    LLE_ERROR_BUFFER_UNDERFLOW,         /**< Buffer size insufficient */
    LLE_ERROR_INVALID_STATE,            /**< Component in invalid state */
    LLE_ERROR_INVALID_RANGE,            /**< Value outside valid range */
    LLE_ERROR_INVALID_FORMAT,           /**< Data format validation failed */
    LLE_ERROR_INVALID_ENCODING,         /**< Text encoding validation failed */

    /* Memory management errors (1100-1199) */
    LLE_ERROR_OUT_OF_MEMORY = 1100,  /**< Memory allocation failed */
    LLE_ERROR_MEMORY_CORRUPTION,     /**< Memory corruption detected */
    LLE_ERROR_MEMORY_POOL_EXHAUSTED, /**< Specific pool exhausted */
    LLE_ERROR_MEMORY_LEAK_DETECTED,  /**< Memory leak detection triggered */
    LLE_ERROR_DOUBLE_FREE_DETECTED,  /**< Double free attempt detected */
    LLE_ERROR_USE_AFTER_FREE,        /**< Use after free detected */
    LLE_ERROR_MEMORY_ALIGNMENT,  /**< Memory alignment requirements violated */
    LLE_ERROR_MEMORY_PROTECTION, /**< Memory protection violation */

    /* System integration errors (1200-1299) */
    LLE_ERROR_SYSTEM_CALL = 1200,   /**< System call failed */
    LLE_ERROR_IO_ERROR,             /**< I/O operation failed */
    LLE_ERROR_TIMEOUT,              /**< Operation timed out */
    LLE_ERROR_INTERRUPT,            /**< Operation interrupted */
    LLE_ERROR_PERMISSION_DENIED,    /**< Permission denied */
    LLE_ERROR_RESOURCE_UNAVAILABLE, /**< System resource unavailable */
    LLE_ERROR_DEVICE_ERROR,         /**< Device or driver error */
    LLE_ERROR_NETWORK_ERROR,        /**< Network operation failed */

    /* Component-specific errors (1300-1399) */
    LLE_ERROR_BUFFER_COMPONENT = 1300,  /**< Buffer management error */
    LLE_ERROR_EVENT_SYSTEM,             /**< Event system error */
    LLE_ERROR_TERMINAL_ABSTRACTION,     /**< Terminal abstraction error */
    LLE_ERROR_INPUT_PARSING,            /**< Input parsing error */
    LLE_ERROR_HISTORY_SYSTEM,           /**< History management error */
    LLE_ERROR_AUTOSUGGESTIONS,          /**< Autosuggestions error */
    LLE_ERROR_SYNTAX_HIGHLIGHTING,      /**< Syntax highlighting error */
    LLE_ERROR_COMPLETION_SYSTEM,        /**< Tab completion error */
    LLE_ERROR_DISPLAY_INTEGRATION,      /**< Display integration error */
    LLE_ERROR_DISPLAY_SUBMISSION,       /**< Display submission error */
    LLE_ERROR_PERFORMANCE_MONITORING,   /**< Performance monitoring error */
    LLE_ERROR_NO_UNDO_AVAILABLE,        /**< No undo operation available */
    LLE_ERROR_NO_REDO_AVAILABLE,        /**< No redo operation available */
    LLE_ERROR_OPERATION_IN_PROGRESS,    /**< Operation already in progress */
    LLE_ERROR_NO_OPERATION_IN_PROGRESS, /**< No operation in progress */
    LLE_ERROR_INVALID_INPUT_EVENT,      /**< Invalid input event data */

    /* Feature and extensibility errors (1400-1499) */
    LLE_ERROR_FEATURE_DISABLED = 1400,  /**< Required feature disabled */
    LLE_ERROR_FEATURE_NOT_AVAILABLE,    /**< Feature not available */
    LLE_ERROR_PLUGIN_LOAD_FAILED,       /**< Plugin loading failed */
    LLE_ERROR_PLUGIN_INIT_FAILED,       /**< Plugin initialization failed */
    LLE_ERROR_PLUGIN_VALIDATION_FAILED, /**< Plugin validation failed */
    LLE_ERROR_DEPENDENCY_MISSING,       /**< Required dependency missing */
    LLE_ERROR_VERSION_MISMATCH,         /**< Version compatibility error */
    LLE_ERROR_API_MISMATCH,             /**< API compatibility error */
    LLE_ERROR_CONFIGURATION_INVALID,    /**< Configuration validation failed */
    LLE_ERROR_CONFIGURATION_MISSING,    /**< Required configuration missing */

    /* Performance and resource errors (1500-1599) */
    LLE_ERROR_PERFORMANCE_DEGRADED = 1500, /**< Performance below threshold */
    LLE_ERROR_RESOURCE_EXHAUSTED,          /**< Resource limit exceeded */
    LLE_ERROR_QUEUE_FULL,                  /**< Event queue full */
    LLE_ERROR_QUEUE_EMPTY,                 /**< Event queue empty */
    LLE_ERROR_NOT_FOUND,                   /**< Item not found */
    LLE_ERROR_ALREADY_INITIALIZED,         /**< Already initialized */
    LLE_ERROR_NOT_INITIALIZED,             /**< Not initialized */
    LLE_ERROR_CACHE_MISS,                  /**< Critical cache miss */
    LLE_ERROR_CACHE_CORRUPTED,             /**< Cache corruption detected */
    LLE_ERROR_THROTTLING_ACTIVE,           /**< Resource throttling active */
    LLE_ERROR_MONITORING_FAILURE,  /**< Performance monitoring failure */
    LLE_ERROR_OPTIMIZATION_FAILED, /**< Optimization attempt failed */
    LLE_ERROR_ALREADY_EXISTS,      /**< Item already exists */
    LLE_ERROR_DISABLED,            /**< Feature/widget is disabled */

    /* Critical system errors (1600-1699) */
    LLE_ERROR_INITIALIZATION_FAILED = 1600, /**< System initialization failed */
    LLE_ERROR_SHUTDOWN_FAILED,              /**< System shutdown failed */
    LLE_ERROR_STATE_CORRUPTION,             /**< Internal state corrupted */
    LLE_ERROR_INVARIANT_VIOLATION,          /**< Internal invariant violated */
    LLE_ERROR_ASSERTION_FAILED,             /**< Assertion failure */
    LLE_ERROR_FATAL_INTERNAL,               /**< Fatal internal error */
    LLE_ERROR_RECOVERY_FAILED,              /**< Error recovery failed */
    LLE_ERROR_DEGRADATION_LIMIT_REACHED     /**< Maximum degradation reached */
} lle_result_t;

/* ============================================================================
 * ERROR SEVERITY CLASSIFICATION
 * ============================================================================
 */

/**
 * @brief Error severity levels for classification and reporting
 */
typedef enum {
    LLE_SEVERITY_INFO,     /**< Informational, no action needed */
    LLE_SEVERITY_WARNING,  /**< Warning, monitoring recommended */
    LLE_SEVERITY_MINOR,    /**< Minor error, degraded functionality */
    LLE_SEVERITY_MAJOR,    /**< Major error, significant impact */
    LLE_SEVERITY_CRITICAL, /**< Critical error, immediate attention */
    LLE_SEVERITY_FATAL     /**< Fatal error, system shutdown required */
} lle_error_severity_t;

/* ============================================================================
 * ERROR HANDLING STATE MACHINE
 * ============================================================================
 */

/**
 * @brief Error handling states for state machine
 */
typedef enum {
    ERROR_STATE_NONE,              /**< No error state */
    ERROR_STATE_DETECTED,          /**< Error detected, analysis pending */
    ERROR_STATE_ANALYZING,         /**< Analyzing error and impact */
    ERROR_STATE_RECOVERY_PLANNING, /**< Planning recovery strategy */
    ERROR_STATE_RECOVERING,        /**< Executing recovery */
    ERROR_STATE_DEGRADING,         /**< Applying degradation strategy */
    ERROR_STATE_MONITORING,        /**< Monitoring post-recovery */
    ERROR_STATE_ESCALATING,        /**< Escalating to higher level */
    ERROR_STATE_CRITICAL           /**< Critical error state */
} lle_error_handling_state_t;

/* ============================================================================
 * COMPONENT-SPECIFIC ERROR CODES
 * ============================================================================
 */

/**
 * @brief Buffer Management specific errors
 */
typedef enum {
    LLE_BUFFER_ERROR_BASE = LLE_ERROR_BUFFER_COMPONENT, ///< Base code for buffer errors
    LLE_BUFFER_ERROR_INVALID_CURSOR_POSITION, /**< Cursor position invalid */
    LLE_BUFFER_ERROR_TEXT_ENCODING_INVALID,   /**< Text encoding error */
    LLE_BUFFER_ERROR_MULTILINE_CORRUPTION, /**< Multiline structure corrupted */
    LLE_BUFFER_ERROR_UNDO_STACK_OVERFLOW,  /**< Undo stack full */
    LLE_BUFFER_ERROR_REDO_UNAVAILABLE,     /**< No redo operations available */
    LLE_BUFFER_ERROR_CHANGE_TRACKING_FAILED, /**< Change tracking failure */
    LLE_BUFFER_ERROR_UTF8_VALIDATION_FAILED, /**< UTF-8 validation failed */
    LLE_BUFFER_ERROR_GRAPHEME_BOUNDARY_ERROR /**< Grapheme cluster boundary
                                                error */
} lle_buffer_error_t;

/**
 * @brief Event System specific errors
 */
typedef enum {
    LLE_EVENT_ERROR_BASE = LLE_ERROR_EVENT_SYSTEM, ///< Base code for event system errors
    LLE_EVENT_ERROR_QUEUE_OVERFLOW,              /**< Event queue overflow */
    LLE_EVENT_ERROR_INVALID_PRIORITY,            /**< Invalid event priority */
    LLE_EVENT_ERROR_HANDLER_REGISTRATION_FAILED, /**< Handler registration
                                                    failed */
    LLE_EVENT_ERROR_CIRCULAR_DEPENDENCY,   /**< Circular event dependency */
    LLE_EVENT_ERROR_DEADLOCK_DETECTED,     /**< Event processing deadlock */
    LLE_EVENT_ERROR_PROCESSING_TIMEOUT,    /**< Event processing timeout */
    LLE_EVENT_ERROR_INVALID_EVENT_TYPE,    /**< Unknown event type */
    LLE_EVENT_ERROR_SYNCHRONIZATION_FAILED /**< Event synchronization failed */
} lle_event_error_t;

/**
 * @brief Terminal Abstraction specific errors
 */
typedef enum {
    LLE_TERMINAL_ERROR_BASE = LLE_ERROR_TERMINAL_ABSTRACTION, ///< Base code for terminal errors
    LLE_TERMINAL_ERROR_CAPABILITY_DETECTION_FAILED, /**< Capability detection
                                                       failed */
    LLE_TERMINAL_ERROR_UNSUPPORTED_TERMINAL, /**< Terminal type unsupported */
    LLE_TERMINAL_ERROR_ESCAPE_SEQUENCE_INVALID,  /**< Invalid escape sequence */
    LLE_TERMINAL_ERROR_INPUT_SEQUENCE_MALFORMED, /**< Malformed input sequence
                                                  */
    LLE_TERMINAL_ERROR_OUTPUT_BUFFER_FULL, /**< Terminal output buffer full */
    LLE_TERMINAL_ERROR_TERMINFO_ACCESS_FAILED, /**< Terminfo database access
                                                  failed */
    LLE_TERMINAL_ERROR_SIGNAL_HANDLING_FAILED, /**< Terminal signal handling
                                                  error */
    LLE_TERMINAL_ERROR_RAW_MODE_FAILED         /**< Raw mode setup failed */
} lle_terminal_error_t;

/* ============================================================================
 * RECOVERY STRATEGY TYPES
 * ============================================================================
 */

/**
 * @brief Recovery strategy types for error recovery
 */
typedef enum {
    RECOVERY_STRATEGY_RETRY,           /**< Retry operation */
    RECOVERY_STRATEGY_ROLLBACK,        /**< Rollback to previous state */
    RECOVERY_STRATEGY_RESET_COMPONENT, /**< Reset component to clean state */
    RECOVERY_STRATEGY_FALLBACK_MODE,   /**< Switch to fallback mode */
    RECOVERY_STRATEGY_GRACEFUL_DEGRADATION, /**< Apply graceful degradation */
    RECOVERY_STRATEGY_RESTART_SUBSYSTEM,    /**< Restart entire subsystem */
    RECOVERY_STRATEGY_USER_INTERVENTION,    /**< Require user intervention */
    RECOVERY_STRATEGY_ESCALATION            /**< Escalate to higher level */
} lle_recovery_strategy_type_t;

/**
 * @brief System degradation levels
 */
typedef enum {
    DEGRADATION_LEVEL_NONE = 0,      /**< 100% functionality */
    DEGRADATION_LEVEL_MINIMAL = 10,  /**< 90% functionality */
    DEGRADATION_LEVEL_LOW = 25,      /**< 75% functionality */
    DEGRADATION_LEVEL_MODERATE = 50, /**< 50% functionality */
    DEGRADATION_LEVEL_HIGH = 75,     /**< 25% functionality */
    DEGRADATION_LEVEL_CRITICAL = 90, /**< 10% functionality */
    DEGRADATION_LEVEL_EMERGENCY = 95 /**< 5% functionality */
} lle_degradation_level_t;

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
    /* Primary error information */
    lle_result_t error_code;       /**< Primary error code */
    const char *error_message;     /**< Human-readable error message */
    const char *technical_details; /**< Technical details for debugging */

    /* Source location information */
    const char *function_name;  /**< Function where error occurred */
    const char *file_name;      /**< Source file name */
    int line_number;            /**< Line number in source */
    const char *component_name; /**< LLE component name */

    /* Execution context */
    uint64_t thread_id;         /**< Thread identifier */
    uint64_t timestamp_ns;      /**< Error timestamp (nanoseconds) */
    uint64_t operation_id;      /**< Unique operation identifier */
    const char *operation_name; /**< Operation being performed */

    /* System state information */
    size_t memory_usage_bytes;      /**< Current memory usage */
    size_t memory_pool_utilization; /**< Memory pool utilization percentage */
    uint32_t active_components;     /**< Bitmask of active components */
    uint32_t system_load_factor;    /**< Current system load (0-100) */

    /* Error chain and causality */
    struct lle_error_context *root_cause;      /**< Root cause error */
    struct lle_error_context *immediate_cause; /**< Immediate cause error */
    uint32_t error_chain_depth;                /**< Depth in error chain */

    /* Recovery and handling information */
    uint32_t recovery_attempts;      /**< Number of recovery attempts made */
    uint32_t degradation_level;      /**< Current system degradation level */
    bool auto_recovery_possible;     /**< Whether auto-recovery is possible */
    bool user_intervention_required; /**< Whether user intervention needed */

    /* Performance impact */
    uint64_t performance_impact_ns; /**< Performance impact measurement */
    bool critical_path_affected;    /**< Whether critical path affected */

    /* Custom context data */
    void *context_data;       /**< Component-specific context data */
    size_t context_data_size; /**< Size of context data */
    void (*context_data_cleanup)(
        void *data); /**< Cleanup function for context data */
} lle_error_context_t;

/**
 * @brief Error handling state machine
 *
 * Tracks the current state of error handling process including state
 * transitions, timing, and state-specific data.
 */
typedef struct lle_error_state_machine {
    lle_error_handling_state_t current_state;  /**< Current state */
    lle_error_handling_state_t previous_state; /**< Previous state */
    uint64_t state_entry_time_ns;    /**< Time entered current state */
    uint64_t total_handling_time_ns; /**< Total time in error handling */
    uint32_t state_transitions;      /**< Number of state transitions */

    /* State-specific data union */
    union {
        struct {
            uint32_t analysis_progress; /**< Analysis progress percentage */
            bool impact_assessment_complete; /**< Impact assessment done */
        } analyzing;

        struct {
            uint32_t strategy_score;             /**< Selected strategy score */
            bool degradation_required;           /**< Degradation needed */
            uint32_t estimated_recovery_time_ms; /**< Estimated recovery time */
        } planning;

        struct {
            uint32_t recovery_progress;    /**< Recovery progress percentage */
            uint32_t attempted_strategies; /**< Number of strategies tried */
            bool partial_success;          /**< Partial recovery success */
        } recovering;

        struct {
            uint32_t monitoring_duration_ms;   /**< Monitoring duration */
            bool stability_confirmed;          /**< Stability confirmed */
            uint32_t performance_recovery_pct; /**< Performance recovery
                                                  percentage */
        } monitoring;
    } state_data;
} lle_error_state_machine_t;

/* ============================================================================
 * ERROR REPORTING CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Function pointer type for error reporting callbacks
 */
typedef void (*lle_error_reporter_t)(const lle_error_context_t *context,
                                     void *user_data);

/**
 * @brief Error reporting configuration
 *
 * Controls how errors are reported through various channels including
 * console, log files, system log, and callbacks.
 */
typedef struct lle_error_reporting_config {
    /* Reporting targets */
    bool console_reporting_enabled;    /**< Enable console reporting */
    bool log_file_reporting_enabled;   /**< Enable log file reporting */
    bool system_log_reporting_enabled; /**< Enable system log reporting */
    bool callback_reporting_enabled;   /**< Enable callback reporting */

    /* Reporting filters */
    lle_error_severity_t
        min_console_severity; /**< Minimum severity for console */
    lle_error_severity_t
        min_log_file_severity; /**< Minimum severity for log file */
    lle_error_severity_t
        min_system_log_severity; /**< Minimum severity for system log */
    lle_error_severity_t
        min_callback_severity; /**< Minimum severity for callback */

    /* Configuration */
    const char *log_file_path;   /**< Path to log file */
    size_t max_log_file_size;    /**< Maximum log file size */
    uint32_t log_rotation_count; /**< Number of rotated logs to keep */

    /* Callback */
    lle_error_reporter_t error_callback; /**< Error reporting callback */
    void *callback_user_data;            /**< User data for callback */

    /* Performance settings */
    bool async_reporting;              /**< Use async reporting */
    uint32_t reporting_queue_size;     /**< Size of async reporting queue */
    uint64_t max_reporting_latency_ns; /**< Maximum reporting latency */
} lle_error_reporting_config_t;

/**
 * @brief Error reporting system state
 *
 * Manages the complete error reporting infrastructure including log files,
 * async queues, statistics, and suppression.
 */
typedef struct lle_error_reporting_system {
    lle_error_reporting_config_t config; /**< Reporting configuration */

    /* Infrastructure */
    FILE *log_file;             /**< Open log file handle */
    void *async_queue;          /**< Async reporting queue (circular buffer) */
    pthread_t reporting_thread; /**< Async reporting thread */
    pthread_mutex_t reporting_mutex; /**< Reporting mutex */

    /* Statistics */
    uint64_t total_errors_reported; /**< Total errors reported */
    uint64_t
        errors_by_severity[LLE_SEVERITY_FATAL + 1]; /**< Errors by severity */
    uint64_t avg_reporting_latency_ns; /**< Average reporting latency */
    uint64_t max_reporting_latency_ns; /**< Maximum reporting latency */

    /* Suppression */
    void *error_suppression_table; /**< Error suppression hashtable */
    uint32_t max_duplicate_errors_per_minute; /**< Max duplicates per minute */
} lle_error_reporting_system_t;

/* ============================================================================
 * RECOVERY STRATEGY STRUCTURES
 * ============================================================================
 */

/**
 * @brief Recovery strategy definition
 *
 * Defines a complete recovery strategy including type, parameters,
 * success probability, cost, and execution function.
 */
typedef struct lle_recovery_strategy {
    lle_recovery_strategy_type_t type; /**< Strategy type */
    const char *strategy_name;         /**< Strategy name */
    const char *description;           /**< Strategy description */

    /* Parameters */
    uint32_t max_attempts;   /**< Maximum retry attempts */
    uint64_t retry_delay_ms; /**< Delay between retries */
    uint64_t timeout_ms;     /**< Strategy timeout */

    /* Success probability and cost */
    float success_probability;  /**< Estimated success probability (0-1) */
    uint64_t estimated_cost_ns; /**< Estimated execution cost */
    uint32_t degradation_level; /**< Degradation level if applied */

    /* Prerequisites */
    uint32_t required_resources;     /**< Required resource bitmask */
    bool requires_user_confirmation; /**< Requires user confirmation */
    bool affects_critical_path;      /**< Affects critical path */

    /* Implementation */
    lle_result_t (*execute_strategy)(
        const lle_error_context_t *error_context,
        void *strategy_data);  /**< Execution function */
    void *strategy_data;       /**< Strategy-specific data */
    size_t strategy_data_size; /**< Size of strategy data */
} lle_recovery_strategy_t;

/* ============================================================================
 * DEGRADATION MANAGEMENT STRUCTURES
 * ============================================================================
 */

/**
 * @brief Feature degradation mapping
 *
 * Maps features to degradation levels, defining when features should be
 * disabled and how to restore them.
 */
typedef struct lle_feature_degradation_map {
    const char *feature_name;                 /**< Feature name */
    lle_degradation_level_t disable_at_level; /**< Level at which to disable */
    bool is_critical_feature;         /**< Whether feature is critical */
    const char *fallback_description; /**< Fallback description */

    /* Degradation functions */
    /** Apply degradation to the feature at the given level */
    lle_result_t (*apply_degradation)(uint32_t degradation_level,
                                      void *feature_data);
    /** Restore the feature from a degraded state */
    lle_result_t (*restore_feature)(void *feature_data);
} lle_feature_degradation_map_t;

/**
 * @brief Degradation controller
 *
 * Controls system degradation including current level, feature mapping,
 * statistics, and recovery monitoring.
 */
typedef struct lle_degradation_controller {
    lle_degradation_level_t current_level;  /**< Current degradation level */
    lle_degradation_level_t previous_level; /**< Previous degradation level */
    uint64_t degradation_start_time_ns;     /**< Time degradation started */

    /* Feature mapping */
    lle_feature_degradation_map_t
        *feature_map;         /**< Feature degradation mappings */
    size_t feature_map_count; /**< Number of feature mappings */

    /* Statistics */
    uint64_t degradation_events;          /**< Number of degradation events */
    uint64_t total_degraded_time_ns;      /**< Total time in degraded state */
    uint64_t automatic_recovery_attempts; /**< Automatic recovery attempts */
    uint64_t successful_recoveries;       /**< Successful recoveries */

    /* Recovery monitoring */
    bool recovery_in_progress;          /**< Recovery in progress */
    uint64_t recovery_start_time_ns;    /**< Recovery start time */
    uint32_t recovery_progress_percent; /**< Recovery progress percentage */
} lle_degradation_controller_t;

/**
 * @brief Circuit breaker for component errors
 *
 * Implements circuit breaker pattern to prevent cascade failures.
 */
typedef struct lle_event_circuit_breaker {
    uint32_t failure_count;        /**< Number of failures */
    uint32_t failure_threshold;    /**< Failure threshold */
    uint64_t last_failure_time_ns; /**< Last failure timestamp */
    uint64_t timeout_duration_ns;  /**< Timeout duration */
    bool is_open;                  /**< Circuit breaker is open */
} lle_event_circuit_breaker_t;

/* ============================================================================
 * MEMORY-SAFE ERROR CONTEXT
 * ============================================================================
 */

/**
 * @brief Memory-safe error context with resource tracking
 *
 * Extends error context with memory tracking and automatic cleanup.
 */
typedef struct lle_memory_safe_error_context {
    lle_error_context_t base_context; /**< Base error context */

    /* Memory tracking */
    struct lle_memory_pool *error_pool; /**< Error memory pool */
    void **allocated_resources;         /**< Allocated resources array */
    size_t allocated_count;             /**< Number of allocated resources */
    size_t allocated_capacity;          /**< Capacity of resources array */

    /* Cleanup functions */
    void (**cleanup_functions)(void *); /**< Cleanup function array */
    size_t cleanup_count;               /**< Number of cleanup functions */

    /* Memory protection */
    uint32_t magic_header; /**< Magic header for validation */
    uint32_t magic_footer; /**< Magic footer for validation */
} lle_memory_safe_error_context_t;

/* ============================================================================
 * FORENSIC LOGGING STRUCTURES
 * ============================================================================
 */

#define LLE_MAX_STACK_FRAMES 64 /**< Maximum stack trace frames */

/**
 * @brief Forensic log entry with complete system state
 *
 * Contains comprehensive information for forensic analysis including
 * system snapshot, stack trace, component state dumps, and recovery log.
 */
typedef struct lle_forensic_log_entry {
    lle_error_context_t error_context; /**< Error context */

    /* System snapshot */
    struct {
        uint64_t total_memory_usage;     /**< Total memory usage */
        uint64_t peak_memory_usage;      /**< Peak memory usage */
        uint32_t active_components_mask; /**< Active components bitmask */
        uint32_t thread_count;           /**< Number of threads */
        float cpu_usage_percent;         /**< CPU usage percentage */
        uint64_t avg_response_time_ns;   /**< Average response time */
        uint64_t max_response_time_ns;   /**< Maximum response time */
        uint32_t operations_per_second;  /**< Operations per second */
        uint32_t cache_hit_rate_percent; /**< Cache hit rate percentage */
    } system_snapshot;

    /* Stack trace */
    struct {
        void *stack_frames[LLE_MAX_STACK_FRAMES]; /**< Stack frame pointers */
        size_t frame_count;        /**< Number of frames captured */
        char **symbol_names;       /**< Symbol names for frames */
        bool stack_trace_complete; /**< Stack trace is complete */
    } stack_trace;

    /* Component state dumps */
    struct {
        char *buffer_state_dump;       /**< Buffer state dump */
        char *event_system_state_dump; /**< Event system state dump */
        char *terminal_state_dump;     /**< Terminal state dump */
        char *memory_pool_state_dump;  /**< Memory pool state dump */
        size_t total_state_dump_size;  /**< Total dump size */
    } component_state;

    /* Recovery log */
    struct {
        lle_recovery_strategy_t
            attempted_strategies[10];    /**< Attempted strategies */
        uint32_t strategy_count;         /**< Number of strategies attempted */
        bool recovery_successful;        /**< Recovery was successful */
        uint64_t total_recovery_time_ns; /**< Total recovery time */
    } recovery_log;
} lle_forensic_log_entry_t;

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
    _Atomic uint64_t total_errors_handled;  /**< Total errors handled */
    _Atomic uint64_t critical_errors_count; /**< Critical errors count */
    _Atomic uint64_t warnings_count;        /**< Warnings count */
    _Atomic uint64_t recoveries_successful; /**< Successful recoveries */
    _Atomic uint64_t recoveries_failed;     /**< Failed recoveries */
    _Atomic uint32_t active_error_contexts; /**< Active error contexts */
    _Atomic uint32_t
        preallocated_contexts_used; /**< Pre-allocated contexts in use */
    _Atomic uint64_t total_recovery_time_ns; /**< Total recovery time */
    _Atomic uint64_t max_recovery_time_ns;   /**< Maximum recovery time */
    _Atomic uint32_t concurrent_errors;      /**< Concurrent errors */
} lle_error_atomic_counters_t;

/* ============================================================================
 * ERROR INJECTION AND TESTING
 * ============================================================================
 */

/**
 * @brief Error injection configuration for testing
 *
 * Controls error injection for testing error handling paths.
 */
typedef struct lle_error_injection_config {
    bool injection_enabled;           /**< Injection enabled */
    float injection_probability;      /**< Injection probability (0-1) */
    lle_result_t *target_error_codes; /**< Target error codes array */
    size_t target_error_count;        /**< Number of target errors */
    const char **target_components;   /**< Target component names */
    size_t target_component_count;    /**< Number of target components */

    uint64_t injection_interval_ns;  /**< Minimum injection interval */
    uint64_t last_injection_time_ns; /**< Last injection time */

    /* Statistics */
    uint64_t total_injections;      /**< Total injections */
    uint64_t successful_recoveries; /**< Successful recoveries */
    uint64_t failed_recoveries;     /**< Failed recoveries */
} lle_error_injection_config_t;

/**
 * @brief Error validation test definition
 *
 * Defines a single error handling validation test.
 */
typedef struct lle_error_validation_test {
    const char *test_name;        /**< Test name */
    lle_result_t target_error;    /**< Target error to test */
    const char *target_component; /**< Target component */

    bool should_recover_automatically; /**< Should recover automatically */
    lle_degradation_level_t
        expected_degradation;      /**< Expected degradation level */
    uint64_t max_recovery_time_ns; /**< Maximum recovery time */

    /* Test functions */
    /** Prepare test fixtures and state before execution */
    lle_result_t (*setup_test)(void *test_context);
    /** Execute the validation test body */
    lle_result_t (*execute_test)(void *test_context);
    /** Validate the result returned by the test */
    lle_result_t (*validate_result)(void *test_context, lle_result_t result);
    /** Tear down test fixtures and release resources */
    lle_result_t (*cleanup_test)(void *test_context);
} lle_error_validation_test_t;

/* ============================================================================
 * FUNCTION DECLARATIONS
 * ============================================================================
 * All implementations are in src/lle/error_handling.c (Layer 1)
 */

/* Error Context Management */
/**
 * @brief Create a new error context populated with the supplied fields
 * @param error_code Primary error code
 * @param message Human-readable error message
 * @param function Function name where the error occurred
 * @param file Source file name
 * @param line Line number in source
 * @param component LLE component name
 * @return New error context on success, NULL on failure
 */
lle_error_context_t *lle_create_error_context(lle_result_t error_code,
                                              const char *message,
                                              const char *function,
                                              const char *file, int line,
                                              const char *component);

#define LLE_CREATE_ERROR_CONTEXT(code, message, component)                     \
    lle_create_error_context(code, message, __func__, __FILE__, __LINE__,      \
                             component)

/**
 * @brief Destroy an error context and release its resources
 * @param ctx Error context to destroy
 */
void lle_error_context_destroy(lle_error_context_t *ctx);

/**
 * @brief Allocate an error context from the fast pre-allocated pool
 * @return New error context on success, NULL on failure
 */
lle_error_context_t *lle_allocate_fast_error_context(void);

/**
 * @brief Release a fast-pool error context back to the pool
 * @param ctx Error context to release
 */
void lle_release_fast_error_context(lle_error_context_t *ctx);

/**
 * @brief Initialize a memory-safe error context
 * @param ctx Memory-safe error context to initialize
 */
void lle_init_memory_safe_error_context(lle_memory_safe_error_context_t *ctx);

/**
 * @brief Cleanup a memory-safe error context and release tracked resources
 * @param ctx Memory-safe error context to clean up
 */
void lle_cleanup_memory_safe_error_context(
    lle_memory_safe_error_context_t *ctx);

/* Error Severity */
/**
 * @brief Determine the severity of an error using its code and context
 * @param error_code Error code to classify
 * @param context Optional error context for refinement
 * @return Severity level
 */
lle_error_severity_t
lle_determine_error_severity(lle_result_t error_code,
                             const lle_error_context_t *context);

/**
 * @brief Fast severity classification from error code alone
 * @param error_code Error code to classify
 * @return Severity level
 */
lle_error_severity_t lle_fast_determine_severity(lle_result_t error_code);

/* Error Reporting */
/**
 * @brief Report an error through all configured reporting channels
 * @param context Error context to report
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_report_error(const lle_error_context_t *context);

/**
 * @brief Report an error to the console
 * @param context Error context to report
 */
void lle_report_error_to_console(const lle_error_context_t *context);

/**
 * @brief Report an error to the configured log file
 * @param system Reporting system holding the log file handle
 * @param context Error context to report
 */
void lle_report_error_to_log_file(lle_error_reporting_system_t *system,
                                  const lle_error_context_t *context);

/**
 * @brief Report an error to the system log
 * @param context Error context to report
 */
void lle_report_error_to_system_log(const lle_error_context_t *context);

/**
 * @brief Fast path for reporting critical errors
 * @param ctx Error context to report
 */
void lle_fast_report_critical_error(const lle_error_context_t *ctx);

/**
 * @brief Check if an error should be suppressed by the suppression table
 * @param system Reporting system holding the suppression table
 * @param context Error context to check
 * @return true if the error should be suppressed, false otherwise
 */
bool lle_should_suppress_error(lle_error_reporting_system_t *system,
                               const lle_error_context_t *context);

/* Recovery Strategy */
/**
 * @brief Select the best recovery strategy for an error context
 * @param error_context Error context to recover from
 * @return Selected recovery strategy on success, NULL on failure
 */
lle_recovery_strategy_t *
lle_select_recovery_strategy(const lle_error_context_t *error_context);

/**
 * @brief Retrieve all candidate recovery strategies for a given error code
 * @param error_code Error code to look up
 * @param strategies Output pointer receiving the strategies array
 * @param strategy_count Output pointer receiving the number of strategies
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_get_recovery_strategies_for_error(lle_result_t error_code,
                                      lle_recovery_strategy_t **strategies,
                                      size_t *strategy_count);

/* Degradation Control */
/**
 * @brief Apply a system degradation level via the degradation controller
 * @param controller Degradation controller
 * @param target_level Degradation level to apply
 * @param reason Human-readable reason for the degradation
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_apply_degradation(lle_degradation_controller_t *controller,
                                   lle_degradation_level_t target_level,
                                   const char *reason);

/**
 * @brief Log a degradation event for diagnostics
 * @param level Degradation level applied
 * @param reason Human-readable reason for the degradation
 */
void lle_log_degradation_event(lle_degradation_level_t level,
                               const char *reason);

/* Component-Specific Error Handlers */
/**
 * @brief Handle a buffer subsystem error
 * @param buffer Buffer instance the error occurred in
 * @param error Buffer-specific error code
 * @param error_context Associated error context
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_handle_buffer_error(void *buffer, lle_buffer_error_t error,
                                     const void *error_context);

/**
 * @brief Handle an event system error, updating the circuit breaker
 * @param event_system Event system instance the error occurred in
 * @param error Event-specific error code
 * @param breaker Circuit breaker tracking failure thresholds
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_handle_event_system_error(void *event_system, lle_event_error_t error,
                              lle_event_circuit_breaker_t *breaker);

/* Memory Integration */
/**
 * @brief Initialize the dedicated memory pools used by the error subsystem
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_init_error_memory_pools(void);

/**
 * @brief Allocate memory from the error subsystem's memory pool
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory on success, NULL on failure
 */
void *lle_error_pool_alloc(size_t size);

/**
 * @brief Duplicate a string into the error subsystem's string pool
 * @param str Null-terminated source string
 * @return Pointer to the duplicated string on success, NULL on failure
 */
char *lle_error_string_pool_strdup(const char *str);

/* Forensic Logging */
/**
 * @brief Create a forensic log entry capturing system state for an error
 * @param error_context Error context to capture
 * @param log_entry Output pointer receiving the new forensic log entry
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_create_forensic_log_entry(const lle_error_context_t *error_context,
                              lle_forensic_log_entry_t **log_entry);

/**
 * @brief Generate a technical details string for an error code
 * @param error_code Error code to describe
 * @return Pointer to a technical details string (static or pool-owned)
 */
const char *lle_generate_technical_details(lle_result_t error_code);

/* Performance-Critical Path */
/**
 * @brief Handle an error on the performance-critical execution path
 * @param error_code Error code that occurred
 * @param component Component name where the error occurred
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_handle_critical_path_error(lle_result_t error_code,
                                            const char *component);

/* Timing Functions */
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

/* Atomic Operations */
/**
 * @brief Atomically increment an error statistics counter
 * @param counter Pointer to the atomic counter
 */
void lle_error_increment_counter(_Atomic uint64_t *counter);

/**
 * @brief Atomically read an error statistics counter
 * @param counter Pointer to the atomic counter
 * @return Current counter value
 */
uint64_t lle_error_read_counter(_Atomic uint64_t *counter);

/**
 * @brief Atomically update a maximum-time counter if new_time is greater
 * @param max_time Pointer to the atomic maximum-time counter
 * @param new_time Candidate new maximum time
 */
void lle_error_update_max_time(_Atomic uint64_t *max_time, uint64_t new_time);

/**
 * @brief Try to atomically acquire a pre-allocated error context slot
 * @return true if a slot was acquired, false otherwise
 */
bool lle_error_try_acquire_context_atomic(void);

/**
 * @brief Atomically release a pre-allocated error context slot
 */
void lle_error_release_context_atomic(void);

/**
 * @brief Update error statistics using lock-free atomic operations
 * @param error_code Error code that occurred
 * @param severity Severity classification
 * @param recovery_time_ns Time taken for recovery in nanoseconds
 * @param recovery_successful Whether recovery succeeded
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_error_update_statistics_lockfree(lle_result_t error_code,
                                                  lle_error_severity_t severity,
                                                  uint64_t recovery_time_ns,
                                                  bool recovery_successful);

/* System State */
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

/* Testing and Validation */
/**
 * @brief Possibly inject a test error based on injection configuration
 * @param component Component name being tested
 * @param operation Operation name being tested
 * @return Injected error code, or LLE_SUCCESS when no injection occurs
 */
lle_result_t lle_maybe_inject_error(const char *component,
                                    const char *operation);

#define LLE_INJECT_ERROR(component, operation)                                 \
    do {                                                                       \
        lle_result_t injected = lle_maybe_inject_error(component, operation);  \
        if (injected != LLE_SUCCESS)                                           \
            return injected;                                                   \
    } while (0)

/**
 * @brief Log an error injection event for diagnostics
 * @param component Component name where injection occurred
 * @param operation Operation name where injection occurred
 * @param error_code Injected error code
 */
void lle_log_error_injection(const char *component, const char *operation,
                             lle_result_t error_code);

/**
 * @brief Run the full error-handling validation test suite
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_run_error_handling_validation_suite(void);

/**
 * @brief Run a single error-handling validation test
 * @param test Validation test definition to execute
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_individual_validation_test(const lle_error_validation_test_t *test);

#endif /* LLE_ERROR_HANDLING_H */
