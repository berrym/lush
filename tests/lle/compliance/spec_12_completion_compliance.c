/**
 * @file spec_12_completion_compliance.c
 * @brief Spec 12 Completion System - Compliance Test
 *
 * API verified from include/lle/completion headers on 2025-11-18
 *
 * Exercises the runtime behavior of the Spec 12 completion subsystem that
 * is reachable with the completion mocks this test links against:
 * - Menu default configuration values
 * - Menu renderer option defaults, column math, size estimate, and
 *   parameter validation
 * - NULL-parameter error handling across the result and menu APIs
 *
 * Type/enum existence and source predicates (lle_shell_is_builtin etc.)
 * are covered by the real-linking unit tests (test_completion_types and
 * the compdef suites), not here.
 *
 * SPECIFICATION: docs/lle_specification/12_completion_complete.md
 */

#include "lle/completion/completion_menu_logic.h"
#include "lle/completion/completion_menu_renderer.h"
#include "lle/completion/completion_menu_state.h"
#include "lle/completion/completion_types.h"
#include "lle/error_handling.h"
#include <stdio.h>
#include <string.h>

/// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (condition) {                                                       \
            tests_passed++;                                                    \
        } else {                                                               \
            printf("FAILED: %s\n", message);                                   \
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

/**
 * @brief Test: menu default config carries the documented defaults
 */
void test_menu_default_config(void) {
    printf("[ TEST ] Menu default config (completion_menu)\n");

    /// Menu config
    lle_completion_menu_config_t config = lle_completion_menu_default_config();
    TEST_ASSERT(config.max_visible_items > 0,
                "default config has a positive visible-item cap");
    TEST_ASSERT(config.max_visible_items == 10,
                "default config has correct max_visible_items");
    TEST_ASSERT(config.show_category_headers == true,
                "default config shows category headers");
    TEST_ASSERT(config.min_items_for_menu == 2,
                "default config has correct min_items_for_menu");

    printf("[ PASS ] Menu default config\n");
}

/**
 * @brief Test: menu renderer option defaults, column math, and validation
 */
void test_menu_renderer_api(void) {
    printf("[ TEST ] Menu renderer API (completion_menu_renderer)\n");

    /// Default render options carry the documented values.
    lle_menu_render_options_t options = lle_menu_renderer_default_options(80);
    TEST_ASSERT(options.terminal_width == 80,
                "default options adopt the requested terminal width");
    TEST_ASSERT(options.show_category_headers == true,
                "default options has category headers enabled");
    TEST_ASSERT(options.use_multi_column == true,
                "default options has multi-column enabled");
    TEST_ASSERT(options.max_rows == 20, "default options has correct max_rows");

    size_t width = lle_menu_renderer_calculate_column_width(NULL, 0, 80, 4);
    TEST_ASSERT(width >= LLE_MENU_RENDERER_MIN_COL_WIDTH,
                "column width is at least the configured minimum");

    size_t cols = lle_menu_renderer_calculate_columns(80, 20, 2);
    TEST_ASSERT(cols >= 1, "at least one column fits an 80-column terminal");

    /// Size estimation returns a usable buffer size.
    size_t estimate = lle_menu_renderer_estimate_size(NULL, NULL);
    TEST_ASSERT(estimate > 0, "size estimate is non-zero");

    /// Rendering a NULL menu is rejected, not crashed.
    char output[128];
    lle_menu_render_stats_t stats;
    lle_result_t result = lle_completion_menu_render(NULL, &options, output,
                                                     sizeof(output), &stats);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "render rejects a NULL menu with INVALID_PARAMETER");

    /// Formatting helpers return a definite result code.
    result = lle_menu_renderer_format_category_header(
        LLE_COMPLETION_TYPE_FILE, output, sizeof(output), true);
    TEST_ASSERT(result == LLE_SUCCESS, "category header formats successfully");

    lle_completion_item_t item = {0};
    result = lle_menu_renderer_format_item(&item, false, false, NULL, output,
                                           sizeof(output));
    TEST_ASSERT(result == LLE_SUCCESS || result == LLE_ERROR_INVALID_PARAMETER,
                "item formats or rejects invalid input");

    printf("[ PASS ] Menu renderer API\n");
}

/**
 * @brief Test: Verify error handling compliance
 */
void test_error_handling(void) {
    printf("[ TEST ] Error handling compliance\n");

    /// All API functions must use lle_result_t for error returns
    /// Verify that functions properly handle NULL parameters

    lle_result_t result;

    /// Result API rejects NULL collectors/items.
    result = lle_completion_result_add_item(NULL, NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "completion_result_add_item handles NULL");

    result = lle_completion_result_sort(NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "completion_result_sort handles NULL");

    result = lle_completion_result_free(NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "completion_result_free handles NULL");

    /// Menu navigation rejects a NULL menu.
    result = lle_completion_menu_move_down(NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "menu_move_down handles NULL");

    result = lle_completion_menu_move_up(NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "menu_move_up handles NULL");

    result = lle_completion_menu_cancel(NULL);
    TEST_ASSERT(result == LLE_ERROR_INVALID_PARAMETER,
                "menu_cancel handles NULL");

    printf("[ PASS ] Error handling compliance\n");
}

int main(void) {
    printf("========================================\n");
    printf("Spec 12 Completion System - Compliance Test\n");
    printf("========================================\n\n");

    /// Menu state and logic
    test_menu_default_config();

    /// Menu renderer
    test_menu_renderer_api();

    /// Cross-cutting concerns
    test_error_handling();

    printf("\n========================================\n");
    printf("Compliance Test Results\n");
    printf("========================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("========================================\n");

    if (tests_failed > 0) {
        printf("COMPLIANCE TEST FAILED\n");
        return 1;
    }

    printf("COMPLIANCE TEST PASSED\n");
    return 0;
}
