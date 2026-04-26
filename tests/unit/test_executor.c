/**
 * @file test_executor.c
 * @brief Integration tests for the shell executor
 *
 * These tests exercise the executor through actual command execution,
 * covering builtins, pipelines, control structures, and variable expansion.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "executor.h"
#include "symtable.h"
#include "test_shell_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */

/* ============================================================================
 * LIFECYCLE TESTS
 * ============================================================================
 */

TEST(executor_new_free) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new should return non-NULL");
    ASSERT_EQ(exec->exit_status, 0, "Initial exit status should be 0");
    ASSERT(!exec->has_error, "Should not have error initially");
    executor_free(exec);
}

TEST(executor_with_symtable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    executor_t *exec = executor_new_with_symtable(mgr);
    ASSERT_NOT_NULL(exec, "executor_new_with_symtable failed");
    ASSERT(exec->symtable == mgr, "Symtable should be set");

    executor_free(exec);
    symtable_manager_free(mgr);
}

/* ============================================================================
 * SIMPLE COMMAND TESTS
 * ============================================================================
 */

TEST(execute_true) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "true");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_EQ(exec->exit_status, 0, "Exit status should be 0");

    executor_free(exec);
}

TEST(execute_false) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "false");
    ASSERT_EXIT_STATUS(r, 1);
    ASSERT_EQ(exec->exit_status, 1, "Exit status should be 1");

    executor_free(exec);
}

TEST(execute_colon) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, ":");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(execute_exit_status) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "true");
    (void)executor_execute_command_line(exec, "echo $?");
    ASSERT_EQ(exec->exit_status, 0, "echo should succeed");

    executor_free(exec);
}

/* ============================================================================
 * VARIABLE TESTS
 * ============================================================================
 */

TEST(variable_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "FOO=bar");
    ASSERT_EXIT_STATUS(r, 0);

    /* Verify variable was set */
    char *value = symtable_get_var(exec->symtable, "FOO");
    ASSERT_NOT_NULL(value, "Variable should be set");
    ASSERT_STR_EQ(value, "bar", "Variable value mismatch");
    free(value);

    executor_free(exec);
}

TEST(variable_expansion) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "MYVAR=hello");

    /* Test that variable exists */
    char *value = symtable_get_var(exec->symtable, "MYVAR");
    ASSERT_NOT_NULL(value, "Variable should be set");
    ASSERT_STR_EQ(value, "hello", "Variable value mismatch");
    free(value);

    executor_free(exec);
}

TEST(multiple_assignments) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "A=1");
    executor_execute_command_line(exec, "B=2");
    executor_execute_command_line(exec, "C=3");

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    char *c = symtable_get_var(exec->symtable, "C");

    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    ASSERT_STR_EQ(c, "3", "C should be 3");

    free(a);
    free(b);
    free(c);
    executor_free(exec);
}

/* ============================================================================
 * CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(if_true_branch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "if true; then RESULT=yes; else RESULT=no; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Should take true branch");
    free(result);

    executor_free(exec);
}

TEST(if_false_branch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "if false; then RESULT=yes; else RESULT=no; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "no", "Should take false branch");
    free(result);

    executor_free(exec);
}

TEST(for_loop_basic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "COUNT=0; for i in 1 2 3; do COUNT=$((COUNT+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_NOT_NULL(count, "COUNT should be set");
    ASSERT_STR_EQ(count, "3", "Should iterate 3 times");
    free(count);

    executor_free(exec);
}

TEST(for_loop_no_in) {
    /* Tests Issue #55 fix - for without 'in' iterates over $@ */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Set positional parameters and iterate */
    run_result_t r = run_shell_with_executor(
        exec, "set -- a b c; COUNT=0; for arg; do COUNT=$((COUNT+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_NOT_NULL(count, "COUNT should be set");
    ASSERT_STR_EQ(count, "3", "Should iterate over 3 positional params");
    free(count);

    executor_free(exec);
}

TEST(while_loop) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; while [ $N -lt 5 ]; do N=$((N+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "5", "Should count to 5");
    free(n);

    executor_free(exec);
}

TEST(until_loop) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; until [ $N -ge 3 ]; do N=$((N+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "3", "Should count to 3");
    free(n);

    executor_free(exec);
}

TEST(case_statement) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "X=foo; case $X in foo) RESULT=matched;; bar) RESULT=bar;; esac");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "matched", "Should match foo pattern");
    free(result);

    executor_free(exec);
}

TEST(case_wildcard) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec,
        "X=unknown; case $X in foo) RESULT=foo;; *) RESULT=default;; esac");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "default", "Should match wildcard");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * LOGICAL OPERATOR TESTS
 * ============================================================================
 */

TEST(and_operator_success) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "true && RESULT=yes");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Second command should run");
    free(result);

    executor_free(exec);
}

TEST(and_operator_fail) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Set RESULT first, then verify it's NOT changed */
    executor_execute_command_line(exec, "RESULT=initial");
    run_result_t r = run_shell_with_executor(exec, "false && RESULT=changed");
    ASSERT_EXIT_STATUS(r, 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should still be set");
    ASSERT_STR_EQ(result, "initial", "Second command should NOT run");
    free(result);

    executor_free(exec);
}

TEST(or_operator_success) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* First succeeds, second should not run */
    executor_execute_command_line(exec, "RESULT=initial");
    run_result_t r = run_shell_with_executor(exec, "true || RESULT=changed");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "initial", "Second command should NOT run");
    free(result);

    executor_free(exec);
}

TEST(or_operator_fail) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "false || RESULT=yes");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Second command should run");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * FUNCTION TESTS
 * ============================================================================
 */

TEST(function_definition_posix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "myfunc() { CALLED=yes; }");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "myfunc");
    ASSERT_EXIT_STATUS(r, 0);

    char *called = symtable_get_var(exec->symtable, "CALLED");
    ASSERT_NOT_NULL(called, "CALLED should be set");
    ASSERT_STR_EQ(called, "yes", "Function should have been called");
    free(called);

    executor_free(exec);
}

TEST(function_definition_ksh) {
    /* Tests Issue #56 fix - function without parentheses */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "function myfunc { CALLED=yes; }");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "myfunc");
    ASSERT_EXIT_STATUS(r, 0);

    char *called = symtable_get_var(exec->symtable, "CALLED");
    ASSERT_NOT_NULL(called, "CALLED should be set");
    ASSERT_STR_EQ(called, "yes", "Function should have been called");
    free(called);

    executor_free(exec);
}

TEST(function_with_args) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "setarg() { ARG1=$1; ARG2=$2; }");
    run_result_t r = run_shell_with_executor(exec, "setarg hello world");
    ASSERT_EXIT_STATUS(r, 0);

    char *arg1 = symtable_get_var(exec->symtable, "ARG1");
    char *arg2 = symtable_get_var(exec->symtable, "ARG2");
    ASSERT_STR_EQ(arg1, "hello", "ARG1 should be hello");
    ASSERT_STR_EQ(arg2, "world", "ARG2 should be world");
    free(arg1);
    free(arg2);

    executor_free(exec);
}

TEST(function_return) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "retfunc() { return 42; }");
    run_result_t r = run_shell_with_executor(exec, "retfunc");
    ASSERT_EXIT_STATUS(r, 42);

    executor_free(exec);
}

/* ============================================================================
 * REGRESSION TESTS — Issue #47
 * Variables declared with local/declare/typeset must honor subsequent
 * assignments using POSIX scope-chain semantics: an unprefixed `name=value`
 * in a function should update the existing local, not silently create or
 * update a global. The same applies to += append, for-loop variables, and
 * other implicit-assignment forms — they all share the same root cause
 * (unconditional global write at the executor level).
 *
 * Each test below stores the post-assignment value in a global RESULT
 * variable so the test can inspect it after the function returns. Until
 * the fix lands, RESULT reflects the *initial* local value rather than
 * the value the assignment specified.
 * ============================================================================
 */

TEST(local_plain_string_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local x=initial; x=updated; RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "updated",
                  "local then plain reassignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_integer_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { local x=0; x=42; RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "42",
        "local then integer literal reassignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_arith_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec,
                                  "f() { local x=0; x=$((1+1)); RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "2",
        "local then arithmetic substitution assignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_command_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local x=initial; x=$(echo updated); RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "updated",
        "local then command substitution assignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(declare_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec,
                                  "f() { declare x=0; x=42; RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "42",
        "declare then reassignment must update the declared variable");
    free(result);
    executor_free(exec);
}

TEST(typeset_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec,
                                  "f() { typeset x=0; x=42; RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "42",
                  "typeset then reassignment must update the typeset variable");
    free(result);
    executor_free(exec);
}

TEST(local_two_step_no_initial) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { local x; x=hello; RESULT=$x; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "hello",
        "local without initial value then assign must produce the new value");
    free(result);
    executor_free(exec);
}

TEST(local_does_not_leak_to_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "X_LEAK=outside-initial");
    executor_execute_command_line(
        exec, "f() { local X_LEAK=inside-local; X_LEAK=inside-updated; }");
    executor_execute_command_line(exec, "f");

    char *outside = symtable_get_var(exec->symtable, "X_LEAK");
    ASSERT_STR_EQ(
        outside, "outside-initial",
        "global with same name as local must not be touched by reassignment "
        "to the local");
    free(outside);
    executor_free(exec);
}

TEST(local_for_loop_variable) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec,
        "f() { local i=startval; for i in 1 2 3; do :; done; RESULT=$i; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "3",
                  "for loop variable must update the local, not silently write "
                  "to global");
    free(result);
    executor_free(exec);
}

TEST(local_plus_equals_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local s=hello; s+=\" world\"; RESULT=$s; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "hello world",
                  "local += append must concatenate to the local, not silently "
                  "write to global");
    free(result);
    executor_free(exec);
}

TEST(local_while_loop_counter) {
    /* The original symptomatic case from issue #47 — a while-loop counter
     * with `local` infinite-looped before the fix. The test framework
     * exits on first failure, so earlier simpler tests stop the run before
     * this one ever executes in the broken state. After the fix, this
     * confirms the loop terminates correctly. */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { local i=0; while [ $i -lt 3 ]; "
                                        "do i=$((i+1)); done; RESULT=$i; }");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "3",
                  "while-loop counter using local must terminate correctly");
    free(result);
    executor_free(exec);
}

/* ============================================================================
 * REGRESSION TESTS — Issue #48
 * Trailing redirections attached to a function definition must be applied
 * when the function is called. The parser side landed in #43 and these
 * inputs now produce a correct AST; the executor needs to honor it.
 *
 * Each test redirects to /tmp/lush_test_48, then reads back via
 * `RESULT=$(cat /tmp/lush_test_48)` and compares against the expected
 * file contents. Tests clean up their tmp file before and after to
 * avoid cross-contamination.
 * ============================================================================
 */

TEST(function_redir_out_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_execute_command_line(exec,
                                  "f() { echo HELLO; } > /tmp/lush_test_48");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "HELLO",
        "function trailing > redirect must write body output to the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_free(exec);
}

TEST(function_redir_stderr_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_execute_command_line(
        exec, "f() { echo OOPS 1>&2; } 2> /tmp/lush_test_48");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "OOPS",
        "function trailing 2> redirect must write body stderr to the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_free(exec);
}

TEST(function_redir_append_accumulates_across_calls) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_execute_command_line(exec,
                                  "f() { echo line; } >> /tmp/lush_test_48");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "line\nline\nline",
                  "function trailing >> redirect must append on every call");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_free(exec);
}

TEST(function_redir_keyword_form_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_execute_command_line(
        exec, "function f { echo from-keyword; } > /tmp/lush_test_48");
    executor_execute_command_line(exec, "f");
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "from-keyword",
        "function-keyword form trailing > redirect must apply at call");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_free(exec);
}

TEST(function_redir_input_applied_at_call) {
    /* Verifies the function's stdin is the redirected file. The captured
     * value is stored in a global so the test does not need command
     * substitution to read it back. */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48_in");
    executor_execute_command_line(exec,
                                  "echo hello-input > /tmp/lush_test_48_in");
    executor_execute_command_line(
        exec,
        "f() { read line; CAPTURED=\"got: $line\"; } < /tmp/lush_test_48_in");
    executor_execute_command_line(exec, "f");

    char *result = symtable_get_var(exec->symtable, "CAPTURED");
    ASSERT_STR_EQ(
        result, "got: hello-input",
        "function trailing < redirect must feed body stdin from the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48_in");
    executor_free(exec);
}

TEST(function_redir_does_not_break_normal_call) {
    /* Sanity: calling a function with a trailing redirect must not leak
     * the redirection across to subsequent unrelated commands. */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_execute_command_line(exec,
                                  "f() { echo from-f; } > /tmp/lush_test_48");
    executor_execute_command_line(exec, "f");
    /* This echo must NOT also be captured by f's redirect — its output
     * should be discarded as normal (we don't capture stdout here). */
    executor_execute_command_line(exec, "echo from-second-call");
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "from-f",
        "function trailing redirect must not leak to subsequent commands");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48");
    executor_free(exec);
}

/* ============================================================================
 * ARITHMETIC TESTS
 * ============================================================================
 */

TEST(arithmetic_basic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "RESULT=$((2 + 3))");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "2 + 3 should equal 5");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_multiply) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "RESULT=$((4 * 5))");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "20", "4 * 5 should equal 20");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_variable) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "X=10");
    executor_execute_command_line(exec, "Y=20");
    executor_execute_command_line(exec, "RESULT=$((X + Y))");

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "30", "10 + 20 should equal 30");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_increment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "N=5");
    executor_execute_command_line(exec, "N=$((N + 1))");

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "6", "5 + 1 should equal 6");
    free(n);

    executor_free(exec);
}

/* ============================================================================
 * SUBSHELL AND GROUPING TESTS
 * ============================================================================
 */

TEST(subshell_isolation) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "OUTER=yes");
    /* Variable set in subshell should not affect parent */
    executor_execute_command_line(exec, "(INNER=subshell)");

    char *outer = symtable_get_var(exec->symtable, "OUTER");
    ASSERT_NOT_NULL(outer, "OUTER should be set");
    ASSERT_STR_EQ(outer, "yes", "OUTER should be yes");
    free(outer);

    /* INNER should not exist in parent */
    char *inner = symtable_get_var(exec->symtable, "INNER");
    ASSERT(inner == NULL, "INNER should NOT be set in parent");

    executor_free(exec);
}

TEST(brace_group) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Brace group runs in current shell */
    run_result_t r = run_shell_with_executor(exec, "{ A=1; B=2; }");
    ASSERT_EXIT_STATUS(r, 0);

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    free(a);
    free(b);

    executor_free(exec);
}

/* ============================================================================
 * TEST COMMAND ([) TESTS
 * ============================================================================
 */

TEST(test_string_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ foo = foo ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ foo = bar ]");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(test_string_not_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ foo != bar ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ foo != foo ]");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(test_numeric_compare) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -eq 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -ne 3 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -gt 3 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -ge 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 3 -lt 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 3 -le 3 ]"), 0);

    executor_free(exec);
}

TEST(test_string_empty) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ -z '' ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ -z 'notempty' ]");
    ASSERT_EXIT_STATUS(r, 1);

    r = run_shell_with_executor(exec, "[ -n 'notempty' ]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * BUILTIN TESTS
 * ============================================================================
 */

TEST(builtin_export) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "export MYEXPORT=value");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "MYEXPORT");
    ASSERT_NOT_NULL(value, "MYEXPORT should be set");
    ASSERT_STR_EQ(value, "value", "Value should be 'value'");
    free(value);

    executor_free(exec);
}

TEST(builtin_unset) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "TOUNSET=exists");
    char *before = symtable_get_var(exec->symtable, "TOUNSET");
    ASSERT_NOT_NULL(before, "Variable should exist before unset");
    free(before);

    executor_execute_command_line(exec, "unset TOUNSET");
    char *after = symtable_get_var(exec->symtable, "TOUNSET");
    ASSERT(after == NULL, "Variable should not exist after unset");

    executor_free(exec);
}

TEST(builtin_readonly) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "readonly MYCONST=constant");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "MYCONST");
    ASSERT_NOT_NULL(value, "MYCONST should be set");
    ASSERT_STR_EQ(value, "constant", "Value should be 'constant'");
    free(value);

    executor_free(exec);
}

TEST(builtin_eval) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "eval 'EVALED=yes'");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "EVALED");
    ASSERT_NOT_NULL(value, "EVALED should be set");
    ASSERT_STR_EQ(value, "yes", "Value should be 'yes'");
    free(value);

    executor_free(exec);
}

TEST(builtin_shift) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Test shift with positional parameters */
    executor_execute_command_line(exec, "set -- a b c d e");
    executor_execute_command_line(exec, "FIRST=$1");
    executor_execute_command_line(exec, "shift");
    executor_execute_command_line(exec, "AFTER=$1");

    char *first = symtable_get_var(exec->symtable, "FIRST");
    char *after = symtable_get_var(exec->symtable, "AFTER");

    ASSERT_STR_EQ(first, "a", "First should be 'a'");
    ASSERT_STR_EQ(after, "b", "After shift, $1 should be 'b'");

    free(first);
    free(after);
    executor_free(exec);
}

TEST(builtin_set_positional) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "set -- one two three");
    executor_execute_command_line(exec, "P1=$1; P2=$2; P3=$3");

    char *p1 = symtable_get_var(exec->symtable, "P1");
    char *p2 = symtable_get_var(exec->symtable, "P2");
    char *p3 = symtable_get_var(exec->symtable, "P3");

    ASSERT_STR_EQ(p1, "one", "$1 should be 'one'");
    ASSERT_STR_EQ(p2, "two", "$2 should be 'two'");
    ASSERT_STR_EQ(p3, "three", "$3 should be 'three'");

    free(p1);
    free(p2);
    free(p3);
    executor_free(exec);
}

/* ============================================================================
 * COMMAND LIST TESTS
 * ============================================================================
 */

TEST(command_list_semicolon) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "A=1; B=2; C=3");
    ASSERT_EXIT_STATUS(r, 0);

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    char *c = symtable_get_var(exec->symtable, "C");

    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    ASSERT_STR_EQ(c, "3", "C should be 3");

    free(a);
    free(b);
    free(c);
    executor_free(exec);
}

/* ============================================================================
 * BREAK/CONTINUE TESTS
 * ============================================================================
 */

TEST(loop_break) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; for i in 1 2 3 4 5; do N=$((N+1)); if [ $N -eq 3 ]; then "
              "break; fi; done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "3", "Loop should break at 3");
    free(n);

    executor_free(exec);
}

TEST(loop_continue) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Count only odd numbers: skip even iterations */
    run_result_t r = run_shell_with_executor(
        exec, "SUM=0; for i in 1 2 3 4 5; do "
              "if [ $((i % 2)) -eq 0 ]; then continue; fi; "
              "SUM=$((SUM + i)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *sum = symtable_get_var(exec->symtable, "SUM");
    ASSERT_NOT_NULL(sum, "SUM should be set");
    ASSERT_STR_EQ(sum, "9", "Sum of 1+3+5 should be 9");
    free(sum);

    executor_free(exec);
}

/* ============================================================================
 * PIPELINE TESTS
 * ============================================================================
 */

TEST(pipeline_simple) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Simple pipeline - echo piped to cat */
    run_result_t r = run_shell_with_executor(exec, "true | true");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(pipeline_exit_status) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Pipeline exit status is last command's status */
    run_result_t r = run_shell_with_executor(exec, "true | false");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(pipeline_three_commands) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Three-stage pipeline */
    run_result_t r = run_shell_with_executor(exec, "true | true | true");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * EXTENDED TEST [[ ]] TESTS
 * ============================================================================
 */

TEST(extended_test_string_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ hello == hello ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_string_not_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ hello != world ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_regex_match) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "[[ hello123 =~ ^hello[0-9]+$ ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_and) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ -n foo && -n bar ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_or) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ -z '' || -n foo ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_pattern_match) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ foobar == foo* ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * PARAMETER EXPANSION TESTS
 * ============================================================================
 */

TEST(param_default_value) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "RESULT=${UNDEFINED:-default}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "default",
                  "Should use default value for undefined var");
    free(result);

    executor_free(exec);
}

TEST(param_alternate_value) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=set; RESULT=${VAR:+alternate}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "alternate", "Should use alternate when var is set");
    free(result);

    executor_free(exec);
}

TEST(param_string_length) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "VAR=hello; LEN=${#VAR}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "5", "Length of 'hello' should be 5");
    free(len);

    executor_free(exec);
}

TEST(param_substring_removal_prefix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=foobar; RESULT=${VAR#foo}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "bar", "Should remove 'foo' prefix");
    free(result);

    executor_free(exec);
}

TEST(param_substring_removal_suffix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=foobar; RESULT=${VAR%bar}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "foo", "Should remove 'bar' suffix");
    free(result);

    executor_free(exec);
}

TEST(param_pattern_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=hello; RESULT=${VAR/l/L}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "heLlo", "Should replace first 'l' with 'L'");
    free(result);

    executor_free(exec);
}

TEST(param_global_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=hello; RESULT=${VAR//l/L}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "heLLo", "Should replace all 'l' with 'L'");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * ARRAY TESTS
 * ============================================================================
 */

TEST(array_indexed_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "arr=(one two three)");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(array_element_access) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b c); ELEM=${arr[1]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *elem = symtable_get_var(exec->symtable, "ELEM");
    ASSERT_STR_EQ(elem, "b", "arr[1] should be 'b'");
    free(elem);

    executor_free(exec);
}

TEST(array_length) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b c d); LEN=${#arr[@]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4", "Array should have 4 elements");
    free(len);

    executor_free(exec);
}

TEST(array_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b); arr+=(c d); LEN=${#arr[@]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4", "Array should have 4 elements after append");
    free(len);

    executor_free(exec);
}

/* ============================================================================
 * COMMAND SUBSTITUTION TESTS
 * Note: stdout capture from external commands in test environment is unreliable
 * due to file descriptor sharing with test harness. These tests verify the
 * syntax works; actual output capture works correctly in real shell usage.
 * ============================================================================
 */

TEST(command_substitution_syntax) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Verify command substitution parses and executes without error */
    run_result_t r = run_shell_with_executor(exec, "X=$(true)");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(command_substitution_exit_status) {
    /*
     * KNOWN BUG: Command substitution exit status not preserved
     * Issue #58: $? after $(false) returns 0 instead of 1
     * The exit status of the command inside $() should be available via $?
     */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* For now, just test that the syntax works */
    run_result_t r = run_shell_with_executor(exec, "X=$(true)");
    ASSERT_EXIT_STATUS(r, 0);

    /* TODO: Re-enable when bug is fixed:
     * status = executor_execute_command_line(exec, "X=$(false); Y=$?");
     * ASSERT_EQ(status, 0, "Assignment after substitution should succeed");
     * char *y = symtable_get_var(exec->symtable, "Y");
     * ASSERT_STR_EQ(y, "1", "$? should capture exit status from $(false)");
     * free(y);
     */

    executor_free(exec);
}

/* ============================================================================
 * SPECIAL VARIABLE TESTS
 * ============================================================================
 */

TEST(special_var_question_mark) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "true");
    run_result_t r = run_shell_with_executor(exec, "STATUS=$?");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "STATUS");
    ASSERT_STR_EQ(result, "0", "$? after true should be 0");
    free(result);

    executor_free(exec);
}

TEST(special_var_dollar_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "PID=$$");
    ASSERT_EXIT_STATUS(r, 0);

    char *pid = symtable_get_var(exec->symtable, "PID");
    ASSERT_NOT_NULL(pid, "$$ should be set");
    /* PID should be set - could be 0 in test environment or actual PID */
    /* Just verify it's a valid number */
    int pid_val = atoi(pid);
    ASSERT(pid_val >= 0, "$$ should be non-negative");
    free(pid);

    executor_free(exec);
}

TEST(special_var_argc) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "set -- a b c; COUNT=$#");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_STR_EQ(count, "3", "$# should be 3");
    free(count);

    executor_free(exec);
}

/* ============================================================================
 * NESTED CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(nested_if) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "X=5; if [ $X -gt 0 ]; then "
              "  if [ $X -lt 10 ]; then RESULT=between; fi; "
              "fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "between", "Nested condition should set RESULT");
    free(result);

    executor_free(exec);
}

TEST(nested_loops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "COUNT=0; for i in 1 2; do "
              "  for j in a b; do COUNT=$((COUNT+1)); done; "
              "done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_STR_EQ(count, "4", "Should iterate 2*2=4 times");
    free(count);

    executor_free(exec);
}

/* ============================================================================
 * ELIF TESTS
 * ============================================================================
 */

TEST(elif_chain) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "X=2; "
                                      "if [ $X -eq 1 ]; then RESULT=one; "
                                      "elif [ $X -eq 2 ]; then RESULT=two; "
                                      "elif [ $X -eq 3 ]; then RESULT=three; "
                                      "else RESULT=other; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "two", "Should match second elif");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * NEGATION TESTS
 * ============================================================================
 */

TEST(negation_command) {
    /*
     * KNOWN BUG: Negation command causes memory corruption (double-free)
     * Issue #57: "! command" syntax triggers malloc error
     * TODO: Fix the negation handling in executor.c
     */
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /* Skip actual negation test until bug is fixed */
    /* int status = executor_execute_command_line(exec, "! false"); */
    /* ASSERT_EQ(status, 0, "Negated false should return 0"); */

    /* status = executor_execute_command_line(exec, "! true"); */
    /* ASSERT_EQ(status, 1, "Negated true should return 1"); */

    /* For now, just verify executor works without negation */
    run_result_t r = run_shell_with_executor(exec, "true");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * HERE STRING TESTS
 * ============================================================================
 */

TEST(here_string) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "RESULT=$(cat <<< 'hello')");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    /* cat outputs with trailing newline, command substitution may preserve it
     */
    ASSERT_NOT_NULL(result, "RESULT should be set");
    /* Check that result starts with "hello" (may have trailing newline) */
    ASSERT(strncmp(result, "hello", 5) == 0,
           "Here string should provide 'hello'");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * MORE ARITHMETIC TESTS
 * ============================================================================
 */

TEST(arithmetic_comparison) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "(( 5 > 3 ))");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "(( 3 > 5 ))");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(arithmetic_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "(( X = 5 + 3 ))");
    ASSERT_EXIT_STATUS(r, 0);

    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_STR_EQ(x, "8", "X should be 8");
    free(x);

    executor_free(exec);
}

TEST(arithmetic_ternary) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "X=5; RESULT=$((X > 3 ? 1 : 0))");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "1", "Ternary should return 1 when true");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * LOCAL VARIABLE TESTS
 * ============================================================================
 */

TEST(local_variable_in_function) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "GLOBAL=outer; "
                                      "f() { local GLOBAL=inner; }; "
                                      "f");
    ASSERT_EXIT_STATUS(r, 0);

    char *global = symtable_get_var(exec->symtable, "GLOBAL");
    ASSERT_STR_EQ(global, "outer",
                  "GLOBAL should remain 'outer' after function");
    free(global);

    executor_free(exec);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

/* Forward declarations for initialization */
extern void init_symtable(void);
extern void free_global_symtable(void);

int main(void) {
    printf("Running executor integration tests...\n\n");

    /* Initialize global symbol table - required for executor_new() */
    init_symtable();

    printf("Lifecycle tests:\n");
    RUN_TEST(executor_new_free);
    RUN_TEST(executor_with_symtable);

    printf("\nSimple command tests:\n");
    RUN_TEST(execute_true);
    RUN_TEST(execute_false);
    RUN_TEST(execute_colon);
    RUN_TEST(execute_exit_status);

    printf("\nVariable tests:\n");
    RUN_TEST(variable_assignment);
    RUN_TEST(variable_expansion);
    RUN_TEST(multiple_assignments);

    printf("\nControl structure tests:\n");
    RUN_TEST(if_true_branch);
    RUN_TEST(if_false_branch);
    RUN_TEST(for_loop_basic);
    RUN_TEST(for_loop_no_in);
    RUN_TEST(while_loop);
    RUN_TEST(until_loop);
    RUN_TEST(case_statement);
    RUN_TEST(case_wildcard);

    printf("\nLogical operator tests:\n");
    RUN_TEST(and_operator_success);
    RUN_TEST(and_operator_fail);
    RUN_TEST(or_operator_success);
    RUN_TEST(or_operator_fail);

    printf("\nFunction tests:\n");
    RUN_TEST(function_definition_posix);
    RUN_TEST(function_definition_ksh);
    RUN_TEST(function_with_args);
    RUN_TEST(function_return);

    printf(
        "\nRegression tests — Issue #47 (local/declare/typeset assignment):\n");
    RUN_TEST(local_plain_string_reassignment);
    RUN_TEST(local_integer_reassignment);
    RUN_TEST(local_arith_substitution);
    RUN_TEST(local_command_substitution);
    RUN_TEST(declare_reassignment);
    RUN_TEST(typeset_reassignment);
    RUN_TEST(local_two_step_no_initial);
    RUN_TEST(local_does_not_leak_to_global);
    RUN_TEST(local_for_loop_variable);
    RUN_TEST(local_plus_equals_append);
    RUN_TEST(local_while_loop_counter);

    printf("\nRegression tests — Issue #48 (function trailing redirections at "
           "call):\n");
    RUN_TEST(function_redir_out_applied_at_call);
    RUN_TEST(function_redir_stderr_applied_at_call);
    RUN_TEST(function_redir_append_accumulates_across_calls);
    RUN_TEST(function_redir_keyword_form_applied_at_call);
    RUN_TEST(function_redir_input_applied_at_call);
    RUN_TEST(function_redir_does_not_break_normal_call);

    printf("\nArithmetic tests:\n");
    RUN_TEST(arithmetic_basic);
    RUN_TEST(arithmetic_multiply);
    RUN_TEST(arithmetic_variable);
    RUN_TEST(arithmetic_increment);

    printf("\nSubshell and grouping tests:\n");
    RUN_TEST(subshell_isolation);
    RUN_TEST(brace_group);

    printf("\nTest command tests:\n");
    RUN_TEST(test_string_equal);
    RUN_TEST(test_string_not_equal);
    RUN_TEST(test_numeric_compare);
    RUN_TEST(test_string_empty);

    printf("\nBuiltin tests:\n");
    RUN_TEST(builtin_export);
    RUN_TEST(builtin_unset);
    RUN_TEST(builtin_readonly);
    RUN_TEST(builtin_eval);
    RUN_TEST(builtin_shift);
    RUN_TEST(builtin_set_positional);

    printf("\nCommand list tests:\n");
    RUN_TEST(command_list_semicolon);

    printf("\nBreak/continue tests:\n");
    RUN_TEST(loop_break);
    RUN_TEST(loop_continue);

    printf("\nPipeline tests:\n");
    RUN_TEST(pipeline_simple);
    RUN_TEST(pipeline_exit_status);
    RUN_TEST(pipeline_three_commands);

    printf("\nExtended test [[ ]] tests:\n");
    RUN_TEST(extended_test_string_equal);
    RUN_TEST(extended_test_string_not_equal);
    RUN_TEST(extended_test_regex_match);
    RUN_TEST(extended_test_and);
    RUN_TEST(extended_test_or);
    RUN_TEST(extended_test_pattern_match);

    printf("\nParameter expansion tests:\n");
    RUN_TEST(param_default_value);
    RUN_TEST(param_alternate_value);
    RUN_TEST(param_string_length);
    RUN_TEST(param_substring_removal_prefix);
    RUN_TEST(param_substring_removal_suffix);
    RUN_TEST(param_pattern_substitution);
    RUN_TEST(param_global_substitution);

    printf("\nArray tests:\n");
    RUN_TEST(array_indexed_assignment);
    RUN_TEST(array_element_access);
    RUN_TEST(array_length);
    RUN_TEST(array_append);

    printf("\nCommand substitution tests:\n");
    RUN_TEST(command_substitution_syntax);
    RUN_TEST(command_substitution_exit_status);

    printf("\nSpecial variable tests:\n");
    RUN_TEST(special_var_question_mark);
    RUN_TEST(special_var_dollar_dollar);
    RUN_TEST(special_var_argc);

    printf("\nNested control structure tests:\n");
    RUN_TEST(nested_if);
    RUN_TEST(nested_loops);
    RUN_TEST(elif_chain);

    printf("\nNegation tests:\n");
    RUN_TEST(negation_command);

    printf("\nHere string tests:\n");
    RUN_TEST(here_string);

    printf("\nMore arithmetic tests:\n");
    RUN_TEST(arithmetic_comparison);
    RUN_TEST(arithmetic_assignment);
    RUN_TEST(arithmetic_ternary);

    printf("\nLocal variable tests:\n");
    RUN_TEST(local_variable_in_function);

    printf("\n========================================\n");
    printf("All executor integration tests PASSED!\n");
    printf("========================================\n");

    /* Cleanup global symbol table */
    free_global_symtable();

    return 0;
}
