/**
 * @file test_toml_parser.c
 * @brief Unit tests for the generic TOML parser
 *
 * Tests the toml_parser.c implementation covering:
 * - All value types: strings, integers, booleans, arrays, tables
 * - Section parsing: simple and nested
 * - Error handling and reporting
 * - Edge cases: escapes, unicode, empty values
 */

#include "toml_parser.h"

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef ASSERT_EQ
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")

/* ============================================================================
 * Test Framework
 * ============================================================================
 */

/* ============================================================================
 * Test Callback Context
 * ============================================================================
 */

typedef struct {
    char sections[32][64];
    char keys[32][64];
    toml_value_t values[32];
    size_t count;
    toml_result_t abort_on_key; ///< Set to abort when specific key seen
    const char *abort_key_name;
} test_ctx_t;

static toml_result_t test_callback(const char *section, const char *key,
                                   const toml_value_t *value, void *user_data) {
    test_ctx_t *ctx = (test_ctx_t *)user_data;

    if (ctx->count >= 32) {
        return TOML_ERROR_CALLBACK_ABORT;
    }

    snprintf(ctx->sections[ctx->count], sizeof(ctx->sections[0]), "%s",
             section ? section : "");
    snprintf(ctx->keys[ctx->count], sizeof(ctx->keys[0]), "%s", key);

    /// Deep copy the value
    ctx->values[ctx->count] = *value;

    /// For arrays, we need to copy items (or set to NULL for empty arrays)
    if (value->type == TOML_VALUE_ARRAY) {
        if (value->data.array.count > 0 && value->data.array.items) {
            ctx->values[ctx->count].data.array.items =
                malloc(value->data.array.count * sizeof(toml_value_t));
            if (ctx->values[ctx->count].data.array.items) {
                memcpy(ctx->values[ctx->count].data.array.items,
                       value->data.array.items,
                       value->data.array.count * sizeof(toml_value_t));
            }
        } else {
            /// Empty array - ensure items is NULL
            ctx->values[ctx->count].data.array.items = NULL;
        }
    }

    /// For inline tables, we need to deep copy entry values
    if (value->type == TOML_VALUE_TABLE) {
        for (size_t i = 0; i < value->data.table.count; i++) {
            if (value->data.table.entries[i].value) {
                ctx->values[ctx->count].data.table.entries[i].value =
                    malloc(sizeof(toml_value_t));
                if (ctx->values[ctx->count].data.table.entries[i].value) {
                    memcpy(ctx->values[ctx->count].data.table.entries[i].value,
                           value->data.table.entries[i].value,
                           sizeof(toml_value_t));
                }
            }
        }
    }

    ctx->count++;

    /// Check if we should abort
    if (ctx->abort_key_name && strcmp(key, ctx->abort_key_name) == 0) {
        return ctx->abort_on_key;
    }

    return TOML_SUCCESS;
}

static void free_test_ctx(test_ctx_t *ctx) {
    for (size_t i = 0; i < ctx->count; i++) {
        if (ctx->values[i].type == TOML_VALUE_ARRAY &&
            ctx->values[i].data.array.items &&
            ctx->values[i].data.array.count > 0) {
            free(ctx->values[i].data.array.items);
            ctx->values[i].data.array.items = NULL;
        }
    }
}

/* ============================================================================
 * Initialization Tests
 * ============================================================================
 */

TEST(parser_init_null_parser) {
    toml_result_t result = toml_parser_init(NULL, "key = \"value\"");
    ASSERT_EQ(result, TOML_ERROR_INVALID_PARAMETER);
}

TEST(parser_init_null_input) {
    toml_parser_t parser;
    toml_result_t result = toml_parser_init(&parser, NULL);
    ASSERT_EQ(result, TOML_ERROR_INVALID_PARAMETER);
}

TEST(parser_init_success) {
    toml_parser_t parser;
    toml_result_t result = toml_parser_init(&parser, "key = \"value\"");
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(parser.line, 1);
    ASSERT_EQ(parser.column, 1);
    ASSERT_EQ(parser.pos, 0);
    toml_parser_cleanup(&parser);
}

TEST(parser_init_with_length) {
    toml_parser_t parser;
    const char *input = "key = \"value\"\nextra garbage";
    toml_result_t result = toml_parser_init_with_length(&parser, input, 13);
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(parser.input_len, 13);
    toml_parser_cleanup(&parser);
}

TEST(parser_reset) {
    toml_parser_t parser;
    toml_parser_init(&parser, "key = \"value\"");
    parser.pos = 5;
    parser.line = 3;
    parser.column = 10;

    toml_parser_reset(&parser);

    ASSERT_EQ(parser.pos, 0);
    ASSERT_EQ(parser.line, 1);
    ASSERT_EQ(parser.column, 1);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * String Value Tests
 * ============================================================================
 */

TEST(parse_simple_string) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "name = \"hello world\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.keys[0], "name");
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_STRING);
    ASSERT_STR_EQ(ctx.values[0].data.string, "hello world");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_string_with_escapes) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser,
                     "text = \"line1\\nline2\\ttab\\\"quote\\\\backslash\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.values[0].data.string,
                  "line1\nline2\ttab\"quote\\backslash");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_empty_string) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "empty = \"\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.values[0].data.string, "");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_unicode_escape_bmp) {
    /// Test \uXXXX escape for Basic Multilingual Plane characters
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    /// \u00E9 = é (Latin Small Letter E with Acute)
    toml_parser_init(&parser, "text = \"caf\\u00E9\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.values[0].data.string, "café");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_unicode_escape_emoji) {
    /// Test \UXXXXXXXX escape for characters outside BMP (emoji)
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    /// \U0001F600 = 😀 (Grinning Face) - requires 4 UTF-8 bytes
    toml_parser_init(&parser, "emoji = \"\\U0001F600\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    /// UTF-8: F0 9F 98 80
    ASSERT_STR_EQ(ctx.values[0].data.string, "😀");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_unicode_escape_mixed) {
    /// Test mixing regular chars, escapes, and unicode escapes
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    /// Hello + \u4E16\u754C (世界 = "world" in Chinese) + !
    toml_parser_init(&parser, "msg = \"Hello \\u4E16\\u754C!\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.values[0].data.string, "Hello 世界!");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_unicode_escape_incomplete) {
    /// Test incomplete unicode escape
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "bad = \"\\u00E\""); /// Only 3 hex digits
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_unicode_escape_invalid_hex) {
    /// Test invalid hex digit in unicode escape
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "bad = \"\\u00GG\""); /// G is not hex
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_unicode_escape_surrogate) {
    /// Test surrogate codepoint rejection (D800-DFFF)
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "bad = \"\\uD800\""); /// High surrogate
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Integer Value Tests
 * ============================================================================
 */

TEST(parse_positive_integer) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "count = 42");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_INTEGER);
    ASSERT_EQ(ctx.values[0].data.integer, 42);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_negative_integer) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "offset = -100");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_EQ(ctx.values[0].data.integer, -100);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_zero) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "zero = 0");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].data.integer, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_large_integer) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "big = 9223372036854775807");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].data.integer, 9223372036854775807LL);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Boolean Value Tests
 * ============================================================================
 */

TEST(parse_boolean_true) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "enabled = true");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_BOOLEAN);
    ASSERT(ctx.values[0].data.boolean == true);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_boolean_false) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "disabled = false");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT(ctx.values[0].data.boolean == false);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Array Value Tests
 * ============================================================================
 */

TEST(parse_string_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "colors = [\"red\", \"green\", \"blue\"]");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_ARRAY);
    ASSERT_EQ(ctx.values[0].data.array.count, 3);
    ASSERT_STR_EQ(ctx.values[0].data.array.items[0].data.string, "red");
    ASSERT_STR_EQ(ctx.values[0].data.array.items[1].data.string, "green");
    ASSERT_STR_EQ(ctx.values[0].data.array.items[2].data.string, "blue");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_integer_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "numbers = [1, 2, 3, 4, 5]");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].data.array.count, 5);
    ASSERT_EQ(ctx.values[0].data.array.items[0].data.integer, 1);
    ASSERT_EQ(ctx.values[0].data.array.items[4].data.integer, 5);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_empty_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "empty = []");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_ARRAY);
    ASSERT_EQ(ctx.values[0].data.array.count, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_multiline_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "items = [\n"
                        "  \"first\",\n"
                        "  \"second\",\n"
                        "  \"third\"\n"
                        "]";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].data.array.count, 3);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Inline Table Tests
 * ============================================================================
 */

TEST(parse_inline_table) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "point = { x = 10, y = 20 }");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_TABLE);
    ASSERT_EQ(ctx.values[0].data.table.count, 2);

    /// Verify table entries
    ASSERT_STR_EQ(ctx.values[0].data.table.entries[0].key, "x");
    ASSERT_EQ(ctx.values[0].data.table.entries[0].value->data.integer, 10);
    ASSERT_STR_EQ(ctx.values[0].data.table.entries[1].key, "y");
    ASSERT_EQ(ctx.values[0].data.table.entries[1].value->data.integer, 20);

    toml_value_free(&ctx.values[0]);
    toml_parser_cleanup(&parser);
}

TEST(parse_inline_table_with_strings) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "color = { fg = \"red\", bold = true }");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_TABLE);
    /// Both entries parse with their typed values in declaration order.
    ASSERT_EQ(ctx.values[0].data.table.count, 2);
    ASSERT_EQ(strcmp(ctx.values[0].data.table.entries[0].key, "fg"), 0);
    ASSERT_EQ(
        strcmp(ctx.values[0].data.table.entries[0].value->data.string, "red"),
        0);
    ASSERT_EQ(strcmp(ctx.values[0].data.table.entries[1].key, "bold"), 0);
    ASSERT_EQ(ctx.values[0].data.table.entries[1].value->data.boolean, true);

    toml_value_free(&ctx.values[0]);
    toml_parser_cleanup(&parser);
}

TEST(parse_empty_inline_table) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "empty = {}");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].type, TOML_VALUE_TABLE);
    ASSERT_EQ(ctx.values[0].data.table.count, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Section Tests
 * ============================================================================
 */

TEST(parse_simple_section) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "[section]\n"
                        "key = \"value\"";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.sections[0], "section");
    ASSERT_STR_EQ(ctx.keys[0], "key");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_nested_section) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "[shell.features]\n"
                        "extended_glob = true";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_STR_EQ(ctx.sections[0], "shell.features");
    ASSERT_STR_EQ(ctx.keys[0], "extended_glob");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_multiple_sections) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "[section1]\n"
                        "a = 1\n"
                        "[section2]\n"
                        "b = 2\n"
                        "[section3]\n"
                        "c = 3";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 3);
    ASSERT_STR_EQ(ctx.sections[0], "section1");
    ASSERT_STR_EQ(ctx.sections[1], "section2");
    ASSERT_STR_EQ(ctx.sections[2], "section3");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_keys_before_section) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "global = true\n"
                        "[section]\n"
                        "local = false";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 2);
    ASSERT_STR_EQ(ctx.sections[0], ""); /// Empty section for global keys
    ASSERT_STR_EQ(ctx.sections[1], "section");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Comment Tests
 * ============================================================================
 */

TEST(parse_with_comments) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "# This is a comment\n"
                        "key = \"value\" # inline comment\n"
                        "# Another comment\n"
                        "other = 123";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 2);
    ASSERT_STR_EQ(ctx.keys[0], "key");
    ASSERT_STR_EQ(ctx.keys[1], "other");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_comment_only) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "# Just a comment\n# And another");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================
 */

TEST(error_unterminated_string) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "key = \"unterminated");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);
    ASSERT(strlen(toml_parser_error(&parser)) > 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_missing_equals) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "key \"value\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_unterminated_section) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "[section\nkey = 1");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_unterminated_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "arr = [1, 2, 3");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_invalid_escape) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "key = \"bad\\xescape\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(error_line_column_tracking) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "good = 1\n"
                        "also_good = 2\n"
                        "bad = \"unterminated";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_INVALID_FORMAT);
    ASSERT_EQ(toml_parser_error_line(&parser), 3);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Callback Abort Tests
 * ============================================================================
 */

TEST(callback_abort) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};
    ctx.abort_key_name = "stop";
    ctx.abort_on_key = TOML_ERROR_CALLBACK_ABORT;

    const char *input = "first = 1\n"
                        "stop = 2\n"
                        "never_reached = 3";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_ERROR_CALLBACK_ABORT);
    ASSERT_EQ(ctx.count, 2); /// Parsed first and stop, but not never_reached

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Value Helper Tests
 * ============================================================================
 */

TEST(value_set_string) {
    toml_value_t value;
    memset(&value, 0, sizeof(value));

    toml_result_t result = toml_value_set_string(&value, "test string");
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(value.type, TOML_VALUE_STRING);
    ASSERT_STR_EQ(value.data.string, "test string");
}

TEST(value_set_integer) {
    toml_value_t value;
    memset(&value, 0, sizeof(value));

    toml_value_set_integer(&value, 12345);
    ASSERT_EQ(value.type, TOML_VALUE_INTEGER);
    ASSERT_EQ(value.data.integer, 12345);
}

TEST(value_set_boolean) {
    toml_value_t value;
    memset(&value, 0, sizeof(value));

    toml_value_set_boolean(&value, true);
    ASSERT_EQ(value.type, TOML_VALUE_BOOLEAN);
    ASSERT(value.data.boolean == true);
}

TEST(value_table_get_string) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "tbl = { name = \"test\", count = 5 }");
    toml_parser_parse(&parser, test_callback, &ctx);

    char buf[64];
    toml_result_t result =
        toml_value_table_get_string(&ctx.values[0], "name", buf, sizeof(buf));
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_STR_EQ(buf, "test");

    /// Test not found
    result = toml_value_table_get_string(&ctx.values[0], "missing", buf,
                                         sizeof(buf));
    ASSERT_EQ(result, TOML_ERROR_NOT_FOUND);

    toml_value_free(&ctx.values[0]);
    toml_parser_cleanup(&parser);
}

TEST(value_table_get_integer) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "tbl = { x = 100, y = 200 }");
    toml_parser_parse(&parser, test_callback, &ctx);

    int64_t val;
    toml_result_t result =
        toml_value_table_get_integer(&ctx.values[0], "x", &val);
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(val, 100);

    toml_value_free(&ctx.values[0]);
    toml_parser_cleanup(&parser);
}

TEST(value_table_get_boolean) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "tbl = { enabled = true, disabled = false }");
    toml_parser_parse(&parser, test_callback, &ctx);

    bool val;
    toml_result_t result =
        toml_value_table_get_boolean(&ctx.values[0], "enabled", &val);
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT(val == true);

    result = toml_value_table_get_boolean(&ctx.values[0], "disabled", &val);
    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT(val == false);

    toml_value_free(&ctx.values[0]);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Convenience Function Tests
 * ============================================================================
 */

TEST(value_is_type) {
    toml_value_t str_val = {.type = TOML_VALUE_STRING};
    toml_value_t int_val = {.type = TOML_VALUE_INTEGER};

    ASSERT(toml_value_is_type(&str_val, TOML_VALUE_STRING) == true);
    ASSERT(toml_value_is_type(&str_val, TOML_VALUE_INTEGER) == false);
    ASSERT(toml_value_is_type(&int_val, TOML_VALUE_INTEGER) == true);
    ASSERT(toml_value_is_type(NULL, TOML_VALUE_STRING) == false);
}

TEST(value_get_string_convenience) {
    toml_value_t value;
    value.type = TOML_VALUE_STRING;
    snprintf(value.data.string, sizeof(value.data.string), "hello");

    const char *result = toml_value_get_string(&value);
    ASSERT(result != NULL);
    ASSERT_STR_EQ(result, "hello");

    /// Wrong type returns NULL
    value.type = TOML_VALUE_INTEGER;
    ASSERT(toml_value_get_string(&value) == NULL);
}

/* ============================================================================
 * Complex Document Test
 * ============================================================================
 */

TEST(parse_complex_document) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "# Lush Configuration\n"
                        "\n"
                        "[shell]\n"
                        "mode = \"lush\"\n"
                        "errexit = false\n"
                        "\n"
                        "[shell.features]\n"
                        "extended_glob = true\n"
                        "arrays = true\n"
                        "\n"
                        "[history]\n"
                        "size = 10000\n"
                        "timestamps = true\n"
                        "\n"
                        "[display]\n"
                        "syntax_highlighting = true\n"
                        "optimization_level = 2\n";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 8);

    /// Verify sections
    ASSERT_STR_EQ(ctx.sections[0], "shell");
    ASSERT_STR_EQ(ctx.sections[2], "shell.features");
    ASSERT_STR_EQ(ctx.sections[4], "history");
    ASSERT_STR_EQ(ctx.sections[6], "display");

    /// Verify some values
    ASSERT_STR_EQ(ctx.keys[0], "mode");
    ASSERT_STR_EQ(ctx.values[0].data.string, "lush");

    ASSERT_STR_EQ(ctx.keys[4], "size");
    ASSERT_EQ(ctx.values[4].data.integer, 10000);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================
 */

TEST(parse_empty_input) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_whitespace_only) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "   \n\n   \t\t\n   ");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 0);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_key_with_underscore) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "my_key_name = 123");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_STR_EQ(ctx.keys[0], "my_key_name");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_key_with_hyphen) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "my-key-name = 456");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_STR_EQ(ctx.keys[0], "my-key-name");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_dotted_key) {
    /// Dotted keys round-trip the config registry's flat dotted paths: the
    /// registry emits "finder.match = ..." under a section and must read it
    /// back as the single key "finder.match".
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "finder.match = \"prefix\"");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 1);
    ASSERT_STR_EQ(ctx.keys[0], "finder.match");
    ASSERT_STR_EQ(ctx.values[0].data.string, "prefix");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_dotted_key_in_section) {
    /// Under a section, a dotted key composes the full path the same way the
    /// registry's load callback does: section "history" + key "finder.match".
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    const char *input = "[history]\n"
                        "finder.match = \"fuzzy\"\n"
                        "search_mode = \"plain\"";

    toml_parser_init(&parser, input);
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.count, 2);
    ASSERT_STR_EQ(ctx.sections[0], "history");
    ASSERT_STR_EQ(ctx.keys[0], "finder.match");
    ASSERT_STR_EQ(ctx.keys[1], "search_mode");

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

TEST(parse_trailing_comma_array) {
    toml_parser_t parser;
    test_ctx_t ctx = {0};

    toml_parser_init(&parser, "arr = [1, 2, 3,]");
    toml_result_t result = toml_parser_parse(&parser, test_callback, &ctx);

    ASSERT_EQ(result, TOML_SUCCESS);
    ASSERT_EQ(ctx.values[0].data.array.count, 3);

    free_test_ctx(&ctx);
    toml_parser_cleanup(&parser);
}

/* ============================================================================
 * Main
 * ============================================================================
 */

int main(void) {
    printf("=== TOML Parser Unit Tests ===\n\n");

    printf("Initialization Tests:\n");
    RUN_TEST(parser_init_null_parser);
    RUN_TEST(parser_init_null_input);
    RUN_TEST(parser_init_success);
    RUN_TEST(parser_init_with_length);
    RUN_TEST(parser_reset);

    printf("\nString Value Tests:\n");
    RUN_TEST(parse_simple_string);
    RUN_TEST(parse_string_with_escapes);
    RUN_TEST(parse_empty_string);

    printf("\nUnicode Escape Tests:\n");
    RUN_TEST(parse_unicode_escape_bmp);
    RUN_TEST(parse_unicode_escape_emoji);
    RUN_TEST(parse_unicode_escape_mixed);
    RUN_TEST(error_unicode_escape_incomplete);
    RUN_TEST(error_unicode_escape_invalid_hex);
    RUN_TEST(error_unicode_escape_surrogate);

    printf("\nInteger Value Tests:\n");
    RUN_TEST(parse_positive_integer);
    RUN_TEST(parse_negative_integer);
    RUN_TEST(parse_zero);
    RUN_TEST(parse_large_integer);

    printf("\nBoolean Value Tests:\n");
    RUN_TEST(parse_boolean_true);
    RUN_TEST(parse_boolean_false);

    printf("\nArray Value Tests:\n");
    RUN_TEST(parse_string_array);
    RUN_TEST(parse_integer_array);
    RUN_TEST(parse_empty_array);
    RUN_TEST(parse_multiline_array);

    printf("\nInline Table Tests:\n");
    RUN_TEST(parse_inline_table);
    RUN_TEST(parse_inline_table_with_strings);
    RUN_TEST(parse_empty_inline_table);

    printf("\nSection Tests:\n");
    RUN_TEST(parse_simple_section);
    RUN_TEST(parse_nested_section);
    RUN_TEST(parse_multiple_sections);
    RUN_TEST(parse_keys_before_section);

    printf("\nComment Tests:\n");
    RUN_TEST(parse_with_comments);
    RUN_TEST(parse_comment_only);

    printf("\nError Handling Tests:\n");
    RUN_TEST(error_unterminated_string);
    RUN_TEST(error_missing_equals);
    RUN_TEST(error_unterminated_section);
    RUN_TEST(error_unterminated_array);
    RUN_TEST(error_invalid_escape);
    RUN_TEST(error_line_column_tracking);

    printf("\nCallback Tests:\n");
    RUN_TEST(callback_abort);

    printf("\nValue Helper Tests:\n");
    RUN_TEST(value_set_string);
    RUN_TEST(value_set_integer);
    RUN_TEST(value_set_boolean);
    RUN_TEST(value_table_get_string);
    RUN_TEST(value_table_get_integer);
    RUN_TEST(value_table_get_boolean);

    printf("\nConvenience Function Tests:\n");
    RUN_TEST(value_is_type);
    RUN_TEST(value_get_string_convenience);

    printf("\nComplex Document Tests:\n");
    RUN_TEST(parse_complex_document);

    printf("\nEdge Case Tests:\n");
    RUN_TEST(parse_empty_input);
    RUN_TEST(parse_whitespace_only);
    RUN_TEST(parse_key_with_underscore);
    RUN_TEST(parse_key_with_hyphen);
    RUN_TEST(parse_dotted_key);
    RUN_TEST(parse_dotted_key_in_section);
    RUN_TEST(parse_trailing_comma_array);

    return TEST_RESULT();
}
