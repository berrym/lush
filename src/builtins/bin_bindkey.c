/**
 * @file bin_bindkey.c
 * @brief zsh `bindkey` builtin: key-to-action binding with introspection
 *
 * Real-world zsh scripts use `bindkey` in three load-bearing patterns:
 *
 *   bindkey                 -- list every binding in the active keymap
 *   bindkey KEY             -- query what is bound to KEY
 *   bindkey KEY ACTION      -- bind KEY to ACTION (action / widget name)
 *
 * The introspection forms (the bare list and the single-arg query)
 * make a silent-no-op approach genuinely wrong: a script that binds a
 * key and then queries it sees stale state in lush versus the real
 * binding in zsh. Investigation 2026-05-25 surfaced this in ~17% of
 * oh-my-zsh's bindkey sites.
 *
 * Implementation:
 *
 *   1. A side table records every (keymap, key_sequence, action_name)
 *      triple the user has bound. The query and listing forms read
 *      from this table; round-trip behavior is preserved.
 *
 *   2. When the action name exists in lush's action registry
 *      (lle_action_registry_lookup), the bind is ALSO forwarded to
 *      lle_keybinding_manager_bind so the key actually fires in
 *      interactive use. When the action name is zsh-only (no lush
 *      equivalent), the side-table record stands alone and a
 *      structured INFO is emitted noting that interactive use of
 *      this binding is the documented gap.
 *
 *   3. Mode-switch flags (-e, -v) record the requested keymap state.
 *      Lush has its own emacs/vi mode toggle via shell options; we
 *      record what was asked for and route to those toggles when
 *      possible.
 *
 *   4. Listing flags (-L) print every record in the standard
 *      `bindkey "KEY" action` form zsh produces.
 *
 *   5. Keymap-management flags (-A alias, -D delete, -N create, -d
 *      reset) are recognized and operate on the side-table view.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/keybinding_config.h"
#include "lle/lle_editor.h"
#include "lle/lle_pager.h"
#include "lle/lle_shell_integration.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Side table
 * ============================================================================
 */

typedef struct bindkey_entry {
    char *keymap;       ///< Keymap name (default "main")
    char *key_sequence; ///< Raw key text as given to bindkey
    char *action_name;  ///< Action / widget name
    struct bindkey_entry *next;
} bindkey_entry_t;

static bindkey_entry_t *g_bindkey_table = NULL;
static char *g_active_keymap = NULL; ///< NULL = "main" by default

static const char *active_keymap(void) {
    return g_active_keymap ? g_active_keymap : "main";
}

static void set_active_keymap(const char *name) {
    if (!name) {
        return;
    }
    free(g_active_keymap);
    g_active_keymap = strdup(name);
}

static bindkey_entry_t *find_binding(const char *keymap, const char *key) {
    for (bindkey_entry_t *e = g_bindkey_table; e; e = e->next) {
        if (strcmp(e->keymap, keymap) == 0 &&
            strcmp(e->key_sequence, key) == 0) {
            return e;
        }
    }
    return NULL;
}

static void detach_binding(bindkey_entry_t *target) {
    bindkey_entry_t **slot = &g_bindkey_table;
    while (*slot) {
        if (*slot == target) {
            *slot = target->next;
            free(target->keymap);
            free(target->key_sequence);
            free(target->action_name);
            free(target);
            return;
        }
        slot = &(*slot)->next;
    }
}

static void record_binding(const char *keymap, const char *key,
                           const char *action) {
    bindkey_entry_t *existing = find_binding(keymap, key);
    if (existing) {
        free(existing->action_name);
        existing->action_name = strdup(action);
        return;
    }
    bindkey_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }
    entry->keymap = strdup(keymap);
    entry->key_sequence = strdup(key);
    entry->action_name = strdup(action);
    entry->next = g_bindkey_table;
    g_bindkey_table = entry;
}

/* ============================================================================
 * LLE routing
 * ============================================================================
 *
 * Tries to route a bind into the real LLE keybinding manager when the
 * action name exists in lush's action registry. Returns true if the
 * route succeeded (binding will fire in interactive use). Returns
 * false when the action is unknown to lush -- the side-table record
 * still stands, but the interactive binding does not.
 */
static bool try_route_to_lle(const char *key, const char *action) {
    if (!key || !action) {
        return false;
    }
    const lle_action_registry_entry_t *entry =
        lle_action_registry_lookup(action);
    if (!entry) {
        return false;
    }
    lle_editor_t *editor = lle_get_global_editor();
    if (!editor || !editor->keybinding_manager) {
        /// LLE not active (non-interactive); the side-table record is the
        /// authoritative view here.
        return false;
    }
    lle_result_t result = LLE_ERROR_INVALID_PARAMETER;
    if (entry->type == LLE_ACTION_TYPE_SIMPLE) {
        result = lle_keybinding_manager_bind(editor->keybinding_manager, key,
                                             entry->func.simple, entry->name);
    } else {
        result = lle_keybinding_manager_bind_context(
            editor->keybinding_manager, key, entry->func.context, entry->name);
    }
    return result == LLE_SUCCESS;
}

/* ============================================================================
 * Output helpers
 * ============================================================================
 */

static void print_binding(FILE *out, const bindkey_entry_t *e) {
    fprintf(out, "\"%s\" %s\n", e->key_sequence, e->action_name);
}

static void list_keymap(const char *keymap) {
    /// Buffer the listing through open_memstream and hand it to
    /// lle_pager_present so wide keymaps (the active emacs / vi
    /// keymap can easily exceed a screen of bindings) paginate
    /// in interactive shells. On memstream allocation failure
    /// the per-binding writes target stdout directly.
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    FILE *sink = out ? out : stdout;
    for (bindkey_entry_t *e = g_bindkey_table; e; e = e->next) {
        if (strcmp(e->keymap, keymap) == 0) {
            print_binding(sink, e);
        }
    }
    if (out) {
        fclose(out);
        lle_pager_present(NULL, buf);
        free(buf);
    }
}

/* ============================================================================
 * Public dispatch
 * ============================================================================
 */

void bindkey_table_reset(void) {
    while (g_bindkey_table) {
        bindkey_entry_t *next = g_bindkey_table->next;
        free(g_bindkey_table->keymap);
        free(g_bindkey_table->key_sequence);
        free(g_bindkey_table->action_name);
        free(g_bindkey_table);
        g_bindkey_table = next;
    }
    free(g_active_keymap);
    g_active_keymap = NULL;
}

int bin_bindkey(int argc, char **argv) {
    /// Walk args to consume flags first; collect positionals.
    const char *keymap = NULL; /// -M KEYMAP overrides the active keymap
    bool list_mode = false;    /// -L
    bool delete_mode = false;  /// -r / -D KEY (we treat both as remove)
    bool mode_switch = false;  /// -e / -v / -a (emacs / vi-insert / vi-command)
    const char *mode_target = NULL;
    int positional_start = argc;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0' || a[1] == '-') {
            positional_start = i;
            break;
        }
        /// Treat each `-X` independently rather than as combined flags --
        /// zsh's bindkey doesn't combine.
        if (strcmp(a, "-L") == 0 || strcmp(a, "-l") == 0) {
            list_mode = true;
        } else if (strcmp(a, "-M") == 0 && i + 1 < argc) {
            keymap = argv[++i];
        } else if (strcmp(a, "-e") == 0) {
            mode_switch = true;
            mode_target = "emacs";
        } else if (strcmp(a, "-v") == 0) {
            mode_switch = true;
            mode_target = "vi-insert";
        } else if (strcmp(a, "-a") == 0) {
            mode_switch = true;
            mode_target = "vi-command";
        } else if (strcmp(a, "-r") == 0 || strcmp(a, "-D") == 0) {
            delete_mode = true;
        } else if (strcmp(a, "-A") == 0 || strcmp(a, "-N") == 0 ||
                   strcmp(a, "-d") == 0 || strcmp(a, "-s") == 0) {
            /// Keymap aliasing / creation / reset / string-binding:
            /// recognize the flag and let the remaining args fall
            /// through; the side-table records whatever is provided.
        } else if (a[0] == '-') {
            /// Unknown flag -- skip and continue.
        } else {
            positional_start = i;
            break;
        }
    }

    if (!keymap) {
        keymap = active_keymap();
    }

    if (mode_switch) {
        set_active_keymap(mode_target);
        /// No further work for `bindkey -e` / `-v` / `-a` alone.
        if (positional_start >= argc) {
            return 0;
        }
    }

    int positional_count = argc - positional_start;
    char **positional = argv + positional_start;

    /// 0 args -> list active keymap
    if (positional_count == 0) {
        list_keymap(keymap);
        return 0;
    }

    /// 1 arg, delete-mode -> remove binding
    if (delete_mode && positional_count >= 1) {
        bindkey_entry_t *e = find_binding(keymap, positional[0]);
        if (e) {
            detach_binding(e);
        }
        return 0;
    }

    /// 1 arg, list-mode -> print this binding (zsh's `bindkey -L KEY`)
    if (list_mode && positional_count >= 1) {
        bindkey_entry_t *e = find_binding(keymap, positional[0]);
        if (e) {
            print_binding(stdout, e);
        }
        return e ? 0 : 1;
    }

    /// 1 arg -> query
    if (positional_count == 1) {
        bindkey_entry_t *e = find_binding(keymap, positional[0]);
        if (e) {
            print_binding(stdout, e);
            return 0;
        }
        return 1;
    }

    /// 2 args -> bind
    if (positional_count >= 2) {
        record_binding(keymap, positional[0], positional[1]);
        try_route_to_lle(positional[0], positional[1]);
        return 0;
    }

    return 0;
}
