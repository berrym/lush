/**
 * @file test_debug_core.c
 * @brief Unit tests for debug core module
 *
 * Tests the debug core functionality including:
 * - Debug context management
 * - Profiling support
 * - Debug state tracking
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */




/* ============================================================================
 * DEBUG CONTEXT TESTS
 * ============================================================================
 */

TEST(debug_init_creates_context) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should return non-NULL");
    debug_cleanup(ctx);
}

TEST(debug_cleanup_null) {
    /* Should not crash */
    debug_cleanup(NULL);
}

TEST(debug_context_enabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    /* Initially should be disabled */
    ASSERT(!ctx->enabled, "Debug should be disabled initially");

    debug_enable(ctx, true);
    ASSERT(ctx->enabled, "Debug should be enabled after enable(true)");

    debug_enable(ctx, false);
    ASSERT(!ctx->enabled, "Debug should be disabled after enable(false)");

    debug_cleanup(ctx);
}

TEST(debug_context_mode) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    debug_enable(ctx, true);

    debug_set_mode(ctx, DEBUG_MODE_STEP);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP, "Mode should be STEP");

    debug_set_mode(ctx, DEBUG_MODE_STEP_OVER);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER, "Mode should be STEP_OVER");

    debug_set_mode(ctx, DEBUG_MODE_CONTINUE);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE, "Mode should be CONTINUE");

    debug_set_mode(ctx, DEBUG_MODE_NORMAL);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_NORMAL, "Mode should be NORMAL");

    debug_cleanup(ctx);
}

TEST(debug_context_level) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    debug_set_level(ctx, DEBUG_NONE);
    ASSERT_EQ(ctx->level, DEBUG_NONE, "Level should be NONE");

    debug_set_level(ctx, DEBUG_BASIC);
    ASSERT_EQ(ctx->level, DEBUG_BASIC, "Level should be BASIC");

    debug_set_level(ctx, DEBUG_VERBOSE);
    ASSERT_EQ(ctx->level, DEBUG_VERBOSE, "Level should be VERBOSE");

    debug_set_level(ctx, DEBUG_TRACE);
    ASSERT_EQ(ctx->level, DEBUG_TRACE, "Level should be TRACE");

    debug_set_level(ctx, DEBUG_PROFILE);
    ASSERT_EQ(ctx->level, DEBUG_PROFILE, "Level should be PROFILE");

    debug_cleanup(ctx);
}

/* ============================================================================
 * PROFILING TESTS
 * ============================================================================
 */

TEST(profiling_disabled_initially) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    ASSERT(!ctx->profile_enabled, "Profiling should be disabled initially");

    debug_cleanup(ctx);
}

TEST(profiling_start_stop) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    debug_profile_start(ctx);
    ASSERT(ctx->profile_enabled, "Profiling should be enabled after start");

    debug_profile_stop(ctx);
    ASSERT(!ctx->profile_enabled, "Profiling should be disabled after stop");

    debug_cleanup(ctx);
}

/* ============================================================================
 * STACK FRAME TESTS
 * ============================================================================
 */

TEST(stack_frame_push_pop) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");
    debug_enable(ctx, true);

    ASSERT_EQ(ctx->stack_depth, 0, "Initial stack depth should be 0");

    debug_push_frame(ctx, "function1", "file.sh", 10);
    ASSERT_EQ(ctx->stack_depth, 1, "Stack depth should be 1 after push");

    debug_push_frame(ctx, "function2", "file.sh", 20);
    ASSERT_EQ(ctx->stack_depth, 2, "Stack depth should be 2 after second push");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 1, "Stack depth should be 1 after pop");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Stack depth should be 0 after second pop");

    debug_cleanup(ctx);
}

TEST(stack_frame_current) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");
    debug_enable(ctx, true);

    debug_push_frame(ctx, "myfunc", "script.sh", 42);

    ASSERT_NOT_NULL(ctx->current_frame, "Current frame should not be NULL");

    debug_cleanup(ctx);
}

/* ============================================================================
 * BREAKPOINT TESTS
 * ============================================================================
 */

TEST(breakpoint_add_remove) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id >= 0, "Breakpoint ID should be non-negative");

    bool removed = debug_remove_breakpoint(ctx, id);
    ASSERT(removed, "Breakpoint should be removed successfully");

    /* Try to remove again - should fail */
    removed = debug_remove_breakpoint(ctx, id);
    ASSERT(!removed, "Removing non-existent breakpoint should fail");

    debug_cleanup(ctx);
}

TEST(breakpoint_enable_disable) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);

    bool result = debug_enable_breakpoint(ctx, id, false);
    ASSERT(result, "Disabling breakpoint should succeed");

    result = debug_enable_breakpoint(ctx, id, true);
    ASSERT(result, "Enabling breakpoint should succeed");

    /* Non-existent breakpoint */
    result = debug_enable_breakpoint(ctx, 9999, true);
    ASSERT(!result, "Enabling non-existent breakpoint should fail");

    debug_cleanup(ctx);
}

TEST(breakpoint_clear_all) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "Context creation should succeed");

    debug_add_breakpoint(ctx, "test1.sh", 10, NULL);
    debug_add_breakpoint(ctx, "test2.sh", 20, NULL);
    debug_add_breakpoint(ctx, "test3.sh", 30, NULL);

    debug_clear_breakpoints(ctx);

    /* After clear, breakpoints list should be NULL */
    ASSERT(ctx->breakpoints == NULL, "Breakpoints should be cleared");

    debug_cleanup(ctx);
}

/* ============================================================================
 * UTILITY TESTS
 * ============================================================================
 */

TEST(debug_get_time_ns) {
    long t1 = debug_get_time_ns();
    ASSERT(t1 > 0, "Time should be positive");

    /* Wait a tiny bit */
    for (volatile int i = 0; i < 10000; i++)
        ;

    long t2 = debug_get_time_ns();
    ASSERT(t2 >= t1, "Time should be monotonic");
}

TEST(debug_format_time) {
    char buffer[64];
    debug_format_time(1000000000L, buffer, sizeof(buffer)); /* 1 second */
    ASSERT(buffer[0] != '\0', "Buffer should not be empty");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running debug_core.c tests...\n\n");

    printf("Debug Context Tests:\n");
    RUN_TEST(debug_init_creates_context);
    RUN_TEST(debug_cleanup_null);
    RUN_TEST(debug_context_enabled);
    RUN_TEST(debug_context_mode);
    RUN_TEST(debug_context_level);

    printf("\nProfiling Tests:\n");
    RUN_TEST(profiling_disabled_initially);
    RUN_TEST(profiling_start_stop);

    printf("\nStack Frame Tests:\n");
    RUN_TEST(stack_frame_push_pop);
    RUN_TEST(stack_frame_current);

    printf("\nBreakpoint Tests:\n");
    RUN_TEST(breakpoint_add_remove);
    RUN_TEST(breakpoint_enable_disable);
    RUN_TEST(breakpoint_clear_all);

    printf("\nUtility Tests:\n");
    RUN_TEST(debug_get_time_ns);
    RUN_TEST(debug_format_time);

    return TEST_RESULT();
}
