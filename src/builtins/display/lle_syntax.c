/**
 * @file display/lle_syntax.c
 * @brief `display lle syntax` -- syntax highlighting on/off
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "display_integration.h"

int display_lle_syntax(int argc, char **argv) {
    /* Control syntax highlighting */
    if (argc < 2) {
        printf("Syntax highlighting: %s\n",
               config.display_syntax_highlighting ? "enabled" : "disabled");
        printf("Usage: display lle syntax on|off\n");
        return 0;
    }

    const char *state = argv[1];
    if (strcmp(state, "on") == 0) {
        config.display_syntax_highlighting = true;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.syntax_highlighting", true);
        }
        /* Apply to runtime: update the command layer */
        display_controller_t *dc = display_integration_get_controller();
        if (dc && dc->compositor && dc->compositor->command_layer) {
            command_layer_set_syntax_enabled(dc->compositor->command_layer,
                                             true);
        }
        printf("Syntax highlighting enabled\n");
        return 0;
    } else if (strcmp(state, "off") == 0) {
        config.display_syntax_highlighting = false;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.syntax_highlighting", false);
        }
        /* Apply to runtime: update the command layer */
        display_controller_t *dc = display_integration_get_controller();
        if (dc && dc->compositor && dc->compositor->command_layer) {
            command_layer_set_syntax_enabled(dc->compositor->command_layer,
                                             false);
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
}
