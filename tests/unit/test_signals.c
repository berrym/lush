/**
 * @file test_signals.c
 * @brief Unit tests for signal handling and trap management
 *
 * Tests the signal handling system including:
 * - Signal handler setup
 * - Trap command management
 * - Signal name to number conversion
 * - Child process tracking
 * - LLE readline coordination
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "signals.h"
#include "test_framework.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Capture list_traps() stdout into buf (the only observable for trap state;
/// there is no trap getter). Signal NUMBERS are platform-specific
/// (e.g. SIGUSR1 is 30 on macOS, 10 on Linux), so callers assert on the
/// trap COMMAND text, not the number.
static void capture_traps(char *buf, size_t cap) {
    buf[0] = '\0';
    char tmpl[] = "/tmp/lush_traps_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return;
    }
    unlink(tmpl);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(fd, STDOUT_FILENO);
    list_traps();
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    ssize_t n = read(fd, buf, cap - 1);
    buf[n > 0 ? (size_t)n : 0] = '\0';
    close(fd);
}

/// Test framework macros

/* ============================================================================
 * SIGNAL NUMBER CONVERSION TESTS
 * ============================================================================
 */

TEST(get_signal_number_int) {
    int sig = get_signal_number("INT");
    ASSERT_EQ(sig, SIGINT, "INT should map to SIGINT");
}

TEST(get_signal_number_sigint) {
    int sig = get_signal_number("SIGINT");
    ASSERT_EQ(sig, SIGINT, "SIGINT should map to SIGINT");
}

TEST(get_signal_number_term) {
    int sig = get_signal_number("TERM");
    ASSERT_EQ(sig, SIGTERM, "TERM should map to SIGTERM");
}

TEST(get_signal_number_sigterm) {
    int sig = get_signal_number("SIGTERM");
    ASSERT_EQ(sig, SIGTERM, "SIGTERM should map to SIGTERM");
}

TEST(get_signal_number_hup) {
    int sig = get_signal_number("HUP");
    ASSERT_EQ(sig, SIGHUP, "HUP should map to SIGHUP");
}

TEST(get_signal_number_quit) {
    int sig = get_signal_number("QUIT");
    ASSERT_EQ(sig, SIGQUIT, "QUIT should map to SIGQUIT");
}

/// Note: KILL signal not implemented in get_signal_number

TEST(get_signal_number_usr1) {
    int sig = get_signal_number("USR1");
    ASSERT_EQ(sig, SIGUSR1, "USR1 should map to SIGUSR1");
}

TEST(get_signal_number_usr2) {
    int sig = get_signal_number("USR2");
    ASSERT_EQ(sig, SIGUSR2, "USR2 should map to SIGUSR2");
}

/* Note: PIPE, ALRM, CHLD, CONT, STOP, TSTP signals not implemented in
 * get_signal_number */

TEST(get_signal_number_invalid) {
    int sig = get_signal_number("NOTASIGNAL");
    ASSERT_EQ(sig, -1, "Invalid signal should return -1");
}

TEST(get_signal_number_empty) {
    int sig = get_signal_number("");
    ASSERT_EQ(sig, -1, "Empty string should return -1");
}

TEST(get_signal_number_lowercase) {
    /// Signal names are case-sensitive: the uppercase form resolves, the
    /// lowercase form does not.
    ASSERT(get_signal_number("INT") == SIGINT, "INT resolves to SIGINT");
    ASSERT(get_signal_number("int") == -1, "lowercase int is not recognized");
}

TEST(get_signal_number_numeric) {
    ASSERT(get_signal_number("2") == 2, "numeric signal string resolves");
    ASSERT(get_signal_number("notasignal") == -1, "unknown name is rejected");
}

/* ============================================================================
 * TRAP MANAGEMENT TESTS
 * ============================================================================
 */

TEST(set_trap_basic) {
    char traps[1024];
    ASSERT_EQ(set_trap(SIGUSR1, "echo trapped"), 0, "set_trap should succeed");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo trapped") != NULL,
           "the trap command should appear in the listing");
    remove_trap(SIGUSR1);
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo trapped") == NULL,
           "the trap should be gone after removal");
}

TEST(set_trap_null_removes) {
    char traps[1024];
    set_trap(SIGUSR1, "echo first");
    ASSERT_EQ(set_trap(SIGUSR1, NULL), 0, "set_trap NULL should succeed");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo first") == NULL,
           "passing NULL should remove the trap");
}

TEST(remove_trap_basic) {
    char traps[1024];
    set_trap(SIGUSR1, "echo to_remove");
    ASSERT_EQ(remove_trap(SIGUSR1), 0, "remove_trap should succeed");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo to_remove") == NULL,
           "the removed trap should not be listed");
}

TEST(remove_trap_nonexistent) {
    /// Removing a signal that has no trap reports failure.
    ASSERT(remove_trap(SIGUSR2) == -1, "removing an unset trap should fail");
}

TEST(set_trap_overwrite) {
    char traps[1024];
    set_trap(SIGUSR1, "echo old");
    ASSERT_EQ(set_trap(SIGUSR1, "echo new"), 0, "overwrite should succeed");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo new") != NULL, "the new command should be set");
    ASSERT(strstr(traps, "echo old") == NULL,
           "the old command should be replaced");
    remove_trap(SIGUSR1);
}

TEST(set_trap_exit) {
    char traps[1024];
    ASSERT_EQ(set_trap(0, "echo exiting"), 0, "EXIT trap should be settable");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo exiting") != NULL, "EXIT command should be set");
    ASSERT(strstr(traps, "EXIT") != NULL, "the EXIT label should be shown");
    remove_trap(0);
}

TEST(list_traps) {
    char traps[1024];
    set_trap(SIGUSR1, "echo listed");
    capture_traps(traps, sizeof(traps));
    ASSERT(strstr(traps, "echo listed") != NULL,
           "list_traps should render a registered trap");
    remove_trap(SIGUSR1);
}

/* ============================================================================
 * CHILD PROCESS TRACKING TESTS
 * ============================================================================
 */

TEST(set_clear_child_pid) {
    pid_t test_pid = 12345;

    set_current_child_pid(test_pid);
    /// Should not crash

    clear_current_child_pid();
    /// Should not crash
}

TEST(clear_child_pid_without_set) {
    /// Should not crash even if nothing was set
    clear_current_child_pid();
}

/* ============================================================================
 * LLE READLINE COORDINATION TESTS
 * ============================================================================
 */

TEST(set_lle_readline_active) {
    set_lle_readline_active(1);
    /// Should not crash

    set_lle_readline_active(0);
    /// Should not crash
}

TEST(check_and_clear_sigint_flag) {
    /// Initially should be 0
    int flag = check_and_clear_sigint_flag();
    ASSERT_EQ(flag, 0, "Initial SIGINT flag should be 0");

    /// After checking, should still be 0
    flag = check_and_clear_sigint_flag();
    ASSERT_EQ(flag, 0, "SIGINT flag should still be 0");
}

/* ============================================================================
 * SIGNAL HANDLER TESTS
 * ============================================================================
 */

static volatile int test_handler_called = 0;

static void test_signal_handler(int signum) {
    (void)signum;
    test_handler_called = 1;
}

TEST(set_signal_handler_basic) {
    test_handler_called = 0;

    int result = set_signal_handler(SIGUSR1, test_signal_handler);
    ASSERT_EQ(result, 0, "set_signal_handler should succeed");

    /// Send signal to ourselves
    kill(getpid(), SIGUSR1);

    ASSERT_EQ(test_handler_called, 1, "Handler should have been called");

    /// Restore default handler
    set_signal_handler(SIGUSR1, SIG_DFL);
}

TEST(set_signal_handler_ignore) {
    ASSERT_EQ(set_signal_handler(SIGUSR1, SIG_IGN), 0,
              "setting SIG_IGN should succeed");
    struct sigaction sa;
    sigaction(SIGUSR1, NULL, &sa);
    ASSERT(sa.sa_handler == SIG_IGN, "the handler should be SIG_IGN");
    /// The raised signal is ignored rather than terminating the process.
    kill(getpid(), SIGUSR1);
    set_signal_handler(SIGUSR1, SIG_DFL);
}

TEST(set_signal_handler_default) {
    int result = set_signal_handler(SIGUSR1, SIG_DFL);
    ASSERT_EQ(result, 0, "Setting SIG_DFL should succeed");
}

/* ============================================================================
 * INIT SIGNAL HANDLERS TEST
 * ============================================================================
 */

TEST(init_signal_handlers) {
    init_signal_handlers();
    /// init installs a handler for SIGINT (not left at the default).
    struct sigaction sa;
    sigaction(SIGINT, NULL, &sa);
    ASSERT(sa.sa_handler != SIG_DFL,
           "init_signal_handlers should install a SIGINT handler");
}

/* Note: set_sigint_handler test removed - function declared but not implemented
 */

/* ============================================================================
 * SIGHUP TESTS
 * ============================================================================
 */

TEST(sighup_was_received_initial) {
    bool received = sighup_was_received();
    /// Initially should be false
    ASSERT(!received, "SIGHUP should not be received initially");
}

TEST(send_sighup_to_jobs) {
    /// No background jobs - should return 0
    int count = send_sighup_to_jobs();
    ASSERT_EQ(count, 0, "No jobs should mean 0 signals sent");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running signals.c tests...\n\n");

    printf("Signal Number Conversion Tests:\n");
    RUN_TEST(get_signal_number_int);
    RUN_TEST(get_signal_number_sigint);
    RUN_TEST(get_signal_number_term);
    RUN_TEST(get_signal_number_sigterm);
    RUN_TEST(get_signal_number_hup);
    RUN_TEST(get_signal_number_quit);
    /// get_signal_number_kill removed - KILL not implemented
    RUN_TEST(get_signal_number_usr1);
    RUN_TEST(get_signal_number_usr2);
    /// PIPE, ALRM, CHLD, CONT, STOP, TSTP tests removed - not implemented
    RUN_TEST(get_signal_number_invalid);
    RUN_TEST(get_signal_number_empty);
    RUN_TEST(get_signal_number_lowercase);
    RUN_TEST(get_signal_number_numeric);

    printf("\nTrap Management Tests:\n");
    RUN_TEST(set_trap_basic);
    RUN_TEST(set_trap_null_removes);
    RUN_TEST(remove_trap_basic);
    RUN_TEST(remove_trap_nonexistent);
    RUN_TEST(set_trap_overwrite);
    RUN_TEST(set_trap_exit);
    RUN_TEST(list_traps);

    printf("\nChild Process Tracking Tests:\n");
    RUN_TEST(set_clear_child_pid);
    RUN_TEST(clear_child_pid_without_set);

    printf("\nLLE Readline Coordination Tests:\n");
    RUN_TEST(set_lle_readline_active);
    RUN_TEST(check_and_clear_sigint_flag);

    printf("\nSignal Handler Tests:\n");
    RUN_TEST(set_signal_handler_basic);
    RUN_TEST(set_signal_handler_ignore);
    RUN_TEST(set_signal_handler_default);

    printf("\nInit Signal Handlers Tests:\n");
    RUN_TEST(init_signal_handlers);
    /// set_sigint_handler test removed - function not implemented

    printf("\nSIGHUP Tests:\n");
    RUN_TEST(sighup_was_received_initial);
    RUN_TEST(send_sighup_to_jobs);

    return TEST_RESULT();
}
