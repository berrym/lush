/**
 * @file bin_jobs.c
 * @brief `jobs` builtin -- list active jobs
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "shell_error.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief List active background jobs
 *
 * Displays all active jobs managed by the current executor.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector with job options
 * @return 0 on success, 1 if no executor available
 */
int bin_jobs(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_jobs(current_executor, argv);
    }
    return 1;
}
