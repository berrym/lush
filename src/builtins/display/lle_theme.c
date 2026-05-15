/**
 * @file display/lle_theme.c
 * @brief `display lle theme` -- prompt theme configuration
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/composer.h"
#include "lle/prompt/theme.h"
#include "lle/prompt/theme_loader.h"

int display_lle_theme(int argc, char **argv) {
    /* LLE prompt theme control */
    if (!g_lle_integration || !g_lle_integration->prompt_composer) {
        fprintf(stderr,
                "display lle theme: LLE prompt system not initialized\n");
        fprintf(stderr, "Run 'display lle enable' first\n");
        return 1;
    }

    lle_theme_registry_t *themes = g_lle_integration->prompt_composer->themes;
    if (!themes) {
        fprintf(stderr, "display lle theme: Theme registry not available\n");
        return 1;
    }

    /* No subcommand - show current theme and usage */
    if (argc < 2) {
        const lle_theme_t *active = lle_theme_registry_get_active(themes);
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

    const char *theme_subcmd = argv[1];

    if (strcmp(theme_subcmd, "list") == 0) {
        /* List all available themes */
        printf("Available LLE Prompt Themes:\n\n");
        const lle_theme_t *active = lle_theme_registry_get_active(themes);

        for (size_t i = 0; i < themes->count; i++) {
            const lle_theme_t *t = themes->themes[i];
            if (t) {
                const char *marker =
                    (active && strcmp(active->name, t->name) == 0) ? "*" : " ";
                printf("  %s %-12s - %s\n", marker, t->name,
                       t->description[0] ? t->description : "(no description)");
            }
        }
        printf("\n  * = currently active\n");
        printf("\nUse 'display lle theme set <name>' to change theme\n");
        return 0;

    } else if (strcmp(theme_subcmd, "set") == 0) {
        /* Set active theme */
        if (argc < 3) {
            fprintf(stderr, "display lle theme set: Missing theme name\n");
            fprintf(stderr, "Usage: display lle theme set <name>\n");
            fprintf(stderr, "Use 'display lle theme list' to see "
                            "available themes\n");
            return 1;
        }

        const char *theme_name = argv[2];
        /* Use lle_composer_set_theme to properly clear cached templates
         */
        lle_result_t result = lle_composer_set_theme(
            g_lle_integration->prompt_composer, theme_name);

        if (result == LLE_SUCCESS) {
            /* Sync PS1/PS2 from the new theme */
            const lle_theme_t *new_theme =
                lle_composer_get_theme(g_lle_integration->prompt_composer);
            if (new_theme) {
                if (new_theme->layout.style != LLE_PROMPT_STYLE_POWERLINE) {
                    /* Plain themes: write template to PS1 */
                    if (strlen(new_theme->layout.ps1_format) > 0) {
                        symtable_set_global("PS1",
                                            new_theme->layout.ps1_format);
                    }
                }
                if (strlen(new_theme->layout.ps2_format) > 0) {
                    symtable_set_global("PS2", new_theme->layout.ps2_format);
                }
            }
            printf("LLE theme set to '%s'\n", theme_name);
            return 0;
        } else if (result == LLE_ERROR_NOT_FOUND) {
            fprintf(stderr, "display lle theme set: Theme '%s' not found\n",
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
        if (lle_theme_get_user_dir(user_dir, sizeof(user_dir)) == LLE_SUCCESS) {
            printf("User theme directory: %s\n", user_dir);
        }
        printf("System theme directory: %s\n", LLE_THEME_SYSTEM_DIR);
        return 0;

    } else if (strcmp(theme_subcmd, "export") == 0) {
        /* Export theme to TOML format */
        if (argc < 3) {
            fprintf(stderr, "display lle theme export: Missing theme name\n");
            fprintf(stderr, "Usage: display lle theme export <name> [file]\n");
            return 1;
        }

        const char *theme_name = argv[2];
        const lle_theme_t *theme = lle_theme_registry_find(themes, theme_name);
        if (!theme) {
            fprintf(stderr, "display lle theme export: Theme '%s' not found\n",
                    theme_name);
            fprintf(stderr, "Use 'display lle theme list' to see "
                            "available themes\n");
            return 1;
        }

        if (argc >= 4) {
            /* Export to file */
            const char *filepath = argv[3];
            lle_result_t result = lle_theme_export_to_file(theme, filepath);
            if (result == LLE_SUCCESS) {
                printf("Theme '%s' exported to '%s'\n", theme_name, filepath);
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
                fprintf(stderr, "display lle theme export: Out of memory\n");
                return 1;
            }
            size_t len = lle_theme_export_to_toml(theme, buffer,
                                                  LLE_THEME_FILE_MAX_SIZE);
            if (len > 0) {
                printf("%s", buffer);
            }
            free(buffer);
            return 0;
        }

    } else {
        fprintf(stderr, "display lle theme: Unknown subcommand '%s'\n",
                theme_subcmd);
        fprintf(stderr, "Usage: display lle theme [list|set|reload|export]\n");
        return 1;
    }
}
