/**
 * @file test_err_trap_per_failure.c
 * @brief The ERR trap reports a failing command, not a propagating status.
 *
 * The ERR pseudo-signal notifies that a command FAILED. Two consequences
 * follow, and lush honored neither uniformly:
 *
 *   1. A compound command carrying that failure outward is the SAME event
 *      being re-reported. lush fired on any non-zero status reaching a
 *      statement walker, so one failing command notified once per enclosing
 *      compound -- three times at two levels of nesting.
 *
 *   2. A command whose failure is being TESTED is not an error: the shell is
 *      asking whether it succeeds. That covers a condition, the operand of
 *      `!`, and any non-final operand of `&&` / `||`. Tested-ness is
 *      INHERITED -- in `true && false || true` the inner `&&` sits in the
 *      tested left position of the `||`, so its final operand is not final
 *      overall.
 *
 * The same root also caused the opposite defect: a `case` arm ran its
 * statements through no firing walker at all, so two failing statements in an
 * arm notified ONCE (issue #729).
 *
 * bash and zsh agree on every shape here, and it is lush's own model of what
 * the trap means, so this is mode-invariant.
 *
 * Usage: test_err_trap_per_failure <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_err_trap_per_failure"

static bool run_c(const char *lush, const char *script, char *out,
                  size_t out_sz) {
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
    waitpid(pid, NULL, 0);
    return true;
}

static int failures = 0;

/// `body` runs with an ERR trap that prints one `T` per notification; `want`
/// is the exact concatenation expected. Counting is the whole point, so the
/// comparison is on the full output, never a substring.
static void check(const char *lush, const char *label, const char *body,
                  const char *want) {
    char script[1024];
    char out[4096];
    snprintf(script, sizeof(script), "trap 'printf T' ERR; %s; printf .", body);
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\", got \"%.200s\"\n", TEST,
                label, want, out);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

/// Same, but the script is used verbatim (for `set -e` / `set -E` shapes).
static void check_raw(const char *lush, const char *label, const char *script,
                      const char *want) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\", got \"%.200s\"\n", TEST,
                label, want, out);
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

    /// One failing command notifies once, however deeply it is nested.
    check(lush, "729 a plain failure notifies once", "false", "T.");
    check(lush, "729 a loop does not re-report its body",
          "for i in 1; do false; done", "T.");
    check(lush, "729 nesting does not multiply the notification",
          "for i in 1; do for j in 1; do false; done; done", "T.");
    check(lush, "729 a brace group inside a loop does not either",
          "for i in 1; do { false; }; done", "T.");
    check(lush, "729 while does not re-report its body",
          "i=0; while [ $i -lt 1 ]; do i=1; false; done", "T.");
    check(lush, "729 if does not re-report its branch",
          "if true; then false; fi", "T.");
    check(lush, "729 a brace group does not re-report", "{ false; }", "T.");

    /// It is per FAILING COMMAND, not per compound status: a loop that
    /// SUCCEEDS still reports the failures inside it.
    check(lush, "729 every failing command reports (loop succeeds)",
          "for i in 1 2; do false; true; done", "TT.");
    check(lush, "729 two iterations, two failures",
          "for i in 1 2; do false; done", "TT.");
    check(lush, "729 two failures in one body",
          "for i in 1; do false; false; done", "TT.");
    check(lush, "729 two failures in a brace group", "{ false; false; }",
          "TT.");
    check(lush, "729 two failures in an if branch",
          "if true; then false; false; fi", "TT.");

    /// The opposite defect: a case arm reported nothing of its own.
    check(lush, "729 a case arm reports its failures",
          "case x in x) false; false;; esac", "TT.");
    check(lush, "729 a single failure in a case arm",
          "case x in x) false;; esac", "T.");

    /// A tested command is a question, not an error.
    check(lush, "729 a false condition is not an error", "if false; then :; fi",
          ".");
    check(lush, "729 an elif condition is not an error",
          "if true; then :; elif false; then :; fi", ".");
    check(lush, "729 a while condition is not an error",
          "while false; do :; done", ".");
    check(lush, "729 an until condition is not an error",
          "until true; do :; done", ".");
    check(lush, "729 a compound condition is not an error",
          "if { false; }; then :; fi", ".");
    check(lush, "729 a loop as a condition is not an error",
          "if for i in 1; do false; done; then :; fi", ".");
    check(lush, "729 the operand of ! is not an error", "! true", ".");
    check(lush, "729 a compound operand of ! is not an error", "! { false; }",
          ".");

    /// Only the LAST command of a && / || list can be an error.
    check(lush, "729 a short-circuited && does not report", "false && true",
          ".");
    check(lush, "729 && with both failing reports neither", "false && false",
          ".");
    check(lush, "729 the final operand of && reports", "true && false", "T.");
    check(lush, "729 a selecting || failure does not report", "false || true",
          ".");
    check(lush, "729 the final operand of || reports", "false || false", "T.");
    check(lush, "729 an unreached || operand does not report", "true || false",
          ".");

    /// Tested-ness is inherited: the whole && sits in the tested left of ||.
    check(lush, "729 an && inside a tested position stays silent",
          "true && false || true", ".");
    check(lush, "729 a brace group there stays silent too",
          "true && { false; } || true", ".");
    check(lush, "729 a compound left of && stays silent", "{ false; } && true",
          ".");
    check(lush, "729 a compound left of || stays silent", "{ false; } || true",
          ".");
    check(lush, "729 a case left of && stays silent",
          "case x in x) false;; esac && true", ".");

    /// A pipeline reports on its own status, not per stage.
    check(lush, "729 a failing stage that is not last does not report",
          "false | true", ".");
    check(lush, "729 a failing last stage reports", "true | false", "T.");

    /// Function calls: the call is an ordinary command where it appears, and
    /// errtrace controls whether the body's own failures surface.
    check(lush, "729 a function call reports once", "f(){ false; }; f", "T.");
    check(lush, "729 a function with two failures still reports once",
          "f(){ false; false; }; f", "T.");
    check_raw(lush, "729 errtrace surfaces the body statement too",
              "set -E; trap 'printf T' ERR; f(){ false; }; f; printf .", "TT.");
    check_raw(lush, "729 errtrace does not multiply a loop body",
              "set -E; trap 'printf T' ERR; f(){ for i in 1 2; do false; done; "
              "}; f; printf .",
              "TTT.");

    /// errexit still aborts, and the notification still precedes the abort.
    check_raw(lush, "729 errexit aborts after one notification",
              "set -e; trap 'printf T' ERR; for i in 1; do false; done; printf "
              "UNREACHED",
              "T");
    check_raw(lush, "729 a tested failure does not abort under errexit",
              "set -e; false || true; printf REACHED", "REACHED");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
