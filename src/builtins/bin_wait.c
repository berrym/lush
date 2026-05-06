/**
 * @file bin_wait.c
 * @brief `wait` builtin -- wait for a job or process to finish
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "shell_error.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Wait for background jobs to complete
 *
 * With no arguments, waits for all background jobs. With arguments,
 * waits for specific job IDs (%n) or process IDs.
 *
 * @param argc Argument count
 * @param argv Argument vector with optional job/process IDs
 * @return Exit status of the last waited process
 */
int bin_wait(int argc, char **argv) {
    // Get the current executor to access job control
    if (!current_executor) {
        // If no executor, there are no jobs to wait for
        return 0;
    }

    // If no arguments, wait for all background jobs
    if (argc == 1) {
        executor_update_job_status(current_executor);

        // Wait for all running jobs
        job_t *job = current_executor->jobs;
        int last_exit_status = 0;

        while (job) {
            if (job->state == JOB_RUNNING) {
                int status;
                pid_t result = waitpid(-job->pgid, &status, 0);

                if (result > 0) {
                    if (WIFEXITED(status)) {
                        last_exit_status = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        last_exit_status = 128 + WTERMSIG(status);
                    } else {
                        last_exit_status = 1;
                    }

                    // Mark job as done
                    job->state = JOB_DONE;
                }
            }
            job = job->next;
        }

        // Clean up completed jobs
        executor_update_job_status(current_executor);

        return last_exit_status;
    }

    // Wait for specific job(s) or process(es)
    int overall_exit_status = 0;

    for (int i = 1; i < argc; i++) {
        char *endptr;
        long target = strtol(argv[i], &endptr, 10);

        // Check for job ID syntax (%n)
        bool is_job_id = false;
        int job_or_pid = (int)target;

        if (argv[i][0] == '%') {
            is_job_id = true;
            // Re-parse without the % sign
            job_or_pid = (int)strtol(argv[i] + 1, &endptr, 10);
            if (*endptr != '\0' || job_or_pid <= 0) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "%s: not a valid job ID", argv[i]);
                return 1;
            }
        } else {
            if (*endptr != '\0' || target <= 0) {
                executor_error_report(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "%s: arguments must be process or job IDs", argv[i]);
                return 1;
            }
        }

        if (is_job_id) {
            // Wait for specific job
            job_t *job = executor_find_job(current_executor, job_or_pid);
            if (!job) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(),
                                      "%%%d: no such job", job_or_pid);
                return 127;
            }

            if (job->state == JOB_RUNNING) {
                int status;
                pid_t result = waitpid(-job->pgid, &status, 0);

                if (result > 0) {
                    if (WIFEXITED(status)) {
                        overall_exit_status = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        overall_exit_status = 128 + WTERMSIG(status);
                    } else {
                        overall_exit_status = 1;
                    }

                    job->state = JOB_DONE;
                }
            } else if (job->state == JOB_DONE) {
                // Job already completed - return 0
                overall_exit_status = 0;
            }
        } else {
            // Wait for specific PID
            int status;
            pid_t result = waitpid(job_or_pid, &status, 0);

            if (result == -1) {
                if (errno == ECHILD) {
                    // Process doesn't exist or not a child
                    executor_error_report(
                        current_executor, SHELL_ERR_JOB_NOT_FOUND,
                        builtin_get_source_location(),
                        "pid %d is not a child of this shell", job_or_pid);
                    return 127;
                } else {
                    int saved_errno = errno;
                    source_location_t loc = builtin_get_source_location();
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, loc, "%s",
                        strerror(saved_errno));
                    if (error) {
                        if (current_executor && SOURCE_LOC_VALID(loc)) {
                            char *src_line = executor_get_source_line(
                                current_executor, loc.line);
                            if (src_line) {
                                shell_error_set_source_line(
                                    error, src_line, loc.column,
                                    loc.column + loc.length);
                                free(src_line);
                            }
                        }
                        if (current_executor) {
                            for (size_t k = 0;
                                 k < current_executor->context_depth &&
                                 k < SHELL_ERROR_CONTEXT_MAX;
                                 k++) {
                                if (current_executor->context_stack[k]) {
                                    shell_error_push_context(
                                        error, "%s",
                                        current_executor->context_stack[k]);
                                }
                            }
                        }
                        shell_error_display(error, stderr,
                                            isatty(STDERR_FILENO));
                        shell_error_free(error);
                    } else {
                        fprintf(stderr, "lush: wait: %s\n",
                                strerror(saved_errno));
                    }
                    return 1;
                }
            } else if (result > 0) {
                if (WIFEXITED(status)) {
                    overall_exit_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    overall_exit_status = 128 + WTERMSIG(status);
                } else {
                    overall_exit_status = 1;
                }
            }
        }
    }

    // Clean up completed jobs
    executor_update_job_status(current_executor);

    return overall_exit_status;
}
