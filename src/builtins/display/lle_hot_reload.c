/**
 * @file display/lle_hot_reload.c
 * @brief `display lle hot-reload` -- theme hot-reload on/off
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "display_integration.h"
#include "lle/lle_shell_integration.h"

int display_lle_hot_reload(int argc, char **argv) {
    /* Control theme hot-reload (auto-reload on file change) */
    if (argc < 2) {
        printf("Theme hot-reload: %s\n",
               config.display_theme_hot_reload ? "enabled" : "disabled");
        printf("Usage: display lle hot-reload on|off\n");
        printf("\nAutomatically reloads the active theme when its\n");
        printf("TOML file is modified on disk.\n");
        return 0;
    }

    const char *state = argv[1];
    if (strcmp(state, "on") == 0) {
        config.display_theme_hot_reload = true;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.theme_hot_reload", true);
        }
        printf("Theme hot-reload enabled\n");
        return 0;
    } else if (strcmp(state, "off") == 0) {
        config.display_theme_hot_reload = false;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.theme_hot_reload", false);
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
}
