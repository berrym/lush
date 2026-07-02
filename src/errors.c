/**
 * @file errors.c
 * @brief Legacy error-reporting helpers for pre-executor and fatal paths
 *
 * Provides the small set of legacy helpers that remain after migration
 * to the structured shell-error system in shell_error.h. See errors.h
 * for the policy on when each is appropriate.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "errors.h"

#include "lush.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Internal error printing function
 *
 * Formats and prints error messages, optionally including errno info.
 *
 * @param errnoflag If true, append strerror(err) to message
 * @param err Error number to use with strerror
 * @param fmt Format string
 * @param args Variable argument list
 */
static void do_error(bool errnoflag, int err, const char *fmt, va_list args) {
    char buf[MAXLINE + 1] = {'\0'};
    char tmp[MAXLINE + 1] = {'\0'};

    strcpy(tmp, buf);

    vsprintf(buf, fmt, args);
    if (errnoflag) {
        sprintf(buf, "%s: %s", tmp, strerror(err));
    }

    strncat(buf, "\n", 2);
    fflush(stdout); /// in case stdout and stdin are the same
    fputs(buf, stderr);
    fflush(NULL); /// flush all stdio output streams
}

void error_return(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_error(true, errno, fmt, args);
    va_end(args);
}

void error_syscall(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_error(true, errno, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

void error_abort(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_error(false, 0, fmt, args);
    va_end(args);
    abort();            /// dump core and terminate
    exit(EXIT_FAILURE); /// should never happen
}
