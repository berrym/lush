/**
 * @file test_event_system.c
 * @brief Unit tests for LLE Event System (Spec 04 Phase 1)
 *
 * Tests cover:
 * - Event system lifecycle (init/stop)
 * - Event creation and destruction
 * - Event queue operations (enqueue/dequeue)
 * - Handler registration and dispatching
 * - Statistics tracking
 * - Error handling
 *
 * SPECIFICATION: docs/lle_specification/04_event_system_complete.md
 * PHASE: Phase 1 - Core Infrastructure
 *
 * NOTE: Uses mock memory pool (test_memory_mock.c) for standalone testing
 */

#include "lle/error_handling.h"
#include "lle/event_system.h"
#include "lle/memory_management.h"
#include "test_framework.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock memory pool instance - just a non-NULL pointer for validation
static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

// Test event handler state
static int handler_call_count = 0;
static lle_event_t *last_handled_event = NULL;
static void *last_handler_user_data = NULL;
static lle_result_t handler_return_value = LLE_SUCCESS;

/* ========================================================================== */
// TEST HELPER FUNCTIONS
/* ========================================================================== */

/**
 * @brief Reset handler state before each test
 */
static void reset_handler_state(void) {
    handler_call_count = 0;
    last_handled_event = NULL;
    last_handler_user_data = NULL;
    handler_return_value = LLE_SUCCESS;
}

/**
 * @brief Test event handler callback
 */
static lle_result_t test_event_handler(lle_event_t *event, void *user_data) {
    handler_call_count++;
    last_handled_event = event;
    last_handler_user_data = user_data;
    return handler_return_value;
}

/* ========================================================================== */
// EVENT SYSTEM LIFECYCLE TESTS
/* ========================================================================== */

TEST(event_system_init_success) {
    lle_event_system_t *system = NULL;

    lle_result_t result = lle_event_system_init(&system, mock_pool);

    ASSERT_EQ(result, LLE_SUCCESS, "Event system init should succeed");
    ASSERT_NOT_NULL(system, "System should be allocated");
    ASSERT_NOT_NULL(system->queue, "Queue should be initialized");
    ASSERT_NOT_NULL(system->handlers, "Handler array should be initialized");
    ASSERT_EQ(system->active, true, "System should be active");
    ASSERT_EQ(system->sequence_counter, 1,
              "Sequence counter should start at 1");

    lle_event_system_stop(system);
}

TEST(event_system_init_null_system) {
    lle_result_t result = lle_event_system_init(NULL, mock_pool);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should reject NULL system pointer");
}

TEST(event_system_init_null_pool) {
    lle_event_system_t *system = NULL;

    lle_result_t result = lle_event_system_init(&system, NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should reject NULL memory pool");
}

TEST(event_system_stop_success) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result = lle_event_system_stop(system);

    ASSERT_EQ(result, LLE_SUCCESS, "Stop should succeed");
    ASSERT_EQ(system->active, false, "System should be inactive");
}

TEST(event_system_stop_null_system) {
    lle_result_t result = lle_event_system_stop(NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Should reject NULL system");
}

/* ========================================================================== */
// EVENT CREATION TESTS
/* ========================================================================== */

TEST(event_create_success_no_data) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);

    ASSERT_EQ(result, LLE_SUCCESS, "Event creation should succeed");
    ASSERT_NOT_NULL(event, "Event should be allocated");
    ASSERT_EQ(event->type, LLE_EVENT_KEY_PRESS, "Event type should match");
    ASSERT_EQ(event->sequence_number, 1, "First event should have sequence 1");
    ASSERT_NULL(event->data, "Data should be NULL");
    ASSERT_EQ(event->data_size, 0, "Data size should be 0");
    ASSERT_TRUE(event->timestamp > 0, "Timestamp should be set");

    lle_event_destroy(system, event);
    lle_event_system_stop(system);
}

TEST(event_create_success_with_data) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    const char test_data[] = "test_event_data";
    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(system, LLE_EVENT_BUFFER_CHANGED, (void *)test_data,
                         strlen(test_data) + 1, &event);

    ASSERT_EQ(result, LLE_SUCCESS, "Event creation should succeed");
    ASSERT_NOT_NULL(event, "Event should be allocated");
    ASSERT_NOT_NULL(event->data, "Data should be copied");
    ASSERT_EQ(event->data_size, strlen(test_data) + 1,
              "Data size should match");
    ASSERT_EQ(strcmp(event->data, test_data), 0, "Data content should match");

    lle_event_destroy(system, event);
    lle_event_system_stop(system);
}

TEST(event_create_sequence_numbers) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event1 = NULL, *event2 = NULL, *event3 = NULL;

    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event1);
    lle_event_create(system, LLE_EVENT_CURSOR_MOVED, NULL, 0, &event2);
    lle_event_create(system, LLE_EVENT_DISPLAY_UPDATE, NULL, 0, &event3);

    ASSERT_EQ(event1->sequence_number, 1, "First event sequence");
    ASSERT_EQ(event2->sequence_number, 2, "Second event sequence");
    ASSERT_EQ(event3->sequence_number, 3, "Third event sequence");

    lle_event_destroy(system, event1);
    lle_event_destroy(system, event2);
    lle_event_destroy(system, event3);
    lle_event_system_stop(system);
}

TEST(event_create_null_system) {
    lle_event_t *event = NULL;
    lle_result_t result =
        lle_event_create(NULL, LLE_EVENT_KEY_PRESS, NULL, 0, &event);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Should reject NULL system");
}

TEST(event_create_null_event_ptr) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result =
        lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should reject NULL event pointer");

    lle_event_system_stop(system);
}

TEST(event_clone_success) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    const char test_data[] = "original_data";
    lle_event_t *original = NULL;
    lle_event_create(system, LLE_EVENT_KEY_SEQUENCE, (void *)test_data,
                     strlen(test_data) + 1, &original);

    lle_event_t *clone = NULL;
    lle_result_t result = lle_event_clone(system, original, &clone);

    ASSERT_EQ(result, LLE_SUCCESS, "Clone should succeed");
    ASSERT_NOT_NULL(clone, "Clone should be allocated");
    ASSERT_EQ(clone->type, original->type, "Type should match");
    ASSERT_EQ(clone->data_size, original->data_size, "Data size should match");
    ASSERT_EQ(strcmp(clone->data, original->data), 0,
              "Data content should match");
    ASSERT_TRUE(clone->data != original->data, "Data should be separate copy");

    lle_event_destroy(system, original);
    lle_event_destroy(system, clone);
    lle_event_system_stop(system);
}

/* ========================================================================== */
// EVENT QUEUE TESTS
/* ========================================================================== */

TEST(event_enqueue_success) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);

    lle_result_t result = lle_event_enqueue(system, event);

    ASSERT_EQ(result, LLE_SUCCESS, "Enqueue should succeed");
    ASSERT_EQ(system->queue->count, 1, "Queue count should be 1");

    lle_event_system_stop(system);
}

TEST(event_enqueue_multiple) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    for (int i = 0; i < 10; i++) {
        lle_event_t *event = NULL;
        lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
        lle_result_t result = lle_event_enqueue(system, event);
        ASSERT_EQ(result, LLE_SUCCESS, "Each enqueue should succeed");
    }

    ASSERT_EQ(system->queue->count, 10, "Queue should contain 10 events");

    lle_event_system_stop(system);
}

TEST(event_enqueue_null_system) {
    lle_event_t event;
    memset(&event, 0, sizeof(event));

    lle_result_t result = lle_event_enqueue(NULL, &event);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Should reject NULL system");
}

TEST(event_enqueue_null_event) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result = lle_event_enqueue(system, NULL);

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Should reject NULL event");

    lle_event_system_stop(system);
}

TEST(event_dequeue_success) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event1 = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event1);
    lle_event_enqueue(system, event1);

    lle_event_t *dequeued = NULL;
    lle_result_t result = lle_event_dequeue(system, &dequeued);

    ASSERT_EQ(result, LLE_SUCCESS, "Dequeue should succeed");
    ASSERT_NOT_NULL(dequeued, "Dequeued event should not be NULL");
    ASSERT_EQ(dequeued, event1, "Should dequeue the same event");
    ASSERT_EQ(system->queue->count, 0, "Queue should be empty");

    lle_event_destroy(system, dequeued);
    lle_event_system_stop(system);
}

TEST(event_dequeue_fifo_order) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event1 = NULL, *event2 = NULL, *event3 = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event1);
    lle_event_create(system, LLE_EVENT_CURSOR_MOVED, NULL, 0, &event2);
    lle_event_create(system, LLE_EVENT_DISPLAY_UPDATE, NULL, 0, &event3);

    lle_event_enqueue(system, event1);
    lle_event_enqueue(system, event2);
    lle_event_enqueue(system, event3);

    lle_event_t *d1 = NULL, *d2 = NULL, *d3 = NULL;
    lle_event_dequeue(system, &d1);
    lle_event_dequeue(system, &d2);
    lle_event_dequeue(system, &d3);

    ASSERT_EQ(d1, event1, "First event should be first out");
    ASSERT_EQ(d2, event2, "Second event should be second out");
    ASSERT_EQ(d3, event3, "Third event should be third out");

    lle_event_destroy(system, d1);
    lle_event_destroy(system, d2);
    lle_event_destroy(system, d3);
    lle_event_system_stop(system);
}

TEST(event_dequeue_empty_queue) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_t *event = NULL;
    lle_result_t result = lle_event_dequeue(system, &event);

    ASSERT_EQ(result, LLE_ERROR_QUEUE_EMPTY, "Should return queue empty error");
    ASSERT_NULL(event, "Event should be NULL");

    lle_event_system_stop(system);
}

TEST(event_queue_size) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    size_t size = lle_event_queue_size(system);
    ASSERT_EQ(size, 0, "Initial queue size should be 0");

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
    lle_event_enqueue(system, event);

    size = lle_event_queue_size(system);
    ASSERT_EQ(size, 1, "Queue size should be 1 after enqueue");

    lle_event_system_stop(system);
}

TEST(event_queue_empty_check) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    ASSERT_TRUE(lle_event_queue_empty(system),
                "Queue should be empty initially");

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
    lle_event_enqueue(system, event);

    ASSERT_FALSE(lle_event_queue_empty(system),
                 "Queue should not be empty after enqueue");

    lle_event_system_stop(system);
}

/* ========================================================================== */
// EVENT HANDLER TESTS
/* ========================================================================== */

TEST(handler_register_success) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result = lle_event_handler_register(
        system, LLE_EVENT_KEY_PRESS, test_event_handler, NULL, "test_handler");

    ASSERT_EQ(result, LLE_SUCCESS, "Handler registration should succeed");
    ASSERT_EQ(system->handler_count, 1, "Handler count should be 1");

    lle_event_system_stop(system);
}

TEST(handler_register_multiple_types) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "handler1");
    lle_event_handler_register(system, LLE_EVENT_CURSOR_MOVED,
                               test_event_handler, NULL, "handler2");
    lle_event_handler_register(system, LLE_EVENT_BUFFER_CHANGED,
                               test_event_handler, NULL, "handler3");

    ASSERT_EQ(system->handler_count, 3, "Should have 3 handlers registered");

    lle_event_system_stop(system);
}

TEST(handler_register_null_system) {
    lle_result_t result = lle_event_handler_register(
        NULL, LLE_EVENT_KEY_PRESS, test_event_handler, NULL, "test");

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Should reject NULL system");
}

TEST(handler_register_null_function) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result = lle_event_handler_register(
        system, LLE_EVENT_KEY_PRESS, NULL, NULL, "test");

    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Should reject NULL handler function");

    lle_event_system_stop(system);
}

TEST(handler_dispatch_success) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test");

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);

    lle_result_t result = lle_event_dispatch(system, event);

    ASSERT_EQ(result, LLE_SUCCESS, "Dispatch should succeed");
    ASSERT_EQ(handler_call_count, 1, "Handler should be called once");
    ASSERT_EQ(last_handled_event, event,
              "Handler should receive correct event");

    lle_event_destroy(system, event);
    lle_event_system_stop(system);
}

TEST(handler_dispatch_no_matching_handler) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test");

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_CURSOR_MOVED, NULL, 0, &event);

    lle_result_t result = lle_event_dispatch(system, event);

    ASSERT_EQ(result, LLE_SUCCESS,
              "Dispatch should succeed even with no handlers");
    ASSERT_EQ(handler_call_count, 0, "Handler should not be called");

    lle_event_destroy(system, event);
    lle_event_system_stop(system);
}

TEST(handler_dispatch_user_data) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    int test_data = 42;
    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               &test_data, "test");

    lle_event_t *event = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);

    lle_event_dispatch(system, event);

    ASSERT_EQ(last_handler_user_data, &test_data,
              "Handler should receive user data");
    ASSERT_EQ(*(int *)last_handler_user_data, 42,
              "User data content should be correct");

    lle_event_destroy(system, event);
    lle_event_system_stop(system);
}

TEST(handler_unregister_by_name) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test_handler");
    ASSERT_EQ(system->handler_count, 1, "Should have 1 handler");

    lle_result_t result = lle_event_handler_unregister(
        system, LLE_EVENT_KEY_PRESS, "test_handler");

    ASSERT_EQ(result, LLE_SUCCESS, "Unregister should succeed");
    ASSERT_EQ(system->handler_count, 0, "Should have 0 handlers");

    lle_event_system_stop(system);
}

TEST(handler_unregister_not_found) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_result_t result = lle_event_handler_unregister(
        system, LLE_EVENT_KEY_PRESS, "nonexistent");

    ASSERT_EQ(result, LLE_ERROR_NOT_FOUND, "Should return not found error");

    lle_event_system_stop(system);
}

TEST(event_process_queue_success) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test");

    // Enqueue 3 events
    for (int i = 0; i < 3; i++) {
        lle_event_t *event = NULL;
        lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
        lle_event_enqueue(system, event);
    }

    lle_result_t result = lle_event_process_queue(system, 10);

    ASSERT_EQ(result, LLE_SUCCESS, "Process queue should succeed");
    ASSERT_EQ(handler_call_count, 3, "Handler should be called 3 times");
    ASSERT_EQ(system->queue->count, 0, "Queue should be empty");

    lle_event_system_stop(system);
}

TEST(event_process_queue_max_events) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test");

    // Enqueue 10 events
    for (int i = 0; i < 10; i++) {
        lle_event_t *event = NULL;
        lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event);
        lle_event_enqueue(system, event);
    }

    lle_result_t result = lle_event_process_queue(system, 5);

    ASSERT_EQ(result, LLE_SUCCESS, "Process queue should succeed");
    ASSERT_EQ(handler_call_count, 5, "Should process only 5 events");
    ASSERT_EQ(system->queue->count, 5, "Should have 5 events remaining");

    lle_event_system_stop(system);
}

/* ========================================================================== */
// STATISTICS TESTS
/* ========================================================================== */

TEST(statistics_events_created) {
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    uint64_t initial = system->events_created;

    lle_event_t *event1 = NULL, *event2 = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event1);
    lle_event_create(system, LLE_EVENT_CURSOR_MOVED, NULL, 0, &event2);

    ASSERT_EQ(system->events_created, initial + 2,
              "Should track created events");

    lle_event_destroy(system, event1);
    lle_event_destroy(system, event2);
    lle_event_system_stop(system);
}

TEST(statistics_events_dispatched) {
    reset_handler_state();
    lle_event_system_t *system = NULL;
    lle_event_system_init(&system, mock_pool);

    lle_event_handler_register(system, LLE_EVENT_KEY_PRESS, test_event_handler,
                               NULL, "test");

    uint64_t initial = system->events_dispatched;

    lle_event_t *event1 = NULL, *event2 = NULL;
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event1);
    lle_event_create(system, LLE_EVENT_KEY_PRESS, NULL, 0, &event2);

    lle_event_dispatch(system, event1);
    lle_event_dispatch(system, event2);

    ASSERT_EQ(system->events_dispatched, initial + 2,
              "Should track dispatched events");

    lle_event_destroy(system, event1);
    lle_event_destroy(system, event2);
    lle_event_system_stop(system);
}

/* ========================================================================== */
// UTILITY FUNCTION TESTS
/* ========================================================================== */

TEST(event_type_name_returns_valid) {
    const char *name1 = lle_event_type_name(LLE_EVENT_KEY_PRESS);
    const char *name2 = lle_event_type_name(LLE_EVENT_BUFFER_CHANGED);
    const char *name3 = lle_event_type_name(LLE_EVENT_SYSTEM_ERROR);

    ASSERT_NOT_NULL(name1, "Should return valid name");
    ASSERT_NOT_NULL(name2, "Should return valid name");
    ASSERT_NOT_NULL(name3, "Should return valid name");
    ASSERT_TRUE(strlen(name1) > 0, "Name should not be empty");
}

TEST(event_type_name_unknown) {
    const char *name = lle_event_type_name(0xFFFF);

    ASSERT_NOT_NULL(name, "Should return valid string");
    ASSERT_EQ(strcmp(name, "UNKNOWN"), 0,
              "Should return 'UNKNOWN' for invalid type");
}

/* ========================================================================== */
// MAIN TEST RUNNER
/* ========================================================================== */

int main(void) {
    printf("=== LLE Event System Phase 1 Tests ===\n\n");

    printf("--- Lifecycle ---\n");
    RUN_TEST(event_system_init_success);
    RUN_TEST(event_system_init_null_system);
    RUN_TEST(event_system_init_null_pool);
    RUN_TEST(event_system_stop_success);
    RUN_TEST(event_system_stop_null_system);

    printf("\n--- Event creation ---\n");
    RUN_TEST(event_create_success_no_data);
    RUN_TEST(event_create_success_with_data);
    RUN_TEST(event_create_sequence_numbers);
    RUN_TEST(event_create_null_system);
    RUN_TEST(event_create_null_event_ptr);
    RUN_TEST(event_clone_success);

    printf("\n--- Queue ---\n");
    RUN_TEST(event_enqueue_success);
    RUN_TEST(event_enqueue_multiple);
    RUN_TEST(event_enqueue_null_system);
    RUN_TEST(event_enqueue_null_event);
    RUN_TEST(event_dequeue_success);
    RUN_TEST(event_dequeue_fifo_order);
    RUN_TEST(event_dequeue_empty_queue);
    RUN_TEST(event_queue_size);
    RUN_TEST(event_queue_empty_check);

    printf("\n--- Handlers ---\n");
    RUN_TEST(handler_register_success);
    RUN_TEST(handler_register_multiple_types);
    RUN_TEST(handler_register_null_system);
    RUN_TEST(handler_register_null_function);
    RUN_TEST(handler_dispatch_success);
    RUN_TEST(handler_dispatch_no_matching_handler);
    RUN_TEST(handler_dispatch_user_data);
    RUN_TEST(handler_unregister_by_name);
    RUN_TEST(handler_unregister_not_found);
    RUN_TEST(event_process_queue_success);
    RUN_TEST(event_process_queue_max_events);

    printf("\n--- Statistics ---\n");
    RUN_TEST(statistics_events_created);
    RUN_TEST(statistics_events_dispatched);

    printf("\n--- Utility ---\n");
    RUN_TEST(event_type_name_returns_valid);
    RUN_TEST(event_type_name_unknown);

    return TEST_RESULT();
}
