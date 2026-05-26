/**
 * @file display_stress_test.c
 * @brief LLE Display Integration Stress Tests
 *
 * Validates display system under stress conditions:
 * - High-frequency updates (1000+ ops/sec)
 * - Large buffer rendering (10K+ characters)
 * - Rapid cache churn (evictions under load)
 * - Memory pressure scenarios
 * - Error recovery under stress
 *
 * Week 8: Production Validation
 */

/// macOS compatibility: Enable full POSIX + BSD extensions for rusage
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "lle/buffer_management.h"
#include "lle/display_integration.h"
#include "lle/error_handling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

/// Mock memory pools - uses liblle.a implementations via linking
static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;
static lush_memory_pool_t *mock_lush_pool =
    (lush_memory_pool_t *)&mock_pool_dummy;

/// Helper functions
static uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static size_t get_memory_usage_kb(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    /// macOS returns ru_maxrss in bytes, convert to KB
    return (size_t)(usage.ru_maxrss / 1024);
#else
    /// Linux returns ru_maxrss in KB
    return (size_t)usage.ru_maxrss;
#endif
}

/// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    printf("\n===============================================================" \
           "==\n");                                                            \
    printf("TEST: %s\n", name);                                                \
    printf("=================================================================" \
           "\n");                                                              \
    tests_run++

#define PASS()                                                                 \
    do {                                                                       \
        printf("✓ PASS\n");                                                    \
        tests_passed++;                                                        \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("✗ FAIL: %s\n", msg);                                           \
        tests_failed++;                                                        \
    } while (0)

/* ========================================================================== */
/// STRESS TEST 1: HIGH-FREQUENCY UPDATES
/* ========================================================================== */

void stress_test_high_frequency_updates(void) {
    TEST("High-Frequency Updates (10,000 renders in 1 second)");

    lle_render_pipeline_t *pipeline = NULL;
    lle_display_cache_t *cache = NULL;
    lle_dirty_tracker_t *tracker = NULL;

    lle_render_pipeline_init(&pipeline, mock_pool);
    lle_display_cache_init(&cache, mock_pool);
    lle_dirty_tracker_init(&tracker, mock_pool);

    /// Create buffer
    lle_buffer_t *buffer = NULL;
    lle_buffer_create(&buffer, mock_lush_pool, 0);
    const char *text = "echo 'stress test'";
    lle_buffer_insert_text(buffer, 0, text, strlen(text));

    printf("Running 10,000 render operations...\n");

    size_t mem_start = get_memory_usage_kb();
    uint64_t time_start = get_nanos();

    int cache_hits = 0;
    int cache_misses = 0;

    for (int i = 0; i < 10000; i++) {
        /// Try cache lookup
        void *cached_data = NULL;
        size_t cached_size = 0;
        uint64_t key = (uint64_t)i % 100; /// Reuse keys for cache hits

        if (lle_display_cache_lookup(cache, key, &cached_data, &cached_size) ==
            LLE_SUCCESS) {
            cache_hits++;
            if (cached_data) {
                lle_pool_free(cached_data);
            }
        } else {
            cache_misses++;

            /// Render
            lle_render_context_t context = {0};
            context.buffer = buffer;

            lle_render_output_t *output = NULL;
            if (lle_render_pipeline_execute(pipeline, &context, &output) ==
                LLE_SUCCESS) {
                lle_display_cache_store(cache, key, output->content,
                                        output->content_length);
                lle_render_output_free(output);
            }
        }

        /// Mark dirty and clear
        lle_dirty_tracker_mark_region(tracker, (size_t)(i % 100));
        if (i % 100 == 0) {
            lle_dirty_tracker_clear(tracker);
        }
    }

    uint64_t time_end = get_nanos();
    size_t mem_end = get_memory_usage_kb();

    double elapsed_ms = (time_end - time_start) / 1000000.0;
    double ops_per_sec = 10000.0 / (elapsed_ms / 1000.0);
    size_t mem_delta = mem_end - mem_start;
    double cache_hit_rate = (double)cache_hits / 10000.0 * 100.0;

    printf("Results:\n");
    printf("  Total time: %.2f ms\n", elapsed_ms);
    printf("  Operations/sec: %.0f\n", ops_per_sec);
    printf("  Cache hits: %d (%.1f%%)\n", cache_hits, cache_hit_rate);
    printf("  Cache misses: %d\n", cache_misses);
    printf("  Memory delta: %zu KB\n", mem_delta);

    /// Validate performance targets
    if (elapsed_ms > 1000.0) {
        FAIL("Took longer than 1 second");
    } else if (cache_hit_rate < 75.0) {
        FAIL("Cache hit rate below 75%");
    } else if (mem_delta > 1024) {
        FAIL("Memory usage exceeded 1MB");
    } else {
        PASS();
    }

    lle_buffer_destroy(buffer);
    lle_dirty_tracker_cleanup(tracker);
    lle_display_cache_cleanup(cache);
    lle_render_pipeline_cleanup(pipeline);
}

/* ========================================================================== */
/// STRESS TEST 2: LARGE BUFFER RENDERING
/* ========================================================================== */

void stress_test_large_buffers(void) {
    TEST("Large Buffer Rendering (10KB buffers)");

    lle_render_pipeline_t *pipeline = NULL;
    lle_render_pipeline_init(&pipeline, mock_pool);

    /// Create large buffer (10KB)
    lle_buffer_t *buffer = NULL;
    lle_buffer_create(&buffer, mock_lush_pool, 0);

    char *large_text = malloc(10240);
    memset(large_text, 'x', 10239);
    large_text[10239] = '\0';

    lle_buffer_insert_text(buffer, 0, large_text, strlen(large_text));

    printf("Rendering 100 large buffers (10KB each)...\n");

    uint64_t time_start = get_nanos();

    for (int i = 0; i < 100; i++) {
        lle_render_context_t context = {0};
        context.buffer = buffer;

        lle_render_output_t *output = NULL;
        lle_result_t result =
            lle_render_pipeline_execute(pipeline, &context, &output);

        if (result != LLE_SUCCESS) {
            FAIL("Pipeline execution failed on large buffer");
            free(large_text);
            lle_buffer_destroy(buffer);
            lle_render_pipeline_cleanup(pipeline);
            return;
        }

        lle_render_output_free(output);
    }

    uint64_t time_end = get_nanos();
    double elapsed_us = (time_end - time_start) / 1000.0;
    double avg_us = elapsed_us / 100.0;

    printf("Results:\n");
    printf("  Total time: %.2f μs\n", elapsed_us);
    printf("  Average per render: %.2f μs\n", avg_us);
    printf("  Target: < 500 μs\n");

    if (avg_us > 500.0) {
        FAIL("Average render time exceeded 500μs");
    } else {
        PASS();
    }

    free(large_text);
    lle_buffer_destroy(buffer);
    lle_render_pipeline_cleanup(pipeline);
}

/* ========================================================================== */
/// STRESS TEST 3: CACHE CHURN
/* ========================================================================== */

void stress_test_cache_churn(void) {
    TEST("Cache Churn (1000 unique keys, LRU eviction)");

    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);

    lle_render_pipeline_t *pipeline = NULL;
    lle_render_pipeline_init(&pipeline, mock_pool);

    lle_buffer_t *buffer = NULL;
    lle_buffer_create(&buffer, mock_lush_pool, 0);

    printf("Storing 1000 unique cache entries (forcing evictions)...\n");

    uint64_t time_start = get_nanos();
    int store_failures = 0;

    for (int i = 0; i < 1000; i++) {
        /// Create unique content
        char content[64];
        snprintf(content, sizeof(content), "echo 'test %d'", i);

        lle_buffer_clear(buffer);
        lle_buffer_insert_text(buffer, 0, content, strlen(content));

        /// Render
        lle_render_context_t context = {0};
        context.buffer = buffer;

        lle_render_output_t *output = NULL;
        if (lle_render_pipeline_execute(pipeline, &context, &output) ==
            LLE_SUCCESS) {
            /// Store with unique key
            lle_result_t result = lle_display_cache_store(
                cache, (uint64_t)i, output->content, output->content_length);
            if (result != LLE_SUCCESS) {
                store_failures++;
            }
            lle_render_output_free(output);
        }
    }

    uint64_t time_end = get_nanos();
    double elapsed_ms = (time_end - time_start) / 1000000.0;

    printf("Results:\n");
    printf("  Total time: %.2f ms\n", elapsed_ms);
    printf("  Store failures: %d\n", store_failures);
    printf("  Average store time: %.2f μs\n", (elapsed_ms * 1000.0) / 1000.0);

    /// Verify cache still works after churn
    void *data = NULL;
    size_t size = 0;
    int recent_hits = 0;

    for (int i = 990; i < 1000; i++) {
        if (lle_display_cache_lookup(cache, (uint64_t)i, &data, &size) ==
            LLE_SUCCESS) {
            recent_hits++;
            if (data) {
                lle_pool_free(data);
                data = NULL;
            }
        }
    }

    printf("  Recent entries still cached: %d/10\n", recent_hits);

    if (store_failures > 100) {
        FAIL("Too many store failures during churn");
    } else if (recent_hits < 5) {
        FAIL("LRU policy not working - recent entries evicted");
    } else {
        PASS();
    }

    lle_buffer_destroy(buffer);
    lle_render_pipeline_cleanup(pipeline);
    lle_display_cache_cleanup(cache);
}

/* ========================================================================== */
/// STRESS TEST 4: DIRTY TRACKER PRESSURE
/* ========================================================================== */

void stress_test_dirty_tracker_pressure(void) {
    TEST("Dirty Tracker Under Pressure (10,000 regions)");

    lle_dirty_tracker_t *tracker = NULL;
    lle_dirty_tracker_init(&tracker, mock_pool);

    printf("Marking 10,000 dirty regions...\n");

    uint64_t time_start = get_nanos();

    for (int i = 0; i < 10000; i++) {
        lle_dirty_tracker_mark_region(tracker, (size_t)(i * 10));
    }

    uint64_t time_mid = get_nanos();

    /// Query all regions
    int dirty_count = 0;
    for (int i = 0; i < 10000; i++) {
        if (lle_dirty_tracker_is_region_dirty(tracker, (size_t)(i * 10))) {
            dirty_count++;
        }
    }

    uint64_t time_end = get_nanos();

    double mark_time_us = (time_mid - time_start) / 1000.0;
    double query_time_us = (time_end - time_mid) / 1000.0;

    printf("Results:\n");
    printf("  Mark time: %.2f μs (%.4f μs per region)\n", mark_time_us,
           mark_time_us / 10000.0);
    printf("  Query time: %.2f μs (%.4f μs per query)\n", query_time_us,
           query_time_us / 10000.0);
    printf("  Dirty regions found: %d/10000\n", dirty_count);

    /// Clear and verify
    lle_dirty_tracker_clear(tracker);

    int clean_count = 0;
    for (int i = 0; i < 10000; i++) {
        if (!lle_dirty_tracker_is_region_dirty(tracker, (size_t)(i * 10))) {
            clean_count++;
        }
    }

    printf("  Clean regions after clear: %d/10000\n", clean_count);

    if (dirty_count < 9000) {
        FAIL("Lost dirty regions under pressure");
    } else if (clean_count < 9000) {
        FAIL("Clear operation failed");
    } else {
        PASS();
    }

    lle_dirty_tracker_cleanup(tracker);
}

/* ========================================================================== */
/// STRESS TEST 5: ERROR RECOVERY
/* ========================================================================== */

void stress_test_error_recovery(void) {
    TEST("Error Recovery (handling NULL and invalid parameters)");

    lle_render_pipeline_t *pipeline = NULL;
    lle_display_cache_t *cache = NULL;
    lle_dirty_tracker_t *tracker = NULL;

    lle_render_pipeline_init(&pipeline, mock_pool);
    lle_display_cache_init(&cache, mock_pool);
    lle_dirty_tracker_init(&tracker, mock_pool);

    printf("Testing error recovery scenarios...\n");

    int errors_handled = 0;
    int total_tests = 0;

    /// Test 1: NULL buffer to pipeline
    total_tests++;
    lle_render_context_t context = {0};
    context.buffer = NULL;
    lle_render_output_t *output = NULL;

    if (lle_render_pipeline_execute(pipeline, &context, &output) !=
        LLE_SUCCESS) {
        errors_handled++;
        printf("  ✓ NULL buffer rejected\n");
    }

    /// Test 2: NULL cache lookup
    total_tests++;
    void *data = NULL;
    size_t size = 0;
    if (lle_display_cache_lookup(NULL, 123, &data, &size) != LLE_SUCCESS) {
        errors_handled++;
        printf("  ✓ NULL cache rejected\n");
    }

    /// Test 3: NULL tracker query
    total_tests++;
    bool result = lle_dirty_tracker_is_region_dirty(NULL, 100);
    if (result == true) { /// Safe default
        errors_handled++;
        printf("  ✓ NULL tracker returns safe default (true)\n");
    }

    /// Test 4: Invalid cache key lookup
    total_tests++;
    if (lle_display_cache_lookup(cache, 99999999, &data, &size) !=
        LLE_SUCCESS) {
        errors_handled++;
        printf("  ✓ Invalid cache key handled\n");
    }

    /// Test 5: System continues after errors
    total_tests++;
    lle_buffer_t *buffer = NULL;
    lle_buffer_create(&buffer, mock_lush_pool, 0);
    lle_buffer_insert_text(buffer, 0, "test", 4);

    context.buffer = buffer;
    if (lle_render_pipeline_execute(pipeline, &context, &output) ==
        LLE_SUCCESS) {
        errors_handled++;
        printf("  ✓ System recovers after errors\n");
        lle_render_output_free(output);
    }

    printf("\nResults:\n");
    printf("  Error scenarios handled: %d/%d\n", errors_handled, total_tests);

    if (errors_handled < total_tests) {
        FAIL("Some error scenarios not handled correctly");
    } else {
        PASS();
    }

    lle_buffer_destroy(buffer);
    lle_dirty_tracker_cleanup(tracker);
    lle_display_cache_cleanup(cache);
    lle_render_pipeline_cleanup(pipeline);
}

/* ========================================================================== */
/// STRESS TEST 6: MEMORY LEAK VALIDATION
/* ========================================================================== */

void stress_test_memory_leaks(void) {
    TEST("Memory Leak Detection (1000 init/cleanup cycles)");

    printf("Running 1000 component init/cleanup cycles...\n");
    printf("Monitor with: valgrind --leak-check=full "
           "./build/stress_display_test\n\n");

    size_t mem_start = get_memory_usage_kb();

    for (int i = 0; i < 1000; i++) {
        lle_render_pipeline_t *pipeline = NULL;
        lle_display_cache_t *cache = NULL;
        lle_dirty_tracker_t *tracker = NULL;

        lle_render_pipeline_init(&pipeline, mock_pool);
        lle_display_cache_init(&cache, mock_pool);
        lle_dirty_tracker_init(&tracker, mock_pool);

        /// Do some work
        lle_buffer_t *buffer = NULL;
        lle_buffer_create(&buffer, mock_lush_pool, 0);
        lle_buffer_insert_text(buffer, 0, "test", 4);

        lle_render_context_t context = {0};
        context.buffer = buffer;
        lle_render_output_t *output = NULL;
        lle_render_pipeline_execute(pipeline, &context, &output);

        lle_display_cache_store(cache, (uint64_t)i, "data", 4);

        if (output) {
            lle_render_output_free(output);
        }
        lle_dirty_tracker_mark_region(tracker, (size_t)i);

        /// Cleanup
        lle_buffer_destroy(buffer);
        lle_dirty_tracker_cleanup(tracker);
        lle_display_cache_cleanup(cache);
        lle_render_pipeline_cleanup(pipeline);
    }

    size_t mem_end = get_memory_usage_kb();
    size_t mem_delta = mem_end > mem_start ? mem_end - mem_start : 0;

    printf("Results:\n");
    printf("  Memory at start: %zu KB\n", mem_start);
    printf("  Memory at end: %zu KB\n", mem_end);
    printf("  Memory delta: %zu KB\n", mem_delta);

    if (mem_delta > 100) {
        printf("  ⚠ WARNING: Memory usage increased by %zu KB\n", mem_delta);
        printf("  Run with Valgrind to check for leaks\n");
        FAIL("Potential memory leak detected");
    } else {
        printf("  ✓ No significant memory growth\n");
        PASS();
    }
}

/* ========================================================================== */
/// MAIN TEST RUNNER
/* ========================================================================== */

int main(void) {
    printf("\n");
    printf(
        "#################################################################\n");
    printf(
        "#                                                               #\n");
    printf(
        "#          LLE Display Integration - Stress Tests               #\n");
    printf(
        "#                  Week 8: Production Validation                #\n");
    printf(
        "#                                                               #\n");
    printf(
        "#################################################################\n");

    /// Run all stress tests
    stress_test_high_frequency_updates();
    stress_test_large_buffers();
    stress_test_cache_churn();
    stress_test_dirty_tracker_pressure();
    stress_test_error_recovery();
    stress_test_memory_leaks();

    /// Summary
    printf("\n");
    printf(
        "=================================================================\n");
    printf("  Stress Test Summary\n");
    printf(
        "=================================================================\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_failed);
    printf(
        "=================================================================\n");

    if (tests_failed > 0) {
        printf("\n⚠ STRESS TESTS FAILED - System not production ready\n");
    } else {
        printf(
            "\n✓ ALL STRESS TESTS PASSED - System validated for production\n");
    }

    printf("\nNOTE: For complete memory leak validation, run:\n");
    printf("  valgrind --leak-check=full --show-leak-kinds=all \\\n");
    printf("    ./build/stress_display_test\n\n");

    return (tests_failed > 0) ? 1 : 0;
}
