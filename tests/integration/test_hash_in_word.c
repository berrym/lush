/**
 * @file test_hash_in_word.c
 * @brief An unquoted mid-word '#' is a literal word by default (#448).
 *
 * `echo abc#def` printed nothing in the default mode: the word was one token,
 * but extended_glob (on) read `c#` as a zsh zero-or-more quantifier and
 * nullglob (on) then dropped the no-match to empty -- matching neither bash
 * (literal) nor zsh's own default (extended_glob is off there, so `#` is
 * literal too). The zsh bare glob operators (X#/X## quantifiers, leading ^
 * negation) now sit behind an opt-in feature (FEATURE_ZSH_EXTENDED_GLOB /
 * `setopt zshextglob`), off by default, so a mid-word `#` stays a literal word.
 * This checks both the default-literal behavior and that the opt-in still
 * restores the working quantifier and negation operators.
 *
 * Usage: test_hash_in_word <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_hash_in_word"

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

    /// A directory with files whose names the zsh `c#` quantifier would match
    /// (abdef, abcdef, abccdef), used to prove the opt-in operator still works.
    char dir[] = "/tmp/lush_hash_XXXXXX";
    if (!mkdtemp(dir)) {
        fprintf(stderr, "FAIL %s: mkdtemp failed\n", TEST);
        return 2;
    }
    const char *names[] = {"abdef", "abcdef", "abccdef", "other"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        int fd = open(path, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
        }
    }
    char script[512];

    /// Default mode: a mid-word # is a literal word, not a glob quantifier.
    /// Run inside the populated dir so a stray quantifier expansion (the bug)
    /// would be visible as the matched filenames instead of the literal.
    snprintf(script, sizeof(script), "cd %s && echo abc#def", dir);
    check(lush, "mid-word # is literal", script, "abc#def", "abccdef");

    /// The surrounding words survive; only the # word was dropped by the bug.
    snprintf(script, sizeof(script), "cd %s && echo XX abc#def YY", dir);
    check(lush, "neighbors survive", script, "XX abc#def YY", NULL);

    /// A leading ^ is literal by default, not a zsh negation glob.
    snprintf(script, sizeof(script), "cd %s && echo ^abdef", dir);
    check(lush, "leading ^ is literal", script, "^abdef", "abccdef");

    /// setopt zshextglob restores the working zsh zero-or-more quantifier:
    /// abc#def now matches abdef, abcdef, abccdef.
    snprintf(script, sizeof(script), "setopt zshextglob\ncd %s && echo abc#def",
             dir);
    check(lush, "opt-in restores # quantifier", script, "abcdef", "abc#def");

    /// setopt zshextglob restores ^ negation: ^abdef matches all but abdef.
    snprintf(script, sizeof(script), "setopt zshextglob\ncd %s && echo ^abdef",
             dir);
    check(lush, "opt-in restores ^ negation", script, "abcdef", "^abdef");

    /// setopt zshextglob restores the ## one-or-more quantifier too: abc##def
    /// requires at least one c, so it matches abcdef and abccdef but NOT abdef.
    snprintf(script, sizeof(script),
             "setopt zshextglob\ncd %s && echo abc##def", dir);
    check(lush, "opt-in restores ## one-or-more", script, "abccdef",
          "abc##def");
    /// Default: abc##def is a literal word (both # are literal).
    snprintf(script, sizeof(script), "cd %s && echo abc##def", dir);
    check(lush, "## is literal by default", script, "abc##def", "abccdef");

    /// Control: a # after whitespace is still a real comment.
    check(lush, "space then # is a comment", "echo abc #def", "abc", "def");

    /// Control: quotes still protect a literal #.
    check(lush, "quotes protect #", "echo 'abc#def'", "abc#def", NULL);

    /// Control: an assignment RHS keeps a literal # (never a glob site).
    check(lush, "assignment RHS keeps #", "x=abc#def; echo \"[$x]\"",
          "[abc#def]", NULL);

    /// Control: structured @(...) extglob is unaffected (still on by default).
    /// absent="@(" so a broken extglob that emitted the literal pattern (which
    /// also contains "abdef") is caught rather than passing on the substring.
    snprintf(script, sizeof(script), "cd %s && echo @(abdef|other)", dir);
    check(lush, "structured extglob still works", script, "abdef", "@(");

    /// The (a|b) bare alternation stays on by default: a mid-word group with
    /// trailing text still globs. a(bd|bcd)ef matches abdef and abcdef.
    snprintf(script, sizeof(script), "cd %s && echo a(bd|bcd)ef", dir);
    check(lush, "bare alternation still globs", script, "abdef", NULL);

    /// A bare # must NOT ride in on that always-on alternation route: a pattern
    /// reaches the zsh matcher whenever it carries an alternation, so with the
    /// opt-in off the # in a(bd|bcd)e#f stays literal and nothing matches (no
    /// file has a literal '#'); with the opt-in on the e# quantifier matches
    /// abdef and abcdef again. This exercises the matcher-flag half of the
    /// gate.
    snprintf(script, sizeof(script), "cd %s && echo a(bd|bcd)e#f", dir);
    check(lush, "bare # does not ride alternation route", script, NULL,
          "abdef");
    snprintf(script, sizeof(script),
             "setopt zshextglob\ncd %s && echo a(bd|bcd)e#f", dir);
    check(lush, "opt-in restores # on alternation route", script, "abdef",
          NULL);

    /// Cleanup.
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
