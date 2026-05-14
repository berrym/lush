/**
 * @file test_builtins.c
 * @brief Unit tests for shell builtin commands
 *
 * Tests individual builtin commands to verify correct behavior,
 * error handling, and POSIX compliance.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "builtins.h"
#include "executor.h"
#include "symtable.h"
#include "test_framework.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */

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
 * TRUE/FALSE BUILTIN TESTS
 * ============================================================================
 */

TEST(bin_true_returns_zero) {
    char *argv[] = {"true", NULL};
    int result = bin_true(1, argv);
    ASSERT_EQ(result, 0, "true should return 0");
}

TEST(bin_false_returns_one) {
    char *argv[] = {"false", NULL};
    int result = bin_false(1, argv);
    ASSERT_EQ(result, 1, "false should return 1");
}

TEST(bin_true_ignores_args) {
    char *argv[] = {"true", "arg1", "arg2", NULL};
    int result = bin_true(3, argv);
    ASSERT_EQ(result, 0, "true should ignore arguments and return 0");
}

TEST(bin_false_ignores_args) {
    char *argv[] = {"false", "arg1", "arg2", NULL};
    int result = bin_false(3, argv);
    ASSERT_EQ(result, 1, "false should ignore arguments and return 1");
}

/* ============================================================================
 * COLON BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(colon_returns_zero) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, ":", 1);
    ASSERT_EQ(status, 0, ": should return 0");

    teardown_executor(exec);
}

TEST(colon_with_args) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, ": arg1 arg2 arg3", 1);
    ASSERT_EQ(status, 0, ": should return 0 even with arguments");

    teardown_executor(exec);
}

/* ============================================================================
 * TEST BUILTIN TESTS
 * ============================================================================
 */

TEST(bin_test_empty_args_is_false) {
    char *argv[] = {"test", NULL};
    int result = bin_test(1, argv);
    ASSERT_EQ(result, 1, "test with no args should be false");
}

TEST(bin_test_nonempty_string_is_true) {
    char *argv[] = {"test", "hello", NULL};
    int result = bin_test(2, argv);
    ASSERT_EQ(result, 0, "test 'hello' should be true");
}

TEST(bin_test_empty_string_is_false) {
    char *argv[] = {"test", "", NULL};
    int result = bin_test(2, argv);
    ASSERT_EQ(result, 1, "test '' should be false");
}

TEST(bin_test_z_empty_string) {
    char *argv[] = {"test", "-z", "", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -z '' should be true");
}

TEST(bin_test_z_nonempty_string) {
    char *argv[] = {"test", "-z", "hello", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 1, "test -z 'hello' should be false");
}

TEST(bin_test_n_empty_string) {
    char *argv[] = {"test", "-n", "", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 1, "test -n '' should be false");
}

TEST(bin_test_n_nonempty_string) {
    char *argv[] = {"test", "-n", "hello", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -n 'hello' should be true");
}

TEST(bin_test_string_equal) {
    char *argv[] = {"test", "abc", "=", "abc", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 'abc' = 'abc' should be true");
}

TEST(bin_test_string_not_equal) {
    char *argv[] = {"test", "abc", "=", "def", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 1, "test 'abc' = 'def' should be false");
}

TEST(bin_test_string_neq_operator) {
    char *argv[] = {"test", "abc", "!=", "def", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 'abc' != 'def' should be true");
}

TEST(bin_test_numeric_eq) {
    char *argv[] = {"test", "42", "-eq", "42", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 42 -eq 42 should be true");
}

TEST(bin_test_numeric_ne) {
    char *argv[] = {"test", "42", "-ne", "43", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 42 -ne 43 should be true");
}

TEST(bin_test_numeric_lt) {
    char *argv[] = {"test", "5", "-lt", "10", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 5 -lt 10 should be true");
}

TEST(bin_test_numeric_gt) {
    char *argv[] = {"test", "10", "-gt", "5", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 10 -gt 5 should be true");
}

TEST(bin_test_numeric_le) {
    char *argv[] = {"test", "5", "-le", "5", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 5 -le 5 should be true");
}

TEST(bin_test_numeric_ge) {
    char *argv[] = {"test", "10", "-ge", "5", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test 10 -ge 5 should be true");
}

TEST(bin_test_negation) {
    char *argv[] = {"test", "!", "hello", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 1, "test ! 'hello' should be false");
}

TEST(bin_test_double_negation) {
    char *argv[] = {"test", "!", "!", "hello", NULL};
    int result = bin_test(4, argv);
    ASSERT_EQ(result, 0, "test ! ! 'hello' should be true");
}

TEST(bin_test_file_exists) {
    /* Test -e on a file that exists */
    char *argv[] = {"test", "-e", "/tmp", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -e /tmp should be true");
}

TEST(bin_test_file_not_exists) {
    char *argv[] = {"test", "-e", "/nonexistent_path_xyz_123", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 1, "test -e /nonexistent should be false");
}

TEST(bin_test_directory) {
    char *argv[] = {"test", "-d", "/tmp", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -d /tmp should be true");
}

TEST(bin_test_regular_file) {
    /* Test on /etc/passwd which should exist as regular file */
    char *argv[] = {"test", "-f", "/etc/passwd", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -f /etc/passwd should be true");
}

TEST(bin_test_readable) {
    char *argv[] = {"test", "-r", "/etc/passwd", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "test -r /etc/passwd should be true");
}

TEST(bin_test_bracket_form) {
    /* Test [ ... ] form */
    char *argv[] = {"[", "hello", "]", NULL};
    int result = bin_test(3, argv);
    ASSERT_EQ(result, 0, "[ hello ] should be true");
}

TEST(bin_test_bracket_missing_close) {
    /* Test [ ... without ] should error */
    char *argv[] = {"[", "hello", NULL};
    int result = bin_test(2, argv);
    ASSERT_EQ(result, 2, "[ without ] should return error status 2");
}

/* ============================================================================
 * PWD BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(pwd_returns_directory) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "pwd", 1);
    ASSERT_EQ(status, 0, "pwd should succeed");

    teardown_executor(exec);
}

TEST(pwd_logical_option) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "pwd -L", 1);
    ASSERT_EQ(status, 0, "pwd -L should succeed");

    teardown_executor(exec);
}

TEST(pwd_physical_option) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "pwd -P", 1);
    ASSERT_EQ(status, 0, "pwd -P should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * CD BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(cd_to_tmp) {
    executor_t *exec = setup_executor();
    char *original_dir = getcwd(NULL, 0);

    int status = executor_execute_command_line(exec, "cd /tmp", 1);
    ASSERT_EQ(status, 0, "cd /tmp should succeed");

    /* Verify we're in /tmp */
    char *current = getcwd(NULL, 0);
    ASSERT_NOT_NULL(current, "getcwd should work");
    /* /tmp might be a symlink to /private/tmp on macOS */
    ASSERT(strstr(current, "tmp") != NULL, "Should be in tmp directory");
    free(current);

    /* Return to original directory */
    ASSERT_EQ(chdir(original_dir), 0, "restore original cwd");
    free(original_dir);

    teardown_executor(exec);
}

TEST(cd_to_home) {
    executor_t *exec = setup_executor();
    char *original_dir = getcwd(NULL, 0);
    char *home = getenv("HOME");

    if (home) {
        int status = executor_execute_command_line(exec, "cd", 1);
        ASSERT_EQ(status, 0, "cd with no args should succeed");
    }

    /* Return to original directory */
    ASSERT_EQ(chdir(original_dir), 0, "restore original cwd");
    free(original_dir);

    teardown_executor(exec);
}

TEST(cd_nonexistent_fails) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "cd /nonexistent_dir_xyz", 1);
    ASSERT_EQ(status, 1, "cd to nonexistent directory should fail");

    teardown_executor(exec);
}

TEST(cd_dash_oldpwd) {
    executor_t *exec = setup_executor();
    char *original_dir = getcwd(NULL, 0);

    /* Go to /tmp first, then to /var - this sets OLDPWD to /tmp */
    executor_execute_command_line(exec, "cd /tmp", 1);
    executor_execute_command_line(exec, "cd /var", 1);

    /* Now cd - should go back to /tmp */
    int status = executor_execute_command_line(exec, "cd -", 1);
    ASSERT_EQ(status, 0, "cd - should succeed");

    /* Verify we're back in tmp (might be /private/tmp on macOS) */
    char *current = getcwd(NULL, 0);
    ASSERT(strstr(current, "tmp") != NULL, "cd - should go to OLDPWD");
    free(current);

    /* Restore */
    ASSERT_EQ(chdir(original_dir), 0, "restore original cwd");
    free(original_dir);

    teardown_executor(exec);
}

/* ============================================================================
 * EXPORT BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(export_new_variable) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "export TESTVAR=testvalue", 1);
    ASSERT_EQ(status, 0, "export TESTVAR=testvalue should succeed");

    /* Check that the variable is set */
    char *value = symtable_get_var(exec->symtable, "TESTVAR");
    ASSERT_NOT_NULL(value, "TESTVAR should be set");
    ASSERT_STR_EQ(value, "testvalue", "TESTVAR should have correct value");
    free(value);

    teardown_executor(exec);
}

TEST(export_existing_variable) {
    executor_t *exec = setup_executor();

    /* First set the variable */
    executor_execute_command_line(exec, "MYEXPORT=myvalue", 1);

    /* Then export it */
    int status = executor_execute_command_line(exec, "export MYEXPORT", 1);
    ASSERT_EQ(status, 0, "export existing variable should succeed");

    /* Verify it's still set correctly */
    char *value = symtable_get_var(exec->symtable, "MYEXPORT");
    ASSERT_NOT_NULL(value, "MYEXPORT should be set");
    ASSERT_STR_EQ(value, "myvalue", "MYEXPORT should retain value");
    free(value);

    teardown_executor(exec);
}

TEST(export_invalid_identifier) {
    executor_t *exec = setup_executor();

    /* Invalid variable name starting with digit */
    int status =
        executor_execute_command_line(exec, "export 1INVALID=value", 1);
    ASSERT_EQ(status, 1, "export with invalid identifier should fail");

    teardown_executor(exec);
}

/* ============================================================================
 * UNSET BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(unset_variable) {
    executor_t *exec = setup_executor();

    /* Set a variable */
    executor_execute_command_line(exec, "TOBEDELETED=value", 1);

    /* Verify it exists */
    char *value = symtable_get_var(exec->symtable, "TOBEDELETED");
    ASSERT_NOT_NULL(value, "Variable should be set initially");
    free(value);

    /* Unset it */
    int status = executor_execute_command_line(exec, "unset TOBEDELETED", 1);
    ASSERT_EQ(status, 0, "unset should succeed");

    /* Verify it's gone */
    value = symtable_get_var(exec->symtable, "TOBEDELETED");
    ASSERT_NULL(value, "Variable should be unset");

    teardown_executor(exec);
}

/* ============================================================================
 * TYPE BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(type_builtin_command) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "type echo", 1);
    ASSERT_EQ(status, 0, "type echo should succeed (echo is builtin)");

    teardown_executor(exec);
}

TEST(type_external_command) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "type ls", 1);
    ASSERT_EQ(status, 0, "type ls should succeed");

    teardown_executor(exec);
}

TEST(type_nonexistent_command) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "type nonexistent_cmd_xyz", 1);
    ASSERT_EQ(status, 1, "type nonexistent command should fail");

    teardown_executor(exec);
}

TEST(type_t_option) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "type -t true", 1);
    ASSERT_EQ(status, 0, "type -t true should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * ECHO BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(echo_simple) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "echo hello", 1);
    ASSERT_EQ(status, 0, "echo hello should succeed");

    teardown_executor(exec);
}

TEST(echo_multiple_args) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "echo hello world", 1);
    ASSERT_EQ(status, 0, "echo hello world should succeed");

    teardown_executor(exec);
}

TEST(echo_no_newline) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "echo -n hello", 1);
    ASSERT_EQ(status, 0, "echo -n should succeed");

    teardown_executor(exec);
}

TEST(echo_escape_sequences) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "echo -e 'hello\\nworld'", 1);
    ASSERT_EQ(status, 0, "echo -e with escapes should succeed");

    teardown_executor(exec);
}

TEST(echo_no_escapes) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "echo -E 'hello\\nworld'", 1);
    ASSERT_EQ(status, 0, "echo -E should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * PRINTF BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(printf_string) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "printf '%s' hello", 1);
    ASSERT_EQ(status, 0, "printf %s should succeed");

    teardown_executor(exec);
}

TEST(printf_integer) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "printf '%d' 42", 1);
    ASSERT_EQ(status, 0, "printf %d should succeed");

    teardown_executor(exec);
}

TEST(printf_hex) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "printf '%x' 255", 1);
    ASSERT_EQ(status, 0, "printf %x should succeed");

    teardown_executor(exec);
}

TEST(printf_width) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "printf '%10s' hello", 1);
    ASSERT_EQ(status, 0, "printf with width should succeed");

    teardown_executor(exec);
}

TEST(printf_escape_newline) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "printf 'line1\\nline2'", 1);
    ASSERT_EQ(status, 0, "printf with \\n should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * EVAL BUILTIN TESTS (via executor)
 * ============================================================================
 */

TEST(eval_simple) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "eval echo hello", 1);
    ASSERT_EQ(status, 0, "eval echo hello should succeed");

    teardown_executor(exec);
}

TEST(eval_variable_expansion) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "CMD=echo", 1);
    int status = executor_execute_command_line(exec, "eval $CMD hello", 1);
    ASSERT_EQ(status, 0, "eval $CMD should expand and execute");

    teardown_executor(exec);
}

TEST(eval_no_args) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "eval", 1);
    ASSERT_EQ(status, 0, "eval with no args should succeed with 0");

    teardown_executor(exec);
}

/* ============================================================================
 * SHIFT BUILTIN TESTS
 * ============================================================================
 */

TEST(shift_default) {
    executor_t *exec = setup_executor();

    /* shift requires positional parameters available; overshift is a
     * diagnostic error per POSIX (matching dash, bash, zsh). Set up
     * three positional params, then shift by the default count of 1. */
    executor_execute_command_line(exec, "set -- a b c", 1);
    int status = executor_execute_command_line(exec, "shift", 1);
    ASSERT_EQ(status, 0, "shift should succeed when params available");

    teardown_executor(exec);
}

TEST(shift_explicit_count) {
    executor_t *exec = setup_executor();

    /* Set up five positional params, then shift by 2. */
    executor_execute_command_line(exec, "set -- a b c d e", 1);
    int status = executor_execute_command_line(exec, "shift 2", 1);
    ASSERT_EQ(status, 0, "shift 2 should succeed");

    teardown_executor(exec);
}

TEST(shift_overshift_errors) {
    executor_t *exec = setup_executor();

    /* Overshift (count > $#) must produce a diagnostic and return
     * non-zero, matching dash/bash/zsh and the POSIX convention. */
    executor_execute_command_line(exec, "set -- a b", 1);
    int status = executor_execute_command_line(exec, "shift 5", 1);
    ASSERT_EQ(status, 1, "shift 5 with 2 params should fail");

    teardown_executor(exec);
}

TEST(shift_invalid_arg) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "shift abc", 1);
    ASSERT_EQ(status, 1, "shift abc should fail");

    teardown_executor(exec);
}

/* ============================================================================
 * RETURN BUILTIN TESTS
 * ============================================================================
 */

TEST(return_outside_function) {
    executor_t *exec = setup_executor();

    /* return outside function should fail */
    int status = executor_execute_command_line(exec, "return", 1);
    ASSERT_EQ(status, 1, "return outside function should fail");

    teardown_executor(exec);
}

TEST(return_in_function) {
    executor_t *exec = setup_executor();

    /* Define and call function with return */
    executor_execute_command_line(exec, "testfunc() { return 5; }", 1);
    int status = executor_execute_command_line(exec, "testfunc", 1);
    ASSERT_EQ(status, 5, "Function should return 5");

    teardown_executor(exec);
}

TEST(return_default_status) {
    executor_t *exec = setup_executor();

    /* Function with return (no value) should use last exit status */
    executor_execute_command_line(exec, "testfunc2() { true; return; }", 1);
    int status = executor_execute_command_line(exec, "testfunc2", 1);
    ASSERT_EQ(status, 0,
              "Function with plain return should return last status");

    teardown_executor(exec);
}

/* ============================================================================
 * BREAK/CONTINUE BUILTIN TESTS
 * ============================================================================
 */

TEST(break_outside_loop) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "break", 1);
    ASSERT_EQ(status, 1, "break outside loop should fail");

    teardown_executor(exec);
}

TEST(continue_outside_loop) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "continue", 1);
    ASSERT_EQ(status, 1, "continue outside loop should fail");

    teardown_executor(exec);
}

TEST(break_in_loop) {
    executor_t *exec = setup_executor();

    /* Loop with break should exit early */
    int status = executor_execute_command_line(
        exec, "for i in 1 2 3; do if [ $i -eq 2 ]; then break; fi; done", 1);
    ASSERT_EQ(status, 0, "for loop with break should succeed");

    teardown_executor(exec);
}

TEST(continue_in_loop) {
    executor_t *exec = setup_executor();

    /* Loop with continue should skip to next iteration */
    int status = executor_execute_command_line(
        exec, "for i in 1 2 3; do if [ $i -eq 2 ]; then continue; fi; done", 1);
    ASSERT_EQ(status, 0, "for loop with continue should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * DECLARE/LOCAL BUILTIN TESTS
 * ============================================================================
 */

TEST(declare_variable) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "declare MYVAR=hello", 1);
    ASSERT_EQ(status, 0, "declare should succeed");

    char *value = symtable_get_var(exec->symtable, "MYVAR");
    ASSERT_NOT_NULL(value, "MYVAR should be set");
    ASSERT_STR_EQ(value, "hello", "MYVAR should have correct value");
    free(value);

    teardown_executor(exec);
}

TEST(local_outside_function) {
    executor_t *exec = setup_executor();

    /* local outside function might succeed but has no effect */
    int status = executor_execute_command_line(exec, "local LOCALVAR=test", 1);
    /* Some shells return error, some succeed - just verify it runs */
    (void)status;

    teardown_executor(exec);
}

TEST(local_in_function) {
    executor_t *exec = setup_executor();

    /* Define function with local variable */
    executor_execute_command_line(
        exec, "testlocal() { local X=inside; echo $X; }", 1);
    int status = executor_execute_command_line(exec, "testlocal", 1);
    ASSERT_EQ(status, 0, "Function with local should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * READONLY BUILTIN TESTS
 * ============================================================================
 */

TEST(readonly_variable) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "readonly ROVAR=constant", 1);
    ASSERT_EQ(status, 0, "readonly should succeed");

    char *value = symtable_get_var(exec->symtable, "ROVAR");
    ASSERT_NOT_NULL(value, "ROVAR should be set");
    ASSERT_STR_EQ(value, "constant", "ROVAR should have correct value");
    free(value);

    teardown_executor(exec);
}

TEST(readonly_prevents_modification) {
    /*
     * KNOWN LIMITATION: readonly enforcement not yet implemented
     * The readonly builtin sets variables but doesn't prevent modification.
     * See bin_readonly() comments in builtins.c - "simplified implementation"
     * TODO: Implement readonly enforcement in symbol table
     */
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "readonly ROVAR2=original", 1);

    /* For now, just verify readonly command works - enforcement is TODO */
    char *value = symtable_get_var(exec->symtable, "ROVAR2");
    ASSERT_NOT_NULL(value, "ROVAR2 should be set");
    ASSERT_STR_EQ(value, "original", "ROVAR2 should have initial value");
    free(value);

    teardown_executor(exec);
}

/* ============================================================================
 * COMMAND BUILTIN TESTS
 * ============================================================================
 */

TEST(command_runs_external) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "command true", 1);
    ASSERT_EQ(status, 0, "command true should succeed");

    teardown_executor(exec);
}

TEST(command_bypasses_alias) {
    executor_t *exec = setup_executor();

    /* Even if 'ls' were aliased, command ls should run the real ls */
    int status = executor_execute_command_line(exec, "command ls /tmp", 1);
    ASSERT_EQ(status, 0, "command ls should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * ALIAS BUILTIN TESTS
 * ============================================================================
 */

TEST(alias_definition) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "alias ll='ls -l'", 1);
    ASSERT_EQ(status, 0, "alias definition should succeed");

    teardown_executor(exec);
}

TEST(alias_list) {
    executor_t *exec = setup_executor();

    /* alias with no args should list aliases */
    int status = executor_execute_command_line(exec, "alias", 1);
    ASSERT_EQ(status, 0, "alias list should succeed");

    teardown_executor(exec);
}

TEST(unalias_removes) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "alias myalias='echo test'", 1);
    int status = executor_execute_command_line(exec, "unalias myalias", 1);
    ASSERT_EQ(status, 0, "unalias should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * HASH BUILTIN TESTS
 * ============================================================================
 */

TEST(hash_list) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "hash", 1);
    /* hash with no commands may return 0 or 1 depending on state */
    (void)status;

    teardown_executor(exec);
}

TEST(hash_command) {
    executor_t *exec = setup_executor();

    /* Hash ls to remember its location */
    int status = executor_execute_command_line(exec, "hash ls", 1);
    ASSERT_EQ(status, 0, "hash ls should succeed");

    teardown_executor(exec);
}

TEST(hash_clear) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "hash -r", 1);
    ASSERT_EQ(status, 0, "hash -r should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * UMASK BUILTIN TESTS
 * ============================================================================
 */

TEST(umask_display) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "umask", 1);
    ASSERT_EQ(status, 0, "umask display should succeed");

    teardown_executor(exec);
}

TEST(umask_set) {
    executor_t *exec = setup_executor();

    /* Save current umask */
    mode_t old_mask = umask(0);
    umask(old_mask);

    int status = executor_execute_command_line(exec, "umask 022", 1);
    ASSERT_EQ(status, 0, "umask 022 should succeed");

    /* Restore */
    umask(old_mask);

    teardown_executor(exec);
}

/* ============================================================================
 * TRAP BUILTIN TESTS
 * ============================================================================
 */

TEST(trap_list) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "trap", 1);
    ASSERT_EQ(status, 0, "trap list should succeed");

    teardown_executor(exec);
}

TEST(trap_set_exit) {
    executor_t *exec = setup_executor();

    int status =
        executor_execute_command_line(exec, "trap 'echo exiting' EXIT", 1);
    ASSERT_EQ(status, 0, "trap EXIT should succeed");

    teardown_executor(exec);
}

TEST(trap_reset) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "trap 'echo test' INT", 1);
    int status = executor_execute_command_line(exec, "trap - INT", 1);
    ASSERT_EQ(status, 0, "trap - INT should reset trap");

    teardown_executor(exec);
}

/* ============================================================================
 * DIRECTORY STACK TESTS
 * ============================================================================
 */

TEST(pushd_and_popd) {
    executor_t *exec = setup_executor();
    char *original_dir = getcwd(NULL, 0);

    int status = executor_execute_command_line(exec, "pushd /tmp", 1);
    ASSERT_EQ(status, 0, "pushd /tmp should succeed");

    status = executor_execute_command_line(exec, "popd", 1);
    ASSERT_EQ(status, 0, "popd should succeed");

    ASSERT_EQ(chdir(original_dir), 0, "restore original cwd");
    free(original_dir);

    teardown_executor(exec);
}

TEST(dirs_command) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "dirs", 1);
    ASSERT_EQ(status, 0, "dirs should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * HELP BUILTIN TEST
 * ============================================================================
 */

TEST(help_command) {
    executor_t *exec = setup_executor();

    int status = executor_execute_command_line(exec, "help", 1);
    ASSERT_EQ(status, 0, "help should succeed");

    teardown_executor(exec);
}

/* ============================================================================
 * IS_BUILTIN FUNCTION TEST
 * ============================================================================
 */

TEST(is_builtin_true_for_builtins) {
    ASSERT(is_builtin("echo"), "echo should be a builtin");
    ASSERT(is_builtin("cd"), "cd should be a builtin");
    ASSERT(is_builtin("export"), "export should be a builtin");
    ASSERT(is_builtin("true"), "true should be a builtin");
    ASSERT(is_builtin("false"), "false should be a builtin");
    ASSERT(is_builtin("test"), "test should be a builtin");
    ASSERT(is_builtin("["), "[ should be a builtin");
    ASSERT(is_builtin(":"), ": should be a builtin");
}

TEST(is_builtin_false_for_external) {
    ASSERT(!is_builtin("ls"), "ls should not be a builtin");
    ASSERT(!is_builtin("grep"), "grep should not be a builtin");
    ASSERT(!is_builtin("nonexistent"), "nonexistent should not be a builtin");
}

/* ============================================================================
 * MODE BUILTIN TESTS
 * ============================================================================
 */

#include "lush.h"
#include "shell_mode.h"

extern int bin_mode(int argc, char **argv);

TEST(bin_mode_no_args_succeeds) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", NULL};
    int result = bin_mode(1, argv);
    ASSERT_EQ(result, 0, "mode with no args should return 0");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "mode with no args should not change the active mode");
}

TEST(bin_mode_switches_to_bash) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "bash", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 0, "mode bash should return 0");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_BASH,
              "mode bash should switch active mode to bash");
}

TEST(bin_mode_switches_to_posix_sets_legacy_flag) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "posix", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 0, "mode posix should return 0");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_POSIX,
              "mode posix should switch active mode to posix");
    ASSERT_TRUE(shell_opts.posix_mode,
                "shell_opts.posix_mode should be true after mode posix");
}

TEST(bin_mode_switches_to_zsh_clears_legacy_flag) {
    apply_mode_preset(SHELL_MODE_POSIX);
    char *argv[] = {"mode", "zsh", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 0, "mode zsh should return 0");
    ASSERT_FALSE(shell_opts.posix_mode,
                 "shell_opts.posix_mode should be false after mode zsh");
}

TEST(bin_mode_unknown_name_errors) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "frobnicate", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 1, "mode <unknown> should return 1");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "mode <unknown> should not change the active mode");
}

TEST(bin_mode_unknown_option_errors) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "--frobnicate", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 1, "mode --<unknown> should return 1");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "mode --<unknown> should not change the active mode");
}

TEST(bin_mode_too_many_args_errors) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "bash", "extra", NULL};
    int result = bin_mode(3, argv);
    ASSERT_EQ(result, 1, "mode <name> <extra> should return 1");
    ASSERT_EQ(shell_mode_get(), SHELL_MODE_LUSH,
              "mode with too many args should not change the active mode");
}

TEST(bin_mode_show_succeeds) {
    apply_mode_preset(SHELL_MODE_LUSH);
    char *argv[] = {"mode", "--show", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 0, "mode --show should return 0");
}

TEST(bin_mode_reset_drops_overrides) {
    apply_mode_preset(SHELL_MODE_LUSH);
    /* Apply a per-feature override that --reset should drop. */
    shell_feature_disable(FEATURE_INDEXED_ARRAYS);
    ASSERT_TRUE(shell_feature_is_overridden(FEATURE_INDEXED_ARRAYS),
                "override should be set before --reset");

    char *argv[] = {"mode", "--reset", NULL};
    int result = bin_mode(2, argv);
    ASSERT_EQ(result, 0, "mode --reset should return 0");
    ASSERT_FALSE(shell_feature_is_overridden(FEATURE_INDEXED_ARRAYS),
                 "override should be cleared after --reset");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("=== Builtin Command Tests ===\n\n");

    /* Initialize global symbol table - required for executor_new() */
    init_symtable();

    /* Initialize alias system */
    init_aliases();

    printf("--- true/false Tests ---\n");
    RUN_TEST(bin_true_returns_zero);
    RUN_TEST(bin_false_returns_one);
    RUN_TEST(bin_true_ignores_args);
    RUN_TEST(bin_false_ignores_args);

    printf("\n--- colon Tests ---\n");
    RUN_TEST(colon_returns_zero);
    RUN_TEST(colon_with_args);

    printf("\n--- test Builtin Tests ---\n");
    RUN_TEST(bin_test_empty_args_is_false);
    RUN_TEST(bin_test_nonempty_string_is_true);
    RUN_TEST(bin_test_empty_string_is_false);
    RUN_TEST(bin_test_z_empty_string);
    RUN_TEST(bin_test_z_nonempty_string);
    RUN_TEST(bin_test_n_empty_string);
    RUN_TEST(bin_test_n_nonempty_string);
    RUN_TEST(bin_test_string_equal);
    RUN_TEST(bin_test_string_not_equal);
    RUN_TEST(bin_test_string_neq_operator);
    RUN_TEST(bin_test_numeric_eq);
    RUN_TEST(bin_test_numeric_ne);
    RUN_TEST(bin_test_numeric_lt);
    RUN_TEST(bin_test_numeric_gt);
    RUN_TEST(bin_test_numeric_le);
    RUN_TEST(bin_test_numeric_ge);
    RUN_TEST(bin_test_negation);
    RUN_TEST(bin_test_double_negation);
    RUN_TEST(bin_test_file_exists);
    RUN_TEST(bin_test_file_not_exists);
    RUN_TEST(bin_test_directory);
    RUN_TEST(bin_test_regular_file);
    RUN_TEST(bin_test_readable);
    RUN_TEST(bin_test_bracket_form);
    RUN_TEST(bin_test_bracket_missing_close);

    printf("\n--- pwd Tests ---\n");
    RUN_TEST(pwd_returns_directory);
    RUN_TEST(pwd_logical_option);
    RUN_TEST(pwd_physical_option);

    printf("\n--- cd Tests ---\n");
    RUN_TEST(cd_to_tmp);
    RUN_TEST(cd_to_home);
    RUN_TEST(cd_nonexistent_fails);
    RUN_TEST(cd_dash_oldpwd);

    printf("\n--- export Tests ---\n");
    RUN_TEST(export_new_variable);
    RUN_TEST(export_existing_variable);
    RUN_TEST(export_invalid_identifier);

    printf("\n--- unset Tests ---\n");
    RUN_TEST(unset_variable);

    printf("\n--- type Tests ---\n");
    RUN_TEST(type_builtin_command);
    RUN_TEST(type_external_command);
    RUN_TEST(type_nonexistent_command);
    RUN_TEST(type_t_option);

    printf("\n--- echo Tests ---\n");
    RUN_TEST(echo_simple);
    RUN_TEST(echo_multiple_args);
    RUN_TEST(echo_no_newline);
    RUN_TEST(echo_escape_sequences);
    RUN_TEST(echo_no_escapes);

    printf("\n--- printf Tests ---\n");
    RUN_TEST(printf_string);
    RUN_TEST(printf_integer);
    RUN_TEST(printf_hex);
    RUN_TEST(printf_width);
    RUN_TEST(printf_escape_newline);

    printf("\n--- eval Tests ---\n");
    RUN_TEST(eval_simple);
    RUN_TEST(eval_variable_expansion);
    RUN_TEST(eval_no_args);

    printf("\n--- shift Tests ---\n");
    RUN_TEST(shift_default);
    RUN_TEST(shift_explicit_count);
    RUN_TEST(shift_overshift_errors);
    RUN_TEST(shift_invalid_arg);

    printf("\n--- return Tests ---\n");
    RUN_TEST(return_outside_function);
    RUN_TEST(return_in_function);
    RUN_TEST(return_default_status);

    printf("\n--- break/continue Tests ---\n");
    RUN_TEST(break_outside_loop);
    RUN_TEST(continue_outside_loop);
    RUN_TEST(break_in_loop);
    RUN_TEST(continue_in_loop);

    printf("\n--- declare/local Tests ---\n");
    RUN_TEST(declare_variable);
    RUN_TEST(local_outside_function);
    RUN_TEST(local_in_function);

    printf("\n--- readonly Tests ---\n");
    RUN_TEST(readonly_variable);
    RUN_TEST(readonly_prevents_modification);

    printf("\n--- command Tests ---\n");
    RUN_TEST(command_runs_external);
    RUN_TEST(command_bypasses_alias);

    printf("\n--- alias Tests ---\n");
    RUN_TEST(alias_definition);
    RUN_TEST(alias_list);
    RUN_TEST(unalias_removes);

    printf("\n--- hash Tests ---\n");
    RUN_TEST(hash_list);
    RUN_TEST(hash_command);
    RUN_TEST(hash_clear);

    printf("\n--- umask Tests ---\n");
    RUN_TEST(umask_display);
    RUN_TEST(umask_set);

    printf("\n--- trap Tests ---\n");
    RUN_TEST(trap_list);
    RUN_TEST(trap_set_exit);
    RUN_TEST(trap_reset);

    printf("\n--- Directory Stack Tests ---\n");
    RUN_TEST(pushd_and_popd);
    RUN_TEST(dirs_command);

    printf("\n--- help Tests ---\n");
    RUN_TEST(help_command);

    printf("\n--- is_builtin Tests ---\n");
    RUN_TEST(is_builtin_true_for_builtins);
    RUN_TEST(is_builtin_false_for_external);

    printf("\n--- mode Builtin Tests ---\n");
    RUN_TEST(bin_mode_no_args_succeeds);
    RUN_TEST(bin_mode_switches_to_bash);
    RUN_TEST(bin_mode_switches_to_posix_sets_legacy_flag);
    RUN_TEST(bin_mode_switches_to_zsh_clears_legacy_flag);
    RUN_TEST(bin_mode_unknown_name_errors);
    RUN_TEST(bin_mode_unknown_option_errors);
    RUN_TEST(bin_mode_too_many_args_errors);
    RUN_TEST(bin_mode_show_succeeds);
    RUN_TEST(bin_mode_reset_drops_overrides);

    return TEST_RESULT();
}
