/**
 * @file bin_bg.c
 * @brief `bg` builtin -- resume a stopped job in the background
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include <signal.h>
#include <sys/wait.h>

/**
 * @brief Resume a job in the background
 *
 * Resumes a stopped job and runs it in the background.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (argv[1] is optional job specification)
 * @return 0 on success, 1 on error or no current job
 */
int bin_bg(int argc, char **argv) {
    (void)argc;
    if (current_executor) {
        return executor_builtin_bg(current_executor, argv);
    }
    executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                          builtin_get_source_location(), "no current job");
    return 1;
}
