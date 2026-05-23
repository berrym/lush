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
#include "lle/lle_shell_integration.h"
#include "lush.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "symtable.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Global shell options instance */
shell_options_t shell_opts = {0};

/**
 * @brief Apply a mode preset across all configuration surfaces
 *
 * Canonical entry point for switching the active shell mode. Performs a
 * full re-seed: sets the active mode, drops per-feature overrides,
 * updates the legacy posix_mode mirror, and persists the canonical mode
 * label to the central registry. Per-mode registry default re-seeding is
 * layered on top of this in commit 4 of the configuration cleanup.
 *
 * @param mode New mode preset to apply
 * @return true on success, false if mode change is disallowed (strict
 *         mode lock or invalid mode value)
 */
bool apply_mode_preset(shell_mode_t mode) {
    if (!shell_mode_set(mode)) {
        return false;
    }

    /* Mode change is a clean re-seed: drop any per-feature overrides so
     * the new mode's matrix defaults take effect. Picking a preset means
     * asking for it. */
    shell_feature_reset_all();

    /* Legacy POSIX bookkeeping mirror. shell_opts.posix_mode predates
     * the mode system; keep it in sync so call sites that still consult
     * it see the right value. */
    shell_opts.posix_mode = (mode == SHELL_MODE_POSIX);

    /* Persist the canonical mode label in the central registry. */
    config.shell_mode = (int)mode;
    if (config_registry_is_initialized()) {
        config_registry_set_string("shell.mode", shell_mode_name(mode));
        /* Re-seed any registered per-mode default overrides. Options
         * without per-mode defaults are unaffected. Re-seed-every-time
         * semantic: mid-session mode changes overwrite user tweaks to
         * mode-aware options (picking a preset means asking for it). */
        config_registry_apply_mode_defaults(mode);
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
    /* Build the full key: shell.<name> */
    char key[CREG_KEY_MAX];
    snprintf(key, sizeof(key), "shell.%s", name);

    /* Update config system's shell option */
    config_set_shell_option(key, value);

    /* Update registry if initialized */
    if (config_registry_is_initialized()) {
        config_registry_set_boolean(key, value);
    }
}

/**
 * @brief Initialize POSIX shell options with defaults
 *
 * Sets all shell options to their default values. Called during
 * shell initialization before command line parsing.
 */
void init_posix_options(void) {
    // Set default values
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
    shell_opts.hash_commands = true; // Default enabled for performance
    shell_opts.job_control = false;
    shell_opts.allexport = false;
    shell_opts.noclobber = false;
    shell_opts.onecmd = false;
    shell_opts.notify = false;
    shell_opts.ignoreeof = false;
    shell_opts.nolog = false;
    shell_opts.emacs_mode = true; // Default to emacs mode
    shell_opts.vi_mode = false;   // Default to emacs mode, not vi
    shell_opts.posix_mode =
        false; // Default to non-strict mode for compatibility
    shell_opts.pipefail_mode = false;  // Default to standard pipeline behavior
    shell_opts.histexpand_mode = true; // Default to history expansion enabled
    shell_opts.history_mode =
        true; // Default to command history recording enabled
    shell_opts.interactive_comments_mode =
        true;                         // Default to interactive comments enabled
    shell_opts.physical_mode = false; // Default to logical directory paths
    shell_opts.privileged_mode = false; // Default to unrestricted mode
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
    const char *name; /**< Long option name (e.g., "errexit") */
    bool *flag;       /**< Pointer to the option flag */
    char short_opt;   /**< Short option character (e.g., 'e'), 0 if none */
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
    {          "histexpand",           &shell_opts.histexpand_mode,   0},
    {             "history",              &shell_opts.history_mode,   0},
    {"interactive-comments", &shell_opts.interactive_comments_mode,   0},
    {            "physical",             &shell_opts.physical_mode,   0},
    {          "privileged",           &shell_opts.privileged_mode,   0},
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
 * @brief Print a shell variable value with proper POSIX quoting
 *
 * @param key Variable name
 * @param value Variable value (already extracted, not raw encoded)
 */
static void print_variable_quoted(const char *key, const char *value) {
    if (!value) {
        printf("%s=''\n", key);
        return;
    }

    /* Check if value needs quoting (contains special chars) */
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
        /* Use single quotes, escaping any single quotes in value */
        printf("%s='", key);
        for (const char *p = value; *p; p++) {
            if (*p == '\'') {
                printf("'\\''"); /* End quote, escaped quote, start quote */
            } else {
                putchar(*p);
            }
        }
        printf("'\n");
    } else {
        printf("%s=%s\n", key, value);
    }
}

/**
 * @brief Callback for printing a single shell variable
 *
 * Used by symtable_enumerate_global_vars to print each variable
 * in POSIX format: NAME=VALUE (with proper quoting for special chars).
 *
 * @param key Variable name
 * @param value Variable value (already clean, no metadata)
 * @param userdata Unused
 */
static void print_variable_callback(const char *key, const char *value,
                                    void *userdata) {
    (void)userdata;

    if (!key) {
        return;
    }

    /* Skip internal/special variables that start with double underscore */
    if (key[0] == '_' && key[1] == '_') {
        return;
    }

    print_variable_quoted(key, value);
}

/**
 * @brief Print all shell variables (POSIX 'set' with no arguments)
 *
 * Enumerates and prints all shell variables in NAME=VALUE format.
 * This is the POSIX-required behavior for 'set' with no arguments.
 */
static void print_all_shell_variables(void) {
    symtable_enumerate_global_vars(print_variable_callback, NULL);
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
int builtin_set(char **args) {
    // Privileged mode security check - block all set operations
    if (shell_opts.privileged_mode && args[1]) {
        executor_error_report(current_executor, SHELL_ERR_PERMISSION_DENIED,
                              builtin_get_source_location(),
                              "cannot modify shell options in privileged mode");
        return 1;
    }

    if (!args[1]) {
        // No arguments - display all shell variables (POSIX requirement)
        // Variables are printed in format: NAME=VALUE (quoted if needed)
        print_all_shell_variables();
        return 0;
    }

    for (int i = 1; args[i]; i++) {
        char *arg = args[i];

        // Handle -o and +o options
        if (strcmp(arg, "-o") == 0) {
            if (args[i + 1]) {
                // Set named option
                i++; // consume the option name

                /* `set -o posix` is preserved as a recognized
                 * bash-bridge alias for `mode posix`: it's bash's
                 * canonical spelling for entering POSIX mode and a
                 * common bash-script idiom. The lush-native spelling
                 * is `mode posix`; both route through the same
                 * apply_mode_preset() entry point.
                 *
                 * `set -o {bash,zsh,lush}` are no longer recognized:
                 * modes are a discriminated enum, not toggles. Use
                 * the `mode` builtin instead. */
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
                        // Handle mutually exclusive editing modes
                        if (strcmp(args[i], "emacs") == 0) {
                            shell_opts.vi_mode =
                                false; // Disable vi when enabling emacs
                            sync_shell_option_to_registry("vi", false);
                            lush_update_editing_mode();
                        } else if (strcmp(args[i], "vi") == 0) {
                            shell_opts.emacs_mode =
                                false; // Disable emacs when enabling vi
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
                // No argument - show all options including shell mode
                printf("Current shell options:\n");
                for (int j = 0; option_map[j].name; j++) {
                    printf("set %co %s\n", *(option_map[j].flag) ? '-' : '+',
                           option_map[j].name);
                }
                // Also show current shell mode (use the `mode` builtin to
                // change it; `set -o posix` is the bash-bridge alias).
                printf("(shell mode: %s -- use `mode` builtin to change)\n",
                       shell_mode_name(shell_mode_get()));
                return 0;
            }
        } else if (strcmp(arg, "+o") == 0) {
            if (args[i + 1]) {
                // Unset named option
                i++; // consume the option name

                /* `set +o posix` is the bash-bridge counterpart to
                 * `set -o posix`: it lifts the POSIX preset and
                 * returns to lush. `set +o {bash,zsh,lush}` are
                 * rejected -- modes aren't toggles. */
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
                } else {
                    option_mapping_t *opt = find_option_by_name(args[i]);
                    if (opt) {
                        *(opt->flag) = false;
                        sync_shell_option_to_registry(opt->name, false);
                        // Handle mutually exclusive editing modes
                        if (strcmp(args[i], "emacs") == 0) {
                            shell_opts.vi_mode =
                                true; // Enable vi when disabling emacs
                            sync_shell_option_to_registry("vi", true);
                            lush_update_editing_mode();
                        } else if (strcmp(args[i], "vi") == 0) {
                            shell_opts.emacs_mode =
                                true; // Enable emacs when disabling vi
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
                // No argument - show all options in +o format (read-only
                // operation, always allowed)
                printf("Current shell options:\n");
                for (int j = 0; option_map[j].name; j++) {
                    printf("set %co %s\n", *(option_map[j].flag) ? '-' : '+',
                           option_map[j].name);
                }
                // Also show current shell mode (use the `mode` builtin to
                // change it; `set -o posix` is the bash-bridge alias).
                printf("(shell mode: %s -- use `mode` builtin to change)\n",
                       shell_mode_name(shell_mode_get()));
                return 0;
            }
        } else if (strcmp(arg, "--") == 0) {
            // Handle -- option: end of options, start of positional parameters
            i++; // Move past the --

            // Clear existing positional parameters $1, $2, etc.
            for (int param_num = 1; param_num <= 99; param_num++) {
                char param_name[4];
                snprintf(param_name, sizeof(param_name), "%d", param_num);
                symtable_unset_global(param_name);
            }

            // Count how many new parameters we have
            int new_argc = 0;
            int temp_i = i;
            while (args[temp_i]) {
                new_argc++;
                temp_i++;
            }

            // Free existing shell_argv if it was dynamically allocated
            if (shell_argv && shell_argv_is_dynamic) {
                for (int j = 0; j < shell_argc; j++) {
                    free(shell_argv[j]);
                }
                free(shell_argv);
            }

            // Allocate new shell_argv (include space for program name)
            shell_argc = new_argc + 1;
            shell_argv = malloc(shell_argc * sizeof(char *));
            if (shell_argv) {
                // Set program name (shell_argv[0])
                shell_argv[0] = strdup("lush");

                // Set new positional parameters in both symbol table and global
                // arrays
                int param_num = 1;
                while (args[i] && param_num <= 99) {
                    char param_name[4];
                    snprintf(param_name, sizeof(param_name), "%d", param_num);
                    symtable_set_global(param_name, args[i]);

                    // Also update global shell_argv
                    shell_argv[param_num] = strdup(args[i]);

                    i++;
                    param_num++;
                }

                // Mark shell_argv as dynamically allocated
                shell_argv_is_dynamic = true;
            }

            // Update $# (number of positional parameters)
            char argc_str[16];
            snprintf(argc_str, sizeof(argc_str), "%d", new_argc);
            symtable_set_global("#", argc_str);

            break; // Process no more arguments after --
        } else if (arg[0] == '-' && arg[1] != '-') {
            // Handle short options like -e, -x, etc.
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
            // Handle short options like +e, +x, etc.
            for (int j = 1; arg[j]; j++) {
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
            // Regular positional parameters without -- prefix
            executor_error_report(current_executor, SHELL_ERR_INVALID_OPTION,
                                  builtin_get_source_location(),
                                  "invalid option: %s", arg);
            return 1;
        }
    }

    return 0;
}
