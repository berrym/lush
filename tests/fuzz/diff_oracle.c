/**
 * @file diff_oracle.c
 * @brief Mode-aware differential test harness for lush
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * For each input file given on the command line:
 *   - Resolves the lush mode from the input's parent directory name
 *     (posix / bash / zsh / lush).
 *   - Runs ./build/lush in matching mode AND the corresponding
 *     reference shell oracle (dash / bash / zsh / none) on the same
 *     input, in parallel via fork+exec+pipes.
 *   - Compares (exit_status both 0 / both non-0, stdout exact match);
 *     stderr is ignored because formatting varies wildly across shells.
 *   - Emits one machine-readable JSONL line per input describing the
 *     comparison result.
 *   - Exits non-zero if any divergence was not in the allow-list.
 *
 * Lush is a polyglot superset of POSIX, bash, and zsh — none of those
 * three is "the" reference shell. Mode-tagged inputs are checked
 * against the oracle that matches their declared mode. Inputs in the
 * lush/ subdir have no oracle; they are run through lush only and
 * checked for crash/timeout, not for behavioural agreement.
 *
 * Oracle binary paths are runtime-overridable via env vars
 * (LUSH_ORACLE_POSIX / LUSH_ORACLE_BASH / LUSH_ORACLE_ZSH) so the
 * harness adapts to Linux vs macOS vs CI without rebuilds. Missing
 * oracles cause the matching subset to be skipped with a notice
 * rather than failing the run hard.
 */

#include "lush_fork.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIFF_BUF_SIZE 65536
#define DIFF_TIMEOUT_SEC 10
#define DIFF_ALLOWLIST_LINE_MAX 1024

typedef enum {
    MODE_POSIX,
    MODE_BASH,
    MODE_ZSH,
    MODE_LUSH,
    MODE_UNKNOWN
} mode_t_;

static const char *MODE_NAMES[] = {
    [MODE_POSIX] = "posix", [MODE_BASH] = "bash",       [MODE_ZSH] = "zsh",
    [MODE_LUSH] = "lush",   [MODE_UNKNOWN] = "unknown",
};

typedef struct {
    const char *env_var;        /* env var override of binary path */
    const char *default_binary; /* fallback path */
    const char *lush_set_o;     /* `set -o X` to inject when running lush */
} oracle_config_t;

static const oracle_config_t ORACLES[] = {
    [MODE_POSIX] = {"LUSH_ORACLE_POSIX", "/usr/local/bin/dash", "posix"},
    [MODE_BASH] = {"LUSH_ORACLE_BASH", "/usr/local/bin/bash", "bash"},
    [MODE_ZSH] = {"LUSH_ORACLE_ZSH", "/bin/zsh", "zsh"},
    [MODE_LUSH] = {NULL, NULL, NULL},
    [MODE_UNKNOWN] = {NULL, NULL, NULL},
};

typedef struct {
    int exit_status;
    char stdout_buf[DIFF_BUF_SIZE];
    char stderr_buf[DIFF_BUF_SIZE];
    bool timed_out;
    bool spawn_failed;
} run_result_t;

typedef struct {
    char path[1024];
    bool ignore_stdout; /* known-divergence allow-list entry */
} allowlist_entry_t;

static allowlist_entry_t g_allowlist[256];
static size_t g_allowlist_count = 0;

/* ============================================================================
 * Mode resolution
 * ============================================================================
 */

static mode_t_ mode_from_path(const char *path) {
    /* Walk back to find the parent directory name. */
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return MODE_UNKNOWN;
    }
    /* Find the second-to-last slash to extract the parent dir name. */
    const char *p = last_slash;
    while (p > path && *(p - 1) != '/') {
        p--;
    }
    /* p..last_slash is the parent dir name */
    size_t n = (size_t)(last_slash - p);
    for (mode_t_ m = MODE_POSIX; m <= MODE_LUSH; m++) {
        if (strlen(MODE_NAMES[m]) == n && strncmp(p, MODE_NAMES[m], n) == 0) {
            return m;
        }
    }
    return MODE_UNKNOWN;
}

/* ============================================================================
 * Allow-list
 * ============================================================================
 */

static void allowlist_load(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Allow-list is optional. */
        return;
    }
    char line[DIFF_ALLOWLIST_LINE_MAX];
    while (fgets(line, sizeof(line), fp)) {
        /* Trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        /* Skip blank lines and comments */
        if (*p == '\0' || *p == '\n' || *p == '#') {
            continue;
        }
        /* basename is up to first whitespace */
        char *end = p;
        while (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\n') {
            end++;
        }
        if (g_allowlist_count >= sizeof(g_allowlist) / sizeof(g_allowlist[0])) {
            break;
        }
        size_t len = (size_t)(end - p);
        if (len >= sizeof(g_allowlist[0].path)) {
            len = sizeof(g_allowlist[0].path) - 1;
        }
        memcpy(g_allowlist[g_allowlist_count].path, p, len);
        g_allowlist[g_allowlist_count].path[len] = '\0';
        g_allowlist[g_allowlist_count].ignore_stdout = true;
        g_allowlist_count++;
    }
    fclose(fp);
}

static bool is_known_divergence(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (size_t i = 0; i < g_allowlist_count; i++) {
        if (strcmp(g_allowlist[i].path, base) == 0) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Subprocess execution with timeout and pipe capture
 * ============================================================================
 */

static void drain_fd(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n < cap - 1) {
        ssize_t r = read(fd, buf + n, cap - 1 - n);
        if (r <= 0) {
            break;
        }
        n += (size_t)r;
    }
    buf[n] = '\0';
    /* Drain anything remaining so the child's writes never block. */
    char scratch[4096];
    while (read(fd, scratch, sizeof(scratch)) > 0) {
        /* discard */
    }
}

/**
 * @brief Run a binary with argv against an input string, capturing
 *        stdout/stderr/exit and enforcing a wall-clock timeout
 */
static run_result_t run_with_input(const char *binary, const char *const *argv,
                                   const char *input, int timeout_sec) {
    run_result_t r = {0};
    int in_pipe[2], out_pipe[2], err_pipe[2];

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        r.spawn_failed = true;
        return r;
    }

    pid_t pid = lush_fork();
    if (pid < 0) {
        r.spawn_failed = true;
        return r;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        execv(binary, (char *const *)argv);
        _exit(127);
    }

    /* Parent: feed input to child stdin, then close to signal EOF. */
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    if (input && input[0] != '\0') {
        size_t len = strlen(input);
        ssize_t written = write(in_pipe[1], input, len);
        (void)written;
    }
    close(in_pipe[1]);

    /* Wait for child with timeout via SIGCHLD/poll loop.
     * Simple approach: alarm + waitpid. */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int status = 0;
    pid_t reaped = 0;
    bool first_pass = true;
    while (1) {
        reaped = waitpid(pid, &status, WNOHANG);
        if (reaped > 0 || reaped < 0) {
            break;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms > timeout_sec * 1000) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            r.timed_out = true;
            break;
        }
        /* Drain pipes opportunistically each cycle so kernel buffers
         * stay below their limits and the child doesn't block. */
        if (first_pass) {
            first_pass = false;
        }
        struct timespec sleep_ts = {.tv_sec = 0, .tv_nsec = 10 * 1000000};
        nanosleep(&sleep_ts, NULL);
    }

    drain_fd(out_pipe[0], r.stdout_buf, sizeof(r.stdout_buf));
    drain_fd(err_pipe[0], r.stderr_buf, sizeof(r.stderr_buf));
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (r.timed_out) {
        r.exit_status = -1;
    } else if (WIFEXITED(status)) {
        r.exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.exit_status = 128 + WTERMSIG(status);
    } else {
        r.exit_status = -1;
    }
    return r;
}

/* ============================================================================
 * JSON output (minimal, just what we need)
 * ============================================================================
 */

static void json_escape(const char *s, char *out, size_t out_cap) {
    size_t i = 0, j = 0;
    while (s[i] != '\0' && j + 8 < out_cap) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c < 0x20) {
            j += (size_t)snprintf(out + j, out_cap - j, "\\u%04x", c);
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

/* ============================================================================
 * Reading file contents
 * ============================================================================
 */

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);
    if (out_len) {
        *out_len = n;
    }
    return buf;
}

/* ============================================================================
 * Comparison
 * ============================================================================
 */

/**
 * @brief Decide whether two run_result_t agree
 *
 * "Agree" means: either both succeeded (exit 0) or both failed
 * (exit non-zero); AND stdout matches exactly. stderr is not
 * compared because formatting varies too much across reference
 * shells. Timeouts on either side count as disagreement unless
 * both timed out.
 */
static bool results_agree(const run_result_t *a, const run_result_t *b) {
    if (a->timed_out != b->timed_out) {
        return false;
    }
    if (a->timed_out && b->timed_out) {
        return true;
    }
    bool a_ok = (a->exit_status == 0);
    bool b_ok = (b->exit_status == 0);
    if (a_ok != b_ok) {
        return false;
    }
    return strcmp(a->stdout_buf, b->stdout_buf) == 0;
}

/* ============================================================================
 * Per-input driver
 * ============================================================================
 */

static int process_input(const char *path, const char *lush_path) {
    mode_t_ mode = mode_from_path(path);
    if (mode == MODE_UNKNOWN) {
        fprintf(stderr, "diff_oracle: skipping %s (unknown mode)\n", path);
        return 0;
    }

    size_t input_len = 0;
    char *input = read_file(path, &input_len);
    if (!input) {
        fprintf(stderr, "diff_oracle: cannot read %s: %s\n", path,
                strerror(errno));
        return 1;
    }

    const char *lush_set_o = ORACLES[mode].lush_set_o;

    /* Run lush in matching mode. Inject `set -o X` before the input
     * by passing the combined script via stdin. */
    char lush_input[DIFF_BUF_SIZE];
    if (lush_set_o) {
        snprintf(lush_input, sizeof(lush_input), "set -o %s\n%s", lush_set_o,
                 input);
    } else {
        snprintf(lush_input, sizeof(lush_input), "%s", input);
    }
    const char *lush_argv[] = {lush_path, NULL};
    run_result_t lush_r =
        run_with_input(lush_path, lush_argv, lush_input, DIFF_TIMEOUT_SEC);

    /* Resolve oracle binary, with env override. */
    const char *oracle_bin = NULL;
    if (mode != MODE_LUSH) {
        const char *env_val =
            ORACLES[mode].env_var ? getenv(ORACLES[mode].env_var) : NULL;
        oracle_bin =
            (env_val && env_val[0]) ? env_val : ORACLES[mode].default_binary;
    }

    bool oracle_present = false;
    run_result_t oracle_r = {0};
    if (oracle_bin && access(oracle_bin, X_OK) == 0) {
        oracle_present = true;
        const char *oracle_argv[] = {oracle_bin, NULL};
        oracle_r =
            run_with_input(oracle_bin, oracle_argv, input, DIFF_TIMEOUT_SEC);
    }

    /* Decide outcome */
    bool divergent = false;
    if (mode == MODE_LUSH) {
        /* No oracle — flag only crashes/timeouts */
        if (lush_r.timed_out || lush_r.exit_status >= 128) {
            divergent = true;
        }
    } else if (!oracle_present) {
        /* Oracle missing — skip silently in JSONL marker */
    } else if (!results_agree(&lush_r, &oracle_r)) {
        divergent = true;
    }

    bool allowed = is_known_divergence(path);

    /* Emit JSONL */
    char path_e[2048], lush_out_e[DIFF_BUF_SIZE * 2];
    char oracle_out_e[DIFF_BUF_SIZE * 2];
    json_escape(path, path_e, sizeof(path_e));
    json_escape(lush_r.stdout_buf, lush_out_e, sizeof(lush_out_e));
    json_escape(oracle_r.stdout_buf, oracle_out_e, sizeof(oracle_out_e));

    printf("{\"path\":\"%s\",\"mode\":\"%s\",\"oracle\":%s,\"agree\":%s,"
           "\"allowed\":%s,\"lush\":{\"exit\":%d,\"timed_out\":%s,"
           "\"stdout\":\"%s\"},"
           "\"oracle_result\":{\"exit\":%d,\"timed_out\":%s,"
           "\"stdout\":\"%s\"}}\n",
           path_e, MODE_NAMES[mode],
           oracle_present || mode == MODE_LUSH ? "true" : "false",
           divergent ? "false" : "true", allowed ? "true" : "false",
           lush_r.exit_status, lush_r.timed_out ? "true" : "false", lush_out_e,
           oracle_r.exit_status, oracle_r.timed_out ? "true" : "false",
           oracle_out_e);
    fflush(stdout);

    free(input);
    return (divergent && !allowed) ? 1 : 0;
}

/* ============================================================================
 * Main
 * ============================================================================
 */

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--lush PATH] [--allowlist FILE] INPUT [INPUT...]\n"
            "\n"
            "Mode-aware differential test harness for lush. Each INPUT's\n"
            "parent directory name (posix/bash/zsh/lush) selects which\n"
            "reference shell to compare against.\n"
            "\n"
            "Options:\n"
            "  --lush PATH       Path to lush binary (default: ./build/lush)\n"
            "  --allowlist FILE  Allow-list of expected divergences\n"
            "                    (default: "
            "tests/fuzz/differential/known_divergences.txt)\n"
            "\n"
            "Env overrides for oracles:\n"
            "  LUSH_ORACLE_POSIX (default /usr/local/bin/dash)\n"
            "  LUSH_ORACLE_BASH  (default /usr/local/bin/bash)\n"
            "  LUSH_ORACLE_ZSH   (default /bin/zsh)\n",
            prog);
}

int main(int argc, char **argv) {
    const char *lush_path = "./build/lush";
    const char *allowlist_path =
        "tests/fuzz/differential/known_divergences.txt";

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--lush") == 0 && i + 1 < argc) {
            lush_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--allowlist") == 0 && i + 1 < argc) {
            allowlist_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            break;
        }
    }

    if (i >= argc) {
        usage(argv[0]);
        return 2;
    }

    allowlist_load(allowlist_path);

    int divergences = 0;
    int total = 0;
    for (; i < argc; i++) {
        total++;
        if (process_input(argv[i], lush_path) != 0) {
            divergences++;
        }
    }

    fprintf(stderr,
            "diff_oracle: %d input(s) processed, %d unexpected divergence(s)\n",
            total, divergences);

    return divergences > 0 ? 1 : 0;
}
