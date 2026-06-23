/**
 * @file test_terminal_fault_reporting.c
 * @brief End-to-end test that a real terminal syscall failure reports a fault
 *
 * Drives the terminal interface into a genuine tcsetattr failure (raw mode on a
 * non-tty file descriptor) and asserts the converted LLE_FAULT site reports
 * through the fault router: a recording sink receives one fault naming the
 * terminal component and carrying the syscall error code.
 *
 * This exercises the real syscall path, not a mock: stdin is redirected to
 * /dev/null so tcsetattr fails deterministically with ENOTTY.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "lle/terminal_abstraction.h"
#include "test_framework.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int g_fault_count;
static lle_fault_t g_last_fault;

static void recording_sink(const lle_fault_t *fault) {
    g_fault_count++;
    g_last_fault = *fault;
}

TEST(raw_mode_failure_reports_a_terminal_fault) {
    /// Redirect stdin to a non-tty so tcsetattr fails deterministically; the
    /// interface uses STDIN_FILENO as its terminal fd.
    int devnull = open("/dev/null", O_RDONLY);
    ASSERT_TRUE(devnull >= 0, "open /dev/null should succeed");
    int saved_stdin = dup(STDIN_FILENO);
    ASSERT_TRUE(saved_stdin >= 0, "dup stdin should succeed");
    dup2(devnull, STDIN_FILENO);

    lle_unix_interface_t *iface = NULL;
    lle_result_t init_rc = lle_unix_interface_init(&iface);

    /// Install the recorder after init so only the raw-mode path is observed.
    lle_fault_set_dev_sink(recording_sink);
    g_fault_count = 0;
    memset(&g_last_fault, 0, sizeof(g_last_fault));

    lle_result_t rc = lle_unix_interface_enter_raw_mode(iface);

    /// Restore stdin and detach the sink before asserting so a failure cannot
    /// leave the harness pointed at /dev/null or the sink dangling.
    lle_fault_set_dev_sink(NULL);
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    close(devnull);

    ASSERT_EQ(init_rc, LLE_SUCCESS,
              "interface init should succeed on a non-tty");
    ASSERT_EQ(rc, LLE_ERROR_SYSTEM_CALL, "raw mode on a non-tty should fail");
    ASSERT_EQ(g_fault_count, 1, "the failure should report exactly one fault");
    ASSERT_STR_EQ(g_last_fault.component, "terminal",
                  "the fault should name the terminal component");
    ASSERT_EQ(g_last_fault.code, LLE_ERROR_SYSTEM_CALL,
              "the fault should carry the syscall error code");
    ASSERT_TRUE(strstr(g_last_fault.detail, "tcsetattr") != NULL,
                "the fault detail should name the failing syscall");

    lle_unix_interface_destroy(iface);
}

TEST(select_failure_reports_a_terminal_fault) {
    lle_unix_interface_t *iface = NULL;
    lle_result_t init_rc = lle_unix_interface_init(&iface);

    lle_fault_set_dev_sink(recording_sink);
    g_fault_count = 0;
    memset(&g_last_fault, 0, sizeof(g_last_fault));

    /// Invalidate the interface's terminal fd (STDIN_FILENO) so the select in
    /// the read path fails with EBADF, which is not EINTR/EAGAIN and so reaches
    /// the genuine-fault return.
    int saved_stdin = dup(STDIN_FILENO);
    ASSERT_TRUE(saved_stdin >= 0, "dup stdin should succeed");
    close(STDIN_FILENO);

    lle_input_event_t event;
    memset(&event, 0, sizeof(event));
    lle_result_t rc = lle_unix_interface_read_event(iface, &event, 0);

    /// Restore stdin and detach the sink before asserting.
    lle_fault_set_dev_sink(NULL);
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);

    ASSERT_EQ(init_rc, LLE_SUCCESS, "interface init should succeed");
    ASSERT_EQ(rc, LLE_ERROR_SYSTEM_CALL, "read on an invalid fd should fail");
    ASSERT_EQ(g_fault_count, 1, "the failure should report exactly one fault");
    ASSERT_STR_EQ(g_last_fault.component, "terminal",
                  "the fault should name the terminal component");
    ASSERT_TRUE(strstr(g_last_fault.detail, "select") != NULL,
                "the fault detail should name the failing syscall");

    lle_unix_interface_destroy(iface);
}

int main(void) {
    printf("=== LLE Terminal Fault Reporting Tests ===\n\n");
    RUN_TEST(raw_mode_failure_reports_a_terminal_fault);
    RUN_TEST(select_failure_reports_a_terminal_fault);
    return TEST_RESULT();
}
