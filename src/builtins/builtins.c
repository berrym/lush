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
#include "config.h"
#include "config_registry.h"
#include "debug.h"
#include "dirstack.h"
#include "display/command_layer.h"
#include "display/composition_engine.h"
#include "display_integration.h"
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
#include "lush_fork.h"
#include "lush_memory_pool.h"
#include "shell_error.h"
#include "shell_mode.h"
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

/* builtin_get_source_location is declared in include/builtins.h so the
 * structured-error helpers in posix_opts.c (and other env-helper TUs)
 * can read the dispatcher-stashed call site directly. */

/* Builtin error reporting deliberately does NOT use a builtin-specific
 * helper layer. Sites use the structured-error API directly:
 *
 *   - Simple case (no `help:` suggestion):
 *       executor_error_report(current_executor, CODE,
 *                             builtin_get_source_location(), "fmt", args);
 *     The wrapper attaches the source-line snippet and walks the executor
 *     context stack (which includes "in builtin '<name>'" pushed by the
 *     dispatcher).
 *
 *   - With suggestion: inline the canonical shell_error_create() block —
 *     create, attach source line via executor_get_source_line(), walk
 *     executor context stack, set_suggestion, display, free, fallback
 *     fprintf on create failure.
 *
 * The pre-2026-04 builtin_error() and builtin_error_help() helpers were
 * removed when the foundation made source-line + dispatcher-context
 * automatic — they were a parallel facade hiding what the canonical API
 * is doing. */

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
    {"disown", "remove jobs from shell or mark to not receive SIGHUP",
     bin_disown},
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
    {"mode", "select active shell mode preset", bin_mode},
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
    {"analyze", "full script analysis with info, warnings, and errors",
     bin_analyze},
    {"lint", "lint scripts and optionally apply automatic fixes", bin_lint},
};

const size_t builtins_count = sizeof(builtins) / sizeof(builtins[0]);

/**
 * @brief Check if a string is a valid shell variable identifier
 *
 * A valid identifier starts with a letter or underscore, followed
 * by zero or more alphanumeric characters or underscores.
 *
 * @param name The string to validate
 * @return 1 if valid identifier, 0 otherwise
 */
int is_valid_identifier(const char *name) {
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

// Forward declarations for logical operator support

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

/* Per-call stash set by execute_builtin_command in executor.c via
 * builtin_set_source_location() before dispatching to the builtin
 * function. Tracks the source location of the command node that
 * invoked the builtin — i.e. the actual call site, not the enclosing
 * control-flow construct. Cleared after the builtin returns.
 *
 * For nested builtin invocations (e.g. `eval` calling another builtin)
 * the dispatcher saves the previous stash on the C stack and restores
 * it on return, so the location is always correct for the innermost
 * builtin currently executing. */
static source_location_t s_builtin_call_loc = SOURCE_LOC_UNKNOWN;

source_location_t builtin_swap_source_location(source_location_t loc) {
    source_location_t prev = s_builtin_call_loc;
    s_builtin_call_loc = loc;
    return prev;
}

/**
 * @brief Get current source location for builtin error reporting
 *
 * Prefers the per-call stash set by the executor's builtin dispatcher
 * (the actual call site of the current builtin invocation). Falls back
 * to the most recent source location on the executor's context stack
 * (the enclosing control-flow construct) when no per-call stash exists,
 * and finally to SOURCE_LOC_UNKNOWN.
 */
source_location_t builtin_get_source_location(void) {
    if (SOURCE_LOC_VALID(s_builtin_call_loc) ||
        s_builtin_call_loc.filename != NULL) {
        return s_builtin_call_loc;
    }
    if (current_executor && current_executor->context_depth > 0) {
        return current_executor
            ->context_locations[current_executor->context_depth - 1];
    }
    return SOURCE_LOC_UNKNOWN;
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

/* Callback for declare -p to print scalar variables */

/* Callback for declare -p to print array variables */

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

/* ============================================================================
 * Directory Stack Builtins
 * ============================================================================
 */

/* ============================================================================
 * Environment Builtin
 * ============================================================================
 */

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
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--strict") == 0) {
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
            } else if (strcmp(issue->severity, "warning") == 0 &&
                       exit_status < 1) {
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
            printf("  --unsafe-fixes      Also apply unsafe fixes (implies "
                   "--fix)\n");
            printf("  --dry-run           Preview fixes without applying\n");
            printf("  --diff              Show unified diff of changes\n");
            printf(
                "  --no-backup         Don't create .bak backup when fixing\n");
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
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--strict") == 0) {
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
                    size_t fixes_found =
                        fixer_collect_fixes(&fixer_ctx, target);

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
                            if (remaining < 0)
                                remaining = 0;
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
        remaining = debug_lint_script(ctx, script_file, fix_mode, unsafe_fixes,
                                      dry_run);
    }

    /* Determine exit code */
    int exit_status = 0;
    if (remaining < 0) {
        exit_status = 3; /* Fix application error */
    } else if (remaining > 0) {
        /* Check if we have errors or just warnings */
        analysis_issue_t *issue = ctx->analysis_issues;
        while (issue) {
            if (strcmp(issue->severity, "error") == 0) {
                exit_status = 2;
                break;
            } else if (strcmp(issue->severity, "warning") == 0 &&
                       exit_status < 1) {
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
