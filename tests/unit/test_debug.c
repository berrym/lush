/**
 * @file test_debug.c
 * @brief Unit tests for shell debugger subsystem
 *
 * Tests the debugging infrastructure including:
 * - Debug context lifecycle
 * - Breakpoint management
 * - Stack frame operations
 * - Execution tracing
 * - Performance profiling
 * - Script analysis
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "node.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * DEBUG CONTEXT LIFECYCLE TESTS
 * ============================================================================
 */

TEST(debug_init_creates_context) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should return non-NULL");

    /// Verify initial state
    ASSERT_EQ(ctx->level, DEBUG_NONE, "Initial level should be DEBUG_NONE");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_NORMAL, "Initial mode should be NORMAL");
    ASSERT_FALSE(ctx->enabled, "Debug should not be enabled initially");
    ASSERT_NULL(ctx->breakpoints, "No breakpoints initially");
    ASSERT_NULL(ctx->current_frame, "No current frame initially");
    ASSERT_EQ(ctx->stack_depth, 0, "Stack depth should be 0");

    debug_cleanup(ctx);
}

TEST(debug_cleanup_handles_null) {
    /// Should not crash
    debug_cleanup(NULL);
}

TEST(debug_set_level) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

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

TEST(debug_set_mode) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_set_mode(ctx, DEBUG_MODE_STEP);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP, "Mode should be STEP");

    debug_set_mode(ctx, DEBUG_MODE_STEP_OVER);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER, "Mode should be STEP_OVER");

    debug_set_mode(ctx, DEBUG_MODE_CONTINUE);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE, "Mode should be CONTINUE");

    debug_cleanup(ctx);
}

TEST(debug_enable_disable) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ASSERT_FALSE(ctx->enabled, "Initially disabled");

    debug_enable(ctx, true);
    ASSERT_TRUE(ctx->enabled, "Should be enabled after enable(true)");

    debug_enable(ctx, false);
    ASSERT_FALSE(ctx->enabled, "Should be disabled after enable(false)");

    debug_cleanup(ctx);
}

/* ============================================================================
 * BREAKPOINT MANAGEMENT TESTS
 * ============================================================================
 */

TEST(breakpoint_add_simple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id >= 0, "Should return valid breakpoint ID");
    ASSERT_NOT_NULL(ctx->breakpoints, "Breakpoint list should not be empty");
    ASSERT_EQ(ctx->breakpoints->line, 10, "Line should be 10");
    ASSERT_TRUE(ctx->breakpoints->enabled, "Breakpoint should be enabled");

    debug_cleanup(ctx);
}

TEST(breakpoint_add_with_condition) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 20, "$x -gt 5");
    ASSERT(id >= 0, "Should return valid breakpoint ID");
    ASSERT_NOT_NULL(ctx->breakpoints->condition, "Condition should be set");

    debug_cleanup(ctx);
}

TEST(breakpoint_add_multiple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int id1 = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "test.sh", 20, NULL);
    int id3 = debug_add_breakpoint(ctx, "other.sh", 5, NULL);

    ASSERT(id1 >= 0 && id2 >= 0 && id3 >= 0, "All IDs should be valid");
    ASSERT(id1 != id2 && id2 != id3 && id1 != id3, "IDs should be unique");

    debug_cleanup(ctx);
}

TEST(breakpoint_remove) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id >= 0, "Add should succeed");

    bool removed = debug_remove_breakpoint(ctx, id);
    ASSERT_TRUE(removed, "Remove should succeed");
    ASSERT_NULL(ctx->breakpoints, "Breakpoint list should be empty");

    /// Try to remove non-existent
    removed = debug_remove_breakpoint(ctx, id);
    ASSERT_FALSE(removed, "Remove non-existent should return false");

    debug_cleanup(ctx);
}

TEST(breakpoint_enable_disable) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id >= 0, "Add should succeed");
    ASSERT_TRUE(ctx->breakpoints->enabled, "Initially enabled");

    bool result = debug_enable_breakpoint(ctx, id, false);
    ASSERT_TRUE(result, "Disable should succeed");
    ASSERT_FALSE(ctx->breakpoints->enabled, "Should be disabled");

    result = debug_enable_breakpoint(ctx, id, true);
    ASSERT_TRUE(result, "Enable should succeed");
    ASSERT_TRUE(ctx->breakpoints->enabled, "Should be enabled");

    /// Non-existent ID
    result = debug_enable_breakpoint(ctx, 999, true);
    ASSERT_FALSE(result, "Should fail for non-existent ID");

    debug_cleanup(ctx);
}

TEST(breakpoint_check_hit) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true);
    /* debug_check_breakpoint reports through debug_output and, on a
     * hit, enters the (no-terminal, immediately-returning) debug
     * prompt; route its chatter to /dev/null. */
    ctx->debug_output = fopen("/dev/null", "w");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id >= 0, "Add should succeed");

    /* A matching, enabled breakpoint is a hit: debug_check_breakpoint
     * returns true and increments the hit count. This exercises the
     * real function -- not a hand-walk of the breakpoint list. */
    bool hit = debug_check_breakpoint(ctx, "test.sh", 10);
    ASSERT_TRUE(hit, "Matching breakpoint should report a hit");
    ASSERT_EQ(ctx->breakpoints->hit_count, 1, "Hit count should be 1");

    /// No breakpoint at line 11 -> no hit.
    bool miss = debug_check_breakpoint(ctx, "test.sh", 11);
    ASSERT_FALSE(miss, "No breakpoint at test.sh:11 should not hit");

    /// Same line number, different file -> no hit.
    bool other_file = debug_check_breakpoint(ctx, "other.sh", 10);
    ASSERT_FALSE(other_file, "Breakpoint is file-specific");

    ASSERT_EQ(ctx->breakpoints->hit_count, 1, "Misses do not raise hit count");

    debug_cleanup(ctx);
}

TEST(breakpoint_check_disabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true);
    ctx->debug_output = fopen("/dev/null", "w");

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    debug_enable_breakpoint(ctx, id, false);

    /// A disabled breakpoint is not a hit even at its own location.
    bool hit = debug_check_breakpoint(ctx, "test.sh", 10);
    ASSERT_FALSE(hit, "Disabled breakpoint should not report a hit");
    ASSERT_EQ(ctx->breakpoints->hit_count, 0,
              "Disabled breakpoint hit count stays 0");

    debug_cleanup(ctx);
}

TEST(breakpoint_clear_all) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    debug_add_breakpoint(ctx, "test.sh", 20, NULL);
    debug_add_breakpoint(ctx, "other.sh", 5, NULL);
    ASSERT_NOT_NULL(ctx->breakpoints, "Should have breakpoints");

    debug_clear_breakpoints(ctx);
    ASSERT_NULL(ctx->breakpoints, "All breakpoints should be cleared");

    debug_cleanup(ctx);
}

/* ============================================================================
 * STACK FRAME TESTS
 * ============================================================================
 */

TEST(stack_frame_push_pop) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ASSERT_EQ(ctx->stack_depth, 0, "Initial depth should be 0");
    ASSERT_NULL(ctx->current_frame, "No frame initially");

    debug_frame_t *frame1 = debug_push_frame(ctx, "main", "script.sh", 1);
    ASSERT_NOT_NULL(frame1, "push_frame should succeed");
    ASSERT_EQ(ctx->stack_depth, 1, "Depth should be 1");
    ASSERT(ctx->current_frame == frame1, "Current frame should be frame1");

    debug_frame_t *frame2 = debug_push_frame(ctx, "helper", "script.sh", 10);
    ASSERT_NOT_NULL(frame2, "Second push should succeed");
    ASSERT_EQ(ctx->stack_depth, 2, "Depth should be 2");
    ASSERT(ctx->current_frame == frame2, "Current frame should be frame2");
    ASSERT(frame2->parent == frame1, "frame2 parent should be frame1");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 1, "Depth should be 1 after pop");
    ASSERT(ctx->current_frame == frame1, "Current should be frame1");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Depth should be 0");
    ASSERT_NULL(ctx->current_frame, "No current frame");

    debug_cleanup(ctx);
}

TEST(stack_frame_pop_empty) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should not crash when popping empty stack
    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Depth should still be 0");

    debug_cleanup(ctx);
}

TEST(stack_frame_update_node) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_frame_t *frame = debug_push_frame(ctx, "main", "script.sh", 1);
    ASSERT_NOT_NULL(frame, "push_frame should succeed");
    ASSERT_NULL(frame->current_node, "No node initially");

    node_t *node = new_node(NODE_COMMAND);
    debug_update_frame_node(ctx, node);
    ASSERT(frame->current_node == node, "Node should be updated");

    debug_pop_frame(ctx);
    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(stack_frame_deep_nesting) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Push 10 frames
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "func_%d", i);
        debug_frame_t *frame = debug_push_frame(ctx, name, "script.sh", i * 10);
        ASSERT_NOT_NULL(frame, "push_frame should succeed");
    }
    ASSERT_EQ(ctx->stack_depth, 10, "Depth should be 10");

    /// Pop all frames
    for (int i = 0; i < 10; i++) {
        debug_pop_frame(ctx);
    }
    ASSERT_EQ(ctx->stack_depth, 0, "Depth should be 0");

    debug_cleanup(ctx);
}

/* ============================================================================
 * PROFILING TESTS
 * ============================================================================
 */

TEST(profile_start_stop) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ASSERT_FALSE(ctx->profile_enabled, "Profiling initially disabled");

    debug_profile_start(ctx);
    ASSERT_TRUE(ctx->profile_enabled, "Profiling should be enabled");

    debug_profile_stop(ctx);
    ASSERT_FALSE(ctx->profile_enabled, "Profiling should be disabled");

    debug_cleanup(ctx);
}

TEST(profile_function_tracking) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_profile_start(ctx);

    debug_profile_function_enter(ctx, "test_func");
    /// Simulate some work
    debug_profile_function_exit(ctx, "test_func");

    ASSERT_NOT_NULL(ctx->profile_data, "Profile data should exist");

    debug_profile_stop(ctx);
    debug_cleanup(ctx);
}

TEST(profile_reset) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_profile_start(ctx);
    debug_profile_function_enter(ctx, "test_func");
    debug_profile_function_exit(ctx, "test_func");

    ASSERT_NOT_NULL(ctx->profile_data, "Profile data should exist");

    debug_profile_reset(ctx);
    ASSERT_NULL(ctx->profile_data, "Profile data should be cleared");

    debug_profile_stop(ctx);
    debug_cleanup(ctx);
}

TEST(profile_multiple_calls) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_profile_start(ctx);

    /// Call same function multiple times - need a frame for profiling to work
    for (int i = 0; i < 5; i++) {
        debug_push_frame(ctx, "repeated_func", "test.sh", 100 + i);
        debug_profile_function_enter(ctx, "repeated_func");
        debug_profile_function_exit(ctx, "repeated_func");
        debug_pop_frame(ctx);
    }

    /// Profile data should exist and have correct call count
    ASSERT_NOT_NULL(ctx->profile_data, "Profile data should exist");

    /// Find the entry for repeated_func
    profile_data_t *profile = ctx->profile_data;
    bool found = false;
    while (profile) {
        if (strcmp(profile->function_name, "repeated_func") == 0) {
            found = true;
            ASSERT_EQ(profile->call_count, 5, "Call count should be 5");
            break;
        }
        profile = profile->next;
    }
    ASSERT_TRUE(found, "Profile entry for repeated_func should exist");

    debug_profile_stop(ctx);
    debug_cleanup(ctx);
}

/* ============================================================================
 * ANALYSIS TESTS
 * ============================================================================
 */

TEST(analysis_add_issue) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ASSERT_EQ(ctx->issue_count, 0, "No issues initially");

    debug_add_analysis_issue(ctx, "test.sh", 10, "warning", "style",
                             "Line too long",
                             "Consider breaking into multiple lines");

    ASSERT_EQ(ctx->issue_count, 1, "Should have 1 issue");
    ASSERT_NOT_NULL(ctx->analysis_issues, "Issue list should not be empty");

    debug_cleanup(ctx);
}

TEST(analysis_multiple_issues) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 10, "warning", "style",
                             "Line too long", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 20, "error", "syntax",
                             "Missing semicolon", "Add ; after command");
    debug_add_analysis_issue(ctx, "test.sh", 30, "info", "performance",
                             "Suboptimal loop", NULL);

    ASSERT_EQ(ctx->issue_count, 3, "Should have 3 issues");

    debug_cleanup(ctx);
}

TEST(analysis_clear_issues) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 10, "warning", "style",
                             "Test issue", NULL);
    ASSERT_EQ(ctx->issue_count, 1, "Should have 1 issue");

    debug_clear_analysis_issues(ctx);
    ASSERT_EQ(ctx->issue_count, 0, "Should have 0 issues");
    ASSERT_NULL(ctx->analysis_issues, "Issue list should be empty");

    debug_cleanup(ctx);
}

/* ============================================================================
 * UTILITY FUNCTION TESTS
 * ============================================================================
 */

TEST(utility_get_time_ns) {
    long time1 = debug_get_time_ns();
    ASSERT(time1 > 0, "Time should be positive");

    /// Small delay
    for (volatile int i = 0; i < 100000; i++)
        ;

    long time2 = debug_get_time_ns();
    ASSERT(time2 >= time1, "Time should not go backwards");
}

TEST(utility_format_time) {
    char buffer[64];

    debug_format_time(1000, buffer, sizeof(buffer));
    ASSERT(strlen(buffer) > 0, "Should format time");

    debug_format_time(1000000, buffer, sizeof(buffer));
    ASSERT(strlen(buffer) > 0, "Should format microseconds");

    debug_format_time(1000000000, buffer, sizeof(buffer));
    ASSERT(strlen(buffer) > 0, "Should format milliseconds");
}

TEST(utility_get_node_description) {
    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    char *desc = debug_get_node_description(node);
    ASSERT_NOT_NULL(desc, "Should return description");
    ASSERT(strlen(desc) > 0, "Description should not be empty");

    free(desc);
    free_node_tree(node);
}

TEST(utility_node_description_various_types) {
    node_type_t types[] = {NODE_COMMAND,  NODE_PIPELINE, NODE_IF,
                           NODE_FOR,      NODE_WHILE,    NODE_CASE,
                           NODE_FUNCTION, NODE_VAR,      NODE_SUBSHELL};
    int num_types = 9;

    for (int i = 0; i < num_types; i++) {
        node_t *node = new_node(types[i]);
        ASSERT_NOT_NULL(node, "new_node should succeed");

        char *desc = debug_get_node_description(node);
        ASSERT_NOT_NULL(desc, "Should return description");

        free(desc);
        free_node_tree(node);
    }
}

/* ============================================================================
 * LOOP CONTEXT TESTS
 * ============================================================================
 */

TEST(loop_context_enter_exit) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true); /// Enable debug mode for loop tracking

    ASSERT_FALSE(ctx->execution_context.in_loop, "Not in loop initially");

    debug_enter_loop(ctx, "for", "i", "1");
    ASSERT_TRUE(ctx->execution_context.in_loop, "Should be in loop");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0,
              "Iteration should be 0");

    debug_exit_loop(ctx);
    ASSERT_FALSE(ctx->execution_context.in_loop, "Should not be in loop");

    debug_cleanup(ctx);
}

TEST(loop_context_update_variable) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true); /// Enable debug mode for loop tracking

    debug_enter_loop(ctx, "for", "i", "1");

    debug_update_loop_variable(ctx, "i", "2");
    /// Value should be updated - exact check depends on implementation

    debug_exit_loop(ctx);
    debug_cleanup(ctx);
}

/* ============================================================================
 * OUTPUT CONTROL TESTS
 * ============================================================================
 */

TEST(output_null_context_safe) {
    /// These should not crash with NULL context
    debug_printf(NULL, "test %d", 123);
    debug_print_separator(NULL);
    debug_print_header(NULL, "Test");
}

/* ============================================================================
 * STEP EXECUTION TESTS
 * ============================================================================
 */

TEST(step_mode_transitions) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true); /// Enable debug mode for step operations

    debug_step_into(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP, "Should be in step mode");

    debug_step_over(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER, "Should be in step-over mode");

    debug_continue(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE, "Should be in continue mode");

    debug_cleanup(ctx);
}

/* ============================================================================
 * EXECUTION CONTEXT PRESERVATION TESTS
 * ============================================================================
 */

TEST(execution_context_cleanup) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");
    debug_enable(ctx, true); /// Enable debug mode for execution context

    debug_enter_loop(ctx, "for", "i", "1");
    ASSERT_TRUE(ctx->execution_context.in_loop, "Should be in loop");

    debug_cleanup_execution_context(ctx);
    ASSERT_FALSE(ctx->execution_context.in_loop, "Context should be cleared");

    debug_cleanup(ctx);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n=== Debug Subsystem Unit Tests ===\n\n");

    /// Lifecycle tests
    printf("Debug Context Lifecycle:\n");
    RUN_TEST(debug_init_creates_context);
    RUN_TEST(debug_cleanup_handles_null);
    RUN_TEST(debug_set_level);
    RUN_TEST(debug_set_mode);
    RUN_TEST(debug_enable_disable);

    /// Breakpoint tests
    printf("\nBreakpoint Management:\n");
    RUN_TEST(breakpoint_add_simple);
    RUN_TEST(breakpoint_add_with_condition);
    RUN_TEST(breakpoint_add_multiple);
    RUN_TEST(breakpoint_remove);
    RUN_TEST(breakpoint_enable_disable);
    RUN_TEST(breakpoint_check_hit);
    RUN_TEST(breakpoint_check_disabled);
    RUN_TEST(breakpoint_clear_all);

    /// Stack frame tests
    printf("\nStack Frame Management:\n");
    RUN_TEST(stack_frame_push_pop);
    RUN_TEST(stack_frame_pop_empty);
    RUN_TEST(stack_frame_update_node);
    RUN_TEST(stack_frame_deep_nesting);

    /// Profiling tests
    printf("\nProfiling:\n");
    RUN_TEST(profile_start_stop);
    RUN_TEST(profile_function_tracking);
    RUN_TEST(profile_reset);
    RUN_TEST(profile_multiple_calls);

    /// Analysis tests
    printf("\nScript Analysis:\n");
    RUN_TEST(analysis_add_issue);
    RUN_TEST(analysis_multiple_issues);
    RUN_TEST(analysis_clear_issues);

    /// Utility tests
    printf("\nUtility Functions:\n");
    RUN_TEST(utility_get_time_ns);
    RUN_TEST(utility_format_time);
    RUN_TEST(utility_get_node_description);
    RUN_TEST(utility_node_description_various_types);

    /// Loop context tests
    printf("\nLoop Context:\n");
    RUN_TEST(loop_context_enter_exit);
    RUN_TEST(loop_context_update_variable);

    /// Output control tests
    printf("\nOutput Control:\n");
    RUN_TEST(output_null_context_safe);

    /// Step execution tests
    printf("\nStep Execution:\n");
    RUN_TEST(step_mode_transitions);

    /// Execution context tests
    printf("\nExecution Context:\n");
    RUN_TEST(execution_context_cleanup);

    return TEST_RESULT();
}
