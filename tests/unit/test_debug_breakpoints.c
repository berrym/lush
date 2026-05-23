/**
 * @file test_debug_breakpoints.c
 * @brief Unit tests for debug breakpoint management
 *
 * Tests breakpoint creation, removal, enabling/disabling, condition
 * evaluation, step execution, loop context tracking, and interactive
 * debugging user input handling.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "executor.h"
#include "node.h"
#include "test_framework.h"

/* Local 1-arg / 2-arg ASSERT_* helpers (no message) bridge to the
 * framework's variants by synthesizing a stringified-expression
 * message. Replaces the historical exit(1) failure path with
 * longjmp-based isolation per RUN_TEST. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")
#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL, #p " is NULL")
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL, #p " is non-NULL")

// Test framework macros

// Helper to create a debug context for testing
static debug_context_t *create_test_context(void) {
    debug_context_t *ctx = calloc(1, sizeof(debug_context_t));
    if (ctx) {
        ctx->enabled = true;
        ctx->debug_output = fopen("/dev/null", "w");
        ctx->next_breakpoint_id = 1;
        ctx->breakpoints = NULL;
        ctx->current_frame = NULL;
        ctx->mode = DEBUG_MODE_CONTINUE;
        ctx->step_mode = false;
        // Initialize execution context
        ctx->execution_context.in_loop = false;
        ctx->execution_context.loop_variable = NULL;
        ctx->execution_context.loop_variable_value = NULL;
        ctx->execution_context.loop_node = NULL;
        ctx->execution_context.loop_iteration = 0;
        ctx->execution_context.loop_body_start_line = 0;
    }
    return ctx;
}

// Helper to free a debug context
static void free_test_context(debug_context_t *ctx) {
    if (ctx) {
        debug_clear_breakpoints(ctx);
        if (ctx->debug_output && ctx->debug_output != stdout) {
            fclose(ctx->debug_output);
        }
        free(ctx->execution_context.loop_variable);
        free(ctx->execution_context.loop_variable_value);
        free(ctx);
    }
}

/* ============================================================
 * BREAKPOINT ADD TESTS
 * ============================================================ */

static int test_add_breakpoint_null_context(void) {
    int result = debug_add_breakpoint(NULL, "test.sh", 10, NULL);
    ASSERT_EQ(result, -1);
    return 1;
}

static int test_add_breakpoint_null_file(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int result = debug_add_breakpoint(ctx, NULL, 10, NULL);
    ASSERT_EQ(result, -1);

    free_test_context(ctx);
    return 1;
}

static int test_add_breakpoint_invalid_line_zero(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int result = debug_add_breakpoint(ctx, "test.sh", 0, NULL);
    ASSERT_EQ(result, -1);

    free_test_context(ctx);
    return 1;
}

static int test_add_breakpoint_invalid_line_negative(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int result = debug_add_breakpoint(ctx, "test.sh", -5, NULL);
    ASSERT_EQ(result, -1);

    free_test_context(ctx);
    return 1;
}

static int test_add_breakpoint_simple(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(id > 0);
    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->breakpoints->id, id);
    ASSERT_STR_EQ(ctx->breakpoints->file, "test.sh");
    ASSERT_EQ(ctx->breakpoints->line, 10);
    ASSERT_NULL(ctx->breakpoints->condition);
    ASSERT_EQ(ctx->breakpoints->hit_count, 0);
    ASSERT(ctx->breakpoints->enabled);

    free_test_context(ctx);
    return 1;
}

static int test_add_breakpoint_with_condition(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "script.sh", 25, "$i == 5");
    ASSERT(id > 0);
    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_NOT_NULL(ctx->breakpoints->condition);
    ASSERT_STR_EQ(ctx->breakpoints->condition, "$i == 5");

    free_test_context(ctx);
    return 1;
}

static int test_add_multiple_breakpoints(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id1 = debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "file2.sh", 20, NULL);
    int id3 = debug_add_breakpoint(ctx, "file1.sh", 30, NULL);

    ASSERT(id1 > 0);
    ASSERT(id2 > 0);
    ASSERT(id3 > 0);
    ASSERT(id1 != id2);
    ASSERT(id2 != id3);
    ASSERT(id1 != id3);

    // Breakpoints are added to head of list, so most recent is first
    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->breakpoints->id, id3);
    ASSERT_NOT_NULL(ctx->breakpoints->next);
    ASSERT_EQ(ctx->breakpoints->next->id, id2);
    ASSERT_NOT_NULL(ctx->breakpoints->next->next);
    ASSERT_EQ(ctx->breakpoints->next->next->id, id1);

    free_test_context(ctx);
    return 1;
}

static int test_add_breakpoint_increments_id(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    ASSERT_EQ(ctx->next_breakpoint_id, 1);
    int id1 = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT_EQ(ctx->next_breakpoint_id, 2);
    int id2 = debug_add_breakpoint(ctx, "test.sh", 20, NULL);
    ASSERT_EQ(ctx->next_breakpoint_id, 3);

    ASSERT_EQ(id1, 1);
    ASSERT_EQ(id2, 2);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * BREAKPOINT REMOVE TESTS
 * ============================================================ */

static int test_remove_breakpoint_null_context(void) {
    bool result = debug_remove_breakpoint(NULL, 1);
    ASSERT(!result);
    return 1;
}

static int test_remove_breakpoint_invalid_id_zero(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    bool result = debug_remove_breakpoint(ctx, 0);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_invalid_id_negative(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    bool result = debug_remove_breakpoint(ctx, -1);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_not_found(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    bool result = debug_remove_breakpoint(ctx, 999);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_single(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT_NOT_NULL(ctx->breakpoints);

    bool result = debug_remove_breakpoint(ctx, id);
    ASSERT(result);
    ASSERT_NULL(ctx->breakpoints);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_from_middle(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id1 = debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "file2.sh", 20, NULL);
    int id3 = debug_add_breakpoint(ctx, "file3.sh", 30, NULL);

    // List order: id3 -> id2 -> id1
    bool result = debug_remove_breakpoint(ctx, id2);
    ASSERT(result);

    // List should now be: id3 -> id1
    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->breakpoints->id, id3);
    ASSERT_NOT_NULL(ctx->breakpoints->next);
    ASSERT_EQ(ctx->breakpoints->next->id, id1);
    ASSERT_NULL(ctx->breakpoints->next->next);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_from_head(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id1 = debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "file2.sh", 20, NULL);

    // List order: id2 -> id1, remove head
    bool result = debug_remove_breakpoint(ctx, id2);
    ASSERT(result);

    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->breakpoints->id, id1);
    ASSERT_NULL(ctx->breakpoints->next);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_from_tail(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id1 = debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "file2.sh", 20, NULL);

    // List order: id2 -> id1, remove tail
    bool result = debug_remove_breakpoint(ctx, id1);
    ASSERT(result);

    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->breakpoints->id, id2);
    ASSERT_NULL(ctx->breakpoints->next);

    free_test_context(ctx);
    return 1;
}

static int test_remove_breakpoint_with_condition_frees_memory(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, "$x > 10");
    ASSERT(id > 0);

    // Verify condition was set
    ASSERT_NOT_NULL(ctx->breakpoints->condition);

    // Remove should free condition string without crashing
    bool result = debug_remove_breakpoint(ctx, id);
    ASSERT(result);
    ASSERT_NULL(ctx->breakpoints);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * BREAKPOINT ENABLE/DISABLE TESTS
 * ============================================================ */

static int test_enable_breakpoint_null_context(void) {
    bool result = debug_enable_breakpoint(NULL, 1, true);
    ASSERT(!result);
    return 1;
}

static int test_enable_breakpoint_invalid_id(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    bool result = debug_enable_breakpoint(ctx, 0, true);
    ASSERT(!result);

    result = debug_enable_breakpoint(ctx, -1, true);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_enable_breakpoint_not_found(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    bool result = debug_enable_breakpoint(ctx, 999, true);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_disable_breakpoint(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(ctx->breakpoints->enabled);

    bool result = debug_enable_breakpoint(ctx, id, false);
    ASSERT(result);
    ASSERT(!ctx->breakpoints->enabled);

    free_test_context(ctx);
    return 1;
}

static int test_enable_breakpoint_already_enabled(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT(ctx->breakpoints->enabled);

    // Enable an already enabled breakpoint
    bool result = debug_enable_breakpoint(ctx, id, true);
    ASSERT(result);
    ASSERT(ctx->breakpoints->enabled);

    free_test_context(ctx);
    return 1;
}

static int test_reenable_breakpoint(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);

    // Disable then re-enable
    debug_enable_breakpoint(ctx, id, false);
    ASSERT(!ctx->breakpoints->enabled);

    bool result = debug_enable_breakpoint(ctx, id, true);
    ASSERT(result);
    ASSERT(ctx->breakpoints->enabled);

    free_test_context(ctx);
    return 1;
}

static int test_enable_specific_breakpoint_in_list(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id1 = debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    int id2 = debug_add_breakpoint(ctx, "file2.sh", 20, NULL);
    int id3 = debug_add_breakpoint(ctx, "file3.sh", 30, NULL);

    // Disable the middle one
    bool result = debug_enable_breakpoint(ctx, id2, false);
    ASSERT(result);

    // Verify only id2 is disabled
    breakpoint_t *bp = ctx->breakpoints;
    while (bp) {
        if (bp->id == id2) {
            ASSERT(!bp->enabled);
        } else {
            ASSERT(bp->enabled);
        }
        bp = bp->next;
    }

    (void)id1;
    (void)id3;
    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * BREAKPOINT LIST TESTS
 * ============================================================ */

static int test_list_breakpoints_null_context(void) {
    // Should not crash
    debug_list_breakpoints(NULL);
    return 1;
}

static int test_list_breakpoints_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    // Should not crash, just return early
    debug_list_breakpoints(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_list_breakpoints_empty(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Should not crash with no breakpoints
    debug_list_breakpoints(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_list_breakpoints_with_entries(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "file1.sh", 10, NULL);
    debug_add_breakpoint(ctx, "file2.sh", 20, "$x == 5");

    // Should not crash, output goes to /dev/null
    debug_list_breakpoints(ctx);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * BREAKPOINT CLEAR TESTS
 * ============================================================ */

static int test_clear_breakpoints_null_context(void) {
    // Should not crash
    debug_clear_breakpoints(NULL);
    return 1;
}

static int test_clear_breakpoints_empty(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_clear_breakpoints(ctx);
    ASSERT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->next_breakpoint_id, 1);

    free_test_context(ctx);
    return 1;
}

static int test_clear_breakpoints_single(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT_NOT_NULL(ctx->breakpoints);

    debug_clear_breakpoints(ctx);
    ASSERT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->next_breakpoint_id, 1);

    free_test_context(ctx);
    return 1;
}

static int test_clear_breakpoints_multiple(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "file1.sh", 10, "condition1");
    debug_add_breakpoint(ctx, "file2.sh", 20, "condition2");
    debug_add_breakpoint(ctx, "file3.sh", 30, NULL);
    ASSERT_NOT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->next_breakpoint_id, 4);

    debug_clear_breakpoints(ctx);
    ASSERT_NULL(ctx->breakpoints);
    ASSERT_EQ(ctx->next_breakpoint_id, 1);

    free_test_context(ctx);
    return 1;
}

static int test_clear_breakpoints_resets_id_counter(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    debug_add_breakpoint(ctx, "test.sh", 20, NULL);
    ASSERT_EQ(ctx->next_breakpoint_id, 3);

    debug_clear_breakpoints(ctx);
    ASSERT_EQ(ctx->next_breakpoint_id, 1);

    // Adding after clear should start from 1
    int id = debug_add_breakpoint(ctx, "test.sh", 30, NULL);
    ASSERT_EQ(id, 1);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * CHECK BREAKPOINT TESTS
 * ============================================================ */

static int test_check_breakpoint_null_context(void) {
    bool result = debug_check_breakpoint(NULL, "test.sh", 10);
    ASSERT(!result);
    return 1;
}

static int test_check_breakpoint_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    bool result = debug_check_breakpoint(ctx, "test.sh", 10);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_check_breakpoint_null_file(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    bool result = debug_check_breakpoint(ctx, NULL, 10);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_check_breakpoint_invalid_line(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    bool result = debug_check_breakpoint(ctx, "test.sh", 0);
    ASSERT(!result);

    result = debug_check_breakpoint(ctx, "test.sh", -1);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_check_breakpoint_no_match(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);

    // Different file
    bool result = debug_check_breakpoint(ctx, "other.sh", 10);
    ASSERT(!result);

    // Different line
    result = debug_check_breakpoint(ctx, "test.sh", 20);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

static int test_check_breakpoint_disabled_breakpoint(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    int id = debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    debug_enable_breakpoint(ctx, id, false);

    bool result = debug_check_breakpoint(ctx, "test.sh", 10);
    ASSERT(!result);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * STEP EXECUTION TESTS
 * ============================================================ */

/* The step-depth gate: debug_check_breakpoint takes its step branch
 * only when stack_depth <= step_target_depth. This is the mechanism
 * behind step-over (skip deeper call frames) and step-out (run on to
 * the caller). With no breakpoint registered, a true return means the
 * step branch fired. */
static int test_step_fires_within_target_depth(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->step_target_depth = 5;
    ctx->stack_depth = 3;

    // 3 <= 5: not deeper than the target -- stepping stops here.
    ASSERT(debug_check_breakpoint(ctx, "f.sh", 1));

    free_test_context(ctx);
    return 1;
}

static int test_step_suppressed_beyond_target_depth(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->step_target_depth = 2;
    ctx->stack_depth = 6;

    // 6 > 2: this node is inside a stepped-over call -- no stop.
    ASSERT(!debug_check_breakpoint(ctx, "f.sh", 1));

    free_test_context(ctx);
    return 1;
}

static int test_step_into_null_context(void) {
    // Should not crash
    debug_step_into(NULL);
    return 1;
}

static int test_step_into_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_step_into(ctx);
    // Should not change state when disabled
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_step_into_sets_mode(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_step_into(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_step_over_null_context(void) {
    debug_step_over(NULL);
    return 1;
}

static int test_step_over_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_step_over(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_step_over_sets_mode(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_step_over(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_step_out_null_context(void) {
    debug_step_out(NULL);
    return 1;
}

static int test_step_out_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_step_out(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);

    free_test_context(ctx);
    return 1;
}

static int test_step_out_sets_mode(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->mode = DEBUG_MODE_STEP;
    ctx->stack_depth = 3;

    debug_step_out(ctx);
    /* Step-out keeps single-stepping, but suppressed until the stack
     * unwinds past the current frame: target depth is stack_depth - 1. */
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);
    ASSERT_EQ(ctx->step_target_depth, 2);

    free_test_context(ctx);
    return 1;
}

static int test_continue_null_context(void) {
    debug_continue(NULL);
    return 1;
}

static int test_continue_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;
    ctx->step_mode = true;

    debug_continue(ctx);
    // Should not change state when disabled
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_continue_clears_step_mode(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->mode = DEBUG_MODE_STEP;

    debug_continue(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * USER INPUT HANDLING TESTS
 * ============================================================ */

static int test_handle_user_input_null_context(void) {
    debug_handle_user_input(NULL, "continue");
    return 1;
}

static int test_handle_user_input_null_input(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_handle_user_input(ctx, NULL);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_empty(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "");
    // Empty input defaults to continue
    ASSERT(!ctx->step_mode);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_whitespace_only(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "   \t  \n");
    // Whitespace-only input defaults to continue
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_continue_short(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "c\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_continue_long(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "continue\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_step_short(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_handle_user_input(ctx, "s\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_step_long(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_handle_user_input(ctx, "step\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_next_short(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_handle_user_input(ctx, "n\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_next_long(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_handle_user_input(ctx, "next\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_finish_short(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->stack_depth = 2;

    // 'f' is a resume command and performs step-out.
    bool resume = debug_handle_user_input(ctx, "f\n");
    ASSERT(resume);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);
    ASSERT_EQ(ctx->step_target_depth, 1);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_finish_long(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    ctx->stack_depth = 2;

    bool resume = debug_handle_user_input(ctx, "finish\n");
    ASSERT(resume);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);
    ASSERT_EQ(ctx->step_target_depth, 1);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_quit_short(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "q\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_quit_long(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "quit\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_unknown_command(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    debug_mode_t original_mode = ctx->mode;

    debug_handle_user_input(ctx, "foobar\n");
    // Unknown command should not change mode
    ASSERT_EQ(ctx->mode, original_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_with_leading_whitespace(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;

    debug_handle_user_input(ctx, "   continue\n");
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_backtrace(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->step_mode = true;
    debug_mode_t original_mode = ctx->mode;

    // bt command shows stack, doesn't change mode
    debug_handle_user_input(ctx, "bt\n");
    ASSERT_EQ(ctx->mode, original_mode);

    debug_handle_user_input(ctx, "backtrace\n");
    ASSERT_EQ(ctx->mode, original_mode);

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_list(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    debug_add_breakpoint(ctx, "test.sh", 10, NULL);

    // l/list shows breakpoints
    debug_handle_user_input(ctx, "l\n");
    debug_handle_user_input(ctx, "list\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_help(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // h/help shows help text
    debug_handle_user_input(ctx, "h\n");
    debug_handle_user_input(ctx, "help\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_vars(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // vars shows all variables
    debug_handle_user_input(ctx, "vars\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_where(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // where shows current location
    debug_handle_user_input(ctx, "where\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_print_variable(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // p <var> inspects variable
    debug_handle_user_input(ctx, "p myvar\n");
    debug_handle_user_input(ctx, "print somevar\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_watch(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // watch <var> adds to watch list
    debug_handle_user_input(ctx, "watch counter\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_set(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // set <var>=<value> sets variable
    debug_handle_user_input(ctx, "set x=10\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_eval(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // eval <expr> evaluates expression
    debug_handle_user_input(ctx, "eval $x + 1\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_mode(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // mode shows current shell mode
    debug_handle_user_input(ctx, "mode\n");

    free_test_context(ctx);
    return 1;
}

static int test_handle_user_input_features(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // features lists all shell features
    debug_handle_user_input(ctx, "features\n");

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * STACK NAVIGATION TESTS
 * ============================================================ */

static int test_stack_up_null_context(void) {
    debug_stack_up(NULL);
    return 1;
}

static int test_stack_up_with_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently a placeholder
    debug_stack_up(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_stack_down_null_context(void) {
    debug_stack_down(NULL);
    return 1;
}

static int test_stack_down_with_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently a placeholder
    debug_stack_down(ctx);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * SHOW CURRENT LOCATION TESTS
 * ============================================================ */

static int test_show_current_location_null_context(void) {
    debug_show_current_location(NULL);
    return 1;
}

static int test_show_current_location_no_frame(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->current_frame = NULL;

    debug_show_current_location(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_show_current_location_with_frame(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_frame_t frame = {.function_name = "test_func",
                           .file_path = "test.sh",
                           .line_number = 42,
                           .parent = NULL};
    ctx->current_frame = &frame;

    debug_show_current_location(ctx);

    ctx->current_frame = NULL; // Don't let free_test_context touch stack frame
    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * SET VARIABLE TESTS
 * ============================================================ */

static int test_set_variable_null_context(void) {
    debug_set_variable(NULL, "x=10");
    return 1;
}

static int test_set_variable_null_assignment(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_set_variable(ctx, NULL);

    free_test_context(ctx);
    return 1;
}

static int test_set_variable_with_assignment(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently a placeholder
    debug_set_variable(ctx, "myvar=hello");

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * EVALUATE EXPRESSION TESTS
 * ============================================================ */

static int test_evaluate_expression_null_context(void) {
    debug_evaluate_expression(NULL, "1 + 2");
    return 1;
}

static int test_evaluate_expression_null_expression(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_evaluate_expression(ctx, NULL);

    free_test_context(ctx);
    return 1;
}

static int test_evaluate_expression_with_expression(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently a placeholder
    debug_evaluate_expression(ctx, "$x + 1");

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * CONDITION EVALUATION TESTS
 * ============================================================ */

static int test_evaluate_condition_null_context(void) {
    bool result = debug_evaluate_condition(NULL, "$x == 5");
    ASSERT(result); // NULL context returns true
    return 1;
}

static int test_evaluate_condition_null_condition(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    bool result = debug_evaluate_condition(ctx, NULL);
    ASSERT(result); // NULL condition means always true

    free_test_context(ctx);
    return 1;
}

static int test_evaluate_condition_with_comparison(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently returns true for any comparison
    bool result = debug_evaluate_condition(ctx, "$x == 5");
    ASSERT(result);

    result = debug_evaluate_condition(ctx, "$i != 10");
    ASSERT(result);

    result = debug_evaluate_condition(ctx, "$count < 100");
    ASSERT(result);

    result = debug_evaluate_condition(ctx, "$value > 0");
    ASSERT(result);

    free_test_context(ctx);
    return 1;
}

static int test_evaluate_condition_with_variable_check(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Currently returns true for variable existence checks
    bool result = debug_evaluate_condition(ctx, "$myvar");
    ASSERT(result);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * SHOW CONTEXT TESTS
 * ============================================================ */

static int test_show_context_null_context(void) {
    debug_show_context(NULL, "test.sh", 10);
    return 1;
}

static int test_show_context_null_file(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_show_context(ctx, NULL, 10);

    free_test_context(ctx);
    return 1;
}

static int test_show_context_nonexistent_file(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Should handle missing file gracefully
    debug_show_context(ctx, "/nonexistent/path/file.sh", 10);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * PRINT HELP TESTS
 * ============================================================ */

static int test_print_help_null_context(void) {
    debug_print_help(NULL);
    return 1;
}

static int test_print_help_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_print_help(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_print_help_enabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_print_help(ctx);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * LOOP CONTEXT TESTS
 * ============================================================ */

static int test_enter_loop_null_context(void) {
    debug_enter_loop(NULL, "for", "i", "1");
    return 1;
}

static int test_enter_loop_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_enter_loop(ctx, "for", "i", "1");
    ASSERT(!ctx->execution_context.in_loop);

    free_test_context(ctx);
    return 1;
}

static int test_enter_loop_for(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "for", "i", "first");
    ASSERT(ctx->execution_context.in_loop);
    ASSERT_NOT_NULL(ctx->execution_context.loop_variable);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable, "i");
    ASSERT_NOT_NULL(ctx->execution_context.loop_variable_value);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable_value, "first");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);

    free_test_context(ctx);
    return 1;
}

static int test_enter_loop_while(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "while", "count", "0");
    ASSERT(ctx->execution_context.in_loop);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable, "count");

    free_test_context(ctx);
    return 1;
}

static int test_enter_loop_null_values(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, NULL, NULL, NULL);
    ASSERT(ctx->execution_context.in_loop);
    ASSERT_NULL(ctx->execution_context.loop_variable);
    ASSERT_NULL(ctx->execution_context.loop_variable_value);

    free_test_context(ctx);
    return 1;
}

static int test_update_loop_variable_null_context(void) {
    debug_update_loop_variable(NULL, "i", "5");
    return 1;
}

static int test_update_loop_variable_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_update_loop_variable(ctx, "i", "5");

    free_test_context(ctx);
    return 1;
}

static int test_update_loop_variable_not_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Not in loop context, should do nothing
    debug_update_loop_variable(ctx, "i", "5");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);

    free_test_context(ctx);
    return 1;
}

static int test_update_loop_variable(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "for", "i", "1");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable_value, "1");

    debug_update_loop_variable(ctx, "i", "2");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 1);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable_value, "2");

    debug_update_loop_variable(ctx, "i", "3");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 2);
    ASSERT_STR_EQ(ctx->execution_context.loop_variable_value, "3");

    free_test_context(ctx);
    return 1;
}

static int test_update_loop_variable_null_value(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "for", "i", "1");
    debug_update_loop_variable(ctx, "i", NULL);
    ASSERT_NULL(ctx->execution_context.loop_variable_value);
    ASSERT_EQ(ctx->execution_context.loop_iteration, 1);

    free_test_context(ctx);
    return 1;
}

static int test_exit_loop_null_context(void) {
    debug_exit_loop(NULL);
    return 1;
}

static int test_exit_loop_disabled_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    ctx->enabled = false;

    debug_exit_loop(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_exit_loop_not_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Not in loop, should do nothing without crashing
    debug_exit_loop(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_exit_loop_clears_context(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "for", "i", "1");
    debug_update_loop_variable(ctx, "i", "2");
    debug_update_loop_variable(ctx, "i", "3");

    debug_exit_loop(ctx);
    ASSERT(!ctx->execution_context.in_loop);
    ASSERT_NULL(ctx->execution_context.loop_variable);
    ASSERT_NULL(ctx->execution_context.loop_variable_value);
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);
    ASSERT_EQ(ctx->execution_context.loop_body_start_line, 0);
    ASSERT_NULL(ctx->execution_context.loop_node);

    free_test_context(ctx);
    return 1;
}

static int test_loop_lifecycle(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Simulate a for loop: for i in a b c
    debug_enter_loop(ctx, "for", "i", "a");
    ASSERT(ctx->execution_context.in_loop);
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);

    debug_update_loop_variable(ctx, "i", "b");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 1);

    debug_update_loop_variable(ctx, "i", "c");
    ASSERT_EQ(ctx->execution_context.loop_iteration, 2);

    debug_exit_loop(ctx);
    ASSERT(!ctx->execution_context.in_loop);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * EXECUTION CONTEXT TESTS
 * ============================================================ */

static int test_save_execution_context_null_params(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // All NULL parameters
    debug_save_execution_context(NULL, NULL, NULL);

    // Partial NULL parameters
    debug_save_execution_context(ctx, NULL, NULL);

    executor_t executor = {0};
    debug_save_execution_context(ctx, &executor, NULL);

    free_test_context(ctx);
    return 1;
}

static int test_save_execution_context_for_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    node_t node = {
        .type = NODE_FOR,
    };
    executor_t executor = {0};

    debug_save_execution_context(ctx, &executor, &node);
    ASSERT(ctx->execution_context.in_loop);
    ASSERT_EQ(ctx->execution_context.loop_node, &node);

    free_test_context(ctx);
    return 1;
}

static int test_save_execution_context_while_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    node_t node = {
        .type = NODE_WHILE,
    };
    executor_t executor = {0};

    debug_save_execution_context(ctx, &executor, &node);
    ASSERT(ctx->execution_context.in_loop);

    free_test_context(ctx);
    return 1;
}

static int test_save_execution_context_until_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    node_t node = {
        .type = NODE_UNTIL,
    };
    executor_t executor = {0};

    debug_save_execution_context(ctx, &executor, &node);
    ASSERT(ctx->execution_context.in_loop);

    free_test_context(ctx);
    return 1;
}

static int test_save_execution_context_non_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    node_t node = {
        .type = NODE_COMMAND,
    };
    executor_t executor = {0};

    debug_save_execution_context(ctx, &executor, &node);
    ASSERT(!ctx->execution_context.in_loop);

    free_test_context(ctx);
    return 1;
}

static int test_restore_execution_context_null_context(void) {
    debug_restore_execution_context(NULL, NULL, NULL);
    return 1;
}

static int test_restore_execution_context_not_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    executor_t executor = {0};
    node_t node = {.type = NODE_COMMAND};

    debug_restore_execution_context(ctx, &executor, &node);

    free_test_context(ctx);
    return 1;
}

static int test_restore_execution_context_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Enter loop first
    debug_enter_loop(ctx, "for", "i", "value");

    executor_t executor = {0};
    node_t node = {.type = NODE_FOR};

    // Restore should work without crash
    debug_restore_execution_context(ctx, &executor, &node);

    free_test_context(ctx);
    return 1;
}

static int test_cleanup_execution_context_null(void) {
    debug_cleanup_execution_context(NULL);
    return 1;
}

static int test_cleanup_execution_context_not_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_cleanup_execution_context(ctx);

    free_test_context(ctx);
    return 1;
}

static int test_cleanup_execution_context_in_loop(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_enter_loop(ctx, "for", "i", "test");
    ASSERT(ctx->execution_context.in_loop);

    debug_cleanup_execution_context(ctx);
    ASSERT(!ctx->execution_context.in_loop);
    ASSERT_NULL(ctx->execution_context.loop_variable);
    ASSERT_NULL(ctx->execution_context.loop_variable_value);
    ASSERT_EQ(ctx->execution_context.loop_iteration, 0);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * BREAKPOINT HIT COUNT TESTS
 * ============================================================ */

static int test_breakpoint_hit_count_initialized_zero(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    debug_add_breakpoint(ctx, "test.sh", 10, NULL);
    ASSERT_EQ(ctx->breakpoints->hit_count, 0);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * INTEGRATION-STYLE TESTS
 * ============================================================ */

static int test_multiple_breakpoints_workflow(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Add several breakpoints
    int bp1 = debug_add_breakpoint(ctx, "main.sh", 10, NULL);
    int bp2 = debug_add_breakpoint(ctx, "main.sh", 20, "$x > 0");
    int bp3 = debug_add_breakpoint(ctx, "utils.sh", 5, NULL);

    ASSERT(bp1 > 0 && bp2 > 0 && bp3 > 0);

    // Disable one
    debug_enable_breakpoint(ctx, bp2, false);

    // Verify state
    breakpoint_t *bp = ctx->breakpoints;
    int count = 0;
    int disabled_count = 0;
    while (bp) {
        count++;
        if (!bp->enabled)
            disabled_count++;
        bp = bp->next;
    }
    ASSERT_EQ(count, 3);
    ASSERT_EQ(disabled_count, 1);

    // Remove one
    debug_remove_breakpoint(ctx, bp1);

    // Verify removal
    bp = ctx->breakpoints;
    count = 0;
    while (bp) {
        ASSERT(bp->id != bp1);
        count++;
        bp = bp->next;
    }
    ASSERT_EQ(count, 2);

    // Clear all
    debug_clear_breakpoints(ctx);
    ASSERT_NULL(ctx->breakpoints);

    free_test_context(ctx);
    return 1;
}

static int test_step_mode_transitions(void) {
    debug_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);

    // Start in continue mode
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    // Step into
    debug_step_into(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP);
    ASSERT(ctx->step_mode);

    // Step over
    debug_step_over(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);

    // Continue
    debug_continue(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_CONTINUE);
    ASSERT(!ctx->step_mode);

    /* Step into again, then step out -- step-out keeps stepping, with
     * a target depth one frame shallower than the current one. */
    debug_step_into(ctx);
    debug_step_out(ctx);
    ASSERT_EQ(ctx->mode, DEBUG_MODE_STEP_OVER);
    ASSERT(ctx->step_mode);

    free_test_context(ctx);
    return 1;
}

/* ============================================================
 * MAIN TEST RUNNER
 * ============================================================ */

int main(void) {
    printf("Running debug breakpoints tests...\n\n");

    printf("=== Breakpoint Add Tests ===\n");
    RUN_TEST(add_breakpoint_null_context);
    RUN_TEST(add_breakpoint_null_file);
    RUN_TEST(add_breakpoint_invalid_line_zero);
    RUN_TEST(add_breakpoint_invalid_line_negative);
    RUN_TEST(add_breakpoint_simple);
    RUN_TEST(add_breakpoint_with_condition);
    RUN_TEST(add_multiple_breakpoints);
    RUN_TEST(add_breakpoint_increments_id);

    printf("\n=== Breakpoint Remove Tests ===\n");
    RUN_TEST(remove_breakpoint_null_context);
    RUN_TEST(remove_breakpoint_invalid_id_zero);
    RUN_TEST(remove_breakpoint_invalid_id_negative);
    RUN_TEST(remove_breakpoint_not_found);
    RUN_TEST(remove_breakpoint_single);
    RUN_TEST(remove_breakpoint_from_middle);
    RUN_TEST(remove_breakpoint_from_head);
    RUN_TEST(remove_breakpoint_from_tail);
    RUN_TEST(remove_breakpoint_with_condition_frees_memory);

    printf("\n=== Breakpoint Enable/Disable Tests ===\n");
    RUN_TEST(enable_breakpoint_null_context);
    RUN_TEST(enable_breakpoint_invalid_id);
    RUN_TEST(enable_breakpoint_not_found);
    RUN_TEST(disable_breakpoint);
    RUN_TEST(enable_breakpoint_already_enabled);
    RUN_TEST(reenable_breakpoint);
    RUN_TEST(enable_specific_breakpoint_in_list);

    printf("\n=== Breakpoint List Tests ===\n");
    RUN_TEST(list_breakpoints_null_context);
    RUN_TEST(list_breakpoints_disabled_context);
    RUN_TEST(list_breakpoints_empty);
    RUN_TEST(list_breakpoints_with_entries);

    printf("\n=== Breakpoint Clear Tests ===\n");
    RUN_TEST(clear_breakpoints_null_context);
    RUN_TEST(clear_breakpoints_empty);
    RUN_TEST(clear_breakpoints_single);
    RUN_TEST(clear_breakpoints_multiple);
    RUN_TEST(clear_breakpoints_resets_id_counter);

    printf("\n=== Check Breakpoint Tests ===\n");
    RUN_TEST(check_breakpoint_null_context);
    RUN_TEST(check_breakpoint_disabled_context);
    RUN_TEST(check_breakpoint_null_file);
    RUN_TEST(check_breakpoint_invalid_line);
    RUN_TEST(check_breakpoint_no_match);
    RUN_TEST(check_breakpoint_disabled_breakpoint);

    printf("\n=== Step Execution Tests ===\n");
    RUN_TEST(step_into_null_context);
    RUN_TEST(step_into_disabled_context);
    RUN_TEST(step_into_sets_mode);
    RUN_TEST(step_over_null_context);
    RUN_TEST(step_over_disabled_context);
    RUN_TEST(step_over_sets_mode);
    RUN_TEST(step_out_null_context);
    RUN_TEST(step_out_disabled_context);
    RUN_TEST(step_out_sets_mode);
    RUN_TEST(step_fires_within_target_depth);
    RUN_TEST(step_suppressed_beyond_target_depth);
    RUN_TEST(continue_null_context);
    RUN_TEST(continue_disabled_context);
    RUN_TEST(continue_clears_step_mode);

    printf("\n=== User Input Handling Tests ===\n");
    RUN_TEST(handle_user_input_null_context);
    RUN_TEST(handle_user_input_null_input);
    RUN_TEST(handle_user_input_empty);
    RUN_TEST(handle_user_input_whitespace_only);
    RUN_TEST(handle_user_input_continue_short);
    RUN_TEST(handle_user_input_continue_long);
    RUN_TEST(handle_user_input_step_short);
    RUN_TEST(handle_user_input_step_long);
    RUN_TEST(handle_user_input_next_short);
    RUN_TEST(handle_user_input_next_long);
    RUN_TEST(handle_user_input_finish_short);
    RUN_TEST(handle_user_input_finish_long);
    RUN_TEST(handle_user_input_quit_short);
    RUN_TEST(handle_user_input_quit_long);
    RUN_TEST(handle_user_input_unknown_command);
    RUN_TEST(handle_user_input_with_leading_whitespace);
    RUN_TEST(handle_user_input_backtrace);
    RUN_TEST(handle_user_input_list);
    RUN_TEST(handle_user_input_help);
    RUN_TEST(handle_user_input_vars);
    RUN_TEST(handle_user_input_where);
    RUN_TEST(handle_user_input_print_variable);
    RUN_TEST(handle_user_input_watch);
    RUN_TEST(handle_user_input_set);
    RUN_TEST(handle_user_input_eval);
    RUN_TEST(handle_user_input_mode);
    RUN_TEST(handle_user_input_features);

    printf("\n=== Stack Navigation Tests ===\n");
    RUN_TEST(stack_up_null_context);
    RUN_TEST(stack_up_with_context);
    RUN_TEST(stack_down_null_context);
    RUN_TEST(stack_down_with_context);

    printf("\n=== Show Current Location Tests ===\n");
    RUN_TEST(show_current_location_null_context);
    RUN_TEST(show_current_location_no_frame);
    RUN_TEST(show_current_location_with_frame);

    printf("\n=== Set Variable Tests ===\n");
    RUN_TEST(set_variable_null_context);
    RUN_TEST(set_variable_null_assignment);
    RUN_TEST(set_variable_with_assignment);

    printf("\n=== Evaluate Expression Tests ===\n");
    RUN_TEST(evaluate_expression_null_context);
    RUN_TEST(evaluate_expression_null_expression);
    RUN_TEST(evaluate_expression_with_expression);

    printf("\n=== Condition Evaluation Tests ===\n");
    RUN_TEST(evaluate_condition_null_context);
    RUN_TEST(evaluate_condition_null_condition);
    RUN_TEST(evaluate_condition_with_comparison);
    RUN_TEST(evaluate_condition_with_variable_check);

    printf("\n=== Show Context Tests ===\n");
    RUN_TEST(show_context_null_context);
    RUN_TEST(show_context_null_file);
    RUN_TEST(show_context_nonexistent_file);

    printf("\n=== Print Help Tests ===\n");
    RUN_TEST(print_help_null_context);
    RUN_TEST(print_help_disabled_context);
    RUN_TEST(print_help_enabled_context);

    printf("\n=== Loop Context Tests ===\n");
    RUN_TEST(enter_loop_null_context);
    RUN_TEST(enter_loop_disabled_context);
    RUN_TEST(enter_loop_for);
    RUN_TEST(enter_loop_while);
    RUN_TEST(enter_loop_null_values);
    RUN_TEST(update_loop_variable_null_context);
    RUN_TEST(update_loop_variable_disabled_context);
    RUN_TEST(update_loop_variable_not_in_loop);
    RUN_TEST(update_loop_variable);
    RUN_TEST(update_loop_variable_null_value);
    RUN_TEST(exit_loop_null_context);
    RUN_TEST(exit_loop_disabled_context);
    RUN_TEST(exit_loop_not_in_loop);
    RUN_TEST(exit_loop_clears_context);
    RUN_TEST(loop_lifecycle);

    printf("\n=== Execution Context Tests ===\n");
    RUN_TEST(save_execution_context_null_params);
    RUN_TEST(save_execution_context_for_loop);
    RUN_TEST(save_execution_context_while_loop);
    RUN_TEST(save_execution_context_until_loop);
    RUN_TEST(save_execution_context_non_loop);
    RUN_TEST(restore_execution_context_null_context);
    RUN_TEST(restore_execution_context_not_in_loop);
    RUN_TEST(restore_execution_context_in_loop);
    RUN_TEST(cleanup_execution_context_null);
    RUN_TEST(cleanup_execution_context_not_in_loop);
    RUN_TEST(cleanup_execution_context_in_loop);

    printf("\n=== Hit Count Tests ===\n");
    RUN_TEST(breakpoint_hit_count_initialized_zero);

    printf("\n=== Integration Tests ===\n");
    RUN_TEST(multiple_breakpoints_workflow);
    RUN_TEST(step_mode_transitions);

    printf("\n========================================\n");
    printf("========================================\n");

    return TEST_RESULT();
}
