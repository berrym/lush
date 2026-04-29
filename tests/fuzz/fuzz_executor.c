/**
 * @file fuzz_executor.c
 * @brief Fuzz target for lush executor (parses AND executes)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Extends fuzzing past AST construction (which fuzz_parser covers)
 * into the executor itself: builtins, control flow, expansion,
 * redirection setup, heredoc collection, parameter resolution,
 * and the command-lookup / argv-prep paths.
 *
 * Sandboxing approach: this binary is built with -DLUSH_FUZZ_SANDBOX,
 * which makes the project's lush_fork() wrapper (include/lush_fork.h)
 * always return -1. Every fork site in the executor already has
 * POSIX-required fork-failure handling, so external command
 * execution, pipelines, subshells, command substitution, and
 * process substitution all bail before spawning anything. The
 * fuzzer can never escape into actually running attacker-controlled
 * external programs.
 *
 * Belt-and-suspenders: at fuzzer init we set RLIMIT_CPU,
 * RLIMIT_FSIZE, RLIMIT_NPROC and chdir() into a per-process scratch
 * directory so even if a future caller bypasses lush_fork(), damage
 * is bounded.
 *
 * Build with libFuzzer:
 *   CC=clang meson setup build -Denable_fuzzing=true -Dfuzzer=libfuzzer
 *   meson compile -C build fuzz_executor
 *   ./build/fuzz_executor tests/fuzz/corpus/parser/ \
 *       -max_len=4096 -timeout=25 -close_fd_mask=3
 *
 * -timeout=25 matches libFuzzer's recommended floor and tolerates
 *   sandbox-vs-production behavior gaps on infinite-loop-shaped inputs
 *   (issue #63). -close_fd_mask=3 silences per-input executor stderr
 *   noise so the log stays useful.
 */

#include "executor.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

/* The shell's persistent global executor and helper functions live
 * in src/lush.c (excluded from this fuzz target). Several other
 * translation units (src/builtins/builtins.c, src/config.c,
 * src/display_integration.c, src/signals.c, the LLE shell-integration
 * sources) reference these symbols. Provide minimal in-target
 * replacements so the linker is satisfied — the fuzz session never
 * exits cleanly anyway, so cleanup is not required.
 *
 * In src/lush.c global_executor is `static` (file-scope) and accessed
 * externally only via get_global_executor(). Replicating that
 * encapsulation here would require duplicating the shape; simpler to
 * make it externally visible in this build. The variable is unique
 * to this fuzz binary and never linked into the production lush
 * executable. */
executor_t *global_executor = NULL;

executor_t *get_global_executor(void) { return global_executor; }

int parse_and_execute(const char *command) {
    if (!global_executor) {
        global_executor = executor_new();
        if (!global_executor) {
            return 1;
        }
    }
    /* Fuzz inputs are independent batches; line 1 of their own slice. */
    return executor_execute_command_line(global_executor, command, 1);
}

/* ============================================================================
 * One-time fuzz-process initialization
 * ============================================================================
 *
 * Runs from LLVMFuzzerInitialize(), which libFuzzer calls before the
 * fuzzing loop begins. Establishes a scratch sandbox so even if any
 * code path slipped past the lush_fork stub, damage is bounded.
 */

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;

    /* CPU limit: 1 hour cumulative process CPU time. RLIMIT_CPU is
     * a process-lifetime limit (NOT per-input), so the value must
     * accommodate full fuzz sessions. libFuzzer's own -timeout=N
     * flag is the proper per-input safety net (fires SIGALRM after
     * N seconds for a single input, recorded as a timeout artifact,
     * fuzzer continues). The earlier 30s value here was a defense-
     * in-depth cap that misfired on every multi-minute fuzz session,
     * killing the process well short of -max_total_time and wasting
     * 95%+ of intended fuzz budget (issue #74). 1 hour is far above
     * any plausible single-session run while still bounding pathological
     * runaway behavior. */
    struct rlimit cpu = {.rlim_cur = 3600, .rlim_max = 3600};
    setrlimit(RLIMIT_CPU, &cpu);

    /* File size limit: 256 MiB. Bounds disk-fill from output
     * redirection or heredoc body collection while leaving generous
     * headroom for libFuzzer's own log output (which is subject to
     * the same RLIMIT_FSIZE when callers pipe the fuzzer's stdio
     * through a shell `>` redirect). A 1 MiB cap was tripping on the
     * executor's parse-error spam during long runs and killing the
     * fuzzer with SIGXFSZ. */
    struct rlimit fs = {.rlim_cur = 1 << 28, .rlim_max = 1 << 28};
    setrlimit(RLIMIT_FSIZE, &fs);

    /* Process limit. lush_fork() returns -1 under LUSH_FUZZ_SANDBOX so
     * the shell itself never spawns. The cap exists in case a stray
     * fork() site is added later. The sanitizer runtime (ASan/UBSan)
     * still needs to fork to spawn its external symbolizer when
     * formatting stack traces — a too-low cap silently breaks crash
     * reporting (symbolizer fork fails, stacks come back as raw
     * addresses, libFuzzer exits with a non-standard code and no
     * usable diagnostic). 256 leaves comfortable headroom for the
     * symbolizer plus libFuzzer's own bookkeeping while still
     * bounding any genuine fork-bomb. */
    struct rlimit np = {.rlim_cur = 256, .rlim_max = 256};
    setrlimit(RLIMIT_NPROC, &np);

    /* Scratch directory. Use a per-PID path so concurrent fuzzers
     * (libFuzzer's `-jobs=N`) don't collide. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/lush-fuzz-exec-%d", (int)getpid());
    mkdir(tmpdir, 0700);
    if (chdir(tmpdir) == 0) {
        setenv("TMPDIR", tmpdir, 1);
    }

    /* Initialize shell mode system once for the whole fuzz session. */
    shell_mode_init();

    /* The global symbol table manager must exist before
     * executor_new() will succeed; it normally gets initialised
     * during shell startup in src/init.c (which we exclude). */
    init_symtable(); /* idempotent */

    return 0;
}

/* ============================================================================
 * Per-input entry point
 * ============================================================================
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Cap input size; very large inputs aren't representative of
     * real shell sessions and slow the fuzzer disproportionately. */
    if (size > 65536) {
        return 0;
    }

    /* Null-terminate input as a C string (the executor expects one). */
    char *input = malloc(size + 1);
    if (!input) {
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';

    /* Reject inputs containing literal NUL bytes — the executor's
     * command-line API can't see past them. The PARSER fuzzer already
     * exercises NUL-byte handling at the parse layer; the executor
     * fuzzer's job is the post-parse code. */
    if (strlen(input) != size) {
        free(input);
        return 0;
    }

    /* Build a fresh executor for each input. Reusing one across
     * inputs would let earlier executions' state (variables,
     * functions, traps, options) leak into later ones, making
     * crashes harder to minimize. */
    executor_t *executor = executor_new();
    if (!executor) {
        free(input);
        return 0;
    }

    /* This is the call we're fuzzing: parse + execute end-to-end.
     * Internally walks parser_new -> parser_parse -> executor_execute.
     * Any segfault, ASan finding, UBSan finding, or libFuzzer timeout
     * is recorded by libFuzzer as a crash artifact. */
    (void)executor_execute_command_line(executor, input, 1);

    executor_free(executor);
    free(input);
    return 0;
}

#ifdef FUZZ_AFL_MAIN
/* AFL++ entry: read input from stdin once, hand it to the libFuzzer
 * entry point. Same shape as fuzz_parser.c's AFL main. */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    LLVMFuzzerInitialize(&argc, &argv);

    uint8_t *buf = NULL;
    size_t capacity = 0;
    size_t size = 0;
    while (1) {
        if (size >= capacity) {
            capacity = capacity ? capacity * 2 : 4096;
            uint8_t *new_buf = realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                return 1;
            }
            buf = new_buf;
        }
        size_t n = fread(buf + size, 1, capacity - size, stdin);
        if (n == 0) {
            break;
        }
        size += n;
    }

    if (buf) {
        LLVMFuzzerTestOneInput(buf, size);
        free(buf);
    }
    return 0;
}
#endif /* FUZZ_AFL_MAIN */
