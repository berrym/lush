/**
 * @file test_array_assign_location.c
 * @brief Diagnostics from an array assignment cite their source.
 *
 * NODE_ARRAY_ASSIGN and NODE_ARRAY_APPEND were built with new_node() rather
 * than new_node_at(), so they carried no source location. executor_current_loc
 * had nothing to report while executing them, and EVERY diagnostic raised on
 * that path -- a readonly refusal, an invalid subscript, an arithmetic failure
 * inside the subscript -- printed without its `--> file:line:col` line and
 * without the rust-style snippet, while the scalar assignment path beside it
 * cited its source correctly (issue #725).
 *
 * lush's identity claim is structured errors WITH source citations, so the
 * citation is part of the contract rather than decoration.
 *
 * Usage: test_array_assign_location <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_array_assign_location"

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

/// Assert the diagnostic carries a `-->` citation, plus any extra needle.
static void cites(const char *lush, const char *label, const char *script,
                  const char *needle) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (!strstr(out, "-->")) {
        fprintf(stderr, "FAIL %s [%s]: no source citation (got: \"%.200s\")\n",
                TEST, label, out);
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

    /// Every raise on the element-assignment path.
    cites(lush, "readonly array element", "a=(1 2); readonly a; a[0]=X",
          "E1117");
    cites(lush, "arithmetic failure in the subscript", "a=(1 2 3); a[1/0]=X",
          "E1105");
    cites(lush, "invalid literal in the subscript", "a=(1 2 3); a[08]=X",
          "invalid octal digit");
    /// The array-literal form builds a different node; it must cite too.
    cites(lush, "readonly array literal", "readonly a=(1 2); a=(3 4)", NULL);

    /// The snippet, not just the line reference -- the caret is the point.
    {
        char out[4096];
        if (run_c(lush, "a=(1 2); readonly a; a[0]=X", out, sizeof(out)) &&
            strstr(out, "a[0]=X") && strstr(out, "^")) {
            fprintf(stderr, "ok   %s [the snippet shows the offending word]\n",
                    TEST);
        } else {
            fprintf(stderr,
                    "FAIL %s [the snippet shows the offending word]: "
                    "(got: \"%.200s\")\n",
                    TEST, out);
            failures++;
        }
    }

    /// The scalar path was always correct and must stay so.
    cites(lush, "scalar assignment still cites", "readonly r=1; r=2", "E1117");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
