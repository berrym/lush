/**
 * @file display/lle_widget.c
 * @brief `display lle widget` -- user-defined LLE widget registration
 *
 * Lets a user (interactively or from a startup script) register a
 * named widget whose body is a shell command. Once registered, the
 * widget participates in the LLE widget system exactly like a
 * built-in widget: keybindings can name it, hooks can invoke it,
 * and `display lle widget list` shows it among the registered set.
 *
 * Subcommands:
 *   list           -- show all widgets (builtin + user + plugin).
 *   add NAME CMD   -- register NAME as a user widget running CMD.
 *   remove NAME    -- unregister a user widget (NAME must be user).
 *   show NAME      -- show a widget's type and (for user widgets)
 *                     the registered command body.
 *   help           -- print usage.
 *
 * Ownership: lle_widget_unregister does not own widget user_data, so
 * this file maintains a side list of (widget_name -> strdup'd cmd
 * body) for the user widgets it has registered. add allocates,
 * remove frees, and a process-exit cleanup hook frees the rest.
 *
 * Persistence: the interactive surface only -- this file does not
 * load widgets from a config file. A future commit can add
 * ~/.config/lush/widgets.toml loading with the same registration
 * primitive (`register_user_widget` below); that work is out of scope
 * for the MVP.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "builtins/display.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_integration.h"
#include "lle/widget_system.h"
#include "lush.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Side list: track user-widget command bodies so we can free them
 * ============================================================================
 */

typedef struct user_widget_entry {
    char *name; ///< Widget name (strdup'd)
    char *cmd;  ///< Shell command string (strdup'd; passed as user_data)
    struct user_widget_entry *next;
} user_widget_entry_t;

static user_widget_entry_t *g_user_widgets = NULL;

static user_widget_entry_t *find_user_widget_entry(const char *name) {
    for (user_widget_entry_t *e = g_user_widgets; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static void detach_user_widget_entry(const char *name) {
    user_widget_entry_t **slot = &g_user_widgets;
    while (*slot) {
        if (strcmp((*slot)->name, name) == 0) {
            user_widget_entry_t *victim = *slot;
            *slot = victim->next;
            free(victim->name);
            free(victim->cmd);
            free(victim);
            return;
        }
        slot = &(*slot)->next;
    }
}

/* ============================================================================
 * Widget callback: run the registered shell command via parse_and_execute
 * ============================================================================
 */

/**
 * @brief Generic callback for user widgets.
 *
 * The widget's user_data holds a strdup'd shell command string. When
 * the widget fires (keybinding pressed, hook triggered, programmatic
 * invoke), this callback hands the command to the shell parser /
 * executor exactly as if the user had typed it. parse_and_execute is
 * the same public entry the REPL uses for a typed line, so positional
 * params, error handling, and exit-status semantics match a typed
 * command exactly.
 *
 * Returning LLE_SUCCESS keeps the LLE editor in a healthy state
 * regardless of the shell command's exit status -- a widget that runs
 * a failing command should not crash the line editor. The exit status
 * still updates $? via the executor as a side effect.
 *
 * @param editor    Editor context (unused here -- the widget is a
 *                  shell-side action, not a buffer manipulation).
 * @param user_data const char* pointing at the registered command.
 * @return Always LLE_SUCCESS.
 */
static lle_result_t user_widget_callback(lle_editor_t *editor,
                                         void *user_data) {
    (void)editor;
    const char *cmd = (const char *)user_data;
    if (!cmd || !*cmd) {
        return LLE_SUCCESS;
    }
    parse_and_execute(cmd, 1);
    return LLE_SUCCESS;
}

/* ============================================================================
 * Subcommand handlers
 * ============================================================================
 */

static void print_usage(void) {
    printf("display lle widget -- user-defined LLE widget registration\n");
    printf("\n");
    printf("Usage:\n");
    printf("  display lle widget list             - show all widgets\n");
    printf("  display lle widget add NAME 'CMD'   - register user widget\n");
    printf(
        "  display lle widget remove NAME      - unregister a user widget\n");
    printf("  display lle widget show NAME        - show widget details\n");
    printf("  display lle widget help             - this message\n");
    printf("\n");
    printf("Once registered, a user widget can be bound to a key in\n");
    printf("~/.config/lush/keybindings.toml exactly like any builtin\n");
    printf("widget. Example:\n");
    printf("\n");
    printf("  display lle widget add greet 'echo hello'\n");
    printf("  # then in keybindings.toml:\n");
    printf("  #   \"C-g\" = \"greet\"\n");
}

static const char *widget_type_name(lle_widget_type_t t) {
    switch (t) {
    case LLE_WIDGET_BUILTIN:
        return "builtin";
    case LLE_WIDGET_USER:
        return "user";
    case LLE_WIDGET_PLUGIN:
        return "plugin";
    }
    return "?";
}

static int do_list(lle_widget_registry_t *registry) {
    if (!registry) {
        fprintf(stderr, "display lle widget list: LLE not active\n");
        return 1;
    }
    printf("%-30s %-8s %s\n", "NAME", "TYPE", "BODY");
    printf("%-30s %-8s %s\n", "----", "----", "----");
    for (lle_widget_t *w = registry->widget_list; w; w = w->next) {
        const char *body = "(native)";
        if (w->type == LLE_WIDGET_USER) {
            user_widget_entry_t *e = find_user_widget_entry(w->name);
            body = e ? e->cmd : "(unknown)";
        }
        printf("%-30s %-8s %s\n", w->name, widget_type_name(w->type), body);
    }
    return 0;
}

static int do_show(lle_widget_registry_t *registry, const char *name) {
    if (!registry) {
        fprintf(stderr, "display lle widget show: LLE not active\n");
        return 1;
    }
    lle_widget_t *w = lle_widget_lookup(registry, name);
    if (!w) {
        fprintf(stderr, "display lle widget show: '%s' not found\n", name);
        return 1;
    }
    printf("name: %s\n", w->name);
    printf("type: %s\n", widget_type_name(w->type));
    if (w->type == LLE_WIDGET_USER) {
        user_widget_entry_t *e = find_user_widget_entry(name);
        printf("body: %s\n", e ? e->cmd : "(unknown)");
    } else {
        printf("body: (native callback)\n");
    }
    printf("invocations: %llu\n", (unsigned long long)w->execution_count);
    printf("enabled: %s\n", w->enabled ? "true" : "false");
    return 0;
}

static int do_add(lle_widget_registry_t *registry, const char *name,
                  const char *cmd) {
    if (!registry) {
        fprintf(stderr, "display lle widget add: LLE not active\n");
        return 1;
    }
    if (lle_widget_lookup(registry, name)) {
        fprintf(stderr,
                "display lle widget add: '%s' already exists; remove "
                "it first\n",
                name);
        return 1;
    }

    user_widget_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        fprintf(stderr, "display lle widget add: out of memory\n");
        return 1;
    }
    entry->name = strdup(name);
    entry->cmd = strdup(cmd);
    if (!entry->name || !entry->cmd) {
        free(entry->name);
        free(entry->cmd);
        free(entry);
        fprintf(stderr, "display lle widget add: out of memory\n");
        return 1;
    }

    lle_result_t r = lle_widget_register(registry, name, user_widget_callback,
                                         LLE_WIDGET_USER, entry->cmd);
    if (r != LLE_SUCCESS) {
        free(entry->name);
        free(entry->cmd);
        free(entry);
        fprintf(stderr, "display lle widget add: register failed (%d)\n",
                (int)r);
        return 1;
    }

    entry->next = g_user_widgets;
    g_user_widgets = entry;

    printf("registered user widget '%s'\n", name);
    return 0;
}

static int do_remove(lle_widget_registry_t *registry, const char *name) {
    if (!registry) {
        fprintf(stderr, "display lle widget remove: LLE not active\n");
        return 1;
    }
    lle_widget_t *w = lle_widget_lookup(registry, name);
    if (!w) {
        fprintf(stderr, "display lle widget remove: '%s' not found\n", name);
        return 1;
    }
    if (w->type != LLE_WIDGET_USER) {
        fprintf(stderr,
                "display lle widget remove: '%s' is a %s widget, "
                "not removable\n",
                name, widget_type_name(w->type));
        return 1;
    }

    lle_result_t r = lle_widget_unregister(registry, name);
    if (r != LLE_SUCCESS) {
        fprintf(stderr, "display lle widget remove: unregister failed (%d)\n",
                (int)r);
        return 1;
    }
    detach_user_widget_entry(name);
    printf("removed user widget '%s'\n", name);
    return 0;
}

/* ============================================================================
 * Public entry: dispatch from bin_display.c's `display lle widget`
 * ============================================================================
 */

/**
 * @brief Implementation of `display lle widget <subcommand> [args]`.
 *
 * argv[0] is "widget" (per the bin_display.c convention). argv[1] is
 * the subcommand name; argv[2..] are subcommand args.
 *
 * @param argc Shifted argc as passed by bin_display.c.
 * @param argv Shifted argv as passed by bin_display.c.
 * @return 0 on success, non-zero on a usage / runtime error.
 */
int display_lle_widget(int argc, char **argv) {
    lle_editor_t *editor = lle_get_global_editor();
    lle_widget_registry_t *registry = editor ? editor->widget_registry : NULL;

    const char *sub = (argc >= 2) ? argv[1] : "help";

    if (strcmp(sub, "help") == 0 || strcmp(sub, "-h") == 0 ||
        strcmp(sub, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(sub, "list") == 0) {
        return do_list(registry);
    }
    if (strcmp(sub, "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "display lle widget show: missing NAME\n");
            return 1;
        }
        return do_show(registry, argv[2]);
    }
    if (strcmp(sub, "add") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "display lle widget add: usage: add NAME 'COMMAND'\n");
            return 1;
        }
        return do_add(registry, argv[2], argv[3]);
    }
    if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0 ||
        strcmp(sub, "unregister") == 0) {
        if (argc < 3) {
            fprintf(stderr, "display lle widget remove: missing NAME\n");
            return 1;
        }
        return do_remove(registry, argv[2]);
    }

    fprintf(stderr, "display lle widget: unknown subcommand '%s'\n", sub);
    print_usage();
    return 1;
}
