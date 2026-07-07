/**
 * @file test_fork_child_script_reexec.c
 * @brief A forked child in a script file must not re-execute the rest of it.
 *
 * A forked child that terminated with exit() (Issues #441, #444) ran stdio
 * cleanup, whose fclose of the inherited script FILE* repositioned the shared,
 * seekable input fd; the parent then re-read and re-executed every statement
 * after the fork point, with side effects. The fix terminates every forked
 * child with _exit(). The trigger is any fork child in a script: a subshell
 * `( ... )`, an external command with a failing redirect, or an `exec` failure
 * inside a subshell or pipeline stage. These run lush on a real script FILE
 * (the `-c` and pipe paths are immune because their input is not seekable) and
 * assert each statement runs exactly once.
 *
 * Usage: test_fork_child_script_reexec <lush-binary-path>
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

#define TEST "test_fork_child_script_reexec"
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

/// Count non-overlapping occurrences of `needle` in `hay`.
static int count_substr(const char *hay, const char *needle) {
    int n = 0;
    size_t nlen = strlen(needle);
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += nlen) {
        n++;
    }
    return n;
}

/// Assert `marker` appears exactly once in lush's output for `script`. Used
/// where the child also prints an error diagnostic (so an exact-output match
/// would be brittle): only the tail marker's repetition matters.
static void expect_marker_once(const char *lush, const char *label,
                               const char *script, const char *marker) {
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
    int c = count_substr(out, marker);
    if (c != 1) {
        fprintf(stderr,
                "FAIL %s [%s]: marker \"%s\" appeared %d times (expected 1) in "
                "\"%.400s\"\n",
                TEST, label, marker, c, out);
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

    /// An external command with a failing redirect: the child terminated with
    /// exit() before exec, corrupting the parent's read offset. The diagnostic
    /// goes to stderr, so assert the tail marker runs exactly once.
    expect_marker_once(lush, "external command with a failing redirect",
                       "ls / > /nonexistent_dir_xyz/f\necho TAILMARK\n",
                       "TAILMARK");

    /// A path-based command that fails to exec (execvp ENOENT): a bare name is
    /// caught before the fork, but a name containing '/' forks and reaches the
    /// exec-failure termination in the child.
    expect_marker_once(lush, "external command that fails to exec",
                       "/nonexistent_cmd_xyz\necho TAILMARK\n", "TAILMARK");

    /// An exec failure inside a subshell: bin_exec's exit() ran in the forked
    /// child. The tail must run once.
    expect_marker_once(lush, "exec failure inside a subshell",
                       "( exec /nonexistent_cmd_xyz )\necho TAILMARK\n",
                       "TAILMARK");

    /// An exec failure in a pipeline stage: same, in the pipeline child.
    expect_marker_once(lush, "exec failure in a pipeline stage",
                       "exec /nonexistent_cmd_xyz | cat\necho TAILMARK\n",
                       "TAILMARK");

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
