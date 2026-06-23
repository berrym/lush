/**
 * @file test_fault_shell_map.c
 * @brief Unit tests for the LLE-fault to shell-error mapping
 *
 * Exercises the pure translation used by the LLE fault user sink:
 *   lle_fault_shell_code, lle_fault_shell_severity
 *
 * Tests cover:
 *   - Memory faults map to out-of-memory / state-corruption
 *   - System and IO faults map to the IO/resource categories
 *   - Component subsystem faults map to subsystem-init-failed
 *   - Internal invariant/assertion faults map appropriately
 *   - The six LLE severities collapse onto the shell's four
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/lle_fault_shell_map.h"
#include "test_framework.h"

/* ============================================================================
 * Code mapping
 * ============================================================================
 */

TEST(memory_faults_map_to_oom_and_corruption) {
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_OUT_OF_MEMORY),
              SHELL_ERR_OUT_OF_MEMORY, "OOM maps to shell OOM");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_MEMORY_POOL_EXHAUSTED),
              SHELL_ERR_OUT_OF_MEMORY, "pool exhaustion maps to shell OOM");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_MEMORY_CORRUPTION),
              SHELL_ERR_STATE_CORRUPTION,
              "corruption maps to state corruption");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_USE_AFTER_FREE),
              SHELL_ERR_STATE_CORRUPTION,
              "use-after-free maps to state corruption");
}

TEST(system_faults_map_to_io_and_resource) {
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_SYSTEM_CALL), SHELL_ERR_IO_ERROR,
              "syscall failure maps to IO error");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_IO_ERROR), SHELL_ERR_IO_ERROR,
              "IO error maps to IO error");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_PERMISSION_DENIED),
              SHELL_ERR_PERMISSION_DENIED, "permission denied maps through");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_RESOURCE_EXHAUSTED),
              SHELL_ERR_RESOURCE_LIMIT, "resource exhaustion maps to limit");
}

TEST(component_faults_map_to_subsystem_init_failed) {
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_HISTORY_SYSTEM),
              SHELL_ERR_SUBSYSTEM_INIT_FAILED,
              "history maps to subsystem init");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_TERMINAL_ABSTRACTION),
              SHELL_ERR_SUBSYSTEM_INIT_FAILED,
              "terminal maps to subsystem init");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_BUFFER_COMPONENT),
              SHELL_ERR_SUBSYSTEM_INIT_FAILED, "buffer maps to subsystem init");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_INITIALIZATION_FAILED),
              SHELL_ERR_SUBSYSTEM_INIT_FAILED,
              "init failure maps to subsystem init");
}

TEST(internal_faults_map_to_corruption_and_assertion) {
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_STATE_CORRUPTION),
              SHELL_ERR_STATE_CORRUPTION, "state corruption maps through");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_INVARIANT_VIOLATION),
              SHELL_ERR_STATE_CORRUPTION,
              "invariant violation maps to corruption");
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_ASSERTION_FAILED),
              SHELL_ERR_ASSERTION, "assertion maps to assertion");
}

TEST(unmapped_validation_codes_fall_back_to_assertion) {
    /// A validation guard reaching the user channel is a logic error.
    ASSERT_EQ(lle_fault_shell_code(LLE_ERROR_INVALID_PARAMETER),
              SHELL_ERR_ASSERTION, "validation falls back to assertion");
}

/* ============================================================================
 * Severity mapping
 * ============================================================================
 */

TEST(severities_collapse_onto_shell_levels) {
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_INFO), SHELL_SEVERITY_NOTE,
              "info maps to note");
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_WARNING),
              SHELL_SEVERITY_WARNING, "warning maps to warning");
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_MINOR),
              SHELL_SEVERITY_WARNING, "minor maps to warning");
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_MAJOR),
              SHELL_SEVERITY_ERROR, "major maps to error");
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_CRITICAL),
              SHELL_SEVERITY_ERROR, "critical maps to error");
    ASSERT_EQ(lle_fault_shell_severity(LLE_SEVERITY_FATAL),
              SHELL_SEVERITY_FATAL, "fatal maps to fatal");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Fault Shell Map Tests ===\n\n");

    printf("--- Code mapping ---\n");
    RUN_TEST(memory_faults_map_to_oom_and_corruption);
    RUN_TEST(system_faults_map_to_io_and_resource);
    RUN_TEST(component_faults_map_to_subsystem_init_failed);
    RUN_TEST(internal_faults_map_to_corruption_and_assertion);
    RUN_TEST(unmapped_validation_codes_fall_back_to_assertion);

    printf("\n--- Severity mapping ---\n");
    RUN_TEST(severities_collapse_onto_shell_levels);

    return TEST_RESULT();
}
