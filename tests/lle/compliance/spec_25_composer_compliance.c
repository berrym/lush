/**
 * @file spec_25_composer_compliance.c
 * @brief Spec 25 Prompt Composer Compliance Tests
 *
 * Verifies that the prompt composer implementation conforms to
 * Spec 25 requirements for template/segment/theme integration.
 *
 * Test Phases:
 * 1. Composer Lifecycle - Init, cleanup, configuration
 * 2. Template Integration - Segment rendering via templates
 * 3. Theme Integration - Color and symbol application
 * 4. Context Management - Exit code, duration, directory tracking
 * 5. Render Output - PS1, PS2, RPROMPT generation
 * 6. Error Handling - Invalid inputs, edge cases
 */

#include "lle/error_handling.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/template.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
// Test Infrastructure
/* ========================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int current_phase = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        tests_run++;                                                           \
        printf("    [%d] %s... ", tests_run, #name);                           \
        test_##name();                                                         \
        tests_passed++;                                                        \
        printf("PASS\n");                                                      \
    } while (0)

#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL\n      Assertion failed: %s\n      at %s:%d\n",       \
                   #cond, __FILE__, __LINE__);                                 \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_NULL(a) ASSERT((a) == NULL)
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_TRUE(a) ASSERT((a))
#define ASSERT_FALSE(a) ASSERT(!(a))

#define BEGIN_PHASE(num, name)                                                 \
    do {                                                                       \
        current_phase = num;                                                   \
        printf("\n  Phase %d: %s\n", num, name);                               \
    } while (0)

/* ========================================================================== */
// Test Fixtures
/* ========================================================================== */

static lle_prompt_composer_t g_composer;
static lle_segment_registry_t g_segments;
static lle_theme_registry_t g_themes;

static void setup_full_composer(void) {
    memset(&g_composer, 0, sizeof(g_composer));
    memset(&g_segments, 0, sizeof(g_segments));
    memset(&g_themes, 0, sizeof(g_themes));

    lle_segment_registry_init(&g_segments);
    lle_theme_registry_init(&g_themes);

    lle_segment_register_builtins(&g_segments);
    lle_theme_register_builtins(&g_themes);

    lle_composer_init(&g_composer, &g_segments, &g_themes);
}

static void teardown_full_composer(void) {
    lle_composer_cleanup(&g_composer);
    lle_segment_registry_cleanup(&g_segments);
    lle_theme_registry_cleanup(&g_themes);
}

/* ========================================================================== */
// Phase 1: Composer Lifecycle Compliance
/* ========================================================================== */

TEST(spec25_composer_init_returns_success) {
    lle_prompt_composer_t composer;
    memset(&composer, 0, sizeof(composer));

    lle_result_t result = lle_composer_init(&composer, NULL, NULL);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_TRUE(composer.initialized);

    lle_composer_cleanup(&composer);
}

TEST(spec25_composer_init_with_registries) {
    setup_full_composer();

    ASSERT_TRUE(g_composer.initialized);
    ASSERT_NOT_NULL(g_composer.segments);
    ASSERT_NOT_NULL(g_composer.themes);

    teardown_full_composer();
}

TEST(spec25_composer_cleanup_releases_resources) {
    setup_full_composer();

    lle_composer_cleanup(&g_composer);
    ASSERT_FALSE(g_composer.initialized);
    ASSERT_NULL(g_composer.cached_left_template);
    ASSERT_NULL(g_composer.cached_right_template);
    ASSERT_NULL(g_composer.cached_ps2_template);

    // Cleanup registries separately since composer doesn't own them
    lle_segment_registry_cleanup(&g_segments);
    lle_theme_registry_cleanup(&g_themes);
}

TEST(spec25_composer_configure_sets_options) {
    setup_full_composer();

    lle_composer_config_t config = {.enable_right_prompt = true,
                                    .enable_transient = true,
                                    .respect_user_ps1 = true,
                                    .use_external_prompt = false};

    lle_result_t result = lle_composer_configure(&g_composer, &config);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_TRUE(g_composer.config.enable_right_prompt);
    ASSERT_TRUE(g_composer.config.enable_transient);
    ASSERT_TRUE(g_composer.config.respect_user_ps1);

    teardown_full_composer();
}

/* ========================================================================== */
// Phase 2: Template Integration Compliance
/* ========================================================================== */

TEST(spec25_composer_renders_literal_templates) {
    setup_full_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "literal text", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "literal text");

    teardown_full_composer();
}

TEST(spec25_composer_renders_segment_tokens) {
    setup_full_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(&g_composer, "${user}",
                                                       output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(output) > 0);

    teardown_full_composer();
}

TEST(spec25_composer_renders_directory_segment) {
    setup_full_composer();
    lle_composer_refresh_directory(&g_composer);

    char output[512];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${directory}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(output) > 0);

    teardown_full_composer();
}

TEST(spec25_composer_renders_conditionals) {
    setup_full_composer();

    char output[256];
    // User segment is always visible
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${?user:visible:hidden}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "visible");

    teardown_full_composer();
}

TEST(spec25_composer_handles_unknown_segments) {
    setup_full_composer();

    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "before ${unknown} after", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    // Unknown segments render as empty
    ASSERT_STR_EQ(output, "before  after");

    teardown_full_composer();
}

TEST(spec25_composer_renders_multiple_segments) {
    setup_full_composer();
    lle_composer_refresh_directory(&g_composer);

    char output[512];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${user}@${host}:${directory}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(output) > 0);
    ASSERT(strstr(output, "@") != NULL);
    ASSERT(strstr(output, ":") != NULL);

    teardown_full_composer();
}

/* ========================================================================== */
// Phase 3: Theme Integration Compliance
/* ========================================================================== */

TEST(spec25_composer_uses_active_theme) {
    setup_full_composer();

    lle_result_t result = lle_composer_set_theme(&g_composer, "default");
    ASSERT_EQ(result, LLE_SUCCESS);

    const lle_theme_t *theme = lle_composer_get_theme(&g_composer);
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "default");

    teardown_full_composer();
}

TEST(spec25_composer_theme_switching) {
    setup_full_composer();

    const char *themes[] = {"minimal",   "default",     "classic",
                            "powerline", "informative", "two-line"};
    size_t count = sizeof(themes) / sizeof(themes[0]);

    for (size_t i = 0; i < count; i++) {
        lle_result_t result = lle_composer_set_theme(&g_composer, themes[i]);
        ASSERT_EQ(result, LLE_SUCCESS);

        const lle_theme_t *active = lle_composer_get_theme(&g_composer);
        ASSERT_NOT_NULL(active);
        ASSERT_STR_EQ(active->name, themes[i]);
    }

    teardown_full_composer();
}

TEST(spec25_composer_theme_affects_output) {
    setup_full_composer();

    lle_prompt_output_t out_minimal, out_default;

    lle_composer_set_theme(&g_composer, "minimal");
    lle_composer_render(&g_composer, &out_minimal);

    lle_composer_set_theme(&g_composer, "default");
    lle_composer_render(&g_composer, &out_default);

    // Different themes should produce different output
    int different = (out_minimal.ps1_len != out_default.ps1_len ||
                     strcmp(out_minimal.ps1, out_default.ps1) != 0);
    ASSERT_TRUE(different);

    teardown_full_composer();
}

TEST(spec25_composer_color_rendering) {
    setup_full_composer();
    lle_composer_set_theme(&g_composer, "default");

    char output[256];
    lle_result_t result = lle_composer_render_template(
        &g_composer, "${primary:colored}", output, sizeof(output));

    ASSERT_EQ(result, LLE_SUCCESS);
    // Should contain "colored" and ANSI escape sequences
    ASSERT(strstr(output, "colored") != NULL);

    teardown_full_composer();
}

TEST(spec25_composer_invalid_theme_rejected) {
    setup_full_composer();

    lle_result_t result = lle_composer_set_theme(&g_composer, "nonexistent");
    ASSERT_NE(result, LLE_SUCCESS);

    teardown_full_composer();
}

/* ========================================================================== */
// Phase 4: Context Management Compliance
/* ========================================================================== */

TEST(spec25_composer_tracks_exit_code) {
    setup_full_composer();

    lle_composer_update_context(&g_composer, 0, 100);
    ASSERT_EQ(g_composer.context.last_exit_code, 0);

    lle_composer_update_context(&g_composer, 127, 200);
    ASSERT_EQ(g_composer.context.last_exit_code, 127);

    teardown_full_composer();
}

TEST(spec25_composer_tracks_duration) {
    setup_full_composer();

    lle_composer_update_context(&g_composer, 0, 1500);
    ASSERT_EQ(g_composer.context.last_cmd_duration_ms, 1500);

    teardown_full_composer();
}

TEST(spec25_composer_refreshes_directory) {
    setup_full_composer();

    lle_result_t result = lle_composer_refresh_directory(&g_composer);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strlen(g_composer.context.cwd) > 0);

    teardown_full_composer();
}

TEST(spec25_composer_invalidates_caches) {
    setup_full_composer();

    // Should not crash and should invalidate segment caches
    lle_composer_invalidate_caches(&g_composer);

    teardown_full_composer();
}

/* ========================================================================== */
// Phase 5: Render Output Compliance
/* ========================================================================== */

TEST(spec25_composer_generates_ps1) {
    setup_full_composer();

    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&g_composer, &output);

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(output.ps1_len > 0);
    ASSERT(output.ps1[0] != '\0');

    teardown_full_composer();
}

TEST(spec25_composer_generates_ps2) {
    setup_full_composer();

    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&g_composer, &output);

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(output.ps2_len > 0);
    ASSERT(output.ps2[0] != '\0');

    teardown_full_composer();
}

TEST(spec25_composer_calculates_visual_width) {
    setup_full_composer();

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);

    // Visual width should be calculated
    ASSERT(output.ps1_visual_width > 0);
    ASSERT(output.ps2_visual_width > 0);

    // Visual width should be <= byte length (ANSI codes don't count)
    ASSERT(output.ps1_visual_width <= output.ps1_len);

    teardown_full_composer();
}

TEST(spec25_composer_tracks_render_count) {
    setup_full_composer();

    ASSERT_EQ(g_composer.total_renders, 0);

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 1);

    lle_composer_render(&g_composer, &output);
    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 3);

    teardown_full_composer();
}

TEST(spec25_composer_detects_multiline) {
    setup_full_composer();

    // Set two-line theme which uses newlines
    lle_composer_set_theme(&g_composer, "two-line");

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);

    // Two-line theme should produce multiline PS1
    ASSERT_TRUE(output.is_multiline);

    teardown_full_composer();
}

/* ========================================================================== */
// Phase 6: Error Handling Compliance
/* ========================================================================== */

TEST(spec25_composer_rejects_null_composer) {
    lle_result_t result = lle_composer_init(NULL, NULL, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
}

TEST(spec25_composer_rejects_null_output) {
    setup_full_composer();

    lle_result_t result = lle_composer_render(&g_composer, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);

    teardown_full_composer();
}

TEST(spec25_composer_rejects_uninitialized) {
    lle_prompt_composer_t composer;
    memset(&composer, 0, sizeof(composer));

    lle_prompt_output_t output;
    lle_result_t result = lle_composer_render(&composer, &output);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
}

TEST(spec25_composer_handles_null_cleanup) {
    // Should not crash
    lle_composer_cleanup(NULL);
}

TEST(spec25_composer_handles_null_invalidate) {
    // Should not crash
    lle_composer_invalidate_caches(NULL);
}

TEST(spec25_composer_render_ctx_null_safe) {
    lle_template_render_ctx_t ctx = lle_composer_create_render_ctx(NULL);

    ASSERT_NULL(ctx.get_segment);
    ASSERT_NULL(ctx.is_visible);
    ASSERT_NULL(ctx.get_color);
    ASSERT_NULL(ctx.user_data);
}

/* ========================================================================== */
// Main Test Runner
/* ========================================================================== */

int main(void) {
    printf("=== Spec 25 Prompt Composer Compliance Tests ===\n");

    // Phase 1: Composer Lifecycle
    BEGIN_PHASE(1, "Composer Lifecycle Compliance");
    RUN_TEST(spec25_composer_init_returns_success);
    RUN_TEST(spec25_composer_init_with_registries);
    RUN_TEST(spec25_composer_cleanup_releases_resources);
    RUN_TEST(spec25_composer_configure_sets_options);

    // Phase 2: Template Integration
    BEGIN_PHASE(2, "Template Integration Compliance");
    RUN_TEST(spec25_composer_renders_literal_templates);
    RUN_TEST(spec25_composer_renders_segment_tokens);
    RUN_TEST(spec25_composer_renders_directory_segment);
    RUN_TEST(spec25_composer_renders_conditionals);
    RUN_TEST(spec25_composer_handles_unknown_segments);
    RUN_TEST(spec25_composer_renders_multiple_segments);

    // Phase 3: Theme Integration
    BEGIN_PHASE(3, "Theme Integration Compliance");
    RUN_TEST(spec25_composer_uses_active_theme);
    RUN_TEST(spec25_composer_theme_switching);
    RUN_TEST(spec25_composer_theme_affects_output);
    RUN_TEST(spec25_composer_color_rendering);
    RUN_TEST(spec25_composer_invalid_theme_rejected);

    // Phase 4: Context Management
    BEGIN_PHASE(4, "Context Management Compliance");
    RUN_TEST(spec25_composer_tracks_exit_code);
    RUN_TEST(spec25_composer_tracks_duration);
    RUN_TEST(spec25_composer_refreshes_directory);
    RUN_TEST(spec25_composer_invalidates_caches);

    // Phase 5: Render Output
    BEGIN_PHASE(5, "Render Output Compliance");
    RUN_TEST(spec25_composer_generates_ps1);
    RUN_TEST(spec25_composer_generates_ps2);
    RUN_TEST(spec25_composer_calculates_visual_width);
    RUN_TEST(spec25_composer_tracks_render_count);
    RUN_TEST(spec25_composer_detects_multiline);

    // Phase 6: Error Handling
    BEGIN_PHASE(6, "Error Handling Compliance");
    RUN_TEST(spec25_composer_rejects_null_composer);
    RUN_TEST(spec25_composer_rejects_null_output);
    RUN_TEST(spec25_composer_rejects_uninitialized);
    RUN_TEST(spec25_composer_handles_null_cleanup);
    RUN_TEST(spec25_composer_handles_null_invalidate);
    RUN_TEST(spec25_composer_render_ctx_null_safe);

    printf("\n=== Compliance Results: %d/%d tests passed ===\n", tests_passed,
           tests_run);

    if (tests_passed == tests_run) {
        printf("=== Spec 25 Composer Implementation: COMPLIANT ===\n");
    } else {
        printf("=== Spec 25 Composer Implementation: NON-COMPLIANT ===\n");
    }

    return (tests_passed == tests_run) ? 0 : 1;
}
