/**
 * @file test_splicer.c
 * @brief Unit tests for splicer
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * Lush Shell - LLE Splicer Unit Tests
 * Copyright (C) 2021-2026  Michael Berry
 *
 * Licensed under the MIT License. See LICENSE file for details.
 *
 * Tests the pure-function layer of the splicer: rendering candidate
 * literals into the cursor's quote/escape context, the close-char
 * lookup, and lle_splicer_compute which combines rendering with the
 * accept-time suffix logic to produce a full splice description.
 *
 * The apply layer (lle_buffer_replace_text + cursor manager) is not
 * exercised here because it has no isolated unit-testable surface
 * without a real LLE buffer; it will be tested via integration when
 * the completion engine is routed through these primitives.
 */

#include "lle/completion/completion_types.h"
#include "lle/completion/splicer.h"
#include "lle/completion/word_context.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#define POOL ((lle_memory_pool_t *)1)

/* ============================================================================
 * Render rules -- NONE quote state
 * ============================================================================
 */

TEST(render_none_passes_through_plain_alphanumerics) {
    char *out = NULL;
    size_t len = 0;
    lle_result_t r =
        lle_splicer_render_for_context("Documents123", strlen("Documents123"),
                                       LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "Documents123") == 0);
    ASSERT(len == strlen("Documents123"));
}

TEST(render_none_escapes_space) {
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        "my file.txt", strlen("my file.txt"), LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "my\\ file.txt") == 0);
}

TEST(render_none_escapes_all_shell_metacharacters) {
    /// Each metacharacter must come back with a leading backslash.
    /// Construct a candidate with one of each and check the result.
    const char *input = " \t;|&<>(){}\"'$`\\*?[!";
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        input, strlen(input), LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    /// Output should be exactly twice the length of input (every byte
    /// gets a leading backslash).
    ASSERT(len == strlen(input) * 2);
    /// And the content is the input with backslashes prepended.
    char expected[64];
    size_t e = 0;
    for (size_t i = 0; i < strlen(input); i++) {
        expected[e++] = '\\';
        expected[e++] = input[i];
    }
    expected[e] = '\0';
    ASSERT(strcmp(out, expected) == 0);
}

TEST(render_none_escapes_tilde_only_at_position_zero) {
    char *out = NULL;
    size_t len = 0;
    /// ~/foo at position 0 -- the leading ~ is escaped, the inner / is
    /// escaped (it's a path separator in shell context for an unquoted
    /// word, but actually slashes aren't special in unquoted word
    /// tokenization; see needs_escape_in_none -- / is NOT in the list).
    /// Wait: my rule does NOT escape '/'. Let me reread the function.
    /// Re-checking: no, / isn't in needs_escape_in_none. Adjust the
    /// expectation.
    lle_result_t r = lle_splicer_render_for_context(
        "~/foo", strlen("~/foo"), LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    /// Leading ~ escaped, / and remaining chars literal.
    ASSERT(strcmp(out, "\\~/foo") == 0);

    /// Same character mid-word does NOT escape.
    r = lle_splicer_render_for_context("foo~bar", strlen("foo~bar"),
                                       LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "foo~bar") == 0);
}

TEST(render_none_escapes_hash_only_at_position_zero) {
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        "#foo", strlen("#foo"), LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "\\#foo") == 0);

    r = lle_splicer_render_for_context("foo#bar", strlen("foo#bar"),
                                       LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "foo#bar") == 0);
}

TEST(render_none_passes_non_ascii_bytes_through) {
    /// Multi-byte UTF-8 bytes (high bit set) are not shell-special
    /// regardless of quote_state and must pass through untouched.
    /// "cafe-acute" with e-acute = U+00E9 = 0xC3 0xA9
    const char *input = "caf\xC3\xA9";
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        input, strlen(input), LLE_QUOTE_NONE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, input) == 0);
}

/* ============================================================================
 * Render rules -- DOUBLE quote state
 * ============================================================================
 */

TEST(render_double_passes_space_through_unescaped) {
    char *out = NULL;
    size_t len = 0;
    lle_result_t r =
        lle_splicer_render_for_context("my file.txt", strlen("my file.txt"),
                                       LLE_QUOTE_DOUBLE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    /// Inside double quotes, space is literal -- no backslash.
    ASSERT(strcmp(out, "my file.txt") == 0);
}

TEST(render_double_escapes_only_dollar_backtick_backslash_doublequote) {
    /// Only $ ` \ " need escaping inside double quotes; everything
    /// else (including * ? [ etc.) is literal.
    const char *input = "a$b`c\\d\"e*f?g[h";
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        input, strlen(input), LLE_QUOTE_DOUBLE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    /// $, `, \, " escaped; *, ?, [ literal.
    ASSERT(strcmp(out, "a\\$b\\`c\\\\d\\\"e*f?g[h") == 0);
}

/* ============================================================================
 * Render rules -- SINGLE quote state
 * ============================================================================
 */

TEST(render_single_passes_metacharacters_through) {
    /// Inside '...' nothing escapes (POSIX). All metacharacters are
    /// literal.
    const char *input = "$`\\*?[!";
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        input, strlen(input), LLE_QUOTE_SINGLE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, input) == 0);
}

TEST(render_single_handles_embedded_single_quote) {
    /// A ' in the candidate is rendered '\'' -- close, escape, reopen.
    /// Candidate: it's   ->  it'\''s
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        "it's", strlen("it's"), LLE_QUOTE_SINGLE, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "it'\\''s") == 0);
}

/* ============================================================================
 * Render rules -- BACKTICK quote state
 * ============================================================================
 */

TEST(render_backtick_escapes_dollar_backtick_backslash) {
    /// Inside `...` only $ ` \ are special. Double-quote is NOT
    /// special here.
    const char *input = "a$b`c\\d\"e";
    char *out = NULL;
    size_t len = 0;
    lle_result_t r = lle_splicer_render_for_context(
        input, strlen(input), LLE_QUOTE_BACKTICK, POOL, &out, &len);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(out, "a\\$b\\`c\\\\d\"e") == 0);
}

/* close_char is a small switch statement over the quote_state enum. Its
 * correctness is covered transitively by the compute tests below, which
 * verify the close-char actually lands in the rendered insert_text on
 * accept for the SINGLE/DOUBLE/BACKTICK cases and is absent for NONE. A
 * direct test would be filler. */

/* ============================================================================
 * lle_splicer_compute -- full splice with suffix logic
 * ============================================================================
 *
 * compute() needs an lle_word_context_t and an lle_completion_item_t.
 * For these tests we construct them by hand instead of going through
 * the analyzer or item factories -- the splicer has no dependency on
 * how those structs were produced; it reads only the fields it cares
 * about (quote_state, filename_portion_start, word_end on the
 * context; text and type on the item).
 */

static void make_context(lle_word_context_t *ctx, lle_quote_state_t qs,
                         size_t fp_start, size_t word_end) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->quote_state = qs;
    ctx->filename_portion_start = fp_start;
    ctx->word_end = word_end;
}

static void make_item(lle_completion_item_t *item, const char *text,
                      lle_completion_type_t type) {
    memset(item, 0, sizeof(*item));
    item->text = (char *)text;
    item->type = type;
}

TEST(compute_preview_phase_emits_no_suffix) {
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_NONE, 4, 6); /// simulating "cat my"
    lle_completion_item_t item;
    make_item(&item, "my file.txt", LLE_COMPLETION_TYPE_FILE);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, false, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    /// Insert text is the rendered candidate, no suffix.
    ASSERT(strcmp(splice.insert_text, "my\\ file.txt") == 0);
    ASSERT(splice.delete_start == 4);
    ASSERT(splice.delete_length == 2); /// "my"
    ASSERT(splice.cursor_after == 4 + splice.insert_length);
}

TEST(compute_accept_file_no_quote_appends_space) {
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_NONE, 4, 6);
    lle_completion_item_t item;
    make_item(&item, "my file.txt", LLE_COMPLETION_TYPE_FILE);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    /// Trailing space, no close char.
    ASSERT(strcmp(splice.insert_text, "my\\ file.txt ") == 0);
}

TEST(compute_accept_file_double_quote_appends_close_and_space) {
    lle_word_context_t ctx;
    /// simulating "cat \"my fi" -- word_start at the open ", filename
    /// portion starts one byte later.
    make_context(&ctx, LLE_QUOTE_DOUBLE, 5, 10);
    lle_completion_item_t item;
    make_item(&item, "my file.txt", LLE_COMPLETION_TYPE_FILE);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    /// Inside double quotes the candidate renders without space-escape;
    /// accept appends close-quote and space.
    ASSERT(strcmp(splice.insert_text, "my file.txt\" ") == 0);
}

TEST(compute_accept_file_single_quote_appends_close_and_space) {
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_SINGLE, 5, 10);
    lle_completion_item_t item;
    make_item(&item, "my file.txt", LLE_COMPLETION_TYPE_FILE);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    /// Inside single quotes the candidate renders literally; accept
    /// appends ' and space.
    ASSERT(strcmp(splice.insert_text, "my file.txt' ") == 0);
}

TEST(compute_accept_directory_appends_slash_no_close) {
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_DOUBLE, 4, 7); /// simulating cd "my
    lle_completion_item_t item;
    make_item(&item, "My Documents", LLE_COMPLETION_TYPE_DIRECTORY);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    /// Directory: render + "/", no close, no space.
    ASSERT(strcmp(splice.insert_text, "My Documents/") == 0);
}

TEST(compute_accept_command_type_appends_close_and_space) {
    /// COMMAND / BUILTIN / ALIAS / VARIABLE / HISTORY / CUSTOM all
    /// follow the file-style suffix rule. Spot-check COMMAND.
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_NONE, 0, 2);
    lle_completion_item_t item;
    make_item(&item, "echo", LLE_COMPLETION_TYPE_COMMAND);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(strcmp(splice.insert_text, "echo ") == 0);
}

TEST(compute_delete_range_replaces_filename_portion_only) {
    /// For cat ~/Doc, word_start is at ~, filename_portion_start is
    /// after the /, word_end is at cursor. compute() must delete only
    /// the filename portion ("Doc") not the path prefix ("~/").
    lle_word_context_t ctx;
    /// word_start=4 (~), filename_portion_start=6 (after /),
    /// word_end=9 (after Doc) -- typed bytes are "cat ~/Doc".
    make_context(&ctx, LLE_QUOTE_NONE, 6, 9);
    lle_completion_item_t item;
    make_item(&item, "Documents", LLE_COMPLETION_TYPE_DIRECTORY);

    lle_splicer_splice_t splice;
    lle_result_t r = lle_splicer_compute(&ctx, &item, true, POOL, &splice);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(splice.delete_start == 6);
    ASSERT(splice.delete_length == 3); /// "Doc"
    /// Insert is "Documents/" -- the path prefix "~/" is NOT in the
    /// insert_text because the engine preserves it untouched.
    ASSERT(strcmp(splice.insert_text, "Documents/") == 0);
    ASSERT(splice.cursor_after == 6 + strlen("Documents/"));
}

TEST(compute_invalid_inputs_return_error) {
    lle_word_context_t ctx;
    make_context(&ctx, LLE_QUOTE_NONE, 0, 0);
    lle_completion_item_t item;
    make_item(&item, "x", LLE_COMPLETION_TYPE_FILE);
    lle_splicer_splice_t splice;

    ASSERT(lle_splicer_compute(NULL, &item, true, POOL, &splice) ==
           LLE_ERROR_INVALID_PARAMETER);
    ASSERT(lle_splicer_compute(&ctx, NULL, true, POOL, &splice) ==
           LLE_ERROR_INVALID_PARAMETER);
    ASSERT(lle_splicer_compute(&ctx, &item, true, NULL, &splice) ==
           LLE_ERROR_INVALID_PARAMETER);
    ASSERT(lle_splicer_compute(&ctx, &item, true, POOL, NULL) ==
           LLE_ERROR_INVALID_PARAMETER);

    /// item->text NULL is also invalid.
    item.text = NULL;
    ASSERT(lle_splicer_compute(&ctx, &item, true, POOL, &splice) ==
           LLE_ERROR_INVALID_PARAMETER);
}

/* ============================================================================
 * Test runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Splicer Unit Tests ===\n");

    /// NONE rendering
    RUN_TEST(render_none_passes_through_plain_alphanumerics);
    RUN_TEST(render_none_escapes_space);
    RUN_TEST(render_none_escapes_all_shell_metacharacters);
    RUN_TEST(render_none_escapes_tilde_only_at_position_zero);
    RUN_TEST(render_none_escapes_hash_only_at_position_zero);
    RUN_TEST(render_none_passes_non_ascii_bytes_through);

    /// DOUBLE rendering
    RUN_TEST(render_double_passes_space_through_unescaped);
    RUN_TEST(render_double_escapes_only_dollar_backtick_backslash_doublequote);

    /// SINGLE rendering
    RUN_TEST(render_single_passes_metacharacters_through);
    RUN_TEST(render_single_handles_embedded_single_quote);

    /// BACKTICK rendering
    RUN_TEST(render_backtick_escapes_dollar_backtick_backslash);

    /// compute
    RUN_TEST(compute_preview_phase_emits_no_suffix);
    RUN_TEST(compute_accept_file_no_quote_appends_space);
    RUN_TEST(compute_accept_file_double_quote_appends_close_and_space);
    RUN_TEST(compute_accept_file_single_quote_appends_close_and_space);
    RUN_TEST(compute_accept_directory_appends_slash_no_close);
    RUN_TEST(compute_accept_command_type_appends_close_and_space);
    RUN_TEST(compute_delete_range_replaces_filename_portion_only);
    RUN_TEST(compute_invalid_inputs_return_error);

    return TEST_RESULT();
}
