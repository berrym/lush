/**
 * @file test_fkey_detection.c
 * @brief Integration tests for fkey detection
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * test_fkey_detection.c - Integration test for F-key detection
 *
 * Tests F1-F12 key detection through the full terminal abstraction stack:
 * - terminal_abstraction_init() (initializes parser + key_detector)
 * - Simulates F-key escape sequences
 * - Verifies correct KEY events with proper key codes
 *
 * This validates the production code path, not just fallback behavior.
 */

#include "lle/terminal_abstraction.h"
#include "lush_memory_pool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// Test counter
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
    static void name(void);                                                    \
    static void run_##name(void) {                                             \
        tests_run++;                                                           \
        printf("  Running %s...", #name);                                      \
        fflush(stdout);                                                        \
        name();                                                                \
        tests_passed++;                                                        \
        printf(" PASS\n");                                                     \
    }                                                                          \
    static void name(void)

/// Helper to create a pipe with data
static int create_pipe_with_data(const void *data, size_t len, int *write_fd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }

    if (data && len > 0) {
        ssize_t written = write(pipefd[1], data, len);
        if (written != (ssize_t)len) {
            perror("write");
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }

    if (write_fd) {
        *write_fd = pipefd[1];
    } else {
        close(pipefd[1]);
    }

    return pipefd[0];
}

/// External memory pool reference (defined in lush_memory_pool.c)
extern lush_memory_pool_system_t *global_memory_pool;

/* ============================================================================
 * F-KEY DETECTION TESTS (WITH FULL PARSER INITIALIZATION)
 * ============================================================================
 */

TEST(test_f1_detection_with_parser) {
    /// F1 key: ESC O P (SS3 sequence)
    unsigned char f1_data[] = {0x1B, 'O', 'P'};
    int pipe_fd = create_pipe_with_data(f1_data, sizeof(f1_data), NULL);
    assert(pipe_fd >= 0);

    /// Initialize unix interface
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    assert(result == LLE_SUCCESS);
    assert(interface != NULL);

    /// Initialize capabilities for parser
    lle_terminal_capabilities_t *capabilities = NULL;
    result = lle_capabilities_detect_environment(&capabilities, interface);
    assert(result == LLE_SUCCESS);

    /// Initialize sequence parser + key_detector (this is what we're testing)
    result = lle_unix_interface_init_sequence_parser(
        interface, capabilities, (lle_memory_pool_t *)global_memory_pool);
    assert(result == LLE_SUCCESS);

    /// Verify parser and key_detector were initialized
    assert(interface->sequence_parser != NULL);
    assert(interface->key_detector != NULL);

    /// Redirect stdin to our pipe
    int saved_stdin = dup(STDIN_FILENO);
    dup2(pipe_fd, STDIN_FILENO);
    interface->terminal_fd = STDIN_FILENO;

    /// Read F1 key - should be properly detected by key_detector
    lle_input_event_t event;
    result = lle_unix_interface_read_event(interface, &event, 1000);

    /// With parser initialized, we might need multiple reads to accumulate the
    /// sequence
    /// Try reading up to 3 times to get the complete event
    int read_attempts = 0;
    while (result == LLE_SUCCESS && event.type == LLE_INPUT_TYPE_TIMEOUT &&
           read_attempts < 3) {
        result = lle_unix_interface_read_event(interface, &event, 100);
        read_attempts++;
    }

    assert(result == LLE_SUCCESS);

    /// Verify F1 was detected correctly
    assert(event.type == LLE_INPUT_TYPE_SPECIAL_KEY);
    assert(event.data.special_key.key == LLE_KEY_F1);

    /// Cleanup
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    close(pipe_fd);
    lle_capabilities_destroy(capabilities);
    lle_unix_interface_destroy(interface);
}

TEST(test_f5_detection_with_parser) {
    /// F5 key: ESC [ 1 5 ~ (CSI sequence)
    unsigned char f5_data[] = {0x1B, '[', '1', '5', '~'};
    int pipe_fd = create_pipe_with_data(f5_data, sizeof(f5_data), NULL);
    assert(pipe_fd >= 0);

    /// Initialize unix interface
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    assert(result == LLE_SUCCESS);

    /// Initialize capabilities for parser
    lle_terminal_capabilities_t *capabilities = NULL;
    result = lle_capabilities_detect_environment(&capabilities, interface);
    assert(result == LLE_SUCCESS);

    /// Initialize sequence parser + key_detector
    result = lle_unix_interface_init_sequence_parser(
        interface, capabilities, (lle_memory_pool_t *)global_memory_pool);
    assert(result == LLE_SUCCESS);
    assert(interface->sequence_parser != NULL);
    assert(interface->key_detector != NULL);

    /// Redirect stdin to our pipe
    int saved_stdin = dup(STDIN_FILENO);
    dup2(pipe_fd, STDIN_FILENO);
    interface->terminal_fd = STDIN_FILENO;

    /// Read F5 key sequence
    lle_input_event_t event;
    result = lle_unix_interface_read_event(interface, &event, 1000);

    /// Parser needs multiple reads to accumulate complete sequence
    int read_attempts = 0;
    while (result == LLE_SUCCESS && event.type == LLE_INPUT_TYPE_TIMEOUT &&
           read_attempts < 5) {
        result = lle_unix_interface_read_event(interface, &event, 100);
        read_attempts++;
    }

    assert(result == LLE_SUCCESS);

    /// Verify F5 was detected correctly
    assert(event.type == LLE_INPUT_TYPE_SPECIAL_KEY);
    assert(event.data.special_key.key == LLE_KEY_F5);

    /// Cleanup
    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    close(pipe_fd);
    lle_capabilities_destroy(capabilities);
    lle_unix_interface_destroy(interface);
}

TEST(test_arrow_key_with_parser) {
    /// Up arrow: ESC [ A (CSI sequence)
    unsigned char up_data[] = {0x1B, '[', 'A'};
    int pipe_fd = create_pipe_with_data(up_data, sizeof(up_data), NULL);
    assert(pipe_fd >= 0);

    /// Initialize unix interface
    lle_unix_interface_t *interface = NULL;
    lle_result_t result = lle_unix_interface_init(&interface);
    assert(result == LLE_SUCCESS);

    /// Initialize capabilities for parser
    lle_terminal_capabilities_t *capabilities = NULL;
    result = lle_capabilities_detect_environment(&capabilities, interface);
    assert(result == LLE_SUCCESS);

    /// Initialize sequence parser + key_detector
    result = lle_unix_interface_init_sequence_parser(
        interface, capabilities, (lle_memory_pool_t *)global_memory_pool);
    assert(result == LLE_SUCCESS);

    int saved_stdin = dup(STDIN_FILENO);
    dup2(pipe_fd, STDIN_FILENO);
    interface->terminal_fd = STDIN_FILENO;

    /// Read up arrow
    lle_input_event_t event;
    result = lle_unix_interface_read_event(interface, &event, 1000);

    int read_attempts = 0;
    while (result == LLE_SUCCESS && event.type == LLE_INPUT_TYPE_TIMEOUT &&
           read_attempts < 3) {
        result = lle_unix_interface_read_event(interface, &event, 100);
        read_attempts++;
    }

    assert(result == LLE_SUCCESS);

    /// Verify UP arrow was detected correctly
    assert(event.type == LLE_INPUT_TYPE_SPECIAL_KEY);
    assert(event.data.special_key.key == LLE_KEY_UP);

    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    close(pipe_fd);
    lle_capabilities_destroy(capabilities);
    lle_unix_interface_destroy(interface);
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running F-Key Detection Integration Tests\n");
    printf("===========================================\n");
    printf("Testing with FULL parser + key_detector initialization\n\n");

    /// Initialize memory pool system (required for parser initialization)
    lush_pool_config_t pool_config = lush_pool_get_default_config();
    lush_pool_error_t pool_result = lush_pool_init(&pool_config);
    if (pool_result != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "Failed to initialize memory pool: %d\n", pool_result);
        return 1;
    }
    printf("Memory pool initialized successfully\n\n");

    printf("F-Key Detection Tests:\n");
    run_test_f1_detection_with_parser();
    run_test_f5_detection_with_parser();
    run_test_arrow_key_with_parser();

    printf("\n===========================================\n");
    printf("Test Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
