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
    const char *env_var;           /* env var override of binary path */
    const char *binary_name;       /* basename for PATH lookup */
    const char *const *candidates; /* NULL-terminated list of absolute paths */
    const char *lush_mode; /* `mode X` preset to inject when running lush */
} oracle_config_t;

/* Candidate paths probed in order when env override is unset and PATH
 * lookup fails. Covers macOS (Homebrew Intel /usr/local, Apple Silicon
 * /opt/homebrew, system /bin) and Linux (Debian/Ubuntu /usr/bin, /bin;
 * Arch /usr/bin; Fedora /usr/bin). dash is not installed by default
 * on macOS or RHEL-family so its absence is expected there. */
static const char *const DASH_CANDIDATES[] = {
    "/usr/bin/dash",          /* Debian/Ubuntu, Arch, Fedora */
    "/bin/dash",              /* some Debian variants */
    "/usr/local/bin/dash",    /* macOS Homebrew (Intel) */
    "/opt/homebrew/bin/dash", /* macOS Homebrew (Apple Silicon) */
    NULL,
};
/* Homebrew prefixes first so macOS prefers modern bash over the
 * Apple-shipped /bin/bash, which is GPLv2-pinned at 3.2.57 (May 2014)
 * and lacks associative arrays, case-modification expansions, [[ -v ]],
 * negative substring offsets, ${arr[@]:N:M} slicing, and most of the
 * surface this corpus exercises. /bin/bash and /usr/bin/bash are the
 * common Linux locations and stay in the list as a fallback for hosts
 * without Homebrew, but they are reached only if Homebrew is absent.
 * The MODE_BASH path additionally rejects bash < 4.0 via the version
 * filter in oracle_candidate_acceptable(), so even if /bin/bash is
 * picked on macOS without Homebrew, the harness will warn and skip it
 * rather than producing a flood of false-positive divergences. */
static const char *const BASH_CANDIDATES[] = {
    "/usr/local/bin/bash",    /* macOS Homebrew (Intel) */
    "/opt/homebrew/bin/bash", /* macOS Homebrew (Apple Silicon) */
    "/bin/bash",              /* most Linux distros */
    "/usr/bin/bash",          /* Fedora/Arch */
    NULL,
};
static const char *const ZSH_CANDIDATES[] = {
    "/bin/zsh",              /* macOS default, some Linux */
    "/usr/bin/zsh",          /* most Linux distros */
    "/usr/local/bin/zsh",    /* macOS Homebrew (Intel) */
    "/opt/homebrew/bin/zsh", /* macOS Homebrew (Apple Silicon) */
    NULL,
};

static const oracle_config_t ORACLES[] = {
    [MODE_POSIX] = {"LUSH_ORACLE_POSIX", "dash", DASH_CANDIDATES, "posix"},
    [MODE_BASH] = { "LUSH_ORACLE_BASH", "bash", BASH_CANDIDATES,  "bash"},
    [MODE_ZSH] = {  "LUSH_ORACLE_ZSH",  "zsh",  ZSH_CANDIDATES,   "zsh"},
    [MODE_LUSH] = {               NULL,   NULL,            NULL,  "lush"},
    [MODE_UNKNOWN] = {               NULL,   NULL,            NULL,    NULL},
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
 * Oracle binary resolution
 * ============================================================================
 *
 * Resolution order:
 *   1. The mode's LUSH_ORACLE_* env var, if set and executable.
 *   2. The binary basename via PATH lookup (handles unusual install
 *      prefixes like Nix store paths or /opt/local).
 *   3. The fixed candidate list (covers macOS Homebrew + the common
 *      Linux distro locations without requiring PATH to be set).
 *
 * Each step is gated by access(path, X_OK) so a present-but-unusable
 * binary doesn't get selected. Returns the first match or NULL.
 *
 * The returned pointer is either a string literal from the candidate
 * table, an env-var pointer (lifetime: program), or a heap pointer
 * returned via the static_buf so callers can treat it as live for the
 * remainder of the per-input call. We use a static buffer for the
 * PATH search result instead of malloc so we never leak; the buffer
 * is overwritten on subsequent calls, which is fine because the
 * caller consumes the path before resolving the next oracle.
 */

static bool path_executable(const char *path) {
    return path && path[0] && access(path, X_OK) == 0;
}

/* Read major version from `binary --version` output. Returns the
 * major-version integer (e.g. 5 for bash 5.3.9) or -1 if the version
 * cannot be determined. The function spawns a short-lived child via
 * fork+exec and captures the first 256 bytes of stdout. Parses the
 * pattern "version N." (used by bash, dash, zsh, ksh, ...) since the
 * leading text and exact format differ across shells but all of them
 * embed "version X.Y" early in the --version output.
 *
 * Used by the bash modernity filter below; benign for the other modes
 * if they ever need it. */
static int read_major_version(const char *binary) {
    if (!path_executable(binary)) {
        return -1;
    }
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid_t pid = lush_fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl(binary, binary, "--version", (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    char buf[256];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    /* Drain anything remaining so the child does not block on pipe. */
    char scratch[64];
    while (read(pipefd[0], scratch, sizeof(scratch)) > 0) {
        /* discard */
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';
    const char *v = strstr(buf, "version ");
    if (!v) {
        return -1;
    }
    v += 8;
    if (!isdigit((unsigned char)*v)) {
        return -1;
    }
    return atoi(v);
}

/* Modernity filter for oracle candidates. For MODE_BASH the corpus
 * exercises features (associative arrays, ${var^^}, [[ -v ]], -- you
 * name it) that bash 3.2.57 -- the Apple-shipped /bin/bash -- does not
 * support. A 3.x bash silently exits non-zero and produces unrelated
 * diagnostics on every test in the bash corpus, so allowing it as an
 * oracle generates a flood of false-positive divergences that obscure
 * any real bugs. Reject < 4.0 with a stderr warning so the user knows
 * why the candidate was skipped. Version probing fails open: if
 * --version output cannot be parsed, the candidate is accepted and
 * any incompatibilities surface as ordinary divergences. */
static bool oracle_candidate_acceptable(mode_t_ mode, const char *path) {
    if (!path_executable(path)) {
        return false;
    }
    if (mode != MODE_BASH) {
        return true;
    }
    int major = read_major_version(path);
    if (major < 0) {
        return true; /* version unknown -- accept */
    }
    if (major < 4) {
        fprintf(stderr,
                "diff_oracle: skipping %s (bash %d.x; need >= 4.0 for "
                "full feature coverage). Install GNU bash via Homebrew "
                "(brew install bash) or set LUSH_ORACLE_BASH to a "
                "modern binary.\n",
                path, major);
        return false;
    }
    return true;
}

/* PATH lookup. Returns true and fills out_buf if found. out_buf must
 * be at least PATH_MAX bytes. */
static bool resolve_via_path(const char *name, char *out_buf, size_t out_cap) {
    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0] || !name || !name[0]) {
        return false;
    }
    const char *p = path_env;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t dirlen = colon ? (size_t)(colon - p) : strlen(p);
        if (dirlen > 0 && dirlen + 1 + strlen(name) + 1 <= out_cap) {
            memcpy(out_buf, p, dirlen);
            out_buf[dirlen] = '/';
            size_t name_len = strlen(name);
            memcpy(out_buf + dirlen + 1, name, name_len);
            out_buf[dirlen + 1 + name_len] = '\0';
            if (path_executable(out_buf)) {
                return true;
            }
        }
        if (!colon) {
            break;
        }
        p = colon + 1;
    }
    return false;
}

static const char *resolve_oracle(mode_t_ mode) {
    if (mode != MODE_POSIX && mode != MODE_BASH && mode != MODE_ZSH) {
        return NULL;
    }
    const oracle_config_t *cfg = &ORACLES[mode];

    /* 1. Env override. The user-set override is honored even if it
     * fails the modernity filter -- they asked for it explicitly --
     * but we still warn so the false-positive divergences are
     * attributable. */
    if (cfg->env_var) {
        const char *env_val = getenv(cfg->env_var);
        if (path_executable(env_val)) {
            (void)oracle_candidate_acceptable(mode, env_val); /* warns */
            return env_val;
        }
    }

    /* 2. PATH lookup. resolve_via_path returns the FIRST PATH-resolved
     * binary; for MODE_BASH the modernity filter rejects an old binary
     * here so the harness falls through to the candidate list if PATH
     * points at /bin/bash 3.2.57 (macOS without Homebrew in PATH). */
    static char path_buf[MODE_LUSH + 1][4096];
    if (resolve_via_path(cfg->binary_name, path_buf[mode],
                         sizeof(path_buf[mode]))) {
        if (oracle_candidate_acceptable(mode, path_buf[mode])) {
            return path_buf[mode];
        }
    }

    /* 3. Candidate list */
    if (cfg->candidates) {
        for (const char *const *c = cfg->candidates; *c; c++) {
            if (oracle_candidate_acceptable(mode, *c)) {
                return *c;
            }
        }
    }
    return NULL;
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

    const char *lush_mode = ORACLES[mode].lush_mode;

    /* Run lush in matching mode. Inject `mode X` before the input by
     * passing the combined script via stdin. `mode` is the canonical
     * mode-preset selector (May-06 configuration cleanup removed the
     * `set -o {bash,zsh,lush}` toggles; `set -o posix` survives only
     * as a bash-bridge alias, so we use `mode` uniformly). */
    char lush_input[DIFF_BUF_SIZE];
    if (lush_mode) {
        snprintf(lush_input, sizeof(lush_input), "mode %s\n%s", lush_mode,
                 input);
    } else {
        snprintf(lush_input, sizeof(lush_input), "%s", input);
    }
    const char *lush_argv[] = {lush_path, NULL};
    run_result_t lush_r =
        run_with_input(lush_path, lush_argv, lush_input, DIFF_TIMEOUT_SEC);

    /* Resolve oracle binary via env override → PATH → candidate list.
     * Missing oracle is not fatal: the JSONL record marks it as
     * absent and the input is skipped from comparison. */
    const char *oracle_bin = (mode != MODE_LUSH) ? resolve_oracle(mode) : NULL;

    bool oracle_present = (oracle_bin != NULL);
    run_result_t oracle_r = {0};
    if (oracle_present) {
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
            "Oracle binary resolution (per mode): LUSH_ORACLE_* env var\n"
            "(if set and executable) -> PATH lookup for 'dash'/'bash'/'zsh'\n"
            "-> portable candidate list (Homebrew + common Linux paths).\n"
            "Env overrides:\n"
            "  LUSH_ORACLE_POSIX  (basename: dash)\n"
            "  LUSH_ORACLE_BASH   (basename: bash)\n"
            "  LUSH_ORACLE_ZSH    (basename: zsh)\n",
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
