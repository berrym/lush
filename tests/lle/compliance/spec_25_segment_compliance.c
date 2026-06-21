/**
 * @file spec_25_segment_compliance.c
 * @brief Spec 25 Section 5 Segment Architecture Compliance Test
 *
 * Tests for LLE Specification 25 Section 5: Segment Architecture
 * Validates API completeness and spec adherence.
 *
 * This compliance test verifies observable segment behavior:
 * - Capability flags are distinct single-bit values and OR-combine cleanly
 * - Rendered output stays within its buffer and is correctly NUL-terminated
 * - Registry register/find/list round-trips return the registered segments
 * - Prompt context init/update populate and mutate the documented fields
 * - Built-in factories produce segments with the correct name and callbacks
 * - Render/is_visible/get_property callbacks return spec-conformant results
 *   (HH:MM:SS time, $/# symbol, status hidden on exit 0)
 *
 * Specification:
 * docs/lle_specification/25_prompt_theme_system_complete.md Section 5
 */

#include "lle/error_handling.h"
#include "lle/prompt/segment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Test assertion counter
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
/// Test: Segment Type Definitions
/* ========================================================================== */

/// True when `v` is exactly one set bit (a single distinct flag).
static bool is_single_bit(unsigned v) { return v != 0 && (v & (v - 1)) == 0; }

static void test_segment_type_definitions(void) {
    printf("Segment Type Definitions\n");
    printf("----------------------------------\n");

    TEST_START("capability flags are distinct single-bit values");
    /// Capabilities are OR-combined into a bitmask on each segment, so every
    /// flag must occupy exactly one bit and no two may collide. A regression
    /// that gave two capabilities the same value would silently conflate them.
    const lle_segment_capability_t caps[] = {
        LLE_SEG_CAP_ASYNC,       LLE_SEG_CAP_CACHEABLE, LLE_SEG_CAP_EXPENSIVE,
        LLE_SEG_CAP_THEME_AWARE, LLE_SEG_CAP_DYNAMIC,   LLE_SEG_CAP_OPTIONAL,
        LLE_SEG_CAP_PROPERTIES};
    const size_t cap_count = sizeof(caps) / sizeof(caps[0]);
    COMPLIANCE_ASSERT(LLE_SEG_CAP_NONE == 0, "NONE is the empty bitmask");
    unsigned seen = 0;
    for (size_t i = 0; i < cap_count; i++) {
        COMPLIANCE_ASSERT(is_single_bit((unsigned)caps[i]),
                          "each capability is a single bit");
        COMPLIANCE_ASSERT((seen & (unsigned)caps[i]) == 0,
                          "no two capabilities share a bit");
        seen |= (unsigned)caps[i];
    }
    TEST_PASS();

    TEST_START("capabilities OR-combine and remain individually testable");
    /// A segment stores its capability set verbatim; combined flags must each
    /// be recoverable by masking.
    lle_segment_capability_t combined =
        (lle_segment_capability_t)(LLE_SEG_CAP_ASYNC | LLE_SEG_CAP_CACHEABLE |
                                   LLE_SEG_CAP_THEME_AWARE);
    lle_prompt_segment_t *seg =
        lle_segment_create("caps", "capability round-trip", combined);
    COMPLIANCE_ASSERT(seg != NULL, "segment created with combined caps");
    COMPLIANCE_ASSERT(seg->capabilities == combined,
                      "capabilities stored verbatim");
    COMPLIANCE_ASSERT((seg->capabilities & LLE_SEG_CAP_ASYNC) != 0,
                      "ASYNC bit recoverable");
    COMPLIANCE_ASSERT((seg->capabilities & LLE_SEG_CAP_CACHEABLE) != 0,
                      "CACHEABLE bit recoverable");
    COMPLIANCE_ASSERT((seg->capabilities & LLE_SEG_CAP_THEME_AWARE) != 0,
                      "THEME_AWARE bit recoverable");
    COMPLIANCE_ASSERT((seg->capabilities & LLE_SEG_CAP_DYNAMIC) == 0,
                      "unset DYNAMIC bit reads as absent");
    lle_segment_free(seg);
    TEST_PASS();

    TEST_START("rendered output respects the content buffer bound");
    /// content_len must never exceed the fixed content[] capacity, and the
    /// content string must be NUL-terminated at exactly content_len.
    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    lle_segment_output_t output;
    memset(&output, 0, sizeof(output));
    dir->render(dir, &ctx, NULL, &output);
    COMPLIANCE_ASSERT(output.content_len < LLE_SEGMENT_OUTPUT_MAX,
                      "content_len stays within LLE_SEGMENT_OUTPUT_MAX");
    COMPLIANCE_ASSERT(output.content[output.content_len] == '\0',
                      "content NUL-terminated at content_len");
    COMPLIANCE_ASSERT(strlen(output.content) == output.content_len,
                      "content_len equals strlen of content");
    lle_segment_free(dir);
    TEST_PASS();

    printf("  complete (3 tests)\n\n");
}

/* ========================================================================== */
/// Test: Segment Registry API
/* ========================================================================== */

static void test_segment_registry_api(void) {
    printf("Segment Registry API\n");
    printf("-----------------------------\n");

    TEST_START("lle_segment_registry_init function");
    lle_segment_registry_t registry;
    lle_result_t result = lle_segment_registry_init(&registry);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_segment_registry_init returns LLE_SUCCESS");
    TEST_PASS();

    TEST_START("lle_segment_registry_register function");
    lle_prompt_segment_t *seg = lle_segment_create_directory();
    COMPLIANCE_ASSERT(seg != NULL, "segment creation works");
    result = lle_segment_registry_register(&registry, seg);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_segment_registry_register returns LLE_SUCCESS");
    TEST_PASS();

    TEST_START("lle_segment_registry_find function");
    lle_prompt_segment_t *found =
        lle_segment_registry_find(&registry, "directory");
    COMPLIANCE_ASSERT(found != NULL, "find returns registered segment");
    COMPLIANCE_ASSERT(found == seg, "find returns same pointer");
    TEST_PASS();

    TEST_START("lle_segment_registry_list function");
    const char *names[16];
    size_t count = lle_segment_registry_list(&registry, names, 16);
    COMPLIANCE_ASSERT(count == 1, "list returns correct count");
    COMPLIANCE_ASSERT(strcmp(names[0], "directory") == 0,
                      "list returns correct name");
    TEST_PASS();

    TEST_START("lle_segment_registry_cleanup function");
    lle_segment_registry_cleanup(&registry);
    /// No crash = success
    TEST_PASS();

    printf("  complete (5 tests)\n\n");
}

/* ========================================================================== */
/// Test: Prompt Context API
/* ========================================================================== */

static void test_prompt_context_api(void) {
    printf("Prompt Context API\n");
    printf("---------------------------\n");

    TEST_START("lle_prompt_context_init function");
    lle_prompt_context_t ctx;
    lle_result_t result = lle_prompt_context_init(&ctx);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "lle_prompt_context_init returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(strlen(ctx.username) > 0, "username populated");
    COMPLIANCE_ASSERT(strlen(ctx.cwd) > 0, "cwd populated");
    TEST_PASS();

    TEST_START("lle_prompt_context_update function");
    lle_prompt_context_update(&ctx, 42, 1000);
    COMPLIANCE_ASSERT(ctx.last_exit_code == 42, "exit code updated");
    COMPLIANCE_ASSERT(ctx.last_cmd_duration_ms == 1000, "duration updated");
    TEST_PASS();

    TEST_START("lle_prompt_context_refresh_directory function");
    result = lle_prompt_context_refresh_directory(&ctx);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS,
                      "refresh_directory returns LLE_SUCCESS");
    TEST_PASS();

    printf("  complete (3 tests)\n\n");
}

/* ========================================================================== */
/// Test: Segment Lifecycle API
/* ========================================================================== */

static void test_segment_lifecycle_api(void) {
    printf("Segment Lifecycle API\n");
    printf("------------------------------\n");

    TEST_START("lle_segment_create function");
    lle_prompt_segment_t *seg =
        lle_segment_create("test", "Test segment", LLE_SEG_CAP_NONE);
    COMPLIANCE_ASSERT(seg != NULL, "lle_segment_create returns segment");
    COMPLIANCE_ASSERT(strcmp(seg->name, "test") == 0, "name set correctly");
    COMPLIANCE_ASSERT(seg->capabilities == LLE_SEG_CAP_NONE,
                      "capabilities set correctly");
    TEST_PASS();

    TEST_START("lle_segment_free function");
    lle_segment_free(seg);
    /// No crash = success
    TEST_PASS();

    printf("  complete (2 tests)\n\n");
}

/* ========================================================================== */
/// Test: Built-in Segment Factories
/* ========================================================================== */

static void test_builtin_segment_factories(void) {
    printf("Built-in Segment Factories\n");
    printf("------------------------------------\n");

    TEST_START("lle_segment_create_directory");
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    COMPLIANCE_ASSERT(dir != NULL, "creates directory segment");
    COMPLIANCE_ASSERT(strcmp(dir->name, "directory") == 0, "correct name");
    COMPLIANCE_ASSERT(dir->render != NULL, "has render callback");
    lle_segment_free(dir);
    TEST_PASS();

    TEST_START("lle_segment_create_user");
    lle_prompt_segment_t *user = lle_segment_create_user();
    COMPLIANCE_ASSERT(user != NULL, "creates user segment");
    COMPLIANCE_ASSERT(strcmp(user->name, "user") == 0, "correct name");
    lle_segment_free(user);
    TEST_PASS();

    TEST_START("lle_segment_create_host");
    lle_prompt_segment_t *host = lle_segment_create_host();
    COMPLIANCE_ASSERT(host != NULL, "creates host segment");
    COMPLIANCE_ASSERT(strcmp(host->name, "host") == 0, "correct name");
    lle_segment_free(host);
    TEST_PASS();

    TEST_START("lle_segment_create_time");
    lle_prompt_segment_t *time_seg = lle_segment_create_time();
    COMPLIANCE_ASSERT(time_seg != NULL, "creates time segment");
    COMPLIANCE_ASSERT(strcmp(time_seg->name, "time") == 0, "correct name");
    lle_segment_free(time_seg);
    TEST_PASS();

    TEST_START("lle_segment_create_status");
    lle_prompt_segment_t *status = lle_segment_create_status();
    COMPLIANCE_ASSERT(status != NULL, "creates status segment");
    COMPLIANCE_ASSERT(strcmp(status->name, "status") == 0, "correct name");
    lle_segment_free(status);
    TEST_PASS();

    TEST_START("lle_segment_create_jobs");
    lle_prompt_segment_t *jobs = lle_segment_create_jobs();
    COMPLIANCE_ASSERT(jobs != NULL, "creates jobs segment");
    COMPLIANCE_ASSERT(strcmp(jobs->name, "jobs") == 0, "correct name");
    lle_segment_free(jobs);
    TEST_PASS();

    TEST_START("lle_segment_create_symbol");
    lle_prompt_segment_t *symbol = lle_segment_create_symbol();
    COMPLIANCE_ASSERT(symbol != NULL, "creates symbol segment");
    COMPLIANCE_ASSERT(strcmp(symbol->name, "symbol") == 0, "correct name");
    lle_segment_free(symbol);
    TEST_PASS();

    TEST_START("lle_segment_create_git");
    lle_prompt_segment_t *git = lle_segment_create_git();
    COMPLIANCE_ASSERT(git != NULL, "creates git segment");
    COMPLIANCE_ASSERT(strcmp(git->name, "git") == 0, "correct name");
    lle_segment_free(git);
    TEST_PASS();

    printf("  complete (8 tests)\n\n");
}

/* ========================================================================== */
/// Test: Segment Callback Interface
/* ========================================================================== */

static void test_segment_callback_interface(void) {
    printf("Segment Callback Interface\n");
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
    /// Status segment visibility depends on last_exit_code != 0
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
    /// Property access without state returns NULL
    const char *branch = git->get_property(git, "branch");
    /// NULL is acceptable when not in git repo
    (void)branch;
    lle_segment_free(git);
    TEST_PASS();

    TEST_START("segment invalidate_cache callback");
    lle_prompt_segment_t *dir2 = lle_segment_create_directory();
    COMPLIANCE_ASSERT(dir2->invalidate_cache != NULL,
                      "invalidate_cache callback exists");
    dir2->invalidate_cache(dir2);
    /// No crash = success
    lle_segment_free(dir2);
    TEST_PASS();

    printf("  complete (4 tests)\n\n");
}

/* ========================================================================== */
/// Test: Segment Output Specification
/* ========================================================================== */

static void test_segment_output_specification(void) {
    printf("Segment Output Specification\n");
    printf("--------------------------------------\n");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    lle_segment_output_t output;

    TEST_START("directory segment output format");
    lle_prompt_segment_t *dir = lle_segment_create_directory();
    memset(&output, 0, sizeof(output));
    dir->render(dir, &ctx, NULL, &output);
    /// Per spec: directory uses ~ for home abbreviation
    /// Content should be non-empty
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
    /// Per spec: $ for user, # for root
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
    /// Per spec: HH:MM:SS format = 8 characters
    COMPLIANCE_ASSERT(output.content_len == 8, "time is HH:MM:SS format");
    COMPLIANCE_ASSERT(output.content[2] == ':', "first colon at position 2");
    COMPLIANCE_ASSERT(output.content[5] == ':', "second colon at position 5");
    lle_segment_free(time_seg);
    TEST_PASS();

    printf("  complete (4 tests)\n\n");
}

/* ========================================================================== */
/// Test: Registry Builtins Helper
/* ========================================================================== */

static void test_register_builtins(void) {
    printf("Built-in Registration\n");
    printf("-------------------------------\n");

    TEST_START("lle_segment_register_builtins function");
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
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "cmd_duration") !=
                          NULL,
                      "cmd_duration segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "virtualenv") !=
                          NULL,
                      "virtualenv segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "container") != NULL,
                      "container segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "aws") != NULL,
                      "aws segment registered");
    COMPLIANCE_ASSERT(lle_segment_registry_find(&registry, "kubernetes") !=
                          NULL,
                      "kubernetes segment registered");
    lle_segment_registry_cleanup(&registry);
    TEST_PASS();

    printf("  complete (1 test)\n\n");
}

/* ========================================================================== */
/// Main
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
