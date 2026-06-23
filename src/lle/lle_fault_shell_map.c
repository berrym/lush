/**
 * @file lle_fault_shell_map.c
 * @brief Mapping from LLE fault codes/severities to the shell error system
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Pure translation, no side effects and no external calls, so it is testable
 * in isolation. The six LLE severity levels collapse onto the shell's four,
 * and the LLE result ranges map onto the shell's error categories. Only faults
 * reported to the user (major severity and above) ever drive the shell
 * renderer; lower-severity codes are mapped for completeness.
 */

#include "lle/lle_fault_shell_map.h"

shell_error_code_t lle_fault_shell_code(lle_result_t code) {
    switch (code) {
    /// Memory faults.
    case LLE_ERROR_OUT_OF_MEMORY:
    case LLE_ERROR_MEMORY_POOL_EXHAUSTED:
        return SHELL_ERR_OUT_OF_MEMORY;
    case LLE_ERROR_MEMORY_CORRUPTION:
    case LLE_ERROR_DOUBLE_FREE_DETECTED:
    case LLE_ERROR_USE_AFTER_FREE:
        return SHELL_ERR_STATE_CORRUPTION;

    /// System / IO faults.
    case LLE_ERROR_PERMISSION_DENIED:
        return SHELL_ERR_PERMISSION_DENIED;
    case LLE_ERROR_SYSTEM_CALL:
    case LLE_ERROR_IO_ERROR:
    case LLE_ERROR_DEVICE_ERROR:
    case LLE_ERROR_TIMEOUT:
    case LLE_ERROR_INTERRUPT:
        return SHELL_ERR_IO_ERROR;
    case LLE_ERROR_RESOURCE_UNAVAILABLE:
    case LLE_ERROR_RESOURCE_EXHAUSTED:
        return SHELL_ERR_RESOURCE_LIMIT;

    /// Feature availability.
    case LLE_ERROR_FEATURE_DISABLED:
    case LLE_ERROR_FEATURE_NOT_AVAILABLE:
        return SHELL_ERR_FEATURE_DISABLED;

    /// State corruption and internal invariants.
    case LLE_ERROR_STATE_CORRUPTION:
    case LLE_ERROR_INVARIANT_VIOLATION:
        return SHELL_ERR_STATE_CORRUPTION;
    case LLE_ERROR_ASSERTION_FAILED:
    case LLE_ERROR_FATAL_INTERNAL:
        return SHELL_ERR_ASSERTION;

    default:
        break;
    }

    /// Component subsystem faults (1300-1399) and critical init/shutdown
    /// failures surface as a subsystem-init failure naming the component.
    if ((code >= LLE_ERROR_BUFFER_COMPONENT &&
         code < LLE_ERROR_FEATURE_DISABLED) ||
        code == LLE_ERROR_INITIALIZATION_FAILED ||
        code == LLE_ERROR_SHUTDOWN_FAILED) {
        return SHELL_ERR_SUBSYSTEM_INIT_FAILED;
    }

    /// Remaining input-validation and internal codes reaching the user channel
    /// indicate a logic error rather than an environmental one.
    return SHELL_ERR_ASSERTION;
}

shell_error_severity_t lle_fault_shell_severity(lle_error_severity_t severity) {
    switch (severity) {
    case LLE_SEVERITY_INFO:
        return SHELL_SEVERITY_NOTE;
    case LLE_SEVERITY_WARNING:
    case LLE_SEVERITY_MINOR:
        return SHELL_SEVERITY_WARNING;
    case LLE_SEVERITY_MAJOR:
    case LLE_SEVERITY_CRITICAL:
        return SHELL_SEVERITY_ERROR;
    case LLE_SEVERITY_FATAL:
        return SHELL_SEVERITY_FATAL;
    }
    return SHELL_SEVERITY_ERROR;
}
