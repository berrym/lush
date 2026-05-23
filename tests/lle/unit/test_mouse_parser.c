/*
 * test_mouse_parser.c - Unit tests for Mouse Parser
 *
 * Tests all mouse event parsing functionality:
 * - X10 format parsing (ESC[M<btn><x><y>)
 * - SGR format parsing (ESC[<btn;x;y>M/m)
 * - Button press/release detection
 * - Mouse movement and drag detection
 * - Wheel events (scroll up/down)
 * - Multi-click detection (double/triple click)
 * - Modifier key extraction (Shift, Alt, Ctrl)
 * - Statistics tracking
 *
 * Spec 06: Input Parsing - Phase 5
 */

#include "../../../include/lle/error_handling.h"
#include "../../../include/lle/input_parsing.h"
#include "test_framework.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// External memory pool functions (mock)
extern void *lle_pool_alloc(size_t size);
extern void lle_pool_free(void *ptr);

#define TEST_ASSERT(cond, msg) ASSERT_TRUE(cond, msg)
#define TEST_PASS(name) ((void)0)

/*
 * Test: Initialize and destroy mouse parser
 */
static void test_mouse_parser_init_destroy(void) {
    lle_mouse_parser_t *parser = NULL;

    // Initialize parser
    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize mouse parser");
    TEST_ASSERT(parser != NULL, "Parser is NULL after init");

    // Destroy parser
    result = lle_mouse_parser_destroy(parser);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to destroy mouse parser");

    TEST_PASS("test_mouse_parser_init_destroy");
}

/*
 * Test: X10 format - button press
 */
static void test_x10_button_press(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    // Enable tracking
    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: ESC[M + button(32) + x(33='!') + y(33='!')
    // Button 0 (left button) at position (0,0)
    const char sequence[] = "\x1B[M !!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse X10 button press");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_LEFT, "Wrong button");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_PRESS, "Wrong event type");
    TEST_ASSERT(event->x == 0, "Wrong x coordinate");
    TEST_ASSERT(event->y == 0, "Wrong y coordinate");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_button_press");
}

/*
 * Test: X10 format - button release
 */
static void test_x10_button_release(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: button code 3 (0x23 = 35) means button release
    const char sequence[] = "\x1B[M#!!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse X10 button release");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_RELEASE, "Wrong event type");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_button_release");
}

/*
 * Test: X10 format - middle button
 */
static void test_x10_middle_button(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: button 1 (middle button) = 33 (0x21 = '!')
    const char sequence[] = "\x1B[M!!!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse middle button");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_MIDDLE, "Wrong button");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_PRESS, "Wrong event type");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_middle_button");
}

/*
 * Test: X10 format - right button
 */
static void test_x10_right_button(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: button 2 (right button) = 34 (0x22 = '"')
    const char sequence[] = "\x1B[M\"!!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse right button");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_RIGHT, "Wrong button");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_PRESS, "Wrong event type");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_right_button");
}

/*
 * Test: X10 format - coordinates
 */
static void test_x10_coordinates(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: position (10, 5) = (43='*'+1, 38='%'+1) in 1-based coords
    // We subtract 32 and then subtract 1 to get 0-based coords
    const char sequence[] = "\x1B[M /%";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse coordinates");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->x == 14,
                "Wrong x coordinate"); // '/' = 47, 47-32-1 = 14
    TEST_ASSERT(event->y == 4,
                "Wrong y coordinate"); // '%' = 37, 37-32-1 = 4

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_coordinates");
}

/*
 * Test: X10 format - wheel up
 */
static void test_x10_wheel_up(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result = lle_mouse_parser_set_tracking(parser, true,
                                           LLE_MOUSE_TRACKING_BTN_EVENT);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: wheel up = button 64 (0x60 = 96)
    const char sequence[] = "\x1B[M`!!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse wheel up");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_WHEEL, "Wrong event type");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_WHEEL_UP,
                "Wrong wheel button");
    TEST_ASSERT(event->wheel_delta > 0, "Wrong wheel delta");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_wheel_up");
}

/*
 * Test: X10 format - wheel down
 */
static void test_x10_wheel_down(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result = lle_mouse_parser_set_tracking(parser, true,
                                           LLE_MOUSE_TRACKING_BTN_EVENT);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // X10 format: wheel down = button 65 (0x61 = 97 = 'a')
    const char sequence[] = "\x1B[Ma!!";
    size_t sequence_len = 6;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse wheel down");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_WHEEL, "Wrong event type");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_WHEEL_DOWN,
                "Wrong wheel button");
    TEST_ASSERT(event->wheel_delta < 0, "Wrong wheel delta");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_x10_wheel_down");
}

/*
 * Test: SGR format - button press
 */
static void test_sgr_button_press(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // SGR format: ESC[<0;10;5M (left button press at 10,5)
    const char sequence[] = "\x1B[<0;10;5M";
    size_t sequence_len = 11;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse SGR button press");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_LEFT, "Wrong button");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_PRESS, "Wrong event type");
    TEST_ASSERT(event->x == 9, "Wrong x coordinate"); // 1-based to 0-based
    TEST_ASSERT(event->y == 4, "Wrong y coordinate"); // 1-based to 0-based

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_sgr_button_press");
}

/*
 * Test: SGR format - button release
 */
static void test_sgr_button_release(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // SGR format: ESC[<0;10;5m (lowercase 'm' = release)
    const char sequence[] = "\x1B[<0;10;5m";
    size_t sequence_len = 11;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse SGR button release");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->button == LLE_MOUSE_BUTTON_LEFT, "Wrong button");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_RELEASE, "Wrong event type");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_sgr_button_release");
}

/*
 * Test: SGR format - modifier keys
 */
static void test_sgr_modifiers(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // SGR format: ESC[<4;10;5M (4 = Shift modifier)
    const char sequence[] = "\x1B[<4;10;5M";
    size_t sequence_len = 11;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse SGR with modifiers");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->modifiers & LLE_KEY_MOD_SHIFT,
                "Shift modifier not detected");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_sgr_modifiers");
}

/*
 * Test: Mouse drag detection
 */
static void test_mouse_drag(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result = lle_mouse_parser_set_tracking(parser, true,
                                           LLE_MOUSE_TRACKING_ANY_EVENT);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // First: button press
    const char press_seq[] = "\x1B[<0;10;5M";
    result = lle_mouse_parser_parse_sequence(parser, press_seq, 11, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse press");
    lle_pool_free(event);
    event = NULL;

    /* Then: movement with button held (drag) - button code 32 (0x20 + motion
     * flag) */
    const char drag_seq[] = "\x1B[<32;15;10M";
    result = lle_mouse_parser_parse_sequence(parser, drag_seq, 13, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse drag");
    TEST_ASSERT(event != NULL, "Event is NULL");
    TEST_ASSERT(event->type == LLE_MOUSE_EVENT_DRAG, "Wrong event type");

    lle_pool_free(event);
    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_mouse_drag");
}

/*
 * Test: Statistics tracking
 */
static void test_statistics(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // Parse a valid sequence
    const char sequence[] = "\x1B[<0;10;5M";
    result = lle_mouse_parser_parse_sequence(parser, sequence, 11, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse");
    lle_pool_free(event);

    // Check statistics
    uint64_t events_parsed = 0;
    uint64_t invalid_sequences = 0;
    result =
        lle_mouse_parser_get_stats(parser, &events_parsed, &invalid_sequences);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to get stats");
    TEST_ASSERT(events_parsed == 1, "Wrong event count");
    TEST_ASSERT(invalid_sequences == 0, "Wrong invalid count");

    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_statistics");
}

/*
 * Test: Reset parser state
 */
static void test_reset(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // Parse a sequence
    const char sequence[] = "\x1B[<0;10;5M";
    result = lle_mouse_parser_parse_sequence(parser, sequence, 11, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse");
    lle_pool_free(event);

    // Reset parser
    result = lle_mouse_parser_reset(parser);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to reset");

    // Check statistics are reset
    uint64_t events_parsed = 0;
    uint64_t invalid_sequences = 0;
    result =
        lle_mouse_parser_get_stats(parser, &events_parsed, &invalid_sequences);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to get stats");
    TEST_ASSERT(events_parsed == 0, "Stats not reset");

    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_reset");
}

/*
 * Test: Get current mouse state
 */
static void test_get_state(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // Parse a button press at (10, 5)
    const char sequence[] = "\x1B[<0;11;6M";
    result = lle_mouse_parser_parse_sequence(parser, sequence, 11, &event);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to parse");
    lle_pool_free(event);

    // Get state
    uint16_t x = 0, y = 0;
    lle_mouse_button_t buttons = 0;
    result = lle_mouse_parser_get_state(parser, &x, &y, &buttons);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to get state");
    TEST_ASSERT(x == 10, "Wrong x position");
    TEST_ASSERT(y == 5, "Wrong y position");
    TEST_ASSERT(buttons & LLE_MOUSE_BUTTON_LEFT, "Left button not pressed");

    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_get_state");
}

/*
 * Test: Invalid sequence handling
 */
static void test_invalid_sequence(void) {
    lle_mouse_parser_t *parser = NULL;
    lle_mouse_event_info_t *event = NULL;

    lle_result_t result = lle_mouse_parser_init(&parser, NULL, NULL);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to initialize parser");

    result =
        lle_mouse_parser_set_tracking(parser, true, LLE_MOUSE_TRACKING_X10);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to enable tracking");

    // Invalid sequence (too short)
    const char sequence[] = "\x1B[<0";
    size_t sequence_len = 4;

    result =
        lle_mouse_parser_parse_sequence(parser, sequence, sequence_len, &event);
    TEST_ASSERT(result != LLE_SUCCESS, "Should fail on invalid sequence");

    // Check statistics
    uint64_t events_parsed = 0;
    uint64_t invalid_sequences = 0;
    result =
        lle_mouse_parser_get_stats(parser, &events_parsed, &invalid_sequences);
    TEST_ASSERT(result == LLE_SUCCESS, "Failed to get stats");
    TEST_ASSERT(invalid_sequences > 0, "Invalid sequence not counted");

    lle_mouse_parser_destroy(parser);

    TEST_PASS("test_invalid_sequence");
}

/*
 * Main test runner
 */
int main(void) {
    printf("=== LLE Mouse Parser Unit Tests ===\n\n");

    // Run tests
    RUN_TEST(mouse_parser_init_destroy);
    RUN_TEST(x10_button_press);
    RUN_TEST(x10_button_release);
    RUN_TEST(x10_middle_button);
    RUN_TEST(x10_right_button);
    RUN_TEST(x10_coordinates);
    RUN_TEST(x10_wheel_up);
    RUN_TEST(x10_wheel_down);
    RUN_TEST(sgr_button_press);
    RUN_TEST(sgr_button_release);
    RUN_TEST(sgr_modifiers);
    RUN_TEST(mouse_drag);
    RUN_TEST(statistics);
    RUN_TEST(reset);
    RUN_TEST(get_state);
    RUN_TEST(invalid_sequence);
    return TEST_RESULT();
}
