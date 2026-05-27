/**
 * @file display/lle_pager.c
 * @brief `display lle pager` -- LLE pager configuration surface
 *
 * Supports the following subcommands:
 *
 *   display lle pager           -- show current settings (status form)
 *   display lle pager status    -- explicit status alias
 *   display lle pager on        -- enable the master switch
 *   display lle pager off       -- disable the master switch
 *   display lle pager min-lines N
 *                               -- set the pagination threshold in
 *                                  visual rows; 0 restores the
 *                                  terminal-rows default
 *   display lle pager wrap-search on|off
 *                               -- toggle the less-style "wrap to
 *                                  top on no-match" search behaviour
 *
 * All operations route through the central config registry so the
 * same setting can be flipped via setopt or by editing lushrc.toml.
 *
 * The design doc (docs/development/LLE_PAGER_DESIGN.md section 10)
 * also lists show_lineno and help_key keys; those still wait on
 * their consumers (render-side line numbering, configurable key
 * dispatch).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins/display.h"
#include "config.h"
#include "config_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_status(void) {
    printf("LLE pager: %s\n",
           config.display_lle_pager_enabled ? "enabled" : "disabled");
    if (config.display_lle_pager_min_lines > 0) {
        printf("Min lines: %d (manual threshold)\n",
               config.display_lle_pager_min_lines);
    } else {
        printf("Min lines: 0 (use terminal rows - 1)\n");
    }
    printf("Wrap search: %s\n",
           config.display_lle_pager_wrap_search ? "on" : "off");
}

static int set_enabled(bool value) {
    config.display_lle_pager_enabled = value;
    config_registry_set_boolean("display.lle.pager.enabled", value);
    printf("LLE pager %s\n", value ? "enabled" : "disabled");
    return 0;
}

static int set_min_lines(const char *arg) {
    if (!arg) {
        fprintf(stderr, "display lle pager min-lines: missing value\n");
        return 1;
    }
    char *endptr = NULL;
    long parsed = strtol(arg, &endptr, 10);
    if (!endptr || *endptr != '\0' || parsed < 0 || parsed > 100000) {
        fprintf(stderr,
                "display lle pager min-lines: invalid value '%s' "
                "(expect a non-negative integer)\n",
                arg);
        return 1;
    }
    int value = (int)parsed;
    config.display_lle_pager_min_lines = value;
    config_registry_set_integer("display.lle.pager.min_lines", value);
    if (value == 0) {
        printf("LLE pager min-lines reset to terminal-rows default\n");
    } else {
        printf("LLE pager min-lines set to %d\n", value);
    }
    return 0;
}

static int set_wrap_search(const char *arg) {
    if (!arg) {
        fprintf(stderr, "display lle pager wrap-search: missing value "
                        "(expect on or off)\n");
        return 1;
    }
    bool value;
    if (strcmp(arg, "on") == 0) {
        value = true;
    } else if (strcmp(arg, "off") == 0) {
        value = false;
    } else {
        fprintf(stderr,
                "display lle pager wrap-search: invalid value '%s' "
                "(expect on or off)\n",
                arg);
        return 1;
    }
    config.display_lle_pager_wrap_search = value;
    config_registry_set_boolean("display.lle.pager.wrap_search", value);
    printf("LLE pager wrap-search %s\n", value ? "on" : "off");
    return 0;
}

int display_lle_pager(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        print_status();
        if (argc < 2) {
            printf("Usage: display lle pager "
                   "on|off|status|min-lines N|wrap-search on|off\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        return set_enabled(true);
    }
    if (strcmp(argv[1], "off") == 0) {
        return set_enabled(false);
    }
    if (strcmp(argv[1], "min-lines") == 0) {
        return set_min_lines(argc >= 3 ? argv[2] : NULL);
    }
    if (strcmp(argv[1], "wrap-search") == 0) {
        return set_wrap_search(argc >= 3 ? argv[2] : NULL);
    }
    fprintf(stderr,
            "display lle pager: unknown subcommand '%s' "
            "(expect on|off|status|min-lines|wrap-search)\n",
            argv[1]);
    return 1;
}
