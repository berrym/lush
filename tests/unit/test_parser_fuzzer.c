/**
 * @file test_parser_fuzzer.c
 * @brief Parser edge case and fuzzer tests
 *
 * Comprehensive tests for parser edge cases including:
 * - Empty command lists in control structures
 * - Whitespace and separator variations
 * - Comment positioning
 * - Heredoc edge cases
 * - Complex quoting scenarios
 * - Deep nesting scenarios
 * - Separator and terminator edge cases
 *
 * These tests are designed to catch subtle parser bugs that may not
 * appear in typical usage but can cause issues with real-world scripts.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "parser.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Test counters

// Test framework macros

#define ASSERT_PARSES(input)                                                   \
    do {                                                                       \
        parser_t *_p = parser_new(input);                                      \
        ASSERT(_p != NULL, "parser_new failed for: " input);                   \
        node_t *_n = parser_parse(_p);                                         \
        if (parser_has_error(_p)) {                                            \
            printf(" FAILED: parse error for: %s\n", input);                   \
            printf("      Error: %s\n", parser_error(_p));                     \
            parser_free(_p);                                                   \
            TEST_FAIL_FMT("parse error for: %s", input);                       \
        }                                                                      \
        ASSERT(_n != NULL, "parser returned NULL for: " input);                \
        free_node_tree(_n);                                                    \
        parser_free(_p);                                                       \
    } while (0)

#define ASSERT_PARSE_FAILS(input)                                              \
    do {                                                                       \
        parser_t *_p = parser_new(input);                                      \
        ASSERT(_p != NULL, "parser_new failed");                               \
        node_t *_n = parser_parse(_p);                                         \
        if (!parser_has_error(_p) && _n != NULL) {                             \
            free_node_tree(_n);                                                \
            parser_free(_p);                                                   \
            TEST_FAIL_FMT("expected parse error for: %s", input);              \
        }                                                                      \
        if (_n)                                                                \
            free_node_tree(_n);                                                \
        parser_free(_p);                                                       \
    } while (0)

#define ASSERT_NODE_TYPE(node, expected_type)                                  \
    do {                                                                       \
        ASSERT((node) != NULL, "Node is NULL");                                \
        ASSERT((node)->type == (expected_type), "Wrong node type");            \
    } while (0)

/* ============================================================================
 * EMPTY COMMAND LISTS IN CASE STATEMENTS
 * Issue #19: Empty case arms caused parse failure
 * ============================================================================
 */

TEST(case_empty_arm_simple) {
    // Basic empty case arm - the bug we just fixed
    ASSERT_PARSES("case x in\n"
                  "    a) ;;\n"
                  "    *) echo default ;;\n"
                  "esac");
}

TEST(case_empty_arm_first) {
    // Empty arm as first case
    ASSERT_PARSES("case x in\n"
                  "    *) ;;\n"
                  "esac");
}

TEST(case_multiple_empty_arms) {
    // Multiple consecutive empty arms
    ASSERT_PARSES("case x in\n"
                  "    a) ;;\n"
                  "    b) ;;\n"
                  "    c) ;;\n"
                  "    *) echo default ;;\n"
                  "esac");
}

TEST(case_all_empty_arms) {
    // All arms empty
    ASSERT_PARSES("case x in\n"
                  "    a) ;;\n"
                  "    b) ;;\n"
                  "    *) ;;\n"
                  "esac");
}

TEST(case_empty_arm_inline) {
    // Empty arm with inline ;;
    ASSERT_PARSES("case x in a) ;; *) echo x ;; esac");
}

TEST(case_empty_arm_with_comment) {
    // Empty arm with comment before ;;
    ASSERT_PARSES("case x in\n"
                  "    a)\n"
                  "        # this arm intentionally empty\n"
                  "        ;;\n"
                  "    *) echo default ;;\n"
                  "esac");
}

TEST(case_cargo_env_pattern) {
    // Real-world pattern from ~/.cargo/env
    ASSERT_PARSES("case \":${PATH}:\" in\n"
                  "    *:\"$HOME/.cargo/bin\":*)\n"
                  "        ;;\n"
                  "    *)\n"
                  "        # Comment here\n"
                  "        export PATH=\"$HOME/.cargo/bin:$PATH\"\n"
                  "        ;;\n"
                  "esac");
}

TEST(case_fallthrough_empty) {
    // Empty arm with fallthrough
    ASSERT_PARSES("case x in\n"
                  "    a) ;&\n"
                  "    b) echo b ;;\n"
                  "esac");
}

TEST(case_continue_empty) {
    // Empty arm with continue
    ASSERT_PARSES("case x in\n"
                  "    a) ;;&\n"
                  "    b) echo b ;;\n"
                  "esac");
}

/* ============================================================================
 * EMPTY COMMAND LISTS IN OTHER CONTROL STRUCTURES
 * ============================================================================
 */

TEST(if_empty_then) {
    // Empty then clause with only comment
    ASSERT_PARSES("if true; then\n"
                  "    # empty\n"
                  "    :\n"
                  "fi");
}

TEST(if_colon_body) {
    // Minimal non-empty body using :
    ASSERT_PARSES("if true; then :; fi");
}

TEST(if_else_empty_branches) {
    // Both branches minimal
    ASSERT_PARSES("if true; then :; else :; fi");
}

TEST(while_minimal_body) {
    // While with minimal body
    ASSERT_PARSES("while false; do :; done");
}

TEST(for_minimal_body) {
    // For with minimal body
    ASSERT_PARSES("for x in a; do :; done");
}

TEST(until_minimal_body) {
    // Until with minimal body
    ASSERT_PARSES("until true; do :; done");
}

TEST(function_minimal_body) {
    // Function with minimal body
    ASSERT_PARSES("foo() { :; }");
}

TEST(brace_group_minimal) {
    // Brace group with minimal body
    ASSERT_PARSES("{ :; }");
}

TEST(subshell_minimal) {
    // Subshell with minimal body
    ASSERT_PARSES("( : )");
}

/* ============================================================================
 * WHITESPACE AND SEPARATOR EDGE CASES
 * ============================================================================
 */

TEST(multiple_newlines) {
    // Multiple newlines between commands
    ASSERT_PARSES("echo a\n\n\necho b");
}

TEST(trailing_semicolon) {
    // Trailing semicolon
    ASSERT_PARSES("echo a;");
}

TEST(multiple_semicolons_separate_commands) {
    // Semicolons between commands (not ;;)
    ASSERT_PARSES("echo a; echo b; echo c");
}

TEST(newline_after_pipe) {
    // Newline after pipe operator
    ASSERT_PARSES("echo a |\ncat");
}

TEST(newline_after_and) {
    // Newline after &&
    ASSERT_PARSES("true &&\necho yes");
}

TEST(newline_after_or) {
    // Newline after ||
    ASSERT_PARSES("false ||\necho no");
}

TEST(tabs_as_whitespace) {
    // Tabs for indentation
    ASSERT_PARSES("if true; then\n\techo indented\nfi");
}

TEST(mixed_whitespace) {
    // Mixed tabs and spaces
    ASSERT_PARSES("echo \t a \t b");
}

TEST(whitespace_in_expansion) {
    // Whitespace around variable
    ASSERT_PARSES("echo $VAR");
    ASSERT_PARSES("echo ${VAR}");
    ASSERT_PARSES("echo \"$VAR\"");
}

TEST(semicolon_newline_mix) {
    // Mixed separators
    ASSERT_PARSES("echo a;\necho b\necho c;");
}

/* ============================================================================
 * COMMENT POSITIONING
 * ============================================================================
 */

TEST(comment_after_command) {
    // Comment after command
    ASSERT_PARSES("echo hello # this is a comment");
}

TEST(comment_own_line) {
    // Comment on its own line
    ASSERT_PARSES("# comment\necho hello");
}

TEST(comment_between_commands) {
    // Comment between commands
    ASSERT_PARSES("echo a\n# comment\necho b");
}

TEST(comment_in_if) {
    // Comment inside if
    ASSERT_PARSES("if true; then\n"
                  "    # comment in if\n"
                  "    echo yes\n"
                  "fi");
}

TEST(comment_in_for) {
    // Comment inside for
    ASSERT_PARSES("for x in a b c; do\n"
                  "    # comment in loop\n"
                  "    echo $x\n"
                  "done");
}

TEST(comment_in_while) {
    // Comment inside while
    ASSERT_PARSES("while true; do\n"
                  "    # comment\n"
                  "    break\n"
                  "done");
}

TEST(comment_in_case) {
    // Comment inside case
    ASSERT_PARSES("case x in\n"
                  "    # comment before pattern\n"
                  "    a)\n"
                  "        # comment in arm\n"
                  "        echo a\n"
                  "        ;;\n"
                  "esac");
}

TEST(comment_in_function) {
    // Comment inside function
    ASSERT_PARSES("foo() {\n"
                  "    # function comment\n"
                  "    echo foo\n"
                  "}");
}

TEST(comment_after_redirect) {
    // Comment after redirection
    ASSERT_PARSES("echo hello > file # redirect comment");
}

TEST(comment_in_pipeline) {
    // Comment in pipeline (tricky - should be part of first command)
    ASSERT_PARSES("echo a # comment\necho b | cat");
}

TEST(hash_in_string_not_comment) {
    // Hash inside string is not a comment
    ASSERT_PARSES("echo \"hello # world\"");
    ASSERT_PARSES("echo 'hello # world'");
}

/* ============================================================================
 * HEREDOC EDGE CASES
 * ============================================================================
 */

TEST(heredoc_simple) {
    // Simple heredoc
    ASSERT_PARSES("cat <<EOF\nhello\nEOF");
}

TEST(heredoc_quoted_delimiter) {
    // Quoted delimiter (no expansion)
    ASSERT_PARSES("cat <<'EOF'\n$VAR\nEOF");
    ASSERT_PARSES("cat <<\"EOF\"\nhello\nEOF");
}

TEST(heredoc_with_tab_strip) {
    // Tab-stripping heredoc
    ASSERT_PARSES("cat <<-EOF\n\thello\n\tEOF");
}

TEST(heredoc_empty_content) {
    // Heredoc with no content
    ASSERT_PARSES("cat <<EOF\nEOF");
}

TEST(heredoc_with_variable) {
    // Heredoc with variable expansion
    ASSERT_PARSES("cat <<EOF\nhello $USER\nEOF");
}

TEST(heredoc_in_function) {
    // Heredoc inside function
    ASSERT_PARSES("foo() {\n"
                  "    cat <<EOF\n"
                  "hello\n"
                  "EOF\n"
                  "}");
}

TEST(heredoc_in_if) {
    // Heredoc inside if
    ASSERT_PARSES("if true; then\n"
                  "    cat <<EOF\n"
                  "hello\n"
                  "EOF\n"
                  "fi");
}

TEST(heredoc_followed_by_command) {
    // Command after heredoc
    ASSERT_PARSES("cat <<EOF\nhello\nEOF\necho done");
}

TEST(herestring_simple) {
    // Herestring
    ASSERT_PARSES("cat <<<'hello'");
    ASSERT_PARSES("cat <<<\"hello $USER\"");
    ASSERT_PARSES("cat <<<hello");
}

/* ============================================================================
 * COMPLEX QUOTING SCENARIOS
 * ============================================================================
 */

TEST(adjacent_quotes) {
    // Adjacent quoted strings
    ASSERT_PARSES("echo \"hello\"'world'");
    ASSERT_PARSES("echo 'a'\"b\"'c'");
}

TEST(empty_strings) {
    // Empty quoted strings
    ASSERT_PARSES("echo \"\"");
    ASSERT_PARSES("echo ''");
    ASSERT_PARSES("echo \"\"\"\"");
}

TEST(quote_in_variable) {
    // Quotes in parameter expansion default
    ASSERT_PARSES("echo \"${VAR:-'default'}\"");
    ASSERT_PARSES("echo \"${VAR:-\"default\"}\"");
}

TEST(escaped_quotes) {
    // Escaped quotes
    ASSERT_PARSES("echo \"hello \\\"world\\\"\"");
    ASSERT_PARSES("echo 'it'\\''s'"); // Escaped single quote trick
}

TEST(nested_command_substitution_quotes) {
    // Quotes in nested command substitution
    ASSERT_PARSES("echo \"$(echo \"nested\")\"");
    ASSERT_PARSES("echo \"$(echo 'single')\"");
}

TEST(dollar_in_single_quotes) {
    // Dollar sign in single quotes (literal)
    ASSERT_PARSES("echo '$VAR'");
}

TEST(backslash_in_double_quotes) {
    // Backslash escaping in double quotes
    ASSERT_PARSES("echo \"\\$VAR\"");
    ASSERT_PARSES("echo \"\\\\\"");
    ASSERT_PARSES("echo \"\\`\"");
}

TEST(multiline_string) {
    // String spanning multiple lines
    ASSERT_PARSES("echo \"hello\nworld\"");
}

/* ============================================================================
 * DEEP NESTING SCENARIOS
 * ============================================================================
 */

TEST(nested_if_2_levels) {
    // 2 levels of nested if
    ASSERT_PARSES("if true; then\n"
                  "    if true; then\n"
                  "        echo nested\n"
                  "    fi\n"
                  "fi");
}

TEST(nested_if_3_levels) {
    // 3 levels of nested if
    ASSERT_PARSES("if true; then\n"
                  "    if true; then\n"
                  "        if true; then\n"
                  "            echo deep\n"
                  "        fi\n"
                  "    fi\n"
                  "fi");
}

TEST(nested_loops) {
    // Nested loops
    ASSERT_PARSES("for i in 1 2; do\n"
                  "    for j in a b; do\n"
                  "        echo $i $j\n"
                  "    done\n"
                  "done");
}

TEST(nested_case_in_if) {
    // Case inside if
    ASSERT_PARSES("if true; then\n"
                  "    case x in\n"
                  "        a) echo a ;;\n"
                  "    esac\n"
                  "fi");
}

TEST(nested_if_in_case) {
    // If inside case
    ASSERT_PARSES("case x in\n"
                  "    a)\n"
                  "        if true; then\n"
                  "            echo yes\n"
                  "        fi\n"
                  "        ;;\n"
                  "esac");
}

TEST(nested_function_with_control) {
    // Function containing control structures
    ASSERT_PARSES("foo() {\n"
                  "    if true; then\n"
                  "        for x in a b; do\n"
                  "            echo $x\n"
                  "        done\n"
                  "    fi\n"
                  "}");
}

TEST(nested_subshells) {
    // Nested subshells
    ASSERT_PARSES("( ( ( echo deep ) ) )");
}

TEST(nested_brace_groups) {
    // Nested brace groups
    ASSERT_PARSES("{ { { echo deep; }; }; }");
}

TEST(nested_command_substitution) {
    // Nested command substitution
    ASSERT_PARSES("echo $(echo $(echo nested))");
}

TEST(nested_arithmetic) {
    // Nested arithmetic
    ASSERT_PARSES("echo $((1 + $((2 + 3))))");
}

/* ============================================================================
 * PIPELINE AND COMMAND LIST EDGE CASES
 * ============================================================================
 */

TEST(long_pipeline) {
    // Long pipeline
    ASSERT_PARSES("cat file | grep pattern | sort | uniq | head");
}

TEST(pipeline_with_redirects) {
    // Pipeline with redirections
    ASSERT_PARSES("cat < input | grep x > output 2>&1");
}

TEST(background_in_list) {
    // Background job in command list
    ASSERT_PARSES("sleep 1 & echo started");
}

TEST(complex_logical_chain) {
    // Complex logical operators
    ASSERT_PARSES("true && echo yes || echo no && echo done");
}

TEST(grouped_logical) {
    // Grouped logical expressions
    ASSERT_PARSES("{ true && false; } || echo failed");
}

TEST(subshell_in_pipeline) {
    // Subshell in pipeline
    ASSERT_PARSES("( echo hello; echo world ) | cat");
}

/* ============================================================================
 * REDIRECTION EDGE CASES
 * ============================================================================
 */

TEST(multiple_redirects) {
    // Multiple redirections
    ASSERT_PARSES("cmd < in > out 2> err");
}

TEST(fd_redirects) {
    // File descriptor redirections
    ASSERT_PARSES("cmd 2>&1");
    ASSERT_PARSES("cmd 1>&2");
    ASSERT_PARSES("cmd 3>&-");
}

TEST(redirect_append) {
    // Append redirections
    ASSERT_PARSES("echo hello >> file");
    ASSERT_PARSES("cmd &>> file");
}

TEST(redirect_noclobber) {
    // Noclobber redirect
    ASSERT_PARSES("echo hello >| file");
}

TEST(redirect_with_variable) {
    // Redirection target with variable
    ASSERT_PARSES("echo hello > $FILE");
    ASSERT_PARSES("echo hello > \"$FILE\"");
}

/* ============================================================================
 * VARIABLE AND EXPANSION EDGE CASES
 * ============================================================================
 */

TEST(special_variables) {
    // Special shell variables
    ASSERT_PARSES("echo $$ $! $? $# $@ $* $0 $1");
}

TEST(brace_expansion_variable) {
    // Variable with braces
    ASSERT_PARSES("echo ${VAR}");
    ASSERT_PARSES("echo ${VAR:-default}");
    ASSERT_PARSES("echo ${VAR:=default}");
    ASSERT_PARSES("echo ${VAR:+alternate}");
    ASSERT_PARSES("echo ${VAR:?error}");
}

TEST(variable_length) {
    // Variable length
    ASSERT_PARSES("echo ${#VAR}");
}

TEST(variable_substring) {
    // Substring expansion
    ASSERT_PARSES("echo ${VAR:0:5}");
    ASSERT_PARSES("echo ${VAR:2}");
}

TEST(variable_pattern) {
    // Pattern removal
    ASSERT_PARSES("echo ${VAR#pattern}");
    ASSERT_PARSES("echo ${VAR##pattern}");
    ASSERT_PARSES("echo ${VAR%pattern}");
    ASSERT_PARSES("echo ${VAR%%pattern}");
}

TEST(variable_replacement) {
    // Pattern replacement
    ASSERT_PARSES("echo ${VAR/old/new}");
    ASSERT_PARSES("echo ${VAR//old/new}");
}

TEST(arithmetic_expansion) {
    // Arithmetic expansion
    ASSERT_PARSES("echo $((1 + 2))");
    ASSERT_PARSES("echo $((VAR * 2))");
    ASSERT_PARSES("echo $((a > b ? a : b))");
}

/* ============================================================================
 * ARRAY SYNTAX
 * ============================================================================
 */

TEST(array_assignment) {
    // Array assignment
    ASSERT_PARSES("arr=(a b c)");
    ASSERT_PARSES("arr=(\"hello world\" 'single' plain)");
}

TEST(array_index) {
    // Array indexing
    ASSERT_PARSES("echo ${arr[0]}");
    ASSERT_PARSES("echo ${arr[@]}");
    ASSERT_PARSES("echo ${arr[*]}");
}

TEST(array_length) {
    // Array length
    ASSERT_PARSES("echo ${#arr[@]}");
}

/* ============================================================================
 * EXTENDED TEST SYNTAX
 * ============================================================================
 */

TEST(extended_test_basic) {
    // Basic extended test
    ASSERT_PARSES("[[ -f file ]]");
    ASSERT_PARSES("[[ -d dir ]]");
}

TEST(extended_test_string) {
    // String comparisons
    ASSERT_PARSES("[[ $a == $b ]]");
    ASSERT_PARSES("[[ $a != $b ]]");
    ASSERT_PARSES("[[ $a < $b ]]");
    ASSERT_PARSES("[[ $a > $b ]]");
}

TEST(extended_test_regex) {
    // Regex matching
    ASSERT_PARSES("[[ $str =~ ^[0-9]+$ ]]");
}

TEST(extended_test_logical) {
    // Logical operators in [[ ]]
    ASSERT_PARSES("[[ -f file && -r file ]]");
    ASSERT_PARSES("[[ -f file || -d file ]]");
    ASSERT_PARSES("[[ ! -f file ]]");
}

TEST(extended_test_grouping) {
    // Grouping in [[ ]]
    ASSERT_PARSES("[[ ( -f file ) && ( -r file ) ]]");
}

/* ============================================================================
 * PROCESS SUBSTITUTION
 * ============================================================================
 */

TEST(process_sub_input) {
    // Process substitution as input
    ASSERT_PARSES("diff <(ls dir1) <(ls dir2)");
}

TEST(process_sub_output) {
    // Process substitution as output
    ASSERT_PARSES("tee >(cat > file)");
}

TEST(process_sub_in_loop) {
    // Process substitution as redirection target - Issue #20 fixed
    ASSERT_PARSES("while read line; do echo $line; done < <(cat file)");
    ASSERT_PARSES("cat < <(echo hello)");
    ASSERT_PARSES("diff <(cat file1) <(cat file2)");
}

/* ============================================================================
 * FUNCTION EDGE CASES
 * ============================================================================
 */

TEST(function_posix_style) {
    // POSIX function syntax
    ASSERT_PARSES("foo() { echo foo; }");
}

TEST(function_ksh_style) {
    // ksh/bash function syntax
    ASSERT_PARSES("function foo { echo foo; }");
}

TEST(function_with_local) {
    // Function with local variables
    ASSERT_PARSES("foo() { local x=1; echo $x; }");
}

TEST(function_multiline) {
    // Multiline function
    ASSERT_PARSES("foo() {\n"
                  "    echo line1\n"
                  "    echo line2\n"
                  "}");
}

/* ============================================================================
 * ERROR CASES - These should fail to parse
 * ============================================================================
 */

TEST(error_unclosed_if) {
    // Unclosed if
    ASSERT_PARSE_FAILS("if true; then echo yes");
}

TEST(error_unclosed_for) {
    // Unclosed for
    ASSERT_PARSE_FAILS("for x in a b; do echo $x");
}

TEST(error_unclosed_while) {
    // Unclosed while
    ASSERT_PARSE_FAILS("while true; do echo loop");
}

TEST(error_unclosed_case) {
    // Unclosed case
    ASSERT_PARSE_FAILS("case x in a) echo a;;");
}

TEST(error_unclosed_quote) {
    // Unclosed quote
    ASSERT_PARSE_FAILS("echo \"hello");
    ASSERT_PARSE_FAILS("echo 'hello");
}

TEST(error_unclosed_paren) {
    // Unclosed parenthesis
    ASSERT_PARSE_FAILS("( echo hello");
}

TEST(error_unclosed_brace) {
    // Unclosed brace
    ASSERT_PARSE_FAILS("{ echo hello");
}

TEST(error_missing_do) {
    // Missing do in `while` loop. The `for` form is intentionally permissive:
    // zsh's short `for NAME in WORDS<sep> sublist` (no `do`/`done`) is
    // supported, so a malformed `for` of that shape parses as the short form
    // and is not an error.
    ASSERT_PARSE_FAILS("while true; echo yes; done");
}

TEST(error_missing_then) {
    // Missing then in if
    ASSERT_PARSE_FAILS("if true; echo yes; fi");
}

TEST(error_stray_fi) {
    // Stray fi without if
    ASSERT_PARSE_FAILS("fi");
}

TEST(error_stray_done) {
    // Stray done without loop
    ASSERT_PARSE_FAILS("done");
}

TEST(error_stray_esac) {
    // Stray esac without case
    ASSERT_PARSE_FAILS("esac");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("=== Parser Fuzzer Tests ===\n\n");

    printf("Empty Case Arms:\n");
    RUN_TEST(case_empty_arm_simple);
    RUN_TEST(case_empty_arm_first);
    RUN_TEST(case_multiple_empty_arms);
    RUN_TEST(case_all_empty_arms);
    RUN_TEST(case_empty_arm_inline);
    RUN_TEST(case_empty_arm_with_comment);
    RUN_TEST(case_cargo_env_pattern);
    RUN_TEST(case_fallthrough_empty);
    RUN_TEST(case_continue_empty);

    printf("\nEmpty Command Lists:\n");
    RUN_TEST(if_empty_then);
    RUN_TEST(if_colon_body);
    RUN_TEST(if_else_empty_branches);
    RUN_TEST(while_minimal_body);
    RUN_TEST(for_minimal_body);
    RUN_TEST(until_minimal_body);
    RUN_TEST(function_minimal_body);
    RUN_TEST(brace_group_minimal);
    RUN_TEST(subshell_minimal);

    printf("\nWhitespace/Separators:\n");
    RUN_TEST(multiple_newlines);
    RUN_TEST(trailing_semicolon);
    RUN_TEST(multiple_semicolons_separate_commands);
    RUN_TEST(newline_after_pipe);
    RUN_TEST(newline_after_and);
    RUN_TEST(newline_after_or);
    RUN_TEST(tabs_as_whitespace);
    RUN_TEST(mixed_whitespace);
    RUN_TEST(whitespace_in_expansion);
    RUN_TEST(semicolon_newline_mix);

    printf("\nComments:\n");
    RUN_TEST(comment_after_command);
    RUN_TEST(comment_own_line);
    RUN_TEST(comment_between_commands);
    RUN_TEST(comment_in_if);
    RUN_TEST(comment_in_for);
    RUN_TEST(comment_in_while);
    RUN_TEST(comment_in_case);
    RUN_TEST(comment_in_function);
    RUN_TEST(comment_after_redirect);
    RUN_TEST(comment_in_pipeline);
    RUN_TEST(hash_in_string_not_comment);

    printf("\nHeredocs:\n");
    RUN_TEST(heredoc_simple);
    RUN_TEST(heredoc_quoted_delimiter);
    RUN_TEST(heredoc_with_tab_strip);
    RUN_TEST(heredoc_empty_content);
    RUN_TEST(heredoc_with_variable);
    RUN_TEST(heredoc_in_function);
    RUN_TEST(heredoc_in_if);
    RUN_TEST(heredoc_followed_by_command);
    RUN_TEST(herestring_simple);

    printf("\nQuoting:\n");
    RUN_TEST(adjacent_quotes);
    RUN_TEST(empty_strings);
    RUN_TEST(quote_in_variable);
    RUN_TEST(escaped_quotes);
    RUN_TEST(nested_command_substitution_quotes);
    RUN_TEST(dollar_in_single_quotes);
    RUN_TEST(backslash_in_double_quotes);
    RUN_TEST(multiline_string);

    printf("\nDeep Nesting:\n");
    RUN_TEST(nested_if_2_levels);
    RUN_TEST(nested_if_3_levels);
    RUN_TEST(nested_loops);
    RUN_TEST(nested_case_in_if);
    RUN_TEST(nested_if_in_case);
    RUN_TEST(nested_function_with_control);
    RUN_TEST(nested_subshells);
    RUN_TEST(nested_brace_groups);
    RUN_TEST(nested_command_substitution);
    RUN_TEST(nested_arithmetic);

    printf("\nPipelines/Lists:\n");
    RUN_TEST(long_pipeline);
    RUN_TEST(pipeline_with_redirects);
    RUN_TEST(background_in_list);
    RUN_TEST(complex_logical_chain);
    RUN_TEST(grouped_logical);
    RUN_TEST(subshell_in_pipeline);

    printf("\nRedirections:\n");
    RUN_TEST(multiple_redirects);
    RUN_TEST(fd_redirects);
    RUN_TEST(redirect_append);
    RUN_TEST(redirect_noclobber);
    RUN_TEST(redirect_with_variable);

    printf("\nVariables/Expansions:\n");
    RUN_TEST(special_variables);
    RUN_TEST(brace_expansion_variable);
    RUN_TEST(variable_length);
    RUN_TEST(variable_substring);
    RUN_TEST(variable_pattern);
    RUN_TEST(variable_replacement);
    RUN_TEST(arithmetic_expansion);

    printf("\nArrays:\n");
    RUN_TEST(array_assignment);
    RUN_TEST(array_index);
    RUN_TEST(array_length);

    printf("\nExtended Test [[]]:\n");
    RUN_TEST(extended_test_basic);
    RUN_TEST(extended_test_string);
    RUN_TEST(extended_test_regex);
    RUN_TEST(extended_test_logical);
    RUN_TEST(extended_test_grouping);

    printf("\nProcess Substitution:\n");
    RUN_TEST(process_sub_input);
    RUN_TEST(process_sub_output);
    RUN_TEST(process_sub_in_loop);

    printf("\nFunctions:\n");
    RUN_TEST(function_posix_style);
    RUN_TEST(function_ksh_style);
    RUN_TEST(function_with_local);
    RUN_TEST(function_multiline);

    printf("\nError Cases:\n");
    RUN_TEST(error_unclosed_if);
    RUN_TEST(error_unclosed_for);
    RUN_TEST(error_unclosed_while);
    RUN_TEST(error_unclosed_case);
    RUN_TEST(error_unclosed_quote);
    RUN_TEST(error_unclosed_paren);
    RUN_TEST(error_unclosed_brace);
    RUN_TEST(error_missing_do);
    RUN_TEST(error_missing_then);
    RUN_TEST(error_stray_fi);
    RUN_TEST(error_stray_done);
    RUN_TEST(error_stray_esac);

    return TEST_RESULT();
}
