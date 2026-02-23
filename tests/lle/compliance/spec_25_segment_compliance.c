/**
 * @file spec_25_segment_compliance.c
 * @brief Spec 25 Section 5 Segment Architecture Compliance Test
 *
 * Tests for LLE Specification 25 Section 5: Segment Architecture
 * Validates API completeness and spec adherence.
 *
 * This compliance test verifies:
 * - All segment types are defined
 * - All segment API functions are declared
 * - Segment registry operations work correctly
 * - Built-in segments implement required callbacks
 * - Segment output structure matches specification
 *
 * Test Coverage:
 * - Segment Registry API (5 functions)
 * - Prompt Context API (2 functions)
 * - Segment Lifecycle API (2 functions)
 * - Built-in Segment Factories (8 segments)
 * - Segment Callback Interface (4 callbacks)
 *
 * Specification:
 * docs/lle_specification/25_prompt_theme_system_complete.md Section 5
 * Date: 2025-12-26
 */

#include "lle/error_handling.h"
#include "lle/prompt/segment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test assertion counter */
static int assertions_passed = 0;
static int tests_run = 0;

#define COMPLIANCE_ASSERT(condition, message)                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "COMPLIANCE VIOLATION: %s\n", message);            \
            fprintf(stderr, "   at %s:%d\n", __FILE__, __LINE__);              \
            exit(1);                                                           \
        }                                                                      \
        assertions_passed++;                                                   \
    } while (0)

#define TEST_START(name)                                                       \
    do {                                                                       \
        tests_run++;                                                           \
        printf("  Test %d: %s...", tests_run, name);                           \
    } while (0)

#define TEST_PASS()                                                            \
    do {                                                                       \
        printf(" PASS\n");                                                     \
    } while (0)

/* ========================================================================== */
/* Test: Segment Type Definitions                                             */
/* ========================================================================== */

static void test_segment_type_definitions(void) {
    printf("Phase 1: Segment Type Definitions\n");
    printf("----------------------------------\n");

    TEST_START("lle_segment_capability_t enum defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_NONE == 0, "LLE_SEG_CAP_NONE defined as 0");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_ASYNC >= 0, "LLE_SEG_CAP_ASYNC defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_CACHEABLE >= 0,
                      "LLE_SEG_CAP_CACHEABLE defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_EXPENSIVE >= 0,
                      "LLE_SEG_CAP_EXPENSIVE defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_THEME_AWARE >= 0,
                      "LLE_SEG_CAP_THEME_AWARE defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_DYNAMIC >= 0, "LLE_SEG_CAP_DYNAMIC defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_OPTIONAL >= 0,
                      "LLE_SEG_CAP_OPTIONAL defined");
    COMPLIANCE_ASSERT(LLE_SEG_CAP_PROPERTIES >= 0,
                      "LLE_SEG_CAP_PROPERTIES defined");
    TEST_PASS();

    TEST_START("lle_segment_output_t structure defined");
    lle_segment_output_t output;
    COMPLIANCE_ASSERT(sizeof(output.content) == LLE_SEGMENT_OUTPUT_MAX,
                      "content buffer size matches LLE_SEGMENT_OUTPUT_MAX");
    COMPLIANCE_ASSERT(sizeof(output.content_len) == sizeof(size_t),
                      "content_len is size_t");
    COMPLIANCE_ASSERT(sizeof(output.visual_width) == sizeof(size_t),
                      "visual_width is size_t");
    TEST_PASS();

    TEST_START("lle_prompt_context_t structure defined");
    lle_prompt_context_t ctx;
    COMPLIANCE_ASSERT(sizeof(ctx.cwd) > 0, "cwd field exists");
    COMPLIANCE_ASSERT(sizeof(ctx.username) > 0, "username field exists");
    COMPLIANCE_ASSERT(sizeof(ctx.hostname) > 0, "hostname field exists");
    COMPLIANCE_ASSERT(sizeof(ctx.last_exit_code) == sizeof(int),
                      "last_exit_code is int");
    COMPLIANCE_ASSERT(sizeof(ctx.background_job_count) == sizeof(int),
                      "background_job_count is int");
    TEST_PASS();

    TEST_START("lle_segment_registry_t structure defined");
    lle_segment_registry_t registry;
    COMPLIANCE_ASSERT(sizeof(registry) > 0, "registry structure has size");
    TEST_PASS();

    printf("  Phase 1 complete (4 tests)\n\n");
}

/* ========================================================================== */
/* Test: Segment Registry API                                                 */
/* ========================================================================== */

static void test_segment_registry_api(void) {
    printf("Phase 2: Segment Registry API\n");
    printf("-----------------------------\n");

    TEST_START("lle_segment_registry_init function");
    COMPLIANCE_ASSERT(lle_segment_registry_init != NULL,
                      "lle_segment_registry_init declared");
    lle_segment_registry_t registry;
    lle_result_t result = lle_segment_registry_init(&registry);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_segment_registry_init returns LLE_SUCCESS");
    TEST_PASS();

    TEST_START("lle_segment_registry_register function");
    COMPLIANCE_ASSERT(lle_segment_registry_register != NULL,
                      "lle_segment_registry_register declared");
    lle_prompt_segment_t *seg = lle_segment_create_directory();
    COMPLIANCE_ASSERT(seg != NULL, "segment creation works");
    result = lle_segment_registry_register(&registry, seg);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_segment_registry_register returns LLE_SUCCESS");
    TEST_PASS();

    TEST_START("lle_segment_registry_find function");
    COMPLIANCE_ASSERT(lle_segment_registry_find != NULL,
                      "lle_segment_registry_find declared");
    lle_prompt_segment_t *found =
        lle_segment_registry_find(&registry, "directory");
    COMPLIANCE_ASSERT(found != NULL, "find returns registered segment");
    COMPLIANCE_ASSERT(found == seg, "find returns same pointer");
    TEST_PASS();

    TEST_START("lle_segment_registry_list function");
    COMPLIANCE_ASSERT(lle_segment_registry_list != NULL,
                      "lle_segment_registry_list declared");
    const char *names[16];
    size_t count = lle_segment_registry_list(&registry, names, 16);
    COMPLIANCE_ASSERT(count == 1, "list returns correct count");
    COMPLIANCE_ASSERT(strcmp(names[0], "directory") == 0,
                      "list returns correct name");
    TEST_PASS();

    TEST_START("lle_segment_registry_cleanup function");
    COMPLIANCE_ASSERT(lle_segment_registry_cleanup != NULL,
                      "lle_segment_registry_cleanup declared");
    lle_segment_registry_cleanup(&registry);
    /* No crash = success */
    TEST_PASS();

    printf("  Phase 2 complete (5 tests)\n\n");
}

/* ========================================================================== */
/* Test: Prompt Context API                                                   */
/* ========================================================================== */

static void test_prompt_context_api(void) {
    printf("Phase 3: Prompt Context API\n");
    printf("---------------------------\n");

    TEST_START("lle_prompt_context_init function");
    COMPLIANCE_ASSERT(lle_prompt_context_init != NULL,
                      "lle_prompt_context_init declared");
    lle_prompt_context_t ctx;
    lle_result_t result = lle_prompt_context_init(&ctx);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_prompt_context_init returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(strlen(ctx.username) > 0, "username populated");
    COMPLIANCE_ASSERT(strlen(ctx.cwd) > 0, "cwd populated");
    TEST_PASS();

    TEST_START("lle_prompt_context_update function");
    COMPLIANCE_ASSERT(lle_prompt_context_update != NULL,
                      "lle_prompt_context_update declared");
    lle_prompt_context_update(&ctx, 42, 1000);
    COMPLIANCE_ASSERT(ctx.last_exit_code == 42, "exit code updated");
    COMPLIANCE_ASSERT(ctx.last_cmd_duration_ms == 1000, "duration updated");
    TEST_PASS();

    TEST_START("lle_prompt_context_refresh_directory function");
    COMPLIANCE_ASSERT(lle_prompt_context_refresh_directory != NULL,
                      "lle_prompt_context_refresh_directory declared");
    result = lle_prompt_context_refresh_directory(&ctx);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "refresh_directory returns LLE_SUCCESS");
    TEST_PASS();

    printf("  Phase 3 complete (3 tests)\n\n");
}

/* ========================================================================== */
/* Test: Segment Lifecycle API                                                */
/* ========================================================================== */

static void test_segment_lifecycle_api(void) {
    printf("Phase 4: Segment Lifecycle API\n");
    printf("------------------------------\n");

    TEST_START("lle_segment_create function");
    COMPLIANCE_ASSERT(lle_segment_create != NULL,
                      "lle_segment_create declared");
    lle_prompt_segment_t *seg =
        lle_segment_create("test", "Test segment", LLE_SEG_CAP_NONE);
    COMPLIANCE_ASSERT(seg != NULL, "lle_segment_create returns segment");
    COMPLIANCE_ASSERT(strcmp(seg->name, "test") == 0, "name set correctly");
    COMPLIANCE_ASSERT(seg->capabilities == LLE_SEG_CAP_NONE,
                      "capabilities set correctly");
    TEST_PASS();

    TEST_START("lle_segment_free function");
    COMPLIANCE_ASSERT(lle_segment_free != NULL, "lle_segment_free declared");
    lle_segment_free(seg);
    /* No crash = success */
    TEST_PASS();

    printf("  Phase 4 complete (2 tests)\n\n");
}

/* ========================================================================== */
/* Test: Built-in Segment Factories                                           */
/* ========================================================================== */

static void test_builtin_segment_factories(void) {
    printf("Phase 5: Built-in Segment Factories\n");
    printf("------------------------------------\n");

    TEST_START("lle_segment_create_directory");
    COMPLIANCE_ASSERT(lle_segment_create_directory != NULL,
                      "lle_segment_create_directory declared");
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    COMPLIANCE_ASSERT(dir != NULL, "creates directory segment");
    COMPLIANCE_ASSERT(strcmp(dir->name, "directory") == 0, "correct name");
    COMPLIANCE_ASSERT(dir->render != NULL, "has render callback");
    lle_segment_free(dir);
    TEST_PASS();

    TEST_START("lle_segment_create_user");
    COMPLIANCE_ASSERT(lle_segment_create_user != NULL,
                      "lle_segment_create_user declared");
    lle_prompt_segment_t *user = lle_segment_create_user();
    COMPLIANCE_ASSERT(user != NULL, "creates user segment");
    COMPLIANCE_ASSERT(strcmp(user->name, "user") == 0, "correct name");
    lle_segment_free(user);
    TEST_PASS();

    TEST_START("lle_segment_create_host");
    COMPLIANCE_ASSERT(lle_segment_create_host != NULL,
                      "lle_segment_create_host declared");
    lle_prompt_segment_t *host = lle_segment_create_host();
    COMPLIANCE_ASSERT(host != NULL, "creates host segment");
    COMPLIANCE_ASSERT(strcmp(host->name, "host") == 0, "correct name");
    lle_segment_free(host);
    TEST_PASS();

    TEST_START("lle_segment_create_time");
    COMPLIANCE_ASSERT(lle_segment_create_time != NULL,
                      "lle_segment_create_time declared");
    lle_prompt_segment_t *time_seg = lle_segment_create_time();
    COMPLIANCE_ASSERT(time_seg != NULL, "creates time segment");
    COMPLIANCE_ASSERT(strcmp(time_seg->name, "time") == 0, "correct name");
    lle_segment_free(time_seg);
    TEST_PASS();

    TEST_START("lle_segment_create_status");
    COMPLIANCE_ASSERT(lle_segment_create_status != NULL,
                      "lle_segment_create_status declared");
    lle_prompt_segment_t *status = lle_segment_create_status();
    COMPLIANCE_ASSERT(status != NULL, "creates status segment");
    COMPLIANCE_ASSERT(strcmp(status->name, "status") == 0, "correct name");
    lle_segment_free(status);
    TEST_PASS();

    TEST_START("lle_segment_create_jobs");
    COMPLIANCE_ASSERT(lle_segment_create_jobs != NULL,
                      "lle_segment_create_jobs declared");
    lle_prompt_segment_t *jobs = lle_segment_create_jobs();
    COMPLIANCE_ASSERT(jobs != NULL, "creates jobs segment");
    COMPLIANCE_ASSERT(strcmp(jobs->name, "jobs") == 0, "correct name");
    lle_segment_free(jobs);
    TEST_PASS();

    TEST_START("lle_segment_create_symbol");
    COMPLIANCE_ASSERT(lle_segment_create_symbol != NULL,
                      "lle_segment_create_symbol declared");
    lle_prompt_segment_t *symbol = lle_segment_create_symbol();
    COMPLIANCE_ASSERT(symbol != NULL, "creates symbol segment");
    COMPLIANCE_ASSERT(strcmp(symbol->name, "symbol") == 0, "correct name");
    lle_segment_free(symbol);
    TEST_PASS();

    TEST_START("lle_segment_create_git");
    COMPLIANCE_ASSERT(lle_segment_create_git != NULL,
                      "lle_segment_create_git declared");
    lle_prompt_segment_t *git = lle_segment_create_git();
    COMPLIANCE_ASSERT(git != NULL, "creates git segment");
    COMPLIANCE_ASSERT(strcmp(git->name, "git") == 0, "correct name");
    lle_segment_free(git);
    TEST_PASS();

    printf("  Phase 5 complete (8 tests)\n\n");
}

/* ========================================================================== */
/* Test: Segment Callback Interface                                           */
/* ========================================================================== */

static void test_segment_callback_interface(void) {
    printf("Phase 6: Segment Callback Interface\n");
    printf("------------------------------------\n");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    TEST_START("segment render callback");
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    COMPLIANCE_ASSERT(dir->render != NULL, "render callback exists");
    lle_segment_output_t output;
    memset(&output, 0, sizeof(output));
    lle_result_t result = dir->render(dir, &ctx, NULL, &output);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "render returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(output.content_len > 0, "render produces content");
    COMPLIANCE_ASSERT(output.visual_width > 0, "render sets visual_width");
    lle_segment_free(dir);
    TEST_PASS();

    TEST_START("segment is_visible callback");
    lle_prompt_segment_t *status = lle_segment_create_status();
    COMPLIANCE_ASSERT(status->is_visible != NULL, "is_visible callback exists");
    /* Status segment visibility depends on last_exit_code != 0 */
    ctx.last_exit_code = 0;
    bool visible = status->is_visible(status, &ctx);
    COMPLIANCE_ASSERT(visible == false, "status hidden when exit code is 0");
    ctx.last_exit_code = 1;
    visible = status->is_visible(status, &ctx);
    COMPLIANCE_ASSERT(visible == true, "status visible when exit code != 0");
    lle_segment_free(status);
    TEST_PASS();

    TEST_START("segment get_property callback");
    lle_prompt_segment_t *git = lle_segment_create_git();
    COMPLIANCE_ASSERT(git->get_property != NULL,
                      "get_property callback exists");
    /* Property access without state returns NULL */
    const char *branch = git->get_property(git, "branch");
    /* NULL is acceptable when not in git repo */
    (void)branch;
    lle_segment_free(git);
    TEST_PASS();

    TEST_START("segment invalidate_cache callback");
    lle_prompt_segment_t *dir2 = lle_segment_create_directory();
    COMPLIANCE_ASSERT(dir2->invalidate_cache != NULL,
                      "invalidate_cache callback exists");
    dir2->invalidate_cache(dir2);
    /* No crash = success */
    lle_segment_free(dir2);
    TEST_PASS();

    printf("  Phase 6 complete (4 tests)\n\n");
}

/* ========================================================================== */
/* Test: Segment Output Specification                                         */
/* ========================================================================== */

static void test_segment_output_specification(void) {
    printf("Phase 7: Segment Output Specification\n");
    printf("--------------------------------------\n");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    lle_segment_output_t output;

    TEST_START("directory segment output format");
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    memset(&output, 0, sizeof(output));
    dir->render(dir, &ctx, NULL, &output);
    /* Per spec: directory uses ~ for home abbreviation */
    /* Content should be non-empty */
    COMPLIANCE_ASSERT(output.content_len > 0, "directory has content");
    COMPLIANCE_ASSERT(output.needs_separator == true, "needs_separator set");
    lle_segment_free(dir);
    TEST_PASS();

    TEST_START("user segment output format");
    lle_prompt_segment_t *user = lle_segment_create_user();
    memset(&output, 0, sizeof(output));
    user->render(user, &ctx, NULL, &output);
    COMPLIANCE_ASSERT(output.content_len > 0, "user has content");
    COMPLIANCE_ASSERT(strlen(output.content) == output.content_len,
                      "content_len matches strlen");
    lle_segment_free(user);
    TEST_PASS();

    TEST_START("symbol segment output format");
    lle_prompt_segment_t *symbol = lle_segment_create_symbol();
    memset(&output, 0, sizeof(output));
    symbol->render(symbol, &ctx, NULL, &output);
    /* Per spec: $ for user, # for root */
    if (ctx.is_root) {
        COMPLIANCE_ASSERT(strcmp(output.content, "#") == 0,
                          "root gets # symbol");
    } else {
        COMPLIANCE_ASSERT(strcmp(output.content, "$") == 0,
                          "user gets $ symbol");
    }
    lle_segment_free(symbol);
    TEST_PASS();

    TEST_START("time segment output format");
    lle_prompt_segment_t *time_seg = lle_segment_create_time();
    memset(&output, 0, sizeof(output));
    time_seg->render(time_seg, &ctx, NULL, &output);
    /* Per spec: HH:MM:SS format = 8 characters */
    COMPLIANCE_ASSERT(output.content_len == 8, "time is HH:MM:SS format");
    COMPLIANCE_ASSERT(output.content[2] == ':', "first colon at position 2");
    COMPLIANCE_ASSERT(output.content[5] == ':', "second colon at position 5");
    lle_segment_free(time_seg);
    TEST_PASS();

    printf("  Phase 7 complete (4 tests)\n\n");
}

/* ========================================================================== */
/* Test: Registry Builtins Helper                                             */
/* ========================================================================== */

static void test_register_builtins(void) {
    printf("Phase 8: Built-in Registration\n");
    printf("-------------------------------\n");

    TEST_START("lle_segment_register_builtins function");
    COMPLIANCE_ASSERT(lle_segment_register_builtins != NULL,
                      "lle_segment_register_builtins declared");
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);
    size_t count = lle_segment_register_builtins(&registry);
    COMPLIANCE_ASSERT(count == 15, "registers 15 built-in segments");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "directory") != NULL,
                      "directory segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "git") != NULL,
                      "git segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "user") != NULL,
                      "user segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "host") != NULL,
                      "host segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "time") != NULL,
                      "time segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "status") != NULL,
                      "status segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "jobs") != NULL,
                      "jobs segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "symbol") != NULL,
                      "symbol segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "shlvl") != NULL,
                      "shlvl segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "ssh") != NULL,
                      "ssh segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "cmd_duration") != NULL,
                      "cmd_duration segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "virtualenv") != NULL,
                      "virtualenv segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "container") != NULL,
                      "container segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "aws") != NULL,
                      "aws segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "kubernetes") != NULL,
                      "kubernetes segment registered");
    lle_segment_registry_cleanup(&registry);
    TEST_PASS();

    printf("  Phase 8 complete (1 test)\n\n");
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
    printf("Spec 25 Section 5 Segment Architecture Compliance Test\n");
    printf("=======================================================\n\n");

    test_segment_type_definitions();
    test_segment_registry_api();
    test_prompt_context_api();
    test_segment_lifecycle_api();
    test_builtin_segment_factories();
    test_segment_callback_interface();
    test_segment_output_specification();
    test_register_builtins();

    printf("=======================================================\n");
    printf("COMPLIANCE TEST PASSED\n");
    printf("  Tests run: %d\n", tests_run);
    printf("  Assertions: %d\n", assertions_passed);
    printf("=======================================================\n");

    return 0;
}
