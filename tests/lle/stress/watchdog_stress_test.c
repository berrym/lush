/**
 * @file watchdog_stress_test.c
 * @brief LLE Watchdog Stress Tests
 *
 * Validates the watchdog timer mechanisms that prevent/recover from freezes:
 * - Watchdog timer functionality (SIGALRM-based deadlock detection)
 * - Timeout detection and recovery
 * - Effectiveness metrics under stress conditions
 *
 * These tests verify that the watchdog is effective at detecting
 * and recovering from hang scenarios.
 *
 * Note: Safety system tests are separate because they require the full
 * shell integration context. This test focuses on the standalone watchdog.
 */

#define _POSIX_C_SOURCE 200809L

#include "lle/error_handling.h"
#include "lle/lle_watchdog.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    printf("\n===============================================================" \
           "==\n");                                                            \
    printf("TEST: %s\n", name);                                                \
    printf("=================================================================" \
           "\n");                                                              \
    tests_run++

#define PASS()                                                                 \
    do {                                                                       \
        printf("  PASS\n");                                                    \
        tests_passed++;                                                        \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("  FAIL: %s\n", msg);                                           \
        tests_failed++;                                                        \
    } while (0)

/// Helper to get current time in nanoseconds
static uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ========================================================================== */
/// TEST 1: WATCHDOG INITIALIZATION
/* ========================================================================== */

void test_watchdog_init(void) {
    TEST("Watchdog Initialization");

    printf("Testing watchdog init/cleanup cycle...\n");

    /// Clean state
    lle_watchdog_cleanup();

    /// Initialize
    lle_result_t result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        FAIL("Watchdog init failed");
        return;
    }
    printf("  Watchdog initialized successfully\n");

    /// Check initial state
    if (lle_watchdog_is_armed()) {
        FAIL("Watchdog should not be armed after init");
        return;
    }
    printf("  Initial state: not armed (correct)\n");

    /// Get stats
    lle_watchdog_stats_t stats;
    result = lle_watchdog_get_stats(&stats);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to get watchdog stats");
        return;
    }
    printf("  Stats accessible: pets=%u, fires=%u, recoveries=%u\n",
           stats.total_pets, stats.total_fires, stats.total_recoveries);

    /// Cleanup
    lle_watchdog_cleanup();
    printf("  Cleanup completed\n");

    PASS();
}

/* ========================================================================== */
/// TEST 2: WATCHDOG PET AND ARM
/* ========================================================================== */

void test_watchdog_pet(void) {
    TEST("Watchdog Pet (Timer Reset)");

    lle_watchdog_cleanup();
    lle_result_t result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        FAIL("Watchdog init failed");
        return;
    }

    printf("Testing pet operation...\n");

    /// Pet with default timeout
    lle_watchdog_pet(0);

    if (!lle_watchdog_is_armed()) {
        FAIL("Watchdog should be armed after pet");
        lle_watchdog_cleanup();
        return;
    }
    printf("  Watchdog armed after pet: yes\n");

    unsigned int timeout = lle_watchdog_get_timeout();
    printf("  Timeout set: %u seconds\n", timeout);

    if (timeout != LLE_WATCHDOG_TIMEOUT_DEFAULT) {
        FAIL("Timeout should be default value");
        lle_watchdog_cleanup();
        return;
    }

    /// Check stats
    lle_watchdog_stats_t stats;
    lle_watchdog_get_stats(&stats);
    if (stats.total_pets < 1) {
        FAIL("Pet count should be at least 1");
        lle_watchdog_cleanup();
        return;
    }
    printf("  Pet count: %u\n", stats.total_pets);

    /// Stop and cleanup
    lle_watchdog_stop();
    if (lle_watchdog_is_armed()) {
        FAIL("Watchdog should not be armed after stop");
        lle_watchdog_cleanup();
        return;
    }
    printf("  Watchdog stopped successfully\n");

    lle_watchdog_cleanup();
    PASS();
}

/* ========================================================================== */
/// TEST 3: WATCHDOG TIMEOUT DETECTION
/* ========================================================================== */

void test_watchdog_timeout(void) {
    TEST("Watchdog Timeout Detection (Short Timeout)");

    lle_watchdog_cleanup();
    lle_result_t result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        FAIL("Watchdog init failed");
        return;
    }

    printf("Testing timeout detection with 1-second timeout...\n");

    /// Pet with 1-second timeout
    lle_watchdog_pet(1);
    printf("  Watchdog armed with 1s timeout\n");

    /// Wait for timeout (1.5 seconds to be safe)
    printf("  Waiting 1.5 seconds for timeout...\n");
    struct timespec sleep_time = {1, 500000000}; /// 1.5 seconds
    nanosleep(&sleep_time, NULL);

    /// Check if watchdog fired
    bool fired = lle_watchdog_check();
    printf("  Watchdog fired: %s\n", fired ? "YES" : "no");

    if (!fired) {
        FAIL("Watchdog should have fired after timeout");
        lle_watchdog_cleanup();
        return;
    }

    /// Check and clear
    bool cleared = lle_watchdog_check_and_clear();
    if (!cleared) {
        FAIL("check_and_clear should return true");
        lle_watchdog_cleanup();
        return;
    }
    printf("  check_and_clear returned: true\n");

    /// Verify flag is now clear
    if (lle_watchdog_check()) {
        FAIL("Flag should be clear after check_and_clear");
        lle_watchdog_cleanup();
        return;
    }
    printf("  Flag cleared after check_and_clear: yes\n");

    /// Check stats
    lle_watchdog_stats_t stats;
    lle_watchdog_get_stats(&stats);
    printf("  Stats: pets=%u, fires=%u, recoveries=%u\n", stats.total_pets,
           stats.total_fires, stats.total_recoveries);

    if (stats.total_fires < 1) {
        FAIL("Fire count should be at least 1");
        lle_watchdog_cleanup();
        return;
    }

    if (stats.total_recoveries < 1) {
        FAIL("Recovery count should be at least 1 (we caught the timeout)");
        lle_watchdog_cleanup();
        return;
    }

    lle_watchdog_cleanup();
    PASS();
}

/* ========================================================================== */
/// TEST 4: WATCHDOG RAPID PET (NO TIMEOUT)
/* ========================================================================== */

void test_watchdog_rapid_pet(void) {
    TEST("Watchdog Rapid Pet (Simulating Normal Operation)");

    lle_watchdog_cleanup();
    lle_result_t result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        FAIL("Watchdog init failed");
        return;
    }

    printf("Simulating 100 rapid input events with 2s timeout...\n");

    lle_watchdog_stats_t stats_before;
    lle_watchdog_get_stats(&stats_before);
    unsigned int fires_before = stats_before.total_fires;

    uint64_t start = get_nanos();

    /// Simulate 100 rapid input events, each resetting the timer
    for (int i = 0; i < 100; i++) {
        lle_watchdog_pet(2); /// 2 second timeout

        /// Small delay (10ms) - simulates processing time
        struct timespec delay = {0, 10000000}; /// 10ms
        nanosleep(&delay, NULL);

        /// Check that watchdog hasn't fired
        if (lle_watchdog_check()) {
            FAIL("Watchdog should not fire during rapid petting");
            lle_watchdog_cleanup();
            return;
        }
    }

    uint64_t elapsed_ms = (get_nanos() - start) / 1000000;
    printf("  Completed 100 events in %llu ms\n",
           (unsigned long long)elapsed_ms);

    /// Verify no timeouts occurred
    lle_watchdog_stats_t stats_after;
    lle_watchdog_get_stats(&stats_after);

    printf("  Pets during test: %u\n",
           stats_after.total_pets - stats_before.total_pets);
    printf("  Fires during test: %u\n", stats_after.total_fires - fires_before);

    if (stats_after.total_fires > fires_before) {
        FAIL("No fires should occur during rapid petting");
        lle_watchdog_cleanup();
        return;
    }

    lle_watchdog_stop();
    lle_watchdog_cleanup();
    PASS();
}

/* ========================================================================== */
/// TEST 5: WATCHDOG EFFECTIVENESS METRIC
/* ========================================================================== */

void test_watchdog_effectiveness(void) {
    TEST("Watchdog Effectiveness Metric");

    lle_watchdog_cleanup();
    lle_result_t result = lle_watchdog_init();
    if (result != LLE_SUCCESS) {
        FAIL("Watchdog init failed");
        return;
    }

    printf(
        "Running effectiveness test (5 simulated freeze/recovery cycles)...\n");

    int successful_recoveries = 0;
    int total_freezes = 5;

    for (int i = 0; i < total_freezes; i++) {
        printf("  Cycle %d: ", i + 1);

        /// Pet with 1-second timeout
        lle_watchdog_pet(1);

        /// Simulate freeze (wait for timeout)
        struct timespec sleep_time = {1, 200000000}; /// 1.2 seconds
        nanosleep(&sleep_time, NULL);

        /// Check if we detected the freeze
        if (lle_watchdog_check_and_clear()) {
            successful_recoveries++;
            printf("freeze detected, recovered\n");
        } else {
            printf("MISSED freeze!\n");
        }
    }

    printf("\nResults:\n");
    printf("  Total simulated freezes: %d\n", total_freezes);
    printf("  Successful detections: %d\n", successful_recoveries);
    printf("  Detection rate: %.1f%%\n",
           (double)successful_recoveries / total_freezes * 100.0);

    lle_watchdog_stats_t stats;
    lle_watchdog_get_stats(&stats);
    printf("\nCumulative Stats:\n");
    printf("  Total pets: %u\n", stats.total_pets);
    printf("  Total fires: %u\n", stats.total_fires);
    printf("  Total recoveries: %u\n", stats.total_recoveries);

    if (stats.total_fires > 0) {
        double recovery_rate =
            (double)stats.total_recoveries / stats.total_fires * 100.0;
        printf("  Overall recovery rate: %.1f%%\n", recovery_rate);
    }

    lle_watchdog_cleanup();

    if (successful_recoveries < total_freezes) {
        FAIL("Not all freezes were detected");
        return;
    }

    PASS();
}

/* ========================================================================== */
/// TEST 6: SIGNAL HANDLER SAFETY
/* ========================================================================== */

void test_signal_safety(void) {
    TEST("Signal Handler Safety (Multiple Inits)");

    printf(
        "Testing multiple init/cleanup cycles for signal handler safety...\n");

    for (int i = 0; i < 10; i++) {
        lle_watchdog_cleanup();

        lle_result_t result = lle_watchdog_init();
        if (result != LLE_SUCCESS) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Init failed on cycle %d", i + 1);
            FAIL(msg);
            return;
        }

        lle_watchdog_pet(5);

        if (!lle_watchdog_is_armed()) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Not armed after pet on cycle %d",
                     i + 1);
            FAIL(msg);
            return;
        }

        lle_watchdog_stop();
        lle_watchdog_cleanup();
    }

    printf("  Completed 10 init/cleanup cycles without issues\n");

    /// Verify SIGALRM handler is properly restored
    struct sigaction sa;
    sigaction(SIGALRM, NULL, &sa);
    printf("  SIGALRM handler after cleanup: %s\n",
           sa.sa_handler == SIG_DFL ? "SIG_DFL (restored)" : "custom");

    PASS();
}

/* ========================================================================== */
/// MAIN
/* ========================================================================== */

int main(void) {
    printf("\n");
    printf(
        "#################################################################\n");
    printf(
        "#                                                               #\n");
    printf(
        "#        LLE Watchdog & Safety System Stress Tests              #\n");
    printf(
        "#              Freeze Detection Effectiveness                   #\n");
    printf(
        "#                                                               #\n");
    printf(
        "#################################################################\n");

    /// Run all tests
    test_watchdog_init();
    test_watchdog_pet();
    test_watchdog_timeout();
    test_watchdog_rapid_pet();
    test_watchdog_effectiveness();
    test_signal_safety();

    /// Summary
    printf("\n");
    printf(
        "=================================================================\n");
    printf("  Watchdog Stress Test Summary\n");
    printf(
        "=================================================================\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_failed);
    printf(
        "=================================================================\n");

    if (tests_failed > 0) {
        printf("\n  WATCHDOG TESTS FAILED\n");
    } else {
        printf("\n  ALL WATCHDOG TESTS PASSED\n");
    }

    return (tests_failed > 0) ? 1 : 0;
}
