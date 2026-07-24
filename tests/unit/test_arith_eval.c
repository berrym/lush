/**
 * @file test_arith_eval.c
 * @brief Unit tests for the AST arithmetic evaluator (stage 3a).
 *
 * Exercises the tree-walking evaluator directly (lex -> parse -> eval) against
 * a real executor / symbol table, before it is wired into the live arithmetic
 * path (stage 3b). Covers operator semantics, scalar reads/writes, and the two
 * deliberate curations the AST engine delivers: recursive variable resolution
 * and short-circuit / single-branch evaluation.
 */

#include "alias.h"
#include "arithmetic_ast.h"
#include "executor.h"
#include "symtable.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

static executor_t *setup_executor(void) {
    executor_t *exec = executor_new();
    if (!exec) {
        fprintf(stderr, "Failed to create executor\n");
        exit(1);
    }
    return exec;
}

static void teardown_executor(executor_t *exec) { executor_free(exec); }

/**
 * @brief Lex, parse, and evaluate @p expr against @p exec.
 * @return true on success (value in *out); false on any lex/parse/eval error.
 */
static bool eval_expr(executor_t *exec, const char *expr, ssize_t *out) {
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    if (!arith_lex(expr, &tokens, &count, &diag)) {
        return false;
    }
    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    if (!ast) {
        return false;
    }
    bool ok = arith_ast_eval(ast, exec, out, &diag);
    arith_ast_free(ast);
    return ok;
}

/**
 * @brief True iff evaluating @p expr fails with @p expected_code.
 */
static bool eval_fails(executor_t *exec, const char *expr,
                       shell_error_code_t expected_code) {
    arith_token_t *tokens = NULL;
    size_t count = 0;
    arith_diag_t diag = {0};
    if (!arith_lex(expr, &tokens, &count, &diag)) {
        return false;
    }
    arith_ast_t *ast = arith_parse(tokens, count, &diag);
    arith_tokens_free(tokens, count);
    if (!ast) {
        return false;
    }
    ssize_t out = 0;
    bool ok = arith_ast_eval(ast, exec, &out, &diag);
    arith_ast_free(ast);
    return !ok && diag.flagged && diag.code == expected_code;
}

/// Assert that evaluating @p expr yields @p want.
#define ASSERT_EVAL(exec, expr, want)                                          \
    do {                                                                       \
        ssize_t got_ = 0;                                                      \
        ASSERT_TRUE(eval_expr((exec), (expr), &got_), (expr));                 \
        ASSERT_EQ((long)got_, (long)(want), (expr));                           \
    } while (0)

/* ==========================================================================
 * Operators
 * ========================================================================== */

TEST(eval_arithmetic) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "1 + 2 * 3", 7);
    ASSERT_EVAL(exec, "(1 + 2) * 3", 9);
    ASSERT_EVAL(exec, "10 / 3", 3);
    ASSERT_EVAL(exec, "10 % 3", 1);
    ASSERT_EVAL(exec, "2 ** 10", 1024);
    ASSERT_EVAL(exec, "-5 + 3", -2);
    ASSERT_EVAL(exec, "- -7", 7);
    teardown_executor(exec);
}

TEST(eval_bitwise_and_shift) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "6 & 3", 2);
    ASSERT_EVAL(exec, "5 | 2", 7);
    ASSERT_EVAL(exec, "5 ^ 1", 4);
    ASSERT_EVAL(exec, "~0", -1);
    ASSERT_EVAL(exec, "1 << 4", 16);
    ASSERT_EVAL(exec, "256 >> 2", 64);
    teardown_executor(exec);
}

TEST(eval_relational_logical) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "1 < 2", 1);
    ASSERT_EVAL(exec, "2 <= 2", 1);
    ASSERT_EVAL(exec, "3 > 5", 0);
    ASSERT_EVAL(exec, "5 == 5", 1);
    ASSERT_EVAL(exec, "5 != 5", 0);
    ASSERT_EVAL(exec, "1 && 1", 1);
    ASSERT_EVAL(exec, "0 || 3", 1);
    ASSERT_EVAL(exec, "!0", 1);
    teardown_executor(exec);
}

TEST(eval_ternary_and_comma) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "1 ? 10 : 20", 10);
    ASSERT_EVAL(exec, "0 ? 10 : 20", 20);
    ASSERT_EVAL(exec, "1, 2, 3", 3);
    teardown_executor(exec);
}

/* ==========================================================================
 * Scalar reads and writes
 * ========================================================================== */

TEST(eval_variable_reads) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "x=5", 1);
    ASSERT_EVAL(exec, "x + 1", 6);
    /// Based literals in a variable read by their base (#578).
    executor_execute_command_line(exec, "h=0x10", 1);
    ASSERT_EVAL(exec, "h + 1", 17);
    executor_execute_command_line(exec, "o=010", 1);
    ASSERT_EVAL(exec, "o + 1", 9);
    teardown_executor(exec);
}

TEST(eval_undefined_is_zero_no_materialize) {
    executor_t *exec = setup_executor();
    /// An undefined variable reads as 0 and is not created (#601).
    ASSERT_EVAL(exec, "undefined_v + 1", 1);
    char *v = symtable_get_var(exec->symtable, "undefined_v");
    ASSERT_NULL(v, "reading an undefined variable does not create it");
    teardown_executor(exec);
}

TEST(eval_assignment) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "y = 9", 9);
    char *y = symtable_get_var(exec->symtable, "y");
    ASSERT_STR_EQ(y, "9", "assignment persists");
    free(y);

    executor_execute_command_line(exec, "z=5", 1);
    ASSERT_EVAL(exec, "z += 3", 8);
    ASSERT_EVAL(exec, "z <<= 1", 16);
    ASSERT_EVAL(exec, "z ^= 3", 19);
    char *z = symtable_get_var(exec->symtable, "z");
    ASSERT_STR_EQ(z, "19", "compound assignments persist");
    free(z);
    teardown_executor(exec);
}

TEST(eval_increment_decrement) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "w=5", 1);
    ASSERT_EVAL(exec, "w++", 5); /// post returns the old value
    ASSERT_EVAL(exec, "++w", 7); /// pre returns the new value
    ASSERT_EVAL(exec, "w--", 7);
    char *w = symtable_get_var(exec->symtable, "w");
    ASSERT_STR_EQ(w, "6", "increment/decrement persist");
    free(w);
    teardown_executor(exec);
}

/* ==========================================================================
 * Array element lvalues (issue #603). The subscript is evaluated exactly once
 * by eval_lvalue, so every array-lvalue operation (read, assign, compound
 * assign, pre/post inc/dec) flows through lvalue_read / lvalue_write. Each test
 * uses a distinct array name because the symbol table is process-global here.
 * ========================================================================== */

TEST(eval_array_read) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "ar_r[0]=5", 1);
    executor_execute_command_line(exec, "ar_r[1]=10", 1);
    ASSERT_EVAL(exec, "ar_r[0] + ar_r[1]", 15);
    ASSERT_EVAL(exec, "ar_r[0] * 2", 10);
    /// An unset element reads 0 and is NOT created, matching an unset scalar.
    ASSERT_EVAL(exec, "ar_r[9] + 1", 1);
    char *missing = symtable_get_array_element("ar_r", "9");
    ASSERT_NULL(missing, "reading an unset element does not create it");
    teardown_executor(exec);
}

TEST(eval_array_write) {
    executor_t *exec = setup_executor();
    /// Assigning an element auto-creates the array and persists the value.
    ASSERT_EVAL(exec, "ar_w[2] = 6 * 7", 42);
    char *v = symtable_get_array_element("ar_w", "2");
    ASSERT_STR_EQ(v, "42", "an arithmetic array assignment persists");
    free(v);
    teardown_executor(exec);
}

TEST(eval_array_compound_and_incdec) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "ar_c[0]=10", 1);
    ASSERT_EVAL(exec, "ar_c[0] += 5", 15);
    ASSERT_EVAL(exec, "ar_c[0]++", 15); /// post returns the old value
    ASSERT_EVAL(exec, "++ar_c[0]", 17); /// pre returns the new value
    char *v = symtable_get_array_element("ar_c", "0");
    ASSERT_STR_EQ(v, "17", "compound / increment on an element persist");
    free(v);
    teardown_executor(exec);
}

TEST(eval_array_subscript_evaluated_once) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "ar_i[0]=100", 1);
    executor_execute_command_line(exec, "idx=0", 1);
    /// The subscript i++ is evaluated exactly once: index 0 is used for both
    /// the read and the write, idx advances by one, and ar_i[1] is untouched.
    ASSERT_EVAL(exec, "ar_i[idx++]++", 100);
    char *elem = symtable_get_array_element("ar_i", "0");
    ASSERT_STR_EQ(elem, "101", "the single resolved element was incremented");
    free(elem);
    char *idx = symtable_get_var(exec->symtable, "idx");
    ASSERT_STR_EQ(idx, "1", "the subscript side effect ran exactly once");
    free(idx);
    char *untouched = symtable_get_array_element("ar_i", "1");
    ASSERT_NULL(untouched, "no second element was created by re-evaluation");
    teardown_executor(exec);
}

TEST(eval_array_recursive_element) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "rb=5", 1);
    executor_execute_command_line(exec, "ar_x[0]='rb + 1'", 1);
    /// An element whose value is itself an expression resolves recursively,
    /// exactly as a scalar value does.
    ASSERT_EVAL(exec, "ar_x[0]", 6);
    ASSERT_EVAL(exec, "ar_x[0] * 2", 12);
    teardown_executor(exec);
}

TEST(eval_array_readonly_element) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "declare -ar ar_ro=(1 2 3)", 1);
    ASSERT_TRUE(eval_fails(exec, "ar_ro[0] = 9", SHELL_ERR_READONLY_VAR),
                "assigning an element of a readonly array is refused");
    char *v = symtable_get_array_element("ar_ro", "0");
    ASSERT_STR_EQ(v, "1", "the readonly element is unchanged");
    free(v);
    teardown_executor(exec);
}

TEST(eval_array_associative_key) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "declare -A ar_as", 1);
    executor_execute_command_line(exec, "ar_as[foo]=5", 1);
    /// An associative subscript is a LITERAL key, never arithmetic (issue
    /// #615): ar_as[foo] keys on "foo", not the arithmetic value of foo, and is
    /// stored/read under that key. Read, then compound-assign.
    ASSERT_EVAL(exec, "ar_as[foo]", 5);
    ASSERT_EVAL(exec, "ar_as[foo] += 1", 6);
    char *foo = symtable_get_array_element("ar_as", "foo");
    ASSERT_STR_EQ(foo, "6",
                  "the compound assignment stored under the key 'foo'");
    free(foo);
    /// The write never mis-keyed to index 0.
    char *zero = symtable_get_array_element("ar_as", "0");
    ASSERT_NULL(zero, "the key was not mis-evaluated to index 0");
    free(zero);
    /// A non-arithmetic-character key (which would fail arithmetic lexing)
    /// works, because the subscript is captured raw and used verbatim.
    executor_execute_command_line(exec, "ar_as[a.b]=7", 1);
    ASSERT_EVAL(exec, "ar_as[a.b]", 7);
    /// A missing key reads 0 and is not created.
    ASSERT_EVAL(exec, "ar_as[nope]", 0);
    char *nope = symtable_get_array_element("ar_as", "nope");
    ASSERT_NULL(nope, "a missing-key read did not create the key");
    free(nope);
    teardown_executor(exec);
}

/* ==========================================================================
 * Curation 1: recursive variable resolution
 * ========================================================================== */

TEST(eval_recursive_variable) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "b=5", 1);
    executor_execute_command_line(exec, "a=b+1", 1);
    /// a's value "b+1" is evaluated as arithmetic (curation toward bash).
    ASSERT_EVAL(exec, "a", 6);
    ASSERT_EVAL(exec, "a * 2", 12);
    teardown_executor(exec);
}

TEST(eval_recursive_cycle_capped) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "p=q", 1);
    executor_execute_command_line(exec, "q=p", 1);
    /// A reference cycle is bounded by the depth cap, not a stack overflow.
    ASSERT_TRUE(eval_fails(exec, "p", SHELL_ERR_ARITH_STACK_OVERFLOW),
                "a variable reference cycle errors cleanly");
    teardown_executor(exec);
}

TEST(eval_recursive_syntax_error) {
    executor_t *exec = setup_executor();
    /// A variable whose value is a malformed expression propagates a syntax
    /// error rather than silently resolving to 0.
    executor_execute_command_line(exec, "bad='1 +'", 1);
    ASSERT_TRUE(eval_fails(exec, "bad + 1", SHELL_ERR_ARITHMETIC_SYNTAX),
                "a malformed variable value errors when resolved");
    teardown_executor(exec);
}

/* ==========================================================================
 * Curation 2: short-circuit && || and single-branch ternary (no stray side
 * effects), fixing the legacy shunting-yard's pre-evaluation of all operands.
 * ========================================================================== */

TEST(eval_short_circuit_and) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "0 && (sc = 5)", 0);
    char *sc = symtable_get_var(exec->symtable, "sc");
    ASSERT_NULL(sc, "&& short-circuits: the RHS assignment did not run");
    teardown_executor(exec);
}

TEST(eval_short_circuit_or) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "1 || (so = 5)", 1);
    char *so = symtable_get_var(exec->symtable, "so");
    ASSERT_NULL(so, "|| short-circuits: the RHS assignment did not run");
    teardown_executor(exec);
}

TEST(eval_ternary_single_branch) {
    executor_t *exec = setup_executor();
    ASSERT_EVAL(exec, "1 ? (ta = 7) : (tb = 9)", 7);
    char *ta = symtable_get_var(exec->symtable, "ta");
    char *tb = symtable_get_var(exec->symtable, "tb");
    ASSERT_STR_EQ(ta, "7", "the taken branch's side effect happens");
    ASSERT_NULL(tb, "the untaken branch's side effect does not happen");
    free(ta);
    teardown_executor(exec);
}

/* ==========================================================================
 * Evaluation errors -> specific structured codes
 * ========================================================================== */

TEST(eval_division_by_zero) {
    executor_t *exec = setup_executor();
    ASSERT_TRUE(eval_fails(exec, "1 / 0", SHELL_ERR_DIVISION_BY_ZERO),
                "division by zero");
    ASSERT_TRUE(eval_fails(exec, "1 % 0", SHELL_ERR_MODULO_BY_ZERO),
                "modulo by zero");
    teardown_executor(exec);
}

TEST(eval_negative_exponent) {
    executor_t *exec = setup_executor();
    ASSERT_TRUE(eval_fails(exec, "2 ** -1", SHELL_ERR_ARITH_NEGATIVE_EXPONENT),
                "negative exponent");
    teardown_executor(exec);
}

TEST(eval_readonly_assignment) {
    executor_t *exec = setup_executor();
    executor_execute_command_line(exec, "readonly r=5", 1);
    ASSERT_TRUE(eval_fails(exec, "r = 9", SHELL_ERR_READONLY_VAR),
                "assignment to a readonly variable is refused");
    char *r = symtable_get_var(exec->symtable, "r");
    ASSERT_STR_EQ(r, "5", "the readonly value is unchanged");
    free(r);
    teardown_executor(exec);
}

int main(void) {
    printf("=== Arithmetic AST Evaluator Tests (Stage 3a) ===\n");

    /// Initialize the subsystems the executor and symbol table need.
    init_symtable();
    init_aliases();

    printf("\n--- Operators ---\n");
    RUN_TEST(eval_arithmetic);
    RUN_TEST(eval_bitwise_and_shift);
    RUN_TEST(eval_relational_logical);
    RUN_TEST(eval_ternary_and_comma);

    printf("\n--- Scalar Reads and Writes ---\n");
    RUN_TEST(eval_variable_reads);
    RUN_TEST(eval_undefined_is_zero_no_materialize);
    RUN_TEST(eval_assignment);
    RUN_TEST(eval_increment_decrement);

    printf("\n--- Array Element Reads and Writes ---\n");
    RUN_TEST(eval_array_read);
    RUN_TEST(eval_array_write);
    RUN_TEST(eval_array_compound_and_incdec);
    RUN_TEST(eval_array_subscript_evaluated_once);
    RUN_TEST(eval_array_recursive_element);
    RUN_TEST(eval_array_readonly_element);
    RUN_TEST(eval_array_associative_key);

    printf("\n--- Recursive Variable Resolution ---\n");
    RUN_TEST(eval_recursive_variable);
    RUN_TEST(eval_recursive_cycle_capped);
    RUN_TEST(eval_recursive_syntax_error);

    printf("\n--- Short-Circuit and Single-Branch Evaluation ---\n");
    RUN_TEST(eval_short_circuit_and);
    RUN_TEST(eval_short_circuit_or);
    RUN_TEST(eval_ternary_single_branch);

    printf("\n--- Evaluation Errors ---\n");
    RUN_TEST(eval_division_by_zero);
    RUN_TEST(eval_negative_exponent);
    RUN_TEST(eval_readonly_assignment);

    return TEST_RESULT();
}
