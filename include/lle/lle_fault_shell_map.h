/**
 * @file lle_fault_shell_map.h
 * @brief Mapping from LLE fault codes/severities to the shell error system
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Pure, dependency-free translation between liblle's result/severity model
 * and the shell's structured-error model. Lives shell-side: it is the point
 * where an LLE fault becomes a shell-renderable error. Used by the LLE fault
 * user sink (see lle_shell_error_bridge.h).
 */

#ifndef LLE_FAULT_SHELL_MAP_H
#define LLE_FAULT_SHELL_MAP_H

#include "lle/error_handling.h"
#include "shell_error.h"

/**
 * @brief Map an LLE result code to the closest shell error code
 * @param code LLE result code raised at a fault site
 * @return The shell error code that best categorizes it
 */
shell_error_code_t lle_fault_shell_code(lle_result_t code);

/**
 * @brief Map an LLE severity to the shell's four-level severity
 * @param severity LLE severity
 * @return The corresponding shell severity
 */
shell_error_severity_t lle_fault_shell_severity(lle_error_severity_t severity);

#endif /* LLE_FAULT_SHELL_MAP_H */
