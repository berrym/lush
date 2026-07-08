/**
 * @file test_comment_after_operator.c
 * @brief A same-line comment after &&, ||, or | does not break the RHS (#450).
 *
 * A trailing comment on the same line as a control operator masked the operator
 * from the completeness scanner, so the statement was judged complete and the
 * next line -- carrying the operator's right-hand command -- was never joined.
 * The pipeline parser also did not skip a comment after `|` before the next
 * stage. These run the multi-line commands via `lush -c` and check the RHS ran.
 *
 * Usage: test_comment_after_operator <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_comment_after_operator"

/// Run `lush -c script` (config-isolated), capture stdout+stderr into out.
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

/// Assert `needle` is present, and `absent` (if non-NULL) is not.
static void check(const char *lush, const char *label, const char *script,
                  const char *needle, const char *absent) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: run failed\n", TEST, label);
        failures++;
        return;
    }
    if (!strstr(out, needle)) {
        fprintf(stderr, "FAIL %s [%s]: missing \"%s\" (got: \"%.200s\")\n",
                TEST, label, needle, out);
        failures++;
        return;
    }
    if (absent && strstr(out, absent)) {
        fprintf(stderr, "FAIL %s [%s]: unexpected \"%s\"\n", TEST, label,
                absent);
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

    /// A comment after && before the RHS on the next line: RHS still runs.
    check(lush, "&& then comment", "true && # note\necho AND_OK", "AND_OK",
          "error");
    /// Same for ||.
    check(lush, "|| then comment", "false || # note\necho OR_OK", "OR_OK",
          "error");
    /// Same for a pipeline stage after |.
    check(lush, "| then comment", "echo PIPE_OK | # note\ncat", "PIPE_OK",
          "error");
    /// Chained operators each with a comment.
    check(lush, "&& chain with comments",
          "true && # a\necho X1 && # b\necho X2", "X2", "error");

    /// Regression: a comment after a complete command stays complete.
    check(lush, "trailing comment after RHS", "true && echo REG # note", "REG",
          "error");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
