/**
 * @file spec_25_theme_compliance.c
 * @brief Spec 25 Section 4 Theme Registry Compliance Test
 *
 * Tests for LLE Specification 25 Section 4: Theme Registry System
 * Validates API completeness and spec adherence.
 *
 * This compliance test verifies:
 * - All theme types are defined
 * - All theme API functions are declared
 * - Theme registry operations work correctly
 * - Built-in themes implement required features
 * - Color and symbol systems match specification
 *
 * Test Coverage:
 * - Theme Registry API (6 functions)
 * - Theme Lifecycle API (3 functions)
 * - Color Helper API (4 functions)
 * - Symbol Set API (2 functions)
 * - Built-in Theme Factories (11 themes)
 * - Theme Capabilities and Categories
 *
 * Specification:
 * docs/lle_specification/25_prompt_theme_system_complete.md Section 4
 * Date: 2025-12-26
 */

#include "lle/error_handling.h"
#include "lle/prompt/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test assertion counter
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
// Test: Type Definitions
/* ========================================================================== */

static void test_type_definitions(void) {
    printf("Phase 1: Type Definitions\n");
    printf("--------------------------\n");

    TEST_START("lle_color_mode_t enum defined");
    COMPLIANCE_ASSERT(LLE_COLOR_MODE_NONE == 0, "LLE_COLOR_MODE_NONE is 0");
    COMPLIANCE_ASSERT(LLE_COLOR_MODE_BASIC >= 0,
                      "LLE_COLOR_MODE_BASIC defined");
    COMPLIANCE_ASSERT(LLE_COLOR_MODE_256 >= 0, "LLE_COLOR_MODE_256 defined");
    COMPLIANCE_ASSERT(LLE_COLOR_MODE_TRUE >= 0, "LLE_COLOR_MODE_TRUE defined");
    TEST_PASS();

    TEST_START("lle_theme_capability_t flags defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_NONE == 0, "LLE_THEME_CAP_NONE is 0");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_256_COLOR > 0,
                      "LLE_THEME_CAP_256_COLOR defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_TRUE_COLOR > 0,
                      "LLE_THEME_CAP_TRUE_COLOR defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_POWERLINE > 0,
                      "LLE_THEME_CAP_POWERLINE defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_UNICODE > 0,
                      "LLE_THEME_CAP_UNICODE defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_MULTILINE > 0,
                      "LLE_THEME_CAP_MULTILINE defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_RIGHT_PROMPT > 0,
                      "LLE_THEME_CAP_RIGHT_PROMPT defined");
    COMPLIANCE_ASSERT(LLE_THEME_CAP_TRANSIENT > 0,
                      "LLE_THEME_CAP_TRANSIENT defined");
    TEST_PASS();

    TEST_START("lle_theme_category_t enum defined");
    COMPLIANCE_ASSERT(LLE_THEME_CATEGORY_MINIMAL >= 0,
                      "LLE_THEME_CATEGORY_MINIMAL defined");
    COMPLIANCE_ASSERT(LLE_THEME_CATEGORY_CLASSIC >= 0,
                      "LLE_THEME_CATEGORY_CLASSIC defined");
    COMPLIANCE_ASSERT(LLE_THEME_CATEGORY_MODERN >= 0,
                      "LLE_THEME_CATEGORY_MODERN defined");
    COMPLIANCE_ASSERT(LLE_THEME_CATEGORY_POWERLINE >= 0,
                      "LLE_THEME_CATEGORY_POWERLINE defined");
    COMPLIANCE_ASSERT(LLE_THEME_CATEGORY_CUSTOM >= 0,
                      "LLE_THEME_CATEGORY_CUSTOM defined");
    TEST_PASS();

    TEST_START("lle_theme_source_t enum defined");
    COMPLIANCE_ASSERT(LLE_THEME_SOURCE_BUILTIN >= 0,
                      "LLE_THEME_SOURCE_BUILTIN defined");
    COMPLIANCE_ASSERT(LLE_THEME_SOURCE_USER >= 0,
                      "LLE_THEME_SOURCE_USER defined");
    COMPLIANCE_ASSERT(LLE_THEME_SOURCE_RUNTIME >= 0,
                      "LLE_THEME_SOURCE_RUNTIME defined");
    TEST_PASS();

    TEST_START("lle_color_t structure defined");
    lle_color_t color = {0};
    COMPLIANCE_ASSERT(&color.mode != NULL, "mode field exists");
    COMPLIANCE_ASSERT(&color.value != NULL, "value field exists");
    TEST_PASS();

    TEST_START("lle_color_scheme_t structure defined");
    lle_color_scheme_t scheme;
    COMPLIANCE_ASSERT(sizeof(scheme.primary) == sizeof(lle_color_t),
                      "primary is lle_color_t");
    COMPLIANCE_ASSERT(sizeof(scheme.error) == sizeof(lle_color_t),
                      "error is lle_color_t");
    COMPLIANCE_ASSERT(sizeof(scheme.git_branch) == sizeof(lle_color_t),
                      "git_branch is lle_color_t");
    TEST_PASS();

    TEST_START("lle_symbol_set_t structure defined");
    lle_symbol_set_t symbols;
    COMPLIANCE_ASSERT(sizeof(symbols.prompt) == LLE_SYMBOL_MAX,
                      "prompt has correct size");
    COMPLIANCE_ASSERT(sizeof(symbols.branch) == LLE_SYMBOL_MAX,
                      "branch has correct size");
    TEST_PASS();

    TEST_START("lle_prompt_layout_t structure defined");
    lle_prompt_layout_t layout;
    COMPLIANCE_ASSERT(sizeof(layout.ps1_format) == LLE_TEMPLATE_MAX,
                      "ps1_format has correct size");
    COMPLIANCE_ASSERT(sizeof(layout.ps2_format) == LLE_TEMPLATE_MAX,
                      "ps2_format has correct size");
    TEST_PASS();

    TEST_START("lle_theme_t structure defined");
    lle_theme_t theme;
    COMPLIANCE_ASSERT(sizeof(theme.name) == LLE_THEME_NAME_MAX,
                      "name has correct size");
    COMPLIANCE_ASSERT(sizeof(theme.colors) == sizeof(lle_color_scheme_t),
                      "colors is lle_color_scheme_t");
    COMPLIANCE_ASSERT(sizeof(theme.symbols) == sizeof(lle_symbol_set_t),
                      "symbols is lle_symbol_set_t");
    COMPLIANCE_ASSERT(sizeof(theme.layout) == sizeof(lle_prompt_layout_t),
                      "layout is lle_prompt_layout_t");
    TEST_PASS();

    TEST_START("lle_theme_registry_t structure defined");
    lle_theme_registry_t registry = {0};
    COMPLIANCE_ASSERT(&registry != NULL, "registry has size");
    TEST_PASS();

    printf("  Phase 1 complete (11 tests)\n\n");
}

/* ========================================================================== */
// Test: Theme Registry API
/* ========================================================================== */

static void test_theme_registry_api(void) {
    printf("Phase 2: Theme Registry API\n");
    printf("----------------------------\n");

    TEST_START("lle_theme_registry_init function");
    COMPLIANCE_ASSERT(lle_theme_registry_init != NULL,
                      "lle_theme_registry_init declared");
    lle_theme_registry_t registry;
    lle_result_t result = lle_theme_registry_init(&registry);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "init returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(registry.initialized == true, "registry is initialized");
    TEST_PASS();

    TEST_START("lle_theme_registry_register function");
    COMPLIANCE_ASSERT(lle_theme_registry_register != NULL,
                      "lle_theme_registry_register declared");
    lle_theme_t *theme =
        lle_theme_create("test", "Test", LLE_THEME_CATEGORY_CUSTOM);
    result = lle_theme_registry_register(&registry, theme);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "register returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(registry.count == 1, "count incremented");
    TEST_PASS();

    TEST_START("lle_theme_registry_find function");
    COMPLIANCE_ASSERT(lle_theme_registry_find != NULL,
                      "lle_theme_registry_find declared");
    lle_theme_t *found = lle_theme_registry_find(&registry, "test");
    COMPLIANCE_ASSERT(found != NULL, "find returns theme");
    COMPLIANCE_ASSERT(found == theme, "find returns correct theme");
    TEST_PASS();

    TEST_START("lle_theme_registry_set_active function");
    COMPLIANCE_ASSERT(lle_theme_registry_set_active != NULL,
                      "lle_theme_registry_set_active declared");
    result = lle_theme_registry_set_active(&registry, "test");
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "set_active returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(theme->is_active == true, "theme is active");
    TEST_PASS();

    TEST_START("lle_theme_registry_get_active function");
    COMPLIANCE_ASSERT(lle_theme_registry_get_active != NULL,
                      "lle_theme_registry_get_active declared");
    lle_theme_t *active = lle_theme_registry_get_active(&registry);
    COMPLIANCE_ASSERT(active == theme, "get_active returns active theme");
    TEST_PASS();

    TEST_START("lle_theme_registry_list function");
    COMPLIANCE_ASSERT(lle_theme_registry_list != NULL,
                      "lle_theme_registry_list declared");
    const char *names[16];
    size_t count = lle_theme_registry_list(&registry, names, 16);
    COMPLIANCE_ASSERT(count == 1, "list returns correct count");
    TEST_PASS();

    TEST_START("lle_theme_registry_cleanup function");
    COMPLIANCE_ASSERT(lle_theme_registry_cleanup != NULL,
                      "lle_theme_registry_cleanup declared");
    lle_theme_registry_cleanup(&registry);
    COMPLIANCE_ASSERT(registry.initialized == false,
                      "registry not initialized after cleanup");
    TEST_PASS();

    printf("  Phase 2 complete (7 tests)\n\n");
}

/* ========================================================================== */
// Test: Theme Lifecycle API
/* ========================================================================== */

static void test_theme_lifecycle_api(void) {
    printf("Phase 3: Theme Lifecycle API\n");
    printf("-----------------------------\n");

    TEST_START("lle_theme_create function");
    COMPLIANCE_ASSERT(lle_theme_create != NULL, "lle_theme_create declared");
    lle_theme_t *theme =
        lle_theme_create("mytest", "My Test", LLE_THEME_CATEGORY_MODERN);
    COMPLIANCE_ASSERT(theme != NULL, "create returns theme");
    COMPLIANCE_ASSERT(strcmp(theme->name, "mytest") == 0, "name set correctly");
    COMPLIANCE_ASSERT(theme->category == LLE_THEME_CATEGORY_MODERN,
                      "category set correctly");
    COMPLIANCE_ASSERT(theme->source == LLE_THEME_SOURCE_RUNTIME,
                      "source is RUNTIME");
    TEST_PASS();

    TEST_START("lle_theme_free function");
    COMPLIANCE_ASSERT(lle_theme_free != NULL, "lle_theme_free declared");
    lle_theme_free(theme);
    TEST_PASS();

    TEST_START("lle_theme_resolve_inheritance function");
    COMPLIANCE_ASSERT(lle_theme_resolve_inheritance != NULL,
                      "lle_theme_resolve_inheritance declared");
    TEST_PASS();

    printf("  Phase 3 complete (3 tests)\n\n");
}

/* ========================================================================== */
// Test: Color Helper API
/* ========================================================================== */

static void test_color_helper_api(void) {
    printf("Phase 4: Color Helper API\n");
    printf("--------------------------\n");

    TEST_START("lle_color_basic function");
    COMPLIANCE_ASSERT(lle_color_basic != NULL, "lle_color_basic declared");
    lle_color_t c = lle_color_basic(LLE_COLOR_RED);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_BASIC, "mode is BASIC");
    COMPLIANCE_ASSERT(c.value.basic == LLE_COLOR_RED, "value is RED");
    TEST_PASS();

    TEST_START("lle_color_256 function");
    COMPLIANCE_ASSERT(lle_color_256 != NULL, "lle_color_256 declared");
    c = lle_color_256(200);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_256, "mode is 256");
    COMPLIANCE_ASSERT(c.value.palette == 200, "palette value correct");
    TEST_PASS();

    TEST_START("lle_color_rgb function");
    COMPLIANCE_ASSERT(lle_color_rgb != NULL, "lle_color_rgb declared");
    c = lle_color_rgb(100, 150, 200);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_TRUE, "mode is TRUE");
    COMPLIANCE_ASSERT(c.value.rgb.r == 100, "R value correct");
    COMPLIANCE_ASSERT(c.value.rgb.g == 150, "G value correct");
    COMPLIANCE_ASSERT(c.value.rgb.b == 200, "B value correct");
    TEST_PASS();

    TEST_START("lle_color_to_ansi function");
    COMPLIANCE_ASSERT(lle_color_to_ansi != NULL, "lle_color_to_ansi declared");
    char buf[64];
    c = lle_color_basic(LLE_COLOR_GREEN);
    size_t len = lle_color_to_ansi(&c, true, buf, sizeof(buf));
    COMPLIANCE_ASSERT(len > 0, "generates escape sequence");
    COMPLIANCE_ASSERT(strstr(buf, "\033[") != NULL, "contains escape sequence");
    TEST_PASS();

    printf("  Phase 4 complete (4 tests)\n\n");
}

/* ========================================================================== */
// Test: Symbol Set API
/* ========================================================================== */

static void test_symbol_set_api(void) {
    printf("Phase 5: Symbol Set API\n");
    printf("------------------------\n");

    TEST_START("lle_symbol_set_init_unicode function");
    COMPLIANCE_ASSERT(lle_symbol_set_init_unicode != NULL,
                      "lle_symbol_set_init_unicode declared");
    lle_symbol_set_t symbols;
    lle_symbol_set_init_unicode(&symbols);
    COMPLIANCE_ASSERT(strlen(symbols.prompt) > 0, "prompt symbol set");
    COMPLIANCE_ASSERT(strlen(symbols.prompt_root) > 0,
                      "prompt_root symbol set");
    TEST_PASS();

    TEST_START("lle_symbol_set_init_ascii function");
    COMPLIANCE_ASSERT(lle_symbol_set_init_ascii != NULL,
                      "lle_symbol_set_init_ascii declared");
    lle_symbol_set_init_ascii(&symbols);
    COMPLIANCE_ASSERT(strlen(symbols.prompt) > 0, "prompt symbol set");
    // ASCII should use simple characters
    COMPLIANCE_ASSERT(strcmp(symbols.prompt, "$") == 0,
                      "prompt is $ for ASCII");
    TEST_PASS();

    printf("  Phase 5 complete (2 tests)\n\n");
}

/* ========================================================================== */
// Test: Built-in Themes
/* ========================================================================== */

static void test_builtin_themes(void) {
    printf("Phase 6: Built-in Themes\n");
    printf("-------------------------\n");

    TEST_START("lle_theme_create_minimal function");
    COMPLIANCE_ASSERT(lle_theme_create_minimal != NULL,
                      "lle_theme_create_minimal declared");
    lle_theme_t *t = lle_theme_create_minimal();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "minimal") == 0, "name is 'minimal'");
    COMPLIANCE_ASSERT(t->source == LLE_THEME_SOURCE_BUILTIN,
                      "source is BUILTIN");
    COMPLIANCE_ASSERT(t->category == LLE_THEME_CATEGORY_MINIMAL,
                      "category is MINIMAL");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_default function");
    COMPLIANCE_ASSERT(lle_theme_create_default != NULL,
                      "lle_theme_create_default declared");
    t = lle_theme_create_default();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "default") == 0, "name is 'default'");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_classic function");
    COMPLIANCE_ASSERT(lle_theme_create_classic != NULL,
                      "lle_theme_create_classic declared");
    t = lle_theme_create_classic();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "classic") == 0, "name is 'classic'");
    COMPLIANCE_ASSERT(t->category == LLE_THEME_CATEGORY_CLASSIC,
                      "category is CLASSIC");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_powerline function");
    COMPLIANCE_ASSERT(lle_theme_create_powerline != NULL,
                      "lle_theme_create_powerline declared");
    t = lle_theme_create_powerline();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "powerline") == 0, "name is 'powerline'");
    COMPLIANCE_ASSERT(t->category == LLE_THEME_CATEGORY_POWERLINE,
                      "category is POWERLINE");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_POWERLINE,
                      "has POWERLINE capability");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_informative function");
    COMPLIANCE_ASSERT(lle_theme_create_informative != NULL,
                      "lle_theme_create_informative declared");
    t = lle_theme_create_informative();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_MULTILINE,
                      "has MULTILINE capability");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_RIGHT_PROMPT,
                      "has RIGHT_PROMPT capability");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_two_line function");
    COMPLIANCE_ASSERT(lle_theme_create_two_line != NULL,
                      "lle_theme_create_two_line declared");
    t = lle_theme_create_two_line();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_MULTILINE,
                      "has MULTILINE capability");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_register_builtins function");
    COMPLIANCE_ASSERT(lle_theme_register_builtins != NULL,
                      "lle_theme_register_builtins declared");
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);
    size_t count = lle_theme_register_builtins(&registry);
    COMPLIANCE_ASSERT(count == 11, "registers 11 themes");
    COMPLIANCE_ASSERT(registry.builtin_count == 11, "builtin_count is 11");
    // Verify all registered
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "minimal") != NULL,
                      "minimal registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "default") != NULL,
                      "default registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "classic") != NULL,
                      "classic registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "powerline") != NULL,
                      "powerline registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "nerd") != NULL,
                      "nerd registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "informative") != NULL,
                      "informative registered");
    COMPLIANCE_ASSERT(lle_theme_registry_find(&registry, "two-line") != NULL,
                      "two-line registered");
    lle_theme_registry_cleanup(&registry);
    TEST_PASS();

    printf("  Phase 6 complete (7 tests)\n\n");
}

/* ========================================================================== */
// Main
/* ========================================================================== */

int main(void) {
    printf("Spec 25 Section 4 Theme Registry Compliance Test\n");
    printf("=================================================\n\n");

    test_type_definitions();
    test_theme_registry_api();
    test_theme_lifecycle_api();
    test_color_helper_api();
    test_symbol_set_api();
    test_builtin_themes();

    printf("=================================================\n");
    printf("COMPLIANCE TEST PASSED\n");
    printf("  Tests run: %d\n", tests_run);
    printf("  Assertions: %d\n", assertions_passed);
    printf("=================================================\n");

    return 0;
}
