/**
 * @file test_subshell_script_execution.c
 * @brief A subshell in a script file must not re-execute the rest of the
 * script.
 *
 * The subshell child once terminated with exit() (Issue #441), whose stdio
 * cleanup fclosed the inherited script FILE* and repositioned the shared,
 * seekable input fd; the parent then re-read and re-executed every statement
 * after the `( ... )`, with side effects. The fix terminates the child with
 * _exit() like every other forked child. These run lush on a real script FILE
 * (the `-c` and pipe paths are immune because their input is not seekable) and
 * assert each statement runs exactly once.
 *
 * Usage: test_subshell_script_execution <lush-binary-path>
 */

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST "test_subshell_script_execution"
#define REAP_TIMEOUT_MS 15000

static void msleep(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/// Write `script` to a fresh temp file and return its path in `path_out`.
static bool write_script(const char *script, char *path_out, size_t path_sz) {
    static int seq = 0;
    snprintf(path_out, path_sz, "/tmp/lush_subshell_%d_%d.sh", (int)getpid(),
             seq++);
    FILE *f = fopen(path_out, "w");
    if (!f) {
        return false;
    }
    fputs(script, f);
    fclose(f);
    return true;
}

/// Run `lush <scriptfile>` (config-isolated), capturing stdout+stderr into out.
static bool run_file(const char *lush, const char *scriptfile, char *out,
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
        execl(lush, "lush", scriptfile, (char *)NULL);
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

/// Assert lush's stdout for `script` (run as a file) equals `expected` exactly.
static void expect_exact(const char *lush, const char *label,
                         const char *script, const char *expected) {
    char path[256];
    if (!write_script(script, path, sizeof(path))) {
        fprintf(stderr, "FAIL %s [%s]: could not write script\n", TEST, label);
        failures++;
        return;
    }
    char out[8192];
    bool ok = run_file(lush, path, out, sizeof(out));
    unlink(path);
    if (!ok) {
        fprintf(stderr, "FAIL %s [%s]: shell did not exit\n", TEST, label);
        failures++;
        return;
    }
    if (strcmp(out, expected) != 0) {
        fprintf(stderr, "FAIL %s [%s]: expected \"%s\" got \"%s\"\n", TEST,
                label, expected, out);
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

    /// The statements after a subshell run exactly once, not twice.
    expect_exact(lush, "subshell then statements", "( : )\necho X\necho Y\n",
                 "X\nY\n");
    /// A statement before the subshell is unaffected; the tail runs once.
    expect_exact(lush, "statement, subshell, statement",
                 "echo P\n( : )\necho X\n", "P\nX\n");
    /// Two subshells do not compound the re-execution (the tail ran 3x).
    expect_exact(lush, "two subshells then statement", "( : )\n( : )\necho X\n",
                 "X\n");
    /// The subshell's own output is preserved (not lost to _exit).
    expect_exact(lush, "subshell output preserved", "( echo Q )\necho R\n",
                 "Q\nR\n");
    /// A subshell with an EXIT trap: body, its trap, then the tail -- once.
    expect_exact(lush, "subshell exit trap then statement",
                 "( trap 'echo INNER' EXIT; echo body )\necho after\n",
                 "body\nINNER\nafter\n");

    /// Side effects must run once: an appending command after a subshell writes
    /// a single line, not two.
    char countfile[256];
    snprintf(countfile, sizeof(countfile), "/tmp/lush_subshell_count_%d.txt",
             (int)getpid());
    unlink(countfile);
    char script[512];
    snprintf(script, sizeof(script), "( : )\necho one >> %s\n", countfile);
    char path[256];
    if (write_script(script, path, sizeof(path))) {
        char out[1024];
        run_file(lush, path, out, sizeof(out));
        unlink(path);
        FILE *cf = fopen(countfile, "r");
        int lines = 0;
        if (cf) {
            int c;
            while ((c = fgetc(cf)) != EOF) {
                if (c == '\n') {
                    lines++;
                }
            }
            fclose(cf);
        }
        unlink(countfile);
        if (lines == 1) {
            fprintf(stderr, "ok   %s [side effect runs once]\n", TEST);
        } else {
            fprintf(stderr,
                    "FAIL %s [side effect runs once]: appended %d lines "
                    "(expected 1)\n",
                    TEST, lines);
            failures++;
        }
    }

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
