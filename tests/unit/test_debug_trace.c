/**
 * @file test_debug_trace.c
 * @brief Unit tests for debug trace module
 *
 * Tests execution tracing functionality including node tracing,
 * command tracing, builtin tracing, function call tracing,
 * stack frame management, and variable inspection.
 */

#include "debug.h"
#include "node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// Test Framework
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        tests_run++;                                                           \
        printf("  Running %s...", #name);                                      \
        fflush(stdout);                                                        \
        test_##name();                                                         \
        printf(" PASSED\n");                                                   \
        tests_passed++;                                                        \
    } while (0)

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf(" FAILED: %s\n", msg);                                      \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)
#define ASSERT_NULL(ptr, msg) ASSERT((ptr) == NULL, msg)
#define ASSERT_TRUE(val, msg) ASSERT((val) == true, msg)
#define ASSERT_FALSE(val, msg) ASSERT((val) == false, msg)
#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_GE(a, b, msg) ASSERT((a) >= (b), msg)
#define ASSERT_LE(a, b, msg) ASSERT((a) <= (b), msg)

// ============================================================================
// Node Tracing Tests
// ============================================================================

TEST(trace_node_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    // Should not crash with NULL parameters
    debug_trace_node(NULL, NULL, NULL, 0);
    debug_trace_node(ctx, NULL, "test.sh", 1);
    debug_trace_node(ctx, NULL, NULL, 1);

    debug_cleanup(ctx);
}

TEST(trace_node_disabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    // Debug disabled - should do nothing
    ctx->enabled = false;
    ctx->trace_execution = true;

    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    long count_before = ctx->total_commands;
    debug_trace_node(ctx, node, "test.sh", 1);
    // Should not increment when disabled
    ASSERT_EQ(ctx->total_commands, count_before, "Command count unchanged");

    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(trace_node_enabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    long count_before = ctx->total_commands;
    debug_trace_node(ctx, node, "test.sh", 10);
    ASSERT_EQ(ctx->total_commands, count_before + 1,
              "Command count incremented");

    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(trace_node_with_timing) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;
    ctx->show_timing = true;

    node_t *node = new_node(NODE_PIPELINE);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    // Should not crash with timing enabled
    debug_trace_node(ctx, node, "script.sh", 5);

    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(trace_node_multiple_types) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    // Test various node types
    node_type_t types[] = {NODE_COMMAND, NODE_PIPELINE, NODE_SUBSHELL,
                           NODE_FOR,     NODE_WHILE,    NODE_IF,
                           NODE_CASE,    NODE_FUNCTION};

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        node_t *node = new_node(types[i]);
        ASSERT_NOT_NULL(node, "new_node should succeed");

        debug_trace_node(ctx, node, "multi.sh", (int)(i + 1));

        free_node_tree(node);
    }

    ASSERT_EQ(ctx->total_commands, 8, "Should have 8 traced commands");

    debug_cleanup(ctx);
}

// ============================================================================
// Command Tracing Tests
// ============================================================================

TEST(trace_command_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    // Should not crash with NULL
    debug_trace_command(NULL, "ls", NULL, 0);
    debug_trace_command(ctx, NULL, NULL, 0);

    debug_cleanup(ctx);
}

TEST(trace_command_simple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"ls", NULL};
    debug_trace_command(ctx, "ls", argv, 1);

    debug_cleanup(ctx);
}

TEST(trace_command_with_args) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"grep", "-r", "pattern", "dir/", NULL};
    debug_trace_command(ctx, "grep", argv, 4);

    debug_cleanup(ctx);
}

TEST(trace_command_disabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ctx->enabled = false;
    ctx->trace_execution = true;

    char *argv[] = {"echo", "hello", NULL};
    // Should do nothing when disabled
    debug_trace_command(ctx, "echo", argv, 2);

    debug_cleanup(ctx);
}

// ============================================================================
// Builtin Tracing Tests
// ============================================================================

TEST(trace_builtin_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    // Should not crash
    debug_trace_builtin(NULL, "cd", NULL, 0);
    debug_trace_builtin(ctx, NULL, NULL, 0);

    debug_cleanup(ctx);
}

TEST(trace_builtin_simple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"cd", NULL};
    debug_trace_builtin(ctx, "cd", argv, 1);

    debug_cleanup(ctx);
}

TEST(trace_builtin_with_args) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"export", "PATH=/usr/bin", "HOME=/home/user", NULL};
    debug_trace_builtin(ctx, "export", argv, 3);

    debug_cleanup(ctx);
}

// ============================================================================
// Function Call Tracing Tests
// ============================================================================

TEST(trace_function_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    // Should not crash
    debug_trace_function_call(NULL, "myfunc", NULL, 0);
    debug_trace_function_call(ctx, NULL, NULL, 0);

    debug_cleanup(ctx);
}

TEST(trace_function_simple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"myfunc", NULL};
    debug_trace_function_call(ctx, "myfunc", argv, 1);

    debug_cleanup(ctx);
}

TEST(trace_function_with_args) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    char *argv[] = {"process_file", "input.txt", "output.txt", NULL};
    debug_trace_function_call(ctx, "process_file", argv, 3);

    debug_cleanup(ctx);
}

// ============================================================================
// Stack Frame Management Tests
// ============================================================================

TEST(push_frame_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    // Should return NULL for NULL ctx
    debug_frame_t *frame = debug_push_frame(NULL, "func", "file.sh", 1);
    ASSERT_NULL(frame, "Should return NULL for NULL ctx");

    // Should return NULL for NULL function
    frame = debug_push_frame(ctx, NULL, "file.sh", 1);
    ASSERT_NULL(frame, "Should return NULL for NULL function");

    debug_cleanup(ctx);
}

TEST(push_frame_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    ASSERT_EQ(ctx->stack_depth, 0, "Initial stack depth should be 0");
    ASSERT_NULL(ctx->current_frame, "Initial frame should be NULL");

    debug_frame_t *frame = debug_push_frame(ctx, "main", "script.sh", 1);
    ASSERT_NOT_NULL(frame, "push_frame should succeed");

    ASSERT_EQ(ctx->stack_depth, 1, "Stack depth should be 1");
    ASSERT_EQ(ctx->current_frame, frame, "Current frame should match");
    ASSERT_STR_EQ(frame->function_name, "main", "Function name");
    ASSERT_STR_EQ(frame->file_path, "script.sh", "File path");
    ASSERT_EQ(frame->line_number, 1, "Line number");
    ASSERT_NULL(frame->parent, "Parent should be NULL");

    debug_cleanup(ctx);
}

TEST(push_frame_nested) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_frame_t *frame1 = debug_push_frame(ctx, "outer", "script.sh", 10);
    ASSERT_NOT_NULL(frame1, "First push should succeed");
    ASSERT_EQ(ctx->stack_depth, 1, "Stack depth 1");

    debug_frame_t *frame2 = debug_push_frame(ctx, "inner", "script.sh", 20);
    ASSERT_NOT_NULL(frame2, "Second push should succeed");
    ASSERT_EQ(ctx->stack_depth, 2, "Stack depth 2");
    ASSERT_EQ(frame2->parent, frame1, "Parent should be first frame");

    debug_frame_t *frame3 = debug_push_frame(ctx, "deepest", "script.sh", 30);
    ASSERT_NOT_NULL(frame3, "Third push should succeed");
    ASSERT_EQ(ctx->stack_depth, 3, "Stack depth 3");
    ASSERT_EQ(frame3->parent, frame2, "Parent should be second frame");

    ASSERT_EQ(ctx->current_frame, frame3, "Current frame is deepest");

    debug_cleanup(ctx);
}

TEST(push_frame_max_depth) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    // Set a low max stack depth for testing
    ctx->max_stack_depth = 3;

    debug_push_frame(ctx, "level1", "test.sh", 1);
    debug_push_frame(ctx, "level2", "test.sh", 2);
    debug_push_frame(ctx, "level3", "test.sh", 3);

    ASSERT_EQ(ctx->stack_depth, 3, "Should be at max depth");

    // Should return NULL when at max depth
    debug_frame_t *frame = debug_push_frame(ctx, "level4", "test.sh", 4);
    ASSERT_NULL(frame, "Should return NULL at max depth");
    ASSERT_EQ(ctx->stack_depth, 3, "Depth unchanged");

    debug_cleanup(ctx);
}

TEST(push_frame_null_file) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_frame_t *frame = debug_push_frame(ctx, "func", NULL, 1);
    ASSERT_NOT_NULL(frame, "Should succeed with NULL file");
    ASSERT_NULL(frame->file_path, "File path should be NULL");
    ASSERT_STR_EQ(frame->function_name, "func", "Function name set");

    debug_cleanup(ctx);
}

TEST(pop_frame_null_ctx) {
    // Should not crash
    debug_pop_frame(NULL);
}

TEST(pop_frame_empty_stack) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    // Should not crash on empty stack
    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Depth still 0");

    debug_cleanup(ctx);
}

TEST(pop_frame_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_push_frame(ctx, "func", "test.sh", 1);
    ASSERT_EQ(ctx->stack_depth, 1, "Depth is 1");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Depth back to 0");
    ASSERT_NULL(ctx->current_frame, "No current frame");

    debug_cleanup(ctx);
}

TEST(pop_frame_nested) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_frame_t *frame1 = debug_push_frame(ctx, "outer", "test.sh", 1);
    debug_frame_t *frame2 = debug_push_frame(ctx, "inner", "test.sh", 2);
    (void)frame2;

    ASSERT_EQ(ctx->stack_depth, 2, "Depth is 2");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 1, "Depth is 1");
    ASSERT_EQ(ctx->current_frame, frame1, "Current is outer");

    debug_pop_frame(ctx);
    ASSERT_EQ(ctx->stack_depth, 0, "Depth is 0");
    ASSERT_NULL(ctx->current_frame, "No current frame");

    debug_cleanup(ctx);
}

TEST(pop_frame_updates_total_time) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    long time_before = ctx->total_time_ns;

    debug_push_frame(ctx, "func", "test.sh", 1);

    // Small delay to ensure measurable time
    struct timespec ts = {0, 1000000}; // 1ms
    nanosleep(&ts, NULL);

    debug_pop_frame(ctx);

    // Total time should have increased
    ASSERT_GE(ctx->total_time_ns, time_before, "Total time increased");

    debug_cleanup(ctx);
}

TEST(update_frame_node) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_frame_t *frame = debug_push_frame(ctx, "func", "test.sh", 1);
    ASSERT_NOT_NULL(frame, "Push should succeed");
    ASSERT_NULL(frame->current_node, "Initial node is NULL");

    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    debug_update_frame_node(ctx, node);
    ASSERT_EQ(frame->current_node, node, "Node updated");

    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(update_frame_node_null) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    // Should not crash with NULL ctx
    debug_update_frame_node(NULL, NULL);

    // Should not crash with no current frame
    debug_update_frame_node(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(show_stack_empty) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should not crash with empty stack
    debug_show_stack(ctx);

    debug_cleanup(ctx);
}

TEST(show_stack_with_frames) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->show_timing = true;

    debug_push_frame(ctx, "main", "script.sh", 1);
    debug_push_frame(ctx, "helper", "script.sh", 10);
    debug_push_frame(ctx, "worker", "lib.sh", 5);

    // Should not crash
    debug_show_stack(ctx);

    debug_cleanup(ctx);
}

// ============================================================================
// Variable Inspection Tests
// ============================================================================

TEST(inspect_variable_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should not crash
    debug_inspect_variable(NULL, "var");
    debug_inspect_variable(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(inspect_variable_with_dollar) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should handle $ prefix
    debug_inspect_variable(ctx, "$PATH");
    debug_inspect_variable(ctx, "$HOME");

    debug_cleanup(ctx);
}

TEST(inspect_variable_without_dollar) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should handle without $ prefix
    debug_inspect_variable(ctx, "PATH");
    debug_inspect_variable(ctx, "HOME");

    debug_cleanup(ctx);
}

TEST(inspect_variable_special) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Special variables
    debug_inspect_variable(ctx, "$?");
    debug_inspect_variable(ctx, "$$");
    debug_inspect_variable(ctx, "PWD");

    debug_cleanup(ctx);
}

TEST(inspect_all_variables_null) {
    // Should not crash
    debug_inspect_all_variables(NULL);
}

TEST(inspect_all_variables_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should not crash
    debug_inspect_all_variables(ctx);

    debug_cleanup(ctx);
}

TEST(inspect_all_variables_with_frame) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    debug_push_frame(ctx, "test_func", "test.sh", 1);

    // Should not crash with active frame
    debug_inspect_all_variables(ctx);

    debug_cleanup(ctx);
}

TEST(watch_variable_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should not crash
    debug_watch_variable(NULL, "var");
    debug_watch_variable(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(watch_variable_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    debug_watch_variable(ctx, "MY_VAR");
    debug_watch_variable(ctx, "$PATH");

    debug_cleanup(ctx);
}

TEST(show_variable_changes) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    // Should not crash
    debug_show_variable_changes(ctx);

    debug_cleanup(ctx);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("Running debug trace tests...\n\n");

    printf("Node Tracing:\n");
    RUN_TEST(trace_node_null_params);
    RUN_TEST(trace_node_disabled);
    RUN_TEST(trace_node_enabled);
    RUN_TEST(trace_node_with_timing);
    RUN_TEST(trace_node_multiple_types);

    printf("\nCommand Tracing:\n");
    RUN_TEST(trace_command_null_params);
    RUN_TEST(trace_command_simple);
    RUN_TEST(trace_command_with_args);
    RUN_TEST(trace_command_disabled);

    printf("\nBuiltin Tracing:\n");
    RUN_TEST(trace_builtin_null_params);
    RUN_TEST(trace_builtin_simple);
    RUN_TEST(trace_builtin_with_args);

    printf("\nFunction Call Tracing:\n");
    RUN_TEST(trace_function_null_params);
    RUN_TEST(trace_function_simple);
    RUN_TEST(trace_function_with_args);

    printf("\nStack Frame - Push:\n");
    RUN_TEST(push_frame_null_params);
    RUN_TEST(push_frame_basic);
    RUN_TEST(push_frame_nested);
    RUN_TEST(push_frame_max_depth);
    RUN_TEST(push_frame_null_file);

    printf("\nStack Frame - Pop:\n");
    RUN_TEST(pop_frame_null_ctx);
    RUN_TEST(pop_frame_empty_stack);
    RUN_TEST(pop_frame_basic);
    RUN_TEST(pop_frame_nested);
    RUN_TEST(pop_frame_updates_total_time);

    printf("\nStack Frame - Update/Show:\n");
    RUN_TEST(update_frame_node);
    RUN_TEST(update_frame_node_null);
    RUN_TEST(show_stack_empty);
    RUN_TEST(show_stack_with_frames);

    printf("\nVariable Inspection:\n");
    RUN_TEST(inspect_variable_null_params);
    RUN_TEST(inspect_variable_with_dollar);
    RUN_TEST(inspect_variable_without_dollar);
    RUN_TEST(inspect_variable_special);
    RUN_TEST(inspect_all_variables_null);
    RUN_TEST(inspect_all_variables_basic);
    RUN_TEST(inspect_all_variables_with_frame);
    RUN_TEST(watch_variable_null_params);
    RUN_TEST(watch_variable_basic);
    RUN_TEST(show_variable_changes);

    printf("\n========================================\n");
    printf("Tests run: %d, Passed: %d, Failed: %d\n", tests_run, tests_passed,
           tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
