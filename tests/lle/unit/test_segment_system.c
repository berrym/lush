/**
 * Unit tests for LLE Segment System (Spec 25 Section 5)
 *
 * Tests segment registry, prompt context, and built-in segments.
 */

#include "lle/error_handling.h"
#include "lle/prompt/segment.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wrap older 2-arg / no-arg ASSERT forms over the shared framework so
 * the test bodies don't need editing. PASS() is now a no-op. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define PASS() ((void)0)

/* ========================================================================== */
/* Registry Tests                                                             */
/* ========================================================================== */

TEST(registry_init) {
    lle_segment_registry_t registry;
    lle_result_t result = lle_segment_registry_init(&registry);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(registry.initialized);
    ASSERT_EQ(registry.count, 0);
    lle_segment_registry_cleanup(&registry);
    PASS();
}

TEST(registry_init_null) {
    lle_result_t result = lle_segment_registry_init(NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
    PASS();
}

TEST(registry_register_segment) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    lle_prompt_segment_t *seg = lle_segment_create("test", "Test segment", 0);
    ASSERT(seg != NULL);

    lle_result_t result = lle_segment_registry_register(&registry, seg);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_EQ(registry.count, 1);

    lle_segment_registry_cleanup(&registry);
    PASS();
}

TEST(registry_find_segment) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    lle_prompt_segment_t *seg = lle_segment_create("findme", "Test", 0);
    lle_segment_registry_register(&registry, seg);

    lle_prompt_segment_t *found =
        lle_segment_registry_find(&registry, "findme");
    ASSERT(found != NULL);
    ASSERT_STR_EQ(found->name, "findme");

    found = lle_segment_registry_find(&registry, "notfound");
    ASSERT(found == NULL);

    lle_segment_registry_cleanup(&registry);
    PASS();
}

TEST(registry_duplicate_name_rejected) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    lle_prompt_segment_t *seg1 = lle_segment_create("duplicate", "First", 0);
    lle_prompt_segment_t *seg2 = lle_segment_create("duplicate", "Second", 0);

    ASSERT_EQ(lle_segment_registry_register(&registry, seg1), LLE_SUCCESS);
    ASSERT_EQ(lle_segment_registry_register(&registry, seg2),
              LLE_ERROR_INVALID_STATE);

    lle_segment_free(seg2); /* Not registered, must free manually */
    lle_segment_registry_cleanup(&registry);
    PASS();
}

TEST(registry_list_segments) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    lle_segment_registry_register(&registry, lle_segment_create("a", "", 0));
    lle_segment_registry_register(&registry, lle_segment_create("b", "", 0));
    lle_segment_registry_register(&registry, lle_segment_create("c", "", 0));

    const char *names[10];
    size_t count = lle_segment_registry_list(&registry, names, 10);
    ASSERT_EQ(count, 3);
    ASSERT_STR_EQ(names[0], "a");
    ASSERT_STR_EQ(names[1], "b");
    ASSERT_STR_EQ(names[2], "c");

    lle_segment_registry_cleanup(&registry);
    PASS();
}

/* ========================================================================== */
/* Prompt Context Tests                                                       */
/* ========================================================================== */

TEST(context_init) {
    lle_prompt_context_t ctx;
    lle_result_t result = lle_prompt_context_init(&ctx);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(ctx.username) > 0);
    ASSERT(strlen(ctx.hostname) > 0);
    ASSERT(strlen(ctx.cwd) > 0);
    ASSERT(strlen(ctx.home_dir) > 0);
    PASS();
}

TEST(context_init_null) {
    lle_result_t result = lle_prompt_context_init(NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
    PASS();
}

TEST(context_update) {
    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    ctx.last_exit_code = 0;
    ctx.last_cmd_duration_ms = 0;

    lle_prompt_context_update(&ctx, 42, 12345);

    ASSERT_EQ(ctx.last_exit_code, 42);
    ASSERT_EQ(ctx.last_cmd_duration_ms, 12345);
    PASS();
}

TEST(context_cwd_display_home) {
    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    /* The cwd_display should contain ~ if we're under home */
    if (strncmp(ctx.cwd, ctx.home_dir, strlen(ctx.home_dir)) == 0) {
        ASSERT(ctx.cwd_display[0] == '~');
    }
    PASS();
}

/* ========================================================================== */
/* Segment Creation Tests                                                     */
/* ========================================================================== */

TEST(segment_create) {
    lle_prompt_segment_t *seg =
        lle_segment_create("test_seg", "A test segment", LLE_SEG_CAP_CACHEABLE);

    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "test_seg");
    ASSERT_STR_EQ(seg->description, "A test segment");
    ASSERT_EQ(seg->capabilities, LLE_SEG_CAP_CACHEABLE);

    lle_segment_free(seg);
    PASS();
}

TEST(segment_create_null_name) {
    lle_prompt_segment_t *seg = lle_segment_create(NULL, "desc", 0);
    ASSERT(seg == NULL);
    PASS();
}

/* ========================================================================== */
/* Built-in Segment Tests                                                     */
/* ========================================================================== */

TEST(builtin_directory_segment) {
    lle_prompt_segment_t *seg = lle_segment_create_directory();
    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "directory");
    ASSERT(seg->render != NULL);

    /* Initialize */
    if (seg->init) {
        ASSERT_EQ(seg->init(seg), LLE_SUCCESS);
    }

    /* Render */
    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT(!output.is_empty);
    ASSERT(strlen(output.content) > 0);

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_user_segment) {
    lle_prompt_segment_t *seg = lle_segment_create_user();
    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "user");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, ctx.username);

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_host_segment) {
    lle_prompt_segment_t *seg = lle_segment_create_host();
    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "host");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, ctx.hostname);

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_time_segment) {
    lle_prompt_segment_t *seg = lle_segment_create_time();
    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "time");

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT(!output.is_empty);
    /* Time format: HH:MM:SS */
    ASSERT_EQ(strlen(output.content), 8);
    ASSERT(output.content[2] == ':');
    ASSERT(output.content[5] == ':');

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_status_segment_zero) {
    lle_prompt_segment_t *seg = lle_segment_create_status();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.last_exit_code = 0;

    /* Status segment hidden when exit code is 0 */
    ASSERT(!seg->is_visible(seg, &ctx));

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_status_segment_nonzero) {
    lle_prompt_segment_t *seg = lle_segment_create_status();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.last_exit_code = 127;

    ASSERT(seg->is_visible(seg, &ctx));

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, "127");

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_symbol_segment_user) {
    lle_prompt_segment_t *seg = lle_segment_create_symbol();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.is_root = false;

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, "$");

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_symbol_segment_root) {
    lle_prompt_segment_t *seg = lle_segment_create_symbol();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.is_root = true;

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, "#");

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_jobs_segment_none) {
    lle_prompt_segment_t *seg = lle_segment_create_jobs();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.background_job_count = 0;

    ASSERT(!seg->is_visible(seg, &ctx));

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_jobs_segment_some) {
    lle_prompt_segment_t *seg = lle_segment_create_jobs();
    ASSERT(seg != NULL);

    lle_prompt_context_t ctx;
    lle_prompt_context_init(&ctx);
    ctx.background_job_count = 3;

    ASSERT(seg->is_visible(seg, &ctx));

    lle_segment_output_t output = {0};
    ASSERT_EQ(seg->render(seg, &ctx, NULL, &output), LLE_SUCCESS);
    ASSERT_STR_EQ(output.content, "3");

    lle_segment_free(seg);
    PASS();
}

TEST(builtin_git_segment) {
    lle_prompt_segment_t *seg = lle_segment_create_git();
    ASSERT(seg != NULL);
    ASSERT_STR_EQ(seg->name, "git");
    ASSERT(seg->capabilities & LLE_SEG_CAP_ASYNC);
    ASSERT(seg->capabilities & LLE_SEG_CAP_CACHEABLE);

    /* Initialize */
    if (seg->init) {
        ASSERT_EQ(seg->init(seg), LLE_SUCCESS);
    }

    lle_segment_free(seg);
    PASS();
}

TEST(register_builtins) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    size_t count = lle_segment_register_builtins(&registry);
    ASSERT(count >= 7); /* At least 7 built-in segments */
    ASSERT_EQ(registry.count, count);

    /* Verify some key segments exist */
    ASSERT(lle_segment_registry_find(&registry, "directory") != NULL);
    ASSERT(lle_segment_registry_find(&registry, "user") != NULL);
    ASSERT(lle_segment_registry_find(&registry, "host") != NULL);
    ASSERT(lle_segment_registry_find(&registry, "git") != NULL);
    ASSERT(lle_segment_registry_find(&registry, "symbol") != NULL);

    lle_segment_registry_cleanup(&registry);
    PASS();
}

TEST(invalidate_all_caches) {
    lle_segment_registry_t registry;
    lle_segment_registry_init(&registry);

    lle_segment_register_builtins(&registry);

    /* Should not crash */
    lle_segment_registry_invalidate_all(&registry);

    lle_segment_registry_cleanup(&registry);
    PASS();
}

/* ========================================================================== */
/* Main test runner                                                           */
/* ========================================================================== */

int main(void) {
    printf("===========================================\n");
    printf("    LLE Segment System Unit Tests\n");
    printf("===========================================\n\n");

    /* Registry tests */
    RUN_TEST(registry_init);
    RUN_TEST(registry_init_null);
    RUN_TEST(registry_register_segment);
    RUN_TEST(registry_find_segment);
    RUN_TEST(registry_duplicate_name_rejected);
    RUN_TEST(registry_list_segments);

    /* Context tests */
    RUN_TEST(context_init);
    RUN_TEST(context_init_null);
    RUN_TEST(context_update);
    RUN_TEST(context_cwd_display_home);

    /* Segment creation tests */
    RUN_TEST(segment_create);
    RUN_TEST(segment_create_null_name);

    /* Built-in segment tests */
    RUN_TEST(builtin_directory_segment);
    RUN_TEST(builtin_user_segment);
    RUN_TEST(builtin_host_segment);
    RUN_TEST(builtin_time_segment);
    RUN_TEST(builtin_status_segment_zero);
    RUN_TEST(builtin_status_segment_nonzero);
    RUN_TEST(builtin_symbol_segment_user);
    RUN_TEST(builtin_symbol_segment_root);
    RUN_TEST(builtin_jobs_segment_none);
    RUN_TEST(builtin_jobs_segment_some);
    RUN_TEST(builtin_git_segment);
    RUN_TEST(register_builtins);
    RUN_TEST(invalidate_all_caches);

    return TEST_RESULT();
}
