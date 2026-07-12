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
 *      group, or pipeline) is rendered in full from its AST, not reduced to its
 *      first word or "unknown".
 *
 * Usage: test_jobs_display <lush-binary-path>
 */

#include <fcntl.h>
#include <regex.h>
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

/// Assert the script's output does or does not match an extended regex.
/// Used for the long job format, whose PID column is non-deterministic.
static void check_regex(const char *lush, const char *label, const char *script,
                        const char *pattern, bool want_match) {
    char out[8192];
    if (!run_script(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit\n", TEST, label);
        failures++;
        return;
    }
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE) != 0) {
        fprintf(stderr, "FAIL %s [%s]: bad regex\n", TEST, label);
        failures++;
        return;
    }
    bool matched = regexec(&re, out, 0, NULL, 0) == 0;
    regfree(&re);
    if (matched != want_match) {
        fprintf(stderr, "FAIL %s [%s]: %s \"%s\" (got: \"%.400s\")\n", TEST,
                label,
                want_match ? "expected match for" : "unexpected match for",
                pattern, out);
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

    /// Law 3c: a backgrounded pipeline registers one job whose command text is
    /// the full pipeline.
    check_present(lush, "pipeline command text", "sleep 2 | cat & jobs",
                  (const char *[]){"[1]", "sleep 2 | cat", NULL});

    /// Law 6: the long format (zsh long_list_jobs / bash `jobs -l`) inserts
    /// the leader PID between the marker and the state. `jobs -l` and the
    /// long_list_jobs option both produce it; the default listing does not.
    check_regex(lush, "jobs -l shows the PID column", "sleep 5 & jobs -l",
                "\\[1\\][-+] [0-9]+ ", true);
    check_regex(lush, "long_list_jobs option shows the PID column",
                "setopt long_list_jobs; sleep 5 & jobs", "\\[1\\][-+] [0-9]+ ",
                true);
    check_regex(lush, "default jobs omits the PID column", "sleep 5 & jobs",
                "\\[1\\][-+] [0-9]+ ", false);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
