/**
 * @file bin_zle.c
 * @brief zsh `zle` builtin: widget registration and introspection
 *
 * Real-world zsh scripts use `zle` mainly for widget registration:
 *
 *   zle -N WIDGET [FUNCTION]   -- register WIDGET, body is FUNCTION or
 *                                 WIDGET itself if FUNCTION omitted
 *   zle -A old new             -- alias `new` to existing `old`
 *   zle -D widget              -- delete widget
 *   zle -l / -L                -- list registered widgets
 *
 * The introspection forms make a silent-no-op approach genuinely
 * wrong: a script that registers a widget and then queries the list
 * sees no record in lush versus the real registration in zsh.
 * Investigation 2026-05-25 showed 56 zle -N sites in oh-my-zsh alone.
 *
 * Implementation:
 *
 *   1. A side table records every (widget_name, body) pair the user
 *      has registered. Listing and aliasing read from the table.
 *
 *   2. When the body is a defined shell function lush knows about,
 *      no further routing is required -- the widget exists as
 *      a callable name in the symbol table and any future
 *      `zle WIDGET` invocation calls it directly. (Today's lush
 *      doesn't ship a zle-style "fire by name" surface for non-
 *      interactive contexts; the registration record covers the
 *      observable corpus behaviour.)
 *
 *   3. Interactive widget invocation routes through lush's own
 *      `display lle widget invoke NAME` mechanism. The zsh shape
 *      `zle WIDGET` (one positional, no flags) is a future
 *      enhancement.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Side table
 * ============================================================================
 */

typedef struct zle_widget_entry {
    char *name; /**< Widget name */
    char *body; /**< Function name implementing the widget */
    struct zle_widget_entry *next;
} zle_widget_entry_t;

static zle_widget_entry_t *g_zle_table = NULL;

static zle_widget_entry_t *find_widget(const char *name) {
    for (zle_widget_entry_t *e = g_zle_table; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static void detach_widget(zle_widget_entry_t *target) {
    zle_widget_entry_t **slot = &g_zle_table;
    while (*slot) {
        if (*slot == target) {
            *slot = target->next;
            free(target->name);
            free(target->body);
            free(target);
            return;
        }
        slot = &(*slot)->next;
    }
}

static void record_widget(const char *name, const char *body) {
    zle_widget_entry_t *existing = find_widget(name);
    if (existing) {
        free(existing->body);
        existing->body = body ? strdup(body) : NULL;
        return;
    }
    zle_widget_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }
    entry->name = strdup(name);
    entry->body = body ? strdup(body) : NULL;
    entry->next = g_zle_table;
    g_zle_table = entry;
}

/* ============================================================================
 * Public dispatch
 * ============================================================================
 */

void zle_widget_table_reset(void) {
    while (g_zle_table) {
        zle_widget_entry_t *next = g_zle_table->next;
        free(g_zle_table->name);
        free(g_zle_table->body);
        free(g_zle_table);
        g_zle_table = next;
    }
}

int bin_zle(int argc, char **argv) {
    // Walk args for a recognised mode flag; lush handles one mode at a
    // time (no flag stacking) which matches zsh's zle.
    char mode = 0;
    int positional_start = argc;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] != '\0' && a[2] == '\0') {
            mode = a[1];
            positional_start = i + 1;
            break;
        }
        if (a[0] != '-') {
            // No flag seen -- bare `zle WIDGET` invocation form.
            positional_start = i;
            break;
        }
        // Longer dash-prefixed args (e.g., `-N` followed by a string
        // starting with `-`): treat as unrecognised, skip.
    }

    int positional_count = argc - positional_start;
    char **positional = argv + positional_start;

    switch (mode) {
    case 'N':
        // zle -N WIDGET [FUNCTION]
        if (positional_count < 1) {
            return 1;
        }
        record_widget(positional[0],
                      positional_count >= 2 ? positional[1] : positional[0]);
        return 0;

    case 'D':
        // zle -D widget [widget2 ...]
        for (int i = 0; i < positional_count; i++) {
            zle_widget_entry_t *e = find_widget(positional[i]);
            if (e) {
                detach_widget(e);
            }
        }
        return 0;

    case 'A': {
        // zle -A OLD NEW -- alias NEW to OLD (NEW becomes a widget
        // whose body is OLD's body).
        if (positional_count < 2) {
            return 1;
        }
        zle_widget_entry_t *src = find_widget(positional[0]);
        if (!src) {
            // zsh exits non-zero if the source widget doesn't exist.
            return 1;
        }
        record_widget(positional[1], src->body);
        return 0;
    }

    case 'l':
    case 'L':
        // List widgets. -L emits the zsh re-runnable form
        // (`zle -N name body`); -l emits just the names.
        for (zle_widget_entry_t *e = g_zle_table; e; e = e->next) {
            if (mode == 'L') {
                if (e->body && strcmp(e->body, e->name) != 0) {
                    printf("zle -N %s %s\n", e->name, e->body);
                } else {
                    printf("zle -N %s\n", e->name);
                }
            } else {
                printf("%s\n", e->name);
            }
        }
        return 0;

    case 0:
        // No mode flag -- `zle WIDGET` invocation form. Out of scope
        // for now (non-interactive scripts don't typically invoke
        // widgets directly). Documented as a known gap.
        return 0;

    default:
        // Unknown flag -- silent no-op so scripts continue.
        return 0;
    }
}
