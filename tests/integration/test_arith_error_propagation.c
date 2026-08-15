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

    /// ------------------------------------------ #710: the parameter surfaces
    /// The engine was asked and its verdict was discarded. A failed subscript
    /// is an evaluation failure, not an element that happens to be empty.
    check(lush, "710 element read reports instead of yielding empty",
          "a=(1 2 3); echo \"[${a[1/0]}]\"", 1, "division by zero", NULL);
    check(lush, "710 the diagnostic names the position",
          "a=(1 2 3); echo \"[${a[1/0]}]\"", 1, "evaluating an array subscript",
          NULL);
    check(lush, "710 element length reports instead of yielding 0",
          "a=(1 2 3); echo \"[${#a[1/0]}]\"", 1, "division by zero", NULL);
    /// A length of 0 is a legitimate answer and must not double as the failure
    /// token.
    check(lush, "710 an absent element still has length 0",
          "a=(1 2 3); echo \"[${#a[9]}]\"", 0, "[0]", NULL);

    /// A failure must not be laundered into "unset" and answered by an
    /// operator: :- and friends are answers to ABSENCE (owner decision, Q3).
    check(lush, "710 :- does not rescue a failed subscript",
          "a=(1 2 3); echo \"[${a[1/0]:-DEF}]\"", 1, "division by zero", "DEF");
    check(lush, "710 - does not rescue it either",
          "a=(1 2 3); echo \"[${a[1/0]-DEF}]\"", 1, "division by zero", "DEF");
    check(lush, "710 :+ does not rescue it",
          "a=(1 2 3); echo \"[${a[1/0]:+SET}]\"", 1, "division by zero", "SET");
    check(lush, "710 a case operator does not rescue it",
          "a=(1 2 3); echo \"[${a[1/0]^^}]\"", 1, "division by zero", NULL);
    /// ... while a genuinely absent element still gets its default.
    check(lush, "710 :- still answers a real absence",
          "a=(1 2 3); echo \"[${a[9]:-DEF}]\"", 0, "[DEF]", NULL);

    /// The write-backs reported a value they never assigned.
    check(lush, "710 := on an existing array does not claim a phantom write",
          "a=(1 2 3); echo \"[${a[1/0]:=X}]\"", 1, "division by zero", "[X]");
    check(lush, "710 ... and leaves the array untouched",
          "a=(1 2 3); ${a[1/0]:=X} 2>/dev/null; echo \"[${a[*]}]\"", -1,
          "[1 2 3]", NULL);
    /// Asserted on `declare -p`'s OUTPUT, not its exit status: `declare -p`
    /// on a missing name prints "not found" and still exits 0, so a status
    /// probe here reports BOUND even for a name that was never touched.
    check(lush, "710 := on an unset name creates no phantom binding",
          "echo \"[${u[1/0]:=X}]\"; declare -p u", -1, "u: not found",
          "declare -a u");
    /// ... while the working assign-backs still bind.
    check(lush, "710 := still creates the array when the subscript is sound",
          "echo \"[${u[2]:=X}]\"; declare -p u", 0, "[2]=\"X\"", NULL);

    /// The write path's status was already right; the REASON was generic.
    check(lush, "710 a failed write subscript reports the engine's cause",
          "a=(1 2 3); a[1/0]=X", 1, "division by zero", NULL);
    check(lush, "710 ... rather than the generic integer-subscript text",
          "a=(1 2 3); a[1/0]=X", 1, NULL, "must evaluate to an integer");

    /// Failure kinds other than division reach the same path.
    check(lush, "710 an invalid literal in a subscript is reported",
          "a=(1 2 3); echo \"[${a[08]}]\"", 1, "invalid octal digit", NULL);
    check(lush, "710 a syntax error in a subscript is reported",
          "a=(1 2 3); echo \"[${a[1+]}]\"", 1, "arithmetic", NULL);

    /// ------------------------------------- #711: a slice spec IS arithmetic
    /// Offset and length are expressions, not decimal literals. strtol took a
    /// leading digit run and dropped the rest, so `${s:1+1:2}` sliced from 1
    /// with no length; a spec with no leading digit was not recognized as a
    /// slice at all, and for ${a[@]:...} the whole reference then fell through
    /// to the scalar path and raised a list-in-scalar type error.
    check(lush, "711 scalar offset from a variable",
          "s=abcdefghij; i=2; echo \"[${s:i:2}]\"", 0, "[cd]", NULL);
    check(lush, "711 scalar offset from an expression",
          "s=abcdefghij; echo \"[${s:1+1:2}]\"", 0, "[cd]", NULL);
    check(lush, "711 length from a variable",
          "s=abcdefghij; i=2; echo \"[${s:2:i}]\"", 0, "[cd]", NULL);
    check(lush, "711 array slice with an expression offset",
          "a=(A B C D E); i=2; echo \"[${a[@]:i:2}]\"", 0, "[C D]", NULL);
    check(lush, "711 star form too",
          "a=(A B C D E); i=2; echo "
          "\"[${a[*]:1+1:2}]\"",
          0, "[C D]", NULL);
    check(lush, "711 offset only, no length",
          "a=(A B C D E); i=2; echo \"[${a[@]:i}]\"", 0, "[C D E]", NULL);

    /// The separator is also the ternary's `:`, so the split is tried at the
    /// LAST `:` and falls back to the whole spec when the left side is not an
    /// expression.
    check(lush, "711 a ternary offset is not split at its own colon",
          "s=abcdefghij; i=2; echo \"[${s:i>1?1:2}]\"", 0, "[bcdefghij]", NULL);
    check(lush, "711 a parenthesized ternary keeps its length",
          "s=abcdefghij; i=2; echo \"[${s:(i>1?1:2):3}]\"", 0, "[bcd]", NULL);

    /// The spec follows lush's own base rule, so it agrees with $(( )).
    check(lush, "711 a leading zero means octal here too, as in $(( ))",
          "s=abcdefghijklmno; echo \"[${s:010:2}]\"", 0, "[ij]", NULL);
    check(lush, "711 ... and follows the mode baseline",
          "mode zsh; s=abcdefghijklmno; echo \"[${s:010:2}]\"", 0, "[kl]",
          NULL);

    /// A failed spec is reported and the slice refused, and the diagnostic
    /// names the real cause rather than a parse error on the separator.
    check(lush, "711 a failed offset is reported",
          "s=abcdefghij; echo \"[${s:1/0:2}]\"", 1, "division by zero", NULL);
    check(lush, "711 a failed length is reported",
          "s=abcdefghij; echo \"[${s:2:1/0}]\"", 1, "division by zero", NULL);

    /// The operators start with `:` too and must stay operators -- the #530
    /// regression class, where strtol ate the `+` of `:+`.
    check(lush, "711 :+ is still an operator, not a slice",
          "a=(A B C); echo \"[${a[*]:+2}]\"", 0, "[2]", NULL);
    check(lush, "711 :- is still an operator", "u=; echo \"[${u:-D}]\"", 0,
          "[D]", NULL);
    check(lush, "711 := is still an operator", "u=; echo \"[${u:=D}]\"", 0,
          "[D]", NULL);
    check(lush, "711 a negative offset still needs its space",
          "s=abcdefghij; echo \"[${s: -3}]\"", 0, "[hij]", NULL);
    check(lush, "711 literal specs are unchanged",
          "s=abcdefghij; echo \"[${s:2:3}]\"", 0, "[cde]", NULL);

    /// ------------------------------------ #647: unset's subscript, evaluated
    /// `unset a[i]` read the subscript with strtoll, so `i` became 0 and the
    /// FIRST element was destroyed instead of the one asked for -- silently,
    /// with status 0. Asserted on `declare -p` rather than ${a[@]}, which
    /// hides which index went.
    check(lush, "647 a variable subscript removes the element asked for",
          "a=(e0 e1 e2 e3 e4); i=3; unset \"a[i]\"; declare -p a", 0,
          "[2]=\"e2\" [4]=\"e4\"", "[3]=");
    check(lush, "647 ... and leaves index 0 alone",
          "a=(e0 e1 e2 e3 e4); i=3; unset \"a[i]\"; declare -p a", 0,
          "[0]=\"e0\"", NULL);
    check(lush, "647 an expression subscript",
          "a=(e0 e1 e2 e3 e4); unset \"a[1+1]\"; declare -p a", 0,
          "[1]=\"e1\" [3]=\"e3\"", "[2]=");
    check(lush, "647 a hex subscript addresses no element here",
          "a=(e0 e1 e2); unset \"a[0xa]\"; declare -p a", 0,
          "[0]=\"e0\" [1]=\"e1\" [2]=\"e2\"", NULL);
    /// A literal index was always right; it must stay right, sparseness and
    /// all.
    check(lush, "647 a literal index still removes exactly that element",
          "a=(e0 e1 e2 e3 e4); unset \"a[3]\"; declare -p a", 0,
          "[2]=\"e2\" [4]=\"e4\"", "[3]=");

    /// A failed subscript destroys nothing and says why.
    check(lush, "647 a failed subscript is reported",
          "a=(e0 e1 e2); unset \"a[1/0]\"", 1, "division by zero", NULL);
    check(lush, "647 ... and the array is untouched",
          "a=(e0 e1 e2); unset \"a[1/0]\" 2>/dev/null; declare -p a", -1,
          "[0]=\"e0\" [1]=\"e1\" [2]=\"e2\"", NULL);

    /// zsh mode indexes from 1 on every element surface, including this one.
    /// It read the first element and destroyed the second before.
    check(lush, "647 zsh mode unsets what zsh mode reads",
          "mode zsh; a=(e0 e1 e2); unset \"a[1]\"; declare -p a", 0,
          "[1]=\"e1\" [2]=\"e2\"", "[0]=");
    check(lush, "647 zsh mode index 0 addresses nothing",
          "mode zsh; a=(e0 e1 e2); unset \"a[0]\"; declare -p a", 0,
          "[0]=\"e0\"", NULL);

    /// Neighbouring unset surfaces are unaffected.
    check(lush, "647 an associative key stays a string",
          "declare -A m; m[1/0]=V; unset \"m[1/0]\"; declare -p m", 0,
          "declare -A m=()", NULL);
    check(lush, "647 a scalar still unsets",
          "x=1; unset x; echo \"[${x-GONE}]\"", 0, "[GONE]", NULL);
    check(lush, "647 a whole array still unsets",
          "a=(1 2); unset a; echo \"[${a[@]-GONE}]\"", 0, "[GONE]", NULL);

    /// ------------------------------------------- where silence stays correct
    /// These evaluate SUCCESSFULLY; only a flagged failure becomes loud.
    check(lush, "a valid index with no element stays quiet",
          "a=(1 2 3); echo \"[${a[9]}]\"", 0, "[]", "error");
    check(lush, "an unset operand resolves to 0, not a failure",
          "a=(1 2 3); echo \"[${a[foo]}]\"", 0, "[1]", "error");
    check(lush, "an empty subscript resolves to 0",
          "a=(1 2 3); i=; echo \"[${a[i]}]\"", 0, "[1]", "error");
    check(lush, "an associative key is a string, never arithmetic",
          "declare -A m; m[1/0]=V; echo \"[${m[1/0]}]\"", 0, "[V]", "error");
    check(lush, "arithmetic that succeeds is untouched",
          "a=(1 2 3); i=1; echo \"[${a[i+1]}]\"", 0, "[3]", "error");

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
