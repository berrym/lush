/**
 * @file lle_watchdog.c
 * @brief LLE Watchdog Timer Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements a SIGALRM-based watchdog mechanism for detecting deadlocks
 * and stuck conditions in the readline input loop.
 */

#include "lle/lle_watchdog.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>

/* ============================================================================
 * PRIVATE STATE
 * ============================================================================
 */

/**
 * Atomic flags for signal-safe communication.
 * These are set by the signal handler and read by the main loop.
 */
static atomic_bool g_watchdog_fired = false;
static atomic_bool g_watchdog_armed = false;

/**
 * Previous SIGALRM handler to restore on cleanup.
 */
static struct sigaction g_old_sigalrm_action;

/**
 * Initialization state.
 */
static bool g_initialized = false;

/**
 * Current timeout setting (for get_timeout).
 */
static unsigned int g_current_timeout = 0;

/**
 * Statistics counters.
 */
static atomic_uint g_stats_pets = 0;
static atomic_uint g_stats_fires = 0;
static atomic_uint g_stats_recoveries = 0;

/* ============================================================================
 * SIGNAL HANDLER
 * ============================================================================
 */

/**
 * @brief SIGALRM signal handler for watchdog timeout
 *
 * Must be async-signal-safe. Only uses atomic operations and avoids
 * any non-reentrant functions. This is called from signal context,
 * so must be minimal.
 *
 * @param sig Signal number (unused, but required by signal handler signature)
 */
static void watchdog_signal_handler(int sig) {
    (void)sig; /// Unused, but required by signal handler signature

    /// Set the fired flag atomically
    atomic_store(&g_watchdog_fired, true);

    /// Disarm the watchdog to prevent repeated firing
    atomic_store(&g_watchdog_armed, false);

    /// Increment fire counter (atomic)
    atomic_fetch_add(&g_stats_fires, 1);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Initialize the watchdog subsystem
 *
 * Installs the SIGALRM handler and initializes watchdog state.
 * Safe to call multiple times - subsequent calls return success
 * without reinitializing.
 *
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_SYSTEM_CALL if sigaction fails
 */
lle_result_t lle_watchdog_init(void) {
    if (g_initialized) {
        return LLE_SUCCESS; /// Already initialized
    }

    struct sigaction sa;
    sa.sa_handler = watchdog_signal_handler;
    sa.sa_flags = 0; /// No SA_RESTART - we want to interrupt syscalls
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, &g_old_sigalrm_action) < 0) {
        return LLE_ERROR_SYSTEM_CALL;
    }

    g_initialized = true;
    atomic_store(&g_watchdog_fired, false);
    atomic_store(&g_watchdog_armed, false);
    g_current_timeout = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Cleanup the watchdog subsystem
 *
 * Cancels any pending alarm and restores the previous SIGALRM handler.
 */
void lle_watchdog_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    /// Cancel any pending alarm
    alarm(0);

    /// Restore previous handler
    sigaction(SIGALRM, &g_old_sigalrm_action, NULL);

    g_initialized = false;
    atomic_store(&g_watchdog_armed, false);
    atomic_store(&g_watchdog_fired, false);
    g_current_timeout = 0;
}

/**
 * @brief Pet (reset) the watchdog timer
 *
 * Resets the watchdog timer to the specified timeout. Must be called
 * periodically to prevent the watchdog from firing.
 *
 * @param timeout_seconds Timeout in seconds (0 = use default)
 */
void lle_watchdog_pet(unsigned int timeout_seconds) {
    if (!g_initialized) {
        return;
    }

    if (timeout_seconds == 0) {
        timeout_seconds = LLE_WATCHDOG_TIMEOUT_DEFAULT;
    }

    /// Clear fired flag and arm the watchdog
    atomic_store(&g_watchdog_fired, false);
    atomic_store(&g_watchdog_armed, true);
    g_current_timeout = timeout_seconds;

    /// Set the alarm
    alarm(timeout_seconds);

    /// Increment pet counter
    atomic_fetch_add(&g_stats_pets, 1);
}

/**
 * @brief Stop the watchdog timer
 *
 * Cancels any pending alarm and disarms the watchdog.
 */
void lle_watchdog_stop(void) {
    if (!g_initialized) {
        return;
    }

    /// Cancel any pending alarm
    alarm(0);

    /// Disarm and clear flags
    atomic_store(&g_watchdog_armed, false);
    atomic_store(&g_watchdog_fired, false);
    g_current_timeout = 0;
}

/**
 * @brief Check if watchdog fired and clear the flag
 *
 * Atomically checks and clears the fired flag. Use this to detect
 * and recover from watchdog timeouts.
 *
 * @return true if watchdog had fired, false otherwise
 */
bool lle_watchdog_check_and_clear(void) {
    bool was_fired = atomic_exchange(&g_watchdog_fired, false);

    if (was_fired) {
        /// Count successful recovery (we caught the timeout)
        atomic_fetch_add(&g_stats_recoveries, 1);
    }

    return was_fired;
}

/**
 * @brief Check if watchdog has fired without clearing
 *
 * @return true if watchdog has fired, false otherwise
 */
bool lle_watchdog_check(void) { return atomic_load(&g_watchdog_fired); }

/**
 * @brief Check if watchdog is currently armed
 *
 * @return true if watchdog is armed and timing, false otherwise
 */
bool lle_watchdog_is_armed(void) { return atomic_load(&g_watchdog_armed); }

/**
 * @brief Get the current timeout setting
 *
 * @return Current timeout in seconds, or 0 if not set
 */
unsigned int lle_watchdog_get_timeout(void) { return g_current_timeout; }

/**
 * @brief Get watchdog statistics
 *
 * Retrieves counters for pets, fires, and recoveries.
 *
 * @param stats Pointer to receive statistics
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if stats is NULL
 */
lle_result_t lle_watchdog_get_stats(lle_watchdog_stats_t *stats) {
    if (!stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    stats->total_pets = atomic_load(&g_stats_pets);
    stats->total_fires = atomic_load(&g_stats_fires);
    stats->total_recoveries = atomic_load(&g_stats_recoveries);

    return LLE_SUCCESS;
}
