/**
 * @file display/lle_autosuggestions.c
 * @brief `display lle autosuggestions` -- autosuggestions on/off
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "display_integration.h"

int display_lle_autosuggestions(int argc, char **argv) {
    // Control autosuggestions
    if (argc < 2) {
        printf("Autosuggestions: %s\n",
               config.display_autosuggestions ? "enabled" : "disabled");
        printf("Usage: display lle autosuggestions on|off\n");
        return 0;
    }

    const char *state = argv[1];
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
}
