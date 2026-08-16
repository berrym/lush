/**
 * @file test_loop_redirection_status.c
 * @brief A redirection attached to a loop does not change the loop's status.
 *
 * A compound command's exit status is the status of the last command it
 * executed; a redirection attached to it is a side-channel that carries no
 * status of its own. Every loop form violated that whenever it carried a
 * trailing redirection:
 *
 *     for i in 1; do false; done              -> 1   (correct)
 *     for i in 1; do false; done 2>/dev/null  -> 0   (wrong)
 *
 * parse_trailing_redirections attaches `done >file` as a SIBLING of the loop
 * body, and the shared body walker (execute_command_chain) ran every sibling
 * as a statement. A redirection node has no case in execute_node, so it fell
 * through the default arm and returned 0, overwriting the body's status. The
 * failure was silent: a redirected loop reported success no matter what its
 * body did, so `for ...; done >log || handle_failure` could never fire.
 *
 * execute_if had always skipped these nodes explicitly, which is why the
 * `if ... fi >file` path was unaffected and hid the shape of the defect
 * (issue #635).
 *
 * Usage: test_loop_redirection_status <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_loop_redirection_status"

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

/// Compare the whole captured stdout, not a substring: "1" occurs inside
/// "10", and a status check that accepts a substring would pass on the
/// wrong number.
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// Every loop form must report the body's status through a redirection.
    check(lush, "635 for keeps the body status",
          "for i in 1; do false; done 2>/dev/null; echo $?", "1");
    check(lush, "635 while keeps the body status",
          "i=0; while [ $i -lt 1 ]; do i=1; false; done 2>/dev/null; echo $?",
          "1");
    check(lush, "635 until keeps the body status",
          "i=0; until [ $i -ge 1 ]; do i=1; false; done 2>/dev/null; echo $?",
          "1");
    check(lush, "635 arithmetic for keeps the body status",
          "for ((i=0;i<1;i++)); do false; done 2>/dev/null; echo $?", "1");
    check(lush, "635 repeat keeps the body status",
          "repeat 1; do false; done 2>/dev/null; echo $?", "1");

    /// A status other than 1, so the fix cannot be "always report failure".
    check(lush, "635 an arbitrary status survives",
          "for i in 1; do (exit 3); done >/dev/null; echo $?", "3");
    /// Success must survive too.
    check(lush, "635 success survives",
          "for i in 1; do true; done "
          "2>/dev/null; echo $?",
          "0");
    /// It is the LAST iteration's status, not the first or an OR of them.
    check(lush, "635 the last iteration decides (fail last)",
          "for i in 1 2; do [ $i = 1 ]; done 2>/dev/null; echo $?", "1");
    check(lush, "635 the last iteration decides (pass last)",
          "for i in 1 2; do [ $i = 2 ]; done 2>/dev/null; echo $?", "0");

    /// Multiple redirections, and both stream directions.
    check(lush, "635 two redirections",
          "for i in 1; do false; done >/dev/null 2>&1; echo $?", "1");
    check(lush, "635 input redirection",
          "for i in 1; do false; done </dev/null; echo $?", "1");
    check(lush, "635 append redirection",
          "for i in 1; do false; done >>/dev/null; echo $?", "1");

    /// Every redirection form is a distinct NODE_REDIR_* value flowing
    /// through the same guard: a here-string, a heredoc, an fd allocation,
    /// a clobber and a combined redirect must all behave like `2>`.
    check(lush, "635 here-string",
          "for i in 1; do false; done <<< \"x\"; echo $?", "1");
    check(lush, "635 heredoc",
          "for i in 1; do false; done <<EOF\nh\nEOF\necho $?", "1");
    check(lush, "635 fd allocation",
          "for i in 1; do false; done {fd}>/dev/null; echo $?", "1");
    check(lush, "635 clobber",
          "for i in 1; do false; done >|/dev/null; echo $?", "1");
    check(lush, "635 combined stdout and stderr",
          "for i in 1; do false; done &>/dev/null; echo $?", "1");
    /// A here-string still feeds the body after the fix.
    check(lush, "the here-string still reaches the body",
          "for i in 1; do cat; done <<< \"hs\"", "hs");

    /// The status must reach a following operator, not just $?.
    check(lush, "635 the status drives ||",
          "for i in 1; do false; done 2>/dev/null || echo took-or", "took-or");
    check(lush, "635 the status drives &&",
          "for i in 1; do true; done 2>/dev/null && echo took-and", "took-and");

    /// A loop that never enters its body is still 0 (all three peers agree).
    check(lush, "empty word list is 0",
          "for i in; do false; done 2>/dev/null; echo $?", "0");
    check(lush, "while with a false condition is 0",
          "while false; do :; done 2>/dev/null; echo $?", "0");

    /// Control flow through a redirected loop is unchanged.
    check(lush, "break through a redirected loop",
          "for i in 1 2 3; do [ $i = 2 ] && break; done 2>/dev/null; echo $?",
          "0");
    check(lush, "continue through a redirected loop",
          "for i in 1 2; do continue; false; done 2>/dev/null; echo $?", "0");
    check(lush, "return through a redirected loop",
          "f(){ for i in 1; do return 4; done 2>/dev/null; }; f; echo $?", "4");

    /// The redirection must still be APPLIED -- a fix that stopped honoring
    /// it would also make these status checks pass.
    check(lush, "stderr is still redirected",
          "for i in 1; do echo LEAKED >&2; done 2>/dev/null; echo done",
          "done");
    check(lush, "stdout is still redirected",
          "for i in 1 2; do echo hi; done >/dev/null; echo done", "done");
    check(lush, "the redirection target still receives the body output",
          "t=$(mktemp); for i in 1 2; do echo hi; done >\"$t\"; "
          "tr '\\n' ',' <\"$t\"; rm -f \"$t\"",
          "hi,hi,");

    /// Compounds that were already correct must stay correct.
    check(lush, "if is unchanged",
          "if true; then false; fi 2>/dev/null; echo $?", "1");
    check(lush, "case is unchanged",
          "case x in x) false;; esac 2>/dev/null; "
          "echo $?",
          "1");
    check(lush, "brace group is unchanged", "{ false; } 2>/dev/null; echo $?",
          "1");
    check(lush, "subshell is unchanged", "( false ) 2>/dev/null; echo $?", "1");
    check(lush, "an unredirected loop is unchanged",
          "for i in 1; do false; done; echo $?", "1");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
