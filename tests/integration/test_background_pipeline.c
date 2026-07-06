/**
 * @file test_background_pipeline.c
 * @brief A trailing `&` backgrounds the whole and-or list, not the last stage.
 *
 * `&` is a list separator: it backgrounds the entire preceding and-or (a
 * pipeline or `&&`/`||` chain), not just its final pipeline stage or operand.
 * The parser previously handled `&` inside the right-recursive pipeline parse,
 * so `a | b &` bound as `a | (b &)` -- the pipeline ran in the foreground,
 * registered no job, and set no `$!`. The laws:
 *
 *   1. `cmd1 | cmd2 &` registers one job for the whole pipeline (with the full
 *      pipeline as its command text) and sets `$!`.
 *   2. `cmd1 && cmd2 &` backgrounds the whole and-or list.
 *   3. A backgrounded pipeline runs asynchronously (the shell does not block on
 *      it), so a following command runs immediately.
 *   4. `a & b &` still registers a separate job per `&`.
 *
 * Usage: test_background_pipeline <lush-binary-path>
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

#define TEST "test_background_pipeline"
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

/// Assert every needle in `needles` is present.
static void check(const char *lush, const char *label, const char *script,
                  const char *const *needles) {
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
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// Law 1: a backgrounded pipeline registers one job for the whole pipeline
    /// and sets $!. Pre-change this pipeline ran in the foreground with no job
    /// and an empty $!.
    check(lush, "pipeline backgrounds with a job and $!",
          "sleep 2 | cat & p=$!; jobs; echo \"PID=[$p]\"",
          (const char *[]){"sleep 2 | cat", "[1]", NULL});
    check(lush, "backgrounded pipeline sets $!",
          "sleep 2 | cat & p=$!; case $p in '') echo NOPID;; *) echo HASPID;; "
          "esac",
          (const char *[]){"HASPID", NULL});

    /// Law 2: `&` backgrounds the whole and-or list, so the job's command text
    /// is the full list, not just the last operand.
    check(lush, "and-or list backgrounds as a whole", "true && sleep 2 & jobs",
          (const char *[]){"true && sleep 2", NULL});

    /// Law 3: a backgrounded pipeline is asynchronous -- the following command
    /// runs without waiting for it.
    check(lush, "backgrounded pipeline is asynchronous",
          "sleep 2 | cat & echo AFTER", (const char *[]){"AFTER", NULL});

    /// Law 4 (regression): each `&` in a list still makes its own job.
    check(lush, "multiple background jobs in a list",
          "sleep 2 & sleep 2 & jobs", (const char *[]){"[1]", "[2]", NULL});

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
