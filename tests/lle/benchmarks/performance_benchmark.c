/**
 * @file performance_benchmark.c
 * @brief Performance benchmarks for LLE Spec 03
 *
 * Validates that operations meet spec performance requirements:
 * - Insert: < 0.5ms (500,000 ns)
 * - Delete: < 0.5ms (500,000 ns)
 * - UTF-8 calculation: < 0.1ms (100,000 ns)
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/memory_management.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// External global from test_memory_mock.c
extern lush_memory_pool_t *global_memory_pool;

// Helper to get nanoseconds
static uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Spec requirements (in nanoseconds)
#define SPEC_INSERT_MAX_NS 500000ULL    // 0.5ms
#define SPEC_DELETE_MAX_NS 500000ULL    // 0.5ms
#define SPEC_UTF8_CALC_MAX_NS 100000ULL // 0.1ms

#define BENCHMARK(name, iterations)                                            \
    printf("\n[ BENCHMARK ] %s\n", name);                                      \
    printf("  Iterations: %d\n", iterations)

#define RUN_BENCHMARK(code_block, spec_max)                                    \
    do {                                                                       \
        uint64_t bench_start = get_nanos();                                    \
        code_block;                                                            \
        uint64_t bench_end = get_nanos();                                      \
        uint64_t bench_elapsed = bench_end - bench_start;                      \
        double bench_ms = bench_elapsed / 1000000.0;                           \
        printf("  Time: %.3f ms (%.0f ns)\n", bench_ms,                        \
               (double)bench_elapsed);                                         \
        printf("  Spec requirement: < %.3f ms\n", spec_max / 1000000.0);       \
        if (bench_elapsed <= spec_max) {                                       \
            printf("  Result: PASS (within spec)\n");                          \
        } else {                                                               \
            printf("  Result: FAIL (exceeds spec by %.0f ns)\n",               \
                   (double)(bench_elapsed - spec_max));                        \
        }                                                                      \
    } while (0)

int main(void) {
    printf("=================================================\n");
    printf("LLE Spec 03 - Performance Benchmarks\n");
    printf("=================================================\n");

    lle_buffer_t *buffer = NULL;
    lle_result_t result;

    /* ========================================================================
     * BENCHMARK 1: Buffer Insert Performance
     * ========================================================================
     */

    BENCHMARK("Buffer Insert (small text)", 1000);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    RUN_BENCHMARK(
        {
            for (int i = 0; i < 1000; i++) {
                lle_buffer_insert_text(buffer, buffer->length, "test", 4);
            }
        },
        SPEC_INSERT_MAX_NS);

    lle_buffer_destroy(buffer);

    /* ========================================================================
     * BENCHMARK 2: Buffer Delete Performance
     * ========================================================================
     */

    BENCHMARK("Buffer Delete (small text)", 1000);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    // Populate buffer first
    for (int i = 0; i < 1000; i++) {
        lle_buffer_insert_text(buffer, buffer->length, "test", 4);
    }

    RUN_BENCHMARK(
        {
            for (int i = 0; i < 1000; i++) {
                lle_buffer_delete_text(buffer, buffer->length - 4, 4);
            }
        },
        SPEC_DELETE_MAX_NS);

    lle_buffer_destroy(buffer);

    /* ========================================================================
     * BENCHMARK 3: UTF-8 Index Building
     * ========================================================================
     */

    BENCHMARK("UTF-8 Index Rebuild (100 char text)", 100);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    // Insert text with UTF-8
    const char *utf8_text =
        "Hello 🌍 World! This is a test with émojis and spëcial çharacters.";
    size_t text_len = strlen(utf8_text);

    lle_buffer_insert_text(buffer, 0, utf8_text, text_len);

    RUN_BENCHMARK(
        {
            for (int i = 0; i < 100; i++) {
                // Rebuild UTF-8 index
                if (buffer->utf8_index) {
                    lle_utf8_index_rebuild(buffer->utf8_index,
                                           (const char *)buffer->data,
                                           buffer->length);
                }
            }
        },
        SPEC_UTF8_CALC_MAX_NS);

    lle_buffer_destroy(buffer);

    /* ========================================================================
     * BENCHMARK 4: Cursor Movement
     * ========================================================================
     */

    BENCHMARK("Cursor Movement (by codepoints)", 1000);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    lle_cursor_manager_t *cursor_mgr = NULL;
    result = lle_cursor_manager_init(&cursor_mgr, buffer);
    if (result != LLE_SUCCESS) {
        printf("Failed to create cursor manager\n");
        return 1;
    }

    // Insert test text
    lle_buffer_insert_text(buffer, 0,
                           "This is a test string for cursor movement", 42);

    RUN_BENCHMARK(
        {
            for (int i = 0; i < 1000; i++) {
                lle_cursor_manager_move_by_codepoints(cursor_mgr, 1);
                if (buffer->cursor.codepoint_index >= buffer->codepoint_count) {
                    lle_cursor_manager_move_to_byte_offset(cursor_mgr, 0);
                }
            }
        },
        SPEC_INSERT_MAX_NS);

    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);

    /* ========================================================================
     * BENCHMARK 5: Undo/Redo Performance
     * ========================================================================
     */

    BENCHMARK("Undo/Redo Operations", 100);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    lle_change_tracker_t *tracker = NULL;
    result = lle_change_tracker_init(&tracker, global_memory_pool, 1000);
    if (result != LLE_SUCCESS) {
        printf("Failed to create change tracker\n");
        return 1;
    }

    buffer->change_tracking_enabled = true;

    // Create 100 changes
    for (int i = 0; i < 100; i++) {
        lle_change_sequence_t *seq = NULL;
        lle_change_tracker_begin_sequence(tracker, "operation", &seq);
        buffer->current_sequence = seq;
        lle_buffer_insert_text(buffer, buffer->length, "x", 1);
        lle_change_tracker_complete_sequence(tracker);
    }

    RUN_BENCHMARK(
        {
            // Undo all
            for (int i = 0; i < 100; i++) {
                lle_change_tracker_undo(tracker, buffer);
            }

            // Redo all
            for (int i = 0; i < 100; i++) {
                lle_change_tracker_redo(tracker, buffer);
            }
        },
        SPEC_INSERT_MAX_NS * 2); // Allow 1ms for 200 operations

    lle_change_tracker_destroy(tracker);
    lle_buffer_destroy(buffer);

    /* ========================================================================
     * BENCHMARK 6: Buffer Validation
     * ========================================================================
     */

    BENCHMARK("Buffer Validation (complete)", 1000);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }

    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    if (result != LLE_SUCCESS) {
        printf("Failed to create validator\n");
        return 1;
    }

    // Insert some text
    lle_buffer_insert_text(buffer, 0,
                           "Test validation performance with UTF-8: 🌍", 46);

    RUN_BENCHMARK(
        {
            for (int i = 0; i < 1000; i++) {
                lle_buffer_validate_complete(buffer, validator);
            }
        },
        SPEC_UTF8_CALC_MAX_NS);

    lle_buffer_validator_destroy(validator);
    lle_buffer_destroy(buffer);

    /* ========================================================================
     * Summary
     * ========================================================================
     */

    printf("\n=================================================\n");
    printf("Performance Benchmark Summary\n");
    printf("=================================================\n");
    printf("All benchmarks completed.\n");
    printf("Review results above to verify spec compliance.\n");
    printf("=================================================\n");

    return 0;
}
