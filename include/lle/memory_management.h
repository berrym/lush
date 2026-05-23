/**
 * @file memory_management.h
 * @brief LLE Memory Management System - Complete Specification Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 15 - Memory Management Complete Specification
 * Version: 1.0.0
 * Status: 100% Complete - All Functions Declared
 *
 * This header declares ALL types, structures, and functions from Spec 15.
 * Every function declared here will have a complete implementation.
 */

#ifndef LLE_MEMORY_MANAGEMENT_H
#define LLE_MEMORY_MANAGEMENT_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Include LLE error handling */
#include "lle/error_handling.h"

/* Include Lush memory pool for integration */
#include "lush_memory_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

/* Lush types from lush_memory_pool.h are included via header */
/* Spec uses lush_memory_pool_t but actual Lush uses
 * lush_memory_pool_system_t */
/* Create alias to match spec naming */
typedef lush_memory_pool_system_t lush_memory_pool_t;

typedef struct lush_memory_system_t lush_memory_system_t;
typedef struct lle_input_event_t lle_input_event_t;
typedef struct lle_display_event_t lle_display_event_t;
typedef struct lle_system_event_t lle_system_event_t;
typedef struct lle_buffer_config_t lle_buffer_config_t;

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/* Memory pool configuration */
#define LLE_MAX_BUFFER_BLOCKS 256
#define LLE_STRING_CACHE_SIZE 128
#define LLE_EVENT_POOL_SIZE 512
#define LLE_INPUT_EVENT_CACHE 64
#define LLE_DISPLAY_EVENT_CACHE 64
#define LLE_SYSTEM_EVENT_CACHE 32
#define LLE_PRIMARY_POOL_COUNT 8
#define LLE_SECONDARY_POOL_COUNT 4
#define LLE_MAX_SHARED_ALLOCATIONS 1024
#define LLE_MAX_FREE_FRAGMENTS 256
#define LLE_MEMORY_ALIGNMENT 16
#define LLE_BUFFER_ALIGNMENT 64
#define LLE_RESIZE_HISTORY_SIZE 32
#define LLE_ACCESS_HISTORY_SIZE 1024
#define LLE_HOT_REGIONS_COUNT 16
#define LLE_TUNING_HISTORY_SIZE 64
#define LLE_ERROR_HISTORY_SIZE 128
#define LLE_MAX_TEST_FAILURES 32
#define LLE_MAX_TRACKED_BUFFERS 512
#define LLE_MAX_KEY_SIZE 64
#define LLE_MAX_DERIVED_KEYS 8
#define LLE_INPUT_EVENT_CACHE_SIZE 64
#define LLE_DISPLAY_EVENT_CACHE_SIZE 64
#define LLE_SYSTEM_EVENT_CACHE_SIZE 32
#define LUSH_POOL_COUNT 4

/* Performance targets */
#define LLE_ALLOCATION_TIME_TARGET_US 100
#define LLE_GC_CYCLE_TIME_TARGET_MS 5
#define LLE_PRESSURE_DETECTION_TIME_US 1000
#define LLE_BOUNDS_CHECK_TIME_US 50
#define LLE_CRYPTO_OPERATION_TIME_US 200

/* Efficiency targets */
#define LLE_MEMORY_UTILIZATION_TARGET 0.90
#define LLE_FRAGMENTATION_LIMIT 0.15
#define LLE_CACHE_HIT_RATE_TARGET 0.85
#define LLE_METADATA_OVERHEAD_LIMIT 0.10
#define LLE_INTEGRATION_OVERHEAD_LIMIT 0.05

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

typedef enum {
    LLE_POOL_BUFFER,     ///< Pool for buffer allocations
    LLE_POOL_EVENT,      ///< Pool for event allocations
    LLE_POOL_STRING,     ///< Pool for string allocations
    LLE_POOL_TEMP,       ///< Pool for short-lived temporary allocations
    LLE_POOL_HISTORY,    ///< Pool for history-related allocations
    LLE_POOL_SYNTAX,     ///< Pool for syntax-highlighting allocations
    LLE_POOL_COMPLETION, ///< Pool for completion-related allocations
    LLE_POOL_CUSTOM,     ///< Pool for caller-defined custom allocations
    LLE_POOL_COUNT       ///< Sentinel: number of pool types
} lle_memory_pool_type_t;

typedef enum {
    LLE_MEMORY_STATE_INITIALIZING, ///< Pools are being initialized
    LLE_MEMORY_STATE_ACTIVE,       ///< Normal steady-state operation
    LLE_MEMORY_STATE_OPTIMIZING,   ///< Optimization pass in progress
    LLE_MEMORY_STATE_GC_RUNNING,   ///< Garbage collection in progress
    LLE_MEMORY_STATE_LOW_MEMORY,   ///< Low-memory condition detected
    LLE_MEMORY_STATE_ERROR,        ///< Unrecoverable error state
    LLE_MEMORY_STATE_SHUTDOWN      ///< Manager is shutting down
} lle_memory_state_t;

typedef enum {
    LLE_GC_STRATEGY_MARK_SWEEP,         ///< Classic mark-sweep collector
    LLE_GC_STRATEGY_MARK_SWEEP_COMPACT, ///< Mark-sweep with compaction phase
    LLE_GC_STRATEGY_GENERATIONAL,       ///< Generational collector
    LLE_GC_STRATEGY_INCREMENTAL         ///< Incremental collector
} lle_gc_strategy_t;

typedef enum {
    LLE_GC_STATE_IDLE,       ///< GC is idle
    LLE_GC_STATE_MARKING,    ///< GC is in the mark phase
    LLE_GC_STATE_SWEEPING,   ///< GC is in the sweep phase
    LLE_GC_STATE_COMPACTING, ///< GC is in the compaction phase
    LLE_GC_STATE_ERROR       ///< GC has entered an error state
} lle_gc_state_t;

typedef enum {
    LLE_POOL_SELECTION_ROUND_ROBIN, ///< Rotate through pools in order
    LLE_POOL_SELECTION_LEAST_USED,  ///< Pick the pool with the most free space
    LLE_POOL_SELECTION_BEST_FIT, ///< Pick the pool whose block size best fits
    LLE_POOL_SELECTION_FIRST_FIT ///< Pick the first pool that can satisfy
} lle_pool_selection_algorithm_t;

typedef enum {
    LLE_STRATEGY_PRIMARY_ONLY,       ///< Allocate from primary pools only
    LLE_STRATEGY_SECONDARY_FALLBACK, ///< Primary then secondary on failure
    LLE_STRATEGY_EMERGENCY_ONLY,     ///< Allocate from emergency reserve
    LLE_STRATEGY_AUTOMATIC           ///< Choose strategy automatically
} lle_allocation_strategy_t;

typedef enum {
    LLE_RESIZE_ACTION_NONE,  ///< No resize action required
    LLE_RESIZE_ACTION_GROW,  ///< Grow the pool
    LLE_RESIZE_ACTION_SHRINK ///< Shrink the pool
} lle_resize_action_t;

typedef enum {
    LLE_RESIZE_REASON_UTILIZATION,   ///< Triggered by utilization thresholds
    LLE_RESIZE_REASON_FRAGMENTATION, ///< Triggered by fragmentation level
    LLE_RESIZE_REASON_PERFORMANCE,   ///< Triggered by performance metrics
    LLE_RESIZE_REASON_MANUAL         ///< Triggered by explicit caller request
} lle_resize_reason_t;

typedef enum {
    LLE_ACCESS_TYPE_READ,      ///< Read-only access
    LLE_ACCESS_TYPE_WRITE,     ///< Write-only access
    LLE_ACCESS_TYPE_READ_WRITE ///< Combined read and write access
} lle_access_type_t;

typedef enum {
    LLE_PREFETCH_NONE,       ///< No prefetching
    LLE_PREFETCH_SEQUENTIAL, ///< Sequential prefetch hints
    LLE_PREFETCH_ADAPTIVE,   ///< Adaptive prefetch tuned to observed patterns
    LLE_PREFETCH_AGGRESSIVE  ///< Aggressive speculative prefetch
} lle_prefetch_strategy_t;

typedef enum {
    LLE_TUNING_ACTION_NONE,       ///< No tuning action
    LLE_TUNING_ACTION_RESIZE,     ///< Resize a pool
    LLE_TUNING_ACTION_DEFRAGMENT, ///< Defragment a pool
    LLE_TUNING_ACTION_REORGANIZE  ///< Reorganize pool layout
} lle_tuning_action_t;

typedef enum {
    LLE_MEMORY_ERROR_LEAK,             ///< Allocated memory was never freed
    LLE_MEMORY_ERROR_BOUNDS_VIOLATION, ///< Access outside allocation bounds
    LLE_MEMORY_ERROR_CORRUPTION,       ///< Heap metadata corruption detected
    LLE_MEMORY_ERROR_DOUBLE_FREE,      ///< Memory freed more than once
    LLE_MEMORY_ERROR_USE_AFTER_FREE    ///< Access to freed memory
} lle_memory_error_type_t;

typedef enum {
    LLE_MEMORY_RECOVERY_ABORT,   ///< Abort execution on error
    LLE_MEMORY_RECOVERY_ISOLATE, ///< Isolate the affected region
    LLE_MEMORY_RECOVERY_REPAIR,  ///< Attempt in-place repair
    LLE_MEMORY_RECOVERY_RESTART  ///< Restart the memory subsystem
} lle_memory_recovery_strategy_t;

typedef enum {
    LLE_ENCRYPTION_NONE,    ///< No encryption
    LLE_ENCRYPTION_AES_128, ///< AES-128 encryption
    LLE_ENCRYPTION_AES_256, ///< AES-256 encryption
    LLE_ENCRYPTION_CHACHA20 ///< ChaCha20 stream cipher
} lle_encryption_algorithm_t;

typedef enum {
    LLE_DATA_SENSITIVITY_LOW,     ///< Non-sensitive data
    LLE_DATA_SENSITIVITY_MEDIUM,  ///< Moderately sensitive data
    LLE_DATA_SENSITIVITY_HIGH,    ///< Highly sensitive data
    LLE_DATA_SENSITIVITY_CRITICAL ///< Critical / secret data
} lle_data_sensitivity_t;

typedef enum {
    LLE_SECURITY_BOUNDS_VIOLATION,     ///< Out-of-bounds access detected
    LLE_SECURITY_PERMISSION_VIOLATION, ///< Access permission violated
    LLE_SECURITY_ENCRYPTION_FAILURE,   ///< Encryption operation failed
    LLE_SECURITY_CORRUPTION_DETECTED   ///< Memory corruption observed
} lle_security_incident_t;

typedef enum {
    LLE_INTEGRATION_MODE_STANDALONE,  ///< LLE memory operates independently
    LLE_INTEGRATION_MODE_COOPERATIVE, ///< Cooperative sharing with Lush
    LLE_INTEGRATION_MODE_UNIFIED      ///< Fully unified memory with Lush
} lle_integration_mode_t;

typedef enum {
    LLE_DISPLAY_MEMORY_PROMPT,         ///< Memory for prompt rendering
    LLE_DISPLAY_MEMORY_SYNTAX,         ///< Memory for syntax highlighting
    LLE_DISPLAY_MEMORY_AUTOSUGGESTION, ///< Memory for autosuggestions
    LLE_DISPLAY_MEMORY_COMPOSITION     ///< Memory for layer composition
} lle_display_memory_type_t;

typedef enum {
    LLE_TEST_FAILURE_BASIC_ALLOCATION, ///< Basic allocation test failed
    LLE_TEST_FAILURE_STRESS_TEST,      ///< Stress test failed
    LLE_TEST_FAILURE_MEMORY_LEAK,      ///< Leak test failed
    LLE_TEST_FAILURE_PERFORMANCE,      ///< Performance benchmark failed
    LLE_TEST_FAILURE_CONCURRENCY       ///< Concurrency test failed
} lle_test_failure_reason_t;

typedef enum {
    LLE_EVENT_TYPE_INPUT,   ///< Input event
    LLE_EVENT_TYPE_DISPLAY, ///< Display event
    LLE_EVENT_TYPE_SYSTEM   ///< System event
} lle_event_type_t;

typedef enum {
    LLE_BUFFER_TYPE_STRING, ///< Immutable string buffer
    LLE_BUFFER_TYPE_EDIT,   ///< Editable buffer
    LLE_BUFFER_TYPE_TEMP    ///< Short-lived scratch buffer
} lle_buffer_type_t;

typedef enum {
    LLE_COMPRESSION_NONE,  ///< No compression
    LLE_COMPRESSION_LZ4,   ///< LZ4 compression
    LLE_COMPRESSION_ZSTD,  ///< Zstandard compression
    LLE_COMPRESSION_SNAPPY ///< Snappy compression
} lle_compression_algorithm_t;

/* ============================================================================
 * FORWARD DECLARATIONS OF OPAQUE TYPES
 * ============================================================================
 */

typedef struct lle_memory_pool_t lle_memory_pool_t;
typedef struct lle_memory_manager_t lle_memory_manager_t;
typedef struct lle_memory_pool_manager_t lle_memory_pool_manager_t;
typedef struct lle_memory_tracker_t lle_memory_tracker_t;
typedef struct lle_memory_optimizer_t lle_memory_optimizer_t;
typedef struct lle_memory_security_t lle_memory_security_t;
typedef struct lle_memory_analytics_t lle_memory_analytics_t;
typedef struct lle_memory_pool_base_t lle_memory_pool_base_t;
typedef struct lle_buffer_memory_pool_t lle_buffer_memory_pool_t;
typedef struct lle_event_memory_pool_t lle_event_memory_pool_t;
typedef struct lle_memory_pool_hierarchy_t lle_memory_pool_hierarchy_t;
typedef struct lle_dynamic_pool_resizer_t lle_dynamic_pool_resizer_t;
typedef struct lle_garbage_collector_t lle_garbage_collector_t;
typedef struct lle_buffer_memory_t lle_buffer_memory_t;
typedef struct lle_multiline_buffer_t lle_multiline_buffer_t;
typedef struct lle_event_memory_integration_t lle_event_memory_integration_t;
typedef struct lle_memory_access_optimizer_t lle_memory_access_optimizer_t;
typedef struct lle_memory_pool_tuner_t lle_memory_pool_tuner_t;
typedef struct lle_memory_error_handler_t lle_memory_error_handler_t;
typedef struct lle_buffer_overflow_protection_t
    lle_buffer_overflow_protection_t;
typedef struct lle_memory_encryption_t lle_memory_encryption_t;
typedef struct lle_lush_memory_integration_complete_t
    lle_lush_memory_integration_complete_t;
typedef struct lle_display_memory_coordination_t
    lle_display_memory_coordination_t;
typedef struct lle_memory_test_framework_t lle_memory_test_framework_t;

/* Types needed for complete structure definitions */
typedef struct {
    int error_code;          ///< Numeric error code
    char error_message[256]; ///< Human-readable error message
} lle_integration_error_t;

/* ============================================================================
 * NOTE: Full struct definitions moved to memory_management.c for encapsulation
 * Only forward declarations (typedef struct) remain in header
 * ============================================================================
 */

typedef struct {
    lle_memory_pool_type_t type;     ///< Logical pool type
    size_t initial_size;             ///< Initial pool size in bytes
    size_t max_size;                 ///< Maximum pool size in bytes
    size_t block_size;               ///< Allocation block size
    size_t alignment;                ///< Required allocation alignment
    double growth_factor;            ///< Multiplicative growth factor
    size_t gc_threshold;             ///< Threshold that triggers GC
    bool enable_compression;         ///< Whether to enable compression
    bool enable_bounds_checking;     ///< Whether to enable bounds checking
    bool enable_encryption;          ///< Whether to enable encryption
    bool enable_poisoning;           ///< Whether to poison freed memory
    bool share_with_lush;            ///< Whether to share with Lush memory
    lush_memory_pool_t *parent_pool; ///< Optional parent Lush pool
} lle_memory_pool_config_t;

typedef struct {
    size_t pool_sizes[LLE_POOL_COUNT];     ///< Initial size per pool type
    size_t max_pool_sizes[LLE_POOL_COUNT]; ///< Maximum size per pool type
    size_t block_size;                     ///< Default allocation block size
    size_t alignment;                      ///< Default allocation alignment
} lle_memory_config_t;

typedef struct {
    uint64_t total_allocated; ///< Cumulative bytes allocated
    uint64_t total_freed;     ///< Cumulative bytes freed
    uint64_t current_usage;   ///< Current bytes in use
    uint64_t peak_usage;      ///< Peak bytes ever in use
    double allocation_rate;   ///< Recent allocation rate (bytes/sec)
    double deallocation_rate; ///< Recent deallocation rate (bytes/sec)
} lle_memory_stats_t;

typedef struct {
    lle_resize_action_t action; ///< Resize action to take
    lle_resize_reason_t reason; ///< Reason driving the action
} lle_resize_decision_t;

typedef struct {
    double locality_score;   ///< Locality score in [0.0, 1.0]
    double sequential_ratio; ///< Fraction of sequential accesses
    size_t hot_region_count; ///< Number of hot memory regions detected
} lle_access_pattern_analysis_t;

typedef struct {
    lle_memory_error_type_t error_type; ///< Kind of memory error
    void *error_address;                ///< Address involved in the error
    size_t error_size;                  ///< Size in bytes involved
    struct timespec error_time;         ///< Timestamp of the error
    char error_description[256];        ///< Human-readable description
} lle_memory_error_t;

/* lle_integration_error_t moved earlier - before complete structure definitions
 */

typedef struct {
    lle_tuning_action_t action; ///< Kind of tuning action
    size_t parameter;           ///< Action-specific numeric parameter
} lle_tuning_action_item_t;

typedef struct {
    lle_tuning_action_item_t actions[16]; ///< Planned tuning actions
    size_t action_count; ///< Number of valid entries in actions
} lle_tuning_action_plan_t;

typedef struct {
    double allocation_rate;                  ///< Allocations per second
    double deallocation_rate;                ///< Deallocations per second
    struct timespec average_allocation_time; ///< Average allocation latency
    struct timespec peak_allocation_time;    ///< Worst-case allocation latency
    double fragmentation_ratio;    ///< Fragmentation ratio in [0.0, 1.0]
    double utilization_efficiency; ///< Utilization efficiency in [0.0, 1.0]
} lle_memory_pool_performance_t;

typedef struct {
    bool high_fragmentation; ///< Fragmentation exceeds threshold
    bool slow_allocations;   ///< Allocation latency exceeds threshold
    bool poor_locality;      ///< Locality score below threshold
} lle_performance_bottleneck_analysis_t;

/* ============================================================================
 * CORE PUBLIC API - Essential Functions
 * ============================================================================
 */

/* Primary allocation/deallocation */
/**
 * @brief Allocate memory from the default LLE pool
 *
 * @param size Number of bytes to allocate
 * @return New allocation on success, NULL on failure
 */
void *lle_pool_alloc(size_t size);
/**
 * @brief Free memory previously returned by lle_pool_alloc
 *
 * @param ptr Pointer returned by lle_pool_alloc (may be NULL)
 */
void lle_pool_free(void *ptr);
/**
 * @brief Allocate memory from a specific base memory pool
 *
 * @param pool Pool to allocate from
 * @param size Number of bytes to allocate
 * @return New allocation on success, NULL on failure
 */
void *lle_pool_allocate(lle_memory_pool_base_t *pool, size_t size);
/**
 * @brief Allocate memory with explicit alignment from a pool
 *
 * @param pool Pool to allocate from
 * @param size Number of bytes to allocate
 * @param alignment Required byte alignment for the returned pointer
 * @return New aligned allocation on success, NULL on failure
 */
void *lle_pool_allocate_aligned(lle_memory_pool_t *pool, size_t size,
                                size_t alignment);
/**
 * @brief Fast-path allocation from a pool (no locking / minimal bookkeeping)
 *
 * @param pool Pool to allocate from
 * @param size Number of bytes to allocate
 * @return New allocation on success, NULL on failure
 */
void *lle_pool_allocate_fast(lle_memory_pool_t *pool, size_t size);
/**
 * @brief Fast-path free for memory obtained via lle_pool_allocate_fast
 *
 * @param pool Pool the pointer was allocated from
 * @param ptr Pointer to free
 */
void lle_pool_free_fast(lle_memory_pool_t *pool, void *ptr);

/* Lush Memory Pool Integration Bridge */
/**
 * @brief Create an LLE memory pool that wraps an existing Lush memory pool
 *
 * @param lle_pool Output parameter receiving the new LLE pool handle
 * @param lush_pool Underlying Lush memory pool to wrap
 * @param pool_type Logical pool type to assign to the wrapper
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_pool_create_from_lush(lle_memory_pool_t **lle_pool,
                                              lush_memory_pool_t *lush_pool,
                                              lle_memory_pool_type_t pool_type);
/**
 * @brief Destroy an LLE memory pool and release its resources
 *
 * @param pool Pool to destroy
 */
void lle_memory_pool_destroy(lle_memory_pool_t *pool);

/* Memory State Management */
/**
 * @brief Transition the memory manager to a new lifecycle state
 *
 * @param manager Memory manager to transition
 * @param new_state Target state to transition to
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_transition_state(lle_memory_manager_t *manager,
                                         lle_memory_state_t new_state);
/**
 * @brief Test whether a state transition is permitted by the state machine
 *
 * @param old_state Current memory manager state
 * @param new_state Proposed next state
 * @return true if the transition is legal, false otherwise
 */
bool lle_memory_is_valid_transition(lle_memory_state_t old_state,
                                    lle_memory_state_t new_state);
/**
 * @brief Initialize all memory pools owned by a memory manager
 *
 * @param manager Memory manager whose pools should be initialized
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_initialize_pools(lle_memory_manager_t *manager);
/**
 * @brief Begin runtime monitoring of memory usage for a manager
 *
 * @param manager Memory manager to monitor
 */
void lle_memory_start_monitoring(lle_memory_manager_t *manager);
/**
 * @brief Begin the optimization phase for a memory manager
 *
 * @param manager Memory manager whose optimizer should start
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_start_optimization(lle_memory_manager_t *manager);
/**
 * @brief Begin a garbage-collection cycle for a memory manager
 *
 * @param manager Memory manager whose GC should run
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_start_garbage_collection(lle_memory_manager_t *manager);
/**
 * @brief Handle the transition into a low-memory condition
 *
 * @param manager Memory manager experiencing memory pressure
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_handle_low_memory(lle_memory_manager_t *manager);
/**
 * @brief Handle the manager entering an error state
 *
 * @param manager Memory manager that has encountered an error
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_handle_error_state(lle_memory_manager_t *manager);
/**
 * @brief Shut down and tear down all memory pools owned by a manager
 *
 * @param manager Memory manager to shut down
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_memory_shutdown_pools(lle_memory_manager_t *manager);

/* Lush Integration */
/**
 * @brief Integrate the LLE memory manager with the host Lush memory subsystem
 *
 * @param manager Memory manager to integrate
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_integrate_with_lush_memory(lle_memory_manager_t *manager);
/**
 * @brief Obtain the global Lush memory pool system handle
 *
 * @return Pointer to the Lush memory pool system, or NULL if unavailable
 */
lush_memory_pool_t *lush_get_memory_pools(void);
/**
 * @brief Analyze Lush's memory pools and derive a matching LLE config
 *
 * @param lush_pools Lush memory pool system to inspect
 * @param lush_config Output parameter receiving the derived configuration
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_analyze_lush_memory_config(lush_memory_pool_t *lush_pools,
                                            lle_memory_config_t *lush_config);
/**
 * @brief Create a specialized memory pool from a configuration descriptor
 *
 * @param manager Memory manager that will own the new pool
 * @param pool_config Configuration describing the pool to create
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_create_specialized_pool(lle_memory_manager_t *manager,
                            const lle_memory_pool_config_t *pool_config);
/**
 * @brief Roll back a partially completed integration up to a given pool index
 *
 * @param manager Memory manager being rolled back
 * @param pool_index Number of pools to tear down during cleanup
 */
void lle_cleanup_partial_integration(lle_memory_manager_t *manager,
                                     size_t pool_index);
/**
 * @brief Create shared memory regions used for LLE/Lush cooperation
 *
 * @param manager Memory manager whose shared regions will be created
 * @param lush_config Configuration describing Lush-side pool sizing
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_create_shared_memory_regions(lle_memory_manager_t *manager,
                                 const lle_memory_config_t *lush_config);
/**
 * @brief Initialize cross-allocation bookkeeping tables for the manager
 *
 * @param manager Memory manager whose cross-allocation tables to initialize
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_initialize_cross_allocation_tables(lle_memory_manager_t *manager);
/**
 * @brief Start the integration-monitoring subsystem for a manager
 *
 * @param manager Memory manager to monitor
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_start_integration_monitoring(lle_memory_manager_t *manager);

/* Shared Memory Pool */
/**
 * @brief Allocate memory from a shared pool tagged with an owning pool type
 *
 * @param pool Shared pool to allocate from
 * @param size Number of bytes to allocate
 * @param owner Logical owning pool type for accounting
 * @return New allocation on success, NULL on failure
 */
void *lle_shared_memory_allocate(void *pool, size_t size,
                                 lle_memory_pool_type_t owner);
/**
 * @brief Round a requested size up to the next multiple of an alignment
 *
 * @param size Requested allocation size
 * @param alignment Required alignment in bytes
 * @return Aligned size
 */
size_t lle_align_memory_size(size_t size, size_t alignment);
/**
 * @brief Locate a free fragment in a shared pool large enough for a request
 *
 * @param pool Pool to search
 * @param size Minimum fragment size required
 * @return Fragment index on success, negative value if no fit is found
 */
int lle_find_suitable_fragment(void *pool, size_t size);
/**
 * @brief Remove a fragment entry from a shared pool's free list
 *
 * @param pool Pool whose fragment list is being modified
 * @param fragment_index Index of the fragment to remove
 */
void lle_remove_fragment(void *pool, int fragment_index);
/**
 * @brief Read the current monotonic time as a timespec
 *
 * @return Current time as a struct timespec
 */
struct timespec lle_get_current_time(void);

/* Buffer Memory Pool */
/**
 * @brief Allocate a typed buffer from a buffer memory pool
 *
 * @param pool Buffer memory pool to allocate from
 * @param size Buffer size in bytes
 * @param buffer_type Type of buffer being requested
 * @return New buffer allocation on success, NULL on failure
 */
void *lle_buffer_memory_allocate(lle_buffer_memory_pool_t *pool, size_t size,
                                 lle_buffer_type_t buffer_type);
/**
 * @brief Look up a cached string allocation of the requested size
 *
 * @param pool Buffer memory pool holding the string cache
 * @param size Desired string allocation size
 * @return Cached allocation on hit, NULL on miss
 */
void *lle_check_string_cache(lle_buffer_memory_pool_t *pool, size_t size);
/**
 * @brief Allocate a raw buffer block from the buffer memory pool
 *
 * @param pool Buffer memory pool to allocate from
 * @param size Block size in bytes
 * @return New buffer block on success, NULL on failure
 */
void *lle_allocate_buffer_block(lle_buffer_memory_pool_t *pool, size_t size);
/**
 * @brief Round a buffer request up to a more efficient allocation size
 *
 * @param size Requested buffer size
 * @param buffer_type Type of buffer being sized
 * @return Optimized allocation size
 */
size_t lle_optimize_buffer_allocation_size(size_t size,
                                           lle_buffer_type_t buffer_type);
/**
 * @brief Initialize a freshly allocated edit buffer to a known state
 *
 * @param buffer Edit buffer to initialize
 * @param size Size of the buffer in bytes
 */
void lle_initialize_edit_buffer(void *buffer, size_t size);

/* Hierarchical Allocation */
/**
 * @brief Allocate memory through the hierarchical pool fallback chain
 *
 * @param hierarchy Pool hierarchy to allocate from
 * @param size Number of bytes to allocate
 * @param preferred_type Preferred pool type for this allocation
 * @return New allocation on success, NULL on failure
 */
void *lle_hierarchical_allocate(lle_memory_pool_hierarchy_t *hierarchy,
                                size_t size,
                                lle_memory_pool_type_t preferred_type);
/**
 * @brief Choose an allocation strategy for a request of the given size
 *
 * @param size Number of bytes to allocate
 * @return Allocation strategy enum value
 */
lle_allocation_strategy_t lle_determine_allocation_strategy(size_t size);
/**
 * @brief Try to allocate from the primary pools of the hierarchy
 *
 * @param hierarchy Pool hierarchy to allocate from
 * @param size Number of bytes to allocate
 * @param preferred_type Preferred pool type for this allocation
 * @return New allocation on success, NULL on failure
 */
void *lle_try_primary_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                 size_t size,
                                 lle_memory_pool_type_t preferred_type);
/**
 * @brief Try to allocate from the secondary (fallback) pools
 *
 * @param hierarchy Pool hierarchy to allocate from
 * @param size Number of bytes to allocate
 * @param preferred_type Preferred pool type for this allocation
 * @return New allocation on success, NULL on failure
 */
void *lle_try_secondary_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size,
                                   lle_memory_pool_type_t preferred_type);
/**
 * @brief Try to allocate from the emergency reserve pool
 *
 * @param hierarchy Pool hierarchy holding the emergency reserve
 * @param size Number of bytes to allocate
 * @return New allocation on success, NULL on failure
 */
void *lle_try_emergency_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size);
/**
 * @brief Log that an emergency allocation occurred
 *
 * @param size Size of the emergency allocation in bytes
 * @param preferred_type Originally preferred pool type
 */
void lle_log_emergency_allocation(size_t size,
                                  lle_memory_pool_type_t preferred_type);
/**
 * @brief Handle a hierarchical allocation failure (logging, accounting, etc.)
 *
 * @param hierarchy Pool hierarchy where the failure occurred
 * @param size Size of the failed allocation
 * @param preferred_type Originally preferred pool type
 */
void lle_handle_allocation_failure(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size,
                                   lle_memory_pool_type_t preferred_type);

/* Dynamic Pool Resizing */
/**
 * @brief Run one dynamic-resize evaluation pass on a pool
 *
 * @param resizer Dynamic resizer driving the pool
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_dynamic_pool_resize(lle_dynamic_pool_resizer_t *resizer);
/**
 * @brief Calculate the current utilization ratio of a pool
 *
 * @param pool Pool to measure
 * @return Utilization ratio in the range [0.0, 1.0]
 */
double lle_calculate_pool_utilization(lle_memory_pool_t *pool);
/**
 * @brief Decide whether and how a pool should be resized
 *
 * @param resizer Dynamic resizer holding policy state
 * @param utilization Current utilization ratio for the pool
 * @return Resize decision describing action and reason
 */
lle_resize_decision_t
lle_evaluate_resize_need(lle_dynamic_pool_resizer_t *resizer,
                         double utilization);
/**
 * @brief Return the current size in bytes of a memory pool
 *
 * @param pool Pool to query
 * @return Pool size in bytes
 */
size_t lle_get_pool_size(lle_memory_pool_t *pool);
/**
 * @brief Clamp a value into the inclusive range [min, max]
 *
 * @param value Value to clamp
 * @param min Lower bound
 * @param max Upper bound
 * @return Clamped value
 */
size_t lle_clamp_size(size_t value, size_t min, size_t max);
/**
 * @brief Atomically resize a pool from old_size to new_size
 *
 * @param pool Pool to resize
 * @param old_size Expected current pool size
 * @param new_size Target pool size
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_atomic_pool_resize(lle_memory_pool_t *pool, size_t old_size,
                                    size_t new_size);
/**
 * @brief Record a utilization sample in the resizer's statistics
 *
 * @param resizer Dynamic resizer
 * @param utilization Utilization sample to record
 */
void lle_update_utilization_stats(lle_dynamic_pool_resizer_t *resizer,
                                  double utilization);
/**
 * @brief Grow a pool's backing memory by an additional number of bytes
 *
 * @param pool Pool to expand
 * @param additional_size Additional bytes to add to the pool
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_expand_pool_memory(lle_memory_pool_t *pool,
                                    size_t additional_size);
/**
 * @brief Shrink a pool's backing memory by a number of bytes
 *
 * @param pool Pool to compact
 * @param reduction_size Bytes to remove from the pool
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_compact_pool_memory(lle_memory_pool_t *pool,
                                     size_t reduction_size);
/**
 * @brief Recompute a pool's free-space bookkeeping after a structural change
 *
 * @param pool Pool whose free-space metadata should be refreshed
 */
void lle_recalculate_free_space(lle_memory_pool_t *pool);
/**
 * @brief Notify registered listeners that a pool has been resized
 *
 * @param pool Pool that was resized
 * @param old_size Pool size before the resize
 * @param new_size Pool size after the resize
 */
void lle_notify_pool_resize_listeners(lle_memory_pool_t *pool, size_t old_size,
                                      size_t new_size);

/* Garbage Collection */
/**
 * @brief Run a complete garbage-collection cycle
 *
 * @param gc Garbage collector to drive
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_perform_garbage_collection(lle_garbage_collector_t *gc);
/**
 * @brief Transition the garbage collector to a new state
 *
 * @param gc Garbage collector to transition
 * @param new_state Target GC state
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_gc_transition_state(lle_garbage_collector_t *gc,
                                     lle_gc_state_t new_state);
/**
 * @brief Run the mark phase of garbage collection
 *
 * @param gc Garbage collector running the mark phase
 * @param objects_marked Output parameter receiving the count of marked objects
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_gc_mark_phase(lle_garbage_collector_t *gc,
                               size_t *objects_marked);
/**
 * @brief Run the sweep phase of garbage collection
 *
 * @param gc Garbage collector running the sweep phase
 * @param memory_freed Output parameter receiving bytes reclaimed
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_gc_sweep_phase(lle_garbage_collector_t *gc,
                                size_t *memory_freed);
/**
 * @brief Run the compaction phase of garbage collection
 *
 * @param gc Garbage collector running the compact phase
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_gc_compact_phase(lle_garbage_collector_t *gc);
/**
 * @brief Compute the difference between two timespec values
 *
 * @param start Start timestamp
 * @param end End timestamp
 * @return Duration as end - start
 */
struct timespec lle_timespec_diff(struct timespec start, struct timespec end);
/**
 * @brief Record performance statistics from a completed GC cycle
 *
 * @param gc Garbage collector whose stats to update
 * @param gc_duration Total duration of the GC cycle
 * @param memory_freed Bytes reclaimed during the cycle
 */
void lle_update_gc_performance_stats(lle_garbage_collector_t *gc,
                                     struct timespec gc_duration,
                                     size_t memory_freed);

/* Buffer Memory Management */
/**
 * @brief Initialize a buffer memory descriptor from a configuration
 *
 * @param buffer_mem Buffer memory state to initialize
 * @param config Configuration describing buffer sizing and features
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_initialize_buffer_memory(lle_buffer_memory_t *buffer_mem,
                                          const lle_buffer_config_t *config);
/**
 * @brief Tear down and free buffer memory regions back to a pool
 *
 * @param buffer_mem Buffer memory state to clean up
 * @param pool Pool that backs the buffer regions
 */
void lle_cleanup_buffer_regions(lle_buffer_memory_t *buffer_mem,
                                lle_memory_pool_t *pool);
/**
 * @brief Initialize UTF-8 tracking for a buffer memory descriptor
 *
 * @param buffer_mem Buffer memory state to initialize UTF-8 tracking for
 * @param config Buffer configuration with UTF-8 sizing parameters
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_initialize_utf8_management(lle_buffer_memory_t *buffer_mem,
                                            const lle_buffer_config_t *config);
/**
 * @brief Compute the scratch-buffer size required by a buffer config
 *
 * @param config Buffer configuration
 * @return Scratch buffer size in bytes
 */
size_t lle_calculate_scratch_buffer_size(const lle_buffer_config_t *config);

/* Multiline Buffer Management */
/**
 * @brief Insert a line into a multiline buffer at the given index
 *
 * @param multiline_buffer Multiline buffer being modified
 * @param buffer_memory Backing buffer memory
 * @param line_index Index at which to insert the line
 * @param line_text Bytes of the line to insert
 * @param line_length Length in bytes of line_text
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_insert_line(lle_multiline_buffer_t *multiline_buffer,
                             lle_buffer_memory_t *buffer_memory,
                             size_t line_index, const char *line_text,
                             size_t line_length);
/**
 * @brief Grow the per-line tracking arrays of a multiline buffer
 *
 * @param multiline_buffer Multiline buffer whose arrays should be expanded
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_expand_line_tracking_arrays(lle_multiline_buffer_t *multiline_buffer);
/**
 * @brief Test whether a buffer has at least the required free space
 *
 * @param buffer_memory Buffer memory state to inspect
 * @param required_space Number of bytes required
 * @return true if at least required_space bytes are available
 */
bool lle_buffer_has_space(lle_buffer_memory_t *buffer_memory,
                          size_t required_space);
/**
 * @brief Grow the primary buffer region by an additional number of bytes
 *
 * @param buffer_memory Buffer memory state to grow
 * @param additional_space Additional bytes to make available
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_expand_primary_buffer(lle_buffer_memory_t *buffer_memory,
                                       size_t additional_space);
/**
 * @brief Compute the number of bytes after a given offset in the buffer
 *
 * @param buffer_memory Buffer memory state to inspect
 * @param offset Byte offset within the buffer
 * @return Number of bytes from offset to end of populated buffer
 */
size_t lle_calculate_buffer_tail_size(lle_buffer_memory_t *buffer_memory,
                                      size_t offset);
/**
 * @brief Mark a specific line in a multiline buffer as modified
 *
 * @param multiline_buffer Multiline buffer to update
 * @param line_index Index of the line that was modified
 */
void lle_mark_line_modified(lle_multiline_buffer_t *multiline_buffer,
                            size_t line_index);
/**
 * @brief Refresh UTF-8 tracking metadata after a byte insertion
 *
 * @param buffer_memory Buffer memory whose UTF-8 tracking to update
 * @param offset Byte offset where the insertion occurred
 * @param size Number of bytes inserted
 */
void lle_update_utf8_tracking_after_insertion(
    lle_buffer_memory_t *buffer_memory, size_t offset, size_t size);

/* Event Memory Integration */
/**
 * @brief Fast-path allocation for an event of a given type
 *
 * @param integration Event memory integration state
 * @param event_type Type of event to allocate
 * @param event_size Size in bytes of the event object
 * @return New event allocation on success, NULL on failure
 */
void *lle_allocate_event_fast(lle_event_memory_integration_t *integration,
                              lle_event_type_t event_type, size_t event_size);
/**
 * @brief Allocate an event from the input-event cache
 *
 * @param integration Event memory integration state
 * @return Input event allocation on success, NULL on failure
 */
void *
lle_allocate_from_input_cache(lle_event_memory_integration_t *integration);
/**
 * @brief Allocate an event from the display-event cache
 *
 * @param integration Event memory integration state
 * @return Display event allocation on success, NULL on failure
 */
void *
lle_allocate_from_display_cache(lle_event_memory_integration_t *integration);
/**
 * @brief Allocate an event from the system-event cache
 *
 * @param integration Event memory integration state
 * @return System event allocation on success, NULL on failure
 */
void *
lle_allocate_from_system_cache(lle_event_memory_integration_t *integration);
/**
 * @brief Fast-path free for an event allocated via lle_allocate_event_fast
 *
 * @param integration Event memory integration state
 * @param event_ptr Pointer to the event to free
 * @param event_type Type of the event being freed
 * @param event_size Size in bytes of the event object
 */
void lle_free_event_fast(lle_event_memory_integration_t *integration,
                         void *event_ptr, lle_event_type_t event_type,
                         size_t event_size);
/**
 * @brief Return an input event to its cache for reuse
 *
 * @param integration Event memory integration state
 * @param event_ptr Pointer to the input event to return
 * @return true if the event was returned to the cache, false otherwise
 */
bool lle_return_to_input_cache(lle_event_memory_integration_t *integration,
                               void *event_ptr);
/**
 * @brief Return a display event to its cache for reuse
 *
 * @param integration Event memory integration state
 * @param event_ptr Pointer to the display event to return
 * @return true if the event was returned to the cache, false otherwise
 */
bool lle_return_to_display_cache(lle_event_memory_integration_t *integration,
                                 void *event_ptr);
/**
 * @brief Return a system event to its cache for reuse
 *
 * @param integration Event memory integration state
 * @param event_ptr Pointer to the system event to return
 * @return true if the event was returned to the cache, false otherwise
 */
bool lle_return_to_system_cache(lle_event_memory_integration_t *integration,
                                void *event_ptr);

/* Memory Access Pattern Optimization */
/**
 * @brief Analyze recent memory access patterns and update optimizer state
 *
 * @param optimizer Memory access optimizer to update
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_analyze_memory_access_patterns(lle_memory_access_optimizer_t *optimizer);
/**
 * @brief Analyze recent access records and produce a pattern analysis
 *
 * @param optimizer Memory access optimizer holding access history
 * @param pattern_analysis Output parameter receiving the analysis result
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_analyze_recent_accesses(lle_memory_access_optimizer_t *optimizer,
                            lle_access_pattern_analysis_t *pattern_analysis);
/**
 * @brief Identify hot memory regions and record them in the analysis
 *
 * @param optimizer Memory access optimizer
 * @param pattern_analysis Pattern analysis to populate with hot regions
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_identify_hot_regions(lle_memory_access_optimizer_t *optimizer,
                         lle_access_pattern_analysis_t *pattern_analysis);
/**
 * @brief Recalculate locality scores from the optimizer's access history
 *
 * @param optimizer Memory access optimizer to update
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_calculate_locality_scores(lle_memory_access_optimizer_t *optimizer);
/**
 * @brief Update the prefetch strategy based on a fresh pattern analysis
 *
 * @param optimizer Memory access optimizer
 * @param pattern_analysis Pattern analysis driving the strategy choice
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_update_prefetch_strategy(lle_memory_access_optimizer_t *optimizer,
                             lle_access_pattern_analysis_t *pattern_analysis);
/**
 * @brief Decide whether the memory layout should be reorganized
 *
 * @param pattern_analysis Pattern analysis to evaluate
 * @return true if a layout optimization is recommended
 */
bool lle_should_optimize_layout(
    lle_access_pattern_analysis_t *pattern_analysis);
/**
 * @brief Reorganize the memory layout to improve locality
 *
 * @param optimizer Memory access optimizer driving the layout change
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_optimize_memory_layout(lle_memory_access_optimizer_t *optimizer);
/**
 * @brief Log performance statistics from a memory-analysis pass
 *
 * @param duration Time spent in the analysis pass
 * @param pattern_analysis Pattern analysis result produced by the pass
 */
void lle_log_memory_analysis_performance(
    struct timespec duration, lle_access_pattern_analysis_t *pattern_analysis);

/* Memory Pool Performance Tuning */
/**
 * @brief Run one tuning pass over the pools managed by a tuner
 *
 * @param tuner Pool performance tuner to drive
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_tune_memory_pool_performance(lle_memory_pool_tuner_t *tuner);
/**
 * @brief Measure performance characteristics of a memory pool
 *
 * @param pool Pool to measure
 * @param sample_size Number of samples to collect
 * @param performance Output parameter receiving measured metrics
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_measure_pool_performance(lle_memory_pool_t *pool, size_t sample_size,
                             lle_memory_pool_performance_t *performance);
/**
 * @brief Analyze a performance measurement to identify bottlenecks
 *
 * @param tuner Pool performance tuner
 * @param current_performance Measured performance metrics
 * @param bottleneck_analysis Output parameter receiving the bottleneck analysis
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_analyze_performance_bottlenecks(
    lle_memory_pool_tuner_t *tuner,
    lle_memory_pool_performance_t *current_performance,
    lle_performance_bottleneck_analysis_t *bottleneck_analysis);
/**
 * @brief Build a plan of tuning actions from a bottleneck analysis
 *
 * @param tuner Pool performance tuner
 * @param bottleneck_analysis Bottlenecks the plan should address
 * @param action_plan Output parameter receiving the tuning action plan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_create_tuning_action_plan(
    lle_memory_pool_tuner_t *tuner,
    lle_performance_bottleneck_analysis_t *bottleneck_analysis,
    lle_tuning_action_plan_t *action_plan);
/**
 * @brief Execute a single tuning action against the tuner's pools
 *
 * @param tuner Pool performance tuner
 * @param action Tuning action to execute
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_execute_tuning_action(lle_memory_pool_tuner_t *tuner,
                                       lle_tuning_action_item_t *action);
/**
 * @brief Roll back tuning actions in a plan up to a given action index
 *
 * @param tuner Pool performance tuner
 * @param action_plan Plan whose actions should be reverted
 * @param action_index Number of actions to roll back
 */
void lle_rollback_tuning_actions(lle_memory_pool_tuner_t *tuner,
                                 lle_tuning_action_plan_t *action_plan,
                                 size_t action_index);
/**
 * @brief Update a running average timespec with a new sample
 *
 * @param average Running average to update in place
 * @param new_sample New time sample to incorporate
 * @param sample_count Number of samples observed so far (including this one)
 */
void lle_update_average_time(struct timespec *average,
                             struct timespec new_sample, size_t sample_count);

/* Error Detection and Recovery */
/**
 * @brief Run all enabled memory-error detection passes
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager whose memory should be scanned
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_detect_memory_errors(lle_memory_error_handler_t *error_handler,
                                      lle_memory_manager_t *memory_manager);
/**
 * @brief Scan for memory leaks in the memory manager
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager to scan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_detect_memory_leaks(lle_memory_error_handler_t *error_handler,
                                     lle_memory_manager_t *memory_manager);
/**
 * @brief Scan for buffer bounds violations in the memory manager
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager to scan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_detect_bounds_violations(lle_memory_error_handler_t *error_handler,
                             lle_memory_manager_t *memory_manager);
/**
 * @brief Scan for memory corruption in the memory manager
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager to scan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_detect_memory_corruption(lle_memory_error_handler_t *error_handler,
                             lle_memory_manager_t *memory_manager);
/**
 * @brief Scan for double-free attempts in the memory manager
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager to scan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_detect_double_free_attempts(lle_memory_error_handler_t *error_handler,
                                lle_memory_manager_t *memory_manager);
/**
 * @brief Scan for use-after-free errors in the memory manager
 *
 * @param error_handler Memory error handler driving the scan
 * @param memory_manager Memory manager to scan
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_detect_use_after_free(lle_memory_error_handler_t *error_handler,
                          lle_memory_manager_t *memory_manager);
/**
 * @brief Record a memory error result in the handler's history
 *
 * @param error_handler Memory error handler to update
 * @param error_result Error result to record
 */
void lle_record_memory_error(lle_memory_error_handler_t *error_handler,
                             lle_result_t error_result);
/**
 * @brief Attempt to recover from a detected memory error
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Error descriptor to recover from
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_memory_error(lle_memory_error_handler_t *error_handler,
                              lle_memory_error_t *error,
                              lle_memory_manager_t *memory_manager);
/**
 * @brief Choose a recovery strategy for a given memory error
 *
 * @param error_handler Memory error handler with policy state
 * @param error Error descriptor to classify
 * @return Recovery strategy enum value
 */
lle_memory_recovery_strategy_t
lle_determine_recovery_strategy(lle_memory_error_handler_t *error_handler,
                                lle_memory_error_t *error);
/**
 * @brief Recover from a memory leak
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Leak descriptor
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_memory_leak(lle_memory_error_handler_t *error_handler,
                             lle_memory_error_t *error,
                             lle_memory_manager_t *memory_manager);
/**
 * @brief Recover from a bounds violation
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Bounds-violation descriptor
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_bounds_violation(lle_memory_error_handler_t *error_handler,
                                  lle_memory_error_t *error,
                                  lle_memory_manager_t *memory_manager);
/**
 * @brief Recover from memory corruption
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Corruption descriptor
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_corruption(lle_memory_error_handler_t *error_handler,
                            lle_memory_error_t *error,
                            lle_memory_manager_t *memory_manager);
/**
 * @brief Recover from a double-free
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Double-free descriptor
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_double_free(lle_memory_error_handler_t *error_handler,
                             lle_memory_error_t *error,
                             lle_memory_manager_t *memory_manager);
/**
 * @brief Recover from a use-after-free
 *
 * @param error_handler Memory error handler driving recovery
 * @param error Use-after-free descriptor
 * @param memory_manager Memory manager being repaired
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_recover_from_use_after_free(lle_memory_error_handler_t *error_handler,
                                lle_memory_error_t *error,
                                lle_memory_manager_t *memory_manager);

/* Buffer Overflow Protection */
/**
 * @brief Check whether an access fits within the bounds of a tracked buffer
 *
 * @param protection Buffer overflow protection state
 * @param buffer_ptr Pointer being accessed
 * @param access_size Size of the access in bytes
 * @param access_type Read/write classification of the access
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_check_buffer_bounds(lle_buffer_overflow_protection_t *protection,
                        void *buffer_ptr, size_t access_size,
                        lle_access_type_t access_type);
/**
 * @brief Convert an access-type enum into a permission bitmask
 *
 * @param access_type Access classification
 * @return Permission bitmask corresponding to the access type
 */
uint32_t lle_access_type_to_permissions(lle_access_type_t access_type);
/**
 * @brief Log a memory-security incident with address and size context
 *
 * @param incident_type Kind of security incident observed
 * @param address Memory address involved in the incident
 * @param size Size in bytes associated with the incident
 */
void lle_log_security_incident(lle_security_incident_t incident_type,
                               void *address, size_t size);

/* Memory Encryption */
/**
 * @brief Encrypt a memory allocation according to a sensitivity level
 *
 * @param encryption Memory encryption subsystem state
 * @param memory_ptr Pointer to the allocation to encrypt
 * @param memory_size Size of the allocation in bytes
 * @param sensitivity Data sensitivity classification driving algorithm choice
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_encrypt_memory_allocation(lle_memory_encryption_t *encryption,
                                           void *memory_ptr, size_t memory_size,
                                           lle_data_sensitivity_t sensitivity);
/**
 * @brief Encrypt a buffer in place using a specific algorithm and key
 *
 * @param data Buffer to encrypt in place
 * @param size Size of the buffer in bytes
 * @param key Encryption key bytes
 * @param key_size Length of the key in bytes
 * @param algorithm Encryption algorithm to apply
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_encrypt_data_in_place(void *data, size_t size, uint8_t *key,
                                       size_t key_size,
                                       lle_encryption_algorithm_t algorithm);

/* Complete Integration */
/**
 * @brief Initialize the complete LLE-Lush memory integration subsystem
 *
 * @param integration Integration descriptor to initialize
 * @param lle_manager LLE memory manager handle
 * @param lush_system Lush memory system handle
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_initialize_complete_memory_integration(
    lle_lush_memory_integration_complete_t *integration,
    lle_memory_manager_t *lle_manager, lush_memory_system_t *lush_system);
/**
 * @brief Tear down the synchronization state for the integration
 *
 * @param integration Integration descriptor to clean up
 */
void lle_cleanup_integration_sync(
    lle_lush_memory_integration_complete_t *integration);
/**
 * @brief Establish shared memory regions for the complete integration
 *
 * @param integration Integration descriptor whose shared regions to create
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_establish_shared_memory_regions(
    lle_lush_memory_integration_complete_t *integration);
/**
 * @brief Configure the cooperation mode of the LLE-Lush integration
 *
 * @param integration Integration descriptor to configure
 * @param mode Desired integration mode
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t lle_configure_integration_mode(
    lle_lush_memory_integration_complete_t *integration,
    lle_integration_mode_t mode);
/**
 * @brief Release shared memory regions held by the integration
 *
 * @param integration Integration descriptor whose shared regions to destroy
 */
void lle_cleanup_shared_memory_regions(
    lle_lush_memory_integration_complete_t *integration);

/* Display Memory Coordination */
/**
 * @brief Allocate display memory using the optimized coordination path
 *
 * @param coord Display memory coordination state
 * @param type Display memory type being requested
 * @param size Allocation size in bytes
 * @return New display allocation on success, NULL on failure
 */
void *
lle_allocate_display_memory_optimized(lle_display_memory_coordination_t *coord,
                                      lle_display_memory_type_t type,
                                      size_t size);
/**
 * @brief Attempt to recycle a previously freed display buffer
 *
 * @param coord Display memory coordination state
 * @param type Display memory type being requested
 * @param size Required allocation size in bytes
 * @return Recycled buffer on success, NULL if no suitable buffer is available
 */
void *lle_try_recycle_display_buffer(lle_display_memory_coordination_t *coord,
                                     lle_display_memory_type_t type,
                                     size_t size);
/**
 * @brief Compute the current display-memory pressure metric
 *
 * @param coord Display memory coordination state
 * @return Pressure value in the range [0.0, 1.0]
 */
double lle_calculate_memory_pressure(lle_display_memory_coordination_t *coord);
/**
 * @brief Apply memory-pressure relief actions for display memory
 *
 * @param coord Display memory coordination state to relieve
 */
void lle_apply_memory_pressure_relief(lle_display_memory_coordination_t *coord);

/* Testing and Validation */
/**
 * @brief Run the full suite of memory tests against a memory manager
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_comprehensive_memory_tests(lle_memory_test_framework_t *test_framework,
                                   lle_memory_manager_t *memory_manager);
/**
 * @brief Run basic memory allocation / free sanity tests
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_basic_memory_tests(lle_memory_test_framework_t *test_framework,
                           lle_memory_manager_t *memory_manager);
/**
 * @brief Record a test failure in the framework's failure log
 *
 * @param test_framework Memory test framework state
 * @param reason Reason category for the failure
 * @param result Result code returned by the failing test
 */
void lle_record_test_failure(lle_memory_test_framework_t *test_framework,
                             lle_test_failure_reason_t reason,
                             lle_result_t result);
/**
 * @brief Run stress tests against a memory manager
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_memory_stress_tests(lle_memory_test_framework_t *test_framework,
                            lle_memory_manager_t *memory_manager);
/**
 * @brief Run memory-leak detection tests against a memory manager
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_memory_leak_tests(lle_memory_test_framework_t *test_framework,
                          lle_memory_manager_t *memory_manager);
/**
 * @brief Run performance benchmarks against a memory manager
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_performance_benchmarks(lle_memory_test_framework_t *test_framework,
                               lle_memory_manager_t *memory_manager);
/**
 * @brief Run concurrency tests against a memory manager
 *
 * @param test_framework Memory test framework state
 * @param memory_manager Memory manager under test
 * @return LLE_SUCCESS or an error code on failure
 */
lle_result_t
lle_run_concurrency_tests(lle_memory_test_framework_t *test_framework,
                          lle_memory_manager_t *memory_manager);
/**
 * @brief Generate a final report summarizing memory test results
 *
 * @param test_framework Memory test framework state holding results
 * @param test_duration Total wall-clock time taken by the tests
 * @param overall_result Aggregated result code for the run
 */
void lle_generate_memory_test_report(
    lle_memory_test_framework_t *test_framework, struct timespec test_duration,
    lle_result_t overall_result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_MEMORY_MANAGEMENT_H */
