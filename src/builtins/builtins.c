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
            printf("  hot-reload on|off       - Control theme hot-reload\n");
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

                        /* When switching to global scope, run full dedup scan
                         */
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
                           config.lle_dedup_navigation ? "enabled"
                                                       : "disabled");
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
                    printf("\nConfig file: ~/.config/lush/keybindings.toml\n");
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
                    config_registry_set_boolean("display.autosuggestions",
                                                true);
                }
                printf("Autosuggestions enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_autosuggestions = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.autosuggestions",
                                                false);
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
                    config_registry_set_boolean("display.syntax_highlighting",
                                                true);
                }
                /* Apply to runtime: update the command layer */
                display_controller_t *dc = display_integration_get_controller();
                if (dc && dc->compositor && dc->compositor->command_layer) {
                    command_layer_set_syntax_enabled(
                        dc->compositor->command_layer, true);
                }
                printf("Syntax highlighting enabled\n");
                return 0;
            } else if (strcmp(state, "off") == 0) {
                config.display_syntax_highlighting = false;
                if (config_registry_is_initialized()) {
                    config_registry_set_boolean("display.syntax_highlighting",
                                                false);
                }
                /* Apply to runtime: update the command layer */
                display_controller_t *dc = display_integration_get_controller();
                if (dc && dc->compositor && dc->compositor->command_layer) {
                    command_layer_set_syntax_enabled(
                        dc->compositor->command_layer, false);
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
                    config_registry_set_boolean("display.transient_prompt",
                                                true);
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
                    config_registry_set_boolean("display.transient_prompt",
                                                false);
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
                printf("Theme hot-reload: %s\n", config.display_theme_hot_reload
                                                     ? "enabled"
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
                printf("  Keybindings: %s\n", editor->keybinding_manager
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
                    /* Sync PS1/PS2 from the new theme */
                    const lle_theme_t *new_theme = lle_composer_get_theme(
                        g_lle_integration->prompt_composer);
                    if (new_theme) {
                        if (new_theme->layout.style !=
                            LLE_PROMPT_STYLE_POWERLINE) {
                            /* Plain themes: write template to PS1 */
                            if (strlen(new_theme->layout.ps1_format) > 0) {
                                symtable_set_global(
                                    "PS1", new_theme->layout.ps1_format);
                            }
                        }
                        if (strlen(new_theme->layout.ps2_format) > 0) {
                            symtable_set_global("PS2",
                                                new_theme->layout.ps2_format);
                        }
                    }
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

        } else if (strcmp(lle_cmd, "completion") == 0) {
            /* LLE completion subsystem. The umbrella command groups
             * source-list management (under `sources`) with future
             * behavior knobs registered alongside it. Bare
             * `display lle completion` prints the namespace help; an
             * unrecognized subcommand is an error. */
            const char *comp_subcmd = (argc >= 4) ? argv[3] : "help";

            if (strcmp(comp_subcmd, "sources") == 0) {
                const char *sources_subcmd = (argc >= 5) ? argv[4] : "list";

                if (strcmp(sources_subcmd, "list") == 0) {
                    /* List all completion sources */
                    printf("LLE Completion Sources\n");
                    printf("======================\n\n");

                    /* Built-in sources */
                    printf("Built-in Sources:\n");
                    size_t total = lle_completion_get_source_count();
                    for (size_t i = 0; i < total; i++) {
                        if (!lle_completion_source_is_custom(i)) {
                            const char *name =
                                lle_completion_get_source_name(i);
                            printf("  - %s\n", name ? name : "(unknown)");
                        }
                    }

                    /* Custom sources */
                    size_t custom_count =
                        lle_completion_get_custom_source_count();
                    if (custom_count > 0) {
                        printf("\nCustom Sources:\n");
                        for (size_t i = 0; i < custom_count; i++) {
                            const char *name =
                                lle_completion_get_custom_source_name(i);
                            const char *desc =
                                lle_completion_get_custom_source_description(i);
                            if (desc) {
                                printf("  - %s: %s\n",
                                       name ? name : "(unknown)", desc);
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
                        printf("Create ~/.config/lush/completions.toml to "
                               "define custom sources.\n");
                    }

                    return 0;

                } else if (strcmp(sources_subcmd, "reload") == 0) {
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

                } else if (strcmp(sources_subcmd, "help") == 0 ||
                           strcmp(sources_subcmd, "--help") == 0) {
                    printf("LLE Completion Source Commands\n");
                    printf("==============================\n\n");
                    printf("Usage: display lle completion sources [command]\n");
                    printf("\nCommands:\n");
                    printf("  list    - Show all completion sources "
                           "(default)\n");
                    printf("  reload  - Reload custom sources from config "
                           "file\n");
                    printf("  help    - Show this help message\n");
                    printf("\nConfig File:\n");
                    printf("  ~/.config/lush/completions.toml\n");
                    printf("\nExample config:\n");
                    printf("  [sources.git-branches]\n");
                    printf("  description = \"Git branch names\"\n");
                    printf(
                        "  applies_to = [\"git checkout\", \"git merge\"]\n");
                    printf("  argument = 2\n");
                    printf("  command = \"git branch --list 2>/dev/null | sed "
                           "'s/^[* ]*//'\"\n");
                    printf("  cache_seconds = 5\n");
                    return 0;

                } else {
                    fprintf(stderr,
                            "display lle completion sources: Unknown "
                            "subcommand '%s'\n",
                            sources_subcmd);
                    fprintf(stderr, "Usage: display lle completion sources "
                                    "[list|reload|help]\n");
                    return 1;
                }

            } else if (strcmp(comp_subcmd, "chain_directories") == 0) {
                /* completion.chain_directories: when on, accepting a
                 * directory completion auto-re-triggers completion at
                 * the new prefix (fish-style cascading). Per-mode
                 * default: lush=true, others=false. */
                if (argc < 5) {
                    bool cur = false;
                    creg_result_t r = config_registry_get_boolean(
                        "completion.chain_directories", &cur);
                    if (r != CREG_SUCCESS) {
                        cur = false;
                    }
                    printf("chain_directories: %s\n", cur ? "on" : "off");
                    printf("Usage: display lle completion chain_directories "
                           "<on|off>\n");
                    return 0;
                }

                const char *state = argv[4];
                if (strcmp(state, "on") == 0) {
                    config_registry_set_boolean("completion.chain_directories",
                                                true);
                    printf("chain_directories enabled\n");
                    return 0;
                } else if (strcmp(state, "off") == 0) {
                    config_registry_set_boolean("completion.chain_directories",
                                                false);
                    printf("chain_directories disabled\n");
                    return 0;
                } else {
                    fprintf(stderr,
                            "display lle completion chain_directories: "
                            "invalid value '%s' (use on|off)\n",
                            state);
                    return 1;
                }

            } else if (strcmp(comp_subcmd, "help") == 0 ||
                       strcmp(comp_subcmd, "--help") == 0) {
                printf("LLE Completion Subsystem\n");
                printf("========================\n\n");
                printf("Usage: display lle completion <subcommand>\n\n");
                printf("Subcommands:\n");
                printf("  sources [list|reload|help]   - Manage completion "
                       "sources\n");
                printf("  chain_directories <on|off>   - Re-trigger "
                       "completion after directory accept\n");
                printf("  help                         - Show this help "
                       "message\n");
                return 0;

            } else {
                fprintf(stderr,
                        "display lle completion: Unknown subcommand '%s'\n",
                        comp_subcmd);
                fprintf(stderr, "Usage: display lle completion "
                                "<sources|chain_directories|help>\n");
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
        printf("  - LUSH_LAYERED_DISPLAY=1|0     Enable/disable at startup\n");
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
