/**
 * @file display/lle_multiline.c
 * @brief `display lle multiline` -- multiline editing on/off
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"

int display_lle_multiline(int argc, char **argv) {
    // Control multiline editing
    if (argc < 2) {
        printf("Multiline editing: %s\n",
               config.lle_enable_multiline_editing ? "enabled" : "disabled");
        printf("Usage: display lle multiline on|off\n");
        return 0;
    }

    const char *state = argv[1];
    if (strcmp(state, "on") == 0) {
        config.lle_enable_multiline_editing = true;
        printf("Multiline editing enabled\n");
        return 0;
    } else if (strcmp(state, "off") == 0) {
        config.lle_enable_multiline_editing = false;
        printf("Multiline editing disabled\n");
        return 0;
    } else {
        fprintf(stderr,
                "display lle multiline: Invalid option '%s' (use 'on' "
                "or 'off')\n",
                state);
        return 1;
    }
}
