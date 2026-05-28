/**
 * @file display/lle_history.c
 * @brief `display lle history` -- LLE history subsystem configuration
 *
 * Configures dedup write-time policy (on/off + scope + strategy),
 * navigation dedup, navigation unique behavior. Status / show /
 * help all live in the same handler. Extracted from bin_display.c
 * during the post-B12 follow-up split.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "lle/history.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"

int display_lle_history(int argc, char **argv) {
    // History behavior configuration commands
    if (argc < 2) {
        // No subcommand - show help with all options and values
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

    const char *hist_subcmd = argv[1];

    if (strcmp(hist_subcmd, "status") == 0) {
        // Show current history settings
        printf("LLE History Settings:\n");
        printf("\nWrite-time Deduplication:\n");
        printf("  Enabled: %s\n",
               config.lle_enable_deduplication ? "yes" : "no");

        // Scope
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

        // Strategy
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
        // Deduplication settings
        if (argc < 3) {
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

        const char *dedup_opt = argv[2];

        if (strcmp(dedup_opt, "on") == 0) {
            config.lle_enable_deduplication = true;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.enable_deduplication", true);
            }
            printf("Write-time deduplication enabled\n");
            return 0;

        } else if (strcmp(dedup_opt, "off") == 0) {
            config.lle_enable_deduplication = false;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.enable_deduplication", false);
            }
            printf("Write-time deduplication disabled\n");
            return 0;

        } else if (strcmp(dedup_opt, "scope") == 0) {
            if (argc < 4) {
                printf("Usage: display lle history dedup scope "
                       "<none|session|recent|global>\n");
                return 1;
            }
            const char *scope_val = argv[3];

            if (strcmp(scope_val, "none") == 0) {
                config.lle_dedup_scope = LLE_DEDUP_SCOPE_NONE;
            } else if (strcmp(scope_val, "session") == 0) {
                config.lle_dedup_scope = LLE_DEDUP_SCOPE_SESSION;
            } else if (strcmp(scope_val, "recent") == 0) {
                config.lle_dedup_scope = LLE_DEDUP_SCOPE_RECENT;
            } else if (strcmp(scope_val, "global") == 0) {
                config.lle_dedup_scope = LLE_DEDUP_SCOPE_GLOBAL;

                /// When switching to global scope, run full dedup scan
                lle_editor_t *editor = lle_get_global_editor();
                if (editor && editor->history_system &&
                    editor->history_system->dedup_engine) {
                    size_t removed = 0;
                    lle_history_dedup_full_scan(
                        editor->history_system->dedup_engine, &removed);
                    if (removed > 0) {
                        printf("Cleaned %zu duplicate entries\n", removed);
                        // Save to persist changes
                        const char *home = getenv("HOME");
                        if (home) {
                            char history_path[1024];
                            snprintf(history_path, sizeof(history_path),
                                     "%s/.lush_history", home);
                            lle_history_save_to_file(editor->history_system,
                                                     history_path);
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
                config_registry_set_string("lle.dedup_scope", scope_val);
            }
            printf("Deduplication scope set to '%s'\n", scope_val);
            return 0;

        } else if (strcmp(dedup_opt, "strategy") == 0) {
            if (argc < 4) {
                printf("Usage: display lle history dedup strategy "
                       "<ignore|keep-recent|keep-frequent|merge|"
                       "keep-all>\n");
                return 1;
            }
            const char *strategy_val = argv[3];

            if (strcmp(strategy_val, "ignore") == 0) {
                config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_IGNORE;
            } else if (strcmp(strategy_val, "keep-recent") == 0) {
                config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_KEEP_RECENT;
            } else if (strcmp(strategy_val, "keep-frequent") == 0) {
                config.lle_dedup_strategy = LLE_DEDUP_STRATEGY_KEEP_FREQUENT;
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
                config_registry_set_string("lle.dedup_strategy", strategy_val);
            }
            printf("Deduplication strategy set to '%s'\n", strategy_val);
            return 0;

        } else if (strcmp(dedup_opt, "clean") == 0) {
            // Full history deduplication scan
            lle_editor_t *editor = lle_get_global_editor();
            if (!editor || !editor->history_system) {
                fprintf(stderr, "Error: History system not available\n");
                return 1;
            }

            if (!editor->history_system->dedup_engine) {
                fprintf(stderr, "Error: Deduplication engine not available\n");
                return 1;
            }

            size_t removed = 0;
            lle_result_t result = lle_history_dedup_full_scan(
                editor->history_system->dedup_engine, &removed);
            if (result != LLE_SUCCESS) {
                fprintf(stderr, "Error: Failed to run deduplication scan\n");
                return 1;
            }

            if (removed > 0) {
                printf("Removed %zu duplicate entries from history\n", removed);
                // Save history to persist changes
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
        // Navigation-time duplicate skipping
        if (argc < 3) {
            printf("Navigation duplicate skipping: %s\n",
                   config.lle_dedup_navigation ? "enabled" : "disabled");
            printf("Usage: display lle history nav-dedup on|off\n");
            return 0;
        }

        const char *val = argv[2];
        if (strcmp(val, "on") == 0) {
            config.lle_dedup_navigation = true;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.dedup_navigation", true);
            }
            printf("Navigation duplicate skipping enabled\n");
            return 0;
        } else if (strcmp(val, "off") == 0) {
            config.lle_dedup_navigation = false;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.dedup_navigation", false);
            }
            printf("Navigation duplicate skipping disabled\n");
            return 0;
        } else {
            fprintf(stderr, "Invalid value '%s'. Use: on, off\n", val);
            return 1;
        }

    } else if (strcmp(hist_subcmd, "nav-unique") == 0) {
        // Unique entries per navigation session
        if (argc < 3) {
            printf("Unique entries per session: %s\n",
                   config.lle_dedup_navigation_unique ? "enabled" : "disabled");
            printf("Usage: display lle history nav-unique on|off\n");
            return 0;
        }

        const char *val = argv[2];
        if (strcmp(val, "on") == 0) {
            config.lle_dedup_navigation_unique = true;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.dedup_navigation_unique",
                                            true);
            }
            printf("Unique entries per navigation session enabled\n");
            return 0;
        } else if (strcmp(val, "off") == 0) {
            config.lle_dedup_navigation_unique = false;
            if (config_registry_is_initialized()) {
                config_registry_set_boolean("lle.dedup_navigation_unique",
                                            false);
            }
            printf("Unique entries per navigation session disabled\n");
            return 0;
        } else {
            fprintf(stderr, "Invalid value '%s'. Use: on, off\n", val);
            return 1;
        }

    } else {
        fprintf(stderr, "Unknown history command '%s'\n", hist_subcmd);
        fprintf(stderr, "Use 'display lle history' for available commands\n");
        return 1;
    }
}
