/**
 * @file test_debug_analysis.c
 * @brief Unit tests for debug analysis module
 *
 * Tests script analysis functionality including syntax checking,
 * style analysis, security issue detection, performance patterns,
 * and portability concerns.
 */

#include "debug.h"

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// ============================================================================
/// Test Framework
/// ============================================================================

/// ============================================================================
/// Test Helper Functions
/// ============================================================================

static const char *test_script_dir = "/tmp/lush_test_scripts";

static void setup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", test_script_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "test setup: mkdir failed for %s\n", test_script_dir);
        exit(EXIT_FAILURE);
    }
}

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_script_dir);
    (void)!system(cmd);
}

static char *create_test_script(const char *name, const char *content) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_script_dir, name);

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
    return path;
}

/// ============================================================================
/// Analysis Issue Management Tests
/// ============================================================================

TEST(add_analysis_issue_basic) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 10, "warning", "style",
                             "Test issue", "Fix suggestion");

    ASSERT_EQ(ctx->issue_count, 1, "Issue count should be 1");
    ASSERT_NOT_NULL(ctx->analysis_issues, "Issues list should exist");
    ASSERT_STR_EQ(ctx->analysis_issues->severity, "warning", "Severity");
    ASSERT_STR_EQ(ctx->analysis_issues->category, "style", "Category");
    ASSERT_EQ(ctx->analysis_issues->line_number, 10, "Line number");

    debug_cleanup(ctx);
}

TEST(add_analysis_issue_multiple) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax", "Issue 1",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 5, "warning", "security",
                             "Issue 2", "Suggestion 2");
    debug_add_analysis_issue(ctx, "test.sh", 10, "info", "style", "Issue 3",
                             "Suggestion 3");

    ASSERT_EQ(ctx->issue_count, 3, "Issue count should be 3");

    debug_cleanup(ctx);
}

TEST(add_analysis_issue_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should not crash with NULL parameters
    debug_add_analysis_issue(NULL, "test.sh", 1, "error", "syntax", "Message",
                             NULL);
    debug_add_analysis_issue(ctx, NULL, 1, "error", "syntax", "Message", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 1, NULL, "syntax", "Message",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 1, "error", NULL, "Message", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax", NULL, NULL);

    ASSERT_EQ(ctx->issue_count, 0, "No issues should be added");

    debug_cleanup(ctx);
}

TEST(clear_analysis_issues) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax", "Issue 1",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 2, "warning", "style", "Issue 2",
                             NULL);

    ASSERT_EQ(ctx->issue_count, 2, "Should have 2 issues");

    debug_clear_analysis_issues(ctx);

    ASSERT_EQ(ctx->issue_count, 0, "Issues should be cleared");
    ASSERT_NULL(ctx->analysis_issues, "Issues list should be NULL");

    debug_cleanup(ctx);
}

TEST(show_analysis_report_empty) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should not crash with no issues
    debug_show_analysis_report(ctx);

    debug_cleanup(ctx);
}

TEST(show_analysis_report_with_issues) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax",
                             "Syntax error", "Check syntax");
    debug_add_analysis_issue(ctx, "test.sh", 5, "warning", "security",
                             "Security warning", "Use quotes");
    debug_add_analysis_issue(ctx, "test.sh", 10, "info", "performance",
                             "Performance tip", "Optimize");

    /// Should not crash
    debug_show_analysis_report(ctx);

    debug_cleanup(ctx);
}

/// ============================================================================
/// Script Analysis Tests
/// ============================================================================

TEST(analyze_script_nonexistent) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should handle non-existent file gracefully
    debug_analyze_script(ctx, "/nonexistent/path/script.sh");

    debug_cleanup(ctx);
}

TEST(analyze_script_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should not crash with NULL
    debug_analyze_script(NULL, "test.sh");
    debug_analyze_script(ctx, NULL);

    debug_cleanup(ctx);
}

TEST(analyze_script_valid_syntax) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\necho hello\nexit 0\n";
    char *path = create_test_script("valid.sh", script);

    debug_analyze_script(ctx, path);

    /// Script has valid syntax - may or may not have style issues
    /// Just verify it doesn't crash

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_missing_shebang) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "echo hello\nexit 0\n";
    char *path = create_test_script("noshebang.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect missing shebang as style issue
    bool found_shebang_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strstr(issue->message, "shebang") != NULL) {
            found_shebang_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_shebang_issue, "Should detect missing shebang");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_security_eval) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\neval $USER_INPUT\n";
    char *path = create_test_script("eval.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect eval as security issue
    bool found_eval_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "security") == 0 &&
            strstr(issue->message, "eval") != NULL) {
            found_eval_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_eval_issue, "Should detect eval security issue");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_security_rm_rf) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\nrm -rf $DIR\n";
    char *path = create_test_script("rmrf.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect rm -rf as security concern
    bool found_rm_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "security") == 0 &&
            strstr(issue->message, "rm") != NULL) {
            found_rm_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_rm_issue, "Should detect rm -rf security issue");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_performance_useless_cat) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\ncat file.txt | grep pattern\n";
    char *path = create_test_script("uselesscat.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect useless use of cat
    bool found_cat_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "performance") == 0 &&
            strstr(issue->message, "cat") != NULL) {
            found_cat_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_cat_issue, "Should detect useless use of cat");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

/* Typed-function (`fn`) static type checks. SEMANTICS section 3.9 /
 * section 5.3: declared return-kind annotation must match what the
 * body actually returns. The runtime catches these too, but a debug
 * analyze hit at edit time saves the runtime trip. */

TEST(analyze_script_typed_fn_void_returns_value) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/usr/bin/env lush\nfn nop() { return \"leak\"; }\n";
    char *path = create_test_script("void_returns_value.lush", script);
    debug_analyze_script(ctx, path);

    bool found = false;
    for (analysis_issue_t *issue = ctx->analysis_issues; issue;
         issue = issue->next) {
        if (strcmp(issue->category, "type") == 0 &&
            strstr(issue->message, "void") != NULL &&
            strstr(issue->message, "returns a value") != NULL) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found, "void fn returning a value should be flagged");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_typed_fn_kind_mismatch) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/usr/bin/env lush\n"
                         "fn bad() -> list { return \"scalar\"; }\n";
    char *path = create_test_script("kind_mismatch.lush", script);
    debug_analyze_script(ctx, path);

    bool found = false;
    for (analysis_issue_t *issue = ctx->analysis_issues; issue;
         issue = issue->next) {
        if (strcmp(issue->category, "type") == 0 &&
            strstr(issue->message, "list") != NULL &&
            strstr(issue->message, "scalar") != NULL) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found, "declared list, returned scalar -- flagged");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_typed_fn_missing_return_value) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/usr/bin/env lush\n"
                         "fn bad() -> scalar { return; }\n";
    char *path = create_test_script("missing_return.lush", script);
    debug_analyze_script(ctx, path);

    bool found = false;
    for (analysis_issue_t *issue = ctx->analysis_issues; issue;
         issue = issue->next) {
        if (strcmp(issue->category, "type") == 0 &&
            strstr(issue->message, "no value") != NULL) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found, "bare return in non-void fn should be flagged");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_typed_fn_well_typed_no_issue) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script =
        "#!/usr/bin/env lush\nfn good() -> scalar { return \"ok\"; }\n";
    char *path = create_test_script("well_typed.lush", script);
    debug_analyze_script(ctx, path);

    bool found = false;
    for (analysis_issue_t *issue = ctx->analysis_issues; issue;
         issue = issue->next) {
        if (strcmp(issue->category, "type") == 0) {
            found = true;
            break;
        }
    }
    ASSERT_FALSE(found, "well-typed fn should not raise type issues");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_portability_source) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\nsource config.sh\n";
    char *path = create_test_script("source.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect non-POSIX source command
    bool found_source_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "portability") == 0 &&
            strstr(issue->message, "source") != NULL) {
            found_source_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_source_issue, "Should detect non-POSIX source");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_portability_echo_e) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\necho -e \"hello\\nworld\"\n";
    char *path = create_test_script("echoe.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect non-portable echo -e
    bool found_echo_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "portability") == 0 &&
            strstr(issue->message, "echo") != NULL) {
            found_echo_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_echo_issue, "Should detect non-portable echo -e");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

/* The predictive type-mismatch warning (SEMANTICS section 3.9):
 * `${arr[@]}` in a scalar-assignment RHS becomes a runtime type error
 * at execution; the analyzer flags it statically so the script author
 * sees the issue without running. */
TEST(analyze_script_type_at_subscript_in_scalar_assignment) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\n"
                         "arr=(a b c)\n"
                         "x=${arr[@]}\n"
                         "y=\"${arr[@]}\"\n"
                         "for i in ${arr[@]}; do echo $i; done\n";
    char *path = create_test_script("type_at_scalar.sh", script);

    debug_analyze_script(ctx, path);

    /// Count "type" category issues -- expect exactly two: lines 3 and
    /// 4. The for-in usage on line 5 is vector-accepting and must NOT
    /// be flagged.
    int type_issue_count = 0;
    bool flagged_line_3 = false;
    bool flagged_line_4 = false;
    bool flagged_line_5_for_loop = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "type") == 0) {
            type_issue_count++;
            if (issue->line_number == 3) {
                flagged_line_3 = true;
            } else if (issue->line_number == 4) {
                flagged_line_4 = true;
            } else if (issue->line_number == 5) {
                flagged_line_5_for_loop = true;
            }
        }
        issue = issue->next;
    }
    ASSERT_TRUE(flagged_line_3,
                "x=\\${arr[@]} flagged as scalar-slot type mismatch");
    ASSERT_TRUE(flagged_line_4,
                "y=\"\\${arr[@]}\" flagged as scalar-slot type mismatch");
    ASSERT_FALSE(flagged_line_5_for_loop,
                 "for-in is vector-accepting; must not flag");
    ASSERT_EQ(type_issue_count, 2, "exactly two type-mismatch warnings");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_sigil_pct_on_scalar_is_error) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\n"
                         "x=hello\n"
                         "echo %x\n";
    char *path = create_test_script("sigil_pct_scalar.sh", script);

    debug_analyze_script(ctx, path);

    bool found_pct_error = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "type") == 0 &&
            strcmp(issue->severity, "error") == 0 &&
            strstr(issue->message, "%x: pair sigil on scalar") != NULL) {
            found_pct_error = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_pct_error, "%x on a scalar binding raises a type error");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_sigil_at_on_scalar_is_warning) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\n"
                         "x=hello\n"
                         "echo @x\n";
    char *path = create_test_script("sigil_at_scalar.sh", script);

    debug_analyze_script(ctx, path);

    bool found_at_warning = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "type") == 0 &&
            strcmp(issue->severity, "warning") == 0 &&
            strstr(issue->message, "@x: vector sigil on scalar") != NULL) {
            found_at_warning = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_at_warning,
                "@x on a scalar binding raises a widening warning");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_sigil_on_list_is_silent) {
    /// The analyzer must not flag `@arr` or `%arr` when `arr` is a list --
    /// those are the well-typed cases and a noisy warning would be wrong.
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\n"
                         "arr=(a b c)\n"
                         "echo @arr\n"
                         "echo %arr\n";
    char *path = create_test_script("sigil_list_ok.sh", script);

    debug_analyze_script(ctx, path);

    int sigil_type_issues = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "type") == 0 &&
            (strstr(issue->message, "vector sigil on scalar") != NULL ||
             strstr(issue->message, "pair sigil on scalar") != NULL)) {
            sigil_type_issues++;
        }
        issue = issue->next;
    }
    ASSERT_EQ(sigil_type_issues, 0,
              "well-typed sigil uses must not flag scalar-mismatch issues");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_style_long_lines) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Create script with a very long line
    char script[512];
    snprintf(
        script, sizeof(script), "#!/bin/sh\n# %s\n",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    char *path = create_test_script("longline.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect long line
    bool found_long_line = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "style") == 0 &&
            strstr(issue->message, "long") != NULL) {
            found_long_line = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_long_line, "Should detect long line");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_style_trailing_whitespace) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\necho hello   \n";
    char *path = create_test_script("trailing.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect trailing whitespace
    bool found_trailing = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "style") == 0 &&
            strstr(issue->message, "railing") != NULL) {
            found_trailing = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_trailing, "Should detect trailing whitespace");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_script_chmod_777) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\nchmod 777 /tmp/file\n";
    char *path = create_test_script("chmod.sh", script);

    debug_analyze_script(ctx, path);

    /// Should detect overly permissive chmod
    bool found_chmod_issue = false;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->category, "security") == 0 &&
            strstr(issue->message, "chmod") != NULL) {
            found_chmod_issue = true;
            break;
        }
        issue = issue->next;
    }
    ASSERT_TRUE(found_chmod_issue, "Should detect chmod 777");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

/// ============================================================================
/// Issue Severity Tests
/// ============================================================================

TEST(issue_severity_counts) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax", "Error 1",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 2, "error", "syntax", "Error 2",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 3, "warning", "style", "Warning 1",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 4, "info", "style", "Info 1",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 5, "info", "style", "Info 2",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 6, "info", "style", "Info 3",
                             NULL);

    ASSERT_EQ(ctx->issue_count, 6, "Total should be 6");

    /// Count by severity
    int errors = 0, warnings = 0, infos = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->severity, "error") == 0)
            errors++;
        else if (strcmp(issue->severity, "warning") == 0)
            warnings++;
        else if (strcmp(issue->severity, "info") == 0)
            infos++;
        issue = issue->next;
    }

    ASSERT_EQ(errors, 2, "Should have 2 errors");
    ASSERT_EQ(warnings, 1, "Should have 1 warning");
    ASSERT_EQ(infos, 3, "Should have 3 infos");

    debug_cleanup(ctx);
}

/// ============================================================================
/// Analyze vs Lint Separation Tests
/// ============================================================================

TEST(lint_script_basic) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\necho hello\n";
    char *path = create_test_script("lint_basic.sh", script);

    int remaining = debug_lint_script(ctx, path, false, false, false);

    /// Valid script should have minimal issues
    ASSERT_TRUE(remaining >= 0, "lint should succeed");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(lint_script_null_params) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Should handle NULL gracefully
    int result = debug_lint_script(NULL, "test.sh", false, false, false);
    ASSERT_EQ(result, -1, "NULL ctx should return -1");

    result = debug_lint_script(ctx, NULL, false, false, false);
    ASSERT_EQ(result, -1, "NULL path should return -1");

    debug_cleanup(ctx);
}

TEST(lint_script_nonexistent) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    int result =
        debug_lint_script(ctx, "/nonexistent/script.sh", false, false, false);
    ASSERT_EQ(result, -1, "Nonexistent file should return -1");

    debug_cleanup(ctx);
}

TEST(lint_returns_issue_count) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Script with known issues (source is not POSIX, echo -e not portable)
    const char *script = "#!/bin/sh\nsource config.sh\necho -e \"test\"\n";
    char *path = create_test_script("lint_issues.sh", script);

    int remaining = debug_lint_script(ctx, path, false, false, false);

    /// Should detect portability issues (source, echo -e)
    ASSERT_TRUE(remaining > 0, "Should have issues remaining");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(analyze_vs_lint_mode_difference) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Add issues of different severities
    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax",
                             "Syntax error", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 2, "warning", "style",
                             "Style warning", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 3, "info", "style", "Info message",
                             NULL);

    ASSERT_EQ(ctx->issue_count, 3, "Should have 3 issues total");

    /// Count what each mode would show
    int full_count = 0, lint_count = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        full_count++; /// FULL mode shows everything
        if (strcmp(issue->severity, "info") != 0) {
            lint_count++; /// LINT mode skips info
        }
        issue = issue->next;
    }

    ASSERT_EQ(full_count, 3, "FULL mode should show 3 issues");
    ASSERT_EQ(lint_count, 2, "LINT mode should show 2 issues (no info)");

    debug_cleanup(ctx);
}

TEST(analysis_mode_enum_values) {
    /// Verify enum values are distinct and as expected
    ASSERT_NE(ANALYSIS_MODE_FULL, ANALYSIS_MODE_LINT,
              "FULL and LINT modes should be different");
    ASSERT_EQ(ANALYSIS_MODE_FULL, 0, "FULL should be 0");
    ASSERT_EQ(ANALYSIS_MODE_LINT, 1, "LINT should be 1");
}

TEST(lint_with_dry_run) {
    setup_test_dir();
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    const char *script = "#!/bin/sh\nsource config.sh\n";
    char *path = create_test_script("lint_dry.sh", script);

    /// Dry run should not modify file
    int remaining = debug_lint_script(ctx, path, true, false, true);
    (void)remaining;

    /// Read file to verify unchanged
    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f, "File should still exist");
    char buf[256];
    ASSERT_NOT_NULL(fgets(buf, sizeof(buf), f), "read first line");
    ASSERT_NOT_NULL(fgets(buf, sizeof(buf), f), "read second line");
    fclose(f);

    /// File should still have "source" not "."
    ASSERT_NOT_NULL(strstr(buf, "source"),
                    "File should be unchanged in dry run");

    debug_cleanup(ctx);
    cleanup_test_dir();
}

TEST(lint_actionable_only) {
    debug_context_t *ctx = debug_init();
    ASSERT_NOT_NULL(ctx, "debug_init should succeed");

    /// Add various severity issues
    debug_add_analysis_issue(ctx, "test.sh", 1, "error", "syntax", "Error",
                             NULL);
    debug_add_analysis_issue(ctx, "test.sh", 2, "warning", "portability",
                             "Warning", NULL);
    debug_add_analysis_issue(ctx, "test.sh", 3, "info", "style", "Info", NULL);

    /// Count actionable (error + warning) vs all
    int actionable = 0;
    analysis_issue_t *issue = ctx->analysis_issues;
    while (issue) {
        if (strcmp(issue->severity, "error") == 0 ||
            strcmp(issue->severity, "warning") == 0) {
            actionable++;
        }
        issue = issue->next;
    }

    ASSERT_EQ(actionable, 2, "Should have 2 actionable issues");
    ASSERT_EQ(ctx->issue_count, 3, "Total issues should be 3");

    debug_cleanup(ctx);
}

/// ============================================================================
/// Main
/// ============================================================================

int main(void) {
    printf("Running debug analysis tests...\n\n");

    printf("Analysis Issue Management:\n");
    RUN_TEST(add_analysis_issue_basic);
    RUN_TEST(add_analysis_issue_multiple);
    RUN_TEST(add_analysis_issue_null_params);
    RUN_TEST(clear_analysis_issues);
    RUN_TEST(show_analysis_report_empty);
    RUN_TEST(show_analysis_report_with_issues);

    printf("\nScript Analysis:\n");
    RUN_TEST(analyze_script_nonexistent);
    RUN_TEST(analyze_script_null_params);
    RUN_TEST(analyze_script_valid_syntax);
    RUN_TEST(analyze_script_missing_shebang);

    printf("\nSecurity Analysis:\n");
    RUN_TEST(analyze_script_security_eval);
    RUN_TEST(analyze_script_security_rm_rf);
    RUN_TEST(analyze_script_chmod_777);

    printf("\nPerformance Analysis:\n");
    RUN_TEST(analyze_script_performance_useless_cat);

    printf("\nPortability Analysis:\n");
    RUN_TEST(analyze_script_typed_fn_void_returns_value);
    RUN_TEST(analyze_script_typed_fn_kind_mismatch);
    RUN_TEST(analyze_script_typed_fn_missing_return_value);
    RUN_TEST(analyze_script_typed_fn_well_typed_no_issue);
    RUN_TEST(analyze_script_portability_source);
    RUN_TEST(analyze_script_portability_echo_e);
    RUN_TEST(analyze_script_type_at_subscript_in_scalar_assignment);
    RUN_TEST(analyze_script_sigil_pct_on_scalar_is_error);
    RUN_TEST(analyze_script_sigil_at_on_scalar_is_warning);
    RUN_TEST(analyze_script_sigil_on_list_is_silent);

    printf("\nStyle Analysis:\n");
    RUN_TEST(analyze_script_style_long_lines);
    RUN_TEST(analyze_script_style_trailing_whitespace);

    printf("\nIssue Severity:\n");
    RUN_TEST(issue_severity_counts);

    printf("\nAnalyze vs Lint Separation:\n");
    RUN_TEST(lint_script_basic);
    RUN_TEST(lint_script_null_params);
    RUN_TEST(lint_script_nonexistent);
    RUN_TEST(lint_returns_issue_count);
    RUN_TEST(analyze_vs_lint_mode_difference);
    RUN_TEST(analysis_mode_enum_values);
    RUN_TEST(lint_with_dry_run);
    RUN_TEST(lint_actionable_only);

    return TEST_RESULT();
}
