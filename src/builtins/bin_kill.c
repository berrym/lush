/**
 * @file bin_kill.c
 * @brief `kill` builtin -- send a signal to jobs and processes
 *
 * Sends a signal (default TERM) to each target: a process id, a negative pid
 * (a process group), or a job spec (%n, %%, %+, %-, %prefix, %?substring)
 * resolved against the shell's job list. A builtin is required because the
 * external /bin/kill cannot see the shell's jobs, so `kill %1` there fails.
 *
 * Forms:
 *   kill [-s SIGNAL | -SIGNAL | -SIGNUM] [--] target...
 *   kill -l [number | name]...
 *
 * The signal argument accepts a name (TERM), a SIG-prefixed name (SIGTERM), or
 * a number (15). Errors are reported through the structured error system with
 * an actionable help line.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "signals.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Number of signal columns per row in the `kill -l` listing.
#define KILL_LIST_COLUMNS 5

/// Print the full `kill -l` listing: every signal the platform defines, by
/// number and canonical name, laid out in aligned columns. lush curates one
/// numbered form (bash lists numbered columns, zsh lists bare names); the
/// numbers make the listing self-describing as a number<->name reference.
static void kill_print_signal_list(void) {
    int col = 0;
    for (int n = 1; n < NSIG; n++) {
        const char *name = signal_number_to_name(n);
        if (!name) {
            continue; /// A number the platform leaves unnamed (a gap or RT).
        }
        printf("%2d) SIG%-9s", n, name);
        if (++col % KILL_LIST_COLUMNS == 0) {
            printf("\n");
        }
    }
    if (col % KILL_LIST_COLUMNS != 0) {
        printf("\n");
    }
}

/// Handle `kill -l [arg...]`. With no argument, list every signal; with numeric
/// arguments, print the corresponding name (stripping a 128+signo exit-status
/// offset); with name arguments, print the number.
static int kill_list_signals(int argc, char **argv, int start) {
    if (start >= argc) {
        kill_print_signal_list();
        return 0;
    }

    int status = 0;
    for (int i = start; i < argc; i++) {
        if (isdigit((unsigned char)argv[i][0])) {
            int n = atoi(argv[i]);
            /// `kill -l $?` reports the signal that killed a job, whose exit
            /// status is 128 + signo; map it back.
            if (n > 128) {
                n -= 128;
            }
            const char *name = signal_number_to_name(n);
            if (name) {
                printf("%s\n", name);
            } else {
                executor_error_report_with_help(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "run `kill -l` to list the valid signal numbers",
                    "%s: invalid signal number", argv[i]);
                status = 1;
            }
        } else {
            int n = get_signal_number(argv[i]);
            if (n > 0) {
                printf("%d\n", n);
            } else {
                executor_error_report_with_help(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "run `kill -l` to list the valid signal names",
                    "%s: invalid signal name", argv[i]);
                status = 1;
            }
        }
    }
    return status;
}

/// Resolve a signal specification (name, SIG-name, or number) to a signal
/// number for the kill target, or emit a targeted error and return -1. Signal 0
/// (the existence probe) is valid; pseudo-signals and out-of-range values are
/// not.
static int kill_resolve_signal(const char *spec) {
    int signo = get_signal_number(spec);
    if (signo < 0 || signo >= NSIG) {
        executor_error_report_with_help(
            current_executor, SHELL_ERR_INVALID_ARGUMENT,
            builtin_get_source_location(),
            "run `kill -l` to list the valid signals",
            "%s: invalid signal specification", spec);
        return -1;
    }
    return signo;
}

/// Report a failed kill(2) with the errno message, mapping the common failures
/// to their structured code.
static void kill_report_send_failure(const char *target_text, pid_t target,
                                     int saved_errno) {
    (void)target;
    shell_error_code_t code = (saved_errno == EPERM)
                                  ? SHELL_ERR_PERMISSION_DENIED
                                  : SHELL_ERR_JOB_NOT_FOUND;
    executor_error_report(current_executor, code, builtin_get_source_location(),
                          "%s: %s", target_text, strerror(saved_errno));
}

int bin_kill(int argc, char **argv) {
    if (!current_executor) {
        return 1;
    }

    int i = 1;

    /// `kill -l ...` lists signal names rather than sending anything.
    if (i < argc && strcmp(argv[i], "-l") == 0) {
        return kill_list_signals(argc, argv, i + 1);
    }

    int signo = SIGTERM;

    if (i < argc && strcmp(argv[i], "-s") == 0) {
        /// `-s SIGNAL`: the signal is the following argument.
        if (i + 1 >= argc) {
            executor_error_report_with_help(
                current_executor, SHELL_ERR_INVALID_ARGUMENT,
                builtin_get_source_location(),
                "specify a signal, e.g. `kill -s TERM %1`",
                "-s requires a signal name or number");
            return 2;
        }
        signo = kill_resolve_signal(argv[i + 1]);
        if (signo < 0) {
            return 1;
        }
        i += 2;
    } else if (i < argc && strcmp(argv[i], "--") == 0) {
        i++;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        /// `-SIGNAL` / `-SIGNUM` (e.g. -9, -KILL, -SIGKILL).
        signo = kill_resolve_signal(argv[i] + 1);
        if (signo < 0) {
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        executor_error_report_with_help(
            current_executor, SHELL_ERR_INVALID_ARGUMENT,
            builtin_get_source_location(),
            "usage: kill [-s SIGNAL | -SIGNAL] pid | %job ...  (or: kill -l)",
            "no process or job specified");
        return 2;
    }

    int status = 0;
    for (; i < argc; i++) {
        pid_t target;
        if (argv[i][0] == '%') {
            /// Job spec: resolve against the shell's job list and signal the
            /// job's process (or group, via the negated pgid from job_target).
            const char *reason;
            job_t *job =
                executor_resolve_job_spec(current_executor, argv[i], &reason);
            if (!job) {
                executor_error_report(current_executor, SHELL_ERR_JOB_NOT_FOUND,
                                      builtin_get_source_location(), "%s: %s",
                                      argv[i], reason);
                status = 1;
                continue;
            }
            target = job_target(job);
        } else {
            /// A pid, or a negative pid for a process group. Reject a
            /// non-numeric or out-of-range operand rather than aliasing it.
            char *end;
            errno = 0;
            long value = strtol(argv[i], &end, 10);
            if (argv[i][0] == '\0' || *end != '\0' || errno == ERANGE ||
                value < INT_MIN || value > INT_MAX) {
                executor_error_report_with_help(
                    current_executor, SHELL_ERR_INVALID_ARGUMENT,
                    builtin_get_source_location(),
                    "a target is a pid, a negative pid for a process group, or "
                    "a %job spec",
                    "%s: arguments must be process or job IDs", argv[i]);
                status = 1;
                continue;
            }
            target = (pid_t)value;
        }

        if (kill(target, signo) != 0) {
            kill_report_send_failure(argv[i], target, errno);
            status = 1;
        }
    }

    return status;
}
