/**
 * @file display/lle_segment.c
 * @brief `display lle segment` -- user-defined prompt segments
 *
 * Lets a user register a named prompt segment whose visible content is
 * the value of a shell variable. The segment lives in the same
 * registry as the built-in segments and is picked up by the prompt
 * composer the moment a theme template names it.
 *
 * Subcommands:
 *   list                       -- show all registered segments
 *   add NAME VARIABLE          -- register NAME, content from $VARIABLE
 *   remove NAME                -- unregister a user segment
 *   show NAME                  -- show segment details
 *   help                       -- usage
 *
 * Design choice: the user segment reads a shell variable rather than
 * running a shell command on each render. Reading $VAR is O(1) and
 * cannot stall the prompt; running a command on every keystroke (as
 * the prompt recomposes) is fundamentally the wrong shape for an
 * interactive line editor. Users who need command output can update
 * the variable on a hook (`display lle hook add post-command set-status`).
 * Async output -- the right answer for git-status-on-every-prompt and
 * similar -- is a separate, larger piece of work.
 *
 * Ownership: the segment's `state` field is a heap-allocated struct
 * holding the strdup'd variable name. A side list of (segment_name ->
 * var_name) lets `show` retrieve the binding; lle_segment_free's
 * cleanup callback frees the strdup on unregister.
 *
 * Theme requirement: a registered segment is invisible until referenced
 * in the active theme template. The help text and the add path's
 * confirmation include a reminder.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins/display.h"
#include "lle/lle_shell_integration.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/theme.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * User segment implementation
 * ============================================================================
 */

typedef struct user_segment_state {
    char *var_name; ///< Strdup'd shell variable name to read on render.
} user_segment_state_t;

static void user_segment_cleanup(lle_prompt_segment_t *self) {
    if (!self || !self->state) {
        return;
    }
    user_segment_state_t *st = (user_segment_state_t *)self->state;
    free(st->var_name);
    st->var_name = NULL;
    // lle_segment_free will free `st` itself (the state struct).
}

static bool user_segment_is_visible(const lle_prompt_segment_t *self,
                                    const lle_prompt_context_t *ctx) {
    (void)ctx;
    if (!self || !self->state) {
        return false;
    }
    const user_segment_state_t *st = (const user_segment_state_t *)self->state;
    if (!st->var_name) {
        return false;
    }
    char *v = symtable_get_var(symtable_manager(), st->var_name);
    bool visible = (v && *v);
    free(v);
    return visible;
}

static lle_result_t user_segment_render(const lle_prompt_segment_t *self,
                                        const lle_prompt_context_t *ctx,
                                        const lle_theme_t *theme,
                                        lle_segment_output_t *output) {
    (void)ctx;
    (void)theme;
    if (!self || !self->state || !output) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    const user_segment_state_t *st = (const user_segment_state_t *)self->state;
    if (!st->var_name) {
        output->is_empty = true;
        return LLE_SUCCESS;
    }
    char *v = symtable_get_var(symtable_manager(), st->var_name);
    if (!v || !*v) {
        free(v);
        output->is_empty = true;
        return LLE_SUCCESS;
    }
    snprintf(output->content, sizeof(output->content), "%s", v);
    output->content_len = strlen(output->content);
    output->visual_width = output->content_len;
    output->is_empty = false;
    output->needs_separator = true;
    free(v);
    return LLE_SUCCESS;
}

static lle_prompt_segment_t *create_user_segment(const char *name,
                                                 const char *var_name) {
    lle_prompt_segment_t *seg = lle_segment_create(
        name, "User-defined prompt segment from shell variable",
        LLE_SEG_CAP_OPTIONAL | LLE_SEG_CAP_DYNAMIC);
    if (!seg) {
        return NULL;
    }
    user_segment_state_t *st = calloc(1, sizeof(*st));
    if (!st) {
        lle_segment_free(seg);
        return NULL;
    }
    st->var_name = strdup(var_name);
    if (!st->var_name) {
        free(st);
        lle_segment_free(seg);
        return NULL;
    }
    seg->state = st;
    seg->cleanup = user_segment_cleanup;
    seg->is_visible = user_segment_is_visible;
    seg->render = user_segment_render;
    return seg;
}

/* ============================================================================
 * Side list: track user-segment names so list/show can report their bindings
 * ============================================================================
 */

typedef struct user_segment_entry {
    char *name;     ///< Strdup'd segment name (for lookup).
    char *var_name; ///< Strdup'd variable name (for show).
    struct user_segment_entry *next;
} user_segment_entry_t;

static user_segment_entry_t *g_user_segments = NULL;

static user_segment_entry_t *find_entry(const char *name) {
    for (user_segment_entry_t *e = g_user_segments; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static void detach_entry(const char *name) {
    user_segment_entry_t **slot = &g_user_segments;
    while (*slot) {
        if (strcmp((*slot)->name, name) == 0) {
            user_segment_entry_t *victim = *slot;
            *slot = victim->next;
            free(victim->name);
            free(victim->var_name);
            free(victim);
            return;
        }
        slot = &(*slot)->next;
    }
}

/* ============================================================================
 * Subcommand handlers
 * ============================================================================
 */

static void print_usage(void) {
    printf("display lle segment -- user-defined prompt segments\n");
    printf("\n");
    printf("Usage:\n");
    printf("  display lle segment list                 - show all segments\n");
    printf("  display lle segment add NAME VAR         - register segment "
           "NAME\n");
    printf("                                             whose content is "
           "$VAR\n");
    printf(
        "  display lle segment remove NAME          - unregister a segment\n");
    printf("  display lle segment show NAME            - show details\n");
    printf("  display lle segment help                 - this message\n");
    printf("\n");
    printf("Reference the registered segment in your theme template to make\n");
    printf("it visible. Update the backing variable to change the content:\n");
    printf("\n");
    printf("  display lle segment add status LUSH_STATUS\n");
    printf("  LUSH_STATUS='OK'\n");
}

static int do_list(lle_segment_registry_t *registry) {
    if (!registry) {
        fprintf(stderr, "display lle segment list: LLE not active\n");
        return 1;
    }
    printf("%-22s %-22s %s\n", "NAME", "BACKING-VAR", "ORIGIN");
    printf("%-22s %-22s %s\n", "----", "-----------", "------");
    for (size_t i = 0; i < registry->count; i++) {
        const lle_prompt_segment_t *s = registry->segments[i];
        if (!s) {
            continue;
        }
        user_segment_entry_t *e = find_entry(s->name);
        printf("%-22s %-22s %s\n", s->name, e ? e->var_name : "(builtin)",
               e ? "user" : "builtin");
    }
    return 0;
}

static int do_show(lle_segment_registry_t *registry, const char *name) {
    if (!registry) {
        fprintf(stderr, "display lle segment show: LLE not active\n");
        return 1;
    }
    lle_prompt_segment_t *s = lle_segment_registry_find(registry, name);
    if (!s) {
        fprintf(stderr, "display lle segment show: '%s' not found\n", name);
        return 1;
    }
    printf("name: %s\n", s->name);
    printf("description: %s\n", s->description);
    user_segment_entry_t *e = find_entry(name);
    if (e) {
        printf("origin: user\n");
        printf("backing-var: %s\n", e->var_name);
        char *v = symtable_get_var(symtable_manager(), e->var_name);
        printf("current-value: %s\n", (v && *v) ? v : "(empty)");
        free(v);
    } else {
        printf("origin: builtin\n");
    }
    printf("renders: %llu\n", (unsigned long long)s->render_count);
    return 0;
}

static int do_add(lle_segment_registry_t *registry, const char *name,
                  const char *var_name) {
    if (!registry) {
        fprintf(stderr, "display lle segment add: LLE not active\n");
        return 1;
    }
    if (lle_segment_registry_find(registry, name)) {
        fprintf(stderr,
                "display lle segment add: '%s' already exists; remove "
                "it first\n",
                name);
        return 1;
    }

    user_segment_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        fprintf(stderr, "display lle segment add: out of memory\n");
        return 1;
    }
    entry->name = strdup(name);
    entry->var_name = strdup(var_name);
    if (!entry->name || !entry->var_name) {
        free(entry->name);
        free(entry->var_name);
        free(entry);
        fprintf(stderr, "display lle segment add: out of memory\n");
        return 1;
    }

    lle_prompt_segment_t *seg = create_user_segment(name, var_name);
    if (!seg) {
        free(entry->name);
        free(entry->var_name);
        free(entry);
        fprintf(stderr, "display lle segment add: segment create failed\n");
        return 1;
    }

    lle_result_t r = lle_segment_registry_register(registry, seg);
    if (r != LLE_SUCCESS) {
        lle_segment_free(seg);
        free(entry->name);
        free(entry->var_name);
        free(entry);
        fprintf(stderr,
                "display lle segment add: registry_register failed (%d)\n",
                (int)r);
        return 1;
    }

    entry->next = g_user_segments;
    g_user_segments = entry;

    printf("registered segment '%s' (from $%s).\n", name, var_name);
    printf("Reference it in your theme template to make it visible.\n");
    return 0;
}

static int do_remove(lle_segment_registry_t *registry, const char *name) {
    if (!registry) {
        fprintf(stderr, "display lle segment remove: LLE not active\n");
        return 1;
    }
    if (!find_entry(name)) {
        fprintf(stderr,
                "display lle segment remove: '%s' is not a user segment\n",
                name);
        return 1;
    }
    lle_result_t r = lle_segment_registry_unregister(registry, name);
    if (r != LLE_SUCCESS) {
        fprintf(stderr, "display lle segment remove: unregister failed (%d)\n",
                (int)r);
        return 1;
    }
    detach_entry(name);
    printf("removed segment '%s'\n", name);
    return 0;
}

/* ============================================================================
 * Public entry
 * ============================================================================
 */

int display_lle_segment(int argc, char **argv) {
    lle_segment_registry_t *registry = lle_get_global_segment_registry();

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
            fprintf(stderr, "display lle segment show: missing NAME\n");
            return 1;
        }
        return do_show(registry, argv[2]);
    }
    if (strcmp(sub, "add") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "display lle segment add: usage: add NAME VARIABLE\n");
            return 1;
        }
        return do_add(registry, argv[2], argv[3]);
    }
    if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0 ||
        strcmp(sub, "unregister") == 0) {
        if (argc < 3) {
            fprintf(stderr, "display lle segment remove: missing NAME\n");
            return 1;
        }
        return do_remove(registry, argv[2]);
    }

    fprintf(stderr, "display lle segment: unknown subcommand '%s'\n", sub);
    print_usage();
    return 1;
}
