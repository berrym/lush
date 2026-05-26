/**
 * @file debug_profile.c
 * @brief Performance Profiling for Shell Debugging
 *
 * Provides profiling capabilities for measuring function execution times,
 * identifying performance hotspots, and generating detailed performance
 * reports for shell script debugging.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "debug.h"
#include "errors.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Start a profiling session
 * @param ctx Debug context to enable profiling on
 */
void debug_profile_start(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    ctx->profile_enabled = true;
    ctx->timing_enabled = true;

    /// Reset profile data
    debug_profile_reset(ctx);

    debug_printf(ctx, "Performance profiling started\n");
}

/**
 * @brief Stop a profiling session
 * @param ctx Debug context to disable profiling on
 */
void debug_profile_stop(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    ctx->profile_enabled = false;
    ctx->timing_enabled = false;

    debug_printf(ctx, "Performance profiling stopped\n");
}

/**
 * @brief Record function entry for profiling
 * @param ctx Debug context
 * @param function Name of the function being entered
 */
void debug_profile_function_enter(debug_context_t *ctx, const char *function) {
    if (!ctx || !ctx->profile_enabled || !function) {
        return;
    }

    /// Find or create profile data entry
    profile_data_t *profile = ctx->profile_data;
    while (profile) {
        if (strcmp(profile->function_name, function) == 0) {
            break;
        }
        profile = profile->next;
    }

    /// Create new profile entry if not found
    if (!profile) {
        profile = malloc(sizeof(profile_data_t));
        if (!profile) {
            return;
        }

        profile->function_name = strdup(function);
        profile->file_path = NULL;
        profile->total_time_ns = 0;
        profile->call_count = 0;
        profile->min_time_ns = LONG_MAX;
        profile->max_time_ns = 0;
        profile->next = ctx->profile_data;

        ctx->profile_data = profile;
    }

    /// Record function entry time in current frame
    if (ctx->current_frame) {
        clock_gettime(CLOCK_MONOTONIC, &ctx->current_frame->start_time);
    }
}

/**
 * @brief Record function exit for profiling
 * @param ctx Debug context
 * @param function Name of the function being exited
 */
void debug_profile_function_exit(debug_context_t *ctx, const char *function) {
    if (!ctx || !ctx->profile_enabled || !function || !ctx->current_frame) {
        return;
    }

    /// Calculate execution time
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    long duration_ns =
        (end_time.tv_sec - ctx->current_frame->start_time.tv_sec) *
            1000000000L +
        (end_time.tv_nsec - ctx->current_frame->start_time.tv_nsec);

    /// Find profile data entry
    profile_data_t *profile = ctx->profile_data;
    while (profile) {
        if (strcmp(profile->function_name, function) == 0) {
            /// Update profile statistics
            profile->total_time_ns += duration_ns;
            profile->call_count++;

            if (duration_ns < profile->min_time_ns) {
                profile->min_time_ns = duration_ns;
            }
            if (duration_ns > profile->max_time_ns) {
                profile->max_time_ns = duration_ns;
            }

            break;
        }
        profile = profile->next;
    }
}

/**
 * @brief Generate and display a profiling report
 * @param ctx Debug context containing profile data
 */
void debug_profile_report(debug_context_t *ctx) {
    if (!ctx || !ctx->profile_data) {
        debug_printf(ctx, "No profile data available\n");
        return;
    }

    debug_print_header(ctx, "Performance Profile Report");

    /// Calculate session duration
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    long session_duration_ns =
        (current_time.tv_sec - ctx->session_start.tv_sec) * 1000000000L +
        (current_time.tv_nsec - ctx->session_start.tv_nsec);

    char session_time_str[64];
    debug_format_time(session_duration_ns, session_time_str,
                      sizeof(session_time_str));

    debug_printf(ctx, "Session Duration: %s\n", session_time_str);
    debug_printf(ctx, "Total Commands: %ld\n", ctx->total_commands);
    debug_printf(ctx, "\n");

    /// Header
    debug_printf(ctx, "%-20s %8s %12s %12s %12s %12s\n", "Function", "Calls",
                 "Total", "Average", "Min", "Max");
    debug_printf(ctx, "%-20s %8s %12s %12s %12s %12s\n", "--------", "-----",
                 "-----", "-------", "---", "---");

    /// Sort profile data by total time (simple bubble sort for now)
    profile_data_t *sorted_list = NULL;
    profile_data_t *current = ctx->profile_data;

    /// Copy to sorted list
    while (current) {
        profile_data_t *new_entry = malloc(sizeof(profile_data_t));
        if (!new_entry) {
            break;
        }

        *new_entry = *current;
        new_entry->function_name = strdup(current->function_name);
        new_entry->file_path =
            current->file_path ? strdup(current->file_path) : NULL;
        new_entry->next = NULL;

        /// Insert in sorted order (by total time, descending)
        if (!sorted_list ||
            new_entry->total_time_ns > sorted_list->total_time_ns) {
            new_entry->next = sorted_list;
            sorted_list = new_entry;
        } else {
            profile_data_t *pos = sorted_list;
            while (pos->next &&
                   pos->next->total_time_ns > new_entry->total_time_ns) {
                pos = pos->next;
            }
            new_entry->next = pos->next;
            pos->next = new_entry;
        }

        current = current->next;
    }

    /// Print sorted results
    profile_data_t *profile = sorted_list;
    while (profile) {
        char total_str[32], avg_str[32], min_str[32], max_str[32];

        debug_format_time(profile->total_time_ns, total_str, sizeof(total_str));
        debug_format_time(profile->call_count > 0
                              ? profile->total_time_ns / profile->call_count
                              : 0,
                          avg_str, sizeof(avg_str));
        debug_format_time(
            profile->min_time_ns == LONG_MAX ? 0 : profile->min_time_ns,
            min_str, sizeof(min_str));
        debug_format_time(profile->max_time_ns, max_str, sizeof(max_str));

        debug_printf(ctx, "%-20s %8d %12s %12s %12s %12s\n",
                     profile->function_name, profile->call_count, total_str,
                     avg_str, min_str, max_str);

        profile = profile->next;
    }

    debug_printf(ctx, "\n");

    /// Performance analysis
    debug_printf(ctx, "Performance Analysis:\n");

    /// Find hotspots
    profile_data_t *hotspot = sorted_list;
    if (hotspot) {
        debug_printf(ctx, "  Hotspot: %s (%.1f%% of total time)\n",
                     hotspot->function_name,
                     (double)hotspot->total_time_ns / ctx->total_time_ns *
                         100.0);
    }

    /// Find most called function
    profile_data_t *most_called = sorted_list;
    profile_data_t *temp = sorted_list;
    while (temp) {
        if (temp->call_count > most_called->call_count) {
            most_called = temp;
        }
        temp = temp->next;
    }

    if (most_called) {
        debug_printf(ctx, "  Most Called: %s (%d calls)\n",
                     most_called->function_name, most_called->call_count);
    }

    /// Find slowest average
    profile_data_t *slowest_avg = sorted_list;
    long slowest_avg_time = 0;
    temp = sorted_list;
    while (temp) {
        long avg_time =
            temp->call_count > 0 ? temp->total_time_ns / temp->call_count : 0;
        if (avg_time > slowest_avg_time) {
            slowest_avg_time = avg_time;
            slowest_avg = temp;
        }
        temp = temp->next;
    }

    if (slowest_avg) {
        char avg_str[32];
        debug_format_time(slowest_avg_time, avg_str, sizeof(avg_str));
        debug_printf(ctx, "  Slowest Average: %s (%s per call)\n",
                     slowest_avg->function_name, avg_str);
    }

    /// Cleanup sorted list
    while (sorted_list) {
        profile_data_t *next = sorted_list->next;
        free(sorted_list->function_name);
        free(sorted_list->file_path);
        free(sorted_list);
        sorted_list = next;
    }
}

/**
 * @brief Reset all profiling data
 * @param ctx Debug context to reset profile data for
 */
void debug_profile_reset(debug_context_t *ctx) {
    if (!ctx) {
        return;
    }

    /// Clean up existing profile data
    profile_data_t *profile = ctx->profile_data;
    while (profile) {
        profile_data_t *next = profile->next;
        free(profile->function_name);
        free(profile->file_path);
        free(profile);
        profile = next;
    }

    ctx->profile_data = NULL;
    ctx->total_commands = 0;
    ctx->total_time_ns = 0;
    clock_gettime(CLOCK_MONOTONIC, &ctx->session_start);

    debug_printf(ctx, "Profile data reset\n");
}
