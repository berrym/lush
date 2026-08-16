/**
 * @file test_nested_backticks.c
 * @brief An escaped backtick opens a nested substitution, not a literal.
 *
 * POSIX 2.6.3: inside `...` a backslash keeps its literal meaning EXCEPT
 * before `$`, a backtick or another backslash, where it escapes that byte.
 * An embedded `\`` is therefore the delimiter of a substitution one level
 * down, and removing the backslashes is what lets the body parse as one.
 *
 * lush got this wrong in two independent places, which is why the two
 * spellings failed differently:
 *
 *   - The UNQUOTED backtick scanner in the tokenizer had no escape handling
 *     at all, so the span ended at the first `\`` and the rest of the line
 *     mis-parsed: `echo \`echo \\\`printf 4\\\`\`` reported an unterminated
 *     backtick for input every other shell runs. The double-quoted reader and
 *     lush_dequote_span both already scanned it correctly -- only this path
 *     did not.
 *
 *   - The substitution body was parsed VERBATIM, so a surviving `\`` reached
 *     the sub-parse as a LITERAL backtick and the nested command was echoed
 *     as its own source text instead of being run (issue #732).
 *
 * bash, zsh and dash all evaluate these, so the consensus is lush's default.
 *
 * Usage: test_nested_backticks <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_nested_backticks"

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

/// For cases whose contract is "a diagnostic of this shape", not exact text.
static void check_contains(const char *lush, const char *label,
                           const char *script, const char *want) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (!strstr(out, want)) {
        fprintf(stderr,
                "FAIL %s [%s]: wanted a message containing \"%s\" "
                "(got: \"%.200s\")\n",
                TEST, label, want, out);
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

    /// The nested substitution RUNS; it is not echoed as its own source.
    check(lush, "732 nested backticks inside double quotes",
          "echo \"[`echo \\`printf 4\\``]\"", "[4]");
    check(lush, "732 nested backticks unquoted", "echo `echo \\`printf 4\\``",
          "4");
    check(lush, "732 nested backticks in an assignment",
          "v=`echo \\`printf 4\\``; echo \"[$v]\"", "[4]");
    check(lush, "732 three levels",
          "echo \"[`echo \\`echo \\\\\\`printf 9\\\\\\`\\``]\"", "[9]");

    /// The escape set is exactly `$`, backtick and backslash; every other
    /// `\X` keeps its backslash, so a body that uses one is unaffected.
    /// `\$` yields `$`, which then expands as usual. This pins parity with
    /// the peers rather than discriminating on its own: an unescaped `$`
    /// reaches the same result here, because a backtick body has no outer
    /// expansion pass for the backslash to protect against.
    check(lush, "732 an escaped dollar becomes a dollar",
          "v=zz; echo \"[`printf %s \\$v`]\"", "[zz]");
    check(lush, "732 an escaped backslash collapses to one",
          "echo \"[`printf %s \\\\\\\\`]\"", "[\\]");
    check(lush, "732 an unrelated escape is left alone",
          "echo \"[`printf 'a\\tb'`]\"", "[a\tb]");

    /// A single level, the `$( )` spelling, and an empty body must be
    /// unchanged -- the scanner change touches every unquoted backtick.
    check(lush, "a single-level backtick is unchanged", "echo \"[`printf 4`]\"",
          "[4]");
    check(lush, "a single-level backtick unquoted is unchanged",
          "echo `printf 4`", "4");
    check(lush, "the $( ) spelling is unchanged",
          "echo \"[$(echo $(printf 4))]\"", "[4]");
    check(lush, "an empty backtick body is unchanged", "echo \"[`:`]\"", "[]");
    check(lush, "a backtick with a pipeline is unchanged",
          "echo \"[`printf 7 | cat`]\"", "[7]");
    check(lush, "a backtick containing a semicolon is unchanged",
          "echo \"[`printf 1; printf 2`]\"", "[12]");
    check(lush, "a trailing literal after a backtick is unchanged",
          "echo `printf 4`y", "4y");

    /// An unterminated backtick must still be DIAGNOSED rather than silently
    /// consuming the rest of the input -- the scanner now skips an escaped
    /// byte, so it must not be able to skip past the end. Asserted by shape,
    /// since the exact diagnostic text is not this test's contract.
    check_contains(lush, "an unterminated backtick is still an error",
                   "echo `printf 4", "unterminated backtick");
    check_contains(lush, "a trailing escape does not run off the end",
                   "echo `printf 4\\", "unterminated backtick");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
