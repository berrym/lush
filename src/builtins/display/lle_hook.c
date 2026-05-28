/**
 * @file display/lle_hook.c
 * @brief `display lle hook` -- attach widgets to LLE/shell lifecycle hooks
 *
 * User surface for the widget-hooks-manager. Registers an existing
 * widget (builtin or user-defined via `display lle widget add`) to
 * fire when an LLE editing-lifecycle event or shell lifecycle event
 * happens. The shell <-> LLE bridge installed at editor init makes
 * PRE_COMMAND / POST_COMMAND hooks fire alongside the shell's
 * lifecycle events.
 *
 * Subcommands:
 *   list                  -- show all hook->widget registrations
 *   add HOOK WIDGET       -- attach WIDGET to HOOK
 *   remove HOOK WIDGET    -- detach WIDGET from HOOK
 *   help                  -- print usage
 *
 * Hook names (ZSH-inspired, lowercase-with-hyphens):
 *   line-init       LLE_HOOK_LINE_INIT
 *   line-accepted   LLE_HOOK_LINE_ACCEPTED
 *   line-finish     LLE_HOOK_LINE_FINISH
 *   buffer-modified LLE_HOOK_BUFFER_MODIFIED
 *   pre-command     LLE_HOOK_PRE_COMMAND
 *   post-command    LLE_HOOK_POST_COMMAND
 *   completion-start LLE_HOOK_COMPLETION_START
 *   completion-end  LLE_HOOK_COMPLETION_END
 *   history-search  LLE_HOOK_HISTORY_SEARCH
 *   terminal-resize LLE_HOOK_TERMINAL_RESIZE
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins/display.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"
#include "lle/widget_hooks.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    lle_widget_hook_type_t type;
} hook_name_entry_t;

static const hook_name_entry_t k_hook_names[] = {
    {       "line-init",        LLE_HOOK_LINE_INIT},
    {   "line-accepted",    LLE_HOOK_LINE_ACCEPTED},
    {     "line-finish",      LLE_HOOK_LINE_FINISH},
    { "buffer-modified",  LLE_HOOK_BUFFER_MODIFIED},
    {     "pre-command",      LLE_HOOK_PRE_COMMAND},
    {    "post-command",     LLE_HOOK_POST_COMMAND},
    {"completion-start", LLE_HOOK_COMPLETION_START},
    {  "completion-end",   LLE_HOOK_COMPLETION_END},
    {  "history-search",   LLE_HOOK_HISTORY_SEARCH},
    { "terminal-resize",  LLE_HOOK_TERMINAL_RESIZE},
};
static const size_t k_hook_name_count =
    sizeof(k_hook_names) / sizeof(k_hook_names[0]);

static bool parse_hook_name(const char *name, lle_widget_hook_type_t *out) {
    for (size_t i = 0; i < k_hook_name_count; i++) {
        if (strcmp(k_hook_names[i].name, name) == 0) {
            *out = k_hook_names[i].type;
            return true;
        }
    }
    return false;
}

static const char *hook_type_to_name(lle_widget_hook_type_t t) {
    for (size_t i = 0; i < k_hook_name_count; i++) {
        if (k_hook_names[i].type == t) {
            return k_hook_names[i].name;
        }
    }
    return "?";
}

static void print_usage(void) {
    printf("display lle hook -- attach widgets to lifecycle hooks\n");
    printf("\n");
    printf("Usage:\n");
    printf("  display lle hook list                 - show all attachments\n");
    printf("  display lle hook add HOOK WIDGET      - attach WIDGET to HOOK\n");
    printf(
        "  display lle hook remove HOOK WIDGET   - detach WIDGET from HOOK\n");
    printf("  display lle hook help                 - this message\n");
    printf("\n");
    printf("Hooks:\n");
    for (size_t i = 0; i < k_hook_name_count; i++) {
        printf("  %s\n", k_hook_names[i].name);
    }
    printf("\n");
    printf("Example:\n");
    printf("  display lle widget add show-time 'date'\n");
    printf("  display lle hook add post-command show-time\n");
}

static int do_list(lle_widget_hooks_manager_t *manager) {
    if (!manager) {
        fprintf(stderr, "display lle hook list: LLE not active\n");
        return 1;
    }
    printf("%-18s %-22s %s\n", "HOOK", "WIDGET", "TRIGGERS");
    printf("%-18s %-22s %s\n", "----", "------", "--------");
    for (int t = 0; t < LLE_HOOK_COUNT; t++) {
        for (lle_hook_registration_t *r = manager->hooks[t]; r; r = r->next) {
            const char *name =
                (r->widget && r->widget->name) ? r->widget->name : "(unknown)";
            printf("%-18s %-22s %llu\n",
                   hook_type_to_name((lle_widget_hook_type_t)t), name,
                   (unsigned long long)r->trigger_count);
        }
    }
    return 0;
}

static int do_add(lle_widget_hooks_manager_t *manager, const char *hook,
                  const char *widget_name) {
    if (!manager) {
        fprintf(stderr, "display lle hook add: LLE not active\n");
        return 1;
    }
    lle_widget_hook_type_t t;
    if (!parse_hook_name(hook, &t)) {
        fprintf(stderr, "display lle hook add: unknown hook '%s'\n", hook);
        return 1;
    }
    lle_result_t r = lle_widget_hook_register(manager, t, widget_name);
    if (r != LLE_SUCCESS) {
        fprintf(stderr,
                "display lle hook add: register failed for '%s' on '%s' (%d)\n",
                widget_name, hook, (int)r);
        return 1;
    }
    printf("attached widget '%s' to hook '%s'\n", widget_name, hook);
    return 0;
}

static int do_remove(lle_widget_hooks_manager_t *manager, const char *hook,
                     const char *widget_name) {
    if (!manager) {
        fprintf(stderr, "display lle hook remove: LLE not active\n");
        return 1;
    }
    lle_widget_hook_type_t t;
    if (!parse_hook_name(hook, &t)) {
        fprintf(stderr, "display lle hook remove: unknown hook '%s'\n", hook);
        return 1;
    }
    lle_result_t r = lle_widget_hook_unregister(manager, t, widget_name);
    if (r != LLE_SUCCESS) {
        fprintf(stderr,
                "display lle hook remove: detach failed for '%s' on "
                "'%s' (%d)\n",
                widget_name, hook, (int)r);
        return 1;
    }
    printf("detached widget '%s' from hook '%s'\n", widget_name, hook);
    return 0;
}

int display_lle_hook(int argc, char **argv) {
    lle_editor_t *editor = lle_get_global_editor();
    lle_widget_hooks_manager_t *manager =
        editor ? editor->widget_hooks_manager : NULL;

    const char *sub = (argc >= 2) ? argv[1] : "help";

    if (strcmp(sub, "help") == 0 || strcmp(sub, "-h") == 0 ||
        strcmp(sub, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(sub, "list") == 0) {
        return do_list(manager);
    }
    if (strcmp(sub, "add") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "display lle hook add: usage: add HOOK WIDGET_NAME\n");
            return 1;
        }
        return do_add(manager, argv[2], argv[3]);
    }
    if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0 ||
        strcmp(sub, "detach") == 0) {
        if (argc < 4) {
            fprintf(
                stderr,
                "display lle hook remove: usage: remove HOOK WIDGET_NAME\n");
            return 1;
        }
        return do_remove(manager, argv[2], argv[3]);
    }

    fprintf(stderr, "display lle hook: unknown subcommand '%s'\n", sub);
    print_usage();
    return 1;
}
