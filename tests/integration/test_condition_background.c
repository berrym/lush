/**
 * @file test_condition_background.c
 * @brief `&` after a condition is a parse error, not a backgrounded condition.
 *
 * `&` is a list separator. It backgrounds a list element, but a condition
 * (if/while/until/elif) is a bare and-or, not a list -- a trailing `&` there
 * is a syntax error, as in bash and zsh. Backgrounding a condition would make
 * it return 0 (async) unconditionally, silently breaking control flow: an
 * `if` always takes the then-branch, a `while` loops forever, an `until` never
 * runs. The condition parse must reject the `&`, while `&` inside a loop or
 * conditional BODY still backgrounds normally. The laws:
 *
 *   1. `if cmd & ; then X; fi` is a parse error; X does not run.
 *   2. `while cmd & ; do ... done` is a parse error (no infinite loop).
 *   3. `until cmd & ; do ... done` is a parse error.
 *   4. A `&` in a loop/conditional BODY still backgrounds (a real job).
 *
 * Usage: test_condition_background <lush-binary-path>
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

#define TEST "test_condition_background"
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

    /// Law 1: `&` after an if-condition is a targeted parse error naming the
    /// construct and how to fix it, and the whole input aborts (nothing runs).
    /// The error is the discriminator: the pre-fix behavior backgrounded the
    /// condition and ran the then-branch with no error. (The then-branch words
    /// cannot be checked for absence -- the error display echoes the offending
    /// source line, which contains them.)
    check(lush, "if-condition & is rejected",
          "if true & ; then echo THEN; fi; echo AFTER",
          (const char *[]){"the condition of 'if' cannot be backgrounded",
                           "remove the '&'", "aborting due to 1 error", NULL},
          NULL);

    /// Law 2: `&` after a while-condition is rejected, not an infinite loop.
    check(lush, "while-condition & is rejected",
          "while true & ; do echo BODY; done; echo AFTER",
          (const char *[]){"the condition of 'while' cannot be backgrounded",
                           "aborting due to 1 error", NULL},
          NULL);

    /// Law 3: `&` after an until-condition is rejected.
    check(lush, "until-condition & is rejected",
          "until true & ; do echo BODY; done; echo AFTER",
          (const char *[]){"the condition of 'until' cannot be backgrounded",
                           "aborting due to 1 error", NULL},
          NULL);

    /// Law 3b: elif conditions are covered too.
    check(lush, "elif-condition & is rejected",
          "if false; then echo A; elif true & ; then echo B; fi",
          (const char *[]){"the condition of 'elif' cannot be backgrounded",
                           NULL},
          NULL);

    /// Law 4a: `&` in a loop BODY still backgrounds a real job.
    check(lush, "loop body & still backgrounds",
          "i=0; while [ $i -lt 2 ]; do sleep 3 & i=$((i+1)); done; jobs",
          (const char *[]){"[1]", "[2]", "sleep 3", NULL}, NULL);

    /// Law 4b: `&` in an if/then BODY still backgrounds.
    check(lush, "then body & still backgrounds",
          "if true; then sleep 3 & fi; jobs",
          (const char *[]){"[1]", "sleep 3", NULL}, NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
