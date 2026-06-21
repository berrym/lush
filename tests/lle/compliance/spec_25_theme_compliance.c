/**
 * @file spec_25_theme_compliance.c
 * @brief Spec 25 Section 4 Theme Registry Compliance Test
 *
 * Tests for LLE Specification 25 Section 4: Theme Registry System
 * Validates API completeness and spec adherence.
 *
 * This compliance test verifies the runtime behavior of the theme system:
 * - Theme registry operations (register, find, set/get active, list)
 * - Theme creation, inheritance resolution, and cleanup
 * - Color helpers produce the correct mode and value
 * - Symbol sets initialize their Unicode and ASCII glyphs
 * - Built-in themes carry their declared capabilities
 *
 * Test Coverage:
 * - Theme Registry API
 * - Theme Lifecycle API (create, resolve inheritance)
 * - Color Helper API
 * - Symbol Set API
 * - Built-in Theme Factories
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
/// Test: Theme Registry API
/* ========================================================================== */

static void test_theme_registry_api(void) {
    printf("Theme Registry API\n");
    printf("----------------------------\n");

    TEST_START("lle_theme_registry_init function");
    lle_theme_registry_t registry;
    lle_result_t result = lle_theme_registry_init(&registry);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "init returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(registry.initialized == true, "registry is initialized");
    TEST_PASS();

    TEST_START("lle_theme_registry_register function");
    lle_theme_t *theme =
        lle_theme_create("test", "Test", LLE_THEME_CATEGORY_CUSTOM);
    result = lle_theme_registry_register(&registry, theme);
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "register returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(registry.count == 1, "count incremented");
    TEST_PASS();

    TEST_START("lle_theme_registry_find function");
    lle_theme_t *found = lle_theme_registry_find(&registry, "test");
    COMPLIANCE_ASSERT(found != NULL, "find returns theme");
    COMPLIANCE_ASSERT(found == theme, "find returns correct theme");
    TEST_PASS();

    TEST_START("lle_theme_registry_set_active function");
    result = lle_theme_registry_set_active(&registry, "test");
    COMPLIANCE_ASSERT(result == LLE_SUCCESS, "set_active returns LLE_SUCCESS");
    COMPLIANCE_ASSERT(theme->is_active == true, "theme is active");
    TEST_PASS();

    TEST_START("lle_theme_registry_get_active function");
    lle_theme_t *active = lle_theme_registry_get_active(&registry);
    COMPLIANCE_ASSERT(active == theme, "get_active returns active theme");
    TEST_PASS();

    TEST_START("lle_theme_registry_list function");
    const char *names[16];
    size_t count = lle_theme_registry_list(&registry, names, 16);
    COMPLIANCE_ASSERT(count == 1, "list returns correct count");
    TEST_PASS();

    TEST_START("lle_theme_registry_cleanup function");
    lle_theme_registry_cleanup(&registry);
    COMPLIANCE_ASSERT(registry.initialized == false,
                      "registry not initialized after cleanup");
    TEST_PASS();

    printf("  complete (7 tests)\n\n");
}

/* ========================================================================== */
/// Test: Theme Lifecycle API
/* ========================================================================== */

static void test_theme_lifecycle_api(void) {
    printf("Theme Lifecycle API\n");
    printf("-----------------------------\n");

    TEST_START("lle_theme_create sets identity and is freed cleanly");
    lle_theme_t *theme =
        lle_theme_create("mytest", "My Test", LLE_THEME_CATEGORY_MODERN);
    COMPLIANCE_ASSERT(theme != NULL, "create returns theme");
    COMPLIANCE_ASSERT(strcmp(theme->name, "mytest") == 0, "name set correctly");
    COMPLIANCE_ASSERT(theme->category == LLE_THEME_CATEGORY_MODERN,
                      "category set correctly");
    COMPLIANCE_ASSERT(theme->source == LLE_THEME_SOURCE_RUNTIME,
                      "source is RUNTIME");
    lle_theme_free(theme);
    TEST_PASS();

    TEST_START("lle_theme_resolve_inheritance inherits unset, preserves set");
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);

    /// Parent defines primary RED; register it so the child can find it.
    lle_theme_t *parent =
        lle_theme_create("parent", "Parent", LLE_THEME_CATEGORY_CLASSIC);
    parent->colors.primary = lle_color_basic(LLE_COLOR_RED);
    lle_theme_registry_register(&registry, parent);

    /// Child inherits from parent: it leaves primary unset (mode NONE from
    /// calloc) but explicitly overrides error to GREEN.
    lle_theme_t *child =
        lle_theme_create("child", "Child", LLE_THEME_CATEGORY_CUSTOM);
    snprintf(child->inherits_from, sizeof(child->inherits_from), "parent");
    child->colors.error = lle_color_basic(LLE_COLOR_GREEN);

    lle_result_t r = lle_theme_resolve_inheritance(&registry, child);
    COMPLIANCE_ASSERT(r == LLE_SUCCESS, "resolve succeeds");
    COMPLIANCE_ASSERT(child->parent == parent, "parent pointer is linked");
    /// The unset primary is filled in from the parent...
    COMPLIANCE_ASSERT(child->colors.primary.mode == LLE_COLOR_MODE_BASIC,
                      "unset primary inherits the parent's mode");
    COMPLIANCE_ASSERT(child->colors.primary.value.basic == LLE_COLOR_RED,
                      "unset primary inherits the parent's value");
    /// ...while the explicitly-set error keeps the child's own value.
    COMPLIANCE_ASSERT(child->colors.error.value.basic == LLE_COLOR_GREEN,
                      "explicitly set error survives inheritance");

    lle_theme_free(child);
    lle_theme_registry_cleanup(&registry); /// frees the registered parent
    TEST_PASS();

    TEST_START("lle_theme_resolve_inheritance reports a missing parent");
    lle_theme_registry_t empty;
    lle_theme_registry_init(&empty);
    lle_theme_t *orphan =
        lle_theme_create("orphan", "Orphan", LLE_THEME_CATEGORY_CUSTOM);
    snprintf(orphan->inherits_from, sizeof(orphan->inherits_from), "nonesuch");
    COMPLIANCE_ASSERT(lle_theme_resolve_inheritance(&empty, orphan) ==
                          LLE_ERROR_NOT_FOUND,
                      "an unresolvable parent yields NOT_FOUND");
    lle_theme_free(orphan);
    lle_theme_registry_cleanup(&empty);
    TEST_PASS();

    printf("  complete (3 tests)\n\n");
}

/* ========================================================================== */
/// Test: Color Helper API
/* ========================================================================== */

static void test_color_helper_api(void) {
    printf("Color Helper API\n");
    printf("--------------------------\n");

    TEST_START("lle_color_basic function");
    lle_color_t c = lle_color_basic(LLE_COLOR_RED);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_BASIC, "mode is BASIC");
    COMPLIANCE_ASSERT(c.value.basic == LLE_COLOR_RED, "value is RED");
    TEST_PASS();

    TEST_START("lle_color_256 function");
    c = lle_color_256(200);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_256, "mode is 256");
    COMPLIANCE_ASSERT(c.value.palette == 200, "palette value correct");
    TEST_PASS();

    TEST_START("lle_color_rgb function");
    c = lle_color_rgb(100, 150, 200);
    COMPLIANCE_ASSERT(c.mode == LLE_COLOR_MODE_TRUE, "mode is TRUE");
    COMPLIANCE_ASSERT(c.value.rgb.r == 100, "R value correct");
    COMPLIANCE_ASSERT(c.value.rgb.g == 150, "G value correct");
    COMPLIANCE_ASSERT(c.value.rgb.b == 200, "B value correct");
    TEST_PASS();

    TEST_START("lle_color_to_ansi function");
    char buf[64];
    c = lle_color_basic(LLE_COLOR_GREEN);
    size_t len = lle_color_to_ansi(&c, true, buf, sizeof(buf));
    COMPLIANCE_ASSERT(len > 0, "generates escape sequence");
    COMPLIANCE_ASSERT(strstr(buf, "\033[") != NULL, "contains escape sequence");
    TEST_PASS();

    printf("  complete (4 tests)\n\n");
}

/* ========================================================================== */
/// Test: Symbol Set API
/* ========================================================================== */

static void test_symbol_set_api(void) {
    printf("Symbol Set API\n");
    printf("------------------------\n");

    TEST_START("lle_symbol_set_init_unicode function");
    lle_symbol_set_t symbols;
    lle_symbol_set_init_unicode(&symbols);
    COMPLIANCE_ASSERT(strlen(symbols.prompt) > 0, "prompt symbol set");
    COMPLIANCE_ASSERT(strlen(symbols.prompt_root) > 0,
                      "prompt_root symbol set");
    TEST_PASS();

    TEST_START("lle_symbol_set_init_ascii function");
    lle_symbol_set_init_ascii(&symbols);
    COMPLIANCE_ASSERT(strlen(symbols.prompt) > 0, "prompt symbol set");
    /// ASCII should use simple characters
    COMPLIANCE_ASSERT(strcmp(symbols.prompt, "$") == 0,
                      "prompt is $ for ASCII");
    TEST_PASS();

    printf("  complete (2 tests)\n\n");
}

/* ========================================================================== */
/// Test: Built-in Themes
/* ========================================================================== */

static void test_builtin_themes(void) {
    printf("Built-in Themes\n");
    printf("-------------------------\n");

    TEST_START("lle_theme_create_minimal function");
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
    t = lle_theme_create_default();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "default") == 0, "name is 'default'");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_classic function");
    t = lle_theme_create_classic();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(strcmp(t->name, "classic") == 0, "name is 'classic'");
    COMPLIANCE_ASSERT(t->category == LLE_THEME_CATEGORY_CLASSIC,
                      "category is CLASSIC");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_powerline function");
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
    t = lle_theme_create_informative();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_MULTILINE,
                      "has MULTILINE capability");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_RIGHT_PROMPT,
                      "has RIGHT_PROMPT capability");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_create_two_line function");
    t = lle_theme_create_two_line();
    COMPLIANCE_ASSERT(t != NULL, "creates theme");
    COMPLIANCE_ASSERT(t->capabilities & LLE_THEME_CAP_MULTILINE,
                      "has MULTILINE capability");
    lle_theme_free(t);
    TEST_PASS();

    TEST_START("lle_theme_register_builtins function");
    lle_theme_registry_t registry;
    lle_theme_registry_init(&registry);
    size_t count = lle_theme_register_builtins(&registry);
    COMPLIANCE_ASSERT(count == 11, "registers 11 themes");
    COMPLIANCE_ASSERT(registry.builtin_count == 11, "builtin_count is 11");
    /// Verify all registered
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

    printf("  complete (7 tests)\n\n");
}

/* ========================================================================== */
/// Main
/* ========================================================================== */

int main(void) {
    printf("Spec 25 Section 4 Theme Registry Compliance Test\n");
    printf("=================================================\n\n");

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
