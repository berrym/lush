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

TEST(cmdsub_leading_with_trailing_text) {
    /// An assignment value beginning with a command substitution followed by
    /// literal text concatenates rather than collapsing to empty (issue #467).
    /// Previously x=$(cmd):b ran the whole word "$(cmd):b" as a command and
    /// stored nothing.
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "A=$(echo a):b", 1);
    char *a = symtable_get_var(exec->symtable, "A");
    ASSERT_NOT_NULL(a, "A should be set");
    ASSERT_STR_EQ(a, "a:b", "$(echo a):b should concatenate to a:b");
    free(a);

    executor_execute_command_line(exec, "B=$(echo a)/lit", 1);
    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_NOT_NULL(b, "B should be set");
    ASSERT_STR_EQ(b, "a/lit", "$(echo a)/lit should concatenate to a/lit");
    free(b);

    executor_execute_command_line(exec, "C=`echo a`:b", 1);
    char *c = symtable_get_var(exec->symtable, "C");
    ASSERT_NOT_NULL(c, "C should be set");
    ASSERT_STR_EQ(c, "a:b", "`echo a`:b should concatenate to a:b");
    free(c);

    /// Arithmetic expansion is the same family: $((expr))<text> also
    /// concatenates.
    executor_execute_command_line(exec, "E=$((1+2)):z", 1);
    char *e = symtable_get_var(exec->symtable, "E");
    ASSERT_NOT_NULL(e, "E should be set");
    ASSERT_STR_EQ(e, "3:z", "$((1+2)):z should concatenate to 3:z");
    free(e);

    /// Bare substitution and bare arithmetic are unaffected.
    executor_execute_command_line(exec, "D=$(echo bare)", 1);
    char *d = symtable_get_var(exec->symtable, "D");
    ASSERT_NOT_NULL(d, "D should be set");
    ASSERT_STR_EQ(d, "bare", "bare $(echo bare) still expands to bare");
    free(d);

    executor_execute_command_line(exec, "F=$((2*3))", 1);
    char *f = symtable_get_var(exec->symtable, "F");
    ASSERT_NOT_NULL(f, "F should be set");
    ASSERT_STR_EQ(f, "6", "bare $((2*3)) still expands to 6");
    free(f);

    teardown_executor(exec);
}

TEST(cmdsub_with_quoted_paren) {
    /// A command substitution whose command contains a quoted `)` must not
    /// mis-terminate at that paren (issue #486). The tokenizer's paren scan for
    /// `$(...)` is quote-aware, so the substitution runs to its real closing
    /// `)` and the surrounding literal text concatenates. Previously the scan
    /// stopped at the quoted `)`, leaving a dangling quote that failed to
    /// parse.
    executor_t *exec = setup_executor();

    /// Leading literal + a double-quoted `)` inside, then a suffix.
    executor_execute_command_line(exec, "A=z$(echo \")\"):b", 1);
    char *a = symtable_get_var(exec->symtable, "A");
    ASSERT_NOT_NULL(a, "A should be set");
    ASSERT_STR_EQ(a, "z):b", "z$(echo \")\"):b should concatenate to z):b");
    free(a);

    /// Embedded substitution with a quoted `)`, literal on both sides.
    executor_execute_command_line(exec, "B=pre$(echo \")\")post", 1);
    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_NOT_NULL(b, "B should be set");
    ASSERT_STR_EQ(b, "pre)post",
                  "pre$(echo \")\")post should concatenate to pre)post");
    free(b);

    /// A single-quoted `)` inside the substitution behaves the same.
    executor_execute_command_line(exec, "C=$(echo ')')", 1);
    char *c = symtable_get_var(exec->symtable, "C");
    ASSERT_NOT_NULL(c, "C should be set");
    ASSERT_STR_EQ(c, ")", "$(echo ')') should expand to a literal )");
    free(c);

    /// An unquoted `(` / `)` inside a quoted argument is still balanced text,
    /// not a nesting change: the whole quoted run is one argument to echo.
    executor_execute_command_line(exec, "D=$(echo \"a(b)c\")", 1);
    char *d = symtable_get_var(exec->symtable, "D");
    ASSERT_NOT_NULL(d, "D should be set");
    ASSERT_STR_EQ(d, "a(b)c", "$(echo \"a(b)c\") should expand to a(b)c");
    free(d);

    teardown_executor(exec);
}

TEST(cmdsub_with_case_pattern_paren) {
    /// A `case` pattern's `)` inside a command substitution is case syntax, not
    /// the substitution's closing paren (issue #494). The boundary scan is
    /// structure-aware, so the whole `case ... esac` is captured.
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "A=$(case x in x) echo M;; esac)", 1);
    char *a = symtable_get_var(exec->symtable, "A");
    ASSERT_NOT_NULL(a, "A should be set");
    ASSERT_STR_EQ(a, "M", "case-pattern ) does not terminate the substitution");
    free(a);

    /// Multiple arms: every pattern `)` is internal to the substitution.
    executor_execute_command_line(
        exec, "B=$(case ab in a) echo A;; b) echo B;; *) echo S;; esac)", 1);
    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_NOT_NULL(b, "B should be set");
    ASSERT_STR_EQ(b, "S", "multi-arm case boundary");
    free(b);

    /// `esac` as an argument in an arm body is not the case terminator.
    executor_execute_command_line(exec, "C=$(case x in x) echo esac;; esac)",
                                  1);
    char *c = symtable_get_var(exec->symtable, "C");
    ASSERT_NOT_NULL(c, "C should be set");
    ASSERT_STR_EQ(c, "esac", "esac argument is not the case terminator");
    free(c);

    /// Nested case inside an arm body.
    executor_execute_command_line(
        exec, "D=$(case a in a) case c in c) echo AC;; esac;; esac)", 1);
    char *d = symtable_get_var(exec->symtable, "D");
    ASSERT_NOT_NULL(d, "D should be set");
    ASSERT_STR_EQ(d, "AC", "nested case boundary");
    free(d);

    /// The double-quoted `"$(...)"` scan path handles it too.
    executor_execute_command_line(exec, "E=\"$(case x in x) echo M;; esac)\"",
                                  1);
    char *e = symtable_get_var(exec->symtable, "E");
    ASSERT_NOT_NULL(e, "E should be set");
    ASSERT_STR_EQ(e, "M", "case in a double-quoted substitution");
    free(e);

    teardown_executor(exec);
}

TEST(cmdsub_fast_read_file) {
    /// $(<file) / `<file` reads the file's contents without forking a
    /// subshell (issue #564): a lone `<` input redirection in a command
    /// substitution expands to the file, trailing newlines stripped like
    /// any command substitution. bash and zsh agree; the idiom is
    /// advertised in ADVANCED_SCRIPTING_GUIDE.md.
    executor_t *exec = setup_executor();

    const char *path = "/tmp/lush_fastread_test_564.txt";
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f, "temp file should open for write");
    fputs("hello\nworld\n", f);
    fclose(f);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "RESULT=$(<%s)", path);
    executor_execute_command_line(exec, cmd, 1);
    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "hello\nworld",
                  "$(<file) reads contents, trailing newline stripped");
    free(result);

    /// Backtick spelling reads the same file.
    snprintf(cmd, sizeof(cmd), "BT=`<%s`", path);
    executor_execute_command_line(exec, cmd, 1);
    char *bt = symtable_get_var(exec->symtable, "BT");
    ASSERT_NOT_NULL(bt, "BT should be set");
    ASSERT_STR_EQ(bt, "hello\nworld", "`<file` reads contents");
    free(bt);

    /// The filename expands like a redirection target: a variable works.
    executor_execute_command_line(exec, "F=/tmp/lush_fastread_test_564.txt", 1);
    executor_execute_command_line(exec, "VR=$(<$F)", 1);
    char *vr = symtable_get_var(exec->symtable, "VR");
    ASSERT_NOT_NULL(vr, "VR should be set");
    ASSERT_STR_EQ(vr, "hello\nworld", "$(<$var) expands the filename");
    free(vr);

    /// The multi-line spelling -- newlines before/after the redirection --
    /// is still recognized (must not fall through to an empty result).
    snprintf(cmd, sizeof(cmd), "ML=$(\n  <%s\n)", path);
    executor_execute_command_line(exec, cmd, 1);
    char *ml = symtable_get_var(exec->symtable, "ML");
    ASSERT_NOT_NULL(ml, "ML should be set");
    ASSERT_STR_EQ(ml, "hello\nworld", "multi-line $(<file) reads the file");
    free(ml);

    remove(path);
    teardown_executor(exec);
}

TEST(cmdsub_fast_read_missing_and_fallthrough) {
    executor_t *exec = setup_executor();

    /// A missing file yields an empty substitution (an error is reported to
    /// stderr; the shell continues), like bash/zsh.
    executor_execute_command_line(exec,
                                  "MISS=$(</tmp/lush_nonexistent_564_zzzz)", 1);
    char *miss = symtable_get_var(exec->symtable, "MISS");
    ASSERT_NOT_NULL(miss, "MISS should be set");
    ASSERT_STR_EQ(miss, "", "$(<missing) expands empty");
    free(miss);

    /// A body that is not a lone input redirection still runs as a normal
    /// (forked) command substitution.
    executor_execute_command_line(exec, "OUT=$(echo forked)", 1);
    char *out = symtable_get_var(exec->symtable, "OUT");
    ASSERT_NOT_NULL(out, "OUT should be set");
    ASSERT_STR_EQ(out, "forked", "non-fast-read body still forks");
    free(out);

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

/// Compound bit/shift assignment operators (<<= >>= &= |= ^=). Each yields
/// the new value and updates the target; the 3-char <<=/>>= must not be
/// mis-read as <</>> followed by a stray =. Issue #563.
TEST(arith_shift_left_assign) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=8", 1);
    executor_execute_command_line(exec, "RESULT=$((X <<= 2))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "32", "8 <<= 2 = 32");
    free(result);

    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_NOT_NULL(x, "X should be set");
    ASSERT_STR_EQ(x, "32", "X updated to 32");
    free(x);

    teardown_executor(exec);
}

TEST(arith_shift_right_assign) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=64", 1);
    executor_execute_command_line(exec, "RESULT=$((X >>= 2))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "16", "64 >>= 2 = 16");
    free(result);

    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_NOT_NULL(x, "X should be set");
    ASSERT_STR_EQ(x, "16", "X updated to 16");
    free(x);

    teardown_executor(exec);
}

TEST(arith_bitand_assign) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=12", 1);
    executor_execute_command_line(exec, "RESULT=$((X &= 10))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "8", "12 &= 10 = 8");
    free(result);

    teardown_executor(exec);
}

TEST(arith_bitor_assign) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=5", 1);
    executor_execute_command_line(exec, "RESULT=$((X |= 2))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "7", "5 |= 2 = 7");
    free(result);

    teardown_executor(exec);
}

TEST(arith_bitxor_assign) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=6", 1);
    executor_execute_command_line(exec, "RESULT=$((X ^= 3))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "6 ^= 3 = 5");
    free(result);

    teardown_executor(exec);
}

/// The bare bit/shift operators must still parse as their binary selves
/// after the compound forms were added.
TEST(arith_bitshift_binary_unaffected) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(
        exec, "RESULT=$(( (1<<4) + (256>>2) + (6&3) + (4|1) + (5^1) ))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    /// 16 + 64 + 2 + 5 + 4 = 91
    ASSERT_STR_EQ(result, "91", "binary << >> & | ^ intact");
    free(result);

    teardown_executor(exec);
}

/// Right-associative chaining: A >>= (B += 1). B becomes 2, then A >>= 2.
TEST(arith_compound_assign_right_assoc) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "A=8", 1);
    executor_execute_command_line(exec, "B=1", 1);
    executor_execute_command_line(exec, "RESULT=$((A >>= B += 1))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "2", "8 >>= (1+=1 -> 2) = 2");
    free(result);

    char *a = symtable_get_var(exec->symtable, "A");
    ASSERT_STR_EQ(a, "2", "A updated to 2");
    free(a);

    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_STR_EQ(b, "2", "B updated to 2");
    free(b);

    teardown_executor(exec);
}

/// The (( )) command form reaches the arithmetic engine through a separate
/// token re-joiner than $(( )); the compound operators must work on that
/// surface too. Regression guard for the `^=` gap (the re-joiner's
/// space-insertion list omitted `^`, so `x ^= 3` reached the engine as
/// `x ^ = 3`). Issue #563.
TEST(arith_compound_assign_command_form) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=6", 1);
    executor_execute_command_line(exec, "(( X ^= 3 ))", 1);
    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_NOT_NULL(x, "X should be set");
    ASSERT_STR_EQ(x, "5", "(( X ^= 3 )) -> 5");
    free(x);

    executor_execute_command_line(exec, "Y=8", 1);
    executor_execute_command_line(exec, "(( Y <<= 2 ))", 1);
    char *y = symtable_get_var(exec->symtable, "Y");
    ASSERT_STR_EQ(y, "32", "(( Y <<= 2 )) -> 32");
    free(y);

    teardown_executor(exec);
}

/// A literal on the left of a compound assignment is not an assignable
/// target: the operator is rejected without mutating anything or crashing,
/// and the engine recovers for the next expression. Issue #563.
TEST(arith_compound_assign_non_lvalue) {
    executor_t *exec = setup_executor();

    executor_execute_command_line(exec, "X=99", 1);
    executor_execute_command_line(exec, "RESULT=$((5 <<= 2))", 1);

    /// The unrelated variable is untouched by the rejected attempt.
    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_STR_EQ(x, "99", "non-lvalue attempt must not mutate other vars");
    free(x);

    /// The engine still works after the error.
    executor_execute_command_line(exec, "Z=4", 1);
    executor_execute_command_line(exec, "RESULT2=$((Z <<= 1))", 1);
    char *r2 = symtable_get_var(exec->symtable, "RESULT2");
    ASSERT_NOT_NULL(r2, "RESULT2 should be set");
    ASSERT_STR_EQ(r2, "8", "engine recovers after a non-lvalue error");
    free(r2);

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
    /// $$ expands to the shell PID. The unit harness does not initialize
    /// shell_pid to getpid(), but the result must still be a non-empty run
    /// of decimal digits -- never the literal "$$" nor an empty string.
    ASSERT(result[0] != '\0', "$$ should not expand to empty");
    for (const char *p = result; *p; p++) {
        ASSERT(*p >= '0' && *p <= '9', "$$ should expand to digits only");
    }
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
    /// A backslash-escaped dollar is a literal $ (issue #60-adjacent,
    /// fixed).
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
    RUN_TEST(cmdsub_leading_with_trailing_text);
    RUN_TEST(cmdsub_with_quoted_paren);
    RUN_TEST(cmdsub_with_case_pattern_paren);
    RUN_TEST(cmdsub_fast_read_file);
    RUN_TEST(cmdsub_fast_read_missing_and_fallthrough);
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
    RUN_TEST(arith_shift_left_assign);
    RUN_TEST(arith_shift_right_assign);
    RUN_TEST(arith_bitand_assign);
    RUN_TEST(arith_bitor_assign);
    RUN_TEST(arith_bitxor_assign);
    RUN_TEST(arith_bitshift_binary_unaffected);
    RUN_TEST(arith_compound_assign_right_assoc);
    RUN_TEST(arith_compound_assign_command_form);
    RUN_TEST(arith_compound_assign_non_lvalue);

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
