/**
 * @file test_event_filters_timers.c
 * @brief Unit tests for LLE event filters, timers, and enhanced statistics
 *
 * Tests cover:
 * - Event filtering system
 * - Timer events
 * - Enhanced statistics
 * - Priority queue handling
 * - Integration of all features
 *
 * SPECIFICATION: docs/lle_specification/04_event_system_complete.md
 *
 */

#include "lle/error_handling.h"
#include "lle/event_system.h"
#include "lle/memory_management.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Mock memory pool
static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

/// Filter callback state
static int filter_call_count = 0;
static lle_filter_result_t filter_return_value = LLE_FILTER_PASS;

/* ============================================================================
 * TEST FILTER CALLBACKS
 * ============================================================================
 */

static lle_filter_result_t test_filter_pass(lle_event_t *event,
                                            void *user_data) {
    filter_call_count++;
    (void)event;
    (void)user_data;
    return LLE_FILTER_PASS;
}

static lle_filter_result_t test_filter_block(lle_event_t *event,
                                             void *user_data) {
    filter_call_count++;
    (void)event;
    (void)user_data;
    return LLE_FILTER_BLOCK;
}

LLE_MAYBE_UNUSED
static lle_filter_result_t test_filter_configurable(lle_event_t *event,
                                                    void *user_data) {
    filter_call_count++;
    (void)event;
    (void)user_data;
    return filter_return_value;
}

/* ============================================================================
 * FILTER SYSTEM TESTS
 * ============================================================================
 */

TEST(filter_system_init) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(system != NULL);

    /// Initialize filter system
    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(system->filter_system != NULL);

    /// Double init should be safe
    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(filter_add_remove) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Add filter
    result =
        lle_event_filter_add(system, "test_filter", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    /// Add duplicate should fail
    result =
        lle_event_filter_add(system, "test_filter", test_filter_pass, NULL);
    ASSERT(result != LLE_SUCCESS);

    /// Remove filter
    result = lle_event_filter_remove(system, "test_filter");
    ASSERT(result == LLE_SUCCESS);

    /// Remove non-existent should fail
    result = lle_event_filter_remove(system, "nonexistent");
    ASSERT(result != LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(filter_enable_disable) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_event_filter_add(system, "test_filter", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    /// Disable filter
    result = lle_event_filter_disable(system, "test_filter");
    ASSERT(result == LLE_SUCCESS);

    /// Enable filter
    result = lle_event_filter_enable(system, "test_filter");
    ASSERT(result == LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(filter_multiple_filters) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Add multiple filters
    result = lle_event_filter_add(system, "filter1", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_add(system, "filter2", test_filter_block, NULL);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_add(system, "filter3", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    /// Remove middle filter
    result = lle_event_filter_remove(system, "filter2");
    ASSERT(result == LLE_SUCCESS);

    /// Verify others still exist by trying to add duplicates
    result = lle_event_filter_add(system, "filter1", test_filter_pass, NULL);
    ASSERT(result != LLE_SUCCESS); /// Should fail - already exists

    lle_event_system_destroy(system);
}

TEST(filter_statistics) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result =
        lle_event_filter_add(system, "test_filter", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    /// A freshly added filter has processed nothing: every counter is 0.
    uint64_t filtered = 1, passed = 1, blocked = 1, transformed = 1,
             errored = 1;
    result =
        lle_event_filter_get_stats(system, "test_filter", &filtered, &passed,
                                   &blocked, &transformed, &errored);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(filtered == 0);
    ASSERT(passed == 0);
    ASSERT(blocked == 0);
    ASSERT(transformed == 0);
    ASSERT(errored == 0);

    /// Querying an unknown filter is an error.
    result =
        lle_event_filter_get_stats(system, "nonexistent", &filtered, &passed,
                                   &blocked, &transformed, &errored);
    ASSERT(result != LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(filter_apply_passes_event) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);
    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_add(system, "passer", test_filter_pass, NULL);
    ASSERT(result == LLE_SUCCESS);

    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    /// Applying the filter must invoke the callback and let the event through.
    filter_call_count = 0;
    lle_filter_result_t fr = lle_event_filter_apply(system, event);
    ASSERT(fr == LLE_FILTER_PASS);
    ASSERT(filter_call_count == 1);

    /// The pass is reflected in the filter's statistics.
    uint64_t filtered = 0, passed = 0, blocked = 0, transformed = 0,
             errored = 0;
    result = lle_event_filter_get_stats(system, "passer", &filtered, &passed,
                                        &blocked, &transformed, &errored);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(filtered == 1);
    ASSERT(passed == 1);
    ASSERT(blocked == 0);

    lle_event_system_destroy(system);
}

TEST(filter_apply_blocks_event) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);
    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_filter_add(system, "blocker", test_filter_block, NULL);
    ASSERT(result == LLE_SUCCESS);

    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    /// A blocking filter stops the event and records a block.
    filter_call_count = 0;
    lle_filter_result_t fr = lle_event_filter_apply(system, event);
    ASSERT(fr == LLE_FILTER_BLOCK);
    ASSERT(filter_call_count == 1);

    uint64_t filtered = 0, passed = 0, blocked = 0, transformed = 0,
             errored = 0;
    result = lle_event_filter_get_stats(system, "blocker", &filtered, &passed,
                                        &blocked, &transformed, &errored);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(blocked == 1);
    ASSERT(passed == 0);

    /// A disabled filter is skipped: the event passes and the callback is not
    /// invoked again.
    result = lle_event_filter_disable(system, "blocker");
    ASSERT(result == LLE_SUCCESS);
    filter_call_count = 0;
    fr = lle_event_filter_apply(system, event);
    ASSERT(fr == LLE_FILTER_PASS);
    ASSERT(filter_call_count == 0);

    lle_event_system_destroy(system);
}

/* ============================================================================
 * TIMER SYSTEM TESTS
 * ============================================================================
 */

TEST(timer_system_init) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    /// Timer system is created on demand
    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(system->timer_system != NULL);

    lle_event_system_destroy(system);
}

TEST(timer_oneshot_add_cancel) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Create event for timer
    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_TIMER_EXPIRED, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    /// Add one-shot timer (100ms delay)
    uint64_t timer_id = 0;
    result = lle_event_timer_add_oneshot(system, event, 100000, &timer_id);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(timer_id > 0);

    /// Cancel timer
    result = lle_event_timer_cancel(system, timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Cancel non-existent timer should fail
    result = lle_event_timer_cancel(system, 99999);
    ASSERT(result != LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(timer_repeating_add) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    lle_event_t *event = NULL;
    result =
        lle_event_create(system, LLE_EVENT_PERIODIC_UPDATE, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    /// Add repeating timer (50ms initial, 100ms interval)
    uint64_t timer_id = 0;
    result =
        lle_event_timer_add_repeating(system, event, 50000, 100000, &timer_id);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(timer_id > 0);

    lle_event_timer_cancel(system, timer_id);
    lle_event_system_destroy(system);
}

TEST(timer_enable_disable) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_TIMER_EXPIRED, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    uint64_t timer_id = 0;
    result = lle_event_timer_add_oneshot(system, event, 100000, &timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Disable timer
    result = lle_event_timer_disable(system, timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Enable timer
    result = lle_event_timer_enable(system, timer_id);
    ASSERT(result == LLE_SUCCESS);

    lle_event_timer_cancel(system, timer_id);
    lle_event_system_destroy(system);
}

TEST(timer_get_info) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_TIMER_EXPIRED, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    uint64_t timer_id = 0;
    result =
        lle_event_timer_add_repeating(system, event, 50000, 100000, &timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Get timer info
    uint64_t next_fire = 0, fire_count = 0;
    bool is_repeating = false;
    result = lle_event_timer_get_info(system, timer_id, &next_fire, &fire_count,
                                      &is_repeating);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(is_repeating == true);
    ASSERT(fire_count == 0);

    lle_event_timer_cancel(system, timer_id);
    lle_event_system_destroy(system);
}

TEST(timer_process_callable) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Process timers with no timers - should succeed
    result = lle_event_timer_process(system);
    ASSERT(result == LLE_SUCCESS);

    /// Add a timer with long delay
    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_TIMER_EXPIRED, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    uint64_t timer_id = 0;
    result = lle_event_timer_add_oneshot(system, event, 1000000, &timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Process timers - none should fire yet
    result = lle_event_timer_process(system);
    ASSERT(result == LLE_SUCCESS);

    lle_event_timer_cancel(system, timer_id);
    lle_event_system_destroy(system);
}

TEST(timer_statistics) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Get initial stats
    uint64_t created = 0, fired = 0, cancelled = 0;
    result = lle_event_timer_get_stats(system, &created, &fired, &cancelled);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(created == 0);

    /// Add a timer
    lle_event_t *event = NULL;
    result = lle_event_create(system, LLE_EVENT_TIMER_EXPIRED, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);

    uint64_t timer_id = 0;
    result = lle_event_timer_add_oneshot(system, event, 1000000, &timer_id);
    ASSERT(result == LLE_SUCCESS);

    /// Check stats again
    result = lle_event_timer_get_stats(system, &created, &fired, &cancelled);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(created == 1);

    lle_event_timer_cancel(system, timer_id);
    lle_event_system_destroy(system);
}

/* ============================================================================
 * ENHANCED STATISTICS TESTS
 * ============================================================================
 */

TEST(enhanced_stats_init) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    /// Initialize enhanced stats
    result = lle_event_enhanced_stats_init(system);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(system->enhanced_stats != NULL);

    lle_event_system_destroy(system);
}

TEST(enhanced_stats_per_type) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_enhanced_stats_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Get stats for specific type
    lle_event_type_stats_t stats;
    result =
        lle_event_enhanced_stats_get_type(system, LLE_EVENT_KEY_PRESS, &stats);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(stats.count == 0); /// No events processed yet

    lle_event_system_destroy(system);
}

TEST(enhanced_stats_all_types) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_enhanced_stats_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Get all type stats
    lle_event_type_stats_t *stats = NULL;
    size_t num_types = 0;
    result = lle_event_enhanced_stats_get_all_types(system, &stats, &num_types);
    ASSERT(result == LLE_SUCCESS);

    lle_event_system_destroy(system);
}

TEST(enhanced_stats_cycles) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_enhanced_stats_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// Get cycle stats
    uint64_t total_cycles = 0, total_time = 0, min_time = 0, max_time = 0;
    result = lle_event_enhanced_stats_get_cycles(
        system, &total_cycles, &total_time, &min_time, &max_time);
    ASSERT(result == LLE_SUCCESS);

    lle_event_system_destroy(system);
}

/* ============================================================================
 * PRIORITY QUEUE TESTS
 * ============================================================================
 */

TEST(priority_queue_exists) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    /// Priority queue should be created during init
    ASSERT(system->priority_queue != NULL);

    lle_event_system_destroy(system);
}

TEST(critical_events_use_priority_queue) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    /// Create CRITICAL priority event (TERMINAL_RESIZE is marked CRITICAL)
    lle_event_t *event = NULL;
    result =
        lle_event_create(system, LLE_EVENT_TERMINAL_RESIZE, NULL, 0, &event);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(event != NULL);
    ASSERT(event->priority == LLE_PRIORITY_CRITICAL);

    lle_event_destroy(system, event);
    lle_event_system_destroy(system);
}

/* ============================================================================
 * INTEGRATION TESTS
 * ============================================================================
 */

TEST(all_systems_together) {
    lle_event_system_t *system = NULL;
    lle_result_t result = lle_event_system_init(&system, mock_pool);
    ASSERT(result == LLE_SUCCESS);

    /// Initialize all systems
    result = lle_event_filter_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_timer_system_init(system);
    ASSERT(result == LLE_SUCCESS);

    result = lle_event_enhanced_stats_init(system);
    ASSERT(result == LLE_SUCCESS);

    /// All systems should be initialized
    ASSERT(system->filter_system != NULL);
    ASSERT(system->timer_system != NULL);
    ASSERT(system->enhanced_stats != NULL);
    ASSERT(system->priority_queue != NULL);

    lle_event_system_destroy(system);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Event Filters and Timers Tests\n");
    printf("====================================\n\n");

    printf("Filter System Tests:\n");
    RUN_TEST(filter_system_init);
    RUN_TEST(filter_add_remove);
    RUN_TEST(filter_enable_disable);
    RUN_TEST(filter_multiple_filters);
    RUN_TEST(filter_statistics);
    RUN_TEST(filter_apply_passes_event);
    RUN_TEST(filter_apply_blocks_event);

    printf("\nTimer System Tests:\n");
    RUN_TEST(timer_system_init);
    RUN_TEST(timer_oneshot_add_cancel);
    RUN_TEST(timer_repeating_add);
    RUN_TEST(timer_enable_disable);
    RUN_TEST(timer_get_info);
    RUN_TEST(timer_process_callable);
    RUN_TEST(timer_statistics);

    printf("\nEnhanced Statistics Tests:\n");
    RUN_TEST(enhanced_stats_init);
    RUN_TEST(enhanced_stats_per_type);
    RUN_TEST(enhanced_stats_all_types);
    RUN_TEST(enhanced_stats_cycles);

    printf("\nPriority Queue Tests:\n");
    RUN_TEST(priority_queue_exists);
    RUN_TEST(critical_events_use_priority_queue);

    printf("\nIntegration Tests:\n");
    RUN_TEST(all_systems_together);

    return TEST_RESULT();
}
