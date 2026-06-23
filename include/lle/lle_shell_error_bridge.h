/**
 * @file lle_shell_error_bridge.h
 * @brief Installs the LLE fault sinks that bridge to the shell
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * The LLE fault router (liblle) drives output through registered sinks. This
 * bridge, compiled into the shell executable, provides the strong sinks: the
 * user channel renders through the shell's structured-error display, and the
 * developer channel routes to the debug trace output. Registered during shell
 * startup so LLE faults reach the same surfaces as shell errors.
 */

#ifndef LLE_SHELL_ERROR_BRIDGE_H
#define LLE_SHELL_ERROR_BRIDGE_H

/**
 * @brief Install the LLE fault user and developer sinks
 */
void lle_shell_error_bridge_init(void);

/**
 * @brief Detach the LLE fault sinks
 */
void lle_shell_error_bridge_shutdown(void);

#endif /* LLE_SHELL_ERROR_BRIDGE_H */
