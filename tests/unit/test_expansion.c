/**
 * @file test_expansion.c
 * @brief Unit tests for shell expansion functionality
 *
 * Tests variable expansion, parameter expansion, arithmetic expansion,
 * and command substitution through both direct API calls and executor.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "executor.h"
#include "expand.h"
#include "symtable.h"
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
 * HELPER FUNCTIONS
 * ============================================================================
 */

static executor_t *setup_executor(void) {
    executor_t *exec = executor_new();
    if (!exec) {
        fprintf(stderr, "Failed to create executor\n");
        exit(1);
    }
    return exec;
}

static void teardown_executor(executor_t *exec) { executor_free(exec); }

/* ============================================================================
 * EXPAND CONTEXT API TESTS
 * ============================================================================
 */

TEST(expand_ctx_init_normal) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT_EQ(ctx.mode, EXPAND_NORMAL, "Mode should be EXPAND_NORMAL");
    ASSERT(!ctx.in_quotes, "in_quotes should be false");
    ASSERT(!ctx.in_backticks, "in_backticks should be false");
}

TEST(expand_ctx_init_with_flags) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR | EXPAND_NOCMD);

    ASSERT_EQ(ctx.mode, EXPAND_NOVAR | EXPAND_NOCMD,
              "Mode should have flags set");
}

TEST(expand_ctx_check_normal) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NORMAL);

    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOVAR), "NOVAR should not be set");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOCMD), "NOCMD should not be set");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOGLOB), "NOGLOB should not be set");
}

TEST(expand_ctx_check_with_flags) {
    expand_ctx_t ctx;
    expand_ctx_init(&ctx, EXPAND_NOVAR | EXPAND_NOGLOB);

    ASSERT(expand_ctx_check(&ctx, EXPAND_NOVAR), "NOVAR should be set");
    ASSERT(!expand_ctx_check(&ctx, EXPAND_NOCMD), "NOCMD should not be set");
    ASSERT(expand_ctx_check(&ctx, EXPAND_NOGLOB), "NOGLOB should be set");
}

TEST(expand_ctx_check_null) {
    ASSERT(!expand_ctx_check(NULL, EXPAND_NOVAR),
           "NULL ctx should return false");
}

TEST(expand_ctx_init_null) {
    /// Should not crash with NULL
    expand_ctx_init(NULL, EXPAND_NORMAL);
}

/* ============================================================================
 * SIMPLE VARIABLE EXPANSION TESTS
 * ============================================================================
 */

TEST(simple_var_expansion) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "MYVAR=hello", 1);
    executor_execute_command_line(exec, "RESULT=$MYVAR", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "hello", "Variable should expand correctly");
    free(result);

    teardown_executor(exec);
}

TEST(braced_var_expansion) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "MYVAR=world", 1);
    executor_execute_command_line(exec, "RESULT=${MYVAR}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "world", "Braced variable should expand correctly");
    free(result);

    teardown_executor(exec);
}

TEST(var_concatenation) {
    /// Adjacent braced expansions joined by a literal separator concatenate
    /// correctly (issue #59, fixed -- previously a double-free).
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "A=hello", 1);
    executor_execute_command_line(exec, "B=world", 1);
    executor_execute_command_line(exec, "RESULT=${A}_${B}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "hello_world",
                  "${A}_${B} should expand to hello_world");
    free(result);

    teardown_executor(exec);
}

TEST(unset_var_expands_empty) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$UNDEFINED_VAR_XYZ", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "", "Unset variable should expand to empty");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * PARAMETER EXPANSION TESTS
 * ============================================================================
 */

TEST(default_value_unset) {
    executor_t *exec = setup_executor();

    /// ${VAR:-default} when VAR is unset
    executor_execute_command_line(exec, "RESULT=${UNSET_VAR:-default_value}",
                                  1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "default_value", "Should use default for unset var");
    free(result);

    teardown_executor(exec);
}

TEST(default_value_empty) {
    executor_t *exec = setup_executor();

    /// ${VAR:-default} when VAR is empty
    executor_execute_command_line(exec, "EMPTY_VAR=", 1);
    executor_execute_command_line(exec, "RESULT=${EMPTY_VAR:-default_value}",
                                  1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "default_value", "Should use default for empty var");
    free(result);

    teardown_executor(exec);
}

TEST(default_value_set) {
    executor_t *exec = setup_executor();

    /// ${VAR:-default} when VAR is set
    executor_execute_command_line(exec, "SET_VAR=actual", 1);
    executor_execute_command_line(exec, "RESULT=${SET_VAR:-default_value}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "actual", "Should use actual value when set");
    free(result);

    teardown_executor(exec);
}

TEST(alternate_value_set) {
    executor_t *exec = setup_executor();

    /// ${VAR:+alt} when VAR is set
    executor_execute_command_line(exec, "SET_VAR=something", 1);
    executor_execute_command_line(exec, "RESULT=${SET_VAR:+alternate}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "alternate", "Should use alternate when var set");
    free(result);

    teardown_executor(exec);
}

TEST(alternate_value_unset) {
    executor_t *exec = setup_executor();

    /// ${VAR:+alt} when VAR is unset
    executor_execute_command_line(exec, "RESULT=${UNSET_VAR_XYZ:+alternate}",
                                  1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "", "Should be empty when var unset");
    free(result);

    teardown_executor(exec);
}

TEST(string_length) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "VAR=hello", 1);
    executor_execute_command_line(exec, "RESULT=${#VAR}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "Length of 'hello' should be 5");
    free(result);

    teardown_executor(exec);
}

TEST(string_length_empty) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "VAR=", 1);
    executor_execute_command_line(exec, "RESULT=${#VAR}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "0", "Length of empty string should be 0");
    free(result);

    teardown_executor(exec);
}

TEST(prefix_removal) {
    executor_t *exec = setup_executor();

    /// ${VAR#pattern} - remove shortest prefix
    executor_execute_command_line(exec, "VAR=foobar", 1);
    executor_execute_command_line(exec, "RESULT=${VAR#foo}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "bar", "Should remove 'foo' prefix");
    free(result);

    teardown_executor(exec);
}

TEST(suffix_removal) {
    executor_t *exec = setup_executor();

    /// ${VAR%pattern} - remove shortest suffix
    executor_execute_command_line(exec, "VAR=foobar", 1);
    executor_execute_command_line(exec, "RESULT=${VAR%bar}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "foo", "Should remove 'bar' suffix");
    free(result);

    teardown_executor(exec);
}

TEST(substitution_first) {
    executor_t *exec = setup_executor();

    /// ${VAR/pattern/replacement} - replace first
    executor_execute_command_line(exec, "VAR=hello", 1);
    executor_execute_command_line(exec, "RESULT=${VAR/l/L}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "heLlo", "Should replace first 'l' with 'L'");
    free(result);

    teardown_executor(exec);
}

TEST(substitution_all) {
    executor_t *exec = setup_executor();

    /// ${VAR//pattern/replacement} - replace all
    executor_execute_command_line(exec, "VAR=hello", 1);
    executor_execute_command_line(exec, "RESULT=${VAR//l/L}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "heLLo", "Should replace all 'l' with 'L'");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * ARITHMETIC EXPANSION TESTS
 * ============================================================================
 */

TEST(arith_simple_add) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((1 + 2))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "3", "1 + 2 = 3");
    free(result);

    teardown_executor(exec);
}

TEST(arith_subtract) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((10 - 3))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "7", "10 - 3 = 7");
    free(result);

    teardown_executor(exec);
}

TEST(arith_multiply) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((4 * 5))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "20", "4 * 5 = 20");
    free(result);

    teardown_executor(exec);
}

TEST(arith_divide) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((20 / 4))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "20 / 4 = 5");
    free(result);

    teardown_executor(exec);
}

TEST(arith_modulo) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((17 % 5))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "2", "17 % 5 = 2");
    free(result);

    teardown_executor(exec);
}

TEST(arith_with_vars) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=10", 1);
    executor_execute_command_line(exec, "Y=3", 1);
    executor_execute_command_line(exec, "RESULT=$((X + Y))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "13", "X(10) + Y(3) = 13");
    free(result);

    teardown_executor(exec);
}

TEST(arith_parentheses) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$(( (2 + 3) * 4 ))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "20", "(2 + 3) * 4 = 20");
    free(result);

    teardown_executor(exec);
}

TEST(arith_comparison_true) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((5 > 3))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "1", "5 > 3 should be 1 (true)");
    free(result);

    teardown_executor(exec);
}

TEST(arith_comparison_false) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((3 > 5))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "0", "3 > 5 should be 0 (false)");
    free(result);

    teardown_executor(exec);
}

TEST(arith_negative) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((-5 + 3))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "-2", "-5 + 3 = -2");
    free(result);

    teardown_executor(exec);
}

TEST(arith_increment) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=5", 1);
    executor_execute_command_line(exec, "RESULT=$((++X))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "6", "++5 = 6");
    free(result);

    /// X should also be updated
    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_NOT_NULL(x, "X should be set");
    ASSERT_STR_EQ(x, "6", "X should be 6 after increment");
    free(x);

    teardown_executor(exec);
}

TEST(arith_decrement) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=5", 1);
    executor_execute_command_line(exec, "RESULT=$((--X))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "4", "--5 = 4");
    free(result);

    teardown_executor(exec);
}

TEST(arith_ternary) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((1 ? 10 : 20))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "10", "1 ? 10 : 20 = 10");
    free(result);

    teardown_executor(exec);
}

TEST(arith_ternary_false) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$((0 ? 10 : 20))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "20", "0 ? 10 : 20 = 20");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * SPECIAL VARIABLE TESTS
 * ============================================================================
 */

TEST(special_var_question_mark) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "true", 1);
    executor_execute_command_line(exec, "RESULT=$?", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "0", "$? after true should be 0");
    free(result);

    teardown_executor(exec);
}

TEST(special_var_question_mark_fail) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "false", 1);
    executor_execute_command_line(exec, "RESULT=$?", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "1", "$? after false should be 1");
    free(result);

    teardown_executor(exec);
}

TEST(special_var_dollar) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "RESULT=$$", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    /// Verify it's a non-negative number (0 is valid in test context if not
    /// initialized)
    ASSERT(atoi(result) >= 0, "$$ should be a non-negative number");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * ARRAY EXPANSION TESTS
 * ============================================================================
 */

TEST(array_element_access) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "arr=(one two three)", 1);
    executor_execute_command_line(exec, "RESULT=${arr[1]}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "two", "arr[1] should be 'two'");
    free(result);

    teardown_executor(exec);
}

TEST(array_length) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "arr=(a b c d e)", 1);
    executor_execute_command_line(exec, "RESULT=${#arr[@]}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "Array length should be 5");
    free(result);

    teardown_executor(exec);
}

TEST(array_first_element) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "arr=(first second third)", 1);
    executor_execute_command_line(exec, "RESULT=${arr[0]}", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "first", "arr[0] should be 'first'");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * QUOTING AND ESCAPING TESTS
 * ============================================================================
 */

TEST(single_quotes_no_expansion) {
    /// Single quotes prevent all expansion per POSIX (issue #60, fixed).
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "VAR=value", 1);
    executor_execute_command_line(exec, "RESULT='$VAR'", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "$VAR", "Single quotes should keep $VAR literal");
    free(result);

    teardown_executor(exec);
}

TEST(double_quotes_with_expansion) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "VAR=value", 1);
    executor_execute_command_line(exec, "RESULT=\"$VAR\"", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "value", "Double quotes should allow expansion");
    free(result);

    teardown_executor(exec);
}

TEST(escaped_dollar) {
    /// A backslash-escaped dollar is a literal $ (issue #60-adjacent, fixed).
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "VAR=value", 1);
    executor_execute_command_line(exec, "RESULT=\\$VAR", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "$VAR", "\\$VAR should stay literal $VAR");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * NESTED EXPANSION TESTS
 * ============================================================================
 */

TEST(nested_var_expansion) {
    /// Single quotes keep an inner $var literal (issue #60, fixed). The
    /// double-quoted counterpart is covered by nested_var_double_quotes.
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "INNER=world", 1);
    executor_execute_command_line(exec, "OUTER='hello $INNER'", 1);

    char *result = symtable_get_var(exec->symtable, "OUTER");
    ASSERT_NOT_NULL(result, "OUTER should be set");
    ASSERT_STR_EQ(result, "hello $INNER", "Single quotes keep $INNER literal");
    free(result);

    teardown_executor(exec);
}

TEST(nested_var_double_quotes) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "INNER=world", 1);
    executor_execute_command_line(exec, "OUTER=\"hello $INNER\"", 1);

    char *result = symtable_get_var(exec->symtable, "OUTER");
    ASSERT_NOT_NULL(result, "OUTER should be set");
    ASSERT_STR_EQ(result, "hello world", "Double quotes allow expansion");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * BRACE EXPANSION TESTS
 * ============================================================================
 */

TEST(brace_adjacent_text) {
    /// A braced variable followed by adjacent text concatenates (issue #59,
    /// fixed -- previously a double-free).
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "PREFIX=hello", 1);
    executor_execute_command_line(exec, "RESULT=${PREFIX}world", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "helloworld", "${PREFIX}world should be helloworld");
    free(result);

    teardown_executor(exec);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("=== Expansion Tests ===\n\n");

    /// Initialize required subsystems
    init_symtable();
    init_aliases();

    printf("--- Expand Context API Tests ---\n");
    RUN_TEST(expand_ctx_init_normal);
    RUN_TEST(expand_ctx_init_with_flags);
    RUN_TEST(expand_ctx_check_normal);
    RUN_TEST(expand_ctx_check_with_flags);
    RUN_TEST(expand_ctx_check_null);
    RUN_TEST(expand_ctx_init_null);

    printf("\n--- Simple Variable Expansion Tests ---\n");
    RUN_TEST(simple_var_expansion);
    RUN_TEST(braced_var_expansion);
    RUN_TEST(var_concatenation);
    RUN_TEST(unset_var_expands_empty);

    printf("\n--- Parameter Expansion Tests ---\n");
    RUN_TEST(default_value_unset);
    RUN_TEST(default_value_empty);
    RUN_TEST(default_value_set);
    RUN_TEST(alternate_value_set);
    RUN_TEST(alternate_value_unset);
    RUN_TEST(string_length);
    RUN_TEST(string_length_empty);
    RUN_TEST(prefix_removal);
    RUN_TEST(suffix_removal);
    RUN_TEST(substitution_first);
    RUN_TEST(substitution_all);

    printf("\n--- Arithmetic Expansion Tests ---\n");
    RUN_TEST(arith_simple_add);
    RUN_TEST(arith_subtract);
    RUN_TEST(arith_multiply);
    RUN_TEST(arith_divide);
    RUN_TEST(arith_modulo);
    RUN_TEST(arith_with_vars);
    RUN_TEST(arith_parentheses);
    RUN_TEST(arith_comparison_true);
    RUN_TEST(arith_comparison_false);
    RUN_TEST(arith_negative);
    RUN_TEST(arith_increment);
    RUN_TEST(arith_decrement);
    RUN_TEST(arith_ternary);
    RUN_TEST(arith_ternary_false);

    printf("\n--- Special Variable Tests ---\n");
    RUN_TEST(special_var_question_mark);
    RUN_TEST(special_var_question_mark_fail);
    RUN_TEST(special_var_dollar);

    printf("\n--- Array Expansion Tests ---\n");
    RUN_TEST(array_element_access);
    RUN_TEST(array_length);
    RUN_TEST(array_first_element);

    printf("\n--- Quoting and Escaping Tests ---\n");
    RUN_TEST(single_quotes_no_expansion);
    RUN_TEST(double_quotes_with_expansion);
    RUN_TEST(escaped_dollar);

    printf("\n--- Nested Expansion Tests ---\n");
    RUN_TEST(nested_var_expansion);
    RUN_TEST(nested_var_double_quotes);

    printf("\n--- Brace Expansion Tests ---\n");
    RUN_TEST(brace_adjacent_text);

    return TEST_RESULT();
}
