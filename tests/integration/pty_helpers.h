/**
 * @file pty_helpers.h
 * @brief Shared PTY-harness primitives for terminal-required login tests.
 *
 * Provides the minimum infrastructure needed to spawn lush on the
 * slave end of a pseudo-terminal, drive it from the master end, and
 * inspect outputs / signal behavior from the parent.
 *
 * This is the same approach the zsh test suite takes (Test/W02jobs.ztst
 * uses the zsh/zpty module, which is a forkpty(3) wrapper) for any
 * test whose subject is *defined by* terminal semantics: SIGHUP
 * delivery on controlling-terminal hangup, SIGTTIN/SIGTTOU, job
 * control on the foreground process group. Bash's upstream tests do
 * NOT cover those cases; lush is following zsh's lead here.
 *
 * Header-only and pulled into each test file that needs it.
 */

#ifndef PTY_HELPERS_H
#define PTY_HELPERS_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#error "PTY harness requires a platform that exposes forkpty(3)"
#endif

/// Maximum bytes of PTY output the harness buffers from a single run.
/// Test scenarios should fit comfortably; if not the test is doing too
/// much.
#define PTY_BUF_SIZE 65536

typedef struct pty_session {
    pid_t pid;       ///< Child shell PID
    int master_fd;   ///< Parent end of the pseudo-terminal (child's
                     ///< stdout/stderr)
    int stdin_fd;    ///< Parent end of the stdin pipe to the child
    char *home_dir;  ///< Owned; mktemp'd; caller frees via pty_cleanup
    char *output;    ///< Owned; accumulated stdout/stderr from master FD
    size_t out_len;  ///< Bytes consumed in `output`
    size_t out_cap;  ///< Allocated size of `output`
    int exit_status; ///< waitpid status (set by pty_wait)
    bool reaped;     ///< true once waitpid returned
} pty_session_t;

/// Fail-printing helper. Test programs use this to surface assertion
/// failures uniformly on stderr.
static inline void pty_fail(const char *test_name, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: FAIL: ", test_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/// Pass-printing helper.
static inline void pty_ok(const char *test_name, const char *msg) {
    fprintf(stdout, "%s: OK: %s\n", test_name, msg);
}

/// Sleep for `ms` milliseconds. POSIX nanosleep wrapper that retries
/// on EINTR; this is the only signal that should reach us during a
/// PTY test and we want the delay to be honored despite it.
static inline void pty_sleep_ms(int ms) {
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        ;
    }
}

/// Make a fresh empty temp directory the caller owns (freed via
/// pty_cleanup). Returns NULL on failure with errno set.
static inline char *pty_make_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) {
        base = "/tmp";
    }
    size_t n = strlen(base) + strlen("/lush_pty.XXXXXX") + 1;
    char *path = (char *)malloc(n);
    if (!path) {
        return NULL;
    }
    snprintf(path, n, "%s/lush_pty.XXXXXX", base);
    if (!mkdtemp(path)) {
        free(path);
        return NULL;
    }
    return path;
}

/// rm -rf equivalent for a directory the harness created. Best-effort;
/// failures are logged to stderr but don't abort.
static inline void pty_rmrf(const char *path) {
    if (!path) {
        return;
    }
    /// Fork+exec rm -rf -- simpler and safer than rolling our own
    /// recursive walk for a test harness.
    pid_t p = fork();
    if (p == 0) {
        execlp("rm", "rm", "-rf", path, (char *)NULL);
        _exit(127);
    } else if (p > 0) {
        int st;
        waitpid(p, &st, 0);
    }
}

/// Write a marker echo line to a file. Used to seed startup files
/// (~/.lush_login, ~/.lush_logout, etc.).
static inline int pty_write_marker(const char *path, const char *marker) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "echo \"%s\"\n", marker);
    fclose(f);
    return 0;
}

/// Spawn lush with a hybrid stdio layout:
///   - stdin  = pipe (NOT a TTY -- avoids LLE raw mode, gives line-
///              mode getline reads on the lush side)
///   - stdout = PTY slave (TTY -- preserves controlling-terminal
///              semantics required for SIGHUP delivery, tcsetpgrp,
///              job control)
///   - stderr = PTY slave
///   - PTY slave is the child's controlling terminal (TIOCSCTTY)
///
/// This split is the canonical recipe for testing terminal-dependent
/// shell behavior without engaging the shell's interactive line
/// editor. zsh achieves the same effect via `-fiV +Z` (where -V
/// disables zle); lush has no equivalent flag yet, so we instead
/// rely on lle_readline.c's existing isatty(STDIN_FILENO) fallback
/// (returns simple getline output for non-TTY stdin even under -i).
///
/// argv must be a NULL-terminated list (first element conventionally
/// "lush"). The child inherits a minimal env: HOME=session->home_dir,
/// PATH (from parent), TERM=dumb.
static inline int pty_spawn(pty_session_t *session, const char *lush_path,
                            const char *const argv[]) {
    memset(session, 0, sizeof(*session));
    session->home_dir = pty_make_tmpdir();
    if (!session->home_dir) {
        perror("pty_spawn: mkdtemp");
        return -1;
    }

    session->output = (char *)calloc(1, PTY_BUF_SIZE);
    if (!session->output) {
        pty_rmrf(session->home_dir);
        free(session->home_dir);
        return -1;
    }
    session->out_cap = PTY_BUF_SIZE;

    /// stdin pipe (parent writes to [1], child reads from [0]).
    int in_pipe[2];
    if (pipe(in_pipe) != 0) {
        perror("pty_spawn: pipe");
        free(session->output);
        pty_rmrf(session->home_dir);
        free(session->home_dir);
        return -1;
    }

    /// PTY pair for stdout/stderr.
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) != 0) {
        perror("pty_spawn: openpty");
        close(in_pipe[0]);
        close(in_pipe[1]);
        free(session->output);
        pty_rmrf(session->home_dir);
        free(session->home_dir);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("pty_spawn: fork");
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(master);
        close(slave);
        free(session->output);
        pty_rmrf(session->home_dir);
        free(session->home_dir);
        return -1;
    }

    if (pid == 0) {
        /// Child: become session leader so we can acquire a
        /// controlling terminal.
        setsid();

        /// Make the PTY slave our controlling terminal. On macOS
        /// and BSDs, opening the slave again after setsid() acquires
        /// it; on Linux, TIOCSCTTY is the explicit ioctl. Use both
        /// patterns for cross-platform safety.
#ifdef TIOCSCTTY
        ioctl(slave, TIOCSCTTY, 0);
#endif

        /// Wire stdio: stdin <- pipe read end, stdout/stderr <-
        /// PTY slave.
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);

        /// Close everything we don't need.
        if (in_pipe[0] > STDERR_FILENO) {
            close(in_pipe[0]);
        }
        close(in_pipe[1]);
        if (slave > STDERR_FILENO) {
            close(slave);
        }
        close(master);

        /// Build a minimal envp so the test doesn't inherit user
        /// state. execve is the portable env-replacement path
        /// (clearenv is GNU-only).
        const char *parent_path = getenv("PATH");
        char home_buf[1024];
        char path_buf[2048];
        char term_buf[] = "TERM=dumb";
        snprintf(home_buf, sizeof(home_buf), "HOME=%s", session->home_dir);
        snprintf(path_buf, sizeof(path_buf), "PATH=%s",
                 parent_path ? parent_path : "/usr/bin:/bin");
        char *envp[] = {home_buf, path_buf, term_buf, NULL};
        execve(lush_path, (char *const *)argv, envp);
        perror("pty_spawn: execve");
        _exit(127);
    }

    /// Parent: keep the master + stdin-write ends. Drop the slave +
    /// stdin-read ends so the child's hangup detection works
    /// (master close → SIGHUP on slave).
    close(in_pipe[0]);
    close(slave);

    session->pid = pid;
    session->master_fd = master;
    session->stdin_fd = in_pipe[1];
    return 0;
}

/// Write a NUL-terminated string to the child's stdin (the pipe end).
/// Returns 0 on success or -1 with errno set.
static inline int pty_send(pty_session_t *session, const char *s) {
    size_t len = strlen(s);
    const char *p = s;
    while (len > 0) {
        ssize_t n = write(session->stdin_fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/// Drain whatever the master FD has available right now, with a
/// timeout. Appends to session->output until the FD blocks for
/// `timeout_ms` or the child exits (read returns 0). Always safe to
/// call multiple times.
static inline void pty_drain(pty_session_t *session, int timeout_ms) {
    while (session->out_len + 1 < session->out_cap) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(session->master_fd, &rfds);
        struct timeval tv = {.tv_sec = timeout_ms / 1000,
                             .tv_usec = (timeout_ms % 1000) * 1000};
        int r = select(session->master_fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (r == 0) {
            return; /// Timed out
        }
        ssize_t n = read(session->master_fd, session->output + session->out_len,
                         session->out_cap - session->out_len - 1);
        if (n <= 0) {
            return; /// Slave closed or read error
        }
        session->out_len += (size_t)n;
        session->output[session->out_len] = '\0';
    }
}

/// Wait for the child to exit. Returns waitpid status (also stashed
/// in session->exit_status). Does NOT close master_fd -- caller may
/// want to drain remaining output first.
static inline int pty_wait(pty_session_t *session, int timeout_ms) {
    /// Poll-with-timeout: waitpid(WNOHANG) loop with small sleeps.
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        int st;
        pid_t r = waitpid(session->pid, &st, WNOHANG);
        if (r == session->pid) {
            session->exit_status = st;
            session->reaped = true;
            return st;
        }
        if (r < 0 && errno == ECHILD) {
            /// Already reaped (shouldn't happen).
            session->reaped = true;
            return 0;
        }
        pty_sleep_ms(50);
        elapsed += 50;
    }
    /// Timed out: hard-kill so the test doesn't hang the suite.
    kill(session->pid, SIGKILL);
    int st;
    waitpid(session->pid, &st, 0);
    session->exit_status = st;
    session->reaped = true;
    return st;
}

/// Send SIGHUP to the child shell, drain any final output, wait for
/// exit. Convenience for the hangup-simulated-via-signal pattern
/// (closing the master FD is the alternative but is messier --
/// in-flight writes from the child get SIGPIPE'd, output is lost).
static inline int pty_sighup_and_wait(pty_session_t *session, int drain_ms,
                                      int exit_ms) {
    if (kill(session->pid, SIGHUP) != 0) {
        if (errno != ESRCH) {
            perror("pty_sighup_and_wait: kill");
        }
    }
    pty_drain(session, drain_ms);
    return pty_wait(session, exit_ms);
}

/// Close the parent's end of the child's stdin. This delivers EOF to
/// the child, which will cause its read loop to terminate and run
/// through the clean exit path (including login-shell logout
/// scripts). Idempotent.
static inline void pty_close_stdin(pty_session_t *session) {
    if (session && session->stdin_fd >= 0) {
        close(session->stdin_fd);
        session->stdin_fd = -1;
    }
}

/// Release session resources. Safe to call regardless of how the
/// session ended.
static inline void pty_cleanup(pty_session_t *session) {
    if (!session) {
        return;
    }
    if (session->stdin_fd >= 0) {
        close(session->stdin_fd);
        session->stdin_fd = -1;
    }
    if (session->master_fd >= 0) {
        close(session->master_fd);
        session->master_fd = -1;
    }
    if (session->pid > 0 && !session->reaped) {
        kill(session->pid, SIGKILL);
        int st;
        waitpid(session->pid, &st, 0);
    }
    free(session->output);
    session->output = NULL;
    pty_rmrf(session->home_dir);
    free(session->home_dir);
    session->home_dir = NULL;
}

/// Read a long-decimal value following a marker `<prefix><digits>` in
/// the accumulated output. Returns -1 if not found.
static inline long pty_extract_long_after(const pty_session_t *session,
                                          const char *prefix) {
    if (!session->output) {
        return -1;
    }
    const char *p = strstr(session->output, prefix);
    if (!p) {
        return -1;
    }
    p += strlen(prefix);
    char *end = NULL;
    errno = 0;
    long v = strtol(p, &end, 10);
    if (end == p || errno != 0) {
        return -1;
    }
    return v;
}

#endif /// PTY_HELPERS_H
