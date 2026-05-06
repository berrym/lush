/**
 * @file bin_times.c
 * @brief `times` builtin -- print accumulated user/system times
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"

#include <errno.h>
#include <sys/times.h>

/**
 * @brief Display accumulated user and system times
 *
 * Prints the accumulated user and system times for the shell
 * and for processes run from the shell (children).
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 * @return 0 on success, 1 on error
 */
int bin_times(int argc, char **argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning

    struct tms tms_buf;
    clock_t real_time;

    // Get process times
    real_time = times(&tms_buf);
    if (real_time == (clock_t)-1) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "times: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    // Get clock ticks per second for conversion
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) {
        ticks_per_sec = 100; // Default fallback
    }

    // Convert ticks to seconds and format output
    double user_time = (double)tms_buf.tms_utime / ticks_per_sec;
    double system_time = (double)tms_buf.tms_stime / ticks_per_sec;
    double child_user_time = (double)tms_buf.tms_cutime / ticks_per_sec;
    double child_system_time = (double)tms_buf.tms_cstime / ticks_per_sec;

    // Output in POSIX format: user_time system_time child_user_time
    // child_system_time
    printf("%.2dm%.3fs %.2dm%.3fs\n", (int)(user_time / 60),
           user_time - (int)(user_time / 60) * 60, (int)(system_time / 60),
           system_time - (int)(system_time / 60) * 60);
    printf("%.2dm%.3fs %.2dm%.3fs\n", (int)(child_user_time / 60),
           child_user_time - (int)(child_user_time / 60) * 60,
           (int)(child_system_time / 60),
           child_system_time - (int)(child_system_time / 60) * 60);

    return 0;
}
