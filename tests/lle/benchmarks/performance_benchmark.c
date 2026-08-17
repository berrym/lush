/**
 * @file performance_benchmark.c
 * @brief Performance benchmarks for LLE Spec 03
 *
 * Validates that operations meet the spec performance budgets from
 * include/lle/buffer_management.h. The budgets are per-operation, so
 * a run of N iterations gets a total budget of `spec_per_op * N`.
 *
 * Each benchmark runs the work block BENCH_TRIAL_COUNT times and
 * asserts against the BEST of the trials. Single-trial timing is
 * brittle under shared-CPU contention (#170); best-of-N captures the
 * unloaded performance the spec actually describes. The suite is
 * marked `is_parallel: false` in meson.build so other test binaries
 * never share the runner's CPU during the trial window.
 *
 * `main()` propagates per-benchmark failures via the exit code, so
 * meson actually reports a failed benchmark instead of always passing.
 */

#include "../../../include/lle/buffer_management.h"
#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/memory_management.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/// External global from test_memory_mock.c
extern lush_memory_pool_t *global_memory_pool;

/// Helper to get nanoseconds
static uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/// Spec requirements per single operation (in nanoseconds).
/// These mirror the LLE_BUFFER_PERF_* macros in buffer_management.h.
#define SPEC_INSERT_MAX_NS_PER_OP 500000ULL    /// 0.5ms per insert
#define SPEC_DELETE_MAX_NS_PER_OP 500000ULL    /// 0.5ms per delete
#define SPEC_UTF8_CALC_MAX_NS_PER_OP 100000ULL /// 0.1ms per UTF-8 calc

/// Number of trials per benchmark; we report the best (lowest) elapsed
/// time. Five trials is enough to absorb most single-sample OS scheduler
/// blips without making the suite slow.
#define BENCH_TRIAL_COUNT 5

/// Tracks per-benchmark pass/fail across the suite so main() can
/// propagate the worst outcome via exit code.
static int g_failures = 0;

#define BENCHMARK(name, iterations)                                            \
    printf("\n[ BENCHMARK ] %s\n", name);                                      \
    printf("  Iterations: %d\n", iterations);                                  \
    printf("  Trials: %d (reporting best)\n", BENCH_TRIAL_COUNT)

/// Run `code_block` BENCH_TRIAL_COUNT times, report the best elapsed
/// time, and check it against `total_spec_max_ns` (per-op budget already
/// multiplied by iteration count by the caller). On FAIL, increments
/// g_failures so main() exits non-zero.
#define RUN_BENCHMARK(code_block, total_spec_max_ns)                           \
    do {                                                                       \
        uint64_t bench_best = UINT64_MAX;                                      \
        for (int bench_trial = 0; bench_trial < BENCH_TRIAL_COUNT;             \
             bench_trial++) {                                                  \
            uint64_t bench_start = get_nanos();                                \
            code_block;                                                        \
            uint64_t bench_end = get_nanos();                                  \
            uint64_t bench_elapsed = bench_end - bench_start;                  \
            if (bench_elapsed < bench_best) {                                  \
                bench_best = bench_elapsed;                                    \
            }                                                                  \
        }                                                                      \
        double bench_ms = bench_best / 1000000.0;                              \
        printf("  Best of %d: %.3f ms (%.0f ns)\n", BENCH_TRIAL_COUNT,         \
               bench_ms, (double)bench_best);                                  \
        printf("  Spec requirement: <= %.3f ms\n",                             \
               (double)(total_spec_max_ns) / 1000000.0);                       \
        if (bench_best <= (total_spec_max_ns)) {                               \
            printf("  Result: PASS (within spec)\n");                          \
        } else {                                                               \
            printf("  Result: FAIL (best trial exceeds spec by %.0f ns)\n",    \
                   (double)(bench_best - (total_spec_max_ns)));                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

int main(void) {
    printf("=================================================\n");
    printf("LLE Spec 03 - Performance Benchmarks\n");
    printf("=================================================\n");

    lle_buffer_t *buffer = NULL;
    lle_result_t result;

    /// ========================================================================
    /// BENCHMARK 1: Buffer Insert Performance
    /// ========================================================================

    enum { INSERT_ITERS = 1000 };
    BENCHMARK("Buffer Insert (small text)", INSERT_ITERS);

    /// Re-create the buffer inside the trial loop so each trial starts
    /// from the same state; lle_buffer_insert_text grows the buffer so
    /// repeated trials on the same buffer would skew later trials.
    RUN_BENCHMARK(
        {
            result = lle_buffer_create(&buffer, global_memory_pool, 0);
            if (result != LLE_SUCCESS) {
                printf("Failed to create buffer\n");
                return 1;
            }
            for (int i = 0; i < INSERT_ITERS; i++) {
                lle_buffer_insert_text(buffer, buffer->length, "test", 4);
            }
            lle_buffer_destroy(buffer);
        },
        SPEC_INSERT_MAX_NS_PER_OP * INSERT_ITERS);

    /// ========================================================================
    /// BENCHMARK 2: Buffer Delete Performance
    /// ========================================================================

    enum { DELETE_ITERS = 1000 };
    BENCHMARK("Buffer Delete (small text)", DELETE_ITERS);

    RUN_BENCHMARK(
        {
            result = lle_buffer_create(&buffer, global_memory_pool, 0);
            if (result != LLE_SUCCESS) {
                printf("Failed to create buffer\n");
                return 1;
            }
            for (int i = 0; i < DELETE_ITERS; i++) {
                lle_buffer_insert_text(buffer, buffer->length, "test", 4);
            }
            for (int i = 0; i < DELETE_ITERS; i++) {
                lle_buffer_delete_text(buffer, buffer->length - 4, 4);
            }
            lle_buffer_destroy(buffer);
        },
        SPEC_DELETE_MAX_NS_PER_OP * DELETE_ITERS);

    /// ========================================================================
    /// BENCHMARK 3: UTF-8 Index Building
    /// ========================================================================

    enum { UTF8_ITERS = 100 };
    BENCHMARK("UTF-8 Index Rebuild (100 char text)", UTF8_ITERS);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }
    const char *utf8_text =
        "Hello \xf0\x9f\x8c\x8d World! This is a test with \xc3\xa9mojis and "
        "sp\xc3\xab"
        "cial \xc3\xa7haracters.";
    size_t text_len = strlen(utf8_text);
    lle_buffer_insert_text(buffer, 0, utf8_text, text_len);

    RUN_BENCHMARK(
        {
            for (int i = 0; i < UTF8_ITERS; i++) {
                if (buffer->utf8_index) {
                    lle_utf8_index_rebuild(buffer->utf8_index,
                                           (const char *)buffer->data,
                                           buffer->length);
                }
            }
        },
        SPEC_UTF8_CALC_MAX_NS_PER_OP * UTF8_ITERS);

    lle_buffer_destroy(buffer);

    /// ========================================================================
    /// BENCHMARK 4: Cursor Movement
    /// ========================================================================

    enum { CURSOR_ITERS = 1000 };
    BENCHMARK("Cursor Movement (by codepoints)", CURSOR_ITERS);

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

    const char *cursor_text = "This is a test string for cursor movement";
    lle_buffer_insert_text(buffer, 0, cursor_text, strlen(cursor_text));

    RUN_BENCHMARK(
        {
            lle_cursor_manager_move_to_byte_offset(cursor_mgr, 0);
            for (int i = 0; i < CURSOR_ITERS; i++) {
                lle_cursor_manager_move_by_codepoints(cursor_mgr, 1);
                if (buffer->cursor.codepoint_index >= buffer->codepoint_count) {
                    lle_cursor_manager_move_to_byte_offset(cursor_mgr, 0);
                }
            }
        },
        SPEC_INSERT_MAX_NS_PER_OP * CURSOR_ITERS);

    lle_cursor_manager_destroy(cursor_mgr);
    lle_buffer_destroy(buffer);

    /// ========================================================================
    /// BENCHMARK 5: Undo/Redo Performance
    /// ========================================================================

    enum { UNDO_REDO_OPS = 100 };
    BENCHMARK("Undo/Redo Operations", UNDO_REDO_OPS);

    RUN_BENCHMARK(
        {
            result = lle_buffer_create(&buffer, global_memory_pool, 0);
            if (result != LLE_SUCCESS) {
                printf("Failed to create buffer\n");
                return 1;
            }
            lle_change_tracker_t *tracker = NULL;
            result =
                lle_change_tracker_init(&tracker, global_memory_pool, 1000);
            if (result != LLE_SUCCESS) {
                printf("Failed to create change tracker\n");
                lle_buffer_destroy(buffer);
                return 1;
            }
            buffer->change_tracking_enabled = true;
            for (int i = 0; i < UNDO_REDO_OPS; i++) {
                lle_change_sequence_t *seq = NULL;
                lle_change_tracker_begin_sequence(tracker, "operation", &seq);
                buffer->current_sequence = seq;
                lle_buffer_insert_text(buffer, buffer->length, "x", 1);
                lle_change_tracker_complete_sequence(tracker);
            }
            for (int i = 0; i < UNDO_REDO_OPS; i++) {
                lle_change_tracker_undo(tracker, buffer);
            }
            for (int i = 0; i < UNDO_REDO_OPS; i++) {
                lle_change_tracker_redo(tracker, buffer);
            }
            lle_change_tracker_destroy(tracker);
            lle_buffer_destroy(buffer);
        },
        /// 2x SPEC_INSERT for an undo + redo pair per op.
        SPEC_INSERT_MAX_NS_PER_OP * 2 * UNDO_REDO_OPS);

    /// ========================================================================
    /// BENCHMARK 6: Buffer Validation
    /// ========================================================================

    enum { VALIDATION_ITERS = 1000 };
    BENCHMARK("Buffer Validation (complete)", VALIDATION_ITERS);

    result = lle_buffer_create(&buffer, global_memory_pool, 0);
    if (result != LLE_SUCCESS) {
        printf("Failed to create buffer\n");
        return 1;
    }
    lle_buffer_validator_t *validator = NULL;
    result = lle_buffer_validator_init(&validator);
    if (result != LLE_SUCCESS) {
        printf("Failed to create validator\n");
        lle_buffer_destroy(buffer);
        return 1;
    }
    const char *validation_text =
        "Test validation performance with UTF-8: \xf0\x9f\x8c\x8d";
    lle_buffer_insert_text(buffer, 0, validation_text, strlen(validation_text));

    RUN_BENCHMARK(
        {
            for (int i = 0; i < VALIDATION_ITERS; i++) {
                lle_buffer_validate_complete(buffer, validator);
            }
        },
        SPEC_UTF8_CALC_MAX_NS_PER_OP * VALIDATION_ITERS);

    lle_buffer_validator_destroy(validator);
    lle_buffer_destroy(buffer);

    /// ========================================================================
    /// Summary
    /// ========================================================================

    printf("\n=================================================\n");
    printf("Performance Benchmark Summary\n");
    printf("=================================================\n");
    if (g_failures == 0) {
        printf("All benchmarks within spec.\n");
    } else {
        printf("FAILED: %d benchmark(s) exceeded their spec budget.\n",
               g_failures);
    }
    printf("=================================================\n");

    return g_failures == 0 ? 0 : 1;
}
