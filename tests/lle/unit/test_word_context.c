/*
 * Lush Shell - LLE Word-Context Analyzer Unit Tests
 * Copyright (C) 2021-2026  Michael Berry
 *
 * Licensed under the MIT License. See LICENSE file for details.
 *
 * Covers cases the current analyzer revision handles end-to-end:
 * quote/escape state, word boundaries respecting quote state,
 * filename_portion_start, NFC dequote, in-progress expansion_kind,
 * context_type detection, command-name + arg-index extraction.
 *
 * Cases that depend on expansion resolution (expanded_directory
 * population from src/expand.c, multi-value brace branches[]) are not
 * exercised here; they will be added when the analyzer's resolution
 * layer is implemented.
 */

#include "lle/completion/word_context.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

/* Mock controls defined in tests/lle/functional/test_completion_mock.c.
 * The analyzer's resolution helpers call expand_if_needed and
 * expand_brace_pattern; the mock returns programmed values controlled
 * by these globals. mock_completion_reset() restores defaults. */
typedef struct executor executor_t;
extern executor_t *current_executor;
extern const char *mock_expand_result;
extern const char *const *mock_brace_branches;
extern int mock_brace_branch_count;
extern void mock_completion_reset(void);

/* The analyzer accepts any non-NULL pool pointer; LLE's pool allocator
 * uses a process-global arena under the hood so a sentinel pointer is
 * sufficient for unit-test purposes. Same pattern as
 * test_completion_types.c. */
#define POOL ((lle_memory_pool_t *)1)

/* Convenience: analyze and assert success. */
#define ANALYZE(buf, cursor, ctx_var)                                          \
    lle_word_context_t *ctx_var = NULL;                                        \
    {                                                                          \
        lle_result_t _r =                                                      \
            lle_word_context_analyze((buf), (cursor), POOL, &(ctx_var));       \
        ASSERT(_r == LLE_SUCCESS);                                             \
        ASSERT(ctx_var != NULL);                                               \
    }

/* ============================================================================
 * Quote-state and word-boundary cases
 * ============================================================================ */

TEST(empty_buffer) {
    ANALYZE("", 0, ctx);
    ASSERT(ctx->word_start == 0);
    ASSERT(ctx->word_end == 0);
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_NONE);
    ASSERT(ctx->filename_portion_start == 0);
    ASSERT(ctx->dequoted_filename_prefix != NULL);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "") == 0);
    ASSERT(ctx->context_type == LLE_CONTEXT_COMMAND_POSITION);
    ASSERT(ctx->command_name == NULL);
}

TEST(plain_word_at_command_position) {
    /* "ec|" — cursor right after typing 'ec' as the first word */
    ANALYZE("ec", 2, ctx);
    ASSERT(ctx->word_start == 0);
    ASSERT(ctx->word_end == 2);
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    ASSERT(ctx->context_type == LLE_CONTEXT_COMMAND_POSITION);
    ASSERT(ctx->filename_portion_start == 0);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "ec") == 0);
}

TEST(plain_word_as_argument) {
    /* "cat my|" — cursor after typing 'my' as first arg to cat */
    ANALYZE("cat my", 6, ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->word_end == 6);
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "my") == 0);
    ASSERT(ctx->command_name != NULL);
    ASSERT(strcmp(ctx->command_name, "cat") == 0);
    ASSERT(ctx->arg_index == 0);
}

TEST(double_quote_open_word) {
    /* cat "my fi|   — quote_state must be DOUBLE; word_start is the open " */
    const char *buf = "cat \"my fi";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    /* filename_portion_start = byte after the open " (no '/' in word). */
    ASSERT(ctx->filename_portion_start == 5);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "my fi") == 0);
    ASSERT(strcmp(ctx->command_name, "cat") == 0);
}

TEST(single_quote_open_word) {
    /* cat 'my fi|   — quote_state must be SINGLE */
    const char *buf = "cat 'my fi";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->quote_state == LLE_QUOTE_SINGLE);
    ASSERT(ctx->filename_portion_start == 5);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "my fi") == 0);
}

TEST(backtick_open_word) {
    /* cat `da|   — quote_state must be BACKTICK */
    const char *buf = "cat `da";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_BACKTICK);
}

TEST(escape_pending_at_cursor) {
    /* cat \   — cursor right after a backslash, escape_pending */
    const char *buf = "cat \\";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_ESCAPE_PENDING);
}

TEST(backslash_escaped_space_in_word) {
    /* cat my\ fi|  — '\ ' is part of the same word; dequoted = "my fi" */
    const char *buf = "cat my\\ fi";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->word_end == strlen(buf));
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "my fi") == 0);
}

TEST(closed_quote_then_more_text) {
    /* cat "abc"de|  — quote closed; the word continues unquoted */
    const char *buf = "cat \"abc\"de";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    /* word_start should still be the open " (the quoted segment is part
     * of the word). filename_portion_start skips the open ". */
    ASSERT(ctx->word_start == 4);
}

TEST(double_quote_with_internal_backslash_escape) {
    /* cat "abc\$|  — \$ inside double quotes escapes the $; dequoted = abc$ */
    const char *buf = "cat \"abc\\$";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "abc$") == 0);
}

/* ============================================================================
 * filename_portion_start computation
 * ============================================================================ */

TEST(filename_portion_after_slash_unquoted) {
    /* cat path/to/Doc|  — filename_portion_start at byte after last '/' */
    const char *buf = "cat path/to/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->filename_portion_start == 12); /* byte after the last '/' */
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "Doc") == 0);
}

TEST(filename_portion_inside_double_quote) {
    /* cat "path/to/Do|c — filename_portion_start at byte after last '/' */
    const char *buf = "cat \"path/to/Do";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    /* The path inside the open quote: "path/to/Do
     *   bytes: 4 = ", 5 = p, 6 = a, 7 = t, 8 = h, 9 = /,
     *          10 = t, 11 = o, 12 = /, 13 = D, 14 = o
     * filename_portion_start should be 13 (byte after last '/'). */
    ASSERT(ctx->filename_portion_start == 13);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "Do") == 0);
}

TEST(no_slash_in_word_filename_portion_at_word_start) {
    /* cat my|  — no '/' in word, filename_portion_start = word_start */
    const char *buf = "cat my";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->filename_portion_start == 4);
}

TEST(no_slash_in_quoted_word_filename_portion_skips_open_quote) {
    /* cat "my  — filename_portion_start = byte after open " */
    const char *buf = "cat \"my";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == 4);
    ASSERT(ctx->filename_portion_start == 5);
}

/* ============================================================================
 * Context type detection
 * ============================================================================ */

TEST(command_position_at_buffer_start) {
    ANALYZE("", 0, ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_COMMAND_POSITION);
}

TEST(command_position_after_pipe) {
    /* ls -l | gr|  — cursor after pipe + space, command position again */
    const char *buf = "ls -l | gr";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_COMMAND_POSITION);
}

TEST(command_position_after_semicolon) {
    /* cmd; ne|  — semicolon resets to command position */
    const char *buf = "cmd; ne";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_COMMAND_POSITION);
}

TEST(redirect_target_after_gt) {
    /* cat foo > /tmp/o|  — after '>', next word is a redirect target */
    const char *buf = "cat foo > /tmp/o";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_REDIRECT_TARGET);
}

TEST(redirect_target_after_lt) {
    /* cat < my|  — after '<', next word is a redirect target */
    const char *buf = "cat < my";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_REDIRECT_TARGET);
}

TEST(argument_after_command) {
    /* cat foo|  — after the command name, this is argument position */
    const char *buf = "cat foo";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
    ASSERT(ctx->arg_index == 0);
}

TEST(arg_index_increments_with_args) {
    /* cat foo bar baz|  — third arg => arg_index == 2 */
    const char *buf = "cat foo bar baz";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->arg_index == 2);
}

TEST(command_name_extraction) {
    const char *buf = "kubectl get po";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->command_name != NULL);
    ASSERT(strcmp(ctx->command_name, "kubectl") == 0);
}

/* ============================================================================
 * In-progress expansion_kind
 * ============================================================================ */

TEST(expansion_kind_variable_name) {
    /* echo $HO|  — cursor mid-variable-name */
    const char *buf = "echo $HO";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_VARIABLE_NAME);
    ASSERT(ctx->context_type == LLE_CONTEXT_VARIABLE_NAME);
}

TEST(expansion_kind_braced_variable_name) {
    /* echo ${HO|  — cursor mid-braced-variable-name */
    const char *buf = "echo ${HO";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_BRACED_VARIABLE_NAME);
    ASSERT(ctx->context_type == LLE_CONTEXT_VARIABLE_NAME);
}

TEST(expansion_kind_command_subst_open) {
    /* echo $(ls|  — cursor inside open $( */
    const char *buf = "echo $(ls";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_COMMAND_SUBST);
}

TEST(expansion_kind_arithmetic_open) {
    /* echo $((1+|  — cursor inside open $(( */
    const char *buf = "echo $((1+";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_ARITHMETIC);
}

TEST(expansion_kind_brace_list_with_comma) {
    /* cat {a,b|  — cursor inside open brace expansion with a comma */
    const char *buf = "cat {a,b";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_BRACE_LIST);
}

TEST(expansion_kind_glob_in_word) {
    /* cat *.tx|  — glob char present, no other in-progress expansion */
    const char *buf = "cat *.tx";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_GLOB);
}

TEST(expansion_kind_none_after_complete_var) {
    /* cat $HOME/Doc|  — variable is complete, cursor in filename portion;
     * expansion_kind is NONE (no in-progress expansion at cursor) */
    const char *buf = "cat $HOME/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_NONE);
}

TEST(expansion_kind_variable_name_at_end_of_word) {
    /* cat $HOME|  — cursor right after a $NAME with no path content. The
     * analyzer is purely structural: it cannot know whether $HOME is a
     * "complete" environment variable or a partial that the user intends
     * to extend. It always reports VARIABLE_NAME; the engine consults the
     * variables source to disambiguate (exact-single-match → resolve;
     * otherwise offer completions). See COMPLETION_REWRITE_PLAN.md
     * walkthrough 10.9 for the engine-side routing. */
    const char *buf = "cat $HOME";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expansion_kind == LLE_EXPANSION_VARIABLE_NAME);
    ASSERT(ctx->context_type == LLE_CONTEXT_VARIABLE_NAME);
}

/* ============================================================================
 * NFC normalization of dequoted prefix
 * ============================================================================ */

TEST(nfc_normalization_handles_decomposed_input) {
    /* The user types 'caf' followed by 'e' + combining acute (U+0301).
     * NFC should recompose to U+00E9. UTF-8 bytes:
     *   c a f e (4 ASCII) + 0xCC 0x81 (U+0301)
     * Total raw input: "cafe\xCC\x81"
     * Expected NFC: "caf" + 0xC3 0xA9  (which is "café")
     */
    const char *buf = "cat cafe\xCC\x81";
    ANALYZE(buf, strlen(buf), ctx);
    /* The dequoted, NFC-normalized prefix should compose the e + combining
     * acute into the single precomposed é codepoint. */
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "caf\xC3\xA9") == 0);
}

/* ============================================================================
 * Multiline buffer handling
 * ============================================================================ */

TEST(multiline_buffer_command_position_after_newline) {
    /* "echo hi\ncat my|"  — newline outside any quote resets to command
     * position for the next pipeline. Cursor on second line in the middle
     * of an argument. */
    const char *buf = "echo hi\ncat my";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
    ASSERT(ctx->command_name != NULL);
    ASSERT(strcmp(ctx->command_name, "cat") == 0);
}

TEST(multiline_buffer_newline_inside_double_quote_is_literal) {
    /* "echo \"abc\ndef|"  — open " spans the newline; the cursor is still
     * inside the same quoted word. */
    const char *buf = "echo \"abc\ndef";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    /* The dequoted prefix of the word is the literal content. */
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "abc\ndef") == 0);
}

/* ============================================================================
 * Edge cases
 * ============================================================================ */

TEST(cursor_past_buffer_end_clamps) {
    /* Pass cursor offset > strlen(buffer); analyzer should clamp. */
    const char *buf = "abc";
    lle_word_context_t *ctx = NULL;
    lle_result_t r = lle_word_context_analyze(buf, 999, POOL, &ctx);
    ASSERT(r == LLE_SUCCESS);
    ASSERT(ctx->word_end == 3);
}

TEST(invalid_inputs_return_error) {
    lle_word_context_t *ctx = NULL;
    ASSERT(lle_word_context_analyze(NULL, 0, POOL, &ctx) ==
           LLE_ERROR_INVALID_PARAMETER);
    ASSERT(lle_word_context_analyze("x", 0, NULL, &ctx) ==
           LLE_ERROR_INVALID_PARAMETER);
    ASSERT(lle_word_context_analyze("x", 0, POOL, NULL) ==
           LLE_ERROR_INVALID_PARAMETER);
}

TEST(cursor_at_whitespace_yields_empty_word) {
    /* cat |  — cursor right after a space; current word is empty */
    const char *buf = "cat ";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->word_start == ctx->word_end);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "") == 0);
    /* Argument-position completion of a fresh empty word. */
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
}

TEST(free_handles_null) {
    /* Smoke test that the free function accepts NULL safely. */
    lle_word_context_free(NULL);
    /* If we got here, no crash. Pass. */
    ASSERT(1 == 1);
}

/* ============================================================================
 * Bug-relevant scenarios (exercise the actual regression surfaces)
 * ============================================================================ */

TEST(cursor_in_second_arg_after_first_quoted_arg) {
    /* cat "my file" /tmp/o|  — first arg is a complete quoted string;
     * cursor is in the SECOND arg's filename portion. The walker must
     * have closed the quote correctly so this doesn't bleed. */
    const char *buf = "cat \"my file\" /tmp/o";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_NONE);
    /* word_start should be at /tmp/o, not still at "my */
    ASSERT(ctx->word_start == 14);
    ASSERT(ctx->arg_index == 1);
    /* filename_portion_start = byte after the last unquoted '/' in the
     * current word ("/tmp/o" → byte after '/' before 'o' = position 19). */
    ASSERT(ctx->filename_portion_start == 19);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "o") == 0);
}

TEST(quoted_arg_after_pipe_is_argument_not_command_position) {
    /* ls | grep "p|  — cursor is inside a quoted argument to grep, not
     * at command position. The pipe reset to command_position when grep
     * was at the start, then grep's command-word terminated normally,
     * and now we're in argument-position with quote_state=DOUBLE. */
    const char *buf = "ls | grep \"p";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
    ASSERT(ctx->command_name != NULL);
    ASSERT(strcmp(ctx->command_name, "grep") == 0);
    ASSERT(ctx->arg_index == 0);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "p") == 0);
}

TEST(unquoted_pipe_inside_quoted_string_is_literal) {
    /* echo "a | b|  — the | is inside a still-open double quote; it
     * must NOT be interpreted as a pipe (which would reset command
     * position). The cursor should remain in argument position with
     * the original "echo" still as the command name. */
    const char *buf = "echo \"a | b";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->quote_state == LLE_QUOTE_DOUBLE);
    ASSERT(ctx->context_type == LLE_CONTEXT_ARGUMENT);
    ASSERT(ctx->command_name != NULL);
    ASSERT(strcmp(ctx->command_name, "echo") == 0);
    /* The dequoted prefix should include the literal pipe and surrounding
     * spaces (everything inside the open " is literal at the analyzer
     * level; interpretation of nested expansions is the resolution
     * layer's job and is not handled in this revision). */
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "a | b") == 0);
}

/* ============================================================================
 * Expansion resolution (path-prefix bytes resolved to a directory or
 * to a per-branch set via the executor's expansion machinery)
 * ============================================================================ */

TEST(resolve_path_prefix_with_tilde) {
    /* cat ~/Doc|  — path-prefix ~/  resolves to /home/test/. */
    mock_completion_reset();
    current_executor = (executor_t *)1; /* sentinel; analyzer only checks NULL */
    mock_expand_result = "/home/test/";
    const char *buf = "cat ~/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expanded_directory != NULL);
    ASSERT(strcmp(ctx->expanded_directory, "/home/test/") == 0);
    ASSERT(ctx->branch_count == 0);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "Doc") == 0);
    mock_completion_reset();
}

TEST(resolve_path_prefix_with_variable) {
    /* cat $HOME/Doc|  — same shape; mock resolves $HOME/ to /home/test/. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    mock_expand_result = "/home/test/";
    const char *buf = "cat $HOME/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expanded_directory != NULL);
    ASSERT(strcmp(ctx->expanded_directory, "/home/test/") == 0);
    ASSERT(strcmp(ctx->dequoted_filename_prefix, "Doc") == 0);
    mock_completion_reset();
}

TEST(resolve_path_prefix_absolute_path) {
    /* cat /tmp/Doc|  — absolute path; mock pass-through. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    mock_expand_result = "/tmp/";
    const char *buf = "cat /tmp/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expanded_directory != NULL);
    ASSERT(strcmp(ctx->expanded_directory, "/tmp/") == 0);
    mock_completion_reset();
}

TEST(no_path_prefix_leaves_expanded_directory_null) {
    /* cat my|  — no path prefix; analyzer doesn't call expansion. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    mock_expand_result = "should_not_be_used";
    const char *buf = "cat my";
    ANALYZE(buf, strlen(buf), ctx);
    /* No '/' before cursor → resolve helper short-circuits without
     * calling the expander. */
    ASSERT(ctx->expanded_directory == NULL);
    mock_completion_reset();
}

TEST(no_executor_leaves_expanded_directory_null) {
    /* When current_executor is NULL the analyzer cannot evaluate
     * expansions; expanded_directory stays NULL even when path-prefix
     * bytes are present. The engine treats this as "search cwd" or
     * "refuse" depending on context. */
    mock_completion_reset();
    /* current_executor stays NULL; mock_expand_result is irrelevant. */
    const char *buf = "cat ~/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->expanded_directory == NULL);
    ASSERT(ctx->branch_count == 0);
    mock_completion_reset();
}

TEST(brace_in_path_prefix_populates_branches) {
    /* cat {a,b}/Doc|  — brace expansion yields per-directory branches.
     * Mock expand_brace_pattern returns ["a/", "b/"]; expand_if_needed
     * is called once per branch and pass-through returns the directory
     * literal in each case. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    static const char *branches[] = {"a/", "b/"};
    mock_brace_branches = branches;
    mock_brace_branch_count = 2;
    /* For the per-branch resolution we want the expander to echo
     * whatever it's given. The current mock returns mock_expand_result
     * unconditionally; setting it to "a/" yields the same value for
     * both branches, which is enough to verify branch_count and the
     * shared dequoted_filename_prefix wiring. */
    mock_expand_result = "a/";
    const char *buf = "cat {a,b}/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->branch_count == 2);
    ASSERT(ctx->branches != NULL);
    ASSERT(ctx->branches[0].expanded_directory != NULL);
    ASSERT(ctx->branches[1].expanded_directory != NULL);
    /* The shared filename prefix is "Doc" — every branch points at the
     * same dequoted_filename_prefix string the analyzer extracted from
     * the typed bytes. */
    ASSERT(ctx->branches[0].dequoted_filename_prefix == ctx->dequoted_filename_prefix);
    ASSERT(strcmp(ctx->branches[0].dequoted_filename_prefix, "Doc") == 0);
    /* When branches[] is populated, expanded_directory is left NULL —
     * the engine reads branches when branch_count > 0. */
    ASSERT(ctx->expanded_directory == NULL);
    mock_completion_reset();
}

TEST(brace_with_no_comma_does_not_populate_branches) {
    /* cat {nocomma}/Doc|  — a brace expression with no comma or range
     * is not a multi-branch pattern; the analyzer falls through to
     * single-value resolution. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    mock_expand_result = "/expanded/";
    const char *buf = "cat {nocomma}/Doc";
    ANALYZE(buf, strlen(buf), ctx);
    ASSERT(ctx->branch_count == 0);
    ASSERT(ctx->expanded_directory != NULL);
    ASSERT(strcmp(ctx->expanded_directory, "/expanded/") == 0);
    mock_completion_reset();
}

TEST(brace_at_end_of_word_no_path_prefix_no_branches) {
    /* cat {a,b}|  — brace at end of word with no '/' after; no path
     * prefix to resolve. The TAB-alone-at-end-of-expansion handling
     * happens at the engine layer, not the analyzer. */
    mock_completion_reset();
    current_executor = (executor_t *)1;
    static const char *branches[] = {"a", "b"};
    mock_brace_branches = branches;
    mock_brace_branch_count = 2;
    const char *buf = "cat {a,b}";
    ANALYZE(buf, strlen(buf), ctx);
    /* No '/' in word → no path-prefix → resolve helper does not run. */
    ASSERT(ctx->branch_count == 0);
    ASSERT(ctx->expanded_directory == NULL);
    mock_completion_reset();
}

/* ============================================================================
 * Test runner
 * ============================================================================ */

int main(void) {
    printf("=== LLE Word-Context Analyzer Foundation Tests ===\n");

    /* Quote-state and word-boundary cases */
    RUN_TEST(empty_buffer);
    RUN_TEST(plain_word_at_command_position);
    RUN_TEST(plain_word_as_argument);
    RUN_TEST(double_quote_open_word);
    RUN_TEST(single_quote_open_word);
    RUN_TEST(backtick_open_word);
    RUN_TEST(escape_pending_at_cursor);
    RUN_TEST(backslash_escaped_space_in_word);
    RUN_TEST(closed_quote_then_more_text);
    RUN_TEST(double_quote_with_internal_backslash_escape);

    /* filename_portion_start */
    RUN_TEST(filename_portion_after_slash_unquoted);
    RUN_TEST(filename_portion_inside_double_quote);
    RUN_TEST(no_slash_in_word_filename_portion_at_word_start);
    RUN_TEST(no_slash_in_quoted_word_filename_portion_skips_open_quote);

    /* context_type */
    RUN_TEST(command_position_at_buffer_start);
    RUN_TEST(command_position_after_pipe);
    RUN_TEST(command_position_after_semicolon);
    RUN_TEST(redirect_target_after_gt);
    RUN_TEST(redirect_target_after_lt);
    RUN_TEST(argument_after_command);
    RUN_TEST(arg_index_increments_with_args);
    RUN_TEST(command_name_extraction);

    /* expansion_kind */
    RUN_TEST(expansion_kind_variable_name);
    RUN_TEST(expansion_kind_braced_variable_name);
    RUN_TEST(expansion_kind_command_subst_open);
    RUN_TEST(expansion_kind_arithmetic_open);
    RUN_TEST(expansion_kind_brace_list_with_comma);
    RUN_TEST(expansion_kind_glob_in_word);
    RUN_TEST(expansion_kind_none_after_complete_var);
    RUN_TEST(expansion_kind_variable_name_at_end_of_word);

    /* NFC */
    RUN_TEST(nfc_normalization_handles_decomposed_input);

    /* Multiline */
    RUN_TEST(multiline_buffer_command_position_after_newline);
    RUN_TEST(multiline_buffer_newline_inside_double_quote_is_literal);

    /* Edge cases */
    RUN_TEST(cursor_past_buffer_end_clamps);
    RUN_TEST(invalid_inputs_return_error);
    RUN_TEST(cursor_at_whitespace_yields_empty_word);
    RUN_TEST(free_handles_null);

    /* Bug-relevant scenarios */
    RUN_TEST(cursor_in_second_arg_after_first_quoted_arg);
    RUN_TEST(quoted_arg_after_pipe_is_argument_not_command_position);
    RUN_TEST(unquoted_pipe_inside_quoted_string_is_literal);

    /* Expansion resolution */
    RUN_TEST(resolve_path_prefix_with_tilde);
    RUN_TEST(resolve_path_prefix_with_variable);
    RUN_TEST(resolve_path_prefix_absolute_path);
    RUN_TEST(no_path_prefix_leaves_expanded_directory_null);
    RUN_TEST(no_executor_leaves_expanded_directory_null);
    RUN_TEST(brace_in_path_prefix_populates_branches);
    RUN_TEST(brace_with_no_comma_does_not_populate_branches);
    RUN_TEST(brace_at_end_of_word_no_path_prefix_no_branches);

    return TEST_RESULT();
}
