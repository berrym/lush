/**
 * @file display/lle_keybindings.c
 * @brief `display lle keybindings` -- LLE keybinding inspection and reload
 *
 * Lists all currently-bound keys with their actions, reloads user
 * keybinding config, prints help.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "lle/keybinding.h"
#include "lle/keybinding_config.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"

int display_lle_keybindings(int argc, char **argv) {
    /* Keybinding management commands */
    lle_editor_t *editor = lle_get_global_editor();

    /* Check for subcommand */
    const char *kb_subcmd = (argc >= 2) ? argv[1] : "list";

    if (strcmp(kb_subcmd, "reload") == 0) {
        /* Reload user keybindings from config file */
        if (!editor || !editor->keybinding_manager) {
            fprintf(stderr, "display lle keybindings reload: LLE not active\n");
            fprintf(stderr, "Run 'display lle enable' first\n");
            return 1;
        }

        printf("Reloading keybindings from "
               "~/.config/lush/keybindings.toml...\n");
        lle_keybinding_load_result_t load_result;
        lle_result_t result = lle_keybinding_reload_user_config(
            editor->keybinding_manager, &load_result);

        if (result == LLE_SUCCESS) {
            printf("Keybindings reloaded: %zu bindings applied, %zu "
                   "errors\n",
                   load_result.bindings_applied, load_result.errors_count);
            if (load_result.errors_count > 0) {
                printf("(Check stderr for error details)\n");
            }
            return 0;
        } else if (result == LLE_ERROR_NOT_FOUND) {
            printf("No keybindings config file found at "
                   "~/.config/lush/keybindings.toml\n");
            printf("Create this file to customize keybindings.\n");
            printf("\nExample format:\n");
            printf("  [bindings]\n");
            printf("  \"C-a\" = \"end-of-line\"      # Swap C-a and "
                   "C-e\n");
            printf("  \"C-e\" = \"beginning-of-line\"\n");
            printf("  \"C-s\" = \"none\"             # Unbind a key\n");
            return 0;
        } else {
            fprintf(stderr,
                    "display lle keybindings reload: Failed (error %d)\n",
                    result);
            return 1;
        }

    } else if (strcmp(kb_subcmd, "actions") == 0) {
        /* List all available action names */
        printf("LLE Available Actions\n");
        printf("=====================\n");
        printf("\nThese action names can be used in "
               "~/.config/lush/keybindings.toml\n\n");

        const lle_action_registry_entry_t *entry;
        size_t index = 0;

        printf("Movement:\n");
        while ((entry = lle_action_registry_get_by_index(index++)) != NULL) {
            if (strstr(entry->name, "beginning") ||
                strstr(entry->name, "end") || strstr(entry->name, "forward") ||
                strstr(entry->name, "backward")) {
                printf("  %-30s  %s\n", entry->name,
                       entry->description ? entry->description : "");
            }
        }

        index = 0;
        printf("\nEditing:\n");
        while ((entry = lle_action_registry_get_by_index(index++)) != NULL) {
            if (strstr(entry->name, "delete") || strstr(entry->name, "kill") ||
                strstr(entry->name, "yank") || strstr(entry->name, "undo") ||
                strstr(entry->name, "redo") ||
                strstr(entry->name, "transpose") ||
                strstr(entry->name, "case") || strstr(entry->name, "upcase") ||
                strstr(entry->name, "downcase") ||
                strstr(entry->name, "capitalize")) {
                printf("  %-30s  %s\n", entry->name,
                       entry->description ? entry->description : "");
            }
        }

        index = 0;
        printf("\nHistory:\n");
        while ((entry = lle_action_registry_get_by_index(index++)) != NULL) {
            if (strstr(entry->name, "history") ||
                strstr(entry->name, "search")) {
                printf("  %-30s  %s\n", entry->name,
                       entry->description ? entry->description : "");
            }
        }

        index = 0;
        printf("\nCompletion:\n");
        while ((entry = lle_action_registry_get_by_index(index++)) != NULL) {
            if (strstr(entry->name, "complet")) {
                printf("  %-30s  %s\n", entry->name,
                       entry->description ? entry->description : "");
            }
        }

        index = 0;
        printf("\nOther:\n");
        while ((entry = lle_action_registry_get_by_index(index++)) != NULL) {
            if (strstr(entry->name, "accept") || strstr(entry->name, "abort") ||
                strstr(entry->name, "clear") || strstr(entry->name, "quoted") ||
                strstr(entry->name, "tab") || strstr(entry->name, "newline") ||
                strstr(entry->name, "eof") || strstr(entry->name, "none")) {
                printf("  %-30s  %s\n", entry->name,
                       entry->description ? entry->description : "");
            }
        }

        printf("\nSpecial:\n");
        printf("  %-30s  %s\n", "none", "Unbind a key (remove action)");

        return 0;

    } else if (strcmp(kb_subcmd, "list") == 0 ||
               strcmp(kb_subcmd, "help") == 0 || kb_subcmd[0] == '-') {
        /* Show help if --help or just 'list' with no bindings to show
         */
        if (strcmp(kb_subcmd, "help") == 0 ||
            strcmp(kb_subcmd, "--help") == 0) {
            printf("LLE Keybinding Commands\n");
            printf("=======================\n\n");
            printf("Usage: display lle keybindings [command]\n\n");
            printf("Commands:\n");
            printf("  list     - Show active keybindings (default)\n");
            printf("  reload   - Reload keybindings from config file\n");
            printf("  actions  - List all available action names\n");
            printf("  help     - Show this help message\n");
            printf("\nConfig file: ~/.config/lush/keybindings.toml\n");
            printf("\nExample config:\n");
            printf("  [bindings]\n");
            printf("  \"C-a\" = \"end-of-line\"\n");
            printf("  \"M-p\" = \"history-search-backward\"\n");
            printf("  \"C-s\" = \"none\"  # unbind\n");
            return 0;
        }
    }

    /* Default: list active keybindings */
    printf("LLE Active Keybindings (Emacs mode)\n");
    printf("====================================\n");

    if (editor && editor->keybinding_manager) {
        lle_keybinding_info_t *bindings = NULL;
        size_t count = 0;

        if (lle_keybinding_manager_list_bindings(
                editor->keybinding_manager, &bindings, &count) == LLE_SUCCESS) {
            printf("\nNavigation:\n");
            for (size_t i = 0; i < count; i++) {
                const char *name = bindings[i].function_name
                                       ? bindings[i].function_name
                                       : "unknown";
                if (strstr(name, "beginning") || strstr(name, "end") ||
                    strstr(name, "forward") || strstr(name, "backward") ||
                    strstr(name, "left") || strstr(name, "right") ||
                    strstr(name, "up") || strstr(name, "down")) {
                    printf("  %-12s  %s\n", bindings[i].key_sequence, name);
                }
            }

            printf("\nEditing:\n");
            for (size_t i = 0; i < count; i++) {
                const char *name = bindings[i].function_name
                                       ? bindings[i].function_name
                                       : "unknown";
                if (strstr(name, "delete") || strstr(name, "kill") ||
                    strstr(name, "yank") || strstr(name, "undo") ||
                    strstr(name, "redo") || strstr(name, "transpose")) {
                    printf("  %-12s  %s\n", bindings[i].key_sequence, name);
                }
            }

            printf("\nHistory:\n");
            for (size_t i = 0; i < count; i++) {
                const char *name = bindings[i].function_name
                                       ? bindings[i].function_name
                                       : "unknown";
                if (strstr(name, "history") || strstr(name, "search") ||
                    strstr(name, "previous") || strstr(name, "next")) {
                    printf("  %-12s  %s\n", bindings[i].key_sequence, name);
                }
            }

            printf("\nOther:\n");
            for (size_t i = 0; i < count; i++) {
                const char *name = bindings[i].function_name
                                       ? bindings[i].function_name
                                       : "unknown";
                if (strstr(name, "accept") || strstr(name, "abort") ||
                    strstr(name, "clear") || strstr(name, "complete")) {
                    printf("  %-12s  %s\n", bindings[i].key_sequence, name);
                }
            }

            printf("\nTotal: %zu keybindings\n", count);
        } else {
            printf("  (Unable to retrieve keybindings)\n");
        }
    } else {
        /* Show default keybindings when LLE not active */
        printf("\nNavigation:\n");
        printf("  C-a          beginning-of-line\n");
        printf("  C-e          end-of-line\n");
        printf("  C-f / RIGHT  forward-char\n");
        printf("  C-b / LEFT   backward-char\n");
        printf("  M-f          forward-word\n");
        printf("  M-b          backward-word\n");
        printf("\nEditing:\n");
        printf("  C-d / DEL    delete-char\n");
        printf("  BACKSPACE    backward-delete-char\n");
        printf("  C-k          kill-line\n");
        printf("  C-u          unix-line-discard\n");
        printf("  C-w          backward-kill-word\n");
        printf("  C-y          yank\n");
        printf("  C-_          undo\n");
        printf("  C-^          redo\n");
        printf("\nHistory:\n");
        printf("  C-p / UP     previous-history\n");
        printf("  C-n / DOWN   next-history\n");
        printf("  C-r          reverse-search-history\n");
        printf("\nOther:\n");
        printf("  RET          accept-line\n");
        printf("  C-c          abort\n");
        printf("  C-l          clear-screen\n");
        printf("  TAB          complete\n");
    }
    return 0;
}
