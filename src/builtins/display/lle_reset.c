/**
 * @file display/lle_reset.c
 * @brief `display lle reset` -- three-tier LLE reset (soft / display / hard)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"

int display_lle_reset(int argc, char **argv) {
    /* LLE reset commands (Spec 26: Three-tier reset hierarchy)
     * - reset        : Hard reset (destroy/recreate editor)
     * - reset --soft : Soft reset (abort current line)
     * - reset --terminal : Nuclear reset (hard + terminal reset)
     */
    if (!lle_is_active()) {
        fprintf(stderr, "display lle reset: LLE shell integration not "
                        "initialized\n");
        return 1;
    }

    /* Check for options */
    if (argc >= 2) {
        const char *opt = argv[1];
        if (strcmp(opt, "--soft") == 0) {
            /* Soft reset: abort current line */
            lle_soft_reset();
            printf("LLE soft reset complete (line aborted)\n");
            return 0;
        } else if (strcmp(opt, "--terminal") == 0) {
            /* Nuclear reset: hard reset + terminal reset */
            printf("Performing LLE nuclear reset...\n");
            lle_nuclear_reset();
            printf("LLE nuclear reset complete (editor recreated, "
                   "terminal reset)\n");
            return 0;
        } else {
            fprintf(stderr, "display lle reset: Unknown option '%s'\n", opt);
            fprintf(stderr, "Options: --soft, --terminal\n");
            return 1;
        }
    }

    /* Default: Hard reset (destroy and recreate editor) */
    printf("Performing LLE hard reset...\n");
    lle_hard_reset();
    printf("LLE hard reset complete (editor recreated)\n");
    return 0;
}
