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

#include "autocorrect.h"
#include "config_registry.h"
#include "executor.h"
#include "input.h"
#include "lle/char_width.h"
#include "lle/lle_pager.h"
#include "lle/lle_shell_integration.h"
#include "lush.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
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

/// The shell.mode string<->enum map retired with the legacy shell.mode row;
/// shell.mode now parses via the canonical shell_mode_parse helper.

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
    /// not this legacy table. completion.show_all was a phantom key (no runtime
    /// consumer) and completion.hints' only reader was an unimplemented no-op;
    /// both are removed.

    /// Behavior settings
    /// behavior.auto_cd, behavior.spell_correction are
    /// migrated to the CREG registry (bound + layered).
    /// behavior.autocorrect_* are migrated to the CREG registry (bound +
    /// layered).
    /// behavior limits (tab_width, brace_expansion_max, regex_pattern_max,
    /// path_negative_cache_ttl_ms, loop_failure_streak/seconds) are migrated to
    /// the CREG registry (bound + layered).

    /// network.* (ssh_completion + SSH host caching) was a phantom section:
    /// the keys had no runtime consumer. SSH host completion and its caching
    /// run unconditionally in the ssh_hosts completion source; the legacy
    /// keys are removed. A future user-configurable SSH-completion toggle is a
    /// separate feature, not a revived phantom key.

    /// Display system settings
    /// v1.3.0: Layered display is now the exclusive system - no configuration
    /// needed
    /// display.system_mode and display.layered_display options removed.
    /// The display.* keys are all migrated to the CREG registry (bound +
    /// layered); they resolve through the registry, not this legacy table.

    /// Autosuggestion settings
    /// autosuggestion.* is migrated to the CREG registry (bound + layered); its
    /// keys resolve through the registry, not this legacy table.

    /// v1.3.0: Legacy enhanced display mode option removed
    /// behavior.enhanced_display_mode option removed

    /// scripts.execution gated execution of the startup/login/logout scripts,
    /// but only as a persistent config-file key -- the wrong surface for that
    /// control (the real use cases want a launch-time flag). The key and its
    /// config_should_execute_scripts gate are removed; startup scripts run
    /// unconditionally as before. A command-line --norc / --noprofile flag is
    /// the correct surface and is a separate future feature.

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
static void display_bind_runtime(void);
static void completion_bind_runtime(void);
static void behavior_bind_runtime(void);
static void autosuggestion_bind_runtime(void);
static void lle_bind_runtime(void);

/* ----------------------------------------------------------------------------
 * History Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t history_options[] = {
    {.name = "enabled",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable command history",
     .persisted = true},
    {.name = "size",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 10000},
     .help = "Maximum history entries",
     .persisted = true,
     .description =
         "How many past commands to keep. Once this many are stored, the "
         "oldest are dropped to make room. Larger values keep more history "
         "to search and recall, at the cost of a bigger history file."},
    {.name = "timestamps",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Record timestamps",
     .persisted = true},
    {.name = "search_mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "prefix"},
     .help = "Up/down navigation filter: prefix (cycle history matching the "
             "typed prefix, cursor kept at the prefix) or plain (browse all, "
             "cursor at end)", .persisted = true},
    {.name = "finder.match",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "fuzzy"},
     .help = "Ctrl-R matching: fuzzy (subsequence), substring, or prefix",
     .persisted = true},
    {.name = "finder.rank",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "frecency"},
     .help = "Ctrl-R ranking: frecency (usage x recency) or recency",
     .persisted = true},
    {.name = "finder.display",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "incremental"},
     .help = "Ctrl-R presentation: incremental (in-line). picker is reserved "
             "and currently falls back to incremental", .persisted = true},
    {.name = "frecency.directory_context",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Boost frecency ranking for commands recorded in the current "
             "directory", .persisted = true},
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
/// The 24 shell.* keys: the compatibility mode plus the 23 POSIX/shell options
/// that mirror shell_opts fields. Boolean defaults match init in
/// posix_opts.c (hashall/emacs/histexpand/history/interactive-comments default
/// true, the rest false) so config show and the registry agree with the
/// runtime struct. Writes still flow through the config builtin's shell.*
/// special-case and set -o's sync_shell_option_to_registry; registering every
/// key here is what makes that sync land (the 19 keys not previously in the
/// schema silently no-opped on CREG_ERROR_NOT_FOUND). Binding these cells and
/// routing every surface through the registry is a later step.
static const creg_option_t shell_options[] = {
    {                .name = "mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "lush"},
     .help = "Shell compatibility mode",
     .persisted = true },
    {         .name = "mode_strict",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Disallow runtime mode changes",
     .persisted = true },
    {             .name = "errexit",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Exit on command failure (set -e)",
     .persisted = true },
    {              .name = "xtrace",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Trace command execution (set -x)",
     .persisted = true },
    {              .name = "noexec",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Syntax check only (set -n)",
     .persisted = true },
    {             .name = "nounset",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Error on unset variables (set -u)",
     .persisted = true },
    {             .name = "verbose",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Print input lines (set -v)",
     .persisted = true },
    {              .name = "noglob",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Disable pathname expansion (set -f)",
     .persisted = true },
    {             .name = "hashall",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Command hashing (set -h)",
     .persisted = true },
    /// monitor: persisted=false: job control is environmentally
    /// auto-determined (interactive shells enable it), never serialized -- like
    /// bash, which does not save monitor. The interactive auto-enable writes
    /// the registry so config get/show are correct, but config save never
    /// records it (a non-interactive reload must not inherit job control).
    {             .name = "monitor",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Job control (set -m)",
     .persisted = false},
    {           .name = "allexport",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Auto export variables (set -a)",
     .persisted = true },
    {           .name = "noclobber",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Prevent file overwrite (set -C)",
     .persisted = true },
    {              .name = "onecmd",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Exit after one command (set -t)",
     .persisted = true },
    {              .name = "notify",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Async job notification (set -b)",
     .persisted = true },
    {           .name = "ignoreeof",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Prevent exit on EOF (set -o ignoreeof)",
     .persisted = true },
    {               .name = "nolog",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Prevent function history logging (set -o nolog)",
     .persisted = true },
    /// editing_mode is the single-valued editor key: one layered string
    /// ("emacs" | "vi") the registry resolves last-wins across config layers.
    /// emacs/vi are derived aliases (persisted=false) that route their writes
    /// here and read back from the live editor field, so multi-file precedence
    /// follows the standard layer order rather than two change-gated booleans.
    {        .name = "editing_mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "emacs"},
     .help = "Line editing mode: emacs or vi (set -o emacs / set -o vi)",
     .persisted = true },
    /// emacs/vi remain loadable (a legacy config's emacs=true/vi=true still
    /// selects the editor, routed to editing_mode by the shell.* subscriber),
    /// but are NOT saved -- editing_mode is the one canonical key config save
    /// writes (see the emacs/vi skip in config_registry_save). persisted=true
    /// keeps them loadable; the save-side skip keeps the alias out of the file.
    {               .name = "emacs",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Emacs-style editing (alias of editing_mode=emacs)",
     .persisted = true },
    {                  .name = "vi",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Vi-style editing (alias of editing_mode=vi)",
     .persisted = true },
    /// posix: persisted=false + derive-only: posix-strictness is purely a
    /// projection of the active mode (only POSIX mode implies it). config
    /// get/show derive it live from shell_mode_get(); config set shell.posix
    /// routes to a mode switch. It is never a layered/saved value a SESSION
    /// write could pin against the mode.
    {               .name = "posix",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Strict POSIX compliance (set -o posix)",
     .persisted = false},
    /// restricted: persisted=false + derive-only + read-only. The
    /// restricted-shell security flag is engaged once at invocation (-r /
    /// set -o restricted) and is one-way. config get/show observe it live
    /// from shell_opts.restricted_mode; config set refuses it (never a
    /// settable or saved value).
    {          .name = "restricted",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Restricted shell (-r / set -o restricted); read-only",
     .persisted = false},
    {            .name = "pipefail",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Pipeline failure detection (set -o pipefail)",
     .persisted = true },
    {          .name = "histexpand",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "History expansion (set -o histexpand)",
     .persisted = true },
    {             .name = "history",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Command history recording (set -o history)",
     .persisted = true },
    {.name = "interactive-comments",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Interactive comments (set -o interactive-comments)",
     .persisted = true },
    {            .name = "physical",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Physical directory paths (set -o physical)",
     .persisted = true },
    {          .name = "privileged",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Restricted shell security (set -o privileged)",
     .persisted = true },
    {            .name = "errtrace",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "ERR trap inherits into functions (set -E)",
     .persisted = true },
    {           .name = "functrace",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "DEBUG/RETURN traps inherit into functions (set -T)",
     .persisted = true },
    { .name = "pipeline-diagnostic",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Structured error per non-zero pipeline stage",
     .persisted = true },
};

static const creg_section_t shell_section = {
    .name = "shell",
    .options = shell_options,
    .option_count = sizeof(shell_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// No sync hooks: shell.* booleans ride the shell.* subscriber and
    /// shell.mode rides the shell.mode subscriber (both reconcile the runtime
    /// on every registry write). The section is pure-subscriber.
    .sync_to_runtime = NULL,
    .sync_from_runtime = NULL,
};

/* ----------------------------------------------------------------------------
 * Display Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t display_options[] = {
    {.name = "syntax_highlighting",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable syntax highlighting",
     .persisted = true,
     .description =
         "Colors the command line as you type. Commands, arguments, strings, "
         "and operators each get a distinct color, so an unknown command or an "
         "unclosed quote stands out before you press Enter."},
    {.name = "autosuggestions",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable Fish-style autosuggestions",
     .persisted = true,
     .description =
         "Shows a greyed-out suggestion for the rest of the line as you type, "
         "taken from your history (and from completion when history has no "
         "match). Press the Right arrow or End to accept it, or keep typing to "
         "ignore it. It only suggests; it never runs anything on its own."},
    {.name = "transient_prompt",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Enable transient prompts",
     .persisted = true,
     .description =
         "After a command runs, collapses its prompt to a compact form so the "
         "scrollback stays clean. The full prompt is shown only on the line "
         "you are currently editing."},
    {.name = "optimization_level",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 2},
     .help = "Display optimization level (0-4)",
     .persisted = true},
    {.name = "theme_hot_reload",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Auto-reload theme when its file changes on disk",
     .persisted = true},
    {.name = "newline_before_prompt",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Print a blank line before each prompt",
     .persisted = true},
    {.name = "lle.pager.enabled",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Master switch for the LLE pager",
     .persisted = true},
    {.name = "lle.pager.min_lines",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 0},
     .help = "Pager threshold in visual rows (0 = use terminal rows)",
     .persisted = true},
    {.name = "lle.pager.wrap_search",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Wrap pager search to top on no-match (less-style)",
     .persisted = true},
    {.name = "lle.theme",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = ""},
     .help = "LLE prompt theme name (empty uses the default theme)",
     .persisted = true},
    {.name = "ambiguous_width",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "narrow"},
     .help = "East Asian Ambiguous-class display width: narrow or wide",
     .persisted = true},
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
    {.name = "enabled",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable tab completion",
     .persisted = true},
    {.name = "case_sensitive",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Case-sensitive completion",
     .persisted = true,
     .description =
         "Whether Tab completion treats uppercase and lowercase as different. "
         "When off, typing 'down' can complete 'Downloads'."},
    {.name = "chain_directories",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Re-trigger completion after accepting a directory (per-mode "
             "default: lush=true, others=false)", .persisted = true},
    {.name = "menu_shadow_ghost",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Show an open menu's highlighted candidate inline as a shadow "
             "ghost (per-mode default: lush=true, others=false)", .persisted = true},
    {.name = "match_mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "prefix"},
     .help = "Completion match predicate: prefix, substring, or fuzzy "
             "(per-mode default: lush=fuzzy, others=prefix)", .persisted = true,
     .description =
         "Decides how Tab completion matches what you have typed against each "
         "candidate: 'prefix' matches only from the start, 'substring' matches "
         "your text appearing anywhere in the candidate, and 'fuzzy' matches "
         "the typed characters in order with gaps allowed (fzf-style)."},
    {.name = "threshold",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 60},
     .help = "Minimum fuzzy match score to accept a completion (0-100)",
     .persisted = true},
    {.name = "fuzzy_min_chars",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 2},
     .help =
         "Typed characters before substring/fuzzy widen past a prefix match "
         "(0 or 1 disables the floor; shorter input stays prefix-scoped)", .persisted = true},
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
    {.name = "dismiss_policy",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "on_deviation"},
     .help = "When to clear the ghost text: on_deviation (only on a "
     "non-matching keystroke; persists through matching spaces) or "
     "on_word_boundary (clear at a trailing space)", .persisted = true  },
    {          .name = "rank",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "frecency"},
     .help = "Which history prefix match to suggest: frecency (most used x "
     "recent) or recency (most recent)", .persisted = true              },
    {.name = "partial_accept",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "path_segment"},
     .help = "Ctrl+Right granularity: path_segment (advance by directory "
     "inside a path) or word (whole whitespace word)", .persisted = true},
    {       .name = "sources",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING,
     .data.string = "history_then_completion"},
     .help = "Ghost text sources: history (history only) or "
     "history_then_completion (fall back to completion when history "
     "misses)", .persisted = true                                       },
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
    {.name = "auto_cd",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Auto-cd to directories",
     .persisted = true,
     .description =
         "Lets you change directories by typing a directory's name on its own, "
         "without the 'cd' command. Typing 'projects' then Enter behaves like "
         "'cd projects'."},
    {.name = "spell_correction",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Enable spell correction",
     .persisted = true,
     .description = "When a command is not found but closely resembles a known "
                    "one, offers "
                    "the likely intended command as a 'did you mean' "
                    "suggestion instead of "
                    "only reporting the error."},
    {.name = "tab_width",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 4},
     .help = "Tab width for display",
     .persisted = true},
    {.name = "brace_expansion_max",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 65536},
     .help = "Max brace expansion result count (0 = unbounded)",
     .persisted = true},
    {.name = "regex_pattern_max",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 1024},
     .help = "Max regex pattern length before rejection (0 = unbounded)",
     .persisted = true},
    {.name = "path_negative_cache_ttl_ms",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     .help = "TTL in milliseconds for the negative PATH-search cache (0 = "
             "disabled)", .persisted = true},
    {.name = "loop_failure_streak",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     .help = "Consecutive non-zero loop iterations before runaway-loop trip "
             "(0 = disable)", .persisted = true},
    {.name = "loop_failure_seconds",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 5},
     .help = "Min wall-clock seconds the streak must last before tripping",
     .persisted = true},
    {.name = "autocorrect_max_suggestions",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 3},
     .help = "Maximum auto-correction suggestions (1-5)",
     .persisted = true},
    {.name = "autocorrect_threshold",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 40},
     .help = "Auto-correction similarity threshold (0-100)",
     .persisted = true},
    {.name = "autocorrect_interactive",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Show interactive correction prompts",
     .persisted = true},
    {.name = "autocorrect_learn_history",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Learn commands from history",
     .persisted = true},
    {.name = "autocorrect_builtins",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Suggest builtin corrections",
     .persisted = true},
    {.name = "autocorrect_external",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Suggest external command corrections",
     .persisted = true},
    {.name = "autocorrect_case_sensitive",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Case-sensitive auto-correction",
     .persisted = true},
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
 * Jobs Section Options (background job control)
 *
 * jobs.retain_completed governs the completed-job retention lifecycle. lush's
 * default is single-consumption: a background job is dropped the moment an
 * explicit `wait` returns its status. This cell is read directly at wait time
 * via config_get_bool (no runtime binding); the lifecycle lives in bin_wait.c.
 * -------------------------------------------------------------------------- */
static const creg_option_t jobs_options[] = {
    {.name = "retain_completed",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Keep a completed job addressable after `wait` reports its status",
     .persisted = true,
     .description =
         "Governs how long a finished background job's exit status stays "
         "reachable by `wait`.\n\n"
         "lush default (off): single-consumption. An explicit `wait` for a job "
         "returns its exit status once and then drops the job -- you asked, "
         "you "
         "received it, it is gone. Listing the job with `jobs` or seeing its "
         "completion notice reports it but does not consume it; only `wait` "
         "does. This keeps job state predictable (no lingering already-waited "
         "jobs) and honors the POSIX rule of remembering a terminated job's "
         "status only until it is waited for. A never-waited completion is "
         "held "
         "in a bounded backstop so a background loop cannot leak.\n\n"
         "On: a completed job stays addressable by repeated or later `wait` "
         "calls until it ages out of the backstop -- the legacy bash behavior "
         "(where `wait %1` twice returns the same status). Offered as an "
         "explicit opt-in, never as an unexamined default."},
};

static const creg_section_t jobs_section = {
    .name = "jobs",
    .options = jobs_options,
    .option_count = sizeof(jobs_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    /// Read directly at wait time (config_get_bool); no runtime binding.
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
    {.name = "enable_deduplication",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Deduplicate history entries",
     .persisted = true,
     .description =
         "Keeps repeated commands from piling up in your history. By default, "
         "running a command again moves its existing entry to the most recent "
         "position rather than storing a second copy."},
    {.name = "hist_ignore_space",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     .help = "Do not save commands that start with a space",
     .persisted = true,
     .description =
         "When on, a command line beginning with a space is not written to "
         "history -- a quick way to run something without recording it. Off by "
         "default, matching bash and zsh."},
    {.name = "dedup_scope",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "session"},
     .help = "Dedup scope: none, session, recent, or global",
     .persisted = true},
    {.name = "dedup_strategy",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "keep-recent"},
     .help = "Dedup strategy: ignore, keep-recent, keep-frequent, merge, or "
             "keep-all", .persisted = true},
    {.name = "dedup_navigation",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Skip duplicates while navigating history",
     .persisted = true},
    {.name = "dedup_navigation_unique",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Show only unique entries during navigation",
     .persisted = true},
    {.name = "dedup_unicode_normalize",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Apply Unicode NFC normalization before comparing entries",
     .persisted = true},
    {.name = "arrow_key_mode",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = "context-aware"},
     .help = "Arrow key behavior: context-aware, classic, always-history, or "
             "multiline-first", .persisted = true},
    {.name = "history_file",
     .type = CREG_VALUE_STRING,
     .default_val = {.type = CREG_VALUE_STRING, .data.string = ""},
     .help = "History file path (empty uses the default ~/.lush_history)",
     .persisted = true},
    {.name = "enable_forensic_tracking",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Track metadata (timestamps, exit codes, cwd) with history "
             "entries", .persisted = true},
    {.name = "enable_history_cache",
     .type = CREG_VALUE_BOOLEAN,
     .default_val = {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     .help = "Cache history for faster lookups",
     .persisted = true},
    {.name = "cache_size",
     .type = CREG_VALUE_INTEGER,
     .default_val = {.type = CREG_VALUE_INTEGER, .data.integer = 100},
     .help = "History cache size",
     .persisted = true},
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

/// @brief Apply a shell.* boolean change to the runtime shell_opts struct.
///
/// Registered as a `shell.*` subscriber so every registry write to a shell
/// option -- from `config set shell.X`, from `set -o`/`+o`'s
/// sync_shell_option_to_registry, or a future setopt route -- write-throughs to
/// shell_opts via config_set_shell_option (which carries the option side
/// effects, e.g. the emacs/vi mutual exclusion + lush_update_editing_mode that
/// a plain binding could not). shell.mode (a string) and shell.feature.* (the
/// separate FEATURE_* matrix) are not shell_opts booleans and are skipped.
/// shell_opts stays the executor's hot-path read target; the registry is the
/// write surface.
static void shell_option_registry_apply(const char *key,
                                        const creg_value_t *old_value,
                                        const creg_value_t *new_value,
                                        void *user_data) {
    (void)old_value;
    (void)user_data;
    if (!key || !new_value) {
        return;
    }
    /// The single-valued editor. shell.editing_mode is the storage key; its
    /// string value ("emacs" | "vi") drives both runtime bools and the live
    /// editor, so every layer / save / set path resolves through this one key.
    if (strcmp(key, "shell.editing_mode") == 0 &&
        new_value->type == CREG_VALUE_STRING) {
        bool vi = (strcmp(new_value->data.string, "vi") == 0);
        shell_opts.vi_mode = vi;
        shell_opts.emacs_mode = !vi;
        lush_update_editing_mode();
        return;
    }
    if (new_value->type != CREG_VALUE_BOOLEAN) {
        return;
    }
    if (strncmp(key, "shell.feature.", 14) == 0) {
        return;
    }
    /// emacs/vi are derived aliases: a boolean write (a legacy config file's
    /// emacs=true / vi=true, or set -o emacs/vi's registry sync) applies to
    /// the runtime editor here and mirrors the choice into the single
    /// editing_mode key so config get / show / save reflect it. The registry's
    /// nested-notify guard suppresses the editing_mode write's callback, so the
    /// bools are set directly above rather than relying on a cascade.
    if (strcmp(key, "shell.emacs") == 0 || strcmp(key, "shell.vi") == 0) {
        bool emacs_on = (strcmp(key, "shell.emacs") == 0)
                            ? new_value->data.boolean
                            : !new_value->data.boolean;
        shell_opts.emacs_mode = emacs_on;
        shell_opts.vi_mode = !emacs_on;
        lush_update_editing_mode();
        config_registry_set_string("shell.editing_mode",
                                   emacs_on ? "emacs" : "vi");
        return;
    }
    config_set_shell_option(key, new_value->data.boolean);
}

/// ===========================================================================
/// shell.feature.* -- the feature matrix as first-class registry keys
/// ===========================================================================
///
/// The FEATURE_* matrix (per-mode feature defaults plus runtime overrides) is a
/// second shell truth store alongside shell_opts. Each feature is registered as
/// a key "shell.feature.<canonical-name>" so config get / show, setopt /
/// unsetopt / shopt, and mode presets all route through the one layered store.
/// The runtime read path stays shell_mode_allows(); the registry is the write +
/// query surface and mirrors it via a subscriber, exactly as shell_opts does.
///
/// register_option stores the option_def pointer, so the defs and their key
/// strings outlive the registry in these file-scope arrays.
static creg_option_t g_feature_options[FEATURE_COUNT];
static char g_feature_keys[FEATURE_COUNT][CREG_KEY_MAX];
static bool g_features_registered = false;

/// Mirror a registry write to shell.feature.<name> into the runtime matrix. A
/// present SESSION layer means the user pinned the feature (override); its
/// absence means follow the active mode's matrix (reset). This keeps
/// shell_mode_allows() -- the executor's read path -- in lockstep with the
/// layered store without making the registry a parallel truth.
static void shell_feature_registry_apply(const char *key,
                                         const creg_value_t *old_value,
                                         const creg_value_t *new_value,
                                         void *user_data) {
    (void)old_value;
    (void)user_data;
    if (!key || strncmp(key, "shell.feature.", 14) != 0) {
        return;
    }
    shell_feature_t feature;
    bool invert = false;
    if (!shell_feature_parse(key + 14, &feature, &invert)) {
        return;
    }
    /// Canonical keys carry the underlying feature's own sense (canonical names
    /// never invert), so the effective value is the feature state directly.
    creg_inspect_t info;
    if (config_registry_inspect(key, &info) == CREG_SUCCESS &&
        info.layers[CREG_LAYER_SESSION].present) {
        if (new_value && new_value->type == CREG_VALUE_BOOLEAN &&
            new_value->data.boolean) {
            shell_feature_enable(feature);
        } else {
            shell_feature_disable(feature);
        }
    } else {
        /// No interactive override -> follow the mode matrix.
        shell_feature_reset(feature);
    }
}

void shell_seed_feature_modes(shell_mode_t mode) {
    if (!config_registry_is_initialized()) {
        return;
    }
    /// Seed every feature's MODE layer from the matrix default for @p mode and
    /// drop any interactive override, so the new mode's defaults take effect.
    /// The mode-change companion to apply_mode_preset's
    /// shell_feature_reset_all: features are mode-sticky (a mode switch clears
    /// overrides), unlike user-sticky shell options.
    for (int i = 0; i < (int)FEATURE_COUNT; i++) {
        shell_feature_t feature = (shell_feature_t)i;
        char key[CREG_KEY_MAX];
        snprintf(key, sizeof(key), "shell.feature.%s",
                 shell_feature_name(feature));
        config_registry_reset(key);
        creg_value_t v =
            creg_value_boolean(shell_mode_feature_default(mode, feature));
        config_registry_set_mode_value(key, &v);
    }
}

void shell_register_features(void) {
    if (g_features_registered || !config_registry_is_initialized()) {
        return;
    }
    for (int i = 0; i < (int)FEATURE_COUNT; i++) {
        shell_feature_t feature = (shell_feature_t)i;
        snprintf(g_feature_keys[i], CREG_KEY_MAX, "feature.%s",
                 shell_feature_name(feature));
        g_feature_options[i].name = g_feature_keys[i];
        g_feature_options[i].type = CREG_VALUE_BOOLEAN;
        g_feature_options[i].default_val = creg_value_boolean(false);
        /// The key name is the feature name; no separate help needed.
        g_feature_options[i].help = NULL;
        /// Features are mode-derived, never written to the config file.
        g_feature_options[i].persisted = false;
        /// No guided description: features are not a wizard surface.
        g_feature_options[i].description = NULL;
        config_registry_register_option("shell", &g_feature_options[i]);
    }
    g_features_registered = true;
    config_registry_subscribe("shell.feature.*", shell_feature_registry_apply,
                              NULL);
    /// MODE-layer seeding is driven by config_init / apply_mode_preset right
    /// after config_registry_apply_mode_defaults, which clears every MODE slot;
    /// seeding here would just be wiped by that overlay pass.
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
/// cell) and need no binding.
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
    config_registry_bind_integer("completion.fuzzy_min_chars",
                                 &config.completion_fuzzy_min_chars);
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
    config_registry_bind_boolean("lle.hist_ignore_space",
                                 &config.lle_hist_ignore_space);
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
    config_registry_register_section(&jobs_section);
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

    /// The shell section keeps shell_opts as the executor's hot-path truth, so
    /// instead of bindings it uses a subscriber: every registry write to a
    /// shell.* boolean write-throughs to shell_opts via config_set_shell_option
    /// (preserving option side effects). config set / set -o route through the
    /// registry; shell_opts is the read target.
    config_registry_subscribe("shell.*", shell_option_registry_apply, NULL);

    /// Register the feature matrix as shell.feature.* keys. The shell.*
    /// subscriber above skips feature keys; a dedicated shell.feature.*
    /// subscriber mirrors them into the runtime matrix. MODE-layer seeding runs
    /// later in config_init, after config_registry_apply_mode_defaults.
    shell_register_features();

    /// shell.mode is registry-driven: a subscriber reconciles the runtime mode
    /// (feature matrix, per-mode defaults) to whatever layer write wins, so a
    /// lushrc mode= (USER) takes effect while a CLI flag (SESSION) still wins.
    shell_mode_register_runtime_subscriber();
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

/// Enum-typed keys: a value must equal one of the pair-table names. The table
/// is the same one the binding maps through, so the allowed set has one source.
/// shell.editing_mode enum members (#390). The int values are unused -- the
/// key is stored and read as its string by the shell.* subscriber, not bound
/// to an int cell -- but registering the pairs gives the key a creg_type_t
/// enum descriptor, so an invalid value is rejected at the single write funnel
/// (config set, setopt, and TOML load alike) rather than only at config set.
static const creg_enum_pair_t editing_mode_pairs[] = {
    {"emacs", 0},
    {   "vi", 1},
    {   NULL, 0},
};

static const struct {
    const char *key;
    const creg_enum_pair_t *pairs;
} k_enum_types[] = {
    {           "shell.editing_mode",                  editing_mode_pairs},
    {        "completion.match_mode",         completion_match_mode_pairs},
    {          "history.search_mode",           history_search_mode_pairs},
    {         "history.finder.match",          history_finder_match_pairs},
    {          "history.finder.rank",           history_finder_rank_pairs},
    {       "history.finder.display",        history_finder_display_pairs},
    {           "lle.arrow_key_mode",                lle_arrow_mode_pairs},
    {              "lle.dedup_scope",               lle_dedup_scope_pairs},
    {           "lle.dedup_strategy",            lle_dedup_strategy_pairs},
    {"autosuggestion.dismiss_policy", autosuggestion_dismiss_policy_pairs},
    {          "autosuggestion.rank",           autosuggestion_rank_pairs},
    {"autosuggestion.partial_accept", autosuggestion_partial_accept_pairs},
    {       "autosuggestion.sources",        autosuggestion_sources_pairs},
};

/// Integer-range keys: [min, max] inclusive. An INT64_MAX upper bound is the
/// open-ended / non-negative case (the documented "0 = unbounded/disabled"
/// limits, where a negative value is invalid but there is no real upper cap).
/// Ranges mirror the bounds in each key's help string.
static const struct {
    const char *key;
    int64_t min;
    int64_t max;
} k_range_types[] = {
    {          "display.optimization_level", 0,       4},
    {                "completion.threshold", 0,     100},
    {          "completion.fuzzy_min_chars", 0,      32},
    {      "behavior.autocorrect_threshold", 0,     100},
    {"behavior.autocorrect_max_suggestions", 1,       5},
    /// The open-ended / non-negative keys bind to 32-bit int runtime cells, so
    /// the upper bound is INT_MAX (the cell's ceiling), not INT64_MAX: a value
    /// the int cell cannot hold must be rejected, not accepted and then
    /// truncated through the binding into a negative (which these keys read as
    /// disabled/unbounded). describe renders an INT_MAX top as open-ended.
    {        "behavior.brace_expansion_max", 0, INT_MAX},
    {          "behavior.regex_pattern_max", 0, INT_MAX},
    { "behavior.path_negative_cache_ttl_ms", 0, INT_MAX},
    {        "behavior.loop_failure_streak", 0, INT_MAX},
    {       "behavior.loop_failure_seconds", 0, INT_MAX},
    /// history.size binds a 32-bit int cell (consumers read `> 0 ? n : 5000`).
    /// Typed so the wizard and config set reject a negative or > INT_MAX value
    /// that would otherwise truncate through the binding and silently diverge
    /// from what the registry stores (the bound-cell == effective invariant).
    {                        "history.size", 0, INT_MAX},
};

/// Descriptor storage: one creg_type_t per typed key, filled at registration
/// and attached. File-scope so the descriptors outlive the registry.
static creg_type_t
    g_enum_type_storage[sizeof(k_enum_types) / sizeof(k_enum_types[0])];
static creg_type_t
    g_range_type_storage[sizeof(k_range_types) / sizeof(k_range_types[0])];

/// Count of type descriptors that failed to attach in config_register_types --
/// nonzero means a table key is misspelled, unregistered, or has a storage kind
/// the descriptor does not match, so that key validates nothing. A test asserts
/// this is zero; the getter below exposes it.
static int g_type_attach_failures = 0;

int config_type_attach_failure_count(void) { return g_type_attach_failures; }

/// Attach type descriptors to registered keys so the registry validates writes
/// (config set, setopt, TOML load) at its single chokepoint. Called from
/// config_init after config_register_sections so the keys exist.
static void config_register_types(void) {
    g_type_attach_failures = 0;
    for (size_t i = 0; i < sizeof(k_enum_types) / sizeof(k_enum_types[0]);
         i++) {
        creg_type_init_enum(&g_enum_type_storage[i], k_enum_types[i].pairs);
        if (config_registry_set_type(k_enum_types[i].key,
                                     &g_enum_type_storage[i]) != CREG_SUCCESS) {
            g_type_attach_failures++;
        }
    }
    for (size_t i = 0; i < sizeof(k_range_types) / sizeof(k_range_types[0]);
         i++) {
        creg_type_init_int_range(&g_range_type_storage[i], k_range_types[i].min,
                                 k_range_types[i].max);
        if (config_registry_set_type(k_range_types[i].key,
                                     &g_range_type_storage[i]) !=
            CREG_SUCCESS) {
            g_type_attach_failures++;
        }
    }
}

/// The curated beginner tier: the handful of settings a new user actually wants
/// to personalize, each chosen for immediate visible feedback, safe semantics
/// (changing it cannot break scripting or POSIX expectations), and high
/// return on personalization. The wizard walks exactly this set. Other keys
/// stay CREG_TIER_UNSET until tiered incrementally; do not pad this list --
/// every addition needs the same three-part justification. This is an
/// attachment set, not a presentation order: collect_by_tier returns the keys
/// in registration (section) order, which is how the wizard presents them.
static const char *const k_beginner_keys[] = {
    "display.syntax_highlighting", ///< color as you type
    "display.autosuggestions",     ///< history ghost suggestions
    "display.transient_prompt",    ///< collapse past prompts, clean scrollback
    "completion.match_mode",       ///< prefix / substring / fuzzy feel
    "completion.case_sensitive",   ///< everyday completion behavior
    "history.size",                ///< how much history to keep
    "behavior.auto_cd",            ///< cd by typing a bare directory name
    "behavior.spell_correction",   ///< offer corrections for mistyped commands
    "lle.enable_deduplication",    ///< drop duplicate history entries
};

/// Count of beginner tiers that failed to attach -- nonzero means a key above
/// is misspelled or unregistered, so the wizard would silently skip it. A test
/// asserts this is zero; the getter exposes it.
static int g_tier_attach_failures = 0;

int config_tier_attach_failure_count(void) { return g_tier_attach_failures; }

/// Attach discoverability tiers to registered keys. Called from config_init
/// after config_register_sections so the keys exist. Mirrors
/// config_register_types -- tiers are schema metadata attached
/// post-registration at the same point, read by the wizard (and a future
/// tier-grouped show).
static void config_register_tiers(void) {
    g_tier_attach_failures = 0;
    for (size_t i = 0; i < sizeof(k_beginner_keys) / sizeof(k_beginner_keys[0]);
         i++) {
        if (config_registry_set_tier(k_beginner_keys[i], CREG_TIER_BEGINNER) !=
            CREG_SUCCESS) {
            g_tier_attach_failures++;
        }
    }
}

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

/// Legacy option name mapping for backward compatibility
typedef struct {
    const char *old_name;
    const char *new_name;
} legacy_option_mapping_t;

static legacy_option_mapping_t legacy_mappings[] = {
    /// History options
    {            "history_enabled",                      "history.enabled"},
    {               "history_size",                         "history.size"},
    {         "history_timestamps",                   "history.timestamps"},

    /// Completion options
    {         "completion_enabled",                   "completion.enabled"},
    {      "completion_match_mode",                "completion.match_mode"},
    {       "completion_threshold",                 "completion.threshold"},
    {  "completion_case_sensitive",            "completion.case_sensitive"},

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
    {                  "tab_width",                   "behavior.tab_width"},
    {        "brace_expansion_max",         "behavior.brace_expansion_max"},
    {          "regex_pattern_max",           "behavior.regex_pattern_max"},
    { "path_negative_cache_ttl_ms",  "behavior.path_negative_cache_ttl_ms"},
    {        "loop_failure_streak",         "behavior.loop_failure_streak"},
    {       "loop_failure_seconds",        "behavior.loop_failure_seconds"},
    {       "display_optimization",           "display.optimization_level"},

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
        /// Editor mode is single-valued: setting one clears the partner field.
        /// config get/show read these live from shell_opts (the executor's
        /// truth), so this field clear is the whole story -- there is no
        /// partner registry write to misfire into the wrong layer. config set /
        /// set -o reconcile the registry pair at the SESSION layer
        /// (config_set_value / sync_shell_option_to_registry) for save and
        /// provenance.
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
    } else if (strcmp(opt_name, "errtrace") == 0) {
        shell_opts.errtrace = value;
    } else if (strcmp(opt_name, "functrace") == 0) {
        shell_opts.functrace = value;
    } else if (strcmp(opt_name, "pipeline-diagnostic") == 0) {
        shell_opts.pipeline_diagnostic_mode = value;
    } else if (strcmp(opt_name, "mode_strict") == 0) {
        /// mode_strict is not a shell_opts field: it gates runtime mode changes
        /// via shell_mode_set_strict (the read truth is g_shell_mode_state),
        /// mirrored into config.shell_mode_strict for the config-get surface.
        shell_mode_set_strict(value);
        config.shell_mode_strict = value;
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
    } else if (strcmp(opt_name, "restricted") == 0) {
        /// Read-only projection of the ENGAGED restricted-shell state -- the
        /// enforcement gate the executor actually consults
        /// (restricted_mode_is_engaged), not the requested-only
        /// restricted_mode. The two diverge for a bare `set -o restricted`,
        /// which sets restricted_mode (requested) without engaging
        /// enforcement, so surfacing restricted_mode would falsely report
        /// "restricted" while cd and friends still work. Under `-r` both flags
        /// are true (engagement runs once after rc files and never clears the
        /// requested flag). restricted_mode_engaged is the honest answer to
        /// "is this shell restricted now".
        return shell_opts.restricted_mode_engaged;
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
    } else if (strcmp(opt_name, "errtrace") == 0) {
        return shell_opts.errtrace;
    } else if (strcmp(opt_name, "functrace") == 0) {
        return shell_opts.functrace;
    } else if (strcmp(opt_name, "pipeline-diagnostic") == 0) {
        return shell_opts.pipeline_diagnostic_mode;
    }

    return false; /// Unknown option
}

/// Traditional shell script file paths
#define PROFILE_SCRIPT_FILE ".profile"
#define LOGIN_SCRIPT_FILE ".lush_login"
#define RC_SCRIPT_FILE ".lushrc"
#define LOGOUT_SCRIPT_FILE ".lush_logout"

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
    int result = 0;

    /// 1. Source /etc/lushrc if it exists (lush-specific system config)
    /// This runs in native lush mode since it's a lush-specific file
    if (config_script_exists("/etc/lushrc")) {
        if (config_execute_script_file("/etc/lushrc") != 0) {
            /// Log warning but continue - system config failure shouldn't
            /// block login.
            shell_error_emit(SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                             SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                             "error sourcing /etc/lushrc");
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
            shell_error_emit(SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                             SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                             "error sourcing /etc/profile");
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
 * Executes ~/.lushrc if it exists.
 *
 * @return 0 on success, -1 if any script fails
 */
int config_execute_startup_scripts(void) {
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
 * Executes ~/.profile and ~/.lush_login if they exist.
 *
 * @return 0 on success, -1 if any script fails
 */
int config_execute_login_scripts(void) {
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
 * Executes ~/.lush_logout if it exists.
 *
 * @return 0 on success, -1 if script fails
 */
int config_execute_logout_scripts(void) {
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
    "history.size = 10000\n"
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
    "# Skip commands that begin with a space (setopt hist_ignore_space)\n"
    "lle.hist_ignore_space = false\n"
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
    "# DISPLAY SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
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

    /// config_init is re-entrant: `config reload` calls it again. The memset
    /// below drops config_ctx's heap-owned path pointers, so free them first or
    /// every reload leaks them (config_cleanup frees the same three at exit).
    /// On the first init config_ctx is zero-initialized, so these are NULL and
    /// free(NULL) is a no-op.
    free(config_ctx.user_config_path);
    free(config_ctx.system_config_path);
    free(config_ctx.xdg_config_dir);

    /// Initialize context
    memset(&config_ctx, 0, sizeof(config_ctx));

    /// Initialize config registry and register all sections
    config_register_sections();

    /// Attach type descriptors so the registry validates writes at its set
    /// chokepoint. Must come after section registration (the keys must exist).
    config_register_types();

    /// Attach discoverability tiers (the wizard's beginner set). Must come
    /// after section registration (the keys must exist).
    config_register_tiers();

    /// Register per-mode default overrides. Must come after section
    /// registration (the options must exist before per-mode defaults
    /// can attach to them), and before the per-mode defaults apply
    /// call below.
    config_register_per_mode_defaults();

    /// Pin an explicitly chosen bootstrap mode in the SESSION layer (highest
    /// precedence) so it outranks a lushrc mode=: a CLI flag
    /// (--posix/--bash/--zsh/--lush) or a script shebang that selected a
    /// non-default mode. Both are the user's/script's explicit choice. A plain
    /// default-lush startup writes nothing, leaving shell.mode on the DEFAULT
    /// layer so a lushrc mode= (USER) can take effect. Either way the registry
    /// matches the runtime (config explain / save stay honest), and the write
    /// fires the shell.mode subscriber, which reconciles the runtime.
    if (shell_opts.cli_mode_override_set ||
        shell_mode_get() != SHELL_MODE_LUSH) {
        config_registry_set_string("shell.mode",
                                   shell_mode_name(shell_mode_get()));
    }

    /// Apply per-mode default overrides for the active shell mode before
    /// loading user config, so any user lushrc settings layer on top of
    /// the right preset.
    config_registry_apply_mode_defaults(shell_mode_get());

    /// Seed the feature matrix's MODE layer after the generic overlay:
    /// apply_mode_defaults clears every MODE slot, so feature seeding has to
    /// follow it or it gets wiped.
    shell_seed_feature_modes(shell_mode_get());

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

    /// Configuration is TOML-only. The user config resolves to the XDG TOML
    /// (~/.config/lush/lushrc.toml), falling back to the home-dir TOML
    /// (~/.lushrc.toml) for non-XDG layouts; both are TOML and load through the
    /// registry. (The "legacy" in config_get_user_config_path / config_get_-
    /// legacy_config_path names that home-dir TOML *location*, not the retired
    /// INI format -- ~/.lushrc, the bare RC_SCRIPT_FILE, is a shell script and
    /// is never read here.) Routing through the shared helper keeps startup,
    /// config save, and config reset-defaults resolving the same path.
    struct stat st;

    config_ctx.user_config_path = config_get_user_config_path();
    config_ctx.format = CONFIG_FORMAT_TOML;

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
    /// History defaults. 10000 matches LLE_HISTORY_DEFAULT_CAPACITY (the 0 ->
    /// "use default" fallback) and the registry DEFAULT layer above; the three
    /// must agree (a CI schema invariant checks the bound cell == effective).
    config.history_enabled = true;
    config.history_size = 10000;
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
    config.lle_hist_ignore_space = false;
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
    config.completion_fuzzy_min_chars = 2;

    /// Behavior defaults
    config.auto_cd = false;
    config.spell_correction = true;
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
                    shell_error_emit(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                                     SOURCE_LOC_UNKNOWN,
                                     "failed to create config directory %s: %s",
                                     xdg_dir, strerror(saved_errno));
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
            /// Update user_config_path to the new TOML location.
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

    /// Configuration is TOML-only. config_registry_save materializes the
    /// runtime config to TOML through the registry; there is no legacy INI
    /// writer.
    config_registry_sync_from_runtime();
    creg_result_t result = config_registry_save(path);
    return (result == CREG_SUCCESS) ? 0 : -1;
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

    /// Configuration is TOML-only. A config file must be valid TOML, parsed
    /// through the registry; there is no legacy INI parser and no INI fallback
    /// on a parse failure -- a malformed file reports an error rather than
    /// silently misreading.
    creg_load_report_t report;
    if (config_registry_load_reported(path, &report) != CREG_SUCCESS) {
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN,
                         "failed to parse TOML config file: %s", path);
        return -1;
    }

    /// The registry drops a key it rejects (unknown, wrong kind, out of spec)
    /// so one bad line does not abort the file -- but the interactive surface
    /// errors on the same value, so report the dropped keys here too rather
    /// than ignoring them silently.
    if (report.skip_count > 0) {
        char msg[640];
        static const char MORE[] = ", ...";
        /// Reserve trailing room so the truncation marker always fits, whatever
        /// cuts the list short.
        const size_t limit = sizeof(msg) - sizeof(MORE);
        size_t off = (size_t)snprintf(
            msg, sizeof(msg), "%s: ignored %zu invalid config key%s: ", path,
            report.skip_count, report.skip_count == 1 ? "" : "s");
        if (off > limit) {
            off = limit;
        }
        /// Cap the list at the recorded array; either this cap or the message
        /// byte budget can cut it, and both must be flagged.
        size_t shown = report.skip_count < CREG_LOAD_SKIP_MAX
                           ? report.skip_count
                           : CREG_LOAD_SKIP_MAX;
        size_t listed = 0;
        while (listed < shown) {
            const char *why =
                report.skipped[listed].reason == CREG_ERROR_NOT_FOUND
                    ? "unknown key"
                : report.skipped[listed].reason == CREG_ERROR_TYPE_MISMATCH
                    ? "wrong value type"
                    : "invalid value";
            int n =
                snprintf(msg + off, sizeof(msg) - off, "%s%s (%s)",
                         listed ? ", " : "", report.skipped[listed].key, why);
            if (n < 0 || off + (size_t)n > limit) {
                break; /// keep the reserved room for the marker
            }
            off += (size_t)n;
            listed++;
        }
        if (listed < report.skip_count) {
            snprintf(msg + off, sizeof(msg) - off, "%s", MORE);
        }
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN, "%s", msg);
    }

    config_registry_sync_to_runtime();
    config_ctx.format = CONFIG_FORMAT_TOML;
    return 0;
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

    /// Apply other settings as needed
    symtable_set_global_int("AUTO_CD", config.auto_cd);
    symtable_set_global_int("SPELL_CORRECTION", config.spell_correction);

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
        printf("  wizard             - Guided setup of common settings\n");
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
            shell_error_emit(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                             SOURCE_LOC_UNKNOWN,
                             "could not determine the user config path");
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
            shell_error_emit(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                             SOURCE_LOC_UNKNOWN, "could not write to %s: %s",
                             path, strerror(errno));
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
            } else if (strcmp(argv[2], "shell") == 0) {
                section = CONFIG_SECTION_SHELL;
            }

            if (section != CONFIG_SECTION_NONE) {
                config_show_section(section);
            } else {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "unknown config section: %s", argv[2]);
            }
        } else {
            config_show_all();
        }
    } else if (strcmp(argv[1], "get") == 0) {
        if (argc < 3) {
            shell_error_emit(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "config get requires a key (usage: config get "
                             "<key>)");
            return;
        }
        config_get_value(argv[2]);
    } else if (strcmp(argv[1], "explain") == 0) {
        if (argc < 3) {
            shell_error_emit(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "config explain requires a key (usage: config "
                             "explain <key>)");
            return;
        }
        config_explain_value(argv[2]);
    } else if (strcmp(argv[1], "wizard") == 0) {
        config_wizard_run();
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            shell_error_emit(SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "config set requires a key and value (usage: "
                             "config set <key> <value>)");
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
            shell_error_emit(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                             SOURCE_LOC_UNKNOWN,
                             "failed to save configuration");
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
        printf("  Format: %s\n",
               config_ctx.format == CONFIG_FORMAT_TOML ? "TOML" : "Unknown");
    } else {
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN, "unknown config command: %s",
                         argv[1]);
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
bool config_parse_bool_text(const char *value, bool *out) {
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
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "unknown feature: %s", feature_name);
            if (err) {
                shell_error_set_suggestion(
                    err, "run 'debug features' to list available features");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            return;
        }

        bool effective = shell_mode_allows(feature) ^ invert;
        printf("%s\n", effective ? "true" : "false");
        return;
    }

    /// shell.* keys split two ways. Genuine POSIX toggles (errexit, nounset,
    /// ...) resolve registry-effective: the registry is the single source of
    /// truth, mirrored into shell_opts by the shell.* subscriber on every write
    /// path. The runtime PROJECTIONS -- shell.mode, shell.mode_strict, and the
    /// single-valued/derived shell.posix / shell.emacs / shell.vi -- read the
    /// executor's own field (shell_opts / shell_mode), so config get equals
    /// what the executor does and can never be skewed by a stale layer a file
    /// load or SESSION write left in the registry. An unknown shell.X reports
    /// Unknown rather than a bogus "false".
    if (strncmp(key, "shell.", 6) == 0) {
        if (strcmp(key, "shell.mode") == 0) {
            printf("%s\n", shell_mode_name(shell_mode_get()));
            return;
        }
        if (strcmp(key, "shell.mode_strict") == 0) {
            printf("%s\n", config.shell_mode_strict ? "true" : "false");
            return;
        }
        if (strcmp(key, "shell.editing_mode") == 0) {
            /// The single-valued editor key derives from the live editor
            /// field (kept in step with the layered registry value by the
            /// shell.* subscriber), so it reads what the executor uses.
            printf("%s\n", shell_opts.vi_mode ? "vi" : "emacs");
            return;
        }
        if (strcmp(key, "shell.posix") == 0 ||
            strcmp(key, "shell.emacs") == 0 || strcmp(key, "shell.vi") == 0 ||
            strcmp(key, "shell.restricted") == 0) {
            printf("%s\n", config_get_shell_option(key) ? "true" : "false");
            return;
        }
        bool shell_bool;
        if (config_registry_get_boolean(key, &shell_bool) == CREG_SUCCESS) {
            printf("%s\n", shell_bool ? "true" : "false");
            return;
        }
        /// Unknown shell.X: fall through to the Unknown-key path below.
    }

    /// First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
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
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN,
                         "config key '%s' is deprecated, use '%s' instead", key,
                         new_name);
        config_get_value(new_name); /// Recursive call with new name
        return;
    }

    shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                     SOURCE_LOC_UNKNOWN, "unknown configuration key: %s", key);
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
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "unknown feature: %s", feature_name);
            if (err) {
                shell_error_set_suggestion(
                    err, "run 'debug features' to list available features");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
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
            shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "invalid boolean '%s' for config key %s (use "
                             "true/false/on/off)",
                             value, key);
            return;
        }

        /// `enable` is the user's intent in alias terms; flip onto the
        /// underlying feature when the alias is inverted. Route through the
        /// registry under the canonical key so the SESSION layer records the
        /// pin and the shell.feature.* subscriber mirrors it into the matrix.
        bool target = enable ^ invert;
        if (config_registry_is_initialized()) {
            char canon[CREG_KEY_MAX];
            snprintf(canon, sizeof(canon), "shell.feature.%s",
                     shell_feature_name(feature));
            config_registry_set_boolean(canon, target);
        } else {
            if (target) {
                shell_feature_enable(feature);
            } else {
                shell_feature_disable(feature);
            }
        }
        printf("Set %s = %s\n", key, value);
        return;
    }

    /// shell.* options route to the registry / runtime directly, not the legacy
    /// config_options[] table, so the table's shell rows retire. shell.mode
    /// drives a full preset (strict-lock honored); shell.mode_strict and the
    /// boolean options write the registry, firing the shell.* subscriber that
    /// applies via config_set_shell_option (mode_strict carries the
    /// shell_mode_set_strict side effect). The registry confirms the key exists
    /// so an unknown shell.X still reports Unknown.
    if (strncmp(key, "shell.", 6) == 0) {
        if (strcmp(key, "shell.mode") == 0) {
            shell_mode_t new_mode;
            if (!shell_mode_parse(value, &new_mode)) {
                shell_error_emit(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                    SOURCE_LOC_UNKNOWN,
                    "invalid shell mode: %s (use posix/bash/zsh/lush)", value);
                return;
            }
            if (!apply_mode_preset(new_mode)) {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "cannot change shell mode: strict mode is "
                                 "enabled");
                return;
            }
            printf("Set %s = %s\n", key, value);
            return;
        }
        if (strcmp(key, "shell.restricted") == 0) {
            /// restricted is a one-way security flag engaged at invocation
            /// (-r / set -o restricted), never cleared and never enabled
            /// through config. It is observable (config get/show) but
            /// read-only: refuse the write rather than pretend to set it.
            shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "shell.restricted is read-only: the "
                             "restricted-shell flag is set only at invocation "
                             "(-r) and cannot be changed via config");
            return;
        }
        if (strcmp(key, "shell.posix") == 0) {
            /// posix is a mode projection, not an independent option: route it
            /// to a mode switch (as `set -o posix` does) so it can never pin a
            /// SESSION value against the active mode. true -> POSIX; false ->
            /// LUSH when leaving POSIX, otherwise a no-op (already non-POSIX).
            bool want;
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "on") == 0) {
                want = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                       strcmp(value, "off") == 0) {
                want = false;
            } else {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid boolean '%s' for config key %s (use "
                                 "true/false/on/off)",
                                 value, key);
                return;
            }
            shell_mode_t target = want ? SHELL_MODE_POSIX
                                       : (shell_mode_get() == SHELL_MODE_POSIX
                                              ? SHELL_MODE_LUSH
                                              : shell_mode_get());
            if (target != shell_mode_get() && !apply_mode_preset(target)) {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "cannot change shell mode: strict mode is "
                                 "enabled");
                return;
            }
            printf("Set %s = %s\n", key, value);
            return;
        }
        if (strcmp(key, "shell.editing_mode") == 0) {
            /// The single-valued editor key. Its enum descriptor (k_enum_types)
            /// validates the value at the registry write funnel, so an invalid
            /// value is rejected there (same path as a TOML load) -- no
            /// per-site value check here. On success the subscriber applies it
            /// to the runtime editor; on an invalid value the registry returns
            /// INVALID_VALUE and the builtin renders the structured error with
            /// the type's expected-values suggestion (as the generic enum path
            /// below does).
            creg_result_t rc =
                config_registry_set_string("shell.editing_mode", value);
            if (rc == CREG_SUCCESS) {
                printf("Set %s = %s\n", key, value);
            } else if (rc == CREG_ERROR_INVALID_VALUE) {
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                    SOURCE_LOC_UNKNOWN, "invalid value '%s' for config key %s",
                    value, key);
                if (err) {
                    char desc[128];
                    if (config_registry_describe_type(
                            key, desc, sizeof(desc)) == CREG_SUCCESS) {
                        char suggestion[160];
                        snprintf(suggestion, sizeof(suggestion), "expected %s",
                                 desc);
                        shell_error_set_suggestion(err, suggestion);
                    }
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                }
            }
            return;
        }
        if (strcmp(key, "shell.emacs") == 0 || strcmp(key, "shell.vi") == 0) {
            /// emacs/vi are derived aliases of the single-valued editing_mode
            /// enum: route the boolean write there so the choice lands as one
            /// layered value (correct last-wins precedence across config
            /// layers), not two change-gated booleans.
            bool want;
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "on") == 0) {
                want = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                       strcmp(value, "off") == 0) {
                want = false;
            } else {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid boolean '%s' for config key %s (use "
                                 "true/false/on/off)",
                                 value, key);
                return;
            }
            bool emacs_on = (strcmp(key, "shell.emacs") == 0) ? want : !want;
            config_registry_set_string("shell.editing_mode",
                                       emacs_on ? "emacs" : "vi");
            printf("Set %s = %s\n", key, value);
            return;
        }
        creg_value_t shell_probe;
        if (config_registry_get(key, &shell_probe) == CREG_SUCCESS) {
            bool b;
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "on") == 0) {
                b = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                       strcmp(value, "off") == 0) {
                b = false;
            } else {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid boolean '%s' for config key %s (use "
                                 "true/false/on/off)",
                                 value, key);
                return;
            }
            config_registry_set_boolean(key, b);
            printf("Set %s = %s\n", key, value);
            return;
        }
        /// Unknown shell.X: fall through to the Unknown-key path below.
    }

    /// First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            switch (opt->type) {
            case CONFIG_TYPE_BOOL:
                if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                    *(bool *)opt->value_ptr = true;
                } else if (strcmp(value, "false") == 0 ||
                           strcmp(value, "0") == 0) {
                    *(bool *)opt->value_ptr = false;
                } else {
                    shell_error_emit(
                        SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                        SOURCE_LOC_UNKNOWN,
                        "invalid boolean '%s' for config key %s (use "
                        "true/false)",
                        value, key);
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
                    shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                     SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                     "invalid color '%s' for config key %s",
                                     value, key);
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
                        shell_error_emit(
                            SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                            SOURCE_LOC_UNKNOWN,
                            "invalid value '%s' for config key %s", value, key);
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
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid boolean '%s' for config key %s (use "
                                 "true/false/on/off/yes/no)",
                                 value, key);
                return;
            }
            break;
        case CREG_VALUE_INTEGER: {
            char *end = NULL;
            errno = 0;
            long long parsed = strtoll(value, &end, 10);
            if (end == value || *end != '\0' || errno != 0) {
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid integer '%s' for config key %s",
                                 value, key);
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
                shell_error_emit(SHELL_ERR_INVALID_ARGUMENT,
                                 SHELL_SEVERITY_WARNING, SOURCE_LOC_UNKNOWN,
                                 "invalid number '%s' for config key %s", value,
                                 key);
                return;
            }
            nv.data.floating = parsed;
            break;
        }
        case CREG_VALUE_STRING:
            snprintf(nv.data.string, sizeof(nv.data.string), "%s", value);
            break;
        case CREG_VALUE_NONE:
            shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "cannot set config key %s: it has no value "
                             "type",
                             key);
            return;
        }

        creg_result_t set_rc = config_registry_set(key, &nv);
        if (set_rc == CREG_ERROR_INVALID_VALUE) {
            /// The value parsed to the right kind but failed the key's type
            /// constraint (enum membership, int range). Report through the
            /// structured error system (as the config builtin's other errors
            /// do), with the type's describe text as the suggestion -- the
            /// registry returns the result code, the shell-side builtin renders
            /// the user-facing error.
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "invalid value '%s' for config key %s",
                value, key);
            if (err) {
                char desc[128];
                if (config_registry_describe_type(key, desc, sizeof(desc)) ==
                    CREG_SUCCESS) {
                    char suggestion[160];
                    snprintf(suggestion, sizeof(suggestion), "expected %s",
                             desc);
                    shell_error_set_suggestion(err, suggestion);
                }
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
            return;
        }
        if (set_rc != CREG_SUCCESS) {
            shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN, "failed to set config key %s",
                             key);
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
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN,
                         "config key '%s' is deprecated, use '%s' instead", key,
                         new_name);
        config_set_value(new_name, value); /// Recursive call with new name
        return;
    }

    shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                     SOURCE_LOC_UNKNOWN, "unknown configuration key: %s", key);
}

/**
 * @brief Show all configuration values
 *
 * Prints all configuration sections and their values to stdout.
 */
/// Defined below; config_show_all writes every section into one buffer through
/// the worker so the whole listing pages as a unit (one pager view, not one per
/// section).
static void config_show_section_to(config_section_t section, FILE *out);

/// Write the full configuration listing to @p out.
static void config_show_all_to(FILE *out) {
    fprintf(out, "LUSH Configuration:\n\n");

    fprintf(out, "[history]\n");
    config_show_section_to(CONFIG_SECTION_HISTORY, out);

    fprintf(out, "\n[lle]\n");
    config_show_section_to(CONFIG_SECTION_LLE, out);

    fprintf(out, "\n[completion]\n");
    config_show_section_to(CONFIG_SECTION_COMPLETION, out);

    fprintf(out, "\n[behavior]\n");
    config_show_section_to(CONFIG_SECTION_BEHAVIOR, out);

    fprintf(out, "\n[display]\n");
    config_show_section_to(CONFIG_SECTION_DISPLAY, out);

    fprintf(out, "\n[autosuggestion]\n");
    config_show_section_to(CONFIG_SECTION_AUTOSUGGESTION, out);

    fprintf(out, "\n[shell]\n");
    config_show_section_to(CONFIG_SECTION_SHELL, out);
}

void config_show_all(void) {
    /// Build the whole listing into a heap buffer and hand it to the LLE pager
    /// (which streams directly when stdout is not a tty, when the pager is
    /// disabled, or when the content fits one screen). Falls back to a direct
    /// stdout render if the memory stream cannot be created.
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    if (!out) {
        config_show_all_to(stdout);
        return;
    }
    config_show_all_to(out);
    fclose(out);
    lle_pager_present(NULL, buf);
    free(buf);
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
static void config_show_registry_section(config_section_t section, FILE *out) {
    const char *reg_name = registry_section_for(section);
    if (!reg_name) {
        return;
    }
    /// Iterate the live option store (config_registry_section_option_count),
    /// not the section definition: runtime-registered options such as the
    /// shell.feature.* keys live in the store, not the static definition.
    size_t count = config_registry_section_option_count(reg_name);
    size_t prefix_len = strlen(reg_name) + 1; /// "<section>."
    for (size_t i = 0; i < count; i++) {
        char full_key[CREG_KEY_MAX];
        if (config_registry_section_option_key(
                reg_name, i, full_key, sizeof(full_key)) != CREG_SUCCESS) {
            continue;
        }
        if (legacy_has_option(full_key)) {
            continue; /// already shown by the legacy loop
        }
        creg_value_t v;
        if (config_registry_get(full_key, &v) != CREG_SUCCESS) {
            continue;
        }
        char text[CREG_VALUE_STRING_MAX];
        config_value_text(&v, text, sizeof(text));
        /// Match the legacy loop's section-relative display.
        const char *display =
            strlen(full_key) > prefix_len ? full_key + prefix_len : full_key;
        fprintf(out, "  %s = %s\n", display, text);
    }
}

/// Render the shell section from the STATIC shell_options[] schema. Boolean
/// Genuine toggles resolve registry-effective (the registry is the source of
/// truth, mirrored into shell_opts by the shell.* subscriber); mode,
/// mode_strict, posix, emacs, and vi are runtime projections read from the
/// executor's own field so the listing equals what the executor does. It does
/// not iterate the live registry store: that store also holds the 56
/// shell.feature.* keys (registered at runtime), which the static schema
/// excludes by construction. Full shell.<name> names + the schema help string,
/// matching the prior shell listing minus the feature flood.
static void config_show_shell_section(FILE *out) {
    size_t count = sizeof(shell_options) / sizeof(shell_options[0]);
    for (size_t i = 0; i < count; i++) {
        const creg_option_t *opt = &shell_options[i];
        char full_key[CREG_KEY_MAX];
        snprintf(full_key, sizeof(full_key), "shell.%s", opt->name);

        const char *val;
        if (strcmp(opt->name, "mode") == 0) {
            val = shell_mode_name(shell_mode_get());
        } else if (strcmp(opt->name, "mode_strict") == 0) {
            val = config.shell_mode_strict ? "true" : "false";
        } else if (strcmp(opt->name, "editing_mode") == 0) {
            /// String projection of the live editor field (kept in step with
            /// the layered editing_mode value by the shell.* subscriber).
            val = shell_opts.vi_mode ? "vi" : "emacs";
        } else if (strcmp(opt->name, "posix") == 0 ||
                   strcmp(opt->name, "emacs") == 0 ||
                   strcmp(opt->name, "vi") == 0 ||
                   strcmp(opt->name, "restricted") == 0) {
            /// Runtime projection: read the executor's field, not the registry.
            val = config_get_shell_option(full_key) ? "true" : "false";
        } else {
            /// Registry-effective (the registry mirrors shell_opts).
            bool b;
            val =
                (config_registry_get_boolean(full_key, &b) == CREG_SUCCESS && b)
                    ? "true"
                    : "false";
        }

        fprintf(out, "  %s = %s", full_key, val ? val : "(null)");
        if (opt->help) {
            fprintf(out, "  # %s", opt->help);
        }
        fprintf(out, "\n");
    }
}

/// Write one section's options to @p out (the legacy config_options[] entries
/// followed by the registry-only keys). The paging public wrappers build a
/// memory stream and hand it to the LLE pager.
static void config_show_section_to(config_section_t section, FILE *out) {
    /// The shell section lives entirely in the registry now (no legacy rows);
    /// render it from its static schema with live values.
    if (section == CONFIG_SECTION_SHELL) {
        config_show_shell_section(out);
        return;
    }

    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (opt->section == section) {
            fprintf(out, "  %s = ", opt->name);

            {
                switch (opt->type) {
                case CONFIG_TYPE_BOOL:
                    fprintf(out, "%s",
                            *(bool *)opt->value_ptr ? "true" : "false");
                    break;
                case CONFIG_TYPE_INT:
                    fprintf(out, "%d", *(int *)opt->value_ptr);
                    break;
                case CONFIG_TYPE_STRING: {
                    char *str_val = *(char **)opt->value_ptr;
                    fprintf(out, "%s", str_val ? str_val : "(null)");
                    break;
                }
                case CONFIG_TYPE_COLOR:
                    fprintf(out, "(color)");
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
                        fprintf(out, "%s", name);
                    }
                    break;
                }
            }

            fprintf(out, "  # %s\n", opt->description);
        }
    }

    /// Then the registry keys this section's migration moved off the legacy
    /// table, so migrated keys stay visible in `config show`.
    config_show_registry_section(section, out);
}

/// Render @p render into a heap buffer and present it through the LLE pager,
/// which streams straight to stdout when it is not a tty, when the pager is
/// disabled, or when the content fits one screen -- so piping and redirection
/// are unaffected. Falls back to a direct stdout render if the memory stream
/// cannot be created.
static void config_page_section(config_section_t section) {
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    if (!out) {
        config_show_section_to(section, stdout);
        return;
    }
    config_show_section_to(section, out);
    fclose(out);
    lle_pager_present(NULL, buf);
    free(buf);
}

void config_show_section(config_section_t section) {
    config_page_section(section);
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

    /// Free AND NULL: config_cleanup may be followed by another config_init
    /// (the config builtin's reload, and test lifecycles), whose free-before-
    /// reinit would otherwise double-free these dangling pointers.
    free(config_ctx.user_config_path);
    config_ctx.user_config_path = NULL;
    free(config_ctx.system_config_path);
    config_ctx.system_config_path = NULL;
    free(config_ctx.xdg_config_dir);
    config_ctx.xdg_config_dir = NULL;
}
