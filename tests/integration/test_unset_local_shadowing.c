/**
 * @file test_unset_local_shadowing.c
 * @brief `unset` of a local leaves the name unset, and the export survives.
 *
 * A tombstone records that a name is unset in the scope that owns it, and
 * that scope pops on return -- which is exactly when the enclosing binding
 * should reappear. The scope-chain walk used to SKIP tombstones and keep
 * searching outward, so the enclosing binding was revealed immediately:
 *
 *     x=global; f() { local x=lcl; unset x; echo "[$x]"; }   ->  [global]
 *
 * Reads were not the only casualty. A write in the frame reached through the
 * tombstone too, so `local -a a=(1 2); unset a; a+=(z)` appended to the
 * CALLER's array and corrupted it (issue #623).
 *
 * Two behaviors are deliberately distinguished:
 *
 *   1. `unset` of a binding owned by the CURRENT frame. bash, zsh and dash
 *      all keep the name unset for the rest of that frame -- consensus, so
 *      it holds in every lush mode.
 *   2. `unset` run in a frame DEEPER than the binding it targets. bash
 *      removes the binding and reveals the next one outward; zsh and dash
 *      keep it unset. bash is the outlier, so lush curates the zsh/dash
 *      reading and reproduces bash's under FEATURE_UNSET_REVEALS_OUTER
 *      (`mode bash`).
 *
 * The exported-variable mirror is a third axis. The process environ is
 * process-level and not governed by the scope model (SEMANTICS 5.5), and
 * unsetenv is permanent while a tombstone is not, so the mirror resolves
 * through tombstones: a local `unset` must never destroy the global's export.
 *
 * Usage: test_unset_local_shadowing <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_unset_local_shadowing"

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

/// Whole-output comparison: a substring match would accept "[global]" for a
/// test that wanted "[]".
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

/// The consensus case must hold identically in every mode.
static void check_all_modes(const char *lush, const char *label,
                            const char *script, const char *want) {
    static const char *modes[] = {"lush", "bash", "zsh", "posix"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        char full[2048];
        char lbl[256];
        snprintf(full, sizeof(full), "mode %s; %s", modes[i], script);
        snprintf(lbl, sizeof(lbl), "%s (mode %s)", label, modes[i]);
        check(lush, lbl, full, want);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];

    /// 1. Unset of a binding owned by the current frame: all three peers
    ///    agree the name is unset, so every mode agrees too.
    check_all_modes(lush, "623 a local unset stays unset",
                    "x=global; f(){ local x=lcl; unset x; echo \"[$x]\"; }; f",
                    "[]");
    check_all_modes(lush, "623 unset is distinguishable from empty",
                    "x=g; f(){ local x=l; unset x; echo \"[${x-U}]\"; }; f",
                    "[U]");
    check_all_modes(lush, "623 the outer binding returns when the frame pops",
                    "x=g; f(){ local x=l; unset x; }; f; echo \"[$x]\"", "[g]");
    check_all_modes(lush, "623 a callee sees it unset too (dynamic scope)",
                    "x=g; f(){ local x=l; unset x; h; }; h(){ echo "
                    "\"[${x-U}]\"; }; f",
                    "[U]");

    /// 2. Arrays: the count, and a write that must not reach the caller.
    check_all_modes(lush, "623 an unset local array is empty",
                    "a=(x y z); f(){ local -a a=(1 2); unset a; echo "
                    "\"[${#a[@]}]\"; }; f",
                    "[0]");
    check_all_modes(lush, "623 a write after unset stays in the frame",
                    "a=(x y z); f(){ local -a a=(1 2); unset a; a+=(z); echo "
                    "\"[${a[*]}]\"; }; f; echo \"[${a[*]}]\"",
                    "[z]\n[x y z]");
    check_all_modes(lush, "623 a scalar write after unset stays in the frame",
                    "x=g; f(){ local x=l; unset x; x=n; echo \"[$x]\"; }; f; "
                    "echo \"[$x]\"",
                    "[n]\n[g]");

    /// 3. The deeper-frame case: curated default, and mode bash's baseline.
    check(
        lush, "623 deeper-frame unset shadows (curated default)",
        "x=g; g(){ unset x; echo \"[${x-U}]\"; }; f(){ local x=outer; g; }; f",
        "[U]");
    check(lush, "623 deeper-frame unset shadows (mode zsh)",
          "mode zsh; x=g; g(){ unset x; echo \"[${x-U}]\"; }; f(){ local "
          "x=outer; g; }; f",
          "[U]");
    check(lush, "623 deeper-frame unset shadows (mode posix)",
          "mode posix; x=g; g(){ unset x; echo \"[${x-U}]\"; }; f(){ local "
          "x=outer; g; }; f",
          "[U]");
    check(lush, "623 mode bash reveals the next binding outward",
          "mode bash; x=g; g(){ unset x; echo \"[${x-U}]\"; }; f(){ local "
          "x=outer; g; }; f",
          "[g]");
    check(lush, "623 mode bash reveals it to the owning frame as well",
          "mode bash; x=g; g(){ unset x; }; f(){ local x=outer; g; echo "
          "\"[${x-U}]\"; }; f",
          "[g]");
    /// The gate is reachable from any mode, in either dialect's spelling.
    check(lush, "623 the curated default is opt-out-able",
          "setopt unset_reveals_outer; x=g; g(){ unset x; echo "
          "\"[${x-U}]\"; }; f(){ local x=outer; g; }; f",
          "[g]");
    check(lush, "623 mode bash's default is opt-out-able",
          "mode bash; unsetopt unset_reveals_outer; x=g; g(){ unset x; echo "
          "\"[${x-U}]\"; }; f(){ local x=outer; g; }; f",
          "[U]");

    /// 4. The export axis is process-level and must survive the frame.
    check_all_modes(lush, "623 a local unset does not destroy the export",
                    "export x=g; f(){ local x=l; unset x; }; f; sh -c 'echo "
                    "\"[$x]\"'",
                    "[g]");
    check_all_modes(lush, "623 the shell binding survives too",
                    "export x=g; f(){ local x=l; unset x; }; f; echo \"[$x]\"",
                    "[g]");
    check_all_modes(lush, "623 unsetting the exported global does remove it",
                    "export x=g; f(){ unset x; }; f; sh -c 'echo \"[$x]\"'",
                    "[]");
    check_all_modes(lush, "623 a top-level unset removes the export",
                    "export x=g; unset x; sh -c 'echo \"[$x]\"'", "[]");

    /// 5. Behavior that must not regress.
    check_all_modes(lush, "repeated unset does not destroy the outer binding",
                    "x=g; f(){ local x=l; unset x; unset x; }; f; echo "
                    "\"[$x]\"",
                    "[g]");
    check_all_modes(lush, "unset of a global from a function still unsets it",
                    "x=g; f(){ unset x; }; f; echo \"[${x-U}]\"", "[U]");
    check_all_modes(lush, "a local shadow without unset is visible to a callee",
                    "x=g; f(){ local x=l; h; }; h(){ echo \"[${x-U}]\"; }; f",
                    "[l]");
    check_all_modes(lush, "unset of an unbound name succeeds",
                    "unset NOPE; echo \"[$?]\"", "[0]");
    check_all_modes(lush, "re-declaring after unset rebinds locally",
                    "x=g; f(){ local x=l; unset x; local x=n; echo "
                    "\"[$x]\"; }; f; echo \"[$x]\"",
                    "[n]\n[g]");
    check_all_modes(lush, "a plain local still shadows",
                    "x=g; f(){ local x=l; echo \"[$x]\"; }; f; echo \"[$x]\"",
                    "[l]\n[g]");

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
