/**
 * @file test_pager_render.c
 * @brief Unit tests for the display-controller pager render path
 *
 * Exercises display_controller_attach_pager / detach_pager and
 * display_controller_render_pager. Tests the rendered output shape
 * (visible lines + padding + status line) without spinning up a
 * full compositor; the controller is allocated with calloc so the
 * pager branch is reachable on a zero-initialised struct.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "display/display_controller.h"
#include "display/pager_layer.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* ============================================================================
 * Attach / detach
 * ============================================================================
 */

TEST(attach_sets_pointer) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "x\n", 2, 5, 80);

    display_controller_attach_pager(ctrl, &pager);
    ASSERT(ctrl->active_pager == &pager, "pointer attached");
    ASSERT(!ctrl->display_cache_valid, "cache invalidated on attach");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(detach_clears_pointer) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "x\n", 2, 5, 80);

    display_controller_attach_pager(ctrl, &pager);
    display_controller_detach_pager(ctrl);
    ASSERT(ctrl->active_pager == NULL, "pointer cleared");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(attach_null_is_safe) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    display_controller_attach_pager(ctrl, NULL);
    ASSERT(ctrl->active_pager == NULL, "NULL attach detaches");
    display_controller_attach_pager(NULL, NULL); /// safety
    free(ctrl);
}

/* ============================================================================
 * render_pager output shape
 * ============================================================================
 */

TEST(render_without_attached_pager_errors) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    char buf[1024];
    display_controller_error_t rc =
        display_controller_render_pager(ctrl, buf, sizeof(buf));
    ASSERT(rc == DISPLAY_CONTROLLER_ERROR_INVALID_PARAM,
           "no pager -> INVALID_PARAM");
    free(ctrl);
}

TEST(render_visible_lines_and_status) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    const char *content = "alpha\nbeta\ngamma\ndelta\nepsilon\n";
    pager_layer_init(&pager, content, strlen(content), 3, 80);
    display_controller_attach_pager(ctrl, &pager);

    char buf[1024];
    display_controller_error_t rc =
        display_controller_render_pager(ctrl, buf, sizeof(buf));
    ASSERT_EQ(rc, DISPLAY_CONTROLLER_SUCCESS, "render OK");

    /// Expected: first three lines + status line
    /// "alpha\nbeta\ngamma\n" followed by status
    ASSERT(strstr(buf, "alpha") != NULL, "alpha present");
    ASSERT(strstr(buf, "beta") != NULL, "beta present");
    ASSERT(strstr(buf, "gamma") != NULL, "gamma present");
    ASSERT(strstr(buf, "delta") == NULL, "delta not yet visible");
    ASSERT(strstr(buf, "Lines 1-3 of 5") != NULL, "status reports 1-3 of 5");
    /// 3 of 5 = 60% rounded
    ASSERT(strstr(buf, "(60%)") != NULL, "percentage shown");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(render_after_scroll_updates_status) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    const char *content = "alpha\nbeta\ngamma\ndelta\nepsilon\n";
    pager_layer_init(&pager, content, strlen(content), 3, 80);
    display_controller_attach_pager(ctrl, &pager);

    pager_layer_scroll_lines(&pager, 2);

    char buf[1024];
    display_controller_render_pager(ctrl, buf, sizeof(buf));
    ASSERT(strstr(buf, "gamma") != NULL, "gamma at top after scroll");
    ASSERT(strstr(buf, "epsilon") != NULL, "epsilon now visible");
    ASSERT(strstr(buf, "Lines 3-5 of 5") != NULL, "status reports 3-5 of 5");
    ASSERT(strstr(buf, "(100%)") != NULL, "scrolled to bottom");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(render_pads_to_view_rows_when_underfull) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "one\ntwo\n", 8, 10, 80);
    display_controller_attach_pager(ctrl, &pager);

    char buf[1024];
    display_controller_render_pager(ctrl, buf, sizeof(buf));

    /// 2 content lines + (10 - 2) = 8 empty lines + 1 status
    /// Count newlines in the output. Status doesn't end with \n.
    size_t newlines = 0;
    for (const char *p = buf; *p; p++) {
        if (*p == '\n') {
            newlines++;
        }
    }
    /// content (2 newlines) + padding (8 newlines) = 10 newlines total
    /// (the status line has no trailing newline)
    ASSERT_EQ(newlines, (size_t)10, "view rows of vertical space emitted");
    ASSERT(strstr(buf, "Lines 1-2 of 2") != NULL, "status correct");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(render_empty_content_shows_empty_status) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "", 0, 5, 80);
    display_controller_attach_pager(ctrl, &pager);

    char buf[1024];
    display_controller_error_t rc =
        display_controller_render_pager(ctrl, buf, sizeof(buf));
    ASSERT_EQ(rc, DISPLAY_CONTROLLER_SUCCESS, "render OK on empty");
    ASSERT(strstr(buf, "(empty)") != NULL, "empty status marker");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(render_buffer_too_small) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    const char *content = "this is a fairly long line of content\n";
    pager_layer_init(&pager, content, strlen(content), 5, 80);
    display_controller_attach_pager(ctrl, &pager);

    char buf[8]; /// way too small
    display_controller_error_t rc =
        display_controller_render_pager(ctrl, buf, sizeof(buf));
    ASSERT(rc == DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL,
           "small buffer -> BUFFER_TOO_SMALL");

    pager_layer_destroy(&pager);
    free(ctrl);
}

TEST(render_zero_size_errors) {
    display_controller_t *ctrl = calloc(1, sizeof(*ctrl));
    pager_layer_t pager;
    memset(&pager, 0, sizeof(pager));
    pager_layer_init(&pager, "x\n", 2, 5, 80);
    display_controller_attach_pager(ctrl, &pager);

    char buf[1];
    display_controller_error_t rc =
        display_controller_render_pager(ctrl, buf, 0);
    ASSERT(rc == DISPLAY_CONTROLLER_ERROR_BUFFER_TOO_SMALL,
           "zero size -> BUFFER_TOO_SMALL");

    pager_layer_destroy(&pager);
    free(ctrl);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running pager render tests...\n\n");

    printf("Attach / detach:\n");
    RUN_TEST(attach_sets_pointer);
    RUN_TEST(detach_clears_pointer);
    RUN_TEST(attach_null_is_safe);

    printf("\nRender output shape:\n");
    RUN_TEST(render_without_attached_pager_errors);
    RUN_TEST(render_visible_lines_and_status);
    RUN_TEST(render_after_scroll_updates_status);
    RUN_TEST(render_pads_to_view_rows_when_underfull);
    RUN_TEST(render_empty_content_shows_empty_status);
    RUN_TEST(render_buffer_too_small);
    RUN_TEST(render_zero_size_errors);

    return TEST_RESULT();
}
