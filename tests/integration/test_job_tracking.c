/**
 * @file test_job_tracking.c
 * @brief Background jobs are tracked independent of job control (set -m).
 *
 * Regression guard for the defect where a background job launched with job
 * control off -- the default for a non-interactive shell -- was not recorded in
 * the job list at all. The pid form of `wait` worked (it calls waitpid on the
 * pid directly), but everything that consults the job list did not: `jobs`
 * printed nothing, no-argument `wait` returned immediately instead of blocking,
 * and `wait %n` reported "no such job". bash maintains the job list regardless
 * of `set -m`; job control governs only process-group topology and terminal
 * ownership.
 *
 * The checks are deterministic rather than timing-based: each backgrounds an
 * external command with a delayed, observable effect (a line printed to the
 * shared output, or an exit status) so that "did the wait actually block" is
 * decided by what appears in the captured output, independent of the shell's
 * startup time or host load. A regressed shell falls through the wait, exits,
 * and is group-killed by the harness before the background command's delay
 * elapses, so the effect never appears.
 *
 * Usage: test_job_tracking <lush-binary-path>
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

#define TEST "test_job_tracking"

/// A shell that never returns is reaped by the harness rather than hanging it.
#define REAP_TIMEOUT_MS 15000

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/**
 * @brief Run `lush -c script`, capturing stdout+stderr.
 *
 * The shell runs in its own process group so any background command it leaves
 * behind -- an orphan whose inherited pipe write-end would otherwise hold the
 * capture open -- is reaped with a single group kill after the shell exits.
 *
 * @param lush   Path to the lush binary.
 * @param script Script text for `-c`.
 * @param out    Captured output (NUL-terminated, truncated to out_sz).
 * @param out_sz Size of out.
 * @return true if the shell exited on its own within the timeout.
 */
static bool run_script(const char *lush, const char *script, char *out,
                       size_t out_sz) {
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
        /// Isolate from any user configuration so a warning from the caller's
        /// lushrc cannot contaminate the captured output the tests match on.
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

    int status = 0;
    bool reaped = false;
    for (long waited = 0; waited < REAP_TIMEOUT_MS; waited += 20) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            reaped = true;
            break;
        }
        while (len + 1 < out_sz &&
               (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
            len += (size_t)n;
        }
        out[len] = '\0';
        msleep(20);
    }

    if (!reaped) {
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    /// Reap the whole group so an orphaned background command's write-end stops
    /// holding the capture pipe open, then drain what is left.
    kill(-pid, SIGKILL);
    while (len + 1 < out_sz &&
           (n = read(outpipe[0], out + len, out_sz - 1 - len)) > 0) {
        len += (size_t)n;
    }
    out[len] = '\0';
    close(outpipe[0]);
    return reaped;
}

static int failures = 0;

/// Assert the shell exited on its own and its output contains `needle`.
static void expect_output(const char *lush, const char *label,
                          const char *script, const char *needle) {
    char out[4096];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit within %d ms\n", TEST,
                label, REAP_TIMEOUT_MS);
        failures++;
        return;
    }
    if (!strstr(out, needle)) {
        fprintf(stderr,
                "FAIL %s [%s]: expected \"%s\" in output (got: \"%.200s\")\n",
                TEST, label, needle, out);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

/// Assert the shell exited on its own and its output does NOT contain
/// `needle` -- used to prove a completed job leaves no phantom entry behind.
static void expect_absent(const char *lush, const char *label,
                          const char *script, const char *needle) {
    char out[4096];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit within %d ms\n", TEST,
                label, REAP_TIMEOUT_MS);
        failures++;
        return;
    }
    if (strstr(out, needle)) {
        fprintf(stderr,
                "FAIL %s [%s]: output unexpectedly contained \"%s\" (got: "
                "\"%.200s\")\n",
                TEST, label, needle, out);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

/// Assert the shell's output equals `expected` exactly -- used to prove a
/// non-interactive shell emits no job-control chatter ([id] launch notices or
/// [id]+ Done completion notices).
static void expect_exact(const char *lush, const char *label,
                         const char *script, const char *expected) {
    char out[4096];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit within %d ms\n", TEST,
                label, REAP_TIMEOUT_MS);
        failures++;
        return;
    }
    if (strcmp(out, expected) != 0) {
        fprintf(stderr, "FAIL %s [%s]: output was \"%s\", expected \"%s\"\n",
                TEST, label, out, expected);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// No-argument `wait` blocks until the tracked background job finishes: the
    /// job's delayed line reaches the capture only if the wait actually
    /// blocked.
    expect_output(lush, "no-arg wait blocks",
                  "sh -c 'sleep 1; echo SLEPT' & wait; echo MAIN", "SLEPT");

    /// `wait %n` resolves the job from the list and returns its exit status
    /// (127 "no such job" when the job list is empty, as it was when
    /// untracked).
    expect_output(lush, "wait %1 returns job status",
                  "sh -c 'sleep 1; exit 42' & wait %1; echo RC=$?", "RC=42");

    /// `jobs` reports the running background job.
    expect_output(lush, "jobs lists running job", "sleep 5 & jobs", "Running");

    /// `wait $!` reports the job's exit status through the tracked job.
    expect_output(lush, "wait $! returns job status",
                  "sh -c 'exit 42' & p=$!; wait $p; echo RC=$?", "RC=42");

    /// `wait $!` still reports the status even after the job was auto-reaped by
    /// an intervening status sweep (here forced by `jobs`): it must not fail
    /// with ECHILD/127.
    expect_output(
        lush, "wait $! after intervening reap",
        "sh -c 'sleep 1; exit 9' & p=$!; sleep 2; jobs; wait $p; echo "
        "RC=$?",
        "RC=9");

    /// A completed job leaves no phantom entry: after waiting on it, `jobs`
    /// reports nothing running.
    expect_absent(lush, "no phantom job after wait $!",
                  "sh -c 'exit 0' & p=$!; wait $p; jobs; echo END", "Running");

    /// No-operand `wait` reports success regardless of a job's failure (POSIX).
    expect_output(lush, "no-arg wait returns 0",
                  "sh -c 'exit 5' & wait; echo RC=$?", "RC=0");

    /// A non-interactive shell prints no launch or completion notices.
    expect_exact(lush, "no job-control chatter",
                 "sleep 1 & echo MID; wait; echo END", "MID\nEND\n");

    if (failures > 0) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all cases passed\n", TEST);
    return 0;
}
