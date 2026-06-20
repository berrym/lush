/**
 * @file spec_09_history_compliance.c
 * @brief Spec 09 history-system behavioral compliance test
 *
 * Drives a real history core -- adding and retrieving entries -- and asserts
 * the resulting state, rather than assigning to a local struct and reading
 * the values back. Covers Phase 1: entry storage, exit-code persistence,
 * statistics, and the core entry-count lifecycle.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assertions_passed = 0;

/// exit(1) on failure so the gate holds regardless of NDEBUG (unlike assert()).
#define COMPLIANCE_ASSERT(condition, message)                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "COMPLIANCE VIOLATION: %s\n", message);            \
            fprintf(stderr, "   at %s:%d\n", __FILE__, __LINE__);              \
            exit(1);                                                           \
        }                                                                      \
        assertions_passed++;                                                   \
    } while (0)

/// A stored entry retains its command text, length, and active state.
static void test_entry_storage(void) {
    lle_history_core_t *core = NULL;
    COMPLIANCE_ASSERT(lle_history_core_create(&core, NULL, NULL) == LLE_SUCCESS,
                      "history core creation");

    uint64_t id = 0;
    COMPLIANCE_ASSERT(lle_history_add_entry(core, "echo hello", 0, &id) ==
                          LLE_SUCCESS,
                      "adding an entry succeeds");

    lle_history_entry_t *entry = NULL;
    COMPLIANCE_ASSERT(lle_history_get_entry_by_index(core, 0, &entry) ==
                              LLE_SUCCESS &&
                          entry != NULL,
                      "the added entry is retrievable by index");
    COMPLIANCE_ASSERT(entry->command != NULL &&
                          strcmp(entry->command, "echo hello") == 0,
                      "the command text is stored verbatim");
    COMPLIANCE_ASSERT(entry->command_length == strlen("echo hello"),
                      "the command length matches the text");
    COMPLIANCE_ASSERT(entry->state == LLE_HISTORY_STATE_ACTIVE,
                      "a freshly added entry is active");

    lle_history_core_destroy(core);
}

/// Exit codes are persisted with their entries (default config saves them).
static void test_exit_code_persistence(void) {
    lle_history_core_t *core = NULL;
    COMPLIANCE_ASSERT(lle_history_core_create(&core, NULL, NULL) == LLE_SUCCESS,
                      "history core creation");

    uint64_t id = 0;
    lle_history_add_entry(core, "true", 0, &id);
    lle_history_add_entry(core, "false", 1, &id);
    lle_history_add_entry(core, "exit 2", 2, &id);

    lle_history_entry_t *entry = NULL;
    lle_history_get_entry_by_index(core, 0, &entry);
    COMPLIANCE_ASSERT(entry->exit_code == 0, "exit code 0 is persisted");
    lle_history_get_entry_by_index(core, 1, &entry);
    COMPLIANCE_ASSERT(entry->exit_code == 1, "exit code 1 is persisted");
    lle_history_get_entry_by_index(core, 2, &entry);
    COMPLIANCE_ASSERT(entry->exit_code == 2, "exit code 2 is persisted");

    lle_history_core_destroy(core);
}

/// Statistics reflect the number of entries added.
static void test_statistics(void) {
    lle_history_core_t *core = NULL;
    COMPLIANCE_ASSERT(lle_history_core_create(&core, NULL, NULL) == LLE_SUCCESS,
                      "history core creation");

    uint64_t id = 0;
    for (int i = 0; i < 5; i++) {
        lle_history_add_entry(core, "cmd", 0, &id);
    }

    const lle_history_stats_t *stats = NULL;
    COMPLIANCE_ASSERT(lle_history_get_stats(core, &stats) == LLE_SUCCESS &&
                          stats != NULL,
                      "statistics are available");
    COMPLIANCE_ASSERT(stats->total_entries == 5,
                      "total_entries counts the five adds");
    COMPLIANCE_ASSERT(stats->add_count == 5, "add_count counts the five adds");

    lle_history_core_destroy(core);
}

/// The entry count starts at zero and tracks additions.
static void test_entry_count_lifecycle(void) {
    lle_history_core_t *core = NULL;
    COMPLIANCE_ASSERT(lle_history_core_create(&core, NULL, NULL) == LLE_SUCCESS,
                      "history core creation");

    size_t count = 999;
    COMPLIANCE_ASSERT(lle_history_get_entry_count(core, &count) == LLE_SUCCESS,
                      "entry count is queryable");
    COMPLIANCE_ASSERT(count == 0, "a fresh core has no entries");

    uint64_t id = 0;
    lle_history_add_entry(core, "a", 0, &id);
    lle_history_add_entry(core, "b", 0, &id);
    lle_history_add_entry(core, "c", 0, &id);

    lle_history_get_entry_count(core, &count);
    COMPLIANCE_ASSERT(count == 3, "the count tracks the three adds");

    lle_history_core_destroy(core);
}

int main(void) {
    printf("Spec 09 History System Behavioral Compliance\n");
    printf("============================================\n\n");

    test_entry_storage();
    test_exit_code_persistence();
    test_statistics();
    test_entry_count_lifecycle();

    printf("Spec 09 behavioral compliance: %d assertions passed\n",
           assertions_passed);
    return 0;
}
