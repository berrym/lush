/**
 * @file test_arith_boundaries.c
 * @brief Where an arithmetic expression begins, ends, and what may follow it.
 *
 * Four defects clustered on the same question -- where does an arithmetic
 * construct stop? -- and each answered it by looking at too few characters:
 *
 *   #613/#646  the `$(( ))` wrapper was detected by "starts with $((" plus
 *              "ends with ))" without checking the two were the SAME pair.
 *              `$((a)) + $((b))` satisfied both on different parens and was
 *              gutted to `a)) + $((b`; `$((1))+1` satisfied only the first and
 *              was declared malformed, which the ${arr[...]} read path
 *              swallowed into an empty result.
 *   #608       the tokenizer fused ANY `))` inside a (( )) command, so the
 *              inner group's close in `(( (3-(2)) ))` consumed the command
 *              terminator. `+(`/`*(` escaped only by lexing as extglob.
 *   #617       a redirection after `))` was rejected outright, making the
 *              arithmetic command the only compound construct that could not
 *              take one.
 * Also pins #596 (command substitution inside `$(( ))`) and #597 (adjacent
 * nested expansions), which the AST-engine cutover fixed but left unguarded.
 *
 * Usage: test_arith_boundaries <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_arith_boundaries"

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

    /// -------------------------------------------------- #613: same-pair test
    /// Both operands are wrapped expansions, so the naive strip removed the
    /// leading `$((` and the trailing `))` of DIFFERENT pairs.
    check(lush, "613 (( )) command with two wrapped operands",
          "a=1; b=2; (( $((a)) + $((b)) )); echo rc=$?", "rc=0", "error");
    check(lush, "613 same shape as an expansion",
          "a=1; b=2; echo R=$(( $((a)) + $((b)) ))", "R=3", "error");
    check(lush, "613 single wrapped operand still evaluates",
          "a=1; (( $((a)) )); echo rc=$?", "rc=0", "error");

    /// ------------------------------------------ #646: leading nested wrapper
    /// A subscript that BEGINS with an expansion and continues with more
    /// arithmetic. The old code called this malformed and the read path turned
    /// the failure into an empty string.
    check(lush, "646 subscript with trailing arithmetic",
          "n=(10 20 30); echo E=${n[$((1))+1]}", "E=30", NULL);
    check(lush, "646 subscript trailing arithmetic, zero offset",
          "n=(10 20 30); echo E=${n[$((0))+2]}", "E=30", NULL);
    check(lush, "646 expansion last in subscript still works",
          "n=(10 20 30); echo E=${n[1+$((1))]}", "E=30", NULL);
    check(lush, "646 whole subscript is an expansion",
          "n=(10 20 30); echo E=${n[$((1))]}", "E=20", NULL);

    /// ------------------------------------------------- #608: group vs closer
    /// The `))` after the 2 closes the inner and outer groups; it is not the
    /// command terminator.
    check(lush, "608 minus before an abutting nested group",
          "(( (3-(2)) )); echo rc=$?", "rc=0", "error");
    check(lush, "608 same shape with a trailing operator",
          "(( (3-(2))*1 )); echo rc=$?", "rc=0", "error");
    check(lush, "608 nested (( )) still balances", "(( ((1+2)) )); echo rc=$?",
          "rc=0", "error");
    /// The comment on the tokenizer's fusion rule cited this construct as the
    /// reason it exists; it must keep working.
    check(lush, "608 nested process substitution still tokenizes",
          "cat <(cat <(echo nested))", "nested", "error");

    /// The paren depth the terminator test consults is position-dependent
    /// state, and the tokenizer rewinds its position to re-read a token. If
    /// the depth does not rewind with it, the same text is counted twice and a
    /// LATER, entirely well-formed (( )) can no longer find its own `))`.
    /// Both shapes below passed before the depth gate existed and must keep
    /// passing with it: here-document bodies are tokenized as shell text and
    /// then skipped, and the parser speculatively reads past `( )` and
    /// restores the position.
    check(lush, "608 here-doc bodies do not poison a later (( ))",
          "f() {\n"
          "  cat <<E1\n((\nE1\n"
          "  cat <<E2\n(\nE2\n"
          "  (( 1+1 )); echo ARITH_OK=$?\n"
          "}\nf",
          "ARITH_OK=0", "error");
    check(lush, "608 speculative backtrack does not poison a later (( ))",
          "(( ((1) ) )); ( ) ; ( echo x ); (( 1+1 )); echo ARITH_OK=$?",
          "ARITH_OK=0", "error");

    /// ------------------------------------------- #617: trailing redirections
    check(lush, "617 stdout redirection on the command",
          "(( 1+1 )) >/dev/null; echo rc=$?", "rc=0", "error");
    check(lush, "617 stderr redirection suppresses the diagnostic",
          "(( 1/0 )) 2>/dev/null; echo rc=$?", "rc=1", "error");
    /// The exit status still reflects the VALUE, not the redirection.
    check(lush, "617 false value keeps exit status 1",
          "(( 0 )) >/dev/null; echo rc=$?", "rc=1", NULL);
    /// The side effect still happens, and the descriptor is restored after.
    check(lush, "617 side effect survives and stdout is restored",
          "i=0; (( i++ )) >/dev/null; echo i=$i", "i=1", NULL);

    /// ------------------------------------- #596/#597: pinned, fixed by #612
    check(lush, "596 command substitution inside arithmetic",
          "echo R=$(( $(printf 7)+1 ))", "R=8", NULL);
    check(lush, "597 adjacent nested expansions concatenate",
          "echo R=$(( $((1))$((2)) ))", "R=12", NULL);

    /// ------------------------------------------------------------ guardrails
    check(lush, "a genuine unterminated wrapper is still diagnosed",
          "echo $((1+2", "error", NULL);
    /// The wrapper test consults a SHELL-structure scanner, which honors `#`
    /// comments and case patterns. Arithmetic has neither, so that scanner's
    /// refusal is not evidence of a missing `))` -- the arithmetic lexer is
    /// the one that can name the real defect, and must be the one to speak.
    check(lush, "an unterminated quote is named, not blamed on ))",
          "echo $(( 'a ))", "unexpected character", "needs a matching");
    check(lush, "a # inside arithmetic is named, not blamed on ))",
          "echo $(( 1 # 2 ))", "unexpected character '#'", "needs a matching");
    /// ... and identically whether or not a space precedes it.
    check(lush, "the # diagnosis does not depend on spacing",
          "echo $(( 16 #ff ))", "unexpected character '#'", "needs a matching");
    check(lush, "plain nesting still evaluates", "echo R=$(( $((2)) ))", "R=2",
          NULL);
    check(lush, "an array read inside arithmetic still works",
          "n=(10 20 30); echo R=$(( n[1] ))", "R=20", NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
