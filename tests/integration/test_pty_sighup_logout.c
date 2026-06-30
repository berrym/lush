/**
 * @file test_pty_sighup_logout.c
 * @brief L11 (SIGHUP variant): logout script fires when a login shell
 *        is hung up via SIGHUP, not just on explicit `exit`/`logout`.
 *
 * The explicit-exit case (`logout` builtin / `exit` from a login
 * shell) is covered by tests/integration/test_logout_explicit.sh
 * without a PTY -- it doesn't depend on controlling-terminal
 * semantics. The SIGHUP-induced case is different: when the user's
 * terminal goes away (ssh disconnect, terminal emulator killed, etc.)
 * the kernel sends SIGHUP to the foreground process group. The
 * login shell's job is to handle that gracefully: run ~/.lush_logout,
 * SIGHUP its own background children, then exit. This test pins that
 * sequence.
 *
 * Approach: spawn lush -l -i in a PTY. Pre-seed ~/.lush_logout with
 * an `echo` marker. Wait for init to finish. Send SIGHUP directly
 * to the shell pid (simulates a terminal hangup -- the equivalent
 * kernel behavior on PTY close-from-master, but without the SIGPIPE
 * complications). Drain the PTY for the marker, wait for exit, and
 * assert both pieces fired.
 *
 * Usage: test_pty_sighup_logout <lush-binary-path>
 */

#include "pty_helpers.h"

#define TEST "test_pty_sighup_logout"
#define LOGOUT_MARKER "LUSH_PTY_LOGOUT_FIRED"

static int run_test(const char *lush_path) {
    pty_session_t s;
    const char *argv[] = {"lush", "-l", "-i", NULL};
    if (pty_spawn(&s, lush_path, argv) != 0) {
        pty_fail(TEST, "pty_spawn failed");
        return 1;
    }

    /// lush resolves ~/.lush_logout lazily at exit
    /// (config_execute_logout_scripts -> config_get_logout_script_path).
    /// Seeding it after pty_spawn but before we deliver SIGHUP is
    /// fine -- the file just has to exist at exit time.
    char logout_path[1024];
    snprintf(logout_path, sizeof(logout_path), "%s/.lush_logout", s.home_dir);
    if (pty_write_marker(logout_path, LOGOUT_MARKER) != 0) {
        pty_fail(TEST, "could not seed ~/.lush_logout: %s", strerror(errno));
        pty_cleanup(&s);
        return 1;
    }

    /// Give the shell a moment to settle through init.
    pty_drain(&s, 500);

    /// Send SIGHUP, then wait specifically for the logout marker to reach us
    /// through the PTY rather than draining a fixed window: the script's echo
    /// can be delayed on a loaded runner. Then reap the exited shell.
    kill(s.pid, SIGHUP);
    pty_expect(&s, LOGOUT_MARKER, 5000);
    pty_wait(&s, 5000);

    /// Final drain in case anything is still buffered.
    pty_drain(&s, 200);

    /// Assertion 1: lush exited GRACEFULLY on SIGHUP -- it ran its main-loop
    /// teardown and exit()ed (WIFEXITED). A shell that instead WEDGED on the
    /// hangup would be force-reaped by the harness and reported as killed by
    /// SIGKILL (WIFSIGNALED); requiring WIFEXITED here makes such a regression
    /// a failure rather than a silent pass.
    if (!WIFEXITED(s.exit_status)) {
        pty_fail(
            TEST,
            "shell did not exit gracefully on SIGHUP (status=%d); a wedged "
            "shell is force-killed by the harness, not exited",
            s.exit_status);
        pty_cleanup(&s);
        return 1;
    }

    /// Assertion 2: ~/.lush_logout output reached us via the PTY
    /// before the slave closed.
    if (!strstr(s.output, LOGOUT_MARKER)) {
        pty_fail(TEST,
                 "no '%s' marker in PTY output -- logout script did not fire "
                 "on SIGHUP\noutput (%zu bytes): %.512s",
                 LOGOUT_MARKER, s.out_len, s.output);
        pty_cleanup(&s);
        return 1;
    }

    pty_ok(TEST, "~/.lush_logout fired on SIGHUP-driven exit");
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
