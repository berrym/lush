/**
 * @file test_case_pattern_substitution.c
 * @brief A case pattern may be produced by a substitution.
 *
 * A case pattern is a word, so a substitution is a legal part of one:
 * `case $x in $(cmd))`, the backtick spelling, and `$((...))` all name a
 * pattern to match against.
 *
 * The case-pattern parser carries a hand-written list of accepted token
 * types. It included TOK_VARIABLE -- so `$var` and `${v}` worked -- but none
 * of TOK_COMMAND_SUB, TOK_BACKQUOTE or TOK_ARITH_EXP, so every substitution
 * form failed to parse at all:
 *
 *     case a in $(printf a)) echo match;; esac
 *       error[E1001]: expected pattern in case statement
 *
 * The two canonical predicates next door in tokenizer.c
 * (token_is_argument_word_token, token_is_assignment_value_token) both already
 * listed all three; only this site missed them (issue #744).
 *
 * This is NOT the #494 shape. That issue was about a `)` inside a
 * substitution being mistaken for the pattern terminator, and that work is
 * intact -- once the tokens are accepted, such a `)` correctly does not end
 * the pattern. Those cases are pinned below so the two cannot be confused
 * again.
 *
 * Usage: test_case_pattern_substitution <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_case_pattern_substitution"

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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// All three substitution spellings name a pattern.
    check(lush, "744 $( ) as a whole pattern",
          "case a in $(printf a)) echo match;; *) echo no;; esac", "match");
    check(lush, "744 backtick as a whole pattern",
          "case a in `printf a`) echo match;; *) echo no;; esac", "match");
    check(lush, "744 arithmetic as a whole pattern",
          "case 3 in $((1+2))) echo match;; *) echo no;; esac", "match");

    /// ... and as PART of one, on either side.
    check(lush, "744 $( ) between literals",
          "case x4y in x$(printf 4)y) echo match;; *) echo no;; esac", "match");
    check(lush, "744 a backtick between literals",
          "case x4y in x`printf 4`y) echo match;; *) echo no;; esac", "match");
    check(lush, "744 a substitution with a trailing literal",
          "case ab in $(printf 'a')b) echo match;; *) echo no;; esac", "match");

    /// Alternation and nesting.
    check(lush, "744 a substitution in an alternation",
          "case b in a|`printf b`) echo match;; *) echo no;; esac", "match");
    check(lush, "744 two substitutions in an alternation",
          "case a in $(printf x)|$(printf a)) echo match;; *) echo no;; esac",
          "match");
    check(lush, "744 a nested substitution",
          "case a in $(echo $(printf a))) echo match;; *) echo no;; esac",
          "match");

    /// A pattern that does not match must still fall through, so the fix is
    /// not "accept and always match".
    check(lush, "744 a non-matching substitution pattern falls through",
          "case zz in $(printf a)) echo match;; *) echo no;; esac", "no");

    /// Glob metacharacters still apply to the produced pattern.
    check(lush, "744 a substitution followed by a glob meta",
          "case abc in $(printf 'a')*) echo match;; *) echo no;; esac",
          "match");

    /// The #494 boundary: a `)` INSIDE the substitution is not the pattern
    /// terminator. This is the case that made the defect look like a scanner
    /// problem; it was not, and it must stay correct.
    check(lush, "494 a ) inside $( ) does not end the pattern",
          "case \")\" in $(printf ')')) echo match;; *) echo no;; esac",
          "match");
    check(lush, "494 a ) inside a backtick does not end the pattern",
          "case \")\" in `printf ')'`) echo match;; *) echo no;; esac",
          "match");

    /// Forms that already worked must be untouched -- the accepted-token list
    /// governs every case pattern.
    check(lush, "a literal pattern is unchanged",
          "case a in a) echo match;; *) echo no;; esac", "match");
    check(lush, "a parenthesized pattern is unchanged",
          "case a in (a) echo match;; *) echo no;; esac", "match");
    check(lush, "an alternation is unchanged",
          "case a in a|b) echo match;; *) echo no;; esac", "match");
    check(lush, "a bracket class is unchanged",
          "case a in [ab]) echo match;; *) echo no;; esac", "match");
    check(lush, "a star pattern is unchanged", "case a in *) echo star;; esac",
          "star");
    check(lush, "a variable pattern is unchanged",
          "p=a; case a in $p) echo match;; *) echo no;; esac", "match");
    check(lush, "a braced variable pattern is unchanged",
          "p=a; case a in ${p}) echo match;; *) echo no;; esac", "match");
    check(lush, "a quoted literal ) is unchanged",
          "case \"x)\" in \"x)\") echo match;; *) echo no;; esac", "match");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
