/**
 * @file display/lle_diagnostics.c
 * @brief `display lle diagnostics` -- comprehensive LLE diagnostics dump
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "config.h"
#include "lle/lle_editor.h"
#include "lle/lle_safety.h"
#include "lle/lle_shell_integration.h"
#include "lle/lle_watchdog.h"

int display_lle_diagnostics(int argc, char **argv) {
    // Diagnostics dump takes no arguments.
    (void)argc;
    (void)argv;

    // Show LLE diagnostics
    lle_editor_t *editor = lle_get_global_editor();

    printf("LLE Diagnostics\n");
    printf("===============\n");

    printf("\nSystem Status:\n");
    printf("  Line Editor: LLE (Lush Line Editor)\n");
    printf("  Global editor: %s\n", editor ? "initialized" : "not initialized");

    if (editor) {
        printf("\nSubsystems:\n");
        /* Buffer and keybindings are session-scoped - created during
         * readline and cleaned up after. They're not "MISSING", just
         * inactive between prompts. */
        printf("  Buffer: %s\n", editor->buffer ? "OK" : "OK (session-scoped)");
        printf("  History: %s\n", editor->history_system ? "OK" : "MISSING");
        printf("  Keybindings: %s\n",
               editor->keybinding_manager ? "OK" : "OK (session-scoped)");
        printf("  Kill ring: %s\n", editor->kill_ring ? "OK" : "MISSING");
        printf("  Change tracker: %s\n",
               editor->change_tracker ? "OK" : "MISSING");
        printf("  Cursor manager: %s\n",
               editor->cursor_manager ? "OK" : "MISSING");

        if (editor->history_system) {
            size_t count = 0;
            lle_history_get_entry_count(editor->history_system, &count);
            printf("\nHistory:\n");
            printf("  Entries loaded: %zu\n", count);
        }

        if (editor->keybinding_manager) {
            size_t kb_count = 0;
            lle_keybinding_manager_get_count(editor->keybinding_manager,
                                             &kb_count);
            printf("\nKeybindings:\n");
            printf("  Bindings registered: %zu\n", kb_count);

            uint64_t avg_us = 0, max_us = 0;
            if (lle_keybinding_manager_get_stats(editor->keybinding_manager,
                                                 &avg_us,
                                                 &max_us) == LLE_SUCCESS) {
                printf("  Avg lookup time: %lu µs\n", (unsigned long)avg_us);
                printf("  Max lookup time: %lu µs\n", (unsigned long)max_us);
                printf("  Performance: %s\n",
                       max_us < 50 ? "OK (<50µs)" : "SLOW (>50µs)");
            }
        }
    }

    printf("\nFeature Configuration:\n");
    printf("  Autosuggestions: %s\n",
           config.display_autosuggestions ? "enabled" : "disabled");
    printf("  Syntax highlighting: %s\n",
           config.display_syntax_highlighting ? "enabled" : "disabled");
    printf("  Transient prompts: %s\n",
           config.display_transient_prompt ? "enabled" : "disabled");
    printf("  Multiline editing: %s\n",
           config.lle_enable_multiline_editing ? "enabled" : "disabled");
    printf("  History deduplication: %s\n",
           config.lle_enable_deduplication ? "enabled" : "disabled");
    printf("  Interactive search: %s\n",
           config.lle_enable_interactive_search ? "enabled" : "disabled");

    printf("\nHealth: ");
    if (!editor) {
        printf("ERROR (editor not initialized)\n");
    } else if (!editor->history_system) {
        /* Only check persistent subsystems - buffer and keybindings
         * are session-scoped and intentionally NULL between prompts */
        printf("DEGRADED (missing persistent subsystems)\n");
    } else {
        printf("OK\n");
    }

    // Watchdog Statistics
    printf("\nWatchdog (Deadlock Detection):\n");
    lle_watchdog_stats_t wd_stats;
    if (lle_watchdog_get_stats(&wd_stats) == LLE_SUCCESS) {
        printf("  Timer resets (pets): %u\n", wd_stats.total_pets);
        printf("  Timeouts fired: %u\n", wd_stats.total_fires);
        printf("  Successful recoveries: %u\n", wd_stats.total_recoveries);
        if (wd_stats.total_fires > 0) {
            double recovery_rate = (double)wd_stats.total_recoveries /
                                   wd_stats.total_fires * 100.0;
            printf("  Recovery rate: %.1f%%\n", recovery_rate);
        }
        printf("  Currently armed: %s\n",
               lle_watchdog_is_armed() ? "yes" : "no");
    } else {
        printf("  Status: not initialized\n");
    }

    // Safety System Statistics
    printf("\nSafety System (Panic Recovery):\n");
    printf("  %s\n", lle_safety_get_stats_summary());
    printf("  Init state: %s\n", lle_safety_get_init_state_summary());
    printf("  Recovery mode: %s\n",
           lle_safety_is_recovery_mode() ? "ACTIVE" : "inactive");

    // Shell Event Hub Statistics
    printf("\nShell Event Hub:\n");
    uint64_t events_fired = 0, dir_changes = 0, commands = 0;
    lle_safety_get_event_stats(&events_fired, &dir_changes, &commands);
    printf("  Total events fired: %llu\n", (unsigned long long)events_fired);
    printf("  Directory changes: %llu\n", (unsigned long long)dir_changes);
    printf("  Commands executed: %llu\n", (unsigned long long)commands);

    return 0;
}
