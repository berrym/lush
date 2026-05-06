/**
 * @file bin_fg.c
 * @brief `fg` builtin -- bring a background job to the foreground
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include <signal.h>
#include <sys/wait.h>

/**
 * @brief Bring a background job to the foreground
 *
 * Resumes a stopped job or brings a background job to the foreground.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (argv[1] is optional job specification)
 * @return 0 on success, 1 on error or no current job
 */
int bin_fg(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_fg(current_executor, argv);
    }
    executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                          builtin_get_source_location(), "no current job");
    return 1;
}
