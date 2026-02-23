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
#include "lle/lle_shell_integration.h"
#include "lle/unicode_compare.h"
#include "lush.h"
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

// Global configuration instance
config_values_t config;
config_context_t config_ctx;

// Current configuration section
static config_section_t current_section = CONFIG_SECTION_NONE;

// Error handling
static char last_error[256] = "";

// ============================================================================
// INTERNAL TYPE DEFINITIONS
// ============================================================================

// Configuration option types (internal to config.c)
typedef enum {
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_COLOR,
    CONFIG_TYPE_ENUM // String-to-integer enum mapping
} config_type_t;

// Enum value mapping for CONFIG_TYPE_ENUM
typedef struct {
    const char *name; // String representation in config file
    int value;        // Corresponding enum/integer value
} config_enum_mapping_t;

// Enum definition for CONFIG_TYPE_ENUM options
typedef struct {
    const config_enum_mapping_t *mappings; // NULL-terminated array of mappings
    int default_value;                     // Default if no match found
} config_enum_def_t;

// Configuration option structure (internal to config.c)
typedef struct config_option {
    const char *name;
    config_type_t type;
    config_section_t section;
    void *value_ptr;
    const char *description;
    bool (*validator)(const char *value);
    const config_enum_def_t
        *enum_def; // For CONFIG_TYPE_ENUM: mapping definition
} config_option_t;

// ============================================================================
// ENUM MAPPING DEFINITIONS
// ============================================================================

// LLE Arrow Key Mode mappings
static const config_enum_mapping_t lle_arrow_mode_mappings[] = {
    {"context-aware", LLE_ARROW_MODE_CONTEXT_AWARE},
    {"classic", LLE_ARROW_MODE_CLASSIC},
    {"always-history", LLE_ARROW_MODE_ALWAYS_HISTORY},
    {"multiline-first", LLE_ARROW_MODE_MULTILINE_FIRST},
    {NULL, 0} // Sentinel
};
static const config_enum_def_t lle_arrow_mode_enum = {
    lle_arrow_mode_mappings, LLE_ARROW_MODE_CONTEXT_AWARE};

// LLE Storage Mode mappings
static const config_enum_mapping_t lle_storage_mode_mappings[] = {
    {"lle-only", LLE_STORAGE_MODE_LLE_ONLY},
    {"bash-only", LLE_STORAGE_MODE_BASH_ONLY},
    {"dual", LLE_STORAGE_MODE_DUAL},
    {"readline-compat", LLE_STORAGE_MODE_READLINE_COMPAT},
    {NULL, 0} // Sentinel
};
static const config_enum_def_t lle_storage_mode_enum = {
    lle_storage_mode_mappings, LLE_STORAGE_MODE_DUAL};

// LLE Dedup Scope mappings
static const config_enum_mapping_t lle_dedup_scope_mappings[] = {
    {"none", LLE_DEDUP_SCOPE_NONE},
    {"session", LLE_DEDUP_SCOPE_SESSION},
    {"recent", LLE_DEDUP_SCOPE_RECENT},
    {"global", LLE_DEDUP_SCOPE_GLOBAL},
    {NULL, 0} // Sentinel
};
static const config_enum_def_t lle_dedup_scope_enum = {lle_dedup_scope_mappings,
                                                       LLE_DEDUP_SCOPE_SESSION};

// LLE Dedup Strategy mappings
static const config_enum_mapping_t lle_dedup_strategy_mappings[] = {
    {"ignore", LLE_DEDUP_STRATEGY_IGNORE},
    {"keep-recent", LLE_DEDUP_STRATEGY_KEEP_RECENT},
    {"keep-frequent", LLE_DEDUP_STRATEGY_KEEP_FREQUENT},
    {"merge", LLE_DEDUP_STRATEGY_MERGE},
    {"keep-all", LLE_DEDUP_STRATEGY_KEEP_ALL},
    {NULL, 0} // Sentinel
};
static const config_enum_def_t lle_dedup_strategy_enum = {
    lle_dedup_strategy_mappings, LLE_DEDUP_STRATEGY_KEEP_RECENT};

// Shell Mode mappings (Phase 0: Extended Language Support)
static const config_enum_mapping_t shell_mode_mappings[] = {
    {"posix", SHELL_MODE_POSIX},
    {"sh", SHELL_MODE_POSIX},  /* Alias for posix */
    {"bash", SHELL_MODE_BASH},
    {"zsh", SHELL_MODE_ZSH},
    {"lush", SHELL_MODE_LUSH},
    {NULL, 0} // Sentinel
};
static const config_enum_def_t shell_mode_enum = {
    shell_mode_mappings, SHELL_MODE_LUSH};

// ============================================================================
// CONFIGURATION OPTION DEFINITIONS
// ============================================================================

// Configuration option definitions
static config_option_t config_options[] = {
    // History settings
    {"history.enabled", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.history_enabled, "Enable command history", config_validate_bool,
     NULL},
    {"history.size", CONFIG_TYPE_INT, CONFIG_SECTION_HISTORY,
     &config.history_size, "Maximum history entries", config_validate_int,
     NULL},
    {"history.no_dups", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.history_no_dups, "Remove duplicate history entries",
     config_validate_bool, NULL},
    {"history.timestamps", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.history_timestamps, "Add timestamps to history",
     config_validate_bool, NULL},
    {"history.file", CONFIG_TYPE_STRING, CONFIG_SECTION_HISTORY,
     &config.history_file, "History file path", config_validate_string, NULL},

    // LLE History Configuration
    {"lle.arrow_key_mode", CONFIG_TYPE_ENUM, CONFIG_SECTION_HISTORY,
     &config.lle_arrow_key_mode, "Arrow key behavior mode",
     config_validate_lle_arrow_mode, &lle_arrow_mode_enum},
    {"lle.enable_multiline_navigation", CONFIG_TYPE_BOOL,
     CONFIG_SECTION_HISTORY, &config.lle_enable_multiline_navigation,
     "Enable vertical cursor navigation in multiline", config_validate_bool,
     NULL},
    {"lle.wrap_history_navigation", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_wrap_history_navigation, "Wrap around at history boundaries",
     config_validate_bool, NULL},
    {"lle.save_line_on_history_nav", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_save_line_on_history_nav,
     "Save current line when navigating history", config_validate_bool, NULL},
    {"lle.preserve_multiline_structure", CONFIG_TYPE_BOOL,
     CONFIG_SECTION_HISTORY, &config.lle_preserve_multiline_structure,
     "Preserve multiline structure in history", config_validate_bool, NULL},
    {"lle.enable_multiline_editing", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_enable_multiline_editing,
     "Enable editing of recalled multiline commands", config_validate_bool,
     NULL},
    {"lle.show_multiline_indicators", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_show_multiline_indicators,
     "Show visual indicators for multiline", config_validate_bool, NULL},
    {"lle.enable_interactive_search", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_enable_interactive_search, "Enable Ctrl-R interactive search",
     config_validate_bool, NULL},
    {"lle.search_fuzzy_matching", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_search_fuzzy_matching, "Use fuzzy matching in search",
     config_validate_bool, NULL},
    {"lle.search_case_sensitive", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_search_case_sensitive, "Case sensitive history search",
     config_validate_bool, NULL},
    {"lle.storage_mode", CONFIG_TYPE_ENUM, CONFIG_SECTION_HISTORY,
     &config.lle_storage_mode, "History storage mode",
     config_validate_lle_storage_mode, &lle_storage_mode_enum},
    {"lle.history_file", CONFIG_TYPE_STRING, CONFIG_SECTION_HISTORY,
     &config.lle_history_file, "LLE history file path", config_validate_string,
     NULL},
    {"lle.sync_with_readline", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_sync_with_readline, "Sync LLE history with GNU Readline",
     config_validate_bool, NULL},
    {"lle.export_to_bash_history", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_export_to_bash_history, "Export to .bash_history format",
     config_validate_bool, NULL},
    {"lle.enable_forensic_tracking", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_enable_forensic_tracking,
     "Track metadata (timestamps, exit codes, cwd)", config_validate_bool,
     NULL},
    {"lle.enable_deduplication", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_enable_deduplication, "Enable history deduplication",
     config_validate_bool, NULL},
    {"lle.dedup_scope", CONFIG_TYPE_ENUM, CONFIG_SECTION_HISTORY,
     &config.lle_dedup_scope, "Deduplication scope",
     config_validate_lle_dedup_scope, &lle_dedup_scope_enum},
    {"lle.dedup_strategy", CONFIG_TYPE_ENUM, CONFIG_SECTION_HISTORY,
     &config.lle_dedup_strategy, "Deduplication strategy",
     config_validate_lle_dedup_strategy, &lle_dedup_strategy_enum},
    {"lle.dedup_navigation", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_dedup_navigation, "Skip duplicates during history navigation",
     config_validate_bool, NULL},
    {"lle.dedup_navigation_unique", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_dedup_navigation_unique,
     "Show only unique entries during navigation session", config_validate_bool,
     NULL},
    {"lle.dedup_unicode_normalize", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_dedup_unicode_normalize,
     "Use Unicode NFC normalization for dedup comparison", config_validate_bool,
     NULL},
    {"lle.enable_history_cache", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_enable_history_cache, "Enable history caching for performance",
     config_validate_bool, NULL},
    {"lle.cache_size", CONFIG_TYPE_INT, CONFIG_SECTION_HISTORY,
     &config.lle_cache_size, "History cache size", config_validate_int, NULL},
    {"lle.readline_compatible_mode", CONFIG_TYPE_BOOL, CONFIG_SECTION_HISTORY,
     &config.lle_readline_compatible_mode, "GNU Readline compatibility mode",
     config_validate_bool, NULL},

    // Completion settings
    {"completion.enabled", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.completion_enabled, "Enable tab completion", config_validate_bool,
     NULL},
    {"completion.fuzzy", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.fuzzy_completion, "Enable fuzzy matching in completion",
     config_validate_bool, NULL},
    {"completion.threshold", CONFIG_TYPE_INT, CONFIG_SECTION_COMPLETION,
     &config.completion_threshold, "Fuzzy matching threshold (0-100)",
     config_validate_int, NULL},
    {"completion.case_sensitive", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.completion_case_sensitive, "Case sensitive completion",
     config_validate_bool, NULL},
    {"completion.show_all", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.completion_show_all, "Show all completions", config_validate_bool,
     NULL},
    {"completion.hints", CONFIG_TYPE_BOOL, CONFIG_SECTION_COMPLETION,
     &config.hints_enabled, "Enable input hints", config_validate_bool, NULL},

    // Prompt settings
    {"prompt.use_theme", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.use_theme_prompt,
     "Use theme system for prompts (false = respect user PS1/PS2)",
     config_validate_bool, NULL},
    {"prompt.theme", CONFIG_TYPE_STRING, CONFIG_SECTION_PROMPT,
     &config.prompt_theme, "Prompt color theme", config_validate_string, NULL},
    {"prompt.git_enabled", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.git_prompt_enabled, "Enable git-aware prompts",
     config_validate_bool, NULL},
    {"prompt.git_cache_timeout", CONFIG_TYPE_INT, CONFIG_SECTION_PROMPT,
     &config.git_cache_timeout, "Git status cache timeout in seconds",
     config_validate_int, NULL},
    {"prompt.format", CONFIG_TYPE_STRING, CONFIG_SECTION_PROMPT,
     &config.prompt_format, "Custom prompt format string",
     config_validate_string, NULL},

    // Theme settings
    {"prompt.theme_name", CONFIG_TYPE_STRING, CONFIG_SECTION_PROMPT,
     &config.theme_name, "Active theme name", config_validate_string, NULL},
    {"prompt.theme_auto_detect_colors", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_auto_detect_colors, "Auto-detect terminal color support",
     config_validate_bool, NULL},
    {"prompt.theme_fallback_basic", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_fallback_basic, "Fallback to basic colors if needed",
     config_validate_bool, NULL},
    {"prompt.theme_corporate_company", CONFIG_TYPE_STRING,
     CONFIG_SECTION_PROMPT, &config.theme_corporate_company,
     "Corporate company name", config_validate_string, NULL},
    {"prompt.theme_corporate_department", CONFIG_TYPE_STRING,
     CONFIG_SECTION_PROMPT, &config.theme_corporate_department,
     "Corporate department name", config_validate_string, NULL},
    {"prompt.theme_corporate_project", CONFIG_TYPE_STRING,
     CONFIG_SECTION_PROMPT, &config.theme_corporate_project,
     "Corporate project name", config_validate_string, NULL},
    {"prompt.theme_corporate_environment", CONFIG_TYPE_STRING,
     CONFIG_SECTION_PROMPT, &config.theme_corporate_environment,
     "Corporate environment name", config_validate_string, NULL},
    {"prompt.theme_show_company", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_show_company, "Show company name in prompt",
     config_validate_bool, NULL},
    {"prompt.theme_show_department", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_show_department, "Show department name in prompt",
     config_validate_bool, NULL},
    {"prompt.theme_show_right_prompt", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_show_right_prompt, "Enable right-side prompt",
     config_validate_bool, NULL},

    {"prompt.theme_enable_animations", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_enable_animations, "Enable prompt animations",
     config_validate_bool, NULL},
    {"prompt.theme_enable_icons", CONFIG_TYPE_BOOL, CONFIG_SECTION_PROMPT,
     &config.theme_enable_icons, "Enable Unicode icons", config_validate_bool,
     NULL},
    {"prompt.theme_color_support_override", CONFIG_TYPE_INT,
     CONFIG_SECTION_PROMPT, &config.theme_color_support_override,
     "Override color support detection (0/8/256/16777216)", config_validate_int,
     NULL},

    // Behavior settings
    {"behavior.auto_cd", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.auto_cd, "Auto-cd to directories", config_validate_bool, NULL},
    {"behavior.spell_correction", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.spell_correction, "Enable command spell correction",
     config_validate_bool, NULL},
    {"behavior.autocorrect_max_suggestions", CONFIG_TYPE_INT,
     CONFIG_SECTION_BEHAVIOR, &config.autocorrect_max_suggestions,
     "Maximum auto-correction suggestions (1-5)", config_validate_int, NULL},
    {"behavior.autocorrect_threshold", CONFIG_TYPE_INT, CONFIG_SECTION_BEHAVIOR,
     &config.autocorrect_threshold,
     "Auto-correction similarity threshold (0-100)", config_validate_int, NULL},
    {"behavior.autocorrect_interactive", CONFIG_TYPE_BOOL,
     CONFIG_SECTION_BEHAVIOR, &config.autocorrect_interactive,
     "Show interactive correction prompts", config_validate_bool, NULL},
    {"behavior.autocorrect_learn_history", CONFIG_TYPE_BOOL,
     CONFIG_SECTION_BEHAVIOR, &config.autocorrect_learn_history,
     "Learn commands from history", config_validate_bool, NULL},
    {"behavior.autocorrect_builtins", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.autocorrect_builtins, "Suggest builtin corrections",
     config_validate_bool, NULL},
    {"behavior.autocorrect_external", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.autocorrect_external, "Suggest external command corrections",
     config_validate_bool, NULL},
    {"behavior.autocorrect_case_sensitive", CONFIG_TYPE_BOOL,
     CONFIG_SECTION_BEHAVIOR, &config.autocorrect_case_sensitive,
     "Case-sensitive auto-correction", config_validate_bool, NULL},
    {"behavior.confirm_exit", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.confirm_exit, "Confirm before exiting", config_validate_bool,
     NULL},
    {"behavior.tab_width", CONFIG_TYPE_INT, CONFIG_SECTION_BEHAVIOR,
     &config.tab_width, "Tab width for display", config_validate_int, NULL},
    {"behavior.no_word_expand", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.no_word_expand, "Disable word expansion and globbing",
     config_validate_bool, NULL},
    {"behavior.multiline_mode", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.multiline_mode, "Enable multiline editing mode",
     config_validate_bool, NULL},

    // Color settings
    {"behavior.color_scheme", CONFIG_TYPE_STRING, CONFIG_SECTION_BEHAVIOR,
     &config.color_scheme, "Color scheme name", config_validate_color_scheme,
     NULL},
    {"behavior.colors_enabled", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.colors_enabled, "Enable color output", config_validate_bool, NULL},

    // Advanced settings
    {"behavior.verbose_errors", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.verbose_errors, "Show verbose error messages",
     config_validate_bool, NULL},
    {"behavior.debug_mode", CONFIG_TYPE_BOOL, CONFIG_SECTION_BEHAVIOR,
     &config.debug_mode, "Enable debug mode", config_validate_bool, NULL},

    // Network settings
    {"network.ssh_completion", CONFIG_TYPE_BOOL, CONFIG_SECTION_NETWORK,
     &config.ssh_completion_enabled, "Enable SSH host completion",
     config_validate_bool, NULL},
    {"network.cloud_discovery", CONFIG_TYPE_BOOL, CONFIG_SECTION_NETWORK,
     &config.cloud_discovery_enabled, "Enable cloud host discovery",
     config_validate_bool, NULL},
    {"network.cache_ssh_hosts", CONFIG_TYPE_BOOL, CONFIG_SECTION_NETWORK,
     &config.cache_ssh_hosts, "Cache SSH hosts for performance",
     config_validate_bool, NULL},
    {"network.cache_timeout_minutes", CONFIG_TYPE_INT, CONFIG_SECTION_NETWORK,
     &config.cache_timeout_minutes, "SSH host cache timeout in minutes",
     config_validate_int, NULL},
    {"network.show_remote_context", CONFIG_TYPE_BOOL, CONFIG_SECTION_NETWORK,
     &config.show_remote_context, "Show remote context in prompt",
     config_validate_bool, NULL},
    {"network.auto_detect_cloud", CONFIG_TYPE_BOOL, CONFIG_SECTION_NETWORK,
     &config.auto_detect_cloud, "Auto-detect cloud environment",
     config_validate_bool, NULL},
    {"network.max_completion_hosts", CONFIG_TYPE_INT, CONFIG_SECTION_NETWORK,
     &config.max_completion_hosts, "Maximum hosts to show in completion",
     config_validate_int, NULL},

    // Display system settings
    // v1.3.0: Layered display is now the exclusive system - no configuration
    // needed
    // display.system_mode and display.layered_display options removed
    {"display.syntax_highlighting", CONFIG_TYPE_BOOL, CONFIG_SECTION_DISPLAY,
     &config.display_syntax_highlighting, "Enable syntax highlighting",
     config_validate_bool, NULL},
    {"display.autosuggestions", CONFIG_TYPE_BOOL, CONFIG_SECTION_DISPLAY,
     &config.display_autosuggestions, "Enable Fish-style autosuggestions",
     config_validate_bool, NULL},
    {"display.transient_prompt", CONFIG_TYPE_BOOL, CONFIG_SECTION_DISPLAY,
     &config.display_transient_prompt,
     "Enable transient prompts (simplify previous prompts in scrollback)",
     config_validate_bool, NULL},
    {"display.theme_hot_reload", CONFIG_TYPE_BOOL, CONFIG_SECTION_DISPLAY,
     &config.display_theme_hot_reload,
     "Auto-reload theme when its file changes on disk",
     config_validate_bool, NULL},
    {"display.performance_monitoring", CONFIG_TYPE_BOOL, CONFIG_SECTION_DISPLAY,
     &config.display_performance_monitoring,
     "Enable display performance monitoring", config_validate_bool, NULL},
    {"display.optimization_level", CONFIG_TYPE_INT, CONFIG_SECTION_DISPLAY,
     &config.display_optimization_level, "Display optimization level (0-4)",
     config_validate_optimization_level, NULL},

    // v1.3.0: Legacy enhanced display mode option removed
    // behavior.enhanced_display_mode option removed

    // Script execution control
    {"scripts.execution", CONFIG_TYPE_BOOL, CONFIG_SECTION_SCRIPTS,
     &config.script_execution, "Enable script execution", config_validate_bool,
     NULL},

    // Shell options integration - all 24 POSIX options with shell.* namespace
    // These map directly to existing shell_opts flags for perfect compatibility
    {"shell.errexit", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Exit on command failure (set -e)", config_validate_shell_option, NULL},
    {"shell.xtrace", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Trace command execution (set -x)", config_validate_shell_option, NULL},
    {"shell.noexec", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Syntax check only (set -n)", config_validate_shell_option, NULL},
    {"shell.nounset", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Error on unset variables (set -u)", config_validate_shell_option, NULL},
    {"shell.verbose", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Print input lines (set -v)", config_validate_shell_option, NULL},
    {"shell.noglob", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Disable pathname expansion (set -f)", config_validate_shell_option, NULL},
    {"shell.hashall", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Command hashing (set -h)", config_validate_shell_option, NULL},
    {"shell.monitor", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Job control (set -m)", config_validate_shell_option, NULL},
    {"shell.allexport", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Auto export variables (set -a)", config_validate_shell_option, NULL},
    {"shell.noclobber", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Prevent file overwrite (set -C)", config_validate_shell_option, NULL},
    {"shell.onecmd", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Exit after one command (set -t)", config_validate_shell_option, NULL},
    {"shell.notify", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Async job notification (set -b)", config_validate_shell_option, NULL},
    {"shell.ignoreeof", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Prevent exit on EOF (set -o ignoreeof)", config_validate_shell_option,
     NULL},
    {"shell.nolog", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Prevent function history logging (set -o nolog)",
     config_validate_shell_option, NULL},
    {"shell.emacs", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Emacs-style editing (set -o emacs)", config_validate_shell_option, NULL},
    {"shell.vi", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Vi-style editing (set -o vi)", config_validate_shell_option, NULL},
    {"shell.posix", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Strict POSIX compliance (set -o posix)", config_validate_shell_option,
     NULL},
    {"shell.pipefail", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Pipeline failure detection (set -o pipefail)",
     config_validate_shell_option, NULL},
    {"shell.histexpand", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "History expansion (set -o histexpand)", config_validate_shell_option,
     NULL},
    {"shell.history", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Command history recording (set -o history)", config_validate_shell_option,
     NULL},
    {"shell.interactive-comments", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Interactive comments (set -o interactive-comments)",
     config_validate_shell_option, NULL},
    {"shell.physical", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Physical directory paths (set -o physical)", config_validate_shell_option,
     NULL},
    {"shell.privileged", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL, NULL,
     "Restricted shell security (set -o privileged)",
     config_validate_shell_option, NULL},

    // Shell mode settings (Phase 0: Extended Language Support)
    {"shell.mode", CONFIG_TYPE_ENUM, CONFIG_SECTION_SHELL, &config.shell_mode,
     "Shell compatibility mode (posix, bash, zsh, lush)",
     config_validate_shell_mode, &shell_mode_enum},
    {"shell.mode_strict", CONFIG_TYPE_BOOL, CONFIG_SECTION_SHELL,
     &config.shell_mode_strict, "Disallow runtime mode changes",
     config_validate_bool, NULL},
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

/* Forward declarations for sync hooks */
static void history_sync_to_runtime(void);
static void history_sync_from_runtime(void);
static void shell_sync_to_runtime(void);
static void shell_sync_from_runtime(void);
static void display_sync_to_runtime(void);
static void display_sync_from_runtime(void);
static void completion_sync_to_runtime(void);
static void completion_sync_from_runtime(void);
static void behavior_sync_to_runtime(void);
static void behavior_sync_from_runtime(void);

/* ----------------------------------------------------------------------------
 * History Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t history_options[] = {
    {"enabled", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable command history", true},
    {"size", CREG_VALUE_INTEGER,
     {.type = CREG_VALUE_INTEGER, .data.integer = 1000},
     "Maximum history entries", true},
    {"no_dups", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Remove duplicate entries", true},
    {"timestamps", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Record timestamps", true},
};

static const creg_section_t history_section = {
    .name = "history",
    .options = history_options,
    .option_count = sizeof(history_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = history_sync_to_runtime,
    .sync_from_runtime = history_sync_from_runtime,
};

/* ----------------------------------------------------------------------------
 * Shell Section Options (POSIX options + mode)
 * -------------------------------------------------------------------------- */
static const creg_option_t shell_options[] = {
    {"mode", CREG_VALUE_STRING,
     {.type = CREG_VALUE_STRING, .data.string = "lush"},
     "Shell compatibility mode", true},
    {"errexit", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Exit on command failure (set -e)", true},
    {"nounset", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Error on unset variables (set -u)", true},
    {"xtrace", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Trace execution (set -x)", true},
    {"pipefail", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Pipeline failure detection", true},
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
    {"syntax_highlighting", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable syntax highlighting", true},
    {"autosuggestions", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable Fish-style autosuggestions", true},
    {"transient_prompt", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Enable transient prompts", true},
    {"optimization_level", CREG_VALUE_INTEGER,
     {.type = CREG_VALUE_INTEGER, .data.integer = 2},
     "Display optimization level (0-4)", true},
};

static const creg_section_t display_section = {
    .name = "display",
    .options = display_options,
    .option_count = sizeof(display_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = display_sync_to_runtime,
    .sync_from_runtime = display_sync_from_runtime,
};

/* ----------------------------------------------------------------------------
 * Completion Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t completion_options[] = {
    {"enabled", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable tab completion", true},
    {"fuzzy", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable fuzzy matching", true},
    {"case_sensitive", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Case-sensitive completion", true},
};

static const creg_section_t completion_section = {
    .name = "completion",
    .options = completion_options,
    .option_count = sizeof(completion_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = completion_sync_to_runtime,
    .sync_from_runtime = completion_sync_from_runtime,
};

/* ----------------------------------------------------------------------------
 * Behavior Section Options
 * -------------------------------------------------------------------------- */
static const creg_option_t behavior_options[] = {
    {"auto_cd", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Auto-cd to directories", true},
    {"spell_correction", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = true},
     "Enable spell correction", true},
    {"confirm_exit", CREG_VALUE_BOOLEAN,
     {.type = CREG_VALUE_BOOLEAN, .data.boolean = false},
     "Confirm before exit", true},
};

static const creg_section_t behavior_section = {
    .name = "behavior",
    .options = behavior_options,
    .option_count = sizeof(behavior_options) / sizeof(creg_option_t),
    .on_load = NULL,
    .on_save = NULL,
    .sync_to_runtime = behavior_sync_to_runtime,
    .sync_from_runtime = behavior_sync_from_runtime,
};

/* ----------------------------------------------------------------------------
 * Sync Hook Implementations
 * -------------------------------------------------------------------------- */

/**
 * @brief Sync history config from registry to runtime
 */
static void history_sync_to_runtime(void) {
    bool bval;
    int64_t ival;

    if (config_registry_get_boolean("history.enabled", &bval) == CREG_SUCCESS) {
        config.history_enabled = bval;
    }
    if (config_registry_get_integer("history.size", &ival) == CREG_SUCCESS) {
        config.history_size = (int)ival;
    }
    if (config_registry_get_boolean("history.no_dups", &bval) == CREG_SUCCESS) {
        config.history_no_dups = bval;
    }
    if (config_registry_get_boolean("history.timestamps", &bval) == CREG_SUCCESS) {
        config.history_timestamps = bval;
    }
}

/**
 * @brief Sync history config from runtime to registry
 */
static void history_sync_from_runtime(void) {
    config_registry_set_boolean("history.enabled", config.history_enabled);
    config_registry_set_integer("history.size", config.history_size);
    config_registry_set_boolean("history.no_dups", config.history_no_dups);
    config_registry_set_boolean("history.timestamps", config.history_timestamps);
}

/**
 * @brief Sync shell options from registry to runtime
 */
static void shell_sync_to_runtime(void) {
    bool bval;
    char sval[CREG_VALUE_STRING_MAX];

    if (config_registry_get_string("shell.mode", sval, sizeof(sval)) == CREG_SUCCESS) {
        /* Map mode string to enum - use Unicode comparison */
        if (lle_unicode_strings_equal(sval, "posix", &LLE_UNICODE_COMPARE_DEFAULT)) {
            config.shell_mode = SHELL_MODE_POSIX;
        } else if (lle_unicode_strings_equal(sval, "bash", &LLE_UNICODE_COMPARE_DEFAULT)) {
            config.shell_mode = SHELL_MODE_BASH;
        } else if (lle_unicode_strings_equal(sval, "zsh", &LLE_UNICODE_COMPARE_DEFAULT)) {
            config.shell_mode = SHELL_MODE_ZSH;
        } else {
            config.shell_mode = SHELL_MODE_LUSH;
        }
    }

    /* POSIX options - sync to shell_opts via config_set_shell_option */
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

/**
 * @brief Sync shell options from runtime to registry
 */
static void shell_sync_from_runtime(void) {
    /* Map mode enum to string */
    const char *mode_str = "lush";
    switch (config.shell_mode) {
    case SHELL_MODE_POSIX: mode_str = "posix"; break;
    case SHELL_MODE_BASH: mode_str = "bash"; break;
    case SHELL_MODE_ZSH: mode_str = "zsh"; break;
    case SHELL_MODE_LUSH: mode_str = "lush"; break;
    }
    config_registry_set_string("shell.mode", mode_str);

    /* POSIX options - read from shell_opts via config_get_shell_option */
    config_registry_set_boolean("shell.errexit", config_get_shell_option("errexit"));
    config_registry_set_boolean("shell.nounset", config_get_shell_option("nounset"));
    config_registry_set_boolean("shell.xtrace", config_get_shell_option("xtrace"));
    config_registry_set_boolean("shell.pipefail", config_get_shell_option("pipefail"));
}

/**
 * @brief Sync display config from registry to runtime
 */
static void display_sync_to_runtime(void) {
    bool bval;
    int64_t ival;

    if (config_registry_get_boolean("display.syntax_highlighting", &bval) == CREG_SUCCESS) {
        config.display_syntax_highlighting = bval;
    }
    if (config_registry_get_boolean("display.autosuggestions", &bval) == CREG_SUCCESS) {
        config.display_autosuggestions = bval;
    }
    if (config_registry_get_boolean("display.transient_prompt", &bval) == CREG_SUCCESS) {
        config.display_transient_prompt = bval;
    }
    if (config_registry_get_boolean("display.theme_hot_reload", &bval) == CREG_SUCCESS) {
        config.display_theme_hot_reload = bval;
    }
    if (config_registry_get_integer("display.optimization_level", &ival) == CREG_SUCCESS) {
        config.display_optimization_level = (int)ival;
    }
}

/**
 * @brief Sync display config from runtime to registry
 */
static void display_sync_from_runtime(void) {
    config_registry_set_boolean("display.syntax_highlighting", config.display_syntax_highlighting);
    config_registry_set_boolean("display.autosuggestions", config.display_autosuggestions);
    config_registry_set_boolean("display.transient_prompt", config.display_transient_prompt);
    config_registry_set_boolean("display.theme_hot_reload", config.display_theme_hot_reload);
    config_registry_set_integer("display.optimization_level", config.display_optimization_level);
}

/**
 * @brief Sync completion config from registry to runtime
 */
static void completion_sync_to_runtime(void) {
    bool bval;

    if (config_registry_get_boolean("completion.enabled", &bval) == CREG_SUCCESS) {
        config.completion_enabled = bval;
    }
    if (config_registry_get_boolean("completion.fuzzy", &bval) == CREG_SUCCESS) {
        config.fuzzy_completion = bval;
    }
    if (config_registry_get_boolean("completion.case_sensitive", &bval) == CREG_SUCCESS) {
        config.completion_case_sensitive = bval;
    }
}

/**
 * @brief Sync completion config from runtime to registry
 */
static void completion_sync_from_runtime(void) {
    config_registry_set_boolean("completion.enabled", config.completion_enabled);
    config_registry_set_boolean("completion.fuzzy", config.fuzzy_completion);
    config_registry_set_boolean("completion.case_sensitive", config.completion_case_sensitive);
}

/**
 * @brief Sync behavior config from registry to runtime
 */
static void behavior_sync_to_runtime(void) {
    bool bval;

    if (config_registry_get_boolean("behavior.auto_cd", &bval) == CREG_SUCCESS) {
        config.auto_cd = bval;
    }
    if (config_registry_get_boolean("behavior.spell_correction", &bval) == CREG_SUCCESS) {
        config.spell_correction = bval;
    }
    if (config_registry_get_boolean("behavior.confirm_exit", &bval) == CREG_SUCCESS) {
        config.confirm_exit = bval;
    }
}

/**
 * @brief Sync behavior config from runtime to registry
 */
static void behavior_sync_from_runtime(void) {
    config_registry_set_boolean("behavior.auto_cd", config.auto_cd);
    config_registry_set_boolean("behavior.spell_correction", config.spell_correction);
    config_registry_set_boolean("behavior.confirm_exit", config.confirm_exit);
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

    // Legacy editor.use_lle option - LLE is now the only line editor
    if (strcmp(key, "editor.use_lle") == 0) {
        // Silently ignore - LLE is always enabled
        return true;
    }

    // Legacy display configuration keys
    if (strcmp(key, "behavior.enhanced_display_mode") == 0) {
        // Legacy key - replaced by layered display system
        // Silently ignore to maintain compatibility with existing .lushrc
        // files
        return true;
    }

    // Handle legacy display.* configuration keys
    if (strncmp(key, "display.", 8) == 0) {
        // Unknown display.* keys - likely legacy, handle gracefully
        const char *display_key = key + 8; // Skip "display." prefix

        if (strcmp(display_key, "system_mode") == 0 ||
            strcmp(display_key, "layered_display") == 0) {
            // Legacy display mode options - layered display is now exclusive
            // Silently ignore to maintain compatibility with existing .lushrc
            // files
            return true;
        }

        // Valid current display keys - let normal processing handle them
        if (strcmp(display_key, "syntax_highlighting") == 0 ||
            strcmp(display_key, "autosuggestions") == 0 ||
            strcmp(display_key, "transient_prompt") == 0 ||
            strcmp(display_key, "performance_monitoring") == 0 ||
            strcmp(display_key, "optimization_level") == 0) {
            return false;
        }

        // Other display.* keys are likely legacy - handle gracefully
        return true;
    }

    return false;
}

// Legacy option name mapping for backward compatibility
typedef struct {
    const char *old_name;
    const char *new_name;
} legacy_option_mapping_t;

static legacy_option_mapping_t legacy_mappings[] = {
    // History options
    {"history_enabled", "history.enabled"},
    {"history_size", "history.size"},
    {"history_no_dups", "history.no_dups"},
    {"history_timestamps", "history.timestamps"},
    {"history_file", "history.file"},

    // Completion options
    {"completion_enabled", "completion.enabled"},
    {"fuzzy_completion", "completion.fuzzy"},
    {"completion_threshold", "completion.threshold"},
    {"completion_case_sensitive", "completion.case_sensitive"},
    {"completion_show_all", "completion.show_all"},
    {"hints_enabled", "completion.hints"},

    // Prompt options
    {"prompt_theme", "prompt.theme"},
    {"git_prompt_enabled", "prompt.git_enabled"},
    {"git_cache_timeout", "prompt.git_cache_timeout"},
    {"prompt_format", "prompt.format"},
    {"theme_name", "prompt.theme_name"},
    {"theme_auto_detect_colors", "prompt.theme_auto_detect_colors"},
    {"theme_fallback_basic", "prompt.theme_fallback_basic"},
    {"theme_corporate_company", "prompt.theme_corporate_company"},
    {"theme_corporate_department", "prompt.theme_corporate_department"},
    {"theme_corporate_project", "prompt.theme_corporate_project"},
    {"theme_corporate_environment", "prompt.theme_corporate_environment"},
    {"theme_show_company", "prompt.theme_show_company"},
    {"theme_show_department", "prompt.theme_show_department"},
    {"theme_show_right_prompt", "prompt.theme_show_right_prompt"},
    {"theme_enable_animations", "prompt.theme_enable_animations"},
    {"theme_enable_icons", "prompt.theme_enable_icons"},
    {"theme_color_support_override", "prompt.theme_color_support_override"},

    // Behavior options
    {"auto_cd", "behavior.auto_cd"},
    {"spell_correction", "behavior.spell_correction"},
    {"autocorrect_max_suggestions", "behavior.autocorrect_max_suggestions"},
    {"autocorrect_threshold", "behavior.autocorrect_threshold"},
    {"autocorrect_interactive", "behavior.autocorrect_interactive"},
    {"autocorrect_learn_history", "behavior.autocorrect_learn_history"},
    {"autocorrect_builtins", "behavior.autocorrect_builtins"},
    {"autocorrect_external", "behavior.autocorrect_external"},
    {"autocorrect_case_sensitive", "behavior.autocorrect_case_sensitive"},
    {"confirm_exit", "behavior.confirm_exit"},
    {"tab_width", "behavior.tab_width"},
    {"no_word_expand", "behavior.no_word_expand"},
    {"multiline_mode", "behavior.multiline_mode"},
    {"color_scheme", "behavior.color_scheme"},
    {"colors_enabled", "behavior.colors_enabled"},
    {"verbose_errors", "behavior.verbose_errors"},
    {"debug_mode", "behavior.debug_mode"},
    {"display_performance", "display.performance_monitoring"},
    {"display_optimization", "display.optimization_level"},

    // Network options
    {"ssh_completion_enabled", "network.ssh_completion"},
    {"cloud_discovery_enabled", "network.cloud_discovery"},
    {"cache_ssh_hosts", "network.cache_ssh_hosts"},
    {"cache_timeout_minutes", "network.cache_timeout_minutes"},
    {"show_remote_context", "network.show_remote_context"},
    {"auto_detect_cloud", "network.auto_detect_cloud"},
    {"max_completion_hosts", "network.max_completion_hosts"},

    // Scripts options
    {"script_execution", "scripts.execution"},

    {NULL, NULL}};

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
    // Remove "shell." prefix to get the actual option name
    const char *opt_name = option_name + 6; // Skip "shell."

    // Map to existing shell option flags using the same names as builtin_set
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
            shell_opts.vi_mode = false; // Mutually exclusive
            lush_update_editing_mode();
        }
    } else if (strcmp(opt_name, "vi") == 0) {
        shell_opts.vi_mode = value;
        if (value) {
            shell_opts.emacs_mode = false; // Mutually exclusive
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
    // Remove "shell." prefix to get the actual option name
    const char *opt_name = option_name + 6; // Skip "shell."

    // Use existing shell option flags - same logic as builtin_set status display
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

    return false; // Unknown option
}

// Traditional shell script file paths
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
 * @brief Get the path to the login script file
 *
 * Returns the full path to ~/.lush_login.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_login_script_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }

    char *path = malloc(strlen(home) + strlen(LOGIN_SCRIPT_FILE) + 2);
    if (!path) {
        return NULL;
    }

    sprintf(path, "%s/%s", home, LOGIN_SCRIPT_FILE);
    return path;
}

/**
 * @brief Get the path to the RC script file
 *
 * Returns the full path to ~/.lushrc.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_rc_script_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }

    char *path = malloc(strlen(home) + strlen(RC_SCRIPT_FILE) + 2);
    if (!path) {
        return NULL;
    }

    sprintf(path, "%s/%s", home, RC_SCRIPT_FILE);
    return path;
}

/**
 * @brief Get the path to the logout script file
 *
 * Returns the full path to ~/.lush_logout.
 *
 * @return Allocated path string, or NULL on failure (caller must free)
 */
char *config_get_logout_script_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }

    char *path = malloc(strlen(home) + strlen(LOGOUT_SCRIPT_FILE) + 2);
    if (!path) {
        return NULL;
    }

    sprintf(path, "%s/%s", home, LOGOUT_SCRIPT_FILE);
    return path;
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

    // Use the same approach as bin_source builtin - read complete multi-line
    // constructs instead of line by line to handle if/then/fi, while/do/done, etc.
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    // Track source depth so 'return' builtin works correctly in sourced scripts
    // Use get_global_executor() since parse_and_execute uses global_executor
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
        // Set script context for proper error reporting with file path
        executor_set_script_context(executor, path, 1);
    }

    char *complete_input;
    int result = 0;
    int construct_number = 1;

    // Read complete multi-line constructs (same as bin_source)
    while ((complete_input = get_input_complete(file)) != NULL) {
        // Skip empty constructs
        char *trimmed = complete_input;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;
        if (*trimmed == '\0') {
            free(complete_input);
            construct_number++;
            continue;
        }

        // Update script context line number for debugging
        if (executor) {
            executor_set_script_context(executor, path, construct_number);
        }

        // Parse and execute the complete construct
        int construct_result = parse_and_execute(complete_input);

        // Check for return from sourced script (exit code 200+)
        // This matches how bin_source handles the special return code
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
        construct_number++;

        // Check if 'return' was called in the sourced script
        if (executor && executor->source_return) {
            break;
        }
    }

    // Restore source depth, source_return state, and script context
    if (executor) {
        executor->source_depth--;
        executor->source_return = saved_source_return;
        executor_set_script_context(executor, saved_script_file, 1);
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
 * This should be called BEFORE user profile scripts (~/.profile, ~/.lush_login).
 *
 * @return 0 on success, -1 if any script fails (non-fatal, continues execution)
 */
int config_execute_system_profile(void) {
    if (!config_should_execute_scripts()) {
        return 0;
    }

    int result = 0;

    // 1. Source /etc/lushrc if it exists (lush-specific system config)
    // This runs in native lush mode since it's a lush-specific file
    if (config_script_exists("/etc/lushrc")) {
        if (config_execute_script_file("/etc/lushrc") != 0) {
            // Log warning but continue - system config failure shouldn't block login
            fprintf(stderr, "lush: warning: error sourcing /etc/lushrc\n");
            result = -1;
        }
    }

    // 2. Source /etc/profile if it exists (standard POSIX login config)
    // Switch to bash mode for compatibility with system scripts
    // Most /etc/profile.d scripts use bash extensions like [[ ]] and $-
    if (config_script_exists("/etc/profile")) {
        shell_mode_t saved_mode = shell_mode_get();
        shell_mode_set(SHELL_MODE_BASH);
        int script_result = config_execute_script_file("/etc/profile");
        shell_mode_set(saved_mode);
        if (script_result != 0) {
            fprintf(stderr, "lush: warning: error sourcing /etc/profile\n");
            result = -1;
        }
    }

    // Note: /etc/profile.d/*.sh files are NOT sourced directly here because
    // /etc/profile already sources them via its internal loop. Sourcing them
    // again would cause double-execution and potential issues with scripts
    // that check for conditions like interactive mode or TTY status.

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

    // Execute .lushrc if it exists (lush-specific RC script)
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

    // Execute .profile if it exists (POSIX standard)
    // Run in bash mode for better compatibility with user scripts
    // that may use bash extensions
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

    // Execute .lush_login if it exists (lush-specific login script)
    // This runs in native lush mode since it's a lush-specific file
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

    // Execute .lush_logout if it exists
    char *logout_path = config_get_logout_script_path();
    if (logout_path && config_script_exists(logout_path)) {
        if (config_execute_script_file(logout_path) != 0) {
            result = -1;
        }
    }
    free(logout_path);

    return result;
}

// Configuration file template - uses dotted notation for all options
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
    "# Enable vertical cursor navigation in multiline mode\n"
    "lle.enable_multiline_navigation = true\n"
    "\n"
    "# Wrap around at history boundaries\n"
    "lle.wrap_history_navigation = false\n"
    "\n"
    "# Save current line when navigating history\n"
    "lle.save_line_on_history_nav = true\n"
    "\n"
    "# Preserve multiline structure in history\n"
    "lle.preserve_multiline_structure = true\n"
    "\n"
    "# Enable editing of recalled multiline commands\n"
    "lle.enable_multiline_editing = true\n"
    "\n"
    "# Show visual indicators for multiline input\n"
    "lle.show_multiline_indicators = true\n"
    "\n"
    "# Enable Ctrl-R interactive history search\n"
    "lle.enable_interactive_search = true\n"
    "\n"
    "# Use fuzzy matching in history search\n"
    "lle.search_fuzzy_matching = false\n"
    "\n"
    "# Case sensitive history search\n"
    "lle.search_case_sensitive = false\n"
    "\n"
    "# History storage mode:\n"
    "#   lle-only       - Store only in LLE format\n"
    "#   bash-only      - Store only in bash format\n"
    "#   dual           - Store in both formats\n"
    "#   readline-compat - GNU Readline compatible mode\n"
    "lle.storage_mode = dual\n"
    "\n"
    "# LLE history file path (default: ~/.lush_history)\n"
    "# lle.history_file = ~/.lush_history\n"
    "\n"
    "# Sync LLE history with GNU Readline\n"
    "lle.sync_with_readline = true\n"
    "\n"
    "# Export to .bash_history format\n"
    "lle.export_to_bash_history = true\n"
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
    "# GNU Readline compatibility mode\n"
    "lle.readline_compatible_mode = false\n"
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
    "# Enable fuzzy matching in completion\n"
    "completion.fuzzy = true\n"
    "\n"
    "# Fuzzy matching threshold (0-100)\n"
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
    "# PROMPT SETTINGS\n"
    "# "
    "=========================================================================="
    "==\n"
    "\n"
    "# Prompt color theme\n"
    "prompt.theme = default\n"
    "\n"
    "# Enable git-aware prompts\n"
    "prompt.git_enabled = true\n"
    "\n"
    "# Git status cache timeout in seconds\n"
    "prompt.git_cache_timeout = 5\n"
    "\n"
    "# Custom prompt format string\n"
    "# Format codes: %u=user, %h=host, %d=directory, %g=git info\n"
    "# prompt.format = \"%u@%h in %d%g %% \"\n"
    "\n"
    "# Active theme name\n"
    "prompt.theme_name = corporate\n"
    "\n"
    "# Auto-detect terminal color support\n"
    "prompt.theme_auto_detect_colors = true\n"
    "\n"
    "# Fallback to basic colors if needed\n"
    "prompt.theme_fallback_basic = true\n"
    "\n"
    "# Corporate theme settings\n"
    "# prompt.theme_corporate_company = \"\"\n"
    "# prompt.theme_corporate_department = \"\"\n"
    "# prompt.theme_corporate_project = \"\"\n"
    "# prompt.theme_corporate_environment = \"\"\n"
    "\n"
    "# Show company name in prompt\n"
    "prompt.theme_show_company = false\n"
    "\n"
    "# Show department name in prompt\n"
    "prompt.theme_show_department = false\n"
    "\n"
    "# Enable right-side prompt\n"
    "prompt.theme_show_right_prompt = true\n"
    "\n"
    "# Enable prompt animations\n"
    "prompt.theme_enable_animations = false\n"
    "\n"
    "# Enable Unicode icons\n"
    "prompt.theme_enable_icons = false\n"
    "\n"
    "# Override color support detection (0/8/256/16777216)\n"
    "prompt.theme_color_support_override = 0\n"
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
    "# Disable word expansion and globbing\n"
    "behavior.no_word_expand = false\n"
    "\n"
    "# Enable multiline editing mode\n"
    "behavior.multiline_mode = true\n"
    "\n"
    "# Color scheme name: default, dark, light, solarized\n"
    "behavior.color_scheme = default\n"
    "\n"
    "# Enable color output\n"
    "behavior.colors_enabled = true\n"
    "\n"
    "# Show verbose error messages\n"
    "behavior.verbose_errors = false\n"
    "\n"
    "# Enable debug mode\n"
    "behavior.debug_mode = false\n"
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
    "# Enable cloud host discovery\n"
    "network.cloud_discovery = false\n"
    "\n"
    "# Cache SSH hosts for performance\n"
    "network.cache_ssh_hosts = true\n"
    "\n"
    "# SSH host cache timeout in minutes\n"
    "network.cache_timeout_minutes = 5\n"
    "\n"
    "# Show remote context in prompt\n"
    "network.show_remote_context = true\n"
    "\n"
    "# Auto-detect cloud environment\n"
    "network.auto_detect_cloud = true\n"
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
    // Set default values
    config_set_defaults();

    // Initialize context
    memset(&config_ctx, 0, sizeof(config_ctx));

    // Initialize config registry and register all sections
    config_register_sections();

    // Get XDG directory path
    char xdg_dir[CONFIG_PATH_MAX];
    if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) == 0) {
        config_ctx.xdg_config_dir = strdup(xdg_dir);
    }

    // Check for XDG config file
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

    // Determine which config to load and set format
    if (xdg_exists) {
        // Prefer XDG config
        config_ctx.user_config_path = strdup(xdg_path);
        config_ctx.format = CONFIG_FORMAT_TOML;
        config_ctx.needs_migration = false;
    } else if (legacy_exists) {
        // Use legacy config, mark for migration
        config_ctx.user_config_path = strdup(legacy_path);
        config_ctx.format = CONFIG_FORMAT_LEGACY;
        config_ctx.needs_migration = true;
    } else {
        // No config exists - use XDG path for new config
        config_ctx.user_config_path = strdup(xdg_path);
        config_ctx.format = CONFIG_FORMAT_TOML;
        config_ctx.needs_migration = false;
    }

    config_ctx.system_config_path = config_get_system_config_path();

    // Check if config files exist
    config_ctx.user_config_exists =
        (config_ctx.user_config_path &&
         stat(config_ctx.user_config_path, &st) == 0);
    config_ctx.system_config_exists =
        (config_ctx.system_config_path &&
         stat(config_ctx.system_config_path, &st) == 0);

    // Load system config first, then user config (ignore errors)
    if (config_ctx.system_config_exists) {
        config_load_system();
    }

    if (config_ctx.user_config_exists) {
        config_load_user();

        // Print migration notice if loading legacy config
        if (config_ctx.needs_migration) {
            fprintf(stderr,
                    "lush: Loading configuration from %s (legacy location)\n",
                    config_ctx.user_config_path);
            fprintf(stderr,
                    "lush: Run 'config save' to migrate to %s\n",
                    xdg_path);
        }
    }

    // Apply loaded settings (always safe to call with defaults)
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
    // History defaults
    config.history_enabled = true;
    config.history_size = 1000;
    config.history_no_dups = true;
    config.history_timestamps = false;
    config.history_file = NULL;

    // LLE History defaults
    config.lle_arrow_key_mode = LLE_ARROW_MODE_CONTEXT_AWARE;
    config.lle_enable_multiline_navigation = true;
    config.lle_wrap_history_navigation = false;
    config.lle_save_line_on_history_nav = true;
    config.lle_preserve_multiline_structure = true;
    config.lle_enable_multiline_editing = true;
    config.lle_show_multiline_indicators = true;
    config.lle_enable_interactive_search = true;
    config.lle_search_fuzzy_matching = false;
    config.lle_search_case_sensitive = false;
    config.lle_storage_mode = LLE_STORAGE_MODE_DUAL;
    config.lle_history_file = NULL; // Will default to ~/.lush_history
    config.lle_sync_with_readline = true;
    config.lle_export_to_bash_history = true;
    config.lle_enable_forensic_tracking = true;
    config.lle_enable_deduplication = true;
    config.lle_dedup_scope = LLE_DEDUP_SCOPE_SESSION;
    config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_KEEP_RECENT;
    config.lle_dedup_navigation =
        true; // Skip duplicates when navigating history
    config.lle_dedup_navigation_unique =
        true; // Show only unique entries during navigation session
    config.lle_dedup_unicode_normalize =
        true; // Use Unicode NFC normalization for comparison
    config.lle_enable_history_cache = true;
    config.lle_cache_size = 100;
    config.lle_readline_compatible_mode = false;

    // Completion defaults
    config.completion_enabled = true;
    config.fuzzy_completion = true;
    config.completion_threshold = 60;
    config.completion_case_sensitive = false;
    config.completion_show_all = false;
    config.hints_enabled = false;

    // Prompt defaults
    config.use_theme_prompt =
        true; // Default: use theme system (backwards compatible)
    config.prompt_theme = strdup("default");
    config.git_prompt_enabled = true;
    config.git_cache_timeout = 5;
    config.prompt_format = NULL;

    // Theme defaults
    config.theme_name = strdup("corporate");
    config.theme_auto_detect_colors = true;
    config.theme_fallback_basic = true;
    config.theme_corporate_company = NULL;
    config.theme_corporate_department = NULL;
    config.theme_corporate_project = NULL;
    config.theme_corporate_environment = NULL;
    config.theme_show_company = false;
    config.theme_show_department = false;
    config.theme_show_right_prompt = true;

    config.theme_enable_animations = false;
    config.theme_enable_icons = false;
    config.theme_color_support_override = 0;

    // Behavior defaults
    config.auto_cd = false;
    config.spell_correction = true;
    config.confirm_exit = false;
    config.tab_width = 4;
    config.no_word_expand = false;
    config.multiline_mode = true;

    // Auto-correction defaults
    config.autocorrect_max_suggestions = 3;
    config.autocorrect_threshold = 40;
    config.autocorrect_interactive = true;
    config.autocorrect_learn_history = true;
    config.autocorrect_builtins = true;
    config.autocorrect_external = true;
    config.autocorrect_case_sensitive = false;

    // Color defaults
    config.color_scheme = strdup("default");
    config.colors_enabled = true;

    // Advanced defaults
    config.verbose_errors = false;
    config.debug_mode = false;

    // Network defaults
    config.ssh_completion_enabled = true;
    config.cloud_discovery_enabled = false;
    config.cache_ssh_hosts = true;
    config.cache_timeout_minutes = 5;
    config.show_remote_context = true;
    config.auto_detect_cloud = true;
    config.max_completion_hosts = 50;

    // Display defaults
    // v1.3.0: Layered display is now the exclusive system - no configuration
    // needed
    config.display_syntax_highlighting = true;
    config.display_autosuggestions = true;
    config.display_transient_prompt =
        true; // Transient prompts enabled by default (Spec 25 Section 12)
    config.display_theme_hot_reload = true; // Auto-reload theme on file change
    config.display_newline_before_prompt =
        true; // Visual separation before prompt (default on)
    config.display_performance_monitoring = false;
    config.display_optimization_level = 0;

    // Script execution defaults
    config.script_execution = true;

    // Shell mode defaults (Phase 0: Extended Language Support)
    config.shell_mode = SHELL_MODE_LUSH;  // Curated best of Bash/Zsh
    config.shell_mode_strict = false;        // Allow runtime mode changes

    // Line editor - LLE is always enabled (sole line editor)
    // LLE is the only line editor - no config option needed
}

/* ============================================================================
 * XDG Path Resolution Helpers
 * ============================================================================ */

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

    /* Check XDG_CONFIG_HOME first */
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        snprintf(buffer, size, "%s/%s", xdg_config, CONFIG_XDG_DIR);
        return 0;
    }

    /* Fall back to ~/.config */
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
        return -1;  // Path truncated
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
bool config_needs_migration(void) {
    return config_ctx.needs_migration;
}

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
        return 0; /* Nothing to migrate */
    }

    /* Ensure XDG directory exists */
    if (ensure_xdg_dir_exists() != 0) {
        config_error("Failed to create config directory");
        return -1;
    }

    /* Get XDG config path */
    char xdg_path[CONFIG_PATH_MAX];
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) != 0) {
        return -1;
    }

    /* Save current config to XDG location in TOML format */
    /* Note: This will be enhanced once registry is wired up */
    if (config_save_file(xdg_path) != 0) {
        config_error("Failed to save config to %s", xdg_path);
        return -1;
    }

    /* Update context */
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

    /* Try XDG path first */
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
        if (stat(xdg_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(xdg_path);
        }
    }

    /* Try legacy path */
    if (config_get_legacy_config_path(legacy_path, sizeof(legacy_path)) == 0) {
        if (stat(legacy_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(legacy_path);
        }
    }

    /* Neither exists - return XDG path for new config creation */
    if (xdg_path[0] != '\0') {
        return strdup(xdg_path);
    }

    /* Fallback to legacy if we couldn't determine XDG path */
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

    /* Always save to XDG location in TOML format */
    if (config_get_xdg_config_path(xdg_path, sizeof(xdg_path)) == 0) {
        /* Ensure XDG directory exists */
        char xdg_dir[CONFIG_PATH_MAX];
        if (config_get_xdg_dir(xdg_dir, sizeof(xdg_dir)) == 0) {
            struct stat st;
            if (stat(xdg_dir, &st) != 0) {
                /* Directory doesn't exist, create it */
                if (mkdir(xdg_dir, 0755) != 0 && errno != EEXIST) {
                    config_error("Failed to create config directory: %s",
                                 xdg_dir);
                    /* Fall back to current path */
                    if (config_ctx.user_config_path) {
                        return config_save_file(config_ctx.user_config_path);
                    }
                    return -1;
                }
            }
        }

        int result = config_save_file(xdg_path);

        if (result == 0) {
            /* Update context to point to new location */
            if (config_ctx.needs_migration) {
                fprintf(stderr, "lush: Configuration saved to %s\n",
                        xdg_path);
                fprintf(stderr,
                        "lush: You may remove the old %s file\n",
                        config_ctx.legacy_config_path
                            ? config_ctx.legacy_config_path
                            : "~/.lushrc");
                config_ctx.needs_migration = false;
            }

            /* Update user_config_path to the new TOML location */
            free(config_ctx.user_config_path);
            config_ctx.user_config_path = strdup(xdg_path);
            config_ctx.format = CONFIG_FORMAT_TOML;
        }

        return result;
    }

    /* Fallback to existing path if XDG resolution failed */
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

    /* Detect file format based on extension */
    size_t path_len = strlen(path);
    bool is_toml = (path_len > 5 &&
                    lle_unicode_strings_equal(path + path_len - 5, ".toml",
                                              &LLE_UNICODE_COMPARE_DEFAULT));

    if (is_toml) {
        /* Sync current runtime config to registry before saving */
        config_registry_sync_from_runtime();

        /* Use registry's TOML save */
        creg_result_t result = config_registry_save(path);
        return (result == CREG_SUCCESS) ? 0 : -1;
    }

    /* Legacy INI-style format */
    FILE *file = fopen(path, "w");
    if (!file) {
        return -1;
    }

    // Write header comment
    fprintf(file, "# Lush Configuration File\n");
    fprintf(file, "# This file is automatically generated by 'config save'\n");
    fprintf(file, "# Lines starting with # are comments\n");
    fprintf(
        file,
        "# This file uses dotted notation (e.g. history.enabled = true)\n\n");

    // Write all options using full dotted names (no sections)
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        // Handle shell options specially
        if (opt->section == CONFIG_SECTION_SHELL) {
            bool value = config_get_shell_option(opt->name);
            fprintf(file, "%s = %s\n", opt->name, value ? "true" : "false");
        } else {
            // Handle regular options
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
                // Skip NULL or empty strings entirely to avoid parsing issues
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

    /* Detect file format based on extension */
    size_t path_len = strlen(path);
    bool is_toml = (path_len > 5 &&
                    lle_unicode_strings_equal(path + path_len - 5, ".toml",
                                              &LLE_UNICODE_COMPARE_DEFAULT));

    if (is_toml) {
        /* Use TOML parser via config registry */
        creg_result_t result = config_registry_load(path);
        if (result == CREG_SUCCESS) {
            /* Sync registry values to runtime config struct */
            config_registry_sync_to_runtime();
            config_ctx.format = CONFIG_FORMAT_TOML;
            return 0;
        }
        /* Fall through to legacy parser on failure */
        config_warning("TOML parsing failed, trying legacy format");
    }

    /* Legacy INI-style parser */
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

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (config_parse_line(line, config_ctx.line_number, path) != 0) {
            config_warning("Error parsing line %d in %s",
                           config_ctx.line_number, path);
        }
    }

    fclose(file);

    /* After loading legacy config, sync values to registry */
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
    // Skip empty lines and comments
    const char *trimmed = line;
    while (isspace(*trimmed)) {
        trimmed++;
    }

    if (*trimmed == '\0' || *trimmed == '#') {
        return 0;
    }

    // Check for section header
    if (*trimmed == '[') {
        const char *end = strchr(trimmed, ']');
        if (!end) {
            config_error("Invalid section header at line %d in %s", line_num,
                         filename);
            return -1;
        }

        char section_name[64];
        size_t section_len = end - trimmed - 1;
        if (section_len >= sizeof(section_name)) {
            config_error("Section name too long at line %d in %s", line_num,
                         filename);
            return -1;
        }

        strncpy(section_name, trimmed + 1, section_len);
        section_name[section_len] = '\0';

        return config_parse_section(section_name);
    }

    // Parse key=value pair
    char *equals = strchr(trimmed, '=');
    if (!equals) {
        config_error("Invalid configuration line at %d in %s", line_num,
                     filename);
        return -1;
    }

    // Extract key
    char key[128];
    size_t key_len = equals - trimmed;
    if (key_len >= sizeof(key)) {
        config_error("Configuration key too long at line %d in %s", line_num,
                     filename);
        return -1;
    }

    strncpy(key, trimmed, key_len);
    key[key_len] = '\0';

    // Trim whitespace from key
    char *key_end = key + strlen(key) - 1;
    while (key_end > key && isspace(*key_end)) {
        *key_end-- = '\0';
    }

    // Extract value
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
    } else if (strcmp(section_name, "prompt") == 0) {
        current_section = CONFIG_SECTION_PROMPT;
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
        config_warning("Unknown configuration section: %s", section_name);
        current_section = CONFIG_SECTION_NONE;
        return -1;
    }

    return 0;
}

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
    // Handle aliases specially
    if (current_section == CONFIG_SECTION_ALIASES) {
        // Add alias: key = value
        char *alias_cmd = malloc(strlen(value) + 1);
        if (alias_cmd) {
            strcpy(alias_cmd, value);
            set_alias(key, alias_cmd);
        }
        return 0;
    }

    // Handle regular options
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            // Handle shell options specially - they use integration functions
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
                    config_error(
                        "Invalid boolean value '%s' for shell option '%s'",
                        value, key);
                    return -1;
                }
                return 0;
            }

            // Validate value if validator exists
            if (opt->validator && !opt->validator(value)) {
                config_error("Invalid value '%s' for option '%s'", value, key);
                return -1;
            }

            // Set value based on type
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
                /* Look up string value in enum mapping table */
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
                // Color handling would go here
                break;
            }

            return 0;
        }
    }

    // Handle legacy configuration keys gracefully to eliminate warnings
    if (config_handle_legacy_key(key, value)) {
        return 0;
    }

    config_warning("Unknown configuration option: %s", key);
    return -1;
}

/**
 * @brief Apply loaded configuration settings to the shell
 *
 * Updates symbol table variables and subsystem configurations
 * based on loaded config values.
 */
void config_apply_settings(void) {
    // Apply settings safely - only set basic variables for now
    // More complex integrations will be added after basic functionality works

    // Apply shell mode settings (Phase 0: Extended Language Support)
    shell_mode_set((shell_mode_t)config.shell_mode);
    shell_mode_set_strict(config.shell_mode_strict);

    // Basic symbol table settings
    symtable_set_global_int("CONFIG_LOADED", 1);
    symtable_set_global_int("HISTORY_NO_DUPS", config.history_no_dups ? 1 : 0);
    symtable_set_global_int("FUZZY_COMPLETION",
                            config.fuzzy_completion ? 1 : 0);
    symtable_set_global_int("COMPLETION_THRESHOLD",
                            config.completion_threshold);

    // Apply prompt settings (handled by LLE prompt composer)

    // Apply history settings
    // History deduplication is handled automatically by readline integration

    // Apply hints system settings
    if (config.hints_enabled) {
        // Hints are handled differently in readline integration
        // TODO: Implement hints system for readline if needed
    } else {
        // Hints disabled - no action needed for readline integration
    }

    // Apply other settings as needed
    symtable_set_global_int("AUTO_CD", config.auto_cd);
    symtable_set_global_int("SPELL_CORRECTION", config.spell_correction);
    symtable_set_global_int("CONFIRM_EXIT", config.confirm_exit);
    symtable_set_global_int("COLORS_ENABLED", config.colors_enabled);
    symtable_set_global_int("NO_WORD_EXPAND", config.no_word_expand);
    symtable_set_global_int("MULTILINE_MODE", config.multiline_mode);

    // Update autocorrect configuration when spell correction settings change
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

    // Debug mode is handled by LLE's internal configuration
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
    // Basic color validation - could be enhanced
    return config_validate_string(value);
}

/**
 * @brief Validate a color scheme name
 *
 * @param value String to validate
 * @return True if value is a known color scheme
 */
bool config_validate_color_scheme(const char *value) {
    return (strcmp(value, "default") == 0 || strcmp(value, "dark") == 0 ||
            strcmp(value, "light") == 0 || strcmp(value, "solarized") == 0);
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
 * @brief Validate LLE storage mode value
 *
 * @param value String to validate
 * @return True if value is a valid storage mode
 */
bool config_validate_lle_storage_mode(const char *value) {
    return (strcmp(value, "lle-only") == 0 || strcmp(value, "bash-only") == 0 ||
            strcmp(value, "dual") == 0 ||
            strcmp(value, "readline-compat") == 0);
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
    return (strcmp(value, "posix") == 0 || strcmp(value, "bash") == 0 ||
            strcmp(value, "zsh") == 0 || strcmp(value, "lush") == 0 ||
            strcmp(value, "sh") == 0);  /* sh is alias for posix */
}

/**
 * @brief Report a configuration error
 *
 * Formats and prints an error message to stderr.
 *
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void config_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);

    fprintf(stderr, "Config Error: %s\n", last_error);
}

/**
 * @brief Report a configuration warning
 *
 * Formats and prints a warning message to stderr.
 *
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void config_warning(const char *format, ...) {
    char warning[256];
    va_list args;
    va_start(args, format);
    vsnprintf(warning, sizeof(warning), format, args);
    va_end(args);

    fprintf(stderr, "Config Warning: %s\n", warning);
}

/**
 * @brief Get the last configuration error message
 *
 * @return Last error message string
 */
const char *config_get_last_error(void) { return last_error; }

/* ============================================================================
 * Type-Safe Configuration Setters and Getters
 * ============================================================================ */

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
                return -1;  // Type mismatch
            }
            *(bool *)config_options[i].value_ptr = value;
            return 0;
        }
    }
    return -1;  // Key not found
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
                return -1;  // Type mismatch
            }
            *(int *)config_options[i].value_ptr = value;
            return 0;
        }
    }
    return -1;  // Key not found
}

/**
 * @brief Set a string configuration value
 *
 * @param key Configuration key (e.g., "history.file")
 * @param value String value to set (will be duplicated)
 * @return 0 on success, -1 if key not found or type mismatch
 */
int config_set_string(const char *key, const char *value) {
    for (int i = 0; i < num_config_options; i++) {
        if (strcmp(config_options[i].name, key) == 0) {
            if (config_options[i].type != CONFIG_TYPE_STRING &&
                config_options[i].type != CONFIG_TYPE_COLOR) {
                return -1;  // Type mismatch
            }
            char **ptr = (char **)config_options[i].value_ptr;
            if (*ptr) {
                free(*ptr);
            }
            *ptr = value ? strdup(value) : NULL;
            return 0;
        }
    }
    return -1;  // Key not found
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
    return default_value;
}

/**
 * @brief Built-in command for configuration management
 *
 * Implements the 'config' builtin with subcommands:
 * show, set, get, reload, save, reset-defaults.
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
        printf("  migrate            - Migrate legacy ~/.lushrc to XDG location\n");
        printf("  path               - Show configuration file paths\n");
        printf("  reset-defaults     - Write default configuration\n");
        return;
    }

    if (strcmp(argv[1], "reset-defaults") == 0) {
        // Write default configuration template to user config file
        char *path = config_get_user_config_path();
        if (!path) {
            printf("Error: Could not determine user config path\n");
            return;
        }

        // Check if file exists and warn user
        struct stat st;
        if (stat(path, &st) == 0) {
            printf("Warning: %s already exists.\n", path);
            printf("This will overwrite your current configuration with "
                   "defaults.\n");
            printf("Continue? [y/N] ");
            fflush(stdout);

            int c = getchar();
            // Clear any remaining input
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
            // Show specific section
            config_section_t section = CONFIG_SECTION_NONE;
            if (strcmp(argv[2], "history") == 0) {
                section = CONFIG_SECTION_HISTORY;
            } else if (strcmp(argv[2], "completion") == 0) {
                section = CONFIG_SECTION_COMPLETION;
            } else if (strcmp(argv[2], "prompt") == 0) {
                section = CONFIG_SECTION_PROMPT;
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
                    printf("Configuration already at XDG location: %s\n", xdg_path);
                } else {
                    printf("No legacy configuration to migrate.\n");
                    printf("Use 'config save' to create a new configuration.\n");
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
        /* Show current config file paths */
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
               config_ctx.format == CONFIG_FORMAT_TOML ? "TOML" :
               config_ctx.format == CONFIG_FORMAT_LEGACY ? "Legacy INI" : "Unknown");
        if (config_ctx.needs_migration) {
            printf("  Status: Migration pending (run 'config migrate' or 'config save')\n");
        }
    } else {
        printf("Unknown config command: %s\n", argv[1]);
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
    // Handle shell.feature.* keys dynamically (not in config_options array)
    if (strncmp(key, "shell.feature.", 14) == 0) {
        const char *feature_name = key + 14;  // Skip "shell.feature."
        shell_feature_t feature;
        
        if (!shell_feature_parse(feature_name, &feature)) {
            printf("Unknown feature: %s\n", feature_name);
            printf("Use 'debug features' to see available features\n");
            return;
        }
        
        printf("%s\n", shell_mode_allows(feature) ? "true" : "false");
        return;
    }

    // First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            // Handle shell.mode specially - it's an enum
            if (strcmp(key, "shell.mode") == 0) {
                printf("%s\n", shell_mode_name(shell_mode_get()));
                return;
            }

            // Handle shell.mode_strict specially
            if (strcmp(key, "shell.mode_strict") == 0) {
                printf("%s\n", config.shell_mode_strict ? "true" : "false");
                return;
            }

            // Handle other shell options specially - they use integration functions
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

    // Check for legacy option name
    const char *new_name = find_new_name_for_legacy(key);
    if (new_name) {
        printf("Warning: '%s' is deprecated, use '%s' instead\n", key,
               new_name);
        config_get_value(new_name); // Recursive call with new name
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
    // Handle shell.feature.* keys dynamically (not in config_options array)
    if (strncmp(key, "shell.feature.", 14) == 0) {
        const char *feature_name = key + 14;  // Skip "shell.feature."
        shell_feature_t feature;
        
        if (!shell_feature_parse(feature_name, &feature)) {
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
            printf("Invalid boolean value: %s (use true/false/on/off)\n", value);
            return;
        }
        
        if (enable) {
            shell_feature_enable(feature);
        } else {
            shell_feature_disable(feature);
        }
        printf("Set %s = %s\n", key, value);
        return;
    }

    // First try the exact key
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (strcmp(opt->name, key) == 0) {
            // Handle shell.mode specially - it's an enum that also updates shell_mode system
            if (strcmp(key, "shell.mode") == 0) {
                shell_mode_t new_mode;
                if (strcmp(value, "posix") == 0 || strcmp(value, "sh") == 0) {
                    new_mode = SHELL_MODE_POSIX;
                } else if (strcmp(value, "bash") == 0) {
                    new_mode = SHELL_MODE_BASH;
                } else if (strcmp(value, "zsh") == 0) {
                    new_mode = SHELL_MODE_ZSH;
                } else if (strcmp(value, "lush") == 0) {
                    new_mode = SHELL_MODE_LUSH;
                } else {
                    printf("Invalid shell mode: %s (use posix/bash/zsh/lush)\n", value);
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

            // Handle shell.mode_strict specially
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
                    printf("Invalid boolean value: %s (use true/false/on/off)\n", value);
                    return;
                }
                shell_mode_set_strict(strict);
                config.shell_mode_strict = strict;
                printf("Set %s = %s\n", key, value);
                return;
            }

            // Handle other shell options specially - they use integration functions
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

            // Apply the setting immediately
            config_apply_settings();
            return;
        }
    }

    // Check for legacy option name
    const char *new_name = find_new_name_for_legacy(key);
    if (new_name) {
        printf("Warning: '%s' is deprecated, use '%s' instead\n", key,
               new_name);
        config_set_value(new_name, value); // Recursive call with new name
        return;
    }

    // Check for common typos and provide helpful suggestions
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

    printf("\n[completion]\n");
    config_show_section(CONFIG_SECTION_COMPLETION);

    printf("\n[prompt]\n");
    config_show_section(CONFIG_SECTION_PROMPT);

    printf("\n[behavior]\n");
    config_show_section(CONFIG_SECTION_BEHAVIOR);

    printf("\n[display]\n");
    config_show_section(CONFIG_SECTION_DISPLAY);

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
void config_show_section(config_section_t section) {
    for (int i = 0; i < num_config_options; i++) {
        config_option_t *opt = &config_options[i];

        if (opt->section == section) {
            printf("  %s = ", opt->name);

            // Handle shell options specially - they use integration functions
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
}

/**
 * @brief Clean up configuration resources
 *
 * Frees all allocated memory used by the configuration system.
 * Should be called during shell shutdown.
 */
void config_cleanup(void) {
    // Clean up config registry
    config_registry_cleanup();

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
    if (config.prompt_theme) {
        free(config.prompt_theme);
    }
    if (config.prompt_format) {
        free(config.prompt_format);
    }

    // Theme cleanup
    if (config.theme_name) {
        free(config.theme_name);
    }
    if (config.theme_corporate_company) {
        free(config.theme_corporate_company);
    }
    if (config.theme_corporate_department) {
        free(config.theme_corporate_department);
    }
    if (config.theme_corporate_project) {
        free(config.theme_corporate_project);
    }
    if (config.theme_corporate_environment) {
        free(config.theme_corporate_environment);
    }
    if (config.history_file) {
        free(config.history_file);
    }
    if (config.color_scheme) {
        free(config.color_scheme);
    }
}
