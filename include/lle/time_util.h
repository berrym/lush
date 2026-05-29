/**
 * @file time_util.h
 * @brief Canonical monotonic-microsecond clock for LLE
 *
 * Every LLE timestamp -- buffer modification times, input-parser state
 * changes, event timer triggers, history-event timestamps, terminal
 * detection windows -- must go through this single function. Routing
 * everything through one canonical implementation guarantees ordering
 * comparisons across subsystems remain meaningful and removes the long
 * tail of identical static `get_current_time_us` / `get_timestamp_us`
 * copies that had accumulated across the LLE.
 *
 * Implementation: `clock_gettime(CLOCK_MONOTONIC, ...)` in
 * src/lle/terminal/terminal_unix_interface.c.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LLE_TIME_UTIL_H
#define LLE_TIME_UTIL_H

#include <stdint.h>

/**
 * @brief Current monotonic time, in microseconds
 *
 * Monotonic in the POSIX sense (CLOCK_MONOTONIC): never decreases, not
 * affected by wall-clock adjustments. Suitable for measuring durations
 * and for ordering events within a single process; not suitable for
 * cross-host or wall-time comparisons.
 *
 * @return Microseconds since an unspecified fixed monotonic origin.
 */
uint64_t lle_get_current_time_microseconds(void);

#endif /* LLE_TIME_UTIL_H */
