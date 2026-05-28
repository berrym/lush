/**
 * @file performance.h
 * @brief LLE Performance Optimization System - Type Definitions and Function
 * Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 14 - Performance Optimization Complete Specification
 * Version: 1.0.0
 *
 * This header contains ALL type definitions and function declarations for the
 * LLE performance optimization system. NO implementations are included here.
 *
 * Layer 0: Type Definitions Only
 * Layer 1: Implementations in src/lle/performance.c (separate file)
 */

#ifndef LLE_PERFORMANCE_H
#define LLE_PERFORMANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/// Include LLE dependencies
#include "lle/error_handling.h"
#include "lle/memory_management.h"

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

/// Forward declarations for complex types
typedef struct lle_performance_monitor_t lle_performance_monitor_t;
typedef struct lle_cache_manager_t lle_cache_manager_t;
typedef struct lle_profiler_t lle_profiler_t;
typedef struct lle_resource_manager_t lle_resource_manager_t;
typedef struct lle_performance_dashboard_t lle_performance_dashboard_t;

/// Forward declarations from other LLE systems
typedef struct lle_terminal_abstraction_t lle_terminal_abstraction_t;
typedef struct lle_buffer_manager_t lle_buffer_manager_t;
typedef struct lle_event_system lle_event_system_t;
typedef struct lle_display_integration_t lle_display_integration_t;
typedef struct lle_history_system_t lle_history_system_t;
typedef struct lle_autosuggestions_t lle_autosuggestions_t;
typedef struct lle_syntax_highlighting_t lle_syntax_highlighting_t;
typedef struct lle_completion_system lle_completion_system_t;
typedef struct lle_user_customization_t lle_user_customization_t;

/// External library forward declarations
typedef struct ht ht_t;
typedef ht_t lle_hashtable_t;

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/// Performance measurement limits
#define LLE_PERF_MAX_MEASUREMENTS 1024
#define LLE_PERF_OPERATION_COUNT 64
#define LLE_PERF_MAX_ALERTS 32

/// Performance targets (nanoseconds)
#define LLE_PERF_TARGET_RESPONSE_TIME_NS 500000ULL /// 500µs
#define LLE_PERF_MONITORING_OVERHEAD_NS 10000ULL   /// 10µs

/// Cache configuration
#define LLE_CACHE_NAME_MAX 64
#define LLE_CACHE_EVICTION_SCAN_DEPTH 10
#define LLE_CACHE_OPT_HISTORY_SIZE 100
#define LLE_CACHE_ENTRY_FLAG_VALID 0x01

/// Memory pattern analysis
#define LLE_MEMORY_PATTERN_SIZE_COUNT 32
#define LLE_MEMORY_PATTERN_TIME_WINDOW 1000
#define LLE_MEMORY_POOL_TYPE_COUNT 8

/// Profiler configuration
#define LLE_PROFILER_MAX_CHILDREN 32
#define LLE_PROFILER_MAX_HOT_SPOTS 20
#define LLE_PROFILER_FUNCTION_KEY_MAX 256
#define LLE_PROFILER_HOT_SPOT_THRESHOLD_NS 100000ULL       /// 100µs
#define LLE_PROFILER_HOT_SPOT_CPU_THRESHOLD 5.0            /// 5% CPU
#define LLE_PROFILER_HOT_SPOT_TIME_THRESHOLD_NS 1000000ULL /// 1ms
#define LLE_PROFILER_HOT_SPOT_CALL_THRESHOLD 10000
#define LLE_PROFILER_HIGH_FREQUENCY_THRESHOLD 1000
#define LLE_PROFILER_LONG_RUNNING_THRESHOLD_NS 10000000ULL /// 10ms
#define LLE_PROFILER_INVALID_CALL_ID ((lle_profiler_call_id_t)0)

/// Performance test configuration
#define LLE_PERF_TEST_NAME_MAX 128
#define LLE_PERF_ANALYSIS_WINDOW_SECONDS 60

/// Performance component count
#define LLE_COMPONENT_COUNT 16

/// Performance monitoring macros (conditional compilation)
#ifdef LLE_PERFORMANCE_MONITORING_ENABLED

#define LLE_PERF_MEASURE_START(monitor, op_type, op_name, context, id_var)     \
    lle_perf_measurement_id_t id_var;                                          \
    lle_perf_measurement_start(monitor, op_type, op_name, context, &id_var)

#define LLE_PERF_MEASURE_END(monitor, id_var, result_code)                     \
    lle_perf_measurement_end(monitor, id_var, result_code)

#define LLE_PERF_MEASURE_SCOPE(monitor, op_type, op_name, context)             \
    LLE_PERF_MEASURE_START(monitor, op_type, op_name, context,                 \
                           __perf_measurement_id);                             \
    lle_perf_scope_guard_t __perf_guard = {monitor, __perf_measurement_id};    \
    (void)__perf_guard

#else

#define LLE_PERF_MEASURE_START(monitor, op_type, op_name, context, id_var)     \
    ((void)0)
#define LLE_PERF_MEASURE_END(monitor, id_var, result_code) ((void)0)
#define LLE_PERF_MEASURE_SCOPE(monitor, op_type, op_name, context) ((void)0)

#endif

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Performance operation types for classification
 */
typedef enum {
    LLE_PERF_OP_TERMINAL_INPUT = 0,
    LLE_PERF_OP_TERMINAL_OUTPUT,
    LLE_PERF_OP_BUFFER_INSERT,
    LLE_PERF_OP_BUFFER_DELETE,
    LLE_PERF_OP_BUFFER_SEARCH,
    LLE_PERF_OP_EVENT_PROCESSING,
    LLE_PERF_OP_EVENT_DISPATCH,
    LLE_PERF_OP_DISPLAY_RENDER,
    LLE_PERF_OP_DISPLAY_UPDATE,
    LLE_PERF_OP_HISTORY_SEARCH,
    LLE_PERF_OP_HISTORY_ADD,
    LLE_PERF_OP_COMPLETION_SEARCH,
    LLE_PERF_OP_COMPLETION_GENERATE,
    LLE_PERF_OP_SYNTAX_HIGHLIGHT,
    LLE_PERF_OP_AUTOSUGGESTION,
    LLE_PERF_OP_CACHE_LOOKUP,
    LLE_PERF_OP_CACHE_INSERT,
    LLE_PERF_OP_CACHE_EVICTION,
    LLE_PERF_OP_CACHE_OPTIMIZATION,
    LLE_PERF_OP_MEMORY_ALLOC,
    LLE_PERF_OP_MEMORY_FREE,
    LLE_PERF_OP_MEMORY_OPTIMIZATION,
    LLE_PERF_OP_RESOURCE_MONITORING,
    LLE_PERF_OP_PROFILER_ANALYSIS,
    LLE_PERF_OP_DASHBOARD_UPDATE,
    LLE_PERF_OP_CUSTOM,
    LLE_PERF_OP_COUNT
} lle_perf_operation_type_t;

/**
 * @brief Performance monitoring levels
 */
typedef enum {
    LLE_PERF_MONITORING_DISABLED = 0,
    LLE_PERF_MONITORING_MINIMAL,
    LLE_PERF_MONITORING_NORMAL,
    LLE_PERF_MONITORING_DETAILED,
    LLE_PERF_MONITORING_EXHAUSTIVE
} lle_perf_monitoring_level_t;

/**
 * @brief Performance result codes
 */
typedef enum {
    LLE_PERF_RESULT_SUCCESS = 0,
    LLE_PERF_RESULT_WARNING,
    LLE_PERF_RESULT_ERROR,
    LLE_PERF_RESULT_TIMEOUT,
    LLE_PERF_RESULT_CANCELLED
} lle_perf_result_t;

/**
 * @brief Performance trend types
 */
typedef enum {
    LLE_PERF_TREND_UNKNOWN = 0,
    LLE_PERF_TREND_STABLE,
    LLE_PERF_TREND_IMPROVING,
    LLE_PERF_TREND_DEGRADING,
    LLE_PERF_TREND_VOLATILE
} lle_perf_trend_t;

/**
 * @brief Cache tier definitions
 */
typedef enum {
    LLE_CACHE_TIER_L1_CPU = 0, ///< CPU cache optimized
    LLE_CACHE_TIER_L2_MEMORY,  ///< Memory cache
    LLE_CACHE_TIER_L3_STORAGE, ///< Storage cache
    LLE_CACHE_TIER_COUNT
} lle_cache_tier_t;

/**
 * @brief Cache eviction policy
 */
typedef enum {
    LLE_CACHE_POLICY_LRU = 0, ///< Least Recently Used
    LLE_CACHE_POLICY_LFU,     ///< Least Frequently Used
    LLE_CACHE_POLICY_FIFO,    ///< First In First Out
    LLE_CACHE_POLICY_ADAPTIVE /// Adaptive based on access patterns
} lle_cache_policy_t;

/**
 * @brief Cache consistency levels
 */
typedef enum {
    LLE_CACHE_CONSISTENCY_EVENTUAL = 0,
    LLE_CACHE_CONSISTENCY_STRONG,
    LLE_CACHE_CONSISTENCY_IMMEDIATE
} lle_cache_consistency_t;

/**
 * @brief Memory pool types
 */
typedef enum {
    LLE_PERF_MEMORY_POOL_PRIMARY = 0,
    LLE_PERF_MEMORY_POOL_EVENT,
    LLE_PERF_MEMORY_POOL_CACHE,
    LLE_PERF_MEMORY_POOL_BUFFER,
    LLE_PERF_MEMORY_POOL_PROFILER,
    LLE_PERF_MEMORY_POOL_TEMPORARY,
    LLE_PERF_MEMORY_POOL_CUSTOM_1,
    LLE_PERF_MEMORY_POOL_CUSTOM_2
} lle_perf_memory_pool_type_t;

/**
 * @brief Profiler modes
 */
typedef enum {
    LLE_PROFILER_MODE_DISABLED = 0,
    LLE_PROFILER_MODE_SAMPLING,
    LLE_PROFILER_MODE_INSTRUMENTATION,
    LLE_PROFILER_MODE_HYBRID
} lle_profiler_mode_t;

/**
 * @brief Performance test types
 */
typedef enum {
    LLE_PERF_TEST_LATENCY = 0,
    LLE_PERF_TEST_THROUGHPUT,
    LLE_PERF_TEST_MEMORY,
    LLE_PERF_TEST_CPU,
    LLE_PERF_TEST_STRESS,
    LLE_PERF_TEST_REGRESSION
} lle_perf_test_type_t;

/**
 * @brief Component types for performance tracking
 */
typedef enum {
    LLE_COMPONENT_TERMINAL = 0,
    LLE_COMPONENT_BUFFER,
    LLE_COMPONENT_EVENT,
    LLE_COMPONENT_DISPLAY,
    LLE_COMPONENT_HISTORY,
    LLE_COMPONENT_COMPLETION,
    LLE_COMPONENT_SYNTAX,
    LLE_COMPONENT_AUTOSUGGESTION,
    LLE_COMPONENT_CACHE,
    LLE_COMPONENT_MEMORY,
    LLE_COMPONENT_PROFILER,
    LLE_COMPONENT_RESOURCE,
    LLE_COMPONENT_DASHBOARD,
    LLE_COMPONENT_CUSTOMIZATION,
    LLE_COMPONENT_INTEGRATION,
    LLE_COMPONENT_OTHER
} lle_component_type_t;

/**
 * @brief Target status enumeration
 */
typedef enum {
    LLE_PERF_TARGET_UNKNOWN = 0,
    LLE_PERF_TARGET_MET,
    LLE_PERF_TARGET_WARNING,
    LLE_PERF_TARGET_CRITICAL,
    LLE_PERF_TARGET_FAILED
} lle_perf_target_status_enum_t;

/* ============================================================================
 * TYPE ALIASES
 * ============================================================================
 */

/**
 * @brief Performance measurement ID type
 */
typedef uint32_t lle_perf_measurement_id_t;

/**
 * @brief Profiler call ID type
 */
typedef uintptr_t lle_profiler_call_id_t;

/**
 * @brief Cache key and value types (opaque handles)
 */
typedef struct lle_cache_key_t lle_cache_key_t;
typedef struct lle_cache_value_t lle_cache_value_t;
typedef struct lle_cache_entry_t lle_cache_entry_t;

/* ============================================================================
 * CORE STRUCTURE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Performance context for measurement
 */
typedef struct {
    uint32_t cache_id;
    uint64_t key_hash;
    lle_cache_tier_t preferred_tier;
    size_t allocation_size;
    lle_perf_memory_pool_type_t pool_type;
    const char *custom_data;
} lle_performance_context_t;

/**
 * @brief Performance filter configuration
 */
typedef struct {
    bool filter_by_operation;
    bool filter_by_thread;
    bool filter_by_time_window;
    uint32_t operation_mask;
    uint32_t thread_id_filter;
    uint64_t time_window_start_ns;
    uint64_t time_window_end_ns;
} lle_perf_filter_t;

/**
 * @brief Performance statistics structure
 */
typedef struct {
    /// Basic statistics
    uint64_t call_count;
    uint64_t completed_count;
    uint64_t error_count;
    uint64_t total_duration_ns;
    uint64_t min_duration_ns;
    uint64_t max_duration_ns;

    /// Advanced statistics
    uint64_t mean_duration_ns;
    uint64_t median_duration_ns;
    uint64_t p95_duration_ns;
    uint64_t p99_duration_ns;
    uint64_t standard_deviation_ns;

    /// Performance indicators
    double operations_per_second;
    double cpu_utilization;
    uint64_t memory_usage_bytes;

    /// Trend analysis
    lle_perf_trend_t trend;
    double trend_coefficient;
    uint64_t trend_window_size;

    /// Quality metrics
    double reliability_score;
    uint64_t consecutive_successes;
    uint64_t max_consecutive_successes;
} lle_perf_statistics_t;

/**
 * @brief Performance history ring buffer
 */
typedef struct {
    lle_perf_statistics_t *entries;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    uint64_t total_entries_recorded;
} lle_perf_history_ring_t;

/**
 * @brief Individual performance measurement
 */
typedef struct {
    /// High-precision timing
    struct timespec start_time;
    struct timespec end_time;
    uint64_t duration_ns;

    /// Operation classification
    lle_perf_operation_type_t operation_type;
    const char *operation_name;

    /// Context information
    lle_performance_context_t context;
    uint32_t thread_id;
    uint64_t call_count;

    /// Statistical data
    lle_perf_statistics_t stats;
    bool is_critical_path;
} lle_performance_measurement_t;

/**
 * @brief Performance configuration
 */
typedef struct {
    /// Monitoring configuration
    bool monitoring_enabled;
    lle_perf_monitoring_level_t monitoring_level;

    /// Performance thresholds (nanoseconds)
    uint64_t warning_threshold_ns;
    uint64_t critical_threshold_ns;

    /// History configuration
    uint32_t history_ring_size;
    uint64_t history_retention_time_ns;

    /// Filtering configuration
    lle_perf_filter_t default_filters;

    /// Cache configuration (nested)
    struct {
        bool enabled;
        size_t max_cache_size_bytes;
        double target_hit_rate;
        lle_cache_policy_t eviction_policy;
    } cache_config;

    /// Memory configuration (nested)
    struct {
        bool zero_allocation_mode;
        size_t memory_pool_size_bytes;
        double target_zero_allocation_percentage;
    } memory_config;

    /// Profiler configuration (nested)
    struct {
        bool enabled;
        lle_profiler_mode_t mode;
        uint32_t sampling_rate_hz;
    } profiler_config;

    /// Resource monitoring configuration (nested)
    struct {
        bool enabled;
        uint32_t update_frequency_hz;
        double max_cpu_usage_percentage;
        size_t max_memory_usage_bytes;
    } resource_config;

    /// Dashboard configuration (nested)
    struct {
        bool enabled;
        uint32_t update_frequency_hz;
    } dashboard_config;
} lle_performance_config_t;

/**
 * @brief Main performance monitor structure
 */
struct lle_performance_monitor_t {
    /// Real-time metrics
    lle_performance_measurement_t measurements[LLE_PERF_MAX_MEASUREMENTS];
    uint32_t measurement_count;
    uint32_t measurement_index;

    /// Aggregated statistics
    lle_perf_statistics_t global_stats;
    lle_perf_statistics_t operation_stats[LLE_PERF_OPERATION_COUNT];

    /// Performance thresholds
    uint64_t warning_threshold_ns;
    uint64_t critical_threshold_ns;

    /// Monitoring configuration
    bool monitoring_enabled;
    lle_perf_monitoring_level_t monitoring_level;
    lle_perf_filter_t active_filters;

    /// Performance history
    lle_perf_history_ring_t history_ring;
    uint64_t total_operations;
};

/**
 * @brief Scope guard for RAII-style performance measurement
 */
typedef struct {
    lle_performance_monitor_t *monitor;
    lle_perf_measurement_id_t measurement_id;
} lle_perf_scope_guard_t;

/* ============================================================================
 * CACHE SYSTEM STRUCTURES
 * ============================================================================
 */

/**
 * @brief Cache metadata for entries
 */
typedef struct {
    uint64_t creation_time;
    uint64_t last_access_time;
    uint64_t access_count;
    lle_cache_tier_t tier;
    uint32_t flags;
    double eviction_score;
} lle_cache_metadata_t;

/**
 * @brief Cache LRU list management
 */
typedef struct lle_cache_lru_t {
    lle_cache_entry_t *head;
    lle_cache_entry_t *tail;
    uint32_t size;
} lle_cache_lru_t;

/**
 * @brief Cache allocator interface
 */
typedef struct {
    void *(*alloc)(size_t size, void *context);
    void (*free)(void *ptr, void *context);
    void *context;
} lle_cache_allocator_t;

/**
 * @brief Individual cache tier
 */
typedef struct {
    /// Cache identification
    lle_cache_tier_t tier;
    char name[LLE_CACHE_NAME_MAX];
    uint32_t cache_id;

    /// Cache configuration
    size_t max_entries;
    size_t entry_size;
    size_t total_size;

    /// Cache data structures
    lle_hashtable_t *entries;       ///< libhashtable for O(1) lookup
    lle_cache_lru_t *lru_list;      ///< LRU eviction management
    lle_cache_metadata_t *metadata; ///< Entry metadata

    /// Cache statistics
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t invalidations;
    uint64_t insertions;

    /// Performance metrics
    uint64_t total_lookup_time_ns;
    uint64_t total_insert_time_ns;
    uint64_t max_lookup_time_ns;
    uint64_t max_insert_time_ns;

    /// Cache management
    lle_cache_policy_t eviction_policy;
    lle_cache_consistency_t consistency_level;
    bool prefetch_enabled;

    /// Callback function pointers
    void (*eviction_callback)(lle_cache_entry_t *entry, void *context);
    void *callback_context;

    /// Memory management
    lle_memory_pool_t *memory_pool;
    lle_cache_allocator_t allocator;
} lle_cache_t;

/**
 * @brief Cache coherence protocol
 */
typedef struct {
    bool enabled;
    uint32_t protocol_version;
    void (*on_invalidate)(lle_cache_key_t *key, void *context);
    void *context;
} lle_cache_coherence_t;

/**
 * @brief Cache prefetch manager
 */
typedef struct {
    bool enabled;
    uint32_t prefetch_queue_size;
    double prediction_accuracy;
    void *predictor_model;
} lle_cache_prefetch_t;

/**
 * @brief Cache statistics summary
 */
typedef struct {
    uint64_t total_hits;
    uint64_t total_misses;
    uint64_t total_evictions;
    double overall_hit_rate;
    double avg_lookup_time_ns;
    uint64_t total_memory_usage_bytes;
} lle_cache_statistics_t;

/**
 * @brief Cache optimization event
 */
typedef struct {
    uint64_t timestamp_ns;
    const char *optimization_type;
    double performance_improvement;
    const char *description;
} lle_cache_optimization_event_t;

/**
 * @brief Machine learning model for cache optimization (opaque)
 */
typedef struct lle_cache_ml_model_t lle_cache_ml_model_t;

/**
 * @brief Cache prediction system (opaque)
 */
typedef struct lle_cache_prediction_t lle_cache_prediction_t;

/**
 * @brief Cache optimizer
 */
typedef struct {
    /// Performance monitoring
    lle_performance_monitor_t *perf_monitor;

    /// Optimization targets
    double target_hit_rate;
    uint64_t target_lookup_time_ns;
    double current_hit_rate;
    uint64_t current_avg_lookup_time_ns;

    /// Optimization strategies
    bool size_optimization_enabled;
    bool prefetch_optimization_enabled;
    bool eviction_optimization_enabled;

    /// Learning system
    lle_cache_ml_model_t *access_pattern_model;
    lle_cache_prediction_t *prefetch_predictor;

    /// Optimization history
    lle_cache_optimization_event_t
        optimization_history[LLE_CACHE_OPT_HISTORY_SIZE];
    uint32_t optimization_history_count;

    /// Auto-tuning parameters
    uint64_t optimization_interval_ns;
    uint64_t last_optimization_time;
    double optimization_threshold;
} lle_cache_optimizer_t;

/**
 * @brief Cache performance metrics
 */
typedef struct {
    double hit_rate;
    double miss_rate;
    uint64_t avg_lookup_time_ns;
    uint64_t avg_insert_time_ns;
    double cache_utilization;
    size_t memory_usage_bytes;
} lle_cache_performance_metrics_t;

/**
 * @brief Main cache manager
 */
struct lle_cache_manager_t {
    /// Multi-tier cache array
    lle_cache_t caches[LLE_CACHE_TIER_COUNT];
    uint32_t active_tiers;
    uint32_t cache_id;

    /// Global cache statistics
    lle_cache_statistics_t global_stats;

    /// Cache coordination
    lle_cache_coherence_t coherence_protocol;
    lle_cache_prefetch_t prefetch_manager;

    /// Performance targets
    double target_hit_rate;
    uint64_t target_lookup_time_ns;

    /// Adaptive optimization
    lle_cache_optimizer_t optimizer;
    bool auto_tuning_enabled;

    /// Performance monitoring integration
    lle_performance_monitor_t performance_monitor;
};

/* ============================================================================
 * MEMORY OPTIMIZATION STRUCTURES
 * ============================================================================
 */

/**
 * @brief Memory pool usage statistics
 */
typedef struct {
    lle_perf_memory_pool_type_t pool_type;
    size_t total_allocated;
    size_t total_freed;
    size_t current_usage;
    size_t peak_usage;
    uint64_t allocation_count;
    uint64_t deallocation_count;
    double utilization_percentage;
} lle_memory_pool_usage_t;

/**
 * @brief Memory usage analyzer (opaque)
 */
typedef struct lle_memory_usage_analyzer_t lle_memory_usage_analyzer_t;

/**
 * @brief Machine learning predictor for memory (opaque)
 */
typedef struct lle_memory_ml_predictor_t lle_memory_ml_predictor_t;

/**
 * @brief Memory optimization recommendations
 */
typedef struct {
    uint32_t recommendation_count;
    const char *recommendations[32];
    double priority_scores[32];
} lle_memory_optimization_recommendations_t;

/**
 * @brief Memory pattern detector
 */
typedef struct {
    /// Allocation patterns
    size_t common_allocation_sizes[LLE_MEMORY_PATTERN_SIZE_COUNT];
    uint64_t allocation_frequencies[LLE_MEMORY_PATTERN_SIZE_COUNT];

    /// Temporal patterns
    uint64_t allocation_times[LLE_MEMORY_PATTERN_TIME_WINDOW];
    uint32_t allocation_time_index;

    /// Pool usage patterns
    lle_memory_pool_usage_t pool_usage[LLE_MEMORY_POOL_TYPE_COUNT];

    /// Fragmentation analysis
    double fragmentation_ratio;
    size_t largest_free_block;
    uint32_t free_block_count;

    /// Predictive model
    lle_memory_ml_predictor_t *allocation_predictor;

    /// Optimization recommendations
    lle_memory_optimization_recommendations_t recommendations;
} lle_memory_pattern_detector_t;

/**
 * @brief Memory optimizer
 */
struct lle_memory_optimizer_t {
    /// Memory pool references
    lle_memory_pool_t *primary_pool; ///< Main allocation pool
    lle_memory_pool_t *event_pool;   ///< Event processing pool
    lle_memory_pool_t *cache_pool;   ///< Cache entry pool
    lle_memory_pool_t *buffer_pool;  ///< Buffer management pool

    /// Allocation tracking
    size_t total_allocated;
    size_t peak_allocated;
    uint64_t allocation_count;
    uint64_t deallocation_count;

    /// Zero-allocation counters
    uint64_t zero_alloc_operations;
    uint64_t total_operations;

    /// Memory usage analytics
    lle_memory_usage_analyzer_t *analyzer;
    lle_memory_pattern_detector_t pattern_detector;

    /// Optimization targets
    double zero_allocation_target_percentage;
    size_t memory_usage_target_bytes;

    /// Auto-optimization flag
    bool auto_optimization_enabled;

    /// Performance monitoring
    lle_performance_monitor_t *perf_monitor;
};

/* ============================================================================
 * RESOURCE MANAGEMENT STRUCTURES
 * ============================================================================
 */

/**
 * @brief Resource scheduler (opaque)
 */
typedef struct lle_resource_scheduler_t lle_resource_scheduler_t;

/**
 * @brief Resource throttle (opaque)
 */
typedef struct lle_resource_throttle_t lle_resource_throttle_t;

/**
 * @brief Resource predictor (opaque)
 */
typedef struct lle_resource_predictor_t lle_resource_predictor_t;

/**
 * @brief Resource manager
 */
struct lle_resource_manager_t {
    /// CPU resource monitoring
    double cpu_usage_percentage;
    uint64_t cpu_cycles_consumed;
    uint32_t active_threads;
    uint32_t cpu_core_count;

    /// Memory resource monitoring
    size_t memory_usage_bytes;
    size_t memory_peak_bytes;
    size_t memory_available_bytes;
    double memory_pressure_level;

    /// I/O resource monitoring
    uint64_t disk_reads;
    uint64_t disk_writes;
    uint64_t network_bytes_in;
    uint64_t network_bytes_out;

    /// Resource allocation targets
    double max_cpu_usage_percentage;
    size_t max_memory_usage_bytes;
    uint32_t max_io_operations_per_second;

    /// Resource optimization
    lle_resource_scheduler_t *scheduler;
    lle_resource_throttle_t *throttle;
    lle_resource_predictor_t *predictor;

    /// Performance monitoring
    lle_performance_monitor_t *perf_monitor;
};

/* ============================================================================
 * PERFORMANCE PROFILING STRUCTURES
 * ============================================================================
 */

/**
 * @brief Profiler call instance (internal)
 */
typedef struct lle_profiler_call_instance_t {
    struct lle_profiler_call_node_t *node;
    struct timespec start_time;
    uint64_t instance_id;
} lle_profiler_call_instance_t;

/**
 * @brief Optimization suggestions
 */
typedef struct {
    uint32_t count;
    const char *suggestions[16];
} lle_profiler_optimization_suggestions_t;

/**
 * @brief Profiler call graph node
 */
typedef struct lle_profiler_call_node_t {
    /// Function call information
    const char *function_name;
    const char *file_name;
    uint32_t line_number;

    /// Performance metrics
    uint64_t call_count;
    uint64_t total_duration_ns;
    uint64_t self_duration_ns;
    uint64_t min_duration_ns;
    uint64_t max_duration_ns;

    /// Call relationships
    struct lle_profiler_call_node_t *parent;
    struct lle_profiler_call_node_t *children[LLE_PROFILER_MAX_CHILDREN];
    uint32_t child_count;

    /// Hot spot detection
    bool is_hot_spot;
    double cpu_percentage;
    uint32_t hot_spot_rank;

    /// Optimization suggestions
    lle_profiler_optimization_suggestions_t suggestions;
} lle_profiler_call_node_t;

/**
 * @brief Profiler main structure
 */
struct lle_profiler_t {
    /// Call graph root
    lle_profiler_call_node_t *call_graph_root;
    lle_hashtable_t *function_nodes; ///< libhashtable for O(1) lookup

    /// Profiling statistics
    uint64_t total_profiling_time_ns;
    uint64_t samples_collected;
    uint32_t hot_spots_detected;

    /// Profiling configuration
    bool profiling_enabled;
    lle_profiler_mode_t profiling_mode;
    uint32_t sampling_rate_hz;

    /// Performance impact measurement
    uint64_t profiling_overhead_ns;
    double profiling_overhead_percentage;

    /// Hot spot analysis
    lle_profiler_call_node_t *hot_spots[LLE_PROFILER_MAX_HOT_SPOTS];
    uint32_t hot_spot_count;

    /// Memory management
    lle_memory_pool_t *profiler_memory_pool;

    /// Performance monitoring
    lle_performance_monitor_t *perf_monitor;
};

/* ============================================================================
 * PERFORMANCE DASHBOARD AND REPORTING
 * ============================================================================
 */

/**
 * @brief Performance overview
 */
typedef struct {
    uint64_t timestamp_ns;
    double overall_performance_score;
    uint64_t total_operations;
    uint64_t successful_operations;
    uint64_t failed_operations;
    double success_rate;
} lle_perf_overview_t;

/**
 * @brief System performance metrics
 */
typedef struct {
    double cpu_utilization;
    size_t memory_usage_bytes;
    uint64_t io_operations;
    uint32_t active_threads;
    double system_load_average;
} lle_system_performance_t;

/**
 * @brief Component performance metrics
 */
typedef struct {
    lle_component_type_t component_type;
    const char *component_name;
    lle_perf_statistics_t stats;
    double performance_score;
} lle_component_performance_t;

/**
 * @brief Performance trend analysis
 */
typedef struct {
    lle_perf_trend_t overall_trend;
    double trend_coefficient;
    const char *trend_description;
    uint64_t analysis_window_ns;
} lle_perf_trend_analysis_t;

/**
 * @brief Performance alert
 */
typedef struct {
    uint64_t timestamp_ns;
    lle_perf_target_status_enum_t severity;
    lle_component_type_t affected_component;
    const char *alert_message;
    const char *recommended_action;
    bool acknowledged;
} lle_perf_alert_t;

/**
 * @brief Performance targets
 */
typedef struct {
    /// Response time targets
    uint64_t max_response_time_ns;
    uint64_t target_response_time_ns;
    uint64_t warning_response_time_ns;

    /// Throughput targets
    uint64_t min_operations_per_second;
    uint64_t target_operations_per_second;

    /// Resource utilization targets
    double max_cpu_utilization;
    size_t max_memory_usage_bytes;

    /// Cache performance targets
    double min_cache_hit_rate;
    double target_cache_hit_rate;

    /// Error rate targets
    double max_error_rate;
    double target_error_rate;

    /// Zero-allocation targets
    double min_zero_allocation_percentage;
    double target_zero_allocation_percentage;

    /// Target achievement tracking (opaque)
    struct lle_perf_target_achievement_history_t *achievement_history;

    /// Adaptive targets
    bool adaptive_targets_enabled;
    struct lle_perf_adaptive_target_config_t *adaptive_config;
} lle_performance_targets_t;

/**
 * @brief Target achievement metrics
 */
typedef struct {
    bool all_targets_met;
    uint32_t targets_met_count;
    uint32_t targets_missed_count;
    double overall_achievement_percentage;
} lle_perf_achievement_t;

/**
 * @brief Report configuration
 */
typedef struct {
    bool include_hot_spots;
    bool include_cache_analysis;
    bool include_memory_analysis;
    bool include_recommendations;
    uint32_t max_hot_spots;
    uint32_t detail_level;
} lle_perf_report_config_t;

/**
 * @brief Performance dashboard
 */
struct lle_performance_dashboard_t {
    /// Performance overview
    lle_perf_overview_t current_overview;
    lle_perf_overview_t historical_overview;

    /// System performance metrics
    lle_system_performance_t system_metrics;

    /// Component-specific metrics
    lle_component_performance_t component_metrics[LLE_COMPONENT_COUNT];

    /// Performance trends
    lle_perf_trend_analysis_t trends;

    /// Alerts and warnings
    lle_perf_alert_t active_alerts[LLE_PERF_MAX_ALERTS];
    uint32_t active_alert_count;

    /// Performance goals and targets
    lle_performance_targets_t performance_targets;
    lle_perf_achievement_t target_achievement;

    /// Reporting configuration
    lle_perf_report_config_t report_config;

    /// Dashboard update tracking
    uint64_t last_update_time_ns;
    uint32_t update_frequency_hz;
};

/**
 * @brief Hot spot analysis for reports
 */
typedef struct {
    uint32_t total_hot_spots;
    uint32_t critical_hotspots;
    lle_profiler_call_node_t *top_hot_spots[LLE_PROFILER_MAX_HOT_SPOTS];
    const char *optimization_priorities[LLE_PROFILER_MAX_HOT_SPOTS];
} lle_perf_hotspot_analysis_t;

/**
 * @brief Cache analysis for reports
 */
typedef struct {
    double overall_hit_rate;
    double l1_hit_rate;
    double l2_hit_rate;
    double l3_hit_rate;
    uint64_t total_cache_memory_bytes;
    const char *optimization_recommendations[16];
    uint32_t recommendation_count;
} lle_perf_cache_analysis_t;

/**
 * @brief Memory analysis for reports
 */
typedef struct {
    size_t total_memory_allocated;
    size_t peak_memory_usage;
    double zero_allocation_percentage;
    double fragmentation_ratio;
    const char *optimization_recommendations[16];
    uint32_t recommendation_count;
} lle_perf_memory_analysis_t;

/**
 * @brief Performance recommendations
 */
typedef struct {
    uint32_t recommendation_count;
    const char *recommendations[32];
    double priority_scores[32];
    const char *implementation_notes[32];
} lle_perf_recommendations_t;

/**
 * @brief Comprehensive performance report
 */
typedef struct {
    /// Report metadata
    uint64_t generation_time;
    uint32_t report_version;
    char lle_version[32];

    /// System overview
    lle_perf_overview_t system_overview;

    /// Performance statistics
    lle_perf_statistics_t performance_stats;

    /// Hot spot analysis
    lle_perf_hotspot_analysis_t hotspot_analysis;

    /// Cache performance analysis
    lle_perf_cache_analysis_t cache_analysis;

    /// Memory optimization analysis
    lle_perf_memory_analysis_t memory_analysis;

    /// Performance recommendations
    lle_perf_recommendations_t recommendations;

    /// Overall performance score
    double overall_performance_score;
} lle_perf_report_t;

/* ============================================================================
 * PERFORMANCE TESTING STRUCTURES
 * ============================================================================
 */

/**
 * @brief Performance test scenario (opaque)
 */
typedef struct lle_perf_test_scenario_t lle_perf_test_scenario_t;

/**
 * @brief Test context (opaque)
 */
typedef struct lle_perf_test_context_t lle_perf_test_context_t;

/**
 * @brief Test environment (opaque)
 */
typedef struct lle_perf_test_environment_t lle_perf_test_environment_t;

/**
 * @brief Test results
 */
typedef struct {
    bool passed;
    uint64_t total_duration_ns;
    uint64_t min_operation_time_ns;
    uint64_t max_operation_time_ns;
    uint64_t avg_operation_time_ns;
    double operations_per_second;
    uint32_t error_count;
    double error_rate;
} lle_perf_test_results_t;

/**
 * @brief Performance test definition
 */
typedef struct {
    /// Test configuration
    char test_name[LLE_PERF_TEST_NAME_MAX];
    lle_perf_test_type_t test_type;
    lle_perf_test_scenario_t *scenario;

    /// Test parameters
    uint32_t iterations;
    uint32_t concurrent_operations;
    uint64_t test_duration_ns;

    /// Performance targets for this test
    lle_performance_targets_t test_targets;

    /// Test data
    void *test_data;
    size_t test_data_size;

    /// Test results
    lle_perf_test_results_t results;

    /// Test execution context
    lle_perf_test_context_t *context;

    /// Memory management
    lle_memory_pool_t *test_memory_pool;
} lle_performance_test_t;

/**
 * @brief Test suite
 */
typedef struct {
    char suite_name[LLE_PERF_TEST_NAME_MAX];
    lle_performance_test_t *tests;
    uint32_t test_count;
    lle_performance_config_t *config;
} lle_performance_test_suite_t;

/**
 * @brief Test results summary
 */
typedef struct {
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t targets_met;
    uint32_t targets_missed;
    double overall_score;
    uint64_t total_test_duration_ns;
} lle_perf_test_results_summary_t;

/**
 * @brief Performance baseline for regression testing
 */
typedef struct {
    char baseline_name[LLE_PERF_TEST_NAME_MAX];
    uint64_t baseline_timestamp;
    lle_perf_statistics_t response_times;
    lle_perf_statistics_t throughput;
    lle_perf_statistics_t memory_usage;
    lle_cache_performance_metrics_t cache_performance;
} lle_performance_baseline_t;

/**
 * @brief Regression comparison results
 */
typedef struct {
    double response_time_regression;
    double throughput_regression;
    double memory_regression;
    double cache_regression;
    bool overall_regression;
    const char *regression_summary;
} lle_perf_regression_results_t;

/**
 * @brief Target status structure
 */
typedef struct {
    lle_perf_target_status_enum_t response_time_status;
    lle_perf_target_status_enum_t throughput_status;
    lle_perf_target_status_enum_t resource_status;
    lle_perf_target_status_enum_t cache_status;
    lle_perf_target_status_enum_t error_status;
    bool all_targets_met;
} lle_perf_target_status_t;

/**
 * @brief Current performance metrics
 */
typedef struct {
    lle_perf_statistics_t response_times;
    lle_perf_statistics_t throughput;
    lle_perf_statistics_t memory_usage;
    lle_cache_performance_metrics_t cache_performance;
    double error_rate;
} lle_perf_current_metrics_t;

/* ============================================================================
 * INTEGRATION STRUCTURES
 * ============================================================================
 */

/**
 * @brief Performance integration configuration
 */
typedef struct {
    bool integrate_with_terminal;
    bool integrate_with_buffer;
    bool integrate_with_event_system;
    bool integrate_with_display;
    bool integrate_with_history;
    bool integrate_with_completion;
    bool integrate_with_syntax;
    bool integrate_with_autosuggestion;
    bool integrate_with_customization;
    uint32_t integration_flags;
} lle_perf_integration_config_t;

/**
 * @brief Cross-system performance coordinator (opaque)
 */
typedef struct lle_perf_coordinator_t lle_perf_coordinator_t;

/**
 * @brief Main performance integration structure
 */
typedef struct {
    /// Core system integrations
    lle_terminal_abstraction_t *terminal;
    lle_buffer_manager_t *buffer_manager;
    lle_event_system_t *event_system;
    lle_display_integration_t *display;
    lle_history_system_t *history;
    lle_autosuggestions_t *autosuggestions;
    lle_syntax_highlighting_t *syntax_highlighter;
    lle_completion_system_t *completion;
    lle_user_customization_t *customization;

    /// Performance systems
    lle_performance_monitor_t *performance_monitor;
    lle_cache_manager_t *cache_manager;
    lle_memory_optimizer_t *memory_optimizer;
    lle_profiler_t *profiler;
    lle_resource_manager_t *resource_manager;

    /// Integration configuration
    lle_perf_integration_config_t config;

    /// Cross-system performance coordination
    lle_perf_coordinator_t *coordinator;

    /// Unified performance dashboard
    lle_performance_dashboard_t dashboard;
} lle_performance_integration_t;

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Monitoring
 * ============================================================================
 */

/// Performance monitor lifecycle
lle_result_t lle_perf_monitor_init(lle_performance_monitor_t *monitor,
                                   lle_performance_config_t *config);

lle_result_t lle_perf_monitor_destroy(lle_performance_monitor_t *monitor);

/// Performance measurement operations
lle_result_t lle_perf_measurement_start(
    lle_performance_monitor_t *monitor, lle_perf_operation_type_t op_type,
    const char *op_name, lle_performance_context_t *context,
    lle_perf_measurement_id_t *measurement_id);

lle_result_t lle_perf_measurement_end(lle_performance_monitor_t *monitor,
                                      lle_perf_measurement_id_t measurement_id,
                                      lle_perf_result_t result_code);

/// Statistics retrieval
lle_result_t lle_perf_get_statistics(lle_performance_monitor_t *monitor,
                                     lle_perf_operation_type_t op_type,
                                     lle_perf_statistics_t *stats);

lle_result_t lle_perf_calculate_statistics(lle_performance_monitor_t *monitor,
                                           lle_perf_operation_type_t op_type,
                                           lle_perf_statistics_t *stats);

/// Performance history management
lle_result_t
lle_perf_history_record(lle_performance_monitor_t *monitor,
                        lle_performance_measurement_t *measurement);

/// Threshold management
lle_result_t lle_perf_handle_warning_threshold_exceeded(
    lle_performance_monitor_t *monitor,
    lle_performance_measurement_t *measurement);

lle_result_t lle_perf_handle_critical_threshold_exceeded(
    lle_performance_monitor_t *monitor,
    lle_performance_measurement_t *measurement);

/// Utility functions
bool lle_perf_should_monitor_operation(lle_performance_monitor_t *monitor,
                                       lle_perf_operation_type_t op_type);

bool lle_perf_is_critical_path(lle_perf_operation_type_t op_type);

uint64_t lle_perf_timespec_diff_ns(const struct timespec *end,
                                   const struct timespec *start);

uint64_t lle_get_current_time_ns(void);

/* ============================================================================
 * FUNCTION DECLARATIONS - Cache Management
 * ============================================================================
 */

/// Cache manager lifecycle
lle_result_t lle_cache_manager_init(lle_cache_manager_t *manager,
                                    lle_performance_config_t *config,
                                    lle_performance_monitor_t *perf_monitor);

lle_result_t lle_cache_manager_destroy(lle_cache_manager_t *manager);

/// Cache operations
lle_result_t lle_cache_lookup(lle_cache_manager_t *manager,
                              lle_cache_key_t *key, lle_cache_value_t **value,
                              lle_cache_tier_t *hit_tier);

lle_result_t lle_cache_insert(lle_cache_manager_t *manager,
                              lle_cache_key_t *key, lle_cache_value_t *value,
                              lle_cache_tier_t preferred_tier);

lle_result_t lle_cache_invalidate(lle_cache_manager_t *manager,
                                  lle_cache_key_t *key);

lle_result_t lle_cache_clear(lle_cache_manager_t *manager,
                             lle_cache_tier_t tier);

/// Cache eviction
lle_result_t lle_cache_evict_entries(lle_cache_t *cache, uint32_t count);

lle_cache_entry_t *lle_cache_select_eviction_victim(lle_cache_t *cache);

double lle_cache_calculate_eviction_score(lle_cache_entry_t *entry,
                                          uint64_t current_time);

/// Cache optimization
lle_result_t lle_cache_optimize_performance(lle_cache_manager_t *manager);

lle_result_t
lle_cache_get_performance_metrics(lle_cache_manager_t *manager,
                                  lle_cache_performance_metrics_t *metrics);

/// Cache LRU management
lle_result_t lle_cache_lru_touch(lle_cache_lru_t *lru_list,
                                 lle_cache_entry_t *entry);

lle_result_t lle_cache_lru_add(lle_cache_lru_t *lru_list,
                               lle_cache_entry_t *entry);

lle_result_t lle_cache_lru_remove(lle_cache_lru_t *lru_list,
                                  lle_cache_entry_t *entry);

lle_cache_entry_t *lle_cache_lru_get_oldest(lle_cache_lru_t *lru_list);

lle_cache_entry_t *lle_cache_lru_get_next_oldest(lle_cache_lru_t *lru_list,
                                                 lle_cache_entry_t *current);

/// Cache utilities
bool lle_cache_entry_is_valid(lle_cache_entry_t *entry);

uint64_t lle_cache_hash_key(lle_cache_key_t *key);

lle_cache_tier_t lle_cache_select_optimal_tier(lle_cache_manager_t *manager,
                                               lle_cache_key_t *key,
                                               lle_cache_value_t *value,
                                               lle_cache_tier_t preferred_tier);

lle_result_t lle_cache_promote_entry(lle_cache_manager_t *manager,
                                     lle_cache_entry_t *entry,
                                     lle_cache_tier_t from_tier);

lle_result_t lle_cache_trigger_prefetch(lle_cache_manager_t *manager,
                                        lle_cache_key_t *key);

lle_result_t lle_cache_trigger_optimization(lle_cache_manager_t *manager,
                                            lle_cache_t *cache);

/// Cache optimization helpers
lle_result_t
lle_cache_consider_size_increase(lle_cache_manager_t *manager,
                                 lle_cache_performance_metrics_t *metrics);

lle_result_t
lle_cache_optimize_prefetch_strategy(lle_cache_manager_t *manager,
                                     lle_cache_performance_metrics_t *metrics);

lle_result_t
lle_cache_optimize_lookup_performance(lle_cache_manager_t *manager,
                                      lle_cache_performance_metrics_t *metrics);

lle_result_t
lle_cache_apply_ml_optimizations(lle_cache_manager_t *manager,
                                 lle_cache_performance_metrics_t *metrics);

/* ============================================================================
 * FUNCTION DECLARATIONS - Memory Optimization
 * ============================================================================
 */

/// Memory optimizer lifecycle
lle_result_t lle_memory_optimizer_init(lle_memory_optimizer_t *optimizer,
                                       lle_performance_config_t *config,
                                       lle_performance_monitor_t *perf_monitor);

lle_result_t lle_memory_optimizer_destroy(lle_memory_optimizer_t *optimizer);

/// Memory allocation operations
void *lle_memory_alloc_optimized(lle_memory_optimizer_t *optimizer, size_t size,
                                 lle_perf_memory_pool_type_t pool_type);

void lle_memory_free_optimized(lle_memory_optimizer_t *optimizer, void *ptr,
                               lle_perf_memory_pool_type_t pool_type);

/// Zero-allocation tracking
lle_result_t
lle_memory_track_zero_alloc_operation(lle_memory_optimizer_t *optimizer,
                                      const char *operation_name);

/// Memory pattern analysis
lle_result_t
lle_memory_analyze_allocation_patterns(lle_memory_optimizer_t *optimizer);

lle_result_t
lle_memory_record_allocation_pattern(lle_memory_optimizer_t *optimizer,
                                     size_t size,
                                     lle_perf_memory_pool_type_t pool_type);

/// Memory pool selection
lle_memory_pool_t *
lle_memory_select_pool(lle_memory_optimizer_t *optimizer,
                       lle_perf_memory_pool_type_t pool_type);

/// Memory optimization
lle_result_t lle_memory_optimize_performance(lle_memory_optimizer_t *optimizer);

lle_result_t
lle_memory_trigger_zero_alloc_optimization(lle_memory_optimizer_t *optimizer);

lle_result_t
lle_memory_apply_pattern_optimizations(lle_memory_optimizer_t *optimizer,
                                       lle_memory_pattern_detector_t *detector);

/// Pattern analysis functions
lle_result_t
lle_memory_analyze_size_patterns(lle_memory_pattern_detector_t *detector);

lle_result_t
lle_memory_analyze_temporal_patterns(lle_memory_pattern_detector_t *detector);

lle_result_t
lle_memory_analyze_pool_usage(lle_memory_pattern_detector_t *detector,
                              lle_memory_optimizer_t *optimizer);

lle_result_t
lle_memory_analyze_fragmentation(lle_memory_pattern_detector_t *detector,
                                 lle_memory_optimizer_t *optimizer);

lle_result_t
lle_memory_generate_recommendations(lle_memory_pattern_detector_t *detector,
                                    lle_memory_optimizer_t *optimizer);

/* ============================================================================
 * FUNCTION DECLARATIONS - Resource Management
 * ============================================================================
 */

/// Resource manager lifecycle
lle_result_t lle_resource_manager_init(lle_resource_manager_t *manager,
                                       lle_performance_config_t *config,
                                       lle_performance_monitor_t *perf_monitor);

lle_result_t lle_resource_manager_destroy(lle_resource_manager_t *manager);

/// Resource monitoring
lle_result_t lle_resource_monitor_update(lle_resource_manager_t *manager);

lle_result_t lle_resource_update_cpu_usage(lle_resource_manager_t *manager);

lle_result_t lle_resource_update_memory_usage(lle_resource_manager_t *manager);

lle_result_t lle_resource_update_io_usage(lle_resource_manager_t *manager);

/// Resource pressure management
lle_result_t
lle_resource_check_pressure_levels(lle_resource_manager_t *manager);

lle_result_t lle_resource_apply_throttling(lle_resource_manager_t *manager);

/// Resource prediction
lle_result_t lle_resource_update_predictions(lle_resource_manager_t *manager);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Profiling
 * ============================================================================
 */

/// Profiler lifecycle
lle_result_t lle_profiler_init(lle_profiler_t *profiler,
                               lle_performance_config_t *config,
                               lle_performance_monitor_t *perf_monitor);

lle_result_t lle_profiler_destroy(lle_profiler_t *profiler);

/// Function profiling
lle_result_t lle_profiler_function_enter(lle_profiler_t *profiler,
                                         const char *function_name,
                                         const char *file_name,
                                         uint32_t line_number,
                                         lle_profiler_call_id_t *call_id);

lle_result_t lle_profiler_function_exit(lle_profiler_t *profiler,
                                        lle_profiler_call_id_t call_id);

/// Hot spot analysis
lle_result_t lle_profiler_analyze_hot_spots(lle_profiler_t *profiler);

lle_result_t lle_profiler_mark_hot_spot(lle_profiler_t *profiler,
                                        lle_profiler_call_node_t *node,
                                        uint64_t duration_ns);

/// Optimization suggestions
lle_result_t
lle_profiler_generate_optimization_suggestions(lle_profiler_call_node_t *node);

/// Profiler utilities
bool lle_profiler_should_sample(lle_profiler_t *profiler);

lle_profiler_call_instance_t *
lle_profiler_create_call_instance(lle_profiler_call_node_t *node);

lle_result_t
lle_profiler_destroy_call_instance(lle_profiler_call_instance_t *instance);

lle_result_t lle_profiler_collect_all_nodes(lle_profiler_t *profiler,
                                            lle_profiler_call_node_t ***nodes,
                                            uint32_t *node_count);

int lle_profiler_compare_by_total_time(const void *a, const void *b);

lle_result_t
lle_profiler_generate_performance_report(lle_profiler_t *profiler,
                                         lle_profiler_call_node_t **nodes,
                                         uint32_t node_count);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Dashboard and Reporting
 * ============================================================================
 */

/// Dashboard lifecycle
lle_result_t lle_perf_dashboard_init(lle_performance_dashboard_t *dashboard,
                                     lle_performance_config_t *config,
                                     lle_performance_monitor_t *monitor);

lle_result_t lle_perf_dashboard_destroy(lle_performance_dashboard_t *dashboard);

/// Dashboard updates
lle_result_t lle_perf_dashboard_update(lle_performance_dashboard_t *dashboard,
                                       lle_performance_monitor_t *monitor);

lle_result_t
lle_perf_update_system_metrics(lle_system_performance_t *system_metrics);

lle_result_t lle_perf_update_component_metrics(
    lle_component_performance_t *component_metrics,
    lle_component_type_t component_type, lle_performance_monitor_t *monitor);

lle_result_t lle_perf_update_overview(lle_perf_overview_t *overview,
                                      lle_performance_monitor_t *monitor);

lle_result_t lle_perf_analyze_trends(lle_perf_trend_analysis_t *trends,
                                     lle_perf_overview_t *current,
                                     lle_perf_overview_t *historical);

lle_result_t lle_perf_check_alerts(lle_performance_dashboard_t *dashboard,
                                   lle_performance_monitor_t *monitor);

lle_result_t
lle_perf_update_target_achievement(lle_perf_achievement_t *achievement,
                                   lle_perf_overview_t *overview,
                                   lle_performance_targets_t *targets);

/// Report generation
lle_result_t lle_perf_generate_report(lle_performance_monitor_t *monitor,
                                      lle_profiler_t *profiler,
                                      lle_cache_manager_t *cache_manager,
                                      lle_memory_optimizer_t *memory_optimizer,
                                      lle_perf_report_t *report);

lle_result_t
lle_perf_generate_system_overview(lle_perf_overview_t *overview,
                                  lle_performance_monitor_t *monitor);

lle_result_t
lle_perf_generate_performance_statistics(lle_perf_statistics_t *stats,
                                         lle_performance_monitor_t *monitor);

lle_result_t
lle_perf_generate_hotspot_analysis(lle_perf_hotspot_analysis_t *analysis,
                                   lle_profiler_t *profiler);

lle_result_t
lle_perf_generate_cache_analysis(lle_perf_cache_analysis_t *analysis,
                                 lle_cache_manager_t *cache_manager);

lle_result_t
lle_perf_generate_memory_analysis(lle_perf_memory_analysis_t *analysis,
                                  lle_memory_optimizer_t *memory_optimizer);

lle_result_t
lle_perf_generate_recommendations(lle_perf_recommendations_t *recommendations,
                                  lle_perf_report_t *report);

/// Performance score calculation
double lle_perf_calculate_performance_score(lle_perf_report_t *report);

double
lle_perf_calculate_test_suite_score(lle_perf_test_results_summary_t *results);

double
lle_perf_calculate_overall_regression(lle_perf_regression_results_t *results);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Target Management
 * ============================================================================
 */

/// Target monitoring
lle_result_t lle_perf_monitor_targets(lle_performance_targets_t *targets,
                                      lle_performance_monitor_t *monitor,
                                      lle_perf_target_status_t *status);

lle_perf_target_status_enum_t lle_perf_check_response_time_targets(
    lle_performance_targets_t *targets,
    lle_perf_current_metrics_t *current_metrics);

lle_perf_target_status_enum_t
lle_perf_check_throughput_targets(lle_performance_targets_t *targets,
                                  lle_perf_current_metrics_t *current_metrics);

lle_perf_target_status_enum_t
lle_perf_check_resource_targets(lle_performance_targets_t *targets,
                                lle_perf_current_metrics_t *current_metrics);

lle_perf_target_status_enum_t
lle_perf_check_cache_targets(lle_performance_targets_t *targets,
                             lle_perf_current_metrics_t *current_metrics);

lle_perf_target_status_enum_t
lle_perf_check_error_targets(lle_performance_targets_t *targets,
                             lle_perf_current_metrics_t *current_metrics);

/// Achievement tracking
lle_result_t
lle_perf_update_achievement_history(lle_performance_targets_t *targets,
                                    lle_perf_target_status_t *status);

/// Adaptive target adjustment
lle_result_t lle_perf_apply_adaptive_adjustments(
    lle_performance_targets_t *targets, lle_perf_target_status_t *status,
    lle_perf_current_metrics_t *current_metrics);

/// Current metrics retrieval
lle_result_t
lle_perf_get_performance_metrics(lle_performance_monitor_t *monitor,
                                 lle_perf_current_metrics_t *metrics);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Testing
 * ============================================================================
 */

/// Test suite execution
lle_result_t
lle_perf_execute_test_suite(lle_performance_test_suite_t *suite,
                            lle_performance_monitor_t *monitor,
                            lle_perf_test_results_summary_t *results);

lle_result_t lle_perf_execute_single_test(lle_performance_test_t *test,
                                          lle_perf_test_environment_t *env);

/// Test environment management
lle_result_t lle_perf_init_test_environment(lle_perf_test_environment_t *env,
                                            lle_performance_monitor_t *monitor);

lle_result_t
lle_perf_cleanup_test_environment(lle_perf_test_environment_t *env);

/// Test validation
bool lle_perf_test_meets_targets(lle_performance_test_t *test);

lle_result_t
lle_perf_update_aggregate_results(lle_perf_test_results_summary_t *results,
                                  lle_performance_test_t *test);

lle_result_t
lle_perf_generate_test_report(lle_performance_test_suite_t *suite,
                              lle_perf_test_results_summary_t *results);

/// Regression testing
lle_result_t lle_perf_regression_test(lle_performance_baseline_t *baseline,
                                      lle_performance_monitor_t *monitor,
                                      lle_perf_regression_results_t *results);

double lle_perf_compare_response_times(lle_perf_statistics_t *baseline,
                                       lle_perf_statistics_t *current);

double lle_perf_compare_throughput(lle_perf_statistics_t *baseline,
                                   lle_perf_statistics_t *current);

double lle_perf_compare_memory_usage(lle_perf_statistics_t *baseline,
                                     lle_perf_statistics_t *current);

double
lle_perf_compare_cache_performance(lle_cache_performance_metrics_t *baseline,
                                   lle_cache_performance_metrics_t *current);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Integration
 * ============================================================================
 */

/// Integration lifecycle
lle_result_t
lle_perf_integration_init(lle_performance_integration_t *integration,
                          lle_performance_config_t *config);

lle_result_t
lle_perf_integration_destroy(lle_performance_integration_t *integration);

/// Integration updates
lle_result_t
lle_perf_integration_update(lle_performance_integration_t *integration);

/// Cross-system coordinator
lle_result_t
lle_perf_coordinator_init(lle_perf_coordinator_t *coordinator,
                          lle_performance_integration_t *integration);

lle_result_t lle_perf_coordinator_update(lle_perf_coordinator_t *coordinator);

/// Dynamic optimization
lle_result_t lle_perf_apply_dynamic_optimizations(
    lle_performance_integration_t *integration);

/// Component integration
lle_result_t
lle_perf_integrate_with_terminal(lle_performance_integration_t *integration,
                                 lle_terminal_abstraction_t *terminal);

lle_result_t
lle_perf_integrate_with_buffer(lle_performance_integration_t *integration,
                               lle_buffer_manager_t *buffer);

lle_result_t
lle_perf_integrate_with_event_system(lle_performance_integration_t *integration,
                                     lle_event_system_t *event_system);

lle_result_t
lle_perf_integrate_with_display(lle_performance_integration_t *integration,
                                lle_display_integration_t *display);

lle_result_t
lle_perf_integrate_with_history(lle_performance_integration_t *integration,
                                lle_history_system_t *history);

lle_result_t
lle_perf_integrate_with_completion(lle_performance_integration_t *integration,
                                   lle_completion_system_t *completion);

lle_result_t
lle_perf_integrate_with_syntax(lle_performance_integration_t *integration,
                               lle_syntax_highlighting_t *syntax);

lle_result_t lle_perf_integrate_with_autosuggestion(
    lle_performance_integration_t *integration,
    lle_autosuggestions_t *autosuggestions);

lle_result_t lle_perf_integrate_with_customization(
    lle_performance_integration_t *integration,
    lle_user_customization_t *customization);

/* ============================================================================
 * FUNCTION DECLARATIONS - Advanced Analysis
 * ============================================================================
 */

/// Percentile calculations
lle_result_t lle_perf_calculate_percentiles(lle_performance_monitor_t *monitor,
                                            lle_perf_operation_type_t op_type,
                                            lle_perf_statistics_t *stats);

lle_result_t
lle_perf_calculate_standard_deviation(lle_performance_monitor_t *monitor,
                                      lle_perf_operation_type_t op_type,
                                      lle_perf_statistics_t *stats);

lle_result_t
lle_perf_calculate_trend_analysis(lle_performance_monitor_t *monitor,
                                  lle_perf_operation_type_t op_type,
                                  lle_perf_statistics_t *stats);

uint64_t lle_perf_count_recent_operations(lle_performance_monitor_t *monitor,
                                          lle_perf_operation_type_t op_type,
                                          uint64_t time_window_ns);

#endif /// LLE_PERFORMANCE_H
