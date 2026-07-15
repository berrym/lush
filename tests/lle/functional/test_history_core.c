/**
 * @file test_history_core.c
 * @brief Functional tests for the LLE history core (entry add/get, config,
 * lifecycle)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * Functional Test: History core
 *
 * Tests actual runtime behavior of core history engine:
 * - Lifecycle management (create/destroy)
 * - Entry creation and destruction
 * - Adding entries to history
 * - Retrieving entries by index and ID
 * - Statistics tracking
 * - Configuration management
 *
 * Unlike compliance tests which verify API structure, these tests
 * verify actual functionality and behavior.
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Test 1: Create and destroy history core
 */
void test_history_core_lifecycle(void) {
    TEST("History core lifecycle (create/destroy)");

    lle_history_core_t *core = NULL;
    lle_result_t result;

    /// Create with default config
    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create history core");
    }

    if (core == NULL) {
        FAIL("Core pointer is NULL after creation");
    }

    if (!core->initialized) {
        FAIL("Core not marked as initialized");
    }

    if (core->entry_count != 0) {
        FAIL("Initial entry count should be 0");
    }

    if (core->next_entry_id != 1) {
        FAIL("Initial next_entry_id should be 1");
    }

    /// Destroy
    result = lle_history_core_destroy(core);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to destroy history core");
    }

    PASS();
}

/*
 * Test 2: Create default configuration
 */
void test_default_config_creation(void) {
    TEST("Default configuration creation");

    lle_history_config_t *config = NULL;
    lle_result_t result;

    result = lle_history_config_create_default(&config, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create default config");
    }

    if (config == NULL) {
        FAIL("Config pointer is NULL");
    }

    /// Verify default values
    if (config->max_entries != 10000) {
        FAIL("Default max_entries should be 10000");
    }

    if (config->initial_capacity != 1000) {
        FAIL("Default initial_capacity should be 1000");
    }

    if (!config->save_timestamps) {
        FAIL("Timestamps should be saved by default");
    }

    /// Note: ignore_duplicates is false by default (deduplication is a separate
    /// feature)
    if (config->ignore_duplicates != false) {
        FAIL("Duplicate ignoring should be disabled by default");
    }

    if (config->history_file_path == NULL) {
        FAIL("History file path should be set");
    }

    /// Cleanup
    result = lle_history_config_destroy(config, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to destroy config");
    }

    PASS();
}

/*
 * Test 3: Create and destroy history entry
 */
void test_history_entry_lifecycle(void) {
    TEST("History entry lifecycle");

    lle_history_entry_t *entry = NULL;
    lle_result_t result;
    const char *test_command = "ls -la /home";

    result = lle_history_entry_create(&entry, test_command, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create history entry");
    }

    if (entry == NULL) {
        FAIL("Entry pointer is NULL");
    }

    if (entry->command == NULL) {
        FAIL("Entry command is NULL");
    }

    if (strcmp(entry->command, test_command) != 0) {
        FAIL("Entry command doesn't match input");
    }

    if (entry->command_length != strlen(test_command)) {
        FAIL("Entry command_length is incorrect");
    }

    if (entry->state != LLE_HISTORY_STATE_ACTIVE) {
        FAIL("Entry should be in ACTIVE state");
    }

    /// Cleanup
    result = lle_history_entry_destroy(entry, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to destroy entry");
    }

    PASS();
}

/*
 * Test 4: Add single entry to history
 */
void test_add_single_entry(void) {
    TEST("Add single entry to history");

    lle_history_core_t *core = NULL;
    lle_result_t result;
    uint64_t entry_id = 0;

    /// Create core
    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entry
    const char *cmd = "echo 'Hello, World!'";
    result = lle_history_add_entry(core, cmd, 0, &entry_id);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to add entry");
    }

    if (entry_id != 1) {
        lle_history_core_destroy(core);
        FAIL("First entry should have ID 1");
    }

    if (core->entry_count != 1) {
        lle_history_core_destroy(core);
        FAIL("Entry count should be 1");
    }

    if (core->stats.total_entries != 1) {
        lle_history_core_destroy(core);
        FAIL("Stats total_entries should be 1");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 5: Add multiple entries
 */
void test_add_multiple_entries(void) {
    TEST("Add multiple entries to history");

    lle_history_core_t *core = NULL;
    lle_result_t result;
    uint64_t entry_id = 0;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add 10 entries
    const char *commands[] = {
        "ls -la",       "cd /home",          "pwd",  "echo test",
        "cat file.txt", "grep pattern *.c",  "make", "git status",
        "vim test.c",   "gcc -o test test.c"};

    for (int i = 0; i < 10; i++) {
        result = lle_history_add_entry(core, commands[i], i, &entry_id);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }

        if (entry_id != (uint64_t)(i + 1)) {
            lle_history_core_destroy(core);
            FAIL("Entry ID mismatch");
        }
    }

    if (core->entry_count != 10) {
        lle_history_core_destroy(core);
        FAIL("Entry count should be 10");
    }

    if (core->stats.total_entries != 10) {
        lle_history_core_destroy(core);
        FAIL("Stats total_entries should be 10");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 6: Retrieve entry by ID
 */
void test_get_entry_by_id(void) {
    TEST("Retrieve entry by ID");

    lle_history_core_t *core = NULL;
    lle_history_entry_t *entry = NULL;
    lle_result_t result;
    uint64_t entry_id = 0;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entry
    const char *cmd = "test command";
    result = lle_history_add_entry(core, cmd, 0, &entry_id);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to add entry");
    }

    /// Retrieve by ID
    result = lle_history_get_entry_by_id(core, entry_id, &entry);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to retrieve entry by ID");
    }

    if (entry == NULL) {
        lle_history_core_destroy(core);
        FAIL("Retrieved entry is NULL");
    }

    if (entry->entry_id != entry_id) {
        lle_history_core_destroy(core);
        FAIL("Entry ID mismatch");
    }

    if (strcmp(entry->command, cmd) != 0) {
        lle_history_core_destroy(core);
        FAIL("Command text mismatch");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 7: Retrieve entry by index
 */
void test_get_entry_by_index(void) {
    TEST("Retrieve entry by index");

    lle_history_core_t *core = NULL;
    lle_history_entry_t *entry = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entries
    const char *commands[] = {"cmd1", "cmd2", "cmd3"};
    for (int i = 0; i < 3; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, commands[i], 0, &id);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }
    }

    /// Retrieve by index (0-based)
    result = lle_history_get_entry_by_index(core, 1, &entry);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to retrieve entry by index");
    }

    if (entry == NULL) {
        lle_history_core_destroy(core);
        FAIL("Retrieved entry is NULL");
    }

    if (strcmp(entry->command, "cmd2") != 0) {
        lle_history_core_destroy(core);
        FAIL("Wrong entry retrieved (expected cmd2)");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 8: Get entry count
 */
void test_get_entry_count(void) {
    TEST("Get entry count");

    lle_history_core_t *core = NULL;
    lle_result_t result;
    size_t count = 0;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Initial count should be 0
    result = lle_history_get_entry_count(core, &count);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to get entry count");
    }

    if (count != 0) {
        lle_history_core_destroy(core);
        FAIL("Initial count should be 0");
    }

    /// Add 5 entries
    for (int i = 0; i < 5; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, "test", 0, &id);
        if (result != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("Failed to add entry");
        }
    }

    /// Count should be 5
    result = lle_history_get_entry_count(core, &count);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to get entry count");
    }

    if (count != 5) {
        lle_history_core_destroy(core);
        FAIL("Count should be 5");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 9: Statistics tracking
 */
void test_statistics_tracking(void) {
    TEST("Statistics tracking");

    lle_history_core_t *core = NULL;
    const lle_history_stats_t *stats = NULL;
    lle_result_t result;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add three entries (the exit code is recorded per entry but does not
    /// feed the aggregate stats, which track entry and operation counts).
    uint64_t id;
    result = lle_history_add_entry(core, "success1", 0, &id);
    result = lle_history_add_entry(core, "success2", 0, &id);
    result = lle_history_add_entry(core, "failure", 1, &id);

    /// Get stats
    result = lle_history_get_stats(core, &stats);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to get stats");
    }

    /// All three adds are reflected across the tracked dimensions: every
    /// entry is present and active, none are deleted, and the add operation
    /// counter recorded all three calls.
    if (stats->total_entries != 3) {
        lle_history_core_destroy(core);
        FAIL("Stats total_entries should be 3");
    }
    if (stats->active_entries != 3) {
        lle_history_core_destroy(core);
        FAIL("Stats active_entries should be 3");
    }
    if (stats->deleted_entries != 0) {
        lle_history_core_destroy(core);
        FAIL("Stats deleted_entries should be 0");
    }
    if (stats->add_count != 3) {
        lle_history_core_destroy(core);
        FAIL("Stats add_count should be 3");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test 10: Clear history
 */
void test_clear_history(void) {
    TEST("Clear history");

    lle_history_core_t *core = NULL;
    lle_result_t result;
    size_t count;

    result = lle_history_core_create(&core, NULL, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Add entries
    for (int i = 0; i < 5; i++) {
        uint64_t id;
        result = lle_history_add_entry(core, "test", 0, &id);
    }

    /// Verify count
    lle_history_get_entry_count(core, &count);
    if (count != 5) {
        lle_history_core_destroy(core);
        FAIL("Count should be 5 before clear");
    }

    /// Clear
    result = lle_history_clear(core);
    if (result != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Failed to clear history");
    }

    /// Verify empty
    lle_history_get_entry_count(core, &count);
    if (count != 0) {
        lle_history_core_destroy(core);
        FAIL("Count should be 0 after clear");
    }

    /// Cleanup
    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test: size-cap trim keeps the NEWEST entries (FIFO drop-oldest),
 * matching bash/zsh -- the incoming command is never the one discarded.
 */
void test_cap_trim_keeps_newest(void) {
    TEST("Size-cap trim keeps newest (FIFO)");

    lle_history_config_t *cfg = NULL;
    if (lle_history_config_create_default(&cfg, NULL) != LLE_SUCCESS) {
        FAIL("config create");
    }
    cfg->max_entries = 3;
    cfg->initial_capacity = 3;
    cfg->load_on_init = false;
    cfg->auto_save = false;

    lle_history_core_t *core = NULL;
    lle_result_t result = lle_history_core_create(&core, NULL, cfg);
    lle_history_config_destroy(cfg, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("core create");
    }

    const char *cmds[] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; i++) {
        uint64_t id;
        if (lle_history_add_entry(core, cmds[i], 0, &id) != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("add");
        }
    }

    size_t count = 0;
    lle_history_get_entry_count(core, &count);
    if (count != 3) {
        lle_history_core_destroy(core);
        FAIL("count should stay at the cap of 3");
    }

    /// Oldest ("a") evicted; the newest three survive in order.
    const char *want[] = {"b", "c", "d"};
    for (int i = 0; i < 3; i++) {
        lle_history_entry_t *e = NULL;
        if (lle_history_get_entry_by_index(core, i, &e) != LLE_SUCCESS || !e ||
            strcmp(e->command, want[i]) != 0) {
            lle_history_core_destroy(core);
            FAIL("expected newest-three [b c d] after cap trim");
        }
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test: hist_expire_dups_first trims the oldest DUPLICATE before a unique
 * entry. Input uniq,b,b,c at cap 3 keeps [uniq,b,c] (one "b" expired),
 * where plain FIFO would drop the unique "uniq" and keep [b,b,c].
 */
void test_cap_trim_expire_dups_first(void) {
    TEST("Size-cap trim expires duplicates first");

    lle_history_config_t *cfg = NULL;
    if (lle_history_config_create_default(&cfg, NULL) != LLE_SUCCESS) {
        FAIL("config create");
    }
    cfg->max_entries = 3;
    cfg->initial_capacity = 3;
    cfg->expire_dups_first = true;
    cfg->load_on_init = false;
    cfg->auto_save = false;

    lle_history_core_t *core = NULL;
    lle_result_t result = lle_history_core_create(&core, NULL, cfg);
    lle_history_config_destroy(cfg, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("core create");
    }

    const char *cmds[] = {"uniq", "b", "b", "c"};
    for (int i = 0; i < 4; i++) {
        uint64_t id;
        if (lle_history_add_entry(core, cmds[i], 0, &id) != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("add");
        }
    }

    const char *want[] = {"uniq", "b", "c"};
    for (int i = 0; i < 3; i++) {
        lle_history_entry_t *e = NULL;
        if (lle_history_get_entry_by_index(core, i, &e) != LLE_SUCCESS || !e ||
            strcmp(e->command, want[i]) != 0) {
            lle_history_core_destroy(core);
            FAIL("expected [uniq b c]: dup 'b' expired before unique 'uniq'");
        }
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test: at the cap, a duplicate the dedup engine rejects must not evict any
 * existing entry (the eviction runs only after dedup accepts a net-new
 * entry). Without the fix, the pre-dedup trim dropped the oldest entry for
 * a command that was then discarded.
 */
void test_cap_trim_rejected_dup_keeps_all(void) {
    TEST("Size-cap trim: a rejected duplicate evicts nothing");

    lle_history_config_t *cfg = NULL;
    if (lle_history_config_create_default(&cfg, NULL) != LLE_SUCCESS) {
        FAIL("config create");
    }
    cfg->max_entries = 3;
    cfg->initial_capacity = 3;
    cfg->ignore_duplicates = true;
    cfg->dedup_strategy = LLE_DEDUP_IGNORE;
    cfg->load_on_init = false;
    cfg->auto_save = false;

    lle_history_core_t *core = NULL;
    lle_result_t result = lle_history_core_create(&core, NULL, cfg);
    lle_history_config_destroy(cfg, NULL);
    if (result != LLE_SUCCESS) {
        FAIL("core create");
    }

    const char *cmds[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        uint64_t id;
        if (lle_history_add_entry(core, cmds[i], 0, &id) != LLE_SUCCESS) {
            lle_history_core_destroy(core);
            FAIL("add");
        }
    }

    /// Re-run an existing command; dedup rejects it, so nothing is added and
    /// no existing entry is evicted to make room.
    uint64_t id;
    lle_history_add_entry(core, "b", 0, &id);

    size_t count = 0;
    lle_history_get_entry_count(core, &count);
    if (count != 3) {
        lle_history_core_destroy(core);
        FAIL("a rejected duplicate must not change the entry count");
    }

    const char *want[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        lle_history_entry_t *e = NULL;
        if (lle_history_get_entry_by_index(core, i, &e) != LLE_SUCCESS || !e ||
            strcmp(e->command, want[i]) != 0) {
            lle_history_core_destroy(core);
            FAIL("expected [a b c] preserved after a rejected duplicate");
        }
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test: runtime ignore_duplicates toggle (#220 -- setopt hist_ignore_dups).
 * Enabling lazily creates the dedup engine and suppresses a duplicate;
 * disabling destroys it and duplicates accumulate again.
 */
void test_runtime_ignore_duplicates_toggle(void) {
    TEST("Runtime ignore_duplicates toggle");

    lle_history_core_t *core = NULL;
    uint64_t id = 0;
    if (lle_history_core_create(&core, NULL, NULL) != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// The setter honors the configured strategy. Use IGNORE so a duplicate is
    /// rejected outright, a clean observable (the shell default is KEEP_RECENT,
    /// which instead keeps the newest and tombstones the old).
    core->config->dedup_strategy = LLE_DEDUP_IGNORE;

    /// Enable at runtime: the dedup engine is created, so a duplicate command
    /// is rejected and the entry count does not grow.
    if (lle_history_set_ignore_duplicates(core, true) != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Enabling ignore_duplicates failed");
    }
    if (!core->dedup_engine) {
        lle_history_core_destroy(core);
        FAIL("Dedup engine should exist after enable");
    }
    lle_history_add_entry(core, "ls", 0, &id);
    lle_history_add_entry(core, "ls", 0, &id);
    if (core->entry_count != 1) {
        lle_history_core_destroy(core);
        FAIL("Duplicate should be rejected while dedup is on");
    }

    /// Disable at runtime: the engine is destroyed and duplicates accumulate.
    lle_history_set_ignore_duplicates(core, false);
    if (core->dedup_engine) {
        lle_history_core_destroy(core);
        FAIL("Dedup engine should be gone after disable");
    }
    lle_history_add_entry(core, "ls", 0, &id);
    if (core->entry_count != 2) {
        lle_history_core_destroy(core);
        FAIL("Duplicate should accumulate after disable");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Test: runtime ignore_space_prefix toggle (#469 -- setopt hist_ignore_space).
 * Enabling suppresses a command that begins with a space at add time;
 * disabling lets space-prefixed commands accumulate again.
 */
void test_runtime_ignore_space_prefix_toggle(void) {
    TEST("Runtime ignore_space_prefix toggle");

    lle_history_core_t *core = NULL;
    uint64_t id = 0;
    if (lle_history_core_create(&core, NULL, NULL) != LLE_SUCCESS) {
        FAIL("Failed to create core");
    }

    /// Default is off: a space-prefixed command is stored like any other.
    lle_history_add_entry(core, " secret", 0, &id);
    if (core->entry_count != 1) {
        lle_history_core_destroy(core);
        FAIL("Space-prefixed command should be stored while filter is off");
    }

    /// Enable at runtime: the next space-prefixed command is dropped silently
    /// and the count does not grow.
    if (lle_history_set_ignore_space_prefix(core, true) != LLE_SUCCESS) {
        lle_history_core_destroy(core);
        FAIL("Enabling ignore_space_prefix failed");
    }
    lle_history_add_entry(core, " hidden", 0, &id);
    if (core->entry_count != 1) {
        lle_history_core_destroy(core);
        FAIL("Space-prefixed command should be dropped while filter is on");
    }

    /// A command without a leading space is still stored while the filter is
    /// on.
    lle_history_add_entry(core, "visible", 0, &id);
    if (core->entry_count != 2) {
        lle_history_core_destroy(core);
        FAIL("Non-space command should be stored while filter is on");
    }

    /// Disable at runtime: space-prefixed commands accumulate again.
    lle_history_set_ignore_space_prefix(core, false);
    lle_history_add_entry(core, " again", 0, &id);
    if (core->entry_count != 3) {
        lle_history_core_destroy(core);
        FAIL("Space-prefixed command should accumulate after disable");
    }

    lle_history_core_destroy(core);
    PASS();
}

/*
 * Main test runner
 */
int main(void) {
    printf("=================================================\n");
    printf("History Core - Functional Tests\n");
    printf("=================================================\n");

    /// Run all tests
    test_history_core_lifecycle();
    test_default_config_creation();
    test_history_entry_lifecycle();
    test_add_single_entry();
    test_add_multiple_entries();
    test_get_entry_by_id();
    test_get_entry_by_index();
    test_get_entry_count();
    test_statistics_tracking();
    test_clear_history();
    test_cap_trim_keeps_newest();
    test_cap_trim_expire_dups_first();
    test_cap_trim_rejected_dup_keeps_all();
    test_runtime_ignore_duplicates_toggle();
    test_runtime_ignore_space_prefix_toggle();

    /// Summary
    printf("\n=================================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("=================================================\n");

    if (tests_failed == 0) {
        printf("ALL FUNCTIONAL TESTS PASSED\n");
        printf("History core is working correctly\n");
        printf("=================================================\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED\n");
        printf("History core needs fixes\n");
        printf("=================================================\n");
        return 1;
    }
}
