/*
 * test_parser_state_machine.c - Unit tests for Parser State Machine
 *
 * Tests state transition management and coordination logic for input parsing.
 *
 * Test Coverage:
 * - Initialization and destruction
 * - State transitions
 * - State queries
 * - Process function routing hints
 * - Error recovery
 * - Statistics tracking
 * - Reset functionality
 *
 * Spec 06: Input Parsing - Phase 6
 */

#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/input_parsing.h"
#include "test_framework.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* External memory pool functions (mock) */
extern void *lle_pool_alloc(size_t size);
extern void lle_pool_free(void *ptr);

#define TEST_ASSERT(cond, msg) ASSERT_TRUE(cond, msg)
#define TEST_PASS(name) ((void)0)

/*
 * Test: Initialize and destroy state machine
 */
static void test_init_destroy(void) {
    lle_parser_state_machine_t *sm = NULL;

    /* Initialize */
    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");
    TEST_ASSERT(sm != NULL, "State machine should not be NULL");

    /* Check initial state */
    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_NORMAL,
                "Initial state should be NORMAL");

    /* Destroy */
    result = lle_parser_state_machine_destroy(sm);
    TEST_ASSERT(result == LLE_SUCCESS, "Destroy should succeed");

    TEST_PASS("test_init_destroy");
}

/*
 * Test: Manual state transitions
 */
static void test_state_transitions(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Transition to ESCAPE */
    result = lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);
    TEST_ASSERT(result == LLE_SUCCESS, "Transition should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_ESCAPE, "Should be in ESCAPE state");

    lle_parser_state_t prev = lle_parser_state_machine_get_previous_state(sm);
    TEST_ASSERT(prev == LLE_PARSER_STATE_NORMAL, "Previous should be NORMAL");

    /* Transition to CSI */
    result = lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_CSI);
    TEST_ASSERT(result == LLE_SUCCESS, "Transition should succeed");

    state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_CSI, "Should be in CSI state");

    prev = lle_parser_state_machine_get_previous_state(sm);
    TEST_ASSERT(prev == LLE_PARSER_STATE_ESCAPE, "Previous should be ESCAPE");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_state_transitions");
}

/*
 * Test: Transition count tracking
 */
static void test_transition_count(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    uint64_t count = lle_parser_state_machine_get_transitions(sm);
    TEST_ASSERT(count == 0, "Initial transition count should be 0");

    /* Make some transitions */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_CSI);
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_NORMAL);

    count = lle_parser_state_machine_get_transitions(sm);
    TEST_ASSERT(count == 3, "Should have 3 transitions");

    /* Transition to same state doesn't count */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_NORMAL);
    count = lle_parser_state_machine_get_transitions(sm);
    TEST_ASSERT(count == 3, "Same-state transition shouldn't count");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_transition_count");
}

/*
 * Test: Process function with escape sequence
 */
static void test_process_escape(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Start in NORMAL state */
    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should start in NORMAL");

    /* Process ESC character */
    const char data[] = "\x1B";
    result = lle_parser_state_machine_process(sm, NULL, data, 1);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_ESCAPE,
                "Should transition to ESCAPE");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_escape");
}

/*
 * Test: Process function with CSI sequence
 */
static void test_process_csi(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Process ESC[ */
    const char data[] = "\x1B[";
    result = lle_parser_state_machine_process(sm, NULL, data, 2);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    /* Should still be in ESCAPE (need more data to determine CSI vs mouse) */
    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_ESCAPE, "Should be in ESCAPE");

    /* Process with third character */
    const char data2[] = "\x1B[A";
    result = lle_parser_state_machine_process(sm, NULL, data2, 3);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_CSI, "Should transition to CSI");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_csi");
}

/*
 * Test: Process function with mouse sequence
 */
static void test_process_mouse(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Set to ESCAPE state first */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    /* Process ESC[M (X10 mouse) */
    const char data[] = "\x1B[M";
    result = lle_parser_state_machine_process(sm, NULL, data, 3);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_MOUSE, "Should transition to MOUSE");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_mouse");
}

/*
 * Test: Process function with SGR mouse sequence
 */
static void test_process_sgr_mouse(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Set to ESCAPE state */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    /* Process ESC[< (SGR mouse) */
    const char data[] = "\x1B[<";
    result = lle_parser_state_machine_process(sm, NULL, data, 3);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_MOUSE, "Should transition to MOUSE");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_sgr_mouse");
}

/*
 * Test: Process function with OSC sequence
 */
static void test_process_osc(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Set to ESCAPE state */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    /* Process ESC] */
    const char data[] = "\x1B]";
    result = lle_parser_state_machine_process(sm, NULL, data, 2);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_OSC, "Should transition to OSC");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_osc");
}

/*
 * Test: Process function with key sequence (SS3)
 */
static void test_process_key_ss3(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Set to ESCAPE state */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    /* Process ESC O (SS3) */
    const char data[] = "\x1BO";
    result = lle_parser_state_machine_process(sm, NULL, data, 2);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_KEY_SEQUENCE,
                "Should transition to KEY_SEQUENCE");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_key_ss3");
}

/*
 * Test: Error recovery
 */
static void test_error_recovery(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    uint32_t errors = lle_parser_state_machine_get_error_recoveries(sm);
    TEST_ASSERT(errors == 0, "Should have no errors initially");

    /* Transition to error state */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ERROR_RECOVERY);

    bool is_error = lle_parser_state_machine_is_error_state(sm);
    TEST_ASSERT(is_error == true, "Should be in error state");

    /* Process to recover */
    const char data[] = "x";
    result = lle_parser_state_machine_process(sm, NULL, data, 1);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should recover to NORMAL");

    errors = lle_parser_state_machine_get_error_recoveries(sm);
    TEST_ASSERT(errors == 1, "Should have 1 error recovery");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_error_recovery");
}

/*
 * Test: Time in state tracking
 */
static void test_time_in_state(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    uint64_t time1 = lle_parser_state_machine_time_in_state(sm);
    /* time1 is uint64_t, always non-negative - just verify we got a value */
    (void)time1;

    /* Wait a bit and check again */
    for (volatile int i = 0; i < 100000; i++)
        ;

    uint64_t time2 = lle_parser_state_machine_time_in_state(sm);
    TEST_ASSERT(time2 > time1, "Time should increase");

    /* Transition to new state resets timer */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    uint64_t time3 = lle_parser_state_machine_time_in_state(sm);
    TEST_ASSERT(time3 < time2, "Time should reset after transition");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_time_in_state");
}

/*
 * Test: Reset functionality
 */
static void test_reset(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Make some state changes */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_CSI);
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ERROR_RECOVERY);

    uint64_t count = lle_parser_state_machine_get_transitions(sm);
    TEST_ASSERT(count == 3, "Should have 3 transitions");

    /* Process to increment error count */
    const char data[] = "x";
    lle_parser_state_machine_process(sm, NULL, data, 1);

    uint32_t errors = lle_parser_state_machine_get_error_recoveries(sm);
    TEST_ASSERT(errors == 1, "Should have 1 error");

    /* Reset */
    result = lle_parser_state_machine_reset(sm);
    TEST_ASSERT(result == LLE_SUCCESS, "Reset should succeed");

    /* Check everything is reset */
    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should be in NORMAL state");

    count = lle_parser_state_machine_get_transitions(sm);
    TEST_ASSERT(count == 0, "Transition count should be reset");

    errors = lle_parser_state_machine_get_error_recoveries(sm);
    TEST_ASSERT(errors == 0, "Error count should be reset");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_reset");
}

/*
 * Test: DCS sequence routing
 */
static void test_process_dcs(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Set to ESCAPE state */
    lle_parser_state_machine_transition(sm, LLE_PARSER_STATE_ESCAPE);

    /* Process ESC P (DCS) */
    const char data[] = "\x1BP";
    result = lle_parser_state_machine_process(sm, NULL, data, 2);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_DCS, "Should transition to DCS");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_dcs");
}

/*
 * Test: Normal text doesn't change state
 */
static void test_process_normal_text(void) {
    lle_parser_state_machine_t *sm = NULL;

    lle_result_t result = lle_parser_state_machine_init(&sm, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Init should succeed");

    /* Process normal text */
    const char data[] = "Hello";
    result = lle_parser_state_machine_process(sm, NULL, data, 5);
    TEST_ASSERT(result == LLE_SUCCESS, "Process should succeed");

    lle_parser_state_t state = lle_parser_state_machine_get_state(sm);
    TEST_ASSERT(state == LLE_PARSER_STATE_NORMAL, "Should stay in NORMAL");

    lle_parser_state_machine_destroy(sm);

    TEST_PASS("test_process_normal_text");
}

/*
 * Main test runner
 */
int main(void) {
    printf("=== LLE Parser State Machine Unit Tests ===\n\n");

    /* Run tests */
    RUN_TEST(init_destroy);
    RUN_TEST(state_transitions);
    RUN_TEST(transition_count);
    RUN_TEST(process_escape);
    RUN_TEST(process_csi);
    RUN_TEST(process_mouse);
    RUN_TEST(process_sgr_mouse);
    RUN_TEST(process_osc);
    RUN_TEST(process_key_ss3);
    RUN_TEST(error_recovery);
    RUN_TEST(time_in_state);
    RUN_TEST(reset);
    RUN_TEST(process_dcs);
    RUN_TEST(process_normal_text);
    return TEST_RESULT();
}
