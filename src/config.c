/**
 * @file config.c
 * @brief Configuration System Implementation for Lush Shell
 *
 * Handles loading, parsing, and managing shell configuration files.
 * Supports TOML-style configuration with sections, type validation,
 * and runtime updates.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2025 Michael Berry. All rights reserved.
 */

#include "config.h"

#include "alias.h"
#include "autocorrect.h"
#include "config_registry.h"
#include "executor.h"
#include "input.h"
#include "lle/char_width.h"
#include "lle/lle_shell_integration.h"
#include "lle/unicode_compare.h"
#include "lush.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/// Global configuration instance
config_values_t config;
config_context_t config_ctx;

/// Current configuration section
static config_section_t current_section = CONFIG_SECTION_NONE;

/// Error handling
/* `last_error` and the config_error/warning wrappers were removed as
 * part of the structured-error migration (#71); their callers now use
 * shell_error_create() directly. */

/// ============================================================================
/// INTERNAL TYPE DEFINITIONS
/// ============================================================================

/// Configuration option types (internal to config.c)
typedef enum {
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_COLOR,
    CONFIG_TYPE_ENUM /// String-to-integer enum mapping
} config_type_t;

/// Enum value mapping for CONFIG_TYPE_ENUM
typedef struct {
    const char *name; /// String representation in config file
    int value;        /// Corresponding enum/integer value
} config_enum_mapping_t;

/// Enum definition for CONFIG_TYPE_ENUM options
typedef struct {
    const config_enum_mapping_t *mappings; /// NULL-terminated array of mappings
    int default_value;                     /// Default if no match found
} config_enum_def_t;

/// Configuration option structure (internal to config.c)
typedef struct config_option {
    const char *name;
    config_type_t type;
    config_section_t section;
    void *value_ptr;
    const char *description;
    bool (*validator)(const char *value);
    const config_enum_def_t
        *enum_def; /// For CONFIG_TYPE_ENUM: mapping definition
} config_option_t;

/// ============================================================================
/// ENUM MAPPING DEFINITIONS
/// ============================================================================

/// The LLE arrow-mode and dedup scope/strategy enum string<->int maps moved to
/// creg_enum_pair_t tables next to lle_bind_runtime when the section migrated
/// to registry bindings.

/// Shell Mode mappings (Extended Language Support)
static const config_enum_mapping_t shell_mode_mappings[] = {
    {"posix", SHELL_MODE_POSIX},
    {   "sh", SHELL_MODE_POSIX}, /// Alias for posix
    { "bash",  SHELL_MODE_BASH},
    {  "zsh",   SHELL_MODE_ZSH},
    { "lush",  SHELL_MODE_LUSH},
    {   NULL,                0}  /// Sentinel
};
static const config_enum_def_t shell_mode_enum = {shell_mode_mappings,
                                                  SHELL_MODE_LUSH};

/// completion.match_mode string<->enum mapping lives with the binding now
/// (completion_match_mode_pairs). The old config_enum_mapping/def tables were
/// only used by the legacy completion.match_mode row the migration removed.

/// autosuggestion.* enum mappings live with the bindings now
/// (autosuggestion_*_pairs, creg_enum_pair_t). The old config_enum_def tables
/// were only used by the legacy config_options[] rows the migration removed.

/// history.* search/finder enum mappings live with the bindings now
/// (history_*_pairs, creg_enum_pair_t): the registry maps the string to the
/// engine enum on change. The old config_enum_def tables here were only used by
/// the legacy config_options[] rows, which the registry migration removed.

/// ============================================================================
/// CONFIGURATION OPTION DEFINITIONS
/// ============================================================================

/// Configuration option definitions
static config_option_t config_options[] = {
    /// History settings
    /// history.* (except history.file) is migrated to the CREG registry: bound
    /// to its runtime cells and layered (default/mode/user/session). The config
    /// builtin reaches these keys through the registry fallback, so they are no
    /// longer in this legacy table -- which is what lets an interactive
    /// `config set` land in the SESSION layer and survive a mode switch.

    /// LLE history/editor configuration (lle.*) is migrated to the CREG
    /// registry: bound to its runtime cells via lle_bind_runtime and layered
    /// (default/mode/user/session). The config builtin reaches these keys
    /// through the registry fallback, so they are no longer in this legacy
    /// table.

    /// Completion settings
    /// completion.{enabled,match_mode,threshold,case_sensitive} are migrated to
    /// the CREG registry (bound + layered); they resolve through the registry,
    /// not this legacy table. (show_all and hints remain legacy-only.)
    {           "completion.show_all", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.completion_show_all,"Show all completions",config_validate_bool,
     NULL                                                                                                        },
    {              "completion.hints", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.hints_enabled,                  "Enable input hints",         config_validate_bool,             NULL},

    /// Behavior settings
    /// behavior.auto_cd, behavior.spell_correction, behavior.confirm_exit are
    /// migrated to the CREG registry (bound + layered).
    /// behavior.autocorrect_* are migrated to the CREG registry (bound +
    /// layered).
    /// behavior limits (tab_width, brace_expansion_max, regex_pattern_max,
    /// path_negative_cache_ttl_ms, loop_failure_streak/seconds) are migrated to
    /// the CREG registry (bound + layered).

    /// Network settings
    {        "network.ssh_completion", CONFIG_TYPE_BOOL,    CONFIG_SECTION_NETWORK,
     &config.ssh_completion_enabled,          "Enable SSH host completion",
     config_validate_bool,             NULL                                                                      },
    {       "network.cache_ssh_hosts", CONFIG_TYPE_BOOL,    CONFIG_SECTION_NETWORK,
     &config.cache_ssh_hosts,     "Cache SSH hosts for performance",
     config_validate_bool,             NULL                                                                      },
    { "network.cache_timeout_minutes",  CONFIG_TYPE_INT,    CONFIG_SECTION_NETWORK,
     &config.cache_timeout_minutes,   "SSH host cache timeout in minutes",
     config_validate_int,             NULL                                                                       },
    {  "network.max_completion_hosts",  CONFIG_TYPE_INT,    CONFIG_SECTION_NETWORK,
     &config.max_completion_hosts, "Maximum hosts to show in completion",
     config_validate_int,             NULL                                                                       },

    /// Display system settings
    /// v1.3.0: Layered display is now the exclusive system - no configuration
    /// needed
    /// display.system_mode and display.layered_display options removed
    /// display.{syntax_highlighting,autosuggestions,transient_prompt,
    /// theme_hot_reload,optimization_level,lle.pager.*,ambiguous_width} are
    /// migrated to the CREG registry (bound + layered); they resolve through
    /// the registry, not this legacy table. performance_monitoring remains
    /// legacy-only for now.
    {"display.performance_monitoring", CONFIG_TYPE_BOOL,    CONFIG_SECTION_DISPLAY,
     &config.display_performance_monitoring,
     "Enable display performance monitoring",         config_validate_bool,             NULL                     },

    /// Autosuggestion settings
    /// autosuggestion.* is migrated to the CREG registry (bound + layered); its
    /// keys resolve through the registry, not this legacy table.

    /// v1.3.0: Legacy enhanced display mode option removed
    /// behavior.enhanced_display_mode option removed

    /// Script execution control
    {             "scripts.execution", CONFIG_TYPE_BOOL,    CONFIG_SECTION_SCRIPTS,
     &config.script_execution,             "Enable script execution",         config_validate_bool,
     NULL                                                                                                        },

    /// Shell options integration - all 24 POSIX options with shell.* namespace
    /// These map directly to existing shell_opts flags for perfect
    /// compatibility
    {                 "shell.errexit", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Exit on command failure (set -e)", config_validate_shell_option,             NULL                          },
    {                  "shell.xtrace", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Trace command execution (set -x)", config_validate_shell_option,             NULL                          },
    {                  "shell.noexec", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Syntax check only (set -n)", config_validate_shell_option,             NULL                                },
    {                 "shell.nounset", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Error on unset variables (set -u)", config_validate_shell_option,             NULL                         },
    {                 "shell.verbose", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Print input lines (set -v)", config_validate_shell_option,             NULL                                },
    {                  "shell.noglob", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Disable pathname expansion (set -f)", config_validate_shell_option,             NULL                       },
    {                 "shell.hashall", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Command hashing (set -h)", config_validate_shell_option,             NULL                                  },
    {                 "shell.monitor", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Job control (set -m)", config_validate_shell_option,             NULL                                      },
    {               "shell.allexport", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Auto export variables (set -a)", config_validate_shell_option,             NULL                            },
    {               "shell.noclobber", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Prevent file overwrite (set -C)", config_validate_shell_option,             NULL                           },
    {                  "shell.onecmd", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Exit after one command (set -t)", config_validate_shell_option,             NULL                           },
    {                  "shell.notify", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Async job notification (set -b)", config_validate_shell_option,             NULL                           },
    {               "shell.ignoreeof", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Prevent exit on EOF (set -o ignoreeof)", config_validate_shell_option,
     NULL                                                                                                        },
    {                   "shell.nolog", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Prevent function history logging (set -o nolog)", config_validate_shell_option,             NULL           },
    {                   "shell.emacs", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Emacs-style editing (set -o emacs)", config_validate_shell_option,             NULL                        },
    {                      "shell.vi", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Vi-style editing (set -o vi)", config_validate_shell_option,             NULL                              },
    {                   "shell.posix", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Strict POSIX compliance (set -o posix)", config_validate_shell_option,
     NULL                                                                                                        },
    {                "shell.pipefail", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Pipeline failure detection (set -o pipefail)", config_validate_shell_option,             NULL              },
    {              "shell.histexpand", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "History expansion (set -o histexpand)", config_validate_shell_option,
     NULL                                                                                                        },
    {                 "shell.history", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Command history recording (set -o history)", config_validate_shell_option,
     NULL                                                                                                        },
    {    "shell.interactive-comments", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Interactive comments (set -o interactive-comments)", config_validate_shell_option,             NULL        },
    {                "shell.physical", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Physical directory paths (set -o physical)", config_validate_shell_option,
     NULL                                                                                                        },
    {              "shell.privileged", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,               NULL,
     "Restricted shell security (set -o privileged)", config_validate_shell_option,             NULL             },

    /// Shell mode settings (Extended Language Support)
    {                    "shell.mode", CONFIG_TYPE_ENUM,      CONFIG_SECTION_SHELL, &config.shell_mode,
     "Shell compatibility mode (posix, bash, zsh, lush)",   config_validate_shell_mode, &shell_mode_enum         },
    {             "shell.mode_strict", CONFIG_TYPE_BOOL,      CONFIG_SECTION_SHELL,
     &config.shell_mode_strict,       "Disallow runtime mode changes",
     config_validate_bool,             NULL                                                                      },
};

static const int num_config_options =
    sizeof(config_options) / sizeof(config_option_t);

/* ============================================================================
 * CONFIG REGISTRY INTEGRATION
 *
 * These sections register the existing config options with the unified
 * config registry system, enabling:
 * - TOML-based configuration files
 * - Change notifications for reactive updates
 * - Bidirectional sync between config struct and registry
 * ============================================================================
 */

/// Forward declarations for sync hooks
static void history_bind_runtime(void);
static void shell_sync_to_runtime(void);
static void shell_sync_from_runtime(void);
static void display_bind_runtime(void);
static void completion_bind_runtime(void);
static void behavior_bind_runtime(void);
static void autosuggestion_bind_runtime(void);
static void lle_bind_runtime(void);

/* ----------------------------------------------------------------------------
 * History Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t history_options[] = {
    {                   "enabled",
     CREG_VALUE_BOOLEAN,        {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable command history", true                                                 },
    {                      "size",
     CREG_VALUE_INTEGER,        {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     "Maximum history entries", true                                                },
    {                   "no_dups",
     CREG_VALUE_BOOLEAN,        {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Remove duplicate entries", true                                               },
    {                "timestamps",
     CREG_VALUE_BOOLEAN,       {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Record timestamps", true                                                      },
    {               "search_mode",
     CREG_VALUE_STRING,      {.type = CREG_VALUE_STRING, .data.string = "prefix"},
     "Up/down navigation filter: prefix (cycle history matching the typed "
     "prefix, cursor kept at the prefix) or plain (browse all, cursor at end)", true},
    {              "finder.match",
     CREG_VALUE_STRING,       {.type = CREG_VALUE_STRING, .data.string = "fuzzy"},
     "Ctrl-R matching: fuzzy (subsequence), substring, or prefix", true             },
    {               "finder.rank",
     CREG_VALUE_STRING,    {.type = CREG_VALUE_STRING, .data.string = "frecency"},
     "Ctrl-R ranking: frecency (usage x recency) or recency", true                  },
    {            "finder.display",
     CREG_VALUE_STRING, {.type = CREG_VALUE_STRING, .data.string = "incremental"},
     "Ctrl-R presentation: incremental (in-line). picker is reserved and "
     "currently falls back to incremental", true                                    },
    {"frecency.directory_context",
     CREG_VALUE_BOOLEAN,        {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Boost frecency ranking for commands recorded in the current directory", true  },
};

static const creg_section_t history_section = {
    .name = "history",
    .options = history_options,
    .option_count = sizeof(history_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// history.* is bound (history_bind_runtime); no sync hooks -- the registry
    /// write-throughs the runtime cells directly. This is the keystone proof.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Shell Section Options (POSIX options + mode)
 * -------------------------------------------------------------------------- */
static const creg_option_t shell_options[] = {
    {    "mode",
     CREG_VALUE_STRING,  {.type = CREG_VALUE_STRING, .data.string = "lush"},
     "Shell compatibility mode", true         },
    { "errexit",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Exit on command failure (set -e)", true },
    { "nounset",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Error on unset variables (set -u)", true},
    {  "xtrace",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Trace execution (set -x)", true         },
    {"pipefail",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Pipeline failure detection", true       },
};

static const creg_section_t shell_section = {
    .name = "shell",
    .options = shell_options,
    .option_count = sizeof(shell_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = shell_sync_to_runtime,
    .sync_from_runtime = shell_sync_from_runtime,
};

/* ----------------------------------------------------------------------------
 * Display Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t display_options[] = {
    {  "syntax_highlighting",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable syntax highlighting", true                              },
    {      "autosuggestions",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable Fish-style autosuggestions", true                       },
    {     "transient_prompt",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Enable transient prompts", true                                },
    {   "optimization_level",
     CREG_VALUE_INTEGER,      {.type = CREG_VALUE_INTEGER, .data.integer = 2},
     "Display optimization level (0-4)", true                        },
    {     "theme_hot_reload",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Auto-reload theme when its file changes on disk", true         },
    {"newline_before_prompt",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Print a blank line before each prompt", true                   },
    {    "lle.pager.enabled",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Master switch for the LLE pager", true                         },
    {  "lle.pager.min_lines",
     CREG_VALUE_INTEGER,      {.type = CREG_VALUE_INTEGER, .data.integer = 0},
     "Pager threshold in visual rows (0 = use terminal rows)", true  },
    {"lle.pager.wrap_search",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Wrap pager search to top on no-match (less-style)", true       },
    {            "lle.theme",
     CREG_VALUE_STRING,       {.type = CREG_VALUE_STRING, .data.string = ""},
     "LLE prompt theme name (empty uses the default theme)", true    },
    {      "ambiguous_width",
     CREG_VALUE_STRING, {.type = CREG_VALUE_STRING, .data.string = "narrow"},
     "East Asian Ambiguous-class display width: narrow or wide", true},
};

static const creg_section_t display_section = {
    .name = "display",
    .options = display_options,
    .option_count = sizeof(display_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Completion Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t completion_options[] = {
    {          "enabled",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable tab completion", true                                   },
    {            "fuzzy",
     CREG_VALUE_BOOLEAN,   {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable fuzzy matching", true                                   },
    {   "case_sensitive",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Case-sensitive completion", true                               },
    {"chain_directories",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Re-trigger completion after accepting a directory (per-mode "
     "default: lush=true, others=false)", true                       },
    {"menu_shadow_ghost",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Show an open menu's highlighted candidate inline as a shadow ghost "
     "(per-mode default: lush=true, others=false)", true             },
    {       "match_mode",
     CREG_VALUE_STRING, {.type = CREG_VALUE_STRING, .data.string = "prefix"},
     "Completion match predicate: prefix, substring, or fuzzy (per-mode "
     "default: lush=fuzzy, others=prefix)", true                     },
    {        "threshold",
     CREG_VALUE_INTEGER,     {.type = CREG_VALUE_INTEGER, .data.integer = 60},
     "Minimum fuzzy match score to accept a completion (0-100)", true},
};

static const creg_section_t completion_section = {
    .name = "completion",
    .options = completion_options,
    .option_count = sizeof(completion_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// Bound (completion_bind_runtime); no sync hooks.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Autosuggestion Section Options
 *
 * The enum is carried as a string here (the CREG value model has no enum type);
 * the registry round-trips the TOML, while the legacy config_options[] entry
 * gives the `config` builtin its enum view. Both write the same runtime field.
 * -------------------------------------------------------------------------- */
static const creg_option_t autosuggestion_options[] = {
    {"dismiss_policy",
     CREG_VALUE_STRING,            {.type = CREG_VALUE_STRING, .data.string = "on_deviation"},
     "When to clear the ghost text: on_deviation (only on a non-matching "
     "keystroke; persists through matching spaces) or on_word_boundary "
     "(clear at a trailing space)", true                  },
    {          "rank",
     CREG_VALUE_STRING,                {.type = CREG_VALUE_STRING, .data.string = "frecency"},
     "Which history prefix match to suggest: frecency (most used x recent) or "
     "recency (most recent)", true                        },
    {"partial_accept",
     CREG_VALUE_STRING,            {.type = CREG_VALUE_STRING, .data.string = "path_segment"},
     "Ctrl+Right granularity: path_segment (advance by directory inside a "
     "path) "
     "or word (whole whitespace word)", true              },
    {       "sources",
     CREG_VALUE_STRING, {.type = CREG_VALUE_STRING, .data.string = "history_then_completion"},
     "Ghost text sources: history (history only) or history_then_completion "
     "(fall back to completion when history misses)", true},
};

static const creg_section_t autosuggestion_section = {
    .name = "autosuggestion",
    .options = autosuggestion_options,
    .option_count = sizeof(autosuggestion_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// Bound (autosuggestion_bind_runtime); no sync hooks.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Behavior Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t behavior_options[] = {
    {                    "auto_cd",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Auto-cd to directories", true                                               },
    {           "spell_correction",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable spell correction", true                                              },
    {               "confirm_exit",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Confirm before exit", true                                                  },
    {                  "tab_width",
     CREG_VALUE_INTEGER,     {.type = CREG_VALUE_INTEGER, .data.integer = 4},
     "Tab width for display", true                                                },
    {        "brace_expansion_max",
     CREG_VALUE_INTEGER, {.type = CREG_VALUE_INTEGER, .data.integer = 65536},
     "Max brace expansion result count (0 = unbounded)", true                     },
    {          "regex_pattern_max",
     CREG_VALUE_INTEGER,  {.type = CREG_VALUE_INTEGER, .data.integer = 1024},
     "Max regex pattern length before rejection (0 = unbounded)", true            },
    { "path_negative_cache_ttl_ms",
     CREG_VALUE_INTEGER,  {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     "TTL in milliseconds for the negative PATH-search cache (0 = disabled)", true},
    {        "loop_failure_streak",
     CREG_VALUE_INTEGER,  {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     "Consecutive non-zero loop iterations before runaway-loop trip (0 = "
     "disable)", true                                                             },
    {       "loop_failure_seconds",
     CREG_VALUE_INTEGER,     {.type = CREG_VALUE_INTEGER, .data.integer = 5},
     "Min wall-clock seconds the streak must last before tripping", true          },
    {"autocorrect_max_suggestions",
     CREG_VALUE_INTEGER,     {.type = CREG_VALUE_INTEGER, .data.integer = 3},
     "Maximum auto-correction suggestions (1-5)", true                            },
    {      "autocorrect_threshold",
     CREG_VALUE_INTEGER,    {.type = CREG_VALUE_INTEGER, .data.integer = 40},
     "Auto-correction similarity threshold (0-100)", true                         },
    {    "autocorrect_interactive",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Show interactive correction prompts", true                                  },
    {  "autocorrect_learn_history",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Learn commands from history", true                                          },
    {       "autocorrect_builtins",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Suggest builtin corrections", true                                          },
    {       "autocorrect_external",
     CREG_VALUE_BOOLEAN,  {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Suggest external command corrections", true                                 },
    { "autocorrect_case_sensitive",
     CREG_VALUE_BOOLEAN, {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Case-sensitive auto-correction", true                                       },
};

static const creg_section_t behavior_section = {
    .name = "behavior",
    .options = behavior_options,
    .option_count = sizeof(behavior_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// Bound (behavior_bind_runtime); no sync hooks.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * LLE Section Options (history deduplication, written by display lle history)
 *
 * The two enums (scope, strategy) are carried as strings here -- the CREG value
 * model has no enum type -- and translated onto the engine enums in the sync
 * hooks via the shared mapping tables. Registering them is what lets the
 * display lle history builtin's config_registry_set calls succeed and these
 * settings round-trip through lushrc.toml.
 * -------------------------------------------------------------------------- */
static const creg_option_t lle_options[] = {
    {    "enable_deduplication",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Deduplicate history entries", true                                           },
    {             "dedup_scope",
     CREG_VALUE_STRING,       {.type = CREG_VALUE_STRING, .data.string = "session"},
     "Dedup scope: none, session, recent, or global", true                         },
    {          "dedup_strategy",
     CREG_VALUE_STRING,   {.type = CREG_VALUE_STRING, .data.string = "keep-recent"},
     "Dedup strategy: ignore, keep-recent, keep-frequent, merge, or keep-all", true},
    {        "dedup_navigation",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Skip duplicates while navigating history", true                              },
    { "dedup_navigation_unique",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Show only unique entries during navigation", true                            },
    { "dedup_unicode_normalize",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Apply Unicode NFC normalization before comparing entries", true              },
    {          "arrow_key_mode",
     CREG_VALUE_STRING, {.type = CREG_VALUE_STRING, .data.string = "context-aware"},
     "Arrow key behavior: context-aware, classic, always-history, or "
     "multiline-first", true                                                       },
    {            "history_file",
     CREG_VALUE_STRING,              {.type = CREG_VALUE_STRING, .data.string = ""},
     "History file path (empty uses the default ~/.lush_history)", true            },
    {"enable_forensic_tracking",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Track metadata (timestamps, exit codes, cwd) with history entries", true     },
    {    "enable_history_cache",
     CREG_VALUE_BOOLEAN,          {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Cache history for faster lookups", true                                      },
    {              "cache_size",
     CREG_VALUE_INTEGER,           {.type = CREG_VALUE_INTEGER, .data.integer = 100},
     "History cache size", true                                                    },
};

static const creg_section_t lle_section = {
    .name = "lle",
    .options = lle_options,
    .option_count = sizeof(lle_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// Bound (lle_bind_runtime); no sync hooks.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Sync Hook Implementations
 * -------------------------------------------------------------------------- */

/// @brief Sync history config from registry to runtime
/// history.* enum-as-string mappings (registry string <-> engine enum). The
/// registry write-throughs the matched int once, on change -- this is what the
/// deleted history_sync_to_runtime's strcmp ladders used to do per sync call.
static const creg_enum_pair_t history_search_mode_pairs[] = {
    {"prefix", HISTORY_SEARCH_MODE_PREFIX},
    { "plain",  HISTORY_SEARCH_MODE_PLAIN},
    {    NULL,                          0},
};
static const creg_enum_pair_t history_finder_match_pairs[] = {
    {    "fuzzy",     HISTORY_FINDER_MATCH_FUZZY},
    {"substring", HISTORY_FINDER_MATCH_SUBSTRING},
    {   "prefix",    HISTORY_FINDER_MATCH_PREFIX},
    {       NULL,                              0},
};
static const creg_enum_pair_t history_finder_rank_pairs[] = {
    {"frecency", HISTORY_FINDER_RANK_FRECENCY},
    { "recency",  HISTORY_FINDER_RANK_RECENCY},
    {      NULL,                            0},
};
static const creg_enum_pair_t history_finder_display_pairs[] = {
    {"incremental", HISTORY_FINDER_DISPLAY_INCREMENTAL},
    {     "picker",      HISTORY_FINDER_DISPLAY_PICKER},
    {         NULL,                                  0},
};

/// @brief Bind the history.* keys to their runtime cells (the keystone).
///
/// Replaces history_sync_to_runtime AND history_sync_from_runtime. After this
/// runs (once, at registration) the registry write-throughs config.history_* on
/// every change from any surface; there is nothing left to "call after a
/// change". Binding an unregistered key would fail loudly, so a typo can no
/// longer silently no-op the way the old sync hooks did.
static void history_bind_runtime(void) {
    config_registry_bind_boolean("history.enabled", &config.history_enabled);
    config_registry_bind_integer("history.size", &config.history_size);
    config_registry_bind_boolean("history.no_dups", &config.history_no_dups);
    config_registry_bind_boolean("history.timestamps",
                                 &config.history_timestamps);
    config_registry_bind_enum(
        "history.search_mode", (int *)&config.history_search_mode,
        history_search_mode_pairs, HISTORY_SEARCH_MODE_PREFIX);
    config_registry_bind_enum(
        "history.finder.match", (int *)&config.history_finder_match,
        history_finder_match_pairs, HISTORY_FINDER_MATCH_FUZZY);
    config_registry_bind_enum(
        "history.finder.rank", (int *)&config.history_finder_rank,
        history_finder_rank_pairs, HISTORY_FINDER_RANK_FRECENCY);
    config_registry_bind_enum(
        "history.finder.display", (int *)&config.history_finder_display,
        history_finder_display_pairs, HISTORY_FINDER_DISPLAY_INCREMENTAL);
    config_registry_bind_boolean("history.frecency.directory_context",
                                 &config.history_frecency_directory_context);
}

/// @brief Sync shell options from registry to runtime
static void shell_sync_to_runtime(void) {
    bool bval;
    char sval[CREG_VALUE_STRING_MAX];

    if (config_registry_get_string("shell.mode", sval, sizeof(sval)) ==
        CREG_SUCCESS) {
        /// Map mode string to enum via the canonical parser. Unknown
        /// names fall back to lush, preserving the prior behavior.
        shell_mode_t parsed;
        config.shell_mode =
            shell_mode_parse(sval, &parsed) ? parsed : SHELL_MODE_LUSH;
    }

    /// POSIX options - sync to shell_opts via config_set_shell_option
    if (config_registry_get_boolean("shell.errexit", &bval) == CREG_SUCCESS) {
        config_set_shell_option("errexit", bval);
    }
    if (config_registry_get_boolean("shell.nounset", &bval) == CREG_SUCCESS) {
        config_set_shell_option("nounset", bval);
    }
    if (config_registry_get_boolean("shell.xtrace", &bval) == CREG_SUCCESS) {
        config_set_shell_option("xtrace", bval);
    }
    if (config_registry_get_boolean("shell.pipefail", &bval) == CREG_SUCCESS) {
        config_set_shell_option("pipefail", bval);
    }
}

/// @brief Sync shell options from runtime to registry
static void shell_sync_from_runtime(void) {
    /// Map mode enum to its canonical string via shell_mode_name.
    const char *mode_str = shell_mode_name(config.shell_mode);
    config_registry_set_string("shell.mode", mode_str ? mode_str : "lush");

    /// POSIX options - read from shell_opts via config_get_shell_option
    config_registry_set_boolean("shell.errexit",
                                config_get_shell_option("errexit"));
    config_registry_set_boolean("shell.nounset",
                                config_get_shell_option("nounset"));
    config_registry_set_boolean("shell.xtrace",
                                config_get_shell_option("xtrace"));
    config_registry_set_boolean("shell.pipefail",
                                config_get_shell_option("pipefail"));
}

/// @brief Sync display config from registry to runtime
/// @brief Bind display config keys to their runtime cells.
/// The registry is the sole writer: config_registry_set write-throughs to the
/// struct field, so config set / TOML / per-mode defaults and the display
/// builtins (which route their on/off through config_registry_set) all keep
/// the struct in step without hand-written sync hooks.
static void display_bind_runtime(void) {
    config_registry_bind_boolean("display.syntax_highlighting",
                                 &config.display_syntax_highlighting);
    config_registry_bind_boolean("display.autosuggestions",
                                 &config.display_autosuggestions);
    config_registry_bind_boolean("display.transient_prompt",
                                 &config.display_transient_prompt);
    config_registry_bind_boolean("display.theme_hot_reload",
                                 &config.display_theme_hot_reload);
    config_registry_bind_boolean("display.newline_before_prompt",
                                 &config.display_newline_before_prompt);
    config_registry_bind_integer("display.optimization_level",
                                 &config.display_optimization_level);
    config_registry_bind_boolean("display.lle.pager.enabled",
                                 &config.display_lle_pager_enabled);
    config_registry_bind_integer("display.lle.pager.min_lines",
                                 &config.display_lle_pager_min_lines);
    config_registry_bind_boolean("display.lle.pager.wrap_search",
                                 &config.display_lle_pager_wrap_search);
    config_registry_bind_string_ptr("display.lle.theme",
                                    &config.display_lle_theme);
    config_registry_bind_string_ptr("display.ambiguous_width",
                                    &config.display_ambiguous_width);
}

/// @brief Sync completion config from registry to runtime
/// completion.match_mode enum-as-string mapping, applied by the binding on
/// change. (completion_match_mode_mappings is kept -- config_apply_settings
/// still uses it to publish the COMPLETION_MATCH_MODE script variable.)
static const creg_enum_pair_t completion_match_mode_pairs[] = {
    {   "prefix",    COMPLETION_MATCH_PREFIX},
    {"substring", COMPLETION_MATCH_SUBSTRING},
    {    "fuzzy",     COMPLETION_MATCH_FUZZY},
    {       NULL,                          0},
};

/// @brief Bind the struct-backed completion.* keys (replaces
/// completion_sync_*). completion.chain_directories and
/// completion.menu_shadow_ghost are read straight from the registry (no runtime
/// cell) and need no binding; completion.fuzzy is a dead legacy shim superseded
/// by match_mode.
static void completion_bind_runtime(void) {
    config_registry_bind_boolean("completion.enabled",
                                 &config.completion_enabled);
    config_registry_bind_boolean("completion.case_sensitive",
                                 &config.completion_case_sensitive);
    config_registry_bind_enum(
        "completion.match_mode", (int *)&config.completion_match_mode,
        completion_match_mode_pairs, COMPLETION_MATCH_PREFIX);
    config_registry_bind_integer("completion.threshold",
                                 &config.completion_threshold);
}

/// @brief Sync completion config from runtime to registry

/// @brief Sync behavior config from registry to runtime
/// @brief Bind the registry-backed behavior.* keys (replaces behavior_sync_*).
/// Only the three keys with a CREG section entry are bound; the many other
/// behavior.* keys still live only in the legacy table and migrate later.
static void behavior_bind_runtime(void) {
    config_registry_bind_boolean("behavior.auto_cd", &config.auto_cd);
    config_registry_bind_boolean("behavior.spell_correction",
                                 &config.spell_correction);
    config_registry_bind_boolean("behavior.confirm_exit", &config.confirm_exit);
    /// Limits migrated into CREG from the legacy table. tab_width is read in
    /// display hot paths -- its cell stays the plain config.tab_width int, the
    /// registry just becomes its sole writer.
    config_registry_bind_integer("behavior.tab_width", &config.tab_width);
    config_registry_bind_integer("behavior.brace_expansion_max",
                                 &config.brace_expansion_max);
    config_registry_bind_integer("behavior.regex_pattern_max",
                                 &config.regex_pattern_max);
    config_registry_bind_integer("behavior.path_negative_cache_ttl_ms",
                                 &config.path_negative_cache_ttl_ms);
    config_registry_bind_integer("behavior.loop_failure_streak",
                                 &config.loop_failure_streak);
    config_registry_bind_integer("behavior.loop_failure_seconds",
                                 &config.loop_failure_seconds);
    /// Autocorrect settings (migrated into CREG from the legacy table).
    config_registry_bind_integer("behavior.autocorrect_max_suggestions",
                                 &config.autocorrect_max_suggestions);
    config_registry_bind_integer("behavior.autocorrect_threshold",
                                 &config.autocorrect_threshold);
    config_registry_bind_boolean("behavior.autocorrect_interactive",
                                 &config.autocorrect_interactive);
    config_registry_bind_boolean("behavior.autocorrect_learn_history",
                                 &config.autocorrect_learn_history);
    config_registry_bind_boolean("behavior.autocorrect_builtins",
                                 &config.autocorrect_builtins);
    config_registry_bind_boolean("behavior.autocorrect_external",
                                 &config.autocorrect_external);
    config_registry_bind_boolean("behavior.autocorrect_case_sensitive",
                                 &config.autocorrect_case_sensitive);
}

/// lle.* enum-as-string mappings (registry string <-> engine enum), applied by
/// the bindings on change. Replace the deleted lle_sync_*'s strcmp ladders and
/// the legacy config_enum_def_t/config_enum_mapping_t tables.
static const creg_enum_pair_t lle_arrow_mode_pairs[] = {
    {  "context-aware",   LLE_ARROW_MODE_CONTEXT_AWARE},
    {        "classic",         LLE_ARROW_MODE_CLASSIC},
    { "always-history",  LLE_ARROW_MODE_ALWAYS_HISTORY},
    {"multiline-first", LLE_ARROW_MODE_MULTILINE_FIRST},
    {             NULL,                              0},
};
static const creg_enum_pair_t lle_dedup_scope_pairs[] = {
    {   "none",    LLE_DEDUP_SCOPE_NONE},
    {"session", LLE_DEDUP_SCOPE_SESSION},
    { "recent",  LLE_DEDUP_SCOPE_RECENT},
    { "global",  LLE_DEDUP_SCOPE_GLOBAL},
    {     NULL,                       0},
};
static const creg_enum_pair_t lle_dedup_strategy_pairs[] = {
    {       "ignore",        LLE_DEDUP_STRATEGY_IGNORE},
    {  "keep-recent",   LLE_DEDUP_STRATEGY_KEEP_RECENT},
    {"keep-frequent", LLE_DEDUP_STRATEGY_KEEP_FREQUENT},
    {        "merge",         LLE_DEDUP_STRATEGY_MERGE},
    {     "keep-all",      LLE_DEDUP_STRATEGY_KEEP_ALL},
    {           NULL,                                0},
};

/// @brief Bind the lle.* keys to their runtime cells (the keystone).
///
/// Replaces lle_sync_to_runtime and lle_sync_from_runtime. After this runs the
/// registry write-throughs config.lle_* on every change from any surface;
/// history_file uses the owned-pointer binding (the field is a char *, NULL
/// when unset).
static void lle_bind_runtime(void) {
    config_registry_bind_boolean("lle.enable_deduplication",
                                 &config.lle_enable_deduplication);
    config_registry_bind_enum("lle.dedup_scope", (int *)&config.lle_dedup_scope,
                              lle_dedup_scope_pairs, LLE_DEDUP_SCOPE_SESSION);
    config_registry_bind_enum(
        "lle.dedup_strategy", (int *)&config.lle_dedup_strategy,
        lle_dedup_strategy_pairs, LLE_DEDUP_STRATEGY_KEEP_RECENT);
    config_registry_bind_boolean("lle.dedup_navigation",
                                 &config.lle_dedup_navigation);
    config_registry_bind_boolean("lle.dedup_navigation_unique",
                                 &config.lle_dedup_navigation_unique);
    config_registry_bind_boolean("lle.dedup_unicode_normalize",
                                 &config.lle_dedup_unicode_normalize);
    config_registry_bind_enum(
        "lle.arrow_key_mode", (int *)&config.lle_arrow_key_mode,
        lle_arrow_mode_pairs, LLE_ARROW_MODE_CONTEXT_AWARE);
    config_registry_bind_string_ptr("lle.history_file",
                                    &config.lle_history_file);
    config_registry_bind_boolean("lle.enable_forensic_tracking",
                                 &config.lle_enable_forensic_tracking);
    config_registry_bind_boolean("lle.enable_history_cache",
                                 &config.lle_enable_history_cache);
    config_registry_bind_integer("lle.cache_size", &config.lle_cache_size);
}

/// autosuggestion.* enum-as-string mappings (registry string <-> engine enum),
/// applied by the binding on change. Replaces the deleted
/// autosuggestion_sync_*'s strcmp ladders.
static const creg_enum_pair_t autosuggestion_dismiss_policy_pairs[] = {
    {    "on_deviation",     AUTOSUGGESTION_DISMISS_ON_DEVIATION},
    {"on_word_boundary", AUTOSUGGESTION_DISMISS_ON_WORD_BOUNDARY},
    {              NULL,                                       0},
};
static const creg_enum_pair_t autosuggestion_rank_pairs[] = {
    {"frecency", AUTOSUGGESTION_RANK_FRECENCY},
    { "recency",  AUTOSUGGESTION_RANK_RECENCY},
    {      NULL,                            0},
};
static const creg_enum_pair_t autosuggestion_partial_accept_pairs[] = {
    {"path_segment", AUTOSUGGESTION_PARTIAL_ACCEPT_PATH_SEGMENT},
    {        "word",         AUTOSUGGESTION_PARTIAL_ACCEPT_WORD},
    {          NULL,                                          0},
};
static const creg_enum_pair_t autosuggestion_sources_pairs[] = {
    {"history_then_completion", AUTOSUGGESTION_SOURCES_HISTORY_THEN_COMPLETION},
    {                "history",                 AUTOSUGGESTION_SOURCES_HISTORY},
    {                     NULL,                                              0},
};

/// @brief Bind the autosuggestion.* keys to their runtime cells (replaces the
/// autosuggestion sync hooks; registry write-throughs config.autosuggestion_*).
static void autosuggestion_bind_runtime(void) {
    config_registry_bind_enum("autosuggestion.dismiss_policy",
                              (int *)&config.autosuggestion_dismiss_policy,
                              autosuggestion_dismiss_policy_pairs,
                              AUTOSUGGESTION_DISMISS_ON_DEVIATION);
    config_registry_bind_enum(
        "autosuggestion.rank", (int *)&config.autosuggestion_rank,
        autosuggestion_rank_pairs, AUTOSUGGESTION_RANK_FRECENCY);
    config_registry_bind_enum("autosuggestion.partial_accept",
                              (int *)&config.autosuggestion_partial_accept,
                              autosuggestion_partial_accept_pairs,
                              AUTOSUGGESTION_PARTIAL_ACCEPT_PATH_SEGMENT);
    config_registry_bind_enum("autosuggestion.sources",
                              (int *)&config.autosuggestion_sources,
                              autosuggestion_sources_pairs,
                              AUTOSUGGESTION_SOURCES_HISTORY_THEN_COMPLETION);
}

/**
 * @brief Register all config sections with the registry
 *
 * Called during config_init to set up the registry with all known sections.
 */
static void config_register_sections(void) {
    if (!config_registry_is_initialized()) {
        if (config_registry_init() != CREG_SUCCESS) {
            return;
        }
    }

    config_registry_register_section(&history_section);
    config_registry_register_section(&shell_section);
    config_registry_register_section(&display_section);
    config_registry_register_section(&completion_section);
    config_registry_register_section(&behavior_section);
    config_registry_register_section(&autosuggestion_section);
    config_registry_register_section(&lle_section);

    /// Bind sections to their runtime cells now that they are registered. From
    /// here, every change (mode preset, TOML load, config set) write-throughs
    /// to the runtime struct with no sync hook -- the keystone.
    history_bind_runtime();
    autosuggestion_bind_runtime();
    completion_bind_runtime();
    behavior_bind_runtime();
    display_bind_runtime();
    lle_bind_runtime();
}

/**
 * @brief Register per-mode default overrides for mode-aware options.
 *
 * Most config options use a single default that's right across all
 * modes. The handful that legitimately diverge per mode register
 * per-mode defaults here. apply_mode_preset() applies these on every
 * mode change.
 *
 * Called from config_init() right after config_register_sections() so
 * the registered options exist by the time we attach mode defaults to
 * them.
 */
static void config_register_per_mode_defaults(void) {
    /// completion.chain_directories: lush curates the fish-style
    /// auto-recurse-into-directory experience as a discoverability
    /// default; bash/zsh/posix all stop after one tab (script users
    /// expect a single insertion per keypress).
    creg_value_t bool_true = creg_value_boolean(true);
    creg_value_t bool_false = creg_value_boolean(false);

    config_registry_set_mode_default("completion.chain_directories",
                                     SHELL_MODE_LUSH, &bool_true);
    config_registry_set_mode_default("completion.chain_directories",
                                     SHELL_MODE_BASH, &bool_false);
    config_registry_set_mode_default("completion.chain_directories",
                                     SHELL_MODE_ZSH, &bool_false);
    config_registry_set_mode_default("completion.chain_directories",
                                     SHELL_MODE_POSIX, &bool_false);

    /// completion.menu_shadow_ghost: lush curates the fish/zsh-autosuggest
    /// style inline shadow of the menu's top candidate as a discoverability
    /// default that pairs with pre-navigation menus; bash/zsh/posix show the
    /// menu box alone, matching the readline/zle menus script users expect.
    config_registry_set_mode_default("completion.menu_shadow_ghost",
                                     SHELL_MODE_LUSH, &bool_true);
    config_registry_set_mode_default("completion.menu_shadow_ghost",
                                     SHELL_MODE_BASH, &bool_false);
    config_registry_set_mode_default("completion.menu_shadow_ghost",
                                     SHELL_MODE_ZSH, &bool_false);
    config_registry_set_mode_default("completion.menu_shadow_ghost",
                                     SHELL_MODE_POSIX, &bool_false);

    /// completion.match_mode: lush curates fuzzy (fzy-style
    /// subsequence with positional bonuses, via
    /// fuzzy_completion_score) as the modern UX default. posix /
    /// bash / zsh inherit prefix matching to align with the
    /// bash-readline + zsh-default behavior script authors expect.
    creg_value_t match_prefix = creg_value_string("prefix");
    creg_value_t match_fuzzy = creg_value_string("fuzzy");
    config_registry_set_mode_default("completion.match_mode", SHELL_MODE_LUSH,
                                     &match_fuzzy);
    config_registry_set_mode_default("completion.match_mode", SHELL_MODE_BASH,
                                     &match_prefix);
    config_registry_set_mode_default("completion.match_mode", SHELL_MODE_ZSH,
                                     &match_prefix);
    config_registry_set_mode_default("completion.match_mode", SHELL_MODE_POSIX,
                                     &match_prefix);

    /// history.search_mode: lush curates prefix navigation (type a prefix,
    /// up/down cycle matching history, cursor kept at the prefix). posix /
    /// bash / zsh inherit plain (browse all, cursor at end) to match the
    /// classic readline up-arrow script authors expect.
    creg_value_t search_plain = creg_value_string("plain");
    config_registry_set_mode_default("history.search_mode", SHELL_MODE_BASH,
                                     &search_plain);
    config_registry_set_mode_default("history.search_mode", SHELL_MODE_ZSH,
                                     &search_plain);
    config_registry_set_mode_default("history.search_mode", SHELL_MODE_POSIX,
                                     &search_plain);

    /// history.finder.*: lush curates a fuzzy, frecency-ranked Ctrl-R finder.
    /// posix / bash / zsh inherit the classic substring match ranked by
    /// recency (most recent first), preserving the reverse-i-search behavior
    /// script authors and interactive users of those shells expect.
    creg_value_t finder_match_substring = creg_value_string("substring");
    creg_value_t finder_rank_recency = creg_value_string("recency");
    config_registry_set_mode_default("history.finder.match", SHELL_MODE_BASH,
                                     &finder_match_substring);
    config_registry_set_mode_default("history.finder.match", SHELL_MODE_ZSH,
                                     &finder_match_substring);
    config_registry_set_mode_default("history.finder.match", SHELL_MODE_POSIX,
                                     &finder_match_substring);
    config_registry_set_mode_default("history.finder.rank", SHELL_MODE_BASH,
                                     &finder_rank_recency);
    config_registry_set_mode_default("history.finder.rank", SHELL_MODE_ZSH,
                                     &finder_rank_recency);
    config_registry_set_mode_default("history.finder.rank", SHELL_MODE_POSIX,
                                     &finder_rank_recency);

    /// autosuggestion.rank: lush ranks ghost-text candidates by frecency (the
    /// command you actually return to). posix / bash / zsh inherit recency
    /// (most recent prefix match), the classic Fish-style behavior.
    creg_value_t autosuggest_rank_recency = creg_value_string("recency");
    config_registry_set_mode_default("autosuggestion.rank", SHELL_MODE_BASH,
                                     &autosuggest_rank_recency);
    config_registry_set_mode_default("autosuggestion.rank", SHELL_MODE_ZSH,
                                     &autosuggest_rank_recency);
    config_registry_set_mode_default("autosuggestion.rank", SHELL_MODE_POSIX,
                                     &autosuggest_rank_recency);

    /// autosuggestion.sources: lush mixes in completion fallback (fish-style).
    /// posix / bash / zsh stay history-only, matching the history-driven
    /// autosuggestion plugins (zsh-autosuggestions) those users expect.
    creg_value_t autosuggest_sources_history = creg_value_string("history");
    config_registry_set_mode_default("autosuggestion.sources", SHELL_MODE_BASH,
                                     &autosuggest_sources_history);
    config_registry_set_mode_default("autosuggestion.sources", SHELL_MODE_ZSH,
                                     &autosuggest_sources_history);
    config_registry_set_mode_default("autosuggestion.sources", SHELL_MODE_POSIX,
                                     &autosuggest_sources_history);

    /// display.transient_prompt: lush curates transient prompts on -- after a
    /// command runs, its prompt collapses to a compact form so elaborate
    /// prompts do not litter the scrollback and the active prompt is the one
    /// that stands out. posix / bash / zsh inherit off: neither bash nor a
    /// vanilla zsh has transient prompts (zsh users opt in via plugins such as
    /// powerlevel10k), so the compat modes mirror those shells' scrollback.
    config_registry_set_mode_default("display.transient_prompt",
                                     SHELL_MODE_LUSH, &bool_true);
    config_registry_set_mode_default("display.transient_prompt",
                                     SHELL_MODE_BASH, &bool_false);
    config_registry_set_mode_default("display.transient_prompt", SHELL_MODE_ZSH,
                                     &bool_false);
    config_registry_set_mode_default("display.transient_prompt",
                                     SHELL_MODE_POSIX, &bool_false);
}

/**
 * @brief Handle legacy configuration keys that were removed or renamed
 *
 * This prevents warnings for deprecated configuration options in existing
 * .lushrc files.
 *
 * @param key The legacy configuration key
 * @param value The value being set (unused but required for API)
 * @return True if key was handled as legacy, false if unknown
 */
static bool config_handle_legacy_key(const char *key, const char *value
                                     __attribute__((unused))) {
    if (!key) {
        return false;
    }

    /// Legacy editor.use_lle option - LLE is now the only line editor
    if (strcmp(key, "editor.use_lle") == 0) {
        /// Silently ignore - LLE is always enabled
        return true;
    }

    /// Legacy completion.fuzzy BOOL replaced by completion.match_mode
    /// ENUM. Translate truthy -> fuzzy, falsy -> prefix so existing
    /// rc files keep working without warnings.
    if (strcmp(key, "completion.fuzzy") == 0) {
        bool truthy =
            value && (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                      strcmp(value, "on") == 0 || strcmp(value, "yes") == 0);
        creg_value_t v = creg_value_string(truthy ? "fuzzy" : "prefix");
        config_registry_set("completion.match_mode", &v);
        return true;
    }

    /// Legacy display configuration keys
    if (strcmp(key, "behavior.enhanced_display_mode") == 0) {
        /// Legacy key - replaced by layered display system
        /// Silently ignore to maintain compatibility with existing .lushrc
        /// files
        return true;
    }

    /// Handle legacy display.* configuration keys
    if (strncmp(key, "display.", 8) == 0) {
        /// Unknown display.* keys - likely legacy, handle gracefully
        const char *display_key = key + 8; /// Skip "display." prefix

        if (strcmp(display_key, "system_mode") == 0 ||
            strcmp(display_key, "layered_display") == 0) {
            /// Legacy display mode options - layered display is now exclusive
            /// Silently ignore to maintain compatibility with existing .lushrc
            /// files
            return true;
        }

        /// Valid current display keys - let normal processing handle them
        if (strcmp(display_key, "syntax_highlighting") == 0 ||
            strcmp(display_key, "autosuggestions") == 0 ||
            strcmp(display_key, "transient_prompt") == 0 ||
            strcmp(display_key, "performance_monitoring") == 0 ||
            strcmp(display_key, "optimization_level") == 0) {
            return false;
        }

        /// Other display.* keys are likely legacy - handle gracefully
        return true;
    }

    return false;
}

/// Legacy option name mapping for backward compatibility
typedef struct {
    const char *old_name;
    const char *new_name;
} legacy_option_mapping_t;

static legacy_option_mapping_t legacy_mappings[] = {
    /// History options
    {            "history_enabled",                      "history.enabled"},
    {               "history_size",                         "history.size"},
    {            "history_no_dups",                      "history.no_dups"},
    {         "history_timestamps",                   "history.timestamps"},

    /// Completion options
    {         "completion_enabled",                   "completion.enabled"},
    {      "completion_match_mode",                "completion.match_mode"},
    {       "completion_threshold",                 "completion.threshold"},
    {  "completion_case_sensitive",            "completion.case_sensitive"},
    {        "completion_show_all",                  "completion.show_all"},
    {              "hints_enabled",                     "completion.hints"},

    /// Behavior options
    {                    "auto_cd",                     "behavior.auto_cd"},
    {           "spell_correction",            "behavior.spell_correction"},
    {"autocorrect_max_suggestions", "behavior.autocorrect_max_suggestions"},
    {      "autocorrect_threshold",       "behavior.autocorrect_threshold"},
    {    "autocorrect_interactive",     "behavior.autocorrect_interactive"},
    {  "autocorrect_learn_history",   "behavior.autocorrect_learn_history"},
    {       "autocorrect_builtins",        "behavior.autocorrect_builtins"},
    {       "autocorrect_external",        "behavior.autocorrect_external"},
    { "autocorrect_case_sensitive",  "behavior.autocorrect_case_sensitive"},
    {               "confirm_exit",                "behavior.confirm_exit"},
    {                  "tab_width",                   "behavior.tab_width"},
    {        "brace_expansion_max",         "behavior.brace_expansion_max"},
    {          "regex_pattern_max",           "behavior.regex_pattern_max"},
    { "path_negative_cache_ttl_ms",  "behavior.path_negative_cache_ttl_ms"},
    {        "loop_failure_streak",         "behavior.loop_failure_streak"},
    {       "loop_failure_seconds",        "behavior.loop_failure_seconds"},
    {        "display_performance",       "display.performance_monitoring"},
    {       "display_optimization",           "display.optimization_level"},

    /// Network options
    {     "ssh_completion_enabled",               "network.ssh_completion"},
    {            "cache_ssh_hosts",              "network.cache_ssh_hosts"},
    {      "cache_timeout_minutes",        "network.cache_timeout_minutes"},
    {       "max_completion_hosts",         "network.max_completion_hosts"},

    /// Scripts options
    {           "script_execution",                    "scripts.execution"},

    {                         NULL,                                   NULL}
};

/**
 * @brief Find the new name for a legacy configuration option
 *
 * Maps old flat configuration keys to new dotted notation names.
 *
 * @param old_name Legacy option name
 * @return New option name, or NULL if not a legacy option
 */
static const char *find_new_name_for_legacy(const char *old_name) {
    for (int i = 0; legacy_mappings[i].old_name; i++) {
        if (strcmp(legacy_mappings[i].old_name, old_name) == 0) {
            return legacy_mappings[i].new_name;
        }
    }
    return NULL;
}

/**
 * @brief Validate shell option values
 *
 * Accepts true/false, 1/0, on/off as valid boolean values.
 *
 * @param value String value to validate
 * @return True if value is a valid boolean string
 */
bool config_validate_shell_option(const char *value) {
    return (strcmp(value, "true") == 0 || strcmp(value, "false") == 0 ||
            strcmp(value, "1") == 0 || strcmp(value, "0") == 0 ||
            strcmp(value, "on") == 0 || strcmp(value, "off") == 0);
}

/**
 * @brief Set a shell option using existing POSIX infrastructure
 *
 * Maps configuration system calls to existing shell_opts flags.
 * Handles the "shell." prefix stripping internally.
 *
 * @param option_name Full option name including "shell." prefix
 * @param value Boolean value to set
 */
void config_set_shell_option(const char *option_name, bool value) {
    /// Remove "shell." prefix to get the actual option name
    const char *opt_name = option_name + 6; /// Skip "shell."

    /// Map to existing shell option flags using the same names as builtin_set
    if (strcmp(opt_name, "errexit") == 0) {
        shell_opts.exit_on_error = value;
    } else if (strcmp(opt_name, "xtrace") == 0) {
        shell_opts.trace_execution = value;
    } else if (strcmp(opt_name, "noexec") == 0) {
        shell_opts.syntax_check = value;
    } else if (strcmp(opt_name, "nounset") == 0) {
        shell_opts.unset_error = value;
    } else if (strcmp(opt_name, "verbose") == 0) {
        shell_opts.verbose = value;
    } else if (strcmp(opt_name, "noglob") == 0) {
        shell_opts.no_globbing = value;
    } else if (strcmp(opt_name, "hashall") == 0) {
        shell_opts.hash_commands = value;
    } else if (strcmp(opt_name, "monitor") == 0) {
        shell_opts.job_control = value;
    } else if (strcmp(opt_name, "allexport") == 0) {
        shell_opts.allexport = value;
    } else if (strcmp(opt_name, "noclobber") == 0) {
        shell_opts.noclobber = value;
    } else if (strcmp(opt_name, "onecmd") == 0) {
        shell_opts.onecmd = value;
    } else if (strcmp(opt_name, "notify") == 0) {
        shell_opts.notify = value;
    } else if (strcmp(opt_name, "ignoreeof") == 0) {
        shell_opts.ignoreeof = value;
    } else if (strcmp(opt_name, "nolog") == 0) {
        shell_opts.nolog = value;
    } else if (strcmp(opt_name, "emacs") == 0) {
        shell_opts.emacs_mode = value;
        if (value) {
            shell_opts.vi_mode = false; /// Mutually exclusive
            lush_update_editing_mode();
        }
    } else if (strcmp(opt_name, "vi") == 0) {
        shell_opts.vi_mode = value;
        if (value) {
            shell_opts.emacs_mode = false; /// Mutually exclusive
            lush_update_editing_mode();
        }
    } else if (strcmp(opt_name, "posix") == 0) {
        shell_opts.posix_mode = value;
    } else if (strcmp(opt_name, "pipefail") == 0) {
        shell_opts.pipefail_mode = value;
    } else if (strcmp(opt_name, "histexpand") == 0) {
        shell_opts.histexpand_mode = value;
    } else if (strcmp(opt_name, "history") == 0) {
        shell_opts.history_mode = value;
    } else if (strcmp(opt_name, "interactive-comments") == 0) {
        shell_opts.interactive_comments_mode = value;
    } else if (strcmp(opt_name, "physical") == 0) {
        shell_opts.physical_mode = value;
    } else if (strcmp(opt_name, "privileged") == 0) {
        shell_opts.privileged_mode = value;
    }
}

/**
 * @brief Get a shell option value
 *
 * Retrieves the current value of a shell option from shell_opts.
 *
 * @param option_name Full option name including "shell." prefix
 * @return Current boolean value of the option
 */
bool config_get_shell_option(const char *option_name) {
    /// Remove "shell." prefix to get the actual option name
    const char *opt_name = option_name + 6; /// Skip "shell."

    /// Use existing shell option flags - same logic as builtin_set status
    /// display
    if (strcmp(opt_name, "errexit") == 0) {
        return shell_opts.exit_on_error;
    } else if (strcmp(opt_name, "xtrace") == 0) {
        return shell_opts.trace_execution;
    } else if (strcmp(opt_name, "noexec") == 0) {
        return shell_opts.syntax_check;
    } else if (strcmp(opt_name, "nounset") == 0) {
        return shell_opts.unset_error;
    } else if (strcmp(opt_name, "verbose") == 0) {
        return shell_opts.verbose;
    } else if (strcmp(opt_name, "noglob") == 0) {
        return shell_opts.no_globbing;
    } else if (strcmp(opt_name, "hashall") == 0) {
        return shell_opts.hash_commands;
    } else if (strcmp(opt_name, "monitor") == 0) {
        return shell_opts.job_control;
    } else if (strcmp(opt_name, "allexport") == 0) {
        return shell_opts.allexport;
    } else if (strcmp(opt_name, "noclobber") == 0) {
        return shell_opts.noclobber;
    } else if (strcmp(opt_name, "onecmd") == 0) {
        return shell_opts.onecmd;
    } else if (strcmp(opt_name, "notify") == 0) {
        return shell_opts.notify;
    } else if (strcmp(opt_name, "ignoreeof") == 0) {
        return shell_opts.ignoreeof;
    } else if (strcmp(opt_name, "nolog") == 0) {
        return shell_opts.nolog;
    } else if (strcmp(opt_name, "emacs") == 0) {
        return shell_opts.emacs_mode;
    } else if (strcmp(opt_name, "vi") == 0) {
        return shell_opts.vi_mode;
    } else if (strcmp(opt_name, "posix") == 0) {
        return shell_opts.posix_mode;
    } else if (strcmp(opt_name, "pipefail") == 0) {
        return shell_opts.pipefail_mode;
    } else if (strcmp(opt_name, "histexpand") == 0) {
        return shell_opts.histexpand_mode;
    } else if (strcmp(opt_name, "history") == 0) {
        return shell_opts.history_mode;
    } else if (strcmp(opt_name, "interactive-comments") == 0) {
        return shell_opts.interactive_comments_mode;
    } else if (strcmp(opt_name, "physical") == 0) {
        return shell_opts.physical_mode;
    } else if (strcmp(opt_name, "privileged") == 0) {
        return shell_opts.privileged_mode;
    }

    return false; /// Unknown option
}

/// Traditional shell script file paths
#define PROFILE_SCRIPT_FILE ".profile"
#define LOGIN_SCRIPT_FILE ".lush_login"
#define RC_SCRIPT_FILE ".lushrc"
#define LOGOUT_SCRIPT_FILE ".lush_logout"

/**
 * @brief Check if script execution is enabled
 *
 * @return True if script execution is enabled in configuration
 */
bool config_should_execute_scripts(void) { return config.script_execution; }

/**
 * @brief Enable or disable script execution
 *
 * @param enabled True to enable script execution, false to disable
 */
void config_set_script_execution(bool enabled) {
    config.script_execution = enabled;
}

/**
 * @brief Get the path to the profile script file
 *
 * Returns the full path to ~/.profile.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_profile_script_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }

    char *path = malloc(strlen(home) + strlen(PROFILE_SCRIPT_FILE) + 2);
    if (!path) {
        return NULL;
    }

    sprintf(path, "%s/%s", home, PROFILE_SCRIPT_FILE);
    return path;
}

/**
 * @brief Resolve a lush-specific script path, XDG default with ~/ fallback.
 *
 * Lush's per-user script lookup order is:
 *   1. `${XDG_CONFIG_HOME:-~/.config}/lush/<xdg_basename>`  -- canonical
 *   2. `~/<home_basename>`                                  -- fallback
 *
 * XDG is the default lush configuration location; `~/` is a fallback
 * for users not on XDG layouts (cross-shell adoption ergonomics). See
 * memory note [[config-file-conventions]].
 *
 * If the XDG path exists as a regular file, its path is returned.
 * Otherwise the home-dir fallback path is returned regardless of
 * whether the file exists (callers gate the actual source step on
 * config_script_exists), so the migration UX ("file not found"
 * messages) shows the legacy path the user is likely looking at.
 *
 * @param xdg_basename  basename under `<XDG>/lush/` (no leading slash)
 * @param home_basename basename under `~/` (typically `.<name>`)
 * @return malloc'd path string, or NULL on allocation / env failure
 */
static char *config_get_xdg_or_home_script(const char *xdg_basename,
                                           const char *home_basename) {
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) == 0) {
        char xdg_path[CONFIG_PATH_MAX];
        if ((size_t)snprintf(xdg_path, sizeof(xdg_path), "%s/%s", xdg_dir,
                             xdg_basename) < sizeof(xdg_path)) {
            struct stat st;
            if (stat(xdg_path, &st) == 0 && S_ISREG(st.st_mode)) {
                return strdup(xdg_path);
            }
        }
    }

    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }
    char *path = malloc(strlen(home) + strlen(home_basename) + 2);
    if (!path) {
        return NULL;
    }
    sprintf(path, "%s/%s", home, home_basename);
    return path;
}

/**
 * @brief Get the path to the login script file
 *
 * Returns `${XDG_CONFIG_HOME:-~/.config}/lush/lush_login` if it
 * exists, else `~/.lush_login`.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_login_script_path(void) {
    return config_get_xdg_or_home_script("lush_login", LOGIN_SCRIPT_FILE);
}

/**
 * @brief Get the path to the RC script file
 *
 * Returns `${XDG_CONFIG_HOME:-~/.config}/lush/lushrc` if it exists,
 * else `~/.lushrc`. XDG is canonical; `~/` is the adoption fallback.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_rc_script_path(void) {
    return config_get_xdg_or_home_script("lushrc", RC_SCRIPT_FILE);
}

/**
 * @brief Get the path to the logout script file
 *
 * Returns `${XDG_CONFIG_HOME:-~/.config}/lush/lush_logout` if it
 * exists, else `~/.lush_logout`. XDG is canonical; `~/` is the
 * adoption fallback. See config_get_xdg_or_home_script.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_logout_script_path(void) {
    return config_get_xdg_or_home_script("lush_logout", LOGOUT_SCRIPT_FILE);
}

/**
 * @brief Check if a script file exists and is readable
 *
 * @param path Path to the script file
 * @return True if file exists and is readable, false otherwise
 */
bool config_script_exists(const char *path) {
    if (!path) {
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    return S_ISREG(st.st_mode) && (access(path, R_OK) == 0);
}

/**
 * @brief Execute a shell script file
 *
 * Reads and executes each line of the script file, skipping
 * empty lines and comments.
 *
 * @param path Path to the script file
 * @return 0 on success, -1 on failure
 */
int config_execute_script_file(const char *path) {
    if (!path || !config_script_exists(path)) {
        return -1;
    }

    /// Use the same approach as bin_source builtin - read complete multi-line
    /// constructs instead of line by line to handle if/then/fi, while/do/done,
    /// etc.
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    /// Track source depth so 'return' builtin works correctly in sourced
    /// scripts Use get_global_executor() since parse_and_execute uses
    /// global_executor
    executor_t *executor = get_global_executor();
    bool saved_source_return = false;
    const char *saved_script_file = NULL;
    if (executor) {
        saved_source_return = executor->source_return;
        saved_script_file = executor_get_current_script_file(executor);
        if (saved_script_file) {
            saved_script_file = strdup(saved_script_file);
        }
        executor->source_depth++;
        executor->source_return = false;
        /// Set script context so breakpoints can match this file.
        executor_set_script_context(executor, path);
    }

    char *complete_input;
    int result = 0;
    /// 1-based file line of the first character of the next construct,
    /// threaded into parse_and_execute as the parser's starting line so
    /// node->loc.line carries the absolute source line.
    size_t source_line = 1;

    /// Read complete multi-line constructs (same as bin_source)
    while ((complete_input = get_input_complete(file)) != NULL) {
        /// Physical lines this construct spans, for advancing source_line.
        size_t construct_lines = 0;
        for (const char *p = complete_input; *p; p++) {
            if (*p == '\n') {
                construct_lines++;
            }
        }

        /// Skip empty constructs
        char *trimmed = complete_input;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;
        if (*trimmed == '\0') {
            free(complete_input);
            source_line += construct_lines;
            continue;
        }

        /// Parse and execute the complete construct at its true file line.
        int construct_result = parse_and_execute(complete_input, source_line);
        source_line += construct_lines;

        /// Check for return from sourced script (exit code 200+)
        /// This matches how bin_source handles the special return code
        if (construct_result >= 200 && construct_result <= 455) {
            result = construct_result - 200;
            if (executor) {
                executor->source_return = true;
            }
            free(complete_input);
            break;
        }

        if (construct_result != 0) {
            result = construct_result;
        }

        free(complete_input);

        /// Check if 'return' was called in the sourced script
        if (executor && executor->source_return) {
            break;
        }
    }

    /// Restore source depth, source_return state, and script context
    if (executor) {
        executor->source_depth--;
        executor->source_return = saved_source_return;
        executor_set_script_context(executor, saved_script_file);
        free((char *)saved_script_file);
    }

    fclose(file);
    return result;
}

/**
 * @brief Execute system profile scripts for login shells
 *
 * Sources system-wide configuration files in the following order:
 * 1. /etc/lushrc (lush-specific system config, if exists)
 * 2. /etc/profile (standard POSIX login config, if exists)
 * 3. /etc/profile.d/ shell scripts (*.sh files, alphabetically)
 *
 * This should be called BEFORE user profile scripts (~/.profile,
 * ~/.lush_login).
 *
 * @return 0 on success, -1 if any script fails (non-fatal, continues execution)
 */
int config_execute_system_profile(void) {
    if (!config_should_execute_scripts()) {
        return 0;
    }

    int result = 0;

    /// 1. Source /etc/lushrc if it exists (lush-specific system config)
    /// This runs in native lush mode since it's a lush-specific file
    if (config_script_exists("/etc/lushrc")) {
        if (config_execute_script_file("/etc/lushrc") != 0) {
            /// Log warning but continue - system config failure shouldn't
            /// block login.
            shell_error_t *err = shell_error_create(
                SHELL_ERR_SUBSYSTEM_INIT_FAILED, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "error sourcing /etc/lushrc");
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            result = -1;
        }
    }

    /// 2. Source /etc/profile if it exists (standard POSIX login config)
    /// Switch to bash mode for compatibility with system scripts
    /// Most /etc/profile.d scripts use bash extensions like [[ ]] and $-
    if (config_script_exists("/etc/profile")) {
        shell_mode_t saved_mode = shell_mode_get();
        shell_mode_set(SHELL_MODE_BASH);
        int script_result = config_execute_script_file("/etc/profile");
        shell_mode_set(saved_mode);
        if (script_result != 0) {
            shell_error_t *err = shell_error_create(
                SHELL_ERR_SUBSYSTEM_INIT_FAILED, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "error sourcing /etc/profile");
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            result = -1;
        }
    }

    /// Note: /etc/profile.d/*.sh files are NOT sourced directly here because
    /// /etc/profile already sources them via its internal loop. Sourcing them
    /// again would cause double-execution and potential issues with scripts
    /// that check for conditions like interactive mode or TTY status.

    return result;
}

/**
 * @brief Execute startup scripts for interactive shells
 *
 * Executes ~/.lushrc if it exists and script execution is enabled.
 *
 * @return 0 on success, -1 if any script fails
 */
int config_execute_startup_scripts(void) {
    if (!config_should_execute_scripts()) {
        return 0;
    }

    int result = 0;

    /// Execute .lushrc if it exists (lush-specific RC script)
    char *rc_path = config_get_rc_script_path();
    if (rc_path && config_script_exists(rc_path)) {
        if (config_execute_script_file(rc_path) != 0) {
            result = -1;
        }
    }
    free(rc_path);

    return result;
}

/**
 * @brief Execute login scripts for login shells
 *
 * Executes ~/.profile and ~/.lush_login if they exist
 * and script execution is enabled.
 *
 * @return 0 on success, -1 if any script fails
 */
int config_execute_login_scripts(void) {
    if (!config_should_execute_scripts()) {
        return 0;
    }

    int result = 0;

    /// Execute .profile if it exists (POSIX standard)
    /// Run in bash mode for better compatibility with user scripts
    /// that may use bash extensions
    char *profile_path = config_get_profile_script_path();
    if (profile_path && config_script_exists(profile_path)) {
        shell_mode_t saved_mode = shell_mode_get();
        shell_mode_set(SHELL_MODE_BASH);
        int script_result = config_execute_script_file(profile_path);
        shell_mode_set(saved_mode);
        if (script_result != 0) {
            result = -1;
        }
    }
    free(profile_path);

    /// Execute .lush_login if it exists (lush-specific login script)
    /// This runs in native lush mode since it's a lush-specific file
    char *login_path = config_get_login_script_path();
    if (login_path && config_script_exists(login_path)) {
        if (config_execute_script_file(login_path) != 0) {
            result = -1;
        }
    }
    free(login_path);

    return result;
}

/**
 * @brief Execute logout scripts when shell exits
 *
 * Executes ~/.lush_logout if it exists and script execution is enabled.
 *
 * @return 0 on success, -1 if script fails
 */
int config_execute_logout_scripts(void) {
    if (!config_should_execute_scripts()) {
        return 0;
    }

    int result = 0;

    /// Execute .lush_logout if it exists
    char *logout_path = config_get_logout_script_path();
    if (logout_path && config_script_exists(logout_path)) {
        if (config_execute_script_file(logout_path) != 0) {
            result = -1;
        }
    }
    free(logout_path);

    return result;
}

/// Configuration file template - uses dotted notation for all options
const char *CONFIG_FILE_TEMPLATE =
    "# LUSH Configuration File\n"
    "# Generated by: config reset-defaults\n"
    "# This file configures the behavior of the lush shell\n"
    "# Lines starting with # are comments\n"
    "# Uses dotted notation (e.g., history.enabled = true)\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# HISTORY SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Enable command history\n"
    "history.enabled = true\n"
    "\n"
    "# Maximum history entries\n"
    "history.size = 1000\n"
    "\n"
    "# Remove duplicate history entries\n"
    "history.no_dups = true\n"
    "\n"
    "# Add timestamps to history\n"
    "history.timestamps = false\n"
    "\n"
    "# History file path (default: ~/.lush_history)\n"
    "# history.file = ~/.lush_history\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# LLE (Lush Line Editor) SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Arrow key behavior mode:\n"
    "#   context-aware   - Smart behavior based on cursor position\n"
    "#   classic         - Traditional readline behavior\n"
    "#   always-history  - Always navigate history\n"
    "#   multiline-first - Prioritize multiline navigation\n"
    "lle.arrow_key_mode = context-aware\n"
    "\n"
    "# LLE history file path (default: ~/.lush_history)\n"
    "# lle.history_file = ~/.lush_history\n"
    "\n"
    "# Track metadata (timestamps, exit codes, cwd)\n"
    "lle.enable_forensic_tracking = true\n"
    "\n"
    "# Enable history deduplication\n"
    "lle.enable_deduplication = true\n"
    "\n"
    "# Deduplication scope:\n"
    "#   none    - No deduplication\n"
    "#   session - Deduplicate within session\n"
    "#   recent  - Deduplicate recent entries\n"
    "#   global  - Deduplicate entire history\n"
    "lle.dedup_scope = session\n"
    "\n"
    "# Deduplication strategy:\n"
    "#   ignore        - Ignore duplicates entirely\n"
    "#   keep-recent   - Keep the most recent entry\n"
    "#   keep-frequent - Keep the most frequent entry\n"
    "#   merge         - Merge duplicate metadata\n"
    "#   keep-all      - Keep all entries (no dedup)\n"
    "lle.dedup_strategy = keep-recent\n"
    "\n"
    "# Skip duplicates during history navigation (up/down arrows)\n"
    "lle.dedup_navigation = true\n"
    "\n"
    "# Show only unique entries during navigation session\n"
    "# (each command shown at most once per navigation session)\n"
    "lle.dedup_navigation_unique = true\n"
    "\n"
    "# Use Unicode NFC normalization for dedup comparison\n"
    "lle.dedup_unicode_normalize = true\n"
    "\n"
    "# Enable history caching for performance\n"
    "lle.enable_history_cache = true\n"
    "\n"
    "# History cache size\n"
    "lle.cache_size = 100\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# COMPLETION SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Enable tab completion\n"
    "completion.enabled = true\n"
    "\n"
    "# Completion match predicate: prefix | substring | fuzzy\n"
    "# Default: prefix (posix/bash/zsh), fuzzy (lush)\n"
    "completion.match_mode = prefix\n"
    "\n"
    "# Fuzzy matching threshold (0-100), applies when match_mode = fuzzy\n"
    "completion.threshold = 60\n"
    "\n"
    "# Case sensitive completion\n"
    "completion.case_sensitive = false\n"
    "\n"
    "# Show all completions\n"
    "completion.show_all = false\n"
    "\n"
    "# Enable input hints\n"
    "completion.hints = false\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# BEHAVIOR SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Auto-cd to directories (type directory name without cd)\n"
    "behavior.auto_cd = false\n"
    "\n"
    "# Enable command spell correction\n"
    "behavior.spell_correction = true\n"
    "\n"
    "# Maximum auto-correction suggestions (1-5)\n"
    "behavior.autocorrect_max_suggestions = 3\n"
    "\n"
    "# Auto-correction similarity threshold (0-100)\n"
    "behavior.autocorrect_threshold = 40\n"
    "\n"
    "# Show interactive correction prompts\n"
    "behavior.autocorrect_interactive = true\n"
    "\n"
    "# Learn commands from history\n"
    "behavior.autocorrect_learn_history = true\n"
    "\n"
    "# Suggest builtin corrections\n"
    "behavior.autocorrect_builtins = true\n"
    "\n"
    "# Suggest external command corrections\n"
    "behavior.autocorrect_external = true\n"
    "\n"
    "# Case-sensitive auto-correction\n"
    "behavior.autocorrect_case_sensitive = false\n"
    "\n"
    "# Confirm before exiting\n"
    "behavior.confirm_exit = false\n"
    "\n"
    "# Tab width for display\n"
    "behavior.tab_width = 4\n"
    "\n"
    "# Max brace expansion result count (0 = unbounded)\n"
    "behavior.brace_expansion_max = 65536\n"
    "\n"
    "# Max regex pattern length before rejection (0 = unbounded). Bounds "
    "compile time on pathological patterns fed to platform regcomp from "
    "[[ =~ ]] and extglob translation paths.\n"
    "behavior.regex_pattern_max = 1024\n"
    "\n"
    "# TTL in milliseconds for negative PATH-search cache (0 = disabled). "
    "Bounds syscall cost of tight loops calling a missing command to O(1) "
    "instead of O(PATH_dirs).\n"
    "behavior.path_negative_cache_ttl_ms = 1000\n"
    "\n"
    "# Consecutive non-zero body iterations before runaway-loop trip (0 = "
    "disable)\n"
    "behavior.loop_failure_streak = 1000\n"
    "\n"
    "# Min wall-clock seconds streak must last before tripping\n"
    "behavior.loop_failure_seconds = 5\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# NETWORK SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Enable SSH host completion\n"
    "network.ssh_completion = true\n"
    "\n"
    "# Cache SSH hosts for performance\n"
    "network.cache_ssh_hosts = true\n"
    "\n"
    "# SSH host cache timeout in minutes\n"
    "network.cache_timeout_minutes = 5\n"
    "\n"
    "# Maximum hosts to show in completion\n"
    "network.max_completion_hosts = 50\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# DISPLAY SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Enable display performance monitoring\n"
    "display.performance_monitoring = false\n"
    "\n"
    "# Display optimization level (0-4)\n"
    "display.optimization_level = 0\n"
    "\n"
    "# LLE pager: master switch for paginated output\n"
    "display.lle.pager.enabled = true\n"
    "\n"
    "# LLE pager: row threshold (0 = use terminal rows)\n"
    "display.lle.pager.min_lines = 0\n"
    "\n"
    "# LLE pager: wrap search to top on no-match (less-style)\n"
    "display.lle.pager.wrap_search = true\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# SCRIPT SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Enable script execution\n"
    "scripts.execution = true\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# SHELL OPTIONS\n"
    "# "
    "=========================================================================="
    "==\n"
    "# These map directly to POSIX shell options (set -o)\n"
    "\n"
    "# Exit on command failure (set -e)\n"
    "# shell.errexit = false\n"
    "\n"
    "# Trace command execution (set -x)\n"
    "# shell.xtrace = false\n"
    "\n"
    "# Syntax check only (set -n)\n"
    "# shell.noexec = false\n"
    "\n"
    "# Error on unset variables (set -u)\n"
    "# shell.nounset = false\n"
    "\n"
    "# Print input lines (set -v)\n"
    "# shell.verbose = false\n"
    "\n"
    "# Disable pathname expansion (set -f)\n"
    "# shell.noglob = false\n"
    "\n"
    "# Pipeline failure detection (set -o pipefail)\n"
    "# shell.pipefail = false\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# ALIASES\n"
    "# "
    "=========================================================================="
    "==\n"
    "[aliases]\n"
    "# Define custom aliases here\n"
    "# ll = ls -l\n"
    "# la = ls -la\n"
    "\n"
    "# "
    "=========================================================================="
    "==\n"
    "# KEY BINDINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "[keys]\n"
    "# Custom key bindings (future feature)\n"
    "# ctrl-r = reverse-search\n";

/**
 * @brief Initialize the configuration system
 *
 * Sets default values, loads system and user config files,
 * and applies the loaded settings.
 *
 * @return 0 on success
 */
int config_init(void) {
    /// Set default values
    config_set_defaults();

    /// Initialize context
    memset(&config_ctx, 0, sizeof(config_ctx));

    /// Initialize config registry and register all sections
    config_register_sections();

    /// Register per-mode default overrides. Must come after section
    /// registration (the options must exist before per-mode defaults
    /// can attach to them), and before the per-mode defaults apply
    /// call below.
    config_register_per_mode_defaults();

    /// The registry's section defaults include shell.mode = "lush". The
    /// active mode (set earlier by apply_mode_preset() from CLI flag /
    /// shebang peek / default lush) may differ from "lush". Write the
    /// active mode into the registry now so subsequent sync_to_runtime
    /// calls (after user-config load) see the right value -- otherwise
    /// the registry's section default would clobber a CLI-flag mode
    /// when sync runs.
    config_registry_set_string("shell.mode", shell_mode_name(shell_mode_get()));

    /// Apply per-mode default overrides for the active shell mode before
    /// loading user config, so any user lushrc settings layer on top of
    /// the right preset.
    config_registry_apply_mode_defaults(shell_mode_get());

    /// Push the freshly-applied per-mode defaults into the runtime struct now,
    /// before the user-config-file existence checks below. On a fresh install
    /// with no lushrc the user-config load path (which is what otherwise runs
    /// sync_to_runtime) never executes, so without this the active mode's
    /// curated defaults -- lush's completion.match_mode=fuzzy,
    /// history.search_mode=prefix, etc. -- would never reach the struct the
    /// engine reads. User config, if present, layers on top via a later sync.
    config_registry_sync_to_runtime();

    /// Get XDG directory path
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) == 0) {
        config_ctx.xdg_config_dir = strdup(xdg_dir);
    }

    /// Check for XDG config file
    char xdg_path[CONFIG_PATH_MAX];
    char legacy_path[CONFIG_PATH_MAX];
    struct stat st;
    bool xdg_exists = false;
    bool legacy_exists = false;

    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
        xdg_exists = (stat(xdg_path, &st) == 0 && S_ISREG(st.st_mode));
    }

    if (config_get_legacy_config_path(legacy_path, sizeof(legacy_path)) == 0) {
        legacy_exists = (stat(legacy_path, &st) == 0 && S_ISREG(st.st_mode));
        if (legacy_exists) {
            config_ctx.legacy_config_path = strdup(legacy_path);
        }
    }

    /// Determine which config to load and set format
    if (xdg_exists) {
        /// Prefer XDG config
        config_ctx.user_config_path = strdup(xdg_path);
        config_ctx.format = CONFIG_FORMAT_TOML;
        config_ctx.needs_migration = false;
    } else if (legacy_exists) {
        /// Use legacy config, mark for migration
        config_ctx.user_config_path = strdup(legacy_path);
        config_ctx.format = CONFIG_FORMAT_LEGACY;
        config_ctx.needs_migration = true;
    } else {
        /// No config exists - use XDG path for new config
        config_ctx.user_config_path = strdup(xdg_path);
        config_ctx.format = CONFIG_FORMAT_TOML;
        config_ctx.needs_migration = false;
    }

    config_ctx.system_config_path = config_get_system_config_path();

    /// Check if config files exist
    config_ctx.user_config_exists =
        (config_ctx.user_config_path &&
         stat(config_ctx.user_config_path, &st) == 0);
    config_ctx.system_config_exists =
        (config_ctx.system_config_path &&
         stat(config_ctx.system_config_path, &st) == 0);

    /// Load system config first, then user config (ignore errors)
    if (config_ctx.system_config_exists) {
        config_load_system();
    }

    if (config_ctx.user_config_exists) {
        config_load_user();

        /// Print migration notice if loading legacy config
        if (config_ctx.needs_migration) {
            fprintf(stderr,
                    "lush: Loading configuration from %s (legacy location)\n",
                    config_ctx.user_config_path);
            fprintf(stderr, "lush: Run 'config save' to migrate to %s\n",
                    xdg_path);
        }
    }

    /// Apply loaded settings (always safe to call with defaults)
    config_apply_settings();

    return 0;
}

/**
 * @brief Set default configuration values
 *
 * Initializes all configuration options to their default values.
 * Called during config_init before loading config files.
 */
void config_set_defaults(void) {
    /// History defaults
    config.history_enabled = true;
    config.history_size = 1000;
    config.history_no_dups = true;
    config.history_timestamps = false;
    /// Up/down navigation filters by the typed prefix (zsh-style) by default;
    /// the per-mode table sets plain for posix/bash/zsh.
    config.history_search_mode = HISTORY_SEARCH_MODE_PREFIX;
    /// Ctrl-R finder: lush curates fuzzy matching ranked by frecency; the
    /// per-mode table sets substring + recency for posix/bash/zsh. picker is
    /// reserved and falls back to the incremental finder everywhere.
    config.history_finder_match = HISTORY_FINDER_MATCH_FUZZY;
    config.history_finder_rank = HISTORY_FINDER_RANK_FRECENCY;
    config.history_finder_display = HISTORY_FINDER_DISPLAY_INCREMENTAL;
    /// Frecency ranking prefers commands recorded in the current directory.
    config.history_frecency_directory_context = true;

    /// LLE History defaults
    config.lle_arrow_key_mode = LLE_ARROW_MODE_CONTEXT_AWARE;
    /// lle.history_file is owned by its string-pointer binding; release any
    /// prior value before re-defaulting so a re-init (config reload) does not
    /// orphan it. free(NULL) is safe on first init.
    free(config.lle_history_file);
    config.lle_history_file = NULL; /// Will default to ~/.lush_history
    config.lle_enable_forensic_tracking = true;
    config.lle_enable_deduplication = true;
    config.lle_dedup_scope = LLE_DEDUP_SCOPE_SESSION;
    config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_KEEP_RECENT;
    config.lle_dedup_navigation =
        true; /// Skip duplicates when navigating history
    config.lle_dedup_navigation_unique =
        true; /// Show only unique entries during navigation session
    config.lle_dedup_unicode_normalize =
        true; /// Use Unicode NFC normalization for comparison
    config.lle_enable_history_cache = true;
    config.lle_cache_size = 100;

    /// Completion defaults
    config.completion_enabled = true;
    config.completion_match_mode = COMPLETION_MATCH_PREFIX;
    config.completion_threshold = 60;
    config.completion_case_sensitive = false;
    config.completion_show_all = false;
    config.hints_enabled = false;

    /// Behavior defaults
    config.auto_cd = false;
    config.spell_correction = true;
    config.confirm_exit = false;
    config.tab_width = 4;
    config.brace_expansion_max = 65536;
    config.regex_pattern_max = 1024;
    config.path_negative_cache_ttl_ms = 1000;
    config.loop_failure_streak = 1000;
    config.loop_failure_seconds = 5;

    /// Auto-correction defaults
    config.autocorrect_max_suggestions = 3;
    config.autocorrect_threshold = 40;
    config.autocorrect_interactive = true;
    config.autocorrect_learn_history = true;
    config.autocorrect_builtins = true;
    config.autocorrect_external = true;
    config.autocorrect_case_sensitive = false;

    /// Network defaults
    config.ssh_completion_enabled = true;
    config.cache_ssh_hosts = true;
    config.cache_timeout_minutes = 5;
    config.max_completion_hosts = 50;

    /// Display defaults
    /// v1.3.0: Layered display is now the exclusive system - no configuration
    /// needed
    config.display_syntax_highlighting = true;
    config.display_autosuggestions = true;
    /// Autosuggestion ghost text persists through matching keystrokes (incl.
    /// spaces) and clears only on a deviation -- the intuitive default.
    config.autosuggestion_dismiss_policy = AUTOSUGGESTION_DISMISS_ON_DEVIATION;
    /// Suggest the most-frecent prefix match; per-mode table sets recency for
    /// posix/bash/zsh.
    config.autosuggestion_rank = AUTOSUGGESTION_RANK_FRECENCY;
    /// Ctrl+Right advances by path segment inside a path, whole word elsewhere.
    config.autosuggestion_partial_accept =
        AUTOSUGGESTION_PARTIAL_ACCEPT_PATH_SEGMENT;
    /// History is primary; completion fills in on a miss. Per-mode table sets
    /// history-only for posix/bash/zsh.
    config.autosuggestion_sources =
        AUTOSUGGESTION_SOURCES_HISTORY_THEN_COMPLETION;
    config.display_transient_prompt =
        true; /// Transient prompts enabled by default (Spec 25 Section 12)
    config.display_theme_hot_reload = true; /// Auto-reload theme on file change
    config.display_newline_before_prompt =
        true; /// Visual separation before prompt (default on)
    config.display_performance_monitoring = false;
    config.display_optimization_level = 0;
    /// East Asian Ambiguous-width policy. Default "narrow" matches
    /// traditional wcwidth behavior and what most modern terminals
    /// (xterm, iTerm2, Alacritty, WezTerm with default settings)
    /// render. Users on Asian-language terminals that render
    /// ambiguous-class chars as 2 columns can set this to "wide".
    if (config.display_ambiguous_width) {
        free(config.display_ambiguous_width);
    }
    config.display_ambiguous_width = strdup("narrow");
    /// display.lle.theme is owned by its string-pointer binding; release any
    /// prior value before re-defaulting so a re-init does not orphan it.
    free(config.display_lle_theme);
    config.display_lle_theme = NULL;
    config.display_lle_pager_enabled = true;
    config.display_lle_pager_min_lines = 0;
    config.display_lle_pager_wrap_search = true;

    /// Script execution defaults
    config.script_execution = true;

    /// Shell mode defaults: config.shell_mode is owned by
    /// apply_mode_preset() (called at startup before config_init from
    /// CLI flag / shebang peek / default lush; called at runtime by the
    /// `mode` builtin and the `set -o posix` bridge). Do NOT reset it
    /// here -- doing so would clobber the early-init mode resolution.
    config.shell_mode_strict = false; /// Allow runtime mode changes

    /// Line editor - LLE is always enabled (sole line editor)
    /// LLE is the only line editor - no config option needed
}

/* ============================================================================
 * XDG Path Resolution Helpers
 * ============================================================================
 */

/**
 * @brief Get the user's home directory
 *
 * Checks HOME environment variable first, then falls back to passwd entry.
 *
 * @return Home directory path, or NULL if not found
 */
static const char *get_home_directory(void) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return pw->pw_dir;
    }

    return NULL;
}

/**
 * @brief Create a directory and all parent directories recursively
 *
 * @param path Directory path to create
 * @return 0 on success, -1 on failure
 */
static int mkdir_recursive(const char *path) {
    char tmp[CONFIG_PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/**
 * @brief Get the XDG config directory path
 *
 * Returns ~/.config/lush or $XDG_CONFIG_HOME/lush.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_xdg_dir(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }

    /// Check XDG_CONFIG_HOME first
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        snprintf(buffer, size, "%s/%s", xdg_config, CONFIG_XDG_DIR);
        return 0;
    }

    /// Fall back to ~/.config
    const char *home = get_home_directory();
    if (!home) {
        return -1;
    }

    snprintf(buffer, size, "%s/.config/%s", home, CONFIG_XDG_DIR);
    return 0;
}

/**
 * @brief Get the XDG config file path
 *
 * Returns the path to lushrc.toml in the XDG directory.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_xdg_config_path(char *buffer, size_t size) {
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) != 0) {
        return -1;
    }

    int written = snprintf(buffer, size, "%s/%s", xdg_dir, CONFIG_XDG_FILE);
    if (written < 0 || (size_t)written >= size) {
        return -1; /// Path truncated
    }
    return 0;
}

/**
 * @brief Get the legacy config file path
 *
 * Returns the path to ~/.lushrc.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_legacy_config_path(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }

    const char *home = get_home_directory();
    if (!home) {
        return -1;
    }

    snprintf(buffer, size, "%s/%s", home, USER_CONFIG_FILE);
    return 0;
}

/**
 * @brief Get the path to the shell script config file
 *
 * Returns the path to lushrc (sourced after lushrc.toml).
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_script_config_path(char *buffer, size_t size) {
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) != 0) {
        return -1;
    }

    snprintf(buffer, size, "%s/%s", xdg_dir, CONFIG_XDG_SCRIPT);
    return 0;
}

/**
 * @brief Check if legacy config needs migration
 *
 * @return true if legacy config exists and XDG config does not
 */
bool config_needs_migration(void) { return config_ctx.needs_migration; }

/**
 * @brief Ensure XDG config directory exists
 *
 * Creates ~/.config/lush if it doesn't exist.
 *
 * @return 0 on success, -1 on error
 */
static int ensure_xdg_dir_exists(void) {
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) != 0) {
        return -1;
    }

    struct stat st;
    if (stat(xdg_dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    return mkdir_recursive(xdg_dir);
}

/**
 * @brief Migrate legacy config to XDG location
 *
 * Converts ~/.lushrc to ~/.config/lush/lushrc.toml format.
 * This is a placeholder - full implementation requires the registry.
 *
 * @return 0 on success, -1 on error
 */
int config_migrate_to_xdg(void) {
    if (!config_ctx.needs_migration) {
        return 0; /// Nothing to migrate
    }

    /// Ensure XDG directory exists
    if (ensure_xdg_dir_exists() != 0) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "failed to create XDG config directory");
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        return -1;
    }

    /// Get XDG config path
    char xdg_path[CONFIG_PATH_MAX];
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) != 0) {
        return -1;
    }

    /// Save current config to XDG location in TOML format
    /// Note: This will be enhanced once registry is wired up
    if (config_save_file(xdg_path) != 0) {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "failed to save config to %s", xdg_path);
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        return -1;
    }

    /// Update context
    free(config_ctx.user_config_path);
    config_ctx.user_config_path = strdup(xdg_path);
    config_ctx.format = CONFIG_FORMAT_TOML;
    config_ctx.needs_migration = false;

    fprintf(stderr, "lush: Configuration saved to %s\n", xdg_path);
    fprintf(stderr, "lush: You may remove the old %s file\n",
            config_ctx.legacy_config_path ? config_ctx.legacy_config_path
                                          : "~/.lushrc");

    return 0;
}

/**
 * @brief Get the path to the user's configuration file
 *
 * Returns the XDG config path if it exists, otherwise the legacy path
 * if it exists. If neither exists, returns the XDG path for new config.
 *
 * @return Allocated path string (caller must free), or NULL on failure
 */
char *config_get_user_config_path(void) {
    char xdg_path[CONFIG_PATH_MAX];
    char legacy_path[CONFIG_PATH_MAX];
    struct stat st;

    /// Try XDG path first
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
        if (stat(xdg_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(xdg_path);
        }
    }

    /// Try legacy path
    if (config_get_legacy_config_path(legacy_path, sizeof(legacy_path)) == 0) {
        if (stat(legacy_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(legacy_path);
        }
    }

    /// Neither exists - return XDG path for new config creation
    if (xdg_path[0] != '\0') {
        return strdup(xdg_path);
    }

    /// Fallback to legacy if we couldn't determine XDG path
    if (legacy_path[0] != '\0') {
        return strdup(legacy_path);
    }

    return NULL;
}

/**
 * @brief Get the path to the system configuration file
 *
 * Returns the path to /etc/lush/lushrc.
 *
 * @return Allocated path string (caller must free)
 */
char *config_get_system_config_path(void) { return strdup(SYSTEM_CONFIG_FILE); }

/**
 * @brief Load user configuration file
 *
 * @return 0 on success, -1 on failure
 */
int config_load_user(void) {
    return config_load_file(config_ctx.user_config_path);
}

/**
 * @brief Load system configuration file
 *
 * @return 0 on success, -1 on failure
 */
int config_load_system(void) {
    return config_load_file(config_ctx.system_config_path);
}

/**
 * @brief Save user configuration to file
 *
 * When saving, always uses the XDG TOML location for new saves.
 * This enables automatic migration from legacy ~/.lushrc to
 * ~/.config/lush/lushrc.toml format.
 *
 * @return 0 on success, -1 on failure
 */
int config_save_user(void) {
    char xdg_path[CONFIG_PATH_MAX];

    /// Always save to XDG location in TOML format
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
        /// Ensure XDG directory exists
        char xdg_dir[CONFIG_PATH_MAX];
        if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) == 0) {
            struct stat st;
            if (stat(xdg_dir, &st) != 0) {
                /// Directory doesn't exist, create it
                if (mkdir(xdg_dir, 0755) != 0 && errno != EEXIST) {
                    int saved_errno = errno;
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN,
                        "failed to create config directory %s: %s", xdg_dir,
                        strerror(saved_errno));
                    if (err) {
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    }
                    /// Fall back to current path
                    if (config_ctx.user_config_path) {
                        return config_save_file(config_ctx.user_config_path);
                    }
                    return -1;
                }
            }
        }

        int result = config_save_file(xdg_path);

        if (result == 0) {
            /// Update context to point to new location
            if (config_ctx.needs_migration) {
                fprintf(stderr, "lush: Configuration saved to %s\n", xdg_path);
                fprintf(stderr, "lush: You may remove the old %s file\n",
                        config_ctx.legacy_config_path
                            ? config_ctx.legacy_config_path
                            : "~/.lushrc");
                config_ctx.needs_migration = false;
            }

            /// Update user_config_path to the new TOML location
            free(config_ctx.user_config_path);
            config_ctx.user_config_path = strdup(xdg_path);
            config_ctx.format = CONFIG_FORMAT_TOML;
        }

        return result;
    }

    /// Fallback to existing path if XDG resolution failed
    if (!config_ctx.user_config_path) {
        return -1;
    }
    return config_save_file(config_ctx.user_config_path);
}

/**
 * @brief Save configuration to a file
 *
 * Writes configuration to the specified file. Format is auto-detected:
 * - Files ending in .toml use TOML format via config registry
 * - Other files use legacy INI-style format
 *
 * @param path Path to the configuration file
 * @return 0 on success, -1 on failure
 */
int config_save_file(const char *path) {
    if (!path) {
        return -1;
    }

    /// Detect file format based on extension
    size_t path_len = strlen(path);
    bool is_toml = (path_len > 5 &&
                    lle_unicode_strings_equal(path + path_len - 5, ".toml",
                                              &LLE_UNICODE_COMPARE_DEFAULT));

    if (is_toml) {
        /// Sync current runtime config to registry before saving
        config_registry_sync_from_runtime();

        /// Use registry's TOML save
        creg_result_t result = config_registry_save(path);
        return (result == CREG_SUCCESS) ? 0 : -1;
    }

    /// Legacy INI-style format
    FILE *file = fopen(path, "w");
    if (!file) {
        return -1;
    }

    /// Write header comment
    fprintf(file, "# Lush Configuration File\n");
    fprintf(file, "# This file is automatically generated by 'config save'\n");
    fprintf(file, "# Lines starting with # are comments\n");
    fprintf(
        file,
        "# This file uses dotted notation (e.g. history.enabled = true)\n\n");

    /// Write all options using full dotted names (no sections)
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        /// Handle shell options specially
        if (opt->section == CONFIG_SECTION_SHELL) {
            bool value = config_get_shell_option(opt->name);
            fprintf(file, "%s = %s\n", opt->name, value ? "true" : "false");
        } else {
            /// Handle regular options
            switch (opt->type) {
            case CONFIG_TYPE_BOOL:
                fprintf(file, "%s = %s\n", opt->name,
                        *(bool *)opt->value_ptr ? "true" : "false");
                break;
            case CONFIG_TYPE_INT:
                fprintf(file, "%s = %d\n", opt->name, *(int *)opt->value_ptr);
                break;
            case CONFIG_TYPE_STRING:
            case CONFIG_TYPE_COLOR:
                if (*(char **)opt->value_ptr &&
                    strlen(*(char **)opt->value_ptr) > 0) {
                    fprintf(file, "%s = %s\n", opt->name,
                            *(char **)opt->value_ptr);
                }
                /// Skip NULL or empty strings entirely to avoid parsing issues
                break;
            case CONFIG_TYPE_ENUM:
                if (opt->enum_def && opt->enum_def->mappings) {
                    int current_value = *(int *)opt->value_ptr;
                    const config_enum_mapping_t *mapping =
                        opt->enum_def->mappings;
                    while (mapping->name) {
                        if (mapping->value == current_value) {
                            fprintf(file, "%s = %s\n", opt->name,
                                    mapping->name);
                            break;
                        }
                        mapping++;
                    }
                }
                break;
            }
        }
    }

    fclose(file);
    return 0;
}

/**
 * @brief Load configuration from a file
 *
 * Reads and parses a configuration file. Automatically detects format:
 * - Files ending in .toml use the TOML parser via config registry
 * - Other files use the legacy INI-style parser
 *
 * @param path Path to the configuration file
 * @return 0 on success, -1 if file cannot be opened
 */
int config_load_file(const char *path) {
    if (!path) {
        return -1;
    }

    /// Detect file format based on extension
    size_t path_len = strlen(path);
    bool is_toml = (path_len > 5 &&
                    lle_unicode_strings_equal(path + path_len - 5, ".toml",
                                              &LLE_UNICODE_COMPARE_DEFAULT));

    if (is_toml) {
        /// Use TOML parser via config registry
        creg_result_t result = config_registry_load(path);
        if (result == CREG_SUCCESS) {
            /// Sync registry values to runtime config struct
            config_registry_sync_to_runtime();
            config_ctx.format = CONFIG_FORMAT_TOML;
            return 0;
        }
        /// Fall through to legacy parser on failure
        {
            shell_error_t *err =
                shell_error_create(SHELL_ERR_INVALID_ARGUMENT,
                                   SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                   "TOML parsing failed, trying legacy format");
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
        }
    }

    /// Legacy INI-style parser
    config_ctx.format = CONFIG_FORMAT_LEGACY;

    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    config_ctx.current_file = path;
    config_ctx.line_number = 0;
    current_section = CONFIG_SECTION_NONE;

    char line[MAX_CONFIG_LINE];
    while (fgets(line, sizeof(line), file)) {
        config_ctx.line_number++;

        /// Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (config_parse_line(line, config_ctx.line_number, path) != 0) {
            source_location_t loc = {.filename = path,
                                     .line = config_ctx.line_number,
                                     .column = 0,
                                     .offset = 0,
                                     .length = 0};
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING, loc,
                "error parsing config line");
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
        }
    }

    fclose(file);

    /// After loading legacy config, sync values to registry
    config_registry_sync_from_runtime();

    return 0;
}

/**
 * @brief Parse a single configuration line
 *
 * Handles comments, section headers, and key=value pairs.
 *
 * @param line Line to parse
 * @param line_num Line number for error messages
 * @param filename File name for error messages
 * @return 0 on success, -1 on parse error
 */
int config_parse_line(const char *line, int line_num, const char *filename) {
    /// Skip empty lines and comments
    const char *trimmed = line;
    while (isspace(*trimmed)) {
        trimmed++;
    }

    if (*trimmed == '\0' || *trimmed == '#') {
        return 0;
    }

    /// Helper closure-style scaffolding: each error site below needs a
    /// source_location_t pointing into the config file. Building it
    /// inline keeps the migration symmetric with the executor's error
    /// sites, per feedback-direct-api-error-system.

    /// Check for section header
    if (*trimmed == '[') {
        const char *end = strchr(trimmed, ']');
        if (!end) {
            source_location_t loc = {.filename = filename,
                                     .line = (size_t)line_num,
                                     .column = 0,
                                     .offset = 0,
                                     .length = 0};
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "invalid section header (missing closing ']')");
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            return -1;
        }

        char section_name[64];
        size_t section_len = end - trimmed - 1;
        if (section_len >= sizeof(section_name)) {
            source_location_t loc = {.filename = filename,
                                     .line = (size_t)line_num,
                                     .column = 0,
                                     .offset = 0,
                                     .length = 0};
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "section name too long (max %zu chars)",
                sizeof(section_name) - 1);
            if (err) {
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            return -1;
        }

        strncpy(section_name, trimmed + 1, section_len);
        section_name[section_len] = '\0';

        return config_parse_section(section_name);
    }

    /// Parse key=value pair
    char *equals = strchr(trimmed, '=');
    if (!equals) {
        source_location_t loc = {.filename = filename,
                                 .line = (size_t)line_num,
                                 .column = 0,
                                 .offset = 0,
                                 .length = 0};
        shell_error_t *err = shell_error_create(
            SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
            "invalid configuration line (expected key=value)");
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        return -1;
    }

    /// Extract key
    char key[128];
    size_t key_len = equals - trimmed;
    if (key_len >= sizeof(key)) {
        source_location_t loc = {.filename = filename,
                                 .line = (size_t)line_num,
                                 .column = 0,
                                 .offset = 0,
                                 .length = 0};
        shell_error_t *err = shell_error_create(
            SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
            "configuration key too long (max %zu chars)", sizeof(key) - 1);
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        return -1;
    }

    strncpy(key, trimmed, key_len);
    key[key_len] = '\0';

    /// Trim whitespace from key
    char *key_end = key + strlen(key) - 1;
    while (key_end > key && isspace(*key_end)) {
        *key_end-- = '\0';
    }

    /// Extract value
    const char *value = equals + 1;
    while (isspace(*value)) {
        value++;
    }

    return config_parse_option(key, value);
}

/**
 * @brief Parse a configuration section header
 *
 * Sets the current section for subsequent option parsing.
 *
 * @param section_name Name of the section (e.g., "history", "prompt")
 * @return 0 on success, -1 if unknown section
 */
int config_parse_section(const char *section_name) {
    if (strcmp(section_name, "history") == 0) {
        current_section = CONFIG_SECTION_HISTORY;
    } else if (strcmp(section_name, "completion") == 0) {
        current_section = CONFIG_SECTION_COMPLETION;
    } else if (strcmp(section_name, "behavior") == 0) {
        current_section = CONFIG_SECTION_BEHAVIOR;
    } else if (strcmp(section_name, "aliases") == 0) {
        current_section = CONFIG_SECTION_ALIASES;
    } else if (strcmp(section_name, "keys") == 0) {
        current_section = CONFIG_SECTION_KEYS;
    } else if (strcmp(section_name, "network") == 0) {
        current_section = CONFIG_SECTION_NETWORK;
    } else if (strcmp(section_name, "scripts") == 0) {
        current_section = CONFIG_SECTION_SCRIPTS;
    } else {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_WARNING,
            SOURCE_LOC_UNKNOWN, "unknown configuration section: %s",
            section_name);
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        current_section = CONFIG_SECTION_NONE;
        return -1;
    }

    return 0;
}

config_section_t config_get_current_section(void) { return current_section; }

/**
 * @brief Parse a configuration option
 *
 * Parses a key=value pair and updates the corresponding config field.
 * Handles aliases specially by adding them to the alias table.
 *
 * @param key Configuration key (may use dotted notation)
 * @param value Configuration value string
 * @return 0 on success, -1 on error
 */
int config_parse_option(const char *key, const char *value) {
    /// Handle aliases specially
    if (current_section == CONFIG_SECTION_ALIASES) {
        /// Add alias: key = value
        char *alias_cmd = malloc(strlen(value) + 1);
        if (alias_cmd) {
            strcpy(alias_cmd, value);
            set_alias(key, alias_cmd);
        }
        return 0;
    }

    /// Handle regular options
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            /// Handle shell options specially - they use integration functions
            if (strncmp(key, "shell.", 6) == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                    strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
                    config_set_shell_option(key, true);
                } else if (strcmp(value, "false") == 0 ||
                           strcmp(value, "0") == 0 ||
                           strcmp(value, "off") == 0 ||
                           strcmp(value, "no") == 0) {
                    config_set_shell_option(key, false);
                } else {
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN,
                        "invalid boolean value '%s' for shell option '%s'",
                        value, key);
                    if (err) {
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    }
                    return -1;
                }
                return 0;
            }

            /// Validate value if validator exists
            if (opt->validator && !opt->validator(value)) {
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                    SOURCE_LOC_UNKNOWN, "invalid value '%s' for option '%s'",
                    value, key);
                if (err) {
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                }
                return -1;
            }

            /// Set value based on type
            switch (opt->type) {
            case CONFIG_TYPE_BOOL: {
                bool bool_val =
                    (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                     strcmp(value, "yes") == 0 || strcmp(value, "on") == 0);
                *(bool *)opt->value_ptr = bool_val;
                break;
            }
            case CONFIG_TYPE_INT: {
                int int_val = atoi(value);
                *(int *)opt->value_ptr = int_val;
                break;
            }
            case CONFIG_TYPE_ENUM: {
                /// Look up string value in enum mapping table
                int enum_val = opt->enum_def->default_value;
                if (opt->enum_def && opt->enum_def->mappings) {
                    for (const config_enum_mapping_t *m =
                             opt->enum_def->mappings;
                         m->name; m++) {
                        if (strcmp(value, m->name) == 0) {
                            enum_val = m->value;
                            break;
                        }
                    }
                }
                *(int *)opt->value_ptr = enum_val;
                break;
            }
            case CONFIG_TYPE_STRING: {
                char **str_ptr = (char **)opt->value_ptr;
                if (*str_ptr) {
                    free(*str_ptr);
                }
                *str_ptr = strdup(value);
                break;
            }
            case CONFIG_TYPE_COLOR:
                /// Color handling would go here
                break;
            }

            return 0;
        }
    }

    /// Handle legacy configuration keys gracefully to eliminate warnings
    if (config_handle_legacy_key(key, value)) {
        return 0;
    }

    {
        shell_error_t *err = shell_error_create(
            SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_WARNING,
            SOURCE_LOC_UNKNOWN, "unknown configuration option: %s", key);
        if (err) {
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
    }
    return -1;
}

/**
 * @brief Apply loaded configuration settings to the shell
 *
 * Updates symbol table variables and subsystem configurations
 * based on loaded config values.
 */
void config_apply_settings(void) {
    /// Apply settings safely - only set basic variables for now
    /// More complex integrations will be added after basic functionality works

    /// Apply shell mode settings (Extended Language Support)
    shell_mode_set((shell_mode_t)config.shell_mode);
    shell_mode_set_strict(config.shell_mode_strict);

    /// Basic symbol table settings
    symtable_set_global_int("CONFIG_LOADED", 1);
    symtable_set_global_int("HISTORY_NO_DUPS", config.history_no_dups ? 1 : 0);
    /// COMPLETION_MATCH_MODE replaces the legacy FUZZY_COMPLETION
    /// 0/1 toggle: scripts read a stable string ("prefix" /
    /// "substring" / "fuzzy") and can branch on the active
    /// predicate without inspecting the registry directly.
    const char *match_mode_name = "prefix";
    switch (config.completion_match_mode) {
    case COMPLETION_MATCH_SUBSTRING:
        match_mode_name = "substring";
        break;
    case COMPLETION_MATCH_FUZZY:
        match_mode_name = "fuzzy";
        break;
    case COMPLETION_MATCH_PREFIX:
    default:
        match_mode_name = "prefix";
        break;
    }
    symtable_set_global("COMPLETION_MATCH_MODE", match_mode_name);
    symtable_set_global_int("COMPLETION_THRESHOLD",
                            config.completion_threshold);

    /// Push prompt-composer settings (transient, newline-before) from the
    /// global config into the LLE composer's own copy. config_apply_settings
    /// runs on every `config set`, so this is what lets a config-set take
    /// effect at the next prompt rather than only after a restart (the composer
    /// is otherwise seeded only at integration init).
    lle_shell_integration_sync_prompt_config();

    /// Apply history settings
    /// History deduplication is handled automatically by readline integration

    /// Apply hints system settings
    if (config.hints_enabled) {
        /// Hints are handled differently in readline integration
        /// TODO: Implement hints system for readline if needed
    } else {
        /// Hints disabled - no action needed for readline integration
    }

    /// Apply other settings as needed
    symtable_set_global_int("AUTO_CD", config.auto_cd);
    symtable_set_global_int("SPELL_CORRECTION", config.spell_correction);
    symtable_set_global_int("CONFIRM_EXIT", config.confirm_exit);

    /// Update autocorrect configuration when spell correction settings change
    autocorrect_config_t autocorrect_cfg;
    autocorrect_get_default_config(&autocorrect_cfg);
    autocorrect_cfg.enabled = config.spell_correction;
    autocorrect_cfg.max_suggestions = config.autocorrect_max_suggestions;
    autocorrect_cfg.similarity_threshold = config.autocorrect_threshold;
    autocorrect_cfg.interactive_prompts = config.autocorrect_interactive;
    autocorrect_cfg.learn_from_history = config.autocorrect_learn_history;
    autocorrect_cfg.correct_builtins = config.autocorrect_builtins;
    autocorrect_cfg.correct_external = config.autocorrect_external;
    autocorrect_cfg.case_sensitive = config.autocorrect_case_sensitive;
    autocorrect_load_config(&autocorrect_cfg);

    /// Push the East Asian Ambiguous-width policy down to the LLE
    /// codepoint-width module (#157). lle_codepoint_width_set_ambiguous_policy
    /// coerces any value other than 2 to 1, so the strcmp is safe.
    if (config.display_ambiguous_width &&
        strcmp(config.display_ambiguous_width, "wide") == 0) {
        lle_codepoint_width_set_ambiguous_policy(2);
    } else {
        lle_codepoint_width_set_ambiguous_policy(1);
    }

    /// Debug mode is handled by LLE's internal configuration
}

/**
 * @brief Create a user configuration file with default values
 *
 * Writes the default configuration template to ~/.lushrc.
 *
 * @return 0 on success, -1 on failure
 */
int config_create_user_config(void) {
    FILE *file = fopen(config_ctx.user_config_path, "w");
    if (!file) {
        return -1;
    }

    fprintf(file, "%s", CONFIG_FILE_TEMPLATE);
    fclose(file);

    return 0;
}

/**
 * @brief Validate a boolean configuration value
 *
 * @param value String to validate
 * @return True if value is a valid boolean (true/false/1/0/yes/no/on/off)
 */
bool config_validate_bool(const char *value) {
    return (strcmp(value, "true") == 0 || strcmp(value, "false") == 0 ||
            strcmp(value, "1") == 0 || strcmp(value, "0") == 0 ||
            strcmp(value, "yes") == 0 || strcmp(value, "no") == 0 ||
            strcmp(value, "on") == 0 || strcmp(value, "off") == 0);
}

/**
 * @brief Validate an integer configuration value
 *
 * @param value String to validate
 * @return True if value is a valid integer
 */
bool config_validate_int(const char *value) {
    char *endptr;
    strtol(value, &endptr, 10);
    return (*endptr == '\0');
}

/**
 * @brief Validate a string configuration value
 *
 * @param value String to validate
 * @return True if value is non-NULL and non-empty
 */
bool config_validate_string(const char *value) {
    return (value != NULL && strlen(value) > 0);
}

/**
 * @brief Validate a color configuration value
 *
 * @param value String to validate
 * @return True if value is a valid color string
 */
bool config_validate_color(const char *value) {
    /// Basic color validation - could be enhanced
    return config_validate_string(value);
}

/**
 * @brief Validate a floating-point configuration value
 *
 * @param value String to validate
 * @return True if value is a valid float
 */
bool config_validate_float(const char *value) {
    char *endptr;
    strtod(value, &endptr);
    return (*endptr == '\0');
}

/**
 * @brief Validate a path configuration value
 *
 * @param value String to validate
 * @return True if value is a valid path string
 */
bool config_validate_path(const char *value) {
    return config_validate_string(value);
}

/**
 * @brief Validate display optimization level
 *
 * @param value String to validate
 * @return True if value is 0-4
 */
bool config_validate_optimization_level(const char *value) {
    char *endptr;
    long level = strtol(value, &endptr, 10);
    return (*endptr == '\0' && level >= 0 && level <= 4);
}

/**
 * @brief Validate East Asian Ambiguous-width policy value.
 *
 * Accepts "narrow" (traditional wcwidth default) or "wide" (the
 * Asian-terminal interpretation). Any other value -- including
 * partial matches like "n" or "wid" -- is rejected so config-file
 * typos surface immediately instead of silently falling back.
 *
 * @param value String to validate
 * @return True if value is exactly "narrow" or "wide"
 */
bool config_validate_ambiguous_width(const char *value) {
    return value != NULL &&
           (strcmp(value, "narrow") == 0 || strcmp(value, "wide") == 0);
}

/**
 * @brief Validate LLE arrow key mode value
 *
 * @param value String to validate
 * @return True if value is a valid arrow key mode
 */
bool config_validate_lle_arrow_mode(const char *value) {
    return (strcmp(value, "context-aware") == 0 ||
            strcmp(value, "classic") == 0 ||
            strcmp(value, "always-history") == 0 ||
            strcmp(value, "multiline-first") == 0);
}

/**
 * @brief Validate completion match mode value
 *
 * @param value String to validate
 * @return True if value is a valid completion match mode
 */
bool config_validate_completion_match_mode(const char *value) {
    return (strcmp(value, "prefix") == 0 || strcmp(value, "substring") == 0 ||
            strcmp(value, "fuzzy") == 0);
}

bool config_validate_autosuggestion_dismiss_policy(const char *value) {
    return (strcmp(value, "on_deviation") == 0 ||
            strcmp(value, "on_word_boundary") == 0);
}

bool config_validate_autosuggestion_rank(const char *value) {
    return (strcmp(value, "frecency") == 0 || strcmp(value, "recency") == 0);
}

bool config_validate_autosuggestion_partial_accept(const char *value) {
    return (strcmp(value, "path_segment") == 0 || strcmp(value, "word") == 0);
}

bool config_validate_autosuggestion_sources(const char *value) {
    return (strcmp(value, "history") == 0 ||
            strcmp(value, "history_then_completion") == 0);
}

bool config_validate_history_search_mode(const char *value) {
    return (strcmp(value, "prefix") == 0 || strcmp(value, "plain") == 0);
}

bool config_validate_history_finder_match(const char *value) {
    return (strcmp(value, "fuzzy") == 0 || strcmp(value, "substring") == 0 ||
            strcmp(value, "prefix") == 0);
}

bool config_validate_history_finder_rank(const char *value) {
    return (strcmp(value, "frecency") == 0 || strcmp(value, "recency") == 0);
}

bool config_validate_history_finder_display(const char *value) {
    return (strcmp(value, "incremental") == 0 || strcmp(value, "picker") == 0);
}

/**
 * @brief Validate LLE deduplication scope value
 *
 * @param value String to validate
 * @return True if value is a valid dedup scope
 */
bool config_validate_lle_dedup_scope(const char *value) {
    return (strcmp(value, "none") == 0 || strcmp(value, "session") == 0 ||
            strcmp(value, "recent") == 0 || strcmp(value, "global") == 0);
}

/**
 * @brief Validate LLE deduplication strategy value
 *
 * @param value String to validate
 * @return True if value is a valid dedup strategy
 */
bool config_validate_lle_dedup_strategy(const char *value) {
    return (strcmp(value, "ignore") == 0 || strcmp(value, "keep-recent") == 0 ||
            strcmp(value, "keep-frequent") == 0 ||
            strcmp(value, "merge") == 0 || strcmp(value, "keep-all") == 0);
}

/**
 * @brief Validate shell mode value
 *
 * @param value String to validate
 * @return True if value is a valid shell mode
 */
bool config_validate_shell_mode(const char *value) {
    /// Delegate to the canonical parser: it knows every mode name and
    /// the "sh"-as-POSIX alias, so this stays correct as new modes are
    /// added without touching the registry validator.
    shell_mode_t scratch;
    return shell_mode_parse(value, &scratch);
}

/* config_error(), config_warning(), and config_get_last_error() removed
 * 2026-05-20 as part of the structured-error migration (#71). The
 * domain-specific helpers were a layering violation per
 * feedback-direct-api-error-system; every former caller now invokes
 * shell_error_create() directly with the appropriate SHELL_ERR_*
 * code, severity, and source_location_t (with line+filename when the
 * caller is inside the config-file parser, SOURCE_LOC_UNKNOWN otherwise).
 * The static `last_error` buffer is gone with the wrapper that fed it. */

/* ============================================================================
 * Type-Safe Configuration Setters and Getters
 * ============================================================================
 */

/**
 * @brief Set a boolean configuration value
 *
 * @param key Configuration key (e.g., "history.enabled")
 * @param value Boolean value to set
 * @return 0 on success, -1 if key not found or type mismatch
 */
int config_set_bool(const char *key, bool value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type != CONFIG_TYPE_BOOL) {
                return -1; /// Type mismatch
            }
            *(bool *)config_options[i].value_ptr = value;
            return 0;
        }
    }
    /// Not in the legacy table: route to the registry, where migrated keys
    /// (e.g. history.*) live. The binding write-throughs the runtime cell.
    if (config_registry_is_initialized() &&
        config_registry_set_boolean(key, value) == CREG_SUCCESS) {
        return 0;
    }
    return -1; /// Key not found
}

/**
 * @brief Set an integer configuration value
 *
 * @param key Configuration key (e.g., "history.size")
 * @param value Integer value to set
 * @return 0 on success, -1 if key not found or type mismatch
 */
int config_set_int(const char *key, int value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type != CONFIG_TYPE_INT &&
                config_options[i].type != CONFIG_TYPE_ENUM) {
                return -1; /// Type mismatch
            }
            *(int *)config_options[i].value_ptr = value;
            return 0;
        }
    }
    if (config_registry_is_initialized() &&
        config_registry_set_integer(key, value) == CREG_SUCCESS) {
        return 0;
    }
    return -1; /// Key not found
}

/**
 * @brief Set a string configuration value
 *
 * @param key Configuration key (e.g., "completion.match_mode")
 * @param value String value to set (will be duplicated)
 * @return 0 on success, -1 if key not found or type mismatch
 */
int config_set_string(const char *key, const char *value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type != CONFIG_TYPE_STRING &&
                config_options[i].type != CONFIG_TYPE_COLOR) {
                return -1; /// Type mismatch
            }
            char **ptr = (char **)config_options[i].value_ptr;
            if (*ptr) {
                free(*ptr);
            }
            *ptr = value ? strdup(value) : NULL;
            return 0;
        }
    }
    /// Not in the legacy table: route to the registry, where migrated keys
    /// (e.g. lle.history_file) live. An empty value clears to "unset".
    if (config_registry_is_initialized() &&
        config_registry_set_string(key, value ? value : "") == CREG_SUCCESS) {
        return 0;
    }
    return -1; /// Key not found
}

/**
 * @brief Get a boolean configuration value
 *
 * @param key Configuration key
 * @param default_value Value to return if key not found
 * @return Configuration value or default
 */
bool config_get_bool(const char *key, bool default_value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type == CONFIG_TYPE_BOOL) {
                return *(bool *)config_options[i].value_ptr;
            }
            break;
        }
    }
    bool out;
    if (config_registry_is_initialized() &&
        config_registry_get_boolean(key, &out) == CREG_SUCCESS) {
        return out;
    }
    return default_value;
}

/**
 * @brief Get an integer configuration value
 *
 * @param key Configuration key
 * @param default_value Value to return if key not found
 * @return Configuration value or default
 */
int config_get_int(const char *key, int default_value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type == CONFIG_TYPE_INT ||
                config_options[i].type == CONFIG_TYPE_ENUM) {
                return *(int *)config_options[i].value_ptr;
            }
            break;
        }
    }
    int64_t out;
    if (config_registry_is_initialized() &&
        config_registry_get_integer(key, &out) == CREG_SUCCESS) {
        return (int)out;
    }
    return default_value;
}

/**
 * @brief Get a string configuration value
 *
 * @param key Configuration key
 * @param default_value Value to return if key not found
 * @return Configuration value or default (do not free)
 */
const char *config_get_string(const char *key, const char *default_value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type == CONFIG_TYPE_STRING ||
                config_options[i].type == CONFIG_TYPE_COLOR) {
                const char *value = *(char **)config_options[i].value_ptr;
                return value ? value : default_value;
            }
            break;
        }
    }
    /// Registry fallback for migrated string keys. The returned pointer is
    /// borrowed and valid until the next config_get_string call; an empty
    /// (unset) value falls through to the caller's default.
    if (config_registry_is_initialized()) {
        static char buf[CREG_VALUE_STRING_MAX];
        if (config_registry_get_string(key, buf, sizeof(buf)) == CREG_SUCCESS) {
            return buf[0] != '\0' ? buf : default_value;
        }
    }
    return default_value;
}

/// Render a registry value as the textual form the config builtin uses.
static void config_value_text(const creg_value_t *v, char *out, size_t n) {
    switch (v->type) {
    case CREG_VALUE_BOOLEAN:
        snprintf(out, n, "%s", v->data.boolean ? "true" : "false");
        break;
    case CREG_VALUE_INTEGER:
        snprintf(out, n, "%lld", (long long)v->data.integer);
        break;
    case CREG_VALUE_STRING:
        snprintf(out, n, "%s", v->data.string);
        break;
    case CREG_VALUE_FLOAT:
        snprintf(out, n, "%g", v->data.floating);
        break;
    default:
        if (n > 0) {
            out[0] = '\0';
        }
        break;
    }
}

/// `config explain <key>`: print the effective value, the layer it came from,
/// and the full shadowed stack with each layer's origin -- "what is this, and
/// where did it come from". Only registry-backed keys carry layer provenance.
static void config_explain_value(const char *key) {
    creg_inspect_t ins;
    if (config_registry_inspect(key, &ins) != CREG_SUCCESS) {
        printf("No layer provenance for '%s' (not a registry-backed key).\n",
               key);
        return;
    }
    char eff[CREG_VALUE_STRING_MAX];
    config_value_text(&ins.effective, eff, sizeof(eff));
    printf("%s = %s  (from %s)\n", key, eff,
           config_registry_layer_name(ins.winning));
    printf("  layers, highest precedence first:\n");
    for (int layer = CREG_LAYER_COUNT - 1; layer >= 0; layer--) {
        if (!ins.layers[layer].present) {
            continue;
        }
        char val[CREG_VALUE_STRING_MAX];
        config_value_text(&ins.layers[layer].value, val, sizeof(val));
        printf("    %s %-8s = %-14s [%s]\n",
               layer == (int)ins.winning ? "->" : "  ",
               config_registry_layer_name((creg_layer_t)layer), val,
               ins.layers[layer].origin);
    }
}

/**
 * Implements the 'config' builtin with subcommands:
 * show, set, get, explain, reload, save, reset-defaults.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void builtin_config(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: config <command> [options]\n\n");
        printf("Commands:\n");
        printf("  show [section]     - Show configuration values\n");
        printf("  set key value      - Set configuration value\n");
        printf("  get key            - Get configuration value\n");
        printf("  reload             - Reload configuration files\n");
        printf("  save               - Save current configuration\n");
        printf("  migrate            - Migrate legacy ~/.lushrc to XDG "
               "location\n");
        printf("  path               - Show configuration file paths\n");
        printf("  reset-defaults     - Write default configuration\n");
        return;
    }

    if (strcmp(argv[1], "reset-defaults") == 0) {
        /// Write default configuration template to user config file
        char *path = config_get_user_config_path();
        if (!path) {
            printf("Error: Could not determine user config path\n");
            return;
        }

        /// Check if file exists and warn user
        struct stat st;
        if (stat(path, &st) == 0) {
            printf("Warning: %s already exists.\n", path);
            printf("This will overwrite your current configuration with "
                   "defaults.\n");
            printf("Continue? [y/N] ");
            fflush(stdout);

            int c = getchar();
            /// Clear any remaining input
            while (getchar() != '\n' && !feof(stdin))
                ;

            if (c != 'y' && c != 'Y') {
                printf("Aborted.\n");
                free(path);
                return;
            }
        }

        FILE *file = fopen(path, "w");
        if (!file) {
            printf("Error: Could not write to %s: %s\n", path, strerror(errno));
            free(path);
            return;
        }

        fprintf(file, "%s", CONFIG_FILE_TEMPLATE);
        fclose(file);

        printf("Default configuration written to %s\n", path);
        printf("Reload with: config reload\n");
        free(path);
        return;
    } else if (strcmp(argv[1], "show") == 0) {
        if (argc > 2) {
            /// Show specific section
            config_section_t section = CONFIG_SECTION_NONE;
            if (strcmp(argv[2], "history") == 0) {
                section = CONFIG_SECTION_HISTORY;
            } else if (strcmp(argv[2], "lle") == 0) {
                section = CONFIG_SECTION_LLE;
            } else if (strcmp(argv[2], "completion") == 0) {
                section = CONFIG_SECTION_COMPLETION;
            } else if (strcmp(argv[2], "behavior") == 0) {
                section = CONFIG_SECTION_BEHAVIOR;
            } else if (strcmp(argv[2], "display") == 0) {
                section = CONFIG_SECTION_DISPLAY;
            } else if (strcmp(argv[2], "network") == 0) {
                section = CONFIG_SECTION_NETWORK;
            } else if (strcmp(argv[2], "scripts") == 0) {
                section = CONFIG_SECTION_SCRIPTS;
            } else if (strcmp(argv[2], "shell") == 0) {
                section = CONFIG_SECTION_SHELL;
            }

            if (section != CONFIG_SECTION_NONE) {
                config_show_section(section);
            } else {
                printf("Unknown section: %s\n", argv[2]);
            }
        } else {
            config_show_all();
        }
    } else if (strcmp(argv[1], "get") == 0) {
        if (argc < 3) {
            printf("Usage: config get <key>\n");
            return;
        }
        config_get_value(argv[2]);
    } else if (strcmp(argv[1], "explain") == 0) {
        if (argc < 3) {
            printf("Usage: config explain <key>\n");
            return;
        }
        config_explain_value(argv[2]);
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf("Usage: config set <key> <value>\n");
            return;
        }
        config_set_value(argv[2], argv[3]);
    } else if (strcmp(argv[1], "reload") == 0) {
        config_init();
        printf("Configuration reloaded.\n");
    } else if (strcmp(argv[1], "save") == 0) {
        if (config_save_user() == 0) {
            printf("Configuration saved to %s\n",
                   config_ctx.user_config_path ? config_ctx.user_config_path
                                               : "~/.lushrc");
        } else {
            printf("Error: Failed to save configuration\n");
        }
    } else if (strcmp(argv[1], "migrate") == 0) {
        if (!config_ctx.needs_migration) {
            char xdg_path[CONFIG_PATH_MAX];
            if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
                struct stat st;
                if (stat(xdg_path, &st) == 0) {
                    printf("Configuration already at XDG location: %s\n",
                           xdg_path);
                } else {
                    printf("No legacy configuration to migrate.\n");
                    printf(
                        "Use 'config save' to create a new configuration.\n");
                }
            }
            return;
        }
        if (config_migrate_to_xdg() == 0) {
            printf("Migration complete.\n");
        } else {
            printf("Error: Migration failed\n");
        }
    } else if (strcmp(argv[1], "path") == 0) {
        /// Show current config file paths
        printf("Configuration paths:\n");
        if (config_ctx.user_config_path) {
            printf("  User config:   %s%s\n", config_ctx.user_config_path,
                   config_ctx.user_config_exists ? "" : " (not found)");
        }
        if (config_ctx.system_config_path) {
            printf("  System config: %s%s\n", config_ctx.system_config_path,
                   config_ctx.system_config_exists ? "" : " (not found)");
        }
        if (config_ctx.xdg_config_dir) {
            printf("  XDG config dir: %s\n", config_ctx.xdg_config_dir);
        }
        if (config_ctx.legacy_config_path) {
            printf("  Legacy config: %s\n", config_ctx.legacy_config_path);
        }
        printf("  Format: %s\n",
               config_ctx.format == CONFIG_FORMAT_TOML     ? "TOML"
               : config_ctx.format == CONFIG_FORMAT_LEGACY ? "Legacy INI"
                                                           : "Unknown");
        if (config_ctx.needs_migration) {
            printf("  Status: Migration pending (run 'config migrate' or "
                   "'config save')\n");
        }
    } else {
        printf("Unknown config command: %s\n", argv[1]);
    }
}

/**
 * @brief Parse a textual boolean for the config builtin
 *
 * Accepts true/1/on/yes and false/0/off/no (case-sensitive, the spelling the
 * builtin has always documented). Returns false if the text is not a
 * recognized boolean so callers can report an error.
 *
 * @param value Text to parse
 * @param out   Receives the parsed boolean on success
 * @return true if recognized, false otherwise
 */
static bool config_parse_bool_text(const char *value, bool *out) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
        *out = false;
        return true;
    }
    return false;
}

/**
 * @brief Print a CREG registry value in the config builtin's textual form
 */
static void config_print_registry_value(const creg_value_t *v) {
    switch (v->type) {
    case CREG_VALUE_BOOLEAN:
        printf("%s\n", v->data.boolean ? "true" : "false");
        break;
    case CREG_VALUE_INTEGER:
        printf("%lld\n", (long long)v->data.integer);
        break;
    case CREG_VALUE_STRING:
        printf("%s\n", v->data.string);
        break;
    case CREG_VALUE_FLOAT:
        printf("%g\n", v->data.floating);
        break;
    case CREG_VALUE_NONE:
        printf("\n");
        break;
    }
}

/**
 * @brief Get and print a single configuration value
 *
 * Looks up the configuration key and prints its current value.
 *
 * @param key Configuration key to look up
 */
void config_get_value(const char *key) {
    /// Handle shell.feature.* keys dynamically (not in config_options array)
    if (strncmp(key, "shell.feature.", 14) == 0) {
        const char *feature_name = key + 14; /// Skip "shell.feature."
        shell_feature_t feature;
        bool invert = false;

        if (!shell_feature_parse(feature_name, &feature, &invert)) {
            printf("Unknown feature: %s\n", feature_name);
            printf("Use 'debug features' to see available features\n");
            return;
        }

        bool effective = shell_mode_allows(feature) ^ invert;
        printf("%s\n", effective ? "true" : "false");
        return;
    }

    /// First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            /// Handle shell.mode specially - it's an enum
            if (strcmp(key, "shell.mode") == 0) {
                printf("%s\n", shell_mode_name(shell_mode_get()));
                return;
            }

            /// Handle shell.mode_strict specially
            if (strcmp(key, "shell.mode_strict") == 0) {
                printf("%s\n", config.shell_mode_strict ? "true" : "false");
                return;
            }

            /// Handle other shell options specially - they use integration
            /// functions
            if (strncmp(key, "shell.", 6) == 0) {
                printf("%s\n", config_get_shell_option(key) ? "true" : "false");
                return;
            }

            switch (opt->type) {
            case CONFIG_TYPE_BOOL:
                printf("%s\n", *(bool *)opt->value_ptr ? "true" : "false");
                break;
            case CONFIG_TYPE_INT:
                printf("%d\n", *(int *)opt->value_ptr);
                break;
            case CONFIG_TYPE_STRING:
                printf("%s\n", *(char **)opt->value_ptr
                                   ? *(char **)opt->value_ptr
                                   : "");
                break;
            case CONFIG_TYPE_COLOR:
                printf("%s\n", *(char **)opt->value_ptr
                                   ? *(char **)opt->value_ptr
                                   : "");
                break;
            case CONFIG_TYPE_ENUM:
                if (opt->enum_def && opt->enum_def->mappings) {
                    int current_value = *(int *)opt->value_ptr;
                    const config_enum_mapping_t *mapping =
                        opt->enum_def->mappings;
                    while (mapping->name) {
                        if (mapping->value == current_value) {
                            printf("%s\n", mapping->name);
                            break;
                        }
                        mapping++;
                    }
                }
                break;
            }
            return;
        }
    }

    /// Not in the legacy table: consult the CREG registry, the central source
    /// of truth for TOML round-trip, per-mode defaults, and subsystem code.
    /// This is what makes registry-only keys (e.g.
    /// completion.chain_directories, completion.menu_shadow_ghost) readable
    /// from the config builtin instead of reporting "Unknown configuration
    /// key".
    creg_value_t rv;
    if (config_registry_get(key, &rv) == CREG_SUCCESS) {
        config_print_registry_value(&rv);
        return;
    }

    /// Check for legacy option name
    const char *new_name = find_new_name_for_legacy(key);
    if (new_name) {
        printf("Warning: '%s' is deprecated, use '%s' instead\n", key,
               new_name);
        config_get_value(new_name); /// Recursive call with new name
        return;
    }

    printf("Unknown configuration key: %s\n", key);
}

/**
 * @brief Set a single configuration value
 *
 * Updates the configuration option and applies the change immediately.
 *
 * @param key Configuration key to set
 * @param value New value to assign
 */
void config_set_value(const char *key, const char *value) {
    /// Handle shell.feature.* keys dynamically (not in config_options array)
    if (strncmp(key, "shell.feature.", 14) == 0) {
        const char *feature_name = key + 14; /// Skip "shell.feature."
        shell_feature_t feature;
        bool invert = false;

        if (!shell_feature_parse(feature_name, &feature, &invert)) {
            printf("Unknown feature: %s\n", feature_name);
            printf("Use 'debug features' to see available features\n");
            return;
        }

        bool enable;
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
            strcmp(value, "on") == 0) {
            enable = true;
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                   strcmp(value, "off") == 0) {
            enable = false;
        } else {
            printf("Invalid boolean value: %s (use true/false/on/off)\n",
                   value);
            return;
        }

        /// `enable` is the user's intent in alias terms; flip onto the
        /// underlying feature when the alias is inverted.
        bool target = enable ^ invert;
        if (target) {
            shell_feature_enable(feature);
        } else {
            shell_feature_disable(feature);
        }
        printf("Set %s = %s\n", key, value);
        return;
    }

    /// First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            /// Handle shell.mode specially - it's an enum that also updates
            /// shell_mode system
            if (strcmp(key, "shell.mode") == 0) {
                /// Parse via the canonical helper (handles every mode
                /// name and the "sh"-as-POSIX alias in one place).
                shell_mode_t new_mode;
                if (!shell_mode_parse(value, &new_mode)) {
                    printf("Invalid shell mode: %s (use posix/bash/zsh/lush)\n",
                           value);
                    return;
                }
                if (!shell_mode_set(new_mode)) {
                    printf("Cannot change shell mode (strict mode enabled)\n");
                    return;
                }
                config.shell_mode = (int)new_mode;
                printf("Set %s = %s\n", key, value);
                return;
            }

            /// Handle shell.mode_strict specially
            if (strcmp(key, "shell.mode_strict") == 0) {
                bool strict;
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                    strcmp(value, "on") == 0) {
                    strict = true;
                } else if (strcmp(value, "false") == 0 ||
                           strcmp(value, "0") == 0 ||
                           strcmp(value, "off") == 0) {
                    strict = false;
                } else {
                    printf(
                        "Invalid boolean value: %s (use true/false/on/off)\n",
                        value);
                    return;
                }
                shell_mode_set_strict(strict);
                config.shell_mode_strict = strict;
                printf("Set %s = %s\n", key, value);
                return;
            }

            /// Handle other shell options specially - they use integration
            /// functions
            if (strncmp(key, "shell.", 6) == 0) {
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                    strcmp(value, "on") == 0) {
                    config_set_shell_option(key, true);
                } else if (strcmp(value, "false") == 0 ||
                           strcmp(value, "0") == 0 ||
                           strcmp(value, "off") == 0) {
                    config_set_shell_option(key, false);
                } else {
                    printf(
                        "Invalid boolean value: %s (use true/false/on/off)\n",
                        value);
                    return;
                }
                printf("Set %s = %s\n", key, value);
                return;
            }

            switch (opt->type) {
            case CONFIG_TYPE_BOOL:
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    *(bool *)opt->value_ptr = true;
                } else if (strcmp(value, "false") == 0 ||
                           strcmp(value, "0") == 0) {
                    *(bool *)opt->value_ptr = false;
                } else {
                    printf("Invalid boolean value: %s (use true/false)\n",
                           value);
                    return;
                }
                break;
            case CONFIG_TYPE_INT:
                *(int *)opt->value_ptr = atoi(value);
                break;
            case CONFIG_TYPE_STRING:
                if (*(char **)opt->value_ptr) {
                    free(*(char **)opt->value_ptr);
                }
                *(char **)opt->value_ptr = strdup(value);
                break;
            case CONFIG_TYPE_COLOR:
                if (config_validate_color(value)) {
                    if (*(char **)opt->value_ptr) {
                        free(*(char **)opt->value_ptr);
                    }
                    *(char **)opt->value_ptr = strdup(value);
                } else {
                    printf("Invalid color value: %s\n", value);
                    return;
                }
                break;
            case CONFIG_TYPE_ENUM:
                if (opt->enum_def && opt->enum_def->mappings) {
                    const config_enum_mapping_t *mapping =
                        opt->enum_def->mappings;
                    bool found = false;
                    while (mapping->name) {
                        if (strcmp(mapping->name, value) == 0) {
                            *(int *)opt->value_ptr = mapping->value;
                            found = true;
                            break;
                        }
                        mapping++;
                    }
                    if (!found) {
                        printf("Invalid enum value: %s\n", value);
                        return;
                    }
                }
                break;
            }
            printf("Set %s = %s\n", key, value);

            /// Apply the setting immediately
            config_apply_settings();
            return;
        }
    }

    /// Not in the legacy table: route through the CREG registry so
    /// registry-only keys are settable. Probe the key to learn its declared
    /// type, parse the text against it, set through the registry (which fires
    /// change notification), then mirror into the runtime struct and apply --
    /// the same path a TOML load takes.
    creg_value_t probe;
    if (config_registry_get(key, &probe) == CREG_SUCCESS) {
        creg_value_t nv;
        nv.type = probe.type;
        switch (probe.type) {
        case CREG_VALUE_BOOLEAN:
            if (!config_parse_bool_text(value, &nv.data.boolean)) {
                printf("Invalid boolean value: %s (use "
                       "true/false/on/off/yes/no)\n",
                       value);
                return;
            }
            break;
        case CREG_VALUE_INTEGER: {
            char *end = NULL;
            errno = 0;
            long long parsed = strtoll(value, &end, 10);
            if (end == value || *end != '\0' || errno != 0) {
                printf("Invalid integer value: %s\n", value);
                return;
            }
            nv.data.integer = (int64_t)parsed;
            break;
        }
        case CREG_VALUE_FLOAT: {
            char *end = NULL;
            errno = 0;
            double parsed = strtod(value, &end);
            if (end == value || *end != '\0' || errno != 0) {
                printf("Invalid number value: %s\n", value);
                return;
            }
            nv.data.floating = parsed;
            break;
        }
        case CREG_VALUE_STRING:
            snprintf(nv.data.string, sizeof(nv.data.string), "%s", value);
            break;
        case CREG_VALUE_NONE:
            printf("Cannot set %s: key has no value type\n", key);
            return;
        }

        if (config_registry_set(key, &nv) != CREG_SUCCESS) {
            printf("Failed to set %s\n", key);
            return;
        }
        config_registry_sync_to_runtime();
        config_apply_settings();
        printf("Set %s = %s\n", key, value);
        return;
    }

    /// Check for legacy option name
    const char *new_name = find_new_name_for_legacy(key);
    if (new_name) {
        printf("Warning: '%s' is deprecated, use '%s' instead\n", key,
               new_name);
        config_set_value(new_name, value); /// Recursive call with new name
        return;
    }

    /// Check for common typos and provide helpful suggestions
    if (strcmp(key, "hints_enable") == 0) {
        printf("Unknown configuration key: %s\n", key);
        printf("Did you mean 'completion.hints'?\n");
        printf("Try: config set completion.hints %s\n", value);
        return;
    }

    printf("Unknown configuration key: %s\n", key);
}

/**
 * @brief Show all configuration values
 *
 * Prints all configuration sections and their values to stdout.
 */
void config_show_all(void) {
    printf("LUSH Configuration:\n\n");

    printf("[history]\n");
    config_show_section(CONFIG_SECTION_HISTORY);

    printf("\n[lle]\n");
    config_show_section(CONFIG_SECTION_LLE);

    printf("\n[completion]\n");
    config_show_section(CONFIG_SECTION_COMPLETION);

    printf("\n[behavior]\n");
    config_show_section(CONFIG_SECTION_BEHAVIOR);

    printf("\n[display]\n");
    config_show_section(CONFIG_SECTION_DISPLAY);

    printf("\n[autosuggestion]\n");
    config_show_section(CONFIG_SECTION_AUTOSUGGESTION);

    printf("\n[network]\n");
    config_show_section(CONFIG_SECTION_NETWORK);

    printf("\n[scripts]\n");
    config_show_section(CONFIG_SECTION_SCRIPTS);

    printf("\n[shell]\n");
    config_show_section(CONFIG_SECTION_SHELL);
}

/**
 * @brief Show configuration values for a specific section
 *
 * Prints all options in the given section to stdout.
 *
 * @param section Section to display
 */
/// Map a legacy section enum to its CREG registry section name (NULL if the
/// section has no registry counterpart).
static const char *registry_section_for(config_section_t section) {
    switch (section) {
    case CONFIG_SECTION_HISTORY:
        return "history";
    case CONFIG_SECTION_SHELL:
        return "shell";
    case CONFIG_SECTION_DISPLAY:
        return "display";
    case CONFIG_SECTION_COMPLETION:
        return "completion";
    case CONFIG_SECTION_BEHAVIOR:
        return "behavior";
    case CONFIG_SECTION_AUTOSUGGESTION:
        return "autosuggestion";
    case CONFIG_SECTION_LLE:
        return "lle";
    default:
        return NULL;
    }
}

/// Whether a full dotted key is present in the legacy config_options[] table.
static bool legacy_has_option(const char *full_key) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, full_key) == 0) {
            return true;
        }
    }
    return false;
}

/// Print the CREG registry keys for a section that the legacy loop did not
/// already show (the migrated keys), with their effective values. This keeps
/// `config show` complete after a section migrates off the legacy table --
/// discoverability must not regress as the registry takes over.
static void config_show_registry_section(config_section_t section) {
    const char *reg_name = registry_section_for(section);
    if (!reg_name) {
        return;
    }
    const creg_section_t *reg = config_registry_get_section(reg_name);
    if (!reg) {
        return;
    }
    for (size_t i = 0; i < reg->option_count; i++) {
        char full_key[CREG_KEY_MAX];
        snprintf(full_key, sizeof(full_key), "%s.%s", reg_name,
                 reg->options[i].name);
        if (legacy_has_option(full_key)) {
            continue; /// already shown by the legacy loop
        }
        creg_value_t v;
        if (config_registry_get(full_key, &v) != CREG_SUCCESS) {
            continue;
        }
        char text[CREG_VALUE_STRING_MAX];
        config_value_text(&v, text, sizeof(text));
        printf("  %s = %s", reg->options[i].name, text);
        if (reg->options[i].help) {
            printf("  # %s", reg->options[i].help);
        }
        printf("\n");
    }
}

void config_show_section(config_section_t section) {
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (opt->section == section) {
            printf("  %s = ", opt->name);

            /// Handle shell options specially - they use integration functions
            if (strncmp(opt->name, "shell.", 6) == 0) {
                printf("%s",
                       config_get_shell_option(opt->name) ? "true" : "false");
            } else {
                switch (opt->type) {
                case CONFIG_TYPE_BOOL:
                    printf("%s", *(bool *)opt->value_ptr ? "true" : "false");
                    break;
                case CONFIG_TYPE_INT:
                    printf("%d", *(int *)opt->value_ptr);
                    break;
                case CONFIG_TYPE_STRING: {
                    char *str_val = *(char **)opt->value_ptr;
                    printf("%s", str_val ? str_val : "(null)");
                    break;
                }
                case CONFIG_TYPE_COLOR:
                    printf("(color)");
                    break;
                case CONFIG_TYPE_ENUM:
                    if (opt->enum_def && opt->enum_def->mappings) {
                        int current_value = *(int *)opt->value_ptr;
                        const config_enum_mapping_t *mapping =
                            opt->enum_def->mappings;
                        const char *name = "(unknown)";
                        while (mapping->name) {
                            if (mapping->value == current_value) {
                                name = mapping->name;
                                break;
                            }
                            mapping++;
                        }
                        printf("%s", name);
                    }
                    break;
                }
            }

            printf("  # %s\n", opt->description);
        }
    }

    /// Then the registry keys this section's migration moved off the legacy
    /// table, so migrated keys stay visible in `config show`.
    config_show_registry_section(section);
}

/**
 * @brief Clean up configuration resources
 *
 * Frees all allocated memory used by the configuration system.
 * Should be called during shell shutdown.
 */
void config_cleanup(void) {
    /// Clean up config registry
    config_registry_cleanup();

    /// lle.history_file and display.lle.theme are owned by their string-pointer
    /// bindings (strdup on change); release the final values.
    if (config.lle_history_file) {
        free(config.lle_history_file);
        config.lle_history_file = NULL;
    }
    if (config.display_lle_theme) {
        free(config.display_lle_theme);
        config.display_lle_theme = NULL;
    }
    if (config.display_ambiguous_width) {
        free(config.display_ambiguous_width);
        config.display_ambiguous_width = NULL;
    }

    if (config_ctx.user_config_path) {
        free(config_ctx.user_config_path);
    }
    if (config_ctx.system_config_path) {
        free(config_ctx.system_config_path);
    }
    if (config_ctx.xdg_config_dir) {
        free(config_ctx.xdg_config_dir);
    }
    if (config_ctx.legacy_config_path) {
        free(config_ctx.legacy_config_path);
    }
}
