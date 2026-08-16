/**
 * @file test_sparse_array_expansion.c
 * @brief A sparse array expands to the elements it holds.
 *
 * lush stores an indexed array packed-sparse: `elements[]` alongside a
 * parallel `indices[]`, so a subscript is NOT a position. The vector
 * expansion walked positions 0..count-1 and asked for each as a SUBSCRIPT,
 * which on a sparse array invented an empty string for every hole and never
 * reached the elements past the last position:
 *
 *     a=(x); a[9]=y   ->   <x><>      instead of   <x><y>
 *
 * The value was not merely misplaced, it was GONE, and the shell reported no
 * error. Four other surfaces already read the array correctly -- ${a[*]},
 * ${#a[@]}, ${!a[@]} and declare -p -- so the array's own count and key list
 * disagreed with what iterating it produced (issue #644).
 *
 * Usage: test_sparse_array_expansion <lush-binary-path>
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST "test_sparse_array_expansion"

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
                  const char *want, const char *absent) {
    char out[4096];
    if (!run_c(lush, script, out, sizeof(out))) {
        fprintf(stderr, "FAIL %s [%s]: harness error\n", TEST, label);
        failures++;
        return;
    }
    if (want && !strstr(out, want)) {
        fprintf(stderr, "FAIL %s [%s]: wanted \"%s\" (got: \"%.200s\")\n", TEST,
                label, want, out);
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

    /// A hole from unset: no phantom element, and nothing dropped off the end.
    check(lush, "644 a hole is not an element",
          "a=(A B C D E); unset \"a[2]\"; printf '<%s>' \"${a[@]}\"; echo",
          "<A><B><D><E>", "<>");
    /// The element past the old length must still be reached.
    check(lush, "644 a far subscript is not lost",
          "a=(x); a[9]=y; printf '<%s>' \"${a[@]}\"; echo", "<x><y>", "<>");
    check(lush, "644 several holes",
          "a=(A B C D E); unset \"a[1]\"; unset \"a[3]\"; printf '<%s>' "
          "\"${a[@]}\"; echo",
          "<A><C><E>", "<>");
    /// A for-in loop is the same expansion and must agree.
    check(lush, "644 for-in sees the same elements",
          "a=(A B C D E); unset \"a[2]\"; for e in \"${a[@]}\"; do printf "
          "'<%s>' \"$e\"; done; echo",
          "<A><B><D><E>", "<>");
    /// Arity is what a callee sees; a phantom element is an extra argument.
    check(lush, "644 the callee receives the real count",
          "a=(x); a[9]=y; c(){ printf 'n=%s' \"$#\"; }; c \"${a[@]}\"", "n=2",
          "n=3");

    /// The array's own surfaces must agree with iterating it.
    check(lush, "644 count agrees with the elements",
          "a=(x); a[9]=y; echo \"n=${#a[@]}\"", "n=2", NULL);
    check(lush, "644 keys report the real subscripts",
          "a=(x); a[9]=y; printf '<%s>' \"${!a[@]}\"; echo", "<0><9>", NULL);
    check(lush, "644 the joined form agrees",
          "a=(x); a[9]=y; echo \"[${a[*]}]\"", "[x y]", NULL);
    check(lush, "644 declare -p agrees", "a=(x); a[9]=y; declare -p a",
          "[9]=\"y\"", NULL);

    /// A slice runs over the elements the array holds.
    check(lush, "644 a slice skips the hole",
          "a=(A B C D E); unset \"a[2]\"; printf '<%s>' \"${a[@]:1:2}\"; echo",
          "<B><D>", NULL);
    check(lush, "644 a slice past the holes",
          "a=(A B C D E); unset \"a[2]\"; printf '<%s>' \"${a[@]:2}\"; echo",
          "<D><E>", NULL);

    /// Dense arrays, the empty array and maps must be untouched.
    check(lush, "dense array unchanged",
          "a=(A B C); printf '<%s>' \"${a[@]}\"; echo", "<A><B><C>", NULL);
    check(lush, "dense slice unchanged",
          "a=(A B C D); printf '<%s>' \"${a[@]:1:2}\"; echo", "<B><C>", NULL);
    check(lush, "single element unchanged",
          "a=(only); printf '<%s>' \"${a[@]}\"; echo", "<only>", NULL);
    check(lush, "empty array contributes nothing",
          "a=(); c(){ printf 'n=%s' \"$#\"; }; c \"${a[@]}\"", "n=0", NULL);
    check(lush, "associative values unchanged",
          "declare -A m; m[k1]=v1; m[k2]=v2; printf '<%s>' \"${m[@]}\"; echo",
          "<v1><v2>", NULL);
    check(lush, "positional parameters unchanged",
          "set -- p q r; printf '<%s>' \"${@}\"; echo", "<p><q><r>", NULL);

    if (failures) {
        fprintf(stderr, "%s: %d failure(s)\n", TEST, failures);
        return 1;
    }
    fprintf(stderr, "%s: all checks passed\n", TEST);
    return 0;
}
