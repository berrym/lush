/**
 * @file test_executor_stubs.c
 * @brief Stub implementations for executor integration tests
 *
 * Provides stubs for functions defined in lush.c that are needed
 * by other modules but we can't link lush.c (has main()).
 */

#include "executor.h"

/* Global executor accessor - returns current_executor */
executor_t *get_global_executor(void) { return current_executor; }

/* Parse and execute - uses executor_execute_command_line */
int parse_and_execute(const char *input) {
    if (!input || !current_executor)
        return 1;
    return executor_execute_command_line(current_executor, input);
}
