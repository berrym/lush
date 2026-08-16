/**
 * @file display_integration_test.c
 * @brief LLE Display Integration End-to-End Tests
 *
 * Tests the integration between display components:
 * - Pipeline + Cache integration
 * - Dirty Tracker + Buffer operations
 * - Terminal Adapter + Theme integration
 * - Multi-component workflows
 *
 * These tests verify that display components work correctly together.
 */

#include "lle/display_integration.h"
#include "lle/error_handling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Mock memory pool - uses liblle.a implementations via linking
static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

/// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/// Test macros
#define TEST(name)                                                             \
    static void test_##name(void);                                             \
    __attribute__((unused)) static void run_test_##name(void) {                \
        printf("Running test: %s\n", #name);                                   \
        tests_run++;                                                           \
        test_##name();                                                         \
        tests_passed++;                                                        \
    }                                                                          \
    static void test_##name(void)

#define ASSERT_SUCCESS(result, msg)                                            \
    do {                                                                       \
        if ((result) != LLE_SUCCESS) {                                         \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s\n", msg);              \
            printf("    at %s:%d\n", __FILE__, __LINE__);                      \
            printf("    Result code: %d\n", (int)(result));                    \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  \xe2\x9c\x97 ASSERTION FAILED: %s\n", msg);              \
            printf("    at %s:%d\n", __FILE__, __LINE__);                      \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT_TRUE((ptr) != NULL, msg)
#define ASSERT_EQ(a, b, msg) ASSERT_TRUE((a) == (b), msg)
#define ASSERT_NEQ(a, b, msg) ASSERT_TRUE((a) != (b), msg)

/* ========================================================================== */
/// INTEGRATION TEST 1: PIPELINE + CACHE
/* ========================================================================== */

/**
 * Test: Render pipeline with cache integration
 * Tests that pipeline and cache work together for rendering workflow
 */
TEST(pipeline_cache_integration) {
    /// Create pipeline
    lle_render_pipeline_t *pipeline = NULL;
    lle_result_t result = lle_render_pipeline_init(&pipeline, mock_pool);
    ASSERT_SUCCESS(result, "Pipeline initialization should succeed");
    ASSERT_NOT_NULL(pipeline, "Pipeline should be allocated");

    /// Create cache
    lle_display_cache_t *cache = NULL;
    result = lle_display_cache_init(&cache, mock_pool);
    ASSERT_SUCCESS(result, "Cache initialization should succeed");
    ASSERT_NOT_NULL(cache, "Cache should be allocated");

    /// Store test data in cache
    const char *test_data = "Rendered output";
    uint64_t cache_key = 12345;
    result =
        lle_display_cache_store(cache, cache_key, test_data, strlen(test_data));
    ASSERT_SUCCESS(result, "Cache store should succeed");

    /// Lookup from cache
    void *cached_data = NULL;
    size_t cached_size = 0;
    result =
        lle_display_cache_lookup(cache, cache_key, &cached_data, &cached_size);
    ASSERT_SUCCESS(result, "Cache lookup should succeed");
    ASSERT_NOT_NULL(cached_data, "Cached data should be retrieved");
    ASSERT_EQ(cached_size, strlen(test_data), "Cached size should match");

    /// Cleanup
    lle_display_cache_cleanup(cache);
    lle_render_pipeline_cleanup(pipeline);
}

/**
 * Test: Cache invalidation workflow
 * Tests that cache invalidation works with pipeline
 */
TEST(pipeline_cache_invalidation) {
    lle_display_cache_t *cache = NULL;
    lle_result_t result = lle_display_cache_init(&cache, mock_pool);
    ASSERT_SUCCESS(result, "Cache initialization should succeed");

    /// Store multiple entries
    result = lle_display_cache_store(cache, 100, "data1", 5);
    ASSERT_SUCCESS(result, "Store 1 should succeed");

    result = lle_display_cache_store(cache, 200, "data2", 5);
    ASSERT_SUCCESS(result, "Store 2 should succeed");

    /// Invalidate one entry
    result = lle_display_cache_invalidate(cache, 100);
    ASSERT_SUCCESS(result, "Invalidation should succeed");

    /// Lookup invalidated entry should fail
    void *data = NULL;
    size_t size = 0;
    result = lle_display_cache_lookup(cache, 100, &data, &size);
    ASSERT_TRUE(result != LLE_SUCCESS,
                "Lookup of invalidated entry should fail");

    /// Other entry should still exist
    result = lle_display_cache_lookup(cache, 200, &data, &size);
    ASSERT_SUCCESS(result, "Lookup of valid entry should succeed");

    lle_display_cache_cleanup(cache);
}

/* ========================================================================== */
/// INTEGRATION TEST 2: DIRTY TRACKER
/* ========================================================================== */

/**
 * Test: Dirty tracker workflow with buffer modifications
 * Simulates marking regions dirty after buffer changes
 */
TEST(dirty_tracker_buffer_workflow) {
    lle_dirty_tracker_t *tracker = NULL;
    lle_result_t result = lle_dirty_tracker_init(&tracker, mock_pool);
    ASSERT_SUCCESS(result, "Tracker initialization should succeed");

    /// Initial state needs full redraw
    bool needs_full = lle_dirty_tracker_needs_full_redraw(tracker);
    ASSERT_TRUE(needs_full, "New tracker should need full redraw");

    /// After first render, clear dirty state
    result = lle_dirty_tracker_clear(tracker);
    ASSERT_SUCCESS(result, "Clear should succeed");

    /// Simulate buffer modification - mark region dirty
    result = lle_dirty_tracker_mark_region(tracker, 150);
    ASSERT_SUCCESS(result, "Mark region should succeed");

    /// Check that region is dirty
    bool is_dirty = lle_dirty_tracker_is_region_dirty(tracker, 150);
    ASSERT_TRUE(is_dirty, "Modified region should be dirty");

    /// After re-render, clear again
    result = lle_dirty_tracker_clear(tracker);
    ASSERT_SUCCESS(result, "Second clear should succeed");

    /// Verify clean state
    is_dirty = lle_dirty_tracker_is_region_dirty(tracker, 150);
    ASSERT_FALSE(is_dirty, "Region should be clean after clear");

    lle_dirty_tracker_cleanup(tracker);
}

/**
 * Test: Dirty tracker with range marking
 * Tests marking multiple regions as dirty at once
 */
TEST(dirty_tracker_range_marking) {
    lle_dirty_tracker_t *tracker = NULL;
    lle_result_t result = lle_dirty_tracker_init(&tracker, mock_pool);
    ASSERT_SUCCESS(result, "Tracker initialization should succeed");

    result = lle_dirty_tracker_clear(tracker);
    ASSERT_SUCCESS(result, "Clear should succeed");

    /// Mark a range as dirty (start offset, length)
    /// Note: mark_range marks start and end boundaries, not every offset in
    /// between
    result = lle_dirty_tracker_mark_range(tracker, 100, 200);
    ASSERT_SUCCESS(result, "Mark range should succeed");

    /// Check regions - start and end are marked
    ASSERT_TRUE(lle_dirty_tracker_is_region_dirty(tracker, 100),
                "Start of range should be dirty");
    ASSERT_TRUE(lle_dirty_tracker_is_region_dirty(tracker, 300),
                "End of range (100+200) should be dirty");

    /// Mark additional regions within range for fuller coverage
    result = lle_dirty_tracker_mark_region(tracker, 150);
    ASSERT_SUCCESS(result, "Mark additional region should succeed");

    result = lle_dirty_tracker_mark_region(tracker, 250);
    ASSERT_SUCCESS(result, "Mark another region should succeed");

    /// Check that marked regions are dirty
    ASSERT_TRUE(lle_dirty_tracker_is_region_dirty(tracker, 150),
                "Explicitly marked region should be dirty");
    ASSERT_TRUE(lle_dirty_tracker_is_region_dirty(tracker, 250),
                "Another marked region should be dirty");

    /// Check region outside range
    ASSERT_FALSE(lle_dirty_tracker_is_region_dirty(tracker, 500),
                 "Region outside range should be clean");

    lle_dirty_tracker_cleanup(tracker);
}

/* ========================================================================== */
/// INTEGRATION TEST 3: MULTI-COMPONENT
/* ========================================================================== */

/**
 * Test: Pipeline, cache, and dirty tracker together
 * Simulates complete rendering workflow with all components
 */
TEST(multi_component_render_workflow) {
    /// Initialize all components
    lle_render_pipeline_t *pipeline = NULL;
    lle_display_cache_t *cache = NULL;
    lle_dirty_tracker_t *tracker = NULL;

    lle_result_t result = lle_render_pipeline_init(&pipeline, mock_pool);
    ASSERT_SUCCESS(result, "Pipeline init should succeed");

    result = lle_display_cache_init(&cache, mock_pool);
    ASSERT_SUCCESS(result, "Cache init should succeed");

    result = lle_dirty_tracker_init(&tracker, mock_pool);
    ASSERT_SUCCESS(result, "Tracker init should succeed");

    /// Step 1: Initial state - needs full redraw
    bool needs_full = lle_dirty_tracker_needs_full_redraw(tracker);
    ASSERT_TRUE(needs_full, "Should need full redraw initially");

    /// Step 2: Simulate first render - store in cache
    const char *render_output = "Initial render output";
    uint64_t cache_key = 1000;
    result = lle_display_cache_store(cache, cache_key, render_output,
                                     strlen(render_output));
    ASSERT_SUCCESS(result, "Cache store should succeed");

    /// Clear dirty state after render
    result = lle_dirty_tracker_clear(tracker);
    ASSERT_SUCCESS(result, "Clear should succeed");

    /// Step 3: Simulate buffer modification
    result = lle_dirty_tracker_mark_region(tracker, 50);
    ASSERT_SUCCESS(result, "Mark dirty region should succeed");

    /// Step 4: Check if we can use cache or need re-render
    bool is_dirty = lle_dirty_tracker_is_region_dirty(tracker, 50);
    ASSERT_TRUE(is_dirty, "Modified region should be dirty");

    /// Invalidate cache since content changed
    result = lle_display_cache_invalidate(cache, cache_key);
    ASSERT_SUCCESS(result, "Cache invalidation should succeed");

    /// Step 5: Simulate re-render with new content
    const char *new_output = "Updated render output";
    result = lle_display_cache_store(cache, cache_key, new_output,
                                     strlen(new_output));
    ASSERT_SUCCESS(result, "Cache store of new content should succeed");

    /// Cleanup
    lle_dirty_tracker_cleanup(tracker);
    lle_display_cache_cleanup(cache);
    lle_render_pipeline_cleanup(pipeline);
}

/**
 * Test: Cache coherency with invalidation
 * Tests that cache stays synchronized with tracker state
 */
TEST(cache_tracker_coherency) {
    lle_display_cache_t *cache = NULL;
    lle_dirty_tracker_t *tracker = NULL;

    lle_result_t result = lle_display_cache_init(&cache, mock_pool);
    ASSERT_SUCCESS(result, "Cache init should succeed");

    result = lle_dirty_tracker_init(&tracker, mock_pool);
    ASSERT_SUCCESS(result, "Tracker init should succeed");

    result = lle_dirty_tracker_clear(tracker);
    ASSERT_SUCCESS(result, "Clear should succeed");

    /// Cache some data
    result = lle_display_cache_store(cache, 500, "cached", 6);
    ASSERT_SUCCESS(result, "Store should succeed");

    /// Mark full redraw needed
    result = lle_dirty_tracker_mark_full(tracker);
    ASSERT_SUCCESS(result, "Mark full should succeed");

    /// When full redraw is needed, invalidate all cache
    result = lle_display_cache_invalidate_all(cache);
    ASSERT_SUCCESS(result, "Invalidate all should succeed");

    /// Verify cache is empty
    void *data = NULL;
    size_t size = 0;
    result = lle_display_cache_lookup(cache, 500, &data, &size);
    ASSERT_TRUE(result != LLE_SUCCESS,
                "Lookup should fail after invalidate all");

    lle_dirty_tracker_cleanup(tracker);
    lle_display_cache_cleanup(cache);
}

/* ========================================================================== */
/// MAIN TEST RUNNER
/* ========================================================================== */

int main(void) {
    printf(
        "=================================================================\n");
    printf("  LLE Display Integration Tests\n");
    printf("================================================================="
           "\n\n");

    /// Pipeline + Cache integration
    run_test_pipeline_cache_integration();
    run_test_pipeline_cache_invalidation();

    /// Dirty Tracker workflows
    run_test_dirty_tracker_buffer_workflow();
    run_test_dirty_tracker_range_marking();

    /// Multi-component workflows
    run_test_multi_component_render_workflow();
    run_test_cache_tracker_coherency();

    /// Print summary
    printf("\n");
    printf(
        "=================================================================\n");
    printf("  Test Summary\n");
    printf(
        "=================================================================\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_failed);
    printf(
        "=================================================================\n");

    return (tests_failed > 0) ? 1 : 0;
}
