/**
 * @file test_trap_disposition.c
 * @brief `trap` signal dispositions: ignore, catch-any-signal, reset, listing.
 *
 * set_trap once conflated `trap '' SIG` (ignore) with removal and only
 * installed a handler for six hardcoded signals. These assert the corrected
 * behavior:
 *
 *   1. `trap '' SIG` installs SIG_IGN -- the signal is ignored, not reset to
 * the default that would kill the shell (#408).
 *   2. `trap 'cmd' SIG` fires for ANY catchable signal, not a hardcoded subset:
 *      a trap on SIGWINCH / SIGTSTP / SIGPIPE now runs its command.
 *   3. `trap - SIG` after an ignore restores the default disposition.
 *   4. `trap '' SIG` appears in the trap listing as `trap -- '' NAME`.
 *   5. Trapping SIGKILL or SIGSTOP is a targeted error (they cannot be caught);
 *      `trap - KILL` (a reset no-op) is still allowed.
 *   6. `trap -l` lists every signal the platform defines, not seven hardcoded.
 *
 * Usage: test_trap_disposition <lush-binary-path>
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

#define TEST "test_trap_disposition"
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

    /// Law 1: `trap '' SIG` ignores the signal. Sending it to the shell no
    /// longer terminates it, so the following echo runs.
    check(lush, "trap '' TERM ignores (survives self-TERM)",
          "trap '' TERM; kill -TERM $$; echo SURVIVED",
          (const char *[]){"SURVIVED", NULL}, NULL);
    check(lush, "trap '' HUP ignores (survives self-HUP)",
          "trap '' HUP; kill -HUP $$; echo SURVIVED",
          (const char *[]){"SURVIVED", NULL}, NULL);

    /// Control: without the ignore, a self-TERM kills the shell -- no echo.
    check(lush, "no trap: self-TERM kills the shell",
          "kill -TERM $$; echo NOPE", NULL, (const char *[]){"NOPE", NULL});

    /// Law 2: a trap fires for ANY catchable signal, not a hardcoded subset.
    /// WINCH, TSTP, and PIPE were all outside the old six-signal list.
    check(lush, "trap fires on SIGWINCH",
          "trap 'echo GOT_WINCH' WINCH; kill -WINCH $$; sleep 0.2; echo AFTER",
          (const char *[]){"GOT_WINCH", "AFTER", NULL}, NULL);
    check(lush, "trap fires on SIGTSTP",
          "trap 'echo GOT_TSTP' TSTP; kill -TSTP $$; sleep 0.2; echo AFTER",
          (const char *[]){"GOT_TSTP", "AFTER", NULL}, NULL);
    check(lush, "trap fires on SIGPIPE",
          "trap 'echo GOT_PIPE' PIPE; kill -PIPE $$; sleep 0.2; echo AFTER",
          (const char *[]){"GOT_PIPE", "AFTER", NULL}, NULL);

    /// Law 3: `trap - SIG` after an ignore restores the default. The default
    /// TERM disposition then kills the shell -- no echo after the second kill.
    check(lush, "trap - SIG restores the default disposition",
          "trap '' TERM; trap - TERM; kill -TERM $$; echo NOPE", NULL,
          (const char *[]){"NOPE", NULL});

    /// Law 4: an ignored signal appears in the listing as `trap -- '' NAME`.
    check(lush, "trap listing shows an ignore entry", "trap '' TERM; trap",
          (const char *[]){"trap -- '' TERM", NULL}, NULL);

    /// Law 5: SIGKILL / SIGSTOP cannot be trapped -- targeted error.
    check(lush, "trapping SIGKILL is a targeted error", "trap 'echo x' KILL",
          (const char *[]){"cannot be trapped", "TERM, INT, or EXIT", NULL},
          NULL);
    check(lush, "trapping SIGSTOP is a targeted error", "trap '' STOP",
          (const char *[]){"cannot be trapped", NULL}, NULL);
    /// `trap - KILL` is a harmless reset no-op and stays allowed.
    check(lush, "trap - KILL is an allowed no-op", "trap - KILL; echo RESET_OK",
          (const char *[]){"RESET_OK", NULL},
          (const char *[]){"cannot be trapped", NULL});

    /// Law 6: `trap -l` lists every signal, not seven hardcoded lines.
    check(lush, "trap -l lists the full signal set", "trap -l",
          (const char *[]){"SIGHUP", "SIGKILL", "SIGTERM", "SIGWINCH", NULL},
          (const char *[]){"exit from shell", NULL});

    /// Law 7: `trap '' EXIT` records an ignore entry (listed as `trap -- ''
    /// EXIT`), matching the bash+zsh consensus, instead of removing it.
    check(lush, "trap '' EXIT records an ignore entry", "trap '' EXIT; trap",
          (const char *[]){"trap -- '' EXIT", NULL}, NULL);
    /// And the recorded ignore suppresses a previously set exit action.
    check(lush, "trap '' EXIT suppresses a prior exit action",
          "trap 'echo SHOULD_NOT_FIRE' EXIT; trap '' EXIT; echo body",
          (const char *[]){"body", NULL},
          (const char *[]){"SHOULD_NOT_FIRE", NULL});

    /// Law 8: a numeric signal past the platform range is rejected, not
    /// silently recorded as a trap that could never fire.
    check(lush, "out-of-range numeric signal is rejected", "trap 'echo x' 9999",
          (const char *[]){"invalid signal specification", NULL}, NULL);

    /// Law 9 (real-time signals, where the platform has them -- Linux): a trap
    /// on an RT signal fires. The former 32-bit pending bitmask recorded such a
    /// trap but could never dispatch it.
#if defined(SIGRTMIN)
    if (SIGRTMIN + 1 < NSIG) {
        char rt_script[160];
        snprintf(rt_script, sizeof rt_script,
                 "trap 'echo GOT_RT' %d; kill -%d $$; sleep 0.2; echo AFTER",
                 SIGRTMIN + 1, SIGRTMIN + 1);
        check(lush, "trap fires on a real-time signal", rt_script,
              (const char *[]){"GOT_RT", "AFTER", NULL}, NULL);
    }
#endif

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
