/**
 * @file test_history_expansion.c
 * @brief Functional tests for LLE history expansion (!!, !$, etc.)
 *
 * Tests bash-compatible history expansion including:
 * - !! (repeat last command)
 * - !n (repeat command number n)
 * - !-n (repeat command n positions back)
 * - !string (repeat most recent command starting with string)
 * - !?string (repeat most recent command containing string)
 * - ^old^new (quick substitution)
 *
 * Validates:
 * - Correct expansion of all formats
 * - Error handling for invalid references
 * - Space-prefix disabling expansion
 * - Recursion prevention
 * - Memory management
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include "posix_history.h"

/// Test harness
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    static void name(void);                                                    \
    static void run_##name(void) {                                             \
        tests_run++;                                                           \
        printf("Running: %s\n", #name);                                        \
        name();                                                                \
        tests_passed++;                                                        \
        printf("  PASSED\n");                                                  \
    }                                                                          \
    static void name(void)

#define ASSERT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("  FAILED: %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_SUCCESS(result) ASSERT((result) == LLE_SUCCESS)

#define ASSERT_ERROR(result) ASSERT((result) != LLE_SUCCESS)

#define ASSERT_EQ(actual, expected) ASSERT((actual) == (expected))

#define ASSERT_STR_EQ(actual, expected)                                        \
    ASSERT(strcmp((actual), (expected)) == 0)

/* ============================================================================
 * TEST SETUP AND TEARDOWN
 * ============================================================================
 */

static lle_history_core_t *g_core = NULL;
static posix_history_manager_t *g_posix = NULL;

/// The expansion engine resolves history references through the bridge, which
/// forwards allocations to lle_pool_alloc; the test mock satisfies those with
/// malloc, so a non-NULL sentinel pool is sufficient (same pattern as the other
/// mock-linked LLE unit tests).
static lle_memory_pool_t *const MOCK_POOL = (lle_memory_pool_t *)1;

static void setup(void) {
    lle_result_t result = lle_history_core_create(&g_core, NULL, NULL);
    ASSERT_SUCCESS(result);

    /// Initialize expansion system.
    result = lle_history_expansion_init(g_core);
    ASSERT_SUCCESS(result);

    /// The bridge links the LLE history core to a POSIX history manager, which
    /// the expansion engine reads when resolving !! / !n / !string. Entries
    /// added to the core sync through to it.
    g_posix = posix_history_create(1000);
    ASSERT(g_posix != NULL);
    result = lle_history_bridge_init(g_core, g_posix, MOCK_POOL);
    ASSERT_SUCCESS(result);
}

static void teardown(void) {
    if (g_core) {
        lle_history_expansion_shutdown();
        lle_history_bridge_shutdown();
        lle_history_core_destroy(g_core);
        g_core = NULL;
    }
    if (g_posix) {
        posix_history_destroy(g_posix);
        g_posix = NULL;
    }
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

static void add_test_commands(void) {
    /// Add some test commands to history
    lle_history_add_entry(g_core, "ls -la", 0, NULL);
    lle_history_add_entry(g_core, "cd /tmp", 0, NULL);
    lle_history_add_entry(g_core, "git status", 0, NULL);
    lle_history_add_entry(g_core, "git commit -m 'test'", 0, NULL);
    lle_history_add_entry(g_core, "make clean", 0, NULL);
    lle_history_add_entry(g_core, "make all", 0, NULL);
    lle_history_add_entry(g_core, "echo hello", 0, NULL);
}

/* ============================================================================
 * BASIC EXPANSION TESTS
 * ============================================================================
 */

TEST(test_expansion_needed_detection) {
    setup();

    /// Commands that need expansion
    ASSERT(lle_history_expansion_needed("!!") == true);
    ASSERT(lle_history_expansion_needed("!5") == true);
    ASSERT(lle_history_expansion_needed("!-2") == true);
    ASSERT(lle_history_expansion_needed("!git") == true);
    ASSERT(lle_history_expansion_needed("!?status") == true);
    ASSERT(lle_history_expansion_needed("^old^new") == true);
    ASSERT(lle_history_expansion_needed("echo !-1") == true);

    /// Commands that don't need expansion
    ASSERT(lle_history_expansion_needed("echo hello") == false);
    ASSERT(lle_history_expansion_needed("ls /tmp") == false);
    ASSERT(lle_history_expansion_needed("") == false);

    /// Space prefix disables expansion (default)
    ASSERT(lle_history_expansion_needed(" !!") == false);

    teardown();
}

TEST(test_double_bang_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expand !! - should get last command
    result = lle_history_expand_line("!!", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo hello");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_number_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Get history count to know valid IDs
    size_t count;
    lle_history_get_entry_count(g_core, &count);
    ASSERT(count == 7);

    /// Expand !1 - should get first command
    result = lle_history_expand_line("!1", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "ls -la");
    lle_pool_free(expanded);

    /// Expand !3 - should get third command
    result = lle_history_expand_line("!3", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "git status");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_relative_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expand !-1 - should get last command (same as !!)
    result = lle_history_expand_line("!-1", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo hello");
    lle_pool_free(expanded);

    /// Expand !-3 - three commands back (echo hello, make all, make clean).
    result = lle_history_expand_line("!-3", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "make clean");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_prefix_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expand !git - should get most recent git command
    result = lle_history_expand_line("!git", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "git commit -m 'test'");
    lle_pool_free(expanded);

    /// Expand !make - should get most recent make command
    result = lle_history_expand_line("!make", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "make all");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_substring_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expand !?status - should find git status
    result = lle_history_expand_line("!?status", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "git status");
    lle_pool_free(expanded);

    /// Expand !?clean - should find make clean
    result = lle_history_expand_line("!?clean", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "make clean");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_quick_substitution) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// ^hello^world - should substitute in last command
    result = lle_history_expand_line("^hello^world", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo world");
    lle_pool_free(expanded);

    teardown();
}

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(test_nonexistent_number) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Try to expand !999 - should fail
    result = lle_history_expand_line("!999", &expanded, NULL);
    ASSERT_ERROR(result);
    ASSERT(result == LLE_ERROR_NOT_FOUND);

    teardown();
}

TEST(test_nonexistent_prefix) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Try to expand !nonexistent - should fail
    result = lle_history_expand_line("!nonexistent", &expanded, NULL);
    ASSERT_ERROR(result);
    ASSERT(result == LLE_ERROR_NOT_FOUND);

    teardown();
}

TEST(test_empty_history) {
    setup();
    /// Don't add any commands

    char *expanded = NULL;
    lle_result_t result;

    /// Try !! with empty history
    result = lle_history_expand_line("!!", &expanded, NULL);
    ASSERT_ERROR(result);

    teardown();
}

TEST(test_no_expansion_needed) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expand regular command - should return as-is
    result = lle_history_expand_line("echo test", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo test");
    lle_pool_free(expanded);

    teardown();
}

/* ============================================================================
 * CONFIGURATION TESTS
 * ============================================================================
 */

TEST(test_space_disables_expansion) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Default: space disables expansion
    ASSERT(lle_history_expansion_get_space_disables() == true);

    /// Leading space should prevent expansion
    result = lle_history_expand_line(" !!", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, " !!"); /// Not expanded
    lle_pool_free(expanded);

    /// Disable the feature
    lle_history_expansion_set_space_disables(false);
    ASSERT(lle_history_expansion_get_space_disables() == false);

    /// Now space should not prevent expansion
    result = lle_history_expand_line(" !!", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, " echo hello"); /// Expanded with leading space
    lle_pool_free(expanded);

    teardown();
}

TEST(test_verify_setting) {
    setup();

    /// Default: verify disabled
    ASSERT(lle_history_expansion_get_verify() == false);

    /// Enable verify
    lle_history_expansion_set_verify(true);
    ASSERT(lle_history_expansion_get_verify() == true);

    /// Disable verify
    lle_history_expansion_set_verify(false);
    ASSERT(lle_history_expansion_get_verify() == false);

    teardown();
}

/* ============================================================================
 * COMPLEX EXPANSION TESTS
 * ============================================================================
 */

TEST(test_expansion_in_middle_of_command) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Expansion in the middle of a command
    result = lle_history_expand_line("echo before !! after", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo before echo hello after");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_multiple_expansions) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    lle_result_t result;

    /// Multiple expansions in one line
    result = lle_history_expand_line("!git && !make", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    /// Should expand both !git and !make
    ASSERT(strstr(expanded, "git commit") != NULL);
    ASSERT(strstr(expanded, "make all") != NULL);
    lle_pool_free(expanded);

    teardown();
}

/* ============================================================================
 * WORD DESIGNATOR TESTS
 * ============================================================================
 */

/// Seed history with a multi-word last command for word-designator selection.
static void add_word_designator_history(void) {
    lle_history_add_entry(g_core, "deploy app prod --force", 0, NULL);
}

TEST(test_word_last_arg) {
    setup();
    add_word_designator_history();

    char *expanded = NULL;
    /// !$ selects the last word of the previous command.
    lle_result_t result = lle_history_expand_line("!$", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "--force");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_word_first_arg) {
    setup();
    add_word_designator_history();

    char *expanded = NULL;
    /// !^ selects the first argument (word 1).
    lle_result_t result = lle_history_expand_line("!^", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "app");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_word_all_args) {
    setup();
    add_word_designator_history();

    char *expanded = NULL;
    /// !* selects every argument (words 1 through last).
    lle_result_t result = lle_history_expand_line("!*", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "app prod --force");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_word_numbered_and_range) {
    setup();
    add_word_designator_history();

    char *expanded = NULL;

    /// :0 selects the command word.
    lle_result_t result = lle_history_expand_line("!!:0", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT_STR_EQ(expanded, "deploy");
    lle_pool_free(expanded);
    expanded = NULL;

    /// :2 selects word 2.
    result = lle_history_expand_line("!!:2", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT_STR_EQ(expanded, "prod");
    lle_pool_free(expanded);
    expanded = NULL;

    /// :1-2 selects words 1 through 2.
    result = lle_history_expand_line("!!:1-2", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT_STR_EQ(expanded, "app prod");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_word_designator_in_context) {
    setup();
    add_word_designator_history();

    char *expanded = NULL;
    /// A word designator splices into surrounding text.
    lle_result_t result = lle_history_expand_line("echo !$", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT_STR_EQ(expanded, "echo --force");
    lle_pool_free(expanded);

    teardown();
}

/* ============================================================================
 * MODIFIER TESTS
 * ============================================================================
 */

TEST(test_print_modifier) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    bool print_only = false;
    /// !!:p expands to the last command and flags print-only.
    lle_result_t result =
        lle_history_expand_line("!!:p", &expanded, &print_only);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo hello");
    ASSERT(print_only == true);
    lle_pool_free(expanded);

    teardown();
}

TEST(test_print_modifier_not_set_without_p) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    bool print_only = true;
    /// Without :p the flag is cleared.
    lle_result_t result = lle_history_expand_line("!!", &expanded, &print_only);
    ASSERT_SUCCESS(result);
    ASSERT(print_only == false);
    lle_pool_free(expanded);

    teardown();
}

TEST(test_substitute_modifier) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    /// !!:s/hello/world/ substitutes the first occurrence.
    lle_result_t result =
        lle_history_expand_line("!!:s/hello/world/", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "echo world");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_global_substitute_modifier) {
    setup();

    /// Seed a last command with repeated text for global substitution.
    lle_history_add_entry(g_core, "x x x", 0, NULL);

    char *expanded = NULL;
    /// !!:gs/x/y/ substitutes every occurrence.
    lle_result_t result =
        lle_history_expand_line("!!:gs/x/y/", &expanded, NULL);
    ASSERT_SUCCESS(result);
    ASSERT(expanded != NULL);
    ASSERT_STR_EQ(expanded, "y y y");
    lle_pool_free(expanded);

    teardown();
}

TEST(test_substitute_missing_pattern_fails) {
    setup();
    add_test_commands();

    char *expanded = NULL;
    /// A :s whose pattern is absent from the command aborts the expansion.
    lle_result_t result =
        lle_history_expand_line("!!:s/absent/x/", &expanded, NULL);
    ASSERT_ERROR(result);

    teardown();
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf(
        "=================================================================\n");
    printf("LLE History Expansion Tests\n");
    printf("================================================================="
           "\n\n");

    /// Basic expansion tests
    run_test_expansion_needed_detection();
    run_test_double_bang_expansion();
    run_test_number_expansion();
    run_test_relative_expansion();
    run_test_prefix_expansion();
    run_test_substring_expansion();
    run_test_quick_substitution();

    /// Error handling tests
    run_test_nonexistent_number();
    run_test_nonexistent_prefix();
    run_test_empty_history();
    run_test_no_expansion_needed();

    /// Configuration tests
    run_test_space_disables_expansion();
    run_test_verify_setting();

    /// Complex expansion tests
    run_test_expansion_in_middle_of_command();
    run_test_multiple_expansions();

    /// Word designator tests
    run_test_word_last_arg();
    run_test_word_first_arg();
    run_test_word_all_args();
    run_test_word_numbered_and_range();
    run_test_word_designator_in_context();

    /// Modifier tests
    run_test_print_modifier();
    run_test_print_modifier_not_set_without_p();
    run_test_substitute_modifier();
    run_test_global_substitute_modifier();
    run_test_substitute_missing_pattern_fails();

    /// Print summary
    printf("\n================================================================="
           "\n");
    printf("Test Summary\n");
    printf(
        "=================================================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf("RESULT: ALL TESTS PASSED ✓\n");
        return 0;
    } else {
        printf("RESULT: %d TEST(S) FAILED ✗\n", tests_failed);
        return 1;
    }
}
