/**
 * @file test_fc.c
 * @brief Unit tests for the fc (fix command) builtin
 *
 * Drives the real fc implementation (fc_internal.h) rather than
 * reimplementations: substitution-pattern parsing, range resolution and
 * list formatting run against a real seeded history core, the default
 * editor resolves through the real environment precedence, and bin_fc's
 * no-history contract is asserted directly.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "fc_internal.h"
#include "lle/history.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The framework's ASSERT(cond) is 1-arg; map the 2-arg form used below to
 * ASSERT_TRUE(cond, msg). */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Helpers: real history core + stdout capture
 * ============================================================================
 */

/// Create a real history core seeded with the given commands (oldest first).
static lle_history_core_t *seed_history(const char *const *commands,
                                        size_t count) {
    lle_history_core_t *core = NULL;
    if (lle_history_core_create(&core, NULL, NULL) != LLE_SUCCESS || !core) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        uint64_t id = 0;
        if (lle_history_add_entry(core, commands[i], 0, &id) != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            return NULL;
        }
    }
    return core;
}

/// Run fc_list against history/opts and return its stdout as a malloc'd
/// string (caller frees), capturing the real listing output.
static char *capture_fc_list(lle_history_core_t *history, fc_options_t *opts) {
    char tmpl[] = "/tmp/fc_list_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return NULL;
    }

    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(fd, STDOUT_FILENO);

    fc_list(history, opts);

    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    close(fd);

    FILE *f = fopen(tmpl, "r");
    unlink(tmpl);
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (buf) {
        size_t got = fread(buf, 1, (size_t)len, f);
        buf[got] = '\0';
    }
    fclose(f);
    return buf;
}

/* ============================================================================
 * Substitution pattern parsing (real fc_parse_substitution_pattern)
 * ============================================================================
 */

TEST(substitution_simple) {
    char *old = NULL, *new_str = NULL;
    ASSERT_TRUE(fc_parse_substitution_pattern("foo=bar", &old, &new_str),
                "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old should be 'foo'");
    ASSERT_STR_EQ(new_str, "bar", "New should be 'bar'");
    free(old);
    free(new_str);
}

TEST(substitution_empty_new) {
    char *old = NULL, *new_str = NULL;
    ASSERT_TRUE(fc_parse_substitution_pattern("foo=", &old, &new_str),
                "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old should be 'foo'");
    ASSERT_STR_EQ(new_str, "", "New should be empty");
    free(old);
    free(new_str);
}

TEST(substitution_no_equals) {
    char *old = NULL, *new_str = NULL;
    ASSERT_TRUE(fc_parse_substitution_pattern("foo", &old, &new_str),
                "Parse should succeed");
    ASSERT_STR_EQ(old, "foo", "Old is whole pattern when no '='");
    ASSERT_STR_EQ(new_str, "", "New should be empty (no equals)");
    free(old);
    free(new_str);
}

TEST(substitution_special_chars) {
    char *old = NULL, *new_str = NULL;
    ASSERT_TRUE(fc_parse_substitution_pattern("a/b=c/d", &old, &new_str),
                "Parse should succeed");
    ASSERT_STR_EQ(old, "a/b", "Old should be 'a/b'");
    ASSERT_STR_EQ(new_str, "c/d", "New should be 'c/d'");
    free(old);
    free(new_str);
}

TEST(substitution_multiple_equals) {
    char *old = NULL, *new_str = NULL;
    /// Only the first '=' separates; the rest belongs to the replacement.
    ASSERT_TRUE(fc_parse_substitution_pattern("a=b=c", &old, &new_str),
                "Parse should succeed");
    ASSERT_STR_EQ(old, "a", "Old should be 'a'");
    ASSERT_STR_EQ(new_str, "b=c", "New should be 'b=c'");
    free(old);
    free(new_str);
}

TEST(substitution_null_inputs) {
    char *old = NULL, *new_str = NULL;
    ASSERT_FALSE(fc_parse_substitution_pattern(NULL, &old, &new_str),
                 "NULL pattern should fail");
    ASSERT_FALSE(fc_parse_substitution_pattern("foo=bar", NULL, &new_str),
                 "NULL old should fail");
    ASSERT_FALSE(fc_parse_substitution_pattern("foo=bar", &old, NULL),
                 "NULL new_str should fail");
}

/* ============================================================================
 * Default editor resolution (real fc_get_default_editor)
 * ============================================================================
 */

TEST(editor_fcedit_takes_precedence) {
    setenv("FCEDIT", "/usr/bin/vim", 1);
    setenv("EDITOR", "/usr/bin/nano", 1);
    setenv("VISUAL", "/usr/bin/emacs", 1);
    char *ed = fc_get_default_editor();
    ASSERT_NOT_NULL(ed, "Editor should resolve");
    ASSERT_STR_EQ(ed, "/usr/bin/vim", "FCEDIT outranks EDITOR and VISUAL");
    free(ed);
}

TEST(editor_editor_fallback) {
    unsetenv("FCEDIT");
    setenv("EDITOR", "/usr/bin/nano", 1);
    setenv("VISUAL", "/usr/bin/emacs", 1);
    char *ed = fc_get_default_editor();
    ASSERT_NOT_NULL(ed, "Editor should resolve");
    ASSERT_STR_EQ(ed, "/usr/bin/nano", "EDITOR used when FCEDIT unset");
    free(ed);
}

TEST(editor_visual_fallback) {
    unsetenv("FCEDIT");
    unsetenv("EDITOR");
    setenv("VISUAL", "/usr/bin/emacs", 1);
    char *ed = fc_get_default_editor();
    ASSERT_NOT_NULL(ed, "Editor should resolve");
    ASSERT_STR_EQ(ed, "/usr/bin/emacs", "VISUAL used when FCEDIT/EDITOR unset");
    free(ed);
}

TEST(editor_posix_default) {
    unsetenv("FCEDIT");
    unsetenv("EDITOR");
    unsetenv("VISUAL");
    char *ed = fc_get_default_editor();
    ASSERT_NOT_NULL(ed, "Editor should resolve");
    ASSERT_STR_EQ(ed, "ed", "POSIX default is ed");
    free(ed);
}

/* ============================================================================
 * Range resolution (real fc_parse_range against a seeded history)
 * ============================================================================
 */

static const char *const RANGE_CMDS[] = {"echo one", "echo two", "echo three",
                                         "ls -la", "grep foo"};
#define RANGE_COUNT (sizeof(RANGE_CMDS) / sizeof(RANGE_CMDS[0]))

TEST(range_positive_is_one_based) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    /// "3" is the 3rd command (1-based) -> 0-based index 2.
    ASSERT_TRUE(fc_parse_range(h, "3", NULL, &opts), "Range 3 resolves");
    ASSERT_EQ(opts.first, 2, "first is 0-based index 2");
    ASSERT_EQ(opts.last, 2, "single edit spec covers one command");
    lle_history_core_destroy(h);
}

TEST(range_negative_offset) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    /// "-1" is the most recent command -> last index (count-1 == 4).
    ASSERT_TRUE(fc_parse_range(h, "-1", NULL, &opts), "Range -1 resolves");
    ASSERT_EQ(opts.first, 4, "-1 is the newest entry");
    lle_history_core_destroy(h);
}

TEST(range_prefix_search) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    /// "grep" matches the most recent command starting with that prefix.
    ASSERT_TRUE(fc_parse_range(h, "grep", NULL, &opts), "Prefix resolves");
    ASSERT_EQ(opts.first, 4, "grep foo is index 4");
    lle_history_core_destroy(h);
}

TEST(range_out_of_bounds_rejected) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    ASSERT_FALSE(fc_parse_range(h, "99", NULL, &opts),
                 "Positive beyond history is rejected");
    ASSERT_FALSE(fc_parse_range(h, "-99", NULL, &opts),
                 "Negative beyond history is rejected");
    lle_history_core_destroy(h);
}

TEST(range_list_default_spans_history) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    opts.list_mode = true;
    /// No spec in list mode: from the start (<=16 entries) to the end.
    ASSERT_TRUE(fc_parse_range(h, NULL, NULL, &opts), "List default resolves");
    ASSERT_EQ(opts.first, 0, "list default starts at oldest");
    ASSERT_EQ(opts.last, 4, "list default ends at newest");
    lle_history_core_destroy(h);
}

TEST(range_reorders_when_first_after_last) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    /// first "4" (idx 3) > last "2" (idx 1): fc swaps so first <= last.
    ASSERT_TRUE(fc_parse_range(h, "4", "2", &opts), "Range resolves");
    ASSERT_EQ(opts.first, 1, "first becomes the lower index");
    ASSERT_EQ(opts.last, 3, "last becomes the higher index");
    lle_history_core_destroy(h);
}

/* ============================================================================
 * List output (real fc_list, captured stdout)
 * ============================================================================
 */

TEST(list_numbered_output) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    opts.first = 0;
    opts.last = 4;
    opts.range_valid = true;
    char *out = capture_fc_list(h, &opts);
    ASSERT_NOT_NULL(out, "Capture should succeed");
    ASSERT_STR_EQ(out,
                  "    1  echo one\n"
                  "    2  echo two\n"
                  "    3  echo three\n"
                  "    4  ls -la\n"
                  "    5  grep foo\n",
                  "Numbered listing matches fc -l format");
    free(out);
    lle_history_core_destroy(h);
}

TEST(list_suppressed_numbers) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    opts.first = 0;
    opts.last = 2;
    opts.range_valid = true;
    opts.suppress_numbers = true;
    char *out = capture_fc_list(h, &opts);
    ASSERT_NOT_NULL(out, "Capture should succeed");
    ASSERT_STR_EQ(out, "echo one\necho two\necho three\n",
                  "fc -ln omits the leading numbers");
    free(out);
    lle_history_core_destroy(h);
}

TEST(list_reverse_order) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    opts.first = 2;
    opts.last = 4;
    opts.range_valid = true;
    opts.reverse_order = true;
    char *out = capture_fc_list(h, &opts);
    ASSERT_NOT_NULL(out, "Capture should succeed");
    ASSERT_STR_EQ(out,
                  "    5  grep foo\n"
                  "    4  ls -la\n"
                  "    3  echo three\n",
                  "fc -lr lists newest first");
    free(out);
    lle_history_core_destroy(h);
}

TEST(list_single_entry) {
    lle_history_core_t *h = seed_history(RANGE_CMDS, RANGE_COUNT);
    ASSERT_NOT_NULL(h, "History should seed");
    fc_options_t opts = {0};
    opts.first = opts.last = 2;
    opts.range_valid = true;
    char *out = capture_fc_list(h, &opts);
    ASSERT_NOT_NULL(out, "Capture should succeed");
    ASSERT_STR_EQ(out, "    3  echo three\n", "Single-entry listing");
    free(out);
    lle_history_core_destroy(h);
}

/* ============================================================================
 * bin_fc dispatcher contract
 * ============================================================================
 */

TEST(bin_fc_no_history_returns_error) {
    /// The unit binary has no global editor, so get_lle_history() is NULL
    /// and bin_fc reports "history system not available" and returns 1.
    /// This is the only bin_fc path reachable without an interactive editor;
    /// the listing/range/substitution logic is covered above against a real
    /// seeded core.
    char *argv[] = {"fc", "-l", NULL};
    ASSERT_EQ(bin_fc(2, argv), 1, "fc with no history returns 1");
}

/* ============================================================================
 * Test runner
 * ============================================================================
 */

int main(void) {
    printf("\n=== FC Builtin Unit Tests ===\n\n");

    printf("Substitution Pattern Parsing:\n");
    RUN_TEST(substitution_simple);
    RUN_TEST(substitution_empty_new);
    RUN_TEST(substitution_no_equals);
    RUN_TEST(substitution_special_chars);
    RUN_TEST(substitution_multiple_equals);
    RUN_TEST(substitution_null_inputs);

    printf("\nDefault Editor Resolution:\n");
    RUN_TEST(editor_fcedit_takes_precedence);
    RUN_TEST(editor_editor_fallback);
    RUN_TEST(editor_visual_fallback);
    RUN_TEST(editor_posix_default);

    printf("\nRange Resolution:\n");
    RUN_TEST(range_positive_is_one_based);
    RUN_TEST(range_negative_offset);
    RUN_TEST(range_prefix_search);
    RUN_TEST(range_out_of_bounds_rejected);
    RUN_TEST(range_list_default_spans_history);
    RUN_TEST(range_reorders_when_first_after_last);

    printf("\nList Output:\n");
    RUN_TEST(list_numbered_output);
    RUN_TEST(list_suppressed_numbers);
    RUN_TEST(list_reverse_order);
    RUN_TEST(list_single_entry);

    printf("\nbin_fc Dispatcher:\n");
    RUN_TEST(bin_fc_no_history_returns_error);

    return TEST_RESULT();
}
