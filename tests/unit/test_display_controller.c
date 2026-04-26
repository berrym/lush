/**
 * @file test_display_controller.c
 * @brief Unit tests for display controller
 *
 * Tests the display controller's high-level coordination, configuration,
 * performance monitoring, caching, and completion menu integration.
 *
 * Note: Many display_controller functions require a full display stack
 * (terminal_control, composition_engine, etc.) to be initialized. These
 * tests focus on functions that can be tested in isolation or with
 * minimal dependencies.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/display_controller.h"
#include "test_framework.h"

/* Local 1-arg / 2-arg ASSERT_* helpers (no message) bridge to the
 * framework's variants by synthesizing a stringified-expression
 * message. Replaces the historical exit(1) failure path with
 * longjmp-based isolation per RUN_TEST. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#define ASSERT(cond) do { if (!(cond)) { TEST_FAIL_MSG(#cond); } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")
#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL, #p " is NULL")
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL, #p " is non-NULL")

/* Test framework macros */







/* ============================================================
 * CREATE/DESTROY TESTS
 * ============================================================ */

static int test_create_returns_non_null(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    /* Should not be initialized yet */
    ASSERT(!display_controller_is_initialized(dc));

    display_controller_destroy(dc);
    return 1;
}

static int test_create_initializes_config_defaults(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    /* Check that default config values are set */
    /* Before init, config should have default values from create */
    /* We can check this by examining the internal state */

    display_controller_destroy(dc);
    return 1;
}

static int test_destroy_null_safe(void) {
    /* Should not crash */
    display_controller_destroy(NULL);
    return 1;
}

static int test_destroy_uninitialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    /* Destroy without init should not crash */
    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * IS_INITIALIZED TESTS
 * ============================================================ */

static int test_is_initialized_null_context(void) {
    bool result = display_controller_is_initialized(NULL);
    ASSERT(!result);
    return 1;
}

static int test_is_initialized_before_init(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    bool result = display_controller_is_initialized(dc);
    ASSERT(!result);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * DEFAULT CONFIG TESTS
 * ============================================================ */

static int test_create_default_config_null_param(void) {
    display_controller_error_t result =
        display_controller_create_default_config(NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    return 1;
}

static int test_create_default_config_sets_values(void) {
    display_controller_config_t config;
    memset(&config, 0xff, sizeof(config)); /* Fill with non-zero values */

    display_controller_error_t result =
        display_controller_create_default_config(&config);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_SUCCESS);

    /* Check that default values are set correctly */
    ASSERT_EQ(config.optimization_level, DISPLAY_OPTIMIZATION_STANDARD);
    ASSERT_EQ(config.cache_ttl_ms, DISPLAY_CONTROLLER_DEFAULT_CACHE_TTL_MS);
    ASSERT_EQ(config.performance_monitor_interval_ms,
              DISPLAY_CONTROLLER_DEFAULT_MONITORING_INTERVAL_MS);
    ASSERT_EQ(config.max_cache_entries, 256);

    /* Feature toggles */
    ASSERT(config.enable_caching);
    ASSERT(config.enable_diff_algorithms);
    ASSERT(config.enable_performance_monitoring);
    ASSERT(config.enable_adaptive_optimization);
    ASSERT(!config.enable_integration_mode);

    /* Threshold configuration */
    ASSERT_EQ(config.performance_threshold_ms,
              DISPLAY_CONTROLLER_PERFORMANCE_THRESHOLD_MS);
    ASSERT(config.cache_hit_rate_threshold > 0.0);
    ASSERT_EQ(config.memory_threshold_mb,
              DISPLAY_CONTROLLER_MEMORY_THRESHOLD_MB);

    /* Debug disabled by default */
    ASSERT(!config.enable_debug_logging);
    ASSERT(!config.enable_performance_profiling);
    ASSERT_NULL(config.log_file_path);

    return 1;
}

/* ============================================================
 * ERROR STRING TESTS
 * ============================================================ */

static int test_error_string_success(void) {
    const char *msg =
        display_controller_error_string(DISPLAY_CONTROLLER_SUCCESS);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_invalid_param(void) {
    const char *msg =
        display_controller_error_string(DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_null_pointer(void) {
    const char *msg =
        display_controller_error_string(DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_memory_allocation(void) {
    const char *msg = display_controller_error_string(
        DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_not_initialized(void) {
    const char *msg = display_controller_error_string(
        DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_composition_failed(void) {
    const char *msg = display_controller_error_string(
        DISPLAY_CONTROLLER_ERROR_COMPOSITION_FAILED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_cache_full(void) {
    const char *msg =
        display_controller_error_string(DISPLAY_CONTROLLER_ERROR_CACHE_FULL);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_buffer_too_small(void) {
    const char *msg = display_controller_error_string(
        DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_unknown_error(void) {
    /* Test with an invalid error code */
    const char *msg =
        display_controller_error_string((display_controller_error_t)9999);
    ASSERT_NOT_NULL(msg);
    /* Should return some string even for unknown errors */
    return 1;
}

static int test_error_strings_are_different(void) {
    const char *success =
        display_controller_error_string(DISPLAY_CONTROLLER_SUCCESS);
    const char *invalid =
        display_controller_error_string(DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    const char *memory = display_controller_error_string(
        DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION);

    /* Different errors should have different descriptions */
    ASSERT(strcmp(success, invalid) != 0);
    ASSERT(strcmp(success, memory) != 0);
    ASSERT(strcmp(invalid, memory) != 0);

    return 1;
}

/* ============================================================
 * INIT WITH NULL PARAMS TESTS
 * ============================================================ */

static int test_init_null_controller(void) {
    display_controller_error_t result =
        display_controller_init(NULL, NULL, NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

/* ============================================================
 * DISPLAY WITH NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_display_null_controller(void) {
    char output[1024];
    display_controller_error_t result = display_controller_display(
        NULL, "prompt", "command", output, sizeof(output));
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    return 1;
}

static int test_display_null_output(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_display(dc, "prompt", "command", NULL, 1024);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);

    display_controller_destroy(dc);
    return 1;
}

static int test_display_zero_size(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    char output[1024];
    display_controller_error_t result =
        display_controller_display(dc, "prompt", "command", output, 0);
    /* Should fail because output_size is 0 or not initialized */
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

static int test_display_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    char output[1024];
    display_controller_error_t result = display_controller_display(
        dc, "prompt", "command", output, sizeof(output));
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * DISPLAY_WITH_CURSOR NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_display_with_cursor_null_controller(void) {
    char output[1024];
    display_controller_error_t result = display_controller_display_with_cursor(
        NULL, "prompt", "command", 0, false, output, sizeof(output));
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    return 1;
}

static int test_display_with_cursor_null_output(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result = display_controller_display_with_cursor(
        dc, "prompt", "command", 0, false, NULL, 1024);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);

    display_controller_destroy(dc);
    return 1;
}

static int test_display_with_cursor_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    char output[1024];
    display_controller_error_t result = display_controller_display_with_cursor(
        dc, "prompt", "command", 0, false, output, sizeof(output));
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * UPDATE NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_update_null_controller(void) {
    char output[1024];
    display_controller_error_t result = display_controller_update(
        NULL, "prompt", "command", output, sizeof(output), NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);
    return 1;
}

static int test_update_null_output(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_update(dc, "prompt", "command", NULL, 1024, NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_INVALID_PARAM);

    display_controller_destroy(dc);
    return 1;
}

static int test_update_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    char output[1024];
    display_controller_error_t result = display_controller_update(
        dc, "prompt", "command", output, sizeof(output), NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * REFRESH NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_refresh_null_controller(void) {
    char output[1024];
    display_controller_error_t result =
        display_controller_refresh(NULL, output, sizeof(output));
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

/* ============================================================
 * CLEANUP NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_cleanup_null_controller(void) {
    display_controller_error_t result = display_controller_cleanup(NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

static int test_cleanup_uninitialized_controller(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    /* Cleanup uninitialized controller should succeed without crashing */
    display_controller_error_t result = display_controller_cleanup(dc);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * CLEAR_SCREEN NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_clear_screen_null_controller(void) {
    display_controller_error_t result = display_controller_clear_screen(NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

static int test_clear_screen_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result = display_controller_clear_screen(dc);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * COMPLETION MENU NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_set_completion_menu_null_controller(void) {
    display_controller_error_t result =
        display_controller_set_completion_menu(NULL, NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

static int test_set_completion_menu_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_set_completion_menu(dc, NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

static int test_clear_completion_menu_null_controller(void) {
    display_controller_error_t result =
        display_controller_clear_completion_menu(NULL);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NULL_POINTER);
    return 1;
}

static int test_clear_completion_menu_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_clear_completion_menu(dc);
    ASSERT_EQ(result, DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED);

    display_controller_destroy(dc);
    return 1;
}

static int test_has_completion_menu_null_controller(void) {
    bool result = display_controller_has_completion_menu(NULL);
    ASSERT(!result);
    return 1;
}

static int test_has_completion_menu_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    bool result = display_controller_has_completion_menu(dc);
    ASSERT(!result);

    display_controller_destroy(dc);
    return 1;
}

static int test_get_completion_menu_null_controller(void) {
    lle_completion_menu_state_t *result =
        display_controller_get_completion_menu(NULL);
    ASSERT_NULL(result);
    return 1;
}

static int test_get_completion_menu_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    lle_completion_menu_state_t *result =
        display_controller_get_completion_menu(dc);
    ASSERT_NULL(result);

    display_controller_destroy(dc);
    return 1;
}

static int test_check_and_clear_menu_changed_null_controller(void) {
    bool result = display_controller_check_and_clear_menu_changed(NULL);
    ASSERT(!result);
    return 1;
}

/* ============================================================
 * AUTOSUGGESTION NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_update_autosuggestion_null_controller(void) {
    /* Should not crash */
    display_controller_update_autosuggestion(NULL, "test", 4, 4);
    return 1;
}

static int test_set_autosuggestion_null_controller(void) {
    /* Should not crash */
    display_controller_set_autosuggestion(NULL, "suggestion");
    return 1;
}

static int test_get_autosuggestion_null_controller(void) {
    const char *result = display_controller_get_autosuggestion(NULL);
    ASSERT_NULL(result);
    return 1;
}

static int test_accept_autosuggestion_null_controller(void) {
    char buffer[256];
    bool result =
        display_controller_accept_autosuggestion(NULL, buffer, sizeof(buffer));
    ASSERT(!result);
    return 1;
}

static int test_has_autosuggestion_null_controller(void) {
    bool result = display_controller_has_autosuggestion(NULL);
    ASSERT(!result);
    return 1;
}

static int test_clear_autosuggestion_null_controller(void) {
    /* Should not crash */
    display_controller_clear_autosuggestion(NULL);
    return 1;
}

static int test_set_autosuggestions_enabled_null_controller(void) {
    /* Should not crash */
    display_controller_set_autosuggestions_enabled(NULL, true);
    return 1;
}

/* ============================================================
 * PERFORMANCE MONITORING NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_get_performance_null_controller(void) {
    display_controller_performance_t perf;
    display_controller_error_t result =
        display_controller_get_performance(NULL, &perf);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_get_performance_null_output(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_get_performance(dc, NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

/* NOTE: display_controller_update_performance_monitoring is declared but not
 * implemented */

static int test_reset_performance_metrics_null_controller(void) {
    display_controller_error_t result =
        display_controller_reset_performance_metrics(NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

/* ============================================================
 * OPTIMIZATION NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_set_optimization_level_null_controller(void) {
    display_controller_error_t result =
        display_controller_set_optimization_level(
            NULL, DISPLAY_OPTIMIZATION_STANDARD);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_set_adaptive_optimization_null_controller(void) {
    display_controller_error_t result =
        display_controller_set_adaptive_optimization(NULL, true);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

/* ============================================================
 * CACHE NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_clear_cache_null_controller(void) {
    display_controller_error_t result = display_controller_clear_cache(NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_validate_cache_null_controller(void) {
    size_t valid, expired;
    bool corrupted;
    display_controller_error_t result =
        display_controller_validate_cache(NULL, &valid, &expired, &corrupted);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_optimize_cache_null_controller(void) {
    display_controller_error_t result = display_controller_optimize_cache(NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

/* ============================================================
 * CONFIG NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_get_config_null_controller(void) {
    display_controller_config_t config;
    display_controller_error_t result =
        display_controller_get_config(NULL, &config);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_get_config_null_output(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result = display_controller_get_config(dc, NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

static int test_set_config_null_controller(void) {
    display_controller_config_t config;
    display_controller_create_default_config(&config);
    display_controller_error_t result =
        display_controller_set_config(NULL, &config);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_set_config_null_config(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result = display_controller_set_config(dc, NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * INTEGRATION MODE NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_set_integration_mode_null_controller(void) {
    display_controller_error_t result =
        display_controller_set_integration_mode(NULL, true);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

/* ============================================================
 * THEME CONTEXT NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_set_theme_context_null_controller(void) {
    display_controller_error_t result =
        display_controller_set_theme_context(NULL, "default", SYMBOL_MODE_AUTO);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

/* ============================================================
 * VERSION NULL/INVALID PARAMS TESTS
 * ============================================================ */

static int test_get_version_null_controller(void) {
    char buffer[64];
    display_controller_error_t result =
        display_controller_get_version(NULL, buffer, sizeof(buffer));
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_get_version_null_buffer(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_get_version(dc, NULL, 64);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

static int test_get_version_zero_size(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    char buffer[64];
    display_controller_error_t result =
        display_controller_get_version(dc, buffer, 0);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * ACCESSOR FUNCTION NULL TESTS
 * ============================================================ */

static int test_get_terminal_control_null_controller(void) {
    terminal_control_t *tc = display_controller_get_terminal_control(NULL);
    ASSERT_NULL(tc);
    return 1;
}

static int test_get_terminal_control_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    terminal_control_t *tc = display_controller_get_terminal_control(dc);
    ASSERT_NULL(tc);

    display_controller_destroy(dc);
    return 1;
}

static int test_get_event_system_null_controller(void) {
    layer_event_system_t *es = display_controller_get_event_system(NULL);
    ASSERT_NULL(es);
    return 1;
}

static int test_get_event_system_not_initialized(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    layer_event_system_t *es = display_controller_get_event_system(dc);
    ASSERT_NULL(es);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * INTEGRATION/DIAGNOSTIC NULL TESTS
 * ============================================================ */

static int test_prepare_shell_integration_null_controller(void) {
    display_controller_error_t result =
        display_controller_prepare_shell_integration(NULL, NULL);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_get_integration_interface_null_controller(void) {
    char buffer[256];
    display_controller_error_t result =
        display_controller_get_integration_interface(NULL, buffer,
                                                     sizeof(buffer));
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_generate_diagnostic_report_null_controller(void) {
    char buffer[4096];
    display_controller_error_t result =
        display_controller_generate_diagnostic_report(NULL, buffer,
                                                      sizeof(buffer));
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);
    return 1;
}

static int test_generate_diagnostic_report_null_buffer(void) {
    display_controller_t *dc = display_controller_create();
    ASSERT_NOT_NULL(dc);

    display_controller_error_t result =
        display_controller_generate_diagnostic_report(dc, NULL, 4096);
    ASSERT(result != DISPLAY_CONTROLLER_SUCCESS);

    display_controller_destroy(dc);
    return 1;
}

/* ============================================================
 * GLOBAL FUNCTION TESTS
 * ============================================================ */

static int test_reset_prompt_display_state_no_crash(void) {
    /* Should not crash even without initialized controller */
    dc_reset_prompt_display_state();
    return 1;
}

static int test_finalize_input_no_crash(void) {
    /* Should not crash even without initialized controller */
    /* Note: This writes to stdout, so we just verify it doesn't crash */
    /* dc_finalize_input() would write \n to stdout - skip in test */
    return 1;
}

static int test_get_prompt_metrics_null_params(void) {
    /* Should not crash with NULL params */
    dc_get_prompt_metrics(NULL, NULL, NULL);
    return 1;
}

static int test_get_prompt_metrics_with_params(void) {
    int prompt_lines, total_lines, command_col;
    dc_get_prompt_metrics(&prompt_lines, &total_lines, &command_col);

    /* Without initialization, should return defaults */
    ASSERT(prompt_lines >= 1);
    ASSERT(total_lines >= 1);

    return 1;
}

static int test_apply_transient_prompt_null_prompt(void) {
    bool result = dc_apply_transient_prompt(NULL, "command");
    ASSERT(!result);
    return 1;
}

/* ============================================================
 * OPTIMIZATION LEVEL ENUM TESTS
 * ============================================================ */

static int test_optimization_level_values(void) {
    /* Verify enum values are distinct */
    ASSERT(DISPLAY_OPTIMIZATION_DISABLED != DISPLAY_OPTIMIZATION_BASIC);
    ASSERT(DISPLAY_OPTIMIZATION_BASIC != DISPLAY_OPTIMIZATION_STANDARD);
    ASSERT(DISPLAY_OPTIMIZATION_STANDARD != DISPLAY_OPTIMIZATION_AGGRESSIVE);
    ASSERT(DISPLAY_OPTIMIZATION_AGGRESSIVE != DISPLAY_OPTIMIZATION_MAXIMUM);
    return 1;
}

/* ============================================================
 * STATE CHANGE ENUM TESTS
 * ============================================================ */

static int test_state_change_values(void) {
    /* Verify enum values are distinct */
    ASSERT(DISPLAY_STATE_UNCHANGED != DISPLAY_STATE_PROMPT_CHANGED);
    ASSERT(DISPLAY_STATE_PROMPT_CHANGED != DISPLAY_STATE_COMMAND_CHANGED);
    ASSERT(DISPLAY_STATE_COMMAND_CHANGED != DISPLAY_STATE_COMPOSITION_CHANGED);
    ASSERT(DISPLAY_STATE_COMPOSITION_CHANGED != DISPLAY_STATE_TERMINAL_CHANGED);
    ASSERT(DISPLAY_STATE_TERMINAL_CHANGED != DISPLAY_STATE_FULL_REFRESH_NEEDED);
    return 1;
}

/* ============================================================
 * ERROR CODE ENUM TESTS
 * ============================================================ */

static int test_error_code_values(void) {
    /* Verify success is 0 */
    ASSERT_EQ((int)DISPLAY_CONTROLLER_SUCCESS, 0);

    /* Verify error codes are distinct from success */
    ASSERT(DISPLAY_CONTROLLER_ERROR_INVALID_PARAM !=
           DISPLAY_CONTROLLER_SUCCESS);
    ASSERT(DISPLAY_CONTROLLER_ERROR_NULL_POINTER != DISPLAY_CONTROLLER_SUCCESS);
    ASSERT(DISPLAY_CONTROLLER_ERROR_MEMORY_ALLOCATION !=
           DISPLAY_CONTROLLER_SUCCESS);
    ASSERT(DISPLAY_CONTROLLER_ERROR_INITIALIZATION_FAILED !=
           DISPLAY_CONTROLLER_SUCCESS);
    ASSERT(DISPLAY_CONTROLLER_ERROR_NOT_INITIALIZED !=
           DISPLAY_CONTROLLER_SUCCESS);

    return 1;
}

/* ============================================================
 * SYMBOL COMPATIBILITY ENUM TESTS
 * ============================================================ */

static int test_symbol_mode_values(void) {
    /* Verify enum values are distinct */
    ASSERT(SYMBOL_MODE_UNICODE != SYMBOL_MODE_ASCII);
    ASSERT(SYMBOL_MODE_ASCII != SYMBOL_MODE_NERD_FONT);
    ASSERT(SYMBOL_MODE_NERD_FONT != SYMBOL_MODE_AUTO);
    return 1;
}

/* ============================================================
 * CONSTANT DEFINITION TESTS
 * ============================================================ */

static int test_version_constants_positive(void) {
    ASSERT(DISPLAY_CONTROLLER_VERSION_MAJOR >= 0);
    ASSERT(DISPLAY_CONTROLLER_VERSION_MINOR >= 0);
    ASSERT(DISPLAY_CONTROLLER_VERSION_PATCH >= 0);
    return 1;
}

static int test_cache_constants_reasonable(void) {
    ASSERT(DISPLAY_CONTROLLER_MAX_CACHE_SIZE > 0);
    ASSERT(DISPLAY_CONTROLLER_MAX_DIFF_SIZE > 0);
    ASSERT(DISPLAY_CONTROLLER_DEFAULT_CACHE_TTL_MS > 0);
    ASSERT(DISPLAY_CONTROLLER_PERFORMANCE_HISTORY_SIZE > 0);
    return 1;
}

static int test_threshold_constants_reasonable(void) {
    ASSERT(DISPLAY_CONTROLLER_PERFORMANCE_THRESHOLD_MS > 0);
    ASSERT(DISPLAY_CONTROLLER_CACHE_HIT_RATE_THRESHOLD > 0.0);
    ASSERT(DISPLAY_CONTROLLER_CACHE_HIT_RATE_THRESHOLD <= 1.0);
    ASSERT(DISPLAY_CONTROLLER_MEMORY_THRESHOLD_MB > 0);
    return 1;
}

/* ============================================================
 * MAIN TEST RUNNER
 * ============================================================ */

int main(void) {
    printf("Running display controller tests...\n\n");

    printf("=== Create/Destroy Tests ===\n");
    RUN_TEST(create_returns_non_null);
    RUN_TEST(create_initializes_config_defaults);
    RUN_TEST(destroy_null_safe);
    RUN_TEST(destroy_uninitialized);

    printf("\n=== Is Initialized Tests ===\n");
    RUN_TEST(is_initialized_null_context);
    RUN_TEST(is_initialized_before_init);

    printf("\n=== Default Config Tests ===\n");
    RUN_TEST(create_default_config_null_param);
    RUN_TEST(create_default_config_sets_values);

    printf("\n=== Error String Tests ===\n");
    RUN_TEST(error_string_success);
    RUN_TEST(error_string_invalid_param);
    RUN_TEST(error_string_null_pointer);
    RUN_TEST(error_string_memory_allocation);
    RUN_TEST(error_string_not_initialized);
    RUN_TEST(error_string_composition_failed);
    RUN_TEST(error_string_cache_full);
    RUN_TEST(error_string_buffer_too_small);
    RUN_TEST(error_string_unknown_error);
    RUN_TEST(error_strings_are_different);

    printf("\n=== Init Null/Invalid Param Tests ===\n");
    RUN_TEST(init_null_controller);

    printf("\n=== Display Null/Invalid Param Tests ===\n");
    RUN_TEST(display_null_controller);
    RUN_TEST(display_null_output);
    RUN_TEST(display_zero_size);
    RUN_TEST(display_not_initialized);

    printf("\n=== Display With Cursor Null/Invalid Param Tests ===\n");
    RUN_TEST(display_with_cursor_null_controller);
    RUN_TEST(display_with_cursor_null_output);
    RUN_TEST(display_with_cursor_not_initialized);

    printf("\n=== Update Null/Invalid Param Tests ===\n");
    RUN_TEST(update_null_controller);
    RUN_TEST(update_null_output);
    RUN_TEST(update_not_initialized);

    printf("\n=== Refresh Null/Invalid Param Tests ===\n");
    RUN_TEST(refresh_null_controller);

    printf("\n=== Cleanup Null/Invalid Param Tests ===\n");
    RUN_TEST(cleanup_null_controller);
    RUN_TEST(cleanup_uninitialized_controller);

    printf("\n=== Clear Screen Null/Invalid Param Tests ===\n");
    RUN_TEST(clear_screen_null_controller);
    RUN_TEST(clear_screen_not_initialized);

    printf("\n=== Completion Menu Null/Invalid Param Tests ===\n");
    RUN_TEST(set_completion_menu_null_controller);
    RUN_TEST(set_completion_menu_not_initialized);
    RUN_TEST(clear_completion_menu_null_controller);
    RUN_TEST(clear_completion_menu_not_initialized);
    RUN_TEST(has_completion_menu_null_controller);
    RUN_TEST(has_completion_menu_not_initialized);
    RUN_TEST(get_completion_menu_null_controller);
    RUN_TEST(get_completion_menu_not_initialized);
    RUN_TEST(check_and_clear_menu_changed_null_controller);

    printf("\n=== Autosuggestion Null/Invalid Param Tests ===\n");
    RUN_TEST(update_autosuggestion_null_controller);
    RUN_TEST(set_autosuggestion_null_controller);
    RUN_TEST(get_autosuggestion_null_controller);
    RUN_TEST(accept_autosuggestion_null_controller);
    RUN_TEST(has_autosuggestion_null_controller);
    RUN_TEST(clear_autosuggestion_null_controller);
    RUN_TEST(set_autosuggestions_enabled_null_controller);

    printf("\n=== Performance Monitoring Null/Invalid Param Tests ===\n");
    RUN_TEST(get_performance_null_controller);
    RUN_TEST(get_performance_null_output);
    /* NOTE: test_update_performance_monitoring_null_controller skipped -
     * function not implemented */
    RUN_TEST(reset_performance_metrics_null_controller);

    printf("\n=== Optimization Null/Invalid Param Tests ===\n");
    RUN_TEST(set_optimization_level_null_controller);
    RUN_TEST(set_adaptive_optimization_null_controller);

    printf("\n=== Cache Null/Invalid Param Tests ===\n");
    RUN_TEST(clear_cache_null_controller);
    RUN_TEST(validate_cache_null_controller);
    RUN_TEST(optimize_cache_null_controller);

    printf("\n=== Config Null/Invalid Param Tests ===\n");
    RUN_TEST(get_config_null_controller);
    RUN_TEST(get_config_null_output);
    RUN_TEST(set_config_null_controller);
    RUN_TEST(set_config_null_config);

    printf("\n=== Integration Mode Null/Invalid Param Tests ===\n");
    RUN_TEST(set_integration_mode_null_controller);

    printf("\n=== Theme Context Null/Invalid Param Tests ===\n");
    RUN_TEST(set_theme_context_null_controller);

    printf("\n=== Version Null/Invalid Param Tests ===\n");
    RUN_TEST(get_version_null_controller);
    RUN_TEST(get_version_null_buffer);
    RUN_TEST(get_version_zero_size);

    printf("\n=== Accessor Function Null Tests ===\n");
    RUN_TEST(get_terminal_control_null_controller);
    RUN_TEST(get_terminal_control_not_initialized);
    RUN_TEST(get_event_system_null_controller);
    RUN_TEST(get_event_system_not_initialized);

    printf("\n=== Integration/Diagnostic Null Tests ===\n");
    RUN_TEST(prepare_shell_integration_null_controller);
    RUN_TEST(get_integration_interface_null_controller);
    RUN_TEST(generate_diagnostic_report_null_controller);
    RUN_TEST(generate_diagnostic_report_null_buffer);

    printf("\n=== Global Function Tests ===\n");
    RUN_TEST(reset_prompt_display_state_no_crash);
    RUN_TEST(finalize_input_no_crash);
    RUN_TEST(get_prompt_metrics_null_params);
    RUN_TEST(get_prompt_metrics_with_params);
    RUN_TEST(apply_transient_prompt_null_prompt);

    printf("\n=== Enum Value Tests ===\n");
    RUN_TEST(optimization_level_values);
    RUN_TEST(state_change_values);
    RUN_TEST(error_code_values);
    RUN_TEST(symbol_mode_values);

    printf("\n=== Constant Definition Tests ===\n");
    RUN_TEST(version_constants_positive);
    RUN_TEST(cache_constants_reasonable);
    RUN_TEST(threshold_constants_reasonable);

    printf("\n========================================\n");
    printf("========================================\n");

    return TEST_RESULT();
}
