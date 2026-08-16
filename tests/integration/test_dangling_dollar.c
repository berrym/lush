/**
 * @file test_dangling_dollar.c
 * @brief A lone `$` is a literal `$`, not the shell PID.
 *
 * `$$` is the process ID. A single `$` introduces nothing -- there is no
 * parameter named "" -- so the only coherent reading is the character itself.
 * lush had an explicit special case returning the PID, so `echo $` printed a
 * number and `v=$` stored one (issue #672).
 *
 * Every OTHER position was already correct: `a$`, `"$"`, `'$'`, `5$` and `$%`
 * all produced the literal, which is why this only showed when the `$` was a
 * whole unquoted word. Those positions are pinned here too, because the fix
 * touches the expander they share.
 *
 * bash, zsh and dash all print the literal, and lush's own model gives the
 * same answer, so this is mode-invariant.
 *
 * Usage: test_dangling_dollar <lush-binary-path>
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_dangling_dollar"

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

/// `$$` has no fixed value, so it is checked by SHAPE: all digits, non-empty,
/// and not the literal the lone `$` now produces.
static void check_is_pid(const char *lush, const char *label,
                         const char *script) {
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
    if (len == 0) {
        fprintf(stderr, "FAIL %s [%s]: empty, wanted a pid\n", TEST, label);
        failures++;
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)out[i])) {
            fprintf(stderr, "FAIL %s [%s]: wanted a pid, got \"%.200s\"\n",
                    TEST, label, out);
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

    /// The lone `$` as a whole unquoted word -- the only broken position.
    check(lush, "672 a lone $ is a literal", "echo $", "$");
    check(lush, "672 a lone $ with a following word", "echo $ b", "$ b");
    check(lush, "672 a lone $ assigned", "v=$; echo \"[$v]\"", "[$]");
    check(lush, "672 two lone $ words", "echo $ $", "$ $");
    check(lush, "672 a lone $ through a pipe", "echo $ | cat", "$");
    check(lush, "672 a lone $ inside a substitution", "echo \"$( echo $ )\"",
          "$");
    check(lush, "672 a lone $ as a printf argument", "printf '%s' $", "$");
    check(lush, "672 a lone $ in a for list",
          "for i in $ a; do printf '[%s]' \"$i\"; done", "[$][a]");

    /// `$$` must still be the process ID -- the fix must not reach it.
    check_is_pid(lush, "672 $$ is still the pid", "echo $$");
    check_is_pid(lush, "672 $$ is still the pid when quoted", "echo \"$$\"");

    /// The other special parameters are untouched.
    check(lush, "$? is unaffected", "false; echo $?", "1");
    check(lush, "$# is unaffected", "set -- a b; echo $#", "2");

    /// Positions that were ALREADY correct and share the expander.
    check(lush, "a trailing $ after a literal", "echo a$", "a$");
    check(lush, "a $ inside double quotes", "echo \"$\"", "$");
    check(lush, "a $ inside single quotes", "echo '$'", "$");
    check(lush, "a trailing $ inside double quotes", "echo \"a$\"", "a$");
    check(lush, "a $ after a digit", "echo 5$ 6", "5$ 6");
    check(lush, "a $ before a non-name character", "echo $%", "$%");

    /// A `$` adjacent to a REAL expansion must not disturb it.
    check(lush, "a $ after a variable", "v=hi; echo $v$", "hi$");
    check(lush, "a $ after a quoted variable", "v=hi; echo \"$v$\"", "hi$");
    check(lush, "a $ after a substitution", "echo $(printf x)$", "x$");
    check(lush, "a $ after arithmetic", "echo $((1+1))$", "2$");
    check(lush, "a $ after a parameter operator", "echo ${HOME:+x}$", "x$");
    check(lush, "a lone $ before another special", "echo $ $?", "$ 0");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
