/**
 * @file test_c_sighup_survival.c
 * @brief A non-interactive `lush -c` shell hangs up promptly on SIGHUP.
 *
 * Regression guard for issue #405: a SIGHUP delivered to a `lush -c '...'`
 * process while it waits on a foreground command must terminate the shell
 * (status 128 + SIGHUP) rather than be recorded in a write-only flag while the
 * command is waited out to completion. The bash/zsh oracle exits 129 within a
 * fraction of a second in every case below; a regressed lush waits out the full
 * sleep, which this test catches via a wait-loop timeout well under the sleep
 * duration.
 *
 * No pseudo-terminal is needed: `kill -HUP <pid>` to the non-interactive shell
 * reproduces the foreground-wait path directly. Three shapes are exercised:
 *   - a lone external command (the foreground wait forwards the hangup);
 *   - a hangup arriving while an in-process `read` builtin blocks, where no
 *     foreground wait is active to forward it -- the statement list must stop
 *     before the following command instead of running it;
 *   - a multi-line `-c` string (each line is a separate parse batch), where the
 *     batch loop must stop after the hung-up line.
 *
 * Usage: test_c_sighup_survival <lush-binary-path>
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST "test_c_sighup_survival"

/// The foreground command runs far longer than the wait-loop timeout, so a
/// shell that fails to hang up is unambiguously distinguished from one that
/// exits promptly: the former is still alive when the timeout expires.
#define SLEEP_SECS "30"
#define HANGUP_DELAY_MS 300
#define WAIT_TIMEOUT_MS 12000

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/**
 * @brief Run `lush -c script`, send SIGHUP after a delay, collect the outcome.
 *
 * @param block_stdin  When true the child's stdin is a pipe the parent holds
 *                     open but never writes, so a `read` builtin blocks on it.
 *                     When false stdin is /dev/null (immediate EOF).
 * @param out          Captured stdout (NUL-terminated, truncated to out_sz).
 * @return true if the child terminated within WAIT_TIMEOUT_MS (status in
 *         *status); false if it was still running and had to be killed.
 */
static bool run_case(const char *lush, const char *script, bool block_stdin,
                     char *out, size_t out_sz, int *status) {
    int outpipe[2];
    if (pipe(outpipe) != 0) {
        fprintf(stderr, "%s: pipe failed: %s\n", TEST, strerror(errno));
        return false;
    }
    int inpipe[2] = {-1, -1};
    if (block_stdin && pipe(inpipe) != 0) {
        fprintf(stderr, "%s: inpipe failed: %s\n", TEST, strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork failed: %s\n", TEST, strerror(errno));
        return false;
    }
    if (pid == 0) {
        /// Child: a fresh process group so the parent can reap the shell's
        /// children too. lush forks the foreground command (and, for a pipeline
        /// or command substitution, grandchildren) into this group; a real
        /// hangup kills them, but if the shell regresses they would be orphaned
        /// and the parent kills the whole group on cleanup.
        setpgid(0, 0);
        dup2(outpipe[1], STDOUT_FILENO);
        close(outpipe[0]);
        close(outpipe[1]);
        if (block_stdin) {
            dup2(inpipe[0], STDIN_FILENO);
            close(inpipe[0]);
            close(inpipe[1]);
        } else {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }
        execl(lush, "lush", "-c", script, (char *)NULL);
        _exit(127);
    }

    /// Parent: set the child's group too (race-free either way), close the ends
    /// owned by the child, keep inpipe write-end open so `read` stays blocked.
    setpgid(pid, pid);
    close(outpipe[1]);
    if (block_stdin) {
        close(inpipe[0]);
    }

    /// The hangup targets only the shell process, mimicking `kill -HUP` to the
    /// shell rather than a terminal hangup that signals the whole group.
    msleep(HANGUP_DELAY_MS);
    kill(pid, SIGHUP);

    /// Poll for termination up to the timeout instead of a blocking waitpid, so
    /// a shell that never hangs up is reaped by the test rather than hanging
    /// it.
    bool reaped = false;
    for (long waited = 0; waited < WAIT_TIMEOUT_MS; waited += 50) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid) {
            reaped = true;
            break;
        }
        msleep(50);
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, status, 0);
    }

    /// Reap any of the shell's children that outlived it (orphaned
    /// grandchildren of a regressed shell, or a lingering pipeline stage).
    /// Without this their open write-end keeps the capture pipe from reaching
    /// EOF.
    kill(-pid, SIGKILL);

    /// Drain whatever the shell printed before terminating. Non-blocking: a
    /// just-killed grandchild's not-yet-closed write-end must not stall the
    /// read. Output is tiny and already flushed by the time the shell exited.
    int flags = fcntl(outpipe[0], F_GETFL, 0);
    fcntl(outpipe[0], F_SETFL, flags | O_NONBLOCK);
    size_t len = 0;
    ssize_t n;
    while (len + 1 < out_sz &&
           (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
        len += (size_t)n;
    }
    out[len] = '\0';
    close(outpipe[0]);
    if (block_stdin) {
        close(inpipe[1]);
    }
    return reaped;
}

/// A hung-up shell exits with 128 + SIGHUP, whether it returns that status
/// normally or is terminated by the signal it forwarded to itself.
static bool is_hangup_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 128 + SIGHUP;
    }
    if (WIFSIGNALED(status)) {
        return WTERMSIG(status) == SIGHUP;
    }
    return false;
}

static int check(const char *lush, const char *label, const char *script,
                 bool block_stdin, const char *forbidden) {
    char out[4096];
    int status = 0;
    bool reaped =
        run_case(lush, script, block_stdin, out, sizeof(out), &status);
    if (!reaped) {
        fprintf(stderr,
                "FAIL %s [%s]: shell did not hang up within %d ms; it waited "
                "out the foreground command\n",
                TEST, label, WAIT_TIMEOUT_MS);
        return 1;
    }
    if (!is_hangup_status(status)) {
        fprintf(stderr,
                "FAIL %s [%s]: expected hangup status 128+SIGHUP (%d), got "
                "exited=%d code=%d signaled=%d sig=%d\n",
                TEST, label, 128 + SIGHUP, WIFEXITED(status),
                WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                WIFSIGNALED(status),
                WIFSIGNALED(status) ? WTERMSIG(status) : -1);
        return 1;
    }
    if (forbidden && strstr(out, forbidden) != NULL) {
        fprintf(stderr,
                "FAIL %s [%s]: statement after the hangup ran -- found "
                "forbidden output \"%s\"\n",
                TEST, label, forbidden);
        return 1;
    }
    fprintf(stderr, "ok %s [%s]\n", TEST, label);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// Ignore SIGHUP in the test process itself so signaling the child's
    /// process group (if it shares ours) cannot take the test down.
    signal(SIGHUP, SIG_IGN);

    int rc = 0;
    rc |=
        check(lush, "lone external command", "sleep " SLEEP_SECS, false, NULL);
    rc |= check(lush, "hangup during read",
                "read x; sleep " SLEEP_SECS "; echo LEAKED", true, "LEAKED");
    rc |= check(lush, "multi-line batch", "sleep " SLEEP_SECS "\necho LEAKED",
                false, "LEAKED");
    rc |= check(lush, "pipeline", "sleep " SLEEP_SECS " | cat", false, NULL);
    rc |= check(lush, "command substitution",
                "x=$(sleep " SLEEP_SECS "); echo LEAKED", false, "LEAKED");

    if (rc == 0) {
        fprintf(stderr, "PASS %s\n", TEST);
    }
    return rc;
}
