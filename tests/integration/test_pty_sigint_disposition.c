/**
 * @file test_pty_sigint_disposition.c
 * @brief SIGINT/SIGQUIT disposition of forked children under a real terminal.
 *
 * POSIX async-list rule (#375): a command in an asynchronous list that shares
 * the shell's process group -- job control off (`set +m`) -- must ignore both
 * SIGINT and SIGQUIT, so a terminal Ctrl-C or Ctrl-\ does not kill it, while a
 * synchronous foreground command is interrupted / quit. A real controlling
 * terminal is required, with all three shell fds on the PTY (forkpty), so that
 * a control character sent while a foreground command runs (terminal in
 * cooked/ISIG mode) generates the signal for the whole foreground group -- the
 * exact path pty_spawn (stdin on a pipe) cannot exercise.
 *
 * The background command is `( echo r > rdy; sleep N && echo x > marker )`: the
 * marker is gated on the exec'd `sleep` surviving, so if the signal kills the
 * subshell or the sleep the marker is never written. The control character is
 * withheld until the subshell writes `rdy` -- proof it has forked and armed
 * SIG_IGN -- so delivery cannot race a slow subshell start under load. Sent
 * during a foreground `sleep`.
 *
 * Usage: test_pty_sigint_disposition <lush-binary-path>
 */

#include "pty_helpers.h" /// forkpty via <util.h>/<pty.h>; pty_make_tmpdir, etc.

#include <limits.h>

#define TEST "test_pty_sigint_disposition"

/// A scheduled input: write `bytes` at `at_ms` from the start of the session,
/// but never before `gate_file` (when non-NULL) exists on disk. The gate turns
/// a fixed-delay send into a readiness barrier -- the input fires only once the
/// shell has reached the state that creates the file, regardless of how long a
/// forked subshell takes to start under load.
typedef struct {
    long at_ms;
    const char *bytes;
    const char *
        gate_file; ///< absolute path; send only once it exists (NULL = ungated)
} timed_input_t;

/// Spawn lush on a fresh controlling PTY (all fds on the terminal), feed the
/// scheduled inputs while continuously draining output so the shell never
/// blocks on a full PTY buffer, run for total_ms, then kill it.
static void drive(const char *lush, const char *home, const timed_input_t *in,
                  size_t n, long total_ms) {
    int master;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);
    if (pid < 0) {
        pty_fail(TEST, "forkpty: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        setenv("HOME", home, 1);
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("ENV");
        setenv("TERM", "dumb", 1);
        execl(lush, "lush", "-i", (char *)NULL);
        _exit(127);
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    size_t idx = 0;
    char buf[4096];
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                       (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= total_ms) {
            break;
        }
        /// Inputs fire in order; a gated input that is not yet ready blocks
        /// every later input as well, so a gated entry must be last in `in`.
        while (idx < n && elapsed >= in[idx].at_ms &&
               (in[idx].gate_file == NULL ||
                access(in[idx].gate_file, F_OK) == 0)) {
            (void)!write(master, in[idx].bytes, strlen(in[idx].bytes));
            idx++;
        }
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        fd_set r;
        FD_ZERO(&r);
        FD_SET(master, &r);
        if (select(master + 1, &r, NULL, NULL, &tv) > 0) {
            if (read(master, buf, sizeof(buf)) <= 0) {
                break;
            }
        }
    }
    close(master);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

/// Returns true if a background command sharing the shell's group survived a
/// terminal control character `ctrl` (Ctrl-C = 0x03 / Ctrl-\ = 0x1c) delivered
/// during a foreground sleep -- i.e. it ignored the signal and wrote its
/// marker.
static bool bg_survives_ctrl(const char *lush, char ctrl, const char *tag) {
    char *home = pty_make_tmpdir();
    if (!home) {
        pty_fail(TEST, "mkdtemp failed");
        return false;
    }
    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/bg_%s", home, tag);
    unlink(marker);
    char ready[PATH_MAX];
    snprintf(ready, sizeof(ready), "%s/rdy_%s", home, tag);
    unlink(ready);

    /// The subshell writes `ready` as the FIRST act of its body. lush arms the
    /// async child's SIG_IGN before running any body command (the #375/#447
    /// disposition under test), so `ready` existing proves SIG_IGN is
    /// installed. Gating the control character on `ready` guarantees it reaches
    /// an already-armed child -- a slow subshell start under load cannot race
    /// the signal. (Were the arm-before-first-command ordering ever changed,
    /// this gate would need to move too.) total_ms leaves wide margin for the
    /// marker write (ready + 2s sleep) even when the subshell start is delayed.
    char line[PATH_MAX * 2 + 96];
    snprintf(line, sizeof(line),
             "( echo r > %s; sleep 2 && echo x > %s ) & sleep 6\n", ready,
             marker);
    char seq[2] = {ctrl, '\0'};
    const timed_input_t in[] = {
        { 600, "set +m\n",  NULL},
        {1200,       line,  NULL},
        {1300,        seq, ready}, /// fires once the armed subshell is running
    };
    drive(lush, home, in, sizeof(in) / sizeof(in[0]), 5200);

    bool survived = access(marker, F_OK) == 0;
    pty_rmrf(home);
    free(home);
    return survived;
}

/// Returns true if a foreground command is interrupted by a terminal Ctrl-C:
/// the marker it would write only on completion is absent. The `sleep` is
/// deliberately shorter than the drive window, so this stays discriminating: a
/// shell that (wrongly) ignored the foreground Ctrl-C would let the sleep run
/// to completion and write the marker inside the window, failing the check --
/// whereas a correct shell's SIG_DFL kills the sleep first, leaving it absent.
static bool fg_interrupted_by_ctrl_c(const char *lush) {
    char *home = pty_make_tmpdir();
    if (!home) {
        pty_fail(TEST, "mkdtemp failed");
        return false;
    }
    char marker[PATH_MAX];
    snprintf(marker, sizeof(marker), "%s/fg", home);
    unlink(marker);
    char ready[PATH_MAX];
    snprintf(ready, sizeof(ready), "%s/rdy_fg", home);
    unlink(ready);

    char line[PATH_MAX * 2 + 64];
    snprintf(line, sizeof(line), "echo r > %s; sleep 2 && echo x > %s\n", ready,
             marker);
    const timed_input_t in[] = {
        {600,   line,  NULL},
        {700, "\x03", ready}, /// interrupt once the foreground sleep is running
    };
    drive(lush, home, in, sizeof(in) / sizeof(in[0]), 3600);

    bool interrupted = access(marker, F_OK) != 0;
    pty_rmrf(home);
    free(home);
    return interrupted;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lush-binary-path>\n", argv[0]);
        return 2;
    }
    const char *lush = argv[1];
    int failures = 0;

    /// The fix: a backgrounded child sharing the shell's group ignores a
    /// terminal SIGINT (SIG_IGN) and survives to write its marker.
    if (bg_survives_ctrl(lush, '\x03', "int")) {
        pty_ok(TEST, "backgrounded subshell survives terminal Ctrl-C (SIGINT)");
    } else {
        pty_fail(TEST, "backgrounded subshell was killed by terminal Ctrl-C");
        failures++;
    }

    /// The POSIX async-list rule also covers SIGQUIT (Ctrl-\): the backgrounded
    /// child must ignore it and not be core-dumped.
    if (bg_survives_ctrl(lush, '\x1c', "quit")) {
        pty_ok(TEST,
               "backgrounded subshell survives terminal Ctrl-\\ (SIGQUIT)");
    } else {
        pty_fail(TEST, "backgrounded subshell was killed by terminal Ctrl-\\");
        failures++;
    }

    /// Regression guard: a foreground command stays interruptible (SIG_DFL).
    if (fg_interrupted_by_ctrl_c(lush)) {
        pty_ok(TEST, "foreground command interrupted by terminal Ctrl-C");
    } else {
        pty_fail(TEST, "foreground command was not interrupted by Ctrl-C");
        failures++;
    }

    if (failures) {
        return 1;
    }
    printf("%s: all checks passed\n", TEST);
    return 0;
}
