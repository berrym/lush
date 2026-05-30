/**
 * @file builtins.c
 * @brief Shell builtin registry, dispatch helpers, and shared infrastructure
 *
 * This file is the registry-side of the builtin system. After the per-
 * builtin file split (commits 7aa2e34c..), individual builtins live in
 * `src/builtins/bin_<name>.c`; this file holds the registry table that
 * maps names to handler functions, the `is_builtin()` lookup, the
 * source-location stash used by every builtin's structured-error
 * sites, the command-path hash table, and a few cross-builtin shared
 * helpers (is_valid_identifier).
 *
 * Builtin error reporting deliberately does NOT use a builtin-specific
 * helper layer. Sites use the structured-error API directly:
 *
 *   - Simple case (no `help:` suggestion):
 *       executor_error_report(current_executor, CODE,
 *                             builtin_get_source_location(), "fmt", args);
 *     The wrapper attaches the source-line snippet and walks the executor
 *     context stack (which includes "in builtin '<name>'" pushed by the
 *     dispatcher).
 *
 *   - With suggestion: inline the canonical shell_error_create() block.
 *
 * The pre-2026-04 builtin_error() and builtin_error_help() helpers were
 * removed when the foundation made source-line + dispatcher-context
 * automatic.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include "config.h"
#include "executor.h"
#include "ht.h"
#include "identifier.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/// bin_set's underlying impl lives in src/posix_opts.c.
int builtin_set(char **argv);

/* Hash table for remembered command paths. Owned here; bin_hash.c
 * (src/builtins/bin_hash.c) declares it `extern`. */
ht_strstr_t *command_hash = NULL;

/* ============================================================================
 * Negative PATH-search cache
 *
 * Bounds the syscall cost of tight loops repeatedly resolving a missing
 * command (the #79 / #73-Layer-2 case). A small fixed-size FIFO of
 * recently-failed lookups with a TTL: hits within the TTL short-circuit
 * find_command_in_path() to return NULL without walking PATH again.
 *
 * Sizing: 32 entries is enough to cover typical interactive churn
 * (compose-pipelines, tab-completion misses, build scripts probing for
 * optional tools). The FIFO replacement policy is simpler than true LRU
 * and still effective for the loop pattern that motivated the cache.
 *
 * TTL is configurable via behavior.path_negative_cache_ttl_ms (default
 * 1000ms). Setting the TTL to 0 disables the cache; this matches POSIX
 * and bash/zsh which have no negative cache. The default is short
 * enough that newly installed binaries appear within a second, long
 * enough that a tight loop's lookups become O(1) instead of
 * O(PATH_dirs).
 * ============================================================================
 */

#define PATH_NEG_CACHE_SIZE 32
#define PATH_NEG_CACHE_NAME_MAX 256

typedef struct {
    char name[PATH_NEG_CACHE_NAME_MAX];
    struct timespec timestamp;
    bool valid;
} path_neg_cache_entry_t;

static path_neg_cache_entry_t path_neg_cache[PATH_NEG_CACHE_SIZE];
static size_t path_neg_cache_next; /// FIFO insertion index

static int64_t timespec_diff_ms(const struct timespec *later,
                                const struct timespec *earlier) {
    int64_t sec_diff = (int64_t)later->tv_sec - (int64_t)earlier->tv_sec;
    int64_t nsec_diff = (int64_t)later->tv_nsec - (int64_t)earlier->tv_nsec;
    return sec_diff * 1000 + nsec_diff / 1000000;
}

/* Returns true if `command` is in the negative cache and the entry is
 * still within TTL. Stale entries are invalidated as a side effect. */
static bool path_neg_cache_check(const char *command) {
    int ttl_ms = config.path_negative_cache_ttl_ms;
    if (ttl_ms <= 0) {
        return false; /// disabled
    }
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return false; /// defensive: treat clock failure as "not cached"
    }
    for (size_t i = 0; i < PATH_NEG_CACHE_SIZE; i++) {
        if (!path_neg_cache[i].valid) {
            continue;
        }
        if (strcmp(path_neg_cache[i].name, command) == 0) {
            if (timespec_diff_ms(&now, &path_neg_cache[i].timestamp) <=
                ttl_ms) {
                return true;
            }
            path_neg_cache[i].valid = false; /// stale
            return false;
        }
    }
    return false;
}

/* Record `command` as recently failed to resolve. FIFO replacement
 * overwrites the oldest entry when the cache is full. */
static void path_neg_cache_insert(const char *command) {
    int ttl_ms = config.path_negative_cache_ttl_ms;
    if (ttl_ms <= 0) {
        return;
    }
    if (strlen(command) >= PATH_NEG_CACHE_NAME_MAX) {
        return; /// too long to cache; PATH walk will retry next time
    }
    size_t idx = path_neg_cache_next;
    path_neg_cache_next = (path_neg_cache_next + 1) % PATH_NEG_CACHE_SIZE;
    strncpy(path_neg_cache[idx].name, command, PATH_NEG_CACHE_NAME_MAX - 1);
    path_neg_cache[idx].name[PATH_NEG_CACHE_NAME_MAX - 1] = '\0';
    if (clock_gettime(CLOCK_MONOTONIC, &path_neg_cache[idx].timestamp) != 0) {
        path_neg_cache[idx].valid = false;
        return;
    }
    path_neg_cache[idx].valid = true;
}

void path_negative_cache_clear(void) {
    for (size_t i = 0; i < PATH_NEG_CACHE_SIZE; i++) {
        path_neg_cache[i].valid = false;
    }
    path_neg_cache_next = 0;
}

/* ============================================================================
 * Builtin Registry
 * ============================================================================
 */

builtin builtins[] = {
    {        "exit",                                               "exit shell",bin_exit                                                                                },
    {      "logout",                                       "exit a login shell",       bin_logout},
    {        "help",                                             "builtin help",         bin_help},
    {          "cd",                                         "change directory",           bin_cd},
    {         "pwd",                                  "print working directory",          bin_pwd},
    {     "history",                                    "print command history",      bin_history},
    {          "fc",                    "fix command (POSIX history edit/list)",           bin_fc},
    {       "alias",                                             "set an alias",        bin_alias},
    {     "unalias",                                           "unset an alias",      bin_unalias},
    {       "clear",                                         "clear the screen",        bin_clear},
    {    "terminal",                             "display terminal information",     bin_terminal},
    {        "type",                                     "display command type",         bin_type},
    {       "unset",                                   "unset a shell variable",        bin_unset},
    {        "echo",                                      "echo text to stdout",         bin_echo},
    {       "print",                      "zsh-style print (-l/-n/-r/-u/-f/-P)",        bin_print},
    {      "printf",                                         "formatted output",       bin_printf},
    {      "export",                                   "export shell variables",       bin_export},
    {      "source",                                          "source a script",       bin_source},
    {           ".",                                          "source a script",       bin_source},
    {        "test",                                         "test expressions",         bin_test},
    {           "[",                                         "test expressions",         bin_test},
    {        "read",                                          "read user input",         bin_read},
    {        "eval",                                       "evaluate arguments",         bin_eval},
    {        "true",                                    "return success status",         bin_true},
    {       "false",                                    "return failure status",        bin_false},
    {         "set",                                        "set shell options",          bin_set},
    {        "jobs",                                         "list active jobs",         bin_jobs},
    {          "fg",                                  "bring job to foreground",           bin_fg},
    {          "bg",                                   "send job to background",           bin_bg},
    {      "disown",     "remove jobs from shell or mark to not receive SIGHUP",
     bin_disown                                                                                  },
    {       "shift",                              "shift positional parameters",        bin_shift},
    {       "break",                                       "break out of loops",        bin_break},
    {    "continue",                          "continue to next loop iteration",     bin_continue},
    {      "return",                                    "return from functions",       bin_return},
    {        "trap",                                      "set signal handlers",         bin_trap},
    {        "exec",                               "replace shell with command",         bin_exec},
    {        "wait",                                 "wait for background jobs",         bin_wait},
    {       "umask",                           "set/display file creation mask",        bin_umask},
    {      "ulimit",                              "set/display resource limits",       bin_ulimit},
    {       "times",                                    "display process times",        bin_times},
    {     "getopts",                                    "parse command options",      bin_getopts},
    {       "local",                                  "declare local variables",        bin_local},
    {     "declare",                        "declare variables with attributes",      bin_declare},
    {     "typeset",                        "declare variables with attributes",      bin_declare},
    {         "let",                          "evaluate arithmetic expressions",          bin_let},
    {           ":",                                     "null command (no-op)",        bin_colon},
    {    "readonly",                               "create read-only variables",     bin_readonly},
    {      "config",                               "manage shell configuration",       bin_config},
    {      "setopt",                            "enable shell options/features",       bin_setopt},
    {    "unsetopt",                           "disable shell options/features",     bin_unsetopt},
    {       "shopt",                                 "bash-style shell options",        bin_shopt},
    {        "mode",                          "select active shell mode preset",         bin_mode},
    {        "hash",                               "remember utility locations",         bin_hash},
    {     "display",                            "manage layered display system",      bin_display},
    {     "network",                             "manage network and SSH hosts",      bin_network},
    {       "debug",                         "advanced debugging and profiling",        bin_debug},
    {     "command",               "execute command bypassing builtins/aliases",      bin_command},
    {       "pushd",                                "push directory onto stack",        bin_pushd},
    {        "popd",                                 "pop directory from stack",         bin_popd},
    {        "dirs",                                  "display directory stack",         bin_dirs},
    {     "mapfile",                         "read lines from stdin into array",      bin_mapfile},
    {   "readarray",                         "read lines from stdin into array",      bin_mapfile},
    {         "env",                    "run command with modified environment",          bin_env},
    {    "printenv",                              "print environment variables",          bin_env},
    {     "analyze",     "full script analysis with info, warnings, and errors",
     bin_analyze                                                                                 },
    {        "lint",        "lint scripts and optionally apply automatic fixes",         bin_lint},
    /// zsh-compatibility stubs (no-op; see bin_zsh_stubs.c)
    {     "bindkey",                "zsh key-binding (records + routes to LLE)",      bin_bindkey},
    {    "autoload",                "zsh lazy function loader (fpath-resolved)",     bin_autoload},
    {    "zmodload",                           "zsh module loader (no-op stub)",     bin_zmodload},
    {     "emulate", "zsh emulation switch (no-op when target == current mode)",
     bin_emulate                                                                                 },
    {      "colors",                           "zsh colors helper (no-op stub)",       bin_colors},
    {         "zle",            "zsh line-editor widget registration / listing",          bin_zle},
    /// bash-completion compatibility (no-op stubs; full record-and-query
    /// implementation tracked in CORPUS_PUNCH_LIST.md)
    {    "complete",                "bash completion registration (no-op stub)",     bin_complete},
    {     "compgen",         "bash completion candidate generator (no-op stub)",
     bin_compgen                                                                                 },
    {     "compopt",              "bash completion option mutator (no-op stub)",      bin_compopt},
    {      "zstyle",                    "zsh pattern-based style configuration",       bin_zstyle},
    {    "compinit",                         "zsh completion init (no-op stub)",     bin_compinit},
    {"bashcompinit",                    "zsh bash-completion init (no-op stub)", bin_bashcompinit},
    {  "unfunction",                               "zsh remove shell functions",   bin_unfunction},
};

const size_t builtins_count = sizeof(builtins) / sizeof(builtins[0]);

/* ============================================================================
 * Registry Lookup
 * ============================================================================
 */

bool is_builtin(const char *name) {
    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(name, builtins[i].name) == 0) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * `set` Wrapper
 *
 * The actual `set` builtin lives in src/posix_opts.c (`builtin_set`).
 * This wrapper exists so the registry table above can dispatch to it
 * via the same int(int, char **) signature as every other builtin.
 * ============================================================================
 */

int bin_set(int argc, char **argv) {
    (void)argc;
    return builtin_set(argv);
}

/* ============================================================================
 * Source-Location Stash for Builtin Error Reporting
 *
 * Per-call stash set by execute_builtin_command in executor.c via
 * builtin_swap_source_location() before dispatching to the builtin
 * function. Tracks the source location of the command node that invoked
 * the builtin -- i.e. the actual call site, not the enclosing
 * control-flow construct. Restored by the dispatcher after the builtin
 * returns. For nested builtin invocations (e.g. `eval` calling another
 * builtin) the dispatcher saves the previous stash on the C stack and
 * restores it on return, so the location is always correct for the
 * innermost builtin currently executing.
 * ============================================================================
 */

static source_location_t s_builtin_call_loc = SOURCE_LOC_UNKNOWN;

source_location_t builtin_swap_source_location(source_location_t loc) {
    source_location_t prev = s_builtin_call_loc;
    s_builtin_call_loc = loc;
    return prev;
}

source_location_t builtin_get_source_location(void) {
    if (SOURCE_LOC_VALID(s_builtin_call_loc) ||
        s_builtin_call_loc.filename != NULL) {
        return s_builtin_call_loc;
    }
    if (current_executor && current_executor->context_depth > 0) {
        return current_executor
            ->context_locations[current_executor->context_depth - 1];
    }
    return SOURCE_LOC_UNKNOWN;
}

/* ============================================================================
 * Shared Helper: identifier validation
 *
 * Every builtin that accepts identifier arguments (declare, local, export,
 * readonly, unset) needs the same validation. Promoted to a public helper
 * (declared in include/builtins.h) so each per-builtin file can call it
 * without duplicating the predicate.
 * ============================================================================
 */

int is_valid_identifier(const char *name) {
    /// Single source of truth for "is this a valid identifier?" is
    /// lush_is_valid_identifier in src/identifier.c, which honors
    /// FEATURE_UNICODE_IDENTIFIERS and NFC-canonicalizes the input
    /// internally so NFD-encoded names validate on equal terms with
    /// the NFC equivalent. This wrapper keeps the int-returning C
    /// API the existing callers (declare, local, export, readonly,
    /// unset) already use.
    return lush_is_valid_identifier(name) ? 1 : 0;
}

/* ============================================================================
 * Shared loop-control implementation
 *
 * `break` and `continue` are 95% identical -- same out-of-loop guard,
 * same numeric-argument parse, same nesting-depth check, same structured-
 * error machinery, differing only in the verb in the error messages /
 * suggestions and the loop_control value they set. bin_break and
 * bin_continue both reduce to one call into this helper.
 * ============================================================================
 */

int builtin_loop_control(int argc, char **argv, const char *verb,
                         loop_control_t ctl) {
    if (!current_executor || current_executor->loop_depth <= 0) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_LOOP_CONTROL, SHELL_SEVERITY_ERROR,
                               loc, "not currently in a loop");
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            char suggestion[96];
            snprintf(suggestion, sizeof(suggestion),
                     "%s is only valid inside while/until/for loops", verb);
            shell_error_set_suggestion(err, suggestion);
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        } else {
            fprintf(stderr, "lush: %s: not currently in a loop\n", verb);
        }
        return 1;
    }

    int level = 1;
    if (argc > 1) {
        char *endptr;
        level = (int)strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || level <= 0) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%s: numeric argument required", argv[1]);
            if (err) {
                if (SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
                shell_error_set_suggestion(err,
                                           "level must be a positive integer");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr, "lush: %s: %s: numeric argument required\n",
                        verb, argv[1]);
            }
            return 1;
        }

        if (level > current_executor->loop_depth) {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "%d: cannot %s %d levels (only %d nested)", level, verb, level,
                current_executor->loop_depth);
            if (err) {
                if (SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
                shell_error_set_suggestion(
                    err, "level cannot exceed the current loop nesting depth");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr,
                        "lush: %s: %d: cannot %s %d levels (only %d nested)\n",
                        verb, level, verb, level, current_executor->loop_depth);
            }
            return 1;
        }
    }

    current_executor->loop_control = ctl;
    current_executor->loop_control_level = level;
    return 0;
}

/* ============================================================================
 * Command Hash Table
 *
 * Owned here so it survives across all bin_<name>.c translation units.
 * bin_hash.c reads/writes via `extern ht_strstr_t *command_hash`.
 * ============================================================================
 */

void init_command_hash(void) {
    if (command_hash == NULL) {
        command_hash = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
    }
}

void free_command_hash(void) {
    if (command_hash != NULL) {
        ht_strstr_destroy(command_hash);
        command_hash = NULL;
    }
}

/* ============================================================================
 * PATH Search
 *
 * Used by various builtins (command -v, type, hash, exec) and by the
 * executor's external-command resolver. Stays here as a shared utility.
 * ============================================================================
 */

char *find_command_in_path(const char *command) {
    if (!command || command[0] == '\0') {
        return NULL;
    }

    /// If command contains slash, check if it exists as-is.
    if (strchr(command, '/')) {
        if (access(command, F_OK) == 0) {
            return strdup(command);
        }
        return NULL;
    }

    /// Positive cache: command_hash holds previously-resolved paths
    /// (also populated by the POSIX `hash` builtin). On hit, revalidate
    /// with access(X_OK) so a removed binary doesn't keep a stale path
    /// pinned -- on revalidation failure, drop the stale entry and fall
    /// through to a fresh PATH walk.
    if (command_hash) {
        const char *cached = ht_strstr_get(command_hash, command);
        if (cached) {
            if (access(cached, X_OK) == 0) {
                return strdup(cached);
            }
            ht_strstr_remove(command_hash, command);
        }
    }

    /// Negative cache: short-circuit a recent miss within TTL so a tight
    /// loop calling a missing command pays O(1) instead of O(PATH_dirs)
    /// per iteration.
    if (path_neg_cache_check(command)) {
        return NULL;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) {
        path_neg_cache_insert(command);
        return NULL;
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
        return NULL;
    }

    char *path_dir = strtok(path_copy, ":");
    char *result = NULL;

    while (path_dir) {
        size_t dir_len = strlen(path_dir);
        size_t cmd_len = strlen(command);
        char *full_path = malloc(dir_len + cmd_len + 2); /// +'/' + '\0'

        if (full_path) {
            snprintf(full_path, dir_len + cmd_len + 2, "%s/%s", path_dir,
                     command);

            if (access(full_path, X_OK) == 0) {
                result = full_path;
                break;
            }

            free(full_path);
        }

        path_dir = strtok(NULL, ":");
    }

    free(path_copy);

    if (result) {
        /// Populate positive cache. POSIX hash table doubles as the
        /// positive cache; the `hash` builtin shows / clears it.
        if (!command_hash) {
            init_command_hash();
        }
        if (command_hash) {
            ht_strstr_insert(command_hash, command, result);
        }
    } else {
        /// Populate negative cache so the next call within TTL is O(1).
        path_neg_cache_insert(command);
    }
    return result;
}
