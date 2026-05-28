/**
 * @file display/lle_newline_before.c
 * @brief `display lle newline-before` -- newline-before-prompt on/off
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/composer.h"

int display_lle_newline_before(int argc, char **argv) {
    // Control newline before prompt for visual separation
    if (argc < 2) {
        printf("Newline before prompt: %s\n",
               config.display_newline_before_prompt ? "enabled" : "disabled");
        printf("Usage: display lle newline-before on|off\n");
        printf("\nPrints a blank line before each prompt for visual "
               "separation\n");
        printf("between command output and the next prompt.\n");
        return 0;
    }

    const char *state = argv[1];
    if (strcmp(state, "on") == 0) {
        config.display_newline_before_prompt = true;
        if (g_lle_integration && g_lle_integration->prompt_composer) {
            g_lle_integration->prompt_composer->config.newline_before_prompt =
                true;
        }
        printf("Newline before prompt enabled\n");
        return 0;
    } else if (strcmp(state, "off") == 0) {
        config.display_newline_before_prompt = false;
        if (g_lle_integration && g_lle_integration->prompt_composer) {
            g_lle_integration->prompt_composer->config.newline_before_prompt =
                false;
        }
        printf("Newline before prompt disabled\n");
        return 0;
    } else {
        fprintf(stderr,
                "display lle newline-before: Invalid option '%s' (use 'on' "
                "or 'off')\n",
                state);
        return 1;
    }
}
