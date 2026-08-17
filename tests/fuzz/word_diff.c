/**
 * @file word_diff.c
 * @brief Four-way differential harness for the Word CST evaluator (Step 1c).
 *
 * For each word in a corpus, compares the expanded argv fields from:
 *   - wordtool          (the candidate: parse_word + word_eval)
 *   - current lush      (the parity oracle)
 *   - bash, zsh         (triage references only)
 *
 * The parity bar is wordtool == current-lush, byte-for-byte -- lush's curations
 * (e.g. word-split default off) are the spec, so bash/zsh are never a pass/fail
 * input, only triage context. A word that wordtool reports not-yet-covered
 * (exit 2) is bucketed and skipped, never counted as a bug.
 *
 * Each shell emits the count-prefixed field format via
 *   set -- WORD; printf '%d\n' "$#"; printf '%s\n' "$@"
 * so a null-word-removed word (0 fields) is distinguished from a quoted empty
 * (1 empty field) without relying on NUL emission. wordtool prints the same
 * shape. Variables the corpus references are preset in the environment, shared
 * by all four children.
 *
 * usage: word_diff <corpus> <wordtool> <lush>   (bash/zsh resolved from PATH;
 *        override with WORD_DIFF_BASH / WORD_DIFF_ZSH)
 * Exits non-zero iff any covered word diverged (wordtool != current-lush).
 */
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CAP 65536

/// Signals a captured child output that filled the buffer (comparing truncated
/// output could report a false match), distinct from a normal exit code.
#define CAPTURE_OVERFLOW (-2)

/// Capture a child's stdout (stderr to /dev/null) into @p out (NUL-terminated),
/// draining the pipe fully so it never fills-and-blocks the child. Sets *outlen
/// to the byte count. Returns the exit code, -1 on spawn/wait failure, or
/// CAPTURE_OVERFLOW if the child produced >= CAP-1 bytes (output too large to
/// compare reliably).
static int capture(char *const argv[], char *out, size_t *outlen) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t total = 0;
    bool overflow = false;
    ssize_t r;
    /// Always drain the pipe to EOF (even past CAP) so the child never blocks
    /// on a full pipe; bytes past CAP-1 are dropped and flagged as overflow.
    char sink[4096];
    while ((r = total < CAP - 1 ? read(pipefd[0], out + total, CAP - 1 - total)
                                : read(pipefd[0], sink, sizeof sink)) > 0) {
        if (total < CAP - 1) {
            total += (size_t)r;
        } else {
            overflow = true;
        }
    }
    if (total >= CAP - 1) {
        overflow = true;
    }
    out[total] = '\0';
    *outlen = total;
    close(pipefd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (overflow) {
        return CAPTURE_OVERFLOW;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/// Build the count-prefixed field emitter for a shell. A `for` loop over "$@"
/// (not `printf '%s\n' "$@"`) is used to print the fields: with zero fields it
/// runs zero times, whereas printf with no operands would run once and emit a
/// spurious empty line -- conflating a 0-field word with a 1-empty-field word.
/// Returns false if the command did not fit (truncation would make lush
/// evaluate a different command than the word).
static bool loopcmd(const char *word, char *cmd, size_t cap) {
    int n = snprintf(cmd, cap,
                     "set -- %s; printf '%%d\\n' \"$#\"; "
                     "for __a in \"$@\"; do printf '%%s\\n' \"$__a\"; done",
                     word);
    return n > 0 && (size_t)n < cap;
}

/// True if `bin` runs at all (resolves + exec's), so a missing oracle is
/// skipped rather than failing the run.
static bool shell_available(const char *bin) {
    char out[CAP];
    size_t n = 0;
    char *const probe[] = {(char *)bin, "-c", "exit 0", NULL};
    return capture(probe, out, &n) == 0;
}

/// True if a corpus line is only white space (skipped, not a word).
static bool all_blank(const char *s) {
    for (; *s; s++) {
        if (*s != ' ' && *s != '\t') {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <corpus> <wordtool> <lush>\n", argv[0]);
        return 2;
    }
    const char *corpus_path = argv[1];
    const char *wordtool = argv[2];
    const char *lush = argv[3];
    const char *bash = getenv("WORD_DIFF_BASH");
    const char *zsh = getenv("WORD_DIFF_ZSH");
    if (!bash) {
        bash = "bash";
    }
    if (!zsh) {
        zsh = "zsh";
    }

    /// Deterministic environment shared by all four children.
    setenv("X", "a b c", 1);
    setenv("Y", "cd", 1);
    setenv("E", "", 1);
    setenv("U", "caf\xc3\xa9",
           1);       /// multi-byte, for the grapheme-aware operators
    unsetenv("IFS"); /// default IFS
    /// Pin the reference lush to the LEGACY expander so a covered line is
    /// compared CST-vs-legacy rather than CST-vs-CST (see the lu_argv comment).
    /// wordtool ignores this variable -- it IS the CST -- so one setenv here
    /// covers both children.
    setenv("LUSH_WORD_CST", "0", 1);

    bool have_bash = shell_available(bash);
    bool have_zsh = shell_available(zsh);

    FILE *f = fopen(corpus_path, "r");
    if (!f) {
        fprintf(stderr, "word_diff: cannot open corpus %s\n", corpus_path);
        return 2;
    }

    int covered = 0, not_covered = 0, bugs = 0, curation = 0, skipped = 0,
        inconclusive = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#' || all_blank(line)) {
            continue; /// blank / comment lines are not words
        }

        char cmd[4096];
        if (!loopcmd(line, cmd, sizeof cmd)) {
            inconclusive++;
            printf("INCONCLUSIVE %s (word too long for oracle command)\n",
                   line);
            continue;
        }
        char wt[CAP], lu[CAP];
        size_t wtlen = 0, lulen = 0;

        char *const wt_argv[] = {(char *)wordtool, line, NULL};
        int wt_rc = capture(wt_argv, wt, &wtlen);
        if (wt_rc == CAPTURE_OVERFLOW) {
            inconclusive++;
            printf("INCONCLUSIVE %s (wordtool output too large)\n", line);
            continue;
        }
        if (wt_rc == 2) {
            not_covered++;
            printf("NOT-COVERED  %s\n", line);
            continue;
        }
        if (wt_rc == 1) {
            /// wordtool exit 1 = "not an argument word at all" (an operator /
            /// non-word corpus line). Skip -- a corpus-hygiene issue, not a
            /// candidate defect. A crash surfaces as rc < 0 below.
            skipped++;
            printf("SKIP         %s (not a word)\n", line);
            continue;
        }
        if (wt_rc != 0) {
            bugs++;
            printf("WORDTOOL-CRASH %s (rc=%d)\n", line, wt_rc);
            continue;
        }

        /// `--lush` pins the reference shell to lush mode. wordtool has no
        /// config surface -- it runs on shell_mode.c's static default (lush) --
        /// while lush resolves its mode from lushrc/CLI, so a developer whose
        /// config selects another mode would otherwise see false BUG verdicts
        /// on every mode-gated form (the extglob dialects reach the shared
        /// operator core through lush_shell_pattern_match).
        ///
        /// The reference is pinned to the LEGACY expander (LUSH_WORD_CST=0,
        /// exported in main). Since #667 made the Word CST the default route,
        /// a covered line would otherwise be expanded by the CST on BOTH
        /// sides -- wordtool compared against itself -- and the stated parity
        /// bar ("wordtool == current lush") would hold vacuously. Pinning the
        /// reference restores the bar this harness exists to enforce.
        char *const lu_argv[] = {(char *)lush, "--lush", "-c", cmd, NULL};
        int lu_rc = capture(lu_argv, lu, &lulen);
        if (lu_rc == CAPTURE_OVERFLOW) {
            inconclusive++;
            printf("INCONCLUSIVE %s (lush output too large)\n", line);
            continue;
        }
        if (lu_rc < 0) {
            fprintf(stderr, "word_diff: lush failed on %s\n", line);
            return 2;
        }

        covered++;
        /// Length-checked memcmp, not strcmp: a field could in principle carry
        /// an embedded NUL (not in the current corpus, but do not assume it).
        if (wtlen != lulen || memcmp(wt, lu, wtlen) != 0) {
            bugs++;
            printf("BUG          %s\n  wordtool: %s  lush: %s\n", line, wt, lu);
            continue;
        }

        /// Triage bash/zsh (references only; never a pass/fail input).
        if (have_bash) {
            char bb[CAP];
            size_t bl = 0;
            char *const b_argv[] = {(char *)bash, "-c", cmd, NULL};
            if (capture(b_argv, bb, &bl) == 0 &&
                (bl != wtlen || memcmp(wt, bb, wtlen) != 0)) {
                curation++;
            }
        }
        if (have_zsh) {
            char zz[CAP];
            size_t zl = 0;
            char *const z_argv[] = {(char *)zsh, "-c", cmd, NULL};
            if (capture(z_argv, zz, &zl) == 0 &&
                (zl != wtlen || memcmp(wt, zz, wtlen) != 0)) {
                curation++;
            }
        }
    }
    fclose(f);

    printf("\n=== word_diff: %d covered (parity OK), %d not-yet-covered, "
           "%d skipped, %d inconclusive, %d curation-diffs vs bash/zsh, "
           "%d bugs ===\n",
           covered, not_covered, skipped, inconclusive, curation, bugs);
    return bugs ? 1 : 0;
}
