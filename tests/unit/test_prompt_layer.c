/**
 * @file test_prompt_layer.c
 * @brief Unit tests for prompt layer
 *
 * Tests the prompt layer's universal prompt rendering system including
 * content management, theme integration, performance monitoring, and
 * error handling.
 *
 * The prompt_layer provides universal prompt rendering that works with
 * any prompt structure without requiring parsing or modification.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display/layer_events.h"
#include "display/prompt_layer.h"
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

/// Helper to create an initialized event system for tests
static layer_event_system_t *create_test_event_system(void) {
    layer_event_system_t *events = layer_events_create(NULL);
    if (events) {
        layer_events_init(events);
    }
    return events;
}

/// Test framework macros

/* ============================================================
 * ERROR STRING TESTS
 * ============================================================ */

static int test_error_string_success(void) {
    const char *msg = prompt_layer_error_string(PROMPT_LAYER_SUCCESS);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Success");
    return 1;
}

static int test_error_string_invalid_param(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_INVALID_PARAM);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Invalid parameter");
    return 1;
}

static int test_error_string_null_pointer(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_NULL_POINTER);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_memory_allocation(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_MEMORY_ALLOCATION);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_buffer_too_small(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_BUFFER_TOO_SMALL);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_content_too_large(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_CONTENT_TOO_LARGE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_theme_not_available(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_THEME_NOT_AVAILABLE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_event_system_failure(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_EVENT_SYSTEM_FAILURE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_rendering_failure(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_RENDERING_FAILURE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_invalid_state(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_INVALID_STATE);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_performance_timeout(void) {
    const char *msg =
        prompt_layer_error_string(PROMPT_LAYER_ERROR_PERFORMANCE_TIMEOUT);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_unknown(void) {
    const char *msg = prompt_layer_error_string(PROMPT_LAYER_ERROR_UNKNOWN);
    ASSERT_NOT_NULL(msg);
    ASSERT(strlen(msg) > 0);
    return 1;
}

static int test_error_string_invalid_code(void) {
    /// An out-of-range code falls through to a generic message.
    const char *msg = prompt_layer_error_string((prompt_layer_error_t)999);
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_EQ(msg, "Unknown error");
    return 1;
}

/* ============================================================
 * VERSION TESTS
 * ============================================================ */

static int test_get_version_all_null(void) {
    /// NULL pointers are skipped; real pointers receive the version triple.
    prompt_layer_get_version(NULL, NULL, NULL);
    int major = -1, minor = -1, patch = -1;
    prompt_layer_get_version(&major, &minor, &patch);
    ASSERT_EQ(major, PROMPT_LAYER_VERSION_MAJOR);
    ASSERT_EQ(minor, PROMPT_LAYER_VERSION_MINOR);
    ASSERT_EQ(patch, PROMPT_LAYER_VERSION_PATCH);
    return 1;
}

static int test_get_version_major_only(void) {
    int major = -1;
    prompt_layer_get_version(&major, NULL, NULL);
    ASSERT_EQ(major, PROMPT_LAYER_VERSION_MAJOR);
    return 1;
}

static int test_get_version_minor_only(void) {
    int minor = -1;
    prompt_layer_get_version(NULL, &minor, NULL);
    ASSERT_EQ(minor, PROMPT_LAYER_VERSION_MINOR);
    return 1;
}

static int test_get_version_patch_only(void) {
    int patch = -1;
    prompt_layer_get_version(NULL, NULL, &patch);
    ASSERT_EQ(patch, PROMPT_LAYER_VERSION_PATCH);
    return 1;
}

static int test_get_version_all(void) {
    int major = -1, minor = -1, patch = -1;
    prompt_layer_get_version(&major, &minor, &patch);
    ASSERT_EQ(major, PROMPT_LAYER_VERSION_MAJOR);
    ASSERT_EQ(minor, PROMPT_LAYER_VERSION_MINOR);
    ASSERT_EQ(patch, PROMPT_LAYER_VERSION_PATCH);
    return 1;
}

/* ============================================================
 * CREATE/DESTROY TESTS
 * ============================================================ */

static int test_create_returns_valid_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    prompt_layer_destroy(layer);
    return 1;
}

static int test_create_initializes_disabled(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Layer should start disabled until initialized
    ASSERT_EQ(layer->initialized, false);
    ASSERT_EQ(layer->enabled, false);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_create_initializes_content_null(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Content pointers should be NULL initially
    ASSERT_NULL(layer->raw_content);
    ASSERT_NULL(layer->rendered_content);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_destroy_null_layer(void) {
    /// Should not crash
    prompt_layer_destroy(NULL);
    return 1;
}

static int test_destroy_twice(void) {
    /// A real layer destroys cleanly, and destroy tolerates NULL (so a
    /// double-free guard at the call site is a safe no-op rather than a crash).
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    prompt_layer_destroy(layer);
    prompt_layer_destroy(NULL);
    return 1;
}

/* ============================================================
 * INIT TESTS
 * ============================================================ */

static int test_init_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_init(NULL, NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_init_null_events(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Init with NULL events returns INVALID_PARAM - events are required
    prompt_layer_error_t result = prompt_layer_init(layer, NULL);
    ASSERT_EQ(result, PROMPT_LAYER_ERROR_INVALID_PARAM);
    ASSERT_EQ(layer->initialized, false);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_init_with_events(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_error_t result = prompt_layer_init(layer, events);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_EQ(layer->initialized, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_init_sets_initialized_flag(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    ASSERT_EQ(layer->initialized, false);

    prompt_layer_init(layer, events);

    ASSERT_EQ(layer->initialized, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_init_enables_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    ASSERT_EQ(layer->enabled, false);

    prompt_layer_init(layer, events);

    ASSERT_EQ(layer->enabled, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * CLEANUP TESTS
 * ============================================================ */

static int test_cleanup_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_cleanup(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_cleanup_uninitialized_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Cleanup should work even if not initialized
    prompt_layer_error_t result = prompt_layer_cleanup(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_cleanup_twice(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    prompt_layer_cleanup(layer);

    /// Second cleanup should also be safe
    prompt_layer_error_t result = prompt_layer_cleanup(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_cleanup_initialized_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);
    ASSERT_EQ(layer->initialized, true);

    prompt_layer_error_t result = prompt_layer_cleanup(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_EQ(layer->initialized, false);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * SET CONTENT TESTS
 * ============================================================ */

static int test_set_content_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_set_content(NULL, "$ ");
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_set_content_null_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_set_content(layer, NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_uninitialized_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Uninitialized layer should return INVALID_STATE
    prompt_layer_error_t result = prompt_layer_set_content(layer, "$ ");
    ASSERT_EQ(result, PROMPT_LAYER_ERROR_INVALID_STATE);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_set_content_empty_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    /// Empty content should be allowed
    prompt_layer_error_t result = prompt_layer_set_content(layer, "");
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_simple_prompt(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_set_content(layer, "$ ");
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    /// Content should be stored
    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, "$ ");

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_complex_prompt(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    const char *complex_prompt = "[user@host ~/path]$ ";
    prompt_layer_error_t result =
        prompt_layer_set_content(layer, complex_prompt);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, complex_prompt);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_multiline_prompt(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    const char *multiline_prompt = "line1\nline2\n$ ";
    prompt_layer_error_t result =
        prompt_layer_set_content(layer, multiline_prompt);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, multiline_prompt);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_with_ansi(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    const char *ansi_prompt = "\033[32muser\033[0m@\033[34mhost\033[0m $ ";
    prompt_layer_error_t result = prompt_layer_set_content(layer, ansi_prompt);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, ansi_prompt);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_marks_dirty(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    layer->content_dirty = false;

    prompt_layer_set_content(layer, "$ ");

    ASSERT_EQ(layer->content_dirty, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_replaces_existing(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "old$ ");
    prompt_layer_set_content(layer, "new$ ");

    ASSERT_STR_EQ(layer->raw_content, "new$ ");

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_ascii_art_prompt(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    const char *ascii_art = "┌─[user@host]─[~/path]\n└─$ ";
    prompt_layer_error_t result = prompt_layer_set_content(layer, ascii_art);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, ascii_art);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_set_content_unicode_prompt(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    const char *unicode_prompt = "λ ~/code » ";
    prompt_layer_error_t result =
        prompt_layer_set_content(layer, unicode_prompt);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    ASSERT_NOT_NULL(layer->raw_content);
    ASSERT_STR_EQ(layer->raw_content, unicode_prompt);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * GET RENDERED CONTENT TESTS
 * ============================================================ */

static int test_get_rendered_content_null_layer(void) {
    char output[256];
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(NULL, output, sizeof(output));
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_get_rendered_content_null_output(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, NULL, 256);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_rendered_content_zero_size(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    char output[256];
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, output, 0);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_rendered_content_no_content_set(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);
    prompt_layer_init(layer, events);

    /// Rendering with no content set is an invalid-state error, not success.
    char output[256];
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, output, sizeof(output));
    ASSERT_EQ(result, PROMPT_LAYER_ERROR_INVALID_STATE);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_rendered_content_basic(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);
    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");
    char output[256];
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, output, sizeof(output));
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_STR_EQ(output, "$ ");

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_rendered_content_preserves_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);
    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "[test]$ ");
    char output[256];
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, output, sizeof(output));
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_STR_EQ(output, "[test]$ ");

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_rendered_content_buffer_too_small(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "this is a long prompt $ ");

    char output[5]; /// Too small
    prompt_layer_error_t result =
        prompt_layer_get_rendered_content(layer, output, sizeof(output));

    ASSERT_EQ(result, PROMPT_LAYER_ERROR_BUFFER_TOO_SMALL);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * GET METRICS TESTS
 * ============================================================ */

static int test_get_metrics_null_layer(void) {
    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(NULL, &metrics);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_get_metrics_null_metrics(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_get_metrics(layer, NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_metrics_no_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);
    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);
    prompt_layer_init(layer, events);

    /// Metrics with no content set is an invalid-state error.
    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(layer, &metrics);
    ASSERT_EQ(result, PROMPT_LAYER_ERROR_INVALID_STATE);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_metrics_simple_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");

    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(layer, &metrics);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_EQ(metrics.line_count, 1);
    ASSERT(metrics.max_line_width >= 2);
    ASSERT_EQ(metrics.is_multiline, false);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_metrics_multiline_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "line1\nline2\n$ ");

    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(layer, &metrics);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT(metrics.line_count >= 2);
    ASSERT_EQ(metrics.is_multiline, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_metrics_detects_ansi(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "\033[32mgreen\033[0m$ ");

    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(layer, &metrics);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_EQ(metrics.has_ansi_sequences, true);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_metrics_no_ansi(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "plain$ ");

    prompt_metrics_t metrics;
    prompt_layer_error_t result = prompt_layer_get_metrics(layer, &metrics);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT_EQ(metrics.has_ansi_sequences, false);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * UPDATE THEME TESTS
 * ============================================================ */

static int test_update_theme_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_update_theme(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_update_theme_uninitialized(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Should return error for uninitialized layer
    prompt_layer_error_t result = prompt_layer_update_theme(layer);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_update_theme_initialized(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_update_theme(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * FORCE RENDER TESTS
 * ============================================================ */

static int test_force_render_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_force_render(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_force_render_uninitialized(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    /// Force render on uninitialized layer should fail
    prompt_layer_error_t result = prompt_layer_force_render(layer);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_force_render_no_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    /// Forcing a render before any content is set has no raw content to
    /// render, so it reports an invalid-state error rather than rendering
    /// an empty prompt.
    prompt_layer_error_t result = prompt_layer_force_render(layer);
    ASSERT_EQ(result, PROMPT_LAYER_ERROR_INVALID_STATE);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_force_render_with_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");

    prompt_layer_error_t result = prompt_layer_force_render(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_force_render_clears_dirty_flag(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");
    ASSERT_EQ(layer->content_dirty, true);

    prompt_layer_force_render(layer);

    ASSERT_EQ(layer->content_dirty, false);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * GET PERFORMANCE TESTS
 * ============================================================ */

static int test_get_performance_null_layer(void) {
    prompt_performance_t perf;
    prompt_layer_error_t result = prompt_layer_get_performance(NULL, &perf);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_get_performance_null_performance(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_get_performance(layer, NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_performance_new_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_performance_t perf;
    prompt_layer_error_t result = prompt_layer_get_performance(layer, &perf);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    /// New layer should have zero render count
    ASSERT_EQ(perf.render_count, 0);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_get_performance_after_render(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");

    char output[256];
    prompt_layer_get_rendered_content(layer, output, sizeof(output));

    prompt_performance_t perf;
    prompt_layer_error_t result = prompt_layer_get_performance(layer, &perf);

    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);
    ASSERT(perf.render_count >= 1);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * RESET PERFORMANCE TESTS
 * ============================================================ */

static int test_reset_performance_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_reset_performance(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_reset_performance_valid(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    /// Do some renders to accumulate stats
    prompt_layer_set_content(layer, "$ ");
    char output[256];
    prompt_layer_get_rendered_content(layer, output, sizeof(output));

    prompt_layer_error_t result = prompt_layer_reset_performance(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    /// Verify counters are zero
    prompt_performance_t perf;
    prompt_layer_get_performance(layer, &perf);
    ASSERT_EQ(perf.render_count, 0);
    ASSERT_EQ(perf.cache_hits, 0);
    ASSERT_EQ(perf.cache_misses, 0);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * OPTIMIZE TESTS
 * ============================================================ */

static int test_optimize_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_optimize(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_optimize_valid(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_optimize(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_optimize_with_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");

    prompt_layer_error_t result = prompt_layer_optimize(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * PROCESS EVENTS TESTS
 * ============================================================ */

static int test_process_events_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_process_events(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_process_events_with_events(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    /// Process events with valid event system
    prompt_layer_error_t result = prompt_layer_process_events(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * VALIDATE TESTS
 * ============================================================ */

static int test_validate_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_validate(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_validate_new_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    prompt_layer_error_t result = prompt_layer_validate(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

static int test_validate_initialized_layer(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_error_t result = prompt_layer_validate(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

static int test_validate_with_content(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    layer_event_system_t *events = create_test_event_system();
    ASSERT_NOT_NULL(events);

    prompt_layer_init(layer, events);

    prompt_layer_set_content(layer, "$ ");

    prompt_layer_error_t result = prompt_layer_validate(layer);
    ASSERT_EQ(result, PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    layer_events_destroy(events);
    return 1;
}

/* ============================================================
 * GENERATE FROM LUSH TESTS
 * ============================================================ */

static int test_generate_from_lush_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_generate_from_lush(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_generate_from_lush_uninitialized(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    prompt_layer_error_t result = prompt_layer_generate_from_lush(layer);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

/* ============================================================
 * RUN TESTS FUNCTION TESTS
 * ============================================================ */

static int test_run_tests_null_layer(void) {
    prompt_layer_error_t result = prompt_layer_run_tests(NULL);
    ASSERT(result != PROMPT_LAYER_SUCCESS);
    return 1;
}

static int test_run_tests_uninitialized(void) {
    prompt_layer_t *layer = prompt_layer_create();
    ASSERT_NOT_NULL(layer);

    prompt_layer_error_t result = prompt_layer_run_tests(layer);
    ASSERT(result != PROMPT_LAYER_SUCCESS);

    prompt_layer_destroy(layer);
    return 1;
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void) {
    printf("=== Prompt Layer Unit Tests ===\n\n");

    printf("=== Error String Tests ===\n");
    RUN_TEST(error_string_success);
    RUN_TEST(error_string_invalid_param);
    RUN_TEST(error_string_null_pointer);
    RUN_TEST(error_string_memory_allocation);
    RUN_TEST(error_string_buffer_too_small);
    RUN_TEST(error_string_content_too_large);
    RUN_TEST(error_string_theme_not_available);
    RUN_TEST(error_string_event_system_failure);
    RUN_TEST(error_string_rendering_failure);
    RUN_TEST(error_string_invalid_state);
    RUN_TEST(error_string_performance_timeout);
    RUN_TEST(error_string_unknown);
    RUN_TEST(error_string_invalid_code);

    printf("\n=== Version Tests ===\n");
    RUN_TEST(get_version_all_null);
    RUN_TEST(get_version_major_only);
    RUN_TEST(get_version_minor_only);
    RUN_TEST(get_version_patch_only);
    RUN_TEST(get_version_all);

    printf("\n=== Create/Destroy Tests ===\n");
    RUN_TEST(create_returns_valid_layer);
    RUN_TEST(create_initializes_disabled);
    RUN_TEST(create_initializes_content_null);
    RUN_TEST(destroy_null_layer);
    RUN_TEST(destroy_twice);

    printf("\n=== Init Tests ===\n");
    RUN_TEST(init_null_layer);
    RUN_TEST(init_null_events);
    RUN_TEST(init_with_events);
    RUN_TEST(init_sets_initialized_flag);
    RUN_TEST(init_enables_layer);

    printf("\n=== Cleanup Tests ===\n");
    RUN_TEST(cleanup_null_layer);
    RUN_TEST(cleanup_uninitialized_layer);
    RUN_TEST(cleanup_twice);
    RUN_TEST(cleanup_initialized_layer);

    printf("\n=== Set Content Tests ===\n");
    RUN_TEST(set_content_null_layer);
    RUN_TEST(set_content_null_content);
    RUN_TEST(set_content_uninitialized_layer);
    RUN_TEST(set_content_empty_content);
    RUN_TEST(set_content_simple_prompt);
    RUN_TEST(set_content_complex_prompt);
    RUN_TEST(set_content_multiline_prompt);
    RUN_TEST(set_content_with_ansi);
    RUN_TEST(set_content_marks_dirty);
    RUN_TEST(set_content_replaces_existing);
    RUN_TEST(set_content_ascii_art_prompt);
    RUN_TEST(set_content_unicode_prompt);

    printf("\n=== Get Rendered Content Tests ===\n");
    RUN_TEST(get_rendered_content_null_layer);
    RUN_TEST(get_rendered_content_null_output);
    RUN_TEST(get_rendered_content_zero_size);
    RUN_TEST(get_rendered_content_no_content_set);
    RUN_TEST(get_rendered_content_basic);
    RUN_TEST(get_rendered_content_preserves_content);
    RUN_TEST(get_rendered_content_buffer_too_small);

    printf("\n=== Get Metrics Tests ===\n");
    RUN_TEST(get_metrics_null_layer);
    RUN_TEST(get_metrics_null_metrics);
    RUN_TEST(get_metrics_no_content);
    RUN_TEST(get_metrics_simple_content);
    RUN_TEST(get_metrics_multiline_content);
    RUN_TEST(get_metrics_detects_ansi);
    RUN_TEST(get_metrics_no_ansi);

    printf("\n=== Update Theme Tests ===\n");
    RUN_TEST(update_theme_null_layer);
    RUN_TEST(update_theme_uninitialized);
    RUN_TEST(update_theme_initialized);

    printf("\n=== Force Render Tests ===\n");
    RUN_TEST(force_render_null_layer);
    RUN_TEST(force_render_uninitialized);
    RUN_TEST(force_render_no_content);
    RUN_TEST(force_render_with_content);
    RUN_TEST(force_render_clears_dirty_flag);

    printf("\n=== Get Performance Tests ===\n");
    RUN_TEST(get_performance_null_layer);
    RUN_TEST(get_performance_null_performance);
    RUN_TEST(get_performance_new_layer);
    RUN_TEST(get_performance_after_render);

    printf("\n=== Reset Performance Tests ===\n");
    RUN_TEST(reset_performance_null_layer);
    RUN_TEST(reset_performance_valid);

    printf("\n=== Optimize Tests ===\n");
    RUN_TEST(optimize_null_layer);
    RUN_TEST(optimize_valid);
    RUN_TEST(optimize_with_content);

    printf("\n=== Process Events Tests ===\n");
    RUN_TEST(process_events_null_layer);
    RUN_TEST(process_events_with_events);

    printf("\n=== Validate Tests ===\n");
    RUN_TEST(validate_null_layer);
    RUN_TEST(validate_new_layer);
    RUN_TEST(validate_initialized_layer);
    RUN_TEST(validate_with_content);

    printf("\n=== Generate From Lush Tests ===\n");
    RUN_TEST(generate_from_lush_null_layer);
    RUN_TEST(generate_from_lush_uninitialized);

    printf("\n=== Run Tests Function Tests ===\n");
    RUN_TEST(run_tests_null_layer);
    RUN_TEST(run_tests_uninitialized);

    printf("\n=== Summary ===\n");
    return TEST_RESULT();
}
