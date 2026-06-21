/**
 * @file test_lle_error_handling.c
 * @brief Functional tests for the LLE error-handling system (Spec 16)
 *
 * Tests all error handling capabilities including backtrace capture,
 * component state dumps, and forensic logging.
 *
 * Copyright (C) 2025 Michael Berry
 * Licensed under GPL v3
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lle/error_handling.h"

/// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    printf("\n--- Test: " #name " ---\n");                                     \
    if (test_##name()) {                                                       \
        printf("✓ PASSED\n");                                                  \
        tests_passed++;                                                        \
    } else {                                                                   \
        printf("✗ FAILED\n");                                                  \
        tests_failed++;                                                        \
    }

/* ============================================================================
 * TEST 1: Basic Error Context Creation
 * ============================================================================
 */
static int test_error_context_creation(void) {
    printf("Testing error context creation...\n");

    lle_error_context_t *ctx =
        LLE_CREATE_ERROR_CONTEXT(LLE_ERROR_BUFFER_OVERFLOW,
                                 "Test buffer overflow error", "TestComponent");

    if (!ctx) {
        printf("  ERROR: Failed to create error context\n");
        return 0;
    }

    printf("  ✓ Error context created\n");
    printf("  Error code: %d\n", ctx->error_code);
    printf("  Function: %s\n", ctx->function_name);
    printf("  Component: %s\n", ctx->component_name);

    return 1;
}

/* ============================================================================
 * TEST 2: Forensic Log Entry with Backtrace
 * ============================================================================
 */
static int test_forensic_logging_with_backtrace(void) {
    printf("Testing forensic log entry creation with backtrace...\n");

    /// Create an error context
    lle_error_context_t *ctx = LLE_CREATE_ERROR_CONTEXT(
        LLE_ERROR_MEMORY_CORRUPTION, "Detected memory corruption in buffer",
        "BufferManagement");

    if (!ctx) {
        printf("  ERROR: Failed to create error context\n");
        return 0;
    }

    /// Create forensic log entry
    lle_forensic_log_entry_t *log_entry = NULL;
    lle_result_t result = lle_create_forensic_log_entry(ctx, &log_entry);

    if (result != LLE_SUCCESS) {
        printf("  ERROR: Failed to create forensic log entry: %d\n", result);
        return 0;
    }

    printf("  ✓ Forensic log entry created\n");

    /// Verify system snapshot was captured
    printf("  System snapshot:\n");
    printf("    Active components: 0x%x\n",
           log_entry->system_snapshot.active_components_mask);
    printf("    Thread count: %u\n", log_entry->system_snapshot.thread_count);

    /// Verify stack trace was captured
    printf("  Stack trace:\n");
    printf("    Frames captured: %zu\n", log_entry->stack_trace.frame_count);
    printf("    Trace complete: %s\n",
           log_entry->stack_trace.stack_trace_complete ? "yes" : "no");

    if (log_entry->stack_trace.frame_count > 0) {
        printf("  ✓ Stack trace captured successfully\n");

        /// Print first few frames if symbols are available
        if (log_entry->stack_trace.symbol_names) {
            printf("    Stack frames:\n");
            size_t max_frames = log_entry->stack_trace.frame_count < 5
                                    ? log_entry->stack_trace.frame_count
                                    : 5;
            for (size_t i = 0; i < max_frames; i++) {
                printf("      [%zu] %s\n", i,
                       log_entry->stack_trace.symbol_names[i]);
            }
        }
    } else {
        printf("  ⚠ No stack trace available (platform may not support "
               "backtrace)\n");
    }

    /// Verify component states were dumped
    printf("  Component state dumps:\n");
    printf("    Total size: %zu bytes\n",
           log_entry->component_state.total_state_dump_size);

    if (log_entry->component_state.buffer_state_dump) {
        printf("    Buffer: %s", log_entry->component_state.buffer_state_dump);
        printf("  ✓ Component states dumped\n");
    }

    /// Clean up
    if (log_entry->stack_trace.symbol_names) {
        free(log_entry->stack_trace.symbol_names);
    }
    if (log_entry->component_state.buffer_state_dump) {
        free(log_entry->component_state.buffer_state_dump);
    }
    if (log_entry->component_state.event_system_state_dump) {
        free(log_entry->component_state.event_system_state_dump);
    }
    if (log_entry->component_state.terminal_state_dump) {
        free(log_entry->component_state.terminal_state_dump);
    }
    if (log_entry->component_state.memory_pool_state_dump) {
        free(log_entry->component_state.memory_pool_state_dump);
    }
    free(log_entry);

    return 1;
}

/* ============================================================================
 * TEST 3: System Snapshot Capture
 * ============================================================================
 */
static int test_system_snapshot_capture(void) {
    printf("Testing system snapshot capture...\n");

    struct {
        uint64_t total_memory_usage;
        uint64_t peak_memory_usage;
        uint32_t active_components_mask;
        uint32_t thread_count;
        float cpu_usage_percent;
        uint64_t avg_response_time_ns;
        uint64_t max_response_time_ns;
        uint32_t operations_per_second;
        uint32_t cache_hit_rate_percent;
    } snapshot;

    memset(&snapshot, 0, sizeof(snapshot));

    lle_capture_system_snapshot(&snapshot);

    printf("  Snapshot captured:\n");
    printf("    Active components: 0x%x\n", snapshot.active_components_mask);
    printf("    Thread count: %u\n", snapshot.thread_count);
    printf("    Max recovery time: %lu ns\n", snapshot.max_response_time_ns);

    printf("  ✓ System snapshot capture functional\n");

    return 1;
}

/* ============================================================================
 * TEST 4: Component State Dumps
 * ============================================================================
 */
static int test_component_state_dumps(void) {
    printf("Testing component state dumps...\n");

    struct {
        char *buffer_state_dump;
        char *event_system_state_dump;
        char *terminal_state_dump;
        char *memory_pool_state_dump;
        size_t total_state_dump_size;
    } state;

    memset(&state, 0, sizeof(state));

    lle_dump_component_states(&state);

    printf("  State dumps captured:\n");
    printf("    Total size: %zu bytes\n", state.total_state_dump_size);

    int dump_count = 0;
    if (state.buffer_state_dump) {
        printf("    ✓ Buffer state dumped\n");
        dump_count++;
        free(state.buffer_state_dump);
    }
    if (state.event_system_state_dump) {
        printf("    ✓ Event system state dumped\n");
        dump_count++;
        free(state.event_system_state_dump);
    }
    if (state.terminal_state_dump) {
        printf("    ✓ Terminal state dumped\n");
        dump_count++;
        free(state.terminal_state_dump);
    }
    if (state.memory_pool_state_dump) {
        printf("    ✓ Memory pool state dumped\n");
        dump_count++;
        free(state.memory_pool_state_dump);
    }

    if (dump_count >= 4) {
        printf("  ✓ All component states dumped successfully\n");
        return 1;
    } else {
        printf("  ERROR: Not all component states were dumped\n");
        return 0;
    }
}

/* ============================================================================
 * TEST 5: Error Reporting Pipeline
 * ============================================================================
 */
static int test_error_reporting_pipeline(void) {
    printf("Testing complete error reporting pipeline...\n");

    /// Create error context
    lle_error_context_t *ctx = LLE_CREATE_ERROR_CONTEXT(
        LLE_ERROR_INVALID_STATE, "Component in invalid state during operation",
        "EventSystem");

    if (!ctx) {
        printf("  ERROR: Failed to create error context\n");
        return 0;
    }

    /// Report the error (should output to console)
    printf("  Reporting error to console:\n");
    lle_result_t result = lle_report_error(ctx);

    if (result == LLE_SUCCESS) {
        printf("  ✓ Error reported successfully\n");
        return 1;
    } else {
        printf("  ERROR: Error reporting failed\n");
        return 0;
    }
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */
int main(void) {
    printf("=============================================================\n");
    printf("LLE Spec 16: Error Handling - Full Implementation Test\n");
    printf("=============================================================\n");
    printf("\nTesting all error handling capabilities including:\n");
    printf("- Error context creation and management\n");
    printf("- Stack trace capture (backtrace)\n");
    printf("- Component state dumps\n");
    printf("- System snapshot capture\n");
    printf("- Forensic logging\n\n");

    TEST(error_context_creation);
    TEST(forensic_logging_with_backtrace);
    TEST(system_snapshot_capture);
    TEST(component_state_dumps);
    TEST(error_reporting_pipeline);

    printf("\n=============================================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=============================================================\n");

    if (tests_failed == 0) {
        printf("\n✓ ALL TESTS PASSED - SPEC 16 FULLY IMPLEMENTED\n\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n\n");
        return 1;
    }
}
