/**
 * @file test_wait_interrupt.c
 * @brief A signal breaks a blocking `wait` the way bash's `wait` exception
 * does.
 *
 * When the shell blocks in the `wait` builtin and a signal it must act on
 * arrives, bash breaks out of the wait rather than swallowing the interrupt:
 * a trapped signal makes `wait` return 128 + signo and runs the trap; a hangup
 * terminates the shell. A regressed lush leaked the interrupted waitpid as a
 * structured error (`error[E1402]: Interrupted system call`) and returned the
 * wrong status.
 *
 * Once the shell prints a readiness marker at the point it begins to block on
 * `wait`, the signal is delivered repeatedly across the wait window so at least
 * one lands while the shell is blocked in waitpid, even under load (a delivery
 * that arrives before the wait is handled at the command boundary instead, so a
 * single delivery could miss the break). Two shapes:
 *   - a trapped signal (SIGUSR1): the trap runs, `wait` returns 128 + SIGUSR1,
 *     and the script continues -- proved by a marker printed after the wait and
 *     the absence of the E1402 leak;
 *   - a hangup (SIGHUP): the shell exits 128 + SIGHUP and does not continue.
 *
 * Usage: test_wait_interrupt <lush-binary-path>
 */

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST "test_wait_interrupt"
#define READY_MARKER "LUSH_READY_FOR_WAIT"
#define READY_TIMEOUT_MS 10000
#define REAP_TIMEOUT_MS 15000

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/**
 * @brief Run `lush -c script`, deliver @p signo once the shell signals
 *        readiness, and collect the outcome.
 *
 * @param lush    Path to the lush binary.
 * @param script  Script text for `-c`. Must print READY_MARKER as it begins to
 *                block on wait, and background a long-lived job.
 * @param signo   Signal to deliver to the shell during the wait.
 * @param out     Captured stdout+stderr (NUL-terminated, truncated to out_sz).
 * @param out_sz  Size of out.
 * @param status  Set to the shell's wait status if it exits within the timeout.
 * @return true if the shell exited on its own; false if it had to be killed.
 */
static bool run_and_signal(const char *lush, const char *script, int signo,
                           char *out, size_t out_sz, int *status) {
    int outpipe[2];
    if (pipe(outpipe) != 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(outpipe[0]);
        close(outpipe[1]);
        return false;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(outpipe[1], STDOUT_FILENO);
        dup2(outpipe[1], STDERR_FILENO);
        close(outpipe[0]);
        close(outpipe[1]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        setenv("HOME", "/nonexistent", 1);
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("ENV");
        execl(lush, "lush", "-c", script, (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    close(outpipe[1]);
    int flags = fcntl(outpipe[0], F_GETFL, 0);
    fcntl(outpipe[0], F_SETFL, flags | O_NONBLOCK);

    size_t len = 0;
    ssize_t n;
    bool ready = false;
    for (long waited = 0; waited < READY_TIMEOUT_MS; waited += 10) {
        while (len + 1 < out_sz &&
               (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
            len += (size_t)n;
        }
        out[len] = '\0';
        if (strstr(out, READY_MARKER)) {
            ready = true;
            break;
        }
        msleep(10);
    }
    /// Deliver the signal repeatedly across the wait window. A single delivery
    /// could land in the gap between the readiness marker and the blocking
    /// waitpid, where it is handled at the next command boundary rather than by
    /// breaking the wait; repeating guarantees one lands while the shell is
    /// actually blocked, even under load. Delivery stops once the shell exits
    /// or the send window closes.
    bool reaped = false;
    for (long waited = 0; waited < REAP_TIMEOUT_MS; waited += 100) {
        if (waitpid(pid, status, WNOHANG) == pid) {
            reaped = true;
            break;
        }
        if (waited < 2000) {
            kill(pid, signo);
        }
        while (len + 1 < out_sz &&
               (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
            len += (size_t)n;
        }
        out[len] = '\0';
        msleep(100);
    }
    if (!reaped) {
        kill(-pid, SIGKILL);
        waitpid(pid, status, 0);
    }
    kill(-pid, SIGKILL);
    while (len + 1 < out_sz &&
           (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
        len += (size_t)n;
    }
    out[len] = '\0';
    close(outpipe[0]);
    (void)ready;
    return reaped;
}

static int failures = 0;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];
    char out[4096];
    int status;

    /// A trapped signal breaks the wait: the trap runs, wait returns
    /// 128 + signo (WAIT_BROKE proves the >128 return, portably across the
    /// platform's SIGUSR1 number), and the script continues. No E1402 leak.
    /// A shell that swallows the interrupt instead runs the trap but returns 0
    /// from wait, so WAIT_BROKE is absent.
    const char *trap_script =
        "trap 'echo TRAPPED' USR1; sleep 30 & echo " READY_MARKER
        "; wait $!; rc=$?; [ \"$rc\" -gt 128 ] && echo WAIT_BROKE; echo "
        "CONTINUED";
    if (!run_and_signal(lush, trap_script, SIGUSR1, out, sizeof(out),
                        &status)) {
        fprintf(stderr, "FAIL %s [trapped signal]: shell did not exit\n", TEST);
        failures++;
    } else {
        bool ok = strstr(out, "TRAPPED") && strstr(out, "WAIT_BROKE") &&
                  strstr(out, "CONTINUED") && !strstr(out, "E1402") &&
                  !strstr(out, "Interrupted system call") &&
                  WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (ok) {
            fprintf(stderr, "ok   %s [trapped signal]\n", TEST);
        } else {
            fprintf(stderr,
                    "FAIL %s [trapped signal]: exit=%d output=\"%.200s\"\n",
                    TEST, WIFEXITED(status) ? WEXITSTATUS(status) : -1, out);
            failures++;
        }
    }

    /// A hangup terminates the shell (128 + SIGHUP) without continuing.
    const char *hup_script =
        "sleep 30 & echo " READY_MARKER "; wait $!; echo CONTINUED";
    if (!run_and_signal(lush, hup_script, SIGHUP, out, sizeof(out), &status)) {
        fprintf(stderr, "FAIL %s [hangup]: shell did not exit\n", TEST);
        failures++;
    } else {
        bool hung_up =
            (WIFEXITED(status) && WEXITSTATUS(status) == 128 + SIGHUP) ||
            (WIFSIGNALED(status) && WTERMSIG(status) == SIGHUP);
        bool ok = hung_up && !strstr(out, "CONTINUED") && !strstr(out, "E1402");
        if (ok) {
            fprintf(stderr, "ok   %s [hangup]\n", TEST);
        } else {
            fprintf(stderr,
                    "FAIL %s [hangup]: exit=%d sig=%d output=\"%.200s\"\n",
                    TEST, WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                    WIFSIGNALED(status) ? WTERMSIG(status) : -1, out);
            failures++;
        }
    }

    if (failures > 0) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all cases passed\n", TEST);
    return 0;
}
