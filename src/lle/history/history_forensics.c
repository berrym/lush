/**
 * @file history_forensics.c
 * @brief LLE History System - Forensic Tracking Implementation (Spec 09 Phase 4
 * Day 11)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements forensic-grade metadata tracking for command history:
 * - Process and session tracking (PID, session ID)
 * - User context (UID, GID)
 * - Terminal information
 * - High-precision timing (nanosecond resolution)
 * - Usage analytics (access count, last access time)
 * - Execution duration tracking
 *
 * Forensic data enables:
 * - Command provenance and accountability
 * - Performance analysis and optimization
 * - Usage pattern analytics
 * - Security auditing
 * - Workflow reconstruction
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Platform-specific includes for terminal name */
#if defined(__linux__)
#include <linux/tty.h>
#include <sys/ioctl.h>
#endif

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get terminal device name
 *
 * Retrieves the name of the terminal device associated with standard input.
 * Falls back to "unknown_tty" if connected to a terminal but name is
 * unavailable, or "not_a_tty" if not connected to a terminal.
 *
 * @param buffer Output buffer for terminal name
 * @param size Buffer size in bytes
 * @return true on success, false if not connected to a terminal
 */
static bool get_terminal_name(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return false;
    }

    /* Try ttyname() first */
    const char *tty = ttyname(STDIN_FILENO);
    if (tty) {
        strncpy(buffer, tty, size - 1);
        buffer[size - 1] = '\0';
        return true;
    }

    /* Fallback: check if connected to terminal */
    if (isatty(STDIN_FILENO)) {
        strncpy(buffer, "unknown_tty", size - 1);
        buffer[size - 1] = '\0';
        return true;
    }

    /* Not a terminal */
    strncpy(buffer, "not_a_tty", size - 1);
    buffer[size - 1] = '\0';
    return false;
}

/**
 * @brief Duplicate string using memory pool
 *
 * Allocates memory from the LLE memory pool and copies the input string.
 *
 * @param str String to duplicate (may be NULL)
 * @return Pointer to duplicated string, or NULL if str is NULL or allocation
 * fails
 */
static char *pool_strdup(const char *str) {
    if (!str)
        return NULL;

    size_t len = strlen(str) + 1;
    char *dup = lle_pool_alloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

/* ============================================================================
 * PUBLIC API - FORENSIC CONTEXT
 * ============================================================================
 */

/**
 * @brief Get high-precision timestamp in nanoseconds
 *
 * Uses CLOCK_MONOTONIC for consistent timing measurements.
 * Falls back to time(NULL) with second precision if clock_gettime fails.
 *
 * @return Timestamp in nanoseconds since arbitrary epoch
 */
uint64_t lle_forensic_get_timestamp_ns(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        /* Fallback to lower precision */
        return (uint64_t)time(NULL) * 1000000000ULL;
    }

    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Capture current forensic context
 *
 * Captures process, user, terminal, and timing information for forensic
 * tracking. Includes PID, session ID, UID, GID, terminal name, working
 * directory, and high-precision timestamp.
 *
 * @param context Output structure for forensic data (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if context is
 * NULL
 */
lle_result_t lle_forensic_capture_context(lle_forensic_context_t *context) {
    if (!context) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Initialize structure */
    memset(context, 0, sizeof(lle_forensic_context_t));

    /* Capture process information */
    context->process_id = getpid();
    context->session_id = getsid(0);
    context->user_id = getuid();
    context->group_id = getgid();

    /* Capture terminal name */
    char tty_buffer[256];
    if (get_terminal_name(tty_buffer, sizeof(tty_buffer))) {
        context->terminal_name = pool_strdup(tty_buffer);
    } else {
        context->terminal_name = NULL;
    }

    /* Capture working directory */
    char cwd_buffer[4096];
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) != NULL) {
        context->working_directory = pool_strdup(cwd_buffer);
    } else {
        context->working_directory = NULL;
    }

    /* Capture high-precision timestamp */
    context->timestamp_ns = lle_forensic_get_timestamp_ns();

    return LLE_SUCCESS;
}

/**
 * @brief Apply forensic context to history entry
 *
 * Populates forensic metadata fields in a history entry from context.
 * Copies process info, terminal name, working directory, timing fields,
 * and initializes usage tracking.
 *
 * @param entry History entry to populate (must not be NULL)
 * @param context Forensic context to apply (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if entry or
 * context is NULL
 */
lle_result_t
lle_forensic_apply_to_entry(lle_history_entry_t *entry,
                            const lle_forensic_context_t *context) {
    if (!entry || !context) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Apply process information */
    entry->process_id = context->process_id;
    entry->session_id = context->session_id;
    entry->user_id = context->user_id;
    entry->group_id = context->group_id;

    /* Apply terminal name */
    if (context->terminal_name) {
        entry->terminal_name = pool_strdup(context->terminal_name);
    }

    /* Apply working directory (if not already set) */
    if (!entry->working_directory && context->working_directory) {
        entry->working_directory = pool_strdup(context->working_directory);
    }

    /* Initialize timing fields */
    entry->start_time_ns = context->timestamp_ns;
    entry->end_time_ns = 0; /* Will be set by mark_end */
    entry->duration_ms = 0;

    /* Initialize usage tracking */
    entry->usage_count = 0;
    entry->last_access_time =
        context->timestamp_ns / 1000000000ULL; /* Convert to seconds */

    return LLE_SUCCESS;
}

/**
 * @brief Free forensic context resources
 *
 * Frees allocated strings in forensic context, including terminal name
 * and working directory. Safe to call with NULL context.
 *
 * @param context Forensic context to free (may be NULL)
 */
void lle_forensic_free_context(lle_forensic_context_t *context) {
    if (!context) {
        return;
    }

    if (context->terminal_name) {
        lle_pool_free(context->terminal_name);
        context->terminal_name = NULL;
    }

    if (context->working_directory) {
        lle_pool_free(context->working_directory);
        context->working_directory = NULL;
    }
}

/* ============================================================================
 * PUBLIC API - TIMING AND DURATION
 * ============================================================================
 */

/**
 * @brief Record command execution start time
 *
 * Captures high-precision start time for duration calculation.
 * Resets end_time_ns and duration_ms to zero.
 *
 * @param entry History entry to update (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if entry is NULL
 */
lle_result_t lle_forensic_mark_start(lle_history_entry_t *entry) {
    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    entry->start_time_ns = lle_forensic_get_timestamp_ns();
    entry->end_time_ns = 0;
    entry->duration_ms = 0;

    return LLE_SUCCESS;
}

/**
 * @brief Record command execution end time and calculate duration
 *
 * Captures end time and calculates execution duration in milliseconds.
 * Duration is set to zero if end time is not after start time.
 *
 * @param entry History entry to update (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if entry is NULL
 */
lle_result_t lle_forensic_mark_end(lle_history_entry_t *entry) {
    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Capture end time */
    entry->end_time_ns = lle_forensic_get_timestamp_ns();

    /* Calculate duration in milliseconds */
    if (entry->end_time_ns > entry->start_time_ns) {
        uint64_t duration_ns = entry->end_time_ns - entry->start_time_ns;
        entry->duration_ms = (uint32_t)(duration_ns / 1000000ULL);
    } else {
        /* Clock issue or zero duration */
        entry->duration_ms = 0;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API - USAGE ANALYTICS
 * ============================================================================
 */

/**
 * @brief Increment usage count for history entry
 *
 * Tracks how many times a command has been reused via expansion or recall.
 * Also updates the last access time to current time.
 *
 * @param entry History entry to update (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if entry is NULL
 */
lle_result_t lle_forensic_increment_usage(lle_history_entry_t *entry) {
    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    entry->usage_count++;

    /* Also update last access time */
    entry->last_access_time = time(NULL);

    return LLE_SUCCESS;
}

/**
 * @brief Update last access time for history entry
 *
 * Records timestamp when entry was last accessed (searched, expanded, etc.).
 * Uses current time from time(NULL).
 *
 * @param entry History entry to update (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if entry is NULL
 */
lle_result_t lle_forensic_update_access_time(lle_history_entry_t *entry) {
    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    entry->last_access_time = time(NULL);

    return LLE_SUCCESS;
}
