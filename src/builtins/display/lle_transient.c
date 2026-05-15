/**
 * @file display/lle_transient.c
 * @brief `display lle transient` -- transient prompts on/off
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
#include "lle/prompt/composer.h"

int display_lle_transient(int argc, char **argv) {
    /* Control transient prompts (Spec 25 Section 12) */
    if (argc < 2) {
        printf("Transient prompts: %s\n",
               config.display_transient_prompt ? "enabled" : "disabled");
        printf("Usage: display lle transient on|off\n");
        printf("\nTransient prompts simplify previous prompts in "
               "scrollback,\n");
        printf("reducing visual clutter from fancy multi-line prompts.\n");
        return 0;
    }

    const char *state = argv[1];
    if (strcmp(state, "on") == 0) {
        config.display_transient_prompt = true;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.transient_prompt", true);
        }
        /* Also update composer config if available */
        if (g_lle_integration && g_lle_integration->prompt_composer) {
            g_lle_integration->prompt_composer->config.enable_transient = true;
        }
        printf("Transient prompts enabled\n");
        return 0;
    } else if (strcmp(state, "off") == 0) {
        config.display_transient_prompt = false;
        if (config_registry_is_initialized()) {
            config_registry_set_boolean("display.transient_prompt", false);
        }
        /* Also update composer config if available */
        if (g_lle_integration && g_lle_integration->prompt_composer) {
            g_lle_integration->prompt_composer->config.enable_transient = false;
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
}
