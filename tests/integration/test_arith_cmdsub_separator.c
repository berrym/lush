/**
 * @file test_arith_cmdsub_separator.c
 * @brief A `;` inside a nested substitution belongs to that substitution.
 *
 * `$((` is ambiguous: arithmetic expansion, or command substitution of an
 * anonymous function `$(() { ...; })`. lush_dollar_paren_is_arithmetic
 * classifies the two by scanning ahead for a bare `{`, `}`, `;` or newline
 * before the matching `))`.
 *
 * The scan already skipped `${...}` spans, because braces belonging to a
 * parameter expansion are not the anonymous-function braces the guard looks
 * for. It did not skip a nested `$(...)` or backtick span, so a separator
 * inside a nested command substitution disqualified the OUTER construct:
 *
 *     $(( $(false; echo 3) + 1 ))
 *
 * was classified as command substitution, which ran the substitution and then
 * executed its OUTPUT as a command -- `error: 3: command not found`. Both
 * commands had run correctly; the result was simply handed to the wrong
 * consumer. A single command with a trailing `;` failed the same way, so the
 * defect was the separator, not the command count (issue #723).
 *
 * Usage: test_arith_cmdsub_separator <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_arith_cmdsub_separator"

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
    waitpid(pid, NULL, 0);
    return true;
}

static int failures = 0;

static void check(const char *lush, const char *label, const char *script,
                  const char *want) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\", got \"%.200s\"\n", TEST,
                label, want, out);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

/// For cases whose correct outcome is a diagnostic: assert the shape, not the
/// wording, and assert the WRONG diagnostic is absent.
static void check_errors(const char *lush, const char *label,
                         const char *script, const char *want_fragment,
                         const char *absent) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (want_fragment && !strstr(out, want_fragment)) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\" (got: \"%.200s\")\n", TEST,
                label, want_fragment, out);
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

    /// The separator must not reclassify the outer construct. Both spellings,
    /// because the defect was in the shared classifier rather than in either
    /// syntax.
    check(lush, "723 multi-command body, $( ) spelling",
          "echo \"[$(( $(false; echo 3) + 1 ))]\"", "[4]");
    check(lush, "723 multi-command body, backtick spelling",
          "echo \"[$(( `false; echo 3` + 1 ))]\"", "[4]");
    check(lush, "723 both commands ran and the output is one operand",
          "echo \"[$(( $(printf 1; printf 2) ))]\"", "[12]");
    /// One command with a trailing `;`: the count was never the trigger.
    check(lush, "723 a single command with a trailing separator",
          "echo \"[$(( $(printf 7;) ))]\"", "[7]");
    check(lush, "723 a leading separator", "echo \"[$(( $(; printf 7) ))]\"",
          "[7]");

    /// Compound bodies contain `;` by construction.
    check(lush, "723 an if-statement body",
          "echo \"[$(( $(if true; then printf 7; fi) ))]\"", "[7]");
    check(lush, "723 a for-loop body",
          "echo \"[$(( $(for i in 7; do printf $i; done) ))]\"", "[7]");
    check(lush, "723 a brace-group body", "echo \"[$(( $({ printf 7; }) ))]\"",
          "[7]");
    check(
        lush, "723 a while-loop body",
        "echo \"[$(( $(i=0; while [ $i -lt 1 ]; do i=1; printf 7; done) ))]\"",
        "[7]");

    /// Separator-free bodies always worked; they must keep working.
    check(lush, "a single-command body is unchanged",
          "echo \"[$(( $(echo 3) + 1 ))]\"", "[4]");
    check(lush, "a pipeline body is unchanged",
          "echo \"[$(( $(printf 7 | cat) ))]\"", "[7]");
    check(lush, "an && body is unchanged",
          "echo \"[$(( $(printf 7 && true) ))]\"", "[7]");

    /// Nesting, and the other expansion forms the classifier already skipped.
    check(lush, "723 nested arithmetic inside the substitution",
          "echo \"[$(( $(echo $(( 2 + 3 ))) + 1 ))]\"", "[6]");
    check(lush, "723 nested command substitution",
          "echo \"[$(( $( echo $(printf 4) ) + 1 ))]\"", "[5]");
    check(lush, "a ${...} operand is unchanged",
          "echo \"[$(( ${x:-4} + 1 ))]\"", "[5]");
    check(lush, "a ${...} holding a separator is unchanged",
          "x=\"a;b\"; echo \"[$(( ${#x} ))]\"", "[3]");
    check(lush, "plain arithmetic is unchanged", "echo \"[$(( 1 + 2 ))]\"",
          "[3]");
    check(lush, "a nested $(( )) operand is unchanged",
          "echo \"[$(( $((1+2)) + 1 ))]\"", "[4]");

    /// The construct this classifier exists to separate must still be routed
    /// to command substitution.
    check(lush, "the anonymous-function form is still command substitution",
          "echo \"[$(() { echo hi; })]\"", "[hi]");

    /// Other call sites of the same classifier.
    check(lush, "723 in an assignment",
          "a=$(( $(printf 3;) + 1 )); echo \"[$a]\"", "[4]");
    check(lush, "723 in an integer declaration",
          "declare -i n=$(( $(printf 3;) + 1 )); echo \"[$n]\"", "[4]");
    check(lush, "723 in an array subscript",
          "arr=(1 2 3); echo \"[${arr[$(( $(printf 1;) ))]}]\"", "[2]");

    /// Output that is not a number is an ARITHMETIC error, not a command
    /// lookup -- the failure mode this fix removes.
    check_errors(lush, "723 non-numeric output is an arithmetic error",
                 "echo \"[$(( $(echo 1; echo 2) ))]\"", "arithmetic",
                 "command not found");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
