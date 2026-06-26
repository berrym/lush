/**
 * @file display/lle_completion.c
 * @brief `display lle completion` -- completion subsystem management
 *
 * Sources / chain_directories / help. Renamed in commit 35276e92 from
 * the prior plural `display lle completions` (the singular umbrella
 * groups source-management + behavior knobs).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "lle/completion/completion_state.h"
#include "lle/completion/completion_system.h"
#include "lle/completion/custom_source.h"

extern config_values_t config;

int display_lle_completion(int argc, char **argv) {
    /// LLE completion subsystem. The umbrella command groups
    /// source-list management (under `sources`) with future
    /// behavior knobs registered alongside it. Bare
    /// `display lle completion` prints the namespace help; an
    /// unrecognized subcommand is an error.
    const char *comp_subcmd = (argc >= 2) ? argv[1] : "help";

    if (strcmp(comp_subcmd, "sources") == 0) {
        const char *sources_subcmd = (argc >= 3) ? argv[2] : "list";

        if (strcmp(sources_subcmd, "list") == 0) {
            // List all completion sources
            printf("LLE Completion Sources\n");
            printf("======================\n\n");

            // Built-in sources
            printf("Built-in Sources:\n");
            size_t total = lle_completion_get_source_count();
            for (size_t i = 0; i < total; i++) {
                if (!lle_completion_source_is_custom(i)) {
                    const char *name = lle_completion_get_source_name(i);
                    printf("  - %s\n", name ? name : "(unknown)");
                }
            }

            // Custom sources
            size_t custom_count = lle_completion_get_custom_source_count();
            if (custom_count > 0) {
                printf("\nCustom Sources:\n");
                for (size_t i = 0; i < custom_count; i++) {
                    const char *name = lle_completion_get_custom_source_name(i);
                    const char *desc =
                        lle_completion_get_custom_source_description(i);
                    if (desc) {
                        printf("  - %s: %s\n", name ? name : "(unknown)", desc);
                    } else {
                        printf("  - %s\n", name ? name : "(unknown)");
                    }
                }
            } else {
                printf("\nNo custom sources registered.\n");
            }

            // Config file info
            const lle_completion_config_t *cfg = lle_completion_get_config();
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
            // Reload completion config
            printf("Reloading completion config...\n");
            lle_result_t result = lle_completion_reload_config();
            if (result == LLE_SUCCESS) {
                const lle_completion_config_t *cfg =
                    lle_completion_get_config();
                if (cfg) {
                    printf("Loaded %zu custom source(s)\n", cfg->source_count);
                } else {
                    printf("Config reloaded (no sources defined)\n");
                }
                return 0;
            } else {
                fprintf(stderr, "Failed to reload config (error %d)\n", result);
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
            printf("  applies_to = [\"git checkout\", \"git merge\"]\n");
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
        /// completion.chain_directories: when on, accepting a
        /// directory completion auto-re-triggers completion at
        /// the new prefix (fish-style cascading). Per-mode
        /// default: lush=true, others=false.
        if (argc < 3) {
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

        const char *state = argv[2];
        if (strcmp(state, "on") == 0) {
            config_registry_set_boolean("completion.chain_directories", true);
            printf("chain_directories enabled\n");
            return 0;
        } else if (strcmp(state, "off") == 0) {
            config_registry_set_boolean("completion.chain_directories", false);
            printf("chain_directories disabled\n");
            return 0;
        } else {
            fprintf(stderr,
                    "display lle completion chain_directories: "
                    "invalid value '%s' (use on|off)\n",
                    state);
            return 1;
        }

    } else if (strcmp(comp_subcmd, "match_mode") == 0) {
        /// completion.match_mode: filter predicate used by the
        /// bridge pre-emit filter and (subsequent commit) the
        /// in-menu type-to-filter. Values: prefix | substring |
        /// fuzzy. Per-mode default: lush=fuzzy, others=prefix.
        if (argc < 3) {
            const char *display = "prefix";
            switch (config.completion_match_mode) {
            case COMPLETION_MATCH_SUBSTRING:
                display = "substring";
                break;
            case COMPLETION_MATCH_FUZZY:
                display = "fuzzy";
                break;
            case COMPLETION_MATCH_PREFIX:
            default:
                display = "prefix";
                break;
            }
            printf("match_mode: %s\n", display);
            printf("Usage: display lle completion match_mode "
                   "<prefix|substring|fuzzy>\n");
            return 0;
        }

        const char *mode = argv[2];
        if (strcmp(mode, "prefix") != 0 && strcmp(mode, "substring") != 0 &&
            strcmp(mode, "fuzzy") != 0) {
            fprintf(stderr,
                    "display lle completion match_mode: "
                    "invalid value '%s' (use prefix|substring|fuzzy)\n",
                    mode);
            return 1;
        }
        /// config_set_value validates and writes through the
        /// canonical config_options storage pointer; it also prints
        /// "Set <key> = <value>" on success, which is the
        /// project-wide feedback line, so no extra echo here.
        config_set_value("completion.match_mode", mode);
        return 0;

    } else if (strcmp(comp_subcmd, "enabled") == 0) {
        /// completion.enabled: master switch for tab completion.
        if (argc < 3) {
            bool cur = true;
            if (config_registry_get_boolean("completion.enabled", &cur) !=
                CREG_SUCCESS) {
                cur = true;
            }
            printf("enabled: %s\n", cur ? "on" : "off");
            printf("Usage: display lle completion enabled <on|off>\n");
            return 0;
        }
        const char *state = argv[2];
        if (strcmp(state, "on") == 0) {
            config_registry_set_boolean("completion.enabled", true);
            printf("completion enabled\n");
            return 0;
        } else if (strcmp(state, "off") == 0) {
            config_registry_set_boolean("completion.enabled", false);
            printf("completion disabled\n");
            return 0;
        }
        fprintf(stderr,
                "display lle completion enabled: invalid value '%s' "
                "(use on|off)\n",
                state);
        return 1;

    } else if (strcmp(comp_subcmd, "case_sensitive") == 0) {
        /// completion.case_sensitive: match candidates case-sensitively.
        if (argc < 3) {
            bool cur = false;
            if (config_registry_get_boolean("completion.case_sensitive",
                                            &cur) != CREG_SUCCESS) {
                cur = false;
            }
            printf("case_sensitive: %s\n", cur ? "on" : "off");
            printf("Usage: display lle completion case_sensitive <on|off>\n");
            return 0;
        }
        const char *state = argv[2];
        if (strcmp(state, "on") == 0) {
            config_registry_set_boolean("completion.case_sensitive", true);
            printf("case_sensitive enabled\n");
            return 0;
        } else if (strcmp(state, "off") == 0) {
            config_registry_set_boolean("completion.case_sensitive", false);
            printf("case_sensitive disabled\n");
            return 0;
        }
        fprintf(stderr,
                "display lle completion case_sensitive: invalid value '%s' "
                "(use on|off)\n",
                state);
        return 1;

    } else if (strcmp(comp_subcmd, "menu_shadow_ghost") == 0) {
        /// completion.menu_shadow_ghost: render the open menu's highlighted
        /// candidate inline as a faint shadow ghost.
        if (argc < 3) {
            bool cur = false;
            if (config_registry_get_boolean("completion.menu_shadow_ghost",
                                            &cur) != CREG_SUCCESS) {
                cur = false;
            }
            printf("menu_shadow_ghost: %s\n", cur ? "on" : "off");
            printf("Usage: display lle completion menu_shadow_ghost "
                   "<on|off>\n");
            return 0;
        }
        const char *state = argv[2];
        if (strcmp(state, "on") == 0) {
            config_registry_set_boolean("completion.menu_shadow_ghost", true);
            printf("menu_shadow_ghost enabled\n");
            return 0;
        } else if (strcmp(state, "off") == 0) {
            config_registry_set_boolean("completion.menu_shadow_ghost", false);
            printf("menu_shadow_ghost disabled\n");
            return 0;
        }
        fprintf(stderr,
                "display lle completion menu_shadow_ghost: invalid value "
                "'%s' (use on|off)\n",
                state);
        return 1;

    } else if (strcmp(comp_subcmd, "threshold") == 0) {
        /// completion.threshold: minimum fuzzy match score (0-100) a
        /// candidate must reach to be offered.
        if (argc < 3) {
            int64_t cur = 60;
            if (config_registry_get_integer("completion.threshold", &cur) !=
                CREG_SUCCESS) {
                cur = 60;
            }
            printf("threshold: %lld\n", (long long)cur);
            printf("Usage: display lle completion threshold <0-100>\n");
            return 0;
        }
        char *end = NULL;
        long val = strtol(argv[2], &end, 10);
        if (argv[2][0] == '\0' || *end != '\0' || val < 0 || val > 100) {
            fprintf(stderr,
                    "display lle completion threshold: invalid value '%s' "
                    "(use an integer 0-100)\n",
                    argv[2]);
            return 1;
        }
        config_registry_set_integer("completion.threshold", (int)val);
        printf("threshold set to %ld\n", val);
        return 0;

    } else if (strcmp(comp_subcmd, "help") == 0 ||
               strcmp(comp_subcmd, "--help") == 0) {
        printf("LLE Completion Subsystem\n");
        printf("========================\n\n");
        printf("Usage: display lle completion <subcommand>\n\n");
        printf("Subcommands:\n");
        printf("  sources [list|reload|help]              - Manage completion "
               "sources\n");
        printf("  chain_directories <on|off>              - Re-trigger "
               "completion after directory accept\n");
        printf("  match_mode <prefix|substring|fuzzy>     - Set the filter "
               "predicate\n");
        printf("  enabled <on|off>                        - Master switch for "
               "tab completion\n");
        printf("  case_sensitive <on|off>                 - Match candidates "
               "case-sensitively\n");
        printf("  threshold <0-100>                       - Minimum fuzzy "
               "match score\n");
        printf("  menu_shadow_ghost <on|off>              - Inline shadow "
               "ghost of the menu's top candidate\n");
        printf("  help                                    - Show this help "
               "message\n");
        return 0;

    } else {
        fprintf(stderr, "display lle completion: Unknown subcommand '%s'\n",
                comp_subcmd);
        fprintf(stderr,
                "Usage: display lle completion <sources|chain_directories|"
                "match_mode|enabled|case_sensitive|threshold|"
                "menu_shadow_ghost|help>\n");
        return 1;
    }
}
