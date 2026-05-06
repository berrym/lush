/**
 * @file builtin_completions.c
 * @brief LLE Builtin Command Completions Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Context-aware tab completions for all shell builtins.
 */

#include "lle/completion/builtin_completions.h"
#include "lle/completion/completion_sources.h"
#include "lle/completion/completion_types.h"

#include <string.h>

// ============================================================================
// SIGNAL NAMES FOR TRAP
// ============================================================================

static const char *signal_names[] = {
    "EXIT", "HUP",    "INT",  "QUIT",  "ILL",  "TRAP", "ABRT", "BUS",
    "FPE",  "KILL",   "USR1", "SEGV",  "USR2", "PIPE", "ALRM", "TERM",
    "CHLD", "CONT",   "STOP", "TSTP",  "TTIN", "TTOU", "URG",  "XCPU",
    "XFSZ", "VTALRM", "PROF", "WINCH", "IO",   "SYS",  NULL};

const char **lle_builtin_get_signal_names(void) { return signal_names; }

// ============================================================================
// OPTION DEFINITIONS
// ============================================================================

/* echo options */
static const lle_builtin_option_t echo_options[] = {
    {"-n", "Do not output trailing newline"},
    {"-e", "Enable interpretation of backslash escapes"},
    {"-E", "Disable interpretation of backslash escapes"},
};

/* read options */
static const lle_builtin_option_t read_options[] = {
    {"-p", "Prompt string"},
    {"-r", "Do not treat backslash as escape character"},
    {"-t", "Timeout in seconds"},
    {"-n", "Read specified number of characters"},
    {"-s", "Silent mode (do not echo input)"},
};

/* type options */
static const lle_builtin_option_t type_options[] = {
    {"-t", "Print only type name"},
    {"-p", "Print path for external commands"},
    {"-a", "Print all matches"},
};

/* ulimit options */
static const lle_builtin_option_t ulimit_options[] = {
    {"-a", "Show all current limits"},
    {"-H", "Set hard limit"},
    {"-S", "Set soft limit"},
    {"-f", "Maximum file size"},
    {"-n", "Maximum number of open file descriptors"},
    {"-t", "Maximum CPU time"},
    {"-s", "Maximum stack size"},
    {"-u", "Maximum number of user processes"},
    {"-v", "Maximum virtual memory size"},
    {"-h", "Show help"},
};

/* fc options */
static const lle_builtin_option_t fc_options[] = {
    {"-e", "Editor to use"},
    {"-l", "List commands"},
    {"-n", "Suppress command numbers"},
    {"-r", "Reverse order"},
    {"-s", "Re-execute without editing"},
};

/* command options */
static const lle_builtin_option_t command_options[] = {
    {"-v", "Print command description"},
    {"-V", "Print verbose command description"},
    {"-p", "Use default PATH"},
};

/* trap options */
static const lle_builtin_option_t trap_options[] = {
    {"-l", "List signal names"},
};

/* unalias options */
static const lle_builtin_option_t unalias_options[] = {
    {"-a", "Remove all aliases"},
};

/* hash options */
static const lle_builtin_option_t hash_options[] = {
    {"-r", "Forget all remembered locations"},
};

/* set options */
static const lle_builtin_option_t set_options[] = {
    {"-a", "Export all variables"},
    {"-C", "Prevent output redirection from overwriting files"},
    {"-e", "Exit on error"},
    {"-f", "Disable filename expansion"},
    {"-n", "Read commands but do not execute"},
    {"-u", "Treat unset variables as error"},
    {"-v", "Print shell input lines"},
    {"-x", "Print commands and arguments"},
    {"-o", "Set option by name"},
    {"+o", "Unset option by name"},
};

/* export options */
static const lle_builtin_option_t export_options[] = {
    {"-p", "Print all exported variables"},
};

/* readonly options */
static const lle_builtin_option_t readonly_options[] = {
    {"-p", "Print all readonly variables"},
};

/* history options */
static const lle_builtin_option_t history_options[] = {
    {"-c", "Clear history"},
    {"-d", "Delete entry at offset"},
    {"-r", "Read history file"},
    {"-w", "Write history file"},
};

/* jobs options */
static const lle_builtin_option_t jobs_options[] = {
    {"-l", "Long format with process IDs"},
    {"-p", "Show only process IDs"},
};

/* setopt options */
static const lle_builtin_option_t setopt_options[] = {
    {"-p", "Print in re-usable format"},
    {"-q", "Query silently (exit status only)"},
};

/* disown options */
static const lle_builtin_option_t disown_options[] = {
    {"-h", "Mark jobs to not receive SIGHUP instead of removing"},
    {"-a", "Apply to all jobs"},
    {"-r", "Apply to running jobs only"},
};

/* mapfile/readarray options */
static const lle_builtin_option_t mapfile_options[] = {
    {"-d", "Use specified delimiter instead of newline"},
    {"-n", "Read at most count lines"},
    {"-O", "Start assigning at index origin"},
    {"-s", "Skip first count lines"},
    {"-t", "Remove trailing delimiter from each line"},
    {"-u", "Read from file descriptor instead of stdin"},
    {"-C", "Execute callback every quantum lines"},
    {"-c", "Quantum for callback (default 5000)"},
};

/* env options */
static const lle_builtin_option_t env_options[] = {
    {"-i", "Start with empty environment"},
    {"-u", "Remove variable from environment"},
    {"-0", "Use NUL instead of newline for output"},
    {"--help", "Display help message"},
};

/* analyze options */
static const lle_builtin_option_t analyze_options[] = {
    {"-t", "Target shell (posix, bash, zsh)"},
    {"--target", "Target shell (posix, bash, zsh)"},
    {"-s", "Treat warnings as errors"},
    {"--strict", "Treat warnings as errors"},
    {"-h", "Show help message"},
    {"--help", "Show help message"},
};

/* lint options */
static const lle_builtin_option_t lint_options[] = {
    {"-t", "Target shell (posix, bash, zsh)"},
    {"--target", "Target shell (posix, bash, zsh)"},
    {"-s", "Treat warnings as errors"},
    {"--strict", "Treat warnings as errors"},
    {"--fix", "Apply safe automatic fixes"},
    {"--fix-interactive", "Interactively approve each fix"},
    {"--unsafe-fixes", "Also apply unsafe fixes"},
    {"--dry-run", "Preview fixes without applying"},
    {"--diff", "Show unified diff of changes"},
    {"--no-backup", "Don't create backup when fixing"},
    {"-h", "Show help message"},
    {"--help", "Show help message"},
};

/* getopts - no options, just takes optstring and varname */

// ============================================================================
// DISPLAY SUBCOMMAND HIERARCHY
// ============================================================================

/* display lle theme subcommands */
static const lle_builtin_subcommand_t display_lle_theme_subcmds[] = {
    {"list", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"set", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_THEME},
    {"export", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"reload", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle history dedup scope values */
static const lle_builtin_subcommand_t display_lle_history_dedup_scope_vals[] = {
    {"none", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"session", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"recent", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"global", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle history dedup strategy values */
static const lle_builtin_subcommand_t
    display_lle_history_dedup_strategy_vals[] = {
        {"ignore", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"keep-recent", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"keep-frequent", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"merge", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"keep-all", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle history dedup subcommands */
static const lle_builtin_subcommand_t display_lle_history_dedup_subcmds[] = {
    {"on", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"off", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"scope", display_lle_history_dedup_scope_vals,
     sizeof(display_lle_history_dedup_scope_vals) /
         sizeof(display_lle_history_dedup_scope_vals[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"strategy", display_lle_history_dedup_strategy_vals,
     sizeof(display_lle_history_dedup_strategy_vals) /
         sizeof(display_lle_history_dedup_strategy_vals[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"clean", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle history nav-dedup values */
static const lle_builtin_subcommand_t display_lle_history_onoff_vals[] = {
    {"on", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"off", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle history subcommands */
static const lle_builtin_subcommand_t display_lle_history_subcmds[] = {
    {"status", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"dedup", display_lle_history_dedup_subcmds,
     sizeof(display_lle_history_dedup_subcmds) /
         sizeof(display_lle_history_dedup_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"nav-dedup", display_lle_history_onoff_vals,
     sizeof(display_lle_history_onoff_vals) /
         sizeof(display_lle_history_onoff_vals[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"nav-unique", display_lle_history_onoff_vals,
     sizeof(display_lle_history_onoff_vals) /
         sizeof(display_lle_history_onoff_vals[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle completion sources subcommands */
static const lle_builtin_subcommand_t display_lle_completion_sources_subcmds[] =
    {
        {"list", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"reload", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
        {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle completion subcommands */
static const lle_builtin_subcommand_t display_lle_completion_subcmds[] = {
    {"sources", display_lle_completion_sources_subcmds,
     sizeof(display_lle_completion_sources_subcmds) /
         sizeof(display_lle_completion_sources_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display lle subcommands */
static const lle_builtin_subcommand_t display_lle_subcmds[] = {
    {"status", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"diagnostics", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"autosuggestions", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"syntax", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"transient", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"theme", display_lle_theme_subcmds,
     sizeof(display_lle_theme_subcmds) / sizeof(display_lle_theme_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"history", display_lle_history_subcmds,
     sizeof(display_lle_history_subcmds) /
         sizeof(display_lle_history_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"reset", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"keybindings", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"completion", display_lle_completion_subcmds,
     sizeof(display_lle_completion_subcmds) /
         sizeof(display_lle_completion_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display performance subcommands */
static const lle_builtin_subcommand_t display_performance_subcmds[] = {
    {"init", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"report", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"layers", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"memory", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"baseline", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"reset", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"targets", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"monitoring", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"debug", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* display top-level subcommands */
static const lle_builtin_subcommand_t display_subcmds[] = {
    {"status", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"config", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"stats", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"diagnostics", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"test", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"performance", display_performance_subcmds,
     sizeof(display_performance_subcmds) /
         sizeof(display_performance_subcmds[0]),
     NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"lle", display_lle_subcmds,
     sizeof(display_lle_subcmds) / sizeof(display_lle_subcmds[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

// ============================================================================
// DEBUG SUBCOMMAND HIERARCHY
// ============================================================================

/* debug break subcommands */
static const lle_builtin_subcommand_t debug_break_subcmds[] = {
    {"add", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"remove", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"delete", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"list", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"clear", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

/* debug top-level subcommands */
static const lle_builtin_subcommand_t debug_subcmds[] = {
    {"on", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"off", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"level", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"trace", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"break", debug_break_subcmds,
     sizeof(debug_break_subcmds) / sizeof(debug_break_subcmds[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"step", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"next", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"continue", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"stack", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"vars", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"print", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"profile", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"analyze", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"functions", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"function", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

// ============================================================================
// CONFIG SUBCOMMAND HIERARCHY
// ============================================================================

static const lle_builtin_subcommand_t config_subcmds[] = {
    {"show", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"get", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"set", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"reload", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"save", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

// ============================================================================
// MODE SUBCOMMAND HIERARCHY
// ============================================================================

static const lle_builtin_subcommand_t mode_subcmds[] = {
    {"lush", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"posix", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"bash", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"zsh", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"--reset", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"--show", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

// ============================================================================
// NETWORK SUBCOMMAND HIERARCHY
// ============================================================================

static const lle_builtin_subcommand_t network_subcmds[] = {
    {"hosts", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"refresh", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
};

// ============================================================================
// MASTER BUILTIN SPECIFICATION REGISTRY
// ============================================================================

static const lle_builtin_completion_spec_t builtin_specs[] = {
    /* Builtins with options */
    {"echo", echo_options, sizeof(echo_options) / sizeof(echo_options[0]), NULL,
     0, LLE_BUILTIN_ARG_NONE},
    {"read", read_options, sizeof(read_options) / sizeof(read_options[0]), NULL,
     0, LLE_BUILTIN_ARG_VARIABLE},
    {"type", type_options, sizeof(type_options) / sizeof(type_options[0]), NULL,
     0, LLE_BUILTIN_ARG_COMMAND},
    {"ulimit", ulimit_options,
     sizeof(ulimit_options) / sizeof(ulimit_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"fc", fc_options, sizeof(fc_options) / sizeof(fc_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"command", command_options,
     sizeof(command_options) / sizeof(command_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_COMMAND},
    {"trap", trap_options, sizeof(trap_options) / sizeof(trap_options[0]), NULL,
     0, LLE_BUILTIN_ARG_SIGNAL},
    {"unalias", unalias_options,
     sizeof(unalias_options) / sizeof(unalias_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_ALIAS},
    {"hash", hash_options, sizeof(hash_options) / sizeof(hash_options[0]), NULL,
     0, LLE_BUILTIN_ARG_COMMAND},
    {"set", set_options, sizeof(set_options) / sizeof(set_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"export", export_options,
     sizeof(export_options) / sizeof(export_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_VARIABLE},
    {"readonly", readonly_options,
     sizeof(readonly_options) / sizeof(readonly_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_VARIABLE},
    {"history", history_options,
     sizeof(history_options) / sizeof(history_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"jobs", jobs_options, sizeof(jobs_options) / sizeof(jobs_options[0]), NULL,
     0, LLE_BUILTIN_ARG_NONE},
    {"setopt", setopt_options,
     sizeof(setopt_options) / sizeof(setopt_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_FEATURE},
    {"unsetopt", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FEATURE},

    /* Builtins with subcommands */
    {"display", NULL, 0, display_subcmds,
     sizeof(display_subcmds) / sizeof(display_subcmds[0]),
     LLE_BUILTIN_ARG_NONE},
    {"debug", NULL, 0, debug_subcmds,
     sizeof(debug_subcmds) / sizeof(debug_subcmds[0]), LLE_BUILTIN_ARG_NONE},
    {"config", NULL, 0, config_subcmds,
     sizeof(config_subcmds) / sizeof(config_subcmds[0]), LLE_BUILTIN_ARG_NONE},
    {"network", NULL, 0, network_subcmds,
     sizeof(network_subcmds) / sizeof(network_subcmds[0]),
     LLE_BUILTIN_ARG_NONE},
    {"mode", NULL, 0, mode_subcmds,
     sizeof(mode_subcmds) / sizeof(mode_subcmds[0]), LLE_BUILTIN_ARG_NONE},

    /* Builtins with only dynamic arguments */
    {"cd", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_DIRECTORY},
    {"source", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FILE},
    {".", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FILE},
    {"unset", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_VARIABLE},
    {"local", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_VARIABLE},
    {"fg", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_JOB},
    {"bg", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_JOB},
    {"wait", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_JOB},

    /* Directory stack builtins */
    {"pushd", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_DIRECTORY},
    {"popd", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"dirs", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},

    /* Simple builtins (no special completions, but registered for lookup) */
    {"exit", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"pwd", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"true", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"false", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"clear", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"terminal", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"break", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"continue", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"return", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"return_value", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"exec", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_COMMAND},
    {"shift", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"umask", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"times", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"getopts", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"alias", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {":", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"help", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},
    {"printf", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FILE},
    {"test", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FILE},
    {"[", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_FILE},
    {"eval", NULL, 0, NULL, 0, LLE_BUILTIN_ARG_NONE},

    /* Job control builtins */
    {"disown", disown_options,
     sizeof(disown_options) / sizeof(disown_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_JOB},

    /* Array builtins */
    {"mapfile", mapfile_options,
     sizeof(mapfile_options) / sizeof(mapfile_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},
    {"readarray", mapfile_options,
     sizeof(mapfile_options) / sizeof(mapfile_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_NONE},

    /* Environment builtins */
    {"env", env_options, sizeof(env_options) / sizeof(env_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_COMMAND},
    {"printenv", env_options, sizeof(env_options) / sizeof(env_options[0]),
     NULL, 0, LLE_BUILTIN_ARG_VARIABLE},

    /* Script analysis builtins */
    {"analyze", analyze_options,
     sizeof(analyze_options) / sizeof(analyze_options[0]), NULL, 0,
     LLE_BUILTIN_ARG_FILE},
    {"lint", lint_options, sizeof(lint_options) / sizeof(lint_options[0]), NULL,
     0, LLE_BUILTIN_ARG_FILE},
};

static const size_t builtin_specs_count =
    sizeof(builtin_specs) / sizeof(builtin_specs[0]);

// ============================================================================
// SPEC LOOKUP
// ============================================================================

const lle_builtin_completion_spec_t *lle_builtin_get_spec(const char *name) {
    if (!name) {
        return NULL;
    }

    for (size_t i = 0; i < builtin_specs_count; i++) {
        if (strcmp(builtin_specs[i].name, name) == 0) {
            return &builtin_specs[i];
        }
    }

    return NULL;
}

size_t lle_builtin_get_spec_count(void) { return builtin_specs_count; }

// ============================================================================
// CONTEXT PARSING
// ============================================================================

/**
 * @brief Parse command arguments to find current subcommand position
 *
 * Given "display lle theme " returns the theme subcommand spec.
 *
 * @param spec Top-level builtin spec
 * @param context Completion context with argument info
 * @param out_subcmd Output: current subcommand (may be NULL if at top level)
 * @param out_depth Output: depth in subcommand hierarchy
 */
static void find_current_subcommand(const lle_builtin_completion_spec_t *spec,
                                    const lle_word_context_t *context,
                                    const lle_builtin_subcommand_t **out_subcmd,
                                    int *out_depth) {
    *out_subcmd = NULL;
    *out_depth = 0;

    if (!spec || !context || spec->subcommand_count == 0) {
        return;
    }

    /* If no arguments yet, we're at top level */
    if (context->argument_count == 0 || !context->arguments) {
        return;
    }

    /* Traverse the subcommand hierarchy using the typed arguments */
    const lle_builtin_subcommand_t *subcommands = spec->subcommands;
    size_t subcommand_count = spec->subcommand_count;
    const lle_builtin_subcommand_t *current = NULL;

    for (size_t i = 0; i < context->argument_count; i++) {
        const char *arg = context->arguments[i];
        if (!arg) {
            break;
        }

        /* Skip options (arguments starting with -) */
        if (arg[0] == '-') {
            continue;
        }

        /* Find matching subcommand at current level */
        bool found = false;
        for (size_t j = 0; j < subcommand_count; j++) {
            if (strcmp(subcommands[j].name, arg) == 0) {
                current = &subcommands[j];
                (*out_depth)++;
                found = true;

                /* Descend into this subcommand's children if it has any */
                if (current->subcommands && current->subcommand_count > 0) {
                    subcommands = current->subcommands;
                    subcommand_count = current->subcommand_count;
                } else {
                    /* No more children - stop here */
                    subcommands = NULL;
                    subcommand_count = 0;
                }
                break;
            }
        }

        /* If no match found at this level, stop traversing */
        if (!found) {
            break;
        }
    }

    *out_subcmd = current;
}

// ============================================================================
// DYNAMIC ARGUMENT GENERATORS
// ============================================================================

/**
 * @brief Generate signal name completions
 */
static lle_result_t
generate_signal_completions(lle_memory_pool_t *pool, const char *prefix,
                            lle_completion_result_t *result) {
    (void)pool; /* Completions allocated via result's pool */
    size_t prefix_len = prefix ? strlen(prefix) : 0;

    for (const char **sig = signal_names; *sig != NULL; sig++) {
        if (prefix_len == 0 || strncmp(*sig, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, *sig, " ", LLE_COMPLETION_TYPE_CUSTOM, 500);
            if (res != LLE_SUCCESS) {
                return res;
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Generate job ID completions
 *
 * Jobs are referenced as %1, %2, etc. or %+ (current), %- (previous)
 */
static lle_result_t generate_job_completions(lle_memory_pool_t *pool,
                                             const char *prefix,
                                             lle_completion_result_t *result) {
    (void)pool; /* Completions allocated via result's pool */
    /* Add common job specifiers */
    static const char *job_specs[] = {"%+", "%-", "%1", "%2", "%3", NULL};

    size_t prefix_len = prefix ? strlen(prefix) : 0;

    for (const char **job = job_specs; *job != NULL; job++) {
        if (prefix_len == 0 || strncmp(*job, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, *job, " ", LLE_COMPLETION_TYPE_CUSTOM, 500);
            if (res != LLE_SUCCESS) {
                return res;
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Generate theme name completions
 *
 * Gets available themes from the theme registry.
 */
static lle_result_t
generate_theme_completions(lle_memory_pool_t *pool, const char *prefix,
                           lle_completion_result_t *result) {
    (void)pool; /* Completions allocated via result's pool */
    /* All built-in theme names */
    static const char *themes[] = {"default",  "minimal",   "powerline",
                                   "classic",  "nerd",      "informative",
                                   "two_line", "corporate", "dark",
                                   "light",    "colorful",  NULL};

    size_t prefix_len = prefix ? strlen(prefix) : 0;

    for (const char **theme = themes; *theme != NULL; theme++) {
        if (prefix_len == 0 || strncmp(*theme, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, *theme, " ", LLE_COMPLETION_TYPE_CUSTOM, 500);
            if (res != LLE_SUCCESS) {
                return res;
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Generate alias completions for unalias
 */
static lle_result_t
generate_alias_completions(lle_memory_pool_t *pool,
                           const lle_word_context_t *context,
                           lle_completion_result_t *result) {
    return lle_completion_source_aliases(pool, context, result);
}

/**
 * @brief Shell feature names for completion
 *
 * Static list of shell feature names matching shell_mode.h.
 * This avoids linking dependency on shell_mode for test binaries.
 */
static const char *shell_feature_names[] = {
    /* Arrays */
    "indexed_arrays", "associative_arrays", "array_zero_indexed",
    "array_append",
    /* Arithmetic */
    "arith_command", "let_builtin",
    /* Tests */
    "extended_test", "regex_match", "pattern_match",
    /* Redirection */
    "process_substitution", "pipe_stderr", "append_both", "coproc",
    /* Parameter expansion */
    "case_modification", "substring_expansion", "pattern_substitution",
    "indirect_expansion", "param_transformation",
    /* Globbing */
    "extended_glob", "null_glob", "dot_glob",
    /* Brace expansion */
    "brace_expansion",
    /* Control flow */
    "case_fallthrough", "select_loop", "time_keyword",
    /* Behavior */
    "word_split_default", "auto_cd", "auto_pushd", "cdable_vars",
    /* Advanced */
    "nameref", "anonymous_functions", "return_anywhere",
    /* Zsh-specific */
    "glob_qualifiers", "hook_functions", "zsh_param_flags", "plugin_system",
    NULL};

/**
 * @brief Generate shell feature completions for setopt/unsetopt
 *
 * Provides completion for shell feature names.
 */
static lle_result_t
generate_feature_completions(lle_memory_pool_t *pool, const char *prefix,
                             lle_completion_result_t *result) {
    (void)pool; /* Completions allocated via result's pool */
    size_t prefix_len = prefix ? strlen(prefix) : 0;

    /* Iterate through all shell feature names */
    for (const char **name = shell_feature_names; *name != NULL; name++) {
        if (prefix_len == 0 || strncmp(*name, prefix, prefix_len) == 0) {
            lle_result_t res = lle_completion_result_add(
                result, *name, " ", LLE_COMPLETION_TYPE_CUSTOM, 600);
            if (res != LLE_SUCCESS) {
                return res;
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Generate completions based on argument type
 *
 * Routes to the appropriate source. Path-shaped sources receive the
 * full word context so they can access expanded_directory; non-path
 * sources read the dequoted prefix from context->dequoted_filename_prefix.
 */
static lle_result_t generate_dynamic_completions(
    lle_memory_pool_t *pool, lle_builtin_arg_type_t arg_type,
    const lle_word_context_t *context, lle_completion_result_t *result) {
    const char *prefix = context->dequoted_filename_prefix
                             ? context->dequoted_filename_prefix
                             : "";
    switch (arg_type) {
    case LLE_BUILTIN_ARG_NONE:
        return LLE_SUCCESS;

    case LLE_BUILTIN_ARG_FILE:
        return lle_completion_source_files(pool, context, result);

    case LLE_BUILTIN_ARG_DIRECTORY:
        return lle_completion_source_directories(pool, context, result);

    case LLE_BUILTIN_ARG_VARIABLE:
        return lle_completion_source_variables(pool, context, result);

    case LLE_BUILTIN_ARG_ALIAS:
        return generate_alias_completions(pool, context, result);

    case LLE_BUILTIN_ARG_COMMAND:
        lle_completion_source_builtins(pool, context, result);
        return lle_completion_source_commands(pool, context, result);

    case LLE_BUILTIN_ARG_SIGNAL:
        return generate_signal_completions(pool, prefix, result);

    case LLE_BUILTIN_ARG_JOB:
        return generate_job_completions(pool, prefix, result);

    case LLE_BUILTIN_ARG_THEME:
        return generate_theme_completions(pool, prefix, result);

    case LLE_BUILTIN_ARG_FEATURE:
        return generate_feature_completions(pool, prefix, result);
    }

    return LLE_SUCCESS;
}

// ============================================================================
// APPLICABILITY CHECK
// ============================================================================

bool lle_builtin_completions_applicable(const lle_word_context_t *context) {
    if (!context) {
        return false;
    }

    /* Only applicable when completing arguments to a command */
    if (context->context_type != LLE_CONTEXT_ARGUMENT) {
        return false;
    }

    /* Check if the command is a builtin */
    if (!context->command_name) {
        return false;
    }

    return lle_builtin_get_spec(context->command_name) != NULL;
}

// ============================================================================
// MAIN COMPLETION GENERATOR
// ============================================================================

lle_result_t lle_builtin_completions_generate(lle_memory_pool_t *pool,
                                              const lle_word_context_t *context,
                                              lle_completion_result_t *result) {
    if (!pool || !context || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Get the builtin spec */
    const lle_builtin_completion_spec_t *spec =
        lle_builtin_get_spec(context->command_name);
    if (!spec) {
        return LLE_SUCCESS; /* Not a builtin we know about */
    }

    const char *match_prefix = context->dequoted_filename_prefix
                                   ? context->dequoted_filename_prefix
                                   : "";
    size_t prefix_len = strlen(match_prefix);

    /* Find current position in subcommand hierarchy */
    const lle_builtin_subcommand_t *current_subcmd = NULL;
    int depth = 0;
    find_current_subcommand(spec, context, &current_subcmd, &depth);

    /* Determine what to complete based on context */
    const lle_builtin_option_t *options = NULL;
    size_t option_count = 0;
    const lle_builtin_subcommand_t *subcommands = NULL;
    size_t subcommand_count = 0;
    lle_builtin_arg_type_t arg_type = LLE_BUILTIN_ARG_NONE;

    if (current_subcmd) {
        /* We're inside a subcommand - use its options/subcommands */
        options = current_subcmd->options;
        option_count = current_subcmd->option_count;
        subcommands = current_subcmd->subcommands;
        subcommand_count = current_subcmd->subcommand_count;
        arg_type = current_subcmd->arg_type;
    } else {
        /* At top level - use spec's options/subcommands */
        options = spec->options;
        option_count = spec->option_count;
        subcommands = spec->subcommands;
        subcommand_count = spec->subcommand_count;
        arg_type = spec->default_arg_type;
    }

    /* Add matching options (if prefix starts with -) */
    if (prefix_len == 0 || match_prefix[0] == '-') {
        for (size_t i = 0; i < option_count; i++) {
            const char *opt_name = options[i].name;
            if (prefix_len == 0 ||
                strncmp(opt_name, match_prefix, prefix_len) == 0) {
                lle_result_t res = lle_completion_result_add_with_description(
                    result, opt_name, " ", LLE_COMPLETION_TYPE_CUSTOM, 800,
                    options[i].description);
                if (res != LLE_SUCCESS) {
                    return res;
                }
            }
        }
    }

    /* Add matching subcommands (if prefix doesn't start with -) */
    if (prefix_len == 0 || match_prefix[0] != '-') {
        for (size_t i = 0; i < subcommand_count; i++) {
            const char *subcmd_name = subcommands[i].name;
            if (prefix_len == 0 ||
                strncmp(subcmd_name, match_prefix, prefix_len) == 0) {
                lle_result_t res = lle_completion_result_add(
                    result, subcmd_name, " ", LLE_COMPLETION_TYPE_CUSTOM, 700);
                if (res != LLE_SUCCESS) {
                    return res;
                }
            }
        }
    }

    /* Add dynamic completions based on arg type */
    if (arg_type != LLE_BUILTIN_ARG_NONE) {
        lle_result_t res =
            generate_dynamic_completions(pool, arg_type, context, result);
        if (res != LLE_SUCCESS) {
            return res;
        }
    }

    return LLE_SUCCESS;
}
