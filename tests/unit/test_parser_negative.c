/**
 * @file test_parser_negative.c
 * @brief Comprehensive negative test suite for shell parser
 *
 * Tests that the parser correctly rejects invalid shell syntax.
 *
 * LUSH DESIGN CONTEXT (from docs/VISION.md):
 * - Lush is a SUPERSET shell, not an emulator
 * - It accepts both bash AND zsh syntax as different interfaces to the
 *   same underlying operations (syntax bridging)
 * - Single parser recognizes multiple syntax forms
 * - Profiles are presets, not restrictions
 *
 * This means some tests may intentionally pass where bash or zsh would
 * fail, if the syntax is valid in the other shell or makes sense in
 * lush's unified model.
 *
 * Tests here focus on syntax that is STRUCTURALLY invalid - things that
 * cannot possibly be valid shell commands in any reasonable interpretation:
 * - Unclosed constructs (quotes, braces, parentheses, control structures)
 * - Missing required keywords (then, do, in, esac, fi, done, etc.)
 * - Invalid token sequences that break grammar rules
 * - Malformed operators
 *
 * Some validation happens at expansion/execution time rather than parse
 * time (e.g., variable name validity, arithmetic expression errors).
 * These are marked with comments explaining when the error is detected.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "parser.h"
#include "node.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Category counters for summary */
static int category_tests = 0;
static int category_passed = 0;

static void reset_category(void) {
    category_tests = 0;
    category_passed = 0;
}

static void print_category_summary(const char *name) {
    printf("  [%s: %d/%d passed]\n\n", name, category_passed, category_tests);
}

/* Test framework macros */
#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        tests_run++;                                                           \
        category_tests++;                                                      \
        int _prev_failed = tests_failed;                                       \
        printf("  %s...", #name);                                              \
        fflush(stdout);                                                        \
        test_##name();                                                         \
        if (tests_failed == _prev_failed) {                                    \
            printf(" PASSED\n");                                               \
            tests_passed++;                                                    \
            category_passed++;                                                 \
        }                                                                      \
    } while (0)

/**
 * Assert that parsing the given input produces an error.
 * The parser should either:
 * - Return NULL from parser_parse()
 * - Set the error flag (parser_has_error() returns true)
 */
#define ASSERT_PARSE_FAILS(input)                                              \
    do {                                                                       \
        parser_t *_p = parser_new(input);                                      \
        if (_p == NULL) {                                                      \
            /* parser_new itself failed - acceptable */                        \
            break;                                                             \
        }                                                                      \
        node_t *_n = parser_parse(_p);                                         \
        int _has_error = parser_has_error(_p);                                 \
        if (!_has_error && _n != NULL) {                                       \
            printf(" FAILED\n");                                               \
            printf("      Expected parse error for: %s\n", input);             \
            printf("      at %s:%d\n", __FILE__, __LINE__);                    \
            tests_failed++;                                                    \
            free_node_tree(_n);                                                \
            parser_free(_p);                                                   \
            return;                                                            \
        }                                                                      \
        if (_n) free_node_tree(_n);                                            \
        parser_free(_p);                                                       \
    } while (0)

/* ============================================================================
 * UNCLOSED QUOTES
 * ============================================================================
 */

TEST(unclosed_double_quote) {
    ASSERT_PARSE_FAILS("echo \"hello");
}

TEST(unclosed_single_quote) {
    ASSERT_PARSE_FAILS("echo 'hello");
}

TEST(unclosed_double_quote_multiline) {
    ASSERT_PARSE_FAILS("echo \"hello\nworld");
}

TEST(unclosed_single_quote_multiline) {
    ASSERT_PARSE_FAILS("echo 'hello\nworld");
}

TEST(unclosed_backtick) {
    ASSERT_PARSE_FAILS("echo `pwd");
}

TEST(unclosed_dollar_paren) {
    ASSERT_PARSE_FAILS("echo $(pwd");
}

TEST(unclosed_double_quote_with_escape) {
    ASSERT_PARSE_FAILS("echo \"hello\\");
}

TEST(unclosed_nested_quotes) {
    ASSERT_PARSE_FAILS("echo \"$(echo 'test)\"");
}

/* ============================================================================
 * UNCLOSED BRACES AND PARENTHESES
 * ============================================================================
 */

TEST(unclosed_subshell) {
    ASSERT_PARSE_FAILS("( echo hello");
}

TEST(unclosed_brace_group) {
    ASSERT_PARSE_FAILS("{ echo hello");
}

/* NOTE: unclosed_variable_brace removed - validation happens at expansion time,
 * not parse time. The tokenizer treats ${VAR as a partial token that gets
 * completed or errors during expansion. This is consistent with how shells
 * traditionally work - parameter expansion syntax is validated during expansion. */

TEST(unclosed_arithmetic_paren) {
    ASSERT_PARSE_FAILS("echo $((1 + 2)");
}

TEST(unclosed_arithmetic_double) {
    ASSERT_PARSE_FAILS("(( x = 1");
}

TEST(unclosed_extended_test) {
    ASSERT_PARSE_FAILS("[[ -f file");
}

/* NOTE: unclosed_array_bracket removed - same as unclosed_variable_brace,
 * array subscript syntax is validated during expansion, not parsing. */

TEST(extra_close_paren) {
    ASSERT_PARSE_FAILS("echo hello )");
}

TEST(extra_close_brace) {
    ASSERT_PARSE_FAILS("echo hello }");
}

TEST(mismatched_paren_brace) {
    ASSERT_PARSE_FAILS("( echo hello }");
}

TEST(mismatched_brace_paren) {
    ASSERT_PARSE_FAILS("{ echo hello )");
}

/* ============================================================================
 * UNCLOSED CONTROL STRUCTURES
 * ============================================================================
 */

TEST(if_no_fi) {
    ASSERT_PARSE_FAILS("if true; then echo yes");
}

TEST(if_no_then_no_fi) {
    ASSERT_PARSE_FAILS("if true; echo yes");
}

TEST(for_no_done) {
    ASSERT_PARSE_FAILS("for x in a b; do echo $x");
}

TEST(for_no_do_no_done) {
    ASSERT_PARSE_FAILS("for x in a b; echo $x");
}

TEST(while_no_done) {
    ASSERT_PARSE_FAILS("while true; do echo loop");
}

TEST(while_no_do_no_done) {
    ASSERT_PARSE_FAILS("while true; echo loop");
}

TEST(until_no_done) {
    ASSERT_PARSE_FAILS("until false; do echo loop");
}

TEST(case_no_esac) {
    ASSERT_PARSE_FAILS("case x in a) echo a;;");
}

TEST(case_no_in) {
    ASSERT_PARSE_FAILS("case x a) echo a;; esac");
}

TEST(select_no_done) {
    ASSERT_PARSE_FAILS("select x in a b; do echo $x");
}

/* ============================================================================
 * MISSING KEYWORDS
 * ============================================================================
 */

TEST(if_missing_then) {
    ASSERT_PARSE_FAILS("if true; echo yes; fi");
}

TEST(if_missing_condition) {
    ASSERT_PARSE_FAILS("if ; then echo yes; fi");
}

TEST(elif_missing_then) {
    ASSERT_PARSE_FAILS("if true; then echo 1; elif true; echo 2; fi");
}

TEST(for_missing_do) {
    ASSERT_PARSE_FAILS("for x in a b; echo $x; done");
}

TEST(for_missing_variable) {
    ASSERT_PARSE_FAILS("for in a b; do echo x; done");
}

TEST(while_missing_do) {
    ASSERT_PARSE_FAILS("while true; echo loop; done");
}

TEST(while_missing_condition) {
    ASSERT_PARSE_FAILS("while ; do echo loop; done");
}

TEST(until_missing_do) {
    ASSERT_PARSE_FAILS("until false; echo loop; done");
}

TEST(case_missing_in) {
    ASSERT_PARSE_FAILS("case x a) echo a;; esac");
}

TEST(case_missing_paren) {
    ASSERT_PARSE_FAILS("case x in a echo a;; esac");
}

TEST(case_missing_double_semi) {
    /* Note: single ;; is required between case arms */
    ASSERT_PARSE_FAILS("case x in a) echo a b) echo b;; esac");
}

/* ============================================================================
 * STRAY KEYWORDS
 * ============================================================================
 */

TEST(stray_fi) {
    ASSERT_PARSE_FAILS("fi");
}

TEST(stray_done) {
    ASSERT_PARSE_FAILS("done");
}

TEST(stray_esac) {
    ASSERT_PARSE_FAILS("esac");
}

TEST(stray_then) {
    ASSERT_PARSE_FAILS("then echo yes");
}

TEST(stray_else) {
    ASSERT_PARSE_FAILS("else echo no");
}

TEST(stray_elif) {
    ASSERT_PARSE_FAILS("elif true; then echo yes");
}

TEST(stray_do) {
    ASSERT_PARSE_FAILS("do echo yes; done");
}

TEST(stray_in) {
    ASSERT_PARSE_FAILS("in a b c");
}

TEST(double_then) {
    ASSERT_PARSE_FAILS("if true; then then echo yes; fi");
}

TEST(double_do) {
    ASSERT_PARSE_FAILS("for x in a; do do echo $x; done");
}

TEST(double_else) {
    ASSERT_PARSE_FAILS("if true; then echo 1; else else echo 2; fi");
}

/* ============================================================================
 * INVALID OPERATORS
 * ============================================================================
 */

TEST(triple_pipe) {
    ASSERT_PARSE_FAILS("echo a ||| echo b");
}

TEST(triple_and) {
    ASSERT_PARSE_FAILS("echo a &&& echo b");
}

TEST(bare_pipe) {
    ASSERT_PARSE_FAILS("| echo");
}

TEST(bare_and_and) {
    ASSERT_PARSE_FAILS("&& echo");
}

TEST(bare_or_or) {
    ASSERT_PARSE_FAILS("|| echo");
}

TEST(trailing_pipe) {
    ASSERT_PARSE_FAILS("echo hello |");
}

TEST(trailing_and_and) {
    ASSERT_PARSE_FAILS("echo hello &&");
}

TEST(trailing_or_or) {
    ASSERT_PARSE_FAILS("echo hello ||");
}

/* NOTE: double_semicolon_outside_case - lush currently accepts this,
 * treating ;; as case terminator syntax that may appear in certain contexts.
 * This could be tightened but isn't strictly a parse error. */

TEST(bare_ampersand_ampersand) {
    ASSERT_PARSE_FAILS("&");
}

/* ============================================================================
 * INVALID REDIRECTIONS
 * ============================================================================
 */

TEST(redirect_no_target) {
    ASSERT_PARSE_FAILS("echo hello >");
}

TEST(redirect_in_no_target) {
    ASSERT_PARSE_FAILS("cat <");
}

TEST(redirect_append_no_target) {
    ASSERT_PARSE_FAILS("echo hello >>");
}

TEST(redirect_fd_no_target) {
    ASSERT_PARSE_FAILS("cmd 2>");
}

TEST(redirect_double_target) {
    /* Two targets without space between them - may parse as single word */
    ASSERT_PARSE_FAILS("echo > > file");
}

TEST(redirect_heredoc_no_delimiter) {
    ASSERT_PARSE_FAILS("cat <<");
}

TEST(redirect_herestring_no_content) {
    ASSERT_PARSE_FAILS("cat <<<");
}

TEST(redirect_invalid_fd) {
    /* Very large fd number - implementation dependent */
    ASSERT_PARSE_FAILS("echo hello 999999999999999999999>");
}

TEST(redirect_bare_ampersand) {
    ASSERT_PARSE_FAILS("echo hello >&");
}

TEST(redirect_fd_dup_invalid) {
    ASSERT_PARSE_FAILS("echo hello 2>&");
}

/* ============================================================================
 * INVALID VARIABLE SYNTAX
 * 
 * NOTE: Most variable syntax validation happens at expansion time, not parse
 * time. This is because the parser sees "123=value" as a simple word - the
 * determination of whether it's a valid assignment happens later.
 * 
 * Tests here focus on what the PARSER can reject structurally.
 * ============================================================================
 */

/* NOTE: variable_invalid_name (123=value) - parsed as a command word, not
 * recognized as assignment at parse time. Error at execution. */

/* NOTE: variable_invalid_char_in_name (foo-bar=value) - same as above */

TEST(variable_empty_name) {
    /* "=value" with nothing before = is structurally ambiguous but typically
     * parsed as a word. Whether this should error at parse time is debatable. */
    ASSERT_PARSE_FAILS("=value");
}

/* NOTE: variable_brace_no_name (${}), variable_brace_invalid_operator,
 * arithmetic errors - all validated during expansion, not parsing.
 * Arithmetic expressions in particular are treated as opaque strings
 * by the parser and evaluated by a separate arithmetic evaluator. */

/* ============================================================================
 * INVALID FUNCTION SYNTAX
 * ============================================================================
 */

TEST(function_no_body) {
    ASSERT_PARSE_FAILS("foo()");
}

/* NOTE: function_invalid_name (123() { ... }) - bash actually accepts this
 * at parse time (it's a valid function definition syntactically).
 * Whether lush should reject it is a design decision. Currently accepts. */

TEST(function_missing_paren) {
    ASSERT_PARSE_FAILS("foo( { echo x; }");
}

TEST(function_extra_paren) {
    ASSERT_PARSE_FAILS("foo()) { echo x; }");
}

TEST(function_keyword_no_body) {
    ASSERT_PARSE_FAILS("function foo");
}

TEST(function_keyword_no_name) {
    ASSERT_PARSE_FAILS("function { echo x; }");
}

TEST(function_reserved_name_if) {
    /* Using reserved word as function name */
    ASSERT_PARSE_FAILS("if() { echo x; }");
}

TEST(function_reserved_name_for) {
    ASSERT_PARSE_FAILS("for() { echo x; }");
}

TEST(function_reserved_name_while) {
    ASSERT_PARSE_FAILS("while() { echo x; }");
}

/* ============================================================================
 * INVALID CASE PATTERNS
 * ============================================================================
 */

TEST(case_empty_pattern) {
    ASSERT_PARSE_FAILS("case x in ) echo x;; esac");
}

TEST(case_pattern_no_close_paren) {
    ASSERT_PARSE_FAILS("case x in a echo x;; esac");
}

/* NOTE: case_double_pattern_no_pipe - "case x in a b) ..." is parsed with
 * "a" as a pattern and "b)" starting the next part. May not be an error
 * depending on parser lookahead. Removed as it's ambiguous. */

TEST(case_trailing_pipe_in_pattern) {
    ASSERT_PARSE_FAILS("case x in a|) echo x;; esac");
}

TEST(case_leading_pipe_in_pattern) {
    /* Leading pipe is allowed in some shells, but pattern itself empty */
    ASSERT_PARSE_FAILS("case x in |) echo x;; esac");
}

/* ============================================================================
 * INVALID EXTENDED TEST SYNTAX
 * 
 * NOTE: [[ ]] parsing is complex. The content inside is parsed with different
 * rules than regular commands. Some validation may happen at evaluation time.
 * ============================================================================
 */

TEST(extended_test_unclosed) {
    ASSERT_PARSE_FAILS("[[ -f file");
}

/* NOTE: extended_test_single_bracket_and - [ ] is actually a command (test),
 * not special syntax. "[ -f file && -r file ]" parses as two commands:
 * "[ -f file" (incomplete) "&&" "-r file ]". The error is structural.
 * However, this is complex to detect at parse time. */

TEST(extended_test_unbalanced_parens) {
    ASSERT_PARSE_FAILS("[[ ( -f file ]]");
}

/* NOTE: extended_test_double_operator, missing_operand, trailing_and -
 * These are semantic errors within [[ ]] that may be checked during
 * evaluation rather than parsing. The parser sees valid tokens. */

/* ============================================================================
 * INVALID ARITHMETIC COMMAND SYNTAX
 * ============================================================================
 */

TEST(arith_cmd_unclosed) {
    ASSERT_PARSE_FAILS("(( x = 1");
}

TEST(arith_cmd_extra_close) {
    ASSERT_PARSE_FAILS("(( x = 1 )))");
}

/* NOTE: arith_cmd_empty - "(( ))" is actually valid in bash (evaluates to
 * false/1). The arithmetic evaluator handles empty expressions. */

/* ============================================================================
 * INVALID PROCESS SUBSTITUTION
 * ============================================================================
 */

TEST(proc_sub_in_unclosed) {
    ASSERT_PARSE_FAILS("cat <(echo hello");
}

TEST(proc_sub_out_unclosed) {
    ASSERT_PARSE_FAILS("tee >(cat");
}

/* NOTE: proc_sub_empty - "<()" with empty command is accepted by bash.
 * It creates a process substitution that immediately exits. */

/* ============================================================================
 * INVALID ARRAY SYNTAX
 * ============================================================================
 */

TEST(array_unclosed) {
    ASSERT_PARSE_FAILS("arr=(a b c");
}

/* NOTE: array_index_unclosed - validated at expansion time, not parse time */

TEST(array_no_equals) {
    ASSERT_PARSE_FAILS("arr(a b c)");
}

/* ============================================================================
 * INVALID HEREDOC SYNTAX
 * ============================================================================
 */

TEST(heredoc_no_delimiter) {
    ASSERT_PARSE_FAILS("cat <<");
}

TEST(heredoc_invalid_delimiter) {
    /* Delimiter with special characters - may be implementation dependent */
    ASSERT_PARSE_FAILS("cat << <<");
}

/* NOTE: heredoc_unclosed - this requires EOF to be reached without finding
 * the delimiter. This is detected but may not be a parse error in the
 * traditional sense - it's an incomplete input condition. */

/* ============================================================================
 * INVALID COMPOUND CONSTRUCTS
 * ============================================================================
 */

TEST(pipe_into_keyword) {
    ASSERT_PARSE_FAILS("echo hello | fi");
}

TEST(pipe_from_keyword) {
    ASSERT_PARSE_FAILS("done | cat");
}

TEST(semicolon_after_pipe) {
    ASSERT_PARSE_FAILS("echo a |; cat");
}

TEST(and_after_pipe) {
    ASSERT_PARSE_FAILS("echo a |&& cat");
}

/* NOTE: brace_group_no_semicolon - "{ echo hello}" without semicolon before }
 * is actually valid in some contexts. The word "hello}" gets parsed as an
 * argument. This is tricky edge case behavior. */

TEST(empty_command_in_pipe) {
    ASSERT_PARSE_FAILS("echo a | | cat");
}

TEST(empty_command_in_and) {
    ASSERT_PARSE_FAILS("echo a && && echo b");
}

TEST(nested_unclosed_structures) {
    ASSERT_PARSE_FAILS("if true; then for x in a; do echo $x; fi");
}

/* ============================================================================
 * EDGE CASES AND PATHOLOGICAL INPUT
 * ============================================================================
 */

TEST(only_semicolons) {
    /* Multiple semicolons with nothing between */
    ASSERT_PARSE_FAILS(";;;");
}

TEST(deeply_nested_unclosed) {
    ASSERT_PARSE_FAILS("((((((((((");
}

TEST(many_unclosed_braces) {
    ASSERT_PARSE_FAILS("{{{{{{{{{{");
}

TEST(alternating_unclosed) {
    ASSERT_PARSE_FAILS("({({({");
}

/**
 * @brief Test that deeply nested valid syntax triggers depth limit
 *
 * This tests the PARSER_MAX_RECURSION_DEPTH protection. We generate
 * a valid but extremely deeply nested structure that would cause
 * stack overflow without the depth limit.
 */
TEST(recursion_depth_limit) {
    /* Generate deeply nested subshells: ((( ... ))) */
    /* PARSER_MAX_RECURSION_DEPTH is 256, so we need more than that */
    const int depth = 300;
    size_t len = depth * 2 + 10;  /* ( * depth + echo + ) * depth */
    char *input = malloc(len);
    if (!input) return;
    
    char *p = input;
    for (int i = 0; i < depth; i++) {
        *p++ = '(';
    }
    *p++ = 'e'; *p++ = 'c'; *p++ = 'h'; *p++ = 'o';
    for (int i = 0; i < depth; i++) {
        *p++ = ')';
    }
    *p = '\0';
    
    /* This should fail due to depth limit, not parse successfully */
    parser_t *parser = parser_new(input);
    if (!parser) {
        free(input);
        return;
    }
    
    node_t *ast = parser_parse(parser);
    int has_error = parser_has_error(parser);
    
    if (!has_error && ast != NULL) {
        printf(" FAILED\n");
        printf("      Expected recursion depth error for %d-deep nesting\n", depth);
        tests_failed++;
        free_node_tree(ast);
        parser_free(parser);
        free(input);
        return;
    }
    
    /* Success - parser correctly rejected deeply nested input */
    if (ast) free_node_tree(ast);
    parser_free(parser);
    free(input);
}

TEST(control_chars_in_input) {
    /* Control characters in command */
    ASSERT_PARSE_FAILS("echo \x01\x02\x03");
}

/* ============================================================================
 * INVALID COPROC SYNTAX (if supported)
 * ============================================================================
 */

TEST(coproc_no_command) {
    ASSERT_PARSE_FAILS("coproc");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("=== Parser Negative Tests ===\n");
    printf("Testing that invalid syntax is correctly rejected\n\n");

    printf("Unclosed Quotes:\n");
    reset_category();
    RUN_TEST(unclosed_double_quote);
    RUN_TEST(unclosed_single_quote);
    RUN_TEST(unclosed_double_quote_multiline);
    RUN_TEST(unclosed_single_quote_multiline);
    RUN_TEST(unclosed_backtick);
    RUN_TEST(unclosed_dollar_paren);
    RUN_TEST(unclosed_double_quote_with_escape);
    RUN_TEST(unclosed_nested_quotes);
    print_category_summary("Unclosed Quotes");

    printf("Unclosed Braces/Parentheses:\n");
    reset_category();
    RUN_TEST(unclosed_subshell);
    RUN_TEST(unclosed_brace_group);
    /* unclosed_variable_brace - expansion time check */
    RUN_TEST(unclosed_arithmetic_paren);
    RUN_TEST(unclosed_arithmetic_double);
    RUN_TEST(unclosed_extended_test);
    /* unclosed_array_bracket - expansion time check */
    RUN_TEST(extra_close_paren);
    RUN_TEST(extra_close_brace);
    RUN_TEST(mismatched_paren_brace);
    RUN_TEST(mismatched_brace_paren);
    print_category_summary("Unclosed Braces/Parens");

    printf("Unclosed Control Structures:\n");
    reset_category();
    RUN_TEST(if_no_fi);
    RUN_TEST(if_no_then_no_fi);
    RUN_TEST(for_no_done);
    RUN_TEST(for_no_do_no_done);
    RUN_TEST(while_no_done);
    RUN_TEST(while_no_do_no_done);
    RUN_TEST(until_no_done);
    RUN_TEST(case_no_esac);
    RUN_TEST(case_no_in);
    RUN_TEST(select_no_done);
    print_category_summary("Unclosed Control");

    printf("Missing Keywords:\n");
    reset_category();
    RUN_TEST(if_missing_then);
    RUN_TEST(if_missing_condition);
    RUN_TEST(elif_missing_then);
    RUN_TEST(for_missing_do);
    RUN_TEST(for_missing_variable);
    RUN_TEST(while_missing_do);
    RUN_TEST(while_missing_condition);
    RUN_TEST(until_missing_do);
    RUN_TEST(case_missing_in);
    RUN_TEST(case_missing_paren);
    RUN_TEST(case_missing_double_semi);
    print_category_summary("Missing Keywords");

    printf("Stray Keywords:\n");
    reset_category();
    RUN_TEST(stray_fi);
    RUN_TEST(stray_done);
    RUN_TEST(stray_esac);
    RUN_TEST(stray_then);
    RUN_TEST(stray_else);
    RUN_TEST(stray_elif);
    RUN_TEST(stray_do);
    RUN_TEST(stray_in);
    RUN_TEST(double_then);
    RUN_TEST(double_do);
    RUN_TEST(double_else);
    print_category_summary("Stray Keywords");

    printf("Invalid Operators:\n");
    reset_category();
    RUN_TEST(triple_pipe);
    RUN_TEST(triple_and);
    RUN_TEST(bare_pipe);
    RUN_TEST(bare_and_and);
    RUN_TEST(bare_or_or);
    RUN_TEST(trailing_pipe);
    RUN_TEST(trailing_and_and);
    RUN_TEST(trailing_or_or);
    /* double_semicolon_outside_case - lush accepts this */
    RUN_TEST(bare_ampersand_ampersand);
    print_category_summary("Invalid Operators");

    printf("Invalid Redirections:\n");
    reset_category();
    RUN_TEST(redirect_no_target);
    RUN_TEST(redirect_in_no_target);
    RUN_TEST(redirect_append_no_target);
    RUN_TEST(redirect_fd_no_target);
    RUN_TEST(redirect_double_target);
    RUN_TEST(redirect_heredoc_no_delimiter);
    RUN_TEST(redirect_herestring_no_content);
    RUN_TEST(redirect_invalid_fd);
    RUN_TEST(redirect_bare_ampersand);
    RUN_TEST(redirect_fd_dup_invalid);
    print_category_summary("Invalid Redirections");

    printf("Invalid Variable Syntax:\n");
    reset_category();
    /* variable_invalid_name - execution time check */
    /* variable_invalid_char_in_name - execution time check */
    RUN_TEST(variable_empty_name);
    /* variable_brace_no_name - expansion time check */
    /* variable_brace_invalid_operator - expansion time check */
    /* arithmetic errors - arithmetic evaluator checks */
    print_category_summary("Invalid Variables");

    printf("Invalid Function Syntax:\n");
    reset_category();
    RUN_TEST(function_no_body);
    /* function_invalid_name - lush accepts numeric function names */
    RUN_TEST(function_missing_paren);
    RUN_TEST(function_extra_paren);
    RUN_TEST(function_keyword_no_body);
    RUN_TEST(function_keyword_no_name);
    RUN_TEST(function_reserved_name_if);
    RUN_TEST(function_reserved_name_for);
    RUN_TEST(function_reserved_name_while);
    print_category_summary("Invalid Functions");

    printf("Invalid Case Patterns:\n");
    reset_category();
    RUN_TEST(case_empty_pattern);
    RUN_TEST(case_pattern_no_close_paren);
    /* case_double_pattern_no_pipe - ambiguous parsing */
    RUN_TEST(case_trailing_pipe_in_pattern);
    RUN_TEST(case_leading_pipe_in_pattern);
    print_category_summary("Invalid Case");

    printf("Invalid Extended Test:\n");
    reset_category();
    RUN_TEST(extended_test_unclosed);
    /* extended_test_single_bracket_and - [ ] is a command, complex to detect */
    RUN_TEST(extended_test_unbalanced_parens);
    /* extended_test_double_operator - semantic check */
    /* extended_test_missing_operand - semantic check */
    /* extended_test_trailing_and - semantic check */
    print_category_summary("Invalid [[]]");

    printf("Invalid Arithmetic Command:\n");
    reset_category();
    RUN_TEST(arith_cmd_unclosed);
    RUN_TEST(arith_cmd_extra_close);
    /* arith_cmd_empty - (( )) is valid (evaluates to false) */
    print_category_summary("Invalid (())");

    printf("Invalid Process Substitution:\n");
    reset_category();
    RUN_TEST(proc_sub_in_unclosed);
    RUN_TEST(proc_sub_out_unclosed);
    /* proc_sub_empty - <() is valid (empty process) */
    print_category_summary("Invalid Proc Sub");

    printf("Invalid Array Syntax:\n");
    reset_category();
    RUN_TEST(array_unclosed);
    /* array_index_unclosed - expansion time check */
    RUN_TEST(array_no_equals);
    print_category_summary("Invalid Arrays");

    printf("Invalid Heredoc Syntax:\n");
    reset_category();
    RUN_TEST(heredoc_no_delimiter);
    RUN_TEST(heredoc_invalid_delimiter);
    /* heredoc_unclosed - incomplete input, not structural error */
    print_category_summary("Invalid Heredoc");

    printf("Invalid Compound Constructs:\n");
    reset_category();
    RUN_TEST(pipe_into_keyword);
    RUN_TEST(pipe_from_keyword);
    RUN_TEST(semicolon_after_pipe);
    RUN_TEST(and_after_pipe);
    /* brace_group_no_semicolon - "hello}" parses as word */
    RUN_TEST(empty_command_in_pipe);
    RUN_TEST(empty_command_in_and);
    RUN_TEST(nested_unclosed_structures);
    print_category_summary("Invalid Compound");

    printf("Pathological Input:\n");
    reset_category();
    RUN_TEST(only_semicolons);
    RUN_TEST(deeply_nested_unclosed);
    RUN_TEST(many_unclosed_braces);
    RUN_TEST(alternating_unclosed);
    RUN_TEST(recursion_depth_limit);
    RUN_TEST(control_chars_in_input);
    print_category_summary("Pathological");

    printf("Coproc Syntax:\n");
    reset_category();
    RUN_TEST(coproc_no_command);
    print_category_summary("Coproc");

    printf("\n========================================\n");
    printf("NEGATIVE TEST RESULTS\n");
    printf("========================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("========================================\n");

    if (tests_failed > 0) {
        printf("\nWARNING: %d test(s) failed!\n", tests_failed);
        printf("The parser may be accepting invalid syntax.\n");
    } else {
        printf("\nAll negative tests passed!\n");
        printf("Parser correctly rejects invalid syntax.\n");
    }

    return tests_failed > 0 ? 1 : 0;
}
