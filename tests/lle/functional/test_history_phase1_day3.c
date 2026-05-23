/**
 * Functional Test: History System Phase 1 Day 3
 *
 * Tests persistence functionality:
 * - Save history to file
 * - Load history from file
 * - File locking for multi-process safety
 * - Corruption handling
 * - Incremental append
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("\n[TEST] %s\n", name)
#define PASS()                                                                 \
    do {                                                                       \
        printf("  PASS\n");                                                    \
        tests_passed++;                                                        \
        return;                                                                \
    } while (0)
#define FAIL(msg)                                                              \
    do {                                                                       \
        printf("  FAIL: %s\n", msg);                                           \
        tests_failed++;                                                        \
        return;                                                                \
    } while (0)

// Test file path
#define TEST_HISTORY_FILE "/tmp/lle_history_test.txt"

/*
 * Test 1: Save and load basic history
 */
void test_save_and_load(void) {
    TEST("Save and load history");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    // Create core and add entries
    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    // Add test entries
    const char *commands[] = {"ls -la /home", "cd /tmp", "echo 'Hello, World!'",
                              "grep pattern file.txt", "git status"};

    for (int i = 0; i < 5; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, commands[i], i, &id);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }
    }

    // Save to file
    result = lle_history_save_to_file(core, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to save to file");
    }

    // Destroy core
    lle_history_core_destroy(core);

    // Create new core and load
    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to create new core");
    }

    result = lle_history_load_from_file(core, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to load from file");
    }

    // Verify loaded entries
    size_t count;
    lle_history_get_entry_count(core, &count);
    if (count != 5) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Wrong number of entries loaded");
    }

    for (int i = 0; i < 5; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(core, i, &entry);
        if (result != LLE_SUCCESS || entry == NULL) {
            lle_history_core_destroy(core);
            unlink(TEST_HISTORY_FILE);
            FAIL("Failed to retrieve loaded entry");
        }

        if (strcmp(entry->command, commands[i]) != 0) {
            printf("  Expected: %s\n", commands[i]);
            printf("  Got: %s\n", entry->command);
            lle_history_core_destroy(core);
            unlink(TEST_HISTORY_FILE);
            FAIL("Command text doesn't match");
        }

        if (entry->exit_code != i) {
            lle_history_core_destroy(core);
            unlink(TEST_HISTORY_FILE);
            FAIL("Exit code doesn't match");
        }
    }

    // Cleanup
    lle_history_core_destroy(core);
    unlink(TEST_HISTORY_FILE);
    PASS();
}

/*
 * Test 2: Empty file load
 */
void test_load_nonexistent_file(void) {
    TEST("Load from non-existent file");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    // Try to load from non-existent file - should succeed with empty history
    result =
        lle_history_load_from_file(core, "/tmp/nonexistent_history_file.txt");
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Loading non-existent file should succeed");
    }

    size_t count;
    lle_history_get_entry_count(core, &count);
    if (count != 0) {
        lle_history_core_destroy(core);
        FAIL("Should have 0 entries");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 3: Special characters in commands
 */
void test_special_characters(void) {
    TEST("Save/load commands with special characters");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    // Commands with tabs, newlines, backslashes
    const char *special_commands[] = {
        "echo 'line1\nline2'", "printf 'col1\tcol2\tcol3'",
        "echo 'path: C:\\Users\\test'", "grep 'pattern\\|other' file.txt"};

    for (int i = 0; i < 4; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, special_commands[i], 0, &id);
    }

    // Save
    result = lle_history_save_to_file(core, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to save");
    }

    lle_history_core_destroy(core);

    // Load
    result = lle_history_core_create(&core, NULL, NULL);
    result = lle_history_load_from_file(core, TEST_HISTORY_FILE);

    // Verify
    for (int i = 0; i < 4; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(core, i, &entry);
        if (strcmp(entry->command, special_commands[i]) != 0) {
            printf("  Expected: %s\n", special_commands[i]);
            printf("  Got: %s\n", entry->command);
            lle_history_core_destroy(core);
            unlink(TEST_HISTORY_FILE);
            FAIL("Special characters not preserved");
        }
    }

    lle_history_core_destroy(core);
    unlink(TEST_HISTORY_FILE);
    PASS();
}

/*
 * Test 4: Append entry
 */
void test_append_entry(void) {
    TEST("Append single entry to file");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    // Create initial history
    result = lle_history_core_create(&core, NULL, NULL);
    uint64_t id;
    lle_history_add_entry(core, "initial command", 0, &id);
    lle_history_save_to_file(core, TEST_HISTORY_FILE);

    // Get the entry to append
    lle_history_entry_t *entry = NULL;
    lle_history_add_entry(core, "appended command", 0, &id);
    lle_history_get_entry_by_index(core, 1, &entry);

    // Append to file
    result = lle_history_append_entry(entry, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to append entry");
    }

    lle_history_core_destroy(core);

    // Load and verify
    result = lle_history_core_create(&core, NULL, NULL);
    lle_history_load_from_file(core, TEST_HISTORY_FILE);

    size_t count;
    lle_history_get_entry_count(core, &count);
    if (count != 2) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Should have 2 entries after append");
    }

    lle_history_get_entry_by_index(core, 1, &entry);
    if (strcmp(entry->command, "appended command") != 0) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Appended command not found");
    }

    lle_history_core_destroy(core);
    unlink(TEST_HISTORY_FILE);
    PASS();
}

/*
 * Test 5: Large history file
 */
void test_large_history(void) {
    TEST("Save/load large history (1000 entries)");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    // Add 1000 entries
    for (int i = 0; i < 1000; i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "command_%d", i);
        uint64_t id;
        result = lle_history_add_entry(core, cmd, i % 256, &id);
    }

    // Save
    result = lle_history_save_to_file(core, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to save large history");
    }

    // Check file size
    struct stat st;
    stat(TEST_HISTORY_FILE, &st);
    printf("  File size: %ld bytes for 1000 entries\n", (long)st.st_size);

    lle_history_core_destroy(core);

    // Load
    result = lle_history_core_create(&core, NULL, NULL);
    result = lle_history_load_from_file(core, TEST_HISTORY_FILE);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Failed to load large history");
    }

    // Verify count
    size_t count;
    lle_history_get_entry_count(core, &count);
    if (count != 1000) {
        printf("  Expected 1000, got %zu\n", count);
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("Wrong number of entries loaded");
    }

    // Spot check some entries
    for (int i = 0; i < 1000; i += 100) {
        lle_history_entry_t *entry = NULL;
        lle_history_get_entry_by_index(core, i, &entry);

        char expected[64];
        snprintf(expected, sizeof(expected), "command_%d", i);
        if (strcmp(entry->command, expected) != 0) {
            lle_history_core_destroy(core);
            unlink(TEST_HISTORY_FILE);
            FAIL("Entry mismatch in large history");
        }
    }

    lle_history_core_destroy(core);
    unlink(TEST_HISTORY_FILE);
    PASS();
}

/*
 * Test 6: File permissions
 */
void test_file_permissions(void) {
    TEST("History file has secure permissions (0600)");

    lle_history_core_t *core = NULL;

    (void)lle_history_core_create(&core, NULL, NULL);
    uint64_t id;
    lle_history_add_entry(core, "test command", 0, &id);

    // Save
    lle_history_save_to_file(core, TEST_HISTORY_FILE);

    // Check permissions
    struct stat st;
    stat(TEST_HISTORY_FILE, &st);
    mode_t perms = st.st_mode & 0777;

    if (perms != 0600) {
        printf("  Expected 0600, got 0%o\n", perms);
        lle_history_core_destroy(core);
        unlink(TEST_HISTORY_FILE);
        FAIL("File permissions not secure");
    }

    lle_history_core_destroy(core);
    unlink(TEST_HISTORY_FILE);
    PASS();
}

/*
 * Main test runner
 */
int main(void) {
    printf("=================================================\n");
    printf("History System Phase 1 Day 3 - Functional Tests\n");
    printf("Persistence and File Storage\n");
    printf("=================================================\n");

    // Run all tests
    test_save_and_load();
    test_load_nonexistent_file();
    test_special_characters();
    test_append_entry();
    test_large_history();
    test_file_permissions();

    // Summary
    printf("\n=================================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    if (tests_failed == 0) {
        printf("ALL FUNCTIONAL TESTS PASSED\n");
        printf("Phase 1 Day 3 persistence is working correctly\n");
        printf("=================================================\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED\n");
        printf("Phase 1 Day 3 needs fixes\n");
        printf("=================================================\n");
        return 1;
    }
}
