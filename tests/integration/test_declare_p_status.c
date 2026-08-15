/**
 * @file test_declare_p_status.c
 * @brief `declare -p` reports failure when it finds nothing.
 *
 * `declare -p NAME` for an unbound name printed a structured "not found"
 * diagnostic and then returned 0, so the diagnostic and the status disagreed.
 * The natural way to ask "is this name bound?" is a status test --
 *
 *     if declare -p u >/dev/null 2>&1; then ... fi
 *
 * -- and it answered "bound" for every name, including ones that had never
 * been touched. That is not hypothetical: it produced a false reading while
 * verifying the #710 array-write rollback, reporting a phantom binding that
 * did not exist (issue #715).
 *
 * Usage: test_declare_p_status <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_declare_p_status"

static bool run_c(const char *lush, const char *script, char *out,
                  size_t out_sz, int *status_out) {
    int pfd[2];
    if (pipe(pfd) != 0) {
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[0]);
        close(pfd[1]);
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
    close(pfd[1]);
    size_t len = 0;
    ssize_t n;
    while (len + 1 < out_sz &&
           (n = read(pfd[0], out + len, out_sz - 1 - len)) > 0) {
        len += (size_t)n;
    }
    out[len] = '\0';
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    *status_out = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return true;
}

static int failures = 0;

static void check(const char *lush, const char *label, const char *script,
                  int want_status, const char *needle) {
    char out[4096];
    int status = 0;
    if (!run_c(lush, script, out, sizeof(out), &status)) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (status != want_status) {
        fprintf(stderr,
                "FAIL %s [%s]: status %d, wanted %d (got: \"%.160s\")\n", TEST,
                label, status, want_status, out);
        failures++;
        return;
    }
    if (needle && !strstr(out, needle)) {
        fprintf(stderr, "FAIL %s [%s]: missing \"%s\" (got: \"%.200s\")\n",
                TEST, label, needle, out);
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

    /// The status agrees with the diagnostic.
    check(lush, "an unbound name fails", "declare -p nosuchvar", 1,
          "not found");
    check(lush, "a scalar succeeds", "x=1; declare -p x", 0, "declare -- x");
    check(lush, "an indexed array succeeds", "a=(1 2); declare -p a", 0,
          "declare -a a");
    check(lush, "an associative array succeeds", "declare -A m; declare -p m",
          0, "declare -A m");
    /// Listing everything is not a lookup and cannot miss.
    check(lush, "the bare listing succeeds", "declare -p", 0, NULL);
    /// One miss among several names is still a failure, as it is for the
    /// reference shells.
    check(lush, "one miss among many fails", "x=1; declare -p x nosuchvar", 1,
          "not found");
    check(lush, "all found among many succeeds", "x=1; y=2; declare -p x y", 0,
          "declare -- y");

    /// The idiom this exists for. Before the fix both branches printed BOUND.
    check(lush, "the status test answers UNBOUND for a missing name",
          "if declare -p nosuchvar >/dev/null 2>&1; then echo BOUND; else echo "
          "UNBOUND; fi",
          0, "UNBOUND");
    check(lush, "the status test answers BOUND for a real one",
          "x=1; if declare -p x >/dev/null 2>&1; then echo BOUND; else echo "
          "UNBOUND; fi",
          0, "BOUND");

    /// An unset name must not be resurrected by asking about it.
    check(lush, "asking does not create the binding",
          "unset u; declare -p u >/dev/null 2>&1; if declare -p u >/dev/null "
          "2>&1; then echo BOUND; else echo UNBOUND; fi",
          0, "UNBOUND");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
