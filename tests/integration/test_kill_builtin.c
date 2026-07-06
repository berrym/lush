/**
 * @file test_kill_builtin.c
 * @brief The kill builtin: job specs, signal forms, -l listing, errors.
 *
 * lush's kill is a builtin (not the external /bin/kill) so it can resolve job
 * specs against the shell's job list. These assert:
 *
 *   1. `kill %n` signals a job (the external kill cannot -- it reports
 *      "illegal process id: %1").
 *   2. `kill -SIG`, `kill -s SIG`, and `kill -NUM` all signal a pid.
 *   3. `kill -l` lists signals by number and name; `kill -l N` prints the name,
 *      `kill -l NAME` prints the number, and a 128+signo status maps back.
 *   4. Bad input produces a targeted structured error with an actionable help
 *      line (invalid signal, no target, non-numeric target, unknown job).
 *
 * Usage: test_kill_builtin <lush-binary-path>
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

#define TEST "test_kill_builtin"
#define REAP_TIMEOUT_MS 15000

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

/// Assert every needle in `needles` is present and every needle in `absent` is
/// not.
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
            fprintf(stderr, "FAIL %s [%s]: expected \"%s\" (got: \"%.400s\")\n",
                    TEST, label, *p, out);
            failures++;
            return;
        }
    }
    for (const char *const *p = absent; p && *p; p++) {
        if (strstr(out, *p)) {
            fprintf(stderr,
                    "FAIL %s [%s]: unexpected \"%s\" (got: \"%.400s\")\n", TEST,
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

    /// Law 1: `kill %n` signals the job. The default TERM ends the sleep; a
    /// following jobs listing no longer shows it Running. The external kill
    /// cannot resolve %1 and would print "illegal process id".
    check(lush, "kill %n signals a job",
          "sleep 5 & kill %1; echo RC=$?; sleep 0.3; jobs; echo END",
          (const char *[]){"RC=0", "END", NULL},
          (const char *[]){"Running", "illegal process id", NULL});

    /// Law 2: signal forms on a pid -- -9, -KILL, -s TERM all terminate it.
    check(lush, "kill -NUM signals a pid",
          "sleep 5 & p=$!; kill -9 $p; echo RC=$?; sleep 0.3; jobs",
          (const char *[]){"RC=0", NULL}, (const char *[]){"Running", NULL});
    check(lush, "kill -SIGNAME signals a pid",
          "sleep 5 & p=$!; kill -KILL $p; echo RC=$?; sleep 0.3; jobs",
          (const char *[]){"RC=0", NULL}, (const char *[]){"Running", NULL});
    check(lush, "kill -s SIGNAL signals a pid",
          "sleep 5 & p=$!; kill -s TERM $p; echo RC=$?; sleep 0.3; jobs",
          (const char *[]){"RC=0", NULL}, (const char *[]){"Running", NULL});

    /// Law 3: -l listing and lookups.
    check(lush, "kill -l lists signals", "kill -l",
          (const char *[]){"SIGHUP", "SIGKILL", "SIGTERM", NULL}, NULL);
    check(lush, "kill -l NUM prints the name", "kill -l 9",
          (const char *[]){"KILL", NULL}, NULL);
    check(lush, "kill -l NAME prints the number", "kill -l KILL; echo -",
          (const char *[]){"9", NULL}, NULL);
    check(lush, "kill -l maps a 128+signo status", "kill -l 143",
          (const char *[]){"TERM", NULL}, NULL);

    /// Law 4: targeted errors with a help line.
    check(lush, "invalid signal is a targeted error", "kill -BOGUS 123",
          (const char *[]){"invalid signal specification", "kill -l", NULL},
          NULL);
    check(lush, "no target is a targeted error", "kill",
          (const char *[]){"no process or job specified", "usage: kill", NULL},
          NULL);
    check(lush, "non-numeric target is a targeted error", "kill abc",
          (const char *[]){"must be process or job IDs", "%job spec", NULL},
          NULL);
    check(lush, "unknown job is reported", "kill %99",
          (const char *[]){"no such job", NULL}, NULL);
    check(lush, "-s without a signal is a targeted error", "kill -s",
          (const char *[]){"-s requires a signal", NULL}, NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
