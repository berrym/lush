/**
 * @file test_compdef_e2e.c
 * @brief End-to-end integration: source-manager dispatch -> compdef-bound
 *        function -> compadd -> candidate surfaces in the LLE result.
 *
 * Tests the full completion pipeline that gets driven on TAB:
 *
 *   lle_completion_system_generate(system, "git che", cursor, &result)
 *     -> lle_word_context_analyze       (figures out command_name, prefix)
 *     -> lle_source_manager_query       (fans out across registered sources)
 *     -> compdef source's is_applicable (checks compdef_lookup)
 *     -> compdef source's generate      (sets active_comp_result, runs fn)
 *     -> bin_compadd                    (appends via completion_bridge_add)
 *     -> lle_completion_result_add_with_description
 *
 * Distinct from tests/unit/test_compdef_source.c, which exercises the
 * source's generate callback directly without going through the
 * source-manager dispatch. This file proves the dispatch wiring is
 * intact: compdef_source_init really did register with the global
 * custom registry, and a freshly-created completion system picks it
 * up via its custom meta-source.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compdef.h"
#include "compdef_source.h"
#include "config.h"
#include "executor.h"
#include "lle/completion/completion_system.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/custom_source.h"
#include "lle/memory_management.h"
#include "lush_memory_pool.h"
#include "test_framework.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern void init_symtable(void);
extern config_values_t config;

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// The completion system's post-dispatch steps (menu_state_create, etc.)
/// exercise more pool surface than the unit-test sentinel can handle, so
/// this integration test wraps a real lush_memory_pool with the LLE
/// pool type the editor uses in production.
static lle_memory_pool_t *test_pool = NULL;
static executor_t *test_exec = NULL;
static lle_completion_system_t *test_system = NULL;

/// Defined in test_compdef_e2e_stubs.c. Set once at global_setup so
/// get_global_executor() returns it for the whole test run.
extern executor_t *test_integration_executor;

static void global_setup(void) {
    /// Heavy LLE infrastructure (memory pools, completion system,
    /// custom-source registry) is created once for the whole binary
    /// run rather than per-test. Per-test churn on these was
    /// unstable; production lush also creates them once at startup.
    lush_pool_init(NULL);
    lle_memory_pool_create_from_lush(&test_pool, global_memory_pool,
                                     LLE_POOL_BUFFER);
    lle_completion_system_create(test_pool, &test_system);
    lle_custom_source_init(test_system->source_manager, test_pool);
    compdef_source_init();
    init_compdef_bindings();
    test_exec = executor_new();
    test_integration_executor = test_exec;
    current_executor = test_exec;
}

static void global_teardown(void) {
    lle_custom_source_shutdown();
    if (test_system) {
        lle_completion_system_destroy(test_system);
        test_system = NULL;
    }
    if (test_pool) {
        lle_memory_pool_destroy(test_pool);
        test_pool = NULL;
    }
    current_executor = NULL;
    test_integration_executor = NULL;
    if (test_exec) {
        executor_free(test_exec);
        test_exec = NULL;
    }
    free_compdef_bindings();
}

/// Per-test reset: clear bindings and any user-defined functions on
/// the executor so each test starts with a known-empty state without
/// rebuilding the heavy infrastructure.
static void per_test_reset(void) {
    /// Wipe bindings table contents by destroying + re-creating.
    free_compdef_bindings();
    init_compdef_bindings();
    /// Drop any functions defined in a prior test so the next
    /// executor_execute_command_line gets a clean slate.
    executor_execute_command_line(
        test_exec, "unset -f _subs _echo1 _never_defined 2>/dev/null; :", 1);
}

static int find_candidate(lle_completion_result_t *result, const char *text) {
    if (!result) {
        return -1;
    }
    for (size_t i = 0; i < result->count; i++) {
        if (result->items[i].text && strcmp(result->items[i].text, text) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ============================================================================
 * Tests
 * ============================================================================
 */

TEST(system_dispatch_fires_compdef_source) {
    per_test_reset();
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);
    compdef_set("git", "_subs");

    lle_completion_result_t *result = NULL;
    /// "git " — cursor at position 4 (just past the space), starting
    /// the next word. command_name = "git", prefix empty. compdef
    /// fires, _subs emits three candidates, all should land in the
    /// result because the empty prefix matches everything.
    lle_result_t r =
        lle_completion_system_generate(test_system, "git ", 4, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_NOT_NULL(result, "result is allocated");
    ASSERT_TRUE(find_candidate(result, "checkout") >= 0,
                "checkout surfaced through full dispatch");
    ASSERT_TRUE(find_candidate(result, "branch") >= 0,
                "branch surfaced through full dispatch");
    ASSERT_TRUE(find_candidate(result, "merge") >= 0,
                "merge surfaced through full dispatch");
}

TEST(system_dispatch_skips_compdef_when_no_binding) {
    per_test_reset();
    /// No compdef binding for "uncoveted". The compdef source's
    /// is_applicable returns false; generate is never called. Other
    /// built-in sources may still surface candidates (files, etc.) so
    /// we only assert that none of the candidates _subs would have
    /// emitted are present.
    executor_execute_command_line(
        test_exec, "_subs() { compadd checkout branch merge; }", 1);

    lle_completion_result_t *result = NULL;
    lle_result_t r =
        lle_completion_system_generate(test_system, "uncoveted ", 10, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_TRUE(find_candidate(result, "checkout") < 0,
                "compdef source did not fire");
    ASSERT_TRUE(find_candidate(result, "branch") < 0,
                "compdef source did not fire");
}

TEST(system_dispatch_with_compdef_to_undefined_function) {
    per_test_reset();
    /// Binding to a function that has never been defined. The
    /// compdef source returns SUCCESS with 0 candidates rather than
    /// erroring; built-in sources still fire and the result is well
    /// formed.
    compdef_set("git", "_never_defined");

    lle_completion_result_t *result = NULL;
    lle_result_t r =
        lle_completion_system_generate(test_system, "git ", 4, &result);
    ASSERT_TRUE(r == LLE_SUCCESS,
                "system_generate succeeded even though the bound function "
                "is not defined");
    ASSERT_TRUE(find_candidate(result, "checkout") < 0,
                "no compdef-emitted candidate is present");
}

TEST(system_dispatch_filters_candidates_by_typed_prefix) {
    per_test_reset();
    /// Function emits its full candidate set blindly; the bridge
    /// filters down to the single one that matches the typed prefix.
    /// This is the user-facing fix: `git c<TAB>` shows only checkout,
    /// not all six git subcommands.
    executor_execute_command_line(
        test_exec,
        "_subs() { compadd checkout branch merge clone commit pull; }", 1);
    compdef_set("git", "_subs");

    lle_completion_result_t *result = NULL;
    /// Buffer "git c", cursor at position 5: command_name = "git",
    /// dequoted_filename_prefix = "c". Only candidates starting with
    /// "c" should survive the bridge filter.
    lle_result_t r =
        lle_completion_system_generate(test_system, "git c", 5, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_NOT_NULL(result, "result is allocated");
    ASSERT_TRUE(find_candidate(result, "checkout") >= 0,
                "checkout survived the c-prefix filter");
    ASSERT_TRUE(find_candidate(result, "clone") >= 0,
                "clone survived the c-prefix filter");
    ASSERT_TRUE(find_candidate(result, "commit") >= 0,
                "commit survived the c-prefix filter");
    ASSERT_TRUE(find_candidate(result, "branch") < 0,
                "branch filtered out by c-prefix");
    ASSERT_TRUE(find_candidate(result, "merge") < 0,
                "merge filtered out by c-prefix");
    ASSERT_TRUE(find_candidate(result, "pull") < 0,
                "pull filtered out by c-prefix");
}

TEST(system_dispatch_substring_mode_admits_internal_match) {
    per_test_reset();
    /// Substring match_mode admits candidates where the typed
    /// prefix appears anywhere inside the candidate, not just at
    /// the start. Buffer "git eck" -> prefix "eck" -> only
    /// "checkout" contains "eck", so only checkout survives.
    completion_match_mode_t saved = config.completion_match_mode;
    config.completion_match_mode = COMPLETION_MATCH_SUBSTRING;

    executor_execute_command_line(
        test_exec,
        "_subs() { compadd checkout branch merge clone commit pull; }", 1);
    compdef_set("git", "_subs");

    lle_completion_result_t *result = NULL;
    lle_result_t r =
        lle_completion_system_generate(test_system, "git eck", 7, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_NOT_NULL(result, "result is allocated");
    ASSERT_TRUE(find_candidate(result, "checkout") >= 0,
                "checkout contains 'eck' as a substring");
    ASSERT_TRUE(find_candidate(result, "branch") < 0,
                "branch does not contain 'eck'");
    ASSERT_TRUE(find_candidate(result, "merge") < 0,
                "merge does not contain 'eck'");

    config.completion_match_mode = saved;
}

TEST(system_dispatch_fuzzy_mode_admits_subsequence) {
    per_test_reset();
    /// Fuzzy match_mode admits subsequence matches. Buffer
    /// "git cko" -> prefix "cko" -> "checkout" matches the
    /// subsequence (c then k then o appear in order).
    completion_match_mode_t saved_mode = config.completion_match_mode;
    int saved_threshold = config.completion_threshold;
    config.completion_match_mode = COMPLETION_MATCH_FUZZY;
    config.completion_threshold = 1;

    executor_execute_command_line(
        test_exec,
        "_subs() { compadd checkout branch merge clone commit pull; }", 1);
    compdef_set("git", "_subs");

    lle_completion_result_t *result = NULL;
    lle_result_t r =
        lle_completion_system_generate(test_system, "git cko", 7, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_NOT_NULL(result, "result is allocated");
    ASSERT_TRUE(find_candidate(result, "checkout") >= 0,
                "checkout is a subsequence-match for 'cko'");
    /// `merge` has no 'k' so it can never subsequence-match;
    /// `branch`'s subsequence is c-?-? but order is wrong (no k).
    ASSERT_TRUE(find_candidate(result, "merge") < 0,
                "merge fails the subsequence gate (no k)");
    ASSERT_TRUE(find_candidate(result, "branch") < 0,
                "branch fails the subsequence gate (no k in order)");

    config.completion_match_mode = saved_mode;
    config.completion_threshold = saved_threshold;
}

TEST(system_dispatch_passes_command_name_to_function) {
    per_test_reset();
    /// The function emits its $1 (command name) as a candidate. Going
    /// through the full system_generate -> word_context_analyze path
    /// proves the command_name field is being populated correctly
    /// from the buffer text (not by our unit-test scaffolding).
    executor_execute_command_line(test_exec, "_echo1() { compadd \"$1\"; }", 1);
    compdef_set("mycmd", "_echo1");

    lle_completion_result_t *result = NULL;
    lle_result_t r =
        lle_completion_system_generate(test_system, "mycmd ", 6, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "system_generate succeeded");
    ASSERT_TRUE(find_candidate(result, "mycmd") >= 0,
                "command name flowed through to $1 inside the function");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /// executor_new() requires the global symtable manager to be
    /// initialized; mirror tests/unit/test_executor.c's main pattern.
    init_symtable();
    global_setup();

    printf("Running compdef E2E integration tests\n");
    printf("=====================================\n");
    printf("\nFull-pipeline dispatch:\n");
    RUN_TEST(system_dispatch_fires_compdef_source);
    RUN_TEST(system_dispatch_skips_compdef_when_no_binding);
    RUN_TEST(system_dispatch_with_compdef_to_undefined_function);
    RUN_TEST(system_dispatch_filters_candidates_by_typed_prefix);
    RUN_TEST(system_dispatch_substring_mode_admits_internal_match);
    RUN_TEST(system_dispatch_fuzzy_mode_admits_subsequence);
    RUN_TEST(system_dispatch_passes_command_name_to_function);

    int rc = TEST_RESULT();
    global_teardown();
    return rc;
}
