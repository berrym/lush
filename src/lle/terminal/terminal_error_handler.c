/**
 * @file terminal_error_handler.c
 * @brief Error Handling (Spec 02 Subsystem 7)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides terminal abstraction specific error handling on top of
 * LLE error handling infrastructure (Spec 16).
 *
 * Key Responsibilities:
 * - Provide terminal error recovery strategies
 * - Handle terminal state restoration on errors
 *
 * NOTE: Full error logging will be implemented when Spec 16 Phase 2 is
 * complete. For now, provides basic error handling and recovery.
 *
 * Spec 02: Terminal Abstraction - Subsystem 7
 */

#include "lle/terminal_abstraction.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TERMINAL ERROR HANDLING
 * ============================================================================
 */

/**
 * @brief Recover from terminal state error
 *
 * Attempts to restore terminal to a known good state after error.
 *
 * @param unix_interface Unix interface for terminal restoration
 * @param error_code The error code that triggered recovery
 * @return LLE_SUCCESS if recovery succeeded, error code on failure
 */
lle_result_t
lle_terminal_recover_from_error(lle_unix_interface_t *unix_interface,
                                lle_result_t error_code) {
    if (!unix_interface) {
        return error_code;
    }

    // Attempt to exit raw mode if active
    if (unix_interface->raw_mode_active) {
        lle_result_t result = lle_unix_interface_exit_raw_mode(unix_interface);
        if (result != LLE_SUCCESS) {
            // Recovery failed - critical
            return LLE_ERROR_RECOVERY_FAILED;
        }
    }

    // Terminal state restored successfully
    return LLE_SUCCESS;
}
