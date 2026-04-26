/**
 * @file test_theme_registry.c
 * @brief Unit tests for LLE Theme Registry System
 *
 * Specification: Spec 25 Section 4 - Theme Registry System
 *
 * Tests theme registry operations, theme creation, inheritance,
 * color helpers, and built-in themes.
 */

#include "lle/error_handling.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"

/* Local 1-arg ASSERT family (cond only — no message) wrapping the
 * framework's failure mechanism. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_NE
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
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

/* ========================================================================== */
/* Theme Registry Tests                                                       */
/* ========================================================================== */

TEST(registry_init) {
    lle_theme_registry_t registry;
    lle_result_t result = lle_theme_registry_init(&registry);

    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(registry.initialized);
    ASSERT_EQ(registry.count, 0);
    ASSERT_NULL(registry.active_theme);
    ASSERT_STR_EQ(registry.default_theme_name, "minimal");

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_init_null) {
    lle_result_t result = lle_theme_registry_init(NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER);
}

TEST(registry_register_theme) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *theme =
        lle_theme_create("test", "Test theme", LLE_THEME_CATEGORY_CUSTOM);
    ASSERT_NOT_NULL(theme);

    lle_result_t result = lle_theme_registry_register(&registry, theme);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_EQ(registry.count, 1);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_register_duplicate) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *theme1 =
        lle_theme_create("test", "Test 1", LLE_THEME_CATEGORY_CUSTOM);
    lle_theme_t *theme2 =
        lle_theme_create("test", "Test 2", LLE_THEME_CATEGORY_CUSTOM);

    lle_result_t result1 = lle_theme_registry_register(&registry, theme1);
    ASSERT_EQ(result1, LLE_SUCCESS);

    lle_result_t result2 = lle_theme_registry_register(&registry, theme2);
    ASSERT_EQ(result2, LLE_ERROR_INVALID_STATE); /* Duplicate */

    /* Free theme2 since it wasn't registered */
    lle_theme_free(theme2);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_find_theme) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *theme =
        lle_theme_create("findme", "Find me", LLE_THEME_CATEGORY_CUSTOM);
    lle_theme_registry_register(&registry, theme);

    lle_theme_t *found = lle_theme_registry_find(&registry, "findme");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found, theme);
    ASSERT_STR_EQ(found->name, "findme");

    lle_theme_t *not_found = lle_theme_registry_find(&registry, "nonexistent");
    ASSERT_NULL(not_found);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_set_active) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *theme1 =
        lle_theme_create("theme1", "Theme 1", LLE_THEME_CATEGORY_MINIMAL);
    lle_theme_t *theme2 =
        lle_theme_create("theme2", "Theme 2", LLE_THEME_CATEGORY_MODERN);

    lle_theme_registry_register(&registry, theme1);
    lle_theme_registry_register(&registry, theme2);

    /* Set first theme active */
    lle_result_t result = lle_theme_registry_set_active(&registry, "theme1");
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(theme1->is_active);
    ASSERT(!theme2->is_active);
    ASSERT_STR_EQ(registry.active_theme_name, "theme1");

    /* Switch to second theme */
    result = lle_theme_registry_set_active(&registry, "theme2");
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(!theme1->is_active);
    ASSERT(theme2->is_active);
    ASSERT_STR_EQ(registry.active_theme_name, "theme2");
    ASSERT_EQ(registry.total_switches, 2);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_set_active_not_found) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_result_t result =
        lle_theme_registry_set_active(&registry, "nonexistent");
    ASSERT_EQ(result, LLE_ERROR_NOT_FOUND);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_get_active) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    /* No active theme initially */
    lle_theme_t *active = lle_theme_registry_get_active(&registry);
    ASSERT_NULL(active);

    /* Register and activate */
    lle_theme_t *theme =
        lle_theme_create("active", "Active theme", LLE_THEME_CATEGORY_CUSTOM);
    lle_theme_registry_register(&registry, theme);
    lle_theme_registry_set_active(&registry, "active");

    active = lle_theme_registry_get_active(&registry);
    ASSERT_NOT_NULL(active);
    ASSERT_EQ(active, theme);

    lle_theme_registry_cleanup(&registry);
}

TEST(registry_list) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *t1 =
        lle_theme_create("alpha", "A", LLE_THEME_CATEGORY_MINIMAL);
    lle_theme_t *t2 = lle_theme_create("beta", "B", LLE_THEME_CATEGORY_MODERN);
    lle_theme_t *t3 =
        lle_theme_create("gamma", "C", LLE_THEME_CATEGORY_CLASSIC);

    lle_theme_registry_register(&registry, t1);
    lle_theme_registry_register(&registry, t2);
    lle_theme_registry_register(&registry, t3);

    const char *names[10];
    size_t count = lle_theme_registry_list(&registry, names, 10);

    ASSERT_EQ(count, 3);
    ASSERT_STR_EQ(names[0], "alpha");
    ASSERT_STR_EQ(names[1], "beta");
    ASSERT_STR_EQ(names[2], "gamma");

    lle_theme_registry_cleanup(&registry);
}

/* ========================================================================== */
/* Theme Creation Tests                                                       */
/* ========================================================================== */

TEST(theme_create) {
    lle_theme_t *theme =
        lle_theme_create("mytest", "My test theme", LLE_THEME_CATEGORY_CUSTOM);
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "mytest");
    ASSERT_STR_EQ(theme->description, "My test theme");
    ASSERT_EQ(theme->category, LLE_THEME_CATEGORY_CUSTOM);
    ASSERT_EQ(theme->source, LLE_THEME_SOURCE_RUNTIME);
    ASSERT(!theme->is_active);

    lle_theme_free(theme);
}

TEST(theme_create_empty_name) {
    lle_theme_t *theme =
        lle_theme_create("", "Empty name", LLE_THEME_CATEGORY_CUSTOM);
    ASSERT_NULL(theme);
}

TEST(theme_create_null_name) {
    lle_theme_t *theme =
        lle_theme_create(NULL, "Null name", LLE_THEME_CATEGORY_CUSTOM);
    ASSERT_NULL(theme);
}

/* ========================================================================== */
/* Theme Inheritance Tests                                                    */
/* ========================================================================== */

TEST(theme_inheritance_basic) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    /* Create parent theme with colors */
    lle_theme_t *parent =
        lle_theme_create("parent", "Parent theme", LLE_THEME_CATEGORY_MODERN);
    parent->colors.primary = lle_color_basic(LLE_COLOR_BLUE);
    parent->colors.error = lle_color_basic(LLE_COLOR_RED);
    snprintf(parent->symbols.prompt, sizeof(parent->symbols.prompt), ">");
    snprintf(parent->layout.ps1_format, sizeof(parent->layout.ps1_format),
             "${directory} $ ");

    lle_theme_registry_register(&registry, parent);

    /* Create child theme that inherits */
    lle_theme_t *child =
        lle_theme_create("child", "Child theme", LLE_THEME_CATEGORY_CUSTOM);
    snprintf(child->inherits_from, sizeof(child->inherits_from), "parent");
    /* Child overrides error color */
    child->colors.error = lle_color_basic(LLE_COLOR_MAGENTA);
    /* Clear child's layout so it inherits from parent */
    child->layout.ps1_format[0] = '\0';
    /* Clear child's prompt symbol so it inherits */
    child->symbols.prompt[0] = '\0';

    lle_result_t result = lle_theme_registry_register(&registry, child);
    ASSERT_EQ(result, LLE_SUCCESS);

    /* Verify inheritance */
    ASSERT_EQ(child->parent, parent);
    /* Primary inherited from parent (child had MODE_NONE) */
    ASSERT_EQ(child->colors.primary.mode, LLE_COLOR_MODE_BASIC);
    ASSERT_EQ(child->colors.primary.value.basic, LLE_COLOR_BLUE);
    /* Error was overridden by child */
    ASSERT_EQ(child->colors.error.value.basic, LLE_COLOR_MAGENTA);
    /* Symbol inherited (was cleared) */
    ASSERT_STR_EQ(child->symbols.prompt, ">");
    /* Layout inherited (was cleared) */
    ASSERT_STR_EQ(child->layout.ps1_format, "${directory} $ ");

    lle_theme_registry_cleanup(&registry);
}

TEST(theme_inheritance_not_found) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    lle_theme_t *child =
        lle_theme_create("orphan", "Orphan theme", LLE_THEME_CATEGORY_CUSTOM);
    snprintf(child->inherits_from, sizeof(child->inherits_from), "nonexistent");

    lle_result_t result = lle_theme_registry_register(&registry, child);
    ASSERT_EQ(result, LLE_ERROR_NOT_FOUND);

    lle_theme_free(child);
    lle_theme_registry_cleanup(&registry);
}

/* ========================================================================== */
/* Color Helper Tests                                                         */
/* ========================================================================== */

TEST(color_basic) {
    lle_color_t c = lle_color_basic(LLE_COLOR_GREEN);

    ASSERT_EQ(c.mode, LLE_COLOR_MODE_BASIC);
    ASSERT_EQ(c.value.basic, LLE_COLOR_GREEN);
    ASSERT(!c.bold);
    ASSERT(!c.italic);
}

TEST(color_256) {
    lle_color_t c = lle_color_256(128);

    ASSERT_EQ(c.mode, LLE_COLOR_MODE_256);
    ASSERT_EQ(c.value.palette, 128);
}

TEST(color_rgb) {
    lle_color_t c = lle_color_rgb(255, 128, 64);

    ASSERT_EQ(c.mode, LLE_COLOR_MODE_TRUE);
    ASSERT_EQ(c.value.rgb.r, 255);
    ASSERT_EQ(c.value.rgb.g, 128);
    ASSERT_EQ(c.value.rgb.b, 64);
}

TEST(color_to_ansi_basic) {
    lle_color_t c = lle_color_basic(LLE_COLOR_RED);
    char buf[32];

    size_t len = lle_color_to_ansi(&c, true, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_STR_EQ(buf, "\033[31m");
}

TEST(color_to_ansi_256) {
    lle_color_t c = lle_color_256(200);
    char buf[32];

    size_t len = lle_color_to_ansi(&c, true, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_STR_EQ(buf, "\033[38;5;200m");
}

TEST(color_to_ansi_rgb) {
    lle_color_t c = lle_color_rgb(100, 150, 200);
    char buf[64];

    size_t len = lle_color_to_ansi(&c, true, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_STR_EQ(buf, "\033[38;2;100;150;200m");
}

TEST(color_to_ansi_background) {
    lle_color_t c = lle_color_basic(LLE_COLOR_BLUE);
    char buf[32];

    size_t len = lle_color_to_ansi(&c, false, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT_STR_EQ(buf, "\033[44m");
}

/* ========================================================================== */
/* Symbol Set Tests                                                           */
/* ========================================================================== */

TEST(symbol_set_unicode) {
    lle_symbol_set_t symbols;
    lle_symbol_set_init_unicode(&symbols);

    ASSERT_STR_EQ(symbols.prompt, "$");
    ASSERT_STR_EQ(symbols.prompt_root, "#");
    ASSERT_STR_EQ(symbols.ahead, "↑");
    ASSERT_STR_EQ(symbols.behind, "↓");
    ASSERT_STR_EQ(symbols.error, "✗");
    ASSERT_STR_EQ(symbols.success, "✓");
}

TEST(symbol_set_ascii) {
    lle_symbol_set_t symbols;
    lle_symbol_set_init_ascii(&symbols);

    ASSERT_STR_EQ(symbols.prompt, "$");
    ASSERT_STR_EQ(symbols.prompt_root, "#");
    ASSERT_STR_EQ(symbols.ahead, "^");
    ASSERT_STR_EQ(symbols.behind, "v");
    ASSERT_STR_EQ(symbols.error, "x");
    ASSERT_STR_EQ(symbols.success, "o");
}

/* ========================================================================== */
/* Built-in Theme Tests                                                       */
/* ========================================================================== */

TEST(builtin_minimal) {
    lle_theme_t *theme = lle_theme_create_minimal();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "minimal");
    ASSERT_EQ(theme->source, LLE_THEME_SOURCE_BUILTIN);
    ASSERT_EQ(theme->category, LLE_THEME_CATEGORY_MINIMAL);
    ASSERT(strlen(theme->layout.ps1_format) > 0);
    lle_theme_free(theme);
}

TEST(builtin_default) {
    lle_theme_t *theme = lle_theme_create_default();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "default");
    ASSERT_EQ(theme->source, LLE_THEME_SOURCE_BUILTIN);
    ASSERT_EQ(theme->category, LLE_THEME_CATEGORY_MODERN);
    lle_theme_free(theme);
}

TEST(builtin_classic) {
    lle_theme_t *theme = lle_theme_create_classic();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "classic");
    ASSERT_EQ(theme->category, LLE_THEME_CATEGORY_CLASSIC);
    lle_theme_free(theme);
}

TEST(builtin_powerline) {
    lle_theme_t *theme = lle_theme_create_powerline();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "powerline");
    ASSERT_EQ(theme->category, LLE_THEME_CATEGORY_POWERLINE);
    ASSERT(theme->capabilities & LLE_THEME_CAP_POWERLINE);
    ASSERT(theme->capabilities & LLE_THEME_CAP_TRANSIENT);
    ASSERT(theme->layout.enable_transient);
    lle_theme_free(theme);
}

TEST(builtin_informative) {
    lle_theme_t *theme = lle_theme_create_informative();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "informative");
    ASSERT(theme->capabilities & LLE_THEME_CAP_MULTILINE);
    ASSERT(theme->capabilities & LLE_THEME_CAP_RIGHT_PROMPT);
    ASSERT(theme->layout.enable_right_prompt);
    ASSERT(theme->layout.enable_multiline);
    lle_theme_free(theme);
}

TEST(builtin_two_line) {
    lle_theme_t *theme = lle_theme_create_two_line();
    ASSERT_NOT_NULL(theme);
    ASSERT_STR_EQ(theme->name, "two-line");
    ASSERT(theme->capabilities & LLE_THEME_CAP_MULTILINE);
    ASSERT(theme->layout.enable_multiline);
    lle_theme_free(theme);
}

TEST(register_builtins) {
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    size_t count = lle_theme_register_builtins(&registry);
    ASSERT_EQ(count, 11);
    ASSERT_EQ(registry.count, 11);
    ASSERT_EQ(registry.builtin_count, 11);

    /* Verify all themes registered */
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "minimal"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "default"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "classic"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "powerline"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "nerd"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "informative"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "two-line"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "corporate"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "dark"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "light"));
    ASSERT_NOT_NULL(lle_theme_registry_find(&registry, "colorful"));

    /* Minimal is set as active */
    lle_theme_t *active = lle_theme_registry_get_active(&registry);
    ASSERT_NOT_NULL(active);
    ASSERT_STR_EQ(active->name, "minimal");

    lle_theme_registry_cleanup(&registry);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
    printf("LLE Theme Registry Unit Tests\n");
    printf("==============================\n\n");

    printf("Registry Tests:\n");
    RUN_TEST(registry_init);
    RUN_TEST(registry_init_null);
    RUN_TEST(registry_register_theme);
    RUN_TEST(registry_register_duplicate);
    RUN_TEST(registry_find_theme);
    RUN_TEST(registry_set_active);
    RUN_TEST(registry_set_active_not_found);
    RUN_TEST(registry_get_active);
    RUN_TEST(registry_list);

    printf("\nTheme Creation Tests:\n");
    RUN_TEST(theme_create);
    RUN_TEST(theme_create_empty_name);
    RUN_TEST(theme_create_null_name);

    printf("\nInheritance Tests:\n");
    RUN_TEST(theme_inheritance_basic);
    RUN_TEST(theme_inheritance_not_found);

    printf("\nColor Helper Tests:\n");
    RUN_TEST(color_basic);
    RUN_TEST(color_256);
    RUN_TEST(color_rgb);
    RUN_TEST(color_to_ansi_basic);
    RUN_TEST(color_to_ansi_256);
    RUN_TEST(color_to_ansi_rgb);
    RUN_TEST(color_to_ansi_background);

    printf("\nSymbol Set Tests:\n");
    RUN_TEST(symbol_set_unicode);
    RUN_TEST(symbol_set_ascii);

    printf("\nBuilt-in Theme Tests:\n");
    RUN_TEST(builtin_minimal);
    RUN_TEST(builtin_default);
    RUN_TEST(builtin_classic);
    RUN_TEST(builtin_powerline);
    RUN_TEST(builtin_informative);
    RUN_TEST(builtin_two_line);
    RUN_TEST(register_builtins);

    return TEST_RESULT();
}
