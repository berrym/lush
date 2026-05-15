/**
 * @file test_ast_roundtrip.c
 * @brief AST round-trip tests for parser validation
 *
 * Tests that verify: parse(input) → AST → source → parse(source) → AST2
 * where AST and AST2 should be structurally equivalent.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "node_to_source.h"
#include "parser.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This file's pre-existing PASS()/FAIL(msg) helpers are stubbed: PASS
 * is a no-op (RUN_TEST already prints PASS) and FAIL routes through
 * the framework's longjmp failure path. */
#define PASS() ((void)0)
#define FAIL(msg) TEST_FAIL_MSG(msg)

/* Test counters */

/**
 * @brief Perform round-trip test on shell input
 * @param input Shell source code to test
 * @param name Test name for error messages
 * @return 1 if passed, 0 if failed
 */
static int roundtrip_test(const char *input, const char *name) {
    /* Parse original input */
    parser_t *parser1 = parser_new(input);
    if (!parser1) {
        printf("FAIL: %s - could not create parser1\n", name);
        return 0;
    }

    node_t *ast1 = parser_parse(parser1);
    if (!ast1) {
        printf("FAIL: %s - parse1 failed: %s\n", name,
               parser1->error_message ? parser1->error_message
                                      : "unknown error");
        parser_free(parser1);
        return 0;
    }
    parser_free(parser1);

    /* Convert AST back to source */
    char *regenerated = node_to_source(ast1);
    if (!regenerated) {
        printf("FAIL: %s - node_to_source failed\n", name);
        free_node_tree(ast1);
        return 0;
    }

    /* Parse regenerated source */
    parser_t *parser2 = parser_new(regenerated);
    if (!parser2) {
        printf("FAIL: %s - could not create parser2\n", name);
        free(regenerated);
        free_node_tree(ast1);
        return 0;
    }

    node_t *ast2 = parser_parse(parser2);
    if (!ast2) {
        printf("FAIL: %s - parse2 failed on '%s': %s\n", name, regenerated,
               parser2->error_message ? parser2->error_message
                                      : "unknown error");
        parser_free(parser2);
        free(regenerated);
        free_node_tree(ast1);
        return 0;
    }
    parser_free(parser2);

    /* Compare ASTs */
    int equal = node_equals(ast1, ast2);

    if (!equal) {
        printf("FAIL: %s - ASTs differ\n", name);
        printf("  Original: %s\n", input);
        printf("  Regenerated: %s\n", regenerated);

        /* Show second regeneration for debugging */
        char *regen2 = node_to_source(ast2);
        if (regen2) {
            printf("  Re-regenerated: %s\n", regen2);
            free(regen2);
        }
    }

    free(regenerated);
    free_node_tree(ast1);
    free_node_tree(ast2);

    return equal;
}

/* ============================================================================
 * SIMPLE COMMAND TESTS
 * ============================================================================
 */

TEST(simple_command) {
    if (roundtrip_test("echo hello", "simple_command"))
        PASS();
    else
        FAIL("simple command roundtrip");
}

TEST(command_with_args) {
    if (roundtrip_test("ls -la /tmp", "command_with_args"))
        PASS();
    else
        FAIL("command with args roundtrip");
}

TEST(command_with_options) {
    if (roundtrip_test("grep -r --include=*.c pattern", "command_with_options"))
        PASS();
    else
        FAIL("command with options roundtrip");
}

/* ============================================================================
 * PIPELINE TESTS
 * ============================================================================
 */

TEST(simple_pipe) {
    if (roundtrip_test("ls | grep foo", "simple_pipe"))
        PASS();
    else
        FAIL("simple pipe roundtrip");
}

TEST(multi_pipe) {
    if (roundtrip_test("cat file | grep pattern | wc -l", "multi_pipe"))
        PASS();
    else
        FAIL("multi pipe roundtrip");
}

/* ============================================================================
 * LOGICAL OPERATOR TESTS
 * ============================================================================
 */

TEST(logical_and) {
    if (roundtrip_test("cmd1 && cmd2", "logical_and"))
        PASS();
    else
        FAIL("logical and roundtrip");
}

TEST(logical_or) {
    if (roundtrip_test("cmd1 || cmd2", "logical_or"))
        PASS();
    else
        FAIL("logical or roundtrip");
}

TEST(mixed_logical) {
    if (roundtrip_test("cmd1 && cmd2 || cmd3", "mixed_logical"))
        PASS();
    else
        FAIL("mixed logical roundtrip");
}

/* ============================================================================
 * REDIRECTION TESTS
 * ============================================================================
 */

TEST(redirect_output) {
    if (roundtrip_test("echo hello >file", "redirect_output"))
        PASS();
    else
        FAIL("redirect output roundtrip");
}

TEST(redirect_append) {
    if (roundtrip_test("echo hello >>file", "redirect_append"))
        PASS();
    else
        FAIL("redirect append roundtrip");
}

TEST(redirect_input) {
    if (roundtrip_test("cat <file", "redirect_input"))
        PASS();
    else
        FAIL("redirect input roundtrip");
}

TEST(redirect_stderr) {
    if (roundtrip_test("cmd 2>errors", "redirect_stderr"))
        PASS();
    else
        FAIL("redirect stderr roundtrip");
}

/* ============================================================================
 * CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(if_then_fi) {
    if (roundtrip_test("if true; then echo yes; fi", "if_then_fi"))
        PASS();
    else
        FAIL("if then fi roundtrip");
}

TEST(if_then_else_fi) {
    if (roundtrip_test("if true; then echo yes; else echo no; fi",
                       "if_then_else_fi"))
        PASS();
    else
        FAIL("if then else fi roundtrip");
}

TEST(for_loop) {
    if (roundtrip_test("for i in a b c; do echo $i; done", "for_loop"))
        PASS();
    else
        FAIL("for loop roundtrip");
}

TEST(while_loop) {
    if (roundtrip_test("while true; do echo loop; done", "while_loop"))
        PASS();
    else
        FAIL("while loop roundtrip");
}

TEST(until_loop) {
    if (roundtrip_test("until false; do echo loop; done", "until_loop"))
        PASS();
    else
        FAIL("until loop roundtrip");
}

TEST(case_statement) {
    if (roundtrip_test("case x in a) echo a;; b) echo b;; esac",
                       "case_statement"))
        PASS();
    else
        FAIL("case statement roundtrip");
}

/* ============================================================================
 * GROUPING TESTS
 * ============================================================================
 */

TEST(subshell) {
    if (roundtrip_test("( echo hello )", "subshell"))
        PASS();
    else
        FAIL("subshell roundtrip");
}

TEST(brace_group) {
    if (roundtrip_test("{ echo hello; }", "brace_group"))
        PASS();
    else
        FAIL("brace group roundtrip");
}

/* ============================================================================
 * FUNCTION TESTS
 * ============================================================================
 */

TEST(function_def) {
    if (roundtrip_test("foo() { echo bar; }", "function_def"))
        PASS();
    else
        FAIL("function definition roundtrip");
}

/* ============================================================================
 * EXTENDED SYNTAX TESTS
 * ============================================================================
 */

TEST(command_substitution) {
    if (roundtrip_test("echo $(pwd)", "command_substitution"))
        PASS();
    else
        FAIL("command substitution roundtrip");
}

TEST(arithmetic_expansion) {
    if (roundtrip_test("echo $((1+2))", "arithmetic_expansion"))
        PASS();
    else
        FAIL("arithmetic expansion roundtrip");
}

TEST(process_substitution_in) {
    if (roundtrip_test("cat <(echo hello)", "process_substitution_in"))
        PASS();
    else
        FAIL("process substitution input roundtrip");
}

TEST(background) {
    if (roundtrip_test("sleep 10 &", "background"))
        PASS();
    else
        FAIL("background roundtrip");
}

TEST(negate) {
    if (roundtrip_test("! false", "negate"))
        PASS();
    else
        FAIL("negate roundtrip");
}

/* ============================================================================
 * COMPLEX/NESTED TESTS
 * ============================================================================
 */

TEST(nested_if) {
    if (roundtrip_test("if true; then if false; then echo a; fi; fi",
                       "nested_if"))
        PASS();
    else
        FAIL("nested if roundtrip");
}

TEST(nested_loops) {
    if (roundtrip_test(
            "for i in 1 2; do for j in a b; do echo $i$j; done; done",
            "nested_loops"))
        PASS();
    else
        FAIL("nested loops roundtrip");
}

TEST(complex_pipeline) {
    if (roundtrip_test("cat file | grep -v exclude | sort | uniq -c | head -10",
                       "complex_pipeline"))
        PASS();
    else
        FAIL("complex pipeline roundtrip");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("AST Round-Trip Tests\n");
    printf("====================\n\n");

    printf("Simple Commands:\n");
    RUN_TEST(simple_command);
    RUN_TEST(command_with_args);
    RUN_TEST(command_with_options);

    printf("\nPipelines:\n");
    RUN_TEST(simple_pipe);
    RUN_TEST(multi_pipe);

    printf("\nLogical Operators:\n");
    RUN_TEST(logical_and);
    RUN_TEST(logical_or);
    RUN_TEST(mixed_logical);

    printf("\nRedirections:\n");
    RUN_TEST(redirect_output);
    RUN_TEST(redirect_append);
    RUN_TEST(redirect_input);
    RUN_TEST(redirect_stderr);

    printf("\nControl Structures:\n");
    RUN_TEST(if_then_fi);
    RUN_TEST(if_then_else_fi);
    RUN_TEST(for_loop);
    RUN_TEST(while_loop);
    RUN_TEST(until_loop);
    RUN_TEST(case_statement);

    printf("\nGrouping:\n");
    RUN_TEST(subshell);
    RUN_TEST(brace_group);

    printf("\nFunctions:\n");
    RUN_TEST(function_def);

    printf("\nExtended Syntax:\n");
    RUN_TEST(command_substitution);
    RUN_TEST(arithmetic_expansion);
    RUN_TEST(process_substitution_in);
    RUN_TEST(background);
    RUN_TEST(negate);

    printf("\nComplex/Nested:\n");
    RUN_TEST(nested_if);
    RUN_TEST(nested_loops);
    RUN_TEST(complex_pipeline);

    return TEST_RESULT();
}
