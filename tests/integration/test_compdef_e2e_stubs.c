/**
 * @file test_compdef_e2e_stubs.c
 * @brief Stubs for the compdef E2E integration test.
 *
 * Distinct from tests/unit/test_executor_stubs.c. That file makes
 * get_global_executor() return `current_executor`, which is fine for
 * unit tests that set current_executor right before each call. The
 * integration test invokes paths (executor_execute_command_line,
 * compdef-bound function bodies) whose internal builtin dispatch
 * sets+clears current_executor as a side effect, so by the time
 * lle_completion_system_generate runs the compdef source the global
 * is NULL.
 *
 * Production lush has a separate session-long pointer in lush.c
 * (`global_executor`) that get_global_executor returns; the
 * integration test mirrors that ownership shape via a dedicated
 * test_integration_executor pointer that the test sets once at
 * global_setup() and clears at global_teardown().
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "executor.h"

executor_t *test_integration_executor = NULL;

executor_t *get_global_executor(void) { return test_integration_executor; }

int parse_and_execute(const char *input, size_t starting_line) {
    if (!input || !test_integration_executor) {
        return 1;
    }
    return executor_execute_command_line(test_integration_executor, input,
                                         starting_line);
}
