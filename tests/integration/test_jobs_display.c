/**
 * @file test_jobs_display.c
 * @brief lush's curated `jobs` display: ordering, markers, command text.
 *
 * These assert lush's OWN curated job-listing format, not another shell's
 * cosmetics. bash and zsh disagree on the exact shape of these lines (spacing,
 * casing, the trailing `&`), so neither is the oracle. The laws:
 *
 *   1. Oldest-first: the listing orders jobs by ascending id, regardless of the
 *      newest-first order the job list is stored in.
 *   2. Current/previous markers: '+' marks the current job (the most recently
 *      backgrounded, stopped, or selected), '-' the previous, space the rest.
 *      The marker reflects tracked current/previous state, not a transient
 * flag.
 *   3. Full command text: a backgrounded compound command (subshell, brace
 *      group) is rendered in full from its AST, not reduced to its first word
 *      or "unknown". (Backgrounded pipelines are a separate, pre-existing case:
 *      they register no job at all, so there is nothing to display; that
 *      tracking gap is tracked independently and is out of scope here.)
 *
 * Usage: test_jobs_display <lush-binary-path>
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

#define TEST "test_jobs_display"
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

/// Assert every needle in `needles` (NULL-terminated) is present.
static void check_present(const char *lush, const char *label,
                          const char *script, const char *const *needles) {
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

/// Assert every needle in `needles` is present and every needle in `absent` is
/// not.
static void check_present_absent(const char *lush, const char *label,
                                 const char *script, const char *const *needles,
                                 const char *const *absent) {
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

/// Assert `first` appears before `second` in the script's output.
static void check_order(const char *lush, const char *label, const char *script,
                        const char *first, const char *second) {
    char out[8192];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit\n", TEST, label);
        failures++;
        return;
    }
    const char *a = strstr(out, first);
    const char *b = strstr(out, second);
    if (!a || !b) {
        fprintf(stderr, "FAIL %s [%s]: missing markers (got: \"%.400s\")\n",
                TEST, label, out);
        failures++;
        return;
    }
    if (a >= b) {
        fprintf(stderr,
                "FAIL %s [%s]: \"%s\" not before \"%s\" (got: \"%.400s\")\n",
                TEST, label, first, second, out);
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

    /// Law 1: oldest-first ordering. The stored list is newest-first, so a
    /// naive walk would print [3] before [1]; the listing must reverse that.
    /// Assert full ascending order (1 before 2, 2 before 3) so a partial
    /// mis-order that still keeps 1 before 3 cannot slip through.
    check_order(lush, "oldest-first ordering (1<2)",
                "sleep 2 & sleep 2 & sleep 2 & jobs", "[1]", "[2]");
    check_order(lush, "oldest-first ordering (2<3)",
                "sleep 2 & sleep 2 & sleep 2 & jobs", "[2]", "[3]");

    /// Law 2: markers. Newest is current (+), the one before it is previous
    /// (-), the oldest is neither (space). Format is "[id]<marker> <state>".
    check_present(
        lush, "current/previous markers", "sleep 2 & sleep 2 & sleep 2 & jobs",
        (const char *[]){"[3]+ Running", "[2]- Running", "[1]  Running", NULL});

    /// Law 3a: a backgrounded subshell shows its full body, not "unknown" --
    /// the first child is a NODE_SUBSHELL, so the pre-change first-word-only
    /// path produced nothing usable.
    check_present(lush, "subshell command text", "( sleep 2; sleep 2 ) & jobs",
                  (const char *[]){"sleep 2; sleep 2", NULL});

    /// Law 3b: a backgrounded brace group likewise shows its full body.
    check_present(lush, "brace group command text",
                  "{ sleep 2; sleep 2; } & jobs",
                  (const char *[]){"sleep 2; sleep 2", NULL});

    /// Law 4: removing the current job re-derives current AND previous. Three
    /// jobs (current=3, previous=2); consuming job 3 must promote 2 to current
    /// (+) and re-derive 1 as previous (-) -- a '-' job must still exist while
    /// two jobs remain.
    check_present(lush, "reconcile markers after removing current",
                  "sleep 5 & sleep 5 & sh -c 'exit 0' & p=$!; wait $p; jobs",
                  (const char *[]){"[2]+ Running", "[1]- Running", NULL});

    /// Law 5: a job that stops asynchronously becomes the current job (+). Two
    /// running jobs (current=2), then STOP job 1 by pid: job 1 becomes current,
    /// job 2 the previous.
    check_present(lush, "stopped job becomes current",
                  "sleep 5 & p=$!; sleep 5 & kill -STOP $p; sleep 0.3; jobs",
                  (const char *[]){"[1]+ Stopped", "[2]- Running", NULL});

    /// Boundary: a backgrounded pipeline registers no job (a separate,
    /// pre-existing tracking gap). Lock that the listing shows nothing for it,
    /// so the day pipelines become jobs this suite trips and the display
    /// assertions are revisited rather than silently skipped.
    check_present_absent(lush, "backgrounded pipeline registers no job",
                         "sleep 2 | cat & jobs; echo END",
                         (const char *[]){"END", NULL},
                         (const char *[]){"[1]", "sleep 2 | cat", NULL});

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
