/**
 * @file shell_mode.h
 * @brief Shell mode and feature flag system
 *
 * Provides the multi-mode architecture that enables POSIX, Bash, Zsh, and
 * Lush-native shell modes. Each mode defines which language features are
 * available, with per-feature override capability for user customization.
 *
 * The shell mode system is the backbone of lush's extended language support,
 * allowing users to choose between strict POSIX compliance, Bash/Zsh
 * compatibility modes, or the curated Lush-native mode that cherry-picks
 * the best features from both.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef SHELL_MODE_H
#define SHELL_MODE_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Shell Mode Types
 * ============================================================================
 */

/**
 * @brief Shell compatibility modes
 *
 * Defines the available shell modes that control default feature availability
 * and behavioral semantics.
 */
typedef enum {
    SHELL_MODE_POSIX, ///< Strict POSIX sh compliance - minimal features
    SHELL_MODE_BASH,  ///< Bash 5.x compatibility mode
    SHELL_MODE_ZSH,   ///< Zsh compatibility mode
    SHELL_MODE_LUSH,  ///< Lush-native: curated best of both (default)
    SHELL_MODE_COUNT  ///< Number of shell modes (for array sizing)
} shell_mode_t;

/* ============================================================================
 * Feature Flags
 * ============================================================================
 */

/**
 * @brief Shell feature flags
 *
 * Enumeration of all optional shell features that can be enabled or disabled
 * based on shell mode or user configuration. Each feature can be queried
 * via shell_mode_allows() to determine if it should be active.
 */
typedef enum {
    /// Arrays (Phase 1)
    FEATURE_INDEXED_ARRAYS,     ///< arr=(a b c), ${arr[0]}
    FEATURE_ASSOCIATIVE_ARRAYS, ///< declare -A, ${arr[key]}
    FEATURE_ARRAY_ZERO_INDEXED, ///< Bash: 0-indexed, Zsh: 1-indexed
    FEATURE_ARRAY_APPEND,       ///< arr+=(value) syntax

    /// Arithmetic (Phase 1)
    FEATURE_ARITH_COMMAND, ///< (( expr )) as command
    FEATURE_LET_BUILTIN,   ///< let builtin command

    /// Extended Tests (Phase 2)
    FEATURE_EXTENDED_TEST, ///< [[ ]] extended test
    FEATURE_REGEX_MATCH,   ///< =~ regex matching
    FEATURE_PATTERN_MATCH, ///< == pattern matching in [[ ]]

    /// Process Substitution (Phase 3)
    FEATURE_PROCESS_SUBSTITUTION, ///< <(cmd) and >(cmd)
    FEATURE_PIPE_STDERR,          ///< |& pipe stderr too
    FEATURE_APPEND_BOTH,          ///< &>> append both streams
    FEATURE_COPROC,               ///< coproc command

    /// Extended Parameter Expansion (Phase 4)
    FEATURE_CASE_MODIFICATION,    ///< ${var^^}, ${var,,}
    FEATURE_SUBSTRING_EXPANSION,  ///< ${var:offset:length}
    FEATURE_PATTERN_SUBSTITUTION, ///< ${var/pattern/replacement}
    FEATURE_INDIRECT_EXPANSION,   ///< ${!var}, ${!prefix*}
    FEATURE_PARAM_TRANSFORMATION, ///< ${var@Q}, ${var@E}, etc.

    /// Extended Globbing
    FEATURE_EXTENDED_GLOB, ///< extglob: ?(pat), *(pat), etc.
    FEATURE_NULL_GLOB,     ///< Null glob: unmatched patterns expand to nothing
    FEATURE_DOT_GLOB,      ///< Include dotfiles in glob matches
    FEATURE_GLOBSTAR,      ///< ** matches recursively through directories

    /// Brace Expansion
    FEATURE_BRACE_EXPANSION, ///< {a,b,c} and {1..10} brace expansion

    /// Quoting Extensions
    FEATURE_ANSI_QUOTING,   ///< $'...' ANSI-C quoting with escape sequences
    FEATURE_LOCALE_QUOTING, ///< $"..." locale-aware quoting (gettext)

    /// Control Flow Extensions (Phase 5)
    FEATURE_CASE_FALLTHROUGH, ///< ;& and ;;& in case statements
    FEATURE_SELECT_LOOP,      ///< select var in list; do ... done
    FEATURE_TIME_KEYWORD,     ///< time command with TIMEFORMAT

    /// Behavior Defaults
    FEATURE_WORD_SPLIT_DEFAULT, ///< Word splitting on by default (Bash)
    FEATURE_AUTO_CD,            ///< Auto-cd to directories without cd command
    FEATURE_AUTO_PUSHD,         ///< Auto-push directories to stack on cd
    FEATURE_CDABLE_VARS,        ///< Treat unset vars as directory names for cd
    FEATURE_ERREXIT_IN_LOOPS,   ///< Loop body's first non-zero exit aborts the
                                ///< loop. Curated lush-mode default; off in
                                ///< POSIX/bash/zsh modes for polyglot parity.
                                ///< Toggleable per-script via setopt/unsetopt.
    FEATURE_XPG_ECHO, ///< echo interprets \n, \t, etc. by default (XSI/zsh
                      ///< behavior); when false, echo prints args literally
                      ///< unless `-e` is given (bash default). Bridged via
                      ///< `xpg_echo` (bash shopt name) and inverted alias
                      ///< `bsd_echo` (zsh setopt name).

    /// History Behavior
    FEATURE_HISTAPPEND,         ///< Append to history file instead of overwrite
    FEATURE_INC_APPEND_HISTORY, ///< Append each command immediately (better
                                ///< crash recovery)
    FEATURE_SHARE_HISTORY,      ///< Share history between concurrent sessions
    FEATURE_HIST_VERIFY,        ///< Verify history expansion before execution
    FEATURE_CHECKJOBS,          ///< Warn about running jobs on exit

    /// Function Enhancements (Phase 6)
    FEATURE_NAMEREF,             ///< local -n nameref variables
    FEATURE_ANONYMOUS_FUNCTIONS, ///< Zsh () { } anonymous functions
    FEATURE_RETURN_ANYWHERE,     ///< return from sourced scripts

    /// Zsh-Specific (Phase 7)
    FEATURE_GLOB_QUALIFIERS,    ///< Zsh glob qualifiers: *(.) *(/)
    FEATURE_HOOK_FUNCTIONS,     ///< precmd, preexec, chpwd hooks
    FEATURE_SIMPLE_HOOK_ARRAYS, ///< precmd+=(fn) in addition to
                                ///< precmd_functions+=(fn)
    FEATURE_PROMPT_COMMAND,     ///< Bash PROMPT_COMMAND (string and array)
    FEATURE_ZSH_PARAM_FLAGS,    ///< Zsh-style parameter flags
    FEATURE_ZSH_BARE_SUBSCRIPT, ///< Zsh bare-$var[N] subscript (vs ${var[N]})
    FEATURE_ZSH_PRINT_BUILTIN,  ///< Zsh `print` builtin (-l/-n/-r/-u/-f)
    FEATURE_PLUGIN_SYSTEM,      ///< Dynamic plugin loading system
    FEATURE_KIND_SIGILS,        ///< Top-level @name / %name sigils for vector
                                ///< and pair presentation contexts.  Stealing
                                ///< @ and % from word characters at word-start
                                ///< position is a lush divergence: curated true
                                ///< in lush mode, false everywhere else so
                                ///< `@reboot`, `user@host`, and `make %.o:%.c`
                                ///< keep their bare-word reading.

    FEATURE_UNICODE_IDENTIFIERS, ///< Accept letter codepoints beyond ASCII as
                                 ///< valid identifier characters (variables,
                                 ///< functions, aliases). When off, identifiers
                                 ///< follow the POSIX rule
                                 ///< [A-Za-z_][A-Za-z0-9_]*. When on,
                                 ///< identifier Start positions accept any
                                 ///< codepoint LLE's Unicode-letter test
                                 ///< recognizes, and Continue positions also
                                 ///< accept any codepoint LLE's Unicode-alnum
                                 ///< test recognizes; identifier names are NFC-
                                 ///< normalized on ingest. Default true in
                                 ///< lush mode, false elsewhere.

    /// Sentinel - must be last
    FEATURE_COUNT ///< Number of features (for array sizing)
} shell_feature_t;

/* ============================================================================
 * Shell Mode State
 * ============================================================================
 */

/**
 * @brief Shell mode state structure
 *
 * Tracks the current shell mode and any per-feature overrides that the user
 * has configured. This allows features to be individually enabled or disabled
 * regardless of the current mode.
 */
typedef struct {
    shell_mode_t current_mode;                ///< Active shell mode
    bool feature_overrides[FEATURE_COUNT];    ///< Override values per feature
    bool feature_override_set[FEATURE_COUNT]; ///< Which features are overridden
    bool strict_mode;                         ///< Disallow runtime mode changes
} shell_mode_state_t;

/** @brief Global shell mode state */
extern shell_mode_state_t g_shell_mode_state;

/* ============================================================================
 * Mode Query Functions
 * ============================================================================
 */

/**
 * @brief Check if a feature is allowed in the current mode
 *
 * This is the primary query function used throughout the codebase to determine
 * if a particular feature should be active. It checks both the current mode's
 * default and any user overrides.
 *
 * @param feature Feature to check
 * @return true if feature is enabled, false otherwise
 */
bool shell_mode_allows(shell_feature_t feature);

/**
 * @brief Check if currently in a specific mode
 *
 * @param mode Mode to check against
 * @return true if current mode matches
 */
bool shell_mode_is(shell_mode_t mode);

/**
 * @brief Get the current shell mode
 *
 * @return Current shell mode
 */
shell_mode_t shell_mode_get(void);

/**
 * @brief Set the shell mode
 *
 * Changes the current shell mode. All feature queries will use the new mode's
 * defaults (unless individually overridden). This may fail if strict_mode is
 * enabled.
 *
 * @param mode New mode to set
 * @return true on success, false if mode change is disallowed
 */
bool shell_mode_set(shell_mode_t mode);

/**
 * @brief Apply a mode preset across all configuration surfaces
 *
 * The canonical entry point for switching the active shell mode. Performs
 * a full re-seed:
 *   - Sets the active mode via shell_mode_set().
 *   - Clears any per-feature overrides (mode-change is a clean reset).
 *   - Updates legacy POSIX bookkeeping (shell_opts.posix_mode).
 *   - Writes the canonical mode label to the central registry
 *     (shell.mode key).
 *
 * Per-mode registry default re-seeding is layered on top of this in
 * commit 4 of the configuration cleanup; for now the surface is the
 * feature matrix + POSIX bookkeeping + the shell.mode label.
 *
 * @param mode Mode preset to apply
 * @return true on success, false if mode change is disallowed (strict mode)
 *         or the mode value is invalid
 */
bool apply_mode_preset(shell_mode_t mode);

/* ============================================================================
 * Feature Override Functions
 * ============================================================================
 */

/**
 * @brief Enable a specific feature regardless of mode
 *
 * Overrides the mode default to enable a feature.
 *
 * @param feature Feature to enable
 */
void shell_feature_enable(shell_feature_t feature);

/**
 * @brief Disable a specific feature regardless of mode
 *
 * Overrides the mode default to disable a feature.
 *
 * @param feature Feature to disable
 */
void shell_feature_disable(shell_feature_t feature);

/**
 * @brief Reset a feature to the mode default
 *
 * Removes any override for the feature, returning to the current mode's
 * default setting.
 *
 * @param feature Feature to reset
 */
void shell_feature_reset(shell_feature_t feature);

/**
 * @brief Reset all feature overrides
 *
 * Removes all user overrides, returning all features to their mode defaults.
 */
void shell_feature_reset_all(void);

/**
 * @brief Check if a feature has a user override
 *
 * @param feature Feature to check
 * @return true if feature has an override set
 */
bool shell_feature_is_overridden(shell_feature_t feature);

/* ============================================================================
 * Mode Information Functions
 * ============================================================================
 */

/**
 * @brief Get the name of a shell mode
 *
 * Returns a human-readable string for the mode.
 *
 * @param mode Mode to get name for
 * @return Mode name string (static, do not free)
 */
const char *shell_mode_name(shell_mode_t mode);

/**
 * @brief Get the name of a feature
 *
 * Returns a human-readable string for the feature.
 *
 * @param feature Feature to get name for
 * @return Feature name string (static, do not free)
 */
const char *shell_feature_name(shell_feature_t feature);

/**
 * @brief Get a feature's default value for a specific mode
 *
 * Returns what the feature's value would be in the given mode without
 * considering any overrides.
 *
 * @param mode Mode to check
 * @param feature Feature to check
 * @return Default value for feature in mode
 */
bool shell_mode_feature_default(shell_mode_t mode, shell_feature_t feature);

/**
 * @brief Parse a mode name string
 *
 * Converts a string like "posix", "bash", "zsh", or "lush" to the
 * corresponding shell_mode_t value.
 *
 * @param name Mode name to parse
 * @param mode Output parameter for parsed mode
 * @return true on success, false if name is not recognized
 */
bool shell_mode_parse(const char *name, shell_mode_t *mode);

/**
 * @brief Parse a feature name string
 *
 * Converts a feature name string to the corresponding shell_feature_t value.
 * Accepts both full names ("indexed_arrays") and short names ("arrays").
 *
 * Some aliases are *inverted*: `setopt <inverted-alias>` is semantically
 * equivalent to `unsetopt <canonical>`. For example, `bsd_echo` (zsh
 * setopt name) is an inverted alias for the canonical `xpg_echo` —
 * `setopt bsd_echo` disables XSI escape interpretation; `setopt xpg_echo`
 * enables it. Pass a non-NULL `invert` to receive that flag; callers that
 * don't care (e.g. test code, config readers that already know the
 * canonical name) may pass NULL.
 *
 * @param name Feature name to parse
 * @param feature Output parameter for parsed feature
 * @param invert Optional output: true if name semantically inverts the
 *               feature (caller should flip enable/disable). May be NULL.
 * @return true on success, false if name is not recognized
 */
/**
 * @brief True if @p name is a zsh-compat option name lush silently accepts.
 *
 * For names whose effect is either always-on or always-off in lush
 * (e.g., zsh's `prompt_subst`, `menu_complete`) and that would
 * otherwise trip `setopt`/`unsetopt` with "unknown option". The
 * builtins consult this before erroring.
 */
bool shell_feature_is_noop_alias(const char *name);

/**
 * @brief Record a setopt / unsetopt call for a noop alias.
 *
 * The behavior the alias would toggle is already always-on in lush;
 * recording the user's set/unset call lets `[[ -o name ]]` and
 * `setopt | grep name` return the right answer.
 *
 * Names not in the noop-alias list are ignored. Pass @p enabled=true for
 * `setopt`, false for `unsetopt`.
 */
void shell_feature_record_noop_alias_state(const char *name, bool enabled);

/**
 * @brief Query the recorded state of a noop-alias option.
 *
 * Returns false if @p name is not a noop alias. Defaults to true for any
 * alias that hasn't been explicitly unset (the underlying behavior is
 * always-on; the implicit answer matches that).
 */
bool shell_feature_noop_alias_is_enabled(const char *name);

bool shell_feature_parse(const char *name, shell_feature_t *feature,
                         bool *invert);

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================
 */

/**
 * @brief Initialize the shell mode system
 *
 * Sets up the shell mode state with default values. Should be called early
 * in shell initialization, before any feature queries.
 */
void shell_mode_init(void);

/**
 * @brief Clean up the shell mode system
 *
 * Frees any resources associated with the shell mode system.
 */
void shell_mode_cleanup(void);

/**
 * @brief Enable strict mode
 *
 * When strict mode is enabled, the shell mode cannot be changed at runtime.
 * This is useful for scripts that require a specific mode.
 *
 * @param strict true to enable strict mode
 */
void shell_mode_set_strict(bool strict);

/**
 * @brief Check if strict mode is enabled
 *
 * @return true if strict mode is enabled
 */
bool shell_mode_is_strict(void);

/* ============================================================================
 * Shebang Detection
 * ============================================================================
 */

/**
 * @brief Detect shell mode from shebang line
 *
 * Parses a shebang line (e.g., "#!/bin/bash") and returns the appropriate
 * shell mode. Used for automatic mode selection when executing scripts.
 *
 * @param shebang The shebang line (including #!)
 * @param mode Output parameter for detected mode
 * @return true if a mode was detected, false otherwise
 */
bool shell_mode_detect_from_shebang(const char *shebang, shell_mode_t *mode);

/* ============================================================================
 * Debugging and Introspection
 * ============================================================================
 */

/**
 * @brief Print current mode and feature states
 *
 * Outputs the current shell mode and the state of all features to stderr.
 * Useful for debugging configuration issues.
 */
void shell_mode_debug_print(void);

/**
 * @brief Get feature state summary as string
 *
 * Returns a formatted string showing the state of a feature, including
 * whether it's using the mode default or has an override.
 *
 * @param feature Feature to describe
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written
 */
int shell_feature_describe(shell_feature_t feature, char *buffer, size_t size);

#endif /// SHELL_MODE_H
