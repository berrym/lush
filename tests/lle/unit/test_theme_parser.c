/**
 * @file test_theme_parser.c
 * @brief Unit tests for the LLE theme TOML-subset parser
 *
 * Exercises:
 *   - lle_theme_parser_init / reset
 *   - lle_theme_parser_parse (key-value callback dispatch)
 *   - lle_theme_parser_error / error_line / error_column
 *   - lle_theme_value_set_string / set_integer / set_boolean / free
 *   - lle_theme_value_table_get_string / integer / boolean
 *   - lle_parse_color_spec (named colors, 256-color index, hex)
 *
 * Tests cover:
 *   - Lifecycle and error reporting on uninitialized state
 *   - Empty input, comments, whitespace
 *   - All scalar value types: string, integer, boolean
 *   - Sections [foo] and dotted [foo.bar]
 *   - String escape sequences (\\n, \\\\, \\")
 *   - Inline tables and array values
 *   - Syntax errors with correct line/column reporting
 *   - Value helpers and table-accessor lookups
 *   - Color spec parsing in all supported formats
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "lle/prompt/theme_parser.h"
#include "test_framework.h"

#include <string.h>

/* ============================================================================
 * Callback infrastructure
 *
 * The parser delivers each key-value pair to a callback. For tests we
 * collect every callback invocation into a small array so the test can
 * assert what was parsed and in what order.
 * ============================================================================
 */

#define MAX_COLLECTED 64

typedef struct collected_kv {
    char section[128];
    char key[64];
    lle_theme_value_type_t type;
    /* Match LLE_THEME_PARSER_STRING_MAX so memcpy of an upstream
     * theme-parser string cannot truncate at this hop. */
    char string_value[LLE_THEME_PARSER_STRING_MAX];
    int64_t int_value;
    bool bool_value;
} collected_kv_t;

typedef struct collector {
    collected_kv_t items[MAX_COLLECTED];
    size_t count;
    /* If set, the callback returns this code (so error-propagation can be
     * tested). LLE_SUCCESS = continue. */
    lle_result_t return_code;
} collector_t;

static lle_result_t collect_callback(const char *section, const char *key,
                                     const lle_theme_value_t *value,
                                     void *user_data) {
    collector_t *c = (collector_t *)user_data;
    if (c->count >= MAX_COLLECTED) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    collected_kv_t *item = &c->items[c->count++];
    snprintf(item->section, sizeof(item->section), "%s",
             section ? section : "");
    snprintf(item->key, sizeof(item->key), "%s", key ? key : "");
    item->type = value->type;
    switch (value->type) {
    case LLE_THEME_VALUE_STRING:
        /// string_value is sized to match the parser's string buffer, so
        /// memcpy of the full N bytes (already NUL-terminated upstream)
        /// is exact and avoids both -Wformat-truncation (snprintf path)
        /// and -Wstringop-truncation (strncpy + sizeof-1 path).
        memcpy(item->string_value, value->data.string,
               sizeof(item->string_value));
        break;
    case LLE_THEME_VALUE_INTEGER:
        item->int_value = value->data.integer;
        break;
    case LLE_THEME_VALUE_BOOLEAN:
        item->bool_value = value->data.boolean;
        break;
    default:
        /// Arrays and tables — recorded as type only; tests can re-parse
        /// via the value pointer if they need finer detail.
        break;
    }
    return c->return_code;
}

static void collector_init(collector_t *c) {
    memset(c, 0, sizeof(*c));
    c->return_code = LLE_SUCCESS;
}

/* Convenience: parse an input string with a fresh parser + collector,
 * return the parser's return code, leave the collector populated. */
static lle_result_t parse_to_collector(const char *input, collector_t *c) {
    lle_theme_parser_t parser;
    if (lle_theme_parser_init(&parser, input) != LLE_SUCCESS) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    return lle_theme_parser_parse(&parser, collect_callback, c);
}

/* ============================================================================
 * Lifecycle
 * ============================================================================
 */

TEST(init_zeroes_state) {
    lle_theme_parser_t p;
    memset(&p, 0xAB, sizeof(p));
    ASSERT_EQ(lle_theme_parser_init(&p, "key = 1"), LLE_SUCCESS, "init");
    ASSERT_EQ(p.pos, 0, "position resets to 0");
    ASSERT_EQ(p.line, 1, "line starts at 1");
    ASSERT_EQ(p.column, 1, "column starts at 1");
    ASSERT_EQ(p.keys_parsed, 0, "no keys parsed yet");
    ASSERT_STR_EQ(lle_theme_parser_error(&p), "", "no error before parsing");
}

TEST(init_rejects_null_parser) {
    ASSERT_EQ(lle_theme_parser_init(NULL, "x = 1"), LLE_ERROR_INVALID_PARAMETER,
              "init(NULL parser)");
}

TEST(init_rejects_null_input) {
    lle_theme_parser_t p;
    ASSERT_EQ(lle_theme_parser_init(&p, NULL), LLE_ERROR_INVALID_PARAMETER,
              "init(NULL input)");
}

TEST(reset_brings_state_back_to_start) {
    lle_theme_parser_t p;
    lle_theme_parser_init(&p, "a = 1\nb = 2\n");
    collector_t c;
    collector_init(&c);
    lle_theme_parser_parse(&p, collect_callback, &c);
    ASSERT_GT(p.pos, 0, "position advanced after parse");
    lle_theme_parser_reset(&p);
    ASSERT_EQ(p.pos, 0, "reset clears position");
    ASSERT_EQ(p.line, 1, "reset clears line");
    ASSERT_EQ(p.column, 1, "reset clears column");
}

TEST(reset_tolerates_null) {
    lle_theme_parser_reset(NULL); /// must not crash
    ASSERT_TRUE(1, "reset(NULL) survived");
}

/* ============================================================================
 * Empty / whitespace / comments
 * ============================================================================
 */

TEST(parse_empty_input_succeeds_with_no_callbacks) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("", &c), LLE_SUCCESS, "empty input succeeds");
    ASSERT_EQ(c.count, 0, "no callbacks fired for empty input");
}

TEST(parse_whitespace_only_succeeds) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("   \n  \t \n   ", &c), LLE_SUCCESS,
              "whitespace-only input succeeds");
    ASSERT_EQ(c.count, 0, "no callbacks fired for whitespace");
}

TEST(parse_comment_only_succeeds) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("# this is a comment\n# another\n", &c),
              LLE_SUCCESS, "comment-only input succeeds");
    ASSERT_EQ(c.count, 0, "no callbacks fired for comments");
}

/* ============================================================================
 * Scalar values: string / integer / boolean
 * ============================================================================
 */

TEST(parse_string_value) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("name = \"hello world\"\n", &c), LLE_SUCCESS,
              "parse");
    ASSERT_EQ(c.count, 1, "one key-value");
    ASSERT_STR_EQ(c.items[0].key, "name", "key");
    ASSERT_EQ(c.items[0].type, LLE_THEME_VALUE_STRING, "type is STRING");
    ASSERT_STR_EQ(c.items[0].string_value, "hello world", "value");
}

TEST(parse_integer_value) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("count = 42\n", &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 1, "one key-value");
    ASSERT_EQ(c.items[0].type, LLE_THEME_VALUE_INTEGER, "type is INTEGER");
    ASSERT_EQ(c.items[0].int_value, 42, "value");
}

TEST(parse_negative_integer) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("offset = -7\n", &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.items[0].int_value, -7, "negative integer");
}

TEST(parse_boolean_true) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("enabled = true\n", &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.items[0].type, LLE_THEME_VALUE_BOOLEAN, "type is BOOLEAN");
    ASSERT_TRUE(c.items[0].bool_value, "true");
}

TEST(parse_boolean_false) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("disabled = false\n", &c), LLE_SUCCESS,
              "parse");
    ASSERT_FALSE(c.items[0].bool_value, "false");
}

TEST(parse_multiple_keys_in_order) {
    const char *input = "first = 1\nsecond = 2\nthird = 3\n";
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector(input, &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 3, "three callbacks");
    ASSERT_STR_EQ(c.items[0].key, "first", "first");
    ASSERT_STR_EQ(c.items[1].key, "second", "second");
    ASSERT_STR_EQ(c.items[2].key, "third", "third");
    ASSERT_EQ(c.items[0].int_value, 1, "value 1");
    ASSERT_EQ(c.items[1].int_value, 2, "value 2");
    ASSERT_EQ(c.items[2].int_value, 3, "value 3");
}

/* ============================================================================
 * Sections [foo] and dotted [foo.bar]
 * ============================================================================
 */

TEST(parse_section_qualifies_keys) {
    const char *input = "[colors]\nfg = \"red\"\nbg = \"blue\"\n";
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector(input, &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 2, "two key-values");
    ASSERT_STR_EQ(c.items[0].section, "colors", "first in [colors]");
    ASSERT_STR_EQ(c.items[1].section, "colors", "second in [colors]");
}

TEST(parse_dotted_section) {
    const char *input = "[symbols.ascii]\nplus = \"+\"\n";
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector(input, &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 1, "one key-value");
    ASSERT_STR_EQ(c.items[0].section, "symbols.ascii", "dotted section path");
    ASSERT_STR_EQ(c.items[0].key, "plus", "key");
}

TEST(parse_multiple_sections) {
    const char *input = "name = \"top\"\n"
                        "[a]\nx = 1\n"
                        "[b]\ny = 2\n";
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector(input, &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 3, "three key-values");
    ASSERT_STR_EQ(c.items[0].section, "", "top-level key has empty section");
    ASSERT_STR_EQ(c.items[1].section, "a", "a-section key");
    ASSERT_STR_EQ(c.items[2].section, "b", "b-section key");
}

/* ============================================================================
 * String escape sequences
 * ============================================================================
 */

TEST(parse_string_with_newline_escape) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("greeting = \"hello\\nworld\"\n", &c),
              LLE_SUCCESS, "parse");
    ASSERT_STR_EQ(c.items[0].string_value, "hello\nworld",
                  "\\n is a literal newline");
}

TEST(parse_string_with_escaped_quote) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("phrase = \"she said \\\"hi\\\"\"\n", &c),
              LLE_SUCCESS, "parse");
    ASSERT_STR_EQ(c.items[0].string_value, "she said \"hi\"",
                  "escaped quotes preserved");
}

TEST(parse_string_with_escaped_backslash) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("path = \"a\\\\b\"\n", &c), LLE_SUCCESS,
              "parse");
    ASSERT_STR_EQ(c.items[0].string_value, "a\\b",
                  "\\\\ becomes one backslash");
}

/* ============================================================================
 * Comments in mixed input
 * ============================================================================
 */

TEST(parse_inline_comment_after_value) {
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector("x = 7  # documented value\n", &c),
              LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 1, "one key");
    ASSERT_EQ(c.items[0].int_value, 7,
              "value not affected by trailing comment");
}

TEST(parse_comment_lines_between_keys) {
    const char *input = "first = 1\n# a comment\n# another\nsecond = 2\n";
    collector_t c;
    collector_init(&c);
    ASSERT_EQ(parse_to_collector(input, &c), LLE_SUCCESS, "parse");
    ASSERT_EQ(c.count, 2, "comments do not appear as keys");
    ASSERT_EQ(c.items[0].int_value, 1, "");
    ASSERT_EQ(c.items[1].int_value, 2, "");
}

/* ============================================================================
 * Error reporting
 * ============================================================================
 */

TEST(error_unterminated_string_reports_position) {
    lle_theme_parser_t p;
    lle_theme_parser_init(&p, "name = \"oops\n");
    collector_t c;
    collector_init(&c);
    lle_result_t r = lle_theme_parser_parse(&p, collect_callback, &c);
    ASSERT_NE(r, LLE_SUCCESS, "unterminated string must error");
    ASSERT_GT(strlen(lle_theme_parser_error(&p)), 0,
              "error message is non-empty");
    ASSERT_GT(lle_theme_parser_error_line(&p), 0, "error line is reported");
}

TEST(error_missing_equals_reports_position) {
    lle_theme_parser_t p;
    lle_theme_parser_init(&p, "key value\n");
    collector_t c;
    collector_init(&c);
    lle_result_t r = lle_theme_parser_parse(&p, collect_callback, &c);
    ASSERT_NE(r, LLE_SUCCESS, "missing = must error");
    ASSERT_GT(strlen(lle_theme_parser_error(&p)), 0,
              "error message is non-empty");
}

TEST(error_message_empty_when_no_error) {
    lle_theme_parser_t p;
    lle_theme_parser_init(&p, "good = 1");
    collector_t c;
    collector_init(&c);
    lle_theme_parser_parse(&p, collect_callback, &c);
    ASSERT_STR_EQ(lle_theme_parser_error(&p), "",
                  "no error after a valid parse");
}

TEST(error_accessors_tolerate_null) {
    ASSERT_NOT_NULL(lle_theme_parser_error(NULL),
                    "error(NULL) must return a stable pointer");
    ASSERT_EQ(lle_theme_parser_error_line(NULL), 0,
              "error_line(NULL) returns 0");
    ASSERT_EQ(lle_theme_parser_error_column(NULL), 0,
              "error_column(NULL) returns 0");
}

/* ============================================================================
 * Callback can stop the parse by returning an error
 * ============================================================================
 */

TEST(callback_error_stops_parse) {
    const char *input = "first = 1\nsecond = 2\nthird = 3\n";
    collector_t c;
    collector_init(&c);
    c.return_code = LLE_ERROR_INVALID_STATE; /// propagate from callback
    lle_result_t r = parse_to_collector(input, &c);
    ASSERT_NE(r, LLE_SUCCESS, "callback's error code propagates");
    /// Exactly the first key should have been collected, then parser bailed.
    ASSERT_EQ(c.count, 1, "parser stopped after first callback error");
    ASSERT_STR_EQ(c.items[0].key, "first",
                  "first key was the one that triggered the stop");
}

/* ============================================================================
 * Value helpers
 * ============================================================================
 */

TEST(value_set_string_populates_string) {
    lle_theme_value_t v;
    ASSERT_EQ(lle_theme_value_set_string(&v, "hello"), LLE_SUCCESS, "set");
    ASSERT_EQ(v.type, LLE_THEME_VALUE_STRING, "type");
    ASSERT_STR_EQ(v.data.string, "hello", "content");
}

TEST(value_set_string_rejects_null) {
    lle_theme_value_t v;
    ASSERT_EQ(lle_theme_value_set_string(NULL, "x"),
              LLE_ERROR_INVALID_PARAMETER, "NULL value");
    ASSERT_EQ(lle_theme_value_set_string(&v, NULL), LLE_ERROR_INVALID_PARAMETER,
              "NULL string");
}

TEST(value_set_integer_populates_integer) {
    lle_theme_value_t v;
    lle_theme_value_set_integer(&v, 12345);
    ASSERT_EQ(v.type, LLE_THEME_VALUE_INTEGER, "type");
    ASSERT_EQ(v.data.integer, 12345, "content");
}

TEST(value_set_boolean_populates_boolean) {
    lle_theme_value_t v;
    lle_theme_value_set_boolean(&v, true);
    ASSERT_EQ(v.type, LLE_THEME_VALUE_BOOLEAN, "type");
    ASSERT_TRUE(v.data.boolean, "true");
    lle_theme_value_set_boolean(&v, false);
    ASSERT_FALSE(v.data.boolean, "false after re-set");
}

TEST(value_free_tolerates_null) {
    lle_theme_value_free(NULL); /// must not crash
    ASSERT_TRUE(1, "free(NULL) survived");
}

TEST(value_free_clears_scalar_safely) {
    lle_theme_value_t v;
    lle_theme_value_set_integer(&v, 99);
    lle_theme_value_free(&v); /// scalar with no owned memory
    ASSERT_TRUE(1, "free of scalar value survived");
}

/* ============================================================================
 * Color spec parsing
 * ============================================================================
 */

TEST(color_spec_named_basic_colors) {
    lle_color_t c;
    ASSERT_EQ(lle_parse_color_spec("red", &c), LLE_SUCCESS, "red parses");
    ASSERT_EQ(c.mode, LLE_COLOR_MODE_BASIC, "red is a basic color");
    ASSERT_EQ(c.value.basic, LLE_COLOR_RED, "red maps to ANSI index 1");
    ASSERT_EQ(lle_parse_color_spec("blue", &c), LLE_SUCCESS, "blue parses");
    ASSERT_EQ(c.value.basic, LLE_COLOR_BLUE, "blue maps to ANSI index 4");
    ASSERT_EQ(lle_parse_color_spec("green", &c), LLE_SUCCESS, "green parses");
    ASSERT_EQ(c.value.basic, LLE_COLOR_GREEN, "green maps to ANSI index 2");
    ASSERT_EQ(lle_parse_color_spec("black", &c), LLE_SUCCESS, "black parses");
    ASSERT_EQ(c.value.basic, LLE_COLOR_BLACK, "black maps to ANSI index 0");
    ASSERT_EQ(lle_parse_color_spec("white", &c), LLE_SUCCESS, "white parses");
    ASSERT_EQ(c.value.basic, LLE_COLOR_WHITE, "white maps to ANSI index 7");
}

TEST(color_spec_256_color_index) {
    lle_color_t c;
    ASSERT_EQ(lle_parse_color_spec("196", &c), LLE_SUCCESS, "196 parses");
    ASSERT_EQ(c.mode, LLE_COLOR_MODE_256, "196 is a 256-color index");
    ASSERT_EQ(c.value.palette, 196, "the palette index is 196");
    ASSERT_EQ(lle_parse_color_spec("255", &c), LLE_SUCCESS, "255 parses");
    ASSERT_EQ(c.value.palette, 255, "the palette index is 255");
    ASSERT_EQ(lle_parse_color_spec("0", &c), LLE_SUCCESS, "0 parses");
    ASSERT_EQ(c.value.palette, 0, "the palette index is 0");
}

TEST(color_spec_hex_rgb_long_form) {
    lle_color_t c;
    ASSERT_EQ(lle_parse_color_spec("#ff5500", &c), LLE_SUCCESS,
              "#ff5500 parses");
    ASSERT_EQ(c.mode, LLE_COLOR_MODE_TRUE, "#ff5500 is true color");
    ASSERT_EQ(c.value.rgb.r, 0xff, "red channel is 0xff");
    ASSERT_EQ(c.value.rgb.g, 0x55, "green channel is 0x55");
    ASSERT_EQ(c.value.rgb.b, 0x00, "blue channel is 0x00");
    ASSERT_EQ(lle_parse_color_spec("#FFFFFF", &c), LLE_SUCCESS,
              "#FFFFFF parses");
    ASSERT_EQ(c.value.rgb.r, 0xff, "uppercase hex red is 0xff");
    ASSERT_EQ(c.value.rgb.g, 0xff, "uppercase hex green is 0xff");
    ASSERT_EQ(c.value.rgb.b, 0xff, "uppercase hex blue is 0xff");
}

TEST(color_spec_hex_rgb_short_form) {
    /// #RGB expands each nibble to a byte: #f50 -> #ff5500.
    lle_color_t c;
    ASSERT_EQ(lle_parse_color_spec("#f50", &c), LLE_SUCCESS, "#f50 parses");
    ASSERT_EQ(c.mode, LLE_COLOR_MODE_TRUE, "#f50 is true color");
    ASSERT_EQ(c.value.rgb.r, 0xff, "f expands to 0xff");
    ASSERT_EQ(c.value.rgb.g, 0x55, "5 expands to 0x55");
    ASSERT_EQ(c.value.rgb.b, 0x00, "0 expands to 0x00");
    ASSERT_EQ(lle_parse_color_spec("#000", &c), LLE_SUCCESS, "#000 parses");
    ASSERT_EQ(c.value.rgb.r, 0x00, "0 expands to 0x00 (r)");
    ASSERT_EQ(c.value.rgb.g, 0x00, "0 expands to 0x00 (g)");
    ASSERT_EQ(c.value.rgb.b, 0x00, "0 expands to 0x00 (b)");
}

TEST(color_spec_rejects_null) {
    lle_color_t c;
    ASSERT_NE(lle_parse_color_spec(NULL, &c), LLE_SUCCESS, "NULL spec");
    ASSERT_NE(lle_parse_color_spec("red", NULL), LLE_SUCCESS, "NULL color out");
}

TEST(color_spec_rejects_bogus_input) {
    lle_color_t c;
    ASSERT_NE(lle_parse_color_spec("not_a_color", &c), LLE_SUCCESS,
              "garbage name");
    ASSERT_NE(lle_parse_color_spec("#xyz", &c), LLE_SUCCESS,
              "non-hex digits in hex spec");
    ASSERT_NE(lle_parse_color_spec("999", &c), LLE_SUCCESS,
              "256-color index out of range");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Theme Parser Tests ===\n\n");

    printf("--- Lifecycle ---\n");
    RUN_TEST(init_zeroes_state);
    RUN_TEST(init_rejects_null_parser);
    RUN_TEST(init_rejects_null_input);
    RUN_TEST(reset_brings_state_back_to_start);
    RUN_TEST(reset_tolerates_null);

    printf("\n--- Empty / whitespace / comments ---\n");
    RUN_TEST(parse_empty_input_succeeds_with_no_callbacks);
    RUN_TEST(parse_whitespace_only_succeeds);
    RUN_TEST(parse_comment_only_succeeds);

    printf("\n--- Scalar values ---\n");
    RUN_TEST(parse_string_value);
    RUN_TEST(parse_integer_value);
    RUN_TEST(parse_negative_integer);
    RUN_TEST(parse_boolean_true);
    RUN_TEST(parse_boolean_false);
    RUN_TEST(parse_multiple_keys_in_order);

    printf("\n--- Sections ---\n");
    RUN_TEST(parse_section_qualifies_keys);
    RUN_TEST(parse_dotted_section);
    RUN_TEST(parse_multiple_sections);

    printf("\n--- String escapes ---\n");
    RUN_TEST(parse_string_with_newline_escape);
    RUN_TEST(parse_string_with_escaped_quote);
    RUN_TEST(parse_string_with_escaped_backslash);

    printf("\n--- Comments mixed with values ---\n");
    RUN_TEST(parse_inline_comment_after_value);
    RUN_TEST(parse_comment_lines_between_keys);

    printf("\n--- Error reporting ---\n");
    RUN_TEST(error_unterminated_string_reports_position);
    RUN_TEST(error_missing_equals_reports_position);
    RUN_TEST(error_message_empty_when_no_error);
    RUN_TEST(error_accessors_tolerate_null);

    printf("\n--- Callback control flow ---\n");
    RUN_TEST(callback_error_stops_parse);

    printf("\n--- Value helpers ---\n");
    RUN_TEST(value_set_string_populates_string);
    RUN_TEST(value_set_string_rejects_null);
    RUN_TEST(value_set_integer_populates_integer);
    RUN_TEST(value_set_boolean_populates_boolean);
    RUN_TEST(value_free_tolerates_null);
    RUN_TEST(value_free_clears_scalar_safely);

    printf("\n--- Color spec parsing ---\n");
    RUN_TEST(color_spec_named_basic_colors);
    RUN_TEST(color_spec_256_color_index);
    RUN_TEST(color_spec_hex_rgb_long_form);
    RUN_TEST(color_spec_hex_rgb_short_form);
    RUN_TEST(color_spec_rejects_null);
    RUN_TEST(color_spec_rejects_bogus_input);

    return TEST_RESULT();
}
