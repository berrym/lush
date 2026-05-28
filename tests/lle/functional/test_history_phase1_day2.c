/**
 * @file test_history_phase1_day2.c
 * @brief Functional tests for history phase1 day2
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/**
 * Functional Test: History System Phase 1 Day 2
 *
 * Tests indexing functionality:
 * - Hashtable index creation and destruction
 * - Fast O(1) lookup by ID
 * - Index insert/remove operations
 * - Reverse index access (for Up arrow navigation)
 * - Get last N entries
 * - Index rebuild
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/// Test counter
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

/*
 * Test 1: Hashtable index is created when use_indexing is true
 */
void test_index_creation(void) {
    TEST("Index creation when enabled");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    /// Create core with default config (use_indexing = true by default)
    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Check that index was created
    if (core->entry_lookup == NULL) {
        lle_history_core_destroy(core);
        FAIL("Index not created despite use_indexing=true");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 2: Fast lookup by ID using hashtable
 */
void test_fast_id_lookup(void) {
    TEST("Fast O(1) lookup by ID using hashtable");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add 1000 entries
    uint64_t ids[1000];
    for (int i = 0; i < 1000; i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "command_%d", i);
        result = lle_history_add_entry(core, cmd, 0, &ids[i]);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }
    }

    /// Time lookups using ID (should be O(1) via hashtable)
    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < 1000; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_id(core, ids[i], &entry);
        if (result != LLE_SUCCESS || entry == NULL) {
            lle_history_core_destroy(core);
            FAIL("Failed to lookup entry by ID");
        }

        /// Verify it's the correct entry
        char expected_cmd[64];
        snprintf(expected_cmd, sizeof(expected_cmd), "command_%d", i);
        if (strcmp(entry->command, expected_cmd) != 0) {
            lle_history_core_destroy(core);
            FAIL("Retrieved wrong entry");
        }
    }

    gettimeofday(&end, NULL);
    uint64_t elapsed_us =
        (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);

    /// O(1) hashtable lookup should be very fast - under 10ms for 1000 lookups
    if (elapsed_us > 10000) {
        printf("  WARNING: Lookup took %lu us (expected < 10000 us)\n",
               (unsigned long)elapsed_us);
    } else {
        printf("  Performance: 1000 lookups in %lu us (%.2f us/lookup)\n",
               (unsigned long)elapsed_us, elapsed_us / 1000.0);
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 3: Reverse index access (for Up arrow)
 */
void test_reverse_index_access(void) {
    TEST("Reverse index access (0 = newest)");

    lle_history_core_t *core = NULL;
    lle_history_entry_t *entry = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entries
    const char *commands[] = {"first", "second", "third", "fourth", "fifth"};
    for (int i = 0; i < 5; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, commands[i], 0, &id);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }
    }

    /// Test reverse indexing
    result = lle_history_get_entry_by_reverse_index(core, 0, &entry);
    if (result != LLE_SUCCESS || entry == NULL) {
        lle_history_core_destroy(core);
        FAIL("Failed to get reverse index 0");
    }
    if (strcmp(entry->command, "fifth") != 0) {
        lle_history_core_destroy(core);
        FAIL("Reverse index 0 should be 'fifth'");
    }

    result = lle_history_get_entry_by_reverse_index(core, 1, &entry);
    if (result != LLE_SUCCESS || strcmp(entry->command, "fourth") != 0) {
        lle_history_core_destroy(core);
        FAIL("Reverse index 1 should be 'fourth'");
    }

    result = lle_history_get_entry_by_reverse_index(core, 4, &entry);
    if (result != LLE_SUCCESS || strcmp(entry->command, "first") != 0) {
        lle_history_core_destroy(core);
        FAIL("Reverse index 4 should be 'first'");
    }

    /// Test out of bounds
    result = lle_history_get_entry_by_reverse_index(core, 5, &entry);
    if (result != LLE_ERROR_INVALID_RANGE) {
        lle_history_core_destroy(core);
        FAIL("Out of bounds should return LLE_ERROR_INVALID_RANGE");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 4: Get last N entries
 */
void test_get_last_n_entries(void) {
    TEST("Get last N entries (most recent)");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add 10 entries
    for (int i = 0; i < 10; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "cmd_%d", i);
        uint64_t id;
        result = lle_history_add_entry(core, cmd, 0, &id);
    }

    /// Get last 3 entries
    lle_history_entry_t *entries[3];
    size_t count = 0;

    result = lle_history_get_last_n_entries(core, 3, entries, &count);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to get last 3 entries");
    }

    if (count != 3) {
        lle_history_core_destroy(core);
        FAIL("Should return 3 entries");
    }

    /// Verify they're the last 3 (cmd_7, cmd_8, cmd_9)
    if (strcmp(entries[0]->command, "cmd_7") != 0 ||
        strcmp(entries[1]->command, "cmd_8") != 0 ||
        strcmp(entries[2]->command, "cmd_9") != 0) {
        lle_history_core_destroy(core);
        FAIL("Wrong entries returned");
    }

    /// Test requesting more than available
    lle_history_entry_t *big_array[20];
    result = lle_history_get_last_n_entries(core, 20, big_array, &count);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed when requesting more than available");
    }

    if (count != 10) {
        lle_history_core_destroy(core);
        FAIL("Should return 10 (all available entries)");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 5: Index rebuild
 */
void test_index_rebuild(void) {
    TEST("Rebuild hashtable index");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entries
    uint64_t ids[5];
    for (int i = 0; i < 5; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "cmd_%d", i);
        result = lle_history_add_entry(core, cmd, 0, &ids[i]);
    }

    /// Rebuild index
    result = lle_history_rebuild_index(core);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to rebuild index");
    }

    /// Verify all entries still accessible
    for (int i = 0; i < 5; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_id(core, ids[i], &entry);
        if (result != LLE_SUCCESS || entry == NULL) {
            lle_history_core_destroy(core);
            FAIL("Entry not found after rebuild");
        }
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 6: Index operations - direct API
 */
void test_index_operations(void) {
    TEST("Direct index operations (insert/lookup/remove)");

    lle_hashtable_t *index = NULL;
    lle_result_t result;

    /// Create index
    result = lle_history_index_create(&index, 100);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create index");
    }

    /// Create a test entry
    lle_history_entry_t *entry = NULL;
    result = lle_history_entry_create(&entry, "test command", NULL);
    if (result != LLE_SUCCESS) {
        lle_history_index_destroy(index);
        FAIL("Failed to create entry");
    }
    entry->entry_id = 42;

    /// Insert into index
    result = lle_history_index_insert(index, 42, entry);
    if (result != LLE_SUCCESS) {
        lle_history_entry_destroy(entry, NULL);
        lle_history_index_destroy(index);
        FAIL("Failed to insert into index");
    }

    /// Lookup
    lle_history_entry_t *found = NULL;
    result = lle_history_index_lookup(index, 42, &found);
    if (result != LLE_SUCCESS || found == NULL) {
        lle_history_entry_destroy(entry, NULL);
        lle_history_index_destroy(index);
        FAIL("Failed to lookup from index");
    }

    if (found != entry) {
        lle_history_entry_destroy(entry, NULL);
        lle_history_index_destroy(index);
        FAIL("Lookup returned wrong entry");
    }

    /// Remove from index
    result = lle_history_index_remove(index, 42);
    if (result != LLE_SUCCESS) {
        lle_history_entry_destroy(entry, NULL);
        lle_history_index_destroy(index);
        FAIL("Failed to remove from index");
    }

    /// Verify removed
    found = NULL;
    result = lle_history_index_lookup(index, 42, &found);
    if (result != LLE_SUCCESS || found != NULL) {
        lle_history_entry_destroy(entry, NULL);
        lle_history_index_destroy(index);
        FAIL("Entry still in index after removal");
    }

    /// Cleanup
    lle_history_entry_destroy(entry, NULL);
    lle_history_index_destroy(index);
    PASS();
}

/*
 * Main test runner
 */
int main(void) {
    printf("=================================================\n");
    printf("History System Phase 1 Day 2 - Functional Tests\n");
    printf("Indexing and Fast Lookup\n");
    printf("=================================================\n");

    /// Run all tests
    test_index_creation();
    test_fast_id_lookup();
    test_reverse_index_access();
    test_get_last_n_entries();
    test_index_rebuild();
    test_index_operations();

    /// Summary
    printf("\n=================================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    if (tests_failed == 0) {
        printf("ALL FUNCTIONAL TESTS PASSED\n");
        printf("Phase 1 Day 2 indexing is working correctly\n");
        printf("=================================================\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED\n");
        printf("Phase 1 Day 2 needs fixes\n");
        printf("=================================================\n");
        return 1;
    }
}
