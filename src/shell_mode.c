/**
 * @file shell_mode.c
 * @brief Shell mode and feature flag system implementation
 *
 * Implements the multi-mode architecture that enables POSIX, Bash, Zsh, and
 * Lush-native shell modes. Contains the feature matrix that defines which
 * features are available in each mode.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "shell_mode.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/// Forward declaration for portability (see ht_fnv1a.c)
int strcasecmp(const char *s1, const char *s2);

/* ============================================================================
 * Global State
 * ============================================================================
 */

/** @brief Global shell mode state instance */
shell_mode_state_t g_shell_mode_state = {.current_mode = SHELL_MODE_LUSH,
                                         .feature_overrides = {false},
                                         .feature_override_set = {false},
                                         .strict_mode = false};

/* ============================================================================
 * Feature Matrix
 * ============================================================================
 *
 * This matrix defines the default value for each feature in each mode.
 * The Lush mode cherry-picks the best defaults from Bash and Zsh.
 */

static const bool feature_matrix[SHELL_MODE_COUNT][FEATURE_COUNT] = {
    /// SHELL_MODE_POSIX - Strict POSIX sh compliance
    [SHELL_MODE_POSIX] =
        {
                            /// Arrays - not in POSIX
            [FEATURE_INDEXED_ARRAYS] = false,
                            [FEATURE_ASSOCIATIVE_ARRAYS] = false,
                            [FEATURE_ARRAY_ZERO_INDEXED] =
                true, /// N/A but default to sane value
            [FEATURE_ARRAY_APPEND] = false,

                            /// Arithmetic
            [FEATURE_ARITH_COMMAND] = false, /// (( )) not in POSIX
            [FEATURE_LET_BUILTIN] = false,

                            /// Extended Tests
            [FEATURE_EXTENDED_TEST] = false, /// [[ ]] not in POSIX
            [FEATURE_REGEX_MATCH] = false,
                            [FEATURE_PATTERN_MATCH] = false,

                            /// Process Substitution
            [FEATURE_PROCESS_SUBSTITUTION] = false,
                            [FEATURE_PIPE_STDERR] = false,
                            [FEATURE_APPEND_BOTH] = false,
                            [FEATURE_COPROC] = false,

                            /// Extended Parameter Expansion
            [FEATURE_CASE_MODIFICATION] = false,
                            [FEATURE_SUBSTRING_EXPANSION] = false,
                            [FEATURE_PATTERN_SUBSTITUTION] = false,
                            [FEATURE_INDIRECT_EXPANSION] = false,
                            [FEATURE_PARAM_TRANSFORMATION] = false,

                            /// Extended Globbing
            [FEATURE_EXTENDED_GLOB] = false,
                            [FEATURE_NULL_GLOB] = false,
                            [FEATURE_DOT_GLOB] = false,
                            [FEATURE_GLOBSTAR] = false, /// POSIX doesn't have ** recursive glob

            /// Brace Expansion
            [FEATURE_BRACE_EXPANSION] =
                false, /// POSIX doesn't have brace expansion

            /// Quoting Extensions
            [FEATURE_ANSI_QUOTING] = false,   /// POSIX doesn't have $'...'
            [FEATURE_LOCALE_QUOTING] = false, /// POSIX doesn't have $"..."

            /// Control Flow Extensions
            [FEATURE_CASE_FALLTHROUGH] = false,
                            [FEATURE_SELECT_LOOP] = false,
                            [FEATURE_TIME_KEYWORD] = false,

                            /// Behavior Defaults
            [FEATURE_WORD_SPLIT_DEFAULT] =
                true, /// POSIX requires word splitting
            [FEATURE_AUTO_CD] = false,
                            [FEATURE_AUTO_PUSHD] = false,
                            [FEATURE_CDABLE_VARS] = false,
                            [FEATURE_ERREXIT_IN_LOOPS] =
                false, /// POSIX permits failing loop bodies; do not deviate
            [FEATURE_ASSIGN_ERROR_EXITS] =
                true, /// POSIX 2.8.1: assignment error exits non-interactive sh
            [FEATURE_ZSH_PARAM_MODIFIERS] =
                false,                 /// ${var:x} is substring-offset in POSIX
            [FEATURE_XPG_ECHO] = true, /// POSIX XSI mandates escape interp

            /// History Behavior
            [FEATURE_HISTAPPEND] = false,
                            [FEATURE_INC_APPEND_HISTORY] = false,
                            [FEATURE_SHARE_HISTORY] = false,
                            [FEATURE_HIST_VERIFY] = false,
                            [FEATURE_CHECKJOBS] = false,

                            /// Function Enhancements
            [FEATURE_NAMEREF] = false,
                            [FEATURE_ANONYMOUS_FUNCTIONS] = false,
                            [FEATURE_RETURN_ANYWHERE] = false,

                            /// Zsh-Specific
            [FEATURE_GLOB_QUALIFIERS] = false,
                            [FEATURE_HOOK_FUNCTIONS] = false,
                            [FEATURE_SIMPLE_HOOK_ARRAYS] = false,
                            [FEATURE_PROMPT_COMMAND] = false, /// Not in POSIX
            [FEATURE_ZSH_PARAM_FLAGS] = false,
                            [FEATURE_ZSH_BARE_SUBSCRIPT] = false, /// No arrays in POSIX
            [FEATURE_ZSH_PRINT_BUILTIN] = false,  /// Not a POSIX builtin
            [FEATURE_PLUGIN_SYSTEM] = false,
                            [FEATURE_KIND_SIGILS] = false,
                            [FEATURE_UNICODE_IDENTIFIERS] = false,
                            [FEATURE_REJECT_MIXED_SCRIPT_IDENTS] = false,
                            },

    /// SHELL_MODE_BASH - Bash 5.x compatibility
    [SHELL_MODE_BASH] =
        {
                            /// Arrays
            [FEATURE_INDEXED_ARRAYS] = true,
                            [FEATURE_ASSOCIATIVE_ARRAYS] = true,
                            [FEATURE_ARRAY_ZERO_INDEXED] = true, /// Bash arrays are 0-indexed
            [FEATURE_ARRAY_APPEND] = true,

                            /// Arithmetic
            [FEATURE_ARITH_COMMAND] = true,
                            [FEATURE_LET_BUILTIN] = true,

                            /// Extended Tests
            [FEATURE_EXTENDED_TEST] = true,
                            [FEATURE_REGEX_MATCH] = true,
                            [FEATURE_PATTERN_MATCH] = true,

                            /// Process Substitution
            [FEATURE_PROCESS_SUBSTITUTION] = true,
                            [FEATURE_PIPE_STDERR] = true,
                            [FEATURE_APPEND_BOTH] = true,
                            [FEATURE_COPROC] = true,

                            /// Extended Parameter Expansion
            [FEATURE_CASE_MODIFICATION] = true,
                            [FEATURE_SUBSTRING_EXPANSION] = true,
                            [FEATURE_PATTERN_SUBSTITUTION] = true,
                            [FEATURE_INDIRECT_EXPANSION] = true,
                            [FEATURE_PARAM_TRANSFORMATION] = true,

                            /// Extended Globbing - off by default in Bash, use shopt extglob
            [FEATURE_EXTENDED_GLOB] = false,
                            [FEATURE_NULL_GLOB] = false,
                            [FEATURE_DOT_GLOB] = false,
                            [FEATURE_GLOBSTAR] = false, /// shopt globstar, off by default

            /// Brace Expansion
            [FEATURE_BRACE_EXPANSION] =
                true, /// Bash has brace expansion on by default

            /// Quoting Extensions
            [FEATURE_ANSI_QUOTING] = true,   /// Bash supports $'...'
            [FEATURE_LOCALE_QUOTING] = true, /// Bash supports $"..."

            /// Control Flow Extensions
            [FEATURE_CASE_FALLTHROUGH] = true,
                            [FEATURE_SELECT_LOOP] = true,
                            [FEATURE_TIME_KEYWORD] = true,

                            /// Behavior Defaults
            [FEATURE_WORD_SPLIT_DEFAULT] =
                true,                  /// Bash does word splitting by default
            [FEATURE_AUTO_CD] = false, /// shopt autocd, off by default
            [FEATURE_AUTO_PUSHD] = false,
                            [FEATURE_CDABLE_VARS] = false,
                            [FEATURE_ERREXIT_IN_LOOPS] =
                false, /// bash permits failing loop bodies; preserve parity
            [FEATURE_ASSIGN_ERROR_EXITS] =
                false, /// bash continues after a readonly assignment error,
                       /// aborting only the current AND-OR list
            [FEATURE_ZSH_PARAM_MODIFIERS] =
                false,                  /// ${var:x} is substring-offset in bash
            [FEATURE_XPG_ECHO] = false, /// shopt xpg_echo, off by default

            /// History Behavior
            [FEATURE_HISTAPPEND] = true,          /// Bash default on
            [FEATURE_INC_APPEND_HISTORY] = false, /// Bash: off by default
            [FEATURE_SHARE_HISTORY] = false,      /// Bash doesn't have this
            [FEATURE_HIST_VERIFY] = false,        /// shopt histverify, off
            [FEATURE_CHECKJOBS] = false,          /// shopt checkjobs, off

            /// Function Enhancements
            [FEATURE_NAMEREF] = true,
                            [FEATURE_ANONYMOUS_FUNCTIONS] = false, /// Bash doesn't have these
            [FEATURE_RETURN_ANYWHERE] = true,

                            /// Zsh-Specific
            [FEATURE_GLOB_QUALIFIERS] = false,
                            [FEATURE_HOOK_FUNCTIONS] =
                false, /// Bash has PROMPT_COMMAND instead
            [FEATURE_SIMPLE_HOOK_ARRAYS] = false, /// Bash uses PROMPT_COMMAND
            [FEATURE_PROMPT_COMMAND] =
                true, /// Bash 5.1+ supports string and array
            [FEATURE_ZSH_PARAM_FLAGS] = false,
                            [FEATURE_ZSH_BARE_SUBSCRIPT] =
                false, /// Bash treats $a[N] as $a + literal [N]
            [FEATURE_ZSH_PRINT_BUILTIN] = false, /// Bash has no `print` builtin
            [FEATURE_PLUGIN_SYSTEM] = false,     /// Not a Bash feature
            [FEATURE_KIND_SIGILS] = false, /// @/% remain word chars in bash
            [FEATURE_UNICODE_IDENTIFIERS] = false, /// Bash ASCII-only ident
            [FEATURE_REJECT_MIXED_SCRIPT_IDENTS] = false, /// opt-in only
        },

    /// SHELL_MODE_ZSH - Zsh compatibility
    [SHELL_MODE_ZSH] =
        {
                            /// Arrays
            [FEATURE_INDEXED_ARRAYS] = true,
                            [FEATURE_ASSOCIATIVE_ARRAYS] = true,
                            [FEATURE_ARRAY_ZERO_INDEXED] = false, /// Zsh arrays are 1-indexed
            [FEATURE_ARRAY_APPEND] = true,

                            /// Arithmetic
            [FEATURE_ARITH_COMMAND] = true,
                            [FEATURE_LET_BUILTIN] = true,

                            /// Extended Tests
            [FEATURE_EXTENDED_TEST] = true,
                            [FEATURE_REGEX_MATCH] = true,
                            [FEATURE_PATTERN_MATCH] = true,

                            /// Process Substitution
            [FEATURE_PROCESS_SUBSTITUTION] = true,
                            [FEATURE_PIPE_STDERR] = true,
                            [FEATURE_APPEND_BOTH] = true,
                            [FEATURE_COPROC] = true,

                            /// Extended Parameter Expansion
            [FEATURE_CASE_MODIFICATION] = true,
                            [FEATURE_SUBSTRING_EXPANSION] = true,
                            [FEATURE_PATTERN_SUBSTITUTION] = true,
                            [FEATURE_INDIRECT_EXPANSION] = true,
                            [FEATURE_PARAM_TRANSFORMATION] = true,

                            /// Extended Globbing - on by default in Zsh
            [FEATURE_EXTENDED_GLOB] = true,
                            [FEATURE_NULL_GLOB] = true, /// CSH_NULL_GLOB behavior
            [FEATURE_DOT_GLOB] = false, /// GLOB_DOTS off by default
            [FEATURE_GLOBSTAR] = true,  /* ** recursive glob on by default */

            /// Brace Expansion
            [FEATURE_BRACE_EXPANSION] =
                true, /// Zsh has brace expansion on by default

            /// Quoting Extensions
            [FEATURE_ANSI_QUOTING] = true,    /// Zsh supports $'...'
            [FEATURE_LOCALE_QUOTING] = false, /// Zsh doesn't have $"..."

            /// Control Flow Extensions
            [FEATURE_CASE_FALLTHROUGH] = true,
                            [FEATURE_SELECT_LOOP] = true,
                            [FEATURE_TIME_KEYWORD] = true,

                            /// Behavior Defaults
            [FEATURE_WORD_SPLIT_DEFAULT] =
                false,                 /// Zsh doesn't word-split by default
            [FEATURE_AUTO_CD] = false, /// AUTO_CD option, off by default
            [FEATURE_AUTO_PUSHD] = false,
                            [FEATURE_CDABLE_VARS] = false,
                            [FEATURE_ERREXIT_IN_LOOPS] =
                false, /// zsh permits failing loop bodies; preserve parity
            [FEATURE_ASSIGN_ERROR_EXITS] =
                true, /// zsh exits a non-interactive shell on a readonly
                      /// assignment error, like dash; match it in zsh mode
            [FEATURE_ZSH_PARAM_MODIFIERS] = true, /// native zsh modifiers
            [FEATURE_XPG_ECHO] = true, /* zsh: BSD_ECHO off → escapes interp'd
                            by default in zsh's echo builtin */

            /// History Behavior
            [FEATURE_HISTAPPEND] = true, /// APPEND_HISTORY
            [FEATURE_INC_APPEND_HISTORY] =
                true, /// INC_APPEND_HISTORY on by default
            [FEATURE_SHARE_HISTORY] = false, /// SHARE_HISTORY off by default
            [FEATURE_HIST_VERIFY] = false,   /// HIST_VERIFY off by default
            [FEATURE_CHECKJOBS] = false,     /// CHECK_JOBS off by default

            /// Function Enhancements
            [FEATURE_NAMEREF] = true,
                            [FEATURE_ANONYMOUS_FUNCTIONS] = true,
                            [FEATURE_RETURN_ANYWHERE] = true,

                            /// Zsh-Specific
            [FEATURE_GLOB_QUALIFIERS] = true,
                            [FEATURE_HOOK_FUNCTIONS] = true,
                            [FEATURE_SIMPLE_HOOK_ARRAYS] = true, /// Zsh supports precmd+=(fn)
            [FEATURE_PROMPT_COMMAND] = false,    /// Zsh uses precmd instead
            [FEATURE_ZSH_PARAM_FLAGS] = true,
                            [FEATURE_ZSH_BARE_SUBSCRIPT] = true, /// Zsh native
            [FEATURE_ZSH_PRINT_BUILTIN] = true,  /// Zsh native builtin
            [FEATURE_PLUGIN_SYSTEM] = false,     /// Not a Zsh feature
            [FEATURE_KIND_SIGILS] = false, /// @/% remain word chars in zsh
            [FEATURE_UNICODE_IDENTIFIERS] = false, /// Zsh ASCII-only ident
            [FEATURE_REJECT_MIXED_SCRIPT_IDENTS] = false, /// opt-in only
        },

    /// SHELL_MODE_LUSH - Curated best of both (DEFAULT)
    [SHELL_MODE_LUSH] =
        {
                            /// Arrays - full support, 0-indexed like Bash (more intuitive)
            [FEATURE_INDEXED_ARRAYS] = true,
                            [FEATURE_ASSOCIATIVE_ARRAYS] = true,
                            [FEATURE_ARRAY_ZERO_INDEXED] =
                true, /// 0-indexed like Bash, C, Python
            [FEATURE_ARRAY_APPEND] = true,

                            /// Arithmetic - full support
            [FEATURE_ARITH_COMMAND] = true,
                            [FEATURE_LET_BUILTIN] = true,

                            /// Extended Tests - full support
            [FEATURE_EXTENDED_TEST] = true,
                            [FEATURE_REGEX_MATCH] = true,
                            [FEATURE_PATTERN_MATCH] = true,

                            /// Process Substitution - full support
            [FEATURE_PROCESS_SUBSTITUTION] = true,
                            [FEATURE_PIPE_STDERR] = true,
                            [FEATURE_APPEND_BOTH] = true,
                            [FEATURE_COPROC] = true,

                            /// Extended Parameter Expansion - full support
            [FEATURE_CASE_MODIFICATION] = true,
                            [FEATURE_SUBSTRING_EXPANSION] = true,
                            [FEATURE_PATTERN_SUBSTITUTION] = true,
                            [FEATURE_INDIRECT_EXPANSION] = true,
                            [FEATURE_PARAM_TRANSFORMATION] = true,

                            /// Extended Globbing - on like Zsh (more powerful)
            [FEATURE_EXTENDED_GLOB] = true,
                            [FEATURE_NULL_GLOB] = true, /// Safer: no literal *.foo
            [FEATURE_DOT_GLOB] = false, /// Explicit is better
            [FEATURE_GLOBSTAR] = true,  /* ** recursive glob - very useful */

            /// Brace Expansion
            [FEATURE_BRACE_EXPANSION] = true, /// Essential shell feature

            /// Quoting Extensions
            [FEATURE_ANSI_QUOTING] = true,    /// Very useful, widely expected
            [FEATURE_LOCALE_QUOTING] = false, /// Niche use, Bash-only

            /// Control Flow Extensions - full support
            [FEATURE_CASE_FALLTHROUGH] = true,
                            [FEATURE_SELECT_LOOP] = true,
                            [FEATURE_TIME_KEYWORD] = true,

                            /// Behavior Defaults - Zsh's safer defaults
            [FEATURE_WORD_SPLIT_DEFAULT] = false, /// Zsh behavior: safer
            [FEATURE_AUTO_CD] = true,             /// Convenience feature
            [FEATURE_AUTO_PUSHD] = false,         /// Optional
            [FEATURE_CDABLE_VARS] = false,        /// Optional
            [FEATURE_ERREXIT_IN_LOOPS] =
                true, /* Curated lush-mode default: a loop body that fails
                            on its first iteration is almost always a
                            programmer error; aborting immediately catches the
                            bug instantly instead of waiting for the
                            failure-streak detector (#73 Layer 1) to fire after
                            5+ seconds. Off in POSIX/bash/zsh modes for
                            polyglot parity. Toggle per-script via
                            `unsetopt errexit_in_loops`. */
            [FEATURE_ASSIGN_ERROR_EXITS] =
                false, /* Curated lush default: a readonly assignment error
                            aborts the current AND-OR list and is reported on
                            stderr, but does not tear down the whole session.
                            Exiting the shell on one bad assignment is hostile
                            in an interactive-first shell; the abort already
                            stops the dangerous `||` fallthrough. Opt into the
                            strict posix/zsh behavior with
                            `setopt assign_error_exits`. */
            [FEATURE_ZSH_PARAM_MODIFIERS] =
                true, /* Curated: the zsh ${var:h/t/r/e/...} modifiers are a
                            genuinely useful, terse path/string toolkit, and a
                            leading modifier letter never collides with a valid
                            substring offset (which is numeric). On in lush. */
            [FEATURE_XPG_ECHO] =
                true, /* Curated zsh-style: echo interprets escapes by
                            default. More predictable and modern than bash's
                            literal-by-default — `echo "\n"` produces a newline
                            consistently. Use `unsetopt xpg_echo` or
                            `setopt bsd_echo` for bash-style literal. */

            /// History Behavior - better defaults
            [FEATURE_HISTAPPEND] = true,         /// Preserve history
            [FEATURE_INC_APPEND_HISTORY] = true, /// Better crash recovery
            [FEATURE_SHARE_HISTORY] = false,     /// Can be confusing, opt-in
            [FEATURE_HIST_VERIFY] = false,       /// Slows workflow, opt-in
            [FEATURE_CHECKJOBS] = true, /// Prevents accidental job loss

            /// Function Enhancements - full support
            [FEATURE_NAMEREF] = true,
                            [FEATURE_ANONYMOUS_FUNCTIONS] = true, /// From Zsh: powerful
            [FEATURE_RETURN_ANYWHERE] = true,

                            /// Zsh-Specific - selective adoption
            [FEATURE_GLOB_QUALIFIERS] = true, /// Powerful feature
            [FEATURE_HOOK_FUNCTIONS] = true,  /// Essential for prompts
            [FEATURE_SIMPLE_HOOK_ARRAYS] =
                true,                         /// precmd+=(fn) works intuitively
            [FEATURE_PROMPT_COMMAND] = true,  /// Bash compat: string and array
            [FEATURE_ZSH_PARAM_FLAGS] = true, /// Now implemented: ${(U)var} etc
            [FEATURE_ZSH_BARE_SUBSCRIPT] =
                true, /// Curated: zsh native, no bash conflict
            [FEATURE_ZSH_PRINT_BUILTIN] = true, /* Curated: -P prompt-expansion
                            uses lush template engine */
            [FEATURE_PLUGIN_SYSTEM] = true,     /// Lush extension
            [FEATURE_KIND_SIGILS] = true,       /// Curated: @/% sigils
            [FEATURE_UNICODE_IDENTIFIERS] = true, /// Curated: lush default
            /// Curated OFF, lush included: the canonical mode stays
            /// permissive (presets are not restrictions) and surfaces
            /// mixed-script identifiers as a `debug analyze` advisory
            /// rather than a hard error. Opt in for a hardening posture.
            [FEATURE_REJECT_MIXED_SCRIPT_IDENTS] = false,
                            },
};

/* ============================================================================
 * Mode Name Table
 * ============================================================================
 */

static const char *mode_names[SHELL_MODE_COUNT] = {
    [SHELL_MODE_POSIX] = "posix",
    [SHELL_MODE_BASH] = "bash",
    [SHELL_MODE_ZSH] = "zsh",
    [SHELL_MODE_LUSH] = "lush",
};

/* ============================================================================
 * Feature Name Table
 * ============================================================================
 */

static const char *feature_names[FEATURE_COUNT] = {
    /// Arrays
    [FEATURE_INDEXED_ARRAYS] = "indexed_arrays",
    [FEATURE_ASSOCIATIVE_ARRAYS] = "associative_arrays",
    [FEATURE_ARRAY_ZERO_INDEXED] = "array_zero_indexed",
    [FEATURE_ARRAY_APPEND] = "array_append",

    /// Arithmetic
    [FEATURE_ARITH_COMMAND] = "arith_command",
    [FEATURE_LET_BUILTIN] = "let_builtin",

    /// Extended Tests
    [FEATURE_EXTENDED_TEST] = "extended_test",
    [FEATURE_REGEX_MATCH] = "regex_match",
    [FEATURE_PATTERN_MATCH] = "pattern_match",

    /// Process Substitution
    [FEATURE_PROCESS_SUBSTITUTION] = "process_substitution",
    [FEATURE_PIPE_STDERR] = "pipe_stderr",
    [FEATURE_APPEND_BOTH] = "append_both",
    [FEATURE_COPROC] = "coproc",

    /// Extended Parameter Expansion
    [FEATURE_CASE_MODIFICATION] = "case_modification",
    [FEATURE_SUBSTRING_EXPANSION] = "substring_expansion",
    [FEATURE_PATTERN_SUBSTITUTION] = "pattern_substitution",
    [FEATURE_INDIRECT_EXPANSION] = "indirect_expansion",
    [FEATURE_PARAM_TRANSFORMATION] = "param_transformation",

    /// Extended Globbing
    [FEATURE_EXTENDED_GLOB] = "extended_glob",
    [FEATURE_NULL_GLOB] = "null_glob",
    [FEATURE_DOT_GLOB] = "dot_glob",
    [FEATURE_GLOBSTAR] = "globstar",

    /// Brace Expansion
    [FEATURE_BRACE_EXPANSION] = "brace_expansion",

    /// Quoting Extensions
    [FEATURE_ANSI_QUOTING] = "ansi_quoting",
    [FEATURE_LOCALE_QUOTING] = "locale_quoting",

    /// Control Flow Extensions
    [FEATURE_CASE_FALLTHROUGH] = "case_fallthrough",
    [FEATURE_SELECT_LOOP] = "select_loop",
    [FEATURE_TIME_KEYWORD] = "time_keyword",

    /// Behavior Defaults
    [FEATURE_WORD_SPLIT_DEFAULT] = "word_split",
    [FEATURE_AUTO_CD] = "auto_cd",
    [FEATURE_AUTO_PUSHD] = "auto_pushd",
    [FEATURE_CDABLE_VARS] = "cdable_vars",
    [FEATURE_ERREXIT_IN_LOOPS] = "errexit_in_loops",
    [FEATURE_ASSIGN_ERROR_EXITS] = "assign_error_exits",
    [FEATURE_ZSH_PARAM_MODIFIERS] = "zsh_param_modifiers",
    [FEATURE_XPG_ECHO] = "xpg_echo",

    /// History Behavior
    [FEATURE_HISTAPPEND] = "histappend",
    [FEATURE_INC_APPEND_HISTORY] = "inc_append_history",
    [FEATURE_SHARE_HISTORY] = "share_history",
    [FEATURE_HIST_VERIFY] = "hist_verify",
    [FEATURE_CHECKJOBS] = "checkjobs",

    /// Function Enhancements
    [FEATURE_NAMEREF] = "nameref",
    [FEATURE_ANONYMOUS_FUNCTIONS] = "anonymous_functions",
    [FEATURE_RETURN_ANYWHERE] = "return_anywhere",

    /// Zsh-Specific
    [FEATURE_GLOB_QUALIFIERS] = "glob_qualifiers",
    [FEATURE_HOOK_FUNCTIONS] = "hook_functions",
    [FEATURE_SIMPLE_HOOK_ARRAYS] = "simple_hook_arrays",
    [FEATURE_PROMPT_COMMAND] = "prompt_command",
    [FEATURE_ZSH_PARAM_FLAGS] = "zsh_param_flags",
    [FEATURE_ZSH_BARE_SUBSCRIPT] = "zsh_bare_subscript",
    [FEATURE_ZSH_PRINT_BUILTIN] = "zsh_print_builtin",
    [FEATURE_PLUGIN_SYSTEM] = "plugin_system",
    [FEATURE_KIND_SIGILS] = "kind_sigils",
    [FEATURE_UNICODE_IDENTIFIERS] = "unicode_identifiers",
    [FEATURE_REJECT_MIXED_SCRIPT_IDENTS] = "reject_mixed_script_idents",
};

/* Feature short names and cross-shell aliases.
 *
 * `invert = true` marks an alias whose semantic is the INVERSE of the
 * canonical feature: `setopt <alias>` disables the feature, `unsetopt
 * <alias>` enables it. This lets one underlying flag bridge two shells'
 * opposite-named knobs (e.g. bash's `xpg_echo` and zsh's `BSD_ECHO`
 * control the same echo-escape behavior with inverted semantics).
 */
static const struct {
    const char *short_name;
    shell_feature_t feature;
    bool invert;
} feature_aliases[] = {
    {          "arrays",       FEATURE_INDEXED_ARRAYS, false},
    {           "assoc",   FEATURE_ASSOCIATIVE_ARRAYS, false},
    {         "exttest",        FEATURE_EXTENDED_TEST, false},
    {           "regex",          FEATURE_REGEX_MATCH, false},
    {         "procsub", FEATURE_PROCESS_SUBSTITUTION, false},
    {         "extglob",        FEATURE_EXTENDED_GLOB, false},
    {        "nullglob",            FEATURE_NULL_GLOB, false},
    {         "dotglob",             FEATURE_DOT_GLOB, false},
    {        "globstar",             FEATURE_GLOBSTAR, false},
    {        "braceexp",      FEATURE_BRACE_EXPANSION, false},
    {     "ansiquoting",         FEATURE_ANSI_QUOTING, false},
    {     "dollarquote",         FEATURE_ANSI_QUOTING, false},
    {          "autocd",              FEATURE_AUTO_CD, false},
    {       "wordsplit",   FEATURE_WORD_SPLIT_DEFAULT, false},
    {      "histappend",           FEATURE_HISTAPPEND, false},
    {"incappendhistory",   FEATURE_INC_APPEND_HISTORY, false},
    {    "sharehistory",        FEATURE_SHARE_HISTORY, false},
    {      "histverify",          FEATURE_HIST_VERIFY, false},
    {       "checkjobs",            FEATURE_CHECKJOBS, false},
    {         "plugins",        FEATURE_PLUGIN_SYSTEM, false},
    {         "xpgecho",             FEATURE_XPG_ECHO, false},
    /* zsh BSD_ECHO is the inverse of xpg_echo:
     * `setopt BSD_ECHO` disables escape interpretation. */
    {        "bsd_echo",             FEATURE_XPG_ECHO,  true},
    {         "bsdecho",             FEATURE_XPG_ECHO,  true},
    {              NULL,                            0, false}
};

/* ============================================================================
 * Zsh-compat no-op option names
 * ============================================================================
 *
 * Names that zsh accepts via `setopt`/`unsetopt` whose effect is
 * either always-on or always-off in lush (or supplanted by a different
 * lush surface). Accepted silently as no-ops so real-world zsh
 * scripts that call `setopt prompt_subst` in their init paths run
 * cleanly. Each entry is here with a one-line rationale; promote to
 * a real feature flag if the behavior ever needs to be toggleable.
 */
static const struct {
    const char *name;
    const char *rationale;
} feature_noop_aliases[] = {
    {     "prompt_subst","lush prompt always supports parameter / arith / "
"command expansion; no opt-in needed"                                             },
    {      "promptsubst",                                  "alias for prompt_subst"},
    {    "menu_complete",        "lush completion is always menu-shaped; no toggle"},
    {     "menucomplete",                                 "alias for menu_complete"},
    {    "always_to_end",         "completion always lands at end of inserted word"},
    {      "alwaystoend",                                 "alias for always_to_end"},
    {        "auto_menu",            "lush completion auto-menus on TAB; no toggle"},
    {         "automenu",                                     "alias for auto_menu"},
    { "complete_in_word",
     "lush completion always treats cursor mid-word as completable"                },
    {   "completeinword",                              "alias for complete_in_word"},
    {      "flowcontrol",                   "terminal flow-control toggle; lush LLE manages its own "
                   "tty raw-mode state"                   },
    {      "correct_all",        "lush has no spelling-correction prompt to toggle"},
    {       "correctall",                                   "alias for correct_all"},
    {"pushd_ignore_dups",
     "lush dirstack dedup behavior is benign and introspection-invisible"          },
    {  "pushdignoredups",                             "alias for pushd_ignore_dups"},
    {     "pushd_silent",       "lush pushd/popd never prints the stack; always-on"},
    {      "pushdsilent",                                  "alias for pushd_silent"},
    {    "pushd_to_home",                        "zsh-specific pushd-with-no-arg-goes-home; lush requires "
                        "an argument"                     },
    {      "pushdtohome",                                 "alias for pushd_to_home"},
    {          "multios",                       "zsh multiple-redirection-target option; lush has its own "
                       "redirection engine"               },
    {          "clobber",
     "zsh positive CLOBBER (inverse of POSIX noclobber); accepted "
     "as alias since unsetopt CLOBBER and set -o noclobber express the same "
     "intent. Full inversion wiring is a future refinement"                        },
    {             "beep",     "zsh line-editor terminal-bell-on-error; LLE has its own bell "
     "behavior and never gates on this option"            },
    {               NULL,                                                      NULL}
};

bool shell_feature_is_noop_alias(const char *name) {
    if (!name) {
        return false;
    }
    for (int i = 0; feature_noop_aliases[i].name != NULL; i++) {
        if (strcasecmp(name, feature_noop_aliases[i].name) == 0) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Noop-alias state recording
 * ============================================================================
 *
 * shell_feature_is_noop_alias names refer to behavior lush already provides
 * (or has supplanted); the underlying behavior is always-on, but real-world
 * scripts introspect option state via `[[ -o name ]]` and `setopt | grep
 * name`. Without recording the user's set/unset call, the introspection
 * returns the wrong answer (verified divergence vs zsh, 2026-05-25).
 *
 * The table below records the user-set state per alias name.  The "default"
 * for every alias is "set" because the underlying behavior is always-on; an
 * explicit `unsetopt name` flips it to false so introspection matches.
 * Names not in the alias list return false from the query.
 */

#define NOOP_ALIAS_MAX 64
static const char *noop_alias_recorded_names[NOOP_ALIAS_MAX];
static bool noop_alias_recorded_state[NOOP_ALIAS_MAX];
static size_t noop_alias_recorded_count = 0;

static size_t find_recorded_alias(const char *name) {
    for (size_t i = 0; i < noop_alias_recorded_count; i++) {
        if (strcasecmp(name, noop_alias_recorded_names[i]) == 0) {
            return i;
        }
    }
    return noop_alias_recorded_count;
}

void shell_feature_record_noop_alias_state(const char *name, bool enabled) {
    if (!name || !shell_feature_is_noop_alias(name)) {
        return;
    }
    size_t idx = find_recorded_alias(name);
    if (idx == noop_alias_recorded_count) {
        if (noop_alias_recorded_count >= NOOP_ALIAS_MAX) {
            return;
        }
        /// The table key points into feature_noop_aliases (lifetime: program).
        for (int i = 0; feature_noop_aliases[i].name != NULL; i++) {
            if (strcasecmp(name, feature_noop_aliases[i].name) == 0) {
                noop_alias_recorded_names[noop_alias_recorded_count] =
                    feature_noop_aliases[i].name;
                break;
            }
        }
        noop_alias_recorded_count++;
    }
    noop_alias_recorded_state[idx] = enabled;
}

bool shell_feature_noop_alias_is_enabled(const char *name) {
    if (!name || !shell_feature_is_noop_alias(name)) {
        return false;
    }
    size_t idx = find_recorded_alias(name);
    if (idx == noop_alias_recorded_count) {
        /// Default for a noop alias is "on" -- the underlying behavior is
        /// always provided, so a query before any setopt/unsetopt should
        /// return true (matches zsh's behavior for these always-on options).
        return true;
    }
    return noop_alias_recorded_state[idx];
}

/* ============================================================================
 * Mode Query Functions
 * ============================================================================
 */

bool shell_mode_allows(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return false;
    }

    /// Check for user override first
    if (g_shell_mode_state.feature_override_set[feature]) {
        return g_shell_mode_state.feature_overrides[feature];
    }

    /// Fall back to mode default
    return feature_matrix[g_shell_mode_state.current_mode][feature];
}

bool shell_mode_is(shell_mode_t mode) {
    return g_shell_mode_state.current_mode == mode;
}

shell_mode_t shell_mode_get(void) { return g_shell_mode_state.current_mode; }

bool shell_mode_set(shell_mode_t mode) {
    if (mode >= SHELL_MODE_COUNT) {
        return false;
    }

    if (g_shell_mode_state.strict_mode) {
        return false;
    }

    g_shell_mode_state.current_mode = mode;
    return true;
}

/* apply_mode_preset() is defined in src/posix_opts.c. It integrates
 * shell_mode, config, config_registry, and shell_opts; keeping the
 * implementation in posix_opts.c (which already pulls those headers)
 * preserves the lighter dependency graph for test binaries that link
 * shell_mode.c standalone. */

/* ============================================================================
 * Feature Override Functions
 * ============================================================================
 */

void shell_feature_enable(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return;
    }

    g_shell_mode_state.feature_overrides[feature] = true;
    g_shell_mode_state.feature_override_set[feature] = true;
}

void shell_feature_disable(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return;
    }

    g_shell_mode_state.feature_overrides[feature] = false;
    g_shell_mode_state.feature_override_set[feature] = true;
}

void shell_feature_reset(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return;
    }

    g_shell_mode_state.feature_override_set[feature] = false;
}

void shell_feature_reset_all(void) {
    for (int i = 0; i < FEATURE_COUNT; i++) {
        g_shell_mode_state.feature_override_set[i] = false;
    }
}

bool shell_feature_is_overridden(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return false;
    }

    return g_shell_mode_state.feature_override_set[feature];
}

/* ============================================================================
 * Mode Information Functions
 * ============================================================================
 */

const char *shell_mode_name(shell_mode_t mode) {
    if (mode >= SHELL_MODE_COUNT) {
        return "unknown";
    }

    return mode_names[mode];
}

const char *shell_feature_name(shell_feature_t feature) {
    if (feature >= FEATURE_COUNT) {
        return "unknown";
    }

    return feature_names[feature];
}

/// Bridge for liblle's completion module. liblle links standalone and so
/// cannot include shell_mode.h or call shell_feature_name() directly; it
/// reaches the feature matrix through these int-typed wrappers (declared
/// weak in src/lle/completion/completion_types.c). This strong definition
/// is linked only into the full shell, so setopt/unsetopt completion
/// enumerates the matrix itself -- the offered option list can never
/// drift from shell_mode.c. A standalone liblle build gets the weak
/// fallbacks (count 0 / name NULL) and simply offers no feature names.
int lle_shell_feature_count(void);
const char *lle_shell_feature_name(int index);

int lle_shell_feature_count(void) { return (int)FEATURE_COUNT; }

const char *lle_shell_feature_name(int index) {
    if (index < 0 || index >= (int)FEATURE_COUNT) {
        return NULL;
    }
    return shell_feature_name((shell_feature_t)index);
}

bool shell_mode_feature_default(shell_mode_t mode, shell_feature_t feature) {
    if (mode >= SHELL_MODE_COUNT || feature >= FEATURE_COUNT) {
        return false;
    }

    return feature_matrix[mode][feature];
}

bool shell_mode_parse(const char *name, shell_mode_t *mode) {
    if (!name || !mode) {
        return false;
    }

    for (int i = 0; i < SHELL_MODE_COUNT; i++) {
        if (strcasecmp(name, mode_names[i]) == 0) {
            *mode = (shell_mode_t)i;
            return true;
        }
    }

    /// Also accept common aliases
    if (strcasecmp(name, "sh") == 0) {
        *mode = SHELL_MODE_POSIX;
        return true;
    }

    return false;
}

bool shell_feature_parse(const char *name, shell_feature_t *feature,
                         bool *invert) {
    if (!name || !feature) {
        return false;
    }

    if (invert) {
        *invert = false;
    }

    /// Check full names
    for (int i = 0; i < FEATURE_COUNT; i++) {
        if (feature_names[i] && strcasecmp(name, feature_names[i]) == 0) {
            *feature = (shell_feature_t)i;
            return true;
        }
    }

    /// Check aliases
    for (int i = 0; feature_aliases[i].short_name != NULL; i++) {
        if (strcasecmp(name, feature_aliases[i].short_name) == 0) {
            *feature = feature_aliases[i].feature;
            if (invert) {
                *invert = feature_aliases[i].invert;
            }
            return true;
        }
    }

    return false;
}

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================
 */

void shell_mode_init(void) {
    g_shell_mode_state.current_mode = SHELL_MODE_LUSH;
    g_shell_mode_state.strict_mode = false;

    for (int i = 0; i < FEATURE_COUNT; i++) {
        g_shell_mode_state.feature_overrides[i] = false;
        g_shell_mode_state.feature_override_set[i] = false;
    }
}

void shell_mode_cleanup(void) {
    /// Currently nothing to clean up - all state is static
}

void shell_mode_set_strict(bool strict) {
    g_shell_mode_state.strict_mode = strict;
}

bool shell_mode_is_strict(void) { return g_shell_mode_state.strict_mode; }

/* ============================================================================
 * Shebang Detection
 * ============================================================================
 */

bool shell_mode_detect_from_shebang(const char *shebang, shell_mode_t *mode) {
    if (!shebang || !mode) {
        return false;
    }

    /// Skip #! if present
    if (shebang[0] == '#' && shebang[1] == '!') {
        shebang += 2;
    }

    /// Skip leading whitespace
    while (*shebang == ' ' || *shebang == '\t') {
        shebang++;
    }

    /// Check for /usr/bin/env pattern
    if (strncmp(shebang, "/usr/bin/env", 12) == 0) {
        shebang += 12;
        while (*shebang == ' ' || *shebang == '\t') {
            shebang++;
        }
    }

    /// Extract the interpreter name
    const char *interpreter = shebang;

    /// Skip path prefix to get basename
    const char *slash = strrchr(interpreter, '/');
    if (slash) {
        interpreter = slash + 1;
    }

    /// Match interpreter names
    if (strncmp(interpreter, "bash", 4) == 0) {
        *mode = SHELL_MODE_BASH;
        return true;
    }

    if (strncmp(interpreter, "zsh", 3) == 0) {
        *mode = SHELL_MODE_ZSH;
        return true;
    }

    if (strncmp(interpreter, "sh", 2) == 0 &&
        (interpreter[2] == '\0' || interpreter[2] == ' ' ||
         interpreter[2] == '\t' || interpreter[2] == '\n')) {
        *mode = SHELL_MODE_POSIX;
        return true;
    }

    if (strncmp(interpreter, "lush", 6) == 0) {
        *mode = SHELL_MODE_LUSH;
        return true;
    }

    /// Also check for dash, ash, etc. as POSIX
    if (strncmp(interpreter, "dash", 4) == 0 ||
        strncmp(interpreter, "ash", 3) == 0) {
        *mode = SHELL_MODE_POSIX;
        return true;
    }

    return false;
}

/* ============================================================================
 * Debugging and Introspection
 * ============================================================================
 */

void shell_mode_debug_print(void) {
    fprintf(stderr, "=== Shell Mode State ===\n");
    fprintf(stderr, "Current mode: %s\n",
            shell_mode_name(g_shell_mode_state.current_mode));
    fprintf(stderr, "Strict mode: %s\n",
            g_shell_mode_state.strict_mode ? "yes" : "no");
    fprintf(stderr, "\nFeature states:\n");

    for (int i = 0; i < FEATURE_COUNT; i++) {
        bool enabled = shell_mode_allows((shell_feature_t)i);
        bool overridden = g_shell_mode_state.feature_override_set[i];
        bool mode_default = feature_matrix[g_shell_mode_state.current_mode][i];

        fprintf(stderr, "  %-25s: %s", feature_names[i],
                enabled ? "ON " : "OFF");

        if (overridden) {
            fprintf(stderr, " (override, mode default: %s)",
                    mode_default ? "on" : "off");
        }

        fprintf(stderr, "\n");
    }
}

int shell_feature_describe(shell_feature_t feature, char *buffer, size_t size) {
    if (feature >= FEATURE_COUNT || !buffer || size == 0) {
        return 0;
    }

    bool enabled = shell_mode_allows(feature);
    bool overridden = g_shell_mode_state.feature_override_set[feature];
    bool mode_default =
        feature_matrix[g_shell_mode_state.current_mode][feature];

    if (overridden) {
        return snprintf(buffer, size, "%s: %s (override, mode default: %s)",
                        feature_names[feature], enabled ? "on" : "off",
                        mode_default ? "on" : "off");
    } else {
        return snprintf(buffer, size, "%s: %s", feature_names[feature],
                        enabled ? "on" : "off");
    }
}
