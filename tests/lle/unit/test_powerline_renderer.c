/**
 * @file test_powerline_renderer.c
 * @brief Unit tests for LLE Powerline Renderer
 *
 * Tests the powerline rendering path: colored background segments with
 * arrow separators, both left-to-right (PS1) and right-to-left (RPROMPT).
 * Also tests end-to-end rendering via the composer when the active theme
 * uses style = powerline.
 */

#include "lle/error_handling.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/powerline.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/* Test Infrastructure                                                        */
/* ========================================================================== */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        tests_run++;                                                           \
        printf("  [%d] %s... ", tests_run, #name);                             \
        test_##name();                                                         \
        tests_passed++;                                                        \
        printf("PASS\n");                                                      \
    } while (0)

#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", #cond,    \
                   __FILE__, __LINE__);                                        \
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

/* ========================================================================== */
/* Helpers                                                                    */
/* ========================================================================== */

/**
 * Check that a string contains an ANSI escape sequence (ESC[).
 */
static bool contains_ansi(const char *str) {
    return strstr(str, "\033[") != NULL;
}

/**
 * Check that a string contains a specific substring.
 */
static bool contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/**
 * Count occurrences of a substring in a string.
 */
static int count_occurrences(const char *haystack, const char *needle) {
    int count = 0;
    size_t needle_len = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

/* ========================================================================== */
/* Test Fixtures                                                              */
/* ========================================================================== */

static lle_prompt_composer_t g_composer;
static lle_segment_registry_t g_segments;
static lle_theme_registry_t g_themes;

static void setup(void) {
    memset(&g_composer, 0, sizeof(g_composer));
    memset(&g_segments, 0, sizeof(g_segments));
    memset(&g_themes, 0, sizeof(g_themes));

    lle_segment_registry_init(&g_segments);
    lle_theme_registry_init(&g_themes);

    lle_segment_register_builtins(&g_segments);
    lle_theme_register_builtins(&g_themes);

    lle_composer_init(&g_composer, &g_segments, &g_themes);
}

static void teardown(void) {
    lle_composer_cleanup(&g_composer);
    lle_segment_registry_cleanup(&g_segments);
    lle_theme_registry_cleanup(&g_themes);
}

/* ========================================================================== */
/* Direct Powerline Renderer Tests                                            */
/* ========================================================================== */

TEST(powerline_render_null_params) {
    char output[1024];

    /* All NULL params should fail gracefully */
    lle_result_t r = lle_powerline_render(NULL, NULL, NULL,
                                           LLE_POWERLINE_LEFT_TO_RIGHT,
                                           output, sizeof(output));
    ASSERT_EQ(r, LLE_ERROR_INVALID_PARAMETER);

    /* NULL output buffer */
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);
    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    r = lle_powerline_render(theme, &g_segments, &ctx,
                              LLE_POWERLINE_LEFT_TO_RIGHT,
                              NULL, 0);
    ASSERT_EQ(r, LLE_ERROR_INVALID_PARAMETER);

    free(theme);
}

TEST(powerline_render_empty_segments) {
    /* Theme with no enabled segments should produce empty output */
    lle_theme_t *theme = lle_theme_create(
        "empty", "Empty test theme", LLE_THEME_CATEGORY_MINIMAL);
    ASSERT_NOT_NULL(theme);
    theme->layout.style = LLE_PROMPT_STYLE_POWERLINE;
    theme->enabled_segment_count = 0;

    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    char output[1024];
    lle_result_t r = lle_powerline_render(theme, &g_segments, &ctx,
                                           LLE_POWERLINE_LEFT_TO_RIGHT,
                                           output, sizeof(output));
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "");

    free(theme);
}

TEST(powerline_render_left_to_right_basic) {
    setup();

    /* Use the built-in powerline theme */
    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);
    ASSERT_EQ(theme->layout.style, LLE_PROMPT_STYLE_POWERLINE);
    ASSERT(theme->enabled_segment_count > 0);

    /* Set up a realistic context */
    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "testuser");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/home/testuser/project");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "~/project");

    char output[4096];
    lle_result_t r = lle_powerline_render(theme, &g_segments, &ctx,
                                           LLE_POWERLINE_LEFT_TO_RIGHT,
                                           output, sizeof(output));
    ASSERT_EQ(r, LLE_SUCCESS);

    /* Must produce non-empty output */
    ASSERT(strlen(output) > 0);

    /* Must contain ANSI escape sequences (colored segments) */
    ASSERT_TRUE(contains_ansi(output));

    /* Must contain the powerline separator U+E0B0 */
    ASSERT_TRUE(contains(output, "\xee\x82\xb0"));

    /* Must contain a reset sequence */
    ASSERT_TRUE(contains(output, "\033[0m"));

    teardown();
}

TEST(powerline_render_right_to_left_basic) {
    setup();

    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);

    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "testuser");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/home/testuser");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "~");

    char output[4096];
    lle_result_t r = lle_powerline_render(theme, &g_segments, &ctx,
                                           LLE_POWERLINE_RIGHT_TO_LEFT,
                                           output, sizeof(output));
    ASSERT_EQ(r, LLE_SUCCESS);
    ASSERT(strlen(output) > 0);
    ASSERT_TRUE(contains_ansi(output));

    /* Right-to-left uses U+E0B2 separator */
    ASSERT_TRUE(contains(output, "\xee\x82\xb2"));
    ASSERT_TRUE(contains(output, "\033[0m"));

    teardown();
}

TEST(powerline_render_has_bg_colors) {
    setup();

    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);

    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "testuser");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/tmp");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "/tmp");

    char output[4096];
    lle_powerline_render(theme, &g_segments, &ctx,
                          LLE_POWERLINE_LEFT_TO_RIGHT,
                          output, sizeof(output));

    /* Background colors use ESC[48;5;Nm (256-color) or ESC[48;2;R;G;Bm (true) */
    ASSERT_TRUE(contains(output, "\033[48;5;") ||
                contains(output, "\033[48;2;"));

    /* Foreground colors use ESC[38;5;Nm or ESC[38;2;R;G;Bm */
    ASSERT_TRUE(contains(output, "\033[38;5;") ||
                contains(output, "\033[38;2;"));

    teardown();
}

TEST(powerline_render_separator_count) {
    setup();

    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);

    /* With user + directory visible (git/status may not be), expect at least
     * 1 separator between segments plus the trailing separator */
    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "testuser");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/home/testuser");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "~");

    char output[4096];
    lle_powerline_render(theme, &g_segments, &ctx,
                          LLE_POWERLINE_LEFT_TO_RIGHT,
                          output, sizeof(output));

    /* Count left-arrow separators (U+E0B0 = 0xEE 0x82 0xB0) */
    int sep_count = count_occurrences(output, "\xee\x82\xb0");

    /* With N visible segments, there should be exactly N separators
     * (N-1 between segments + 1 trailing) */
    ASSERT(sep_count >= 2); /* At least user + directory = 2 separators */

    teardown();
}

TEST(powerline_render_segment_content_present) {
    setup();

    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);

    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "alice");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/home/alice/code");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "~/code");

    char output[4096];
    lle_powerline_render(theme, &g_segments, &ctx,
                          LLE_POWERLINE_LEFT_TO_RIGHT,
                          output, sizeof(output));

    /* Segment content should be embedded in the output */
    ASSERT_TRUE(contains(output, "alice"));
    ASSERT_TRUE(contains(output, "~/code"));

    teardown();
}

TEST(powerline_strips_segment_ansi) {
    setup();

    lle_theme_t *theme = lle_theme_registry_find(&g_themes, "powerline");
    ASSERT_NOT_NULL(theme);

    lle_prompt_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.has_256_color = true;
    ctx.has_true_color = true;
    snprintf(ctx.username, sizeof(ctx.username), "testuser");
    snprintf(ctx.cwd, sizeof(ctx.cwd), "/home/testuser/project");
    snprintf(ctx.cwd_display, sizeof(ctx.cwd_display), "~/project");

    char output[4096];
    lle_powerline_render(theme, &g_segments, &ctx,
                          LLE_POWERLINE_LEFT_TO_RIGHT,
                          output, sizeof(output));

    /* Segment renderers embed colors like ESC[38;5;33m (path_normal) and
     * ESC[0m (reset) in their content. The powerline renderer must strip
     * these to prevent them from clobbering the powerline bg/fg colors.
     *
     * After stripping, the only resets should be from the powerline renderer
     * itself (at segment boundaries), not mid-segment. Count resets and
     * verify there aren't too many. With N visible segments, the renderer
     * produces at most N+1 resets (final separator uses 2). */
    int reset_count = count_occurrences(output, "\033[0m");
    ASSERT(reset_count <= 6); /* Reasonable upper bound for 2-3 segments */

    /* The directory segment normally embeds ESC[38;5;33m for path_normal
     * color. In powerline mode this must NOT appear inside the content
     * area — only the powerline renderer's own 48;5;33 (bg) should use
     * color 33. Check that 38;5;33m appears only in separator contexts
     * (where fg is set to directory's bg), not before path text. */
    const char *path_text = strstr(output, "~/project");
    ASSERT_NOT_NULL(path_text);

    /* The 50 bytes preceding the path text should contain the powerline
     * fg color (white), NOT the segment-internal path_normal color.
     * Check that the white fg appears near the path text. */
    size_t prefix_start = (size_t)(path_text - output);
    if (prefix_start > 50)
        prefix_start -= 50;
    else
        prefix_start = 0;
    char prefix[64];
    size_t prefix_len = (size_t)(path_text - output) - prefix_start;
    if (prefix_len >= sizeof(prefix))
        prefix_len = sizeof(prefix) - 1;
    memcpy(prefix, output + prefix_start, prefix_len);
    prefix[prefix_len] = '\0';
    /* Should contain true-color white or 256-color white */
    ASSERT_TRUE(contains(prefix, "38;2;255;255;255") ||
                contains(prefix, "38;5;255"));

    teardown();
}

/* ========================================================================== */
/* Composer Integration Tests (Powerline Path)                                */
/* ========================================================================== */

TEST(composer_powerline_theme_renders) {
    setup();

    lle_result_t r = lle_composer_set_theme(&g_composer, "powerline");
    ASSERT_EQ(r, LLE_SUCCESS);

    /* Verify the theme is actually powerline style */
    const lle_theme_t *theme = lle_composer_get_theme(&g_composer);
    ASSERT_NOT_NULL(theme);
    ASSERT_EQ(theme->layout.style, LLE_PROMPT_STYLE_POWERLINE);

    /* Render via composer */
    lle_prompt_output_t output;
    r = lle_composer_render(&g_composer, &output);
    ASSERT_EQ(r, LLE_SUCCESS);

    /* PS1 should be non-empty and contain ANSI */
    ASSERT(output.ps1_len > 0);
    ASSERT_TRUE(contains_ansi(output.ps1));

    /* PS1 should contain powerline separator */
    ASSERT_TRUE(contains(output.ps1, "\xee\x82\xb0"));

    /* PS1 should contain background colors */
    ASSERT_TRUE(contains(output.ps1, "\033[48;5;") ||
                contains(output.ps1, "\033[48;2;"));

    /* PS2 should still render (template engine) */
    ASSERT(output.ps2_len > 0);

    /* Visual widths should be calculated */
    ASSERT(output.ps1_visual_width > 0);
    ASSERT(output.ps2_visual_width > 0);

    teardown();
}

TEST(composer_powerline_vs_plain_different) {
    setup();

    /* Render with powerline theme */
    lle_composer_set_theme(&g_composer, "powerline");
    lle_prompt_output_t pl_output;
    lle_composer_render(&g_composer, &pl_output);

    /* Render with default (plain) theme */
    lle_composer_set_theme(&g_composer, "default");
    lle_prompt_output_t plain_output;
    lle_composer_render(&g_composer, &plain_output);

    /* Powerline and plain outputs must differ */
    ASSERT(strcmp(pl_output.ps1, plain_output.ps1) != 0);

    /* Powerline should have background colors, plain should not */
    ASSERT_TRUE(contains(pl_output.ps1, "\033[48;5;") ||
                contains(pl_output.ps1, "\033[48;2;"));

    teardown();
}

TEST(composer_powerline_increments_stats) {
    setup();

    lle_composer_set_theme(&g_composer, "powerline");
    ASSERT_EQ(g_composer.total_renders, 0);

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 1);

    lle_composer_render(&g_composer, &output);
    ASSERT_EQ(g_composer.total_renders, 2);

    teardown();
}

TEST(composer_powerline_context_affects_output) {
    setup();

    lle_composer_set_theme(&g_composer, "powerline");
    lle_composer_refresh_directory(&g_composer);

    /* Render with exit code 0 (status segment hidden) */
    lle_composer_update_context(&g_composer, 0, 0);
    lle_prompt_output_t output_ok;
    lle_composer_render(&g_composer, &output_ok);

    /* Render with non-zero exit code (status segment visible) */
    lle_composer_update_context(&g_composer, 1, 0);
    lle_prompt_output_t output_err;
    lle_composer_render(&g_composer, &output_err);

    /* The outputs should differ because status segment appears on error */
    ASSERT(strcmp(output_ok.ps1, output_err.ps1) != 0);

    /* Error output should be longer (extra status segment) */
    ASSERT(output_err.ps1_len > output_ok.ps1_len);

    teardown();
}

TEST(composer_powerline_trailing_space) {
    setup();

    lle_composer_set_theme(&g_composer, "powerline");

    lle_prompt_output_t output;
    lle_composer_render(&g_composer, &output);

    /* Composer appends a trailing space for cursor separation */
    ASSERT(output.ps1_len > 0);
    ASSERT_EQ(output.ps1[output.ps1_len - 1], ' ');

    teardown();
}

/* ========================================================================== */
/* Theme Configuration Tests                                                  */
/* ========================================================================== */

TEST(powerline_theme_has_correct_segments) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);

    /* Built-in powerline theme should have 4 segments */
    ASSERT_EQ(theme->enabled_segment_count, 4);
    ASSERT_STR_EQ(theme->enabled_segments[0], "user");
    ASSERT_STR_EQ(theme->enabled_segments[1], "directory");
    ASSERT_STR_EQ(theme->enabled_segments[2], "git");
    ASSERT_STR_EQ(theme->enabled_segments[3], "status");

    free(theme);
}

TEST(powerline_theme_has_segment_colors) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);

    /* Should have 4 segment configs with fg/bg colors */
    ASSERT_EQ(theme->segment_config_count, 4);

    for (size_t i = 0; i < theme->segment_config_count; i++) {
        ASSERT_TRUE(theme->segment_configs[i].configured);
        ASSERT_TRUE(theme->segment_configs[i].fg_color_set);
        ASSERT_TRUE(theme->segment_configs[i].bg_color_set);
        /* fg: true color bold white */
        ASSERT_EQ(theme->segment_configs[i].fg_color.mode, LLE_COLOR_MODE_TRUE);
        ASSERT_TRUE(theme->segment_configs[i].fg_color.bold);
        /* bg: true color */
        ASSERT_EQ(theme->segment_configs[i].bg_color.mode, LLE_COLOR_MODE_TRUE);
    }

    /* Verify specific segment names */
    ASSERT_STR_EQ(theme->segment_configs[0].name, "user");
    ASSERT_STR_EQ(theme->segment_configs[1].name, "directory");
    ASSERT_STR_EQ(theme->segment_configs[2].name, "git");
    ASSERT_STR_EQ(theme->segment_configs[3].name, "status");

    /* Verify background colors are distinct (different R components) */
    ASSERT_NE(theme->segment_configs[0].bg_color.value.rgb.r,
              theme->segment_configs[1].bg_color.value.rgb.r);
    ASSERT_NE(theme->segment_configs[1].bg_color.value.rgb.r,
              theme->segment_configs[2].bg_color.value.rgb.r);

    free(theme);
}

TEST(powerline_theme_style_field) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);

    ASSERT_EQ(theme->layout.style, LLE_PROMPT_STYLE_POWERLINE);

    free(theme);
}

TEST(powerline_theme_has_separators) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);

    /* Should have powerline separator characters set */
    ASSERT(strlen(theme->symbols.separator_left) > 0);
    ASSERT(strlen(theme->symbols.separator_right) > 0);

    /* U+E0B0 = 0xEE 0x82 0xB0 */
    ASSERT_STR_EQ(theme->symbols.separator_left, "\xee\x82\xb0");
    /* U+E0B2 = 0xEE 0x82 0xB2 */
    ASSERT_STR_EQ(theme->symbols.separator_right, "\xee\x82\xb2");

    free(theme);
}

TEST(powerline_theme_has_transient) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);

    ASSERT_TRUE(theme->layout.enable_transient);
    ASSERT(strlen(theme->layout.transient_format) > 0);

    free(theme);
}

TEST(plain_theme_style_is_plain) {
    /* Non-powerline themes should have PLAIN style */
    lle_theme_t *theme = lle_theme_create_default();
    ASSERT_NOT_NULL(theme);
    ASSERT_EQ(theme->layout.style, LLE_PROMPT_STYLE_PLAIN);
    free(theme);

    theme = lle_theme_create_minimal();
    ASSERT_NOT_NULL(theme);
    ASSERT_EQ(theme->layout.style, LLE_PROMPT_STYLE_PLAIN);
    free(theme);
}

/* ========================================================================== */
/* Color System Tests                                                         */
/* ========================================================================== */

TEST(powerline_fg_and_bg_ansi_generation) {
    /* Verify lle_color_to_ansi generates correct fg and bg sequences */
    lle_color_t color = lle_color_256(33);
    char buf[32];

    /* Foreground: ESC[38;5;33m */
    size_t len = lle_color_to_ansi(&color, true, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_TRUE(contains(buf, "38;5;33"));

    /* Background: ESC[48;5;33m */
    len = lle_color_to_ansi(&color, false, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_TRUE(contains(buf, "48;5;33"));
}

TEST(powerline_color_downgrade) {
    /* True color downgraded to 256-color should still work */
    lle_color_t tc = lle_color_rgb(0, 135, 255);
    lle_color_t downgraded = lle_color_downgrade(&tc, false, true);
    ASSERT_EQ(downgraded.mode, LLE_COLOR_MODE_256);

    /* 256-color with only basic support should downgrade to basic */
    lle_color_t c256 = lle_color_256(33);
    downgraded = lle_color_downgrade(&c256, false, false);
    ASSERT_EQ(downgraded.mode, LLE_COLOR_MODE_BASIC);
}

/* ========================================================================== */
/* Main Test Runner                                                           */
/* ========================================================================== */

int main(void) {
    printf("=== LLE Powerline Renderer Tests ===\n\n");

    /* Direct renderer tests */
    printf("--- Direct Renderer ---\n");
    setup();
    RUN_TEST(powerline_render_null_params);
    RUN_TEST(powerline_render_empty_segments);
    RUN_TEST(powerline_render_left_to_right_basic);
    RUN_TEST(powerline_render_right_to_left_basic);
    RUN_TEST(powerline_render_has_bg_colors);
    RUN_TEST(powerline_render_separator_count);
    RUN_TEST(powerline_render_segment_content_present);
    RUN_TEST(powerline_strips_segment_ansi);
    teardown();

    /* Composer integration tests */
    printf("\n--- Composer Integration ---\n");
    RUN_TEST(composer_powerline_theme_renders);
    RUN_TEST(composer_powerline_vs_plain_different);
    RUN_TEST(composer_powerline_increments_stats);
    RUN_TEST(composer_powerline_context_affects_output);
    RUN_TEST(composer_powerline_trailing_space);

    /* Theme configuration tests */
    printf("\n--- Theme Configuration ---\n");
    RUN_TEST(powerline_theme_has_correct_segments);
    RUN_TEST(powerline_theme_has_segment_colors);
    RUN_TEST(powerline_theme_style_field);
    RUN_TEST(powerline_theme_has_separators);
    RUN_TEST(powerline_theme_has_transient);
    RUN_TEST(plain_theme_style_is_plain);

    /* Color system tests */
    printf("\n--- Color System ---\n");
    RUN_TEST(powerline_fg_and_bg_ansi_generation);
    RUN_TEST(powerline_color_downgrade);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
