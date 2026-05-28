/**
 * @file test_composition_engine.c
 * @brief Unit tests for composition engine layer
 *
 * Tests the composition engine layer's layer combination, prompt analysis,
 * positioning calculations, caching, and cursor tracking functionality.
 *
 * The composition_engine layer intelligently combines prompt and command
 * layers. These tests focus on functions that can be tested in isolation:
 * error handling, strategy strings, and creation/destruction.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/composition_engine.h"
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
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b), #a " == " #b)
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0, "strings equal")
#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL, #p " is NULL")
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL, #p " is non-NULL")

/// Test framework macros

/* ============================================================
 * ERROR STRING TESTS
 * ============================================================ */

static int test_error_string_success(void) {
    const char *msg =
        composition_engine_error_string(COMPOSITION_ENGINE_SUCCESS);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Success");
    return 1;
}

static int test_error_string_invalid_param(void) {
    const char *msg =
        composition_engine_error_string(COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Invalid parameter");
    return 1;
}

static int test_error_string_null_pointer(void) {
    const char *msg =
        composition_engine_error_string(COMPOSITION_ENGINE_ERROR_NULL_POINTER);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "NULL pointer");
    return 1;
}

static int test_error_string_memory_allocation(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_MEMORY_ALLOCATION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Memory allocation failed");
    return 1;
}

static int test_error_string_buffer_too_small(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_BUFFER_TOO_SMALL);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Buffer too small");
    return 1;
}

static int test_error_string_content_too_large(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_CONTENT_TOO_LARGE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Content too large");
    return 1;
}

static int test_error_string_layer_not_ready(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_LAYER_NOT_READY);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Layer not ready");
    return 1;
}

static int test_error_string_analysis_failed(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_ANALYSIS_FAILED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Analysis failed");
    return 1;
}

static int test_error_string_composition_failed(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_COMPOSITION_FAILED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Composition failed");
    return 1;
}

static int test_error_string_cache_invalid(void) {
    const char *msg =
        composition_engine_error_string(COMPOSITION_ENGINE_ERROR_CACHE_INVALID);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Cache invalid");
    return 1;
}

static int test_error_string_event_failed(void) {
    const char *msg =
        composition_engine_error_string(COMPOSITION_ENGINE_ERROR_EVENT_FAILED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Event handling failed");
    return 1;
}

static int test_error_string_not_initialized(void) {
    const char *msg = composition_engine_error_string(
        COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Engine not initialized");
    return 1;
}

static int test_error_string_unknown(void) {
    const char *msg =
        composition_engine_error_string((composition_engine_error_t)999);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    ASSERT_STR_EQ(msg, "Unknown error");
    return 1;
}

/* ============================================================
 * STRATEGY STRING TESTS
 * ============================================================ */

static int test_strategy_string_simple(void) {
    const char *msg =
        composition_engine_strategy_string(COMPOSITION_STRATEGY_SIMPLE);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Simple");
    return 1;
}

static int test_strategy_string_multiline(void) {
    const char *msg =
        composition_engine_strategy_string(COMPOSITION_STRATEGY_MULTILINE);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Multiline");
    return 1;
}

static int test_strategy_string_complex(void) {
    const char *msg =
        composition_engine_strategy_string(COMPOSITION_STRATEGY_COMPLEX);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Complex");
    return 1;
}

static int test_strategy_string_ascii_art(void) {
    const char *msg =
        composition_engine_strategy_string(COMPOSITION_STRATEGY_ASCII_ART);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "ASCII Art");
    return 1;
}

static int test_strategy_string_adaptive(void) {
    const char *msg =
        composition_engine_strategy_string(COMPOSITION_STRATEGY_ADAPTIVE);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Adaptive");
    return 1;
}

static int test_strategy_string_unknown(void) {
    const char *msg =
        composition_engine_strategy_string((composition_strategy_t)999);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Unknown");
    return 1;
}

/* ============================================================
 * CREATE/DESTROY TESTS
 * ============================================================ */

static int test_create_returns_valid_engine(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);
    composition_engine_destroy(engine);
    return 1;
}

static int test_create_initializes_defaults(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// Check default configuration
    ASSERT_EQ(engine->intelligent_positioning, true);
    ASSERT_EQ(engine->adaptive_strategy, true);
    ASSERT_EQ(engine->performance_monitoring, true);
    ASSERT_EQ(engine->current_strategy, COMPOSITION_STRATEGY_ADAPTIVE);

    /// Not initialized until composition_engine_init() called
    ASSERT_EQ(engine->initialized, false);

    composition_engine_destroy(engine);
    return 1;
}

static int test_create_initializes_version_string(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// Version string should be set
    ASSERT(strlen(engine->version_string) > 0);

    composition_engine_destroy(engine);
    return 1;
}

static int test_destroy_null_engine(void) {
    /// Should not crash
    composition_engine_destroy(NULL);
    return 1;
}

static int test_destroy_cleans_up_resources(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// Destroy should clean up
    composition_engine_destroy(engine);
    /// If we get here without crash, success
    return 1;
}

/* ============================================================
 * INITIALIZATION TESTS
 * ============================================================ */

static int test_init_null_engine(void) {
    composition_engine_error_t result =
        composition_engine_init(NULL, NULL, NULL, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_init_null_prompt_layer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result = composition_engine_init(
        engine, NULL, (command_layer_t *)0x1, (layer_event_system_t *)0x1);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_init_null_command_layer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result = composition_engine_init(
        engine, (prompt_layer_t *)0x1, NULL, (layer_event_system_t *)0x1);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_init_null_event_system(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result = composition_engine_init(
        engine, (prompt_layer_t *)0x1, (command_layer_t *)0x1, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * IS_INITIALIZED TESTS
 * ============================================================ */

static int test_is_initialized_null_engine(void) {
    bool result = composition_engine_is_initialized(NULL);
    ASSERT_EQ(result, false);
    return 1;
}

static int test_is_initialized_uninitialized_engine(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    bool result = composition_engine_is_initialized(engine);
    ASSERT_EQ(result, false);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * CLEANUP TESTS
 * ============================================================ */

static int test_cleanup_null_engine(void) {
    composition_engine_error_t result = composition_engine_cleanup(NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_cleanup_uninitialized_engine(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// Should not crash even if not initialized
    composition_engine_error_t result = composition_engine_cleanup(engine);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * GET OUTPUT TESTS
 * ============================================================ */

static int test_get_output_null_engine(void) {
    char output[256];
    composition_engine_error_t result =
        composition_engine_get_output(NULL, output, sizeof(output));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_output_null_buffer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_get_output(engine, NULL, 256);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_output_zero_size(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char output[256];
    composition_engine_error_t result =
        composition_engine_get_output(engine, output, 0);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_output_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char output[256];
    composition_engine_error_t result =
        composition_engine_get_output(engine, output, sizeof(output));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * GET ANALYSIS TESTS
 * ============================================================ */

static int test_get_analysis_null_engine(void) {
    composition_analysis_t analysis;
    composition_engine_error_t result =
        composition_engine_get_analysis(NULL, &analysis);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_analysis_null_analysis(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_get_analysis(engine, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_analysis_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_analysis_t analysis;
    composition_engine_error_t result =
        composition_engine_get_analysis(engine, &analysis);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * GET POSITIONING TESTS
 * ============================================================ */

static int test_get_positioning_null_engine(void) {
    composition_positioning_t positioning;
    composition_engine_error_t result =
        composition_engine_get_positioning(NULL, &positioning);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_positioning_null_positioning(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_get_positioning(engine, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_positioning_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_positioning_t positioning;
    composition_engine_error_t result =
        composition_engine_get_positioning(engine, &positioning);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * GET PERFORMANCE TESTS
 * ============================================================ */

static int test_get_performance_null_engine(void) {
    composition_performance_t performance;
    composition_engine_error_t result =
        composition_engine_get_performance(NULL, &performance);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_performance_null_performance(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_get_performance(engine, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_performance_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_performance_t performance;
    composition_engine_error_t result =
        composition_engine_get_performance(engine, &performance);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * SET STRATEGY TESTS
 * ============================================================ */

static int test_set_strategy_null_engine(void) {
    composition_engine_error_t result =
        composition_engine_set_strategy(NULL, COMPOSITION_STRATEGY_SIMPLE);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_strategy_invalid_strategy(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_strategy(engine, (composition_strategy_t)999);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_set_strategy_valid_strategy(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_strategy(engine, COMPOSITION_STRATEGY_MULTILINE);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->current_strategy, COMPOSITION_STRATEGY_MULTILINE);

    composition_engine_destroy(engine);
    return 1;
}

static int test_set_strategy_invalidates_cache(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    engine->composition_cache_valid = true;

    composition_engine_set_strategy(engine, COMPOSITION_STRATEGY_SIMPLE);

    ASSERT_EQ(engine->composition_cache_valid, false);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * SET INTELLIGENT POSITIONING TESTS
 * ============================================================ */

static int test_set_intelligent_positioning_null_engine(void) {
    composition_engine_error_t result =
        composition_engine_set_intelligent_positioning(NULL, true);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_intelligent_positioning_enable(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_intelligent_positioning(engine, true);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->intelligent_positioning, true);

    composition_engine_destroy(engine);
    return 1;
}

static int test_set_intelligent_positioning_disable(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_intelligent_positioning(engine, false);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->intelligent_positioning, false);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * SET PERFORMANCE MONITORING TESTS
 * ============================================================ */

static int test_set_performance_monitoring_null_engine(void) {
    composition_engine_error_t result =
        composition_engine_set_performance_monitoring(NULL, true);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_performance_monitoring_enable(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_performance_monitoring(engine, true);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->performance_monitoring, true);

    composition_engine_destroy(engine);
    return 1;
}

static int test_set_performance_monitoring_disable(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_performance_monitoring(engine, false);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->performance_monitoring, false);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * SET CACHE MAX AGE TESTS
 * ============================================================ */

static int test_set_cache_max_age_null_engine(void) {
    composition_engine_error_t result =
        composition_engine_set_cache_max_age(NULL, 100);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_cache_max_age_valid(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_cache_max_age(engine, 1000);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->max_cache_age_ms, 1000);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * CLEAR CACHE TESTS
 * ============================================================ */

static int test_clear_cache_null_engine(void) {
    composition_engine_error_t result = composition_engine_clear_cache(NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_clear_cache_valid(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    engine->composition_cache_valid = true;

    composition_engine_error_t result = composition_engine_clear_cache(engine);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->composition_cache_valid, false);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * SET SCREEN BUFFER TESTS
 * ============================================================ */

static int test_set_screen_buffer_null_engine(void) {
    screen_buffer_t buffer;
    composition_engine_error_t result =
        composition_engine_set_screen_buffer(NULL, &buffer);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_set_screen_buffer_valid(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);

    composition_engine_error_t result =
        composition_engine_set_screen_buffer(engine, &buffer);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(engine->screen_buffer, &buffer);

    screen_buffer_cleanup(&buffer);
    composition_engine_destroy(engine);
    return 1;
}

static int test_set_screen_buffer_null_buffer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_set_screen_buffer(engine, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_NULL(engine->screen_buffer);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * GET VERSION TESTS
 * ============================================================ */

static int test_get_version_null_engine(void) {
    char version[32];
    composition_engine_error_t result =
        composition_engine_get_version(NULL, version, sizeof(version));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_get_version_null_buffer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_get_version(engine, NULL, 32);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_version_zero_size(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char version[32];
    composition_engine_error_t result =
        composition_engine_get_version(engine, version, 0);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_get_version_valid(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char version[32];
    composition_engine_error_t result =
        composition_engine_get_version(engine, version, sizeof(version));
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);

    /// Version should be in format x.y.z
    ASSERT(strlen(version) > 0);
    ASSERT(strchr(version, '.') != NULL);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * VALIDATE CACHE TESTS
 * ============================================================ */

static int test_validate_cache_null_engine(void) {
    size_t valid = 0, expired = 0;
    composition_engine_error_t result =
        composition_engine_validate_cache(NULL, &valid, &expired);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_validate_cache_null_outputs(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// NULL outputs should be allowed
    composition_engine_error_t result =
        composition_engine_validate_cache(engine, NULL, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);

    composition_engine_destroy(engine);
    return 1;
}

static int test_validate_cache_empty(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    size_t valid = 999, expired = 999;
    composition_engine_error_t result =
        composition_engine_validate_cache(engine, &valid, &expired);
    ASSERT_EQ(result, COMPOSITION_ENGINE_SUCCESS);
    ASSERT_EQ(valid, 0);
    ASSERT_EQ(expired, 0);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * COMPOSE TESTS (require initialized engine)
 * ============================================================ */

static int test_compose_null_engine(void) {
    composition_engine_error_t result = composition_engine_compose(NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);
    return 1;
}

static int test_compose_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result = composition_engine_compose(engine);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * COMPOSE WITH CURSOR TESTS
 * ============================================================ */

static int test_compose_with_cursor_null_engine(void) {
    composition_with_cursor_t result_buf;
    composition_engine_error_t result =
        composition_engine_compose_with_cursor(NULL, 0, 80, &result_buf);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_compose_with_cursor_null_result(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_compose_with_cursor(engine, 0, 80, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_compose_with_cursor_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_with_cursor_t result_buf;
    composition_engine_error_t result =
        composition_engine_compose_with_cursor(engine, 0, 80, &result_buf);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

static int test_compose_with_cursor_invalid_width(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    /// Invalid width should use fallback of 80
    composition_with_cursor_t result_buf;
    composition_engine_error_t result =
        composition_engine_compose_with_cursor(engine, 0, -1, &result_buf);
    /// Should return NOT_INITIALIZED since engine not initialized
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * ANALYZE PROMPT TESTS
 * ============================================================ */

static int test_analyze_prompt_null_engine(void) {
    composition_analysis_t analysis;
    composition_engine_error_t result =
        composition_engine_analyze_prompt(NULL, &analysis);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_analyze_prompt_null_analysis(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_analyze_prompt(engine, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_analyze_prompt_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_analysis_t analysis;
    composition_engine_error_t result =
        composition_engine_analyze_prompt(engine, &analysis);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * CALCULATE POSITIONING TESTS
 * ============================================================ */

static int test_calculate_positioning_null_engine(void) {
    composition_analysis_t analysis = {0};
    composition_positioning_t positioning;
    composition_engine_error_t result =
        composition_engine_calculate_positioning(NULL, &analysis, &positioning);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_calculate_positioning_null_analysis(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_positioning_t positioning;
    composition_engine_error_t result =
        composition_engine_calculate_positioning(engine, NULL, &positioning);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_calculate_positioning_null_positioning(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_analysis_t analysis = {0};
    composition_engine_error_t result =
        composition_engine_calculate_positioning(engine, &analysis, NULL);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_calculate_positioning_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_analysis_t analysis = {0};
    composition_positioning_t positioning;
    composition_engine_error_t result =
        composition_engine_calculate_positioning(engine, &analysis,
                                                 &positioning);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * CALCULATE HASH TESTS
 * ============================================================ */

static int test_calculate_hash_null_engine(void) {
    char hash[32];
    composition_engine_error_t result =
        composition_engine_calculate_hash(NULL, hash, sizeof(hash));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);
    return 1;
}

static int test_calculate_hash_null_buffer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    composition_engine_error_t result =
        composition_engine_calculate_hash(engine, NULL, 32);
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_calculate_hash_small_buffer(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char hash[8]; /// Too small
    composition_engine_error_t result =
        composition_engine_calculate_hash(engine, hash, sizeof(hash));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_INVALID_PARAM);

    composition_engine_destroy(engine);
    return 1;
}

static int test_calculate_hash_not_initialized(void) {
    composition_engine_t *engine = composition_engine_create();
    ASSERT_NOT_NULL(engine);

    char hash[32];
    composition_engine_error_t result =
        composition_engine_calculate_hash(engine, hash, sizeof(hash));
    ASSERT_EQ(result, COMPOSITION_ENGINE_ERROR_NOT_INITIALIZED);

    composition_engine_destroy(engine);
    return 1;
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void) {
    printf("=== Composition Engine Unit Tests ===\n\n");

    printf("=== Error String Tests ===\n");
    RUN_TEST(error_string_success);
    RUN_TEST(error_string_invalid_param);
    RUN_TEST(error_string_null_pointer);
    RUN_TEST(error_string_memory_allocation);
    RUN_TEST(error_string_buffer_too_small);
    RUN_TEST(error_string_content_too_large);
    RUN_TEST(error_string_layer_not_ready);
    RUN_TEST(error_string_analysis_failed);
    RUN_TEST(error_string_composition_failed);
    RUN_TEST(error_string_cache_invalid);
    RUN_TEST(error_string_event_failed);
    RUN_TEST(error_string_not_initialized);
    RUN_TEST(error_string_unknown);

    printf("\n=== Strategy String Tests ===\n");
    RUN_TEST(strategy_string_simple);
    RUN_TEST(strategy_string_multiline);
    RUN_TEST(strategy_string_complex);
    RUN_TEST(strategy_string_ascii_art);
    RUN_TEST(strategy_string_adaptive);
    RUN_TEST(strategy_string_unknown);

    printf("\n=== Create/Destroy Tests ===\n");
    RUN_TEST(create_returns_valid_engine);
    RUN_TEST(create_initializes_defaults);
    RUN_TEST(create_initializes_version_string);
    RUN_TEST(destroy_null_engine);
    RUN_TEST(destroy_cleans_up_resources);

    printf("\n=== Initialization Tests ===\n");
    RUN_TEST(init_null_engine);
    RUN_TEST(init_null_prompt_layer);
    RUN_TEST(init_null_command_layer);
    RUN_TEST(init_null_event_system);

    printf("\n=== Is Initialized Tests ===\n");
    RUN_TEST(is_initialized_null_engine);
    RUN_TEST(is_initialized_uninitialized_engine);

    printf("\n=== Cleanup Tests ===\n");
    RUN_TEST(cleanup_null_engine);
    RUN_TEST(cleanup_uninitialized_engine);

    printf("\n=== Get Output Tests ===\n");
    RUN_TEST(get_output_null_engine);
    RUN_TEST(get_output_null_buffer);
    RUN_TEST(get_output_zero_size);
    RUN_TEST(get_output_not_initialized);

    printf("\n=== Get Analysis Tests ===\n");
    RUN_TEST(get_analysis_null_engine);
    RUN_TEST(get_analysis_null_analysis);
    RUN_TEST(get_analysis_not_initialized);

    printf("\n=== Get Positioning Tests ===\n");
    RUN_TEST(get_positioning_null_engine);
    RUN_TEST(get_positioning_null_positioning);
    RUN_TEST(get_positioning_not_initialized);

    printf("\n=== Get Performance Tests ===\n");
    RUN_TEST(get_performance_null_engine);
    RUN_TEST(get_performance_null_performance);
    RUN_TEST(get_performance_not_initialized);

    printf("\n=== Set Strategy Tests ===\n");
    RUN_TEST(set_strategy_null_engine);
    RUN_TEST(set_strategy_invalid_strategy);
    RUN_TEST(set_strategy_valid_strategy);
    RUN_TEST(set_strategy_invalidates_cache);

    printf("\n=== Set Intelligent Positioning Tests ===\n");
    RUN_TEST(set_intelligent_positioning_null_engine);
    RUN_TEST(set_intelligent_positioning_enable);
    RUN_TEST(set_intelligent_positioning_disable);

    printf("\n=== Set Performance Monitoring Tests ===\n");
    RUN_TEST(set_performance_monitoring_null_engine);
    RUN_TEST(set_performance_monitoring_enable);
    RUN_TEST(set_performance_monitoring_disable);

    printf("\n=== Set Cache Max Age Tests ===\n");
    RUN_TEST(set_cache_max_age_null_engine);
    RUN_TEST(set_cache_max_age_valid);

    printf("\n=== Clear Cache Tests ===\n");
    RUN_TEST(clear_cache_null_engine);
    RUN_TEST(clear_cache_valid);

    printf("\n=== Set Screen Buffer Tests ===\n");
    RUN_TEST(set_screen_buffer_null_engine);
    RUN_TEST(set_screen_buffer_valid);
    RUN_TEST(set_screen_buffer_null_buffer);

    printf("\n=== Get Version Tests ===\n");
    RUN_TEST(get_version_null_engine);
    RUN_TEST(get_version_null_buffer);
    RUN_TEST(get_version_zero_size);
    RUN_TEST(get_version_valid);

    printf("\n=== Validate Cache Tests ===\n");
    RUN_TEST(validate_cache_null_engine);
    RUN_TEST(validate_cache_null_outputs);
    RUN_TEST(validate_cache_empty);

    printf("\n=== Compose Tests ===\n");
    RUN_TEST(compose_null_engine);
    RUN_TEST(compose_not_initialized);

    printf("\n=== Compose with Cursor Tests ===\n");
    RUN_TEST(compose_with_cursor_null_engine);
    RUN_TEST(compose_with_cursor_null_result);
    RUN_TEST(compose_with_cursor_not_initialized);
    RUN_TEST(compose_with_cursor_invalid_width);

    printf("\n=== Analyze Prompt Tests ===\n");
    RUN_TEST(analyze_prompt_null_engine);
    RUN_TEST(analyze_prompt_null_analysis);
    RUN_TEST(analyze_prompt_not_initialized);

    printf("\n=== Calculate Positioning Tests ===\n");
    RUN_TEST(calculate_positioning_null_engine);
    RUN_TEST(calculate_positioning_null_analysis);
    RUN_TEST(calculate_positioning_null_positioning);
    RUN_TEST(calculate_positioning_not_initialized);

    printf("\n=== Calculate Hash Tests ===\n");
    RUN_TEST(calculate_hash_null_engine);
    RUN_TEST(calculate_hash_null_buffer);
    RUN_TEST(calculate_hash_small_buffer);
    RUN_TEST(calculate_hash_not_initialized);

    printf("\n=== Summary ===\n");
    return TEST_RESULT();
}
