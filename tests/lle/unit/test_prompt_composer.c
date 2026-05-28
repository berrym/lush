/**
 * @file test_prompt_composer.c
 * @brief Unit tests for LLE Prompt Composer
 *
 * Tests the integration of template engine, segment registry, and theme
 * registry through the prompt composer.
 */

#include "lle/error_handling.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/template.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"

/* Local 1-arg ASSERT family (cond only — no message) wrapping the
 * framework's failure mechanism, replacing the historical exit(1)
 * pattern with longjmp-based isolation per RUN_TEST. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_NE
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#undef ASSERT_TRUE
#undef ASSERT_FALSE
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_TRUE(a) ASSERT((a))
#define ASSERT_FALSE(a) ASSERT(!(a))

/* ========================================================================== */
/// Test Fixtures
/* ========================================================================== */

static lle_prompt_composer_t g_composer;
static lle_segment_registry_t g_segments;
static lle_theme_registry_t g_themes;

static void setup_composer(void) {
    memset(&g_composer, 0, sizeof(g_composer));
    memset(&g_segments, 0, sizeof(g_segments));
    memset(&g_themes, 0, sizeof(g_themes));

    lle_segment_registry_init(&g_segments);
    lle_theme_registry_init(&g_themes);

    lle_segment_register_builtins(&g_segments);
    lle_theme_register_builtins(&g_themes);

    lle_composer_init(&g_composer, &g_segments, &g_themes);
}

static void teardown_composer(void) {
    lle_composer_cleanup(&g_composer);
    lle_segment_registry_cleanup(&g_segments);
    lle_theme_registry_cleanup(&g_themes);
}

/* ========================================================================== */
/// Composer Lifecycle Tests
/* ========================================================================== */

TEST(composer_init_basic) {
    lle_prompt_composer_t composer;
    memset(&composer, 0, sizeof(composer));

    lle_result_t result = lle_composer_init(&composer, NULL, NULL);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_TRUE(composer.initialized);

    lle_composer_cleanup(&composer);
}

TEST(composer_init_with_registries) {
    setup_composer();

    ASSERT_TRUE(g_composer.initialized);
    ASSERT_EQ(g_composer.segments, &g_segments);
    ASSERT_EQ(g_composer.themes, &g_themes);

    teardown_composer();
}

TEST(composer_init_null) {
    lle_result_t result = lle_composer_init(NULL, NULL, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
}

TEST(composer_cleanup_null) {
    /// Should not crash
    lle_composer_cleanup(NULL);
}

TEST(composer_configure) {
    setup_composer();

    lle_composer_config_t config = {.enable_right_prompt = true,
                                    .enable_transient = true,
                                    .respect_user_ps1 = false,
                                    .use_external_prompt = false};

    lle_result_t result = lle_composer_configure(&g_composer, &config);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_TRUE(g_composer.config.enable_right_prompt);
    ASSERT_TRUE(g_composer.config.enable_transient);

    teardown_composer();
}

/* ========================================================================== */
/// Prompt Rendering Tests
/* ========================================================================== */

TEST(composer_render_basic) {
    setup_composer();

    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&g_composer, &output);

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(output.ps1_len > 0);
    ASSERT(output.ps2_len > 0);

    teardown_composer();
}

TEST(composer_render_null) {
    setup_composer();

    lle_result_t result = lle_composer_render(&g_composer, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);

    result = lle_composer_render(NULL, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);

    teardown_composer();
}

TEST(composer_render_uninitialized) {
    lle_prompt_composer_t composer;
    memset(&composer, 0, sizeof(composer));

    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&composer, &output);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
}

TEST(composer_render_template_simple) {
    setup_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "Hello World", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "Hello World");

    teardown_composer();
}

TEST(composer_render_template_with_segment) {
    setup_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(&g_composer, "${user}",
                                                       output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(output) > 0);

    teardown_composer();
}

TEST(composer_render_template_conditional) {
    setup_composer();

    char output[256];
    /// User segment should always be visible
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${?user:yes:no}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "yes");

    teardown_composer();
}

TEST(composer_render_template_unknown_segment) {
    setup_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "prefix ${nonexistent} suffix", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    /// Unknown segment should be omitted
    ASSERT_STR_EQ(output, "prefix  suffix");

    teardown_composer();
}

/* ========================================================================== */
/// Context Management Tests
/* ========================================================================== */

TEST(composer_update_context) {
    setup_composer();

    lle_result_t result = lle_composer_update_context(&g_composer, 0, 100);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_EQ(g_composer.context.last_exit_code, 0);
    ASSERT_EQ(g_composer.context.last_cmd_duration_ms, 100);

    result = lle_composer_update_context(&g_composer, 1, 500);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_EQ(g_composer.context.last_exit_code, 1);

    teardown_composer();
}

TEST(composer_refresh_directory) {
    setup_composer();

    lle_result_t result = lle_composer_refresh_directory(&g_composer);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(g_composer.context.cwd) > 0);

    teardown_composer();
}

TEST(composer_invalidate_caches) {
    setup_composer();

    /// Should not crash
    lle_composer_invalidate_caches(&g_composer);
    lle_composer_invalidate_caches(NULL);

    teardown_composer();
}

/* ========================================================================== */
/// Theme Integration Tests
/* ========================================================================== */

TEST(composer_set_theme) {
    setup_composer();

    lle_result_t result = lle_composer_set_theme(&g_composer, "default");
    ASSERT_EQ(result, LLE_SUCCESS);

    const lle_theme_t *theme = lle_composer_get_theme(&g_composer);
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "default");

    teardown_composer();
}

TEST(composer_set_theme_invalid) {
    setup_composer();

    lle_result_t result = lle_composer_set_theme(&g_composer, "nonexistent");
    ASSERT_NE(result, LLE_SUCCESS);

    teardown_composer();
}

TEST(composer_get_theme_null) {
    const lle_theme_t *theme = lle_composer_get_theme(NULL);
    ASSERT_NULL(theme);
}

TEST(composer_theme_affects_render) {
    setup_composer();

    lle_prompt_output_t output1, output2;

    /// Render with minimal theme
    lle_composer_set_theme(&g_composer, "minimal");
    lle_composer_render(&g_composer, &output1);

    /// Render with default theme
    lle_composer_set_theme(&g_composer, "default");
    lle_composer_render(&g_composer, &output2);

    /// Different themes should produce different output
    int different = (output1.ps1_len != output2.ps1_len ||
                     strcmp(output1.ps1, output2.ps1) != 0);
    ASSERT_TRUE(different);

    teardown_composer();
}

/* ========================================================================== */
/// Render Context Tests
/* ========================================================================== */

TEST(composer_create_render_ctx) {
    setup_composer();

    lle_template_render_ctx_t ctx = lle_composer_create_render_ctx(&g_composer);

    ASSERT_NOT_NULL(ctx.get_segment);
    ASSERT_NOT_NULL(ctx.is_visible);
    ASSERT_NOT_NULL(ctx.get_color);
    ASSERT_NOT_NULL(ctx.user_data);

    teardown_composer();
}

TEST(composer_render_ctx_null) {
    lle_template_render_ctx_t ctx = lle_composer_create_render_ctx(NULL);

    ASSERT_NULL(ctx.get_segment);
    ASSERT_NULL(ctx.is_visible);
    ASSERT_NULL(ctx.get_color);
}

/* ========================================================================== */
/// Integration Tests
/* ========================================================================== */

TEST(composer_full_prompt_render) {
    setup_composer();

    /// Set a theme with all features
    lle_composer_set_theme(&g_composer, "informative");

    /// Update context with realistic values
    lle_composer_update_context(&g_composer, 0, 250);
    lle_composer_refresh_directory(&g_composer);

    /// Render complete prompt
    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&g_composer, &output);

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(output.ps1_len > 0);
    ASSERT(output.ps2_len > 0);

    /// Verify visual width is calculated
    ASSERT(output.ps1_visual_width > 0);
    ASSERT(output.ps2_visual_width > 0);

    teardown_composer();
}

TEST(composer_multiple_themes) {
    setup_composer();

    const char *themes[] = {"minimal",   "default",     "classic",
                            "powerline", "informative", "two-line"};
    size_t num_themes = sizeof(themes) / sizeof(themes[0]);

    for (size_t i = 0; i < num_themes; i++) {
        lle_result_t result = lle_composer_set_theme(&g_composer, themes[i]);
        ASSERT_EQ(result, LLE_SUCCESS);

        lle_prompt_output_t output;
        result = lle_composer_render(&g_composer, &output);
        ASSERT_EQ(result, LLE_SUCCESS);
        ASSERT(output.ps1_len > 0);
    }

    teardown_composer();
}

TEST(composer_segment_visibility) {
    setup_composer();

    /// Git segment should not be visible outside git repo (usually)
    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${?git:IN_GIT:NOT_GIT}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    /// Result depends on whether we're in a git repo
    ASSERT(strlen(output) > 0);

    teardown_composer();
}

TEST(composer_statistics) {
    setup_composer();

    ASSERT_EQ(g_composer.total_renders, 0);

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 1);

    lle_composer_render(&g_composer, &output);
    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 3);

    teardown_composer();
}

/* ========================================================================== */
/// Main Test Runner
/* ========================================================================== */

int main(void) {
    printf("=== LLE Prompt Composer Tests ===\n\n");

    /// Lifecycle tests
    RUN_TEST(composer_init_basic);
    RUN_TEST(composer_init_with_registries);
    RUN_TEST(composer_init_null);
    RUN_TEST(composer_cleanup_null);
    RUN_TEST(composer_configure);

    /// Rendering tests
    RUN_TEST(composer_render_basic);
    RUN_TEST(composer_render_null);
    RUN_TEST(composer_render_uninitialized);
    RUN_TEST(composer_render_template_simple);
    RUN_TEST(composer_render_template_with_segment);
    RUN_TEST(composer_render_template_conditional);
    RUN_TEST(composer_render_template_unknown_segment);

    /// Context tests
    RUN_TEST(composer_update_context);
    RUN_TEST(composer_refresh_directory);
    RUN_TEST(composer_invalidate_caches);

    /// Theme integration tests
    RUN_TEST(composer_set_theme);
    RUN_TEST(composer_set_theme_invalid);
    RUN_TEST(composer_get_theme_null);
    RUN_TEST(composer_theme_affects_render);

    /// Render context tests
    RUN_TEST(composer_create_render_ctx);
    RUN_TEST(composer_render_ctx_null);

    /// Integration tests
    RUN_TEST(composer_full_prompt_render);
    RUN_TEST(composer_multiple_themes);
    RUN_TEST(composer_segment_visibility);
    RUN_TEST(composer_statistics);

    return TEST_RESULT();
}
