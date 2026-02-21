/**
 * @file test_fixer.c
 * @brief Unit tests for the fixer module
 *
 * Tests the auto-fix linter functionality including:
 * - Context lifecycle management
 * - Fix collection and counting
 * - Fix application
 * - Interactive mode components
 * - Diff generation
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "fixer.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * Test Framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        tests_run++;                                                           \
        int _prev_failed = tests_failed;                                       \
        printf("  Running %s...", #name);                                      \
        fflush(stdout);                                                        \
        test_##name();                                                         \
        if (tests_failed == _prev_failed) {                                    \
            printf(" PASSED\n");                                               \
            tests_passed++;                                                    \
        }                                                                      \
    } while (0)

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf(" FAILED: %s\n", msg);                                      \
            tests_failed++;                                                    \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)
#define ASSERT_NULL(ptr, msg) ASSERT((ptr) == NULL, msg)
#define ASSERT_TRUE(val, msg) ASSERT((val) == true, msg)
#define ASSERT_FALSE(val, msg) ASSERT((val) == false, msg)
#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

static const char *test_script_dir = "/tmp/lush_fixer_test";

static void setup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", test_script_dir);
    system(cmd);
}

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_script_dir);
    system(cmd);
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

/* ============================================================================
 * Context Lifecycle Tests
 * ============================================================================ */

TEST(fixer_init_basic) {
    fixer_context_t ctx;
    fixer_result_t result = fixer_init(&ctx);
    
    ASSERT_EQ(result, FIXER_OK, "fixer_init should succeed");
    ASSERT_NOT_NULL(ctx.fixes, "fixes array should be allocated");
    ASSERT_EQ(ctx.count, 0, "count should be 0");
    ASSERT_EQ(ctx.capacity > 0, true, "capacity should be > 0");
    ASSERT_NULL(ctx.script_path, "script_path should be NULL");
    ASSERT_NULL(ctx.content, "content should be NULL");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_init_null) {
    fixer_result_t result = fixer_init(NULL);
    ASSERT_EQ(result, FIXER_ERR_NOMEM, "fixer_init(NULL) should return error");
}

TEST(fixer_cleanup_null) {
    /* Should not crash */
    fixer_cleanup(NULL);
}

TEST(fixer_cleanup_double) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_cleanup(&ctx);
    
    /* Second cleanup should be safe */
    fixer_cleanup(&ctx);
}

/* ============================================================================
 * Load Tests
 * ============================================================================ */

TEST(fixer_load_string_basic) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    const char *script = "#!/bin/sh\necho hello\n";
    fixer_result_t result = fixer_load_string(&ctx, script, "test.sh");
    
    ASSERT_EQ(result, FIXER_OK, "load_string should succeed");
    ASSERT_NOT_NULL(ctx.content, "content should be set");
    ASSERT_STR_EQ(ctx.content, script, "content should match");
    ASSERT_NOT_NULL(ctx.script_path, "script_path should be set");
    ASSERT_STR_EQ(ctx.script_path, "test.sh", "path should match");
    ASSERT_EQ(ctx.content_len, strlen(script), "content_len should match");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_load_string_null_path) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    const char *script = "echo hello\n";
    fixer_result_t result = fixer_load_string(&ctx, script, NULL);
    
    ASSERT_EQ(result, FIXER_OK, "load_string with NULL path should succeed");
    ASSERT_NULL(ctx.script_path, "script_path should be NULL");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_load_string_null_content) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    fixer_result_t result = fixer_load_string(&ctx, NULL, "test.sh");
    ASSERT_EQ(result, FIXER_ERR_IO, "load_string with NULL content should fail");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_load_file_basic) {
    setup_test_dir();
    
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    const char *script = "#!/bin/sh\necho hello\n";
    char *path = create_test_script("basic.sh", script);
    
    fixer_result_t result = fixer_load_file(&ctx, path);
    
    ASSERT_EQ(result, FIXER_OK, "load_file should succeed");
    ASSERT_NOT_NULL(ctx.content, "content should be loaded");
    ASSERT_STR_EQ(ctx.content, script, "content should match");
    
    fixer_cleanup(&ctx);
    cleanup_test_dir();
}

TEST(fixer_load_file_nonexistent) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    fixer_result_t result = fixer_load_file(&ctx, "/nonexistent/path.sh");
    ASSERT_EQ(result, FIXER_ERR_IO, "load_file with nonexistent path should fail");
    
    fixer_cleanup(&ctx);
}

/* ============================================================================
 * Fix Add and Count Tests
 * ============================================================================ */

TEST(fixer_add_fix_basic) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo hello", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.line = 1;
    fix.column = 1;
    fix.match_start = 0;
    fix.match_length = 4;
    fix.original = "echo";
    fix.replacement = "printf";
    fix.type = FIX_TYPE_SAFE;
    fix.message = "Use printf for portability";
    fix.entry = NULL;
    
    fixer_result_t result = fixer_add_fix(&ctx, &fix);
    ASSERT_EQ(result, FIXER_OK, "add_fix should succeed");
    ASSERT_EQ(ctx.count, 1, "count should be 1");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_add_fix_null) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    
    fixer_result_t result = fixer_add_fix(&ctx, NULL);
    ASSERT_EQ(result, FIXER_ERR_NOMEM, "add_fix with NULL fix should fail");
    
    result = fixer_add_fix(NULL, NULL);
    ASSERT_EQ(result, FIXER_ERR_NOMEM, "add_fix with NULL ctx should fail");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_count_safe) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t safe_fix = {0};
    safe_fix.type = FIX_TYPE_SAFE;
    fixer_fix_t unsafe_fix = {0};
    unsafe_fix.type = FIX_TYPE_UNSAFE;
    fixer_fix_t manual_fix = {0};
    manual_fix.type = FIX_TYPE_MANUAL;
    
    fixer_add_fix(&ctx, &safe_fix);
    fixer_add_fix(&ctx, &safe_fix);
    fixer_add_fix(&ctx, &unsafe_fix);
    fixer_add_fix(&ctx, &manual_fix);
    
    ASSERT_EQ(fixer_count_safe(&ctx), 2, "Should have 2 safe fixes");
    ASSERT_EQ(fixer_count_unsafe(&ctx), 1, "Should have 1 unsafe fix");
    ASSERT_EQ(fixer_count_manual(&ctx), 1, "Should have 1 manual fix");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_count_null) {
    ASSERT_EQ(fixer_count_safe(NULL), 0, "count_safe(NULL) should return 0");
    ASSERT_EQ(fixer_count_unsafe(NULL), 0, "count_unsafe(NULL) should return 0");
    ASSERT_EQ(fixer_count_manual(NULL), 0, "count_manual(NULL) should return 0");
}

/* ============================================================================
 * Fix Application Tests
 * ============================================================================ */

TEST(fixer_apply_fixes_no_fixes) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo hello", "test.sh");
    
    fixer_options_t opts = {
        .include_unsafe = false,
        .dry_run = false,
    };
    
    char output[256];
    size_t applied = 0;
    
    fixer_result_t result = fixer_apply_fixes(&ctx, &opts, output, sizeof(output),
                                               &applied);
    
    ASSERT_EQ(result, FIXER_OK, "apply_fixes should succeed");
    ASSERT_EQ(applied, 0, "No fixes should be applied");
    ASSERT_STR_EQ(output, "echo hello", "Output should match original");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_apply_fixes_single) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source config.sh", "test.sh");
    
    fixer_fix_t fix = {0};  /* Zero-initialize all fields first */
    fix.line = 1;
    fix.column = 1;
    fix.match_start = 0;
    fix.match_length = 6;  /* "source" */
    fix.original = "source";
    fix.replacement = ".";
    fix.type = FIX_TYPE_SAFE;
    fix.message = "Use . for POSIX compatibility";
    fix.entry = NULL;
    
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = {
        .include_unsafe = false,
        .dry_run = false,
    };
    
    char output[256];
    size_t applied = 0;
    
    fixer_result_t result = fixer_apply_fixes(&ctx, &opts, output, sizeof(output),
                                               &applied);
    
    ASSERT_EQ(result, FIXER_OK, "apply_fixes should succeed");
    ASSERT_EQ(applied, 1, "One fix should be applied");
    ASSERT_STR_EQ(output, ". config.sh", "Output should have fix applied");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_apply_fixes_skip_unsafe) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo test", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 4;
    fix.replacement = "printf";
    fix.type = FIX_TYPE_UNSAFE;
    
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = {
        .include_unsafe = false,  /* Don't include unsafe */
    };
    
    char output[256];
    size_t applied = 0;
    
    fixer_apply_fixes(&ctx, &opts, output, sizeof(output), &applied);
    
    ASSERT_EQ(applied, 0, "Unsafe fix should be skipped");
    ASSERT_STR_EQ(output, "echo test", "Output should be unchanged");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_apply_fixes_include_unsafe) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo test", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 4;
    fix.replacement = "printf";
    fix.type = FIX_TYPE_UNSAFE;
    
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = {
        .include_unsafe = true,  /* Include unsafe */
    };
    
    char output[256];
    size_t applied = 0;
    
    fixer_apply_fixes(&ctx, &opts, output, sizeof(output), &applied);
    
    ASSERT_EQ(applied, 1, "Unsafe fix should be applied");
    ASSERT_STR_EQ(output, "printf test", "Output should have fix");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_apply_fixes_skip_manual) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "eval $cmd", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 4;
    fix.replacement = NULL;  /* No replacement for manual */
    fix.type = FIX_TYPE_MANUAL;
    
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = {
        .include_unsafe = true,  /* Even with unsafe, manual should be skipped */
    };
    
    char output[256];
    size_t applied = 0;
    
    fixer_apply_fixes(&ctx, &opts, output, sizeof(output), &applied);
    
    ASSERT_EQ(applied, 0, "Manual fix should not be applied");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_apply_fixes_alloc) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source file.sh", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 6;
    fix.replacement = ".";
    fix.type = FIX_TYPE_SAFE;
    
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { .include_unsafe = false };
    
    char *output = NULL;
    size_t applied = 0;
    
    fixer_result_t result = fixer_apply_fixes_alloc(&ctx, &opts, &output, &applied);
    
    ASSERT_EQ(result, FIXER_OK, "apply_fixes_alloc should succeed");
    ASSERT_NOT_NULL(output, "output should be allocated");
    ASSERT_EQ(applied, 1, "One fix should be applied");
    ASSERT_STR_EQ(output, ". file.sh", "Output should be correct");
    
    free(output);
    fixer_cleanup(&ctx);
}

/* ============================================================================
 * Syntax Verification Tests
 * ============================================================================ */

TEST(fixer_verify_syntax_valid) {
    const char *valid = "#!/bin/sh\necho hello\nexit 0\n";
    ASSERT_TRUE(fixer_verify_syntax(valid, SHELL_MODE_POSIX),
                "Valid script should pass verification");
}

TEST(fixer_verify_syntax_null) {
    ASSERT_FALSE(fixer_verify_syntax(NULL, SHELL_MODE_POSIX),
                 "NULL content should fail verification");
}

/* ============================================================================
 * File Write Tests
 * ============================================================================ */

TEST(fixer_write_file_basic) {
    setup_test_dir();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/write_test.sh", test_script_dir);
    
    const char *content = "#!/bin/sh\necho hello\n";
    fixer_result_t result = fixer_write_file(path, content, false);
    
    ASSERT_EQ(result, FIXER_OK, "write_file should succeed");
    
    /* Verify content was written */
    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f, "File should exist");
    
    char buf[256];
    size_t read = fread(buf, 1, sizeof(buf) - 1, f);
    buf[read] = '\0';
    fclose(f);
    
    ASSERT_STR_EQ(buf, content, "Written content should match");
    
    cleanup_test_dir();
}

TEST(fixer_write_file_with_backup) {
    setup_test_dir();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/backup_test.sh", test_script_dir);
    
    /* Create original file */
    const char *original = "#!/bin/sh\necho original\n";
    FILE *f = fopen(path, "w");
    fputs(original, f);
    fclose(f);
    
    /* Write new content with backup */
    const char *content = "#!/bin/sh\necho new\n";
    fixer_result_t result = fixer_write_file(path, content, true);
    
    ASSERT_EQ(result, FIXER_OK, "write_file with backup should succeed");
    
    /* Verify backup exists */
    char backup_path[520];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", path);
    
    f = fopen(backup_path, "r");
    ASSERT_NOT_NULL(f, "Backup file should exist");
    
    char buf[256];
    size_t read = fread(buf, 1, sizeof(buf) - 1, f);
    buf[read] = '\0';
    fclose(f);
    
    ASSERT_STR_EQ(buf, original, "Backup should have original content");
    
    cleanup_test_dir();
}

TEST(fixer_write_file_null) {
    fixer_result_t result = fixer_write_file(NULL, "content", false);
    ASSERT_EQ(result, FIXER_ERR_IO, "write_file with NULL path should fail");
    
    result = fixer_write_file("/tmp/test.sh", NULL, false);
    ASSERT_EQ(result, FIXER_ERR_IO, "write_file with NULL content should fail");
}

/* ============================================================================
 * Diff Generation Tests
 * ============================================================================ */

TEST(fixer_generate_diff_no_changes) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo hello", "test.sh");
    
    fixer_options_t opts = { 0 };
    
    char diff[1024];
    int len = fixer_generate_diff(&ctx, &opts, diff, sizeof(diff));
    
    ASSERT_TRUE(len > 0, "Diff should be generated");
    ASSERT_NOT_NULL(strstr(diff, "No changes"), "Should indicate no changes");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_generate_diff_with_changes) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source file.sh", "test.sh");
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 6;
    fix.replacement = ".";
    fix.type = FIX_TYPE_SAFE;
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    
    char diff[1024];
    int len = fixer_generate_diff(&ctx, &opts, diff, sizeof(diff));
    
    ASSERT_TRUE(len > 0, "Diff should be generated");
    ASSERT_NOT_NULL(strstr(diff, "---"), "Diff should have header");
    ASSERT_NOT_NULL(strstr(diff, "+++"), "Diff should have header");
    
    fixer_cleanup(&ctx);
}

/* ============================================================================
 * Interactive Mode Tests
 * ============================================================================ */

TEST(fixer_interactive_init_basic) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "echo test", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = "printf";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    
    fixer_result_t result = fixer_interactive_init(&session, &ctx, &opts);
    
    ASSERT_EQ(result, FIXER_OK, "interactive_init should succeed");
    ASSERT_EQ(session.ctx, &ctx, "session ctx should be set");
    ASSERT_NOT_NULL(session.accepted, "accepted array should be allocated");
    ASSERT_EQ(session.current, 0, "current should be 0");
    ASSERT_FALSE(session.apply_all, "apply_all should be false");
    ASSERT_FALSE(session.aborted, "aborted should be false");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_init_null) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    
    fixer_result_t result = fixer_interactive_init(NULL, &ctx, &opts);
    ASSERT_EQ(result, FIXER_ERR_NOMEM, "init with NULL session should fail");
    
    result = fixer_interactive_init(&session, NULL, &opts);
    ASSERT_EQ(result, FIXER_ERR_NOMEM, "init with NULL ctx should fail");
    
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_cleanup_null) {
    /* Should not crash */
    fixer_interactive_cleanup(NULL);
}

TEST(fixer_interactive_next_basic) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source file", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = ".";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    bool has_next = fixer_interactive_next(&session, &next_fix);
    
    ASSERT_TRUE(has_next, "Should have a next fix");
    ASSERT_NOT_NULL(next_fix, "next_fix should not be NULL");
    ASSERT_EQ(next_fix->type, FIX_TYPE_SAFE, "Fix type should match");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_next_skip_manual) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "eval cmd", NULL);
    
    fixer_fix_t manual = {0};
    manual.type = FIX_TYPE_MANUAL;
    fixer_fix_t safe = {0};
    safe.type = FIX_TYPE_SAFE;
    safe.replacement = "test";
    
    fixer_add_fix(&ctx, &manual);
    fixer_add_fix(&ctx, &safe);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    bool has_next = fixer_interactive_next(&session, &next_fix);
    
    ASSERT_TRUE(has_next, "Should have a next fix");
    ASSERT_EQ(next_fix->type, FIX_TYPE_SAFE, "Should skip manual, get safe");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_next_skip_unsafe_when_disabled) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t unsafe = {0};
    unsafe.type = FIX_TYPE_UNSAFE;
    unsafe.replacement = "a";
    fixer_fix_t safe = {0};
    safe.type = FIX_TYPE_SAFE;
    safe.replacement = "b";
    
    fixer_add_fix(&ctx, &unsafe);
    fixer_add_fix(&ctx, &safe);
    
    fixer_options_t opts = { .include_unsafe = false };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    bool has_next = fixer_interactive_next(&session, &next_fix);
    
    ASSERT_TRUE(has_next, "Should have next fix");
    ASSERT_EQ(next_fix->type, FIX_TYPE_SAFE, "Should skip unsafe, get safe");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_respond_yes) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = "a";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_YES);
    
    ASSERT_TRUE(session.accepted[0], "Fix should be accepted");
    ASSERT_EQ(session.current, 1, "current should advance");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_respond_no) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = "a";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_NO);
    
    ASSERT_FALSE(session.accepted[0], "Fix should not be accepted");
    ASSERT_EQ(session.current, 1, "current should advance");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_respond_all) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t fix1 = {0};
    fix1.type = FIX_TYPE_SAFE;
    fix1.replacement = "a";
    fixer_fix_t fix2 = {0};
    fix2.type = FIX_TYPE_SAFE;
    fix2.replacement = "b";
    fixer_fix_t fix3 = {0};
    fix3.type = FIX_TYPE_SAFE;
    fix3.replacement = "c";
    
    fixer_add_fix(&ctx, &fix1);
    fixer_add_fix(&ctx, &fix2);
    fixer_add_fix(&ctx, &fix3);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_ALL);
    
    ASSERT_TRUE(session.apply_all, "apply_all should be set");
    ASSERT_TRUE(session.accepted[0], "Fix 0 should be accepted");
    ASSERT_TRUE(session.accepted[1], "Fix 1 should be accepted");
    ASSERT_TRUE(session.accepted[2], "Fix 2 should be accepted");
    ASSERT_EQ(session.current, ctx.count, "current should be at end");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_respond_quit) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "test", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = "a";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_QUIT);
    
    ASSERT_TRUE(session.aborted, "session should be aborted");
    ASSERT_EQ(session.current, ctx.count, "current should be at end");
    
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_apply_none_accepted) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source file", NULL);
    
    fixer_fix_t fix = {0};
    fix.type = FIX_TYPE_SAFE;
    fix.replacement = ".";
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    /* Don't accept any fixes */
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_NO);
    
    char *output = NULL;
    size_t applied = 0;
    fixer_result_t result = fixer_interactive_apply(&session, &output, &applied);
    
    ASSERT_EQ(result, FIXER_OK, "apply should succeed");
    ASSERT_EQ(applied, 0, "No fixes should be applied");
    ASSERT_STR_EQ(output, "source file", "Output should be original");
    
    free(output);
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

TEST(fixer_interactive_apply_some_accepted) {
    fixer_context_t ctx;
    fixer_init(&ctx);
    fixer_load_string(&ctx, "source file", NULL);
    
    fixer_fix_t fix = {0};
    fix.match_start = 0;
    fix.match_length = 6;
    fix.replacement = ".";
    fix.type = FIX_TYPE_SAFE;
    fixer_add_fix(&ctx, &fix);
    
    fixer_options_t opts = { 0 };
    fixer_interactive_t session;
    fixer_interactive_init(&session, &ctx, &opts);
    
    /* Accept the fix */
    const fixer_fix_t *next_fix;
    fixer_interactive_next(&session, &next_fix);
    fixer_interactive_respond(&session, FIXER_RESPONSE_YES);
    
    char *output = NULL;
    size_t applied = 0;
    fixer_result_t result = fixer_interactive_apply(&session, &output, &applied);
    
    ASSERT_EQ(result, FIXER_OK, "apply should succeed");
    ASSERT_EQ(applied, 1, "One fix should be applied");
    ASSERT_STR_EQ(output, ". file", "Output should have fix");
    
    free(output);
    fixer_interactive_cleanup(&session);
    fixer_cleanup(&ctx);
}

/* ============================================================================
 * Result String Tests
 * ============================================================================ */

TEST(fixer_result_string_all) {
    ASSERT_STR_EQ(fixer_result_string(FIXER_OK), "Success", "OK string");
    ASSERT_STR_EQ(fixer_result_string(FIXER_ERR_IO), "I/O error", "IO string");
    ASSERT_STR_EQ(fixer_result_string(FIXER_ERR_PARSE), "Parse error", "Parse string");
    ASSERT_STR_EQ(fixer_result_string(FIXER_ERR_NOMEM), "Out of memory", "Nomem string");
    ASSERT_STR_EQ(fixer_result_string(FIXER_ERR_NOFIX), "No fixes to apply", "Nofix string");
    ASSERT_STR_EQ(fixer_result_string(FIXER_ERR_VERIFY), "Verification failed", "Verify string");
    ASSERT_STR_EQ(fixer_result_string((fixer_result_t)99), "Unknown error", "Unknown string");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running fixer module tests...\n\n");
    
    printf("Context Lifecycle:\n");
    RUN_TEST(fixer_init_basic);
    RUN_TEST(fixer_init_null);
    RUN_TEST(fixer_cleanup_null);
    RUN_TEST(fixer_cleanup_double);
    
    printf("\nLoad Operations:\n");
    RUN_TEST(fixer_load_string_basic);
    RUN_TEST(fixer_load_string_null_path);
    RUN_TEST(fixer_load_string_null_content);
    RUN_TEST(fixer_load_file_basic);
    RUN_TEST(fixer_load_file_nonexistent);
    
    printf("\nFix Add and Count:\n");
    RUN_TEST(fixer_add_fix_basic);
    RUN_TEST(fixer_add_fix_null);
    RUN_TEST(fixer_count_safe);
    RUN_TEST(fixer_count_null);
    
    printf("\nFix Application:\n");
    RUN_TEST(fixer_apply_fixes_no_fixes);
    RUN_TEST(fixer_apply_fixes_single);
    RUN_TEST(fixer_apply_fixes_skip_unsafe);
    RUN_TEST(fixer_apply_fixes_include_unsafe);
    RUN_TEST(fixer_apply_fixes_skip_manual);
    RUN_TEST(fixer_apply_fixes_alloc);
    
    printf("\nSyntax Verification:\n");
    RUN_TEST(fixer_verify_syntax_valid);
    RUN_TEST(fixer_verify_syntax_null);
    
    printf("\nFile Write:\n");
    RUN_TEST(fixer_write_file_basic);
    RUN_TEST(fixer_write_file_with_backup);
    RUN_TEST(fixer_write_file_null);
    
    printf("\nDiff Generation:\n");
    RUN_TEST(fixer_generate_diff_no_changes);
    RUN_TEST(fixer_generate_diff_with_changes);
    
    printf("\nInteractive Mode:\n");
    RUN_TEST(fixer_interactive_init_basic);
    RUN_TEST(fixer_interactive_init_null);
    RUN_TEST(fixer_interactive_cleanup_null);
    RUN_TEST(fixer_interactive_next_basic);
    RUN_TEST(fixer_interactive_next_skip_manual);
    RUN_TEST(fixer_interactive_next_skip_unsafe_when_disabled);
    RUN_TEST(fixer_interactive_respond_yes);
    RUN_TEST(fixer_interactive_respond_no);
    RUN_TEST(fixer_interactive_respond_all);
    RUN_TEST(fixer_interactive_respond_quit);
    RUN_TEST(fixer_interactive_apply_none_accepted);
    RUN_TEST(fixer_interactive_apply_some_accepted);
    
    printf("\nResult Strings:\n");
    RUN_TEST(fixer_result_string_all);
    
    printf("\n========================================\n");
    printf("Tests run: %d, Passed: %d, Failed: %d\n",
           tests_run, tests_passed, tests_failed);
    printf("========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
