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

TEST(generate_passes_command_name_via_dollar_one) {
    setup_env();
    /// Verifies argv[1] (the command name) flows through
    /// executor_run_function to "$1" inside the bound function.
    /// Empty prefix bypasses the bridge filter so the test isolates
    /// argv forwarding.
    executor_execute_command_line(test_exec, "_echo_cmd() { compadd \"$1\"; }",
                                  1);
    compdef_set("git", "_echo_cmd");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 1u, "one candidate accumulated");
    ASSERT_TRUE(find_candidate("git") >= 0, "$1 carried the command name");
    teardown_env();
}

TEST(generate_passes_current_prefix_via_dollar_two) {
    setup_env();
    /// Verifies argv[2] (the current-word prefix) flows through to
    /// "$2". The function emits "${2}_suffix" so the candidate
    /// naturally starts with the prefix and survives the bridge
    /// filter; if "$2" were not the typed prefix, the resulting
    /// candidate would not begin with "che" and would be filtered
    /// out, producing a count of zero.
    executor_execute_command_line(
        test_exec, "_echo_prefix() { compadd \"${2}_suffix\"; }", 1);
    compdef_set("git", "_echo_prefix");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "che");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 1u, "one candidate accumulated");
    ASSERT_TRUE(find_candidate("che_suffix") >= 0,
                "$2 carried the current prefix to the function");
    teardown_env();
}

TEST(generate_filters_candidates_against_active_prefix) {
    setup_env();
    /// The user's bound function emits its entire candidate set blindly
    /// (no manual prefix filter). The bridge filters down to only those
    /// that NFC-prefix-match the current word -- prefix "c" keeps
    /// "checkout"; "branch" and "merge" are silently skipped. This is
    /// the user-facing fix for `git c<TAB>` showing all candidates
    /// instead of narrowing to commit/checkout/clone.
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);
    compdef_set("git", "_subs");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "c");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 1u,
              "only the prefix-matching candidate kept");
    ASSERT_TRUE(find_candidate("checkout") >= 0, "checkout matched prefix 'c'");
    ASSERT_TRUE(find_candidate("branch") < 0, "branch filtered out");
    ASSERT_TRUE(find_candidate("merge") < 0, "merge filtered out");
    teardown_env();
}

TEST(generate_filter_skips_all_when_no_candidate_matches) {
    setup_env();
    /// A prefix that no candidate shares produces an empty result --
    /// no spurious match, no candidate inserted as inline preview.
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);
    compdef_set("git", "_subs");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "xyz");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 0u,
              "no candidates matched the non-matching prefix");
    teardown_env();
}

TEST(generate_filter_inactive_with_empty_prefix) {
    setup_env();
    /// Empty / NULL prefix accepts every candidate (matches the
    /// first-TAB-at-word-boundary case where the user has typed
    /// nothing yet for the current word).
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);
    compdef_set("git", "_subs");

    lle_word_context_t ctx;
    make_context(&ctx, "git", "");
    lle_result_t r = compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(r == LLE_SUCCESS, "generate returns LLE_SUCCESS");
    ASSERT_EQ(test_result->count, 3u,
              "every candidate kept when the prefix is empty");
    teardown_env();
}

TEST(generate_filter_restores_prior_prefix) {
    setup_env();
    /// active_comp_prefix must be saved + restored just like
    /// active_comp_result so nested completion contexts (a function
    /// that itself triggers completion lookups) don't leak the inner
    /// prefix to the outer scope.
    executor_execute_command_line(test_exec, "_noop() { compadd x; }", 1);
    compdef_set("git", "_noop");
    const char *sentinel = "outer-prefix";
    test_exec->active_comp_prefix = sentinel;

    lle_word_context_t ctx;
    make_context(&ctx, "git", "inner");
    (void)compdef_source_generate(NULL, &ctx, test_result);
    ASSERT_TRUE(test_exec->active_comp_prefix == sentinel,
                "active_comp_prefix restored to prior value after the call");

    test_exec->active_comp_prefix = NULL;
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
    RUN_TEST(generate_passes_command_name_via_dollar_one);
    RUN_TEST(generate_passes_current_prefix_via_dollar_two);
    RUN_TEST(generate_filters_candidates_against_active_prefix);
    RUN_TEST(generate_filter_skips_all_when_no_candidate_matches);
    RUN_TEST(generate_filter_inactive_with_empty_prefix);
    RUN_TEST(generate_filter_restores_prior_prefix);
    RUN_TEST(generate_restores_active_comp_result);

    return TEST_RESULT();
}
