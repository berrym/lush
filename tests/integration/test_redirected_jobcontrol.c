/**
 * @file test_redirected_jobcontrol.c
 * @brief Redirected job-control builtins run in the shell, not a subprocess.
 *
 * jobs, wait, fg, and bg depend on the shell's job list and its ability to
 * waitpid the shell's background children. When their stdout is redirected,
 * they must still run in the parent (with the redirection applied there), not
 * in a forked child that has only a copy of the job list. The laws:
 *
 *   1. `wait $! >file` returns the job's actual exit status (a forked wait
 *      cannot reap the parent's child and would return 0).
 *   2. A no-argument `wait >file` still waits every job and returns 0.
 *   3. `jobs >file` writes the job listing to the file.
 *   4. `bg %n >file` resumes the job AND the resume is reflected in the
 *      parent's job list (a forked bg would signal the process but leave the
 *      parent's job state stale).
 *
 * Usage: test_redirected_jobcontrol <lush-binary-path>
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

#define TEST "test_redirected_jobcontrol"
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

    /// Law 1: a redirected `wait $!` returns the job's real status, not 0. A
    /// forked wait cannot reap the parent's child and would report 0.
    check(lush, "redirected wait returns the job status",
          "sh -c 'sleep 0.3; exit 7' & wait $! >/dev/null; echo R=$?",
          (const char *[]){"R=7", NULL}, NULL);

    /// Law 2: a redirected no-argument wait still waits every job (returns 0).
    check(lush, "redirected no-arg wait waits all",
          "sh -c 'exit 4' & sh -c 'exit 5' & wait >/dev/null; echo R=$?",
          (const char *[]){"R=0", NULL}, NULL);

    /// Law 3: a redirected `jobs` writes the listing to the file.
    check(lush, "redirected jobs writes the listing",
          "sleep 5 & f=$(mktemp); jobs >$f; cat $f; rm -f $f",
          (const char *[]){"Running", "sleep 5", NULL}, NULL);

    /// Law 4: a redirected `bg` resumes the job AND updates the parent's job
    /// list. The first `jobs` (to /dev/null) observes the stop; after the
    /// redirected bg, an unredirected `jobs` must show Running, not Stopped --
    /// a forked bg would signal the process but leave the parent state stale.
    check(lush, "redirected bg updates the parent job list",
          "sleep 0.6 & p=$!; kill -STOP $p; sleep 0.2; jobs >/dev/null; bg %1 "
          ">/dev/null; jobs; wait",
          (const char *[]){"Running", NULL}, (const char *[]){"Stopped", NULL});

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
