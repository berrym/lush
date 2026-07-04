/**
 * @file bin_wait.c
 * @brief `wait` builtin -- wait for a job or process to finish
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "signals.h"

#include <errno.h>
#include <sys/wait.h>

/**
 * @brief Report a tracked job's completion status.
 *
 * Reaps the job if it is still running, caches its status, and marks it
 * reported so the job-status sweep can prune it later. The job is deliberately
 * left in the list rather than removed here, so a repeated `wait` on the same
 * job reports the same status until a status sweep retires it -- matching bash.
 *
 * If a signal the shell must act on (a trap, a hangup, an interrupt) breaks the
 * wait, the job is left running and 128 + signo is reported instead, matching
 * bash's `wait` behavior; the trap then dispatches at the next command
 * boundary. The break is reported separately in *break_signo (nonzero) so a
 * caller iterating multiple operands can stop immediately, rather than
 * inferring it from the 128 + signo return, which a job legitimately killed by
 * a signal also produces.
 *
 * @param job         Tracked job to wait for
 * @param break_signo Set to the breaking signal number, or 0 if the job was
 *                    reaped normally
 * @return The job's shell exit code, or 128 + signo if a signal broke the wait
 */
static int wait_for_tracked_job(job_t *job, int *break_signo) {
    int brk = executor_reap_job(job, true);
    if (brk > 0) {
        *break_signo = brk;
        return 128 + brk;
    }
    *break_signo = 0;
    if (job->state == JOB_DONE) {
        job->reported = true;
    }
    return executor_job_status_code(job);
}

/**
 * @brief Emit the structured error for a waitpid failure other than ECHILD.
 *
 * @param pid Process id that was waited on
 */
static void report_wait_io_error(long pid) {
    (void)pid;
    int saved_errno = errno;
    source_location_t loc = builtin_get_source_location();
    shell_error_t *error =
        shell_error_create(SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, loc, "%s",
                           strerror(saved_errno));
    if (!error) {
        fprintf(stderr, "lush: wait: %s\n", strerror(saved_errno));
        return;
    }
    if (current_executor && SOURCE_LOC_VALID(loc)) {
        char *src_line = executor_get_source_line(current_executor, loc.line);
        if (src_line) {
            shell_error_set_source_line(error, src_line, loc.column,
                                        loc.column + loc.length);
            free(src_line);
        }
    }
    if (current_executor) {
        for (size_t k = 0;
             k < current_executor->context_depth && k < SHELL_ERROR_CONTEXT_MAX;
             k++) {
            if (current_executor->context_stack[k]) {
                shell_error_push_context(error, "%s",
                                         current_executor->context_stack[k]);
            }
        }
    }
    shell_error_display(error, stderr, isatty(STDERR_FILENO));
    shell_error_free(error);
}

/**
 * @brief Wait for background jobs to complete
 *
 * With no arguments, waits for all background jobs and returns 0 (POSIX). With
 * arguments, waits for specific job IDs (%n) or process IDs and returns the
 * last one's status. A pid that names a tracked job is resolved through the job
 * list so its status is cached and reported consistently; a pid with no tracked
 * job is waited on directly.
 *
 * @param argc Argument count
 * @param argv Argument vector with optional job/process IDs
 * @return Exit status of the last waited job, or 0 for a no-operand wait
 */
int bin_wait(int argc, char **argv) {
    if (!current_executor) {
        return 0;
    }

    /// No operands: wait for every running job, then report success regardless
    /// of the jobs' statuses (POSIX). The status sweep afterward prunes the
    /// now-reported completed jobs.
    if (argc == 1) {
        for (job_t *job = current_executor->jobs; job; job = job->next) {
            int brk = executor_reap_job(job, true);
            if (brk > 0) {
                return 128 + brk;
            }
            if (job->state == JOB_DONE) {
                job->reported = true;
            }
        }
        executor_update_job_status(current_executor);
        return 0;
    }

    int overall_exit_status = 0;

    for (int i = 1; i < argc; i++) {
        char *endptr;
        long target = strtol(argv[i], &endptr, 10);

        if (argv[i][0] == '%') {
            /// Job-id form (%n).
            long id = strtol(argv[i] + 1, &endptr, 10);
            if (*endptr != '\0' || id <= 0) {
                executor_error_report(current_executor,
                                      SHELL_ERR_INVALID_ARGUMENT,
                                      builtin_get_source_location(),
                                      "%s: not a valid job ID", argv[i]);
                return 1;
            }
            job_t *job = executor_find_job(current_executor, (int)id);
            if (!job) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(),
                                      "%%%ld: no such job", id);
                return 127;
            }
            int break_signo;
            overall_exit_status = wait_for_tracked_job(job, &break_signo);
            if (break_signo > 0) {
                return overall_exit_status;
            }
            continue;
        }

        if (*endptr != '\0' || target <= 0) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "%s: arguments must be process or job IDs",
                                  argv[i]);
            return 1;
        }

        /// Pid form: prefer a tracked job so its status is cached and a
        /// repeated wait stays consistent; otherwise wait on the pid directly.
        job_t *job = executor_find_job_by_pid(current_executor, (pid_t)target);
        if (job) {
            int break_signo;
            overall_exit_status = wait_for_tracked_job(job, &break_signo);
            if (break_signo > 0) {
                return overall_exit_status;
            }
            continue;
        }

        int status;
        pid_t result;
        for (;;) {
            result = waitpid((pid_t)target, &status, 0);
            if (result != -1 || errno != EINTR) {
                break;
            }
            /// The same signal handling as a tracked wait: break for a trap,
            /// hangup, or interrupt (report 128 + signo); resume across an
            /// incidental signal.
            int brk = signal_wait_break_check();
            if (brk > 0) {
                return 128 + brk;
            }
        }
        if (result == -1) {
            if (errno == ECHILD) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(),
                                      "pid %ld is not a child of this shell",
                                      target);
                return 127;
            }
            report_wait_io_error(target);
            return 1;
        }
        if (result > 0) {
            if (WIFEXITED(status)) {
                overall_exit_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                overall_exit_status = 128 + WTERMSIG(status);
            } else {
                overall_exit_status = 1;
            }
        }
    }

    return overall_exit_status;
}
