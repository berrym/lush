/**
 * @file display/lle_status.c
 * @brief `display lle status` -- LLE status and configuration summary
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "lle/history.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"

int display_lle_status(int argc, char **argv) {
    (void)argc;
    (void)argv;
    lle_editor_t *editor = lle_get_global_editor();

    printf("LLE Status:\n");
    printf("  Line Editor: LLE (Lush Line Editor)\n");
    printf("  History file: ~/.lush_history\n");
    printf("  Editor: %s\n", editor ? "initialized" : "not initialized");

    printf("\nLLE Features:\n");
    printf("  Multi-line editing: %s\n",
           config.lle_enable_multiline_editing ? "enabled" : "disabled");
    printf("  History deduplication: %s\n",
           config.lle_enable_deduplication ? "enabled" : "disabled");
    printf("  Forensic tracking: %s\n",
           config.lle_enable_forensic_tracking ? "enabled" : "disabled");

    if (editor && editor->history_system) {
        size_t count = 0;
        lle_history_get_entry_count(editor->history_system, &count);
        printf("\nHistory:\n");
        printf("  Entries: %zu\n", count);
    }
    return 0;
}
