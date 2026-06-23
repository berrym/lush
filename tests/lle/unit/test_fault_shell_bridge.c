/**
 * @file test_fault_shell_bridge.c
 * @brief End-to-end tests for the LLE fault shell bridge
 *
 * Drives a real fault through the registered bridge and captures stderr to
 * assert the user channel renders through the shell's structured-error display
 * and that severity gating holds:
 *   - a high-severity fault renders as a shell error[CODE] line
 *   - a low-severity fault is suppressed from the user channel
 *   - a detached bridge renders nothing
 *
 * The real shell renderer (shell_error.c) is linked. Only debug_trace_printf is
 * stubbed: linking the real debug core would drag in the executor and shell
 * globals, and the developer channel is not what this test asserts.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "lle/lle_shell_error_bridge.h"
#include "test_framework.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/// Minimal developer-channel stubs. The bridge developer sink references these;
/// the real implementations live in the shell's debug core, which pulls in the
/// executor and shell globals not needed here.
typedef struct debug_context debug_context_t;
debug_context_t *g_debug_context = NULL;
void debug_trace_printf(debug_context_t *ctx, const char *fmt, ...) {
    (void)ctx;
    (void)fmt;
}

/// Fire a fault with stderr redirected to a temporary file, then return the
/// captured text in @p out.
static void fire_and_capture(lle_result_t code, const char *component,
                             const char *detail, char *out, size_t out_size) {
    out[0] = '\0';
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    FILE *tmp = tmpfile();
    if (tmp == NULL || saved < 0) {
        return;
    }
    dup2(fileno(tmp), STDERR_FILENO);

    lle_fault_report(code, component, detail, __func__, __FILE__, __LINE__);

    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);

    rewind(tmp);
    size_t n = fread(out, 1, out_size - 1, tmp);
    out[n] = '\0';
    fclose(tmp);
}

/* ============================================================================
 * Rendering
 * ============================================================================
 */

TEST(high_severity_fault_renders_shell_error) {
    lle_shell_error_bridge_init();
    char out[4096];
    fire_and_capture(LLE_ERROR_OUT_OF_MEMORY, "history",
                     "command history could not be loaded", out, sizeof(out));
    /// E1401 is SHELL_ERR_OUT_OF_MEMORY; the message carries the component.
    ASSERT_TRUE(strstr(out, "error[E1401]") != NULL,
                "out-of-memory fault renders as shell error[E1401]");
    ASSERT_TRUE(strstr(out, "history") != NULL,
                "render carries the component name");
    ASSERT_TRUE(strstr(out, "command history could not be loaded") != NULL,
                "render carries the detail message");
    lle_shell_error_bridge_shutdown();
}

TEST(low_severity_fault_is_suppressed_from_user_channel) {
    lle_shell_error_bridge_init();
    char out[4096];
    fire_and_capture(LLE_ERROR_NOT_FOUND, "history", "no match", out,
                     sizeof(out));
    ASSERT_TRUE(strstr(out, "error[") == NULL,
                "a warning-severity fault must not reach the user channel");
    lle_shell_error_bridge_shutdown();
}

TEST(detached_bridge_renders_nothing) {
    lle_shell_error_bridge_init();
    lle_shell_error_bridge_shutdown();
    char out[4096];
    fire_and_capture(LLE_ERROR_OUT_OF_MEMORY, "history", "alloc", out,
                     sizeof(out));
    ASSERT_TRUE(strstr(out, "error[") == NULL,
                "a detached bridge renders nothing to the user");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Fault Shell Bridge Tests ===\n\n");

    printf("--- Rendering ---\n");
    RUN_TEST(high_severity_fault_renders_shell_error);
    RUN_TEST(low_severity_fault_is_suppressed_from_user_channel);
    RUN_TEST(detached_bridge_renders_nothing);

    return TEST_RESULT();
}
