/**
 * @file bin_debug.c
 * @brief `debug` builtin -- integrated debugger interface
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include "debug.h"

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
    /// Initialize debug context if not already done
    if (!g_debug_context) {
        g_debug_context = debug_init();
        if (!g_debug_context) {
            fprintf(stderr, "debug: Failed to initialize debug context\n");
            return 1;
        }
    }

    debug_context_t *ctx = g_debug_context;

    /// Count arguments
    int argc_real = 0;
    while (argv[argc_real]) {
        argc_real++;
    }

    /// No arguments - show current debug status
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

    /// Process subcommands - comprehensive implementation
    const char *subcmd = argv[1];

    if (strcmp(subcmd, "on") == 0 || strcmp(subcmd, "enable") == 0) {
        debug_enable(ctx, true);
        if (argc_real > 2) {
            /// Set level if provided
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
