/**
 * @file test_history_fault_reporting.c
 * @brief End-to-end test that history entry corruption reports a fault
 *
 * Builds a corrupt history entry (its command length disagrees with the actual
 * command) and runs the entry validator, asserting the converted LLE_FAULT site
 * reports through the fault router: a recording sink receives one fault naming
 * the history component and carrying the corruption error code.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "test_framework.h"

#include <string.h>

static int g_fault_count;
static lle_fault_t g_last_fault;

static void recording_sink(const lle_fault_t *fault) {
    g_fault_count++;
    g_last_fault = *fault;
}

TEST(corrupt_history_entry_reports_a_history_fault) {
    char command[] = "ls";
    lle_history_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.command = command;
    entry.command_length = 99; /// disagrees with strlen("ls") == 2

    lle_fault_set_dev_sink(recording_sink);
    g_fault_count = 0;
    memset(&g_last_fault, 0, sizeof(g_last_fault));

    lle_result_t rc = lle_history_validate_entry(&entry);

    lle_fault_set_dev_sink(NULL);

    ASSERT_EQ(rc, LLE_ERROR_STATE_CORRUPTION,
              "the validator should detect the length mismatch");
    ASSERT_EQ(g_fault_count, 1,
              "the corruption should report exactly one fault");
    ASSERT_STR_EQ(g_last_fault.component, "history",
                  "the fault should name the history component");
    ASSERT_EQ(g_last_fault.code, LLE_ERROR_STATE_CORRUPTION,
              "the fault should carry the corruption error code");
}

int main(void) {
    printf("=== LLE History Fault Reporting Tests ===\n\n");
    RUN_TEST(corrupt_history_entry_reports_a_history_fault);
    return TEST_RESULT();
}
