/**
 * @file test_strings.c
 * @brief Unit tests for string utilities
 *
 * Tests the string manipulation module including:
 * - String allocation and deallocation
 * - Case conversion
 * - Whitespace handling
 * - Quote processing
 * - Escape sequence handling
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "strings.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Test framework macros

/* ============================================================================
 * STRING ALLOCATION TESTS
 * ============================================================================
 */

TEST(alloc_str_basic) {
    char *s = alloc_str(100, false);
    ASSERT_NOT_NULL(s, "alloc_str should return non-NULL");
    free_str(s);
}

TEST(alloc_str_zero) {
    char *s = alloc_str(0, false);
    // Implementation may return NULL or minimal buffer
    if (s != NULL) {
        free_str(s);
    }
}

TEST(alloc_str_large) {
    char *s = alloc_str(10000, false);
    ASSERT_NOT_NULL(s, "Large allocation should succeed");
    // Write to verify it's usable
    memset(s, 'x', 9999);
    s[9999] = '\0';
    ASSERT_EQ(strlen(s), 9999, "Buffer should hold data");
    free_str(s);
}

TEST(get_alloced_str_direct_basic) {
    char original[] = "hello world";
    char *copy = get_alloced_str_direct(original);
    ASSERT_NOT_NULL(copy, "get_alloced_str_direct should return non-NULL");
    ASSERT_STR_EQ(copy, original, "Copy should match original");
    ASSERT(copy != original, "Should be a new allocation");
    free_alloced_str(copy);
}

TEST(get_alloced_str_direct_empty) {
    char original[] = "";
    char *copy = get_alloced_str_direct(original);
    ASSERT_NOT_NULL(copy, "Empty string copy should succeed");
    ASSERT_STR_EQ(copy, "", "Empty string should be preserved");
    free_alloced_str(copy);
}

TEST(get_alloced_str_basic) {
    char original[] = "test string";
    char *copy = get_alloced_str(original);
    ASSERT_NOT_NULL(copy, "get_alloced_str should return non-NULL");
    ASSERT_STR_EQ(copy, original, "Copy should match original");
    free_alloced_str(copy);
}

/* ============================================================================
 * CASE CONVERSION TESTS
 * ============================================================================
 */

TEST(strupper_basic) {
    char s[] = "hello";
    bool result = strupper(s);
    ASSERT(result, "strupper should succeed");
    ASSERT_STR_EQ(s, "HELLO", "String should be uppercase");
}

TEST(strupper_mixed) {
    char s[] = "HeLLo WoRLd 123";
    bool result = strupper(s);
    ASSERT(result, "strupper should succeed");
    ASSERT_STR_EQ(s, "HELLO WORLD 123", "Mixed case should be uppercase");
}

TEST(strupper_empty) {
    char s[] = "";
    bool result = strupper(s);
    ASSERT(result, "strupper on empty string should succeed");
    ASSERT_STR_EQ(s, "", "Empty string should remain empty");
}

TEST(strupper_numbers_only) {
    char s[] = "12345";
    bool result = strupper(s);
    ASSERT(result, "strupper on numbers should succeed");
    ASSERT_STR_EQ(s, "12345", "Numbers should be unchanged");
}

TEST(strlower_basic) {
    char s[] = "HELLO";
    bool result = strlower(s);
    ASSERT(result, "strlower should succeed");
    ASSERT_STR_EQ(s, "hello", "String should be lowercase");
}

TEST(strlower_mixed) {
    char s[] = "HeLLo WoRLd 123";
    bool result = strlower(s);
    ASSERT(result, "strlower should succeed");
    ASSERT_STR_EQ(s, "hello world 123", "Mixed case should be lowercase");
}

TEST(strlower_empty) {
    char s[] = "";
    bool result = strlower(s);
    ASSERT(result, "strlower on empty string should succeed");
    ASSERT_STR_EQ(s, "", "Empty string should remain empty");
}

/* ============================================================================
 * WHITESPACE HANDLING TESTS
 * ============================================================================
 */

TEST(str_strip_whitespace_both) {
    char s[] = "  hello world  ";
    char *result = str_strip_whitespace(s);
    ASSERT_NOT_NULL(result, "str_strip_whitespace should return non-NULL");
    ASSERT_STR_EQ(result, "hello world", "Whitespace should be stripped");
}

TEST(str_strip_whitespace_leading) {
    char s[] = "   hello";
    char *result = str_strip_whitespace(s);
    ASSERT_STR_EQ(result, "hello", "Leading whitespace should be stripped");
}

TEST(str_strip_whitespace_trailing) {
    char s[] = "hello   ";
    char *result = str_strip_whitespace(s);
    ASSERT_STR_EQ(result, "hello", "Trailing whitespace should be stripped");
}

TEST(str_strip_whitespace_none) {
    char s[] = "hello";
    char *result = str_strip_whitespace(s);
    ASSERT_STR_EQ(result, "hello",
                  "String without whitespace should be unchanged");
}

TEST(str_strip_whitespace_empty) {
    char s[] = "";
    char *result = str_strip_whitespace(s);
    ASSERT_STR_EQ(result, "", "Empty string should remain empty");
}

TEST(str_strip_whitespace_only_whitespace) {
    char s[] = "   \t\n   ";
    char *result = str_strip_whitespace(s);
    ASSERT_STR_EQ(result, "", "All whitespace should result in empty string");
}

TEST(str_skip_whitespace_basic) {
    char s[] = "   hello";
    size_t count = str_skip_whitespace(s);
    ASSERT_EQ(count, 3, "Should count 3 leading spaces");
}

TEST(str_skip_whitespace_tabs) {
    char s[] = "\t\t hello";
    size_t count = str_skip_whitespace(s);
    ASSERT_EQ(count, 3, "Should count tabs and spaces");
}

TEST(str_skip_whitespace_none) {
    char s[] = "hello";
    size_t count = str_skip_whitespace(s);
    ASSERT_EQ(count, 0, "No leading whitespace should return 0");
}

TEST(str_strip_leading_whitespace_basic) {
    char s[] = "   hello";
    size_t result = str_strip_leading_whitespace(s);
    // Function returns total offset processed, not count removed
    ASSERT(result > 0, "Should report non-zero on success");
    ASSERT_STR_EQ(s, "hello", "Leading whitespace should be removed");
}

TEST(str_strip_trailing_whitespace_basic) {
    char s[] = "hello   ";
    ssize_t offset = str_strip_trailing_whitespace(s);
    // Function returns negative offset (count of removed chars)
    ASSERT(offset < 0, "Should report negative offset for removed chars");
    ASSERT_STR_EQ(s, "hello", "Trailing whitespace should be removed");
}

TEST(str_strip_trailing_whitespace_none) {
    char s[] = "hello";
    ssize_t offset = str_strip_trailing_whitespace(s);
    ASSERT_EQ(offset, 0, "No removal should return 0");
    ASSERT_STR_EQ(s, "hello", "String should be unchanged");
}

/* ============================================================================
 * NEWLINE HANDLING TESTS
 * ============================================================================
 */

TEST(null_replace_newline_basic) {
    // Function only removes trailing newline, not newlines in middle
    char s[] = "hello\nworld";
    null_replace_newline(s);
    // No trailing newline, so string unchanged
    ASSERT_STR_EQ(s, "hello\nworld",
                  "String without trailing newline should be unchanged");
}

TEST(null_replace_newline_end) {
    char s[] = "hello\n";
    null_replace_newline(s);
    ASSERT_STR_EQ(s, "hello", "Trailing newline should be removed");
}

TEST(null_replace_newline_none) {
    char s[] = "hello";
    null_replace_newline(s);
    ASSERT_STR_EQ(s, "hello", "String without newline should be unchanged");
}

/* ============================================================================
 * CHARACTER DELETION TESTS
 * ============================================================================
 */

TEST(delete_char_at_start) {
    char s[] = "hello";
    delete_char_at(s, 0);
    ASSERT_STR_EQ(s, "ello", "First character should be deleted");
}

TEST(delete_char_at_middle) {
    char s[] = "hello";
    delete_char_at(s, 2);
    ASSERT_STR_EQ(s, "helo", "Middle character should be deleted");
}

TEST(delete_char_at_end) {
    char s[] = "hello";
    delete_char_at(s, 4);
    ASSERT_STR_EQ(s, "hell", "Last character should be deleted");
}

/* ============================================================================
 * STRING SEARCH TESTS
 * ============================================================================
 */

TEST(strchr_any_found) {
    char s[] = "hello world";
    char *result = strchr_any(s, "aeiou");
    ASSERT_NOT_NULL(result, "Should find a vowel");
    ASSERT(*result == 'e', "First vowel should be 'e'");
}

TEST(strchr_any_not_found) {
    char s[] = "xyz";
    char *result = strchr_any(s, "abc");
    ASSERT_NULL(result, "Should not find any character");
}

TEST(strchr_any_first_char) {
    char s[] = "apple";
    char *result = strchr_any(s, "abc");
    ASSERT_NOT_NULL(result, "Should find 'a'");
    ASSERT(*result == 'a', "Should find 'a' first");
}

/* ============================================================================
 * QUOTE HANDLING TESTS
 * ============================================================================
 */

TEST(find_opening_quote_type_single) {
    char s[] = "'hello'";
    char quote = find_opening_quote_type(s);
    ASSERT_EQ(quote, '\'', "Should detect single quote");
}

TEST(find_opening_quote_type_double) {
    char s[] = "\"hello\"";
    char quote = find_opening_quote_type(s);
    ASSERT_EQ(quote, '"', "Should detect double quote");
}

TEST(find_opening_quote_type_none) {
    char s[] = "hello";
    char quote = find_opening_quote_type(s);
    ASSERT_EQ(quote, 0, "Should return 0 for no quote");
}

TEST(find_closing_quote_single) {
    char s[] = "'hello'";
    size_t pos = find_closing_quote(s);
    ASSERT_EQ(pos, 6, "Closing quote should be at position 6");
}

TEST(find_closing_quote_double) {
    char s[] = "\"hello\"";
    size_t pos = find_closing_quote(s);
    ASSERT_EQ(pos, 6, "Closing quote should be at position 6");
}

TEST(find_closing_quote_escaped) {
    char s[] = "\"hello\\\"world\"";
    size_t pos = find_closing_quote(s);
    // Should find the final quote, not the escaped one
    ASSERT(pos > 6, "Should skip escaped quote");
}

TEST(find_last_quote_basic) {
    char s[] = "'hello' 'world'";
    size_t pos = find_last_quote(s);
    ASSERT_EQ(pos, 14, "Last quote should be at position 14");
}

/* ============================================================================
 * BRACE HANDLING TESTS
 * ============================================================================
 */

TEST(find_closing_brace_basic) {
    char s[] = "{hello}";
    size_t pos = find_closing_brace(s);
    ASSERT_EQ(pos, 6, "Closing brace should be at position 6");
}

TEST(find_closing_brace_nested) {
    char s[] = "{a{b}c}";
    size_t pos = find_closing_brace(s);
    ASSERT_EQ(pos, 6, "Should find outer closing brace");
}

TEST(find_closing_brace_deeply_nested) {
    char s[] = "{{{deep}}}";
    size_t pos = find_closing_brace(s);
    ASSERT_EQ(pos, 9, "Should find outermost closing brace");
}

/* ============================================================================
 * QUOTING TESTS
 * ============================================================================
 */

TEST(quote_val_simple) {
    char val[] = "hello";
    char *result = quote_val(val, true);
    ASSERT_NOT_NULL(result, "quote_val should return non-NULL");
    // Should be quoted appropriately
    ASSERT(strchr(result, '\'') != NULL || strchr(result, '"') != NULL,
           "Result should contain quotes");
    free(result);
}

TEST(quote_val_with_spaces) {
    char val[] = "hello world";
    char *result = quote_val(val, true);
    ASSERT_NOT_NULL(result, "quote_val should return non-NULL");
    free(result);
}

TEST(quote_val_with_special_chars) {
    char val[] = "hello$world";
    char *result = quote_val(val, true);
    ASSERT_NOT_NULL(result, "quote_val should return non-NULL");
    free(result);
}

// Note: substitute_str tests removed - function declared but not implemented

/* ============================================================================
 * ESCAPE PROCESSING TESTS
 * ============================================================================
 */

TEST(process_token_escapes_newline) {
    const char *input = "hello\\nworld";
    char *result = process_token_escapes(input);
    ASSERT_NOT_NULL(result, "process_token_escapes should return non-NULL");
    ASSERT_STR_EQ(result, "hello\nworld", "Newline escape should be processed");
    free(result);
}

TEST(process_token_escapes_tab) {
    const char *input = "hello\\tworld";
    char *result = process_token_escapes(input);
    ASSERT_NOT_NULL(result, "process_token_escapes should return non-NULL");
    ASSERT_STR_EQ(result, "hello\tworld", "Tab escape should be processed");
    free(result);
}

TEST(process_token_escapes_backslash) {
    const char *input = "hello\\\\world";
    char *result = process_token_escapes(input);
    ASSERT_NOT_NULL(result, "process_token_escapes should return non-NULL");
    ASSERT_STR_EQ(result, "hello\\world",
                  "Backslash escape should be processed");
    free(result);
}

TEST(process_token_escapes_multiple) {
    const char *input = "\\thello\\nworld\\t";
    char *result = process_token_escapes(input);
    ASSERT_NOT_NULL(result, "process_token_escapes should return non-NULL");
    ASSERT_STR_EQ(result, "\thello\nworld\t",
                  "Multiple escapes should be processed");
    free(result);
}

TEST(process_token_escapes_no_escapes) {
    const char *input = "hello world";
    char *result = process_token_escapes(input);
    ASSERT_NOT_NULL(result, "process_token_escapes should return non-NULL");
    ASSERT_STR_EQ(result, "hello world",
                  "String without escapes should be unchanged");
    free(result);
}

/* ============================================================================
 * BUFFER BOUNDS TESTS
 * ============================================================================
 */

TEST(check_buffer_bounds_needs_growth) {
    size_t count = 10;
    size_t len = 10;
    char **buf = malloc(len * sizeof(char *));
    ASSERT_NOT_NULL(buf, "Initial malloc should succeed");

    bool result = check_buffer_bounds(&count, &len, &buf);
    ASSERT(result, "check_buffer_bounds should succeed");
    ASSERT(len > 10, "Buffer should have grown");

    free(buf);
}

TEST(check_buffer_bounds_no_growth) {
    size_t count = 5;
    size_t len = 10;
    char **buf = malloc(len * sizeof(char *));
    ASSERT_NOT_NULL(buf, "Initial malloc should succeed");

    size_t old_len = len;
    bool result = check_buffer_bounds(&count, &len, &buf);
    ASSERT(result, "check_buffer_bounds should succeed");
    ASSERT_EQ(len, old_len, "Buffer should not have grown");

    free(buf);
}

/* ============================================================================
 * ARGV HANDLING TESTS
 * ============================================================================
 */

TEST(free_argv_basic) {
    char **argv = malloc(3 * sizeof(char *));
    argv[0] = strdup("arg0");
    argv[1] = strdup("arg1");
    argv[2] = strdup("arg2");
    // Should not crash
    free_argv(3, argv);
}

TEST(free_argv_empty) {
    char **argv = malloc(1 * sizeof(char *));
    argv[0] = NULL;
    // Should not crash
    free_argv(0, NULL);
    free(argv);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running strings.c tests...\n\n");

    printf("String Allocation Tests:\n");
    RUN_TEST(alloc_str_basic);
    RUN_TEST(alloc_str_zero);
    RUN_TEST(alloc_str_large);
    RUN_TEST(get_alloced_str_direct_basic);
    RUN_TEST(get_alloced_str_direct_empty);
    RUN_TEST(get_alloced_str_basic);

    printf("\nCase Conversion Tests:\n");
    RUN_TEST(strupper_basic);
    RUN_TEST(strupper_mixed);
    RUN_TEST(strupper_empty);
    RUN_TEST(strupper_numbers_only);
    RUN_TEST(strlower_basic);
    RUN_TEST(strlower_mixed);
    RUN_TEST(strlower_empty);

    printf("\nWhitespace Handling Tests:\n");
    RUN_TEST(str_strip_whitespace_both);
    RUN_TEST(str_strip_whitespace_leading);
    RUN_TEST(str_strip_whitespace_trailing);
    RUN_TEST(str_strip_whitespace_none);
    RUN_TEST(str_strip_whitespace_empty);
    RUN_TEST(str_strip_whitespace_only_whitespace);
    RUN_TEST(str_skip_whitespace_basic);
    RUN_TEST(str_skip_whitespace_tabs);
    RUN_TEST(str_skip_whitespace_none);
    RUN_TEST(str_strip_leading_whitespace_basic);
    RUN_TEST(str_strip_trailing_whitespace_basic);
    RUN_TEST(str_strip_trailing_whitespace_none);

    printf("\nNewline Handling Tests:\n");
    RUN_TEST(null_replace_newline_basic);
    RUN_TEST(null_replace_newline_end);
    RUN_TEST(null_replace_newline_none);

    printf("\nCharacter Deletion Tests:\n");
    RUN_TEST(delete_char_at_start);
    RUN_TEST(delete_char_at_middle);
    RUN_TEST(delete_char_at_end);

    printf("\nString Search Tests:\n");
    RUN_TEST(strchr_any_found);
    RUN_TEST(strchr_any_not_found);
    RUN_TEST(strchr_any_first_char);

    printf("\nQuote Handling Tests:\n");
    RUN_TEST(find_opening_quote_type_single);
    RUN_TEST(find_opening_quote_type_double);
    RUN_TEST(find_opening_quote_type_none);
    RUN_TEST(find_closing_quote_single);
    RUN_TEST(find_closing_quote_double);
    RUN_TEST(find_closing_quote_escaped);
    RUN_TEST(find_last_quote_basic);

    printf("\nBrace Handling Tests:\n");
    RUN_TEST(find_closing_brace_basic);
    RUN_TEST(find_closing_brace_nested);
    RUN_TEST(find_closing_brace_deeply_nested);

    printf("\nQuoting Tests:\n");
    RUN_TEST(quote_val_simple);
    RUN_TEST(quote_val_with_spaces);
    RUN_TEST(quote_val_with_special_chars);

    printf("\nString Substitution Tests:\n");
    // substitute_str tests removed - function not implemented

    printf("\nEscape Processing Tests:\n");
    RUN_TEST(process_token_escapes_newline);
    RUN_TEST(process_token_escapes_tab);
    RUN_TEST(process_token_escapes_backslash);
    RUN_TEST(process_token_escapes_multiple);
    RUN_TEST(process_token_escapes_no_escapes);

    printf("\nBuffer Bounds Tests:\n");
    RUN_TEST(check_buffer_bounds_needs_growth);
    RUN_TEST(check_buffer_bounds_no_growth);

    printf("\nArgv Handling Tests:\n");
    RUN_TEST(free_argv_basic);
    RUN_TEST(free_argv_empty);

    return TEST_RESULT();
}
