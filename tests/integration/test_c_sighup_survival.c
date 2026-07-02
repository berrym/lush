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
#define WAIT_TIMEOUT_MS 10000

/// Readiness handshake. Each script prints this marker at the exact point the
/// test wants to interrupt: the non-read cases echo it just before the
/// foreground command; the read case uses `read -p`, whose prompt is flushed as
/// read begins to block. The test waits for the marker before delivering SIGHUP
/// instead of guessing with a fixed delay -- a fixed delay misfires under
/// parallel-test CPU load, where the shell may not have reached the target
/// state in time (the original cause of this test flaking under load).
#define READY_MARKER "LUSH_READY_FOR_HANGUP"
#define READY_TIMEOUT_MS 10000

/// A brief settle after the readiness marker. The marker proves init is done
/// and the shell is executing; this covers only the fast, unobservable
/// fork -> exec -> enter-wait transition for cases that spawn a foreground
/// command, so SIGHUP lands on the running command rather than in the shell's
/// fork/pre-exec window (where a forwarded hangup can be caught by the child
/// before it resets its inherited handler). Sized with large margin over that
/// microsecond transition; the deeper fork-window race is a signal-model
/// concern for the P1 work, not this test.
#define SETTLE_MS 300

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/**
 * @brief Run `lush -c script`, deliver SIGHUP once the shell signals readiness,
 *        and collect the outcome.
 *
 * @param block_stdin    When true the child's stdin is a pipe the parent holds
 *                       open but never writes, so a `read` builtin blocks on
 * it. When false stdin is /dev/null (immediate EOF).
 * @param out            Captured stdout (NUL-terminated, truncated to out_sz);
 *                       includes the readiness marker.
 * @param reached_ready  Set true once the readiness marker was observed.
 * @return true if the child terminated within WAIT_TIMEOUT_MS (status in
 *         *status); false if it was still running and had to be killed.
 */
static bool run_case(const char *lush, const char *script, bool block_stdin,
                     char *out, size_t out_sz, int *status,
                     bool *reached_ready) {
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

    /// Capture non-blocking from the start so the readiness wait and the final
    /// drain never block on the pipe.
    int flags = fcntl(outpipe[0], F_GETFL, 0);
    fcntl(outpipe[0], F_SETFL, flags | O_NONBLOCK);
    size_t len = 0;
    ssize_t n;

    /// Readiness handshake: wait for the marker (proof the shell is past init
    /// and blocked at the point to interrupt) before signalling, rather than
    /// guessing with a fixed delay that misfires under parallel-test CPU load.
    *reached_ready = false;
    for (long waited = 0; waited < READY_TIMEOUT_MS; waited += 10) {
        while (len + 1 < out_sz &&
               (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
            len += (size_t)n;
        }
        out[len] = '\0';
        if (strstr(out, READY_MARKER) != NULL) {
            *reached_ready = true;
            break;
        }
        msleep(10);
    }
    if (*reached_ready) {
        msleep(SETTLE_MS);
    }

    /// The hangup targets only the shell process, mimicking `kill -HUP` to the
    /// shell rather than a terminal hangup that signals the whole group.
    kill(pid, SIGHUP);

    /// Poll for termination up to the timeout instead of a blocking waitpid, so
    /// a shell that never hangs up is reaped by the test rather than hanging
    /// it; keep draining so a filled pipe never blocks the child mid-teardown.
    bool reaped = false;
    for (long waited = 0; waited < WAIT_TIMEOUT_MS; waited += 50) {
        if (waitpid(pid, status, WNOHANG) == pid) {
            reaped = true;
            break;
        }
        while (len + 1 < out_sz &&
               (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
            len += (size_t)n;
        }
        out[len] = '\0';
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

    /// Final drain of anything printed before termination.
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
    bool reached_ready = false;
    bool reaped = run_case(lush, script, block_stdin, out, sizeof(out), &status,
                           &reached_ready);
    if (!reached_ready) {
        fprintf(stderr,
                "FAIL %s [%s]: shell never printed the readiness marker within "
                "%d ms (captured: \"%.120s\")\n",
                TEST, label, READY_TIMEOUT_MS, out);
        return 1;
    }
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
    rc |= check(lush, "lone external command",
                "echo " READY_MARKER "; sleep " SLEEP_SECS, false, NULL);
    /// read -p flushes the marker exactly as read begins to block, so SIGHUP
    /// lands during the read (a marker before the read could let the signal
    /// arrive before read starts, where read would block with no input).
    rc |= check(lush, "hangup during read",
                "read -p " READY_MARKER " x; sleep " SLEEP_SECS "; echo LEAKED",
                true, "LEAKED");
    rc |= check(lush, "multi-line batch",
                "echo " READY_MARKER "\nsleep " SLEEP_SECS "\necho LEAKED",
                false, "LEAKED");
    rc |=
        check(lush, "pipeline",
              "echo " READY_MARKER "; sleep " SLEEP_SECS " | cat", false, NULL);
    rc |= check(lush, "command substitution",
                "echo " READY_MARKER "; x=$(sleep " SLEEP_SECS "); echo LEAKED",
                false, "LEAKED");

    if (rc == 0) {
        fprintf(stderr, "PASS %s\n", TEST);
    }
    return rc;
}
