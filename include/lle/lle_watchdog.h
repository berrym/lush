/**
 * @file lle_watchdog.h
 * @brief LLE Watchdog Timer for Deadlock Detection
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides a SIGALRM-based watchdog mechanism that detects when the
 * readline input loop is stuck and forces recovery. This catches
 * scenarios where:
 * - Readline is stuck in infinite loop while processing
 * - Event handler never returns
 * - Deadlock in completion/search subsystem
 *
 * Design Principles:
 * - Non-invasive: Signal handler sets flag, main loop checks flag
 * - Fail-safe: If alarm fires, force hard reset
 * - Self-resetting: Each successful input resets watchdog timer
 * - Configurable: Timeout adjustable (default 10 seconds)
 *
 * Usage:
 *   lle_watchdog_init();           /// At shell startup
 *
 *   while (!done) {
 *       lle_watchdog_pet(0);       /// Reset timer before blocking
 *       event = read_input();      /// May block
 *       if (lle_watchdog_check_and_clear()) {
 *           /// Watchdog fired - force recovery
 *           lle_hard_reset();
 *       }
 *       process(event);
 *   }
 *
 *   lle_watchdog_stop();           /// On normal exit
 *   lle_watchdog_cleanup();        /// At shell shutdown
 *
 * @see src/lle/lle_watchdog.c for implementation
 * @see src/lle/lle_readline.c for integration
 */

#ifndef LLE_WATCHDOG_H
#define LLE_WATCHDOG_H

#include "lle/error_handling.h"

#include <stdbool.h>

/**
 * @brief Default watchdog timeout in seconds
 *
 * If no input is processed for this many seconds, the watchdog fires.
 * 10 seconds is long enough for slow operations (git status, large
 * completions) but short enough to catch real hangs.
 */
#define LLE_WATCHDOG_TIMEOUT_DEFAULT 10

/**
 * @brief Initialize watchdog system
 *
 * Installs SIGALRM handler. Safe to call multiple times - subsequent
 * calls are no-ops.
 *
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_SYSTEM if sigaction fails
 */
lle_result_t lle_watchdog_init(void);

/**
 * @brief Cleanup watchdog system
 *
 * Cancels any pending alarm and restores previous SIGALRM handler.
 * Safe to call even if init was never called.
 */
void lle_watchdog_cleanup(void);

/**
 * @brief Start/reset watchdog timer ("pet the dog")
 *
 * Call this before any potentially-blocking operation. If the operation
 * takes longer than timeout_seconds, the watchdog will fire.
 *
 * The watchdog is automatically disarmed if it fires, so you must
 * call pet() again after handling a timeout.
 *
 * @param timeout_seconds Timeout value in seconds (0 = use default)
 */
void lle_watchdog_pet(unsigned int timeout_seconds);

/**
 * @brief Stop watchdog timer
 *
 * Cancels any pending alarm without cleaning up the handler.
 * Call this when readline exits normally to prevent spurious timeouts.
 */
void lle_watchdog_stop(void);

/**
 * @brief Check if watchdog has fired and clear the flag
 *
 * This is the main check point in the input loop. Call this after
 * any potentially-blocking operation.
 *
 * The flag is automatically cleared by this call, so you only get
 * one chance to handle each timeout.
 *
 * @return true if watchdog timeout occurred, false otherwise
 */
bool lle_watchdog_check_and_clear(void);

/**
 * @brief Check if watchdog fired without clearing
 *
 * Useful for logging/debugging. Does not clear the flag.
 *
 * @return true if watchdog has fired, false otherwise
 */
bool lle_watchdog_check(void);

/**
 * @brief Check if watchdog is currently armed (timer running)
 *
 * @return true if watchdog timer is active, false otherwise
 */
bool lle_watchdog_is_armed(void);

/**
 * @brief Get current watchdog timeout setting
 *
 * @return Timeout in seconds (or 0 if not initialized)
 */
unsigned int lle_watchdog_get_timeout(void);

/**
 * @brief Statistics for watchdog monitoring
 */
typedef struct {
    unsigned int total_pets;       ///< Total times watchdog was petted
    unsigned int total_fires;      ///< Total times watchdog fired
    unsigned int total_recoveries; ///< Total successful recoveries
} lle_watchdog_stats_t;

/**
 * @brief Get watchdog statistics
 *
 * @param stats Output structure for statistics
 * @return LLE_SUCCESS on success
 */
lle_result_t lle_watchdog_get_stats(lle_watchdog_stats_t *stats);

#endif /// LLE_WATCHDOG_H
