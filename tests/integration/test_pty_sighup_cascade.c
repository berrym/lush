/**
 * @file test_pty_sighup_cascade.c
 * @brief L10: SIGHUP cascade to background jobs at login-shell exit.
 *
 * Spawns lush as a login shell on the slave end of a pseudo-terminal,
 * starts a background `sleep` process inside it, captures the bg PID
 * from the shell's $! print, then drives the shell to exit and
 * verifies the kernel/lush delivered SIGHUP to the background process
 * group (the `sleep` is dead within a short grace period after the
 * shell terminates).
 *
 * This is the canonical test pattern zsh uses in Test/W02jobs.ztst
 * via the zsh/zpty module. Behavior under test is *defined by*
 * terminal semantics, so a real PTY is required -- the stdin-pipe
 * variant of this same scenario would short-circuit the kernel's
 * controlling-terminal-driven signal delivery and validate nothing
 * meaningful.
 *
 * Usage: test_pty_sighup_cascade <lush-binary-path>
 */

#include "pty_helpers.h"

#define TEST "test_pty_sighup_cascade"

static int run_test(const char *lush_path) {
    pty_session_t s;
    const char *argv[] = {"lush", "-l", "-i", NULL};
    if (pty_spawn(&s, lush_path, argv) != 0) {
        pty_fail(TEST, "pty_spawn failed");
        return 1;
    }

    /// Give lush a moment to finish init and print its first prompt.
    pty_drain(&s, 500);

    /// Start a background sleep and print its PID. The sleep duration
    /// (60s) is long enough that we can definitively distinguish "still
    /// alive" from "killed by SIGHUP" via a kill(pid, 0) probe; short
    /// enough that a runaway test scrubs itself off the system in a
    /// minute.
    if (pty_send(&s, "sleep 60 &\n") != 0) {
        pty_fail(TEST, "send sleep failed: %s", strerror(errno));
        pty_cleanup(&s);
        return 1;
    }
    if (pty_send(&s, "echo BGPID:$!\n") != 0) {
        pty_fail(TEST, "send echo failed: %s", strerror(errno));
        pty_cleanup(&s);
        return 1;
    }

    /// Wait for the BGPID line to appear.
    int waited = 0;
    while (waited < 3000 && pty_extract_long_after(&s, "BGPID:") < 0) {
        pty_drain(&s, 100);
        waited += 100;
    }

    long bgpid = pty_extract_long_after(&s, "BGPID:");
    if (bgpid <= 0) {
        pty_fail(TEST,
                 "did not capture BGPID from lush output (got %ld bytes)\n"
                 "output: %s",
                 (long)s.out_len, s.output);
        pty_cleanup(&s);
        return 1;
    }

    /// Sanity: the bg sleep is alive right now.
    if (kill((pid_t)bgpid, 0) != 0) {
        pty_fail(TEST,
                 "background sleep pid %ld not alive before shell exit: %s",
                 bgpid, strerror(errno));
        pty_cleanup(&s);
        return 1;
    }

    /// Drive the login shell to exit cleanly. send_sighup_to_jobs()
    /// is in lush's exit path (lush.c) regardless of whether the
    /// shell exited via `exit` or via SIGHUP-from-the-PTY; we test
    /// the explicit-exit path here since it's the most-common
    /// daily-driver case. The SIGHUP-induced-exit case (terminal
    /// hangup vs. user typing exit) is covered separately in
    /// test_pty_sighup_logout.
    if (pty_send(&s, "exit\n") != 0) {
        pty_fail(TEST, "send exit failed: %s", strerror(errno));
        pty_cleanup(&s);
        return 1;
    }
    /// Close stdin so the shell's read returns EOF if `exit` somehow
    /// gets queued past the prompt redraw -- defensive against a
    /// blocked-on-read shell that ate the line but didn't terminate.
    pty_close_stdin(&s);

    /// Wait for lush to exit. 5s is comfortably more than enough.
    pty_drain(&s, 1000);
    pty_wait(&s, 5000);

    /// Brief grace period so the SIGHUP to the bg process group has
    /// time to actually terminate the sleep. The kill(0) probe is the
    /// real assertion.
    pty_sleep_ms(500);

    if (kill((pid_t)bgpid, 0) == 0) {
        /// Still alive: SIGHUP didn't cascade. Clean up the sleep so
        /// it doesn't outlive the test, then fail.
        kill((pid_t)bgpid, SIGKILL);
        pty_fail(TEST,
                 "background sleep pid %ld still alive after login shell "
                 "exited; SIGHUP cascade did not fire",
                 bgpid);
        pty_cleanup(&s);
        return 1;
    }

    pty_ok(TEST, "background sleep killed by SIGHUP on login shell exit");
    pty_cleanup(&s);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    return run_test(argv[1]);
}
