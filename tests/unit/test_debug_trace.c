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
#include "symtable.h"

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Capture debug output into a fixed buffer for assertions
 *
 * Reads ctx->debug_output (assumed to be a tmpfile) from offset 0.
 *
 * @param ctx Debug context with a tmpfile-backed debug_output.
 * @param buf Destination buffer.
 * @param cap Buffer capacity (NUL-terminator included).
 */
static void read_debug_output(debug_context_t *ctx, char *buf, size_t cap) {
    buf[0] = '\0';
    if (!ctx || !ctx->debug_output || ctx->debug_output == stderr) {
        return;
    }
    fflush(ctx->debug_output);
    rewind(ctx->debug_output);
    size_t n = fread(buf, 1, cap - 1, ctx->debug_output);
    buf[n] = '\0';
}

/**
 * @brief Build a debug context with a tmpfile-captured debug_output
 *
 * The cleanup path (debug_cleanup) fcloses any non-stderr
 * debug_output, so the tmpfile is owned by the returned context and
 * lives until debug_cleanup.
 *
 * @return Heap-allocated, enabled debug context with debug_output set
 *         to a tmpfile, or NULL on allocation failure.
 */
static debug_context_t *new_captured_ctx(void) {
    debug_context_t *ctx = debug_init();
    if (!ctx) {
        return NULL;
    }
    FILE *capture = tmpfile();
    if (capture) {
        ctx->debug_output = capture;
    }
    debug_enable(ctx, true);
    return ctx;
}

/// ============================================================================
/// Test Framework
/// ============================================================================

/// ============================================================================
/// Node Tracing Tests
/// ============================================================================

TEST(trace_node_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    /// Should not crash with NULL parameters
    debug_trace_node(NULL, NULL, NULL, 0);
    debug_trace_node(ctx, NULL, "test.sh", 1);
    debug_trace_node(ctx, NULL, NULL, 1);

    debug_cleanup(ctx);
}

TEST(trace_node_disabled) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Debug disabled - should do nothing
    ctx->enabled = false;
    ctx->trace_execution = true;

    node_t *node = new_node(NODE_COMMAND);
    ASSERT_NOT_NULL(node, "new_node should succeed");

    long count_before = ctx->total_commands;
    debug_trace_node(ctx, node, "test.sh", 1);
    /// Should not increment when disabled
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

    /// Should not crash with timing enabled
    debug_trace_node(ctx, node, "script.sh", 5);

    free_node_tree(node);
    debug_cleanup(ctx);
}

TEST(trace_node_multiple_types) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    /// Test various node types
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

/// ============================================================================
/// Command Tracing Tests
/// ============================================================================

TEST(trace_command_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    /// Should not crash with NULL
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
    /// Should do nothing when disabled
    debug_trace_command(ctx, "echo", argv, 2);

    debug_cleanup(ctx);
}

/// ============================================================================
/// Builtin Tracing Tests
/// ============================================================================

TEST(trace_builtin_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    /// Should not crash
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

/// ============================================================================
/// Function Call Tracing Tests
/// ============================================================================

TEST(trace_function_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    ctx->trace_execution = true;

    /// Should not crash
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

/// ============================================================================
/// Stack Frame Management Tests
/// ============================================================================

TEST(push_frame_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should return NULL for NULL ctx
    debug_frame_t *frame = debug_push_frame(NULL, "func", "file.sh", 1);
    ASSERT_NULL(frame, "Should return NULL for NULL ctx");

    /// Should return NULL for NULL function
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

    /// Set a low max stack depth for testing
    ctx->max_stack_depth = 3;

    debug_push_frame(ctx, "level1", "test.sh", 1);
    debug_push_frame(ctx, "level2", "test.sh", 2);
    debug_push_frame(ctx, "level3", "test.sh", 3);

    ASSERT_EQ(ctx->stack_depth, 3, "Should be at max depth");

    /// Should return NULL when at max depth
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
    /// Should not crash
    debug_pop_frame(NULL);
}

TEST(pop_frame_empty_stack) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should not crash on empty stack
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

    /// Small delay to ensure measurable time
    struct timespec ts = {0, 1000000}; /// 1ms
    nanosleep(&ts, NULL);

    debug_pop_frame(ctx);

    /// Total time should have increased
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

    /// Should not crash with NULL ctx
    debug_update_frame_node(NULL, NULL);

    /// Should not crash with no current frame
    debug_update_frame_node(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(show_stack_empty) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should not crash with empty stack
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

    /// Should not crash
    debug_show_stack(ctx);

    debug_cleanup(ctx);
}

/// ============================================================================
/// Variable Inspection Tests
/// ============================================================================

TEST(inspect_variable_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should not crash
    debug_inspect_variable(NULL, "var");
    debug_inspect_variable(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(inspect_variable_with_dollar) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should handle $ prefix
    debug_inspect_variable(ctx, "$PATH");
    debug_inspect_variable(ctx, "$HOME");

    debug_cleanup(ctx);
}

TEST(inspect_variable_without_dollar) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should handle without $ prefix
    debug_inspect_variable(ctx, "PATH");
    debug_inspect_variable(ctx, "HOME");

    debug_cleanup(ctx);
}

/// Return a pointer to the inspection-frame portion of a captured debug log,
/// skipping the variable-timestamp preamble that debug_enable() emits.  Falls
/// back to the full buffer if the frame marker is absent.
static const char *frame_body(const char *buf) {
    const char *frame = strstr(buf, "[Variable:");
    return frame ? frame : buf;
}

TEST(inspect_variable_accepts_at_sigil) {
    /// The @ sigil prefix must be stripped before lookup so `@arr` resolves to
    /// the same binding as bare `arr` -- sigils are presentation-only, not a
    /// namespace.
    debug_context_t *ctx_a = new_captured_ctx();
    debug_context_t *ctx_b = new_captured_ctx();
    ASSERT_NOT_NULL(ctx_a, "ctx_a");
    ASSERT_NOT_NULL(ctx_b, "ctx_b");

    char with_sigil[2048];
    char without[2048];

    debug_inspect_variable(ctx_a, "@PATH");
    read_debug_output(ctx_a, with_sigil, sizeof(with_sigil));

    debug_inspect_variable(ctx_b, "PATH");
    read_debug_output(ctx_b, without, sizeof(without));

    ASSERT_STR_EQ(frame_body(with_sigil), frame_body(without),
                  "@PATH inspection matches bare PATH");

    debug_cleanup(ctx_a);
    debug_cleanup(ctx_b);
}

TEST(inspect_variable_accepts_pct_sigil) {
    debug_context_t *ctx_a = new_captured_ctx();
    debug_context_t *ctx_b = new_captured_ctx();
    ASSERT_NOT_NULL(ctx_a, "ctx_a");
    ASSERT_NOT_NULL(ctx_b, "ctx_b");

    char with_sigil[2048];
    char without[2048];

    debug_inspect_variable(ctx_a, "%PATH");
    read_debug_output(ctx_a, with_sigil, sizeof(with_sigil));

    debug_inspect_variable(ctx_b, "PATH");
    read_debug_output(ctx_b, without, sizeof(without));

    ASSERT_STR_EQ(frame_body(with_sigil), frame_body(without),
                  "%PATH inspection matches bare PATH");

    debug_cleanup(ctx_a);
    debug_cleanup(ctx_b);
}

TEST(inspect_variable_special) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Special variables
    debug_inspect_variable(ctx, "$?");
    debug_inspect_variable(ctx, "$$");
    debug_inspect_variable(ctx, "PWD");

    debug_cleanup(ctx);
}

TEST(inspect_all_variables_null) {
    /// Should not crash
    debug_inspect_all_variables(NULL);
}

TEST(inspect_all_variables_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should not crash
    debug_inspect_all_variables(ctx);

    debug_cleanup(ctx);
}

TEST(inspect_all_variables_with_frame) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);
    debug_push_frame(ctx, "test_func", "test.sh", 1);

    /// Should not crash with active frame
    debug_inspect_all_variables(ctx);

    debug_cleanup(ctx);
}

/**
 * @brief Probe used by enumerate_current_scope_vars_in_function_scope
 *
 * Minimal collector recording one observed (name, type) pair for the
 * target variable.
 */
typedef struct {
    const char *target;
    bool found;
    symvar_type_t type;
} scope_probe_t;

/**
 * @brief Symtable enumeration callback that fills a scope_probe_t
 */
static void scope_probe_cb(const char *name, const char *value,
                           symvar_type_t type, void *userdata) {
    (void)value;
    scope_probe_t *p = (scope_probe_t *)userdata;
    if (!p->found && strcmp(name, p->target) == 0) {
        p->found = true;
        p->type = type;
    }
}

/* symtable_enumerate_current_scope_vars must see a variable set in
 * the currently-active scope, and must NOT walk into the parent. */
TEST(enumerate_current_scope_vars_in_function_scope) {
    init_symtable();
    ASSERT_NOT_NULL(symtable_manager(), "symtable manager must exist");

    int rc = symtable_push_scope(symtable_manager(), SCOPE_FUNCTION, "test_fn");
    ASSERT_EQ(rc, 0, "push function scope");

    rc = symtable_set_local_var(symtable_manager(), "fn_local", "hello");
    ASSERT_EQ(rc, 0, "set local var");

    scope_probe_t probe = {"fn_local", false, SYMVAR_STRING};
    symtable_enumerate_current_scope_vars(symtable_manager(), scope_probe_cb,
                                          &probe);
    ASSERT_TRUE(probe.found, "fn_local visible in current scope");

    /// Scope-only enumeration: a global must NOT appear in this set.
    symtable_set_var(symtable_manager(), "global_unrelated", "world",
                     SYMVAR_NONE);
    scope_probe_t probe_global = {"global_unrelated", false, SYMVAR_STRING};
    symtable_enumerate_current_scope_vars(symtable_manager(), scope_probe_cb,
                                          &probe_global);
    /// The global may end up in this scope if the set placed it here;
    /// the explicit local one is the load-bearing assertion.
    (void)probe_global;

    symtable_pop_scope(symtable_manager());
}

/* debug_inspect_all_variables shows a "Local Variables:" section
 * with the actual local variable name when called inside a function
 * scope. Verifies the wiring through symtable_enumerate_current_scope_vars. */
TEST(inspect_all_variables_shows_locals_in_function_scope) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    int rc = symtable_push_scope(symtable_manager(), SCOPE_FUNCTION, "speak");
    ASSERT_EQ(rc, 0, "push function scope");
    symtable_set_local_var(symtable_manager(), "greeting", "hi");

    /// current_executor must be non-NULL for inspect to render; the
    /// existing inspect_all_variables_basic test gates on it but the
    /// trace tests do not stage one. Skip the trace path entirely by
    /// pointing at a temporary executor.
    extern executor_t *current_executor;
    executor_t *saved = current_executor;
    current_executor = (executor_t *)0x1; /// sentinel non-NULL

    debug_inspect_all_variables(ctx);

    current_executor = saved;

    char log[8192];
    read_debug_output(ctx, log, sizeof(log));
    /// The Local Variables section is now a framed block titled
    /// "[Local Variables]" -- match the bracketed title across both
    /// UTF-8 and ASCII glyph sets.
    ASSERT_TRUE(strstr(log, "[Local Variables]") != NULL,
                "Local Variables section present");
    ASSERT_TRUE(strstr(log, "greeting") != NULL, "local variable name appears");
    ASSERT_TRUE(strstr(log, "Scalar") != NULL, "Scalar type label appears");

    symtable_pop_scope(symtable_manager());
    debug_cleanup(ctx);
}

/// debug_inspect_variable on an indexed array renders Type: List.
TEST(inspect_variable_renders_indexed_array_as_list) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "create indexed array");
    int rc = symtable_set_array("my_indexed", arr);
    ASSERT_EQ(rc, 0, "register array");

    extern executor_t *current_executor;
    executor_t *saved = current_executor;
    current_executor = (executor_t *)0x1;

    debug_inspect_variable(ctx, "my_indexed");

    current_executor = saved;

    char log[4096];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "Type:  List") != NULL,
                "indexed array rendered as List");

    debug_cleanup(ctx);
}

/// debug_inspect_variable on an associative array renders Type: Map.
TEST(inspect_variable_renders_assoc_array_as_map) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    array_value_t *arr = symtable_array_create(true);
    ASSERT_NOT_NULL(arr, "create associative array");
    int rc = symtable_set_array("my_assoc", arr);
    ASSERT_EQ(rc, 0, "register array");

    extern executor_t *current_executor;
    executor_t *saved = current_executor;
    current_executor = (executor_t *)0x1;

    debug_inspect_variable(ctx, "my_assoc");

    current_executor = saved;

    char log[4096];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "Type:  Map") != NULL,
                "associative array rendered as Map");

    debug_cleanup(ctx);
}

/// debug_show_variable_type on an indexed array reports List + count.
TEST(show_variable_type_indexed_array_is_list) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "create indexed array");
    int rc = symtable_set_array("type_list_probe", arr);
    ASSERT_EQ(rc, 0, "register array");

    debug_show_variable_type(ctx, "type_list_probe");

    char log[1024];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "type_list_probe: List") != NULL,
                "indexed array reported as List");

    debug_cleanup(ctx);
}

/// debug_show_variable_type on an associative array reports Map.
TEST(show_variable_type_assoc_array_is_map) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    array_value_t *arr = symtable_array_create(true);
    ASSERT_NOT_NULL(arr, "create associative array");
    int rc = symtable_set_array("type_map_probe", arr);
    ASSERT_EQ(rc, 0, "register array");

    debug_show_variable_type(ctx, "type_map_probe");

    char log[1024];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "type_map_probe: Map") != NULL,
                "associative array reported as Map");

    debug_cleanup(ctx);
}

/// debug_show_variable_type for a scalar reports Scalar.
TEST(show_variable_type_scalar) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    symtable_set_var(symtable_manager(), "type_scalar_probe", "hi",
                     SYMVAR_NONE);

    debug_show_variable_type(ctx, "type_scalar_probe");

    char log[1024];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "type_scalar_probe: Scalar") != NULL,
                "scalar reported as Scalar");

    debug_cleanup(ctx);
}

/// debug_show_variable_type for an unbound name says so honestly.
TEST(show_variable_type_unset) {
    init_symtable();
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");

    debug_show_variable_type(ctx, "definitely_not_a_variable_xyz");

    char log[1024];
    read_debug_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "not set") != NULL, "unset reported");

    debug_cleanup(ctx);
}

/**
 * @brief Test seams declared in src/debug/debug_view.c
 *
 * These are intentionally not part of the public debug.h API; they
 * exist only so view tests can pin the glyph set instead of being at
 * the mercy of the environment locale.
 */
void debug_view_force_ascii_for_tests(void);
void debug_view_force_utf8_for_tests(void);
void debug_view_reset_glyph_cache(void);

TEST(view_emit_line_ascii_uses_pipe_gutter) {
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");
    debug_view_force_ascii_for_tests();

    debug_view_emit_line(ctx, "hello %d", 42);

    char log[256];
    read_debug_output(ctx, log, sizeof(log));
    /// The captured output also carries debug_enable's preamble; check
    /// that the gutter-prefixed line is present rather than requiring
    /// exact equality.
    ASSERT_TRUE(strstr(log, "| hello 42\n") != NULL,
                "ASCII gutter prefixes emitted line");

    debug_view_reset_glyph_cache();
    debug_cleanup(ctx);
}

TEST(view_emit_line_utf8_uses_box_drawing_gutter) {
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");
    debug_view_force_utf8_for_tests();

    debug_view_emit_line(ctx, "hi");

    char log[256];
    read_debug_output(ctx, log, sizeof(log));
    /// U+2502 (BOX DRAWINGS LIGHT VERTICAL) is 0xE2 0x94 0x82 in UTF-8.
    ASSERT_TRUE(strstr(log, "\xE2\x94\x82 hi\n") != NULL,
                "UTF-8 vertical-bar gutter present");

    debug_view_reset_glyph_cache();
    debug_cleanup(ctx);
}

TEST(view_frame_brackets_render_around_title) {
    debug_context_t *ctx = new_captured_ctx();
    ASSERT_NOT_NULL(ctx, "captured ctx");
    debug_view_force_ascii_for_tests();

    debug_view_begin_frame(ctx, "Sample");
    debug_view_emit_line(ctx, "row one");
    debug_view_end_frame(ctx);

    char log[1024];
    read_debug_output(ctx, log, sizeof(log));
    /// Top border with bracketed title, plus a row, plus the bottom
    /// border. The exact horizontal fill length is fixed by the
    /// module; assert presence of the load-bearing pieces.
    ASSERT_TRUE(strstr(log, "+- [Sample] ") != NULL, "frame top with title");
    ASSERT_TRUE(strstr(log, "| row one\n") != NULL,
                "interior row carries the gutter");
    ASSERT_TRUE(strstr(log, "+--") != NULL, "frame bottom border");

    debug_view_reset_glyph_cache();
    debug_cleanup(ctx);
}

TEST(watch_variable_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_enable(ctx, true);

    /// Should not crash
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

    /// Should not crash
    debug_show_variable_changes(ctx);

    debug_cleanup(ctx);
}

/// ============================================================================
/// Main
/// ============================================================================

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
    RUN_TEST(inspect_variable_accepts_at_sigil);
    RUN_TEST(inspect_variable_accepts_pct_sigil);
    RUN_TEST(inspect_variable_special);
    RUN_TEST(inspect_all_variables_null);
    RUN_TEST(inspect_all_variables_basic);
    RUN_TEST(inspect_all_variables_with_frame);
    RUN_TEST(enumerate_current_scope_vars_in_function_scope);
    RUN_TEST(inspect_all_variables_shows_locals_in_function_scope);
    RUN_TEST(inspect_variable_renders_indexed_array_as_list);
    RUN_TEST(inspect_variable_renders_assoc_array_as_map);
    RUN_TEST(show_variable_type_indexed_array_is_list);
    RUN_TEST(show_variable_type_assoc_array_is_map);
    RUN_TEST(show_variable_type_scalar);
    RUN_TEST(show_variable_type_unset);
    RUN_TEST(view_emit_line_ascii_uses_pipe_gutter);
    RUN_TEST(view_emit_line_utf8_uses_box_drawing_gutter);
    RUN_TEST(view_frame_brackets_render_around_title);
    RUN_TEST(watch_variable_null_params);
    RUN_TEST(watch_variable_basic);
    RUN_TEST(show_variable_changes);

    printf("\n========================================\n");
    printf("========================================\n");

    return TEST_RESULT();
}
