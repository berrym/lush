/**
 * @file widget_hooks.c
 * @brief LLE Widget Hooks Manager Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implementation of widget lifecycle hooks system providing automatic
 * widget triggering at key editor lifecycle points.
 *
 * Specification: docs/lle_specification/07_extensibility_framework_complete.md
 * Section: 5 - Widget Hooks Integration
 */

#include "lle/widget_hooks.h"
#include "lle/lle_editor.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/widget_hooks_bridge.h"
#include <string.h>

/* ============================================================================
 * INTERNAL DATA
 * ============================================================================
 */

/**
 * Hook names for debugging and logging
 *
 * Must match order of lle_widget_hook_type_t enum in widget_hooks.h
 */
static const char *HOOK_NAMES[LLE_HOOK_COUNT] = {
    "line-init",      "line-accepted",  "line-finish",      "buffer-modified",
    "pre-command",    "post-command",   "completion-start", "completion-end",
    "history-search", "terminal-resize"};

/* ============================================================================
 * HOOKS MANAGER LIFECYCLE
 * ============================================================================
 */

lle_result_t lle_widget_hooks_manager_init(lle_widget_hooks_manager_t **manager,
                                           lle_widget_registry_t *registry,
                                           lle_memory_pool_t *memory_pool) {
    if (!manager || !registry || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate manager structure
    lle_widget_hooks_manager_t *mgr =
        lle_pool_alloc(sizeof(lle_widget_hooks_manager_t));
    if (!mgr) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "widget",
                         "widget hook allocation failed");
    }

    memset(mgr, 0, sizeof(lle_widget_hooks_manager_t));

    // Initialize fields
    mgr->registry = registry;
    mgr->memory_pool = memory_pool;
    mgr->total_hooks_triggered = 0;
    mgr->hooks_enabled = true;

    // Initialize hook arrays to NULL
    for (size_t i = 0; i < LLE_HOOK_COUNT; i++) {
        mgr->hooks[i] = NULL;
        mgr->hook_trigger_counts[i] = 0;
    }

    *manager = mgr;
    return LLE_SUCCESS;
}

lle_result_t
lle_widget_hooks_manager_destroy(lle_widget_hooks_manager_t *manager) {
    if (!manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Free all hook registrations
    for (size_t i = 0; i < LLE_HOOK_COUNT; i++) {
        lle_hook_registration_t *reg = manager->hooks[i];
        while (reg) {
            lle_hook_registration_t *next = reg->next;
            lle_pool_free(reg);
            reg = next;
        }
    }

    // Free manager structure
    lle_pool_free(manager);

    return LLE_SUCCESS;
}

/* ============================================================================
 * HOOK MANAGEMENT
 * ============================================================================
 */

lle_result_t lle_widget_hook_register(lle_widget_hooks_manager_t *manager,
                                      lle_widget_hook_type_t hook_type,
                                      const char *widget_name) {
    if (!manager || !widget_name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (hook_type >= LLE_HOOK_COUNT) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Lookup widget in registry
    lle_widget_t *widget = lle_widget_lookup(manager->registry, widget_name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    // Check if widget already registered for this hook
    lle_hook_registration_t *existing = manager->hooks[hook_type];
    while (existing) {
        if (existing->widget == widget) {
            return LLE_ERROR_ALREADY_EXISTS;
        }
        existing = existing->next;
    }

    // Allocate registration structure
    lle_hook_registration_t *reg =
        lle_pool_alloc(sizeof(lle_hook_registration_t));
    if (!reg) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "widget",
                         "widget hook allocation failed");
    }

    // Initialize registration
    reg->widget = widget;
    reg->hook_type = hook_type;
    reg->trigger_count = 0;
    reg->enabled = true;
    reg->next = NULL;

    // Add to end of list (maintains registration order)
    if (!manager->hooks[hook_type]) {
        // First registration for this hook
        manager->hooks[hook_type] = reg;
    } else {
        // Find end of list
        lle_hook_registration_t *last = manager->hooks[hook_type];
        while (last->next) {
            last = last->next;
        }
        last->next = reg;
    }

    return LLE_SUCCESS;
}

lle_result_t lle_widget_hook_unregister(lle_widget_hooks_manager_t *manager,
                                        lle_widget_hook_type_t hook_type,
                                        const char *widget_name) {
    if (!manager || !widget_name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (hook_type >= LLE_HOOK_COUNT) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Lookup widget in registry
    lle_widget_t *widget = lle_widget_lookup(manager->registry, widget_name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    // Find and remove registration
    lle_hook_registration_t *reg = manager->hooks[hook_type];
    lle_hook_registration_t *prev = NULL;

    while (reg) {
        if (reg->widget == widget) {
            // Found the registration - remove it
            if (prev) {
                prev->next = reg->next;
            } else {
                manager->hooks[hook_type] = reg->next;
            }

            lle_pool_free(reg);
            return LLE_SUCCESS;
        }

        prev = reg;
        reg = reg->next;
    }

    return LLE_ERROR_NOT_FOUND;
}

lle_result_t lle_widget_hook_trigger(lle_widget_hooks_manager_t *manager,
                                     lle_widget_hook_type_t hook_type,
                                     lle_editor_t *editor) {
    if (!manager || !editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (hook_type >= LLE_HOOK_COUNT) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Check if hooks are globally enabled
    if (!manager->hooks_enabled) {
        return LLE_SUCCESS;
    }

    // Update statistics
    manager->total_hooks_triggered++;
    manager->hook_trigger_counts[hook_type]++;

    // Execute all widgets registered for this hook
    lle_hook_registration_t *reg = manager->hooks[hook_type];

    while (reg) {
        // Check if registration is enabled
        if (!reg->enabled) {
            reg = reg->next;
            continue;
        }

        // Execute widget (error-resilient: continue even if widget fails)
        lle_widget_execute_direct(reg->widget, editor);

        /// Note: Widget errors are logged but do not stop hook execution.
        /// This ensures that one misbehaving widget doesn't break the entire
        /// hook chain. Individual widget failures can be diagnosed via widget
        /// execution statistics and logs.

        // Update registration statistics
        reg->trigger_count++;

        reg = reg->next;
    }

    // Hook triggering always succeeds (error-resilient design)
    return LLE_SUCCESS;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

const char *lle_widget_hook_get_name(lle_widget_hook_type_t hook_type) {
    if (hook_type >= LLE_HOOK_COUNT) {
        return "unknown";
    }

    return HOOK_NAMES[hook_type];
}

size_t lle_widget_hook_get_count(lle_widget_hooks_manager_t *manager,
                                 lle_widget_hook_type_t hook_type) {
    if (!manager || hook_type >= LLE_HOOK_COUNT) {
        return 0;
    }

    size_t count = 0;
    lle_hook_registration_t *reg = manager->hooks[hook_type];

    while (reg) {
        count++;
        reg = reg->next;
    }

    return count;
}

bool lle_widget_hooks_enabled(lle_widget_hooks_manager_t *manager) {
    if (!manager) {
        return false;
    }

    return manager->hooks_enabled;
}

lle_result_t lle_widget_hooks_enable(lle_widget_hooks_manager_t *manager) {
    if (!manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    manager->hooks_enabled = true;
    return LLE_SUCCESS;
}

lle_result_t lle_widget_hooks_disable(lle_widget_hooks_manager_t *manager) {
    if (!manager) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    manager->hooks_enabled = false;
    return LLE_SUCCESS;
}

/* ============================================================================
 * Shell event hub bridge (declared in widget_hooks_bridge.h)
 * ============================================================================
 *
 * Composes two independent subsystems: lle_shell_event_hub (shell-side
 * lifecycle events) and lle_widget_hooks_manager (LLE-side widget
 * triggers). The shell-side has no notion of widgets; the LLE-side has
 * no notion of shell lifecycle. The bridge runs both, so a user widget
 * attached to LLE_HOOK_PRE_COMMAND fires every time lush.c emits a
 * PRE_COMMAND event before parse_and_execute.
 */

static void bridge_pre_command(void *event_data, void *user_data) {
    (void)event_data;
    lle_editor_t *editor = (lle_editor_t *)user_data;
    if (!editor || !editor->widget_hooks_manager) {
        return;
    }
    lle_widget_hook_trigger(editor->widget_hooks_manager, LLE_HOOK_PRE_COMMAND,
                            editor);
}

static void bridge_post_command(void *event_data, void *user_data) {
    (void)event_data;
    lle_editor_t *editor = (lle_editor_t *)user_data;
    if (!editor || !editor->widget_hooks_manager) {
        return;
    }
    lle_widget_hook_trigger(editor->widget_hooks_manager, LLE_HOOK_POST_COMMAND,
                            editor);
}

void lle_widget_hooks_bridge_install(struct lle_shell_event_hub *hub,
                                     struct lle_editor *editor) {
    if (!hub || !editor) {
        return;
    }
    lle_shell_event_hub_register(
        (lle_shell_event_hub_t *)hub, LLE_SHELL_EVENT_PRE_COMMAND,
        bridge_pre_command, editor, "widget-hooks-pre-command-bridge");
    lle_shell_event_hub_register(
        (lle_shell_event_hub_t *)hub, LLE_SHELL_EVENT_POST_COMMAND,
        bridge_post_command, editor, "widget-hooks-post-command-bridge");
}
