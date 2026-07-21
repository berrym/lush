/**
 * @file test_job_retention.c
 * @brief lush's completed-job retention laws (single-consumption model).
 *
 * These assert lush's OWN curated state laws, not another shell's behavior --
 * bash and zsh both fail law 1 by design (bash keeps a completed job waitable,
 * zsh purges it even from a listing), so neither is the oracle here. The laws:
 *
 *   1. Consume-on-wait: an explicit `wait` returns a finished job's status once
 *      and then drops it; a second `wait` for the same job finds no such job.
 *   2. A consumed job is gone from `jobs`.
 *   3. Report is not consume: `jobs` (or a completion notice) displays a
 *      finished job but does not consume it; a following `wait` still returns
 *      its status.
 *   4. No-argument `wait` consumes every job.
 *   5. The `jobs.retain_completed` config cell restores legacy retention: a
 *      completed job stays addressable by a repeated `wait`.
 *   6. Never-waited completions are held in a bounded backstop (they do not
 *      grow without limit).
 *
 * Usage: test_job_retention <lush-binary-path>
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

#define TEST "test_job_retention"
#define REAP_TIMEOUT_MS 15000
/// Matches COMPLETED_JOB_CAP in src/executor.c: the never-waited backstop
/// bound.
#define BACKSTOP_CAP 32

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/// Run `lush -c script` (config-isolated), capturing stdout+stderr.
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

static size_t count_substr(const char *hay, const char *needle) {
    size_t count = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle)) {
        count++;
    }
    return count;
}

/// Assert every needle in `needles` (NULL-terminated) is present, and every
/// needle in `absent` (NULL-terminated, may be NULL) is absent.
static void check(const char *lush, const char *label, const char *script,
                  const char *const *needles, const char *const *absent) {
    char out[8192];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit\n", TEST, label);
        failures++;
        return;
    }
    for (const char *const *p = needles; p && *p; p++) {
        if (!strstr(out, *p)) {
            fprintf(stderr, "FAIL %s [%s]: expected \"%s\" (got: \"%.300s\")\n",
                    TEST, label, *p, out);
            failures++;
            return;
        }
    }
    for (const char *const *p = absent; p && *p; p++) {
        if (strstr(out, *p)) {
            fprintf(stderr,
                    "FAIL %s [%s]: unexpected \"%s\" (got: \"%.300s\")\n", TEST,
                    label, *p, out);
            failures++;
            return;
        }
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// Law 1: consume-on-wait. First wait returns 7; second finds no job (127).
    check(
        lush, "consume-on-wait",
        "sh -c 'exit 7' & p=$!; wait $p; echo A=$?; wait $p 2>/dev/null; echo "
        "B=$?",
        (const char *[]){"A=7", "B=127", NULL}, NULL);

    /// Law 2: a consumed job is gone from `jobs`.
    check(lush, "consumed job absent from jobs",
          "sleep 0.2 & wait $!; jobs; echo END", (const char *[]){"END", NULL},
          (const char *[]){"Running", "Done", NULL});

    /// Law 3: report is not consume -- jobs reports it, wait still gets 9.
    check(lush, "report is not consume",
          "sh -c 'sleep 0.3; exit 9' & p=$!; sleep 0.7; jobs; wait $p; echo "
          "RC=$?",
          (const char *[]){"RC=9", NULL}, NULL);

    /// Law 4: no-argument wait consumes every job.
    check(lush, "no-arg wait consumes all",
          "sleep 0.2 & sleep 0.2 & wait; jobs; echo END",
          (const char *[]){"END", NULL},
          (const char *[]){"Running", "Done", NULL});

    /// Law 5: jobs.retain_completed restores legacy retention (wait twice = 5).
    check(lush, "retain_completed opt-in",
          "config set jobs.retain_completed true; sh -c 'exit 5' & p=$!; wait "
          "$p; echo A=$?; wait $p 2>/dev/null; echo B=$?",
          (const char *[]){"A=5", "B=5", NULL}, NULL);

    /// Law 6: never-waited completions are bounded (backstop). Launch more than
    /// the cap; at most BACKSTOP_CAP remain listed as Done.
    {
        char out[8192];
        /// {1..40} brace expansion rather than `$(seq 1 40)`: an unquoted
        /// command substitution does not word-split in lush mode (SEMANTICS
        /// section 4.1 / FEATURE_CMDSUB_WORD_SPLIT off), so the loop needs a
        /// construct that yields 40 words directly.
        if (!run_script(lush,
                        "for i in {1..40}; do /usr/bin/true & done; sleep "
                        "0.5; jobs",
                        out, sizeof(out))) {
            fprintf(stderr, "FAIL %s [backstop bound]: shell did not exit\n",
                    TEST);
            failures++;
        } else {
            /// 40 never-waited completions launched; the backstop settles the
            /// retained count at exactly COMPLETED_JOB_CAP (over-prune to fewer
            /// is a regression, growth past it is a leak -- assert the exact
            /// bound). `jobs` reaps and prunes synchronously before listing, so
            /// this is deterministic.
            size_t done = count_substr(out, "Done");
            if (done == BACKSTOP_CAP) {
                fprintf(stderr, "ok   %s [backstop bound] (== %d)\n", TEST,
                        BACKSTOP_CAP);
            } else {
                fprintf(stderr,
                        "FAIL %s [backstop bound]: %zu completions retained "
                        "(want exactly %d)\n",
                        TEST, done, BACKSTOP_CAP);
                failures++;
            }
        }
    }

    if (failures > 0) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all laws hold\n", TEST);
    return 0;
}
