/**
 * @file init.c
 * @brief Shell initialization and startup routines
 *
 * Handles shell initialization including:
 * - Command line argument parsing
 * - Environment variable import
 * - Signal handler setup
 * - Interactive/non-interactive mode detection
 * - Configuration and script execution
 * - Memory pool and display integration initialization
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "init.h"

#include "alias.h"
#include "autocorrect.h"
#include "builtins.h"
#include "compat.h"
#include "config.h"
#include "dirstack.h"
#include "errors.h"
#include "input.h"
#include "posix_history.h"
#include "shell_error.h"
#include "shell_mode.h"

#include "lle/completion/ssh_hosts.h"
#include "lush.h"
#include "signals.h"
#include "symtable.h"

#include "display_integration.h"
#include "lle/adaptive_terminal_integration.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"
#include "lle/widget_system.h"

extern lle_result_t lush_register_inspect_widget(lle_widget_registry_t *);
#include "lush_memory_pool.h"
#include "version.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#else
/// macOS doesn't have sys/sysmacros.h, major/minor are in sys/types.h
/// but we'll avoid using them for portability
#endif

extern char **environ;

/// POSIX-compliant shell type tracking
static int SHELL_TYPE;
static bool IS_LOGIN_SHELL = false;
static bool IS_INTERACTIVE_SHELL = false;
static bool IS_SESSION_LEADER = false;

static int parse_opts(int argc, char **argv);
static void usage(int err);

/**
 * @brief Initialize environment variables for login shells
 *
 * Ensures critical environment variables are set for login shells.
 * Only sets variables if they are not already present in the environment.
 *
 * Variables initialized:
 * - SHELL: Path to the shell executable
 * - USER: Current username (from getpwuid)
 * - LOGNAME: Current username (POSIX standard)
 * - HOME: User's home directory
 * - PATH: Default path if completely unset
 *
 * @param argv0 The shell's argv[0] for determining SHELL path
 */
static void init_login_environment(const char *argv0) {
    struct passwd *pw = getpwuid(getuid());

    /// Set SHELL if not already set
    if (!getenv("SHELL")) {
        /// Try to find our executable path
        char shell_path[PATH_MAX];
        bool found_path = false;

        if (argv0 && argv0[0] == '/') {
            /// Absolute path provided
            setenv("SHELL", argv0, 0);
            found_path = true;
        }
#ifdef __APPLE__
        if (!found_path) {
            /// macOS: use _NSGetExecutablePath
            uint32_t size = sizeof(shell_path);
            if (_NSGetExecutablePath(shell_path, &size) == 0) {
                /// Resolve symlinks to get canonical path
                char resolved[PATH_MAX];
                if (realpath(shell_path, resolved) != NULL) {
                    setenv("SHELL", resolved, 0);
                } else {
                    setenv("SHELL", shell_path, 0);
                }
                found_path = true;
            }
        }
#else
        if (!found_path && realpath("/proc/self/exe", shell_path) != NULL) {
            /// Linux: use /proc/self/exe
            setenv("SHELL", shell_path, 0);
            found_path = true;
        }
#endif
        if (!found_path && pw && pw->pw_shell && pw->pw_shell[0]) {
            /// Fall back to passwd entry
            setenv("SHELL", pw->pw_shell, 0);
            found_path = true;
        }
        if (!found_path) {
            /// Last resort: assume we're in PATH
            setenv("SHELL", "lush", 0);
        }
        /// Also set in symbol table
        const char *shell = getenv("SHELL");
        if (shell) {
            symtable_set_global("SHELL", shell);
            symtable_export_global("SHELL");
        }
    }

    /// Set USER if not already set
    if (!getenv("USER")) {
        if (pw && pw->pw_name) {
            setenv("USER", pw->pw_name, 0);
            symtable_set_global("USER", pw->pw_name);
            symtable_export_global("USER");
        }
    }

    /// Set LOGNAME if not already set (POSIX standard)
    if (!getenv("LOGNAME")) {
        if (pw && pw->pw_name) {
            setenv("LOGNAME", pw->pw_name, 0);
            symtable_set_global("LOGNAME", pw->pw_name);
            symtable_export_global("LOGNAME");
        }
    }

    /// Set HOME if not already set
    if (!getenv("HOME")) {
        if (pw && pw->pw_dir) {
            setenv("HOME", pw->pw_dir, 0);
            symtable_set_global("HOME", pw->pw_dir);
            symtable_export_global("HOME");
        }
    }

    /// Set PATH if completely unset (very rare, but handle it)
    if (!getenv("PATH")) {
        const char *default_path = "/usr/local/bin:/usr/bin:/bin";
        setenv("PATH", default_path, 0);
        symtable_set_global("PATH", default_path);
        symtable_export_global("PATH");
    }
}

/**
 * @brief Ensure safe bottom margin for interactive shell display
 *
 * Creates scroll space at the bottom of the terminal by moving the
 * cursor to the bottom and scrolling, then restoring cursor position.
 * Only executes once per shell session.
 */
static void ensure_bottom_margin(void) {
    static bool margin_created = false;
    if (margin_created) {
        return;
    }

    int rows = 0;
    lle_get_terminal_size(NULL, &rows);
    if (rows <= 0) {
        rows = 24;
    }

    /// Save cursor, move to bottom, scroll, restore
    char cmd[64];
    int len = snprintf(cmd, sizeof(cmd), "\x1b[s\x1b[%d;1H\n\x1b[u", rows);
    if (len > 0 && (size_t)len < sizeof(cmd)) {
        /// (void)! is the portable suppression idiom; plain (void)cast
        /// does not silence gcc -Wunused-result on functions decorated
        /// with warn_unused_result.
        (void)!write(STDOUT_FILENO, cmd, (size_t)len);
    }

    margin_created = true;
}

/**
 * @brief Resolve the initial shell mode at startup.
 *
 * Priority order:
 *   1. CLI flag (--posix, --bash, --zsh, --lush) -- most explicit.
 *   2. Shebang of the script being run (peeked early; consumed later
 *      by process_shebang during script execution).
 *   3. Default to SHELL_MODE_LUSH.
 *
 * Resolved before config_init() so the user's lushrc layers on top of
 * the right preset rather than being overwritten by a later mode
 * change.
 *
 * @param argc   argv count from main
 * @param argv   argv from main
 * @param optind Index of the first non-option argument (script file
 *               candidate), as returned by parse_opts.
 * @return Initial shell mode.
 */
static shell_mode_t detect_initial_mode(int argc, char **argv, size_t optind) {
    /// CLI flag: highest priority.
    if (shell_opts.cli_mode_override_set) {
        return (shell_mode_t)shell_opts.cli_mode_override;
    }

    /// Script file: peek at shebang. process_shebang() will fire later
    /// when the script is actually opened for execution; this peek is
    /// just for early mode resolution.
    if (optind < (size_t)argc && argv[optind] && argv[optind][0] != '\0' &&
        argv[optind][0] != '-') {
        FILE *f = fopen(argv[optind], "r");
        if (f) {
            char line[256];
            if (fgets(line, sizeof(line), f) != NULL &&
                strncmp(line, "#!", 2) == 0) {
                shell_mode_t detected;
                if (shell_mode_detect_from_shebang(line, &detected)) {
                    fclose(f);
                    return detected;
                }
            }
            fclose(f);
        }
    }

    /// Default.
    return SHELL_MODE_LUSH;
}

/**
 * @brief Process shebang line in script files
 *
 * Checks if the first line of a script file starts with #! and
 * consumes it if present. If no shebang is found, rewinds to
 * the beginning of the file.
 *
 * @param file File pointer to the script being executed
 */
static void process_shebang(FILE *file) {
    if (!file) {
        return;
    }

    /// Check if the first line starts with #!
    long pos = ftell(file);
    char line[256];

    if (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "#!", 2) == 0) {
            /// This is a shebang line - detect shell mode from it
            /// Examples:
            ///   #!/bin/bash      -> SHELL_MODE_BASH
            ///   #!/bin/zsh       -> SHELL_MODE_ZSH
            ///   #!/bin/sh        -> SHELL_MODE_POSIX
            ///   #!/usr/bin/env bash -> SHELL_MODE_BASH
            ///   #!/usr/bin/env lush -> SHELL_MODE_LUSH (default)
            shell_mode_t detected;
            if (shell_mode_detect_from_shebang(line, &detected)) {
                if (detected != SHELL_MODE_LUSH) {
                    /// Only switch if not already the default mode
                    shell_mode_set(detected);
                }
            }
            /// Shebang line consumed - continue with rest of script
            return;
        }
    }

    /// Not a shebang line, rewind to beginning
    fseek(file, pos, SEEK_SET);
}

/**
 * @brief Get the current shell type
 *
 * @return Shell type constant (SHELL_INTERACTIVE, SHELL_NON_INTERACTIVE, etc.)
 */
int shell_type(void) { return SHELL_TYPE; }

/**
 * @brief Check if shell is running in interactive mode
 *
 * @return true if interactive, false otherwise
 */
bool is_interactive_shell(void) { return IS_INTERACTIVE_SHELL; }

/**
 * @brief Check if shell is a login shell
 *
 * @return true if login shell, false otherwise
 */
bool is_login_shell(void) { return IS_LOGIN_SHELL; }

/**
 * @brief Check if this shell is the session leader
 *
 * A shell is a session leader if its PID equals its session ID.
 * This typically happens when the shell is started directly by
 * a login process or terminal emulator.
 *
 * @return true if session leader, false otherwise
 */
bool is_session_leader(void) { return IS_SESSION_LEADER; }

/**
 * @brief Cleanup wrapper for POSIX history (atexit compatible)
 *
 * Saves and destroys the global POSIX history manager.
 */
static void posix_history_cleanup(void) {
    if (global_posix_history) {
        posix_history_destroy(global_posix_history);
        global_posix_history = NULL;
    }
}

/**
 * @brief Initialize the shell
 *
 * Performs complete shell initialization including:
 * - Locale setup
 * - Signal handler installation
 * - Symbol table initialization
 * - Environment variable import
 * - Configuration loading
 * - Terminal capability detection
 * - Interactive feature setup (if applicable)
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param in Output parameter for input stream (stdin or script file)
 * @return 0 on success, non-zero on failure
 */
int init(int argc, char **argv, FILE **in) {
    struct stat st; /// stat buffer

    if (argv == NULL) {
        exit(EXIT_FAILURE);
    }

    /// Set all locales according to environment
    setlocale(LC_ALL, "");

    /// Setup signal handlers
    init_signal_handlers();

    /// Initialize symbol table
    init_symtable();

    /// Import environment variables into symtable
    char **env_ptr = environ;
    while (*env_ptr) {
        char *eq = strchr(*env_ptr, '=');
        if (eq) {
            /// Split name=value
            size_t name_len = eq - *env_ptr;
            char *name = malloc(name_len + 1);
            if (!name) {
                error_abort(
                    "Failed to allocate memory for environment variable name");
            }

            strncpy(name, *env_ptr, name_len);
            name[name_len] = '\0';

            /// Set variable and mark as exported
            symtable_set_global(name, eq + 1);
            symtable_export_global(name);

            free(name);
        } else {
            /// Environment variable without value (set but empty)
            symtable_set_global(*env_ptr, "");
            symtable_export_global(*env_ptr);
        }
        env_ptr++;
    }

    init_shell_opts();

    /// Initialize POSIX shell options with defaults
    init_posix_options();

    /// Parse command line options EARLY - needed for login/interactive
    /// detection
    size_t optind = parse_opts(argc, argv);

    /// Resolve initial shell mode BEFORE config_init so the user's
    /// lushrc layers on top of the right preset. Priority: CLI flag
    /// (--posix/--bash/--zsh/--lush) -> shebang of script argument ->
    /// default lush. The registry isn't initialized yet, so
    /// apply_mode_preset() only flips the feature matrix and the legacy
    /// posix_mode mirror; config_registry_apply_mode_defaults fires
    /// inside config_init() after section registration but before user
    /// config load.
    shell_mode_t initial_mode = detect_initial_mode(argc, argv, optind);
    apply_mode_preset(initial_mode);

    /// POSIX-compliant shell type determination - must happen before config
    /// scripts
    /// 1. Determine if this is a login shell
    IS_LOGIN_SHELL = (**argv == '-') || shell_opts.login_shell;

    /// 2. Check if we are the session leader (PID == SID)
    IS_SESSION_LEADER = (getsid(0) == getpid());

    /// 3. Preliminary interactive detection for startup scripts
    /// Full detection happens later after we know about script files
    bool preliminary_interactive =
        shell_opts.interactive ||
        (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO));

    /// Initialize configuration system
    config_init();

    /// Initialize critical environment variables for login shells
    /// This must happen before profile scripts which may depend on them
    if (IS_LOGIN_SHELL) {
        init_login_environment(argv[0]);
    }

    /// Execute login scripts for login shells
    if (IS_LOGIN_SHELL) {
        /// First source system-wide profiles (/etc/profile,
        /// /etc/profile.d/*.sh)
        config_execute_system_profile();
        /// Then source user profiles (~/.profile, ~/.lush_login)
        config_execute_login_scripts();
    }

    /// Execute startup scripts for interactive shells (preliminary check)
    /// Note: This uses a preliminary check; full interactive detection happens
    /// below
    if (preliminary_interactive && !shell_opts.command_mode) {
        config_execute_startup_scripts();
    }

    /// Initialize auto-correction system
    autocorrect_init();

    /// Initialize SSH host cache for completion
    if (ssh_hosts_init() != 0) {
        if (IS_INTERACTIVE_SHELL) {
            shell_error_t *err = shell_error_create(
                SHELL_ERR_SUBSYSTEM_INIT_FAILED, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "failed to initialize SSH host cache");
            if (err) {
                shell_error_set_suggestion(
                    err, "SSH host completion will fall back to typed input "
                         "for this session; check ~/.ssh permissions");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
        }
    }

    /// Initialize terminal capabilities via LLE adaptive detection
    lle_terminal_detection_result_t *detection = NULL;
    if (lle_detect_terminal_capabilities_optimized(&detection) != LLE_SUCCESS) {
        if (IS_INTERACTIVE_SHELL) {
            shell_error_t *err = shell_error_create(
                SHELL_ERR_SUBSYSTEM_INIT_FAILED, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "failed to detect terminal capabilities");
            if (err) {
                shell_error_set_suggestion(
                    err, "the line editor will fall back to a conservative "
                         "terminal profile; set TERM appropriately to enable "
                         "richer features");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
        }
    } else {
        /// Terminal capabilities successfully detected
        if (IS_INTERACTIVE_SHELL && detection->stdout_is_tty) {
            /// Create safe bottom margin for interactive shells
            ensure_bottom_margin();
        }
        /// Note: detection result is cached, no need to destroy here
    }

    /// Theme system removed - LLE prompt composer handles themes now

    /// Set up auto-correction configuration from config system
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

    /// Final interactive vs non-interactive determination
    /// (parse_opts and login/session detection already done above)
    bool has_script_file = (optind && argv[optind] && *argv[optind]);
    bool forced_interactive = shell_opts.interactive;
    bool stdin_is_terminal = isatty(STDIN_FILENO);

    /// Debug: Show TTY detection details
    const char *debug_env = getenv("LUSH_DEBUG");
    if (debug_env &&
        (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
        fprintf(stderr, "[INIT] TTY Detection: STDIN_FILENO=%d, isatty()=%s\n",
                STDIN_FILENO, stdin_is_terminal ? "true" : "false");

        /// Show what stdin is connected to
        struct stat st;
        if (fstat(STDIN_FILENO, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a regular file\n");
            } else if (S_ISDIR(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a directory\n");
            } else if (S_ISCHR(st.st_mode)) {
#ifdef __linux__
                fprintf(stderr,
                        "[INIT] stdin is a character device (dev %lu:%lu)\n",
                        (unsigned long)major(st.st_rdev),
                        (unsigned long)minor(st.st_rdev));
#else
                fprintf(stderr, "[INIT] stdin is a character device\n");
#endif
            } else if (S_ISBLK(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a block device\n");
            } else if (S_ISFIFO(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a FIFO/pipe\n");
            } else if (S_ISLNK(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a symbolic link\n");
            } else if (S_ISSOCK(st.st_mode)) {
                fprintf(stderr, "[INIT] stdin is a socket\n");
            } else {
                fprintf(stderr, "[INIT] stdin is unknown type (mode 0x%x)\n",
                        st.st_mode);
            }
        } else {
            fprintf(stderr, "[INIT] fstat() failed: %s\n", strerror(errno));
        }
    }

    if (shell_opts.command_mode) {
        /// -c command mode: always non-interactive
        IS_INTERACTIVE_SHELL = false;
        SHELL_TYPE = SHELL_NON_INTERACTIVE;
        *in = stdin; /// Not used in -c mode
    } else if (has_script_file) {
        /// Script file execution: always non-interactive
        IS_INTERACTIVE_SHELL = false;
        SHELL_TYPE = SHELL_NON_INTERACTIVE;

        /// Check that the script file is valid
        stat(argv[optind], &st);
        if (!(S_ISREG(st.st_mode))) {
            shell_error_t *err = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                SOURCE_LOC_UNKNOWN, "%s is not a regular file", argv[optind]);
            if (err) {
                shell_error_set_suggestion(
                    err, "pass a regular file as the script argument");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            } else {
                fprintf(stderr, "lush: %s is not a regular file\n",
                        argv[optind]);
            }
            /// Fall back to interactive mode
            IS_INTERACTIVE_SHELL = true;
            SHELL_TYPE = SHELL_INTERACTIVE;
            optind = 0;
            *in = stdin;
        } else {
            if ((*in = fopen(argv[optind], "r")) == NULL) {
                error_syscall("error: `init`: fopen");
            } else {
                process_shebang(*in);
            }
        }
    } else if (lle_adaptive_should_shell_be_interactive(
                   forced_interactive, has_script_file,
                   shell_opts.stdin_mode)) {
        /// Interactive shell detection using LLE adaptive terminal system.
        /// This handles:
        /// - Traditional TTY detection (stdin/stdout are terminals)
        /// - Editor terminals (Zed, VS Code, Cursor) with non-TTY stdin
        /// - Terminal multiplexers (tmux, screen)
        /// - Force interactive flag (-i)
        IS_INTERACTIVE_SHELL = true;
        SHELL_TYPE = SHELL_INTERACTIVE;
        *in = stdin;

        /// Debug: Show interactive detection
        const char *debug_env = getenv("LUSH_DEBUG");
        if (debug_env &&
            (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
            fprintf(
                stderr,
                "[INIT] Interactive shell detected via adaptive detection\n");
            fprintf(stderr,
                    "[INIT]   forced=%s, stdin_is_terminal=%s, stdin_mode=%s\n",
                    forced_interactive ? "true" : "false",
                    stdin_is_terminal ? "true" : "false",
                    shell_opts.stdin_mode ? "true" : "false");
        }
    } else {
        /// Non-interactive: piped input, -s mode, or no capable terminal
        IS_INTERACTIVE_SHELL = false;
        SHELL_TYPE = SHELL_NON_INTERACTIVE;
        *in = stdin;

        /// Debug: Show non-interactive detection
        const char *debug_env = getenv("LUSH_DEBUG");
        if (debug_env &&
            (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
            fprintf(stderr,
                    "[INIT] Non-interactive shell (adaptive detection)\n");
            fprintf(stderr,
                    "[INIT]   forced=%s, stdin_is_terminal=%s, stdin_mode=%s\n",
                    forced_interactive ? "true" : "false",
                    stdin_is_terminal ? "true" : "false",
                    shell_opts.stdin_mode ? "true" : "false");
        }
    }

    /// Set up interactive shell features if needed
    if (IS_INTERACTIVE_SHELL) {
        /// Enable job control by default for interactive shells (POSIX
        /// behavior) This allows background jobs (cmd &) to be tracked and
        /// managed
        shell_opts.job_control = true;

        /// Initialize display integration system config first (needed for debug
        /// flags)
        display_integration_config_t display_config;
        display_integration_create_default_config(&display_config);

        /// Configure based on environment and user preferences
        const char *layered_display_env = getenv("LUSH_LAYERED_DISPLAY");
        if (layered_display_env) {
            /// v1.3.0: Layered display is now exclusive - environment variable
            /// ignored Layered display is always enabled
        }

        /// Enable debug mode if requested
        const char *display_debug_env = getenv("LUSH_DISPLAY_DEBUG");
        if (display_debug_env && (strcmp(display_debug_env, "1") == 0 ||
                                  strcmp(display_debug_env, "true") == 0)) {
            display_config.debug_mode = true;
        }

        /// Set optimization level based on environment
        const char *optimization_env = getenv("LUSH_DISPLAY_OPTIMIZATION");
        if (optimization_env) {
            int opt_level = atoi(optimization_env);
            if (opt_level >= DISPLAY_OPTIMIZATION_DISABLED &&
                opt_level <= DISPLAY_OPTIMIZATION_MAXIMUM) {
                display_config.optimization_level =
                    (display_optimization_level_t)opt_level;
            }
        }

        /// Initialize memory pool system FIRST - required by LLE and display
        lush_pool_config_t pool_config =
            lush_pool_get_display_optimized_config();
        pool_config.enable_debugging = (getenv("LUSH_MEMORY_DEBUG") != NULL);

        lush_pool_error_t pool_result = lush_pool_init(&pool_config);
        if (pool_result != LUSH_POOL_SUCCESS) {
            if (display_config.debug_mode || getenv("LUSH_MEMORY_DEBUG")) {
                fprintf(
                    stderr,
                    "Warning: Failed to initialize memory pool system: %s\n",
                    lush_pool_error_string(pool_result));
                fprintf(stderr,
                        "Continuing with standard malloc/free operations\n");
            }
        } else {
            /// Register memory pool cleanup FIRST so it runs LAST in atexit.
            /// atexit handlers run in LIFO order (last registered = first run).
            /// By registering pool shutdown BEFORE LLE init, the LLE atexit
            /// handler (registered later) will run BEFORE pool shutdown,
            /// ensuring LLE can safely save history using pool memory.
            atexit(lush_pool_shutdown);

            if (display_config.debug_mode || getenv("LUSH_MEMORY_DEBUG")) {
                fprintf(stderr,
                        "Memory pool system initialized successfully\n");
            }
        }

        /// Initialize LLE shell integration (Spec 26)
        /// LLE is the sole line editor - no GNU readline fallback.
        /// Requires: global_memory_pool (initialized above)
        lle_result_t lle_result = lle_shell_integration_init();
        if (lle_result != LLE_SUCCESS) {
            shell_error_t *err = shell_error_create(
                SHELL_ERR_SUBSYSTEM_INIT_FAILED, SHELL_SEVERITY_WARNING,
                SOURCE_LOC_UNKNOWN, "failed to initialize the LLE (code %d)",
                (int)lle_result);
            if (err) {
                shell_error_set_suggestion(
                    err, "the shell will continue with a minimal line editor; "
                         "completion and syntax highlighting are unavailable "
                         "this session");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
        } else {
            /// Shell-side built-in widgets that need the symbol table -- liblle
            /// cannot link symtable directly, so we register them once the
            /// editor exists.
            lle_editor_t *ed = lle_get_global_editor();
            if (ed && ed->widget_registry) {
                lush_register_inspect_widget(ed->widget_registry);
            }
        }

        /// Initialize display integration ONLY in interactive mode
        if (IS_INTERACTIVE_SHELL) {
            /// Configure display options based on environment and command line
            /// v1.3.0: Layered display is now exclusive - always enabled
            /// No configuration needed

            /// Always initialize display integration to support runtime display
            /// enable
            if (!display_integration_init(&display_config)) {
                if (display_config.debug_mode || getenv("LUSH_DISPLAY_DEBUG")) {
                    fprintf(stderr, "Warning: Failed to initialize display "
                                    "integration, using standard display\n");
                }
                /// Continue with standard display - no fatal error
            } else {
                /// Announce activation with visual impact (only when debug mode
                /// enabled)
                if (display_config.debug_mode) {
                    printf("Display integration initialized "
                           "(layered_display=exclusive)\n");
                }
            }
        } else {
            /// Non-interactive mode: no display integration needed
            if (getenv("LUSH_DISPLAY_DEBUG")) {
                fprintf(stderr,
                        "Display integration skipped (non-interactive mode)\n");
            }
        }

        /// Generate initial prompt
        lle_shell_update_prompt();
    }

    /// For login shells, set the appropriate type
    if (IS_LOGIN_SHELL) {
        SHELL_TYPE = SHELL_LOGIN;
    }

    /// Get and set shell's ppid in environment
    pid_t ppid = getppid();
    char ppid_str[32];
    snprintf(ppid_str, sizeof(ppid_str), "%d", ppid);
    setenv("PPID", ppid_str, 1);
    symtable_set_global("PPID", ppid_str);
    symtable_export_global("PPID");

    /// Set initial exit status
    set_exit_status(0);

    /// Set shell PID ($$ special variable)
    pid_t shell_pid = getpid();
    char shell_pid_str[32];
    snprintf(shell_pid_str, sizeof(shell_pid_str), "%d", (int)shell_pid);
    symtable_set_global("$", shell_pid_str);

    /// Set shell name/script name and positional parameters
    if (shell_type() == NORMAL_SHELL && optind > 0 && argv[optind]) {
        /// Running a script - set up script arguments
        symtable_set_global("0", argv[optind]); /// Script name

        /// Update global shell_argc and shell_argv for script arguments
        shell_argc =
            argc - optind; /// Number of script args (including script name)
        shell_argv = &argv[optind]; /// Pointer to script args
    } else {
        /// Interactive or command mode - use shell arguments
        symtable_set_global("0", argv[0]);
    }

    /// Initialize history for interactive shells
    if (IS_INTERACTIVE_SHELL) {
        /// LLE history is initialized via lle_shell_integration_init()
        /// Initialize enhanced POSIX history system
        if (!global_posix_history) {
            global_posix_history = posix_history_create(0);
            if (global_posix_history) {
                /// Set default filename and load existing history
                char *home = symtable_get_global_default("HOME", "");
                if (home && *home) {
                    char histfile[1024];
                    snprintf(histfile, sizeof(histfile), "%s/.lush_history",
                             home);
                    posix_history_set_filename(global_posix_history, histfile);
                    posix_history_load(global_posix_history, histfile, false);
                }
                free(home); /* symtable_get_global_default returns strdup'd
                               value */

                /// Enable duplicate detection by default
                posix_history_set_no_duplicates(global_posix_history, true);
            }
        }
    }

    /// Initialize aliases
    init_aliases();

    /// Initialize command hash table
    init_command_hash();

    /// Initialize directory stack for pushd/popd
    dirstack_init();

    /// Completion is handled automatically by readline integration
    /// No need to set callbacks - they're integrated in lush_readline_init()

    /// Hints system is not implemented in readline integration yet
    /// TODO: Implement hints for readline if needed

    /// Set memory cleanup procedures on termination
    /// Note: atexit handlers run in REVERSE order of registration (LIFO)
    /// Register in order: last-to-run first, first-to-run last
    atexit(free_global_symtable);
    atexit(free_aliases);
    atexit(free_command_hash);
    atexit(dirstack_cleanup);
    atexit(autocorrect_cleanup);
    atexit(ssh_hosts_cleanup);
    atexit(config_cleanup);
    atexit(posix_history_cleanup);
    atexit(lle_terminal_detection_cache_cleanup);
    atexit(free_input_buffers);

    /// Process shebang if the shell is invoked with a script
    if (!IS_INTERACTIVE_SHELL && *in && has_script_file) {
        process_shebang(*in);
    }

    /// Register cleanup for display integration
    if (IS_INTERACTIVE_SHELL) {
        atexit(display_integration_cleanup);
    }

    /// Memory pool cleanup is now registered immediately after pool init
    /// (before LLE init) so it runs LAST in atexit order (LIFO).
    /// This ensures LLE can safely use pool memory during shutdown.

    return 0;
}

/**
 * @brief Parse command line options
 *
 * POSIX-compliant argument parsing that handles:
 * - Short options (-c, -s, -i, -l, etc.)
 * - Long options (--help, --version)
 * - Option terminator (--)
 * - Script file and script arguments
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Index of first non-option argument (script name)
 */
static int parse_opts(int argc, char **argv) {
    /// POSIX-compliant argument parsing: shell [options] script [script-args]
    /// Only parse options that come before the script name
    /// Everything after the script name should be treated as script arguments

    int arg_index = 1; /// Start from first argument (skip program name)

    /// Manual option parsing to properly handle POSIX argument separation
    while (arg_index < argc) {
        char *arg = argv[arg_index];

        /// If argument doesn't start with '-', it's the script name
        /// Stop parsing options here - everything after is script arguments
        if (arg[0] != '-' || strlen(arg) == 1) {
            break;
        }

        /// Handle special case: "--" ends option parsing
        if (strcmp(arg, "--") == 0) {
            arg_index++; /// Skip the "--"
            break;
        }

        /// Handle long options
        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) {
                usage(EXIT_SUCCESS);
            } else if (strcmp(arg, "--version") == 0) {
                printf("%s %s\n", LUSH_NAME, LUSH_VERSION_STRING);
                printf("%s\n", LUSH_DESCRIPTION);
                printf("Copyright (C) 2021-2026 Michael Berry. Licensed under "
                       "MIT.\n");
                exit(EXIT_SUCCESS);
            } else if (strcmp(arg, "--strict") == 0) {
                /// Enable strict compatibility mode - warnings become errors
                compat_set_strict(true);
            } else if (strcmp(arg, "--lush") == 0) {
                shell_opts.cli_mode_override = (int)SHELL_MODE_LUSH;
                shell_opts.cli_mode_override_set = true;
            } else if (strcmp(arg, "--posix") == 0) {
                shell_opts.cli_mode_override = (int)SHELL_MODE_POSIX;
                shell_opts.cli_mode_override_set = true;
            } else if (strcmp(arg, "--bash") == 0) {
                shell_opts.cli_mode_override = (int)SHELL_MODE_BASH;
                shell_opts.cli_mode_override_set = true;
            } else if (strcmp(arg, "--zsh") == 0) {
                shell_opts.cli_mode_override = (int)SHELL_MODE_ZSH;
                shell_opts.cli_mode_override_set = true;
            } else if (strncmp(arg, "--target=", 9) == 0) {
                /// Set target shell for compatibility checking (stored as
                /// string)
                const char *target_name = arg + 9;
                compat_set_target(target_name);
            } else if (strncmp(arg, "--target", 8) == 0) {
                /// Handle --target <shell> (space-separated)
                if (arg_index + 1 < argc) {
                    arg_index++;
                    const char *target_name = argv[arg_index];
                    compat_set_target(target_name);
                } else {
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN, "--target requires an argument");
                    if (err) {
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    }
                    usage(EXIT_FAILURE);
                }
            } else if (strcmp(arg, "--analyze") == 0) {
                /// Enable full analyze mode
                shell_opts.analyze_mode = true;
            } else if (strcmp(arg, "--lint") == 0) {
                /// Enable lint mode (actionable issues only)
                shell_opts.lint_mode = true;
            } else if (strncmp(arg, "--analyze=", 10) == 0) {
                /// --analyze=FILE
                shell_opts.analyze_mode = true;
                shell_opts.analyze_file = strdup(arg + 10);
            } else if (strncmp(arg, "--lint=", 7) == 0) {
                /// --lint=FILE (actionable linting)
                shell_opts.lint_mode = true;
                shell_opts.analyze_file = strdup(arg + 7);
            } else if (strcmp(arg, "--fix") == 0) {
                /// Enable safe auto-fix (implies --lint)
                shell_opts.lint_mode = true;
                shell_opts.fix_mode = true;
            } else if (strcmp(arg, "--unsafe-fixes") == 0) {
                /// Include unsafe fixes (implies --fix and --lint)
                shell_opts.lint_mode = true;
                shell_opts.fix_mode = true;
                shell_opts.unsafe_fixes = true;
            } else if (strcmp(arg, "--dry-run") == 0) {
                /// Preview fixes without applying
                shell_opts.dry_run = true;
            } else if (strcmp(arg, "--format") == 0) {
                /// --format <fmt> (space-separated)
                if (arg_index + 1 < argc) {
                    arg_index++;
                    shell_opts.output_format = strdup(argv[arg_index]);
                } else {
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN, "--format requires an argument");
                    if (err) {
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    }
                    usage(EXIT_FAILURE);
                }
            } else if (strncmp(arg, "--format=", 9) == 0) {
                /// --format=FMT
                shell_opts.output_format = strdup(arg + 9);
            } else {
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR,
                    SOURCE_LOC_UNKNOWN, "invalid option: %s", arg);
                if (err) {
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                }
                usage(EXIT_FAILURE);
            }
            arg_index++;
            continue;
        }

        /// Handle short options (can be combined like -abc)
        for (int i = 1; arg[i] != '\0'; i++) {
            char opt = arg[i];

            switch (opt) {
            case 'h':
                shell_opts.hash_commands = true;
                break;
            case 'V':
                printf("%s %s\n", LUSH_NAME, LUSH_VERSION_STRING);
                printf("%s\n", LUSH_DESCRIPTION);
                printf("Copyright (C) 2021-2026 Michael Berry. Licensed under "
                       "MIT.\n");
                exit(EXIT_SUCCESS);
                break;
            case 'c':
                /// Execute command string - requires argument
                if (arg[i + 1] != '\0') {
                    /// Argument attached to option: -ccommand
                    shell_opts.command_mode = true;
                    shell_opts.command_string = strdup(&arg[i + 1]);
                    if (!shell_opts.command_string) {
                        error_abort(
                            "failed to allocate memory for command string");
                    }
                    goto next_arg; /// Skip rest of this argument
                } else if (arg_index + 1 < argc) {
                    /// Argument in next parameter: -c command
                    arg_index++;
                    shell_opts.command_mode = true;
                    shell_opts.command_string = strdup(argv[arg_index]);
                    if (!shell_opts.command_string) {
                        error_abort(
                            "failed to allocate memory for command string");
                    }
                    goto next_arg;
                } else {
                    shell_error_t *err = shell_error_create(
                        SHELL_ERR_MISSING_ARGUMENT, SHELL_SEVERITY_ERROR,
                        SOURCE_LOC_UNKNOWN,
                        "option requires an argument -- '%c'", opt);
                    if (err) {
                        shell_error_display(err, stderr, isatty(STDERR_FILENO));
                        shell_error_free(err);
                    }
                    usage(EXIT_FAILURE);
                }
                break;
            case 's':
                shell_opts.stdin_mode = true;
                break;
            case 'i':
                shell_opts.interactive = true;
                break;
            case 'l':
                shell_opts.login_shell = true;
                break;
            case 'e':
                shell_opts.exit_on_error = true;
                break;
            case 'x':
                shell_opts.trace_execution = true;
                break;
            case 'n':
                shell_opts.syntax_check = true;
                break;
            case 'u':
                shell_opts.unset_error = true;
                break;
            case 'v':
                shell_opts.verbose = true;
                break;
            case 'f':
                shell_opts.no_globbing = true;
                break;
            case 'm':
                shell_opts.job_control = true;
                break;
            case 'b':
                shell_opts.notify = true;
                break;
            case 't':
                shell_opts.onecmd = true;
                break;
            case 'a':
                shell_opts.allexport = true;
                break;
            case 'C':
                shell_opts.noclobber = true;
                break;
            default: {
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR,
                    SOURCE_LOC_UNKNOWN, "invalid option -- '%c'", opt);
                if (err) {
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                }
                usage(EXIT_FAILURE);
            }
            }
        }

    next_arg:
        arg_index++;
    }

    /// Return the index of the first non-option argument (script name)
    return arg_index;
}

/**
 * @brief Display usage information and exit
 *
 * Prints command line usage, available options, and shell
 * configuration information to stdout, then exits.
 *
 * @param err Exit code (EXIT_SUCCESS or EXIT_FAILURE)
 */
static void usage(int err) {
    printf("Usage: %s [OPTIONS] [SCRIPT]\n", LUSH_NAME);
    printf("A POSIX-compliant shell with modern features\n\n");
    printf("Options:\n");
    printf("      --help              Show this help message and exit\n");
    printf("  -V, --version           Show version information and exit\n");
    printf("      --analyze[=FILE]    Full script analysis (info, warnings, "
           "errors)\n");
    printf("      --lint[=FILE]       Lint for actionable issues (warnings, "
           "errors)\n");
    printf("      --fix               Apply safe automatic fixes (implies "
           "--lint)\n");
    printf(
        "      --unsafe-fixes      Also apply unsafe fixes (implies --fix)\n");
    printf("      --dry-run           Preview fixes without applying\n");
    printf(
        "      --format=FMT        Output format: text (default), json, gcc\n");
    printf(
        "      --strict            Treat compatibility warnings as errors\n");
    printf("      --target=<shell>    Check compatibility against shell "
           "(posix, bash, zsh)\n");
    printf("  -c command       Execute command string and exit\n");
    printf("  -s               Read commands from standard input\n");
    printf("  -i               Force interactive mode\n");
    printf("  -l               Act as login shell\n");
    printf("  -e               Exit immediately on command failure (set -e)\n");
    printf("  -x               Trace command execution (set -x)\n");
    printf("  -n               Syntax check mode - read but don't execute (set "
           "-n)\n");
    printf("  -u               Treat unset variables as error (set -u)\n");
    printf("  -v               Verbose mode - print input lines (set -v)\n");
    printf("  -f               Disable pathname expansion (set -f)\n");
    printf("  -h               Enable command path hashing (set -o hashall)\n");
    printf("  -m               Enable job control (set -m)\n");
    printf("  -a               Auto-export all variables (set -o allexport)\n");
    printf("  -b               Notify asynchronously of background job "
           "completion (set -o notify)\n");
    printf("  -t               Exit after executing one command (set -o "
           "onecmd)\n");
    printf("  -C               Prevent overwriting files with redirection (set "
           "-o noclobber)\n");
    printf("\nArguments:\n");
    printf("  SCRIPT           Execute commands from script file\n");
    printf("\nShell Options:\n");
    printf(
        "  Use 'set -o option' or 'set +o option' to control shell behavior\n");
    printf("  Available options: errexit, xtrace, noexec, nounset, verbose,\n");
    printf("                     noglob, hashall, monitor, notify, onecmd\n");
    printf("\nFor more information, see the manual or documentation.\n");
    exit(err);
}
