/**
 * @file testing.h
 * @brief LLE Testing Framework - Type Definitions and Function Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 17 - Testing Framework Complete Specification
 * Version: 1.0.0
 *
 * This header contains ALL type definitions and function declarations for the
 * LLE testing framework. NO implementations are included here.
 *
 * Layer 0: Type Definitions Only
 * Layer 1: Implementations in src/lle/testing.c (separate file)
 */

#ifndef LLE_TESTING_H
#define LLE_TESTING_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/// Include LLE dependencies
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/// Performance Constants
#define LLE_PERF_MAX_RESPONSE_TIME_NS 500000ULL    /// 500μs
#define LLE_PERF_MAX_ALLOCATION_TIME_NS 100000ULL  /// 100μs
#define LLE_PERF_MAX_RENDER_TIME_NS 1000000ULL     /// 1ms
#define LLE_PERF_MAX_EVENT_PROCESSING_NS 250000ULL /// 250μs
#define LLE_PERF_MIN_CACHE_HIT_RATE 75.0           /// 75%
#define LLE_PERF_MIN_MEMORY_UTILIZATION 85.0       /// 85%
#define LLE_PERF_MAX_REGRESSION_PERCENT 10.0       /// 10%

/// Size/Limit Constants
#define LLE_MAX_TEST_NAME_LENGTH 256
#define LLE_MAX_TEST_DESC_LENGTH 1024
#define LLE_MAX_TEST_DEPENDENCIES 32
#define LLE_MAX_PATH_LENGTH 4096

/// Test Registration Macros
#define LLE_REGISTER_TEST(name, type, priority, description)                   \
    static lle_test_result_t test_##name(lle_test_context_t *ctx);             \
    static lle_test_case_t                                                     \
        __attribute__((section("lle_tests"))) test_case_##name = {             \
            .test_name = #name,                                                \
            .test_description = description,                                   \
            .test_type = type,                                                 \
            .priority = priority,                                              \
            .test_function = test_##name,                                      \
            .setup_function = NULL,                                            \
            .teardown_function = NULL,                                         \
    };                                                                         \
    static lle_test_result_t test_##name(lle_test_context_t *ctx)

/// Assertion Macros
#define LLE_ASSERT_EQ(expected, actual, message)                               \
    do {                                                                       \
        if ((expected) != (actual)) {                                          \
            lle_test_record_assertion_failure(                                 \
                ctx, __FILE__, __LINE__,                                       \
                "Expected: %s, Actual: %s, Message: %s", #expected, #actual,   \
                message);                                                      \
            return LLE_TEST_RESULT_ASSERTION_FAILED;                           \
        }                                                                      \
        lle_test_record_assertion_success(ctx, __FILE__, __LINE__);            \
    } while (0)

#define LLE_ASSERT_NULL(ptr, message) LLE_ASSERT_EQ(NULL, ptr, message)

#define LLE_ASSERT_NOT_NULL(ptr, message)                                      \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            lle_test_record_assertion_failure(                                 \
                ctx, __FILE__, __LINE__,                                       \
                "Expected non-null pointer, Message: %s", message);            \
            return LLE_TEST_RESULT_ASSERTION_FAILED;                           \
        }                                                                      \
        lle_test_record_assertion_success(ctx, __FILE__, __LINE__);            \
    } while (0)

#define LLE_ASSERT_PERFORMANCE(operation, max_duration_us, message)            \
    do {                                                                       \
        uint64_t start_time = lle_get_microsecond_timestamp();                 \
        operation;                                                             \
        uint64_t duration = lle_get_microsecond_timestamp() - start_time;      \
        if (duration > (max_duration_us)) {                                    \
            lle_test_record_performance_failure(                               \
                ctx, __FILE__, __LINE__,                                       \
                "Performance exceeded: %llu us > %llu us, Message: %s",        \
                duration, (uint64_t)(max_duration_us), message);               \
            return LLE_TEST_RESULT_PERFORMANCE_FAILED;                         \
        }                                                                      \
        lle_test_record_performance_success(ctx, __FILE__, __LINE__,           \
                                            duration);                         \
    } while (0)

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

/// Core testing framework types
typedef struct lle_testing_framework_t lle_testing_framework_t;
typedef struct lle_test_suite_registry_t lle_test_suite_registry_t;
typedef struct lle_test_runner_t lle_test_runner_t;
typedef struct lle_test_reporter_t lle_test_reporter_t;
typedef struct lle_performance_tracker_t lle_performance_tracker_t;
typedef struct lle_memory_validator_t lle_memory_validator_t;
typedef struct lle_error_injector_t lle_error_injector_t;
typedef struct lle_coverage_analyzer_t lle_coverage_analyzer_t;
typedef struct lle_regression_detector_t lle_regression_detector_t;
typedef struct lle_test_environment_t lle_test_environment_t;
typedef struct lle_mock_system_t lle_mock_system_t;
typedef struct lle_fixture_manager_t lle_fixture_manager_t;
typedef struct lle_quality_metrics_t lle_quality_metrics_t;
typedef struct lle_test_statistics_t lle_test_statistics_t;
typedef struct lle_benchmark_database_t lle_benchmark_database_t;

/// Test case and suite types
typedef struct lle_test_case_t lle_test_case_t;
typedef struct lle_test_suite_t lle_test_suite_t;
typedef struct lle_test_context_t lle_test_context_t;
typedef struct lle_test_assertion_list_t lle_test_assertion_list_t;
typedef struct lle_performance_expectations_t lle_performance_expectations_t;
typedef struct lle_error_scenarios_t lle_error_scenarios_t;

/// Performance testing types
typedef struct lle_performance_testing_framework_t
    lle_performance_testing_framework_t;
typedef struct lle_regression_analyzer_t lle_regression_analyzer_t;
typedef struct lle_performance_reporter_t lle_performance_reporter_t;

/// Memory testing types
typedef struct lle_memory_safety_framework_t lle_memory_safety_framework_t;
typedef struct lle_leak_detector_t lle_leak_detector_t;
typedef struct lle_corruption_detector_t lle_corruption_detector_t;
typedef struct lle_usage_analyzer_t lle_usage_analyzer_t;

/// Error injection types
typedef struct lle_error_injection_system_t lle_error_injection_system_t;
typedef struct lle_error_recovery_monitor_t lle_error_recovery_monitor_t;

/// CI/CD types
typedef struct lle_ci_pipeline_t lle_ci_pipeline_t;
typedef struct lle_ci_trigger_t lle_ci_trigger_t;
typedef struct lle_test_stage_t lle_test_stage_t;
typedef struct lle_artifact_manager_t lle_artifact_manager_t;
typedef struct lle_notification_system_t lle_notification_system_t;

/// Reporting types
typedef struct lle_test_reporting_framework_t lle_test_reporting_framework_t;
typedef struct lle_report_generator_t lle_report_generator_t;
typedef struct lle_analytics_engine_t lle_analytics_engine_t;
typedef struct lle_trend_analyzer_t lle_trend_analyzer_t;
typedef struct lle_dashboard_manager_t lle_dashboard_manager_t;
typedef struct lle_trend_data_t lle_trend_data_t;

/// Integration types
typedef struct lle_display_integration_t lle_display_integration_t;
typedef struct lle_autosuggestions_t lle_autosuggestions_t;
typedef struct lle_plugin_manager_t lle_plugin_manager_t;
typedef struct lle_plugin_t lle_plugin_t;

/// External LLE system types (from other specs)
typedef struct lle_unix_terminal_t lle_unix_terminal_t;
typedef struct lle_buffer_t lle_buffer_t;
typedef struct lle_event_system lle_event_system_t;
typedef struct lle_history_system_t lle_history_system_t;
typedef struct lle_system_t lle_system_t;
typedef struct lle_display_context_t lle_display_context_t;

/// External Lush types
typedef struct lush_display_controller_t lush_display_controller_t;

/// Plugin registry types
typedef struct lle_plugin_registry_t lle_plugin_registry_t;
typedef struct lle_custom_assertion_registry_t lle_custom_assertion_registry_t;
typedef struct lle_report_plugin_registry_t lle_report_plugin_registry_t;
typedef struct lle_metrics_plugin_registry_t lle_metrics_plugin_registry_t;
typedef struct lle_test_runner_interface_t lle_test_runner_interface_t;

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Test Type Classification
 */
typedef enum {
    LLE_TEST_TYPE_UNIT = 1,
    LLE_TEST_TYPE_INTEGRATION,
    LLE_TEST_TYPE_PERFORMANCE,
    LLE_TEST_TYPE_MEMORY,
    LLE_TEST_TYPE_ERROR_RECOVERY,
    LLE_TEST_TYPE_REGRESSION,
    LLE_TEST_TYPE_LOAD,
    LLE_TEST_TYPE_COMPATIBILITY,
    LLE_TEST_TYPE_SECURITY,
    LLE_TEST_TYPE_PLUGIN,
    LLE_TEST_TYPE_END_TO_END,
    LLE_TEST_TYPE_SMOKE,
} lle_test_type_t;

/**
 * @brief Test Execution Priorities
 */
typedef enum {
    LLE_TEST_PRIORITY_CRITICAL = 1,
    LLE_TEST_PRIORITY_HIGH,
    LLE_TEST_PRIORITY_MEDIUM,
    LLE_TEST_PRIORITY_LOW,
    LLE_TEST_PRIORITY_NIGHTLY,
} lle_test_priority_t;

/**
 * @brief Test Result Types
 */
typedef enum {
    LLE_TEST_RESULT_SUCCESS = 0,
    LLE_TEST_RESULT_ASSERTION_FAILED,
    LLE_TEST_RESULT_PERFORMANCE_FAILED,
    LLE_TEST_RESULT_MEMORY_LEAK,
    LLE_TEST_RESULT_TIMEOUT,
    LLE_TEST_RESULT_ERROR,
    LLE_TEST_RESULT_SKIPPED,
} lle_test_result_t;

/**
 * @brief CI Trigger Types
 */
typedef enum {
    LLE_CI_TRIGGER_COMMIT = 1,
    LLE_CI_TRIGGER_PULL_REQUEST,
    LLE_CI_TRIGGER_MERGE_TO_MAIN,
    LLE_CI_TRIGGER_NIGHTLY,
    LLE_CI_TRIGGER_RELEASE_TAG,
    LLE_CI_TRIGGER_MANUAL,
} lle_ci_trigger_type_t;

/**
 * @brief CI Stage Types
 */
typedef enum {
    LLE_CI_STAGE_FAST_VALIDATION = 1,
    LLE_CI_STAGE_UNIT_TESTS,
    LLE_CI_STAGE_INTEGRATION_TESTS,
    LLE_CI_STAGE_PERFORMANCE_TESTS,
    LLE_CI_STAGE_MEMORY_VALIDATION,
    LLE_CI_STAGE_CROSS_PLATFORM,
    LLE_CI_STAGE_SECURITY_ANALYSIS,
    LLE_CI_STAGE_REGRESSION_DETECTION,
} lle_ci_stage_type_t;

/**
 * @brief CI Status
 */
typedef enum {
    LLE_CI_STATUS_SUCCESS = 0,
    LLE_CI_STATUS_PARTIAL_SUCCESS,
    LLE_CI_STATUS_FAILED,
    LLE_CI_STATUS_RUNNING,
    LLE_CI_STATUS_CANCELLED,
} lle_ci_status_t;

/**
 * @brief Report Format Flags
 */
typedef enum {
    LLE_REPORT_FORMAT_CONSOLE = 1 << 0,
    LLE_REPORT_FORMAT_JUNIT = 1 << 1,
    LLE_REPORT_FORMAT_HTML = 1 << 2,
    LLE_REPORT_FORMAT_JSON = 1 << 3,
    LLE_REPORT_FORMAT_CSV = 1 << 4,
    LLE_REPORT_FORMAT_MARKDOWN = 1 << 5,
    LLE_REPORT_FORMAT_PDF = 1 << 6,
} lle_report_format_t;

/**
 * @brief Test-specific Error Types (for injection)
 * Note: Common error types are defined in error_handling.h
 */
typedef enum {
    LLE_TEST_ERROR_PLUGIN_PERMISSION_DENIED = 10000,
    LLE_TEST_ERROR_PLUGIN_MEMORY_LIMIT_EXCEEDED,
    LLE_TEST_ERROR_PLUGIN_EXECUTION_TIMEOUT,
    LLE_TEST_ERROR_FILE_CREATION_FAILED,
    LLE_TEST_ERROR_CI_PIPELINE_FAILED,
    LLE_TEST_ERROR_TEST_FAILURES_DETECTED,
} lle_test_error_type_t;

/**
 * @brief Recovery Actions (for error injection testing)
 */
typedef enum {
    LLE_RECOVERY_ACTION_FALLBACK_ALLOCATION = 1,
    LLE_RECOVERY_ACTION_GRACEFUL_DEGRADATION,
    LLE_RECOVERY_ACTION_RETRY,
} lle_recovery_action_t;

/**
 * @brief Sandbox Levels
 */
typedef enum {
    LLE_SANDBOX_LEVEL_STRICT = 1,
    LLE_SANDBOX_LEVEL_MODERATE,
    LLE_SANDBOX_LEVEL_RELAXED,
} lle_sandbox_level_t;

/**
 * @brief Plugin Permissions
 */
typedef enum {
    LLE_PLUGIN_PERM_BUFFER_READ = 1 << 0,
    LLE_PLUGIN_PERM_BUFFER_WRITE = 1 << 1,
    LLE_PLUGIN_PERM_EVENT_HANDLE = 1 << 2,
    LLE_PLUGIN_PERM_FILE_READ = 1 << 3,
    LLE_PLUGIN_PERM_FILE_WRITE = 1 << 4,
    LLE_PLUGIN_PERM_NETWORK = 1 << 5,
} lle_plugin_permission_t;

/**
 * @brief Pool Types
 */
typedef enum {
    LLE_POOL_TYPE_EDITING = 1,
    LLE_POOL_TYPE_TESTING,
} lle_pool_type_t;

/**
 * @brief Test Event Priorities
 * Note: Event types are defined in memory_management.h (lle_event_type_t)
 */
typedef enum {
    LLE_TEST_EVENT_PRIORITY_CRITICAL = 1,
    LLE_TEST_EVENT_PRIORITY_HIGH,
    LLE_TEST_EVENT_PRIORITY_MEDIUM,
    LLE_TEST_EVENT_PRIORITY_LOW,
} lle_test_event_priority_t;

/* ============================================================================
 * STRUCTURE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Function pointer types for test functions
 */
typedef lle_test_result_t (*lle_test_function_t)(lle_test_context_t *ctx);
typedef lle_result_t (*lle_test_setup_function_t)(lle_test_context_t *ctx);
typedef lle_result_t (*lle_test_teardown_function_t)(lle_test_context_t *ctx);
typedef lle_result_t (*lle_performance_test_function_t)(
    void *context, lle_cache_performance_metrics_t *metrics);

/**
 * @brief Test Case Definition
 */
struct lle_test_case_t {
    char test_name[LLE_MAX_TEST_NAME_LENGTH];
    char test_description[LLE_MAX_TEST_DESC_LENGTH];
    lle_test_type_t test_type;
    lle_test_priority_t priority;
    lle_test_function_t test_function;
    lle_test_setup_function_t setup_function;
    lle_test_teardown_function_t teardown_function;

    /// Test requirements and dependencies
    char *required_components[LLE_MAX_TEST_DEPENDENCIES];
    double expected_max_duration_ms;
    size_t expected_max_memory_bytes;

    /// Test validation criteria
    lle_test_assertion_list_t *assertions;
    lle_performance_expectations_t *perf_expectations;
    lle_error_scenarios_t *error_scenarios;
};

/**
 * @brief Core Testing Framework
 */
struct lle_testing_framework_t {
    lle_test_suite_registry_t *suite_registry;
    lle_test_runner_t *test_runner;
    lle_test_reporter_t *reporter;
    lle_performance_tracker_t *perf_tracker;
    lle_memory_validator_t *memory_validator;
    lle_error_injector_t *error_injector;
    lle_coverage_analyzer_t *coverage_analyzer;
    lle_regression_detector_t *regression_detector;

    /// Test execution context
    lle_test_environment_t *test_environment;
    lle_mock_system_t *mock_system;
    lle_fixture_manager_t *fixture_manager;

    /// Quality assurance metrics
    lle_quality_metrics_t *quality_metrics;
    lle_test_statistics_t *test_statistics;
    lle_benchmark_database_t *benchmark_db;
};

/**
 * @brief Performance Benchmark
 */
typedef struct {
    char test_name[LLE_MAX_TEST_NAME_LENGTH];
    lle_cache_performance_metrics_t baseline_metrics;
    lle_cache_performance_metrics_t current_metrics;
    double regression_threshold_percent;
    uint64_t last_updated_timestamp;
    uint32_t sample_count;

    /// Statistical analysis
    double mean_duration_us;
    double std_deviation_us;
    double min_duration_us;
    double max_duration_us;
    double percentile_95_us;
    double percentile_99_us;
} lle_performance_benchmark_t;

/**
 * @brief Performance Statistics
 */
typedef struct {
    uint64_t mean_duration_ns;
    uint64_t std_deviation_ns;
    uint64_t min_duration_ns;
    uint64_t max_duration_ns;
    uint64_t percentile_95_ns;
    uint64_t percentile_99_ns;
} lle_performance_statistics_t;

/**
 * @brief Performance Result
 */
typedef struct {
    lle_performance_statistics_t statistics;
    bool regression_detected;
    double performance_change_percent;
    uint32_t sample_count;
} lle_performance_result_t;

/**
 * @brief Performance Testing Framework
 */
struct lle_performance_testing_framework_t {
    lle_hashtable_t *benchmark_database;
    lle_performance_monitor_t *monitor;
    lle_regression_analyzer_t *analyzer;
    lle_performance_reporter_t *reporter;

    /// Test configuration
    uint32_t warmup_iterations;
    uint32_t measurement_iterations;
    uint32_t statistical_confidence_level;
    double regression_threshold_percent;
};

/**
 * @brief Test Error Injection Configuration
 * Note: lle_error_injection_config_t is defined in error_handling.h
 */
typedef struct {
    lle_result_t error_type;
    uint32_t injection_probability_percent;
    uint32_t injection_delay_ms;
    bool auto_recovery_enabled;
    lle_recovery_action_t expected_recovery;
} lle_test_error_injection_config_t;

/**
 * @brief Error Injection System
 */
struct lle_error_injection_system_t {
    lle_test_error_injection_config_t *injection_configs;
    size_t config_count;
    lle_hashtable_t *injection_history;
    lle_error_recovery_monitor_t *recovery_monitor;
    uint32_t total_injections;
    uint32_t successful_recoveries;
    uint32_t failed_recoveries;
};

/**
 * @brief Recovery Info
 */
typedef struct {
    lle_recovery_action_t strategy;
    bool recovery_successful;
    uint64_t recovery_time_us;
    char recovery_message[256];
} lle_recovery_info_t;

/**
 * @brief Memory Safety Framework
 */
struct lle_memory_safety_framework_t {
    lle_memory_tracker_t *tracker;
    lle_leak_detector_t *leak_detector;
    lle_corruption_detector_t *corruption_detector;
    lle_usage_analyzer_t *usage_analyzer;

    /// Testing configuration
    bool enable_allocation_tracking;
    bool enable_leak_detection;
    bool enable_corruption_detection;
    bool enable_double_free_detection;
    bool enable_use_after_free_detection;

    /// Memory testing statistics
    uint64_t total_allocations;
    uint64_t total_deallocations;
    uint64_t peak_memory_usage_bytes;
    uint64_t current_memory_usage_bytes;
    uint32_t detected_leaks;
    uint32_t detected_corruptions;
};

/**
 * @brief Memory Snapshot
 */
typedef struct {
    uint64_t timestamp;
    uint64_t allocated_bytes;
    uint64_t allocation_count;
    uint32_t active_blocks;
} lle_memory_snapshot_t;

/**
 * @brief Memory Leak Report
 */
typedef struct {
    uint32_t leaked_blocks;
    uint64_t leaked_bytes;
    char **leak_locations;
    size_t location_count;
} lle_memory_leak_report_t;

/**
 * @brief Memory Pool Stats
 */
typedef struct {
    uint64_t total_size_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t active_allocations;
    uint32_t total_allocations;
    uint32_t failed_allocations;
    double fragmentation_ratio;
    uint64_t leaked_bytes;
} lle_memory_pool_stats_t;

/**
 * @brief Memory Validator Config
 */
typedef struct {
    bool enable_leak_detection;
    bool enable_corruption_detection;
    bool enable_double_free_detection;
    bool enable_use_after_free_detection;
    bool zero_tolerance_policy;
} lle_memory_validator_config_t;

/**
 * @brief Test Runner Config
 */
typedef struct {
    uint32_t max_parallel_tests;
    uint32_t timeout_seconds;
    bool enable_performance_monitoring;
    bool enable_memory_tracking;
    bool enable_error_injection;
} lle_test_runner_config_t;

/**
 * @brief Regression Detector Config
 */
typedef struct {
    double performance_threshold_percent;
    double memory_threshold_percent;
    bool enable_statistical_analysis;
    uint32_t confidence_level;
} lle_regression_detector_config_t;

/**
 * @brief Test Execution Result
 */
typedef struct {
    lle_test_result_t result;
    char *failure_reason;
    uint64_t execution_time_us;
    uint64_t peak_memory_usage;
} lle_test_execution_result_t;

/**
 * @brief Test Failure Info
 */
typedef struct {
    const char *test_name;
    const char *failure_reason;
    uint64_t execution_time_us;
    uint64_t memory_usage_bytes;
} lle_test_failure_info_t;

/**
 * @brief Test Results
 */
typedef struct {
    uint64_t start_timestamp;
    uint64_t end_timestamp;
    uint64_t total_duration_us;

    size_t total_tests;
    size_t passed_tests;
    size_t failed_tests;

    uint64_t total_execution_time_us;
    uint64_t peak_memory_usage;

    lle_test_failure_info_t *failures;
    size_t failure_count;
} lle_test_results_t;

/**
 * @brief Test Run Config
 */
typedef struct {
    bool include_nightly_tests;
    bool fail_fast;
    bool parallel_execution;
    uint32_t max_parallel_jobs;
} lle_test_run_config_t;

/**
 * @brief CI Trigger Context
 */
typedef struct {
    lle_ci_trigger_type_t trigger_type;
    char *commit_sha;
    char *branch_name;
    char *author;
    uint64_t timestamp;
} lle_ci_trigger_context_t;

/**
 * @brief CI Stage Result
 */
typedef struct {
    lle_ci_stage_type_t stage_type;
    lle_ci_status_t status;
    uint64_t start_time;
    uint64_t end_time;
    uint64_t duration_us;
    const char *failure_reason;
} lle_ci_stage_result_t;

/**
 * @brief CI Results
 */
typedef struct {
    uint64_t pipeline_start_time;
    uint64_t pipeline_end_time;
    uint64_t total_duration_us;
    lle_ci_trigger_type_t trigger_type;
    lle_ci_status_t pipeline_status;

    lle_ci_stage_result_t stage_results[16];
    size_t stage_count;
    size_t passed_stages;
    size_t failed_stages;

    const char *failure_reason;
} lle_ci_results_t;

/**
 * @brief CI Pipeline
 */
struct lle_ci_pipeline_t {
    lle_ci_trigger_t *triggers;
    size_t trigger_count;
    lle_test_stage_t *stages;
    size_t stage_count;
    lle_artifact_manager_t *artifact_manager;
    lle_notification_system_t *notifications;

    /// Pipeline configuration
    bool parallel_stage_execution;
    uint32_t max_concurrent_jobs;
    uint32_t timeout_minutes;
    bool fail_fast_enabled;
};

/**
 * @brief Test Reporting Framework
 */
struct lle_test_reporting_framework_t {
    lle_report_generator_t *generators;
    size_t generator_count;
    lle_analytics_engine_t *analytics;
    lle_trend_analyzer_t *trend_analyzer;
    lle_dashboard_manager_t *dashboard;

    /// Reporting configuration
    lle_report_format_t supported_formats;
    char output_directory[LLE_MAX_PATH_LENGTH];
    bool enable_real_time_updates;
    uint32_t retention_days;
};

/**
 * @brief Test Analytics
 */
typedef struct {
    /// Test execution statistics
    uint64_t total_test_runs;
    uint64_t successful_runs;
    uint64_t failed_runs;
    double success_rate_percent;

    /// Performance analytics
    double average_execution_time_ms;
    double median_execution_time_ms;
    double percentile_95_execution_time_ms;
    uint64_t fastest_test_time_us;
    uint64_t slowest_test_time_us;

    /// Memory usage analytics
    uint64_t average_memory_usage_bytes;
    uint64_t peak_memory_usage_bytes;
    uint32_t memory_leak_incidents;
    uint32_t memory_corruption_incidents;

    /// Reliability metrics
    double test_stability_score;
    uint32_t flaky_test_count;
    double mean_time_between_failures_hours;
    double mean_time_to_recovery_minutes;

    /// Coverage metrics
    double code_coverage_percent;
    double branch_coverage_percent;
    uint32_t uncovered_lines;
    uint32_t uncovered_branches;

    /// Trend analysis
    lle_trend_data_t *performance_trends;
    lle_trend_data_t *reliability_trends;
    lle_trend_data_t *coverage_trends;
} lle_test_analytics_t;

/**
 * @brief Test Report
 */
typedef struct {
    lle_test_results_t *results;
    lle_test_analytics_t *analytics;
    char *html_output;
    char *json_output;
    char *junit_xml;
} lle_test_report_t;

/**
 * @brief Performance Report (opaque)
 */
typedef struct lle_performance_report_t lle_performance_report_t;

/**
 * @brief Integration Requirements
 */
typedef struct {
    bool requires_zero_allocation_testing;
    bool requires_leak_detection;
    bool requires_pool_stress_testing;
    uint64_t max_allocation_time_ns;
    uint64_t max_deallocation_time_ns;
    size_t min_pool_utilization_percent;
} lle_memory_pool_integration_t;

typedef struct {
    bool requires_layered_display_compatibility;
    bool requires_theme_compatibility;
    bool requires_real_time_updates;
    uint64_t max_render_time_ns;
    uint32_t min_refresh_rate_hz;
    bool requires_cursor_synchronization;
} lle_test_display_integration_t;

typedef struct {
    uint64_t max_response_time_ns;
    double min_cache_hit_rate_percent;
    uint64_t max_memory_overhead_bytes;
    uint32_t max_cpu_usage_percent;
    bool requires_sub_millisecond_operations;
} lle_test_performance_integration_t;

typedef struct {
    bool requires_theme_compatibility;
} lle_test_theme_integration_t;

typedef struct {
    bool requires_config_validation;
} lle_test_config_integration_t;

typedef struct {
    lle_memory_pool_integration_t memory_integration;
    lle_test_display_integration_t display_integration;
    lle_test_theme_integration_t theme_integration;
    lle_test_config_integration_t config_integration;
    lle_test_performance_integration_t performance_integration;
} lle_integration_requirements_t;

/**
 * @brief Performance Requirements
 */
typedef struct {
    uint64_t max_response_time_ns;
    uint64_t max_allocation_time_ns;
    uint64_t max_render_time_ns;
    uint64_t max_event_processing_time_ns;
    double min_cache_hit_rate_percent;
    double min_memory_utilization_percent;
    double max_regression_percent;
} lle_performance_requirements_t;

/**
 * @brief Plugin Configuration
 */
typedef struct {
    const char *name;
    lle_plugin_permission_t permissions;
    size_t memory_limit_bytes;
    uint32_t execution_timeout_ms;
    lle_sandbox_level_t sandbox_level;
} lle_plugin_config_t;

/**
 * @brief Terminal Capabilities
 *
 * Note: Testing-specific version. When terminal_abstraction.h is included
 * (Spec 02), that provides the authoritative complete definition.
 */
#ifndef LLE_TERMINAL_ABSTRACTION_H
typedef struct {
    bool supports_colors;
    bool supports_cursor_movement;
    bool supports_mouse;
    uint32_t color_depth;
    uint32_t max_width;
    uint32_t max_height;
} lle_terminal_capabilities_t;
#endif /// LLE_TERMINAL_ABSTRACTION_H

/**
 * @brief Test Input Event
 * Note: lle_input_event_t is defined in memory_management.h
 */
typedef struct {
    lle_event_type_t type;
    lle_test_event_priority_t priority;
    union {
        struct {
            char key;
            uint32_t modifiers;
        } key_data;
        struct {
            int32_t x;
            int32_t y;
            uint32_t buttons;
        } mouse_data;
    };
    void *data;
} lle_test_input_event_t;

/**
 * @brief Cursor Position
 *
 * Note: Testing framework version. When buffer_management.h is included,
 * use the complete lle_cursor_position_t from Spec 03.
 */
#ifndef LLE_BUFFER_MANAGEMENT_H
typedef struct {
    uint32_t line;
    uint32_t column;
} lle_cursor_position_t;
#endif /// LLE_BUFFER_MANAGEMENT_H

/**
 * @brief Lush Cursor Info
 */
typedef struct {
    uint32_t line;
    uint32_t column;
    bool visible;
} lush_cursor_info_t;

/**
 * @brief Suggestion Result
 */
typedef struct {
    char *suggestion_text;
    double confidence;
    char *source;
} lle_suggestion_result_t;

/**
 * @brief Performance Metrics
 */
typedef struct {
    uint64_t total_operations;
    uint64_t total_duration_ns;
    uint64_t min_duration_ns;
    uint64_t max_duration_ns;
    double average_duration_ns;
    double cache_hit_rate_percent;
    uint64_t memory_usage_bytes;
} lle_performance_metrics_t;

/**
 * @brief Plugin Capabilities
 */
typedef struct {
    bool supports_parallel_execution;
    bool supports_performance_monitoring;
    bool supports_memory_tracking;
    uint32_t max_concurrent_tests;
} lle_plugin_capabilities_t;

/**
 * @brief Test Metrics
 */
typedef struct {
    uint64_t execution_time_ns;
    uint64_t memory_usage_bytes;
} lle_test_metrics_t;

/**
 * @brief Test Runner Plugin
 */
typedef struct {
    const char *plugin_name;
    const char *plugin_version;
    lle_test_runner_interface_t *interface;
    lle_plugin_capabilities_t capabilities;

    /// Plugin functions
    lle_result_t (*initialize)(void *config);
    lle_result_t (*execute_test)(lle_test_case_t *test,
                                 lle_test_result_t *result);
    lle_result_t (*cleanup)(void);
    lle_result_t (*get_performance_metrics)(lle_test_metrics_t *metrics);
} lle_test_runner_plugin_t;

/**
 * @brief Testing Extensibility
 */
typedef struct {
    lle_plugin_registry_t *test_plugin_registry;
    lle_custom_assertion_registry_t *assertions;
    lle_report_plugin_registry_t *report_plugins;
    lle_metrics_plugin_registry_t *metrics_plugins;
    lle_analytics_engine_t *analytics_engine;

    /// Extensibility configuration
    bool enable_plugin_system;
    bool enable_custom_metrics;
    bool enable_ml_analytics;
    char plugin_directory[LLE_MAX_PATH_LENGTH];
} lle_testing_extensibility_t;

/* ============================================================================
 * FUNCTION DECLARATIONS - Framework Initialization and Lifecycle
 * ============================================================================
 */

lle_result_t
lle_testing_framework_initialize(lle_testing_framework_t **framework);
lle_result_t lle_testing_framework_destroy(lle_testing_framework_t *framework);
lle_result_t
lle_testing_framework_run_all_tests(lle_testing_framework_t *framework,
                                    lle_test_run_config_t *config,
                                    lle_test_results_t *results);

/* ============================================================================
 * FUNCTION DECLARATIONS - Test Discovery and Registration
 * ============================================================================
 */

lle_result_t
lle_test_discovery_scan_and_register(lle_testing_framework_t *framework);
const char *lle_test_type_to_suite_name(lle_test_type_t test_type);

/* ============================================================================
 * FUNCTION DECLARATIONS - Test Suite Management
 * ============================================================================
 */

lle_test_suite_registry_t *lle_test_suite_registry_create(void);
void lle_test_suite_registry_destroy(lle_test_suite_registry_t *registry);
lle_result_t
lle_test_suite_registry_get_all_suites(lle_test_suite_registry_t *registry,
                                       lle_test_suite_t ***suites,
                                       size_t *suite_count);
lle_test_suite_t *
lle_test_suite_registry_find_suite(lle_test_suite_registry_t *registry,
                                   const char *suite_name);
lle_result_t
lle_test_suite_registry_add_suite(lle_test_suite_registry_t *registry,
                                  lle_test_suite_t *suite);

lle_test_suite_t *lle_test_suite_create(const char *name, lle_test_type_t type);
void lle_test_suite_destroy(lle_test_suite_t *suite);
lle_result_t lle_test_suite_add_test_case(lle_test_suite_t *suite,
                                          lle_test_case_t *test_case);
lle_result_t lle_test_suite_get_tests_by_priority(lle_test_suite_t *suite,
                                                  lle_test_priority_t priority,
                                                  lle_test_case_t ***tests,
                                                  size_t *test_count);

/* ============================================================================
 * FUNCTION DECLARATIONS - Test Context Management
 * ============================================================================
 */

lle_test_context_t *lle_test_context_create(lle_test_case_t *test_case,
                                            lle_testing_framework_t *framework);
void lle_test_context_destroy(lle_test_context_t *ctx);

/* ============================================================================
 * FUNCTION DECLARATIONS - Test Execution
 * ============================================================================
 */

lle_test_runner_t *
lle_test_runner_create_with_config(lle_test_runner_config_t *config);
void lle_test_runner_destroy(lle_test_runner_t *runner);
lle_result_t lle_test_runner_execute_test(lle_test_runner_t *runner,
                                          lle_test_context_t *ctx,
                                          lle_test_execution_result_t *result);

/* ============================================================================
 * FUNCTION DECLARATIONS - Assertion Recording
 * ============================================================================
 */

void lle_test_record_assertion_failure(lle_test_context_t *ctx,
                                       const char *file, int line,
                                       const char *format, ...);

void lle_test_record_assertion_success(lle_test_context_t *ctx,
                                       const char *file, int line);

void lle_test_record_performance_failure(lle_test_context_t *ctx,
                                         const char *file, int line,
                                         const char *format, ...);

void lle_test_record_performance_success(lle_test_context_t *ctx,
                                         const char *file, int line,
                                         uint64_t duration_us);

void lle_test_record_failure(lle_test_context_t *ctx, const char *file,
                             int line, const char *format, ...);

/* ============================================================================
 * FUNCTION DECLARATIONS - Performance Testing
 * ============================================================================
 */

lle_performance_testing_framework_t *
lle_performance_testing_framework_create(void);
void lle_performance_testing_framework_destroy(
    lle_performance_testing_framework_t *framework);
lle_result_t lle_performance_test_execute(
    lle_performance_testing_framework_t *framework, const char *test_name,
    lle_performance_test_function_t test_function, void *test_context,
    lle_performance_result_t *result);
lle_result_t
lle_calculate_performance_statistics(uint64_t *sample_durations,
                                     uint32_t sample_count,
                                     lle_performance_statistics_t *stats);
lle_result_t lle_performance_testing_generate_report(
    lle_performance_testing_framework_t *framework,
    lle_performance_report_t *report);

/* ============================================================================
 * FUNCTION DECLARATIONS - Error Injection
 * ============================================================================
 */

lle_error_injection_system_t *lle_error_injection_system_create(void);
void lle_error_injection_system_destroy(lle_error_injection_system_t *system);
lle_result_t lle_error_injection_system_add_config(
    lle_error_injection_system_t *system,
    lle_test_error_injection_config_t *config);
lle_result_t
lle_error_injection_system_enable(lle_error_injection_system_t *system);
lle_result_t
lle_error_injection_system_disable(lle_error_injection_system_t *system);
lle_result_t lle_get_last_recovery_info(lle_recovery_info_t *info);
lle_result_t lle_get_last_error(void);
lle_recovery_action_t
lle_error_get_recommended_recovery(lle_result_t error_type,
                                   lle_error_context_t *ctx);

/* ============================================================================
 * FUNCTION DECLARATIONS - Memory Safety Testing
 * ============================================================================
 */

lle_memory_safety_framework_t *lle_memory_safety_framework_create(void);
void lle_memory_safety_framework_destroy(
    lle_memory_safety_framework_t *framework);
lle_result_t
lle_memory_safety_framework_enable(lle_memory_safety_framework_t *framework);
uint32_t lle_memory_safety_framework_get_corruption_count(
    lle_memory_safety_framework_t *framework);
uint32_t lle_memory_safety_framework_get_double_free_count(
    lle_memory_safety_framework_t *framework);
uint32_t lle_memory_safety_framework_get_use_after_free_count(
    lle_memory_safety_framework_t *framework);

lle_result_t lle_memory_take_snapshot(lle_memory_snapshot_t *snapshot);
lle_result_t lle_memory_compare_snapshots(lle_memory_snapshot_t *initial,
                                          lle_memory_snapshot_t *final,
                                          lle_memory_leak_report_t *report);
lle_result_t lle_memory_get_leak_report(lle_memory_leak_report_t *report);
bool lle_memory_corruption_check(void *ptr);

/* ============================================================================
 * FUNCTION DECLARATIONS - CI/CD Pipeline
 * ============================================================================
 */

lle_ci_pipeline_t *lle_ci_pipeline_create(void);
void lle_ci_pipeline_destroy(lle_ci_pipeline_t *pipeline);
lle_result_t lle_ci_pipeline_execute(lle_ci_pipeline_t *pipeline,
                                     lle_ci_trigger_context_t *trigger_context,
                                     lle_ci_results_t *results);
bool lle_ci_stage_should_run(lle_test_stage_t *stage,
                             lle_ci_trigger_type_t trigger_type);
lle_result_t lle_ci_stage_execute(lle_test_stage_t *stage,
                                  lle_ci_trigger_context_t *trigger_context,
                                  lle_ci_stage_result_t *result);
lle_result_t lle_ci_generate_artifacts(lle_artifact_manager_t *manager,
                                       lle_ci_results_t *results);
lle_result_t lle_ci_send_notifications(lle_notification_system_t *notifications,
                                       lle_ci_results_t *results);

/* ============================================================================
 * FUNCTION DECLARATIONS - Test Reporting
 * ============================================================================
 */

lle_test_reporter_t *
lle_test_reporter_create_with_formats(lle_report_format_t formats);
void lle_test_reporter_destroy(lle_test_reporter_t *reporter);
lle_result_t lle_test_reporter_generate_report(lle_test_reporter_t *reporter,
                                               lle_test_results_t *results,
                                               lle_test_report_t *report);
lle_result_t lle_test_results_add_failure(lle_test_results_t *results,
                                          lle_test_failure_info_t *failure);

lle_result_t lle_generate_html_dashboard(
    lle_test_reporting_framework_t *framework, lle_test_results_t *results,
    lle_test_analytics_t *analytics, const char *output_path);

/* ============================================================================
 * FUNCTION DECLARATIONS - Utility Functions
 * ============================================================================
 */

uint64_t lle_get_microsecond_timestamp(void);
uint64_t lle_get_nanosecond_timestamp(void);
const char *lle_format_timestamp(uint64_t timestamp_us);
const char *lle_format_bytes(uint64_t bytes);

/* ============================================================================
 * FUNCTION DECLARATIONS - Buffer Operations
 *
 * Note: Testing framework helpers. When buffer_management.h is included
 * (Spec 03), use those authoritative APIs instead.
 * ============================================================================
 */

#ifndef LLE_BUFFER_MANAGEMENT_H
lle_buffer_t *lle_buffer_create(size_t initial_capacity);
lle_buffer_t *lle_buffer_create_with_size(lle_buffer_t **buffer, size_t size);
void lle_buffer_destroy(lle_buffer_t *buffer);
lle_result_t lle_buffer_insert_text(lle_buffer_t *buffer, const char *text,
                                    ssize_t position);
lle_result_t lle_buffer_delete_range(lle_buffer_t *buffer, size_t start,
                                     size_t end);
lle_result_t lle_buffer_clear(lle_buffer_t *buffer);
char *lle_buffer_get_text(lle_buffer_t *buffer);
size_t lle_buffer_get_grapheme_cluster_count(lle_buffer_t *buffer);
lle_cursor_position_t lle_buffer_get_cursor_position(lle_buffer_t *buffer);
#endif /// LLE_BUFFER_MANAGEMENT_H

/* ============================================================================
 * FUNCTION DECLARATIONS - UTF-8 Operations
 * ============================================================================
 */

size_t lle_utf8_count_grapheme_clusters(const char *text);

/* ============================================================================
 * FUNCTION DECLARATIONS - Terminal Operations
 * ============================================================================
 */

lle_unix_terminal_t *lle_unix_terminal_create(void);
void lle_unix_terminal_destroy(lle_unix_terminal_t *terminal);
lle_result_t
lle_unix_terminal_detect_capabilities(lle_unix_terminal_t *terminal,
                                      lle_terminal_capabilities_t *caps,
                                      uint32_t timeout_ms);
lle_result_t lle_unix_terminal_read_input(lle_unix_terminal_t *terminal,
                                          lle_test_input_event_t *event,
                                          uint32_t timeout_ms);

/* ============================================================================
 * FUNCTION DECLARATIONS - Event System Operations
 * ============================================================================
 */

lle_event_system_t *lle_event_system_create(void);
void lle_event_system_destroy(lle_event_system_t *event_system);
lle_result_t lle_event_system_process_event(lle_event_system_t *system,
                                            lle_test_input_event_t *event);
lle_result_t lle_event_system_queue_event(lle_event_system_t *system,
                                          lle_test_input_event_t *event);
lle_result_t
lle_event_system_process_pending_events(lle_event_system_t *system);

/* ============================================================================
 * FUNCTION DECLARATIONS - Display Integration
 * ============================================================================
 */

lle_display_integration_t *lle_display_integration_create(void);
void lle_display_integration_destroy(lle_display_integration_t *integration);
lle_result_t
lle_display_integration_render_buffer(lle_display_integration_t *integration,
                                      lle_buffer_t *buffer,
                                      lush_display_controller_t *display);
lle_result_t lle_display_render_buffer(lle_display_context_t *ctx,
                                       lle_buffer_t *buffer);

/* ============================================================================
 * FUNCTION DECLARATIONS - Lush Display Operations
 * ============================================================================
 */

lush_display_controller_t *lush_display_controller_get_instance(void);
lush_cursor_info_t
lush_display_get_cursor_info(lush_display_controller_t *display);

/* ============================================================================
 * FUNCTION DECLARATIONS - History and Autosuggestions
 * ============================================================================
 */

lle_history_system_t *lle_history_system_create(void);
void lle_history_system_destroy(lle_history_system_t *history);
lle_result_t lle_history_add_command(lle_history_system_t *history,
                                     const char *command, bool persist);

lle_autosuggestions_t *lle_autosuggestions_create(void);
void lle_autosuggestions_destroy(lle_autosuggestions_t *suggestions);
lle_result_t lle_autosuggestions_generate(lle_autosuggestions_t *suggestions,
                                          lle_buffer_t *buffer,
                                          lle_history_system_t *history,
                                          lle_suggestion_result_t *result);

/* ============================================================================
 * FUNCTION DECLARATIONS - Plugin System
 * ============================================================================
 */

lle_plugin_manager_t *lle_plugin_manager_create(void);
void lle_plugin_manager_destroy(lle_plugin_manager_t *manager);
lle_plugin_t *lle_plugin_create(lle_plugin_config_t *config);
void lle_plugin_destroy(lle_plugin_t *plugin);
lle_result_t lle_plugin_call_function(lle_plugin_t *plugin,
                                      const char *function_name, void *arg);

/* ============================================================================
 * FUNCTION DECLARATIONS - System Integration
 * ============================================================================
 */

lle_result_t
lle_system_initialize(lle_system_t **system,
                      lle_integration_requirements_t *requirements);
void lle_system_destroy(lle_system_t *system);
lle_result_t lle_system_get_memory_stats(lle_system_t *system,
                                         lle_memory_pool_stats_t *stats);
void *lle_system_allocate(lle_system_t *system, size_t size);
void lle_system_deallocate(lle_system_t *system, void *ptr);
lle_display_context_t *lle_system_get_display_context(lle_system_t *system);
lle_result_t
lle_testing_get_performance_metrics(lle_system_t *system,
                                    lle_performance_metrics_t *metrics);

/* ============================================================================
 * FUNCTION DECLARATIONS - Hash Table Operations
 * ============================================================================
 */

lle_result_t lle_hashtable_get(lle_hashtable_t *table, const char *key,
                               void **value);
lle_result_t lle_hashtable_set(lle_hashtable_t *table, const char *key,
                               void *value);

/* ============================================================================
 * FUNCTION DECLARATIONS - Error Context
 * ============================================================================
 */

lle_error_context_t *lle_error_context_create(void);
void lle_error_context_destroy(lle_error_context_t *ctx);

#endif /// LLE_TESTING_H
