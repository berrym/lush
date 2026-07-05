/**
 * @file bin_wait.c
 * @brief `wait` builtin -- wait for a job or process to finish
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "config.h"
#include "signals.h"

#include <errno.h>
#include <limits.h>
#include <sys/wait.h>

/**
 * @brief Report a tracked job's completion status, then consume it.
 *
 * Reaps the job if it is still running and returns its exit code. lush's
 * completed-job lifecycle is single-consumption: an explicit `wait` delivers a
 * finished job's status once and then drops the job, so job state stays
 * predictable (no lingering already-waited jobs) and honors the POSIX rule of
 * remembering a terminated job's status only until it is waited for. The
 * `jobs.retain_completed` config cell instead keeps the job addressable for
 * repeated or later waits (an opt-in for the legacy behavior), in which case it
 * is marked reported so the status sweep does not re-display it.
 *
 * If a signal the shell must act on (a trap, a hangup, an interrupt) breaks the
 * wait, the job is left running -- not consumed -- and 128 + signo is returned;
 * the trap then dispatches at the next command boundary. The break is reported
 * separately in *break_signo (nonzero) so a caller iterating multiple operands
 * stops immediately, rather than inferring it from the 128 + signo return that
 * a job killed by a signal also produces.
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
    int code = executor_job_status_code(job);
    if (job->state == JOB_DONE) {
        if (config_get_bool("jobs.retain_completed", false)) {
            /// Opt-in retention: keep the job addressable; suppress re-display.
            job->reported = true;
        } else {
            /// lush default: single-consumption -- the status has been
            /// delivered, so drop the job.
            executor_remove_job(current_executor, job->job_id);
        }
    }
    return code;
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
 * list so it runs through the completed-job lifecycle (consume-on-wait by
 * default) and its raw wait status is decoded to a shell exit code; a pid with
 * no tracked job is waited on directly.
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
    /// of the jobs' statuses (POSIX). Each completed job is consumed here (the
    /// single-consumption default), or retained if jobs.retain_completed is
    /// set.
    if (argc == 1) {
        bool retain = config_get_bool("jobs.retain_completed", false);
        job_t *job = current_executor->jobs;
        while (job) {
            /// Capture next before a possible remove frees this node.
            job_t *next = job->next;
            int brk = executor_reap_job(job, true);
            if (brk > 0) {
                return 128 + brk;
            }
            if (job->state == JOB_DONE) {
                if (retain) {
                    job->reported = true;
                } else {
                    executor_remove_job(current_executor, job->job_id);
                }
            }
            job = next;
        }
        executor_update_job_status(current_executor);
        return 0;
    }

    int overall_exit_status = 0;

    for (int i = 1; i < argc; i++) {
        char *endptr;

        if (argv[i][0] == '%') {
            /// Job-spec form. The shared resolver handles %n, %%/%+ (current),
            /// %- (previous), %prefix, and %?substring, rejecting an out-of-
            /// range or ambiguous spec rather than aliasing it onto an
            /// unrelated job.
            const char *reason;
            job_t *job =
                executor_resolve_job_spec(current_executor, argv[i], &reason);
            if (!job) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(), "%s: %s",
                                      argv[i], reason);
                return 127;
            }
            int break_signo;
            overall_exit_status = wait_for_tracked_job(job, &break_signo);
            if (break_signo > 0) {
                return overall_exit_status;
            }
            continue;
        }

        /// Pid form. Bound the value to a valid pid_t before narrowing: an
        /// out-of-range literal would otherwise wrap under the (pid_t) cast --
        /// e.g. 4294967295 to -1, making waitpid reap an arbitrary child -- so
        /// reject it rather than aliasing it onto an unintended target.
        errno = 0;
        long target = strtol(argv[i], &endptr, 10);
        if (*endptr != '\0' || target <= 0 || errno == ERANGE ||
            target > INT_MAX) {
            executor_error_report(current_executor, SHELL_ERR_INVALID_ARGUMENT,
                                  builtin_get_source_location(),
                                  "%s: arguments must be process or job IDs",
                                  argv[i]);
            return 1;
        }

        /// Pid form: resolve a tracked job so the wait runs through the
        /// completed-job lifecycle (consume-on-wait, or retain under
        /// jobs.retain_completed); a pid with no tracked job is waited
        /// directly.
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
