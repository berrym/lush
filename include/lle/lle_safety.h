/**
 * @file lle_safety.h
 * @brief LLE Safety System Public API
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides safety utilities for LLE shell integration:
 * - Diagnostic functions for debugging initialization and error states
 * - Recovery mode tracking
 * - Event statistics
 *
 * The core safety logic (error counting, Ctrl+G tracking, reset functions)
 * is in lle_shell_integration.h. This header provides additional utility
 * functions for diagnostics and monitoring.
 *
 * Specification: docs/lle_specification/26_initialization_system_complete.md
 */

#ifndef LLE_SAFETY_H
#define LLE_SAFETY_H

#include "lle/error_handling.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * DIAGNOSTIC FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get human-readable initialization state summary
 *
 * Returns a static buffer describing which LLE subsystems are initialized.
 * Useful for debugging startup issues.
 *
 * Example output: "pool:OK term:OK hub:OK edit:OK hist:OK hooks:OK"
 *
 * @return Static string describing initialization state (not thread-safe)
 */
const char *lle_safety_get_init_state_summary(void);

/**
 * @brief Get safety statistics summary
 *
 * Returns statistics about errors, Ctrl+G presses, and recovery operations.
 * Useful for debugging and monitoring LLE health.
 *
 * Example output: "errors:0 ctrl_g:0 recovery:inactive readline_calls:42
 *                  hard_resets:0 nuclear_resets:0"
 *
 * @return Static string with safety statistics (not thread-safe)
 */
const char *lle_safety_get_stats_summary(void);

/**
 * @brief Check if LLE is in recovery mode
 *
 * Recovery mode is entered after hitting the error threshold (default 5).
 * It's cleared after a successful readline completion.
 *
 * @return true if LLE is in recovery mode
 */
bool lle_safety_is_recovery_mode(void);

/* ============================================================================
 * READLINE TRACKING
 * ============================================================================
 */

/**
 * @brief Mark a successful readline operation
 *
 * Call after lle_readline() completes successfully. This:
 * - Resets the error counter
 * - Clears recovery mode
 * - Increments successful read count
 *
 * Prevents spurious resets when LLE is working correctly.
 */
void lle_safety_mark_success(void);

/**
 * @brief Mark a failed readline operation
 *
 * Call when lle_readline() encounters an error. Records the error
 * for threshold tracking. If errors exceed the threshold, recovery
 * mode is entered.
 *
 * @param error The error that occurred
 */
void lle_safety_mark_failure(lle_result_t error);

/* ============================================================================
 * EVENT STATISTICS
 * ============================================================================
 */

/**
 * @brief Get shell event hub statistics
 *
 * Retrieves counters for events processed by the shell event hub.
 * Pass NULL for any counter you don't need.
 *
 * @param events_fired    Output: total events fired (or NULL)
 * @param dir_changes     Output: directory change count (or NULL)
 * @param commands        Output: commands executed (or NULL)
 */
void lle_safety_get_event_stats(uint64_t *events_fired, uint64_t *dir_changes,
                                uint64_t *commands);

#ifdef __cplusplus
}
#endif

#endif // LLE_SAFETY_H
