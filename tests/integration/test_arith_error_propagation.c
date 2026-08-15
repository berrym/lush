/**
 * @file test_arith_error_propagation.c
 * @brief An arithmetic failure is reported and refused, wherever it happens.
 *
 * lush states this contract and implements it correctly in $(( )), in (( )),
 * and in `let`: render the engine's typed diagnostic, refuse the operation,
 * and surface non-zero. docs/SEMANTICS.md names the category in writing --
 * reported, non-fatal, the operation refused.
 *
 * The C-style for header did none of that. Its init and update expressions
 * checked the engine's flag only to write a DEBUG line when executor->debug
 * happened to be on, and carried on regardless; its test expression stopped
 * the loop but freed the diagnostic unread. A failed update is the dangerous
 * one: the counter never advances, so a test that depends on it stays true
 * and the loop never ends. `for ((i=0;i<1;i=1/0))` ran forever, printing
 * nothing (#713).
 *
 * Every child here runs under alarm(), so a re-hang fails this test instead
 * of wedging the suite.
 *
 * Usage: test_arith_error_propagation <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_arith_error_propagation"
#define CHILD_TIMEOUT_SECS 10

/// Run `lush -c script` (config-isolated) under an alarm, capturing
/// stdout+stderr and the exit status. Returns false only if the harness
/// itself failed.
static bool run_c(const char *lush, const char *script, char *out,
                  size_t out_sz, int *status_out, bool *timed_out) {
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
        /// The point of several of these cases is that the shell used to loop
        /// forever. Bound the child so a regression is a FAILURE, not a hung
        /// test run.
        alarm(CHILD_TIMEOUT_SECS);
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
    *timed_out = WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM;
    *status_out = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return true;
}

static int failures = 0;

/// Assert the exit status, that `needle` is present (when non-NULL), that
/// `absent` is not (when non-NULL), and that the child never hit the alarm.
static void check(const char *lush, const char *label, const char *script,
                  int want_status, const char *needle, const char *absent) {
    char out[4096];
    int status = 0;
    bool timed_out = false;
    if (!run_c(lush, script, out, sizeof(out), &status, &timed_out)) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (timed_out) {
        fprintf(stderr, "FAIL %s [%s]: did not terminate within %d s\n", TEST,
                label, CHILD_TIMEOUT_SECS);
        failures++;
        return;
    }
    if (want_status >= 0 && status != want_status) {
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
    if (absent && strstr(out, absent)) {
        fprintf(stderr, "FAIL %s [%s]: unexpected \"%s\" (got: \"%.200s\")\n",
                TEST, label, absent, out);
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

    /// ------------------------------------------- the hang, and its siblings
    /// A failed update leaves the counter where it was. Terminating at all is
    /// the property under test; the status and the diagnostic follow from the
    /// contract.
    check(lush, "713 a failing update terminates the loop",
          "for ((i=0;i<1;i=1/0)); do :; done", 1, "division by zero", NULL);
    check(lush, "713 a failing init does not run the loop",
          "for ((i=1/0;i<3;i++)); do echo BODY_RAN; done", 1,
          "division by zero", "BODY_RAN");
    check(lush, "713 a failing test stops the loop",
          "for ((i=0;i<1/0;i++)); do "
          "echo BODY_RAN; done",
          1, "division by zero", "BODY_RAN");

    /// The diagnostic says WHICH expression failed, not merely that arithmetic
    /// failed somewhere in the header.
    check(lush, "713 the diagnostic names the position",
          "for ((i=0;i<1;i=1/0)); do :; done", 1,
          "evaluating the update expression of for", NULL);
    check(lush, "713 ... and for the init", "for ((i=1/0;0;)); do :; done", 1,
          "evaluating the init expression of for", NULL);
    check(lush, "713 ... and for the test", "for ((i=0;i<1/0;i++)); do :; done",
          1, "evaluating the test expression of for", NULL);

    /// Other failure kinds reach the same path, not just division.
    check(lush, "713 a syntax failure in the update also terminates",
          "for ((i=0;i<1;i=)); do :; done", -1, NULL, NULL);
    check(lush, "713 an invalid literal in the init is reported",
          "for ((i=09;0;)); do :; done", 1, "invalid octal digit", NULL);

    /// ------------------------------------------------ severity is non-fatal
    /// Reported, the construct refused, the batch continues -- the calibration
    /// $(( )) already uses. Not the ${var:?} script abort.
    check(lush, "713 the next statement still runs",
          "for ((i=0;i<1;i=1/0)); do :; done; echo AFTER", 0, "AFTER", NULL);
    /// ... and set -e escalates it, as it escalates any non-zero.
    check(lush, "713 set -e escalates it",
          "set -e\n"
          "for ((i=0;i<1;i=1/0)); do :; done\n"
          "echo AFTER",
          1, "division by zero", "AFTER");

    /// ---------------------------------------------- working loops unaffected
    check(lush, "counting up", "for ((i=0;i<3;i++)); do printf %s $i; done", 0,
          "012", NULL);
    check(lush, "counting down", "for ((i=3;i>0;i--)); do printf %s $i; done",
          0, "321", NULL);
    check(lush, "empty header with break",
          "for ((;;)); do break; done; echo ok", 0, "ok", NULL);
    check(lush, "comma operators in init and update",
          "for ((i=0,j=9;i<2;i++,j--)); do printf \"%s%s \" $i $j; done", 0,
          "09 18", NULL);
    check(lush, "a variable bound in the test",
          "n=3; for ((i=0;i<n;i++)); do printf %s $i; done", 0, "012", NULL);
    check(lush, "continue still reaches the update",
          "for ((i=0;i<5;i++)); do if ((i==2)); then continue; fi; printf %s "
          "$i; done",
          0, "0134", NULL);
    check(lush, "nested headers",
          "for ((i=0;i<2;i++)); do for ((j=0;j<2;j++)); "
          "do printf %s%s $i $j; done; done",
          0, "00011011", NULL);
    /// A loop whose body fails must not be mistaken for a header failure.
    check(lush, "a failing body does not stop the loop",
          "for ((i=0;i<3;i++)); do false; done; echo done", 0, "done", NULL);

    /// ------------------------------- the sibling positions already conformant
    /// These pin the contract this fix was measured against; if they ever go
    /// quiet, the reference implementation itself has drifted.
    check(lush, "reference: $(( )) reports and refuses", "echo \"[$((1/0))]\"",
          1, "division by zero", NULL);
    check(lush, "reference: (( )) reports and refuses", "(( 1/0 ))", 1,
          "division by zero", NULL);
    check(lush, "reference: let reports and refuses", "let 'x = 1/0'", 1,
          "division by zero", NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
