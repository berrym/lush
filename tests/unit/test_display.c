/**
 * @file test_display.c
 * @brief Unit tests for display subsystem components
 *
 * Tests the display layer system including:
 * - Command layer (syntax highlighting)
 * - Layer events system
 * - Color schemes
 * - Completion menu
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/// Include display headers
#include "display/command_layer.h"
#include "display/layer_events.h"
#include "lle/syntax_highlighting.h"
#include "test_framework.h"

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/// Create a fully initialized command layer with event system
static command_layer_t *
create_initialized_layer(layer_event_system_t **events_out) {
    layer_event_system_t *events = layer_events_create(NULL);
    if (!events)
        return NULL;

    layer_events_init(events);

    command_layer_t *layer = command_layer_create();
    if (!layer) {
        layer_events_destroy(events);
        return NULL;
    }

    command_layer_error_t err = command_layer_init(layer, events);
    if (err != COMMAND_LAYER_SUCCESS) {
        command_layer_destroy(layer);
        layer_events_destroy(events);
        return NULL;
    }

    /// Force truecolor so highlighting output is deterministic across CI
    /// environments. Terminal detection in a headless Docker run reports no
    /// color support, which collapses every token class to bold-only and
    /// makes per-class color assertions environment-dependent.
    if (layer->spec_highlighter) {
        layer->spec_highlighter->color_depth = 3;
    }

    if (events_out) {
        *events_out = events;
    }
    return layer;
}

static void destroy_initialized_layer(command_layer_t *layer,
                                      layer_event_system_t *events) {
    if (layer)
        command_layer_destroy(layer);
    if (events)
        layer_events_destroy(events);
}

/* ============================================================================
 * COMMAND LAYER LIFECYCLE TESTS
 * ============================================================================
 */

TEST(command_layer_create_destroy) {
    command_layer_t *layer = command_layer_create();
    ASSERT_NOT_NULL(layer, "command_layer_create should succeed");

    command_layer_destroy(layer);
    /// Should not crash
}

TEST(command_layer_destroy_null) {
    /// Should not crash with NULL
    command_layer_destroy(NULL);
}

TEST(command_layer_set_command_simple) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_error_t err =
        command_layer_set_command(layer, "echo hello", 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "set_command should succeed");

    /// The layer should now hold the command: its metrics report the byte
    /// length of "echo hello" and the cursor at the requested offset 0.
    command_metrics_t metrics;
    ASSERT_EQ(command_layer_get_metrics(layer, &metrics), COMMAND_LAYER_SUCCESS,
              "get_metrics should succeed after set_command");
    ASSERT_EQ(metrics.command_length, strlen("echo hello"),
              "metrics should report the command byte length");
    ASSERT_EQ(metrics.cursor_position, 0u, "cursor should be at offset 0");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_set_command_empty) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_error_t err = command_layer_set_command(layer, "", 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "Empty command should succeed");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_set_command_null) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_error_t err = command_layer_set_command(layer, NULL, 0);
    ASSERT(err == COMMAND_LAYER_ERROR_NULL_POINTER ||
               err == COMMAND_LAYER_ERROR_INVALID_PARAM,
           "NULL command should return error");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_update) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo hello", 5);
    command_layer_error_t err = command_layer_update(layer);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "update should succeed");

    /// After update the highlighted text must contain the command's tokens
    /// (colorized but byte-preserved), and metrics must reflect the cursor
    /// offset that was set.
    char buffer[1024];
    ASSERT_EQ(command_layer_get_highlighted_text(layer, buffer, sizeof(buffer)),
              COMMAND_LAYER_SUCCESS, "get_highlighted_text should succeed");
    ASSERT(strstr(buffer, "echo") != NULL, "highlighted text should keep echo");
    ASSERT(strstr(buffer, "hello") != NULL,
           "highlighted text should keep hello");

    command_metrics_t metrics;
    ASSERT_EQ(command_layer_get_metrics(layer, &metrics), COMMAND_LAYER_SUCCESS,
              "get_metrics should succeed");
    ASSERT_EQ(metrics.cursor_position, 5u, "cursor should be at offset 5");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_cursor_position) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Set command with cursor at position 5
    command_layer_error_t err =
        command_layer_set_command(layer, "echo hello", 5);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS,
              "set_command with cursor should succeed");

    /// Update cursor position
    err = command_layer_set_cursor_position(layer, 8);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "set_cursor_position should succeed");

    /// The new cursor offset must be reflected in the layer's metrics, not
    /// merely accepted by the setter.
    command_metrics_t metrics;
    ASSERT_EQ(command_layer_get_metrics(layer, &metrics), COMMAND_LAYER_SUCCESS,
              "get_metrics should succeed");
    ASSERT_EQ(metrics.cursor_position, 8u,
              "metrics should report the updated cursor offset");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_get_highlighted_text) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo hello", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_error_t err =
        command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS,
              "get_highlighted_text should succeed");

    /// The highlighted output keeps the source tokens verbatim and wraps
    /// them in ANSI color sequences, so it both contains the words and is
    /// strictly longer than the bare command text.
    ASSERT(strstr(buffer, "echo") != NULL, "highlighted text should keep echo");
    ASSERT(strstr(buffer, "hello") != NULL,
           "highlighted text should keep hello");
    ASSERT(strlen(buffer) > strlen("echo hello"),
           "highlighted text should add color sequences around the tokens");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_get_metrics) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo hello", 0);
    command_layer_update(layer);

    command_metrics_t metrics;
    command_layer_error_t err = command_layer_get_metrics(layer, &metrics);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "get_metrics should succeed");

    /// "echo hello" is a single-line command of ten bytes with the cursor
    /// at the start.
    ASSERT_EQ(metrics.command_length, strlen("echo hello"),
              "metrics should report the command byte length");
    ASSERT_EQ(metrics.cursor_position, 0u, "cursor should be at offset 0");
    ASSERT_FALSE(metrics.is_multiline_command,
                 "single-line command should not be multiline");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_clear) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo hello", 5);

    command_layer_error_t err = command_layer_clear(layer);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "clear should succeed");

    /// Clearing must empty the command: its length and cursor both return to
    /// zero rather than retaining the previous "echo hello" state.
    command_metrics_t metrics;
    ASSERT_EQ(command_layer_get_metrics(layer, &metrics), COMMAND_LAYER_SUCCESS,
              "get_metrics should succeed after clear");
    ASSERT_EQ(metrics.command_length, 0u, "cleared command length should be 0");
    ASSERT_EQ(metrics.cursor_position, 0u, "cleared cursor should be 0");

    destroy_initialized_layer(layer, events);
}

/* ============================================================================
 * SYNTAX HIGHLIGHTING TESTS
 * ============================================================================
 */

/// Copy `src` into `dst` with every ANSI SGR escape sequence ("ESC [ ... m")
/// removed, leaving only the literal payload text. The highlighter wraps each
/// token in color codes and may split a single shell operator across multiple
/// colored spans, so the way to assert the source text round-trips verbatim is
/// to strip the color and compare the remaining bytes.
static void strip_ansi(const char *src, char *dst, size_t dstsz) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstsz;) {
        if (src[i] == '\x1b' && src[i + 1] == '[') {
            i += 2;
            while (src[i] && src[i] != 'm') {
                i++;
            }
            if (src[i] == 'm') {
                i++;
            }
            continue;
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

/// Extract the SGR parameter bytes of the ANSI color sequence that
/// immediately precedes `token` in `buffer` -- i.e. the text between the
/// "ESC [" introducer and the final "m" of "ESC [ <params> m <token>".
/// Returns true and fills `out` when `token` is found wrapped in a color;
/// false when it is absent or rendered without a preceding color. This lets
/// the highlighting tests assert that each token class is colored, and that
/// distinct classes receive distinct colors, without hardcoding theme RGB.
static bool sgr_before(const char *buffer, const char *token, char *out,
                       size_t outsz) {
    const char *p = strstr(buffer, token);
    if (!p || p == buffer || *(p - 1) != 'm') {
        return false;
    }
    const char *m = p - 1;
    const char *esc = m;
    while (esc > buffer && (unsigned char)*esc != 0x1b) {
        esc--;
    }
    if ((unsigned char)*esc != 0x1b || *(esc + 1) != '[') {
        return false;
    }
    size_t len = (size_t)(m - (esc + 2));
    if (len >= outsz) {
        len = outsz - 1;
    }
    memcpy(out, esc + 2, len);
    out[len] = '\0';
    return true;
}

TEST(command_layer_syntax_command) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// The command word 'ls' is wrapped in a color sequence; its argument
    /// '-la' is colored differently (command class != argument class).
    command_layer_set_command(layer, "ls -la", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    char cmd_color[64], arg_color[64];
    ASSERT(sgr_before(buffer, "ls", cmd_color, sizeof(cmd_color)),
           "command 'ls' should be color-wrapped");
    ASSERT(sgr_before(buffer, "-la", arg_color, sizeof(arg_color)),
           "argument '-la' should be color-wrapped");
    ASSERT(strcmp(cmd_color, arg_color) != 0,
           "command and argument should use distinct colors");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_syntax_pipe) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "ls | grep foo", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    /// The pipe operator is colored, and distinctly from the command word.
    char cmd_color[64], pipe_color[64];
    ASSERT(sgr_before(buffer, "ls", cmd_color, sizeof(cmd_color)),
           "command 'ls' should be color-wrapped");
    ASSERT(sgr_before(buffer, "|", pipe_color, sizeof(pipe_color)),
           "pipe operator should be color-wrapped");
    ASSERT(strcmp(pipe_color, cmd_color) != 0,
           "pipe operator and command should use distinct colors");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_syntax_redirect) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo hello > file.txt", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    /// The redirection operator is colored, and distinctly from the command.
    char cmd_color[64], redir_color[64];
    ASSERT(sgr_before(buffer, "echo", cmd_color, sizeof(cmd_color)),
           "command 'echo' should be color-wrapped");
    ASSERT(sgr_before(buffer, ">", redir_color, sizeof(redir_color)),
           "redirection operator should be color-wrapped");
    ASSERT(strcmp(redir_color, cmd_color) != 0,
           "redirection operator and command should use distinct colors");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_syntax_variable) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo $HOME", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    /// The variable expansion is colored, and distinctly from the command.
    char cmd_color[64], var_color[64];
    ASSERT(sgr_before(buffer, "echo", cmd_color, sizeof(cmd_color)),
           "command 'echo' should be color-wrapped");
    ASSERT(sgr_before(buffer, "$HOME", var_color, sizeof(var_color)),
           "variable '$HOME' should be color-wrapped");
    ASSERT(strcmp(var_color, cmd_color) != 0,
           "variable and command should use distinct colors");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_syntax_string) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "echo \"hello world\"", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    /// The quoted string is colored as one span, distinctly from the command.
    char cmd_color[64], str_color[64];
    ASSERT(sgr_before(buffer, "echo", cmd_color, sizeof(cmd_color)),
           "command 'echo' should be color-wrapped");
    ASSERT(sgr_before(buffer, "\"hello world\"", str_color, sizeof(str_color)),
           "quoted string should be color-wrapped as a single span");
    ASSERT(strcmp(str_color, cmd_color) != 0,
           "string and command should use distinct colors");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_syntax_keyword) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_command(layer, "if true; then echo yes; fi", 0);
    command_layer_update(layer);

    char buffer[1024];
    command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));

    /// The 'if' keyword is colored, and distinctly from the command 'echo'
    /// (keywords and commands are separate token classes).
    char kw_color[64], cmd_color[64];
    ASSERT(sgr_before(buffer, "if", kw_color, sizeof(kw_color)),
           "keyword 'if' should be color-wrapped");
    ASSERT(sgr_before(buffer, "echo", cmd_color, sizeof(cmd_color)),
           "command 'echo' should be color-wrapped");
    ASSERT(strcmp(kw_color, cmd_color) != 0,
           "keyword and command should use distinct colors");

    destroy_initialized_layer(layer, events);
}

/* ============================================================================
 * LAYER EVENTS TESTS
 * ============================================================================
 */

/// Test callback for events
static int test_event_callback_count = 0;
static layer_events_error_t test_event_callback(const layer_event_t *event,
                                                void *user_data) {
    (void)event;
    (void)user_data;
    test_event_callback_count++;
    return LAYER_EVENTS_SUCCESS;
}

TEST(layer_events_create_destroy) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");

    layer_events_destroy(events);
}

TEST(layer_events_destroy_null) {
    layer_events_destroy(NULL);
    /// Should not crash
}

TEST(layer_events_init) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");

    layer_events_error_t err = layer_events_init(events);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "init should succeed");

    layer_events_destroy(events);
}

TEST(layer_events_subscribe) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_error_t err = layer_events_subscribe(
        events, LAYER_EVENT_CONTENT_CHANGED, LAYER_ID_COMMAND_LAYER,
        test_event_callback, NULL, LAYER_EVENT_PRIORITY_NORMAL);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "subscribe should succeed");

    /// A successful return is not enough: the subscription must be live.
    /// Publish a matching event and process it; the callback fires exactly
    /// once, proving the subscriber was actually registered.
    test_event_callback_count = 0;
    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, LAYER_ID_COMMAND_LAYER,
                                LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(test_event_callback_count, 1,
              "subscribed callback should fire once for a matching event");

    layer_events_destroy(events);
}

TEST(layer_events_unsubscribe) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);

    layer_events_error_t err = layer_events_unsubscribe(
        events, LAYER_EVENT_CONTENT_CHANGED, LAYER_ID_COMMAND_LAYER);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "unsubscribe should succeed");

    /// After unsubscribing, a matching event must no longer reach the
    /// callback: publish and process, then assert the callback stays silent.
    test_event_callback_count = 0;
    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, LAYER_ID_COMMAND_LAYER,
                                LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(test_event_callback_count, 0,
              "unsubscribed callback should not fire");

    layer_events_destroy(events);
}

TEST(layer_events_unsubscribe_all) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_subscribe(events, LAYER_EVENT_CURSOR_MOVED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);

    layer_events_error_t err =
        layer_events_unsubscribe_all(events, LAYER_ID_COMMAND_LAYER);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "unsubscribe_all should succeed");

    /// unsubscribe_all must remove every subscription for the layer, not
    /// just one event type: publish both subscribed types and process, then
    /// assert neither reaches the callback.
    test_event_callback_count = 0;
    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, LAYER_ID_COMMAND_LAYER,
                                LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_publish_simple(events, LAYER_EVENT_CURSOR_MOVED,
                                LAYER_ID_PROMPT_LAYER, LAYER_ID_COMMAND_LAYER,
                                LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(test_event_callback_count, 0,
              "no callback should fire after unsubscribe_all");

    layer_events_destroy(events);
}

TEST(layer_events_publish_simple) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    test_event_callback_count = 0;

    layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);

    layer_events_error_t err = layer_events_publish_simple(
        events, LAYER_EVENT_CONTENT_CHANGED, LAYER_ID_PROMPT_LAYER,
        LAYER_ID_COMMAND_LAYER, LAYER_EVENT_PRIORITY_NORMAL);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "publish_simple should succeed");

    /// Processing the queue must deliver the published event to the
    /// subscribed callback exactly once.
    layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(test_event_callback_count, 1,
              "published event should reach the subscriber once");

    layer_events_destroy(events);
}

TEST(layer_events_publish_content_changed) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    /// Subscribe so the published content-changed event has a destination,
    /// then assert it is actually delivered rather than only that the
    /// publish call returned success.
    test_event_callback_count = 0;
    layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);

    layer_events_error_t err = layer_events_publish_content_changed(
        events, LAYER_ID_COMMAND_LAYER, "test content", 12, false);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS,
              "publish_content_changed should succeed");

    layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(test_event_callback_count, 1,
              "content-changed event should reach the subscriber once");

    layer_events_destroy(events);
}

TEST(layer_events_publish_size_changed) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_error_t err = layer_events_publish_size_changed(
        events, LAYER_ID_BASE_TERMINAL, 80, 24, 120, 40);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "publish_size_changed should succeed");

    layer_events_destroy(events);
}

TEST(layer_events_has_pending) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    /// Initially no pending events
    bool has_pending = layer_events_has_pending(events);
    /// May or may not have pending depending on init

    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, 0,
                                LAYER_EVENT_PRIORITY_NORMAL);

    has_pending = layer_events_has_pending(events);
    ASSERT_TRUE(has_pending, "Should have pending after publish");

    layer_events_destroy(events);
}

TEST(layer_events_get_pending_count) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    uint32_t count = layer_events_get_pending_count(events);
    /// Initial count may be 0 or more

    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, 0,
                                LAYER_EVENT_PRIORITY_NORMAL);
    layer_events_publish_simple(events, LAYER_EVENT_CURSOR_MOVED,
                                LAYER_ID_PROMPT_LAYER, 0,
                                LAYER_EVENT_PRIORITY_NORMAL);

    uint32_t new_count = layer_events_get_pending_count(events);
    /// The two publishes above each enqueue exactly one pending event.
    ASSERT_EQ(new_count, count + 2, "two publishes add two pending events");

    layer_events_destroy(events);
}

TEST(layer_events_process_pending) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    test_event_callback_count = 0;

    layer_events_subscribe(events, LAYER_EVENT_CONTENT_CHANGED,
                           LAYER_ID_COMMAND_LAYER, test_event_callback, NULL,
                           LAYER_EVENT_PRIORITY_NORMAL);

    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, LAYER_ID_COMMAND_LAYER,
                                LAYER_EVENT_PRIORITY_NORMAL);

    /// Exactly one event was published to a subscribed layer, so processing
    /// must report one event handled and invoke the callback once.
    int processed = layer_events_process_pending(events, 0, 100);
    ASSERT_EQ(processed, 1, "process_pending should process the one event");
    ASSERT_EQ(test_event_callback_count, 1,
              "the processed event should reach the subscriber once");

    layer_events_destroy(events);
}

TEST(layer_events_process_priority) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    /// Publish one low-priority and one high-priority event.
    layer_events_publish_simple(events, LAYER_EVENT_CONTENT_CHANGED,
                                LAYER_ID_PROMPT_LAYER, 0,
                                LAYER_EVENT_PRIORITY_LOW);
    layer_events_publish_simple(events, LAYER_EVENT_CURSOR_MOVED,
                                LAYER_ID_PROMPT_LAYER, 0,
                                LAYER_EVENT_PRIORITY_HIGH);
    ASSERT_EQ(layer_events_get_pending_count(events), 2u,
              "both events should be queued before processing");

    /// Processing only the high priority level drains exactly the one
    /// high-priority event and leaves the low-priority event queued --
    /// proving priority selectivity rather than draining everything.
    int processed =
        layer_events_process_priority(events, LAYER_EVENT_PRIORITY_HIGH, 10);
    ASSERT_EQ(processed, 1, "only the high-priority event should be processed");
    ASSERT_EQ(layer_events_get_pending_count(events), 1u,
              "the low-priority event should remain queued");

    layer_events_destroy(events);
}

TEST(layer_events_get_type_name) {
    const char *name = layer_events_get_type_name(LAYER_EVENT_CONTENT_CHANGED);
    ASSERT_NOT_NULL(name, "get_type_name should return non-NULL");
    ASSERT(strlen(name) > 0, "Event type name should not be empty");
}

TEST(layer_events_get_layer_name) {
    const char *name = layer_events_get_layer_name(LAYER_ID_COMMAND_LAYER);
    ASSERT_NOT_NULL(name, "get_layer_name should return non-NULL");
    ASSERT(strlen(name) > 0, "Layer name should not be empty");
}

TEST(layer_events_error_string) {
    const char *msg = layer_events_error_string(LAYER_EVENTS_SUCCESS);
    ASSERT_NOT_NULL(msg, "error_string should return non-NULL");

    msg = layer_events_error_string(LAYER_EVENTS_ERROR_MEMORY_ALLOCATION);
    ASSERT_NOT_NULL(msg, "error_string for error should return non-NULL");
}

TEST(layer_events_default_config) {
    layer_events_config_t config = layer_events_create_default_config();
    /// Config should have reasonable defaults
    ASSERT(config.max_queue_size > 0, "Queue size should be positive");
    ASSERT(config.max_subscribers > 0, "Max subscribers should be positive");
}

TEST(layer_events_statistics) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_event_stats_t stats = layer_events_get_statistics(events);
    /// Stats should be available - verify struct is properly initialized
    (void)stats; /// Stats retrieved successfully

    layer_events_destroy(events);
}

TEST(layer_events_clear_statistics) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_error_t err = layer_events_clear_statistics(events);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "clear_statistics should succeed");

    layer_events_destroy(events);
}

TEST(layer_events_debug_enabled) {
    layer_event_system_t *events = layer_events_create(NULL);
    ASSERT_NOT_NULL(events, "layer_events_create should succeed");
    layer_events_init(events);

    layer_events_error_t err = layer_events_set_debug_enabled(events, true);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "set_debug_enabled should succeed");

    err = layer_events_set_debug_enabled(events, false);
    ASSERT_EQ(err, LAYER_EVENTS_SUCCESS, "disable debug should succeed");

    layer_events_destroy(events);
}

/* ============================================================================
 * COMPLETION MENU TESTS
 * ============================================================================
 */

TEST(command_layer_completion_menu_set) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_error_t err =
        command_layer_set_completion_menu(layer, "item1\nitem2\nitem3", 3, 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "set_completion_menu should succeed");

    ASSERT_TRUE(command_layer_is_menu_visible(layer), "Menu should be visible");
    ASSERT_EQ(command_layer_get_menu_lines(layer), 3,
              "Menu should have 3 lines");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_completion_menu_clear) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_completion_menu(layer, "item1\nitem2", 2, 0);
    ASSERT_TRUE(command_layer_is_menu_visible(layer), "Menu should be visible");

    command_layer_error_t err = command_layer_clear_completion_menu(layer);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS,
              "clear_completion_menu should succeed");

    ASSERT_FALSE(command_layer_is_menu_visible(layer),
                 "Menu should not be visible");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_completion_menu_selection) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    command_layer_set_completion_menu(layer, "item1\nitem2\nitem3", 3, 0);

    command_layer_error_t err = command_layer_set_menu_selection(layer, 2);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "set_menu_selection should succeed");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_completion_menu_content) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    const char *menu_text = "item1\nitem2\nitem3";
    command_layer_set_completion_menu(layer, menu_text, 3, 0);

    const char *content = command_layer_get_menu_content(layer);
    ASSERT_NOT_NULL(content, "Menu content should be returned");
    /// The stored menu text round-trips verbatim.
    ASSERT_STR_EQ(content, menu_text, "menu content round-trips unchanged");

    destroy_initialized_layer(layer, events);
}

/* ============================================================================
 * MULTILINE COMMAND TESTS
 * ============================================================================
 */

TEST(command_layer_multiline) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    const char *multiline = "if true\nthen\n  echo hello\nfi";
    command_layer_error_t err = command_layer_set_command(layer, multiline, 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS,
              "Multiline set_command should succeed");

    command_layer_update(layer);

    char buffer[2048];
    err = command_layer_get_highlighted_text(layer, buffer, sizeof(buffer));
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS,
              "Multiline get_highlighted should succeed");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_continuation) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Test simple continuation line (without trailing backslash that may
    /// trigger input)
    const char *multiline = "echo hello world";
    command_layer_error_t err = command_layer_set_command(layer, multiline, 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "Simple command should succeed");

    destroy_initialized_layer(layer, events);
}

/* ============================================================================
 * EDGE CASE TESTS
 * ============================================================================
 */

TEST(command_layer_very_long_command) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Build a moderately long command
    char long_cmd[2048];
    strcpy(long_cmd, "echo ");
    for (int i = 0; i < 50; i++) {
        strcat(long_cmd, "word ");
    }

    command_layer_error_t err = command_layer_set_command(layer, long_cmd, 0);
    /// Should handle gracefully
    ASSERT(err == COMMAND_LAYER_SUCCESS ||
               err == COMMAND_LAYER_ERROR_COMMAND_TOO_LARGE,
           "Long command should be handled");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_unicode) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Unicode in command
    command_layer_error_t err =
        command_layer_set_command(layer, "echo 日本語", 0);
    ASSERT_EQ(err, COMMAND_LAYER_SUCCESS, "Unicode should be handled");

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_special_chars) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Each command mixes shell metacharacters (variables, operators,
    /// redirections, command substitution). The layer must accept each,
    /// report the correct command length, and -- once the highlighter's
    /// color codes are stripped -- reproduce the source bytes exactly. The
    /// highlighter may split an operator like "2>&1" across several colored
    /// spans, so stripping ANSI is the reliable round-trip check.
    const char *cmds[] = {
        "echo $HOME && ls || true",
        "cat < input > output 2>&1",
        "echo $(pwd) `date`",
    };

    char buffer[2048];
    char stripped[2048];
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ASSERT_EQ(command_layer_set_command(layer, cmds[i], 0),
                  COMMAND_LAYER_SUCCESS,
                  "set_command should accept special characters");
        command_layer_update(layer);

        command_metrics_t metrics;
        ASSERT_EQ(command_layer_get_metrics(layer, &metrics),
                  COMMAND_LAYER_SUCCESS, "get_metrics should succeed");
        ASSERT_EQ(metrics.command_length, strlen(cmds[i]),
                  "metrics should report the full command length");

        ASSERT_EQ(
            command_layer_get_highlighted_text(layer, buffer, sizeof(buffer)),
            COMMAND_LAYER_SUCCESS, "get_highlighted_text should succeed");
        strip_ansi(buffer, stripped, sizeof(stripped));
        ASSERT_STR_EQ(stripped, cmds[i],
                      "stripping color must reproduce the source command");
    }

    destroy_initialized_layer(layer, events);
}

TEST(command_layer_version) {
    const char *version = command_layer_get_version();
    ASSERT_NOT_NULL(version, "get_version should return non-NULL");
    ASSERT(strlen(version) > 0, "Version string should not be empty");
}

/* ============================================================================
 * PERFORMANCE TESTS
 * ============================================================================
 */

TEST(command_layer_performance_target) {
    layer_event_system_t *events = NULL;
    command_layer_t *layer = create_initialized_layer(&events);
    ASSERT_NOT_NULL(layer, "create_initialized_layer should succeed");

    /// Measure update time
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < 100; i++) {
        command_layer_set_command(layer, "echo hello | grep h > /dev/null",
                                  i % 30);
        command_layer_update(layer);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                      (end.tv_nsec - start.tv_nsec);
    long avg_ns = elapsed_ns / 100;
    long avg_ms = avg_ns / 1000000;

    /// Target is <5ms per update, allow some slack for test environment
    ASSERT(avg_ms < 50, "Average update time should be reasonable");

    destroy_initialized_layer(layer, events);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("\n=== Display Subsystem Unit Tests ===\n\n");

    /// Command layer lifecycle tests
    printf("Command Layer Lifecycle:\n");
    RUN_TEST(command_layer_create_destroy);
    RUN_TEST(command_layer_destroy_null);
    RUN_TEST(command_layer_set_command_simple);
    RUN_TEST(command_layer_set_command_empty);
    RUN_TEST(command_layer_set_command_null);
    RUN_TEST(command_layer_update);
    RUN_TEST(command_layer_cursor_position);
    RUN_TEST(command_layer_get_highlighted_text);
    RUN_TEST(command_layer_get_metrics);
    RUN_TEST(command_layer_clear);

    /// Syntax highlighting tests
    printf("\nSyntax Highlighting:\n");
    RUN_TEST(command_layer_syntax_command);
    RUN_TEST(command_layer_syntax_pipe);
    RUN_TEST(command_layer_syntax_redirect);
    RUN_TEST(command_layer_syntax_variable);
    RUN_TEST(command_layer_syntax_string);
    RUN_TEST(command_layer_syntax_keyword);

    /// Layer events tests
    printf("\nLayer Events System:\n");
    RUN_TEST(layer_events_create_destroy);
    RUN_TEST(layer_events_destroy_null);
    RUN_TEST(layer_events_init);
    RUN_TEST(layer_events_subscribe);
    RUN_TEST(layer_events_unsubscribe);
    RUN_TEST(layer_events_unsubscribe_all);
    RUN_TEST(layer_events_publish_simple);
    RUN_TEST(layer_events_publish_content_changed);
    RUN_TEST(layer_events_publish_size_changed);
    RUN_TEST(layer_events_has_pending);
    RUN_TEST(layer_events_get_pending_count);
    RUN_TEST(layer_events_process_pending);
    RUN_TEST(layer_events_process_priority);
    RUN_TEST(layer_events_get_type_name);
    RUN_TEST(layer_events_get_layer_name);
    RUN_TEST(layer_events_error_string);
    RUN_TEST(layer_events_default_config);
    RUN_TEST(layer_events_statistics);
    RUN_TEST(layer_events_clear_statistics);
    RUN_TEST(layer_events_debug_enabled);

    /// Completion menu tests
    printf("\nCompletion Menu:\n");
    RUN_TEST(command_layer_completion_menu_set);
    RUN_TEST(command_layer_completion_menu_clear);
    RUN_TEST(command_layer_completion_menu_selection);
    RUN_TEST(command_layer_completion_menu_content);

    /// Multiline tests
    printf("\nMultiline Commands:\n");
    RUN_TEST(command_layer_multiline);
    RUN_TEST(command_layer_continuation);

    /// Edge case tests
    printf("\nEdge Cases:\n");
    RUN_TEST(command_layer_very_long_command);
    RUN_TEST(command_layer_unicode);
    RUN_TEST(command_layer_special_chars);
    RUN_TEST(command_layer_version);

    /// Performance tests
    printf("\nPerformance:\n");
    RUN_TEST(command_layer_performance_target);

    return TEST_RESULT();
}
