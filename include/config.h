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

/**
 * @brief Home-directory TOML config fallback (legacy / adoption path).
 *
 * XDG (`~/.config/lush/lushrc.toml`) is the canonical default. This
 * home-dir fallback exists for users not on XDG layouts. Note the
 * `.toml` suffix: bare `~/.lushrc` is the SHELL-SCRIPT location (see
 * RC_SCRIPT_FILE in config.c), not a TOML file. The two MUST stay
 * separate -- previously this macro was `.lushrc` (no suffix) which
 * collided with the script loader and emitted a spurious E1204 parse
 * error whenever a user had a normal shell-script `~/.lushrc`.
 */
#define USER_CONFIG_FILE ".lushrc.toml"

/** @brief System-wide configuration file path (TOML) */
#define SYSTEM_CONFIG_FILE "/etc/lush/lushrc.toml"

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
    CONFIG_FORMAT_UNKNOWN, ///< Unknown or invalid format
    CONFIG_FORMAT_TOML     ///< TOML format (lushrc.toml) -- the only format
} config_format_t;

/**
 * @brief Configuration section identifiers
 *
 * Enumerates the different sections in configuration files.
 */
typedef enum {
    CONFIG_SECTION_NONE,           ///< No section (default)
    CONFIG_SECTION_HISTORY,        ///< History settings
    CONFIG_SECTION_COMPLETION,     ///< Completion settings
    CONFIG_SECTION_BEHAVIOR,       ///< Behavior settings
    CONFIG_SECTION_ALIASES,        ///< Alias definitions
    CONFIG_SECTION_KEYS,           ///< Key binding settings
    CONFIG_SECTION_SHELL,          ///< Shell options
    CONFIG_SECTION_DISPLAY,        ///< Display settings
    CONFIG_SECTION_AUTOSUGGESTION, ///< Autosuggestion settings
    CONFIG_SECTION_LLE             ///< LLE history/editor settings
} config_section_t;

/**
 * @brief LLE History arrow key behavior modes
 *
 * Controls how arrow keys behave in multiline editing contexts.
 */
typedef enum {
    LLE_ARROW_MODE_CONTEXT_AWARE,  ///< Smart: multiline navigation when in
                                   ///< multiline
    LLE_ARROW_MODE_CLASSIC,        ///< GNU Readline: always history navigation
    LLE_ARROW_MODE_ALWAYS_HISTORY, ///< Always history, use Ctrl-P/N only
    LLE_ARROW_MODE_MULTILINE_FIRST ///< Prioritize multiline navigation
} lle_arrow_key_mode_t;

/**
 * @brief LLE History deduplication scope
 *
 * Controls the scope of duplicate detection.
 */
typedef enum {
    LLE_DEDUP_SCOPE_NONE,    ///< No deduplication
    LLE_DEDUP_SCOPE_SESSION, ///< Within current session
    LLE_DEDUP_SCOPE_RECENT,  ///< Last N entries
    LLE_DEDUP_SCOPE_GLOBAL   ///< Entire history
} lle_dedup_scope_t;

/**
 * @brief LLE History deduplication strategy
 *
 * Controls how duplicates are handled when detected.
 */
typedef enum {
    LLE_DEDUP_STRATEGY_IGNORE,        ///< Reject new duplicates, keep old
    LLE_DEDUP_STRATEGY_KEEP_RECENT,   ///< Keep newest, mark old as deleted
                                      ///< (default)
    LLE_DEDUP_STRATEGY_KEEP_FREQUENT, ///< Keep entry with highest usage count
    LLE_DEDUP_STRATEGY_MERGE,   ///< Merge forensic metadata, keep existing
    LLE_DEDUP_STRATEGY_KEEP_ALL ///< No dedup (track frequency only)
} lle_dedup_strategy_t;

/**
 * @brief Completion match mode
 *
 * Selects the predicate used to filter completion candidates against
 * the typed word prefix. Applies uniformly to the bridge pre-emit
 * filter and (subsequent commit) the in-menu type-to-filter:
 *
 *  - PREFIX: NFC-aware prefix match via lle_unicode_is_prefix_z.
 *    Most predictable; matches bash / zsh consensus.
 *  - SUBSTRING: NFC-aware substring containment via
 *    lle_unicode_contains_z. Matches a typed pattern anywhere in
 *    the candidate.
 *  - FUZZY: fzy-style subsequence scoring via fuzzy_completion_score,
 *    gated by completion.threshold. Most forgiving.
 *
 * Per-mode defaults: prefix for posix / bash / zsh; fuzzy for lush.
 */
typedef enum {
    COMPLETION_MATCH_PREFIX,    ///< NFC prefix match (default outside lush)
    COMPLETION_MATCH_SUBSTRING, ///< NFC substring containment
    COMPLETION_MATCH_FUZZY      ///< fzy-style scoring (default in lush)
} completion_match_mode_t;

/**
 * @brief Autosuggestion dismiss policy
 *
 * Controls when the autosuggestion ghost text is cleared while typing.
 * `on_deviation` keeps the suggestion alive as long as each keystroke (spaces
 * included) still matches it, clearing only on a deviation -- the intuitive
 * default. `on_word_boundary` clears the suggestion whenever the buffer ends at
 * a word boundary (a trailing space), the quieter legacy behavior.
 */
typedef enum {
    AUTOSUGGESTION_DISMISS_ON_DEVIATION,    ///< Clear only on a non-matching
                                            ///< keystroke (lush default)
    AUTOSUGGESTION_DISMISS_ON_WORD_BOUNDARY ///< Clear at a trailing-space word
                                            ///< boundary
} autosuggestion_dismiss_policy_t;

/**
 * @brief Which history prefix match to suggest as the ghost text
 *
 * `recency` suggests the most recent history entry starting with the typed
 * prefix (Fish-style, the classic behavior). `frecency` suggests the prefix
 * match with the highest frecency score (usage frequency weighted by recency)
 * -- the command the user actually returns to, the lush default.
 */
typedef enum {
    AUTOSUGGESTION_RANK_FRECENCY, ///< Highest frecency prefix match (lush)
    AUTOSUGGESTION_RANK_RECENCY   ///< Most recent prefix match (others)
} autosuggestion_rank_t;

/**
 * @brief Granularity of partial autosuggestion acceptance (Ctrl+Right)
 *
 * `path_segment` accepts up to the next `/` inside a path token and the whole
 * whitespace word elsewhere -- so a long path is accepted directory by
 * directory (matching fish's forward-word, the default). `word` always accepts
 * a whole whitespace-delimited word at a time.
 */
typedef enum {
    AUTOSUGGESTION_PARTIAL_ACCEPT_PATH_SEGMENT, ///< Path segment / word
                                                ///< (default)
    AUTOSUGGESTION_PARTIAL_ACCEPT_WORD          ///< Always whole word
} autosuggestion_partial_accept_t;

/**
 * @brief Sources the autosuggestion draws ghost text from
 *
 * `history` suggests only from command history (prefix match). `history_then_
 * completion` keeps history as the fast primary source and, only when history
 * has no match, falls back to the completion engine's best candidate (mainly
 * filesystem paths as you type them) -- fish-style, the lush default. The
 * fallback runs a single completion query and never on a history hit, so the
 * common case stays instant.
 */
typedef enum {
    AUTOSUGGESTION_SOURCES_HISTORY_THEN_COMPLETION, ///< History, then
                                                    ///< completion
    AUTOSUGGESTION_SOURCES_HISTORY                  ///< History only
} autosuggestion_sources_t;

/**
 * @brief History up/down navigation search mode
 *
 * Controls which history entries up/down arrows cycle through. `prefix` filters
 * to entries beginning with the text typed before the cursor and keeps the
 * cursor at that boundary (zsh history-beginning-search style) -- the lush
 * default. `plain` browses all history with the cursor at end of line (the
 * classic bash/readline behavior).
 */
typedef enum {
    HISTORY_SEARCH_MODE_PREFIX, ///< Filter by the typed prefix (lush default)
    HISTORY_SEARCH_MODE_PLAIN   ///< Browse all history (bash/posix/zsh default)
} history_search_mode_t;

/**
 * @brief Interactive history finder (Ctrl-R) matching strategy
 *
 * `fuzzy` matches the query characters in order with gaps (the lush default);
 * `substring` requires a contiguous match (classic reverse-i-search);
 * `prefix` matches only at the start of the command.
 */
typedef enum {
    HISTORY_FINDER_MATCH_FUZZY,     ///< Subsequence match (lush default)
    HISTORY_FINDER_MATCH_SUBSTRING, ///< Contiguous match (bash/posix/zsh)
    HISTORY_FINDER_MATCH_PREFIX     ///< Match at command start only
} history_finder_match_t;

/**
 * @brief Interactive history finder ranking strategy
 *
 * `frecency` orders matches by usage frequency weighted by recency (the lush
 * default); `recency` orders strictly by most-recent-first (classic behavior).
 */
typedef enum {
    HISTORY_FINDER_RANK_FRECENCY, ///< Frequency x recency (lush default)
    HISTORY_FINDER_RANK_RECENCY   ///< Most recent first (bash/posix/zsh)
} history_finder_rank_t;

/**
 * @brief Interactive history finder presentation
 *
 * `incremental` is the in-line reverse-i-search prompt. `picker` is reserved
 * for a future full-screen multi-line selector; until it ships, selecting it
 * falls back to the incremental finder.
 */
typedef enum {
    HISTORY_FINDER_DISPLAY_INCREMENTAL, ///< In-line reverse-i-search (default)
    HISTORY_FINDER_DISPLAY_PICKER       ///< Reserved; falls back to incremental
} history_finder_display_t;

/**
 * @brief Configuration context structure
 *
 * Tracks the current parsing context during configuration file processing.
 */
typedef struct {
    char *user_config_path;    ///< Path to user configuration file
    char *system_config_path;  ///< Path to system configuration file
    char *xdg_config_dir;      ///< XDG config directory path
    bool user_config_exists;   ///< Whether user config file exists
    bool system_config_exists; ///< Whether system config file exists
    config_format_t format;    ///< Format of loaded config file
} config_context_t;

/**
 * @brief Configuration values structure
 *
 * Contains all configuration settings for the shell.
 */
typedef struct {
    /// History settings
    bool history_enabled;    ///< Enable command history
    int history_size;        ///< Maximum history entries
    bool history_no_dups;    ///< Ignore duplicate entries
    bool history_timestamps; ///< Record timestamps
    history_search_mode_t
        history_search_mode; ///< Up/down navigation filter (prefix / plain)
    history_finder_match_t
        history_finder_match; ///< Ctrl-R matching (fuzzy / substring / prefix)
    history_finder_rank_t
        history_finder_rank; ///< Ctrl-R ranking (frecency / recency)
    history_finder_display_t
        history_finder_display; ///< Ctrl-R presentation (incremental / picker)
    bool history_frecency_directory_context; ///< Boost frecency for commands
                                             ///< recorded in the current dir

    /// LLE History Configuration
    lle_arrow_key_mode_t lle_arrow_key_mode; ///< Arrow key behavior mode
    char *lle_history_file;                  ///< LLE history file path
    bool lle_enable_forensic_tracking;       ///< Enable forensic tracking
    bool lle_enable_deduplication;           ///< Enable deduplication
    lle_dedup_scope_t lle_dedup_scope;       ///< Deduplication scope
    lle_dedup_strategy_t lle_dedup_strategy; ///< Deduplication strategy
    bool lle_dedup_navigation;        ///< Skip duplicates during navigation
    bool lle_dedup_navigation_unique; ///< Show only unique entries
    bool lle_dedup_unicode_normalize; ///< Use Unicode NFC normalization
    bool lle_enable_history_cache;    ///< Enable history cache
    int lle_cache_size;               ///< Cache size

    /// Completion settings
    bool completion_enabled;                       ///< Enable tab completion
    completion_match_mode_t completion_match_mode; ///< Match predicate
    int completion_threshold;                      ///< Minimum match score
    bool completion_case_sensitive; ///< Case-sensitive completion
    int completion_fuzzy_min_chars; ///< Min typed chars before fuzzy/substring
                                    ///< widen (shorter stays prefix)

    /// Behavior settings
    bool auto_cd;            ///< Auto-cd to directories
    bool spell_correction;   ///< Enable spell correction
    bool confirm_exit;       ///< Confirm before exit
    int tab_width;           ///< Tab display width
    int brace_expansion_max; ///< Max brace expansion result count (0 =
                             ///< unbounded)
    int regex_pattern_max;   ///< Max regex pattern length before rejection (0 =
                             ///< unbounded; covers `[[ =~ ]]` and extglob
                             ///< translation paths)
    int path_negative_cache_ttl_ms; ///< TTL (ms) for negative PATH-search
                                    ///< cache; bounds repeated lookups of a
                                    ///< missing command in tight loops to O(1)
                                    ///< instead of O(PATH_dirs) (0 = disabled)
    int loop_failure_streak;  ///< Consecutive non-zero body iterations before
                              ///< runaway-loop trip (0 = disable)
    int loop_failure_seconds; ///< Min wall-clock seconds streak must last
                              ///< before tripping

    /// Auto-correction settings
    int autocorrect_max_suggestions; ///< Maximum suggestions
    int autocorrect_threshold;       ///< Minimum similarity threshold
    bool autocorrect_interactive;    ///< Interactive prompts
    bool autocorrect_learn_history;  ///< Learn from history
    bool autocorrect_builtins;       ///< Correct builtin names
    bool autocorrect_external;       ///< Correct external commands
    bool autocorrect_case_sensitive; ///< Case-sensitive matching

    /// Display system settings
    bool display_syntax_highlighting;   ///< Enable syntax highlighting
    bool display_autosuggestions;       ///< Enable autosuggestions
    bool display_transient_prompt;      ///< Enable transient prompts
    bool display_theme_hot_reload;      ///< Auto-reload theme on file change
    bool display_newline_before_prompt; ///< Print newline before prompt
    int display_optimization_level;     ///< Optimization level (0-4)
    char *display_ambiguous_width;      ///< East Asian Ambiguous width
                                        ///< policy: "narrow" or "wide"
    char *display_lle_theme;    ///< Persisted LLE prompt theme name (NULL/empty
                                ///< uses the composer's default theme)
    bool enhanced_display_mode; ///< Legacy display setting (deprecated)

    /// LLE pager (lle_pager_present)
    bool display_lle_pager_enabled;     ///< Master switch for pagination
    int display_lle_pager_min_lines;    ///< Threshold rows; 0 = terminal_rows
    bool display_lle_pager_wrap_search; ///< Wrap to top on search no-match

    /// Autosuggestion settings
    autosuggestion_dismiss_policy_t
        autosuggestion_dismiss_policy; ///< When to clear the ghost text
    autosuggestion_rank_t
        autosuggestion_rank; ///< Which prefix match to suggest
    autosuggestion_partial_accept_t
        autosuggestion_partial_accept; ///< Ctrl+Right granularity
    autosuggestion_sources_t
        autosuggestion_sources; ///< History only, or history then completion

    /// Shell mode settings (Extended Language Support)
    int shell_mode;         ///< Shell mode: 0=posix, 1=bash, 2=zsh, 3=lush
    bool shell_mode_strict; ///< Disallow runtime mode changes
} config_values_t;

/** @brief Global configuration instance */
extern config_values_t config;

/** @brief Global configuration context */
extern config_context_t config_ctx;

/* ============================================================================
 * Core Configuration Functions
 * ============================================================================
 */

/**
 * @brief Initialize the configuration system
 *
 * Sets up default values and prepares for configuration loading.
 *
 * @return 0 on success, non-zero on error
 */
int config_init(void);

/**
 * @brief Number of type descriptors that failed to attach during config_init.
 *
 * Zero in a correct build. Nonzero means a key in the internal type tables is
 * misspelled, unregistered, or has a storage kind its descriptor does not
 * match, so that key validates nothing. A unit test asserts this is zero, so a
 * mis-registered table entry cannot ship silently.
 *
 * @return Count of failed attachments from the most recent config_register.
 */
int config_type_attach_failure_count(void);

/**
 * @brief Count of discoverability tiers that failed to attach.
 *
 * Zero in a correct build. Nonzero means a key in the beginner-tier table is
 * misspelled or unregistered, so the wizard would silently skip it. A unit test
 * asserts this is zero.
 *
 * @return Count of failed tier attachments from the most recent
 * config_register.
 */
int config_tier_attach_failure_count(void);

/**
 * @brief Parse a textual boolean as the config builtin does.
 *
 * Accepts true/1/on/yes and false/0/off/no. Shared so the wizard parses
 * booleans identically to `config set`.
 *
 * @return true if recognized (with @p out set), false otherwise.
 */
bool config_parse_bool_text(const char *value, bool *out);

/**
 * @brief Run the interactive configuration wizard.
 *
 * Walks the curated beginner tier (config_registry_collect_by_tier), prompting
 * for each setting with its help text, current value, and valid values, and
 * applies each answer to the SESSION layer live. Offers to persist at the end.
 * Requires an interactive terminal.
 *
 * @return 0 on completion (including a user cancel), -1 if not interactive.
 */
int config_wizard_run(void);

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
 * ============================================================================
 */

/* ============================================================================
 * Shell Option Integration Functions
 * ============================================================================
 */

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

/* ============================================================================
 * Configuration Validation Functions
 * ============================================================================
 */

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
 * @brief Validate an East Asian Ambiguous-width policy value
 *
 * @param value Width policy string ("narrow" or "wide")
 * @return true if valid, false otherwise
 */
bool config_validate_ambiguous_width(const char *value);

/**
 * @brief Validate an LLE arrow mode value
 *
 * @param value Arrow mode string to validate
 * @return true if valid arrow mode, false otherwise
 */
bool config_validate_lle_arrow_mode(const char *value);

/**
 * @brief Validate a completion match mode value
 *
 * @param value Match mode string to validate ("prefix", "substring", "fuzzy")
 * @return true if valid match mode, false otherwise
 */
bool config_validate_completion_match_mode(const char *value);

/**
 * @brief Validate an autosuggestion dismiss policy value
 *
 * @param value Policy string ("on_deviation" or "on_word_boundary")
 * @return true if valid, false otherwise
 */
bool config_validate_autosuggestion_dismiss_policy(const char *value);

/**
 * @brief Validate an autosuggestion rank value
 *
 * @param value Rank string ("frecency" or "recency")
 * @return true if valid, false otherwise
 */
bool config_validate_autosuggestion_rank(const char *value);

/**
 * @brief Validate an autosuggestion partial-accept value
 *
 * @param value Granularity string ("path_segment" or "word")
 * @return true if valid, false otherwise
 */
bool config_validate_autosuggestion_partial_accept(const char *value);

/**
 * @brief Validate an autosuggestion sources value
 *
 * @param value Sources string ("history" or "history_then_completion")
 * @return true if valid, false otherwise
 */
bool config_validate_autosuggestion_sources(const char *value);

/**
 * @brief Validate a history search mode value
 *
 * @param value Mode string ("prefix" or "plain")
 * @return true if valid, false otherwise
 */
bool config_validate_history_search_mode(const char *value);

/**
 * @brief Validate a history finder match value
 *
 * @param value Match string ("fuzzy", "substring", or "prefix")
 * @return true if valid, false otherwise
 */
bool config_validate_history_finder_match(const char *value);

/**
 * @brief Validate a history finder rank value
 *
 * @param value Rank string ("frecency" or "recency")
 * @return true if valid, false otherwise
 */
bool config_validate_history_finder_rank(const char *value);

/**
 * @brief Validate a history finder display value
 *
 * @param value Display string ("incremental" or "picker")
 * @return true if valid, false otherwise
 */
bool config_validate_history_finder_display(const char *value);

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
 * ============================================================================
 */

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
 * ============================================================================
 */

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
 * ============================================================================
 */

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

/* ============================================================================
 * Traditional Shell Script File Detection
 * ============================================================================
 */

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
 * ============================================================================
 */

/* config_error(), config_warning(), and config_get_last_error() were
 * removed as part of the structured-error migration (#71). Use the
 * shell_error_create() / shell_error_display() / shell_error_free()
 * API in include/shell_error.h directly at each error site. */

/* ============================================================================
 * Configuration Display Functions
 * ============================================================================
 */

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
 * ============================================================================
 */

/**
 * @brief Save configuration to a specific file
 *
 * @param path File path to save to
 * @return 0 on success, non-zero on error
 */
int config_save_file(const char *path);

/* ============================================================================
 * Built-in Command Integration
 * ============================================================================
 */

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

#endif /// CONFIG_H
