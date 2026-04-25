/**
 * @file lush_fork.h
 * @brief fork() wrapper that flushes stdio buffers first
 *
 * POSIX-mandated discipline: before fork(), any pending content in the
 * libc stdio buffers must be flushed. Otherwise both parent and child
 * inherit identical buffer copies, and any later flush in either process
 * writes the same content twice (or, when the child has set up
 * redirections, writes the parent's pending content to the redirection
 * target). This is a well-known source of duplicated / misdirected
 * output.
 *
 * Every fork site in lush — in the shell core, in the LLE, anywhere —
 * should call lush_fork() instead of fork() directly. Implemented as
 * static inline so this header has no link-time dependency on any other
 * translation unit, allowing it to be included from any subsystem
 * (including liblle.a, which does not link the shell-execution layer).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LUSH_FORK_H
#define LUSH_FORK_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief fork() wrapper that flushes libc stdio buffers first
 *
 * Same return-value semantics as fork(): -1 on error, 0 in the child,
 * child PID in the parent. The flush is the only added behavior.
 *
 * @return Same as fork()
 */
static inline pid_t lush_fork(void) {
    fflush(stdout);
    fflush(stderr);
    return fork();
}

#endif /* LUSH_FORK_H */
