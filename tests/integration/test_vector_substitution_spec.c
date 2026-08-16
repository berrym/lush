/**
 * @file test_vector_substitution_spec.c
 * @brief The element and scalar substitution paths split the spec identically.
 *
 * `${v/p/r}` and `${arr[@]/p/r}` take the same `pattern/replacement` spec, so
 * they must split it the same way. The per-element dispatch carried its own
 * copy of that split, and the copy had drifted: its no-separator branch
 * skipped the `\/` -> `/` canonicalization, so
 *
 *     v=a/b/c; arr=(a/b/c)
 *     ${v//\/}        -> abc
 *     ${arr[@]//\/}   -> a/b/c     (the slashes survived)
 *
 * The element path now calls lush_param_op_split_substitution_spec, the same
 * function the scalar path uses, so the two cannot diverge again (issue #684).
 *
 * These checks compare the scalar and element results against EACH OTHER as
 * well as against a literal, because the contract is that they agree -- a
 * future change that breaks both in the same way should still fail here.
 *
 * NOTE: `"${arr[@]}"` with an operator currently yields ONE field where bash
 * and zsh yield N (issue #749, pre-existing and separate -- the element values
 * are right, the field boundary is lost). These tests therefore compare
 * CONTENT, and the field count is pinned in that issue rather than here.
 *
 * Usage: test_vector_substitution_spec <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_vector_substitution_spec"

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

/// `spec` is the operator text after the name, e.g. "//\\/" -- applied to a
/// scalar and to a one-element array holding the same value. The two results
/// are printed as <scalar><element> and must be identical AND equal to `want`.
static void check_agree(const char *lush, const char *label, const char *value,
                        const char *spec, const char *want) {
    char script[1024];
    char out[4096];
    char expect[512];
    /// The value is SINGLE-QUOTED on both sides. Unquoted, a value with a
    /// space became a command prefix (`v=a b`) and a value with a glob
    /// metacharacter was expanded inside the array literal -- which, with
    /// null_glob on in lush mode, left an EMPTY array and made the element
    /// side compare against nothing. Both looked like code defects and were
    /// purely the harness.
    snprintf(script, sizeof(script),
             "v='%s'; arr=('%s'); printf '<%%s><%%s>' \"${v%s}\" "
             "\"${arr[0]%s}\"",
             value, value, spec, spec);
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    snprintf(expect, sizeof(expect), "<%s><%s>", want, want);
    if (strcmp(out, expect) != 0) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\", got \"%.200s\"\n", TEST,
                label, expect, out);
        failures++;
        return;
    }
    fprintf(stderr, "ok   %s [%s]\n", TEST, label);
}

static void check(const char *lush, const char *label, const char *script,
                  const char *want) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
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

    /// THE DISCRIMINATING CASE: a `\/` pattern with NO separator (a delete).
    /// This is the branch the element copy had drifted on.
    check_agree(lush, "684 escaped slash, delete", "a/b/c", "//\\/", "abc");
    check_agree(lush, "684 escaped slash, replace-first", "a/b/c", "/\\//-",
                "a-b/c");
    check_agree(lush, "684 escaped slash, replace-all", "a/b/c", "//\\//-",
                "a-b-c");

    /// The rest of the family, to prove the shared splitter is used
    /// everywhere and not just on the reported shape.
    check_agree(lush, "684 plain delete-all", "aXbXc", "//X", "abc");
    check_agree(lush, "684 plain replace-all", "aXbXc", "//X/-", "a-b-c");
    check_agree(lush, "684 plain replace-first", "aXbXc", "/X/-", "a-bXc");
    check_agree(lush, "684 empty replacement is a delete", "aXbXc", "//X/",
                "abc");
    check_agree(lush, "684 prefix anchor", "abc", "/#a/X", "Xbc");
    check_agree(lush, "684 suffix anchor", "abc", "/%c/X", "abX");
    check_agree(lush, "684 a bracket class pattern", "abc", "//[bc]/X", "aXX");
    check_agree(lush, "684 a glob pattern", "abc", "/b*/X", "aX");
    check_agree(lush, "684 no match leaves the value alone", "abc", "//z/X",
                "abc");

    /// The vector forms produce the same CONTENT as the scalar. Field count
    /// is issue #749 and deliberately not asserted here.
    check(lush, "684 the vector form deletes the slashes",
          "arr=(a/b/c); printf '<%s>' \"${arr[@]//\\/}\"", "<abc>");
    check(lush, "684 the joined form deletes them too",
          "arr=(a/b/c); printf '<%s>' \"${arr[*]//\\/}\"", "<abc>");
    check(lush, "684 positionals delete them too",
          "set -- a/b/c; printf '<%s>' \"${@//\\/}\"", "<abc>");
    check(lush, "684 every element is transformed",
          "arr=(a/b x/y); printf '<%s>' \"${arr[*]//\\/}\"", "<ab xy>");

    /// An escaped separator in the REPLACEMENT half: the two paths must give
    /// the same answer, which is this fix's contract. They agree on KEEPING
    /// the backslash, which is also the curated behavior -- zsh keeps it, bash
    /// drops it, and lush follows zsh. (An earlier version of this comment
    /// said bash AND zsh drop it; that was wrong, and it is what made #750
    /// look like a defect.)
    check_agree(lush, "684 an escaped slash in the replacement agrees", "aXb",
                "//X/\\/", "a\\/b");

    /// A `[*]` form joins on the first character of IFS. With an operator
    /// applied the join used a hardcoded space, so `${arr[*]#a}` and the
    /// operator-free `${arr[*]}` beside it disagreed under a custom IFS
    /// (issue #752). The unset-vs-empty distinction is load-bearing: unset
    /// IFS means a space, an EMPTY IFS means no separator at all.
    check(lush, "752 a custom IFS joins the operator result",
          "IFS=,; arr=(ab xy); printf '<%s>' \"${arr[*]#a}\"", "<b,xy>");
    check(lush, "752 case conversion joins the same way",
          "IFS=,; arr=(ab xy); printf '<%s>' \"${arr[*]^^}\"", "<AB,XY>");
    check(lush, "752 substitution joins the same way",
          "IFS=,; arr=(ab xy); printf '<%s>' \"${arr[*]//b/Z}\"", "<aZ,xy>");
    check(lush, "752 positionals join the same way",
          "IFS=,; set -- ab xy; printf '<%s>' \"${*#a}\"", "<b,xy>");
    check(lush, "752 an EMPTY IFS concatenates with no separator",
          "IFS=; arr=(ab xy); printf '<%s>' \"${arr[*]#a}\"", "<bxy>");
    check(lush, "752 an UNSET IFS joins on a space",
          "unset IFS; arr=(ab xy); printf '<%s>' \"${arr[*]#a}\"", "<b xy>");
    check(lush, "752 the operator-free form is unchanged",
          "IFS=,; arr=(ab xy); printf '<%s>' \"${arr[*]}\"", "<ab,xy>");
    check(lush, "752 a single element needs no separator",
          "IFS=,; arr=(ab); printf '<%s>' \"${arr[*]#a}\"", "<b>");
    check(lush, "752 an empty array yields an empty field",
          "IFS=,; arr=(); printf '<%s>' \"${arr[*]#a}\"", "<>");

    /// The PATTERN half is a pattern: `\X` means a literal X. This used to
    /// take a plain substring search -- `is_glob` saw no metacharacter in
    /// `\b`, so the search hunted for the two BYTES `\b` and never matched,
    /// where bash and zsh both substitute and lush's own `${v#\a}` and
    /// `case abc in a\bc)` already agreed (issue #750).
    check_agree(lush, "750 an escaped ordinary char in the pattern", "abc",
                "//\\b/-", "a-c");
    check_agree(lush, "750 escaping a different ordinary char", "aXb",
                "//\\X/-", "a-b");
    check_agree(lush, "750 replace-first with an escaped char", "abcabc",
                "/\\b/-", "a-cabc");
    check_agree(lush, "750 two escapes in one pattern", "abc", "//\\a\\b/-",
                "-c");
    check_agree(lush, "750 an escaped space", "a b", "//\\ /-", "a-b");
    check(lush, "750 anchored at the start",
          "v=abc; printf '<%s>' \"${v/#\\a/-}\"", "<-bc>");
    check(lush, "750 anchored at the end",
          "v=abc; printf '<%s>' \"${v/%\\c/-}\"", "<ab->");
    check(lush, "750 an escaped char that does not occur",
          "v=abc; printf '<%s>' \"${v//\\z/-}\"", "<abc>");

    /// An escaped METAcharacter already routed to the matcher, because the
    /// metacharacter itself satisfied the old check. Pinned so the widened
    /// condition cannot regress them.
    check_agree(lush, "750 an escaped asterisk still matches literally", "a*b",
                "//\\*/-", "a-b");
    check_agree(lush, "750 an escaped bracket", "a[b", "//\\[/-", "a-b");
    check_agree(lush, "750 an escaped question mark", "a?b", "//\\?/-", "a-b");

    /// CURATED, and deliberately NOT changed: the REPLACEMENT half is literal
    /// text. lush follows zsh; bash unescapes. No capability is lost, because
    /// the delimiter is the FIRST unescaped `/`, so a literal slash needs no
    /// escape at all. Pinned here so a future "fix" toward bash has to argue
    /// with a test rather than silently flip a curated default.
    check(lush, "750 the replacement keeps its backslash",
          "v=aXb; printf '<%s>' \"${v//X/\\a}\"", "<a\\ab>");
    check(lush, "750 a literal slash needs no escape",
          "v=aXb; printf '<%s>' \"${v//X//}\"", "<a/b>");
    check(lush, "750 a replacement may contain slashes",
          "v=aXb; printf '<%s>' \"${v//X//usr/lib}\"", "<a/usr/libb>");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
