/**
 * @file test_buffer_fault_reporting.c
 * @brief End-to-end test that buffer corruption reports a fault
 *
 * Corrupts a buffer's internal invariant and runs the integrity validator,
 * asserting the converted LLE_FAULT site reports through the fault router: a
 * recording sink receives one fault naming the buffer component and carrying
 * the corruption error code.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lush_memory_pool.h"
#include "test_framework.h"

#include <string.h>

extern lush_memory_pool_t *global_memory_pool;

static int g_fault_count;
static lle_fault_t g_last_fault;

static void recording_sink(const lle_fault_t *fault) {
    g_fault_count++;
    g_last_fault = *fault;
}

TEST(corrupt_buffer_reports_a_buffer_fault) {
    lush_pool_config_t config = lush_pool_get_default_config();
    lush_pool_init(&config);

    lle_buffer_t *buffer = NULL;
    ASSERT_EQ(lle_buffer_create(&buffer, global_memory_pool, 1024), LLE_SUCCESS,
              "buffer creation should succeed");

    /// Corrupt an internal invariant the validator checks: used > length.
    buffer->used = buffer->length + 1;

    lle_fault_set_dev_sink(recording_sink);
    g_fault_count = 0;
    memset(&g_last_fault, 0, sizeof(g_last_fault));

    lle_result_t rc = lle_buffer_validate(buffer);

    lle_fault_set_dev_sink(NULL);

    ASSERT_EQ(rc, LLE_ERROR_MEMORY_CORRUPTION,
              "the validator should detect the corruption");
    ASSERT_EQ(g_fault_count, 1,
              "the corruption should report exactly one fault");
    ASSERT_STR_EQ(g_last_fault.component, "buffer",
                  "the fault should name the buffer component");
    ASSERT_EQ(g_last_fault.code, LLE_ERROR_MEMORY_CORRUPTION,
              "the fault should carry the corruption error code");

    /// Repair the invariant so destroy operates on a consistent buffer.
    buffer->used = buffer->length;
    lle_buffer_destroy(buffer);
    lush_pool_shutdown();
}

int main(void) {
    printf("=== LLE Buffer Fault Reporting Tests ===\n\n");
    RUN_TEST(corrupt_buffer_reports_a_buffer_fault);
    return TEST_RESULT();
}
