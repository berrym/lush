/**
 * @file test_autoload.c
 * @brief Unit tests for the autoload registry and fpath resolution
 *
 * Exercises the public surface: registration, declaration probe, clear,
 * and the file-resolution path (FPATH search + default-glob fallback).
 * The end-to-end `autoload_try_resolve` happy path needs an executor
 * with a working symbol table, so it lives here with init_symtable()
 * setup mirroring test_redirection.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "autoload.h"
#include "executor.h"
#include "symtable.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Registry
 * ============================================================================
 */

TEST(register_then_declared) {
    autoload_clear();
    ASSERT(!autoload_is_declared("alpha"), "fresh registry: not declared");
    ASSERT(autoload_register("alpha"), "register alpha");
    ASSERT(autoload_is_declared("alpha"), "alpha is now declared");
    autoload_clear();
    ASSERT(!autoload_is_declared("alpha"), "after clear: not declared");
}

TEST(register_idempotent) {
    autoload_clear();
    ASSERT(autoload_register("beta"), "first register");
    ASSERT(autoload_register("beta"), "second register (idempotent)");
    ASSERT(autoload_is_declared("beta"), "still declared");
    autoload_clear();
}

TEST(register_rejects_empty_and_null) {
    autoload_clear();
    ASSERT(!autoload_register(NULL), "NULL rejected");
    ASSERT(!autoload_register(""), "empty rejected");
    autoload_clear();
}

TEST(is_declared_safe_on_null) {
    ASSERT(!autoload_is_declared(NULL), "NULL probe returns false");
}

TEST(clear_is_idempotent) {
    autoload_clear();
    autoload_clear(); // must not crash on already-empty
    ASSERT(!autoload_is_declared("anything"), "still empty");
}

/* ============================================================================
 * Resolution
 * ============================================================================
 *
 * Build a temporary directory with a function file, point FPATH at it,
 * register the name, resolve, and confirm the function is now defined.
 */

static char tmp_dir[256];
static char tmp_func[512];

static void write_function_file(const char *name, const char *body) {
    snprintf(tmp_func, sizeof(tmp_func), "%s/%s", tmp_dir, name);
    FILE *fp = fopen(tmp_func, "w");
    if (fp) {
        fputs(body, fp);
        fclose(fp);
    }
}

static bool make_tmpdir(void) {
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/lush_autoload_test_%d", getpid());
    return mkdir(tmp_dir, 0755) == 0 ||
           (mkdir(tmp_dir, 0755) == -1 && access(tmp_dir, X_OK) == 0);
}

TEST(resolve_via_fpath_defines_function) {
    autoload_clear();
    ASSERT(make_tmpdir(), "create tmpdir");
    write_function_file("greet", "echo 'hi from autoload'\n");

    char old_fpath[1024] = {0};
    const char *e = getenv("FPATH");
    if (e) {
        strncpy(old_fpath, e, sizeof(old_fpath) - 1);
    }
    setenv("FPATH", tmp_dir, 1);

    executor_t *executor = executor_new();
    ASSERT(executor != NULL, "executor_new");
    ASSERT(autoload_register("greet"), "register greet");
    ASSERT(autoload_try_resolve(executor, "greet"),
           "resolve succeeds via FPATH");
    ASSERT(!autoload_is_declared("greet"), "removed from registry on success");

    executor_free(executor);

    // Cleanup
    unlink(tmp_func);
    rmdir(tmp_dir);
    if (old_fpath[0]) {
        setenv("FPATH", old_fpath, 1);
    } else {
        unsetenv("FPATH");
    }
}

TEST(resolve_returns_false_when_not_on_fpath) {
    autoload_clear();
    setenv("FPATH", "/nonexistent-autoload-dir", 1);

    executor_t *executor = executor_new();
    ASSERT(executor != NULL, "executor_new");
    ASSERT(autoload_register("definitely_missing_func"), "register");
    ASSERT(!autoload_try_resolve(executor, "definitely_missing_func"),
           "resolve fails when file not found");
    ASSERT(autoload_is_declared("definitely_missing_func"),
           "registry entry persists on failure");

    executor_free(executor);
    autoload_clear();
    unsetenv("FPATH");
}

TEST(resolve_rejects_undeclared_name) {
    autoload_clear();
    executor_t *executor = executor_new();
    ASSERT(executor != NULL, "executor_new");
    ASSERT(!autoload_try_resolve(executor, "never_registered"),
           "undeclared names cannot be auto-resolved");
    executor_free(executor);
}

TEST(resolve_rejects_path_traversal) {
    autoload_clear();
    executor_t *executor = executor_new();
    ASSERT(executor != NULL, "executor_new");
    // Name containing `/` would otherwise let an attacker reach
    // arbitrary paths via FPATH+name composition.
    ASSERT(autoload_register("../etc/passwd"),
           "register pretends to accept the name");
    ASSERT(!autoload_try_resolve(executor, "../etc/passwd"),
           "resolve refuses names containing slashes");
    autoload_clear();
    executor_free(executor);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running autoload tests...\n\n");

    init_symtable();

    printf("Registry:\n");
    RUN_TEST(register_then_declared);
    RUN_TEST(register_idempotent);
    RUN_TEST(register_rejects_empty_and_null);
    RUN_TEST(is_declared_safe_on_null);
    RUN_TEST(clear_is_idempotent);

    printf("\nResolution:\n");
    RUN_TEST(resolve_via_fpath_defines_function);
    RUN_TEST(resolve_returns_false_when_not_on_fpath);
    RUN_TEST(resolve_rejects_undeclared_name);
    RUN_TEST(resolve_rejects_path_traversal);

    return TEST_RESULT();
}
