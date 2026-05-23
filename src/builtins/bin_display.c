/**
 * @file bin_display.c
 * @brief `display` builtin -- top-level dispatcher for the layered display
 * system
 *
 * Top-level subcommands (status / config / stats / diagnostics / test /
 * performance / help) live inline here. The LLE subcommand chain
 * delegates to per-subcommand handlers in
 * `src/builtins/display/lle_<sub>.c`, declared in
 * `include/builtins/display.h`. The router is a thin
 * `if/else if (strcmp(lle_cmd, X)) return display_lle_X(argc-2, argv+2)`
 * chain.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "display_integration.h"

#include <inttypes.h>

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
            executor_error_report(current_executor,
                                  SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                                  builtin_get_source_location(),
                                  "display: failed to get configuration");
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
            executor_error_report(current_executor,
                                  SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                                  builtin_get_source_location(),
                                  "display: failed to get statistics");
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
                executor_error_report(
                    current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                    builtin_get_source_location(),
                    "display: failed to initialise performance monitoring");
                return 1;
            }

        } else if (strcmp(perf_cmd, "report") == 0) {
            bool detailed = (argc > 3 && strcmp(argv[3], "detail") == 0);
            if (display_integration_perf_monitor_report(detailed)) {
                return 0;
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                    builtin_get_source_location(),
                    "display: failed to generate performance report");
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
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "display: failed to establish baseline (need more "
                    "measurements)");
                return 1;
            }

        } else if (strcmp(perf_cmd, "reset") == 0) {
            if (display_integration_perf_monitor_reset()) {
                printf("Performance metrics reset\n");
                return 0;
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                    builtin_get_source_location(),
                    "display: failed to reset performance metrics");
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
                executor_error_report(
                    current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                    builtin_get_source_location(),
                    "display: failed to check performance targets");
                return 1;
            }

        } else if (strcmp(perf_cmd, "monitoring") == 0) {
            if (argc < 4) {
                executor_error_report(
                    current_executor, SHELL_ERR_MISSING_ARGUMENT,
                    builtin_get_source_location(),
                    "display performance monitoring: requires 'on' or 'off'");
                return 1;
            }

            const char *state = argv[3];
            if (strcmp(state, "on") == 0) {
                if (display_integration_perf_monitor_set_active(true, 10)) {
                    printf("Real-time performance monitoring enabled (10Hz)\n");
                    return 0;
                } else {
                    executor_error_report(
                        current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                        builtin_get_source_location(),
                        "display performance monitoring: failed to enable");
                    return 1;
                }
            } else if (strcmp(state, "off") == 0) {
                if (display_integration_perf_monitor_set_active(false, 0)) {
                    printf("Real-time performance monitoring disabled\n");
                    return 0;
                } else {
                    executor_error_report(
                        current_executor, SHELL_ERR_SUBSYSTEM_INIT_FAILED,
                        builtin_get_source_location(),
                        "display performance monitoring: failed to disable");
                    return 1;
                }
            } else {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "display performance monitoring: invalid state '%s' "
                    "(use 'on' or 'off')",
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
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "display performance: unknown subcommand '%s'", perf_cmd);
            if (err) {
                if (current_executor && SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                if (current_executor) {
                    for (size_t i = 0; i < current_executor->context_depth &&
                                       i < SHELL_ERROR_CONTEXT_MAX;
                         i++) {
                        if (current_executor->context_stack[i]) {
                            shell_error_push_context(
                                err, "%s", current_executor->context_stack[i]);
                        }
                    }
                }
                shell_error_set_suggestion(
                    err, "run 'display performance' with no subcommand to "
                         "see the available commands");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
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
            printf("\nWidgets:\n");
            printf("  widget [cmd]     - User widget registration\n");
            printf("                     list    - Show all widgets\n");
            printf("                     add NAME 'CMD'\n");
            printf("                     remove NAME\n");
            printf("                     show NAME\n");
            printf("  hook [cmd]       - Attach widgets to lifecycle hooks\n");
            printf("                     list    - Show all attachments\n");
            printf("                     add HOOK WIDGET\n");
            printf("                     remove HOOK WIDGET\n");
            printf("  segment [cmd]    - User-defined prompt segments\n");
            printf("                     list    - Show all segments\n");
            printf("                     add NAME VARIABLE\n");
            printf("                     remove NAME\n");
            printf("                     show NAME\n");
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
            return display_lle_status(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "history") == 0) {
            return display_lle_history(argc - 2, argv + 2);

        } else if (strcmp(lle_cmd, "keybindings") == 0) {
            return display_lle_keybindings(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "autosuggestions") == 0) {
            return display_lle_autosuggestions(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "syntax") == 0) {
            return display_lle_syntax(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "transient") == 0) {
            return display_lle_transient(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "hot-reload") == 0) {
            return display_lle_hot_reload(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "newline-before") == 0) {
            return display_lle_newline_before(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "multiline") == 0) {
            return display_lle_multiline(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "diagnostics") == 0) {
            return display_lle_diagnostics(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "reset") == 0) {
            return display_lle_reset(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "theme") == 0) {
            return display_lle_theme(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "completion") == 0) {
            return display_lle_completion(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "widget") == 0) {
            return display_lle_widget(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "hook") == 0) {
            return display_lle_hook(argc - 2, argv + 2);
        } else if (strcmp(lle_cmd, "segment") == 0) {
            return display_lle_segment(argc - 2, argv + 2);
        } else {
            source_location_t loc = builtin_get_source_location();
            shell_error_t *err = shell_error_create(
                SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR, loc,
                "display lle: unknown subcommand '%s'", lle_cmd);
            if (err) {
                if (current_executor && SOURCE_LOC_VALID(loc)) {
                    char *src_line =
                        executor_get_source_line(current_executor, loc.line);
                    if (src_line) {
                        shell_error_set_source_line(err, src_line, loc.column,
                                                    loc.column + loc.length);
                        free(src_line);
                    }
                }
                if (current_executor) {
                    for (size_t i = 0; i < current_executor->context_depth &&
                                       i < SHELL_ERROR_CONTEXT_MAX;
                         i++) {
                        if (current_executor->context_stack[i]) {
                            shell_error_push_context(
                                err, "%s", current_executor->context_stack[i]);
                        }
                    }
                }
                shell_error_set_suggestion(
                    err, "run 'display lle' with no subcommand to see usage");
                shell_error_display(err, stderr, isatty(STDERR_FILENO));
                shell_error_free(err);
            }
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
        source_location_t loc = builtin_get_source_location();
        shell_error_t *err =
            shell_error_create(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_ERROR,
                               loc, "display: unknown subcommand '%s'", subcmd);
        if (err) {
            if (current_executor && SOURCE_LOC_VALID(loc)) {
                char *src_line =
                    executor_get_source_line(current_executor, loc.line);
                if (src_line) {
                    shell_error_set_source_line(err, src_line, loc.column,
                                                loc.column + loc.length);
                    free(src_line);
                }
            }
            if (current_executor) {
                for (size_t i = 0; i < current_executor->context_depth &&
                                   i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (current_executor->context_stack[i]) {
                        shell_error_push_context(
                            err, "%s", current_executor->context_stack[i]);
                    }
                }
            }
            shell_error_set_suggestion(
                err, "run 'display help' to see the available subcommands");
            shell_error_display(err, stderr, isatty(STDERR_FILENO));
            shell_error_free(err);
        }
        return 1;
    }
}
