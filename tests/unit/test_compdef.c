/**
 * @file test_compdef.c
 * @brief Unit tests for compdef bindings + compadd builtin.
 *
 * Covers:
 *   - compdef binding table CRUD via the public API
 *   - bin_compdef argv parsing (bind / list / -d / errors)
 *   - bin_compadd outside-context error
 *   - bin_compadd accumulator behavior (positional, -d, -a, -k, --,
 *     bad flags, missing-arg)
 *
 * The active-context tests assemble a minimal executor_t with only
 * the active_comp_result field set, plus a real lle_completion_result_t
 * created via the LLE API; they avoid touching unrelated executor
 * state so the structure does not need full executor_create()
 * initialization.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "compdef.h"
#include "executor.h"
#include "lle/completion/completion_types.h"
#include "symtable.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/* The legacy ASSERT(cond, msg) macro local to other test files
 * conflicts with the framework's single-arg ASSERT(cond); re-alias to
 * the framework's two-arg form for symmetry with test_alias.c. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

static lle_memory_pool_t *test_pool = (lle_memory_pool_t *)1;
static lle_completion_result_t *test_result = NULL;
static executor_t *test_exec = NULL;

static void setup_bindings(void) { init_compdef_bindings(); }

static void teardown_bindings(void) { free_compdef_bindings(); }

static void setup_active_context(void) {
    init_compdef_bindings();
    init_symtable();
    test_exec = calloc(1, sizeof(*test_exec));
    if (!test_exec) {
        return;
    }
    lle_result_t r = lle_completion_result_create(test_pool, 16, &test_result);
    if (r != LLE_SUCCESS || !test_result) {
        free(test_exec);
        test_exec = NULL;
        return;
    }
    test_exec->active_comp_result = test_result;
    current_executor = test_exec;
}

static void teardown_active_context(void) {
    current_executor = NULL;
    if (test_result) {
        lle_completion_result_free(test_result);
        test_result = NULL;
    }
    if (test_exec) {
        free(test_exec);
        test_exec = NULL;
    }
    free_compdef_bindings();
}

static size_t result_count(void) {
    return test_result ? test_result->count : 0;
}

static const char *result_text(size_t i) {
    if (!test_result || i >= test_result->count) {
        return NULL;
    }
    return test_result->items[i].text;
}

static const char *result_desc(size_t i) {
    if (!test_result || i >= test_result->count) {
        return NULL;
    }
    return test_result->items[i].description;
}

static int find_candidate(const char *text) {
    for (size_t i = 0; i < result_count(); i++) {
        if (result_text(i) && strcmp(result_text(i), text) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ============================================================================
 * compdef BINDINGS API
 * ============================================================================
 */

TEST(compdef_lookup_empty_returns_null) {
    setup_bindings();
    ASSERT_NULL(compdef_lookup("git"), "lookup on empty bindings returns NULL");
    teardown_bindings();
}

TEST(compdef_set_then_lookup_returns_value) {
    setup_bindings();
    ASSERT_TRUE(compdef_set("git", "_git"), "set succeeds");
    ASSERT_STR_EQ(compdef_lookup("git"), "_git",
                  "lookup after set returns the bound function name");
    teardown_bindings();
}

TEST(compdef_set_replaces_existing) {
    setup_bindings();
    compdef_set("git", "_git");
    ASSERT_TRUE(compdef_set("git", "_other_git"),
                "re-set on the same key succeeds");
    ASSERT_STR_EQ(compdef_lookup("git"), "_other_git",
                  "second set replaces the prior binding");
    teardown_bindings();
}

TEST(compdef_unset_removes_binding) {
    setup_bindings();
    compdef_set("git", "_git");
    ASSERT_TRUE(compdef_unset("git"),
                "unset returns true when a binding existed");
    ASSERT_NULL(compdef_lookup("git"), "lookup after unset returns NULL");
    ASSERT_FALSE(compdef_unset("git"),
                 "unset on a non-existent binding returns false");
    teardown_bindings();
}

TEST(compdef_set_rejects_null_or_empty) {
    setup_bindings();
    ASSERT_FALSE(compdef_set(NULL, "fn"), "NULL cmd is rejected");
    ASSERT_FALSE(compdef_set("cmd", NULL), "NULL fn is rejected");
    ASSERT_FALSE(compdef_set("", "fn"), "empty cmd is rejected");
    ASSERT_FALSE(compdef_set("cmd", ""), "empty fn is rejected");
    teardown_bindings();
}

TEST(compdef_count_reflects_state) {
    setup_bindings();
    ASSERT_EQ(compdef_count(), 0u, "count starts at 0");
    compdef_set("a", "f1");
    compdef_set("b", "f1");
    compdef_set("c", "f2");
    ASSERT_EQ(compdef_count(), 3u, "count reflects three bindings");
    compdef_unset("a");
    ASSERT_EQ(compdef_count(), 2u, "count drops after unset");
    teardown_bindings();
}

static bool count_visits(const char *cmd, const char *fn, void *user_data) {
    (void)cmd;
    (void)fn;
    (*(int *)user_data)++;
    return true;
}

TEST(compdef_enum_visits_all_bindings) {
    setup_bindings();
    compdef_set("a", "f");
    compdef_set("b", "f");
    compdef_set("c", "f");
    int visits = 0;
    compdef_enum(count_visits, &visits);
    ASSERT_EQ(visits, 3, "enum visits each binding once");
    teardown_bindings();
}

/* ============================================================================
 * bin_compdef ARGV PARSING
 * ============================================================================
 */

TEST(bin_compdef_no_args_returns_zero) {
    setup_bindings();
    char a0[] = "compdef";
    char *argv[] = {a0};
    int rc = bin_compdef(1, argv);
    ASSERT_EQ(rc, 0, "compdef with no args returns 0 (lists, empty here)");
    teardown_bindings();
}

TEST(bin_compdef_two_args_binds) {
    setup_bindings();
    char a0[] = "compdef";
    char a1[] = "_git";
    char a2[] = "git";
    char *argv[] = {a0, a1, a2};
    int rc = bin_compdef(3, argv);
    ASSERT_EQ(rc, 0, "compdef _git git returns 0");
    ASSERT_STR_EQ(compdef_lookup("git"), "_git",
                  "binding is recorded for `git`");
    teardown_bindings();
}

TEST(bin_compdef_binds_multiple_commands) {
    setup_bindings();
    char a0[] = "compdef";
    char a1[] = "_git";
    char a2[] = "git";
    char a3[] = "hub";
    char *argv[] = {a0, a1, a2, a3};
    int rc = bin_compdef(4, argv);
    ASSERT_EQ(rc, 0, "compdef FN CMD1 CMD2 returns 0");
    ASSERT_STR_EQ(compdef_lookup("git"), "_git", "first cmd bound");
    ASSERT_STR_EQ(compdef_lookup("hub"), "_git", "second cmd bound to same fn");
    teardown_bindings();
}

TEST(bin_compdef_dash_d_unbinds) {
    setup_bindings();
    compdef_set("git", "_git");
    char a0[] = "compdef";
    char a1[] = "-d";
    char a2[] = "git";
    char *argv[] = {a0, a1, a2};
    int rc = bin_compdef(3, argv);
    ASSERT_EQ(rc, 0, "compdef -d git returns 0 when a binding existed");
    ASSERT_NULL(compdef_lookup("git"), "binding removed");
    teardown_bindings();
}

TEST(bin_compdef_dash_d_missing_arg_returns_error) {
    setup_bindings();
    char a0[] = "compdef";
    char a1[] = "-d";
    char *argv[] = {a0, a1};
    int rc = bin_compdef(2, argv);
    ASSERT_NE(rc, 0, "compdef -d with no argument is an error");
    teardown_bindings();
}

TEST(bin_compdef_unknown_flag_returns_error) {
    setup_bindings();
    char a0[] = "compdef";
    char a1[] = "-X";
    char a2[] = "_git";
    char a3[] = "git";
    char *argv[] = {a0, a1, a2, a3};
    int rc = bin_compdef(4, argv);
    ASSERT_NE(rc, 0, "unknown flag -X is an error");
    ASSERT_NULL(compdef_lookup("git"),
                "no binding is recorded when the call fails");
    teardown_bindings();
}

TEST(bin_compdef_one_arg_is_usage_error) {
    setup_bindings();
    char a0[] = "compdef";
    char a1[] = "_git";
    char *argv[] = {a0, a1};
    int rc = bin_compdef(2, argv);
    ASSERT_NE(rc, 0, "compdef FN with no CMD is a usage error");
    teardown_bindings();
}

/* ============================================================================
 * bin_compadd OUTSIDE COMPLETION CONTEXT
 * ============================================================================
 */

TEST(bin_compadd_outside_context_returns_one) {
    /// No active_comp_result and no executor: compadd must refuse to
    /// run rather than crash or silently accept candidates.
    current_executor = NULL;
    char a0[] = "compadd";
    char a1[] = "foo";
    char *argv[] = {a0, a1};
    int rc = bin_compadd(2, argv);
    ASSERT_EQ(rc, 1,
              "compadd called with no active completion context returns 1");
}

/* ============================================================================
 * bin_compadd INSIDE COMPLETION CONTEXT
 * ============================================================================
 */

TEST(bin_compadd_positional_adds_candidate) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = "checkout";
    char *argv[] = {a0, a1};
    int rc = bin_compadd(2, argv);
    ASSERT_EQ(rc, 0, "compadd succeeded");
    ASSERT_EQ(result_count(), 1u, "one candidate added");
    ASSERT_STR_EQ(result_text(0), "checkout", "candidate text matches argv");
    ASSERT_NULL(result_desc(0), "no description when -d is not supplied");
    teardown_active_context();
}

TEST(bin_compadd_multiple_positionals) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = "branch";
    char a2[] = "checkout";
    char a3[] = "merge";
    char *argv[] = {a0, a1, a2, a3};
    int rc = bin_compadd(4, argv);
    ASSERT_EQ(rc, 0, "compadd succeeded");
    ASSERT_EQ(result_count(), 3u, "three candidates added");
    ASSERT_TRUE(find_candidate("branch") >= 0, "branch added");
    ASSERT_TRUE(find_candidate("checkout") >= 0, "checkout added");
    ASSERT_TRUE(find_candidate("merge") >= 0, "merge added");
    teardown_active_context();
}

TEST(bin_compadd_dash_d_applies_uniform_description) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = "-d";
    char a2[] = "git subcommand";
    char a3[] = "checkout";
    char a4[] = "merge";
    char *argv[] = {a0, a1, a2, a3, a4};
    int rc = bin_compadd(5, argv);
    ASSERT_EQ(rc, 0, "compadd -d desc x y succeeded");
    ASSERT_EQ(result_count(), 2u, "two candidates added");
    int co = find_candidate("checkout");
    int mr = find_candidate("merge");
    ASSERT_TRUE(co >= 0 && mr >= 0, "both candidates present");
    ASSERT_STR_EQ(result_desc((size_t)co), "git subcommand",
                  "checkout has the uniform description");
    ASSERT_STR_EQ(result_desc((size_t)mr), "git subcommand",
                  "merge has the uniform description");
    teardown_active_context();
}

TEST(bin_compadd_dash_a_adds_indexed_array_values) {
    setup_active_context();
    /// Indexed array: values become candidates with no description.
    array_value_t *arr = symtable_array_create(false);
    symtable_array_append(arr, "alpha");
    symtable_array_append(arr, "beta");
    symtable_array_append(arr, "gamma");
    symtable_set_array("subs", arr);

    char a0[] = "compadd";
    char a1[] = "-a";
    char a2[] = "subs";
    char *argv[] = {a0, a1, a2};
    int rc = bin_compadd(3, argv);
    ASSERT_EQ(rc, 0, "compadd -a subs succeeded");
    ASSERT_EQ(result_count(), 3u, "three values added");
    ASSERT_TRUE(find_candidate("alpha") >= 0, "alpha present");
    ASSERT_TRUE(find_candidate("beta") >= 0, "beta present");
    ASSERT_TRUE(find_candidate("gamma") >= 0, "gamma present");
    int a_idx = find_candidate("alpha");
    ASSERT_NULL(result_desc((size_t)a_idx),
                "indexed-array values have no description");
    teardown_active_context();
}

TEST(bin_compadd_dash_k_adds_assoc_keys_and_values_as_descs) {
    setup_active_context();
    /// Associative array: keys become candidates, values become
    /// per-key descriptions (lush-additive extension over zsh's -k).
    array_value_t *arr = symtable_array_create(true);
    symtable_array_set_assoc(arr, "checkout", "switch branches");
    symtable_array_set_assoc(arr, "merge", "join branches");
    symtable_set_array("git_cmds", arr);

    char a0[] = "compadd";
    char a1[] = "-k";
    char a2[] = "git_cmds";
    char *argv[] = {a0, a1, a2};
    int rc = bin_compadd(3, argv);
    ASSERT_EQ(rc, 0, "compadd -k git_cmds succeeded");
    ASSERT_EQ(result_count(), 2u, "two keys added");
    int ci = find_candidate("checkout");
    int mi = find_candidate("merge");
    ASSERT_TRUE(ci >= 0 && mi >= 0, "both keys present");
    ASSERT_STR_EQ(result_desc((size_t)ci), "switch branches",
                  "checkout's description is its assoc value");
    ASSERT_STR_EQ(result_desc((size_t)mi), "join branches",
                  "merge's description is its assoc value");
    teardown_active_context();
}

TEST(bin_compadd_dash_dash_terminates_flags) {
    setup_active_context();
    /// After --, dash-prefixed words become candidates instead of
    /// flag-parse errors. Mirrors POSIX option-parsing convention.
    char a0[] = "compadd";
    char a1[] = "--";
    char a2[] = "-d"; /// would otherwise be a flag
    char *argv[] = {a0, a1, a2};
    int rc = bin_compadd(3, argv);
    ASSERT_EQ(rc, 0, "compadd -- -d succeeded");
    ASSERT_EQ(result_count(), 1u, "one candidate added");
    ASSERT_STR_EQ(result_text(0), "-d",
                  "the dash-prefixed word is treated as a candidate");
    teardown_active_context();
}

TEST(bin_compadd_unknown_flag_returns_error) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = "-Z";
    char a2[] = "candidate";
    char *argv[] = {a0, a1, a2};
    int rc = bin_compadd(3, argv);
    ASSERT_NE(rc, 0, "unknown flag -Z is an error");
    ASSERT_EQ(result_count(), 0u,
              "no candidates accumulate when the call fails");
    teardown_active_context();
}

TEST(bin_compadd_missing_d_arg_returns_error) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = "-d";
    char *argv[] = {a0, a1};
    int rc = bin_compadd(2, argv);
    ASSERT_NE(rc, 0, "compadd -d without its argument is an error");
    teardown_active_context();
}

TEST(bin_compadd_empty_strings_are_skipped) {
    setup_active_context();
    char a0[] = "compadd";
    char a1[] = ""; /// empty
    char a2[] = "real";
    char a3[] = ""; /// empty
    char *argv[] = {a0, a1, a2, a3};
    int rc = bin_compadd(4, argv);
    ASSERT_EQ(rc, 0, "compadd succeeded");
    ASSERT_EQ(result_count(), 1u,
              "empty-string candidates are silently skipped");
    ASSERT_STR_EQ(result_text(0), "real", "non-empty candidate landed");
    teardown_active_context();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Running compdef tests\n");
    printf("=====================\n");

    printf("\nCompdef Bindings API:\n");
    RUN_TEST(compdef_lookup_empty_returns_null);
    RUN_TEST(compdef_set_then_lookup_returns_value);
    RUN_TEST(compdef_set_replaces_existing);
    RUN_TEST(compdef_unset_removes_binding);
    RUN_TEST(compdef_set_rejects_null_or_empty);
    RUN_TEST(compdef_count_reflects_state);
    RUN_TEST(compdef_enum_visits_all_bindings);

    printf("\nbin_compdef Argv Parsing:\n");
    RUN_TEST(bin_compdef_no_args_returns_zero);
    RUN_TEST(bin_compdef_two_args_binds);
    RUN_TEST(bin_compdef_binds_multiple_commands);
    RUN_TEST(bin_compdef_dash_d_unbinds);
    RUN_TEST(bin_compdef_dash_d_missing_arg_returns_error);
    RUN_TEST(bin_compdef_unknown_flag_returns_error);
    RUN_TEST(bin_compdef_one_arg_is_usage_error);

    printf("\nbin_compadd Outside Context:\n");
    RUN_TEST(bin_compadd_outside_context_returns_one);

    printf("\nbin_compadd Inside Context:\n");
    RUN_TEST(bin_compadd_positional_adds_candidate);
    RUN_TEST(bin_compadd_multiple_positionals);
    RUN_TEST(bin_compadd_dash_d_applies_uniform_description);
    RUN_TEST(bin_compadd_dash_a_adds_indexed_array_values);
    RUN_TEST(bin_compadd_dash_k_adds_assoc_keys_and_values_as_descs);
    RUN_TEST(bin_compadd_dash_dash_terminates_flags);
    RUN_TEST(bin_compadd_unknown_flag_returns_error);
    RUN_TEST(bin_compadd_missing_d_arg_returns_error);
    RUN_TEST(bin_compadd_empty_strings_are_skipped);

    return TEST_RESULT();
}
