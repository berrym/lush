/**
 * @file test_compdef_source.c
 * @brief Tests for the LLE compdef-binding completion source.
 *
 * Covers:
 *   - compdef_source_applicable: false with no executor, false with no
 *     binding, true with a binding for the current command_name
 *   - compdef_source_generate: silent no-op when no executor / no
 *     binding / undefined function; honors push/pop discipline of
 *     active_comp_result; invokes a real shell function and surfaces
 *     the candidates emitted by compadd
 *
 * Tests construct a real executor and define real shell functions via
 * executor_execute_command_line so the function-invocation path is
 * exercised end-to-end. The active_comp_result accumulator is a real
 * lle_completion_result_t built with the standard LLE memory pool
 * sentinel ((lle_memory_pool_t *)1) per the project pattern.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compdef.h"
#include "executor.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern void init_symtable(void);

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* compdef_source.c exposes these via external linkage; declared here
 * directly rather than added to compdef_source.h to keep the public
 * header narrow (the production caller only ever uses
 * compdef_source_init). */
bool compdef_source_applicable(void *user_data,
                               const struct lle_word_context *context);
lle_result_t compdef_source_generate(void *user_data,
                                     const struct lle_word_context *context,
                                     lle_completion_result_t *result);

static lle_memory_pool_t *test_pool = (lle_memory_pool_t *)1;
static executor_t *test_exec = NULL;
static lle_completion_result_t *test_result = NULL;

static void setup_env(void) {
    init_compdef_bindings();
    test_exec = executor_new();
    current_executor = test_exec;
    lle_completion_result_create(test_pool, 16, &test_result);
}

static void teardown_env(void) {
    if (test_result) {
        lle_completion_result_free(test_result);
        test_result = NULL;
    }
    current_executor = NULL;
    if (test_exec) {
        executor_free(test_exec);
        test_exec = NULL;
    }
    free_compdef_bindings();
}

static void make_context(lle_word_context_t *ctx, const char *cmd,
                         const char *cur_prefix) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->command_name = (char *)cmd;
    ctx->dequoted_filename_prefix = (char *)cur_prefix;
    ctx->arg_index = 1;
}

static int find_candidate(const char *text) {
    if (!test_result) {
        return -1;
    }
    for (size_t i = 0; i < test_result->count; i++) {
        if (test_result->items[i].text &&
            strcmp(test_result->items[i].text, text) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ============================================================================
 * compdef_source_applicable
 * ============================================================================
 */

TEST(applicable_returns_false_with_null_context) {
    setup_env();
    ASSERT_FALSE(compdef_source_applicable(NULL, NULL),
                 "NULL context is never applicable");
    teardown_env();
}

TEST(applicable_returns_false_with_null_command_name) {
    setup_env();
    lle_word_context_t ctx;
    make_context(&ctx, NULL, NULL);
    ASSERT_FALSE(compdef_source_applicable(NULL, &ctx),
                 "context without a command_name is not applicable");
    teardown_env();
}

TEST(applicable_returns_false_when_no_binding) {
    setup_env();
    lle_word_context_t ctx;
    make_context(&ctx, "uncoveted", "");
    ASSERT_FALSE(compdef_source_applicable(NULL, &ctx),
                 "command with no compdef binding is not applicable");
    teardown_env();
}

TEST(applicable_returns_true_when_binding_exists) {
    setup_env();
    compdef_set("git", "_git");
    lle_word_context_t ctx;
    make_context(&ctx, "git", "che");
    ASSERT_TRUE(compdef_source_applicable(NULL, &ctx),
                "command with a compdef binding is applicable");
    teardown_env();
}

/* ============================================================================
 * compdef_source_generate
 * ============================================================================
 */

TEST(generate_no_executor_returns_success_silently) {
    setup_env();
    executor_t *saved = current_executor;
    current_executor = NULL;
    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS,
                "generate with no current_executor returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 0u, "no candidates accumulated");
    current_executor = saved;
    teardown_env();
}

TEST(generate_no_binding_returns_success_silently) {
    setup_env();
    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS,
                "generate with no binding returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 0u, "no candidates accumulated");
    teardown_env();
}

TEST(generate_undefined_function_returns_success_silently) {
    setup_env();
    /// Binding exists but the function has never been defined. This
    /// is the autoload-style use case: register early, define later.
    compdef_set("git", "_git_never_defined");
    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS,
                "generate with undefined function returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 0u, "no candidates accumulated");
    teardown_env();
}

TEST(generate_runs_function_and_surfaces_positional_candidates) {
    setup_env();
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);
    compdef_set("git", "_subs");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 3u, "three candidates accumulated");
    ASSERT_TRUE(find_candidate("checkout") >= 0, "checkout candidate present");
    ASSERT_TRUE(find_candidate("branch") >= 0, "branch candidate present");
    ASSERT_TRUE(find_candidate("merge") >= 0, "merge candidate present");
    teardown_env();
}

TEST(generate_passes_command_and_prefix_via_positional_params) {
    setup_env();
    /// The function emits its received positional params as candidates;
    /// reading them back through the accumulator verifies the argv
    /// layout (argv[1]=cmd, argv[2]=cur, argv[3]=prev) flows through
    /// executor_run_function correctly.
    executor_execute_command_line(test_exec,
                                  "_echo_args() { compadd \"$1\" \"$2\"; }", 1);
    compdef_set("git", "_echo_args");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "che");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 2u, "two candidates accumulated");
    ASSERT_TRUE(find_candidate("git") >= 0, "$1 carried the command name");
    ASSERT_TRUE(find_candidate("che") >= 0, "$2 carried the current prefix");
    teardown_env();
}

TEST(generate_restores_active_comp_result) {
    setup_env();
    executor_execute_command_line(test_exec, "_noop() { compadd x; }", 1);
    compdef_set("git", "_noop");

    /// Place a sentinel value in active_comp_result before invocation;
    /// generate must overwrite during the call and restore on exit.
    void *sentinel = (void *)0xDEADBEEFul;
    test_exec->active_comp_result = sentinel;

    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    (void)compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(test_exec->active_comp_result == sentinel,
                "active_comp_result restored to prior value after the call");

    test_exec->active_comp_result = NULL;
    teardown_env();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /// The global symbol table backs executor_new(); without it,
    /// executor_new returns NULL and the function-invocation tests
    /// can't proceed. Mirror test_executor.c's main(), which calls
    /// init_symtable() once before running tests.
    init_symtable();

    printf("Running compdef-source tests\n");
    printf("============================\n");

    printf("\nApplicable:\n");
    RUN_TEST(applicable_returns_false_with_null_context);
    RUN_TEST(applicable_returns_false_with_null_command_name);
    RUN_TEST(applicable_returns_false_when_no_binding);
    RUN_TEST(applicable_returns_true_when_binding_exists);

    printf("\nGenerate:\n");
    RUN_TEST(generate_no_executor_returns_success_silently);
    RUN_TEST(generate_no_binding_returns_success_silently);
    RUN_TEST(generate_undefined_function_returns_success_silently);
    RUN_TEST(generate_runs_function_and_surfaces_positional_candidates);
    RUN_TEST(generate_passes_command_and_prefix_via_positional_params);
    RUN_TEST(generate_restores_active_comp_result);

    return TEST_RESULT();
}
