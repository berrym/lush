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
    LLE_POOL_BUFFER,
    LLE_POOL_EVENT,
    LLE_POOL_STRING,
    LLE_POOL_TEMP,
    LLE_POOL_HISTORY,
    LLE_POOL_SYNTAX,
    LLE_POOL_COMPLETION,
    LLE_POOL_CUSTOM,
    LLE_POOL_COUNT
} lle_memory_pool_type_t;

typedef enum {
    LLE_MEMORY_STATE_INITIALIZING,
    LLE_MEMORY_STATE_ACTIVE,
    LLE_MEMORY_STATE_OPTIMIZING,
    LLE_MEMORY_STATE_GC_RUNNING,
    LLE_MEMORY_STATE_LOW_MEMORY,
    LLE_MEMORY_STATE_ERROR,
    LLE_MEMORY_STATE_SHUTDOWN
} lle_memory_state_t;

typedef enum {
    LLE_GC_STRATEGY_MARK_SWEEP,
    LLE_GC_STRATEGY_MARK_SWEEP_COMPACT,
    LLE_GC_STRATEGY_GENERATIONAL,
    LLE_GC_STRATEGY_INCREMENTAL
} lle_gc_strategy_t;

typedef enum {
    LLE_GC_STATE_IDLE,
    LLE_GC_STATE_MARKING,
    LLE_GC_STATE_SWEEPING,
    LLE_GC_STATE_COMPACTING,
    LLE_GC_STATE_ERROR
} lle_gc_state_t;

typedef enum {
    LLE_POOL_SELECTION_ROUND_ROBIN,
    LLE_POOL_SELECTION_LEAST_USED,
    LLE_POOL_SELECTION_BEST_FIT,
    LLE_POOL_SELECTION_FIRST_FIT
} lle_pool_selection_algorithm_t;

typedef enum {
    LLE_STRATEGY_PRIMARY_ONLY,
    LLE_STRATEGY_SECONDARY_FALLBACK,
    LLE_STRATEGY_EMERGENCY_ONLY,
    LLE_STRATEGY_AUTOMATIC
} lle_allocation_strategy_t;

typedef enum {
    LLE_RESIZE_ACTION_NONE,
    LLE_RESIZE_ACTION_GROW,
    LLE_RESIZE_ACTION_SHRINK
} lle_resize_action_t;

typedef enum {
    LLE_RESIZE_REASON_UTILIZATION,
    LLE_RESIZE_REASON_FRAGMENTATION,
    LLE_RESIZE_REASON_PERFORMANCE,
    LLE_RESIZE_REASON_MANUAL
} lle_resize_reason_t;

typedef enum {
    LLE_ACCESS_TYPE_READ,
    LLE_ACCESS_TYPE_WRITE,
    LLE_ACCESS_TYPE_READ_WRITE
} lle_access_type_t;

typedef enum {
    LLE_PREFETCH_NONE,
    LLE_PREFETCH_SEQUENTIAL,
    LLE_PREFETCH_ADAPTIVE,
    LLE_PREFETCH_AGGRESSIVE
} lle_prefetch_strategy_t;

typedef enum {
    LLE_TUNING_ACTION_NONE,
    LLE_TUNING_ACTION_RESIZE,
    LLE_TUNING_ACTION_DEFRAGMENT,
    LLE_TUNING_ACTION_REORGANIZE
} lle_tuning_action_t;

typedef enum {
    LLE_MEMORY_ERROR_LEAK,
    LLE_MEMORY_ERROR_BOUNDS_VIOLATION,
    LLE_MEMORY_ERROR_CORRUPTION,
    LLE_MEMORY_ERROR_DOUBLE_FREE,
    LLE_MEMORY_ERROR_USE_AFTER_FREE
} lle_memory_error_type_t;

typedef enum {
    LLE_MEMORY_RECOVERY_ABORT,
    LLE_MEMORY_RECOVERY_ISOLATE,
    LLE_MEMORY_RECOVERY_REPAIR,
    LLE_MEMORY_RECOVERY_RESTART
} lle_memory_recovery_strategy_t;

typedef enum {
    LLE_ENCRYPTION_NONE,
    LLE_ENCRYPTION_AES_128,
    LLE_ENCRYPTION_AES_256,
    LLE_ENCRYPTION_CHACHA20
} lle_encryption_algorithm_t;

typedef enum {
    LLE_DATA_SENSITIVITY_LOW,
    LLE_DATA_SENSITIVITY_MEDIUM,
    LLE_DATA_SENSITIVITY_HIGH,
    LLE_DATA_SENSITIVITY_CRITICAL
} lle_data_sensitivity_t;

typedef enum {
    LLE_SECURITY_BOUNDS_VIOLATION,
    LLE_SECURITY_PERMISSION_VIOLATION,
    LLE_SECURITY_ENCRYPTION_FAILURE,
    LLE_SECURITY_CORRUPTION_DETECTED
} lle_security_incident_t;

typedef enum {
    LLE_INTEGRATION_MODE_STANDALONE,
    LLE_INTEGRATION_MODE_COOPERATIVE,
    LLE_INTEGRATION_MODE_UNIFIED
} lle_integration_mode_t;

typedef enum {
    LLE_DISPLAY_MEMORY_PROMPT,
    LLE_DISPLAY_MEMORY_SYNTAX,
    LLE_DISPLAY_MEMORY_AUTOSUGGESTION,
    LLE_DISPLAY_MEMORY_COMPOSITION
} lle_display_memory_type_t;

typedef enum {
    LLE_TEST_FAILURE_BASIC_ALLOCATION,
    LLE_TEST_FAILURE_STRESS_TEST,
    LLE_TEST_FAILURE_MEMORY_LEAK,
    LLE_TEST_FAILURE_PERFORMANCE,
    LLE_TEST_FAILURE_CONCURRENCY
} lle_test_failure_reason_t;

typedef enum {
    LLE_EVENT_TYPE_INPUT,
    LLE_EVENT_TYPE_DISPLAY,
    LLE_EVENT_TYPE_SYSTEM
} lle_event_type_t;

typedef enum {
    LLE_BUFFER_TYPE_STRING,
    LLE_BUFFER_TYPE_EDIT,
    LLE_BUFFER_TYPE_TEMP
} lle_buffer_type_t;

typedef enum {
    LLE_COMPRESSION_NONE,
    LLE_COMPRESSION_LZ4,
    LLE_COMPRESSION_ZSTD,
    LLE_COMPRESSION_SNAPPY
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
    int error_code;
    char error_message[256];
} lle_integration_error_t;

/* ============================================================================
 * NOTE: Full struct definitions moved to memory_management.c for encapsulation
 * Only forward declarations (typedef struct) remain in header
 * ============================================================================
 */

typedef struct {
    lle_memory_pool_type_t type;
    size_t initial_size;
    size_t max_size;
    size_t block_size;
    size_t alignment;
    double growth_factor;
    size_t gc_threshold;
    bool enable_compression;
    bool enable_bounds_checking;
    bool enable_encryption;
    bool enable_poisoning;
    bool share_with_lush;
    lush_memory_pool_t *parent_pool;
} lle_memory_pool_config_t;

typedef struct {
    size_t pool_sizes[LLE_POOL_COUNT];
    size_t max_pool_sizes[LLE_POOL_COUNT];
    size_t block_size;
    size_t alignment;
} lle_memory_config_t;

typedef struct {
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t current_usage;
    uint64_t peak_usage;
    double allocation_rate;
    double deallocation_rate;
} lle_memory_stats_t;

typedef struct {
    lle_resize_action_t action;
    lle_resize_reason_t reason;
} lle_resize_decision_t;

typedef struct {
    double locality_score;
    double sequential_ratio;
    size_t hot_region_count;
} lle_access_pattern_analysis_t;

typedef struct {
    lle_memory_error_type_t error_type;
    void *error_address;
    size_t error_size;
    struct timespec error_time;
    char error_description[256];
} lle_memory_error_t;

/* lle_integration_error_t moved earlier - before complete structure definitions
 */

typedef struct {
    lle_tuning_action_t action;
    size_t parameter;
} lle_tuning_action_item_t;

typedef struct {
    lle_tuning_action_item_t actions[16];
    size_t action_count;
} lle_tuning_action_plan_t;

typedef struct {
    double allocation_rate;
    double deallocation_rate;
    struct timespec average_allocation_time;
    struct timespec peak_allocation_time;
    double fragmentation_ratio;
    double utilization_efficiency;
} lle_memory_pool_performance_t;

typedef struct {
    bool high_fragmentation;
    bool slow_allocations;
    bool poor_locality;
} lle_performance_bottleneck_analysis_t;

/* ============================================================================
 * CORE PUBLIC API - Essential Functions
 * ============================================================================
 */

/* Primary allocation/deallocation */
void *lle_pool_alloc(size_t size);
void lle_pool_free(void *ptr);
void *lle_pool_allocate(lle_memory_pool_base_t *pool, size_t size);
void *lle_pool_allocate_aligned(lle_memory_pool_t *pool, size_t size,
                                size_t alignment);
void *lle_pool_allocate_fast(lle_memory_pool_t *pool, size_t size);
void lle_pool_free_fast(lle_memory_pool_t *pool, void *ptr);

/* Lush Memory Pool Integration Bridge */
lle_result_t lle_memory_pool_create_from_lush(lle_memory_pool_t **lle_pool,
                                              lush_memory_pool_t *lush_pool,
                                              lle_memory_pool_type_t pool_type);
void lle_memory_pool_destroy(lle_memory_pool_t *pool);

/* Memory State Management */
lle_result_t lle_memory_transition_state(lle_memory_manager_t *manager,
                                         lle_memory_state_t new_state);
bool lle_memory_is_valid_transition(lle_memory_state_t old_state,
                                    lle_memory_state_t new_state);
lle_result_t lle_memory_initialize_pools(lle_memory_manager_t *manager);
void lle_memory_start_monitoring(lle_memory_manager_t *manager);
lle_result_t lle_memory_start_optimization(lle_memory_manager_t *manager);
lle_result_t lle_memory_start_garbage_collection(lle_memory_manager_t *manager);
lle_result_t lle_memory_handle_low_memory(lle_memory_manager_t *manager);
lle_result_t lle_memory_handle_error_state(lle_memory_manager_t *manager);
lle_result_t lle_memory_shutdown_pools(lle_memory_manager_t *manager);

/* Lush Integration */
lle_result_t lle_integrate_with_lush_memory(lle_memory_manager_t *manager);
lush_memory_pool_t *lush_get_memory_pools(void);
lle_result_t lle_analyze_lush_memory_config(lush_memory_pool_t *lush_pools,
                                            lle_memory_config_t *lush_config);
lle_result_t
lle_create_specialized_pool(lle_memory_manager_t *manager,
                            const lle_memory_pool_config_t *pool_config);
void lle_cleanup_partial_integration(lle_memory_manager_t *manager,
                                     size_t pool_index);
lle_result_t
lle_create_shared_memory_regions(lle_memory_manager_t *manager,
                                 const lle_memory_config_t *lush_config);
lle_result_t
lle_initialize_cross_allocation_tables(lle_memory_manager_t *manager);
lle_result_t lle_start_integration_monitoring(lle_memory_manager_t *manager);

/* Shared Memory Pool */
void *lle_shared_memory_allocate(void *pool, size_t size,
                                 lle_memory_pool_type_t owner);
size_t lle_align_memory_size(size_t size, size_t alignment);
int lle_find_suitable_fragment(void *pool, size_t size);
void lle_remove_fragment(void *pool, int fragment_index);
struct timespec lle_get_current_time(void);

/* Buffer Memory Pool */
void *lle_buffer_memory_allocate(lle_buffer_memory_pool_t *pool, size_t size,
                                 lle_buffer_type_t buffer_type);
void *lle_check_string_cache(lle_buffer_memory_pool_t *pool, size_t size);
void *lle_allocate_buffer_block(lle_buffer_memory_pool_t *pool, size_t size);
size_t lle_optimize_buffer_allocation_size(size_t size,
                                           lle_buffer_type_t buffer_type);
void lle_initialize_edit_buffer(void *buffer, size_t size);

/* Hierarchical Allocation */
void *lle_hierarchical_allocate(lle_memory_pool_hierarchy_t *hierarchy,
                                size_t size,
                                lle_memory_pool_type_t preferred_type);
lle_allocation_strategy_t lle_determine_allocation_strategy(size_t size);
void *lle_try_primary_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                 size_t size,
                                 lle_memory_pool_type_t preferred_type);
void *lle_try_secondary_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size,
                                   lle_memory_pool_type_t preferred_type);
void *lle_try_emergency_allocation(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size);
void lle_log_emergency_allocation(size_t size,
                                  lle_memory_pool_type_t preferred_type);
void lle_handle_allocation_failure(lle_memory_pool_hierarchy_t *hierarchy,
                                   size_t size,
                                   lle_memory_pool_type_t preferred_type);

/* Dynamic Pool Resizing */
lle_result_t lle_dynamic_pool_resize(lle_dynamic_pool_resizer_t *resizer);
double lle_calculate_pool_utilization(lle_memory_pool_t *pool);
lle_resize_decision_t
lle_evaluate_resize_need(lle_dynamic_pool_resizer_t *resizer,
                         double utilization);
size_t lle_get_pool_size(lle_memory_pool_t *pool);
size_t lle_clamp_size(size_t value, size_t min, size_t max);
lle_result_t lle_atomic_pool_resize(lle_memory_pool_t *pool, size_t old_size,
                                    size_t new_size);
void lle_update_utilization_stats(lle_dynamic_pool_resizer_t *resizer,
                                  double utilization);
lle_result_t lle_expand_pool_memory(lle_memory_pool_t *pool,
                                    size_t additional_size);
lle_result_t lle_compact_pool_memory(lle_memory_pool_t *pool,
                                     size_t reduction_size);
void lle_recalculate_free_space(lle_memory_pool_t *pool);
void lle_notify_pool_resize_listeners(lle_memory_pool_t *pool, size_t old_size,
                                      size_t new_size);

/* Garbage Collection */
lle_result_t lle_perform_garbage_collection(lle_garbage_collector_t *gc);
lle_result_t lle_gc_transition_state(lle_garbage_collector_t *gc,
                                     lle_gc_state_t new_state);
lle_result_t lle_gc_mark_phase(lle_garbage_collector_t *gc,
                               size_t *objects_marked);
lle_result_t lle_gc_sweep_phase(lle_garbage_collector_t *gc,
                                size_t *memory_freed);
lle_result_t lle_gc_compact_phase(lle_garbage_collector_t *gc);
struct timespec lle_timespec_diff(struct timespec start, struct timespec end);
void lle_update_gc_performance_stats(lle_garbage_collector_t *gc,
                                     struct timespec gc_duration,
                                     size_t memory_freed);

/* Buffer Memory Management */
lle_result_t lle_initialize_buffer_memory(lle_buffer_memory_t *buffer_mem,
                                          const lle_buffer_config_t *config);
void lle_cleanup_buffer_regions(lle_buffer_memory_t *buffer_mem,
                                lle_memory_pool_t *pool);
lle_result_t lle_initialize_utf8_management(lle_buffer_memory_t *buffer_mem,
                                            const lle_buffer_config_t *config);
size_t lle_calculate_scratch_buffer_size(const lle_buffer_config_t *config);

/* Multiline Buffer Management */
lle_result_t lle_insert_line(lle_multiline_buffer_t *multiline_buffer,
                             lle_buffer_memory_t *buffer_memory,
                             size_t line_index, const char *line_text,
                             size_t line_length);
lle_result_t
lle_expand_line_tracking_arrays(lle_multiline_buffer_t *multiline_buffer);
bool lle_buffer_has_space(lle_buffer_memory_t *buffer_memory,
                          size_t required_space);
lle_result_t lle_expand_primary_buffer(lle_buffer_memory_t *buffer_memory,
                                       size_t additional_space);
size_t lle_calculate_buffer_tail_size(lle_buffer_memory_t *buffer_memory,
                                      size_t offset);
void lle_mark_line_modified(lle_multiline_buffer_t *multiline_buffer,
                            size_t line_index);
void lle_update_utf8_tracking_after_insertion(
    lle_buffer_memory_t *buffer_memory, size_t offset, size_t size);

/* Event Memory Integration */
void *lle_allocate_event_fast(lle_event_memory_integration_t *integration,
                              lle_event_type_t event_type, size_t event_size);
void *
lle_allocate_from_input_cache(lle_event_memory_integration_t *integration);
void *
lle_allocate_from_display_cache(lle_event_memory_integration_t *integration);
void *
lle_allocate_from_system_cache(lle_event_memory_integration_t *integration);
void lle_free_event_fast(lle_event_memory_integration_t *integration,
                         void *event_ptr, lle_event_type_t event_type,
                         size_t event_size);
bool lle_return_to_input_cache(lle_event_memory_integration_t *integration,
                               void *event_ptr);
bool lle_return_to_display_cache(lle_event_memory_integration_t *integration,
                                 void *event_ptr);
bool lle_return_to_system_cache(lle_event_memory_integration_t *integration,
                                void *event_ptr);

/* Memory Access Pattern Optimization */
lle_result_t
lle_analyze_memory_access_patterns(lle_memory_access_optimizer_t *optimizer);
lle_result_t
lle_analyze_recent_accesses(lle_memory_access_optimizer_t *optimizer,
                            lle_access_pattern_analysis_t *pattern_analysis);
lle_result_t
lle_identify_hot_regions(lle_memory_access_optimizer_t *optimizer,
                         lle_access_pattern_analysis_t *pattern_analysis);
lle_result_t
lle_calculate_locality_scores(lle_memory_access_optimizer_t *optimizer);
lle_result_t
lle_update_prefetch_strategy(lle_memory_access_optimizer_t *optimizer,
                             lle_access_pattern_analysis_t *pattern_analysis);
bool lle_should_optimize_layout(
    lle_access_pattern_analysis_t *pattern_analysis);
lle_result_t
lle_optimize_memory_layout(lle_memory_access_optimizer_t *optimizer);
void lle_log_memory_analysis_performance(
    struct timespec duration, lle_access_pattern_analysis_t *pattern_analysis);

/* Memory Pool Performance Tuning */
lle_result_t lle_tune_memory_pool_performance(lle_memory_pool_tuner_t *tuner);
lle_result_t
lle_measure_pool_performance(lle_memory_pool_t *pool, size_t sample_size,
                             lle_memory_pool_performance_t *performance);
lle_result_t lle_analyze_performance_bottlenecks(
    lle_memory_pool_tuner_t *tuner,
    lle_memory_pool_performance_t *current_performance,
    lle_performance_bottleneck_analysis_t *bottleneck_analysis);
lle_result_t lle_create_tuning_action_plan(
    lle_memory_pool_tuner_t *tuner,
    lle_performance_bottleneck_analysis_t *bottleneck_analysis,
    lle_tuning_action_plan_t *action_plan);
lle_result_t lle_execute_tuning_action(lle_memory_pool_tuner_t *tuner,
                                       lle_tuning_action_item_t *action);
void lle_rollback_tuning_actions(lle_memory_pool_tuner_t *tuner,
                                 lle_tuning_action_plan_t *action_plan,
                                 size_t action_index);
void lle_update_average_time(struct timespec *average,
                             struct timespec new_sample, size_t sample_count);

/* Error Detection and Recovery */
lle_result_t lle_detect_memory_errors(lle_memory_error_handler_t *error_handler,
                                      lle_memory_manager_t *memory_manager);
lle_result_t lle_detect_memory_leaks(lle_memory_error_handler_t *error_handler,
                                     lle_memory_manager_t *memory_manager);
lle_result_t
lle_detect_bounds_violations(lle_memory_error_handler_t *error_handler,
                             lle_memory_manager_t *memory_manager);
lle_result_t
lle_detect_memory_corruption(lle_memory_error_handler_t *error_handler,
                             lle_memory_manager_t *memory_manager);
lle_result_t
lle_detect_double_free_attempts(lle_memory_error_handler_t *error_handler,
                                lle_memory_manager_t *memory_manager);
lle_result_t
lle_detect_use_after_free(lle_memory_error_handler_t *error_handler,
                          lle_memory_manager_t *memory_manager);
void lle_record_memory_error(lle_memory_error_handler_t *error_handler,
                             lle_result_t error_result);
lle_result_t
lle_recover_from_memory_error(lle_memory_error_handler_t *error_handler,
                              lle_memory_error_t *error,
                              lle_memory_manager_t *memory_manager);
lle_memory_recovery_strategy_t
lle_determine_recovery_strategy(lle_memory_error_handler_t *error_handler,
                                lle_memory_error_t *error);
lle_result_t
lle_recover_from_memory_leak(lle_memory_error_handler_t *error_handler,
                             lle_memory_error_t *error,
                             lle_memory_manager_t *memory_manager);
lle_result_t
lle_recover_from_bounds_violation(lle_memory_error_handler_t *error_handler,
                                  lle_memory_error_t *error,
                                  lle_memory_manager_t *memory_manager);
lle_result_t
lle_recover_from_corruption(lle_memory_error_handler_t *error_handler,
                            lle_memory_error_t *error,
                            lle_memory_manager_t *memory_manager);
lle_result_t
lle_recover_from_double_free(lle_memory_error_handler_t *error_handler,
                             lle_memory_error_t *error,
                             lle_memory_manager_t *memory_manager);
lle_result_t
lle_recover_from_use_after_free(lle_memory_error_handler_t *error_handler,
                                lle_memory_error_t *error,
                                lle_memory_manager_t *memory_manager);

/* Buffer Overflow Protection */
lle_result_t
lle_check_buffer_bounds(lle_buffer_overflow_protection_t *protection,
                        void *buffer_ptr, size_t access_size,
                        lle_access_type_t access_type);
uint32_t lle_access_type_to_permissions(lle_access_type_t access_type);
void lle_log_security_incident(lle_security_incident_t incident_type,
                               void *address, size_t size);

/* Memory Encryption */
lle_result_t lle_encrypt_memory_allocation(lle_memory_encryption_t *encryption,
                                           void *memory_ptr, size_t memory_size,
                                           lle_data_sensitivity_t sensitivity);
lle_result_t lle_encrypt_data_in_place(void *data, size_t size, uint8_t *key,
                                       size_t key_size,
                                       lle_encryption_algorithm_t algorithm);

/* Complete Integration */
lle_result_t lle_initialize_complete_memory_integration(
    lle_lush_memory_integration_complete_t *integration,
    lle_memory_manager_t *lle_manager, lush_memory_system_t *lush_system);
void lle_cleanup_integration_sync(
    lle_lush_memory_integration_complete_t *integration);
lle_result_t lle_establish_shared_memory_regions(
    lle_lush_memory_integration_complete_t *integration);
lle_result_t lle_configure_integration_mode(
    lle_lush_memory_integration_complete_t *integration,
    lle_integration_mode_t mode);
void lle_cleanup_shared_memory_regions(
    lle_lush_memory_integration_complete_t *integration);

/* Display Memory Coordination */
void *
lle_allocate_display_memory_optimized(lle_display_memory_coordination_t *coord,
                                      lle_display_memory_type_t type,
                                      size_t size);
void *lle_try_recycle_display_buffer(lle_display_memory_coordination_t *coord,
                                     lle_display_memory_type_t type,
                                     size_t size);
double lle_calculate_memory_pressure(lle_display_memory_coordination_t *coord);
void lle_apply_memory_pressure_relief(lle_display_memory_coordination_t *coord);

/* Testing and Validation */
lle_result_t
lle_run_comprehensive_memory_tests(lle_memory_test_framework_t *test_framework,
                                   lle_memory_manager_t *memory_manager);
lle_result_t
lle_run_basic_memory_tests(lle_memory_test_framework_t *test_framework,
                           lle_memory_manager_t *memory_manager);
void lle_record_test_failure(lle_memory_test_framework_t *test_framework,
                             lle_test_failure_reason_t reason,
                             lle_result_t result);
lle_result_t
lle_run_memory_stress_tests(lle_memory_test_framework_t *test_framework,
                            lle_memory_manager_t *memory_manager);
lle_result_t
lle_run_memory_leak_tests(lle_memory_test_framework_t *test_framework,
                          lle_memory_manager_t *memory_manager);
lle_result_t
lle_run_performance_benchmarks(lle_memory_test_framework_t *test_framework,
                               lle_memory_manager_t *memory_manager);
lle_result_t
lle_run_concurrency_tests(lle_memory_test_framework_t *test_framework,
                          lle_memory_manager_t *memory_manager);
void lle_generate_memory_test_report(
    lle_memory_test_framework_t *test_framework, struct timespec test_duration,
    lle_result_t overall_result);

#ifdef __cplusplus
}
#endif

#endif /* LLE_MEMORY_MANAGEMENT_H */
