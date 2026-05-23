/**
 * @file test_debug_integration.c
 * @brief The debugger integration gate -- PHILOSOPHY.md section 7
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Every other debug test calls a debug_*.c function in isolation. None
 * of them answer the question PHILOSOPHY.md section 7 actually asks:
 * when a real script runs through the real executor, does the debugger
 * still see it? This file does. It drives executor_execute_command_line
 * with a live debug context and asserts that breakpoints fire, stay
 * silent when they should, honour the script-execution gate, accumulate
 * hits, and that execution degrades cleanly when no terminal is present.
 *
 * This is the gate the section-7 rule names: "an integration test suite
 * that drives the debugger over representative scripts." When a core
 * change outpaces the debugger, a test here goes red.
 *
 * Scope, honestly stated -- the gate starts against the currently-
 * working surface and grows per debugger tier:
 *
 *  - Breakpoints match node->loc.line, the parser-recorded absolute
 *    1-based source line, so they land correctly inside control
 *    structures and loop bodies too -- see
 *    breakpoint_in_loop_body_fires_each_iteration. The line is
 *    absolute as long as the executed batch is parsed with the right
 *    starting line, which every script-execution path now threads.
 *  - Conditional breakpoints are not exercised here: debug_evaluate_
 *    condition() is still a documented TODO stub that returns true for
 *    every comparison. Asserting against a stub would test nothing.
 *    Conditional coverage arrives with a real evaluator.
 *  - Type-aware variable inspection at a break is debugger Tier 1
 *    (task #13); its assertions land here when that work does.
 */

#include "debug.h"
#include "executor.h"
#include "symtable.h"
#include "test_framework.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Fixtures
 * ============================================================================
 */

/**
 * @brief Build a live debug context with output captured to a tmpfile
 *
 * The capture file is owned by the context: debug_cleanup() fcloses
 * any non-stderr debug_output, so callers must not close it
 * separately.
 *
 * @return Heap-allocated, enabled debug context, or NULL on failure.
 */
static debug_context_t *debug_ctx_new_captured(void) {
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

/**
 * @brief Read the debugger's captured output into @p buf
 *
 * @param ctx Debug context with a tmpfile-backed debug_output.
 * @param buf Destination buffer (NUL-terminated on return).
 * @param buf_size Buffer capacity (including NUL).
 */
static void debug_ctx_read_output(debug_context_t *ctx, char *buf,
                                  size_t buf_size) {
    buf[0] = '\0';
    if (!ctx || !ctx->debug_output || ctx->debug_output == stderr) {
        return;
    }
    fflush(ctx->debug_output);
    rewind(ctx->debug_output);
    size_t n = fread(buf, 1, buf_size - 1, ctx->debug_output);
    buf[n] = '\0';
}

typedef struct {
    int status;
    char out[4096];
} gate_result_t;

/**
 * @brief Run @p script through @p exec with @p ctx as the global debug
 *        context
 *
 * When @p with_script_context is true the executor is placed in
 * script-execution mode (file "gate.sh", first line 1) -- the state
 * the breakpoint check in execute_node is gated on. Script stdout is
 * captured so tests can prove execution continued past a break.
 *
 * @param exec Executor to drive (caller owns lifetime).
 * @param ctx Debug context (installed as g_debug_context for the
 *            duration of the call, then cleared).
 * @param script Shell source to execute.
 * @param with_script_context Set script-execution mode for the call.
 * @return Exit status plus captured stdout.
 */
static gate_result_t run_under_debugger(executor_t *exec,
                                        debug_context_t *ctx,
                                        const char *script,
                                        bool with_script_context) {
    gate_result_t r = {0};

    g_debug_context = ctx;
    if (with_script_context) {
        executor_set_script_context(exec, "gate.sh");
    }

    FILE *out_tmp = tmpfile();
    int saved_out = dup(STDOUT_FILENO);
    fflush(stdout);
    if (out_tmp) {
        dup2(fileno(out_tmp), STDOUT_FILENO);
    }

    r.status = executor_execute_command_line(exec, script, 1);

    fflush(stdout);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
    if (out_tmp) {
        rewind(out_tmp);
        size_t n = fread(r.out, 1, sizeof(r.out) - 1, out_tmp);
        r.out[n] = '\0';
        fclose(out_tmp);
    }

    if (with_script_context) {
        executor_clear_script_context(exec);
    }
    g_debug_context = NULL;
    return r;
}

/**
 * @brief Walk the breakpoint list for the breakpoint with this id
 *
 * @param ctx Debug context.
 * @param id Breakpoint id (as returned by debug_add_breakpoint).
 * @return The breakpoint, or NULL if not found.
 */
static breakpoint_t *find_breakpoint(debug_context_t *ctx, int id) {
    for (breakpoint_t *bp = ctx->breakpoints; bp; bp = bp->next) {
        if (bp->id == id) {
            return bp;
        }
    }
    return NULL;
}

/* The flat three-command probe script. Under the line counter, command
 * one is line 1, command two line 2, command three line 3. */
#define FLAT_SCRIPT "echo one\necho two\necho three"

/* ============================================================================
 * Breakpoint firing during real execution
 * ============================================================================
 */

TEST(breakpoint_fires_at_reached_line) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int id = debug_add_breakpoint(ctx, "gate.sh", 2, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    breakpoint_t *bp = find_breakpoint(ctx, id);
    ASSERT_NOT_NULL(bp, "breakpoint still present");
    ASSERT_EQ(bp->hit_count, 1, "breakpoint at the reached line hit once");

    char log[4096];
    debug_ctx_read_output(ctx, log, sizeof(log));
    ASSERT_TRUE(strstr(log, "BREAKPOINT HIT") != NULL,
                "debugger reported the hit");

    debug_cleanup(ctx);
    executor_free(exec);
}

TEST(breakpoint_silent_at_unreached_line) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    /* Line 99 is never reached by a three-line script. */
    int id = debug_add_breakpoint(ctx, "gate.sh", 99, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    breakpoint_t *bp = find_breakpoint(ctx, id);
    ASSERT_NOT_NULL(bp, "breakpoint still present");
    ASSERT_EQ(bp->hit_count, 0, "unreached breakpoint never fires");

    debug_cleanup(ctx);
    executor_free(exec);
}

TEST(disabled_breakpoint_does_not_fire) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int id = debug_add_breakpoint(ctx, "gate.sh", 2, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");
    ASSERT_TRUE(debug_enable_breakpoint(ctx, id, false), "breakpoint disabled");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    breakpoint_t *bp = find_breakpoint(ctx, id);
    ASSERT_NOT_NULL(bp, "breakpoint still present");
    ASSERT_EQ(bp->hit_count, 0, "disabled breakpoint never fires");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* The breakpoint check in execute_node is gated on in_script_execution.
 * Without script context the executor is in command-line mode and the
 * debugger must stay out of the way entirely. */
TEST(breakpoint_requires_script_context) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int id = debug_add_breakpoint(ctx, "gate.sh", 2, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, false);

    breakpoint_t *bp = find_breakpoint(ctx, id);
    ASSERT_NOT_NULL(bp, "breakpoint still present");
    ASSERT_EQ(bp->hit_count, 0,
              "no breakpoint fires outside script execution");

    debug_cleanup(ctx);
    executor_free(exec);
}

TEST(multiple_breakpoints_fire_independently) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int first = debug_add_breakpoint(ctx, "gate.sh", 1, NULL);
    int third = debug_add_breakpoint(ctx, "gate.sh", 3, NULL);
    ASSERT_TRUE(first >= 0 && third >= 0, "both breakpoints added");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    ASSERT_EQ(find_breakpoint(ctx, first)->hit_count, 1,
              "breakpoint on line 1 hit once");
    ASSERT_EQ(find_breakpoint(ctx, third)->hit_count, 1,
              "breakpoint on line 3 hit once");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* Hit counts persist across runs that share a context. */
TEST(breakpoint_hit_count_accumulates) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int id = debug_add_breakpoint(ctx, "gate.sh", 2, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);
    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    ASSERT_EQ(find_breakpoint(ctx, id)->hit_count, 2,
              "hit count accumulates over two runs");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* node->loc.line is the real source line, so a breakpoint inside a loop
 * body lands correctly and re-fires every iteration. The old command-
 * ordinal counter needed a dedicated loop_body_start_line workaround for
 * this; node->loc.line needs none. This is the headline #12 win. */
TEST(breakpoint_in_loop_body_fires_each_iteration) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    /* Lines: 1 `for i in a b c` / 2 `do` / 3 `echo $i` / 4 `done`.
     * Line 3 runs once per loop iteration -- three times. */
    int id = debug_add_breakpoint(ctx, "gate.sh", 3, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    run_under_debugger(exec, ctx, "for i in a b c\ndo\necho $i\ndone", true);

    ASSERT_EQ(find_breakpoint(ctx, id)->hit_count, 3,
              "loop-body breakpoint fired once per iteration");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* ============================================================================
 * Execution survives the break -- the section-7 anti-hang guarantee
 * ============================================================================
 */

/* A breakpoint fires mid-script with no terminal attached. The debug
 * prompt (lle_readline_no_history) gets EOF and continues. Execution
 * must reach every command after the break -- no hang, no abort. */
TEST(execution_continues_past_breakpoint) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    int id = debug_add_breakpoint(ctx, "gate.sh", 2, NULL);
    ASSERT_TRUE(id >= 0, "breakpoint added");

    gate_result_t r = run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    ASSERT_TRUE(strstr(r.out, "one") != NULL, "command before break ran");
    ASSERT_TRUE(strstr(r.out, "two") != NULL, "command at break ran");
    ASSERT_TRUE(strstr(r.out, "three") != NULL,
                "command after break ran -- execution resumed");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* ============================================================================
 * Step mode reaches execution
 * ============================================================================
 */

/* With step_mode set, the breakpoint check takes its step branch on the
 * next executed node even with no breakpoint registered. With no
 * terminal the debug prompt returns EOF, which clears step_mode -- so
 * the step fires once and execution then runs free. This proves the
 * step path is wired into execute_node, not just into the debug API. */
TEST(step_mode_breaks_at_next_node) {
    executor_t *exec = executor_new();
    debug_context_t *ctx = debug_ctx_new_captured();
    ASSERT_NOT_NULL(exec, "executor_new");
    ASSERT_NOT_NULL(ctx, "debug_ctx_new_captured");

    ctx->step_mode = true;

    run_under_debugger(exec, ctx, FLAT_SCRIPT, true);

    char log[4096];
    debug_ctx_read_output(ctx, log, sizeof(log));
    /* The framed step banner -- "[STEP]" is the title token, reliable
     * across the UTF-8 and ASCII glyph sets. */
    ASSERT_TRUE(strstr(log, "[STEP]") != NULL,
                "step branch reached during execution");
    ASSERT_FALSE(ctx->step_mode,
                 "EOF at the debug prompt cleared step mode");

    debug_cleanup(ctx);
    executor_free(exec);
}

/* ============================================================================
 * Runner
 * ============================================================================
 */

int main(void) {
    printf("\n=== Debugger Integration Gate (PHILOSOPHY section 7) ===\n\n");

    /* Initialize the global symbol table -- executor_new() needs it. */
    init_symtable();

    printf("Breakpoint firing during execution:\n");
    RUN_TEST(breakpoint_fires_at_reached_line);
    RUN_TEST(breakpoint_silent_at_unreached_line);
    RUN_TEST(disabled_breakpoint_does_not_fire);
    RUN_TEST(breakpoint_requires_script_context);
    RUN_TEST(multiple_breakpoints_fire_independently);
    RUN_TEST(breakpoint_hit_count_accumulates);
    RUN_TEST(breakpoint_in_loop_body_fires_each_iteration);

    printf("\nExecution survives the break:\n");
    RUN_TEST(execution_continues_past_breakpoint);

    printf("\nStep mode:\n");
    RUN_TEST(step_mode_breaks_at_next_node);

    return TEST_RESULT();
}
