/**
 * @file config.h
 * @brief Shell configuration system and settings management
 *
 * Provides configuration file parsing, settings storage, and runtime
 * configuration management for all shell subsystems.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/** @brief Legacy user configuration file name (in home directory) */
#define USER_CONFIG_FILE ".lushrc"

/** @brief System-wide configuration file path */
#define SYSTEM_CONFIG_FILE "/etc/lush/lushrc"

/** @brief XDG config directory name (relative to XDG_CONFIG_HOME) */
#define CONFIG_XDG_DIR "lush"

/** @brief XDG TOML config file name */
#define CONFIG_XDG_FILE "lushrc.toml"

/** @brief XDG shell script file name (sourced after lushrc.toml) */
#define CONFIG_XDG_SCRIPT "lushrc"

/** @brief Maximum path length for configuration files */
#define CONFIG_PATH_MAX 4096

/** @brief Maximum length of a configuration line */
#define MAX_CONFIG_LINE 1024

/** @brief Maximum length of a configuration value */
#define MAX_CONFIG_VALUE 512

/**
 * @brief Configuration file format types
 */
typedef enum {
    CONFIG_FORMAT_UNKNOWN,  /**< Unknown or invalid format */
    CONFIG_FORMAT_LEGACY,   /**< Legacy INI-like format (.lushrc) */
    CONFIG_FORMAT_TOML      /**< TOML format (lushrc.toml) */
} config_format_t;

/**
 * @brief Configuration section identifiers
 *
 * Enumerates the different sections in configuration files.
 */
typedef enum {
    CONFIG_SECTION_NONE,       /**< No section (default) */
    CONFIG_SECTION_HISTORY,    /**< History settings */
    CONFIG_SECTION_COMPLETION, /**< Completion settings */
    CONFIG_SECTION_PROMPT,     /**< Prompt settings */
    CONFIG_SECTION_BEHAVIOR,   /**< Behavior settings */
    CONFIG_SECTION_ALIASES,    /**< Alias definitions */
    CONFIG_SECTION_KEYS,       /**< Key binding settings */
    CONFIG_SECTION_NETWORK,    /**< Network settings */
    CONFIG_SECTION_SCRIPTS,    /**< Script settings */
    CONFIG_SECTION_SHELL,      /**< Shell options */
    CONFIG_SECTION_DISPLAY     /**< Display settings */
} config_section_t;

/**
 * @brief LLE History arrow key behavior modes
 *
 * Controls how arrow keys behave in multiline editing contexts.
 */
typedef enum {
    LLE_ARROW_MODE_CONTEXT_AWARE,  /**< Smart: multiline navigation when in multiline */
    LLE_ARROW_MODE_CLASSIC,        /**< GNU Readline: always history navigation */
    LLE_ARROW_MODE_ALWAYS_HISTORY, /**< Always history, use Ctrl-P/N only */
    LLE_ARROW_MODE_MULTILINE_FIRST /**< Prioritize multiline navigation */
} lle_arrow_key_mode_t;

/**
 * @brief LLE History storage modes
 *
 * Controls how history is stored on disk.
 */
typedef enum {
    LLE_STORAGE_MODE_LLE_ONLY,       /**< Store only in LLE format */
    LLE_STORAGE_MODE_BASH_ONLY,      /**< Store only in bash format */
    LLE_STORAGE_MODE_DUAL,           /**< Store in both formats (recommended) */
    LLE_STORAGE_MODE_READLINE_COMPAT /**< Use GNU Readline's storage */
} lle_history_storage_mode_t;

/**
 * @brief LLE History deduplication scope
 *
 * Controls the scope of duplicate detection.
 */
typedef enum {
    LLE_DEDUP_SCOPE_NONE,    /**< No deduplication */
    LLE_DEDUP_SCOPE_SESSION, /**< Within current session */
    LLE_DEDUP_SCOPE_RECENT,  /**< Last N entries */
    LLE_DEDUP_SCOPE_GLOBAL   /**< Entire history */
} lle_dedup_scope_t;

/**
 * @brief LLE History deduplication strategy
 *
 * Controls how duplicates are handled when detected.
 */
typedef enum {
    LLE_DEDUP_STRATEGY_IGNORE,        /**< Reject new duplicates, keep old */
    LLE_DEDUP_STRATEGY_KEEP_RECENT,   /**< Keep newest, mark old as deleted (default) */
    LLE_DEDUP_STRATEGY_KEEP_FREQUENT, /**< Keep entry with highest usage count */
    LLE_DEDUP_STRATEGY_MERGE,         /**< Merge forensic metadata, keep existing */
    LLE_DEDUP_STRATEGY_KEEP_ALL       /**< No dedup (track frequency only) */
} lle_dedup_strategy_t;

/**
 * @brief Configuration context structure
 *
 * Tracks the current parsing context during configuration file processing.
 */
typedef struct {
    char *user_config_path;    /**< Path to user configuration file */
    char *system_config_path;  /**< Path to system configuration file */
    char *xdg_config_dir;      /**< XDG config directory path */
    char *legacy_config_path;  /**< Path to legacy config if it exists */
    bool user_config_exists;   /**< Whether user config file exists */
    bool system_config_exists; /**< Whether system config file exists */
    config_format_t format;    /**< Format of loaded config file */
    bool needs_migration;      /**< True if legacy config needs migration */
    int line_number;           /**< Current line number being parsed */
    const char *current_file;  /**< Current file being parsed */
} config_context_t;

/**
 * @brief Configuration values structure
 *
 * Contains all configuration settings for the shell.
 */
typedef struct {
    /* History settings */
    bool history_enabled;      /**< Enable command history */
    int history_size;          /**< Maximum history entries */
    bool history_no_dups;      /**< Ignore duplicate entries */
    bool history_timestamps;   /**< Record timestamps */
    char *history_file;        /**< History file path */

    /* LLE History Configuration */
    lle_arrow_key_mode_t lle_arrow_key_mode;     /**< Arrow key behavior mode */
    bool lle_enable_multiline_navigation;        /**< Enable multiline navigation */
    bool lle_wrap_history_navigation;            /**< Wrap at history ends */
    bool lle_save_line_on_history_nav;           /**< Save line when navigating */
    bool lle_preserve_multiline_structure;       /**< Preserve multiline structure */
    bool lle_enable_multiline_editing;           /**< Enable multiline editing */
    bool lle_show_multiline_indicators;          /**< Show multiline indicators */
    bool lle_enable_interactive_search;          /**< Enable interactive search */
    bool lle_search_fuzzy_matching;              /**< Enable fuzzy search matching */
    bool lle_search_case_sensitive;              /**< Case-sensitive search */
    lle_history_storage_mode_t lle_storage_mode; /**< History storage mode */
    char *lle_history_file;                      /**< LLE history file path */
    bool lle_sync_with_readline;                 /**< Sync with readline history */
    bool lle_export_to_bash_history;             /**< Export to bash history */
    bool lle_enable_forensic_tracking;           /**< Enable forensic tracking */
    bool lle_enable_deduplication;               /**< Enable deduplication */
    lle_dedup_scope_t lle_dedup_scope;           /**< Deduplication scope */
    lle_dedup_strategy_t lle_dedup_strategy;     /**< Deduplication strategy */
    bool lle_dedup_navigation;                   /**< Skip duplicates during navigation */
    bool lle_dedup_navigation_unique;            /**< Show only unique entries */
    bool lle_dedup_unicode_normalize;            /**< Use Unicode NFC normalization */
    bool lle_enable_history_cache;               /**< Enable history cache */
    int lle_cache_size;                          /**< Cache size */
    bool lle_readline_compatible_mode;           /**< Readline compatibility mode */

    /* Completion settings */
    bool completion_enabled;        /**< Enable tab completion */
    bool fuzzy_completion;          /**< Enable fuzzy matching */
    int completion_threshold;       /**< Minimum match score */
    bool completion_case_sensitive; /**< Case-sensitive completion */
    bool completion_show_all;       /**< Show all completions */
    bool hints_enabled;             /**< Enable inline hints */

    /* Prompt settings */
    bool use_theme_prompt;   /**< Use theme system for prompts */
    char *prompt_theme;      /**< Theme name */
    bool git_prompt_enabled; /**< Enable git info in prompt */
    int git_cache_timeout;   /**< Git info cache timeout (seconds) */
    char *prompt_format;     /**< Custom prompt format string */

    /* Theme settings */
    char *theme_name;                  /**< Active theme name */
    bool theme_auto_detect_colors;     /**< Auto-detect color support */
    bool theme_fallback_basic;         /**< Fall back to basic theme */
    char *theme_corporate_company;     /**< Company name for corporate theme */
    char *theme_corporate_department;  /**< Department for corporate theme */
    char *theme_corporate_project;     /**< Project for corporate theme */
    char *theme_corporate_environment; /**< Environment for corporate theme */
    bool theme_show_company;           /**< Show company in prompt */
    bool theme_show_department;        /**< Show department in prompt */
    bool theme_show_right_prompt;      /**< Enable right-side prompt */
    bool theme_enable_animations;      /**< Enable prompt animations */
    bool theme_enable_icons;           /**< Enable Unicode icons */
    int theme_color_support_override;  /**< Override detected color support */

    /* Behavior settings */
    bool auto_cd;         /**< Auto-cd to directories */
    bool spell_correction; /**< Enable spell correction */
    bool confirm_exit;    /**< Confirm before exit */
    int tab_width;        /**< Tab display width */
    bool no_word_expand;  /**< Disable word expansion */
    bool multiline_mode;  /**< Enable multiline editing */

    /* Auto-correction settings */
    int autocorrect_max_suggestions;  /**< Maximum suggestions */
    int autocorrect_threshold;        /**< Minimum similarity threshold */
    bool autocorrect_interactive;     /**< Interactive prompts */
    bool autocorrect_learn_history;   /**< Learn from history */
    bool autocorrect_builtins;        /**< Correct builtin names */
    bool autocorrect_external;        /**< Correct external commands */
    bool autocorrect_case_sensitive;  /**< Case-sensitive matching */

    /* Color settings */
    char *color_scheme;   /**< Active color scheme name */
    bool colors_enabled;  /**< Enable colored output */

    /* Advanced settings */
    bool verbose_errors; /**< Verbose error messages */
    bool debug_mode;     /**< Enable debug mode */

    /* Display system settings */
    bool display_syntax_highlighting;    /**< Enable syntax highlighting */
    bool display_autosuggestions;        /**< Enable autosuggestions */
    bool display_transient_prompt;       /**< Enable transient prompts */
    bool display_theme_hot_reload;       /**< Auto-reload theme on file change */
    bool display_newline_before_prompt;  /**< Print newline before prompt */
    bool display_performance_monitoring; /**< Enable performance monitoring */
    int display_optimization_level;      /**< Optimization level (0-4) */
    bool enhanced_display_mode;          /**< Legacy display setting (deprecated) */

    /* Network settings */
    bool ssh_completion_enabled;  /**< Enable SSH host completion */
    bool cloud_discovery_enabled; /**< Enable cloud host discovery */
    bool cache_ssh_hosts;         /**< Cache SSH hosts */
    int cache_timeout_minutes;    /**< Cache timeout in minutes */
    bool show_remote_context;     /**< Show remote context */
    bool auto_detect_cloud;       /**< Auto-detect cloud environment */
    int max_completion_hosts;     /**< Maximum hosts for completion */

    /* Script execution control */
    bool script_execution; /**< Enable script execution */

    /* Shell mode settings (Phase 0: Extended Language Support) */
    int shell_mode;            /**< Shell mode: 0=posix, 1=bash, 2=zsh, 3=lush */
    bool shell_mode_strict;    /**< Disallow runtime mode changes */
} config_values_t;

/** @brief Global configuration instance */
extern config_values_t config;

/** @brief Global configuration context */
extern config_context_t config_ctx;

/* ============================================================================
 * Core Configuration Functions
 * ============================================================================ */

/**
 * @brief Initialize the configuration system
 *
 * Sets up default values and prepares for configuration loading.
 *
 * @return 0 on success, non-zero on error
 */
int config_init(void);

/**
 * @brief Load user configuration file
 *
 * Loads configuration from the user's home directory.
 *
 * @return 0 on success, non-zero on error
 */
int config_load_user(void);

/**
 * @brief Load system configuration file
 *
 * Loads configuration from the system-wide configuration file.
 *
 * @return 0 on success, non-zero on error
 */
int config_load_system(void);

/**
 * @brief Load configuration from a specific file
 *
 * @param path Path to configuration file
 * @return 0 on success, non-zero on error
 */
int config_load_file(const char *path);

/**
 * @brief Save current configuration to user file
 *
 * @return 0 on success, non-zero on error
 */
int config_save_user(void);

/**
 * @brief Clean up configuration system resources
 *
 * Frees all dynamically allocated configuration data.
 */
void config_cleanup(void);

/* ============================================================================
 * Configuration Parsing Functions
 * ============================================================================ */

/**
 * @brief Parse a single configuration line
 *
 * @param line Line to parse
 * @param line_num Line number for error reporting
 * @param filename Filename for error reporting
 * @return 0 on success, non-zero on error
 */
int config_parse_line(const char *line, int line_num, const char *filename);

/**
 * @brief Parse a section header
 *
 * @param section_name Section name from header
 * @return 0 on success, non-zero on error
 */
int config_parse_section(const char *section_name);

/**
 * @brief Parse a configuration option
 *
 * @param key Option key
 * @param value Option value
 * @return 0 on success, non-zero on error
 */
int config_parse_option(const char *key, const char *value);

/* ============================================================================
 * Shell Option Integration Functions
 * ============================================================================ */

/**
 * @brief Validate a shell option value
 *
 * @param value Value to validate
 * @return true if valid, false otherwise
 */
bool config_validate_shell_option(const char *value);

/**
 * @brief Set a shell option by name
 *
 * @param option_name Option name
 * @param value Boolean value to set
 */
void config_set_shell_option(const char *option_name, bool value);

/**
 * @brief Get a shell option by name
 *
 * @param option_name Option name
 * @return Current value of the option
 */
bool config_get_shell_option(const char *option_name);

/**
 * @brief Get the current configuration section
 *
 * @return Current section being parsed
 */
config_section_t config_get_current_section(void);

/* ============================================================================
 * Configuration Validation Functions
 * ============================================================================ */

/**
 * @brief Validate a boolean configuration value
 *
 * @param value Value string to validate
 * @return true if valid boolean, false otherwise
 */
bool config_validate_bool(const char *value);

/**
 * @brief Validate an integer configuration value
 *
 * @param value Value string to validate
 * @return true if valid integer, false otherwise
 */
bool config_validate_int(const char *value);

/**
 * @brief Validate a string configuration value
 *
 * @param value Value string to validate
 * @return true if valid string, false otherwise
 */
bool config_validate_string(const char *value);

/**
 * @brief Validate a color configuration value
 *
 * @param value Color value to validate
 * @return true if valid color, false otherwise
 */
bool config_validate_color(const char *value);

/**
 * @brief Validate a float configuration value
 *
 * @param value Value string to validate
 * @return true if valid float, false otherwise
 */
bool config_validate_float(const char *value);

/**
 * @brief Validate a path configuration value
 *
 * @param value Path string to validate
 * @return true if valid path, false otherwise
 */
bool config_validate_path(const char *value);

/**
 * @brief Validate a display mode value
 *
 * @param value Display mode string to validate
 * @return true if valid display mode, false otherwise
 */
bool config_validate_display_mode(const char *value);

/**
 * @brief Validate an optimization level value
 *
 * @param value Optimization level string to validate
 * @return true if valid optimization level, false otherwise
 */
bool config_validate_optimization_level(const char *value);

/**
 * @brief Validate a color scheme value
 *
 * @param value Color scheme name to validate
 * @return true if valid color scheme, false otherwise
 */
bool config_validate_color_scheme(const char *value);

/**
 * @brief Validate an LLE arrow mode value
 *
 * @param value Arrow mode string to validate
 * @return true if valid arrow mode, false otherwise
 */
bool config_validate_lle_arrow_mode(const char *value);

/**
 * @brief Validate an LLE storage mode value
 *
 * @param value Storage mode string to validate
 * @return true if valid storage mode, false otherwise
 */
bool config_validate_lle_storage_mode(const char *value);

/**
 * @brief Validate an LLE dedup scope value
 *
 * @param value Dedup scope string to validate
 * @return true if valid dedup scope, false otherwise
 */
bool config_validate_lle_dedup_scope(const char *value);

/**
 * @brief Validate an LLE dedup strategy value
 *
 * @param value Dedup strategy string to validate
 * @return true if valid dedup strategy, false otherwise
 */
bool config_validate_lle_dedup_strategy(const char *value);

/**
 * @brief Validate a shell mode value
 *
 * @param value Shell mode string to validate (posix, bash, zsh, lush)
 * @return true if valid shell mode, false otherwise
 */
bool config_validate_shell_mode(const char *value);

/* ============================================================================
 * Configuration Value Setters and Getters
 * ============================================================================ */

/**
 * @brief Set a boolean configuration value
 *
 * @param key Configuration key
 * @param value Boolean value
 * @return 0 on success, non-zero on error
 */
int config_set_bool(const char *key, bool value);

/**
 * @brief Set an integer configuration value
 *
 * @param key Configuration key
 * @param value Integer value
 * @return 0 on success, non-zero on error
 */
int config_set_int(const char *key, int value);

/**
 * @brief Set a string configuration value
 *
 * @param key Configuration key
 * @param value String value
 * @return 0 on success, non-zero on error
 */
int config_set_string(const char *key, const char *value);

/**
 * @brief Get a boolean configuration value
 *
 * @param key Configuration key
 * @param default_value Default if key not found
 * @return Configuration value or default
 */
bool config_get_bool(const char *key, bool default_value);

/**
 * @brief Get an integer configuration value
 *
 * @param key Configuration key
 * @param default_value Default if key not found
 * @return Configuration value or default
 */
int config_get_int(const char *key, int default_value);

/**
 * @brief Get a string configuration value
 *
 * @param key Configuration key
 * @param default_value Default if key not found
 * @return Configuration value or default
 */
const char *config_get_string(const char *key, const char *default_value);

/* ============================================================================
 * Configuration Utility Functions
 * ============================================================================ */

/**
 * @brief Set all configuration values to defaults
 */
void config_set_defaults(void);

/**
 * @brief Apply loaded configuration settings
 *
 * Applies all configuration values to their respective subsystems.
 */
void config_apply_settings(void);

/**
 * @brief Create a default user configuration file
 *
 * @return 0 on success, non-zero on error
 */
int config_create_user_config(void);

/**
 * @brief Get the path to the user configuration file
 *
 * Returns the XDG config path (~/.config/lush/lushrc.toml) if it exists,
 * otherwise returns the legacy path (~/.lushrc) if it exists.
 * If neither exists, returns the XDG path for new config creation.
 *
 * @return Path string (caller must free), or NULL on error
 */
char *config_get_user_config_path(void);

/**
 * @brief Get the path to the system configuration file
 *
 * @return Path string (caller must free), or NULL on error
 */
char *config_get_system_config_path(void);

/**
 * @brief Get the XDG config directory path
 *
 * Returns the path to ~/.config/lush or $XDG_CONFIG_HOME/lush.
 * Creates the directory if it doesn't exist.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_xdg_dir(char *buffer, size_t size);

/**
 * @brief Get the XDG config file path
 *
 * Returns the path to the TOML config file in the XDG directory.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_xdg_config_path(char *buffer, size_t size);

/**
 * @brief Get the legacy config file path
 *
 * Returns the path to ~/.lushrc.
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_legacy_config_path(char *buffer, size_t size);

/**
 * @brief Check if legacy config needs migration
 *
 * @return true if legacy config exists and XDG config does not
 */
bool config_needs_migration(void);

/**
 * @brief Migrate legacy config to XDG location
 *
 * Converts ~/.lushrc to ~/.config/lush/lushrc.toml format.
 *
 * @return 0 on success, -1 on error
 */
int config_migrate_to_xdg(void);

/**
 * @brief Get the path to the shell script config file
 *
 * Returns the path to lushrc (sourced after lushrc.toml).
 *
 * @param buffer Buffer to receive the path
 * @param size Size of the buffer
 * @return 0 on success, -1 on error
 */
int config_get_script_config_path(char *buffer, size_t size);

/* ============================================================================
 * Script Execution Support
 * ============================================================================ */

/**
 * @brief Execute startup scripts
 *
 * Runs shell startup scripts in the proper order.
 *
 * @return 0 on success, non-zero on error
 */
int config_execute_startup_scripts(void);

/**
 * @brief Execute system profile scripts for login shells
 *
 * Sources system-wide configuration files in the following order:
 * 1. /etc/lushrc (lush-specific system config, if exists)
 * 2. /etc/profile (standard POSIX login config, if exists)
 * 3. /etc/profile.d/ shell scripts (*.sh files, alphabetically)
 *
 * This should be called BEFORE user profile scripts.
 *
 * @return 0 on success, -1 if any script fails (non-fatal)
 */
int config_execute_system_profile(void);

/**
 * @brief Execute login scripts
 *
 * Runs user login-specific shell scripts (~/.profile, ~/.lush_login).
 *
 * @return 0 on success, non-zero on error
 */
int config_execute_login_scripts(void);

/**
 * @brief Execute logout scripts
 *
 * Runs shell logout scripts.
 *
 * @return 0 on success, non-zero on error
 */
int config_execute_logout_scripts(void);

/**
 * @brief Execute a specific script file
 *
 * @param path Path to script file
 * @return 0 on success, non-zero on error
 */
int config_execute_script_file(const char *path);

/**
 * @brief Check if script execution is enabled
 *
 * @return true if script execution is enabled, false otherwise
 */
bool config_should_execute_scripts(void);

/**
 * @brief Enable or disable script execution
 *
 * @param enabled Whether to enable script execution
 */
void config_set_script_execution(bool enabled);

/* ============================================================================
 * Traditional Shell Script File Detection
 * ============================================================================ */

/**
 * @brief Get path to profile script
 *
 * @return Path string (caller must free), or NULL if not found
 */
char *config_get_profile_script_path(void);

/**
 * @brief Get path to login script
 *
 * @return Path string (caller must free), or NULL if not found
 */
char *config_get_login_script_path(void);

/**
 * @brief Get path to rc script
 *
 * @return Path string (caller must free), or NULL if not found
 */
char *config_get_rc_script_path(void);

/**
 * @brief Get path to logout script
 *
 * @return Path string (caller must free), or NULL if not found
 */
char *config_get_logout_script_path(void);

/**
 * @brief Check if a script file exists
 *
 * @param path Path to script file
 * @return true if exists and is readable, false otherwise
 */
bool config_script_exists(const char *path);

/* ============================================================================
 * Configuration Error Handling
 * ============================================================================ */

/**
 * @brief Report a configuration error
 *
 * Prints a formatted error message with file and line information.
 *
 * @param format printf-style format string
 * @param ... Format arguments
 */
void config_error(const char *format, ...);

/**
 * @brief Report a configuration warning
 *
 * Prints a formatted warning message with file and line information.
 *
 * @param format printf-style format string
 * @param ... Format arguments
 */
void config_warning(const char *format, ...);

/**
 * @brief Get the last configuration error message
 *
 * @return Error message string, or NULL if no error
 */
const char *config_get_last_error(void);

/* ============================================================================
 * Configuration Display Functions
 * ============================================================================ */

/**
 * @brief Display all configuration settings
 *
 * Prints all current configuration values to stdout.
 */
void config_show_all(void);

/**
 * @brief Display configuration settings for a section
 *
 * @param section Section to display
 */
void config_show_section(config_section_t section);

/**
 * @brief Display a specific configuration option
 *
 * @param key Option key to display
 */
void config_show_option(const char *key);

/**
 * @brief Get a configuration value by key
 *
 * Prints the value of the specified configuration key.
 *
 * @param key Configuration key
 */
void config_get_value(const char *key);

/**
 * @brief Set a configuration value by key
 *
 * Sets the specified configuration key to the given value.
 *
 * @param key Configuration key
 * @param value Value to set
 */
void config_set_value(const char *key, const char *value);

/* ============================================================================
 * Configuration Save Functions
 * ============================================================================ */

/**
 * @brief Save configuration to a specific file
 *
 * @param path File path to save to
 * @return 0 on success, non-zero on error
 */
int config_save_file(const char *path);

/* ============================================================================
 * Built-in Command Integration
 * ============================================================================ */

/**
 * @brief Configuration builtin command handler
 *
 * Implements the 'config' builtin command for runtime configuration.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void builtin_config(int argc, char **argv);

/** @brief Configuration file template for new installations */
extern const char *CONFIG_FILE_TEMPLATE;

#endif /* CONFIG_H */
