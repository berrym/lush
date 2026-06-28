/**
 * @file posix_opts.c
 * @brief POSIX shell options management
 *
 * Implements POSIX-compliant shell options including:
 * - Option initialization with sensible defaults
 * - Option query functions (errexit, xtrace, etc.)
 * - The 'set' builtin command for runtime option control
 * - Named option mapping (-o optname / +o optname)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config.h"
#include "config_registry.h"
#include "errors.h"
#include "executor.h"
#include "init.h"
#include "lle/lle_pager.h"
#include "lle/lle_shell_integration.h"
#include "lush.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Forward declare POSIX strcasecmp: the project's own include/strings.h
/// header (legacy lush utilities) shadows the system <strings.h>, so a
/// straight #include cannot pull in the BSD declaration.
extern int strcasecmp(const char *s1, const char *s2);

/** @brief Global shell options instance */
shell_options_t shell_opts = {0};

/// Apply a mode to the RUNTIME: drop per-feature overrides, update the legacy
/// posix_mode mirror and the config.shell_mode mirror, and (once the registry
/// exists) re-seed the per-mode option and feature defaults and push them into
/// the runtime struct. Deliberately does NOT write shell.mode into the registry
/// or call config_apply_settings: the registry is the source of truth and this
/// is the downstream application, so the shell.mode subscriber and the
/// pre-registry bootstrap both call it with no write-back recursion. The
/// explicit-change path (apply_mode_preset) runs config_apply_settings after;
/// startup runs it once at the end of config_init.
static void shell_mode_runtime_apply(shell_mode_t mode) {
    /// Mode change is a clean re-seed: drop any per-feature overrides so the
    /// new mode's matrix defaults take effect. Picking a preset means asking.
    shell_feature_reset_all();

    /// posix_mode is the executor's posix-strictness field (is_posix_mode_-
    /// enabled, read in parser/executor hot paths). posix-strictness is purely
    /// (mode == POSIX); this mirror keeps the field in step on the mode-preset
    /// path and is its writer. shell.posix is a derive-only registry key
    /// (persisted=false, no per-mode default, never SESSION-written), and
    /// config get/show read this same posix_mode field, so the config surface
    /// always equals what the executor does. LOAD-BEARING -- do not remove this
    /// line.
    shell_opts.posix_mode = (mode == SHELL_MODE_POSIX);

    config.shell_mode = (int)mode;

    if (config_registry_is_initialized()) {
        /// Re-seed registered per-mode option defaults (re-seed-every-time:
        /// a mode change overwrites mode-aware tweaks; picking a preset means
        /// asking for it).
        config_registry_apply_mode_defaults(mode);

        /// Re-seed the feature matrix's MODE layer after the generic overlay
        /// (apply_mode_defaults clears every MODE slot) so config get / show
        /// resolve the new mode's features.
        shell_seed_feature_modes(mode);

        /// Push the re-seeded defaults into the runtime struct so mode-aware
        /// options (completion.match_mode, history.search_mode, ...) change
        /// with the mode rather than stranding the previous mode's value.
        config_registry_sync_to_runtime();
    }
}

/// Subscriber: a registry shell.mode change reconciles the runtime. Every
/// surface (CLI flag, lushrc, the mode builtin, set -o posix) writes the
/// appropriate layer; the resolved effective value is applied here exactly once
/// per change. No write-back -- shell_mode_runtime_apply does not touch
/// shell.mode -- so there is no recursion.
static void shell_mode_registry_apply(const char *key,
                                      const creg_value_t *old_value,
                                      const creg_value_t *new_value,
                                      void *user_data) {
    (void)key;
    (void)old_value;
    (void)user_data;
    if (!new_value || new_value->type != CREG_VALUE_STRING) {
        return;
    }
    shell_mode_t parsed;
    if (!shell_mode_parse(new_value->data.string, &parsed)) {
        parsed = SHELL_MODE_LUSH;
    }
    /// shell_mode_set honors the strict-mode lock; apply only if accepted.
    if (shell_mode_set(parsed)) {
        shell_mode_runtime_apply(parsed);
    }
}

void shell_mode_register_runtime_subscriber(void) {
    config_registry_subscribe("shell.mode", shell_mode_registry_apply, NULL);
}

/// Canonical entry point for an explicit mode selection (the mode builtin,
/// set -o posix, config set shell.mode, the startup bootstrap). Records the
/// choice in the registry SESSION layer for provenance and persistence, applies
/// the preset to the runtime unconditionally (drops overrides, re-seeds), and
/// propagates via config_apply_settings. Returns false if the strict-mode lock
/// forbids the change.
bool apply_mode_preset(shell_mode_t mode) {
    if (!shell_mode_set(mode)) {
        return false;
    }

    /// Record the explicit choice in the SESSION layer (highest precedence) for
    /// provenance and persistence. This fires the shell.mode subscriber only on
    /// a value change.
    if (config_registry_is_initialized()) {
        config_registry_set_string("shell.mode", shell_mode_name(mode));
    }

    /// Apply unconditionally. An explicit preset selection must drop
    /// per-feature overrides and re-seed even when the effective mode is
    /// unchanged (mode
    /// --reset, set -o posix while already posix); the change-gated subscriber
    /// would skip those. shell_mode_runtime_apply is idempotent, so the
    /// redundant application when the subscriber also fires on a real change is
    /// harmless. (Picking a preset means asking for it.)
    shell_mode_runtime_apply(mode);

    /// Propagate to the rest of the shell, as the prior apply_mode_preset did
    /// (only meaningful once the registry/runtime exist).
    if (config_registry_is_initialized()) {
        config_apply_settings();
    }

    return true;
}

/**
 * @brief Sync a shell option to the config registry
 *
 * Updates both config system and registry when a shell option changes.
 * Called after each option is set/unset via set -o / +o commands.
 *
 * @param name The option name (e.g., "errexit")
 * @param value The new boolean value
 */
static void sync_shell_option_to_registry(const char *name, bool value) {
    /// Build the full key: shell.<name>
    char key[CREG_KEY_MAX];
    snprintf(key, sizeof(key), "shell.%s", name);

    /// Update config system's shell option
    config_set_shell_option(key, value);

    /// Update registry if initialized
    if (config_registry_is_initialized()) {
        config_registry_set_boolean(key, value);
    }
}

/// Maps each registry shell.<name> boolean key to its shell_opts field. The
/// single source for the key<->field correspondence used by argv hydration;
/// mirrors config_set_shell_option's strcmp ladder (a future cleanup folds both
/// onto this table).
static const struct {
    const char *key;
    size_t offset;
} k_shell_bool_opts[] = {
    {             "shell.errexit",offsetof(shell_options_t,             exit_on_error)                                  },
    {              "shell.xtrace", offsetof(shell_options_t,           trace_execution)},
    {              "shell.noexec", offsetof(shell_options_t,              syntax_check)},
    {             "shell.nounset", offsetof(shell_options_t,               unset_error)},
    {             "shell.verbose", offsetof(shell_options_t,                   verbose)},
    {              "shell.noglob", offsetof(shell_options_t,               no_globbing)},
    {             "shell.hashall", offsetof(shell_options_t,             hash_commands)},
    {             "shell.monitor", offsetof(shell_options_t,               job_control)},
    {           "shell.allexport", offsetof(shell_options_t,                 allexport)},
    {           "shell.noclobber", offsetof(shell_options_t,                 noclobber)},
    {              "shell.onecmd", offsetof(shell_options_t,                    onecmd)},
    {              "shell.notify", offsetof(shell_options_t,                    notify)},
    {           "shell.ignoreeof", offsetof(shell_options_t,                 ignoreeof)},
    {               "shell.nolog", offsetof(shell_options_t,                     nolog)},
    {               "shell.emacs", offsetof(shell_options_t,                emacs_mode)},
    {                  "shell.vi", offsetof(shell_options_t,                   vi_mode)},
    {               "shell.posix", offsetof(shell_options_t,                posix_mode)},
    {            "shell.pipefail", offsetof(shell_options_t,             pipefail_mode)},
    {          "shell.histexpand", offsetof(shell_options_t,           histexpand_mode)},
    {             "shell.history", offsetof(shell_options_t,              history_mode)},
    {"shell.interactive-comments",
     offsetof(shell_options_t, interactive_comments_mode)                              },
    {            "shell.physical", offsetof(shell_options_t,             physical_mode)},
    {          "shell.privileged", offsetof(shell_options_t,           privileged_mode)},
    {            "shell.errtrace", offsetof(shell_options_t,                  errtrace)},
    {           "shell.functrace", offsetof(shell_options_t,                 functrace)},
    { "shell.pipeline-diagnostic",
     offsetof(shell_options_t,  pipeline_diagnostic_mode)                              },
};
#define K_SHELL_BOOL_OPT_COUNT                                                 \
    (sizeof(k_shell_bool_opts) / sizeof(k_shell_bool_opts[0]))

static inline bool shell_opt_field(const shell_options_t *opts, size_t offset) {
    return *(const bool *)((const char *)opts + offset);
}

/// Snapshot of shell_opts taken right after init_posix_options, before
/// parse_opts applies argv flags. The capture step diffs against it to find
/// exactly which options the command line set.
static shell_options_t g_shell_opts_baseline;
static bool g_shell_opts_baseline_recorded = false;

/// The argv-set option keys captured at the clean window, replayed into the
/// registry SESSION layer once it exists.
static const char *g_argv_override_keys[K_SHELL_BOOL_OPT_COUNT];
static bool g_argv_override_values[K_SHELL_BOOL_OPT_COUNT];
static size_t g_argv_override_count = 0;

void shell_opts_record_baseline(void) {
    g_shell_opts_baseline = shell_opts;
    g_shell_opts_baseline_recorded = true;
}

void shell_opts_capture_argv_overrides(void) {
    g_argv_override_count = 0;
    if (!g_shell_opts_baseline_recorded) {
        return;
    }
    /// Diff after parse_opts, before apply_mode_preset: shell_opts holds the
    /// init defaults plus argv-set values only. Capturing here (not after
    /// config_init) excludes mode-driven changes -- notably posix_mode, which
    /// apply_mode_preset sets from the active mode -- so only explicit command
    /// line flags become SESSION overrides.
    for (size_t i = 0; i < K_SHELL_BOOL_OPT_COUNT; i++) {
        size_t off = k_shell_bool_opts[i].offset;
        bool now = shell_opt_field(&shell_opts, off);
        if (now != shell_opt_field(&g_shell_opts_baseline, off)) {
            g_argv_override_keys[g_argv_override_count] =
                k_shell_bool_opts[i].key;
            g_argv_override_values[g_argv_override_count] = now;
            g_argv_override_count++;
        }
    }
}

void shell_opts_hydrate_argv_to_registry(void) {
    if (!config_registry_is_initialized()) {
        return;
    }
    /// Replay the captured argv flags into the SESSION layer -- the highest
    /// precedence -- so a command line flag wins over a lushrc (USER) value and
    /// over a mode preset (MODE), and config get / show / explain report the
    /// option's true source. The shell.* subscriber write-throughs each value
    /// back onto shell_opts (a no-op here since argv already set it).
    for (size_t i = 0; i < g_argv_override_count; i++) {
        config_registry_set_boolean(g_argv_override_keys[i],
                                    g_argv_override_values[i]);
    }
}

/**
 * @brief Initialize POSIX shell options with defaults
 *
 * Sets all shell options to their default values. Called during
 * shell initialization before command line parsing.
 */
void init_posix_options(void) {
    /// Set default values
    shell_opts.command_mode = false;
    shell_opts.command_string = NULL;
    shell_opts.stdin_mode = false;
    shell_opts.interactive = false;
    shell_opts.login_shell = false;
    shell_opts.exit_on_error = false;
    shell_opts.trace_execution = false;
    shell_opts.syntax_check = false;
    shell_opts.unset_error = false;
    shell_opts.verbose = false;
    shell_opts.no_globbing = false;
    shell_opts.hash_commands = true; /// Default enabled for performance
    shell_opts.job_control = false;
    shell_opts.allexport = false;
    shell_opts.noclobber = false;
    shell_opts.onecmd = false;
    shell_opts.notify = false;
    shell_opts.ignoreeof = false;
    shell_opts.nolog = false;
    shell_opts.emacs_mode = true; /// Default to emacs mode
    shell_opts.vi_mode = false;   /// Default to emacs mode, not vi
    shell_opts.posix_mode =
        false; /// Default to non-strict mode for compatibility
    shell_opts.pipefail_mode = false; /// Default to standard pipeline behavior
    shell_opts.pipeline_diagnostic_mode =
        false;                         /// Off by default in posix/bash;
                                       /// lush mode enables it
    shell_opts.histexpand_mode = true; /// Default to history expansion enabled
    shell_opts.history_mode =
        true; /// Default to command history recording enabled
    shell_opts.interactive_comments_mode =
        true; /// Default to interactive comments enabled
    shell_opts.physical_mode = false;   /// Default to logical directory paths
    shell_opts.privileged_mode = false; /// Default to unrestricted mode
    shell_opts.restricted_mode = false; /// `-r`: requested at invocation
    shell_opts.restricted_mode_engaged = false; /// engaged after rc files
}

/**
 * @brief Check if a specific POSIX option is set
 *
 * @param option Single character option flag (e.g., 'e', 'x', 'n')
 * @return true if the option is enabled, false otherwise
 */
bool is_posix_option_set(char option) {
    switch (option) {
    case 'c':
        return shell_opts.command_mode;
    case 's':
        return shell_opts.stdin_mode;
    case 'i':
        return shell_opts.interactive;
    case 'l':
        return shell_opts.login_shell;
    case 'e':
        return shell_opts.exit_on_error;
    case 'x':
        return shell_opts.trace_execution;
    case 'n':
        return shell_opts.syntax_check;
    case 'u':
        return shell_opts.unset_error;
    case 'v':
        return shell_opts.verbose;
    case 'f':
        return shell_opts.no_globbing;
    case 'h':
        return shell_opts.hash_commands;
    case 'm':
        return shell_opts.job_control;
    case 'a':
        return shell_opts.allexport;
    case 'C':
        return shell_opts.noclobber;
    case 't':
        return shell_opts.onecmd;
    case 'b':
        return shell_opts.notify;
    case 'r':
        return shell_opts.restricted_mode;
    default:
        return false;
    }
}

/** @brief Check if errexit (-e) is enabled */
bool should_exit_on_error(void) { return shell_opts.exit_on_error; }

/** @brief Check if xtrace (-x) is enabled */
bool should_trace_execution(void) { return shell_opts.trace_execution; }

/** @brief Check if noexec (-n) syntax check mode is enabled */
bool is_syntax_check_mode(void) { return shell_opts.syntax_check; }

/** @brief Check if nounset (-u) is enabled */
bool should_error_unset_vars(void) { return shell_opts.unset_error; }

/** @brief Check if verbose (-v) mode is enabled */
bool is_verbose_mode(void) { return shell_opts.verbose; }

/** @brief Check if noglob (-f) is enabled */
bool is_globbing_disabled(void) { return shell_opts.no_globbing; }

/** @brief Check if allexport (-a) is enabled */
bool should_auto_export(void) { return shell_opts.allexport; }

/** @brief Check if noclobber (-C) is enabled */
bool is_noclobber_enabled(void) { return shell_opts.noclobber; }

/** @brief Check if ignoreeof is enabled */
bool is_ignoreeof_enabled(void) { return shell_opts.ignoreeof; }

/** @brief Check if nolog is enabled */
bool is_nolog_enabled(void) { return shell_opts.nolog; }

/** @brief Check if emacs editing mode is enabled */
bool is_emacs_mode_enabled(void) { return shell_opts.emacs_mode; }

/** @brief Check if vi editing mode is enabled */
bool is_vi_mode_enabled(void) { return shell_opts.vi_mode; }

/** @brief Check if strict POSIX mode is enabled */
bool is_posix_mode_enabled(void) { return shell_opts.posix_mode; }

/** @brief Check if pipefail is enabled */
bool is_pipefail_enabled(void) { return shell_opts.pipefail_mode; }

/** @brief Check if pipeline-diagnostic mode is enabled */
bool is_pipeline_diagnostic_enabled(void) {
    return shell_opts.pipeline_diagnostic_mode;
}

/** @brief Check if history expansion (!!) is enabled */
bool is_histexpand_enabled(void) { return shell_opts.histexpand_mode; }

/** @brief Check if command history recording is enabled */
bool is_history_enabled(void) { return shell_opts.history_mode; }

/** @brief Check if interactive comments (#) are enabled */
bool is_interactive_comments_enabled(void) {
    return shell_opts.interactive_comments_mode;
}

/**
 * @brief Print command trace for -x option
 *
 * When xtrace is enabled, prints each command before execution
 * prefixed with "+ ".
 *
 * @param command Command string to trace
 */
void print_command_trace(const char *command) {
    if (should_trace_execution()) {
        fprintf(stderr, "+ %s\n", command);
        fflush(stderr);
    }
}

/**
 * @brief Named option mapping structure
 *
 * Maps long option names to their flag pointers and short option characters.
 */
typedef struct option_mapping {
    const char *name; ///< Long option name (e.g., "errexit")
    bool *flag;       ///< Pointer to the option flag
    char short_opt;   ///< Short option character (e.g., 'e'), 0 if none
} option_mapping_t;

/** @brief Map of option names to flags */
static option_mapping_t option_map[] = {
    {             "errexit",             &shell_opts.exit_on_error, 'e'},
    {              "xtrace",           &shell_opts.trace_execution, 'x'},
    {              "noexec",              &shell_opts.syntax_check, 'n'},
    {             "nounset",               &shell_opts.unset_error, 'u'},
    {             "verbose",                   &shell_opts.verbose, 'v'},
    {              "noglob",               &shell_opts.no_globbing, 'f'},
    {             "hashall",             &shell_opts.hash_commands, 'h'},
    {             "monitor",               &shell_opts.job_control, 'm'},
    {           "allexport",                 &shell_opts.allexport, 'a'},
    {           "noclobber",                 &shell_opts.noclobber, 'C'},
    {              "onecmd",                    &shell_opts.onecmd, 't'},
    {              "notify",                    &shell_opts.notify, 'b'},
    {           "ignoreeof",                 &shell_opts.ignoreeof,   0},
    {               "nolog",                     &shell_opts.nolog,   0},
    {            "errtrace",                  &shell_opts.errtrace, 'E'},
    {           "functrace",                 &shell_opts.functrace, 'T'},
    {               "emacs",                &shell_opts.emacs_mode,   0},
    {                  "vi",                   &shell_opts.vi_mode,   0},
    {               "posix",                &shell_opts.posix_mode,   0},
    {            "pipefail",             &shell_opts.pipefail_mode,   0},
    { "pipeline-diagnostic",  &shell_opts.pipeline_diagnostic_mode,   0},
    {          "histexpand",           &shell_opts.histexpand_mode,   0},
    {             "history",              &shell_opts.history_mode,   0},
    {"interactive-comments", &shell_opts.interactive_comments_mode,   0},
    {            "physical",             &shell_opts.physical_mode,   0},
    {          "privileged",           &shell_opts.privileged_mode,   0},
    {          "restricted",           &shell_opts.restricted_mode, 'r'},
    {                  NULL,                                  NULL,   0}
};

/**
 * @brief Find option mapping by long name
 *
 * @param name Long option name to search for
 * @return Pointer to option mapping, or NULL if not found
 */
static option_mapping_t *find_option_by_name(const char *name) {
    for (int i = 0; option_map[i].name; i++) {
        if (strcmp(option_map[i].name, name) == 0) {
            return &option_map[i];
        }
    }
    return NULL;
}

/**
 * @brief Query the boolean state of a named shell option for `[[ -o name ]]`.
 *
 * Consults sources in order:
 *   1. The interactive-shell pseudo-option (`interactive` is not in
 *      option_map; resolved via is_interactive_shell()).
 *   2. The POSIX option_map (errexit, xtrace, nounset, etc.).
 *   3. The feature-matrix names + aliases (FEATURE_INDEXED_ARRAYS etc.,
 *      and short aliases like `extglob`, `nullglob`).
 *   4. The noop-alias recorded-state table (prompt_subst, menu_complete,
 *      etc. -- behavior is always-on but introspection sees what the
 *      user set/unset).
 *
 * Returns false for any name not matched by any of the four sources.
 *
 * @param name Long option name (case-insensitive for the noop-alias and
 *             feature-matrix layers; case-sensitive for option_map names
 *             to match `set -o` exact-match semantics).
 * @return true if the option is on, false otherwise (including "unknown").
 */
bool shell_is_option_set(const char *name) {
    if (!name || !*name) {
        return false;
    }
    /// 1. interactive
    if (strcasecmp(name, "interactive") == 0) {
        return is_interactive_shell();
    }
    /// 2. POSIX option_map (long names like errexit)
    option_mapping_t *opt = find_option_by_name(name);
    if (opt) {
        return *(opt->flag);
    }
    /// 3. feature matrix names + short aliases
    shell_feature_t feature;
    bool invert = false;
    if (shell_feature_parse(name, &feature, &invert)) {
        bool effective = shell_mode_allows(feature);
        return invert ? !effective : effective;
    }
    /// 4. noop-alias recorded state (default-true for any unset entry)
    if (shell_feature_is_noop_alias(name)) {
        return shell_feature_noop_alias_is_enabled(name);
    }
    return false;
}

/**
 * @brief Find option mapping by short option character
 *
 * @param opt Short option character to search for
 * @return Pointer to option mapping, or NULL if not found
 */
static option_mapping_t *find_option_by_short(char opt) {
    for (int i = 0; option_map[i].name; i++) {
        if (option_map[i].short_opt == opt) {
            return &option_map[i];
        }
    }
    return NULL;
}

/**
 * @brief Write a shell variable value to `out` with POSIX quoting
 *
 * Writes into an arbitrary FILE* so the same formatter can target
 * an open_memstream buffer (paginated path) or stdout directly
 * (fallback path), without two copies of the quoting logic.
 *
 * @param out   Destination stream
 * @param key   Variable name
 * @param value Variable value (already extracted, not raw encoded)
 */
static void print_variable_quoted(FILE *out, const char *key,
                                  const char *value) {
    if (!value) {
        fprintf(out, "%s=''\n", key);
        return;
    }

    /// Check if value needs quoting (contains special chars)
    bool needs_quote = false;
    for (const char *p = value; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\'' || *p == '"' ||
            *p == '\\' || *p == '$' || *p == '`' || *p == '!' || *p == '*' ||
            *p == '?' || *p == '[' || *p == ']' || *p == '(' || *p == ')' ||
            *p == '{' || *p == '}' || *p == '|' || *p == '&' || *p == ';' ||
            *p == '<' || *p == '>') {
            needs_quote = true;
            break;
        }
    }

    if (needs_quote) {
        /// Use single quotes, escaping any single quotes in value
        fprintf(out, "%s='", key);
        for (const char *p = value; *p; p++) {
            if (*p == '\'') {
                fputs("'\\''", out); /// End quote, escaped quote, start quote
            } else {
                fputc(*p, out);
            }
        }
        fputs("'\n", out);
    } else {
        fprintf(out, "%s=%s\n", key, value);
    }
}

/**
 * @brief Callback for printing a single shell variable
 *
 * Used by symtable_enumerate_global_vars to write each variable in
 * POSIX `NAME=VALUE` format to the FILE* threaded through `userdata`.
 *
 * @param key      Variable name
 * @param value    Variable value (already clean, no metadata)
 * @param userdata Destination FILE* (must not be NULL)
 */
static void print_variable_callback(const char *key, const char *value,
                                    void *userdata) {
    FILE *out = (FILE *)userdata;
    if (!out || !key) {
        return;
    }

    /// Skip internal/special variables that start with double underscore
    if (key[0] == '_' && key[1] == '_') {
        return;
    }

    print_variable_quoted(out, key, value);
}

/**
 * @brief Print all shell variables (POSIX 'set' with no arguments)
 *
 * Builds the full `NAME=VALUE` listing into an open_memstream heap
 * buffer and hands the buffer to lle_pager_present, so long
 * environments paginate in interactive shells. The pager's decision
 * tree picks between paginating and streaming directly (non-tty,
 * disabled master switch, or content that fits in one screen).
 * On open_memstream allocation failure the function falls back to
 * the prior stdout-streaming path so the listing still surfaces.
 */
static void print_all_shell_variables(void) {
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    if (!out) {
        symtable_enumerate_global_vars(print_variable_callback, stdout);
        return;
    }
    symtable_enumerate_global_vars(print_variable_callback, out);
    fclose(out);
    lle_pager_present(NULL, buf);
    free(buf);
}

/**
 * @brief Implementation of the 'set' builtin command
 *
 * Handles shell option management including:
 * - No args: display all shell variables (POSIX requirement)
 * - -o name: enable named option
 * - +o name: disable named option
 * - -x, -e, etc.: enable short options
 * - +x, +e, etc.: disable short options
 * - --: set positional parameters
 *
 * @param args Argument array (NULL-terminated)
 * @return 0 on success, 1 on error
 */

/**
 * @brief Free the positional-parameter vector at shell exit
 *
 * `set --` replaces shell_argv with a dynamically allocated vector of strdup'd
 * parameters (freeing the previous one each time). Nothing freed the final
 * vector at exit, so the last positional parameters leaked. Registered via
 * atexit; a no-op when shell_argv still points at the process argv.
 */
void free_shell_argv(void) {
    if (shell_argv && shell_argv_is_dynamic) {
        for (int j = 0; j < shell_argc; j++) {
            free(shell_argv[j]);
        }
        free(shell_argv);
        shell_argv = NULL;
        shell_argv_is_dynamic = false;
    }
}
int builtin_set(char **args) {
    /// Privileged mode security check - block all set operations
    if (shell_opts.privileged_mode && args[1]) {
        executor_error_report(current_executor, SHELL_ERR_PERMISSION_DENIED,
                              builtin_get_source_location(),
                              "cannot modify shell options in privileged mode");
        return 1;
    }

    if (!args[1]) {
        /// No arguments - display all shell variables (POSIX requirement)
        /// Variables are printed in format: NAME=VALUE (quoted if needed)
        print_all_shell_variables();
        return 0;
    }

    for (int i = 1; args[i]; i++) {
        char *arg = args[i];

        /// Handle -o and +o options
        if (strcmp(arg, "-o") == 0) {
            if (args[i + 1]) {
                /// Set named option
                i++; /// consume the option name

                /// `set -o posix` is preserved as a recognized
                /// bash-bridge alias for `mode posix`: it's bash's
                /// canonical spelling for entering POSIX mode and a
                /// common bash-script idiom. The lush-native spelling
                /// is `mode posix`; both route through the same
                /// apply_mode_preset() entry point.
                ///
                /// `set -o {bash,zsh,lush}` are no longer recognized:
                /// modes are a discriminated enum, not toggles. Use
                /// the `mode` builtin instead.
                if (strcmp(args[i], "posix") == 0) {
                    if (!apply_mode_preset(SHELL_MODE_POSIX)) {
                        executor_error_report(
                            current_executor, SHELL_ERR_FEATURE_DISABLED,
                            builtin_get_source_location(),
                            "cannot change shell mode (strict mode enabled)");
                        return 1;
                    }
                } else if (strcmp(args[i], "bash") == 0 ||
                           strcmp(args[i], "zsh") == 0 ||
                           strcmp(args[i], "lush") == 0) {
                    executor_error_report(
                        current_executor, SHELL_ERR_INVALID_OPTION,
                        builtin_get_source_location(),
                        "set -o %s: shell modes are not toggles; "
                        "use `mode %s` instead",
                        args[i], args[i]);
                    return 1;
                } else {
                    option_mapping_t *opt = find_option_by_name(args[i]);
                    if (opt) {
                        *(opt->flag) = true;
                        sync_shell_option_to_registry(opt->name, true);
                        /// Handle mutually exclusive editing modes
                        if (strcmp(args[i], "emacs") == 0) {
                            shell_opts.vi_mode =
                                false; /// Disable vi when enabling emacs
                            sync_shell_option_to_registry("vi", false);
                            lush_update_editing_mode();
                        } else if (strcmp(args[i], "vi") == 0) {
                            shell_opts.emacs_mode =
                                false; /// Disable emacs when enabling vi
                            sync_shell_option_to_registry("emacs", false);
                            lush_update_editing_mode();
                        }
                    } else {
                        executor_error_report(
                            current_executor, SHELL_ERR_INVALID_OPTION,
                            builtin_get_source_location(),
                            "invalid option name: %s", args[i]);
                        return 1;
                    }
                }
            } else {
                /// No argument - show all options including shell mode.
                /// Buffered through open_memstream + lle_pager_present so
                /// the ~30-option listing paginates when it overflows a
                /// small terminal; fits-in-one-screen and non-tty cases
                /// stream through unchanged.
                char *opts_buf = NULL;
                size_t opts_len = 0;
                FILE *opts_out = open_memstream(&opts_buf, &opts_len);
                FILE *sink = opts_out ? opts_out : stdout;
                fprintf(sink, "Current shell options:\n");
                for (int j = 0; option_map[j].name; j++) {
                    fprintf(sink, "set %co %s\n",
                            *(option_map[j].flag) ? '-' : '+',
                            option_map[j].name);
                }
                /// Also show current shell mode (use the `mode` builtin to
                /// change it; `set -o posix` is the bash-bridge alias).
                fprintf(sink,
                        "(shell mode: %s -- use `mode` builtin to change)\n",
                        shell_mode_name(shell_mode_get()));
                if (opts_out) {
                    fclose(opts_out);
                    lle_pager_present(NULL, opts_buf);
                    free(opts_buf);
                }
                return 0;
            }
        } else if (strcmp(arg, "+o") == 0) {
            if (args[i + 1]) {
                /// Unset named option
                i++; /// consume the option name

                /// `set +o posix` is the bash-bridge counterpart to
                /// `set -o posix`: it lifts the POSIX preset and
                /// returns to lush. `set +o {bash,zsh,lush}` are
                /// rejected -- modes aren't toggles.
                if (strcmp(args[i], "posix") == 0) {
                    if (!apply_mode_preset(SHELL_MODE_LUSH)) {
                        executor_error_report(
                            current_executor, SHELL_ERR_FEATURE_DISABLED,
                            builtin_get_source_location(),
                            "cannot change shell mode (strict mode enabled)");
                        return 1;
                    }
                } else if (strcmp(args[i], "bash") == 0 ||
                           strcmp(args[i], "zsh") == 0 ||
                           strcmp(args[i], "lush") == 0) {
                    executor_error_report(
                        current_executor, SHELL_ERR_INVALID_OPTION,
                        builtin_get_source_location(),
                        "set +o %s: shell modes are not toggles; "
                        "use `mode <name>` to switch presets",
                        args[i]);
                    return 1;
                } else if (strcmp(args[i], "restricted") == 0 &&
                           shell_opts.restricted_mode_engaged) {
                    /// `set +o restricted` is rejected once `-r` has
                    /// taken effect -- restricted mode is one-way,
                    /// matching bash (rbash) and POSIX 2024 text.
                    executor_error_report(
                        current_executor, SHELL_ERR_PERMISSION_DENIED,
                        builtin_get_source_location(),
                        "set: cannot clear restricted mode once engaged");
                    return 1;
                } else {
                    option_mapping_t *opt = find_option_by_name(args[i]);
                    if (opt) {
                        *(opt->flag) = false;
                        sync_shell_option_to_registry(opt->name, false);
                        /// Handle mutually exclusive editing modes
                        if (strcmp(args[i], "emacs") == 0) {
                            shell_opts.vi_mode =
                                true; /// Enable vi when disabling emacs
                            sync_shell_option_to_registry("vi", true);
                            lush_update_editing_mode();
                        } else if (strcmp(args[i], "vi") == 0) {
                            shell_opts.emacs_mode =
                                true; /// Enable emacs when disabling vi
                            sync_shell_option_to_registry("emacs", true);
                            lush_update_editing_mode();
                        }
                    } else {
                        executor_error_report(
                            current_executor, SHELL_ERR_INVALID_OPTION,
                            builtin_get_source_location(),
                            "invalid option name: %s", args[i]);
                        return 1;
                    }
                }
            } else {
                /// No argument - show all options in +o format (read-only
                /// operation, always allowed)
                printf("Current shell options:\n");
                for (int j = 0; option_map[j].name; j++) {
                    printf("set %co %s\n", *(option_map[j].flag) ? '-' : '+',
                           option_map[j].name);
                }
                /// Also show current shell mode (use the `mode` builtin to
                /// change it; `set -o posix` is the bash-bridge alias).
                printf("(shell mode: %s -- use `mode` builtin to change)\n",
                       shell_mode_name(shell_mode_get()));
                return 0;
            }
        } else if (strcmp(arg, "--") == 0) {
            /// Handle -- option: end of options, start of positional parameters
            i++; /// Move past the --

            /// Clear existing positional parameters $1, $2, etc.
            for (int param_num = 1; param_num <= 99; param_num++) {
                char param_name[4];
                snprintf(param_name, sizeof(param_name), "%d", param_num);
                symtable_unset_global(param_name);
            }

            /// Count how many new parameters we have
            int new_argc = 0;
            int temp_i = i;
            while (args[temp_i]) {
                new_argc++;
                temp_i++;
            }

            /// Free existing shell_argv if it was dynamically allocated
            if (shell_argv && shell_argv_is_dynamic) {
                for (int j = 0; j < shell_argc; j++) {
                    free(shell_argv[j]);
                }
                free(shell_argv);
            }

            /// Allocate new shell_argv (include space for program name)
            shell_argc = new_argc + 1;
            shell_argv = malloc(shell_argc * sizeof(char *));
            if (shell_argv) {
                /// Set program name (shell_argv[0])
                shell_argv[0] = strdup("lush");

                /// Set new positional parameters in both symbol table and
                /// global arrays
                int param_num = 1;
                while (args[i] && param_num <= 99) {
                    char param_name[4];
                    snprintf(param_name, sizeof(param_name), "%d", param_num);
                    symtable_set_global(param_name, args[i]);

                    /// Also update global shell_argv
                    shell_argv[param_num] = strdup(args[i]);

                    i++;
                    param_num++;
                }

                /// Mark shell_argv as dynamically allocated
                shell_argv_is_dynamic = true;
            }

            /// Update $# (number of positional parameters)
            char argc_str[16];
            snprintf(argc_str, sizeof(argc_str), "%d", new_argc);
            symtable_set_global("#", argc_str);

            break; /// Process no more arguments after --
        } else if (arg[0] == '-' && arg[1] != '-') {
            /// Handle short options like -e, -x, etc.
            for (int j = 1; arg[j]; j++) {
                option_mapping_t *opt = find_option_by_short(arg[j]);
                if (opt) {
                    *(opt->flag) = true;
                    sync_shell_option_to_registry(opt->name, true);
                } else {
                    executor_error_report(current_executor,
                                          SHELL_ERR_INVALID_OPTION,
                                          builtin_get_source_location(),
                                          "invalid option: -%c", arg[j]);
                    return 1;
                }
            }
        } else if (arg[0] == '+' && arg[1] != '+') {
            /// Handle short options like +e, +x, etc.
            for (int j = 1; arg[j]; j++) {
                /// Restricted mode is one-way: once `-r` is engaged it
                /// cannot be cleared. Matches bash (rbash refuses
                /// `set +r`) and POSIX 2024 `set -r` text. The check
                /// fires whether the user typed `+r` or `+o restricted`.
                if (arg[j] == 'r' && shell_opts.restricted_mode_engaged) {
                    executor_error_report(
                        current_executor, SHELL_ERR_PERMISSION_DENIED,
                        builtin_get_source_location(),
                        "set: cannot clear restricted mode once engaged");
                    return 1;
                }
                option_mapping_t *opt = find_option_by_short(arg[j]);
                if (opt) {
                    *(opt->flag) = false;
                    sync_shell_option_to_registry(opt->name, false);
                } else {
                    executor_error_report(current_executor,
                                          SHELL_ERR_INVALID_OPTION,
                                          builtin_get_source_location(),
                                          "invalid option: +%c", arg[j]);
                    return 1;
                }
            }
        } else {
            /// Regular positional parameters without -- prefix
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "invalid option: %s", arg);
            return 1;
        }
    }

    return 0;
}
