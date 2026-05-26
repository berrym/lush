/*
 * test_terminal_state.c - Unit tests for terminal state management
 *
 * Tests Spec 02 Phase 2: Terminal State Management
 *
 * Test Categories:
 * 1. Interface initialization and cleanup
 * 2. Raw mode operations
 * 3. Window size queries
 * 4. Signal handling (limited testing)
 * 5. Error handling
 */

#include "lle/terminal_abstraction.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ============================================================================
 * INTERFACE INITIALIZATION AND CLEANUP TESTS
 * ============================================================================
 */

TEST(interface_init_basic) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface != NULL);
    ASSERT(interface->terminal_fd >= 0);
    ASSERT(interface->raw_mode_active == false);

    /// Cleanup
    lle_unix_interface_destroy(interface);
}

TEST(interface_init_null_parameter) {
    lle_result_t result = lle_unix_interface_init(NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);
}

TEST(interface_destroy_null) {
    /// Should not crash
    lle_unix_interface_destroy(NULL);
}

TEST(interface_double_destroy) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// First destroy
    lle_unix_interface_destroy(interface);

    /// Second destroy on same pointer is undefined, but we test that
    /// destroying NULL doesn't crash
    lle_unix_interface_destroy(NULL);
}

TEST(interface_preserves_terminal_fd) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    int fd = interface->terminal_fd;
    ASSERT(fd >= 0);

    /// Verify the fd is still valid (skip if not a TTY)
    if (isatty(fd)) {
        struct termios current;
        int tcget_result = tcgetattr(fd, &current);
        ASSERT(tcget_result == 0);
    }

    lle_unix_interface_destroy(interface);
}

/* ============================================================================
 * RAW MODE TESTS
 * ============================================================================
 */

TEST(raw_mode_enter_exit) {
    if (!isatty(STDIN_FILENO)) {
        printf(" SKIP (not a tty)");
        return;
    }

    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// Get original terminal settings
    struct termios original;
    int tcget_result = tcgetattr(STDIN_FILENO, &original);
    ASSERT(tcget_result == 0);

    /// Enter raw mode
    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == true);

    /// Verify raw mode is active
    struct termios raw;
    tcget_result = tcgetattr(STDIN_FILENO, &raw);
    ASSERT(tcget_result == 0);

    /// Check key raw mode characteristics
    ASSERT((raw.c_lflag & ICANON) == 0); /// Non-canonical
    ASSERT((raw.c_lflag & ECHO) == 0);   /// No echo

    /// Exit raw mode
    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == false);

    /// Verify original settings restored
    struct termios restored;
    tcget_result = tcgetattr(STDIN_FILENO, &restored);
    ASSERT(tcget_result == 0);

    /// Compare key flags (exact match may vary by system)
    ASSERT((restored.c_lflag & ICANON) == (original.c_lflag & ICANON));
    ASSERT((restored.c_lflag & ECHO) == (original.c_lflag & ECHO));

    lle_unix_interface_destroy(interface);
}

TEST(raw_mode_idempotent_enter) {
    if (!isatty(STDIN_FILENO)) {
        printf(" SKIP (not a tty)");
        return;
    }

    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// Enter raw mode twice
    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == true);

    /// Exit once should restore
    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    lle_unix_interface_destroy(interface);
}

TEST(raw_mode_idempotent_exit) {
    if (!isatty(STDIN_FILENO)) {
        printf(" SKIP (not a tty)");
        return;
    }

    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// Exit without entering should be safe
    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == false);

    /// Enter and exit twice
    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    lle_unix_interface_destroy(interface);
}

TEST(raw_mode_null_parameter) {
    lle_result_t result = lle_unix_interface_enter_raw_mode(NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    result = lle_unix_interface_exit_raw_mode(NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);
}

TEST(raw_mode_cleanup_on_destroy) {
    if (!isatty(STDIN_FILENO)) {
        printf(" SKIP (not a tty)");
        return;
    }

    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// Get original settings
    struct termios original;
    int tcget_result = tcgetattr(STDIN_FILENO, &original);
    ASSERT(tcget_result == 0);

    /// Enter raw mode
    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);

    /// Destroy without exiting raw mode
    lle_unix_interface_destroy(interface);

    /// Verify terminal restored
    struct termios restored;
    tcget_result = tcgetattr(STDIN_FILENO, &restored);
    ASSERT(tcget_result == 0);

    ASSERT((restored.c_lflag & ICANON) == (original.c_lflag & ICANON));
    ASSERT((restored.c_lflag & ECHO) == (original.c_lflag & ECHO));
}

/* ============================================================================
 * WINDOW SIZE TESTS
 * ============================================================================
 */

TEST(get_window_size_basic) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    size_t width = 0, height = 0;
    result = lle_unix_interface_get_window_size(interface, &width, &height);

    ASSERT(result == LLE_SUCCESS);
    ASSERT(width > 0);
    ASSERT(height > 0);

    /// In TTY environments, expect reasonable bounds.
    /// In non-TTY, we should get at least the fallback values (80x24).
    /// Allow very small values in case COLUMNS/LINES env vars are weird.
    ASSERT(width <= 10000);
    ASSERT(height <= 10000);

    /// Verify cached values match
    ASSERT(interface->current_width == width);
    ASSERT(interface->current_height == height);

    lle_unix_interface_destroy(interface);
}

TEST(get_window_size_null_parameters) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    size_t width = 0, height = 0;

    /// Null interface
    result = lle_unix_interface_get_window_size(NULL, &width, &height);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    /// Null width
    result = lle_unix_interface_get_window_size(interface, NULL, &height);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    /// Null height
    result = lle_unix_interface_get_window_size(interface, &width, NULL);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    lle_unix_interface_destroy(interface);
}

TEST(get_window_size_caching) {
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    size_t width1 = 0, height1 = 0;
    result = lle_unix_interface_get_window_size(interface, &width1, &height1);
    ASSERT(result == LLE_SUCCESS);

    size_t width2 = 0, height2 = 0;
    result = lle_unix_interface_get_window_size(interface, &width2, &height2);
    ASSERT(result == LLE_SUCCESS);

    /// Should return same values (assuming no resize between calls)
    ASSERT(width1 == width2);
    ASSERT(height1 == height2);

    /// Cached values should match
    ASSERT(interface->current_width == width2);
    ASSERT(interface->current_height == height2);

    lle_unix_interface_destroy(interface);
}

TEST(window_size_fallback_values) {
    /// This test verifies that we get reasonable defaults even in
    /// non-terminal environments
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    size_t width = 0, height = 0;
    result = lle_unix_interface_get_window_size(interface, &width, &height);

    /// Should succeed even if not a tty (fallback to 80x24 or env vars)
    ASSERT(result == LLE_SUCCESS);
    ASSERT(width > 0);
    ASSERT(height > 0);

    lle_unix_interface_destroy(interface);
}

/* ============================================================================
 * READ EVENT TESTS (VERIFICATION)
 * ============================================================================
 */

TEST(read_event_stub) {
    /// Phase 2 only provides a stub for read_event, which will be
    /// fully implemented in Phase 3. Verify the stub exists and
    /// handles null parameters correctly.

    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    lle_input_event_t event;

    /// Null interface
    result = lle_unix_interface_read_event(NULL, &event, 0);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    /// Null event
    result = lle_unix_interface_read_event(interface, NULL, 0);
    ASSERT(result == LLE_ERROR_INVALID_PARAMETER);

    lle_unix_interface_destroy(interface);
}

/* ============================================================================
 * INTEGRATION TESTS
 * ============================================================================
 */

TEST(multiple_interfaces) {
    /// Verify we can create multiple interface instances
    lle_unix_interface_t *interface1 = NULL;
    lle_unix_interface_t *interface2 = NULL;

    lle_result_t result1 = lle_unix_interface_init(&interface1);
    ASSERT(result1 == LLE_SUCCESS);
    ASSERT(interface1 != NULL);

    lle_result_t result2 = lle_unix_interface_init(&interface2);
    ASSERT(result2 == LLE_SUCCESS);
    ASSERT(interface2 != NULL);

    /// Should be different instances
    ASSERT(interface1 != interface2);

    /// Cleanup
    lle_unix_interface_destroy(interface1);
    lle_unix_interface_destroy(interface2);
}

TEST(full_lifecycle) {
    if (!isatty(STDIN_FILENO)) {
        printf(" SKIP (not a tty)");
        return;
    }

    /// Test complete lifecycle: init -> raw mode -> operations -> cleanup
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    ASSERT(result == LLE_SUCCESS);

    /// Get window size
    size_t width = 0, height = 0;
    result = lle_unix_interface_get_window_size(interface, &width, &height);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(width > 0 && height > 0);

    /// Enter raw mode
    result = lle_unix_interface_enter_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == true);

    /// Get window size again in raw mode
    size_t width2 = 0, height2 = 0;
    result = lle_unix_interface_get_window_size(interface, &width2, &height2);
    ASSERT(result == LLE_SUCCESS);

    /// Exit raw mode
    result = lle_unix_interface_exit_raw_mode(interface);
    ASSERT(result == LLE_SUCCESS);
    ASSERT(interface->raw_mode_active == false);

    /// Cleanup
    lle_unix_interface_destroy(interface);
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================
 */

int main(void) {
    printf("Running Terminal State Management Tests (Spec 02 Phase 2)\n");
    printf("============================================================\n\n");

    printf("Interface Initialization Tests:\n");
    RUN_TEST(interface_init_basic);
    RUN_TEST(interface_init_null_parameter);
    RUN_TEST(interface_destroy_null);
    RUN_TEST(interface_double_destroy);
    RUN_TEST(interface_preserves_terminal_fd);

    printf("\nRaw Mode Tests:\n");
    RUN_TEST(raw_mode_enter_exit);
    RUN_TEST(raw_mode_idempotent_enter);
    RUN_TEST(raw_mode_idempotent_exit);
    RUN_TEST(raw_mode_null_parameter);
    RUN_TEST(raw_mode_cleanup_on_destroy);

    printf("\nWindow Size Tests:\n");
    RUN_TEST(get_window_size_basic);
    RUN_TEST(get_window_size_null_parameters);
    RUN_TEST(get_window_size_caching);
    RUN_TEST(window_size_fallback_values);

    printf("\nRead Event Tests (Stub):\n");
    RUN_TEST(read_event_stub);

    printf("\nIntegration Tests:\n");
    RUN_TEST(multiple_interfaces);
    RUN_TEST(full_lifecycle);

    return TEST_RESULT();
}
