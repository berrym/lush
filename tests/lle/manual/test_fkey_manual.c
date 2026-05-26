/**
 * @file test_fkey_manual.c
 * @brief Manual test harness for fkey manual
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * test_fkey_manual.c - Manual F-key detection test
 *
 * Interactive test program for verifying F-key detection in real terminals.
 *
 * Usage:
 *   1. Compile and run: make -C tests/lle/manual test_fkey_manual &&
 * tests/lle/manual/test_fkey_manual
 *   2. Or in lush: ./test_fkey_manual
 *   3. Press F1-F12 keys and verify they're detected correctly
 *   4. Press Ctrl+C to exit
 *
 * This validates F-key detection in the actual production environment.
 */

#include "lle/terminal_abstraction.h"
#include "lush_memory_pool.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// External memory pool reference (defined in lush_memory_pool.c)
extern lush_memory_pool_system_t *global_memory_pool;

/// Global state for cleanup
static lle_unix_interface_t *g_interface = NULL;
static lle_terminal_capabilities_t *g_capabilities = NULL;
static volatile sig_atomic_t g_running = 1;

/// Signal handler for clean exit
static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n\nCaught Ctrl+C - exiting...\n");
}

/// Convert key code to string
static const char *key_to_string(lle_special_key_t key) {
    switch (key) {
    case LLE_KEY_F1:
        return "F1";
    case LLE_KEY_F2:
        return "F2";
    case LLE_KEY_F3:
        return "F3";
    case LLE_KEY_F4:
        return "F4";
    case LLE_KEY_F5:
        return "F5";
    case LLE_KEY_F6:
        return "F6";
    case LLE_KEY_F7:
        return "F7";
    case LLE_KEY_F8:
        return "F8";
    case LLE_KEY_F9:
        return "F9";
    case LLE_KEY_F10:
        return "F10";
    case LLE_KEY_F11:
        return "F11";
    case LLE_KEY_F12:
        return "F12";
    case LLE_KEY_UP:
        return "UP";
    case LLE_KEY_DOWN:
        return "DOWN";
    case LLE_KEY_LEFT:
        return "LEFT";
    case LLE_KEY_RIGHT:
        return "RIGHT";
    case LLE_KEY_HOME:
        return "HOME";
    case LLE_KEY_END:
        return "END";
    case LLE_KEY_PAGE_UP:
        return "PAGE_UP";
    case LLE_KEY_PAGE_DOWN:
        return "PAGE_DOWN";
    case LLE_KEY_INSERT:
        return "INSERT";
    case LLE_KEY_DELETE:
        return "DELETE";
    case LLE_KEY_BACKSPACE:
        return "BACKSPACE";
    case LLE_KEY_TAB:
        return "TAB";
    case LLE_KEY_ENTER:
        return "ENTER";
    case LLE_KEY_ESCAPE:
        return "ESCAPE";
    default:
        return "UNKNOWN";
    }
}

/// Display help text
static void print_help(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("         F-Key Detection Manual Test\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("This program tests F-key detection in your terminal.\n");
    printf("\n");
    printf("Instructions:\n");
    printf("  1. Press F1-F12 keys on your keyboard\n");
    printf("  2. Watch the detection output below\n");
    printf("  3. Verify the correct key is detected\n");
    printf("  4. Press Ctrl+C when done\n");
    printf("\n");
    printf("Also try:\n");
    printf("  - Arrow keys (Up, Down, Left, Right)\n");
    printf("  - Home, End, PageUp, PageDown\n");
    printf("  - Insert, Delete\n");
    printf("\n");
    printf("Parser Status:\n");
    printf("  - Sequence Parser: %s\n",
           g_interface->sequence_parser ? "INITIALIZED" : "NOT INITIALIZED");
    printf("  - Key Detector: %s\n",
           g_interface->key_detector ? "INITIALIZED" : "NOT INITIALIZED");
    printf("\n");
    printf("Press any key to begin (or Ctrl+C to exit)...\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
}

int main(void) {
    /// Install signal handler
    signal(SIGINT, handle_sigint);

    /// Initialize memory pool system (required for parser initialization)
    lush_pool_config_t pool_config = lush_pool_get_default_config();
    lush_pool_error_t pool_result = lush_pool_init(&pool_config);
    if (pool_result != LUSH_POOL_SUCCESS) {
        fprintf(stderr, "Failed to initialize memory pool: %d\n", pool_result);
        return 1;
    }

    /// Initialize unix interface
    lle_result_t result = lle_unix_interface_init(&g_interface);
    if (result != LLE_SUCCESS) {
        fprintf(stderr, "Failed to initialize unix interface: %d\n", result);
        return 1;
    }

    /// Initialize capabilities
    result = lle_capabilities_detect_environment(&g_capabilities, g_interface);
    if (result != LLE_SUCCESS) {
        fprintf(stderr, "Failed to detect capabilities: %d\n", result);
        lle_unix_interface_destroy(g_interface);
        return 1;
    }

    /// Initialize sequence parser + key_detector
    result = lle_unix_interface_init_sequence_parser(
        g_interface, g_capabilities, (lle_memory_pool_t *)global_memory_pool);
    if (result != LLE_SUCCESS) {
        fprintf(stderr, "Failed to initialize parser: %d\n", result);
        lle_capabilities_destroy(g_capabilities);
        lle_unix_interface_destroy(g_interface);
        return 1;
    }

    /// Enter raw mode
    result = lle_unix_interface_enter_raw_mode(g_interface);
    if (result != LLE_SUCCESS) {
        fprintf(stderr, "Failed to enter raw mode: %d\n", result);
        lle_capabilities_destroy(g_capabilities);
        lle_unix_interface_destroy(g_interface);
        return 1;
    }

    /// Display help
    print_help();

    /// Event loop
    int event_count = 0;
    while (g_running) {
        lle_input_event_t event;
        result = lle_unix_interface_read_event(g_interface, &event, 100);

        if (result != LLE_SUCCESS) {
            printf("Error reading event: %d\n", result);
            continue;
        }

        switch (event.type) {
        case LLE_INPUT_TYPE_CHARACTER:
            /// Regular character
            if (event.data.character.codepoint == 3) { /// Ctrl+C
                g_running = 0;
                printf("[%04d] Ctrl+C detected - exiting\n", ++event_count);
            } else if (event.data.character.codepoint >= 32 &&
                       event.data.character.codepoint < 127) {
                printf("[%04d] CHARACTER: '%c' (codepoint: %u)\n",
                       ++event_count, (char)event.data.character.codepoint,
                       event.data.character.codepoint);
            } else {
                printf("[%04d] CHARACTER: (codepoint: %u, control char)\n",
                       ++event_count, event.data.character.codepoint);
            }
            break;

        case LLE_INPUT_TYPE_SPECIAL_KEY:
            /// Special key - this is what we want to see!
            printf("[%04d] SPECIAL KEY: %s", ++event_count,
                   key_to_string(event.data.special_key.key));

            /// Show modifiers if any
            if (event.data.special_key.modifiers) {
                printf(" (");
                if (event.data.special_key.modifiers & LLE_MOD_SHIFT)
                    printf("Shift+");
                if (event.data.special_key.modifiers & LLE_MOD_ALT)
                    printf("Alt+");
                if (event.data.special_key.modifiers & LLE_MOD_CTRL)
                    printf("Ctrl+");
                if (event.data.special_key.modifiers & LLE_MOD_META)
                    printf("Meta+");
                printf("\b)"); /// Remove last '+'
            }
            printf("\n");

            /// Special success message for F-keys
            if (event.data.special_key.key >= LLE_KEY_F1 &&
                event.data.special_key.key <= LLE_KEY_F12) {
                printf("     >> F-key detected successfully!\n");
            }
            break;

        case LLE_INPUT_TYPE_WINDOW_RESIZE:
            printf("[%04d] WINDOW RESIZE: %zux%zu\n", ++event_count,
                   event.data.resize.new_width, event.data.resize.new_height);
            break;

        case LLE_INPUT_TYPE_TIMEOUT:
            /// Ignore timeouts in display
            break;

        case LLE_INPUT_TYPE_EOF:
            printf("[%04d] EOF detected\n", ++event_count);
            g_running = 0;
            break;

        case LLE_INPUT_TYPE_ERROR:
            printf("[%04d] ERROR: %s\n", ++event_count,
                   event.data.error.error_message);
            break;

        default:
            printf("[%04d] UNKNOWN EVENT TYPE: %d\n", ++event_count,
                   event.type);
            break;
        }

        fflush(stdout);
    }

    /// Cleanup
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Test Summary:\n");
    printf("  Total events processed: %d\n", event_count);
    printf("  Parser status: %s\n",
           g_interface->sequence_parser ? "Active" : "Inactive");
    printf("\n");
    printf("Cleaning up and restoring terminal...\n");

    lle_unix_interface_exit_raw_mode(g_interface);
    lle_capabilities_destroy(g_capabilities);
    lle_unix_interface_destroy(g_interface);

    printf("Done.\n");
    return 0;
}
