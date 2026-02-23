/**
 * @file builtins.c
 * @brief Shell builtin command implementations
 *
 * Comprehensive implementation of shell builtin commands including cd, echo,
 * export, pwd, exit, jobs, fg, bg, history, config, debug, and many more.
 * Provides POSIX-compliant builtins alongside Lush-specific extensions.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include "alias.h"
#include "arithmetic.h"
#include "compat.h"
#include "dirstack.h"
#include "config.h"
#include "config_registry.h"
#include "debug.h"
#include "shell_error.h"
#include "shell_mode.h"
#include "display_integration.h"
#include "display/command_layer.h"
#include "display/composition_engine.h"
#include "errors.h"
#include "executor.h"
#include "fixer.h"
#include "ht.h"
#include "input.h"
#include "lle/adaptive_terminal_integration.h"
#include "lle/completion/custom_source.h"
#include "lle/completion/ssh_hosts.h"
#include "lle/history.h"
#include "lle/keybinding.h"
#include "lle/keybinding_config.h"
#include "lle/lle_editor.h"
#include "lle/lle_safety.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_integration.h"
#include "lle/lle_watchdog.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/theme.h"
#include "lle/prompt/theme_loader.h"
#include "lush.h"
#include "lush_memory_pool.h"
#include "posix_history.h"
#include "signals.h"
#include "symtable.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// Forward declarations for job control builtins
int bin_jobs(int argc, char **argv);
int bin_fg(int argc, char **argv);
int bin_bg(int argc, char **argv);
int bin_colon(int argc, char **argv);
int bin_readonly(int argc, char **argv);
int bin_config(int argc, char **argv);
int bin_setopt(int argc, char **argv);
int bin_unsetopt(int argc, char **argv);
int bin_shopt(int argc, char **argv);
int bin_hash(int argc, char **argv);
int bin_display(int argc, char **argv);
int bin_network(int argc, char **argv);
int bin_debug(int argc, char **argv);
int bin_mapfile(int argc, char **argv);
int bin_env(int argc, char **argv);
int bin_analyze(int argc, char **argv);
int bin_lint(int argc, char **argv);
int bin_disown(int argc, char **argv);
int bin_let(int argc, char **argv);

// Forward declarations for POSIX compliance
bool is_posix_mode_enabled(void);

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Hash table for remembered command paths
ht_strstr_t *command_hash = NULL;

/**
 * @brief Report a structured builtin error
 *
 * Creates and displays a structured error for builtin commands.
 * Since builtins don't have source locations, uses a minimal display format.
 *
 * @param builtin_name Name of the builtin (e.g., "cd", "export")
 * @param code Error code from shell_error_code_t
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
static void builtin_error(const char *builtin_name, shell_error_code_t code,
                          const char *fmt, ...) {
    /* Create error with no source location */
    va_list args;
    va_start(args, fmt);
    shell_error_t *error = shell_error_createv(code, SHELL_SEVERITY_ERROR,
                                                SOURCE_LOC_UNKNOWN, fmt, args);
    va_end(args);

    if (error) {
        /* Add builtin context */
        shell_error_push_context(error, "in builtin '%s'", builtin_name);
        
        /* Display the error */
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
    } else {
        /* Fallback to simple error message */
        va_start(args, fmt);
        fprintf(stderr, "lush: %s: ", builtin_name);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}

// Table of builtin commands
builtin builtins[] = {
    {"exit", "exit shell", bin_exit},
    {"help", "builtin help", bin_help},
    {"cd", "change directory", bin_cd},
    {"pwd", "print working directory", bin_pwd},
    {"history", "print command history", bin_history},
    {"fc", "fix command (POSIX history edit/list)", bin_fc},
    {"alias", "set an alias", bin_alias},
    {"unalias", "unset an alias", bin_unalias},
    {"clear", "clear the screen", bin_clear},
    {"terminal", "display terminal information", bin_terminal},

    {"type", "display command type", bin_type},
    {"unset", "unset a shell variable", bin_unset},

    {"echo", "echo text to stdout", bin_echo},
    {"printf", "formatted output", bin_printf},
    {"export", "export shell variables", bin_export},
    {"source", "source a script", bin_source},
    {".", "source a script", bin_source},
    {"test", "test expressions", bin_test},
    {"[", "test expressions", bin_test},
    {"read", "read user input", bin_read},
    {"eval", "evaluate arguments", bin_eval},
    {"true", "return success status", bin_true},
    {"false", "return failure status", bin_false},
    {"set", "set shell options", bin_set},
    {"jobs", "list active jobs", bin_jobs},
    {"fg", "bring job to foreground", bin_fg},
    {"bg", "send job to background", bin_bg},
    {"disown", "remove jobs from shell or mark to not receive SIGHUP", bin_disown},
    {"shift", "shift positional parameters", bin_shift},
    {"break", "break out of loops", bin_break},
    {"continue", "continue to next loop iteration", bin_continue},
    {"return", "return from functions", bin_return},
    {"return_value", "set function return value", bin_return_value},
    {"trap", "set signal handlers", bin_trap},
    {"exec", "replace shell with command", bin_exec},
    {"wait", "wait for background jobs", bin_wait},
    {"umask", "set/display file creation mask", bin_umask},
    {"ulimit", "set/display resource limits", bin_ulimit},
    {"times", "display process times", bin_times},
    {"getopts", "parse command options", bin_getopts},
    {"local", "declare local variables", bin_local},
    {"declare", "declare variables with attributes", bin_declare},
    {"typeset", "declare variables with attributes", bin_declare},
    {"let", "evaluate arithmetic expressions", bin_let},
    {":", "null command (no-op)", bin_colon},
    {"readonly", "create read-only variables", bin_readonly},
    {"config", "manage shell configuration", bin_config},
    {"setopt", "enable shell options/features", bin_setopt},
    {"unsetopt", "disable shell options/features", bin_unsetopt},
    {"shopt", "bash-style shell options", bin_shopt},
    {"hash", "remember utility locations", bin_hash},
    {"display", "manage layered display system", bin_display},
    {"network", "manage network and SSH hosts", bin_network},
    {"debug", "advanced debugging and profiling", bin_debug},
    {"command", "execute command bypassing builtins/aliases", bin_command},
    {"pushd", "push directory onto stack", bin_pushd},
    {"popd", "pop directory from stack", bin_popd},
    {"dirs", "display directory stack", bin_dirs},
    {"mapfile", "read lines from stdin into array", bin_mapfile},
    {"readarray", "read lines from stdin into array", bin_mapfile},
    {"env", "run command with modified environment", bin_env},
    {"printenv", "print environment variables", bin_env},
    {"analyze", "full script analysis with info, warnings, and errors", bin_analyze},
    {"lint", "lint scripts and optionally apply automatic fixes", bin_lint},
};

const size_t builtins_count = sizeof(builtins) / sizeof(builtins[0]);

/**
 * @brief Null command - does nothing and returns success
 *
 * Used for parameter expansions and as a no-op placeholder.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0 (success)
 */
int bin_colon(int argc __attribute__((unused)),
              char **argv __attribute__((unused))) {
    if (getenv("PARAM_EXPANSION_DEBUG")) {
        fprintf(stderr, "DEBUG: colon builtin received %d arguments:\n", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(stderr, "  argv[%d] = '%s'\n", i,
                    argv[i] ? argv[i] : "(null)");
        }
    }
    return 0;
}

/**
 * @brief Exit the shell (builtin command)
 *
 * Sets the exit_flag to terminate the main loop gracefully,
 * allowing proper cleanup of parser, executor, and other resources.
 * EXIT traps are executed in the main loop after normal cleanup.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional exit code)
 * @return The exit code (though the shell will exit before this matters)
 */
int bin_exit(int argc, char **argv) {
    int exit_code = last_exit_status; /* Default to last command's status */

    /* Parse exit code argument if provided */
    if (argc > 1) {
        exit_code = atoi(argv[1]);
    }

    /* Set exit flag to break main loop - allows proper cleanup */
    exit_flag = true;

    /* Store exit code for use after main loop exits */
    last_exit_status = exit_code;

    return exit_code;
}

/**
 * @brief Print a list of builtins and their descriptions
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0
 */
int bin_help(int argc __attribute__((unused)),
             char **argv __attribute__((unused))) {
    for (size_t i = 0; i < builtins_count; i++) {
        fprintf(stderr, "\t%-10s%-40s\n", builtins[i].name, builtins[i].doc);
    }

    return 0;
}

/**
 * @brief Canonicalize a path by resolving . and .. components logically
 *
 * Resolves relative path components without following symlinks,
 * maintaining the logical path as entered by the user.
 *
 * @param path The path to canonicalize
 * @return Newly allocated canonicalized path, or NULL on error (caller must free)
 */
static char *canonicalize_logical_path(const char *path) {
    if (!path)
        return NULL;

    size_t path_len = strlen(path);
    char *result = malloc(path_len + 1);
    if (!result)
        return NULL;

    strcpy(result, path);

    // Simple canonicalization: remove /./  and resolve /../
    char *src = result;
    char *dst = result;

    while (*src) {
        if (*src == '/') {
            // Skip multiple slashes
            while (*src == '/')
                src++;
            if (dst > result || dst == result)
                *dst++ = '/';

            // Check for . and ..
            if (*src == '.') {
                if (src[1] == '/' || src[1] == '\0') {
                    // Skip ./
                    src++;
                    continue;
                } else if (src[1] == '.' && (src[2] == '/' || src[2] == '\0')) {
                    // Handle ../
                    src += 2;
                    // Remove last component from dst
                    if (dst > result + 1) {
                        dst--; // Back up from the /
                        while (dst > result && dst[-1] != '/')
                            dst--;
                    }
                    continue;
                }
            }
        }
        *dst++ = *src++;
    }

    // Remove trailing slash unless it's root
    if (dst > result + 1 && dst[-1] == '/') {
        dst--;
    }

    *dst = '\0';

    // Handle empty path
    if (dst == result) {
        strcpy(result, "/");
    }

    return result;
}

/**
 * @brief Change the current working directory
 *
 * Implements the cd builtin with support for:
 * - cd (no args): go to HOME
 * - cd -: go to previous directory (OLDPWD)
 * - cd path: change to specified path
 *
 * Respects physical_mode setting for symlink resolution.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_cd(int argc __attribute__((unused)),
           char **argv __attribute__((unused))) {
    static char *previous_dir = NULL;
    char *current_dir = NULL;
    char *target_dir = NULL;

    // Privileged mode security check
    if (shell_opts.privileged_mode) {
        fprintf(stderr, "cd: restricted command in privileged mode\n");
        return 1;
    }

    // Get current directory before changing
    current_dir = getcwd(NULL, 0);
    if (!current_dir && errno != ENOENT) {
        error_return("cd: getcwd");
        return 1;
    }

    // Parse arguments - handle -- as option terminator
    int arg_index = 1;
    if (argc > 1 && strcmp(argv[1], "--") == 0) {
        arg_index = 2;  // Skip past --
    }

    if (arg_index >= argc) {
        // cd with no arguments (or just --) - go to HOME
        target_dir = getenv("HOME");
        if (!target_dir) {
            builtin_error("cd", SHELL_ERR_UNBOUND_VARIABLE, "HOME not set");
            free(current_dir);
            return 1;
        }
    } else if (arg_index == argc - 1) {
        if (strcmp(argv[arg_index], "-") == 0) {
            // cd - : go to previous directory
            if (!previous_dir) {
                builtin_error("cd", SHELL_ERR_UNBOUND_VARIABLE, "OLDPWD not set");
                free(current_dir);
                return 1;
            }
            target_dir = previous_dir;
            // Print the directory we're changing to (standard behavior)
            printf("%s\n", target_dir);
        } else {
            target_dir = argv[arg_index];
        }
    } else {
        builtin_error("cd", SHELL_ERR_TOO_MANY_ARGUMENTS, "usage: cd [pathname | -]");
        free(current_dir);
        return 1;
    }

    // Attempt to change directory
    if (chdir(target_dir) < 0) {
        // If chdir failed and cdable_vars is enabled, try treating target as variable name
        if (shell_mode_allows(FEATURE_CDABLE_VARS) && target_dir[0] != '/' && 
            target_dir[0] != '.' && target_dir[0] != '~') {
            const char *var_value = symtable_get_global(target_dir);
            if (var_value && var_value[0] == '/') {
                // Try cd to the variable's value
                if (chdir(var_value) == 0) {
                    // Success - update target_dir for PWD setting
                    target_dir = (char *)var_value;
                    goto cd_success;
                }
            }
        }
        error_return("cd");
        free(current_dir);
        return 1;
    }
cd_success:

    // Auto-push old directory to stack if enabled
    if (shell_mode_allows(FEATURE_AUTO_PUSHD) && current_dir) {
        dirstack_push(current_dir);
    }

    // Update previous directory
    if (previous_dir) {
        free(previous_dir);
    }
    previous_dir = current_dir;

    // Set OLDPWD variable according to current mode
    if (previous_dir) {
        if (shell_opts.physical_mode) {
            // In physical mode, resolve OLDPWD to physical path
            char *resolved_prev = realpath(previous_dir, NULL);
            if (resolved_prev) {
                symtable_set_global("OLDPWD", resolved_prev);
                free(resolved_prev);
            } else {
                symtable_set_global("OLDPWD", previous_dir);
            }
        } else {
            // In logical mode, use logical path
            symtable_set_global("OLDPWD", previous_dir);
        }
    }

    // Set PWD variable according to current mode
    if (shell_opts.physical_mode) {
        // In physical mode, resolve PWD to physical path
        char *resolved_dir = realpath(".", NULL);
        if (resolved_dir) {
            symtable_set_global("PWD", resolved_dir);
            free(resolved_dir);
        }
    } else {
        // In logical mode, preserve the logical path taken
        if (argc == 2 && strcmp(argv[1], "-") == 0) {
            // cd - case: PWD becomes old OLDPWD (already handled above in cd -
            // logic)
            char *new_dir = getcwd(NULL, 0);
            if (new_dir) {
                symtable_set_global("PWD", new_dir);
                free(new_dir);
            }
        } else if (target_dir && target_dir[0] == '/') {
            // Absolute path - canonicalize it in logical mode
            char *canonical_path = canonicalize_logical_path(target_dir);
            if (canonical_path) {
                symtable_set_global("PWD", canonical_path);
                free(canonical_path);
            } else {
                symtable_set_global("PWD", target_dir);
            }
        } else if (target_dir) {
            // Relative path - build logical path from current PWD
            char *current_pwd = symtable_get_global("PWD");
            if (current_pwd && strlen(current_pwd) > 0) {
                size_t pwd_len = strlen(current_pwd);
                size_t target_len = strlen(target_dir);
                char *logical_path = malloc(pwd_len + target_len + 2);
                if (logical_path) {
                    strcpy(logical_path, current_pwd);
                    if (logical_path[pwd_len - 1] != '/') {
                        strcat(logical_path, "/");
                    }
                    strcat(logical_path, target_dir);

                    // Canonicalize the logical path to handle . and ..
                    char *canonical_path =
                        canonicalize_logical_path(logical_path);
                    if (canonical_path) {
                        symtable_set_global("PWD", canonical_path);
                        free(canonical_path);
                    } else {
                        symtable_set_global("PWD", logical_path);
                    }
                    free(logical_path);
                } else {
                    // Fallback to getcwd if malloc fails
                    char *new_dir = getcwd(NULL, 0);
                    if (new_dir) {
                        symtable_set_global("PWD", new_dir);
                        free(new_dir);
                    }
                }
            } else {
                // No current PWD - fallback to getcwd
                char *new_dir = getcwd(NULL, 0);
                if (new_dir) {
                    symtable_set_global("PWD", new_dir);
                    free(new_dir);
                }
            }
        } else {
            // cd with no arguments - go to HOME
            char *home = getenv("HOME");
            if (home) {
                symtable_set_global("PWD", home);
            } else {
                char *new_dir = getcwd(NULL, 0);
                if (new_dir) {
                    symtable_set_global("PWD", new_dir);
                    free(new_dir);
                }
            }
        }
    }

    /* Fire LLE shell event for directory change (Spec 26)
     * This notifies registered handlers (prompt composer, autosuggestions,
     * etc.) that the working directory has changed. previous_dir holds the old
     * dir.
     */
    lle_fire_directory_changed(previous_dir, NULL);

    return 0;
}

/**
 * @brief Print the current working directory
 *
 * In physical mode, resolves symlinks and shows the physical path.
 * In logical mode, uses PWD from the symbol table if available.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success, 1 on error
 */
int bin_pwd(int argc, char **argv) {
    // Default to shell's physical_mode setting, but allow -P/-L to override
    bool physical = shell_opts.physical_mode;

    // Parse options: -P (physical), -L (logical)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-P") == 0) {
            physical = true;
        } else if (strcmp(argv[i], "-L") == 0) {
            physical = false;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "pwd: %s: invalid option\n", argv[i]);
            return 1;
        }
    }

    if (physical) {
        // In physical mode, resolve symlinks and show physical path
        char *physical_path = realpath(".", NULL);
        if (physical_path) {
            printf("%s\n", physical_path);
            free(physical_path);
            return 0;
        }
        // Fall through to error handling if realpath fails
    } else {
        // In logical mode, use PWD from symbol table if available
        char *pwd_value = symtable_get_global("PWD");
        if (pwd_value) {
            printf("%s\n", pwd_value);
            free(pwd_value);
            return 0;
        }
        // Fall through to getcwd if PWD not available
    }

    // Fallback - use getcwd
    char cwd[MAXLINE] = {'\0'};
    if (getcwd(cwd, MAXLINE) == NULL) {
        error_return("pwd");
        return 1;
    }

    printf("%s\n", cwd);

    return 0;
}

/**
 * @brief Display or manipulate the command history
 *
 * Implements the history builtin using the LLE history system.
 * Supports listing history entries and various manipulation options.
 *
 * @param argc Argument count
 * @param argv Argument vector with history options
 * @return 0 on success, 1 on error
 */
int bin_history(int argc, char **argv) {
    char *output = NULL;
    lle_result_t result =
        lle_history_bridge_handle_builtin(argc, argv, &output);

    if (output) {
        printf("%s", output);
        free(output);
    }

    return (result == LLE_SUCCESS) ? 0 : 1;
}

/**
 * @brief Display terminal capability information
 *
 * Shows detailed terminal detection results from LLE including:
 * TTY status, terminal type, dimensions, color support,
 * unicode support, mouse support, and multiplexer detection.
 *
 * @param argc Argument count
 * @param argv Argument vector (supports "help" subcommand)
 * @return 0 on success, 1 on error
 */
int bin_terminal(int argc, char **argv) {
    if (argc > 2) {
        error_message("terminal: too many arguments");
        error_message("Run 'terminal help' for usage information");
        return 1;
    }

    if (argc == 2 &&
        (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("terminal - Display terminal capability information\n\n");
        printf("Usage: terminal [option]\n\n");
        printf("Options:\n");
        printf("  (none)  Show terminal capabilities (default)\n");
        printf("  help    Show this help message\n\n");
        printf("The terminal command displays information about the current\n");
        printf("terminal environment detected by LLE.\n");
        return 0;
    }

    lle_terminal_detection_result_t *detection = NULL;
    if (lle_detect_terminal_capabilities_optimized(&detection) != LLE_SUCCESS ||
        !detection) {
        error_message("terminal: failed to detect terminal capabilities");
        return 1;
    }

    printf("Terminal Information (LLE Detection)\n");
    printf("=====================================\n\n");

    printf("TTY Status:\n");
    printf("  stdin:  %s\n", detection->stdin_is_tty ? "yes" : "no");
    printf("  stdout: %s\n", detection->stdout_is_tty ? "yes" : "no");
    printf("  stderr: %s\n", detection->stderr_is_tty ? "yes" : "no");

    printf("\nTerminal Type:\n");
    printf("  TERM:         %s\n",
           detection->term_name[0] ? detection->term_name : "(not set)");
    printf("  TERM_PROGRAM: %s\n",
           detection->term_program[0] ? detection->term_program : "(not set)");
    printf("  COLORTERM:    %s\n",
           detection->colorterm[0] ? detection->colorterm : "(not set)");

    printf("\nDimensions:\n");
    printf("  Columns: %d\n", detection->terminal_cols);
    printf("  Rows:    %d\n", detection->terminal_rows);

    printf("\nCapabilities:\n");
    printf("  Colors:        %s\n", detection->supports_colors ? "yes" : "no");
    printf("  256 colors:    %s\n",
           detection->supports_256_colors ? "yes" : "no");
    printf("  True color:    %s\n",
           detection->supports_truecolor ? "yes" : "no");
    printf("  Unicode:       %s\n", detection->supports_unicode ? "yes" : "no");
    printf("  Mouse:         %s\n", detection->supports_mouse ? "yes" : "no");
    printf("  Bracketed paste: %s\n",
           detection->supports_bracketed_paste ? "yes" : "no");

    printf("\nMultiplexer:\n");
    if (lle_is_tmux(detection)) {
        printf("  Running inside: tmux\n");
    } else if (lle_is_screen(detection)) {
        printf("  Running inside: GNU screen\n");
    } else {
        printf("  Running inside: (none detected)\n");
    }

    if (lle_is_iterm2(detection)) {
        printf("  Terminal app:   iTerm2\n");
    }

    printf("\nLLE Mode:\n");
    printf("  Recommended: %s\n",
           lle_adaptive_mode_to_string(detection->recommended_mode));
    printf("  Capability:  %s\n",
           lle_capability_level_to_string(detection->capability_level));

    return 0;
}

/**
 * @brief Clear the terminal screen
 *
 * Uses the display integration layer to clear the screen.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0
 */
int bin_clear(int argc __attribute__((unused)),
              char **argv __attribute__((unused))) {
    display_integration_clear_screen();
    return 0;
}

/**
 * @brief Remove a variable from the global symbol table
 *
 * Unsets the specified shell variable, removing it from the environment.
 *
 * @param argc Argument count (must be 2)
 * @param argv Argument vector (argv[1] is variable name)
 * @return 0 on success, 1 on invalid usage
 */
int bin_unset(int argc, char **argv) {
    // No arguments - silently succeed (POSIX behavior)
    if (argc < 2) {
        return 0;
    }

    // Unset each variable specified
    for (int i = 1; i < argc; i++) {
        const char *var_name = argv[i];
        
        // Resolve nameref if applicable - unset the target, not the nameref itself
        symtable_manager_t *mgr = symtable_get_global_manager();
        if (mgr && symtable_is_nameref(mgr, var_name)) {
            const char *target = symtable_resolve_nameref(mgr, var_name, 10);
            if (target && target != var_name) {
                var_name = target;
            }
        }

        // Use legacy API function for unsetting variables
        symtable_unset_global(var_name);
    }
    return 0;
}

/**
 * @brief Display how a command would be interpreted
 *
 * Shows whether a command is a builtin, alias, function, or external file.
 * Supports -t (type only), -p (path only), and -a (show all) options.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and command names
 * @return 0 if all commands found, 1 if any not found
 */
int bin_type(int argc, char **argv) {
    bool type_only = false; // -t flag: output only the type
    bool path_only = false; // -p flag: output only the path
    bool show_all = false;  // -a flag: show all locations
    int name_start = 1;

    // Parse options
    for (int i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            type_only = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            path_only = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = true;
            name_start = i + 1;
        } else if (strcmp(argv[i], "--") == 0) {
            name_start = i + 1;
            break;
        } else {
            error_message("type: invalid option: %s", argv[i]);
            return 1;
        }
    }

    if (name_start >= argc) {
        error_message("usage: type [-t] [-p] [-a] name [name ...]");
        return 1;
    }

    int result = 0;
    for (int i = name_start; i < argc; i++) {
        const char *name = argv[i];

        bool found_any = false;

        // Check if it's a builtin command
        if (is_builtin(name)) {
            found_any = true;
            if (type_only) {
                printf("builtin\n");
            } else if (path_only) {
                // -p flag: builtins have no path, so output nothing
            } else {
                printf("%s is a shell builtin\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's an alias (simplified - would need full alias parsing)
        char *alias_value = symtable_get_global("alias");
        if (alias_value) {
            found_any = true;
            if (type_only) {
                printf("alias\n");
            } else if (path_only) {
                // -p flag: aliases have no path, so output nothing
            } else {
                printf("%s is aliased\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's a function (stored in symbol table)
        char *func_value = symtable_get_global(name);
        if (func_value && strstr(func_value, "function")) {
            found_any = true;
            if (type_only) {
                printf("function\n");
            } else if (path_only) {
                // -p flag: functions have no path, so output nothing
            } else {
                printf("%s is a function\n", name);
            }
            if (!show_all)
                continue;
        }

        // Check if it's an executable file in PATH
        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");
            bool found_in_path = false;

            while (dir != NULL) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

                if (access(full_path, X_OK) == 0) {
                    found_any = true;
                    found_in_path = true;

                    if (type_only) {
                        printf("file\n");
                    } else if (path_only) {
                        printf("%s\n", full_path);
                    } else {
                        printf("%s is %s\n", name, full_path);
                    }

                    if (!show_all)
                        break;
                }
                dir = strtok(NULL, ":");
            }

            free(path_copy);
            if (found_in_path && !show_all) {
                continue;
            }
        }

        // Not found anywhere
        if (!found_any) {
            if (type_only || path_only) {
                // For -t and -p, output nothing for not found commands
            } else {
                printf("%s: not found\n", name);
            }
            result = 1;
        }
    }

    return result;
}

/**
 * @brief Process escape sequences in a string
 *
 * Converts escape sequences like \n, \t, \r, etc. to their
 * corresponding characters.
 *
 * @param str The string to process
 * @return Newly allocated string with escapes processed (caller must free),
 *         or NULL on error
 */
static char *process_escape_sequences(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    const char *src = str;
    char *dst = result;

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++; // Skip the backslash
            switch (*src) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            case 'b':
                *dst++ = '\b';
                break;
            case 'a':
                *dst++ = '\a';
                break;
            case 'v':
                *dst++ = '\v';
                break;
            case 'f':
                *dst++ = '\f';
                break;
            case '\\':
                *dst++ = '\\';
                break;
            case '"':
                *dst++ = '"';
                break;
            case '\'':
                *dst++ = '\'';
                break;
            default:
                *dst++ = '\\';
                *dst++ = *src;
                break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
    }

    *dst = '\0';
    return result;
}

/**
 * @brief Echo arguments to stdout
 *
 * Implements the echo builtin with XSI escape sequence processing.
 * Per POSIX XSI extension, escape sequences like \n, \t are interpreted.
 * Supports -n (no newline), -e (interpret escapes), -E (no escapes) options.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and strings to echo
 * @return Always returns 0
 */
int bin_echo(int argc, char **argv) {
    bool interpret_escapes = true; // XSI: interpret escape sequences by default
    bool no_newline = false;
    int arg_start = 1;

    // Parse options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0) {
            interpret_escapes = true;
            arg_start = i + 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            no_newline = true;
            arg_start = i + 1;
        } else if (strcmp(argv[i], "-E") == 0) {
            interpret_escapes = false;
            arg_start = i + 1;
        } else if (argv[i][0] == '-') {
            // Unknown option, treat as argument
            break;
        } else {
            break;
        }
    }

    // Clear any previous error state on stdout
    clearerr(stdout);

    // Print arguments
    for (int i = arg_start; i < argc; i++) {
        if (i > arg_start) {
            printf(" ");
        }

        if (interpret_escapes) {
            char *processed = process_escape_sequences(argv[i]);
            if (processed) {
                printf("%s", processed);
                free(processed);
            } else {
                printf("%s", argv[i]);
            }
        } else {
            printf("%s", argv[i]);
        }
    }

    if (!no_newline) {
        printf("\n");
    }

    // Flush and check for write errors (e.g., writing to closed/invalid fd)
    if (fflush(stdout) == EOF || ferror(stdout)) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "echo: write error: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    return 0;
}

/**
 * @brief Formatted output to stdout
 *
 * Implements the printf builtin with POSIX format specifier support.
 * Handles width specifiers, precision, and all standard format conversions
 * including %s, %d, %i, %c, %x, %X, %o, %u, %f, %g, %e.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is format, rest are arguments)
 * @return 0 on success, 1 on usage error
 */
int bin_printf(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "printf: usage: printf format [arguments ...]\n");
        return 1;
    }

    const char *format = argv[1];
    int arg_index = 2;

    // POSIX: The format string is reused as often as necessary to satisfy
    // the remaining arguments. If the format string contains no conversion
    // specifications, and there are arguments, the format is used once and
    // subsequent arguments are ignored.
    do {
        int args_used_this_pass = 0;

        for (int i = 0; format[i] != '\0'; i++) {
            if (format[i] == '%' && format[i + 1] != '\0') {
                i++; // Skip the %

                // Handle literal %
                if (format[i] == '%') {
                    putchar('%');
                    continue;
                }

                // Parse flags, width, precision
                int width = 0;
                int precision = -1;
                bool zero_pad = false;
                bool left_align = false;

                // Parse flags
                while (format[i] == '-' || format[i] == '+' ||
                       format[i] == ' ' || format[i] == '#' ||
                       format[i] == '0') {
                    if (format[i] == '0') {
                        zero_pad = true;
                    }
                    if (format[i] == '-') {
                        left_align = true;
                    }
                    i++;
                }

                // Parse width
                if (format[i] == '*') {
                    // Dynamic width from argument
                    if (arg_index < argc) {
                        width = atoi(argv[arg_index]);
                        if (width < 0) {
                            left_align = true;
                            width = -width;
                        }
                        arg_index++;
                    }
                    i++;
                } else if (isdigit(format[i])) {
                    while (isdigit(format[i])) {
                        width = width * 10 + (format[i] - '0');
                        i++;
                    }
                }

                // Parse precision
                if (format[i] == '.') {
                    i++;
                    if (format[i] == '*') {
                        // Dynamic precision from argument
                        if (arg_index < argc) {
                            precision = atoi(argv[arg_index]);
                            arg_index++;
                        }
                        i++;
                    } else {
                        precision = 0;
                        while (isdigit(format[i])) {
                            precision = precision * 10 + (format[i] - '0');
                            i++;
                        }
                    }
                }

                // Handle conversion specifier
                char specifier = format[i];
                // Get the argument for the format specifier
                const char *format_arg =
                    (arg_index < argc) ? argv[arg_index] : "";

                switch (specifier) {
                case 's': {
                    // String format
                    int len = strlen(format_arg);
                    int padding = (width > len) ? width - len : 0;

                    if (!left_align && padding > 0) {
                        // Right-align with padding
                        char pad_char = zero_pad ? '0' : ' ';
                        for (int p = 0; p < padding; p++) {
                            putchar(pad_char);
                        }
                    }

                    // Print the string (truncated if precision specified)
                    if (precision >= 0 && precision < len) {
                        for (int j = 0; j < precision; j++) {
                            putchar(format_arg[j]);
                        }
                    } else {
                        fputs(format_arg, stdout);
                    }

                    if (left_align && padding > 0) {
                        // Left-align with padding
                        for (int p = 0; p < padding; p++) {
                            putchar(' ');
                        }
                    }

                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    // Integer format
                    int value = (arg_index < argc) ? atoi(format_arg) : 0;
                    int effective_width = left_align ? -width : width;
                    printf("%*d", effective_width, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'c': {
                    // Character format
                    int value = (arg_index < argc) ? atoi(format_arg) : 0;

                    if (!left_align && width > 1) {
                        // Right-align with padding
                        for (int p = 1; p < width; p++) {
                            putchar(' ');
                        }
                    }

                    putchar(value);

                    if (left_align && width > 1) {
                        // Left-align with padding
                        for (int p = 1; p < width; p++) {
                            putchar(' ');
                        }
                    }

                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    // Hexadecimal format
                    unsigned int value =
                        (arg_index < argc)
                            ? (unsigned int)strtoul(format_arg, NULL, 10)
                            : 0;
                    int effective_width = left_align ? -width : width;
                    printf(specifier == 'x' ? "%*x" : "%*X", effective_width,
                           value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'o': {
                    // Octal format
                    unsigned int value =
                        (arg_index < argc)
                            ? (unsigned int)strtoul(format_arg, NULL, 10)
                            : 0;
                    int effective_width = left_align ? -width : width;
                    printf("%*o", effective_width, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'u': {
                    // Unsigned integer format
                    unsigned int value =
                        (arg_index < argc)
                            ? (unsigned int)strtoul(format_arg, NULL, 10)
                            : 0;
                    int effective_width = left_align ? -width : width;
                    printf("%*u", effective_width, value);
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'f':
                case 'F': {
                    // Float format
                    double value =
                        (arg_index < argc) ? strtod(format_arg, NULL) : 0.0;
                    int effective_width = left_align ? -width : width;
                    if (precision >= 0) {
                        printf("%*.*f", effective_width, precision, value);
                    } else {
                        printf("%*f", effective_width, value);
                    }
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'g':
                case 'G': {
                    // General float format
                    double value =
                        (arg_index < argc) ? strtod(format_arg, NULL) : 0.0;
                    int effective_width = left_align ? -width : width;
                    if (precision >= 0) {
                        printf(specifier == 'g' ? "%*.*g" : "%*.*G",
                               effective_width, precision, value);
                    } else {
                        printf(specifier == 'g' ? "%*g" : "%*G",
                               effective_width, value);
                    }
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                case 'e':
                case 'E': {
                    // Scientific notation
                    double value =
                        (arg_index < argc) ? strtod(format_arg, NULL) : 0.0;
                    int effective_width = left_align ? -width : width;
                    if (precision >= 0) {
                        printf(specifier == 'e' ? "%*.*e" : "%*.*E",
                               effective_width, precision, value);
                    } else {
                        printf(specifier == 'e' ? "%*e" : "%*E",
                               effective_width, value);
                    }
                    if (arg_index < argc) {
                        arg_index++;
                        args_used_this_pass++;
                    }
                    break;
                }
                default:
                    // Unknown specifier - just print as is
                    putchar('%');
                    putchar(specifier);
                    break;
                }
            } else if (format[i] == '\\' && format[i + 1] != '\0') {
                // Handle escaped characters
                i++;
                switch (format[i]) {
                case 'n':
                    putchar('\n');
                    break;
                case 't':
                    putchar('\t');
                    break;
                case 'r':
                    putchar('\r');
                    break;
                case 'b':
                    putchar('\b');
                    break;
                case 'f':
                    putchar('\f');
                    break;
                case 'a':
                    putchar('\a');
                    break;
                case 'v':
                    putchar('\v');
                    break;
                case '\\':
                    putchar('\\');
                    break;
                case '"':
                    putchar('"');
                    break;
                case '\'':
                    putchar('\'');
                    break;
                default:
                    // Unknown escape - print as literal
                    putchar('\\');
                    putchar(format[i]);
                    break;
                }
            } else {
                // Regular character
                putchar(format[i]);
            }
        }

        // If no format specifiers consumed arguments, stop to avoid infinite loop
        if (args_used_this_pass == 0) {
            break;
        }

    } while (arg_index < argc);

    return 0;
}

/**
 * @brief Check if a string is a valid shell variable identifier
 *
 * A valid identifier starts with a letter or underscore, followed
 * by zero or more alphanumeric characters or underscores.
 *
 * @param name The string to validate
 * @return 1 if valid identifier, 0 otherwise
 */
static int is_valid_identifier(const char *name) {
    if (!name || !*name) {
        return 0;
    }

    // First character must be letter or underscore
    if (!isalpha(*name) && *name != '_') {
        return 0;
    }

    // Subsequent characters must be alphanumeric or underscore
    for (const char *p = name + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Export shell variables to the environment
 *
 * With no arguments, prints all exported variables.
 * With arguments, exports the specified variables to child processes.
 * Supports VAR=value syntax for simultaneous assignment and export.
 *
 * @param argc Argument count
 * @param argv Argument vector with variable names/assignments
 * @return 0 on success, 1 on error
 */
int bin_export(int argc, char **argv) {
    if (argc == 1) {
        // Print all exported variables
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("export %s\n", *env);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            // Variable assignment: VAR=value
            size_t name_len = eq - argv[i];
            char *name = malloc(name_len + 1);
            if (!name) {
                error_message("export: memory allocation failed");
                return 1;
            }
            strncpy(name, argv[i], name_len);
            name[name_len] = '\0';

            const char *value = eq + 1;

            // Validate variable name
            if (!is_valid_identifier(name)) {
                error_message("export: invalid variable name: %s", name);
                free(name);
                return 1;
            }

            // Set variable value using modern API
            symtable_set_global(name, value);

            // Export the variable using modern API
            symtable_export_global(name);

            free(name);
        } else if (i + 2 < argc && strcmp(argv[i + 1], "=") == 0) {
            // Handle tokenized assignment: VAR = value
            const char *name = argv[i];
            const char *value = argv[i + 2];

            // Validate variable name
            if (!is_valid_identifier(name)) {
                error_message("export: invalid variable name: %s", name);
                return 1;
            }

            // Set variable value using modern API
            symtable_set_global(name, value);

            // Export the variable using modern API
            symtable_export_global(name);

            // Skip the = and value tokens
            i += 2;
        } else {
            // Just export existing variable
            if (!is_valid_identifier(argv[i])) {
                error_message("export: '%s' not a valid identifier", argv[i]);
                return 1;
            }

            // Check if variable exists and get its value
            char *current_value = symtable_get_global(argv[i]);
            if (current_value) {
                // Variable exists - just export it
                symtable_export_global(argv[i]);
            } else {
                // Variable doesn't exist - create with empty value and export
                symtable_set_global(argv[i], "");
                symtable_export_global(argv[i]);
            }
        }
    }

    return 0;
}

/**
 * @brief Source (execute) a script file in the current shell
 *
 * Reads and executes commands from the specified file in the current
 * shell environment. Variables set in the sourced file persist.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is the filename)
 * @return 0 on success, 1 on error, or last command's exit status
 */
int bin_source(int argc, char **argv) {
    if (argc < 2) {
        error_message("source: usage: source filename");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        error_message("source: cannot open '%s'", argv[1]);
        return 1;
    }

    // Get global executor for script context tracking
    executor_t *executor = get_global_executor();
    if (!executor) {
        fclose(file);
        error_message("source: no execution context available");
        return 1;
    }

    // Track source depth for return support
    // Save and reset source_return flag for this source level
    bool saved_source_return = executor->source_return;
    executor->source_depth++;
    executor->source_return = false;

    // Set script execution context for debugging
    executor_set_script_context(executor, argv[1], 1);

    char *complete_input;
    int result = 0;
    int construct_number = 1;

    // Read complete multi-line constructs instead of line by line
    while ((complete_input = get_input_complete(file)) != NULL) {
        // Check if return was called in sourced script
        if (executor->source_return) {
            free(complete_input);
            break;
        }

        // Skip empty constructs
        char *trimmed = complete_input;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;
        if (*trimmed == '\0') {
            free(complete_input);
            construct_number++;
            continue;
        }

        // Update script context for debugging
        executor_set_script_context(executor, argv[1], construct_number);

        // Parse and execute the complete construct
        int construct_result = parse_and_execute(complete_input);
        
        // Check for return from sourced script (exit code 200+)
        if (construct_result >= 200 && construct_result <= 455) {
            result = construct_result - 200;
            executor->source_return = true;
            free(complete_input);
            break;
        }
        
        if (construct_result != 0) {
            result = construct_result;
        }

        free(complete_input);
        construct_number++;
    }

    // Clear source tracking and restore parent's source_return state
    executor->source_depth--;
    executor->source_return = saved_source_return;

    // Clear script execution context
    executor_clear_script_context(executor);

    fclose(file);
    return result;
}

// Forward declarations for logical operator support
static int evaluate_test_expression(char **argv, int start, int end);
static int evaluate_single_test(char **argv, int start, int end);

/**
 * @brief Evaluate conditional expressions
 *
 * Enhanced POSIX-compliant test builtin with logical operators.
 * Supports file tests (-f, -d, -e, etc.), string tests (-z, -n, =, !=),
 * numeric comparisons (-eq, -ne, -lt, etc.), and logical operators (!, -a, -o).
 *
 * @param argc Argument count
 * @param argv Argument vector with test expression
 * @return 0 if expression is true, 1 if false, 2 on error
 */
int bin_test(int argc, char **argv) {
    if (argc == 1) {
        return 1; // False if no arguments
    }

    // Handle closing bracket for '[' command
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            error_message("test: '[' command missing closing ']'");
            return 2;
        }
        argc--; // Remove the closing bracket
    }

    // Use enhanced evaluation with logical operators
    return evaluate_test_expression(argv, 1, argc);
}

/**
 * @brief Recursively evaluate test expressions with logical operators
 *
 * Handles operator precedence: -o (OR) has lower precedence than -a (AND).
 * Also handles negation (!) operator.
 *
 * @param argv Argument vector
 * @param start Starting index in argv
 * @param end Ending index in argv (exclusive)
 * @return 0 if expression is true, 1 if false
 */
static int evaluate_test_expression(char **argv, int start, int end) {
    if (start >= end) {
        return 1; // Empty expression is false
    }

    // Handle negation operator
    if (start < end && strcmp(argv[start], "!") == 0) {
        int result = evaluate_test_expression(argv, start + 1, end);
        return (result == 0) ? 1 : 0; // Flip the result
    }

    // Find logical operators (-o has lower precedence than -a)
    // First pass: look for -o (OR)
    for (int i = start + 1; i < end - 1; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            int left = evaluate_test_expression(argv, start, i);
            int right = evaluate_test_expression(argv, i + 1, end);
            return (left == 0 || right == 0) ? 0 : 1;
        }
    }

    // Second pass: look for -a (AND)
    for (int i = start + 1; i < end - 1; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            int left = evaluate_test_expression(argv, start, i);
            int right = evaluate_test_expression(argv, i + 1, end);
            return (left == 0 && right == 0) ? 0 : 1;
        }
    }

    // No logical operators found, evaluate as single test
    return evaluate_single_test(argv, start, end);
}

/**
 * @brief Evaluate a single test condition
 *
 * Handles unary operators (-z, -n, -f, -d, etc.) and binary operators
 * (=, !=, -eq, -ne, -lt, -le, -gt, -ge).
 *
 * @param argv Argument vector
 * @param start Starting index in argv
 * @param end Ending index in argv (exclusive)
 * @return 0 if condition is true, 1 if false, 2 on error
 */
static int evaluate_single_test(char **argv, int start, int end) {
    int argc = end - start;

    // Simple test implementations
    if (argc == 1) {
        // test STRING - true if string is non-empty
        return (strlen(argv[start]) > 0) ? 0 : 1;
    }

    if (argc == 2) {
        if (strcmp(argv[start], "-z") == 0) {
            // test -z STRING - true if string is empty
            return (strlen(argv[start + 1]) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-n") == 0) {
            // test -n STRING - true if string is non-empty
            return (strlen(argv[start + 1]) > 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-f") == 0) {
            // test -f FILE - true if file exists and is regular
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISREG(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-d") == 0) {
            // test -d DIR - true if directory exists
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISDIR(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-e") == 0) {
            // test -e PATH - true if path exists
            struct stat st;
            return (stat(argv[start + 1], &st) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-c") == 0) {
            // test -c FILE - true if file is character device
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISCHR(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-b") == 0) {
            // test -b FILE - true if file is block device
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISBLK(st.st_mode)) ? 0
                                                                            : 1;
        } else if (strcmp(argv[start], "-L") == 0 ||
                   strcmp(argv[start], "-h") == 0) {
            // test -L FILE or -h FILE - true if file is symbolic link
            struct stat st;
            return (lstat(argv[start + 1], &st) == 0 && S_ISLNK(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-p") == 0) {
            // test -p FILE - true if file is named pipe (FIFO)
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISFIFO(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-S") == 0) {
            // test -S FILE - true if file is socket
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && S_ISSOCK(st.st_mode))
                       ? 0
                       : 1;
        } else if (strcmp(argv[start], "-r") == 0) {
            // test -r FILE - true if file is readable
            return (access(argv[start + 1], R_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-w") == 0) {
            // test -w FILE - true if file is writable
            return (access(argv[start + 1], W_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-x") == 0) {
            // test -x FILE - true if file is executable
            return (access(argv[start + 1], X_OK) == 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-s") == 0) {
            // test -s FILE - true if file exists and is not empty
            struct stat st;
            return (stat(argv[start + 1], &st) == 0 && st.st_size > 0) ? 0 : 1;
        } else if (strcmp(argv[start], "-t") == 0) {
            // test -t FD - true if file descriptor is open and refers to a terminal
            int fd = atoi(argv[start + 1]);
            return isatty(fd) ? 0 : 1;
        }
    }

    if (argc == 3) {
        if (strcmp(argv[start + 1], "=") == 0) {
            // test STRING1 = STRING2
            return (strcmp(argv[start], argv[start + 2]) == 0) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "!=") == 0) {
            // test STRING1 != STRING2
            return (strcmp(argv[start], argv[start + 2]) != 0) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-eq") == 0) {
            // test NUM1 -eq NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 == n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-ne") == 0) {
            // test NUM1 -ne NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 != n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-lt") == 0) {
            // test NUM1 -lt NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 < n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-le") == 0) {
            // test NUM1 -le NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 <= n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-gt") == 0) {
            // test NUM1 -gt NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 > n2) ? 0 : 1;
        } else if (strcmp(argv[start + 1], "-ge") == 0) {
            // test NUM1 -ge NUM2
            int n1 = atoi(argv[start]);
            int n2 = atoi(argv[start + 2]);
            return (n1 >= n2) ? 0 : 1;
        }
    }

    error_message("test: unknown test condition or invalid arguments");
    return 2;
}

/**
 * @brief Read a line of input into shell variables
 *
 * Enhanced POSIX-compliant read builtin that reads user input into variables.
 * Supports -p (prompt), -r (raw mode), -t (timeout), -n (nchars),
 * and -s (silent) options.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and variable name
 * @return 0 on success, 1 on EOF or error
 */
int bin_read(int argc, char **argv) {
    // Option flags
    char *prompt = NULL;
    bool raw_mode = false;
    int timeout_secs = -1;
    int nchars = -1;
    bool silent_mode = false;

    int opt_index = 1;

    // Parse options
    while (opt_index < argc && argv[opt_index][0] == '-') {
        char *arg = argv[opt_index];

        if (strcmp(arg, "-p") == 0) {
            // -p prompt: Display prompt before reading
            if (opt_index + 1 >= argc) {
                error_message("read: -p requires a prompt string");
                return 1;
            }
            prompt = argv[++opt_index];
        } else if (strcmp(arg, "-r") == 0) {
            // -r: Raw mode (don't interpret backslashes)
            raw_mode = true;
        } else if (strcmp(arg, "-t") == 0) {
            // -t timeout: Timeout after specified seconds
            if (opt_index + 1 >= argc) {
                error_message("read: -t requires a timeout value");
                return 1;
            }
            timeout_secs = atoi(argv[++opt_index]);
            if (timeout_secs < 0) {
                error_message("read: invalid timeout value");
                return 1;
            }
        } else if (strcmp(arg, "-n") == 0) {
            // -n nchars: Read only specified number of characters
            if (opt_index + 1 >= argc) {
                error_message("read: -n requires a character count");
                return 1;
            }
            nchars = atoi(argv[++opt_index]);
            if (nchars <= 0) {
                error_message("read: invalid character count");
                return 1;
            }
        } else if (strcmp(arg, "-s") == 0) {
            // -s: Silent mode (don't echo input)
            silent_mode = true;
        } else if (strcmp(arg, "--") == 0) {
            // End of options
            opt_index++;
            break;
        } else {
            error_message("read: invalid option: %s", arg);
            return 1;
        }
        opt_index++;
    }

    // Must have at least one variable name
    if (opt_index >= argc) {
        error_message("read: usage: read [-p prompt] [-r] variable_name");
        return 1;
    }

    // Validate variable name
    char *varname = argv[opt_index];
    if (!is_valid_identifier(varname)) {
        error_message("read: '%s' not a valid identifier", varname);
        return 1;
    }

    // Display prompt if specified
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    char *line = NULL;
    int result = 0;
    int fd = fileno(stdin);
    bool is_tty = isatty(fd);

    // Save original terminal settings if we need to modify them
    struct termios orig_termios, new_termios;
    bool termios_modified = false;

    if (is_tty && (silent_mode || nchars > 0)) {
        if (tcgetattr(fd, &orig_termios) == 0) {
            new_termios = orig_termios;

            if (silent_mode) {
                // Disable echo
                new_termios.c_lflag &= ~ECHO;
            }

            if (nchars > 0) {
                // Non-canonical mode for character-by-character reading
                new_termios.c_lflag &= ~ICANON;
                new_termios.c_cc[VMIN] = 1;
                new_termios.c_cc[VTIME] = 0;
            }

            if (tcsetattr(fd, TCSANOW, &new_termios) == 0) {
                termios_modified = true;
            }
        }
    }

    // Handle timeout with select()
    if (timeout_secs >= 0) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = timeout_secs;
        tv.tv_usec = 0;

        int select_result = select(fd + 1, &readfds, NULL, NULL, &tv);

        if (select_result <= 0) {
            // Timeout (0) or error (-1)
            if (termios_modified) {
                tcsetattr(fd, TCSANOW, &orig_termios);
            }
            symtable_set_global(varname, "");
            return (select_result == 0) ? 142 : 1; // 142 = timeout exit code
        }
    }

    // Read input based on options
    if (nchars > 0) {
        // Read exactly nchars characters
        line = malloc(nchars + 1);
        if (!line) {
            if (termios_modified) {
                tcsetattr(fd, TCSANOW, &orig_termios);
            }
            return 1;
        }

        int chars_read = 0;
        while (chars_read < nchars) {
            // Check timeout for each character if specified
            if (timeout_secs >= 0) {
                fd_set readfds;
                struct timeval tv;

                FD_ZERO(&readfds);
                FD_SET(fd, &readfds);
                tv.tv_sec = timeout_secs;
                tv.tv_usec = 0;

                int select_result = select(fd + 1, &readfds, NULL, NULL, &tv);
                if (select_result <= 0) {
                    line[chars_read] = '\0';
                    result = (select_result == 0) ? 142 : 1;
                    break;
                }
            }

            ssize_t n = read(fd, &line[chars_read], 1);
            if (n <= 0) {
                // EOF or error
                if (chars_read == 0) {
                    free(line);
                    line = NULL;
                    result = 1;
                } else {
                    line[chars_read] = '\0';
                }
                break;
            }

            // Stop at newline even in nchars mode
            if (line[chars_read] == '\n') {
                line[chars_read] = '\0';
                break;
            }

            chars_read++;
        }

        if (line && chars_read == nchars) {
            line[nchars] = '\0';
        }

        // Print newline if silent mode (since echo was disabled)
        if (silent_mode && is_tty) {
            printf("\n");
        }
    } else {
        // Normal line reading
        line = get_input(stdin);

        // Print newline if silent mode (since echo was disabled)
        if (silent_mode && is_tty && line) {
            printf("\n");
        }
    }

    // Restore terminal settings
    if (termios_modified) {
        tcsetattr(fd, TCSANOW, &orig_termios);
    }

    if (!line) {
        // EOF or input error
        symtable_set_global(varname, "");
        return result ? result : 1;
    }

    // Process backslashes unless in raw mode
    if (!raw_mode && line) {
        char *processed = malloc(strlen(line) + 1);
        if (processed) {
            int j = 0;
            for (int i = 0; line[i]; i++) {
                if (line[i] == '\\' && line[i + 1]) {
                    // Process escape sequences
                    i++; // Skip the backslash
                    switch (line[i]) {
                    case 'n':
                        processed[j++] = '\n';
                        break;
                    case 't':
                        processed[j++] = '\t';
                        break;
                    case 'r':
                        processed[j++] = '\r';
                        break;
                    case '\\':
                        processed[j++] = '\\';
                        break;
                    default:
                        processed[j++] = '\\';
                        processed[j++] = line[i];
                        break;
                    }
                } else {
                    processed[j++] = line[i];
                }
            }
            processed[j] = '\0';
            free(line);
            line = processed;
        }
    }

    // Set the variable using modern API
    symtable_set_global(varname, line ? line : "");

    if (line)
        free(line);
    return result;
}

/**
 * @brief Read lines from stdin into an array variable
 *
 * mapfile/readarray builtin - reads lines from standard input into
 * an indexed array variable. Supports various options for controlling
 * the reading behavior.
 *
 * Options:
 *   -d delim   Use delim as line delimiter instead of newline
 *   -n count   Read at most count lines (0 means all)
 *   -O origin  Start assigning at index origin (default 0)
 *   -s count   Skip the first count lines
 *   -t         Remove trailing delimiter from each line
 *   -u fd      Read from file descriptor fd instead of stdin
 *   -C callback  Execute callback for each line read
 *   -c quantum   Call callback every quantum lines
 *
 * @param argc Argument count
 * @param argv Argument vector with options and array name
 * @return 0 on success, 1 on error
 */
int bin_mapfile(int argc, char **argv) {
    // Default options
    char delim = '\n';
    int max_count = 0;  // 0 means read all
    int origin = 0;
    int skip_count = 0;
    bool trim_delim = false;
    int fd = STDIN_FILENO;
    char *callback = NULL;
    int callback_quantum = 5000;
    
    // Default array name
    char *array_name = "MAPFILE";
    
    int opt_index = 1;
    
    // Parse options
    while (opt_index < argc && argv[opt_index][0] == '-' && 
           argv[opt_index][1] != '\0') {
        char *arg = argv[opt_index];
        
        if (strcmp(arg, "-d") == 0) {
            // -d delim: Use delim as delimiter
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -d requires a delimiter");
                return 1;
            }
            opt_index++;
            if (argv[opt_index][0] != '\0') {
                delim = argv[opt_index][0];
            } else {
                delim = '\0';  // NUL delimiter
            }
        } else if (strcmp(arg, "-n") == 0) {
            // -n count: Read at most count lines
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -n requires a count");
                return 1;
            }
            max_count = atoi(argv[++opt_index]);
            if (max_count < 0) {
                error_message("mapfile: invalid count");
                return 1;
            }
        } else if (strcmp(arg, "-O") == 0) {
            // -O origin: Start index
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -O requires an origin index");
                return 1;
            }
            origin = atoi(argv[++opt_index]);
            if (origin < 0) {
                error_message("mapfile: invalid origin");
                return 1;
            }
        } else if (strcmp(arg, "-s") == 0) {
            // -s count: Skip lines
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -s requires a count");
                return 1;
            }
            skip_count = atoi(argv[++opt_index]);
            if (skip_count < 0) {
                error_message("mapfile: invalid skip count");
                return 1;
            }
        } else if (strcmp(arg, "-t") == 0) {
            // -t: Trim delimiter
            trim_delim = true;
        } else if (strcmp(arg, "-u") == 0) {
            // -u fd: Read from file descriptor
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -u requires a file descriptor");
                return 1;
            }
            fd = atoi(argv[++opt_index]);
            if (fd < 0) {
                error_message("mapfile: invalid file descriptor");
                return 1;
            }
        } else if (strcmp(arg, "-C") == 0) {
            // -C callback: Callback command
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -C requires a callback command");
                return 1;
            }
            callback = argv[++opt_index];
        } else if (strcmp(arg, "-c") == 0) {
            // -c quantum: Callback frequency
            if (opt_index + 1 >= argc) {
                error_message("mapfile: -c requires a quantum");
                return 1;
            }
            callback_quantum = atoi(argv[++opt_index]);
            if (callback_quantum <= 0) {
                error_message("mapfile: invalid quantum");
                return 1;
            }
        } else if (strcmp(arg, "--") == 0) {
            opt_index++;
            break;
        } else {
            error_message("mapfile: invalid option: %s", arg);
            return 1;
        }
        opt_index++;
    }
    
    // Get array name if provided
    if (opt_index < argc) {
        array_name = argv[opt_index];
        if (!is_valid_identifier(array_name)) {
            error_message("mapfile: '%s' not a valid identifier", array_name);
            return 1;
        }
    }
    
    // Clear existing array (unless using -O with non-zero origin)
    if (origin == 0) {
        symtable_unset_global(array_name);
    }
    
    // Read lines
    FILE *input = (fd == STDIN_FILENO) ? stdin : fdopen(fd, "r");
    if (!input) {
        error_message("mapfile: cannot open file descriptor %d", fd);
        return 1;
    }
    
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    int lines_read = 0;
    int lines_skipped = 0;
    int array_index = origin;
    
    while ((line_len = getdelim(&line, &line_cap, delim, input)) != -1) {
        // Skip lines if requested
        if (lines_skipped < skip_count) {
            lines_skipped++;
            continue;
        }
        
        // Check max count - but continue reading to consume input
        if (max_count > 0 && lines_read >= max_count) {
            // Consume remaining input to prevent it from being executed
            while (getdelim(&line, &line_cap, delim, input) != -1) {
                // Just discard
            }
            break;
        }
        
        // Trim delimiter if requested
        char *value = line;
        if (trim_delim && line_len > 0 && line[line_len - 1] == delim) {
            line[line_len - 1] = '\0';
        }
        
        // Set array element - convert index to string
        char index_str[32];
        snprintf(index_str, sizeof(index_str), "%d", array_index);
        symtable_set_array_element(array_name, index_str, value);
        
        array_index++;
        lines_read++;
        
        // Execute callback if specified
        if (callback && (lines_read % callback_quantum) == 0) {
            // Execute callback with index and line
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "%s %d", callback, array_index - 1);
            // Would need executor access here - skip for now
            // executor_execute_command_line(executor, cmd);
        }
    }
    
    free(line);
    
    // Close fd if we opened it (not stdin)
    if (fd != STDIN_FILENO && input != stdin) {
        fclose(input);
    }
    
    // Suppress unused variable warning
    (void)callback;
    (void)callback_quantum;
    
    return 0;
}

/**
 * @brief Evaluate arguments as shell commands
 *
 * Concatenates all arguments into a single command string and
 * executes it in the current shell context.
 *
 * @param argc Argument count
 * @param argv Argument vector with command fragments
 * @return Exit status of the evaluated command, or 0 if no arguments
 */
int bin_eval(int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }

    // Concatenate all arguments
    size_t total_len = 0;
    for (int i = 1; i < argc; i++) {
        total_len += strlen(argv[i]) + 1; // +1 for space
    }

    char *command = malloc(total_len);
    if (!command) {
        return 1;
    }

    command[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            strcat(command, " ");
        }
        strcat(command, argv[i]);
    }

    // Execute the command string
    int result = parse_and_execute(command);

    free(command);
    return result;
}

/**
 * @brief Check if a command name is a shell builtin
 *
 * Searches the builtins table for the specified command name.
 *
 * @param name The command name to check
 * @return true if name is a builtin, false otherwise
 */
bool is_builtin(const char *name) {
    for (size_t i = 0; i < builtins_count; i++) {
        if (strcmp(name, builtins[i].name) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Return success status
 *
 * Always returns 0 (success). Used in shell scripts and conditionals.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 0
 */
int bin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

/**
 * @brief Return failure status
 *
 * Always returns 1 (failure). Used in shell scripts and conditionals.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return Always returns 1
 */
int bin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}

/**
 * @brief Get current source location from executor context stack
 *
 * Returns the most recent source location from the executor's context stack,
 * or SOURCE_LOC_UNKNOWN if no context is available.
 */
static source_location_t builtin_get_source_location(void) {
    if (current_executor && current_executor->context_depth > 0) {
        return current_executor->context_locations[current_executor->context_depth - 1];
    }
    return SOURCE_LOC_UNKNOWN;
}

/**
 * @brief Evaluate arithmetic expressions (let builtin)
 *
 * Evaluates each argument as an arithmetic expression.
 * Returns 0 if the last expression evaluates to non-zero,
 * returns 1 if the last expression evaluates to zero.
 *
 * Usage: let expr [expr ...]
 *
 * Examples:
 *   let x=5+3        # Sets x to 8
 *   let "x += 1"     # Increments x
 *   let a=1 b=2 c=a+b  # Multiple expressions
 *
 * @param argc Argument count
 * @param argv Argument vector containing arithmetic expressions
 * @return 0 if last expression is non-zero, 1 if zero or on error
 */
int bin_let(int argc, char **argv) {
    if (argc < 2) {
        source_location_t loc = builtin_get_source_location();
        shell_error_t *error = shell_error_create(
            SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
            loc, "missing arithmetic expression");
        if (error) {
            /* Add executor context stack if available */
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth; i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(error, "%s",
                            current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_push_context(error, "in builtin 'let'");
            shell_error_set_detail(error, "let requires at least one expression to evaluate");
            shell_error_set_suggestion(error,
                "usage: let expr [expr ...]\n"
                "   examples: let x=5+3    let \"x++\"    let a=1 b=2 c=a+b");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
        }
        return 1;
    }

    ssize_t last_result = 0;

    /* Evaluate each argument as an arithmetic expression */
    for (int i = 1; i < argc; i++) {
        arithm_clear_error();
        char *result = arithm_expand(argv[i]);

        if (arithm_error_flag || !result) {
            const char *err_msg = arithm_get_last_error();
            source_location_t loc = builtin_get_source_location();
            shell_error_t *error = shell_error_create(
                SHELL_ERR_ARITHMETIC_SYNTAX, SHELL_SEVERITY_ERROR,
                loc, "invalid expression '%s'", argv[i]);
            if (error) {
                /* Add executor context stack if available */
                if (current_executor) {
                    for (size_t i = 0; i < current_executor->context_depth; i++) {
                        if (current_executor->context_stack[i]) {
                            shell_error_push_context(error, "%s",
                                current_executor->context_stack[i]);
                        }
                    }
                }
                shell_error_push_context(error, "in builtin 'let'");
                shell_error_push_context(error, "evaluating argument %d of %d", i, argc - 1);
                if (err_msg) {
                    shell_error_set_detail(error, err_msg);
                }
                shell_error_set_suggestion(error,
                    "supported operators: + - * / %% ** ++ -- = += -= *= /= %%=\n"
                    "   comparisons: == != < > <= >= && || !\n"
                    "   note: variables don't need $: let x=5 y=x+1");
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
            }
            if (result) {
                free(result);
            }
            return 1;
        }

        last_result = strtol(result, NULL, 10);
        free(result);
    }

    /* Return 0 if last result is non-zero, 1 if zero (bash behavior) */
    return (last_result == 0) ? 1 : 0;
}

/**
 * @brief Set or unset shell options
 *
 * Manages shell behavior flags like errexit, nounset, etc.
 * With no arguments, displays all shell variables.
 *
 * @param argc Argument count
 * @param argv Argument vector with option flags
 * @return Result from builtin_set()
 */
int bin_set(int argc, char **argv) {
    (void)argc;
    return builtin_set(argv);
}

/**
 * @brief List active background jobs
 *
 * Displays all active jobs managed by the current executor.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector with job options
 * @return 0 on success, 1 if no executor available
 */
int bin_jobs(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_jobs(current_executor, argv);
    }
    return 1;
}

/**
 * @brief Bring a background job to the foreground
 *
 * Resumes a stopped job or brings a background job to the foreground.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (argv[1] is optional job specification)
 * @return 0 on success, 1 on error or no current job
 */
int bin_fg(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_fg(current_executor, argv);
    }
    fprintf(stderr, "fg: no current job\n");
    return 1;
}

/**
 * @brief Resume a job in the background
 *
 * Resumes a stopped job and runs it in the background.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (argv[1] is optional job specification)
 * @return 0 on success, 1 on error or no current job
 */
int bin_bg(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_bg(current_executor, argv);
    }
    fprintf(stderr, "bg: no current job\n");
    return 1;
}

/**
 * @brief Disown jobs from the shell
 *
 * Removes jobs from the shell's job table or marks them to not receive
 * SIGHUP when the shell exits (login shell behavior).
 *
 * Usage: disown [-h] [-a | -r] [jobspec ...]
 *   -h  Mark jobs to not receive SIGHUP (keep in job table)
 *   -a  Apply to all jobs
 *   -r  Apply to running jobs only
 *   Without options: remove job(s) from job table
 *   Without jobspec: apply to current job
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_disown(int argc, char **argv) {
    if (!current_executor) {
        fprintf(stderr, "disown: no job control\n");
        return 1;
    }

    bool flag_h = false;  // Mark no_sighup instead of removing
    bool flag_a = false;  // Apply to all jobs
    bool flag_r = false;  // Apply to running jobs only

    // Parse options
    int optind_local = 1;
    while (optind_local < argc && argv[optind_local][0] == '-' &&
           argv[optind_local][1] != '\0') {
        const char *opt = argv[optind_local];
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'h':
                flag_h = true;
                break;
            case 'a':
                flag_a = true;
                break;
            case 'r':
                flag_r = true;
                break;
            default:
                fprintf(stderr, "disown: -%c: invalid option\n", opt[i]);
                fprintf(stderr, "disown: usage: disown [-h] [-a | -r] [jobspec ...]\n");
                return 1;
            }
        }
        optind_local++;
    }

    // -a and -r are mutually exclusive in practice
    // -a means all, -r means running only

    int result = 0;

    if (flag_a || flag_r) {
        // Apply to all jobs (or running jobs if -r)
        job_t *job = current_executor->jobs;
        job_t *prev = NULL;

        while (job) {
            job_t *next = job->next;

            // Skip non-running jobs if -r flag is set
            if (flag_r && job->state != JOB_RUNNING) {
                prev = job;
                job = next;
                continue;
            }

            if (flag_h) {
                // Mark job to not receive SIGHUP
                job->no_sighup = true;
                prev = job;
            } else {
                // Remove job from table
                if (prev) {
                    prev->next = next;
                } else {
                    current_executor->jobs = next;
                }
                // Free job resources
                if (job->processes) {
                    process_t *p = job->processes;
                    while (p) {
                        process_t *pnext = p->next;
                        free(p->command);
                        free(p);
                        p = pnext;
                    }
                }
                free(job->command_line);
                free(job);
                // Don't update prev since we removed current
            }

            job = next;
        }
    } else if (optind_local < argc) {
        // Apply to specified jobspecs
        for (int i = optind_local; i < argc; i++) {
            const char *jobspec = argv[i];
            int job_id;

            // Parse jobspec (accept %N or just N)
            if (jobspec[0] == '%') {
                job_id = atoi(jobspec + 1);
            } else {
                job_id = atoi(jobspec);
            }

            if (job_id <= 0) {
                fprintf(stderr, "disown: %s: invalid job specification\n", jobspec);
                result = 1;
                continue;
            }

            job_t *job = executor_find_job(current_executor, job_id);
            if (!job) {
                fprintf(stderr, "disown: %s: no such job\n", jobspec);
                result = 1;
                continue;
            }

            if (flag_h) {
                job->no_sighup = true;
            } else {
                executor_remove_job(current_executor, job_id);
            }
        }
    } else {
        // Apply to current job (most recent background job)
        // Find the highest job_id (most recent)
        job_t *current_job = NULL;
        int max_id = 0;
        for (job_t *j = current_executor->jobs; j; j = j->next) {
            if (j->job_id > max_id) {
                max_id = j->job_id;
                current_job = j;
            }
        }

        if (!current_job) {
            fprintf(stderr, "disown: current: no such job\n");
            return 1;
        }

        if (flag_h) {
            current_job->no_sighup = true;
        } else {
            executor_remove_job(current_executor, current_job->job_id);
        }
    }

    return result;
}

/**
 * @brief Shift positional parameters left
 *
 * Shifts positional parameters ($1, $2, etc.) left by n positions.
 * Default shift count is 1. Updates $# and individual parameters.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional shift count)
 * @return 0 on success, 1 on invalid argument
 */
int bin_shift(int argc, char **argv) {
    int shift_count = 1; // Default shift by 1

    // Parse optional shift count argument
    if (argc > 1) {
        char *endptr;
        shift_count = strtol(argv[1], &endptr, 10);

        // Validate that the argument is a valid number
        if (*endptr != '\0' || shift_count < 0) {
            fprintf(stderr, "shift: %s: numeric argument required\n", argv[1]);
            return 1;
        }
    }

    // Check if we're in a function scope
    symtable_manager_t *mgr = symtable_get_global_manager();
    if (mgr && symtable_in_function_scope(mgr)) {
        // In function scope - shift local positional parameters
        char *argc_str = symtable_get_var(mgr, "#");
        int func_argc = argc_str ? atoi(argc_str) : 0;
        free(argc_str);

        // Limit shift count to available parameters
        if (shift_count > func_argc) {
            shift_count = func_argc;
        }

        if (shift_count > 0 && func_argc > 0) {
            int new_argc = func_argc - shift_count;

            // Collect values of parameters that will remain after shift
            char **new_values = malloc((new_argc + 1) * sizeof(char *));
            if (!new_values) {
                return 1;
            }

            for (int i = 0; i < new_argc; i++) {
                char param_name[16];
                snprintf(param_name, sizeof(param_name), "%d",
                         i + 1 + shift_count);
                new_values[i] = symtable_get_var(mgr, param_name);
            }
            new_values[new_argc] = NULL;

            // Update positional parameters with shifted values
            for (int i = 1; i <= func_argc; i++) {
                char param_name[16];
                snprintf(param_name, sizeof(param_name), "%d", i);
                if (i <= new_argc && new_values[i - 1]) {
                    symtable_set_local_var(mgr, param_name, new_values[i - 1]);
                } else {
                    // Clear parameters beyond new count
                    symtable_set_local_var(mgr, param_name, "");
                }
            }

            // Free collected values
            for (int i = 0; i < new_argc; i++) {
                free(new_values[i]);
            }
            free(new_values);

            // Update $#
            char new_argc_str[16];
            snprintf(new_argc_str, sizeof(new_argc_str), "%d", new_argc);
            symtable_set_local_var(mgr, "#", new_argc_str);
        }

        return 0;
    }

    // Not in function scope - shift global shell_argv
    // Calculate available parameters to shift (excluding script name at
    // argv[0])
    int available_params = shell_argc > 1 ? shell_argc - 1 : 0;

    // If shift count exceeds available parameters, limit to available count
    // This matches POSIX behavior - don't error, just shift what's available
    if (shift_count > available_params) {
        shift_count = available_params;
    }

    // Perform the shift by adjusting shell_argc and shell_argv
    if (shift_count > 0 && shell_argc > 1) {
        // Shift the argv array
        for (int i = 1; i < shell_argc - shift_count; i++) {
            shell_argv[i] = shell_argv[i + shift_count];
        }

        // Update argc to reflect the new parameter count
        shell_argc -= shift_count;

        // Update shell variables to reflect the new parameter count
        // Update $# (parameter count)
        char param_count_str[16];
        snprintf(param_count_str, sizeof(param_count_str), "%d",
                 shell_argc > 1 ? shell_argc - 1 : 0);
        symtable_set_global("#", param_count_str);

        // Update individual positional parameters ($1, $2, etc.)
        // Clear old parameters first
        for (int i = 1; i <= 9; i++) {
            char param_name[3];
            snprintf(param_name, sizeof(param_name), "%d", i);
            if (i < shell_argc && shell_argv[i]) {
                symtable_set_global(param_name, shell_argv[i]);
            } else {
                symtable_set_global(param_name, "");
            }
        }
    }

    return 0;
}
/**
 * @brief Break out of enclosing loop
 *
 * Exits from a for, while, or until loop. An optional numeric argument
 * specifies how many levels of loops to break out of.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or invalid argument
 */
int bin_break(int argc, char **argv) {
    if (!current_executor) {
        fprintf(stderr, "break: not currently in a loop\n");
        return 1;
    }

    // Check if we're actually in a loop
    if (current_executor->loop_depth <= 0) {
        fprintf(stderr, "break: not currently in a loop\n");
        return 1;
    }

    // Parse optional level argument (break n)
    int break_level = 1;
    if (argc > 1) {
        char *endptr;
        break_level = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || break_level <= 0) {
            fprintf(stderr, "break: %s: numeric argument required\n", argv[1]);
            return 1;
        }

        if (break_level > current_executor->loop_depth) {
            fprintf(stderr,
                    "break: %d: cannot break %d levels (only %d nested)\n",
                    break_level, break_level, current_executor->loop_depth);
            return 1;
        }
    }

    // Set loop control state to break
    current_executor->loop_control = LOOP_BREAK;

    return 0;
}

/**
 * @brief Continue to the next iteration of enclosing loop
 *
 * Skips the remaining commands in the current loop iteration and
 * continues with the next iteration. An optional numeric argument
 * specifies which enclosing loop to continue.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional loop level)
 * @return 0 on success, 1 if not in a loop or invalid argument
 */
int bin_continue(int argc, char **argv) {
    if (!current_executor) {
        fprintf(stderr, "continue: not currently in a loop\n");
        return 1;
    }

    // Check if we're actually in a loop
    if (current_executor->loop_depth <= 0) {
        fprintf(stderr, "continue: not currently in a loop\n");
        return 1;
    }

    // Parse optional level argument (continue n)
    int continue_level = 1;
    if (argc > 1) {
        char *endptr;
        continue_level = strtol(argv[1], &endptr, 10);

        if (*endptr != '\0' || continue_level <= 0) {
            fprintf(stderr, "continue: %s: numeric argument required\n",
                    argv[1]);
            return 1;
        }

        if (continue_level > current_executor->loop_depth) {
            fprintf(
                stderr,
                "continue: %d: cannot continue %d levels (only %d nested)\n",
                continue_level, continue_level, current_executor->loop_depth);
            return 1;
        }
    }

    // Set loop control state to continue
    current_executor->loop_control = LOOP_CONTINUE;

    return 0;
}

/**
 * @brief Set a string return value for the current function
 *
 * Lush extension (not POSIX) that allows functions to return
 * string values via command substitution. Not available in POSIX mode.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is the return value)
 * @return 0 on success, 1 on error or in POSIX mode
 */
int bin_return_value(int argc, char **argv) {
    // POSIX compliance: return_value is not available in strict POSIX mode
    if (is_posix_mode_enabled()) {
        fprintf(stderr, "return_value: not available in POSIX mode\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "return_value: missing value argument\n");
        return 1;
    }

    // Output the return value with a special marker that command substitution
    // can recognize
    printf("__LUSH_RETURN__:%s:__END__\n", argv[1]);
    fflush(stdout);

    // Return success
    return 0;
}

/**
 * @brief Return from a function with optional exit code
 *
 * Exits from a shell function with the specified exit status.
 * If no argument is given, returns with status 0.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional exit code)
 * @return Special return code (200 + exit_code) for executor recognition
 */
int bin_return(int argc, char **argv) {
    // Default to last exit status (POSIX behavior)
    int return_code = last_exit_status;

    // Get executor context to check if we're in a valid return context
    executor_t *executor = get_global_executor();

    // Check if we're in a function or sourced script
    bool in_function = executor && executor->symtable &&
                       symtable_in_function_scope(executor->symtable);
    bool in_source = executor && executor->source_depth > 0;

    if (!in_function && !in_source) {
        fprintf(stderr, "return: can only `return' from a function or sourced script\n");
        return 1;
    }

    // Parse optional return code argument
    if (argc > 1) {
        char *endptr;
        return_code = strtol(argv[1], &endptr, 10);

        // Validate that the argument is a valid number
        if (*endptr != '\0') {
            fprintf(stderr, "return: %s: numeric argument required\n", argv[1]);
            return 1;
        }
    }

    // Set the last exit status to the return code
    last_exit_status = return_code;

    // Return a special exit code that the executor can recognize as "function
    // return" We'll use a specific value that doesn't conflict with normal exit
    // codes
    return 200 + (return_code & 0xFF); // 200-455 range for function/source returns
}

/**
 * @brief Set or display signal traps
 *
 * Manages signal handlers for the shell. Can set, display, or reset
 * traps for signals like EXIT, INT, TERM, HUP, etc.
 *
 * @param argc Argument count
 * @param argv Argument vector with trap action and signal names
 * @return 0 on success, 1 on error
 */
int bin_trap(int argc, char **argv) {
    // If no arguments, list all traps
    if (argc == 1) {
        list_traps();
        return 0;
    }

    // Handle special options
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        // List available signals
        printf("EXIT  0) exit from shell\n");
        printf("HUP   1) hangup\n");
        printf("INT   2) interrupt\n");
        printf("QUIT  3) quit\n");
        printf("TERM  15) software termination signal\n");
        printf("USR1  10) user defined signal 1\n");
        printf("USR2  12) user defined signal 2\n");
        return 0;
    }

    // Parse arguments: trap [-l] [action] [signal ...]
    int arg_index = 1;

    // Skip -l option if present
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        arg_index = 2;
    }

    // Need at least action argument
    if (arg_index >= argc) {
        fprintf(stderr, "trap: usage: trap [-l] [action] [signal ...]\n");
        return 1;
    }

    const char *action = argv[arg_index++];

    // If no signals specified, this is an error
    if (arg_index >= argc) {
        fprintf(stderr, "trap: usage: trap [-l] [action] [signal ...]\n");
        return 1;
    }

    // Process each signal
    for (int i = arg_index; i < argc; i++) {
        int signal = get_signal_number(argv[i]);

        if (signal < 0) {
            fprintf(stderr, "trap: %s: invalid signal specification\n",
                    argv[i]);
            return 1;
        }

        // Handle special cases
        if (strcmp(action, "-") == 0) {
            // Reset to default
            remove_trap(signal);
        } else if (strcmp(action, "") == 0 || strcmp(action, "\"\"") == 0) {
            // Ignore signal
            if (signal == 0) {
                // EXIT trap - remove it
                remove_trap(signal);
            } else {
                set_trap(signal, "");
            }
        } else {
            // Set trap command
            if (set_trap(signal, action) != 0) {
                fprintf(stderr, "trap: failed to set trap for signal %s\n",
                        argv[i]);
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Replace the shell process with a command
 *
 * Replaces the current shell with the specified command using execvp.
 * Can also be used for file descriptor manipulation (redirections only).
 * Executes EXIT traps before replacing the process.
 *
 * @param argc Argument count
 * @param argv Argument vector with command and arguments
 * @return Does not return on success; 1 on error or restricted mode
 */
int bin_exec(int argc, char **argv) {
    // Privileged mode security check
    if (shell_opts.privileged_mode) {
        fprintf(stderr, "exec: restricted command in privileged mode\n");
        return 1;
    }

    // If no arguments, exec does nothing and returns success
    if (argc == 1) {
        return 0;
    }

    // Check for redirection-only exec (exec < file, exec > file, etc.)
    bool has_redirections = false;
    bool has_command = false;

    // Scan arguments to determine if this is redirection-only or command exec
    for (int i = 1; i < argc; i++) {
        if (strchr(argv[i], '<') || strchr(argv[i], '>')) {
            has_redirections = true;
        } else if (argv[i][0] != '<' && argv[i][0] != '>' &&
                   !isdigit(argv[i][0])) {
            has_command = true;
            break;
        }
    }

    // If only redirections, handle file descriptor manipulation
    if (has_redirections && !has_command) {
        // TODO: Implement redirection-only exec
        // For now, we'll focus on command replacement exec
        fprintf(stderr, "exec: redirection-only exec not yet implemented\n");
        return 1;
    }

    // Command replacement exec - find the command to execute
    char *command = NULL;
    char **exec_argv = NULL;
    int exec_argc = 0;
    (void)exec_argc; /* Reserved for argument count validation */

    // Find the first non-redirection argument as the command
    int cmd_start = 1;
    while (cmd_start < argc &&
           (argv[cmd_start][0] == '<' || argv[cmd_start][0] == '>' ||
            isdigit(argv[cmd_start][0]))) {
        cmd_start++;
    }

    if (cmd_start >= argc) {
        fprintf(stderr, "exec: no command specified\n");
        return 1;
    }

    command = argv[cmd_start];
    exec_argc = argc - cmd_start;
    exec_argv = &argv[cmd_start];

    // Execute EXIT traps before replacing the process
    execute_exit_traps();

    // Flush all output streams before exec
    fflush(stdout);
    fflush(stderr);
    fflush(stdin);

    // Try to execute the command using execvp
    // This replaces the current process entirely
    execvp(command, exec_argv);

    // If we get here, exec failed
    int saved_errno = errno;
    shell_error_t *error = shell_error_create(
        SHELL_ERR_COMMAND_NOT_FOUND, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
        "exec: %s: %s", command, strerror(saved_errno));
    shell_error_display(error, stderr, isatty(STDERR_FILENO));
    shell_error_free(error);

    // exec failure should exit the shell with error status
    exit(127);
}

/**
 * @brief Wait for background jobs to complete
 *
 * With no arguments, waits for all background jobs. With arguments,
 * waits for specific job IDs (%n) or process IDs.
 *
 * @param argc Argument count
 * @param argv Argument vector with optional job/process IDs
 * @return Exit status of the last waited process
 */
int bin_wait(int argc, char **argv) {
    // Get the current executor to access job control
    if (!current_executor) {
        // If no executor, there are no jobs to wait for
        return 0;
    }

    // If no arguments, wait for all background jobs
    if (argc == 1) {
        executor_update_job_status(current_executor);

        // Wait for all running jobs
        job_t *job = current_executor->jobs;
        int last_exit_status = 0;

        while (job) {
            if (job->state == JOB_RUNNING) {
                int status;
                pid_t result = waitpid(-job->pgid, &status, 0);

                if (result > 0) {
                    if (WIFEXITED(status)) {
                        last_exit_status = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        last_exit_status = 128 + WTERMSIG(status);
                    } else {
                        last_exit_status = 1;
                    }

                    // Mark job as done
                    job->state = JOB_DONE;
                }
            }
            job = job->next;
        }

        // Clean up completed jobs
        executor_update_job_status(current_executor);

        return last_exit_status;
    }

    // Wait for specific job(s) or process(es)
    int overall_exit_status = 0;

    for (int i = 1; i < argc; i++) {
        char *endptr;
        long target = strtol(argv[i], &endptr, 10);

        // Check for job ID syntax (%n)
        bool is_job_id = false;
        int job_or_pid = (int)target;

        if (argv[i][0] == '%') {
            is_job_id = true;
            // Re-parse without the % sign
            job_or_pid = (int)strtol(argv[i] + 1, &endptr, 10);
            if (*endptr != '\0' || job_or_pid <= 0) {
                fprintf(stderr, "wait: %s: not a valid job ID\n", argv[i]);
                return 1;
            }
        } else {
            if (*endptr != '\0' || target <= 0) {
                fprintf(stderr,
                        "wait: %s: arguments must be process or job IDs\n",
                        argv[i]);
                return 1;
            }
        }

        if (is_job_id) {
            // Wait for specific job
            job_t *job = executor_find_job(current_executor, job_or_pid);
            if (!job) {
                fprintf(stderr, "wait: %%%d: no such job\n", job_or_pid);
                return 127;
            }

            if (job->state == JOB_RUNNING) {
                int status;
                pid_t result = waitpid(-job->pgid, &status, 0);

                if (result > 0) {
                    if (WIFEXITED(status)) {
                        overall_exit_status = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        overall_exit_status = 128 + WTERMSIG(status);
                    } else {
                        overall_exit_status = 1;
                    }

                    job->state = JOB_DONE;
                }
            } else if (job->state == JOB_DONE) {
                // Job already completed - return 0
                overall_exit_status = 0;
            }
        } else {
            // Wait for specific PID
            int status;
            pid_t result = waitpid(job_or_pid, &status, 0);

            if (result == -1) {
                if (errno == ECHILD) {
                    // Process doesn't exist or not a child
                    fprintf(stderr,
                            "wait: pid %d is not a child of this shell\n",
                            job_or_pid);
                    return 127;
                } else {
                    int saved_errno = errno;
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN, "wait: %s", strerror(saved_errno));
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    return 1;
                }
            } else if (result > 0) {
                if (WIFEXITED(status)) {
                    overall_exit_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    overall_exit_status = 128 + WTERMSIG(status);
                } else {
                    overall_exit_status = 1;
                }
            }
        }
    }

    // Clean up completed jobs
    executor_update_job_status(current_executor);

    return overall_exit_status;
}

/**
 * @brief Set or display the file creation mask
 *
 * With no arguments, displays the current umask in octal format.
 * With an octal argument, sets the new file creation mask.
 *
 * @param argc Argument count
 * @param argv Argument vector (argv[1] is optional octal mask)
 * @return 0 on success, 1 on invalid mode
 */
int bin_umask(int argc, char **argv) {
    // If no arguments, display current umask
    if (argc == 1) {
        mode_t current_mask = umask(0); // Get current mask
        umask(current_mask);            // Restore it
        printf("%04o\n", current_mask);
        return 0;
    }

    // If one argument, set new umask
    if (argc == 2) {
        // Check for empty argument
        if (argv[1][0] == '\0') {
            fprintf(stderr, "umask: invalid mode\n");
            return 1;
        }

        char *endptr;
        long mask_val = strtol(argv[1], &endptr, 8); // Parse as octal

        // Validate argument
        if (*endptr != '\0' || mask_val < 0 || mask_val > 0777) {
            fprintf(stderr, "umask: %s: invalid mode\n", argv[1]);
            return 1;
        }

        umask((mode_t)mask_val);
        return 0;
    }

    // Too many arguments
    fprintf(stderr, "umask: too many arguments\n");
    return 1;
}

/**
 * @brief Set or display resource limits
 *
 * Displays or modifies shell resource limits (file size, open files,
 * CPU time, stack size, etc.). Supports -a to show all limits,
 * -H for hard limits, -S for soft limits.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and limit values
 * @return 0 on success, 1 on error
 */
int bin_ulimit(int argc, char **argv) {
    int opt;
    int resource = RLIMIT_FSIZE; // Default to file size limit
    bool show_all = false;
    bool hard_limit = false;
    char *limit_value = NULL;

    // Reset getopt for this call
    optind = 1;

    // Parse options
    while ((opt = getopt(argc, argv, "aHSfntsuvh")) != -1) {
        switch (opt) {
        case 'a':
            show_all = true;
            break;
        case 'H':
            hard_limit = true;
            break;
        case 'S':
            hard_limit = false; // Soft limit (default)
            break;
        case 'f':
            resource = RLIMIT_FSIZE;
            break;
        case 'n':
            resource = RLIMIT_NOFILE;
            break;
        case 't':
            resource = RLIMIT_CPU;
            break;
        case 's':
#ifdef RLIMIT_STACK
            resource = RLIMIT_STACK;
#else
            fprintf(stderr, "ulimit: -s not supported on this system\n");
            return 1;
#endif
            break;
        case 'u':
#ifdef RLIMIT_NPROC
            resource = RLIMIT_NPROC;
#else
            fprintf(stderr, "ulimit: -u not supported on this system\n");
            return 1;
#endif
            break;
        case 'v':
#ifdef RLIMIT_AS
            resource = RLIMIT_AS;
#else
            fprintf(stderr, "ulimit: -v not supported on this system\n");
            return 1;
#endif
            break;
        case 'h':
            printf("ulimit: set or display resource limits\n");
            printf("Options:\n");
            printf("  -a     Display all limits\n");
            printf("  -H     Set hard limit\n");
            printf("  -S     Set soft limit (default)\n");
            printf("  -f     File size limit (512-byte blocks)\n");
            printf("  -n     Number of open files\n");
            printf("  -t     CPU time limit (seconds)\n");
#ifdef RLIMIT_STACK
            printf("  -s     Stack size limit (1024-byte blocks)\n");
#endif
#ifdef RLIMIT_NPROC
            printf("  -u     Number of user processes\n");
#endif
#ifdef RLIMIT_AS
            printf("  -v     Virtual memory limit (1024-byte blocks)\n");
#endif
            return 0;
        default:
            fprintf(stderr, "ulimit: invalid option -%c\n", optopt);
            return 1;
        }
    }

    // Get remaining argument (limit value)
    if (optind < argc) {
        limit_value = argv[optind];
    }

    if (show_all) {
        // Display all limits
        struct rlimit rlim;

#ifdef RLIMIT_CORE
        printf("core file size          (blocks, -c) ");
        if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 512));
            }
        } else {
            printf("unknown\n");
        }
#endif

#ifdef RLIMIT_DATA
        printf("data seg size           (kbytes, -d) ");
        if (getrlimit(RLIMIT_DATA, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }
#endif

        printf("file size               (blocks, -f) ");
        if (getrlimit(RLIMIT_FSIZE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 512));
            }
        } else {
            printf("unknown\n");
        }

        printf("open files                    (-n) ");
        if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }

        printf("stack size              (kbytes, -s) ");
        if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }

        printf("cpu time               (seconds, -t) ");
        if (getrlimit(RLIMIT_CPU, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }

#ifdef RLIMIT_NPROC
        printf("max user processes            (-u) ");
        if (getrlimit(RLIMIT_NPROC, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }
#endif

#ifdef RLIMIT_AS
        printf("virtual memory          (kbytes, -v) ");
        if (getrlimit(RLIMIT_AS, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }
#endif

        return 0;
    }

    // Handle specific resource
    struct rlimit rlim;
    if (getrlimit(resource, &rlim) != 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "ulimit: getrlimit: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    if (limit_value == NULL) {
        // Display current limit
        rlim_t current = hard_limit ? rlim.rlim_max : rlim.rlim_cur;
        if (current == RLIM_INFINITY) {
            printf("unlimited\n");
        } else {
            // Convert to appropriate units
            switch (resource) {
            case RLIMIT_FSIZE:
#ifdef RLIMIT_CORE
            case RLIMIT_CORE:
#endif
                printf("%lu\n",
                       (unsigned long)(current / 512)); // 512-byte blocks
                break;
#ifdef RLIMIT_STACK
            case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
            case RLIMIT_DATA:
#endif
#ifdef RLIMIT_AS
            case RLIMIT_AS:
                printf("%lu\n",
                       (unsigned long)(current / 1024)); // 1024-byte blocks
                break;
#endif
            default:
                printf("%lu\n", (unsigned long)current);
                break;
            }
        }
        return 0;
    }

    // Set new limit
    rlim_t new_limit;
    if (strcmp(limit_value, "unlimited") == 0) {
        new_limit = RLIM_INFINITY;
    } else {
        char *endptr;
        long val = strtol(limit_value, &endptr, 10);
        if (*endptr != '\0' || val < 0) {
            fprintf(stderr, "ulimit: %s: invalid limit\n", limit_value);
            return 1;
        }

        // Convert from display units to bytes
        switch (resource) {
        case RLIMIT_FSIZE:
#ifdef RLIMIT_CORE
        case RLIMIT_CORE:
#endif
            new_limit = val * 512; // 512-byte blocks
            break;
#ifdef RLIMIT_STACK
        case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
        case RLIMIT_DATA:
#endif
#ifdef RLIMIT_AS
        case RLIMIT_AS:
            new_limit = val * 1024; // 1024-byte blocks
            break;
#endif
        default:
            new_limit = val;
            break;
        }
    }

    // Set the limit
    if (hard_limit) {
        rlim.rlim_max = new_limit;
        // Can't set hard limit higher than current hard limit without
        // privileges
        if (new_limit > rlim.rlim_max) {
            rlim.rlim_cur = rlim.rlim_max;
        }
    } else {
        rlim.rlim_cur = new_limit;
        // Can't set soft limit higher than hard limit
        if (new_limit > rlim.rlim_max) {
            rlim.rlim_cur = rlim.rlim_max;
        }
    }

    if (setrlimit(resource, &rlim) != 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "ulimit: setrlimit: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    return 0;
}

/**
 * @brief Display accumulated user and system times
 *
 * Prints the accumulated user and system times for the shell
 * and for processes run from the shell (children).
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success, 1 on error
 */
int bin_times(int argc, char **argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning

    struct tms tms_buf;
    clock_t real_time;

    // Get process times
    real_time = times(&tms_buf);
    if (real_time == (clock_t)-1) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "times: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    // Get clock ticks per second for conversion
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) {
        ticks_per_sec = 100; // Default fallback
    }

    // Convert ticks to seconds and format output
    double user_time = (double)tms_buf.tms_utime / ticks_per_sec;
    double system_time = (double)tms_buf.tms_stime / ticks_per_sec;
    double child_user_time = (double)tms_buf.tms_cutime / ticks_per_sec;
    double child_system_time = (double)tms_buf.tms_cstime / ticks_per_sec;

    // Output in POSIX format: user_time system_time child_user_time
    // child_system_time
    printf("%.2dm%.3fs %.2dm%.3fs\n", (int)(user_time / 60),
           user_time - (int)(user_time / 60) * 60, (int)(system_time / 60),
           system_time - (int)(system_time / 60) * 60);
    printf("%.2dm%.3fs %.2dm%.3fs\n", (int)(child_user_time / 60),
           child_user_time - (int)(child_user_time / 60) * 60,
           (int)(child_system_time / 60),
           child_system_time - (int)(child_system_time / 60) * 60);

    return 0;
}

/**
 * @brief Parse positional parameters for shell scripts
 *
 * POSIX-compliant option parser for shell scripts. Uses OPTIND and
 * OPTARG variables to track parsing state. Supports silent mode
 * (optstring starts with ':') for custom error handling.
 *
 * @param argc Argument count
 * @param argv Argument vector (optstring, varname, [args...])
 * @return 0 if option found, 1 if no more options
 */
int bin_getopts(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "getopts: usage: getopts optstring name [args...]\n");
        return 1;
    }

    char *optstring = argv[1];
    char *varname = argv[2];

    // Get current OPTIND value from environment
    char *optind_str = symtable_get_global("OPTIND");
    int current_optind = optind_str ? atoi(optind_str) : 1;

    // Determine arguments to parse
    char **parse_args;
    int parse_argc;

    if (argc > 3) {
        // Use provided arguments
        parse_args = &argv[3];
        parse_argc = argc - 3;
    } else {
        // Use positional parameters - get from shell environment
        // For now, use a simple implementation
        parse_args = NULL;
        parse_argc = 0;

        // Try to get positional parameters from shell variables
        char *argc_str = symtable_get_global("#");
        if (argc_str) {
            parse_argc = atoi(argc_str);
            if (parse_argc > 0) {
                parse_args = malloc((parse_argc + 1) * sizeof(char *));
                for (int i = 0; i < parse_argc; i++) {
                    char param_name[16];
                    snprintf(param_name, sizeof(param_name), "%d", i + 1);
                    char *param_val = symtable_get_global(param_name);
                    parse_args[i] = param_val ? strdup(param_val) : strdup("");
                }
                parse_args[parse_argc] = NULL;
            }
        }
    }

    // Check if we have arguments to parse
    if (parse_argc == 0 || current_optind > parse_argc) {
        // No more arguments - OPTIND should point to first non-option argument
        // Don't reset OPTIND to 1, leave it pointing to where non-options start
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return 1;
    }

    // Get current argument to parse
    char *current_arg = parse_args[current_optind - 1];

    // Static variables to maintain state between calls
    static char *current_option_arg = NULL;
    static int option_pos = 0;

    // Check if we're continuing with a combined option (like -abc)
    if (option_pos == 0) {
        // Starting new argument
        if (!current_arg || current_arg[0] != '-' ||
            strcmp(current_arg, "-") == 0) {
            // Not an option or single dash - OPTIND should point to this
            // non-option Don't reset OPTIND to 1, it should point to current
            // position
            if (argc <= 3 && parse_args) {
                for (int i = 0; i < parse_argc; i++) {
                    free(parse_args[i]);
                }
                free(parse_args);
            }
            return 1;
        }

        if (strcmp(current_arg, "--") == 0) {
            // End of options marker
            char next_optind[16];
            snprintf(next_optind, sizeof(next_optind), "%d",
                     current_optind + 1);
            symtable_set_global("OPTIND", next_optind);
            if (argc <= 3 && parse_args) {
                for (int i = 0; i < parse_argc; i++) {
                    free(parse_args[i]);
                }
                free(parse_args);
            }
            return 1;
        }

        current_option_arg = current_arg;
        option_pos = 1; // Skip the initial '-'
    }

    // Get current option character
    char opt_char = current_option_arg[option_pos];
    if (opt_char == '\0') {
        // Move to next argument
        current_optind++;
        option_pos = 0;
        char next_optind[16];
        snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
        symtable_set_global("OPTIND", next_optind);
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return bin_getopts(argc,
                           argv); // Recursive call to process next argument
    }

    // Check if option is valid
    bool silent_mode = (optstring[0] == ':');
    char *opt_pos = strchr(silent_mode ? optstring + 1 : optstring, opt_char);

    if (!opt_pos) {
        // Invalid option
        if (silent_mode) {
            symtable_set_global(varname, "?");
            char optarg_val[2] = {opt_char, '\0'};
            symtable_set_global("OPTARG", optarg_val);
        } else {
            fprintf(stderr, "getopts: illegal option -- %c\n", opt_char);
            symtable_set_global(varname, "?");
            symtable_set_global("OPTARG", "");
        }
        option_pos++;

        // If we've finished processing this argument, move to next
        if (current_option_arg[option_pos] == '\0') {
            current_optind++;
            option_pos = 0;
        }

        char next_optind[16];
        snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
        symtable_set_global("OPTIND", next_optind);
        if (argc <= 3 && parse_args) {
            for (int i = 0; i < parse_argc; i++) {
                free(parse_args[i]);
            }
            free(parse_args);
        }
        return 0;
    }

    // Check if option requires an argument
    bool needs_arg = (opt_pos[1] == ':');

    if (needs_arg) {
        char *arg_value = NULL;

        if (current_option_arg[option_pos + 1] != '\0') {
            // Argument is attached (like -fvalue)
            arg_value = &current_option_arg[option_pos + 1];
            option_pos = 0; // Move to next argument
            current_optind++;
        } else {
            // Argument should be in next parameter
            if (current_optind < parse_argc) {
                arg_value = parse_args[current_optind];
                current_optind++; // Move past the option argument
                option_pos = 0;
            } else {
                // Missing argument
                if (silent_mode) {
                    symtable_set_global(varname, ":");
                    char optarg_val[2] = {opt_char, '\0'};
                    symtable_set_global("OPTARG", optarg_val);
                } else {
                    fprintf(stderr,
                            "getopts: option requires an argument -- %c\n",
                            opt_char);
                    symtable_set_global(varname, "?");
                    symtable_set_global("OPTARG", "");
                }
                char next_optind[16];
                snprintf(next_optind, sizeof(next_optind), "%d",
                         current_optind);
                symtable_set_global("OPTIND", next_optind);
                if (argc <= 3 && parse_args) {
                    for (int i = 0; i < parse_argc; i++) {
                        free(parse_args[i]);
                    }
                    free(parse_args);
                }
                return 0;
            }
        }

        // Set option and argument
        char opt_val[2] = {opt_char, '\0'};
        symtable_set_global(varname, opt_val);
        symtable_set_global("OPTARG", arg_value ? arg_value : "");

        // For options with arguments, we need to increment current_optind again
        // since we consumed both the option and its argument
        current_optind++;
    } else {
        // Option doesn't take an argument
        char opt_val[2] = {opt_char, '\0'};
        symtable_set_global(varname, opt_val);
        symtable_set_global("OPTARG", "");
        option_pos++;

        // If we've finished processing this argument, move to next
        if (current_option_arg[option_pos] == '\0') {
            current_optind++;
            option_pos = 0;
        }
    }

    // Update OPTIND
    char next_optind[16];
    snprintf(next_optind, sizeof(next_optind), "%d", current_optind);
    symtable_set_global("OPTIND", next_optind);

    // Clean up if we allocated parse_args
    if (argc <= 3 && parse_args) {
        for (int i = 0; i < parse_argc; i++) {
            free(parse_args[i]);
        }
        free(parse_args);
    }

    return 0; // Found an option
}

/**
 * @brief Declare local variables within function scope
 *
 * Creates variables that are local to the current function.
 * Can only be used inside a function. Supports assignment syntax
 * (local var=value) or declaration only (local var).
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, 1 on error or if not in a function
 */
int bin_local(int argc, char **argv) {
    if (argc == 1) {
        // No arguments - just return success (bash behavior)
        return 0;
    }

    // Get the current symbol table manager
    symtable_manager_t *manager = symtable_get_global_manager();
    if (!manager) {
        error_message("local: symbol table not available");
        return 1;
    }

    // Check if we're in a function scope
    size_t current_level = symtable_current_level(manager);
    if (current_level == 0) {
        error_message("local: can only be used in a function");
        return 1;
    }

    // Parse options
    bool opt_nameref = false;
    int opt_idx = 1;

    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        const char *opt = argv[opt_idx];

        // Handle -- to stop option processing
        if (strcmp(opt, "--") == 0) {
            opt_idx++;
            break;
        }

        // Process each character in the option string
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'n':
                opt_nameref = true;
                break;
            default:
                fprintf(stderr, "local: -%c: invalid option\n", opt[i]);
                return 2;
            }
        }
        opt_idx++;
    }

    // Process each argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        char *eq = strchr(arg, '=');

        if (eq) {
            // Assignment: local var=value or local -n ref=target
            size_t name_len = eq - arg;
            char *name = malloc(name_len + 1);
            if (!name) {
                error_message("local: memory allocation failed");
                return 1;
            }

            strncpy(name, arg, name_len);
            name[name_len] = '\0';

            // Validate variable name
            if (!name[0] || (!isalpha(name[0]) && name[0] != '_')) {
                error_message("local: invalid variable name");
                free(name);
                return 1;
            }

            for (size_t j = 1; j < name_len; j++) {
                if (!isalnum(name[j]) && name[j] != '_') {
                    error_message("local: invalid variable name");
                    free(name);
                    return 1;
                }
            }

            char *value = eq + 1;

            if (opt_nameref) {
                // Create local nameref: local -n ref=target
                symvar_flags_t flags = SYMVAR_LOCAL | SYMVAR_NAMEREF_FLAG;
                if (symtable_set_nameref(manager, name, value, flags) != 0) {
                    error_message("local: failed to create nameref");
                    free(name);
                    return 1;
                }
            } else {
                // Set the local variable
                if (symtable_set_local_var(manager, name, value) != 0) {
                    error_message("local: failed to set variable");
                    free(name);
                    return 1;
                }
            }

            free(name);
        } else {
            // Declaration only: local var
            // Validate variable name
            if (!arg[0] || (!isalpha(arg[0]) && arg[0] != '_')) {
                error_message("local: invalid variable name");
                return 1;
            }

            for (size_t j = 1; arg[j]; j++) {
                if (!isalnum(arg[j]) && arg[j] != '_') {
                    error_message("local: invalid variable name");
                    return 1;
                }
            }

            if (opt_nameref) {
                error_message("local: -n requires assignment (local -n ref=target)");
                return 1;
            }

            // Declare the local variable with empty value
            if (symtable_set_local_var(manager, arg, "") != 0) {
                error_message("local: failed to declare variable");
                return 1;
            }
        }
    }

    return 0;
}

/* Callback for declare -p to print scalar variables */
static void declare_print_var_callback(const char *key, const char *value,
                                       void *userdata) {
    (void)userdata;
    if (!key) return;
    /* Skip internal variables starting with __ */
    if (key[0] == '_' && key[1] == '_') return;
    /* Skip if this is actually an array (handled separately) */
    if (symtable_is_array(key)) return;
    printf("declare -- %s=\"%s\"\n", key, value ? value : "");
}

/* Callback for declare -p to print array variables */
static void declare_print_array_callback(const char *name, array_value_t *array,
                                         void *userdata) {
    (void)userdata;
    if (!name || !array) return;
    if (array->is_associative) {
        printf("declare -A %s=(", name);
        /* Print associative array elements */
        if (array->assoc_map) {
            ht_enum_t *e = ht_strstr_enum_create(array->assoc_map);
            if (e) {
                const char *k, *v;
                bool first = true;
                while (ht_strstr_enum_next(e, &k, &v)) {
                    printf("%s[%s]=\"%s\"", first ? "" : " ", k, v ? v : "");
                    first = false;
                }
                ht_strstr_enum_destroy(e);
            }
        }
        printf(")\n");
    } else {
        printf("declare -a %s=(", name);
        /* Print indexed array elements */
        bool first = true;
        for (size_t i = 0; i < array->count; i++) {
            if (array->elements[i]) {
                int idx = array->indices ? array->indices[i] : (int)i;
                printf("%s[%d]=\"%s\"", first ? "" : " ", idx, array->elements[i]);
                first = false;
            }
        }
        printf(")\n");
    }
}

/**
 * @brief Declare variables with attributes
 *
 * Bash/Zsh-compatible declare builtin for declaring variables with
 * special attributes like indexed arrays, associative arrays, or integers.
 *
 * Options:
 *   -a  Declare indexed array
 *   -A  Declare associative array
 *   -i  Declare integer variable (auto-evaluates arithmetic)
 *   -r  Declare readonly variable
 *   -x  Export variable to environment
 *   -p  Print variable attributes and values
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on error
 */
int bin_declare(int argc, char **argv) {
    bool opt_indexed_array = false;
    bool opt_assoc_array = false;
    bool opt_integer = false;
    bool opt_readonly = false;
    bool opt_export = false;
    bool opt_print = false;
    bool opt_nameref = false;
    bool opt_global = false;
    bool opt_lowercase = false;
    bool opt_uppercase = false;
    bool opt_trace = false;

    int opt_idx = 1;

    // Parse options
    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        const char *opt = argv[opt_idx];

        // Handle -- to stop option processing
        if (strcmp(opt, "--") == 0) {
            opt_idx++;
            break;
        }

        // Process each character in the option string
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'a':
                opt_indexed_array = true;
                break;
            case 'A':
                opt_assoc_array = true;
                break;
            case 'i':
                opt_integer = true;
                break;
            case 'r':
                opt_readonly = true;
                break;
            case 'x':
                opt_export = true;
                break;
            case 'p':
                opt_print = true;
                break;
            case 'n':
                opt_nameref = true;
                break;
            case 'g':
                opt_global = true;
                break;
            case 'l':
                opt_lowercase = true;
                break;
            case 'u':
                opt_uppercase = true;
                break;
            case 't':
                opt_trace = true;
                break;
            default:
                fprintf(stderr, "declare: -%c: invalid option\n", opt[i]);
                return 2;
            }
        }
        opt_idx++;
    }

    // Can't have both -l and -u
    if (opt_lowercase && opt_uppercase) {
        fprintf(stderr, "declare: cannot use -l and -u simultaneously\n");
        return 1;
    }

    // Can't have both -a and -A
    if (opt_indexed_array && opt_assoc_array) {
        fprintf(stderr, "declare: cannot use -a and -A simultaneously\n");
        return 1;
    }

    // If no variable names provided and -p not specified, just return success
    if (opt_idx >= argc && !opt_print) {
        return 0;
    }

    // Handle -p (print) option with no arguments - list all variables
    if (opt_print && opt_idx >= argc) {
        // Print all arrays first
        symtable_enumerate_arrays(declare_print_array_callback, NULL);
        // Then print all scalar variables
        symtable_enumerate_global_vars(declare_print_var_callback, NULL);
        return 0;
    }

    // Process each variable argument
    for (int i = opt_idx; i < argc; i++) {
        char *arg = argv[i];
        char *eq = strchr(arg, '=');
        char *name = NULL;
        char *value = NULL;

        if (eq) {
            // Assignment: declare var=value or declare -a arr=(...)
            size_t name_len = eq - arg;
            name = malloc(name_len + 1);
            if (!name) {
                fprintf(stderr, "declare: memory allocation failed\n");
                return 1;
            }
            strncpy(name, arg, name_len);
            name[name_len] = '\0';
            value = eq + 1;
        } else {
            // Declaration only: declare var
            name = strdup(arg);
            if (!name) {
                fprintf(stderr, "declare: memory allocation failed\n");
                return 1;
            }
            value = NULL;
        }

        // Validate variable name
        if (!name[0] || (!isalpha(name[0]) && name[0] != '_')) {
            fprintf(stderr, "declare: `%s': not a valid identifier\n", name);
            free(name);
            return 1;
        }
        for (size_t j = 1; name[j]; j++) {
            if (!isalnum(name[j]) && name[j] != '_') {
                fprintf(stderr, "declare: `%s': not a valid identifier\n", name);
                free(name);
                return 1;
            }
        }

        // Handle -p for specific variable
        if (opt_print) {
            symtable_manager_t *manager = symtable_get_global_manager();
            // Check if it's an array
            array_value_t *arr = symtable_get_array(name);
            if (arr) {
                if (arr->is_associative) {
                    printf("declare -A %s\n", name);
                } else {
                    printf("declare -a %s\n", name);
                }
            } else if (manager) {
                char *var_value = symtable_get(manager, name);
                if (var_value) {
                    printf("declare -- %s=\"%s\"\n", name, var_value);
                } else {
                    fprintf(stderr, "declare: %s: not found\n", name);
                }
            } else {
                fprintf(stderr, "declare: %s: not found\n", name);
            }
            free(name);
            continue;
        }

        // Handle array declarations
        if (opt_indexed_array || opt_assoc_array) {
            array_value_t *arr = symtable_array_create(opt_assoc_array);
            if (!arr) {
                fprintf(stderr, "declare: failed to create array\n");
                free(name);
                return 1;
            }

            // If value is provided and starts with (, parse as array literal
            if (value && value[0] == '(') {
                // Parse array literal (elem1 elem2 ...)
                const char *p = value + 1;
                int idx = 0;

                while (*p && *p != ')') {
                    // Skip whitespace
                    while (*p && isspace(*p)) p++;
                    if (*p == ')' || !*p) break;

                    // Find end of element
                    const char *elem_start = p;
                    bool in_quote = false;
                    char quote_char = 0;

                    while (*p && (in_quote || (!isspace(*p) && *p != ')'))) {
                        if (!in_quote && (*p == '"' || *p == '\'')) {
                            in_quote = true;
                            quote_char = *p;
                        } else if (in_quote && *p == quote_char) {
                            in_quote = false;
                        }
                        p++;
                    }

                    size_t elem_len = p - elem_start;
                    if (elem_len > 0) {
                        char *elem = malloc(elem_len + 1);
                        if (elem) {
                            strncpy(elem, elem_start, elem_len);
                            elem[elem_len] = '\0';

                            // Check for [n]=value syntax
                            if (elem[0] == '[') {
                                char *bracket_end = strchr(elem, ']');
                                if (bracket_end && bracket_end[1] == '=') {
                                    *bracket_end = '\0';
                                    const char *idx_str = elem + 1;
                                    const char *elem_val = bracket_end + 2;

                                    if (opt_assoc_array) {
                                        symtable_array_set_assoc(arr, idx_str, elem_val);
                                    } else {
                                        int parsed_idx = atoi(idx_str);
                                        symtable_array_set_index(arr, parsed_idx, elem_val);
                                    }
                                }
                            } else {
                                // Regular element
                                symtable_array_set_index(arr, idx++, elem);
                            }
                            free(elem);
                        }
                    }
                }
            }

            if (symtable_set_array(name, arr) != 0) {
                fprintf(stderr, "declare: failed to store array\n");
                symtable_array_free(arr);
                free(name);
                return 1;
            }
        }
        // Handle integer declaration
        else if (opt_integer) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                fprintf(stderr, "declare: symbol table not available\n");
                free(name);
                return 1;
            }
            // For integer variables, evaluate value as arithmetic
            if (value) {
                char *result = arithm_expand(value);
                if (result) {
                    symtable_set(manager, name, result);
                    free(result);
                } else {
                    // If arithmetic eval fails, set to 0
                    symtable_set(manager, name, "0");
                }
            } else {
                symtable_set(manager, name, "0");
            }
        }
        // Handle nameref declaration (-n)
        else if (opt_nameref) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                fprintf(stderr, "declare: symbol table not available\n");
                free(name);
                return 1;
            }
            if (!value) {
                fprintf(stderr, "declare: -n requires a target variable\n");
                free(name);
                return 1;
            }
            symvar_flags_t flags = SYMVAR_NAMEREF_FLAG;
            if (!opt_global) {
                flags |= SYMVAR_LOCAL;
            }
            if (symtable_set_nameref(manager, name, value, flags) != 0) {
                fprintf(stderr, "declare: failed to create nameref\n");
                free(name);
                return 1;
            }
        }
        // Regular variable declaration
        else {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (!manager) {
                fprintf(stderr, "declare: symbol table not available\n");
                free(name);
                return 1;
            }

            // Apply case transformations if requested
            char *final_value = NULL;
            if (value) {
                if (opt_lowercase) {
                    final_value = strdup(value);
                    if (final_value) {
                        for (char *p = final_value; *p; p++) {
                            *p = tolower((unsigned char)*p);
                        }
                    }
                } else if (opt_uppercase) {
                    final_value = strdup(value);
                    if (final_value) {
                        for (char *p = final_value; *p; p++) {
                            *p = toupper((unsigned char)*p);
                        }
                    }
                } else {
                    final_value = strdup(value);
                }
            }

            // Build flags
            symvar_flags_t flags = SYMVAR_NONE;
            if (opt_readonly) {
                flags |= SYMVAR_READONLY;
            }
            if (opt_export) {
                flags |= SYMVAR_EXPORTED;
            }
            if (opt_lowercase) {
                flags |= SYMVAR_LOWERCASE;
            }
            if (opt_uppercase) {
                flags |= SYMVAR_UPPERCASE;
            }
            if (opt_trace) {
                flags |= SYMVAR_TRACE;
            }
            if (!opt_global) {
                flags |= SYMVAR_LOCAL;
            }

            if (final_value) {
                if (opt_global) {
                    // Use global scope for -g flag
                    symtable_set_global_var(manager, name, final_value);
                } else {
                    symtable_set_var(manager, name, final_value, flags);
                }
                free(final_value);
            } else {
                // Just declare without value
                if (opt_global) {
                    symtable_set_global_var(manager, name, "");
                } else {
                    symtable_set_var(manager, name, "", flags);
                }
            }
        }

        // Handle export (also set in environment)
        if (opt_export) {
            symtable_manager_t *manager = symtable_get_global_manager();
            if (manager) {
                char *var_value = symtable_get(manager, name);
                if (var_value) {
                    setenv(name, var_value, 1);
                }
            }
        }

        free(name);
    }

    return 0;
}

/**
 * @brief Create read-only variables
 *
 * Marks variables as read-only according to POSIX standards.
 * With no arguments, lists all readonly variables.
 * Supports assignment syntax (readonly var=value).
 *
 * @param argc Argument count
 * @param argv Argument vector with variable declarations
 * @return 0 on success, 1 on invalid identifier
 */
int bin_readonly(int argc, char **argv) {
    if (argc == 1) {
        // No arguments - print all readonly variables
        symtable_manager_t *manager = symtable_get_global_manager();
        if (!manager) {
            error_message("readonly: symbol table not available");
            return 1;
        }

        // Print readonly variables in the format: readonly name=value
        printf("readonly functionality not fully implemented for listing\n");
        return 0;
    }

    // Process each argument
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        char *equals = strchr(arg, '=');

        if (equals) {
            // Variable assignment: readonly var=value
            *equals = '\0';
            char *name = arg;
            char *value = equals + 1;

            // Validate variable name
            if (!is_valid_identifier(name)) {
                error_message("readonly: '%s' not a valid identifier", name);
                *equals = '='; // Restore the string
                return 1;
            }

            // Set the variable value
            symtable_set_global(name, value);

            // Mark as readonly (note: this is a simplified implementation)
            // In a full implementation, we would need to track readonly status
            // and prevent future modifications

            *equals = '='; // Restore the string
        } else {
            // No assignment: readonly var (make existing variable readonly)
            if (!is_valid_identifier(arg)) {
                error_message("readonly: '%s' not a valid identifier", arg);
                return 1;
            }

            // Check if variable exists
            char *value = symtable_get_global(arg);
            if (!value) {
                // Variable doesn't exist, create it with empty value
                symtable_set_global(arg, "");
            }

            // Mark as readonly (simplified implementation)
            // Note: Full readonly implementation would require symbol table
            // modifications to track and enforce readonly status
        }
    }

    return 0;
}

/**
 * @brief Manage shell configuration
 *
 * Interface to the shell configuration system. Supports subcommands:
 * show, set, get, reload, save for managing configuration options.
 *
 * @param argc Argument count
 * @param argv Argument vector with config subcommand and options
 * @return Always returns 0
 */
int bin_config(int argc, char **argv) {
    builtin_config(argc, argv);
    return 0;
}

/**
 * @brief Set shell options (enable features)
 *
 * Zsh-style setopt command for enabling shell features.
 * Usage:
 *   setopt              - List all options with current state
 *   setopt -p           - Print in re-usable format
 *   setopt -q <opt>     - Query silently (exit status only)
 *   setopt <opt> [...]  - Enable one or more options
 *
 * Options can be specified using:
 *   - Canonical name: extended_glob
 *   - Short alias: extglob
 *   - Underscore or no underscore: extended_glob / extendedglob
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_setopt(int argc, char **argv) {
    bool print_format = false;
    bool query_mode = false;
    int start_idx = 1;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-p") == 0) {
                print_format = true;
                start_idx = i + 1;
            } else if (strcmp(argv[i], "-q") == 0) {
                query_mode = true;
                start_idx = i + 1;
            } else if (strcmp(argv[i], "--") == 0) {
                start_idx = i + 1;
                break;
            } else {
                fprintf(stderr, "setopt: invalid option: %s\n", argv[i]);
                return 1;
            }
        } else {
            break;
        }
    }

    /* No option specified - list all options */
    if (start_idx >= argc) {
        if (query_mode) {
            fprintf(stderr, "setopt: -q requires an option name\n");
            return 1;
        }

        printf("Shell options:\n");
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            const char *name = shell_feature_name(feature);
            bool enabled = shell_mode_allows(feature);

            if (print_format) {
                printf("%s %s\n", enabled ? "setopt" : "unsetopt", name);
            } else {
                printf("  %-30s %s\n", name, enabled ? "on" : "off");
            }
        }
        return 0;
    }

    /* Process each option */
    for (int i = start_idx; i < argc; i++) {
        shell_feature_t feature;

        if (!shell_feature_parse(argv[i], &feature)) {
            if (!query_mode) {
                fprintf(stderr, "setopt: unknown option: %s\n", argv[i]);
            }
            return 1;
        }

        if (query_mode) {
            /* Return status based on current state */
            return shell_mode_allows(feature) ? 0 : 1;
        }

        /* Enable the feature */
        shell_feature_enable(feature);

        /* Sync to registry if initialized */
        if (config_registry_is_initialized()) {
            char key[CREG_KEY_MAX];
            snprintf(key, sizeof(key), "shell.features.%s",
                     shell_feature_name(feature));
            config_registry_set_boolean(key, true);
        }
    }

    return 0;
}

/**
 * @brief Unset shell options (disable features)
 *
 * Zsh-style unsetopt command for disabling shell features.
 * Usage:
 *   unsetopt              - List all disabled options
 *   unsetopt <opt> [...]  - Disable one or more options
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_unsetopt(int argc, char **argv) {
    /* No option specified - list disabled options */
    if (argc < 2) {
        printf("Disabled options:\n");
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            if (!shell_mode_allows(feature)) {
                printf("  %s\n", shell_feature_name(feature));
            }
        }
        return 0;
    }

    /* Process each option */
    for (int i = 1; i < argc; i++) {
        /* Skip flags */
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                continue;
            }
            fprintf(stderr, "unsetopt: invalid option: %s\n", argv[i]);
            return 1;
        }

        shell_feature_t feature;

        if (!shell_feature_parse(argv[i], &feature)) {
            fprintf(stderr, "unsetopt: unknown option: %s\n", argv[i]);
            return 1;
        }

        /* Disable the feature */
        shell_feature_disable(feature);

        /* Sync to registry if initialized */
        if (config_registry_is_initialized()) {
            char key[CREG_KEY_MAX];
            snprintf(key, sizeof(key), "shell.features.%s",
                     shell_feature_name(feature));
            config_registry_set_boolean(key, false);
        }
    }

    return 0;
}

/**
 * @brief Bash-style shopt builtin for shell options
 *
 * Provides bash-compatible syntax for managing shell options.
 * Usage:
 *   shopt                  - List all shopt options with current state
 *   shopt -p               - Print in re-usable format (shopt -s/-u)
 *   shopt -s <opt> [...]   - Set (enable) one or more options
 *   shopt -u <opt> [...]   - Unset (disable) one or more options
 *   shopt -q <opt>         - Query option silently (exit status only)
 *   shopt -o               - Operate on set -o options instead
 *
 * Option names accepted:
 *   - Bash names: extglob, nullglob, globstar, dotglob, etc.
 *   - Lush names: extended_glob, null_glob, etc.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error, 2 if -q and option is off
 */
int bin_shopt(int argc, char **argv)
{
    bool set_mode = false;      /* -s: enable options */
    bool unset_mode = false;    /* -u: disable options */
    bool query_mode = false;    /* -q: query silently */
    bool print_mode = false;    /* -p: print format */
    bool set_o_mode = false;    /* -o: use set -o options */
    int opt_end = 1;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            break;
        }
        if (strcmp(argv[i], "--") == 0) {
            opt_end = i + 1;
            break;
        }

        /* Process each character in the option string */
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 's':
                set_mode = true;
                break;
            case 'u':
                unset_mode = true;
                break;
            case 'q':
                query_mode = true;
                break;
            case 'p':
                print_mode = true;
                break;
            case 'o':
                set_o_mode = true;
                break;
            default:
                fprintf(stderr, "shopt: -%c: invalid option\n", *p);
                fprintf(stderr,
                        "shopt: usage: shopt [-pqsu] [-o] [optname ...]\n");
                return 1;
            }
        }
        opt_end = i + 1;
    }

    /* -s and -u are mutually exclusive */
    if (set_mode && unset_mode) {
        fprintf(stderr, "shopt: cannot set and unset options simultaneously\n");
        return 1;
    }

    /* -o mode: operate on set -o options (not implemented, just note it) */
    if (set_o_mode) {
        /* For now, -o options map to the same features */
        /* In bash, -o uses different option namespace, but we unify them */
    }

    /* No option names given - list all options */
    if (opt_end >= argc) {
        if (query_mode) {
            fprintf(stderr, "shopt: -q: option name required\n");
            return 1;
        }

        /* List all features */
        for (int i = 0; i < (int)FEATURE_COUNT; i++) {
            shell_feature_t feature = (shell_feature_t)i;
            const char *name = shell_feature_name(feature);
            bool enabled = shell_mode_allows(feature);

            /* If -s or -u specified without names, filter by state */
            if (set_mode && !enabled) {
                continue;
            }
            if (unset_mode && enabled) {
                continue;
            }

            if (print_mode) {
                printf("shopt %s %s\n", enabled ? "-s" : "-u", name);
            } else {
                printf("%-30s %s\n", name, enabled ? "on" : "off");
            }
        }
        return 0;
    }

    /* Process each option name */
    int result = 0;
    for (int i = opt_end; i < argc; i++) {
        shell_feature_t feature;

        if (!shell_feature_parse(argv[i], &feature)) {
            if (!query_mode) {
                fprintf(stderr, "shopt: %s: invalid shell option name\n",
                        argv[i]);
            }
            result = 1;
            continue;
        }

        bool enabled = shell_mode_allows(feature);

        if (query_mode) {
            /* -q: return status based on option state */
            if (!enabled) {
                result = 1;
            }
        } else if (set_mode) {
            /* -s: enable the option */
            shell_feature_enable(feature);

            /* Sync to registry if initialized */
            if (config_registry_is_initialized()) {
                char key[CREG_KEY_MAX];
                snprintf(key, sizeof(key), "shell.features.%s",
                         shell_feature_name(feature));
                config_registry_set_boolean(key, true);
            }
        } else if (unset_mode) {
            /* -u: disable the option */
            shell_feature_disable(feature);

            /* Sync to registry if initialized */
            if (config_registry_is_initialized()) {
                char key[CREG_KEY_MAX];
                snprintf(key, sizeof(key), "shell.features.%s",
                         shell_feature_name(feature));
                config_registry_set_boolean(key, false);
            }
        } else {
            /* No -s/-u: just print the option state */
            if (print_mode) {
                printf("shopt %s %s\n", enabled ? "-s" : "-u",
                       shell_feature_name(feature));
            } else {
                printf("%-30s %s\n", shell_feature_name(feature),
                       enabled ? "on" : "off");
            }
        }
    }

    return result;
}

/**
 * @brief Initialize the command hash table
 *
 * Creates the hash table used by the hash builtin for remembering
 * utility locations in PATH.
 */
void init_command_hash(void) {
    if (command_hash == NULL) {
        command_hash = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
    }
}

/**
 * @brief Free the command hash table
 *
 * Destroys the hash table used for remembering utility locations.
 */
void free_command_hash(void) {
    if (command_hash != NULL) {
        ht_strstr_destroy(command_hash);
        command_hash = NULL;
    }
}

/**
 * @brief Search for a command in PATH
 *
 * Searches each directory in PATH for an executable matching
 * the command name. If command contains a slash, checks if
 * it exists as-is.
 *
 * @param command The command name to find
 * @return Newly allocated full path string (caller must free),
 *         or NULL if not found
 */
char *find_command_in_path(const char *command) {
    if (!command || command[0] == '\0') {
        return NULL;
    }

    // If command contains slash, check if it exists as-is
    if (strchr(command, '/')) {
        if (access(command, F_OK) == 0) {
            return strdup(command);
        }
        return NULL;
    }

    // Search in PATH
    const char *path_env = getenv("PATH");
    if (!path_env) {
        return NULL;
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
        return NULL;
    }

    char *path_dir = strtok(path_copy, ":");
    char *result = NULL;

    while (path_dir) {
        // Construct full path
        size_t dir_len = strlen(path_dir);
        size_t cmd_len = strlen(command);
        char *full_path = malloc(dir_len + cmd_len + 2); // +2 for '/' and '\0'

        if (full_path) {
            snprintf(full_path, dir_len + cmd_len + 2, "%s/%s", path_dir,
                     command);

            // Check if file exists and is executable
            if (access(full_path, X_OK) == 0) {
                result = full_path;
                break;
            }

            free(full_path);
        }

        path_dir = strtok(NULL, ":");
    }

    free(path_copy);
    return result;
}

/**
 * @brief Remember or report utility locations (POSIX hash)
 *
 * With no arguments, displays all remembered utility locations.
 * With utility names, finds and remembers their PATH locations.
 * With -r, forgets all remembered locations.
 * With -t name, prints the remembered pathname of name.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and utility names
 * @return 0 on success, 1 if utility not found, 2 on invalid option
 */
int bin_hash(int argc, char **argv) {
    init_command_hash();

    // Handle -r option (forget all remembered locations)
    if (argc == 2 && strcmp(argv[1], "-r") == 0) {
        if (command_hash) {
            ht_strstr_destroy(command_hash);
            command_hash = ht_strstr_create(HT_STR_CASECMP | HT_SEED_RANDOM);
        }
        return 0;
    }

    // Handle -t option (print remembered pathname)
    if (argc >= 2 && strcmp(argv[1], "-t") == 0) {
        if (argc < 3) {
            error_message("hash: -t: option requires an argument");
            return 2;
        }
        int ret = 0;
        for (int i = 2; i < argc; i++) {
            const char *utility = argv[i];
            const char *path = command_hash ? ht_strstr_get(command_hash, utility) : NULL;
            if (path) {
                printf("%s\n", path);
            } else {
                error_message("hash: %s: not found", utility);
                ret = 1;
            }
        }
        return ret;
    }

    // Handle invalid options
    if (argc >= 2 && argv[1][0] == '-' && strcmp(argv[1], "-r") != 0) {
        error_message("hash: invalid option");
        return 2;
    }

    // No arguments - display all remembered locations
    if (argc == 1) {
        if (!command_hash) {
            return 0;
        }

        ht_enum_t *enumerator = ht_strstr_enum_create(command_hash);
        if (enumerator) {
            const char *key, *value;
            while (ht_strstr_enum_next(enumerator, &key, &value)) {
                if (key && value) {
                    printf("%s\t%s\n", key, value);
                }
            }
            ht_strstr_enum_destroy(enumerator);
        }
        return 0;
    }

    // Arguments provided - find and remember utility locations
    for (int i = 1; i < argc; i++) {
        const char *utility = argv[i];

        // Skip if it's a builtin (POSIX requirement)
        if (is_builtin(utility)) {
            continue;
        }

        // Find the utility in PATH
        char *path = find_command_in_path(utility);
        if (path) {
            // Remember this location
            ht_strstr_insert(command_hash, utility, path);
            free(path);
        } else {
            error_message("hash: %s: not found", utility);
            return 1;
        }
    }

    return 0;
}
/**
 * @brief Manage network and SSH host completion
 *
 * Provides subcommands for SSH host management:
 * - (no args): Show SSH host count and status
 * - hosts: List all cached SSH hosts
 * - refresh: Reload SSH host cache from config files
 * - help: Show usage information
 *
 * @param argc Argument count
 * @param argv Argument vector with subcommand
 * @return 0 on success, 1 on unknown command or error
 */
int bin_network(int argc, char **argv) {
    if (!argv) {
        return 1;
    }

    /* No arguments - show SSH host count */
    if (argc == 1) {
        ssh_host_cache_t *cache = get_ssh_host_cache();
        printf("SSH Hosts Status:\n");
        printf("  Hosts cached: %zu\n", cache ? cache->count : 0);
        printf("  Remote session: %s\n", getenv("SSH_CLIENT") ? "Yes" : "No");
        return 0;
    }

    /* Handle subcommands */
    if (strcmp(argv[1], "hosts") == 0) {
        ssh_host_cache_t *cache = get_ssh_host_cache();
        if (!cache || cache->count == 0) {
            printf("No SSH hosts found\n");
            printf("Check ~/.ssh/config and ~/.ssh/known_hosts\n");
            return 0;
        }

        printf("SSH Hosts (%zu total):\n", cache->count);
        printf("%-25s %-25s %-8s %s\n", "Alias", "Hostname", "Port", "Source");
        printf("%-25s %-25s %-8s %s\n", "-----", "--------", "----", "------");

        for (size_t i = 0; i < cache->count && i < 50; i++) {
            ssh_host_t *host = &cache->hosts[i];
            const char *alias = host->alias[0] ? host->alias : "-";
            const char *hostname = host->hostname[0] ? host->hostname : "-";
            const char *port = host->port[0] ? host->port : "22";
            const char *source = host->from_config ? "config" : "known_hosts";
            printf("%-25s %-25s %-8s %s\n", alias, hostname, port, source);
        }

        if (cache->count > 50) {
            printf("... and %zu more hosts\n", cache->count - 50);
        }
        return 0;
    }

    if (strcmp(argv[1], "refresh") == 0) {
        printf("Refreshing SSH host cache...\n");
        ssh_hosts_refresh();
        ssh_host_cache_t *cache = get_ssh_host_cache();
        printf("Loaded %zu SSH hosts\n", cache ? cache->count : 0);
        return 0;
    }

    if (strcmp(argv[1], "help") == 0) {
        printf("Usage: network [command]\n\n");
        printf("Commands:\n");
        printf("  hosts    - List SSH hosts from config and known_hosts\n");
        printf("  refresh  - Refresh SSH host cache\n");
        printf("  help     - Show this help message\n");
        return 0;
    }

    printf("network: unknown command '%s'\n", argv[1]);
    printf("Use 'network help' for usage\n");
    return 1;
}

/**
 * @brief Advanced debugging and profiling builtin
 *
 * Provides comprehensive debugging capabilities including:
 * - Enable/disable debug mode with configurable levels
 * - Breakpoint management (add, remove, list, clear)
 * - Execution stepping (step, next, continue)
 * - Variable inspection (vars, print)
 * - Performance profiling
 * - Script analysis
 *
 * @param argc Argument count (unused directly, counted from argv)
 * @param argv Argument vector with debug subcommands
 * @return 0 on success, 1 on error
 */
int bin_debug(int argc __attribute__((unused)), char **argv) {
    // Initialize debug context if not already done
    if (!g_debug_context) {
        g_debug_context = debug_init();
        if (!g_debug_context) {
            fprintf(stderr, "debug: Failed to initialize debug context\n");
            return 1;
        }
    }

    debug_context_t *ctx = g_debug_context;

    // Count arguments
    int argc_real = 0;
    while (argv[argc_real]) {
        argc_real++;
    }

    // No arguments - show current debug status
    if (argc_real == 1) {
        printf("Debug Status:\n");
        printf("  Enabled: %s\n", ctx->enabled ? "yes" : "no");
        printf("  Level: %d ", ctx->level);
        switch (ctx->level) {
        case DEBUG_NONE:
            printf("(none)\n");
            break;
        case DEBUG_BASIC:
            printf("(basic)\n");
            break;
        case DEBUG_VERBOSE:
            printf("(verbose)\n");
            break;
        case DEBUG_TRACE:
            printf("(trace)\n");
            break;
        case DEBUG_PROFILE:
            printf("(profile)\n");
            break;
        default:
            printf("(unknown)\n");
            break;
        }
        printf("  Stack Depth: %d\n", ctx->stack_depth);
        printf("  Total Commands: %ld\n", ctx->total_commands);
        return 0;
    }

    // Process subcommands - comprehensive implementation
    const char *subcmd = argv[1];

    if (strcmp(subcmd, "on") == 0 || strcmp(subcmd, "enable") == 0) {
        debug_enable(ctx, true);
        if (argc_real > 2) {
            // Set level if provided
            int level = atoi(argv[2]);
            if (level >= DEBUG_NONE && level <= DEBUG_PROFILE) {
                debug_set_level(ctx, (debug_level_t)level);
            }
        }
        printf("Debug mode enabled\n");
        return 0;
    }

    if (strcmp(subcmd, "off") == 0 || strcmp(subcmd, "disable") == 0) {
        debug_enable(ctx, false);
        printf("Debug mode disabled\n");
        return 0;
    }

    if (strcmp(subcmd, "level") == 0) {
        if (argc_real < 3) {
            printf("Current debug level: %d\n", ctx->level);
            return 0;
        }

        int level = atoi(argv[2]);
        if (level < DEBUG_NONE || level > DEBUG_PROFILE) {
            fprintf(stderr, "debug: Invalid level %d (must be 0-4)\n", level);
            return 1;
        }

        debug_set_level(ctx, (debug_level_t)level);
        printf("Debug level set to %d\n", level);
        return 0;
    }

    if (strcmp(subcmd, "trace") == 0) {
        if (argc_real < 3) {
            printf("Trace execution: %s\n",
                   ctx->trace_execution ? "enabled" : "disabled");
            return 0;
        }

        if (strcmp(argv[2], "on") == 0) {
            ctx->trace_execution = true;
            printf("Trace execution enabled\n");
        } else if (strcmp(argv[2], "off") == 0) {
            ctx->trace_execution = false;
            printf("Trace execution disabled\n");
        } else {
            fprintf(stderr,
                    "debug: Invalid trace option '%s' (use 'on' or 'off')\n",
                    argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(subcmd, "break") == 0 || strcmp(subcmd, "breakpoint") == 0) {
        if (argc_real < 3) {
            debug_list_breakpoints(ctx);
            return 0;
        }

        if (strcmp(argv[2], "add") == 0) {
            if (argc_real < 5) {
                fprintf(stderr, "debug: Usage: debug break add <file> <line> "
                                "[condition]\n");
                return 1;
            }

            const char *file = argv[3];
            int line = atoi(argv[4]);
            const char *condition = argc_real > 5 ? argv[5] : NULL;

            int id = debug_add_breakpoint(ctx, file, line, condition);
            if (id > 0) {
                printf("Breakpoint %d added at %s:%d\n", id, file, line);
            } else {
                fprintf(stderr, "debug: Failed to add breakpoint\n");
                return 1;
            }
            return 0;
        }

        if (strcmp(argv[2], "remove") == 0 || strcmp(argv[2], "delete") == 0) {
            if (argc_real < 4) {
                fprintf(stderr, "debug: Usage: debug break remove <id>\n");
                return 1;
            }

            int id = atoi(argv[3]);
            if (debug_remove_breakpoint(ctx, id)) {
                printf("Breakpoint %d removed\n", id);
            } else {
                fprintf(stderr, "debug: Breakpoint %d not found\n", id);
                return 1;
            }
            return 0;
        }

        if (strcmp(argv[2], "list") == 0) {
            debug_list_breakpoints(ctx);
            return 0;
        }

        if (strcmp(argv[2], "clear") == 0) {
            debug_clear_breakpoints(ctx);
            printf("All breakpoints cleared\n");
            return 0;
        }

        fprintf(stderr, "debug: Unknown breakpoint command '%s'\n", argv[2]);
        return 1;
    }

    if (strcmp(subcmd, "step") == 0) {
        debug_step_into(ctx);
        return 0;
    }

    if (strcmp(subcmd, "next") == 0) {
        debug_step_over(ctx);
        return 0;
    }

    if (strcmp(subcmd, "continue") == 0) {
        debug_continue(ctx);
        return 0;
    }

    if (strcmp(subcmd, "stack") == 0) {
        debug_show_stack(ctx);
        return 0;
    }

    if (strcmp(subcmd, "vars") == 0) {
        debug_inspect_all_variables(ctx);
        return 0;
    }

    if (strcmp(subcmd, "print") == 0) {
        if (argc_real < 3) {
            fprintf(stderr, "debug: Usage: debug print <variable>\n");
            return 1;
        }

        debug_inspect_variable(ctx, argv[2]);
        return 0;
    }

    if (strcmp(subcmd, "profile") == 0) {
        if (argc_real < 3) {
            printf("Performance profiling: %s\n",
                   ctx->profile_enabled ? "enabled" : "disabled");
            return 0;
        }

        if (strcmp(argv[2], "on") == 0) {
            debug_profile_start(ctx);
            printf("Performance profiling enabled\n");
        } else if (strcmp(argv[2], "off") == 0) {
            debug_profile_stop(ctx);
            printf("Performance profiling disabled\n");
        } else if (strcmp(argv[2], "report") == 0) {
            debug_profile_report(ctx);
        } else if (strcmp(argv[2], "reset") == 0) {
            debug_profile_reset(ctx);
            printf("Profile data reset\n");
        } else {
            fprintf(stderr, "debug: Invalid profile option '%s'\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(subcmd, "analyze") == 0) {
        if (argc_real < 3) {
            fprintf(stderr, "debug: Usage: debug analyze <script>\n");
            return 1;
        }

        debug_analyze_script(ctx, argv[2]);
        return 0;
    }

    if (strcmp(subcmd, "functions") == 0) {
        debug_list_functions(ctx);
        return 0;
    }

    if (strcmp(subcmd, "function") == 0) {
        if (argc_real < 3) {
            fprintf(stderr, "debug: Usage: debug function <name>\n");
            return 1;
        }

        debug_show_function(ctx, argv[2]);
        return 0;
    }

    if (strcmp(subcmd, "help") == 0) {
        printf("Debug command usage:\n");
        printf("  debug                    - Show debug status\n");
        printf("  debug on [level]         - Enable debug mode with optional "
               "level\n");
        printf("  debug off                - Disable debug mode\n");
        printf("  debug level [0-4]        - Set debug level\n");
        printf(
            "  debug trace on|off       - Enable/disable execution tracing\n");
        printf(
            "  debug break add <file> <line> [condition] - Add breakpoint\n");
        printf("  debug break remove <id>  - Remove breakpoint\n");
        printf("  debug break list         - List all breakpoints\n");
        printf("  debug break clear        - Clear all breakpoints\n");
        printf("  debug step               - Step into next statement\n");
        printf("  debug next               - Step over next statement\n");
        printf("  debug continue           - Continue execution\n");
        printf("  debug stack              - Show call stack\n");
        printf("  debug vars               - Show all variables\n");
        printf("  debug print <var>        - Print variable value\n");
        printf("  debug profile on|off|report|reset - Control profiling\n");
        printf("  debug analyze <script>   - Analyze script for issues\n");
        printf("  debug functions          - List all defined functions\n");
        printf("  debug function <name>    - Show function definition\n");
        printf("  debug help               - Show this help\n");
        printf("\nDebug levels:\n");
        printf("  0 - None (disabled)\n");
        printf("  1 - Basic debugging\n");
        printf("  2 - Verbose debugging\n");
        printf("  3 - Trace execution\n");
        printf("  4 - Full profiling\n");
        printf("\nShell Scripting Enhancement - ADVANCED FEATURES READY\n");
        return 0;
    }

    fprintf(stderr, "debug: Unknown command '%s'\n", subcmd);
    fprintf(stderr, "debug: Use 'debug help' for usage information\n");
    return 1;
}

/**
 * @brief Manage the layered display system
 *
 * Provides comprehensive control over the display integration system.
 * Supports subcommands:
 * - status: Show system status and health
 * - config: Show detailed configuration
 * - stats: Show usage statistics
 * - diagnostics: Show system diagnostics
 * - performance: Performance monitoring commands
 * - lle: LLE (Lush Line Editor) control commands
 * - help: Show usage information
 *
 * @param argc Argument count
 * @param argv Argument vector with display subcommand
 * @return 0 on success, 1 on error or unknown command
 */
int bin_display(int argc, char **argv) {
    if (argc < 2) {
        printf("Display Integration System\n");
        printf("Usage: display <command> [options]\n");
        printf("\nCommands:\n");
        printf("  status      - Show display integration status\n");
        printf("  config      - Show current configuration\n");
        printf("  stats       - Show performance statistics\n");
        printf("  diagnostics - Show detailed diagnostic information\n");
        printf("  lle         - LLE (Lush Line Editor) control commands\n");
        printf("  help        - Show this help message\n");
        printf("\nEnvironment Variables:\n");
        printf("  LUSH_DISPLAY_DEBUG=1|0        - Enable/disable debug "
               "output\n");
        printf("  LUSH_DISPLAY_OPTIMIZATION=0-4 - Set optimization level\n");
        return 0;
    }

    // NOTE: testsuggestion command was removed in v1.3.0 cleanup.
    // Legacy autosuggestions system was abandoned. LLE has its own
    // autosuggestions.

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "status") == 0) {
        // Show display integration status
        printf("Display Integration: ACTIVE (Layered display exclusive)\n");
        display_integration_health_t health = display_integration_get_health();
        printf("Health Status: %s\n",
               display_integration_health_string(health));

        display_integration_config_t config;
        if (display_integration_get_config(&config)) {
            printf("Configuration:\n");
            printf("  Layered display: enabled (exclusive system)\n");
            printf("  Caching: %s\n",
                   config.enable_caching ? "enabled" : "disabled");
            printf("  Performance monitoring: %s\n",
                   config.enable_performance_monitoring ? "enabled"
                                                        : "disabled");
            printf("  Optimization level: %d\n", config.optimization_level);
            printf("  Debug mode: %s\n",
                   config.debug_mode ? "enabled" : "disabled");
        }
        return 0;

    } else if (strcmp(subcmd, "config") == 0) {
        // Show detailed configuration
        display_integration_config_t config;
        if (!display_integration_get_config(&config)) {
            fprintf(stderr, "display: Failed to get configuration\n");
            return 1;
        }

        printf("=== Display Integration Configuration ===\n");
        printf("Core Features:\n");
        printf("  Layered display: enabled (exclusive system)\n");
        printf("  Caching: %s\n",
               config.enable_caching ? "enabled" : "disabled");
        printf("  Performance monitoring: %s\n",
               config.enable_performance_monitoring ? "enabled" : "disabled");
        printf("\nOptimization:\n");
        printf("  Optimization level: %d ", config.optimization_level);
        switch (config.optimization_level) {
        case 0:
            printf("(Disabled)\n");
            break;
        case 1:
            printf("(Basic)\n");
            break;
        case 2:
            printf("(Standard)\n");
            break;
        case 3:
            printf("(Aggressive)\n");
            break;
        case 4:
            printf("(Maximum)\n");
            break;
        default:
            printf("(Unknown)\n");
            break;
        }
        printf("  Performance threshold: %u ms\n",
               config.performance_threshold_ms);
        printf("  Cache hit rate threshold: %.1f%%\n",
               config.cache_hit_rate_threshold * 100.0);
        printf("\nBehavior:\n");
        printf("  Fallback on error: %s\n",
               config.fallback_on_error ? "enabled" : "disabled");
        printf("  Debug mode: %s\n",
               config.debug_mode ? "enabled" : "disabled");
        printf("  Max output size: %zu bytes\n", config.max_output_size);
        printf("========================================\n");
        return 0;

    } else if (strcmp(subcmd, "stats") == 0) {
        // Show performance statistics
        display_integration_stats_t stats;
        if (!display_integration_get_stats(&stats)) {
            fprintf(stderr, "display: Failed to get statistics\n");
            return 1;
        }

        printf("=== Display Integration Statistics ===\n");
        printf("Usage:\n");
        printf("  Total display calls: %llu\n",
               (unsigned long long)stats.total_display_calls);
        printf("  Layered display calls: %llu\n",
               (unsigned long long)stats.layered_display_calls);
        printf("  Fallback calls: %llu\n",
               (unsigned long long)stats.fallback_calls);

        if (stats.total_display_calls > 0) {
            double layered_rate = (double)stats.layered_display_calls /
                                  stats.total_display_calls * 100.0;
            double fallback_rate = (double)stats.fallback_calls /
                                   stats.total_display_calls * 100.0;
            printf("  Layered display rate: %.1f%%\n", layered_rate);
            printf("  Fallback rate: %.1f%%\n", fallback_rate);
        }

        if (display_integration_is_layered_active()) {
            printf("\nPerformance:\n");
            printf("  Average display time: %.2f ms\n",
                   stats.avg_layered_display_time_ns / 1000000.0);
            printf("  Cache hit rate: %.1f%%\n", stats.cache_hit_rate * 100.0);
            printf("  Memory usage: %zu bytes\n", stats.memory_usage_bytes);

            printf("\nHealth:\n");
            printf("  Performance within threshold: %s\n",
                   stats.performance_within_threshold ? "yes" : "no");
            printf("  Cache efficiency good: %s\n",
                   stats.cache_efficiency_good ? "yes" : "no");
            printf("  Memory usage acceptable: %s\n",
                   stats.memory_usage_acceptable ? "yes" : "no");
        }
        printf("=====================================\n");
        return 0;

    } else if (strcmp(subcmd, "diagnostics") == 0) {
        // Show detailed diagnostics
        display_integration_print_diagnostics();
        return 0;

    } else if (strcmp(subcmd, "test") == 0) {
        // Test layered display with actual content
        printf("Testing layered display system with actual content...\n");

        if (!display_integration_is_layered_active()) {
            printf("Error: Layered display system is not active. Run 'display "
                   "enable' first.\n");
            return 1;
        }

        // Force a redisplay with current content
        printf("Triggering display_integration_redisplay()...\n");
        display_integration_redisplay();
        printf("Display test completed.\n");

        return 0;

    } else if (strcmp(subcmd, "performance") == 0) {
        // Performance monitoring commands
        if (argc < 3) {
            printf("Performance Monitoring Commands:\n");
            printf("  display performance init          - Initialize "
                   "performance monitoring\n");
            printf("  display performance report        - Show performance "
                   "report\n");
            printf("  display performance report detail - Show detailed "
                   "performance report\n");
            printf("  display performance layers        - Show layer-specific "
                   "cache performance\n");
            printf("  display performance memory        - Show memory pool "
                   "fallback analysis\n");
            printf("  display performance baseline      - Establish "
                   "performance baseline\n");
            printf("  display performance reset         - Reset performance "
                   "metrics\n");
            printf("  display performance targets       - Check if targets are "
                   "being met\n");
            printf("  display performance monitoring on - Enable real-time "
                   "monitoring\n");
            printf("  display performance monitoring off - Disable real-time "
                   "monitoring\n");
            printf("  display performance debug         - Show debug "
                   "information\n");
            return 0;
        }

        const char *perf_cmd = argv[2];

        if (strcmp(perf_cmd, "init") == 0) {
            if (display_integration_perf_monitor_init()) {
                printf("Performance monitoring initialized\n");
                printf("Targets: Cache hit rate >75%%, Display timing <50ms\n");
                return 0;
            } else {
                fprintf(
                    stderr,
                    "display: Failed to initialize performance monitoring\n");
                return 1;
            }

        } else if (strcmp(perf_cmd, "report") == 0) {
            bool detailed = (argc > 3 && strcmp(argv[3], "detail") == 0);
            if (display_integration_perf_monitor_report(detailed)) {
                return 0;
            } else {
                fprintf(stderr,
                        "display: Failed to generate performance report\n");
                return 1;
            }

        } else if (strcmp(perf_cmd, "layers") == 0) {
            display_integration_print_layer_cache_report();
            return 0;

        } else if (strcmp(perf_cmd, "memory") == 0) {
            lush_pool_analyze_fallback_patterns();
            return 0;

        } else if (strcmp(perf_cmd, "baseline") == 0) {
            if (display_integration_establish_baseline()) {
                printf("Performance baseline established\n");
                return 0;
            } else {
                fprintf(stderr, "display: Failed to establish baseline (need "
                                "more measurements)\n");
                return 1;
            }

        } else if (strcmp(perf_cmd, "reset") == 0) {
            if (display_integration_perf_monitor_reset()) {
                printf("Performance metrics reset\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display: Failed to reset performance metrics\n");
                return 1;
            }

        } else if (strcmp(perf_cmd, "targets") == 0) {
            bool cache_met, timing_met;
            if (display_integration_perf_monitor_check_targets(&cache_met,
                                                           &timing_met)) {
                printf("Performance Target Status:\n");
                printf("  Cache Hit Rate: %s\n",
                       cache_met ? "OK MET" : "X NOT MET");
                printf("  Display Timing: %s\n",
                       timing_met ? "OK MET" : "X NOT MET");
                printf("  Overall: %s\n", (cache_met && timing_met)
                                              ? "OK ALL TARGETS MET"
                                              : "! NEEDS OPTIMIZATION");
                return 0;
            } else {
                fprintf(stderr,
                        "display: Failed to check performance targets\n");
                return 1;
            }

        } else if (strcmp(perf_cmd, "monitoring") == 0) {
            if (argc < 4) {
                fprintf(stderr,
                        "display: 'monitoring' requires 'on' or 'off'\n");
                return 1;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                if (display_integration_perf_monitor_set_active(true, 10)) {
                    printf("Real-time performance monitoring enabled (10Hz)\n");
                    return 0;
                } else {
                    fprintf(
                        stderr,
                        "display: Failed to enable performance monitoring\n");
                    return 1;
                }
            } else if (strcmp(state, "off") == 0) {
                if (display_integration_perf_monitor_set_active(false, 0)) {
                    printf("Real-time performance monitoring disabled\n");
                    return 0;
                } else {
                    fprintf(
                        stderr,
                        "display: Failed to disable performance monitoring\n");
                    return 1;
                }
            } else {
                fprintf(stderr,
                        "display: Invalid monitoring state '%s' (use 'on' or "
                        "'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(perf_cmd, "debug") == 0) {
            // Debug command to troubleshoot data collection
            printf("Performance Monitoring Debug Information:\n");

            // Check initialization status
            display_perf_metrics_t metrics;
            if (display_integration_perf_monitor_get_metrics(&metrics)) {
                printf("  Monitoring initialized: YES\n");
                printf("  Cache operations recorded: %" PRIu64 "\n",
                       metrics.cache_operations_total);
                printf("  Display operations recorded: %" PRIu64 "\n",
                       metrics.display_operations_measured);
                printf("  Monitoring active: %s\n",
                       metrics.monitoring_active ? "YES" : "NO");
                printf("  Last measurement time: %ld\n",
                       metrics.last_measurement_time);
            } else {
                printf("  Monitoring initialized: NO\n");
            }

            // Check integration stats
            display_integration_stats_t stats;
            if (display_integration_get_stats(&stats)) {
                printf("  Total display calls: %" PRIu64 "\n",
                       stats.total_display_calls);
                printf("  Layered display calls: %" PRIu64 "\n",
                       stats.layered_display_calls);
                printf("  Fallback calls: %" PRIu64 "\n", stats.fallback_calls);
                printf("  Integration active: %s\n",
                       display_integration_is_layered_active() ? "YES" : "NO");
            }

            // Force a measurement test
            printf("Triggering test measurements...\n");
            display_integration_record_display_timing(5000000); // 5ms test
            display_integration_record_cache_operation(true); // Test cache hit
            display_integration_record_cache_operation(
                false); // Test cache miss
            printf("Test measurements recorded.\n");

            return 0;

        } else {
            fprintf(stderr, "display: Unknown performance command '%s'\n",
                    perf_cmd);
            fprintf(
                stderr,
                "display: Use 'display performance' for available commands\n");
            return 1;
        }

    } else if (strcmp(subcmd, "lle") == 0) {
        // LLE (Lush Line Editor) control commands
        if (argc < 3) {
            printf("LLE (Lush Line Editor) Commands\n");
            printf("Usage: display lle <command> [options]\n");
            printf("\nStatus:\n");
            printf("  status           - Show LLE status and configuration\n");
            printf("  diagnostics      - Show LLE diagnostics and health\n");
            printf("\nFeature Control:\n");
            printf("  autosuggestions on|off  - Control Fish-style "
                   "autosuggestions\n");
            printf("  syntax on|off           - Control syntax highlighting\n");
            printf("  transient on|off        - Control transient prompts\n");
            printf(
                "  hot-reload on|off       - Control theme hot-reload\n");
            printf(
                "  newline-before on|off   - Control newline before prompt\n");
            printf("  multiline on|off        - Control multiline editing\n");
            printf("  theme [list|set <name>] - Control LLE prompt theme\n");
            printf("\nReset Commands (recovery):\n");
            printf(
                "  reset            - Hard reset: destroy/recreate editor\n");
            printf("  reset --soft     - Soft reset: abort current line\n");
            printf(
                "  reset --terminal - Nuclear reset: hard + terminal reset\n");
            printf("\nInformation:\n");
            printf("  keybindings [cmd] - Keybinding management\n");
            printf("                      list    - Show active bindings\n");
            printf("                      reload  - Reload from config file\n");
            printf("                      actions - List all action names\n");
            printf(
                "  completions [cmd] - Custom completion source management\n");
            printf("                      list    - Show all sources\n");
            printf("                      reload  - Reload from config file\n");
            printf("\nHistory:\n");
            printf("  history [cmd]    - History behavior configuration\n");
            printf("                     status  - Show current settings\n");
            printf("                     dedup <scope|strategy|on|off>\n");
            printf("                     nav-dedup on|off - Skip dups when "
                   "navigating\n");
            printf("                     nav-unique on|off - Show each cmd "
                   "once per session\n");
            printf("\nNote: Changes apply immediately. Use 'config save' to "
                   "persist.\n");
            return 0;
        }

        const char *lle_cmd = argv[2];

        if (strcmp(lle_cmd, "status") == 0) {
            lle_editor_t *editor = lle_get_global_editor();

            printf("LLE Status:\n");
            printf("  Line Editor: LLE (Lush Line Editor)\n");
            printf("  History file: ~/.lush_history\n");
            printf("  Editor: %s\n",
                   editor ? "initialized" : "not initialized");

            printf("\nLLE Features:\n");
            printf("  Multi-line editing: %s\n",
                   config.lle_enable_multiline_editing ? "enabled"
                                                       : "disabled");
            printf("  History deduplication: %s\n",
                   config.lle_enable_deduplication ? "enabled" : "disabled");
            printf("  Forensic tracking: %s\n",
                   config.lle_enable_forensic_tracking ? "enabled"
                                                       : "disabled");

            if (editor && editor->history_system) {
                size_t count = 0;
                lle_history_get_entry_count(editor->history_system, &count);
                printf("\nHistory:\n");
                printf("  Entries: %zu\n", count);
            }
            return 0;

        } else if (strcmp(lle_cmd, "history") == 0) {
            /* History behavior configuration commands */
            if (argc < 4) {
                /* No subcommand - show help with all options and values */
                printf("Usage: display lle history <command> [options]\n");
                printf("\nCommands:\n");
                printf("  status                  - Show current settings\n");
                printf("  dedup on|off            - Enable/disable write-time "
                       "deduplication\n");
                printf("  dedup scope <value>     - Set dedup scope\n");
                printf("      Values: none, session, recent, global\n");
                printf("  dedup strategy <value>  - Set dedup strategy\n");
                printf("      Values: ignore, keep-recent, keep-frequent, "
                       "merge, keep-all\n");
                printf("  nav-dedup on|off        - Skip duplicates when "
                       "navigating history\n");
                printf("  nav-unique on|off       - Show each command once "
                       "per navigation session\n");
                printf("\nExamples:\n");
                printf("  display lle history status\n");
                printf("  display lle history dedup on\n");
                printf("  display lle history dedup scope global\n");
                printf("  display lle history dedup strategy keep-recent\n");
                printf("  display lle history nav-dedup off\n");
                printf("  display lle history nav-unique on\n");
                printf("\nNote: Use 'config save' to persist changes.\n");
                return 0;
            }

            const char *hist_subcmd = argv[3];

            if (strcmp(hist_subcmd, "status") == 0) {
                /* Show current history settings */
                printf("LLE History Settings:\n");
                printf("\nWrite-time Deduplication:\n");
                printf("  Enabled: %s\n",
                       config.lle_enable_deduplication ? "yes" : "no");

                /* Scope */
                const char *scope_str = "unknown";
                switch (config.lle_dedup_scope) {
                case LLE_DEDUP_SCOPE_NONE:
                    scope_str = "none";
                    break;
                case LLE_DEDUP_SCOPE_SESSION:
                    scope_str = "session";
                    break;
                case LLE_DEDUP_SCOPE_RECENT:
                    scope_str = "recent";
                    break;
                case LLE_DEDUP_SCOPE_GLOBAL:
                    scope_str = "global";
                    break;
                }
                printf("  Scope: %s\n", scope_str);

                /* Strategy */
                const char *strategy_str = "unknown";
                switch (config.lle_dedup_strategy) {
                case LLE_DEDUP_STRATEGY_IGNORE:
                    strategy_str = "ignore";
                    break;
                case LLE_DEDUP_STRATEGY_KEEP_RECENT:
                    strategy_str = "keep-recent";
                    break;
                case LLE_DEDUP_STRATEGY_KEEP_FREQUENT:
                    strategy_str = "keep-frequent";
                    break;
                case LLE_DEDUP_STRATEGY_MERGE:
                    strategy_str = "merge";
                    break;
                case LLE_DEDUP_STRATEGY_KEEP_ALL:
                    strategy_str = "keep-all";
                    break;
                }
                printf("  Strategy: %s\n", strategy_str);

                printf("\nNavigation Deduplication:\n");
                printf("  Skip duplicates: %s\n",
                       config.lle_dedup_navigation ? "yes" : "no");
                printf("  Unique per session: %s\n",
                       config.lle_dedup_navigation_unique ? "yes" : "no");

                printf("\nOther Settings:\n");
                printf("  Unicode normalize: %s\n",
                       config.lle_dedup_unicode_normalize ? "yes" : "no");

                printf("\nUse 'display lle history <option> <value>' to "
                       "change settings.\n");
                printf("Use 'config save' to persist changes.\n");
                return 0;

            } else if (strcmp(hist_subcmd, "dedup") == 0) {
                /* Deduplication settings */
                if (argc < 5) {
                    printf("Usage: display lle history dedup <option>\n");
                    printf("Options:\n");
                    printf("  on              - Enable write-time "
                           "deduplication\n");
                    printf("  off             - Disable write-time "
                           "deduplication\n");
                    printf("  scope <value>   - Set scope (none, session, "
                           "recent, global)\n");
                    printf("  strategy <value> - Set strategy (ignore, "
                           "keep-recent, keep-frequent, merge, keep-all)\n");
                    printf("  clean           - Remove all duplicates from "
                           "existing history\n");
                    return 0;
                }

                const char *dedup_opt = argv[4];

                if (strcmp(dedup_opt, "on") == 0) {
                    config.lle_enable_deduplication = true;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean("lle.enable_deduplication",
                                                    true);
                    }
                    printf("Write-time deduplication enabled\n");
                    return 0;

                } else if (strcmp(dedup_opt, "off") == 0) {
                    config.lle_enable_deduplication = false;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean("lle.enable_deduplication",
                                                    false);
                    }
                    printf("Write-time deduplication disabled\n");
                    return 0;

                } else if (strcmp(dedup_opt, "scope") == 0) {
                    if (argc < 6) {
                        printf("Usage: display lle history dedup scope "
                               "<none|session|recent|global>\n");
                        return 1;
                    }
                    const char *scope_val = argv[5];

                    if (strcmp(scope_val, "none") == 0) {
                        config.lle_dedup_scope = LLE_DEDUP_SCOPE_NONE;
                    } else if (strcmp(scope_val, "session") == 0) {
                        config.lle_dedup_scope = LLE_DEDUP_SCOPE_SESSION;
                    } else if (strcmp(scope_val, "recent") == 0) {
                        config.lle_dedup_scope = LLE_DEDUP_SCOPE_RECENT;
                    } else if (strcmp(scope_val, "global") == 0) {
                        config.lle_dedup_scope = LLE_DEDUP_SCOPE_GLOBAL;

                        /* When switching to global scope, run full dedup scan */
                        lle_editor_t *editor = lle_get_global_editor();
                        if (editor && editor->history_system &&
                            editor->history_system->dedup_engine) {
                            size_t removed = 0;
                            lle_history_dedup_full_scan(
                                editor->history_system->dedup_engine, &removed);
                            if (removed > 0) {
                                printf("Cleaned %zu duplicate entries\n",
                                       removed);
                                /* Save to persist changes */
                                const char *home = getenv("HOME");
                                if (home) {
                                    char history_path[1024];
                                    snprintf(history_path, sizeof(history_path),
                                             "%s/.lush_history", home);
                                    lle_history_save_to_file(
                                        editor->history_system, history_path);
                                }
                            }
                        }
                    } else {
                        fprintf(stderr,
                                "Invalid scope '%s'. Use: none, session, "
                                "recent, global\n",
                                scope_val);
                        return 1;
                    }

                    if (config_registry_is_initialized()) {
                        config_registry_set_string("lle.dedup_scope",
                                                   scope_val);
                    }
                    printf("Deduplication scope set to '%s'\n", scope_val);
                    return 0;

                } else if (strcmp(dedup_opt, "strategy") == 0) {
                    if (argc < 6) {
                        printf("Usage: display lle history dedup strategy "
                               "<ignore|keep-recent|keep-frequent|merge|"
                               "keep-all>\n");
                        return 1;
                    }
                    const char *strategy_val = argv[5];

                    if (strcmp(strategy_val, "ignore") == 0) {
                        config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_IGNORE;
                    } else if (strcmp(strategy_val, "keep-recent") == 0) {
                        config.lle_dedup_strategy =
                            LLE_DEDUP_STRATEGY_KEEP_RECENT;
                    } else if (strcmp(strategy_val, "keep-frequent") == 0) {
                        config.lle_dedup_strategy =
                            LLE_DEDUP_STRATEGY_KEEP_FREQUENT;
                    } else if (strcmp(strategy_val, "merge") == 0) {
                        config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_MERGE;
                    } else if (strcmp(strategy_val, "keep-all") == 0) {
                        config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_KEEP_ALL;
                    } else {
                        fprintf(stderr,
                                "Invalid strategy '%s'. Use: ignore, "
                                "keep-recent, keep-frequent, merge, keep-all\n",
                                strategy_val);
                        return 1;
                    }

                    if (config_registry_is_initialized()) {
                        config_registry_set_string("lle.dedup_strategy",
                                                   strategy_val);
                    }
                    printf("Deduplication strategy set to '%s'\n",
                           strategy_val);
                    return 0;

                } else if (strcmp(dedup_opt, "clean") == 0) {
                    /* Full history deduplication scan */
                    lle_editor_t *editor = lle_get_global_editor();
                    if (!editor || !editor->history_system) {
                        fprintf(stderr,
                                "Error: History system not available\n");
                        return 1;
                    }

                    if (!editor->history_system->dedup_engine) {
                        fprintf(stderr,
                                "Error: Deduplication engine not available\n");
                        return 1;
                    }

                    size_t removed = 0;
                    lle_result_t result = lle_history_dedup_full_scan(
                        editor->history_system->dedup_engine, &removed);
                    if (result != LLE_SUCCESS) {
                        fprintf(stderr,
                                "Error: Failed to run deduplication scan\n");
                        return 1;
                    }

                    if (removed > 0) {
                        printf("Removed %zu duplicate entries from history\n",
                               removed);
                        /* Save history to persist changes */
                        const char *home = getenv("HOME");
                        if (home) {
                            char history_path[1024];
                            snprintf(history_path, sizeof(history_path),
                                     "%s/.lush_history", home);
                            lle_history_save_to_file(editor->history_system,
                                                     history_path);
                        }
                    } else {
                        printf("No duplicate entries found in history\n");
                    }
                    return 0;

                } else {
                    fprintf(stderr,
                            "Unknown dedup option '%s'. Use: on, off, scope, "
                            "strategy, clean\n",
                            dedup_opt);
                    return 1;
                }

            } else if (strcmp(hist_subcmd, "nav-dedup") == 0) {
                /* Navigation-time duplicate skipping */
                if (argc < 5) {
                    printf("Navigation duplicate skipping: %s\n",
                           config.lle_dedup_navigation ? "enabled" : "disabled");
                    printf("Usage: display lle history nav-dedup on|off\n");
                    return 0;
                }

                const char *val = argv[4];
                if (strcmp(val, "on") == 0) {
                    config.lle_dedup_navigation = true;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean("lle.dedup_navigation",
                                                    true);
                    }
                    printf("Navigation duplicate skipping enabled\n");
                    return 0;
                } else if (strcmp(val, "off") == 0) {
                    config.lle_dedup_navigation = false;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean("lle.dedup_navigation",
                                                    false);
                    }
                    printf("Navigation duplicate skipping disabled\n");
                    return 0;
                } else {
                    fprintf(stderr, "Invalid value '%s'. Use: on, off\n", val);
                    return 1;
                }

            } else if (strcmp(hist_subcmd, "nav-unique") == 0) {
                /* Unique entries per navigation session */
                if (argc < 5) {
                    printf("Unique entries per session: %s\n",
                           config.lle_dedup_navigation_unique ? "enabled"
                                                              : "disabled");
                    printf("Usage: display lle history nav-unique on|off\n");
                    return 0;
                }

                const char *val = argv[4];
                if (strcmp(val, "on") == 0) {
                    config.lle_dedup_navigation_unique = true;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean(
                            "lle.dedup_navigation_unique", true);
                    }
                    printf("Unique entries per navigation session enabled\n");
                    return 0;
                } else if (strcmp(val, "off") == 0) {
                    config.lle_dedup_navigation_unique = false;
                    if (config_registry_is_initialized()) {
                        config_registry_set_boolean(
                            "lle.dedup_navigation_unique", false);
                    }
                    printf("Unique entries per navigation session disabled\n");
                    return 0;
                } else {
                    fprintf(stderr, "Invalid value '%s'. Use: on, off\n", val);
                    return 1;
                }

            } else {
                fprintf(stderr, "Unknown history command '%s'\n", hist_subcmd);
                fprintf(stderr,
                        "Use 'display lle history' for available commands\n");
                return 1;
            }

        } else if (strcmp(lle_cmd, "keybindings") == 0) {
            /* Keybinding management commands */
            lle_editor_t *editor = lle_get_global_editor();

            /* Check for subcommand */
            const char *kb_subcmd = (argc >= 4) ? argv[3] : "list";

            if (strcmp(kb_subcmd, "reload") == 0) {
                /* Reload user keybindings from config file */
                if (!editor || !editor->keybinding_manager) {
                    fprintf(stderr,
                            "display lle keybindings reload: LLE not active\n");
                    fprintf(stderr, "Run 'display lle enable' first\n");
                    return 1;
                }

                printf("Reloading keybindings from "
                       "~/.config/lush/keybindings.toml...\n");
                lle_keybinding_load_result_t load_result;
                lle_result_t result = lle_keybinding_reload_user_config(
                    editor->keybinding_manager, &load_result);

                if (result == LLE_SUCCESS) {
                    printf("Keybindings reloaded: %zu bindings applied, %zu "
                           "errors\n",
                           load_result.bindings_applied,
                           load_result.errors_count);
                    if (load_result.errors_count > 0) {
                        printf("(Check stderr for error details)\n");
                    }
                    return 0;
                } else if (result == LLE_ERROR_NOT_FOUND) {
                    printf("No keybindings config file found at "
                           "~/.config/lush/keybindings.toml\n");
                    printf("Create this file to customize keybindings.\n");
                    printf("\nExample format:\n");
                    printf("  [bindings]\n");
                    printf("  \"C-a\" = \"end-of-line\"      # Swap C-a and "
                           "C-e\n");
                    printf("  \"C-e\" = \"beginning-of-line\"\n");
                    printf("  \"C-s\" = \"none\"             # Unbind a key\n");
                    return 0;
                } else {
                    fprintf(
                        stderr,
                        "display lle keybindings reload: Failed (error %d)\n",
                        result);
                    return 1;
                }

            } else if (strcmp(kb_subcmd, "actions") == 0) {
                /* List all available action names */
                printf("LLE Available Actions\n");
                printf("=====================\n");
                printf("\nThese action names can be used in "
                       "~/.config/lush/keybindings.toml\n\n");

                const lle_action_registry_entry_t *entry;
                size_t index = 0;

                printf("Movement:\n");
                while ((entry = lle_action_registry_get_by_index(index++)) !=
                       NULL) {
                    if (strstr(entry->name, "beginning") ||
                        strstr(entry->name, "end") ||
                        strstr(entry->name, "forward") ||
                        strstr(entry->name, "backward")) {
                        printf("  %-30s  %s\n", entry->name,
                               entry->description ? entry->description : "");
                    }
                }

                index = 0;
                printf("\nEditing:\n");
                while ((entry = lle_action_registry_get_by_index(index++)) !=
                       NULL) {
                    if (strstr(entry->name, "delete") ||
                        strstr(entry->name, "kill") ||
                        strstr(entry->name, "yank") ||
                        strstr(entry->name, "undo") ||
                        strstr(entry->name, "redo") ||
                        strstr(entry->name, "transpose") ||
                        strstr(entry->name, "case") ||
                        strstr(entry->name, "upcase") ||
                        strstr(entry->name, "downcase") ||
                        strstr(entry->name, "capitalize")) {
                        printf("  %-30s  %s\n", entry->name,
                               entry->description ? entry->description : "");
                    }
                }

                index = 0;
                printf("\nHistory:\n");
                while ((entry = lle_action_registry_get_by_index(index++)) !=
                       NULL) {
                    if (strstr(entry->name, "history") ||
                        strstr(entry->name, "search")) {
                        printf("  %-30s  %s\n", entry->name,
                               entry->description ? entry->description : "");
                    }
                }

                index = 0;
                printf("\nCompletion:\n");
                while ((entry = lle_action_registry_get_by_index(index++)) !=
                       NULL) {
                    if (strstr(entry->name, "complet")) {
                        printf("  %-30s  %s\n", entry->name,
                               entry->description ? entry->description : "");
                    }
                }

                index = 0;
                printf("\nOther:\n");
                while ((entry = lle_action_registry_get_by_index(index++)) !=
                       NULL) {
                    if (strstr(entry->name, "accept") ||
                        strstr(entry->name, "abort") ||
                        strstr(entry->name, "clear") ||
                        strstr(entry->name, "quoted") ||
                        strstr(entry->name, "tab") ||
                        strstr(entry->name, "newline") ||
                        strstr(entry->name, "eof") ||
                        strstr(entry->name, "none")) {
                        printf("  %-30s  %s\n", entry->name,
                               entry->description ? entry->description : "");
                    }
                }

                printf("\nSpecial:\n");
                printf("  %-30s  %s\n", "none", "Unbind a key (remove action)");

                return 0;

            } else if (strcmp(kb_subcmd, "list") == 0 ||
                       strcmp(kb_subcmd, "help") == 0 || kb_subcmd[0] == '-') {
                /* Show help if --help or just 'list' with no bindings to show
                 */
                if (strcmp(kb_subcmd, "help") == 0 ||
                    strcmp(kb_subcmd, "--help") == 0) {
                    printf("LLE Keybinding Commands\n");
                    printf("=======================\n\n");
                    printf("Usage: display lle keybindings [command]\n\n");
                    printf("Commands:\n");
                    printf("  list     - Show active keybindings (default)\n");
                    printf(
                        "  reload   - Reload keybindings from config file\n");
                    printf("  actions  - List all available action names\n");
                    printf("  help     - Show this help message\n");
                    printf(
                        "\nConfig file: ~/.config/lush/keybindings.toml\n");
                    printf("\nExample config:\n");
                    printf("  [bindings]\n");
                    printf("  \"C-a\" = \"end-of-line\"\n");
                    printf("  \"M-p\" = \"history-search-backward\"\n");
                    printf("  \"C-s\" = \"none\"  # unbind\n");
                    return 0;
                }
            }

            /* Default: list active keybindings */
            printf("LLE Active Keybindings (Emacs mode)\n");
            printf("====================================\n");

            if (editor && editor->keybinding_manager) {
                lle_keybinding_info_t *bindings = NULL;
                size_t count = 0;

                if (lle_keybinding_manager_list_bindings(
                        editor->keybinding_manager, &bindings, &count) ==
                    LLE_SUCCESS) {
                    printf("\nNavigation:\n");
                    for (size_t i = 0; i < count; i++) {
                        const char *name = bindings[i].function_name
                                               ? bindings[i].function_name
                                               : "unknown";
                        if (strstr(name, "beginning") || strstr(name, "end") ||
                            strstr(name, "forward") ||
                            strstr(name, "backward") || strstr(name, "left") ||
                            strstr(name, "right") || strstr(name, "up") ||
                            strstr(name, "down")) {
                            printf("  %-12s  %s\n", bindings[i].key_sequence,
                                   name);
                        }
                    }

                    printf("\nEditing:\n");
                    for (size_t i = 0; i < count; i++) {
                        const char *name = bindings[i].function_name
                                               ? bindings[i].function_name
                                               : "unknown";
                        if (strstr(name, "delete") || strstr(name, "kill") ||
                            strstr(name, "yank") || strstr(name, "undo") ||
                            strstr(name, "redo") || strstr(name, "transpose")) {
                            printf("  %-12s  %s\n", bindings[i].key_sequence,
                                   name);
                        }
                    }

                    printf("\nHistory:\n");
                    for (size_t i = 0; i < count; i++) {
                        const char *name = bindings[i].function_name
                                               ? bindings[i].function_name
                                               : "unknown";
                        if (strstr(name, "history") || strstr(name, "search") ||
                            strstr(name, "previous") || strstr(name, "next")) {
                            printf("  %-12s  %s\n", bindings[i].key_sequence,
                                   name);
                        }
                    }

                    printf("\nOther:\n");
                    for (size_t i = 0; i < count; i++) {
                        const char *name = bindings[i].function_name
                                               ? bindings[i].function_name
                                               : "unknown";
                        if (strstr(name, "accept") || strstr(name, "abort") ||
                            strstr(name, "clear") || strstr(name, "complete")) {
                            printf("  %-12s  %s\n", bindings[i].key_sequence,
                                   name);
                        }
                    }

                    printf("\nTotal: %zu keybindings\n", count);
                } else {
                    printf("  (Unable to retrieve keybindings)\n");
                }
            } else {
                /* Show default keybindings when LLE not active */
                printf("\nNavigation:\n");
                printf("  C-a          beginning-of-line\n");
                printf("  C-e          end-of-line\n");
                printf("  C-f / RIGHT  forward-char\n");
                printf("  C-b / LEFT   backward-char\n");
                printf("  M-f          forward-word\n");
                printf("  M-b          backward-word\n");
                printf("\nEditing:\n");
                printf("  C-d / DEL    delete-char\n");
                printf("  BACKSPACE    backward-delete-char\n");
                printf("  C-k          kill-line\n");
                printf("  C-u          unix-line-discard\n");
                printf("  C-w          backward-kill-word\n");
                printf("  C-y          yank\n");
                printf("  C-_          undo\n");
                printf("  C-^          redo\n");
                printf("\nHistory:\n");
                printf("  C-p / UP     previous-history\n");
                printf("  C-n / DOWN   next-history\n");
                printf("  C-r          reverse-search-history\n");
                printf("\nOther:\n");
                printf("  RET          accept-line\n");
                printf("  C-c          abort\n");
                printf("  C-l          clear-screen\n");
                printf("  TAB          complete\n");
            }
            return 0;

        } else if (strcmp(lle_cmd, "autosuggestions") == 0) {
            /* Control autosuggestions */
            if (argc < 4) {
                printf("Autosuggestions: %s\n",
                       config.display_autosuggestions ? "enabled" : "disabled");
                printf("Usage: display lle autosuggestions on|off\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.display_autosuggestions = true;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.autosuggestions", true);
                }
                printf("Autosuggestions enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_autosuggestions = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.autosuggestions", false);
                }
                printf("Autosuggestions disabled\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display lle autosuggestions: Invalid option '%s' (use "
                        "'on' or 'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "syntax") == 0) {
            /* Control syntax highlighting */
            if (argc < 4) {
                printf("Syntax highlighting: %s\n",
                       config.display_syntax_highlighting ? "enabled"
                                                          : "disabled");
                printf("Usage: display lle syntax on|off\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.display_syntax_highlighting = true;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.syntax_highlighting", true);
                }
                /* Apply to runtime: update the command layer */
                display_controller_t *dc = display_integration_get_controller();
                if (dc && dc->compositor && dc->compositor->command_layer) {
                    command_layer_set_syntax_enabled(dc->compositor->command_layer, true);
                }
                printf("Syntax highlighting enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_syntax_highlighting = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.syntax_highlighting", false);
                }
                /* Apply to runtime: update the command layer */
                display_controller_t *dc = display_integration_get_controller();
                if (dc && dc->compositor && dc->compositor->command_layer) {
                    command_layer_set_syntax_enabled(dc->compositor->command_layer, false);
                }
                printf("Syntax highlighting disabled\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display lle syntax: Invalid option '%s' (use 'on' or "
                        "'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "transient") == 0) {
            /* Control transient prompts (Spec 25 Section 12) */
            if (argc < 4) {
                printf("Transient prompts: %s\n",
                       config.display_transient_prompt ? "enabled"
                                                       : "disabled");
                printf("Usage: display lle transient on|off\n");
                printf("\nTransient prompts simplify previous prompts in "
                       "scrollback,\n");
                printf(
                    "reducing visual clutter from fancy multi-line prompts.\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.display_transient_prompt = true;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.transient_prompt", true);
                }
                /* Also update composer config if available */
                if (g_lle_integration && g_lle_integration->prompt_composer) {
                    g_lle_integration->prompt_composer->config
                        .enable_transient = true;
                }
                printf("Transient prompts enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_transient_prompt = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.transient_prompt", false);
                }
                /* Also update composer config if available */
                if (g_lle_integration && g_lle_integration->prompt_composer) {
                    g_lle_integration->prompt_composer->config
                        .enable_transient = false;
                }
                printf("Transient prompts disabled\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display lle transient: Invalid option '%s' (use 'on' "
                        "or 'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "hot-reload") == 0) {
            /* Control theme hot-reload (auto-reload on file change) */
            if (argc < 4) {
                printf("Theme hot-reload: %s\n",
                       config.display_theme_hot_reload ? "enabled"
                                                       : "disabled");
                printf("Usage: display lle hot-reload on|off\n");
                printf("\nAutomatically reloads the active theme when its\n");
                printf("TOML file is modified on disk.\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.display_theme_hot_reload = true;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.theme_hot_reload",
                                                true);
                }
                printf("Theme hot-reload enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_theme_hot_reload = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.theme_hot_reload",
                                                false);
                }
                printf("Theme hot-reload disabled\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display lle hot-reload: Invalid option '%s' (use "
                        "'on' or 'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "newline-before") == 0) {
            /* Control newline before prompt for visual separation */
            if (argc < 4) {
                printf("Newline before prompt: %s\n",
                       config.display_newline_before_prompt ? "enabled"
                                                            : "disabled");
                printf("Usage: display lle newline-before on|off\n");
                printf("\nPrints a blank line before each prompt for visual "
                       "separation\n");
                printf("between command output and the next prompt.\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.display_newline_before_prompt = true;
                if (g_lle_integration && g_lle_integration->prompt_composer) {
                    g_lle_integration->prompt_composer->config
                        .newline_before_prompt = true;
                }
                printf("Newline before prompt enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_newline_before_prompt = false;
                if (g_lle_integration && g_lle_integration->prompt_composer) {
                    g_lle_integration->prompt_composer->config
                        .newline_before_prompt = false;
                }
                printf("Newline before prompt disabled\n");
                return 0;
            } else {
                fprintf(
                    stderr,
                    "display lle newline-before: Invalid option '%s' (use 'on' "
                    "or 'off')\n",
                    state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "multiline") == 0) {
            /* Control multiline editing */
            if (argc < 4) {
                printf("Multiline editing: %s\n",
                       config.lle_enable_multiline_editing ? "enabled"
                                                           : "disabled");
                printf("Usage: display lle multiline on|off\n");
                return 0;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                config.lle_enable_multiline_editing = true;
                printf("Multiline editing enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.lle_enable_multiline_editing = false;
                printf("Multiline editing disabled\n");
                return 0;
            } else {
                fprintf(stderr,
                        "display lle multiline: Invalid option '%s' (use 'on' "
                        "or 'off')\n",
                        state);
                return 1;
            }

        } else if (strcmp(lle_cmd, "diagnostics") == 0) {
            /* Show LLE diagnostics */
            lle_editor_t *editor = lle_get_global_editor();

            printf("LLE Diagnostics\n");
            printf("===============\n");

            printf("\nSystem Status:\n");
            printf("  Line Editor: LLE (Lush Line Editor)\n");
            printf("  Global editor: %s\n",
                   editor ? "initialized" : "not initialized");

            if (editor) {
                printf("\nSubsystems:\n");
                /* Buffer and keybindings are session-scoped - created during
                 * readline and cleaned up after. They're not "MISSING", just
                 * inactive between prompts. */
                printf("  Buffer: %s\n",
                       editor->buffer ? "OK" : "OK (session-scoped)");
                printf("  History: %s\n",
                       editor->history_system ? "OK" : "MISSING");
                printf("  Keybindings: %s\n",
                       editor->keybinding_manager
                           ? "OK"
                           : "OK (session-scoped)");
                printf("  Kill ring: %s\n",
                       editor->kill_ring ? "OK" : "MISSING");
                printf("  Change tracker: %s\n",
                       editor->change_tracker ? "OK" : "MISSING");
                printf("  Cursor manager: %s\n",
                       editor->cursor_manager ? "OK" : "MISSING");

                if (editor->history_system) {
                    size_t count = 0;
                    lle_history_get_entry_count(editor->history_system, &count);
                    printf("\nHistory:\n");
                    printf("  Entries loaded: %zu\n", count);
                }

                if (editor->keybinding_manager) {
                    size_t kb_count = 0;
                    lle_keybinding_manager_get_count(editor->keybinding_manager,
                                                     &kb_count);
                    printf("\nKeybindings:\n");
                    printf("  Bindings registered: %zu\n", kb_count);

                    uint64_t avg_us = 0, max_us = 0;
                    if (lle_keybinding_manager_get_stats(
                            editor->keybinding_manager, &avg_us, &max_us) ==
                        LLE_SUCCESS) {
                        printf("  Avg lookup time: %lu µs\n",
                               (unsigned long)avg_us);
                        printf("  Max lookup time: %lu µs\n",
                               (unsigned long)max_us);
                        printf("  Performance: %s\n",
                               max_us < 50 ? "OK (<50µs)" : "SLOW (>50µs)");
                    }
                }
            }

            printf("\nFeature Configuration:\n");
            printf("  Autosuggestions: %s\n",
                   config.display_autosuggestions ? "enabled" : "disabled");
            printf("  Syntax highlighting: %s\n",
                   config.display_syntax_highlighting ? "enabled" : "disabled");
            printf("  Transient prompts: %s\n",
                   config.display_transient_prompt ? "enabled" : "disabled");
            printf("  Multiline editing: %s\n",
                   config.lle_enable_multiline_editing ? "enabled"
                                                       : "disabled");
            printf("  History deduplication: %s\n",
                   config.lle_enable_deduplication ? "enabled" : "disabled");
            printf("  Interactive search: %s\n",
                   config.lle_enable_interactive_search ? "enabled"
                                                        : "disabled");

            printf("\nHealth: ");
            if (!editor) {
                printf("ERROR (editor not initialized)\n");
            } else if (!editor->history_system) {
                /* Only check persistent subsystems - buffer and keybindings
                 * are session-scoped and intentionally NULL between prompts */
                printf("DEGRADED (missing persistent subsystems)\n");
            } else {
                printf("OK\n");
            }

            /* Watchdog Statistics */
            printf("\nWatchdog (Deadlock Detection):\n");
            lle_watchdog_stats_t wd_stats;
            if (lle_watchdog_get_stats(&wd_stats) == LLE_SUCCESS) {
                printf("  Timer resets (pets): %u\n", wd_stats.total_pets);
                printf("  Timeouts fired: %u\n", wd_stats.total_fires);
                printf("  Successful recoveries: %u\n",
                       wd_stats.total_recoveries);
                if (wd_stats.total_fires > 0) {
                    double recovery_rate = (double)wd_stats.total_recoveries /
                                           wd_stats.total_fires * 100.0;
                    printf("  Recovery rate: %.1f%%\n", recovery_rate);
                }
                printf("  Currently armed: %s\n",
                       lle_watchdog_is_armed() ? "yes" : "no");
            } else {
                printf("  Status: not initialized\n");
            }

            /* Safety System Statistics */
            printf("\nSafety System (Panic Recovery):\n");
            printf("  %s\n", lle_safety_get_stats_summary());
            printf("  Init state: %s\n", lle_safety_get_init_state_summary());
            printf("  Recovery mode: %s\n",
                   lle_safety_is_recovery_mode() ? "ACTIVE" : "inactive");

            /* Shell Event Hub Statistics */
            printf("\nShell Event Hub:\n");
            uint64_t events_fired = 0, dir_changes = 0, commands = 0;
            lle_safety_get_event_stats(&events_fired, &dir_changes, &commands);
            printf("  Total events fired: %llu\n",
                   (unsigned long long)events_fired);
            printf("  Directory changes: %llu\n",
                   (unsigned long long)dir_changes);
            printf("  Commands executed: %llu\n", (unsigned long long)commands);

            return 0;

        } else if (strcmp(lle_cmd, "reset") == 0) {
            /* LLE reset commands (Spec 26: Three-tier reset hierarchy)
             * - reset        : Hard reset (destroy/recreate editor)
             * - reset --soft : Soft reset (abort current line)
             * - reset --terminal : Nuclear reset (hard + terminal reset)
             */
            if (!lle_is_active()) {
                fprintf(stderr, "display lle reset: LLE shell integration not "
                                "initialized\n");
                return 1;
            }

            /* Check for options */
            if (argc >= 4) {
                const char *opt = argv[3];
                if (strcmp(opt, "--soft") == 0) {
                    /* Soft reset: abort current line */
                    lle_soft_reset();
                    printf("LLE soft reset complete (line aborted)\n");
                    return 0;
                } else if (strcmp(opt, "--terminal") == 0) {
                    /* Nuclear reset: hard reset + terminal reset */
                    printf("Performing LLE nuclear reset...\n");
                    lle_nuclear_reset();
                    printf("LLE nuclear reset complete (editor recreated, "
                           "terminal reset)\n");
                    return 0;
                } else {
                    fprintf(stderr, "display lle reset: Unknown option '%s'\n",
                            opt);
                    fprintf(stderr, "Options: --soft, --terminal\n");
                    return 1;
                }
            }

            /* Default: Hard reset (destroy and recreate editor) */
            printf("Performing LLE hard reset...\n");
            lle_hard_reset();
            printf("LLE hard reset complete (editor recreated)\n");
            return 0;

        } else if (strcmp(lle_cmd, "theme") == 0) {
            /* LLE prompt theme control */
            if (!g_lle_integration || !g_lle_integration->prompt_composer) {
                fprintf(
                    stderr,
                    "display lle theme: LLE prompt system not initialized\n");
                fprintf(stderr, "Run 'display lle enable' first\n");
                return 1;
            }

            lle_theme_registry_t *themes =
                g_lle_integration->prompt_composer->themes;
            if (!themes) {
                fprintf(stderr,
                        "display lle theme: Theme registry not available\n");
                return 1;
            }

            /* No subcommand - show current theme and usage */
            if (argc < 4) {
                const lle_theme_t *active =
                    lle_theme_registry_get_active(themes);
                printf("LLE Prompt Theme\n");
                printf("  Current: %s\n", active ? active->name : "(none)");
                if (active && active->description[0]) {
                    printf("  Description: %s\n", active->description);
                }
                printf("\nUsage:\n");
                printf("  display lle theme list             - List available "
                       "themes\n");
                printf("  display lle theme set <name>       - Set active "
                       "theme\n");
                printf("  display lle theme reload           - Reload themes "
                       "from files\n");
                printf("  display lle theme export <name>    - Export theme to "
                       "stdout\n");
                printf("  display lle theme export <name> <file> - Export "
                       "theme to file\n");
                return 0;
            }

            const char *theme_subcmd = argv[3];

            if (strcmp(theme_subcmd, "list") == 0) {
                /* List all available themes */
                printf("Available LLE Prompt Themes:\n\n");
                const lle_theme_t *active =
                    lle_theme_registry_get_active(themes);

                for (size_t i = 0; i < themes->count; i++) {
                    const lle_theme_t *t = themes->themes[i];
                    if (t) {
                        const char *marker =
                            (active && strcmp(active->name, t->name) == 0)
                                ? "*"
                                : " ";
                        printf("  %s %-12s - %s\n", marker, t->name,
                               t->description[0] ? t->description
                                                 : "(no description)");
                    }
                }
                printf("\n  * = currently active\n");
                printf(
                    "\nUse 'display lle theme set <name>' to change theme\n");
                return 0;

            } else if (strcmp(theme_subcmd, "set") == 0) {
                /* Set active theme */
                if (argc < 5) {
                    fprintf(stderr,
                            "display lle theme set: Missing theme name\n");
                    fprintf(stderr, "Usage: display lle theme set <name>\n");
                    fprintf(stderr, "Use 'display lle theme list' to see "
                                    "available themes\n");
                    return 1;
                }

                const char *theme_name = argv[4];
                /* Use lle_composer_set_theme to properly clear cached templates
                 */
                lle_result_t result = lle_composer_set_theme(
                    g_lle_integration->prompt_composer, theme_name);

                if (result == LLE_SUCCESS) {
                    printf("LLE theme set to '%s'\n", theme_name);
                    return 0;
                } else if (result == LLE_ERROR_NOT_FOUND) {
                    fprintf(stderr,
                            "display lle theme set: Theme '%s' not found\n",
                            theme_name);
                    fprintf(stderr, "Use 'display lle theme list' to see "
                                    "available themes\n");
                    return 1;
                } else {
                    fprintf(stderr,
                            "display lle theme set: Failed to set theme (error "
                            "%d)\n",
                            result);
                    return 1;
                }

            } else if (strcmp(theme_subcmd, "reload") == 0) {
                /* Reload user themes from files */
                printf("Reloading themes from files...\n");
                size_t loaded = lle_theme_reload_user_themes(themes);
                printf("Loaded %zu new theme(s)\n", loaded);

                /* Show theme directories */
                char user_dir[LLE_THEME_PATH_MAX];
                if (lle_theme_get_user_dir(user_dir, sizeof(user_dir)) ==
                    LLE_SUCCESS) {
                    printf("User theme directory: %s\n", user_dir);
                }
                printf("System theme directory: %s\n", LLE_THEME_SYSTEM_DIR);
                return 0;

            } else if (strcmp(theme_subcmd, "export") == 0) {
                /* Export theme to TOML format */
                if (argc < 5) {
                    fprintf(stderr,
                            "display lle theme export: Missing theme name\n");
                    fprintf(stderr,
                            "Usage: display lle theme export <name> [file]\n");
                    return 1;
                }

                const char *theme_name = argv[4];
                const lle_theme_t *theme =
                    lle_theme_registry_find(themes, theme_name);
                if (!theme) {
                    fprintf(stderr,
                            "display lle theme export: Theme '%s' not found\n",
                            theme_name);
                    fprintf(stderr, "Use 'display lle theme list' to see "
                                    "available themes\n");
                    return 1;
                }

                if (argc >= 6) {
                    /* Export to file */
                    const char *filepath = argv[5];
                    lle_result_t result =
                        lle_theme_export_to_file(theme, filepath);
                    if (result == LLE_SUCCESS) {
                        printf("Theme '%s' exported to '%s'\n", theme_name,
                               filepath);
                        return 0;
                    } else {
                        fprintf(stderr,
                                "display lle theme export: Failed to write "
                                "file '%s'\n",
                                filepath);
                        return 1;
                    }
                } else {
                    /* Export to stdout */
                    char *buffer = malloc(LLE_THEME_FILE_MAX_SIZE);
                    if (!buffer) {
                        fprintf(stderr,
                                "display lle theme export: Out of memory\n");
                        return 1;
                    }
                    size_t len = lle_theme_export_to_toml(
                        theme, buffer, LLE_THEME_FILE_MAX_SIZE);
                    if (len > 0) {
                        printf("%s", buffer);
                    }
                    free(buffer);
                    return 0;
                }

            } else {
                fprintf(stderr, "display lle theme: Unknown subcommand '%s'\n",
                        theme_subcmd);
                fprintf(stderr,
                        "Usage: display lle theme [list|set|reload|export]\n");
                return 1;
            }

        } else if (strcmp(lle_cmd, "completions") == 0) {
            /* LLE custom completion source management */
            const char *comp_subcmd = (argc >= 4) ? argv[3] : "list";

            if (strcmp(comp_subcmd, "list") == 0) {
                /* List all completion sources */
                printf("LLE Completion Sources\n");
                printf("======================\n\n");

                /* Built-in sources */
                printf("Built-in Sources:\n");
                size_t total = lle_completion_get_source_count();
                for (size_t i = 0; i < total; i++) {
                    if (!lle_completion_source_is_custom(i)) {
                        const char *name = lle_completion_get_source_name(i);
                        printf("  - %s\n", name ? name : "(unknown)");
                    }
                }

                /* Custom sources */
                size_t custom_count = lle_completion_get_custom_source_count();
                if (custom_count > 0) {
                    printf("\nCustom Sources:\n");
                    for (size_t i = 0; i < custom_count; i++) {
                        const char *name =
                            lle_completion_get_custom_source_name(i);
                        const char *desc =
                            lle_completion_get_custom_source_description(i);
                        if (desc) {
                            printf("  - %s: %s\n", name ? name : "(unknown)",
                                   desc);
                        } else {
                            printf("  - %s\n", name ? name : "(unknown)");
                        }
                    }
                } else {
                    printf("\nNo custom sources registered.\n");
                }

                /* Config file info */
                const lle_completion_config_t *cfg =
                    lle_completion_get_config();
                if (cfg && cfg->config_path) {
                    printf("\nConfig file: %s\n", cfg->config_path);
                    printf("Config sources: %zu\n", cfg->source_count);
                } else {
                    printf("\nNo config file loaded.\n");
                    printf("Create ~/.config/lush/completions.toml to define "
                           "custom sources.\n");
                }

                return 0;

            } else if (strcmp(comp_subcmd, "reload") == 0) {
                /* Reload completion config */
                printf("Reloading completion config...\n");
                lle_result_t result = lle_completion_reload_config();
                if (result == LLE_SUCCESS) {
                    const lle_completion_config_t *cfg =
                        lle_completion_get_config();
                    if (cfg) {
                        printf("Loaded %zu custom source(s)\n",
                               cfg->source_count);
                    } else {
                        printf("Config reloaded (no sources defined)\n");
                    }
                    return 0;
                } else {
                    fprintf(stderr, "Failed to reload config (error %d)\n",
                            result);
                    return 1;
                }

            } else if (strcmp(comp_subcmd, "help") == 0 ||
                       strcmp(comp_subcmd, "--help") == 0) {
                printf("LLE Completion Source Commands\n");
                printf("==============================\n\n");
                printf("Usage: display lle completions [command]\n\n");
                printf("Commands:\n");
                printf("  list    - Show all completion sources (default)\n");
                printf("  reload  - Reload custom sources from config file\n");
                printf("  help    - Show this help message\n");
                printf("\nConfig File:\n");
                printf("  ~/.config/lush/completions.toml\n");
                printf("\nExample config:\n");
                printf("  [sources.git-branches]\n");
                printf("  description = \"Git branch names\"\n");
                printf("  applies_to = [\"git checkout\", \"git merge\"]\n");
                printf("  argument = 2\n");
                printf("  command = \"git branch --list 2>/dev/null | sed "
                       "'s/^[* ]*//'\"\n");
                printf("  cache_seconds = 5\n");
                return 0;

            } else {
                fprintf(stderr,
                        "display lle completions: Unknown subcommand '%s'\n",
                        comp_subcmd);
                fprintf(stderr,
                        "Usage: display lle completions [list|reload|help]\n");
                return 1;
            }

        } else {
            fprintf(stderr, "display lle: Unknown command '%s'\n", lle_cmd);
            fprintf(stderr,
                    "display lle: Use 'display lle' for usage information\n");
            return 1;
        }

    } else if (strcmp(subcmd, "help") == 0) {
        // Show help
        printf("Display Integration System\n");
        printf(
            "\nThe display integration system provides coordinated display\n");
        printf("management using the revolutionary layered display "
               "architecture.\n");
        printf("It enables universal prompt compatibility, real-time syntax\n");
        printf("highlighting, and intelligent layer combination with "
               "enterprise-\n");
        printf("grade performance optimization.\n");
        printf("\nCommands:\n");
        printf("  display status           - Show system status and health\n");

        printf("  display config           - Show detailed configuration\n");
        printf("  display stats            - Show usage statistics\n");
        printf("  display diagnostics      - Show system diagnostics\n");
        printf(
            "  display performance      - Performance monitoring commands\n");
        printf("  display test             - Test layered display with actual "
               "content\n");
        printf("  display help             - Show this help message\n");
        printf("\nConfiguration:\n");
        printf("  Environment variables can be used to control behavior:\n");
        printf(
            "  - LUSH_LAYERED_DISPLAY=1|0     Enable/disable at startup\n");
        printf("  - LUSH_DISPLAY_DEBUG=1|0       Enable debug output\n");
        printf("  - LUSH_DISPLAY_OPTIMIZATION=0-4 Set optimization level\n");
        printf("\nOptimization Levels:\n");
        printf("  0 - Disabled (no optimization)\n");
        printf("  1 - Basic (basic caching only)\n");
        printf("  2 - Standard (default optimization)\n");
        printf("  3 - Aggressive (aggressive optimization)\n");
        printf("  4 - Maximum (maximum performance mode)\n");
        printf("\nFor more information, see the Week 8 implementation "
               "documentation.\n");
        return 0;

    } else {
        fprintf(stderr, "display: Unknown command '%s'\n", subcmd);
        fprintf(stderr, "display: Use 'display help' for usage information\n");
        return 1;
    }
}

/**
 * @brief Execute command bypassing shell functions and builtins
 *
 * POSIX command builtin. With no options, executes the specified command
 * bypassing any shell functions, aliases, or builtins of the same name.
 *
 * Options:
 *   -v  Print command path (like 'which')
 *   -V  Print verbose command description (like 'type')
 *   -p  Use default PATH for command search
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Command exit status, or 127 if command not found
 */
int bin_command(int argc, char **argv) {
    bool opt_v = false;      /* -v: print path only */
    bool opt_V = false;      /* -V: verbose description */
    bool opt_p = false;      /* -p: use default PATH */
    int cmd_start = 1;

    /* Parse options */
    for (int i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            opt_v = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-V") == 0) {
            opt_V = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            opt_p = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-pv") == 0 ||
                   strcmp(argv[i], "-vp") == 0) {
            opt_p = true;
            opt_v = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-pV") == 0 ||
                   strcmp(argv[i], "-Vp") == 0) {
            opt_p = true;
            opt_V = true;
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "--") == 0) {
            cmd_start = i + 1;
            break;
        } else {
            /* Unknown option - might be command name starting with dash */
            break;
        }
    }

    /* No command specified */
    if (cmd_start >= argc) {
        if (opt_v || opt_V) {
            /* -v or -V with no arguments is valid, just do nothing */
            return 0;
        }
        /* command with no arguments does nothing */
        return 0;
    }

    const char *cmd_name = argv[cmd_start];

    /* Handle -v option: print command path */
    if (opt_v) {
        int result = 0;
        for (int i = cmd_start; i < argc; i++) {
            const char *name = argv[i];

            /* Check for builtin (not bypassed by -v, just reported) */
            if (is_builtin(name)) {
                printf("%s\n", name);
                continue;
            }

            /* Check for alias - lookup_alias returns pointer into hash table */
            const char *alias_value = lookup_alias(name);
            if (alias_value) {
                printf("alias %s='%s'\n", name, alias_value);
                continue;
            }

            /* Search in PATH */
            char *path = find_command_in_path(name);
            if (path) {
                printf("%s\n", path);
                free(path);
            } else {
                /* Not found - silent for -v, but return failure */
                result = 1;
            }
        }
        return result;
    }

    /* Handle -V option: verbose description */
    if (opt_V) {
        int result = 0;
        for (int i = cmd_start; i < argc; i++) {
            const char *name = argv[i];

            /* Check for builtin */
            if (is_builtin(name)) {
                printf("%s is a shell builtin\n", name);
                continue;
            }

            /* Check for alias - lookup_alias returns pointer into hash table */
            const char *alias_value = lookup_alias(name);
            if (alias_value) {
                printf("%s is aliased to '%s'\n", name, alias_value);
                continue;
            }

            /* Search in PATH */
            char *path = find_command_in_path(name);
            if (path) {
                printf("%s is %s\n", name, path);
                free(path);
            } else {
                fprintf(stderr, "%s: not found\n", name);
                result = 1;
            }
        }
        return result;
    }

    /* Execute the command, bypassing builtins/aliases/functions */

    /* Find command in PATH */
    char *cmd_path = NULL;

    if (opt_p) {
        /* Use default POSIX PATH */
        const char *saved_path = getenv("PATH");
        char *saved_path_copy = saved_path ? strdup(saved_path) : NULL;

        /* Set default POSIX PATH */
        setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);

        cmd_path = find_command_in_path(cmd_name);

        /* Restore original PATH */
        if (saved_path_copy) {
            setenv("PATH", saved_path_copy, 1);
            free(saved_path_copy);
        } else {
            unsetenv("PATH");
        }
    } else {
        /* If command contains a slash, use it directly */
        if (strchr(cmd_name, '/')) {
            if (access(cmd_name, X_OK) == 0) {
                cmd_path = strdup(cmd_name);
            }
        } else {
            cmd_path = find_command_in_path(cmd_name);
        }
    }

    if (!cmd_path) {
        fprintf(stderr, "command: %s: not found\n", cmd_name);
        return 127;
    }

    /* Fork and execute */
    pid_t pid = fork();

    if (pid < 0) {
        /* Fork failed */
        error_return("command: fork");
        free(cmd_path);
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        /* Reset signal handlers to default */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        /* Build argv for execv - use original argv from cmd_start */
        execv(cmd_path, &argv[cmd_start]);

        /* execv failed */
        fprintf(stderr, "command: %s: %s\n", cmd_path, strerror(errno));
        _exit(126);
    }

    /* Parent process - wait for child */
    free(cmd_path);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

/* ============================================================================
 * Directory Stack Builtins
 * ============================================================================ */

/**
 * @brief Push directory onto stack and change to it
 *
 * Usage:
 *   pushd [dir]  - Push current dir, cd to dir
 *   pushd +N     - Rotate stack so Nth entry is at top, cd there
 *   pushd -N     - Rotate stack so Nth from bottom is at top, cd there
 *   pushd        - Exchange top two stack entries
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_pushd(int argc, char **argv) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        error_return("pushd: getcwd");
        return 1;
    }

    if (argc == 1) {
        // pushd with no args: exchange top two entries
        if (dirstack_size() < 1) {
            fprintf(stderr, "pushd: no other directory\n");
            free(cwd);
            return 1;
        }
        
        const char *top = dirstack_peek(0);
        if (!top) {
            fprintf(stderr, "pushd: directory stack empty\n");
            free(cwd);
            return 1;
        }
        
        // Save current directory, pop top, push current, cd to old top
        char *old_top = dirstack_pop();
        dirstack_push(cwd);
        
        if (chdir(old_top) < 0) {
            error_return("pushd");
            // Restore stack state
            dirstack_pop();
            dirstack_push(old_top);
            free(old_top);
            free(cwd);
            return 1;
        }
        
        // Update PWD
        symtable_set_global("OLDPWD", cwd);
        char *new_cwd = getcwd(NULL, 0);
        if (new_cwd) {
            symtable_set_global("PWD", new_cwd);
            free(new_cwd);
        }
        
        free(old_top);
        free(cwd);
        dirstack_print(false, false);
        return 0;
    }

    char *arg = argv[1];
    
    // Check for +N or -N rotation
    if (arg[0] == '+' || arg[0] == '-') {
        char *endptr;
        long n = strtol(arg + 1, &endptr, 10);
        if (*endptr == '\0' && n >= 0) {
            int idx = (arg[0] == '+') ? (int)n : -(int)n - 1;
            
            // Need to account for cwd being index 0
            if (idx == 0) {
                // +0 means current dir, nothing to do
                free(cwd);
                dirstack_print(false, false);
                return 0;
            }
            
            // Adjust for the fact that cwd is position 0
            int stack_idx = idx - 1;
            
            const char *target = dirstack_peek(stack_idx);
            if (!target) {
                fprintf(stderr, "pushd: %s: directory stack index out of range\n", arg);
                free(cwd);
                return 1;
            }
            
            // Rotate stack and cd
            if (dirstack_rotate(stack_idx) < 0) {
                fprintf(stderr, "pushd: %s: rotation failed\n", arg);
                free(cwd);
                return 1;
            }
            
            target = dirstack_peek(0);
            if (chdir(target) < 0) {
                error_return("pushd");
                free(cwd);
                return 1;
            }
            
            symtable_set_global("OLDPWD", cwd);
            char *new_cwd = getcwd(NULL, 0);
            if (new_cwd) {
                symtable_set_global("PWD", new_cwd);
                free(new_cwd);
            }
            
            free(cwd);
            dirstack_print(false, false);
            return 0;
        }
    }
    
    // Push current directory and cd to new one
    if (chdir(arg) < 0) {
        error_return("pushd");
        free(cwd);
        return 1;
    }
    
    dirstack_push(cwd);
    
    symtable_set_global("OLDPWD", cwd);
    char *new_cwd = getcwd(NULL, 0);
    if (new_cwd) {
        symtable_set_global("PWD", new_cwd);
        free(new_cwd);
    }
    
    free(cwd);
    dirstack_print(false, false);
    return 0;
}

/**
 * @brief Pop directory from stack and change to it
 *
 * Usage:
 *   popd         - Pop top entry and cd there
 *   popd +N      - Remove Nth entry from top (0 = current dir)
 *   popd -N      - Remove Nth entry from bottom
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_popd(int argc, char **argv) {
    if (dirstack_size() < 1) {
        fprintf(stderr, "popd: directory stack empty\n");
        return 1;
    }

    if (argc == 1) {
        // popd with no args: pop top and cd there
        char *dir = dirstack_pop();
        if (!dir) {
            fprintf(stderr, "popd: directory stack empty\n");
            return 1;
        }
        
        char *cwd = getcwd(NULL, 0);
        
        if (chdir(dir) < 0) {
            error_return("popd");
            // Put it back
            dirstack_push(dir);
            free(dir);
            free(cwd);
            return 1;
        }
        
        if (cwd) {
            symtable_set_global("OLDPWD", cwd);
            free(cwd);
        }
        
        char *new_cwd = getcwd(NULL, 0);
        if (new_cwd) {
            symtable_set_global("PWD", new_cwd);
            free(new_cwd);
        }
        
        free(dir);
        dirstack_print(false, false);
        return 0;
    }

    char *arg = argv[1];
    
    // Check for +N or -N removal
    if (arg[0] == '+' || arg[0] == '-') {
        char *endptr;
        long n = strtol(arg + 1, &endptr, 10);
        if (*endptr == '\0' && n >= 0) {
            int idx;
            if (arg[0] == '+') {
                idx = (int)n;
            } else {
                idx = dirstack_size() - (int)n;
            }
            
            // +0 means current directory, can't remove that
            if (idx == 0) {
                fprintf(stderr, "popd: can't remove current directory\n");
                return 1;
            }
            
            // Adjust for cwd being position 0
            int stack_idx = idx - 1;
            
            if (dirstack_remove(stack_idx) < 0) {
                fprintf(stderr, "popd: %s: directory stack index out of range\n", arg);
                return 1;
            }
            
            dirstack_print(false, false);
            return 0;
        }
    }
    
    fprintf(stderr, "popd: %s: invalid argument\n", arg);
    return 1;
}

/**
 * @brief Display directory stack
 *
 * Usage:
 *   dirs         - Display stack on one line
 *   dirs -p      - Print one entry per line
 *   dirs -v      - Print with stack index numbers
 *   dirs -c      - Clear the stack
 *   dirs -l      - Use full paths (no ~ substitution)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_dirs(int argc, char **argv) {
    bool one_per_line = false;
    bool show_index = false;
    bool clear_stack = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            one_per_line = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            show_index = true;
            one_per_line = true;
        } else if (strcmp(argv[i], "-c") == 0) {
            clear_stack = true;
        } else if (strcmp(argv[i], "-l") == 0) {
            // Full paths (currently default, ~ substitution not implemented)
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "dirs: %s: invalid option\n", argv[i]);
            return 1;
        }
    }
    
    if (clear_stack) {
        dirstack_clear();
        return 0;
    }
    
    dirstack_print(one_per_line, show_index);
    return 0;
}

/* ============================================================================
 * Environment Builtin
 * ============================================================================ */

/**
 * @brief Run command with modified environment
 *
 * Usage:
 *   env                    - Print all environment variables
 *   env NAME=VALUE... cmd  - Run cmd with additional env vars
 *   env -i [NAME=VALUE...] cmd - Run cmd with empty environment
 *   env -u NAME cmd        - Run cmd with NAME unset
 *   env -0                 - Use NUL instead of newline (for printing)
 *   printenv [NAME...]     - Print environment variable(s)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit status of command, or 0 for printing
 */
int bin_env(int argc, char **argv) {
    bool ignore_env = false;      /* -i: start with empty environment */
    bool null_terminator = false; /* -0: NUL instead of newline */
    char **unset_vars = NULL;     /* -u: variables to unset */
    int unset_count = 0;
    int unset_capacity = 0;
    
    /* Check if invoked as printenv */
    bool is_printenv = (strcmp(argv[0], "printenv") == 0);
    
    int i = 1;
    
    /* Parse options */
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else if (strcmp(argv[i], "-i") == 0) {
            ignore_env = true;
            i++;
        } else if (strcmp(argv[i], "-0") == 0) {
            null_terminator = true;
            i++;
        } else if (strcmp(argv[i], "-u") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "env: -u requires an argument\n");
                free(unset_vars);
                return 1;
            }
            /* Add to unset list */
            if (unset_count >= unset_capacity) {
                unset_capacity = unset_capacity ? unset_capacity * 2 : 4;
                char **new_unset = realloc(unset_vars, 
                                           unset_capacity * sizeof(char *));
                if (!new_unset) {
                    free(unset_vars);
                    return 1;
                }
                unset_vars = new_unset;
            }
            unset_vars[unset_count++] = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]\n");
            printf("Run COMMAND with modified environment.\n\n");
            printf("Options:\n");
            printf("  -i          Start with empty environment\n");
            printf("  -u NAME     Remove NAME from environment\n");
            printf("  -0          Use NUL instead of newline for output\n");
            printf("  --help      Display this help\n\n");
            printf("If no COMMAND, print the environment.\n");
            free(unset_vars);
            return 0;
        } else {
            fprintf(stderr, "env: invalid option: %s\n", argv[i]);
            free(unset_vars);
            return 125;
        }
    }
    
    /* Collect NAME=VALUE assignments */
    char **env_assignments = NULL;
    int env_count = 0;
    int env_capacity = 0;
    
    while (i < argc && strchr(argv[i], '=') != NULL) {
        if (env_count >= env_capacity) {
            env_capacity = env_capacity ? env_capacity * 2 : 8;
            char **new_env = realloc(env_assignments, 
                                     env_capacity * sizeof(char *));
            if (!new_env) {
                free(unset_vars);
                free(env_assignments);
                return 1;
            }
            env_assignments = new_env;
        }
        env_assignments[env_count++] = argv[i];
        i++;
    }
    
    /* printenv: just print variable values */
    if (is_printenv) {
        char delim = null_terminator ? '\0' : '\n';
        if (i >= argc) {
            /* Print all environment variables */
            extern char **environ;
            for (char **env = environ; *env; env++) {
                printf("%s%c", *env, delim);
            }
        } else {
            /* Print specific variables */
            int result = 0;
            for (; i < argc; i++) {
                const char *val = getenv(argv[i]);
                if (val) {
                    printf("%s%c", val, delim);
                } else {
                    result = 1;  /* Variable not found */
                }
            }
            free(unset_vars);
            free(env_assignments);
            return result;
        }
        free(unset_vars);
        free(env_assignments);
        return 0;
    }
    
    /* No command: print environment */
    if (i >= argc) {
        char delim = null_terminator ? '\0' : '\n';
        if (ignore_env) {
            /* Only print explicit assignments */
            for (int j = 0; j < env_count; j++) {
                printf("%s%c", env_assignments[j], delim);
            }
        } else {
            extern char **environ;
            /* Print current environment (minus unset vars, plus assignments) */
            for (char **env = environ; *env; env++) {
                /* Check if should be unset */
                bool skip = false;
                for (int u = 0; u < unset_count; u++) {
                    size_t ulen = strlen(unset_vars[u]);
                    if (strncmp(*env, unset_vars[u], ulen) == 0 && 
                        (*env)[ulen] == '=') {
                        skip = true;
                        break;
                    }
                }
                /* Check if overridden by assignment */
                for (int j = 0; j < env_count; j++) {
                    char *eq = strchr(env_assignments[j], '=');
                    size_t nlen = eq - env_assignments[j];
                    if (strncmp(*env, env_assignments[j], nlen) == 0 &&
                        (*env)[nlen] == '=') {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    printf("%s%c", *env, delim);
                }
            }
            /* Print assignments */
            for (int j = 0; j < env_count; j++) {
                printf("%s%c", env_assignments[j], delim);
            }
        }
        free(unset_vars);
        free(env_assignments);
        return 0;
    }
    
    /* Run command with modified environment */
    pid_t pid = fork();
    if (pid < 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_FORK_FAILED, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "env: fork: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(unset_vars);
        free(env_assignments);
        return 126;
    }
    
    if (pid == 0) {
        /* Child process */
        
        if (ignore_env) {
            /* Clear entire environment - use portable approach
             * clearenv() is a GNU extension not available on macOS/BSD
             * Setting environ to NULL or empty array is POSIX portable
             */
            extern char **environ;
            static char *empty_env[] = { NULL };
            environ = empty_env;
        } else {
            /* Unset specified variables */
            for (int u = 0; u < unset_count; u++) {
                unsetenv(unset_vars[u]);
            }
        }
        
        /* Apply assignments */
        for (int j = 0; j < env_count; j++) {
            char *eq = strchr(env_assignments[j], '=');
            if (eq) {
                *eq = '\0';
                setenv(env_assignments[j], eq + 1, 1);
                *eq = '=';  /* Restore for potential re-use */
            }
        }
        
        /* Execute command */
        execvp(argv[i], &argv[i]);
        
        /* exec failed */
        int exit_code = (errno == ENOENT) ? 127 : 126;
        fprintf(stderr, "env: %s: %s\n", argv[i], strerror(errno));
        _exit(exit_code);
    }
    
    /* Parent: wait for child */
    free(unset_vars);
    free(env_assignments);
    
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "env: waitpid: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 126;
    }
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    
    return 126;
}

/**
 * @brief Analyze scripts for issues and portability (builtin command)
 *
 * Analyzes shell scripts for syntax errors, style issues, security
 * vulnerabilities, performance problems, and portability concerns.
 *
 * Usage: analyze [OPTIONS] <script>
 *        lint [OPTIONS] <script>
 *
 * Options:
 *   -t, --target=SHELL  Target shell for compatibility (posix, bash, zsh)
 *   -s, --strict        Treat warnings as errors
 *   -h, --help          Show help message
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 if no issues, 1 if warnings, 2 if errors
 */
int bin_analyze(int argc, char **argv) {
    bool strict_mode = false;
    const char *target_shell = NULL;
    const char *script_file = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS] <script>\n", argv[0]);
            printf("\nAnalyze shell scripts for issues and portability.\n");
            printf("\nOptions:\n");
            printf("  -t, --target=SHELL  Target shell (posix, bash, zsh)\n");
            printf("  -s, --strict        Treat warnings as errors\n");
            printf("  -h, --help          Show this help message\n");
            printf("\nCategories checked:\n");
            printf("  syntax       - Syntax errors and parsing issues\n");
            printf("  style        - Code style and formatting\n");
            printf("  performance  - Performance anti-patterns\n");
            printf("  security     - Security vulnerabilities\n");
            printf("  portability  - Shell compatibility issues\n");
            printf("\nExit codes:\n");
            printf("  0  No issues found\n");
            printf("  1  Warnings found\n");
            printf("  2  Errors found\n");
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                target_shell = argv[++i];
            } else {
                fprintf(stderr, "%s: -t requires an argument\n", argv[0]);
                return 1;
            }
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            target_shell = argv[i] + 9;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[i]);
            return 1;
        } else {
            script_file = argv[i];
        }
    }
    
    if (!script_file) {
        fprintf(stderr, "%s: missing script file argument\n", argv[0]);
        fprintf(stderr, "Usage: %s [OPTIONS] <script>\n", argv[0]);
        return 1;
    }
    
    /* Set target shell if specified (stored as string for flexibility) */
    if (target_shell) {
        compat_set_target(target_shell);
    }
    
    /* Set strict mode if requested */
    if (strict_mode) {
        compat_set_strict(true);
    }
    
    /* Initialize debug context for analysis */
    debug_context_t *ctx = debug_init();
    if (!ctx) {
        fprintf(stderr, "%s: failed to initialize analysis context\n", argv[0]);
        return 1;
    }
    
    /* Enable context so debug_printf works for output */
    debug_enable(ctx, true);
    
    /* Run analysis (includes report output) */
    debug_analyze_script(ctx, script_file);
    
    /* Determine exit code based on issues found */
    int exit_status = 0;
    if (ctx->issue_count > 0) {
        analysis_issue_t *issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->severity, "error") == 0) {
                exit_status = 2;
                break;
            } else if (strcmp(issue->severity, "warning") == 0 && exit_status < 1) {
                exit_status = 1;
            }
            issue = issue->next;
        }
        
        /* In strict mode, warnings become errors */
        if (strict_mode && exit_status == 1) {
            exit_status = 2;
        }
    }
    
    /* Cleanup */
    debug_cleanup(ctx);
    
    /* Reset strict mode */
    if (strict_mode) {
        compat_set_strict(false);
    }
    
    return exit_status;
}

/**
 * @brief Lint builtin - actionable script linting with optional auto-fix
 *
 * Unlike analyze, lint only shows warnings and errors (not info items)
 * and can optionally apply automatic fixes.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 if no issues, 1 if unfixed warnings remain, 2 if errors remain
 */
int bin_lint(int argc, char **argv) {
    bool strict_mode = false;
    bool fix_mode = false;
    bool fix_interactive = false;
    bool unsafe_fixes = false;
    bool dry_run = false;
    bool show_diff = false;
    bool create_backup = true;
    const char *target_shell = NULL;
    const char *script_file = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS] <script>\n", argv[0]);
            printf("\nLint shell scripts for actionable issues.\n");
            printf("\nOptions:\n");
            printf("  -t, --target=SHELL  Target shell (posix, bash, zsh)\n");
            printf("  -s, --strict        Treat warnings as errors\n");
            printf("  --fix               Apply safe automatic fixes\n");
            printf("  --fix-interactive   Interactively approve each fix\n");
            printf("  --unsafe-fixes      Also apply unsafe fixes (implies --fix)\n");
            printf("  --dry-run           Preview fixes without applying\n");
            printf("  --diff              Show unified diff of changes\n");
            printf("  --no-backup         Don't create .bak backup when fixing\n");
            printf("  -h, --help          Show this help message\n");
            printf("\nFix safety levels:\n");
            printf("  safe   - Applied with --fix (e.g., source -> .)\n");
            printf("  unsafe - Requires --unsafe-fixes (e.g., [[ ]] -> [ ])\n");
            printf("  manual - Cannot be auto-fixed, shown as suggestions\n");
            printf("\nExit codes:\n");
            printf("  0  No issues or all fixed\n");
            printf("  1  Unfixed warnings remain\n");
            printf("  2  Unfixed errors remain\n");
            printf("  3  Fix application failed\n");
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
        } else if (strcmp(argv[i], "--fix") == 0) {
            fix_mode = true;
        } else if (strcmp(argv[i], "--fix-interactive") == 0) {
            fix_interactive = true;
        } else if (strcmp(argv[i], "--unsafe-fixes") == 0) {
            fix_mode = true;
            unsafe_fixes = true;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--diff") == 0) {
            show_diff = true;
        } else if (strcmp(argv[i], "--no-backup") == 0) {
            create_backup = false;
        } else if (strcmp(argv[i], "--backup") == 0) {
            create_backup = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                target_shell = argv[++i];
            } else {
                fprintf(stderr, "%s: -t requires an argument\n", argv[0]);
                return 1;
            }
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            target_shell = argv[i] + 9;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[i]);
            return 1;
        } else {
            script_file = argv[i];
        }
    }
    
    if (!script_file) {
        fprintf(stderr, "%s: missing script file argument\n", argv[0]);
        fprintf(stderr, "Usage: %s [OPTIONS] <script>\n", argv[0]);
        return 1;
    }
    
    /* Set target shell if specified (stored as string for flexibility) */
    if (target_shell) {
        compat_set_target(target_shell);
    }
    
    /* Set strict mode if requested */
    if (strict_mode) {
        compat_set_strict(true);
    }
    
    /* Initialize debug context for analysis */
    debug_context_t *ctx = debug_init();
    if (!ctx) {
        fprintf(stderr, "%s: failed to initialize lint context\n", argv[0]);
        return 1;
    }
    
    /* Enable context so debug_printf works for output */
    debug_enable(ctx, true);
    
    /* Suppress unused variable warnings */
    (void)show_diff;

    int remaining;

    if (fix_interactive) {
        /* Interactive fix mode - run analysis first, then interactive fixer */
        remaining = debug_lint_script(ctx, script_file, false, false, false);

        if (remaining > 0) {
            /* Load script for interactive fixing */
            fixer_context_t fixer_ctx;
            if (fixer_init(&fixer_ctx) == FIXER_OK) {
                if (fixer_load_file(&fixer_ctx, script_file) == FIXER_OK) {
                    /* Get target shell */
                    shell_mode_t target = SHELL_MODE_POSIX;
                    const char *target_str = compat_get_target();
                    if (target_str) {
                        shell_mode_parse(target_str, &target);
                    }

                    /* Collect fixes */
                    size_t fixes_found = fixer_collect_fixes(&fixer_ctx, target);

                    if (fixes_found > 0) {
                        fixer_options_t opts = {
                            .include_unsafe = unsafe_fixes,
                            .dry_run = dry_run,
                            .create_backup = create_backup,
                            .verify_syntax = true,
                            .target = target,
                        };

                        int applied = fixer_run_interactive(&fixer_ctx, &opts,
                                                             script_file);
                        if (applied > 0) {
                            remaining -= applied;
                            if (remaining < 0) remaining = 0;
                        }
                    } else {
                        printf("No automatic fixes available.\n");
                    }
                }
                fixer_cleanup(&fixer_ctx);
            }
        }
    } else {
        /* Standard fix mode (automatic or none) */
        remaining = debug_lint_script(ctx, script_file, fix_mode,
                                       unsafe_fixes, dry_run);
    }
    
    /* Determine exit code */
    int exit_status = 0;
    if (remaining < 0) {
        exit_status = 3;  /* Fix application error */
    } else if (remaining > 0) {
        /* Check if we have errors or just warnings */
        analysis_issue_t *issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->severity, "error") == 0) {
                exit_status = 2;
                break;
            } else if (strcmp(issue->severity, "warning") == 0 && exit_status < 1) {
                exit_status = 1;
            }
            issue = issue->next;
        }
        
        /* In strict mode, warnings become errors */
        if (strict_mode && exit_status == 1) {
            exit_status = 2;
        }
    }
    
    /* Cleanup */
    debug_cleanup(ctx);
    
    /* Reset strict mode */
    if (strict_mode) {
        compat_set_strict(false);
    }
    
    return exit_status;
}
