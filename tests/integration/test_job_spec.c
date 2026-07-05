/**
 * @file test_job_spec.c
 * @brief The shared job-control job-spec parser (executor_resolve_job_spec).
 *
 * fg, bg, and wait resolve a `%...` job spec through one shared parser. These
 * exercise it end-to-end via `wait` (whose exit status echoes the resolved
 * job's status, making resolution observable) plus a bg/fg no-argument case.
 * The spec forms are the bash/zsh consensus:
 *
 *   %n          job number n
 *   %% / %+     the current job
 *   %-          the previous job
 *   %str        the job whose command begins with str
 *   %?str       the job whose command contains str
 *   (no arg)    fg/bg act on the current job
 *
 * Note: an unquoted `%name` word is a lush Map kind sigil, so the name-matching
 * forms are quoted here (`wait "%sh"`); the numeric and positional forms have
 * no kind-sigil collision and are used unquoted.
 *
 * Usage: test_job_spec <lush-binary-path>
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

#define TEST "test_job_spec"
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

    /// %+ / %% resolve the current job (the most recently backgrounded): job 2
    /// (exit 7), not job 1 (exit 4).
    check(lush, "%+ is the current job",
          "sh -c 'exit 4' & sh -c 'exit 7' & wait %+; echo R=$?",
          (const char *[]){"R=7", NULL}, NULL);
    check(lush, "%% is the current job",
          "sh -c 'exit 4' & sh -c 'exit 6' & wait %%; echo R=$?",
          (const char *[]){"R=6", NULL}, NULL);

    /// %- resolves the previous job: job 1 (exit 4).
    check(lush, "%- is the previous job",
          "sh -c 'exit 4' & sh -c 'exit 7' & wait %-; echo R=$?",
          (const char *[]){"R=4", NULL}, NULL);

    /// %n resolves job number n.
    check(lush, "%1 / %2 are job numbers",
          "sh -c 'exit 3' & sh -c 'exit 9' & wait %1; echo A=$?; sh -c 'exit "
          "5' & wait %3; echo B=$?",
          (const char *[]){"A=3", "B=5", NULL}, NULL);

    /// %str (quoted) matches a job whose command begins with str.
    check(lush, "%str prefix match (quoted)",
          "sh -c 'exit 8' & wait \"%sh\"; echo R=$?",
          (const char *[]){"R=8", NULL}, NULL);

    /// %?str (quoted) matches a job whose command contains str.
    check(lush, "%?str substring match (quoted)",
          "sh -c 'exit 8' & wait \"%?xit\"; echo R=$?",
          (const char *[]){"R=8", NULL}, NULL);

    /// Prefix and substring are distinct: for a command that CONTAINS "sleep"
    /// but does not BEGIN with it, %sleep (prefix) must miss while %?sleep
    /// (substring) matches. This fails if prefix matching degrades to a
    /// substring search.
    check(lush, "%str is a prefix, not a substring",
          "sh -c 'sleep 0.2' & wait \"%sleep\" 2>&1; echo R=$?",
          (const char *[]){"no such job", "R=127", NULL}, NULL);
    check(lush, "%?str matches mid-command",
          "sh -c 'sleep 0.2' & wait \"%?sleep\"; echo R=$?",
          (const char *[]){"R=0", NULL}, (const char *[]){"no such job", NULL});

    /// An ambiguous prefix (two matching jobs) is rejected before waiting.
    check(lush, "ambiguous spec rejected",
          "sleep 5 & sleep 5 & wait \"%sleep\" 2>&1; echo R=$?",
          (const char *[]){"ambiguous job spec", "R=127", NULL}, NULL);

    /// A spec that names no job reports "no such job".
    check(lush, "no such job", "sleep 5 & wait %9 2>&1; echo R=$?",
          (const char *[]){"no such job", "R=127", NULL}, NULL);

    /// bg with an explicit %n resolves and resumes a stopped job. The
    /// unredirected `jobs` forces a status poll so the stop is observed before
    /// bg (bg does not itself poll -- a separate, pre-existing job-control
    /// gap).
    check(lush, "bg %n resumes a stopped job",
          "sleep 0.5 & p=$!; kill -STOP $p; sleep 0.2; jobs; bg %1; echo "
          "BG=$?; wait",
          (const char *[]){"BG=0", NULL},
          (const char *[]){"no such job", "already in background", NULL});

    /// bg with no argument acts on the current job.
    check(lush, "bg no-arg resumes the current job",
          "sleep 0.5 & p=$!; kill -STOP $p; sleep 0.2; jobs; bg; echo BG=$?; "
          "wait",
          (const char *[]){"BG=0", NULL},
          (const char *[]){"no such job", "already in background", NULL});

    /// fg with no argument foregrounds the CURRENT job (job 2, the short one),
    /// leaving the older job 1 still running -- the pre-change default of "job
    /// 1" would instead have blocked on the long job.
    check(lush, "fg no-arg foregrounds the current job",
          "sleep 5 & sleep 0.2 & fg; jobs; echo END",
          (const char *[]){"[1]", "Running", "END", NULL}, NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
