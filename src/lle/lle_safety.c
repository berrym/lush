/**
 * @file lle_safety.c
 * @brief LLE Safety System - Panic detection and recovery helpers
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides additional safety utilities for LLE shell integration:
 * - Triple Ctrl+G panic detection (integrated with lle_shell_integration.c)
 * - Error counter auto-reset on success
 * - Diagnostic helpers for debugging
 *
 * The core safety logic (error counting, Ctrl+G tracking, reset functions)
 * is implemented in lle_shell_integration.c. This file provides additional
 * utility functions and future watchdog timer support.
 *
 * Specification: docs/lle_specification/26_initialization_system_complete.md
 * Date: 2025-01-16
 */

#include "lle/lle_safety.h"
#include "lle/lle_shell_event_hub.h"
#include "lle/lle_shell_integration.h"

#include <stdio.h>

/* ============================================================================
 * DIAGNOSTIC FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get human-readable initialization state summary
 *
 * Returns a static buffer with the current init state for debugging.
 * Not thread-safe.
 *
 * @return Static string describing initialization state
 */
const char *lle_safety_get_init_state_summary(void) {
    static char summary[256];

    if (!g_lle_integration) {
        return "LLE not initialized";
    }

    lle_init_state_t *state = &g_lle_integration->init_state;

    snprintf(summary, sizeof(summary),
             "pool:%s term:%s hub:%s edit:%s hist:%s hooks:%s",
             state->memory_pool_verified ? "OK" : "NO",
             state->terminal_detected ? "OK" : "NO",
             state->event_hub_initialized ? "OK" : "NO",
             state->editor_initialized ? "OK" : "NO",
             state->history_initialized ? "OK" : "NO",
             state->shell_hooks_installed ? "OK" : "NO");

    return summary;
}

/**
 * @brief Get safety statistics summary
 *
 * Returns statistics about errors, resets, and recovery operations.
 * Not thread-safe.
 *
 * @return Static string with safety statistics
 */
const char *lle_safety_get_stats_summary(void) {
    static char summary[512];

    if (!g_lle_integration) {
        return "LLE not initialized";
    }

    lle_shell_integration_t *integ = g_lle_integration;

    snprintf(summary, sizeof(summary),
             "errors:%u ctrl_g:%u recovery:%s "
             "readline_calls:%lu hard_resets:%lu nuclear_resets:%lu",
             integ->error_count, integ->ctrl_g_count,
             integ->recovery_mode ? "active" : "inactive",
             (unsigned long)integ->total_readline_calls,
             (unsigned long)integ->hard_reset_count,
             (unsigned long)integ->nuclear_reset_count);

    return summary;
}

/**
 * @brief Check if LLE is in recovery mode
 *
 * Recovery mode is entered after hitting the error threshold.
 * It's cleared after a successful readline completion.
 *
 * @return true if in recovery mode
 */
bool lle_safety_is_recovery_mode(void) {
    if (!g_lle_integration) {
        return false;
    }
    return g_lle_integration->recovery_mode;
}

/**
 * @brief Mark a successful readline operation
 *
 * Called after lle_readline() completes successfully. Resets error
 * counters and clears recovery mode, preventing spurious resets.
 */
void lle_safety_mark_success(void) {
    if (!g_lle_integration) {
        return;
    }

    g_lle_integration->successful_reads++;
    g_lle_integration->total_readline_calls++;

    // Reset error counter on success
    lle_reset_error_counter();
}

/**
 * @brief Mark a failed readline operation
 *
 * Called when lle_readline() encounters an error. Records the error
 * for threshold tracking.
 *
 * @param error The error that occurred
 */
void lle_safety_mark_failure(lle_result_t error) {
    if (!g_lle_integration) {
        return;
    }

    g_lle_integration->total_readline_calls++;
    lle_record_error(error);
}

/* ============================================================================
 * EVENT HUB STATISTICS
 * ============================================================================
 */

/**
 * @brief Get shell event hub statistics
 *
 * @param events_fired    Output: total events fired (NULL to skip)
 * @param dir_changes     Output: directory change count (NULL to skip)
 * @param commands        Output: commands executed (NULL to skip)
 */
void lle_safety_get_event_stats(uint64_t *events_fired, uint64_t *dir_changes,
                                uint64_t *commands) {
    if (!g_lle_integration || !g_lle_integration->event_hub) {
        if (events_fired)
            *events_fired = 0;
        if (dir_changes)
            *dir_changes = 0;
        if (commands)
            *commands = 0;
        return;
    }

    lle_shell_event_hub_t *hub = g_lle_integration->event_hub;

    if (events_fired)
        *events_fired = hub->events_fired;
    if (dir_changes)
        *dir_changes = hub->directory_changes;
    if (commands)
        *commands = hub->commands_executed;
}

/* ============================================================================
 * FUTURE: WATCHDOG TIMER
 * ============================================================================
 *
 * The watchdog timer would detect if lle_readline() hangs and trigger
 * automatic recovery. This is a future enhancement.
 *
 * Design:
 * - Set alarm before entering input loop
 * - Cancel alarm on successful input
 * - On alarm expiration, trigger soft reset
 *
 * For now, the triple Ctrl+G panic detection provides user-initiated
 * recovery, which is sufficient for most hang scenarios.
 */
