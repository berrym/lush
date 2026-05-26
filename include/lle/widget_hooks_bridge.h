/**
 * @file widget_hooks_bridge.h
 * @brief Bridge between the shell event hub and the widget hooks manager
 *
 * Tiny header so lle_shell_integration.c can install the bridge without
 * pulling in widget_hooks.h (which conflicts with lle_shell_hooks.h on
 * the LLE_HOOK_COUNT enumerator name -- the two systems were named
 * before the conflict surfaced, and the conflict only matters at the
 * single integration point).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LLE_WIDGET_HOOKS_BRIDGE_H
#define LLE_WIDGET_HOOKS_BRIDGE_H

/// Forward declarations; full types pulled in only by the .c that
/// implements the bridge.
struct lle_shell_event_hub;
struct lle_editor;

/**
 * @brief Install the shell-event-hub -> widget-hooks-manager bridge
 *
 * Registers handlers on the provided hub that, when a shell lifecycle
 * event fires (PRE_COMMAND, POST_COMMAND), trigger the corresponding
 * widget hook on the editor's widget_hooks_manager. Lets user widgets
 * registered via `display lle hook add` participate in the shell
 * lifecycle without each one knowing about both subsystems.
 *
 * Safe to call once per editor; calling more than once will install
 * duplicate handlers and so fire each hook multiple times. The
 * caller (lle_shell_integration_init) controls call count.
 *
 * @param hub    Shell event hub (must be non-NULL).
 * @param editor LLE editor whose widget_hooks_manager receives the
 *               bridged triggers (must be non-NULL; must have an
 *               initialized widget_hooks_manager).
 */
void lle_widget_hooks_bridge_install(struct lle_shell_event_hub *hub,
                                     struct lle_editor *editor);

#endif /// LLE_WIDGET_HOOKS_BRIDGE_H
