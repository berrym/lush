/**
 * @file bin_disown.c
 * @brief `disown` builtin -- remove a job from the shell's job table
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
 * @brief Disown jobs from the shell
 *
 * Removes jobs from the shell's job table or marks them to not receive
 * SIGHUP when the shell exits (login shell behavior).
 *
 * Usage: disown [-h] [-a | -r] [jobspec ...]
 *   -h  Mark jobs to not receive SIGHUP (keep in job table)
 *   -a  Apply to all jobs
 *   -r  Apply to running jobs only
 *   Without options: remove job(s) from job table
 *   Without jobspec: apply to current job
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int bin_disown(int argc, char **argv) {
    if (!current_executor) {
        executor_error_report(current_executor, SHELL_ERR_STATE_CORRUPTION,
                              builtin_get_source_location(), "no job control");
        return 1;
    }

    bool flag_h = false; // Mark no_sighup instead of removing
    bool flag_a = false; // Apply to all jobs
    bool flag_r = false; // Apply to running jobs only

    // Parse options
    int optind_local = 1;
    while (optind_local < argc && argv[optind_local][0] == '-' &&
           argv[optind_local][1] != '\0') {
        const char *opt = argv[optind_local];
        for (int i = 1; opt[i]; i++) {
            switch (opt[i]) {
            case 'h':
                flag_h = true;
                break;
            case 'a':
                flag_a = true;
                break;
            case 'r':
                flag_r = true;
                break;
            default: {
                source_location_t loc = builtin_get_source_location();
                shell_error_t *err = shell_error_create(
                    SHELL_ERR_INVALID_OPTION, SHELL_SEVERITY_ERROR, loc,
                    "-%c: invalid option", opt[i]);
                if (err) {
                    if (current_executor && SOURCE_LOC_VALID(loc)) {
                        char *src_line = executor_get_source_line(
                            current_executor, loc.line);
                        if (src_line) {
                            shell_error_set_source_line(
                                err, src_line, loc.column,
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
                                    err, "%s",
                                    current_executor->context_stack[k]);
                            }
                        }
                    }
                    shell_error_set_suggestion(
                        err, "usage: disown [-h] [-a | -r] [jobspec ...]");
                    shell_error_display(err, stderr, isatty(STDERR_FILENO));
                    shell_error_free(err);
                } else {
                    fprintf(stderr, "lush: disown: -%c: invalid option\n",
                            opt[i]);
                }
                return 1;
            }
            }
        }
        optind_local++;
    }

    // -a and -r are mutually exclusive in practice
    // -a means all, -r means running only

    int result = 0;

    if (flag_a || flag_r) {
        // Apply to all jobs (or running jobs if -r)
        job_t *job = current_executor->jobs;
        job_t *prev = NULL;

        while (job) {
            job_t *next = job->next;

            // Skip non-running jobs if -r flag is set
            if (flag_r && job->state != JOB_RUNNING) {
                prev = job;
                job = next;
                continue;
            }

            if (flag_h) {
                // Mark job to not receive SIGHUP
                job->no_sighup = true;
                prev = job;
            } else {
                // Remove job from table
                if (prev) {
                    prev->next = next;
                } else {
                    current_executor->jobs = next;
                }
                // Free job resources
                if (job->processes) {
                    process_t *p = job->processes;
                    while (p) {
                        process_t *pnext = p->next;
                        free(p->command);
                        free(p);
                        p = pnext;
                    }
                }
                free(job->command_line);
                free(job);
                // Don't update prev since we removed current
            }

            job = next;
        }
    } else if (optind_local < argc) {
        // Apply to specified jobspecs
        for (int i = optind_local; i < argc; i++) {
            const char *jobspec = argv[i];
            int job_id;

            // Parse jobspec (accept %N or just N)
            if (jobspec[0] == '%') {
                job_id = atoi(jobspec + 1);
            } else {
                job_id = atoi(jobspec);
            }

            if (job_id <= 0) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "%s: invalid job specification", jobspec);
                result = 1;
                continue;
            }

            job_t *job = executor_find_job(current_executor, job_id);
            if (!job) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(),
                                      "%s: no such job", jobspec);
                result = 1;
                continue;
            }

            if (flag_h) {
                job->no_sighup = true;
            } else {
                executor_remove_job(current_executor, job_id);
            }
        }
    } else {
        // Apply to current job (most recent background job)
        // Find the highest job_id (most recent)
        job_t *current_job = NULL;
        int max_id = 0;
        for (job_t *j = current_executor->jobs; j; j = j->next) {
            if (j->job_id > max_id) {
                max_id = j->job_id;
                current_job = j;
            }
        }

        if (!current_job) {
            executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                  builtin_get_source_location(),
                                  "current: no such job");
            return 1;
        }

        if (flag_h) {
            current_job->no_sighup = true;
        } else {
            executor_remove_job(current_executor, current_job->job_id);
        }
    }

    return result;
}
