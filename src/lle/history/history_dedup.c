/**
 * @file history_dedup.c
 * @brief Intelligent deduplication engine for LLE history system
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Part of Spec 09: History System (Phase 4 Day 12)
 *
 * Provides intelligent duplicate detection and merging with configurable
 * strategies. Supports frequency tracking, recency-based preservation,
 * and minimal performance impact through efficient algorithms.
 *
 * DEDUPLICATION STRATEGIES:
 * - IGNORE: Ignore all duplicates (simple dedup)
 * - KEEP_RECENT: Keep most recent, discard older
 * - KEEP_FREQUENT: Keep entry with highest usage count
 * - MERGE_METADATA: Merge forensic metadata, keep recent command
 * - KEEP_ALL: No deduplication (track frequency only)
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include "lle/performance.h"
#include "lle/unicode_compare.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================
 */

/**
 * Deduplication engine state
 */
struct lle_history_dedup_engine {
    lle_history_dedup_strategy_t strategy; /* Active strategy */
    lle_history_dedup_scope_t scope;       /* Dedup scope (how far to scan) */
    lle_history_core_t *history_core;      /* Reference to history core */

    /* Statistics */
    uint64_t duplicates_detected; /* Total duplicates found */
    uint64_t duplicates_merged;   /* Total duplicates merged */
    uint64_t duplicates_ignored;  /* Total duplicates ignored */

    /* Performance tracking */
    lle_performance_monitor_t *perf_monitor; /* Performance monitor */

    /* Configuration */
    bool case_sensitive;    /* Case-sensitive comparison */
    bool trim_whitespace;   /* Trim whitespace before compare */
    bool merge_forensics;   /* Merge forensic metadata */
    bool unicode_normalize; /* Use Unicode NFC normalization */
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Normalize command for comparison
 *
 * Applies configured normalization (case conversion, whitespace trimming)
 * to prepare a command for duplicate comparison.
 *
 * @param dedup Dedup engine with configuration (must not be NULL)
 * @param command Command string to normalize (must not be NULL)
 * @param normalized Output buffer for normalized command (must not be NULL)
 * @param normalized_size Size of normalized buffer in bytes
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL, LLE_ERROR_BUFFER_OVERFLOW if command is too long for buffer
 */
static lle_result_t normalize_command(const lle_history_dedup_engine_t *dedup,
                                      const char *command, char *normalized,
                                      size_t normalized_size) {
    if (!dedup || !command || !normalized || normalized_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    const char *src = command;
    char *dst = normalized;
    size_t remaining =
        normalized_size - 1; /* Reserve space for null terminator */

    /* Trim leading whitespace if configured */
    if (dedup->trim_whitespace) {
        while (*src && (*src == ' ' || *src == '\t')) {
            src++;
        }
    }

    /* Copy and optionally convert case */
    while (*src && remaining > 0) {
        char c = *src++;

        /* Convert to lowercase if case-insensitive */
        if (!dedup->case_sensitive && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }

        *dst++ = c;
        remaining--;
    }

    /* Trim trailing whitespace if configured */
    if (dedup->trim_whitespace) {
        while (dst > normalized && (dst[-1] == ' ' || dst[-1] == '\t')) {
            dst--;
        }
    }

    *dst = '\0';

    if (*src && remaining == 0) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Compare two commands for equality (respecting dedup config)
 *
 * Compares commands using the engine's configuration for case sensitivity,
 * whitespace handling, and Unicode normalization.
 *
 * @param dedup Dedup engine with comparison configuration (may be NULL)
 * @param cmd1 First command to compare (may be NULL)
 * @param cmd2 Second command to compare (may be NULL)
 * @return true if commands are equal, false otherwise or if any parameter is
 * NULL
 */
static bool commands_equal(const lle_history_dedup_engine_t *dedup,
                           const char *cmd1, const char *cmd2) {
    if (!dedup || !cmd1 || !cmd2) {
        return false;
    }

    /* Fast path: pointer equality */
    if (cmd1 == cmd2) {
        return true;
    }

    /* Use Unicode-aware comparison if enabled */
    if (dedup->unicode_normalize) {
        lle_unicode_compare_options_t opts = {
            .normalize = true,
            .case_insensitive = !dedup->case_sensitive,
            .trim_whitespace = dedup->trim_whitespace};
        return lle_unicode_strings_equal(cmd1, cmd2, &opts);
    }

    /* Fast path: if both config options are default (case-sensitive, no trim),
     * we can just do direct strcmp */
    if (dedup->case_sensitive && !dedup->trim_whitespace) {
        return strcmp(cmd1, cmd2) == 0;
    }

/* Slow path: need whitespace/case normalization (no Unicode)
 * Use reasonable stack buffers (4KB each) - commands are typically < 1KB
 * For very large commands, we'll truncate during normalization */
#define NORM_BUFFER_SIZE 4096
    char norm1[NORM_BUFFER_SIZE];
    char norm2[NORM_BUFFER_SIZE];

    if (normalize_command(dedup, cmd1, norm1, NORM_BUFFER_SIZE) !=
        LLE_SUCCESS) {
        return false;
    }

    if (normalize_command(dedup, cmd2, norm2, NORM_BUFFER_SIZE) !=
        LLE_SUCCESS) {
        return false;
    }

    return strcmp(norm1, norm2) == 0;
}

/**
 * @brief Merge forensic metadata from old entry to new entry
 *
 * Combines usage counts, preserves earliest start time, keeps most recent
 * access time, and accumulates execution duration.
 *
 * @param new_entry Entry to update (keep) (must not be NULL)
 * @param old_entry Entry being merged (will be discarded) (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if either entry
 * is NULL
 */
static lle_result_t
merge_forensic_metadata(lle_history_entry_t *new_entry,
                        const lle_history_entry_t *old_entry) {
    if (!new_entry || !old_entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Merge usage counts */
    new_entry->usage_count += old_entry->usage_count;

    /* Keep earliest start time (when command was first used) */
    if (old_entry->start_time_ns > 0 &&
        (new_entry->start_time_ns == 0 ||
         old_entry->start_time_ns < new_entry->start_time_ns)) {
        new_entry->start_time_ns = old_entry->start_time_ns;
    }

    /* Keep most recent access time */
    if (old_entry->last_access_time > new_entry->last_access_time) {
        new_entry->last_access_time = old_entry->last_access_time;
    }

    /* Accumulate total execution time for duration tracking */
    if (old_entry->end_time_ns > 0 && old_entry->start_time_ns > 0) {
        uint64_t old_duration_ns =
            old_entry->end_time_ns - old_entry->start_time_ns;
        uint64_t new_duration_ns = 0;

        if (new_entry->end_time_ns > 0 && new_entry->start_time_ns > 0) {
            new_duration_ns = new_entry->end_time_ns - new_entry->start_time_ns;
        }

        /* Store combined duration in duration_ms field (convert to ms) */
        uint64_t total_duration_ms =
            (old_duration_ns + new_duration_ns) / 1000000;
        if (total_duration_ms > UINT32_MAX) {
            total_duration_ms = UINT32_MAX;
        }
        new_entry->duration_ms = (uint32_t)total_duration_ms;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Create deduplication engine
 *
 * Allocates and initializes a new deduplication engine with the specified
 * strategy, scope, and default configuration (case-sensitive, trim whitespace,
 * merge forensics, Unicode normalize).
 *
 * @param dedup Output pointer for created engine (must not be NULL)
 * @param history_core History core to deduplicate (must not be NULL)
 * @param strategy Deduplication strategy to use
 * @param scope Deduplication scope (how far back to scan for duplicates)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup or
 * history_core is NULL, LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t lle_history_dedup_create(lle_history_dedup_engine_t **dedup,
                                      lle_history_core_t *history_core,
                                      lle_history_dedup_strategy_t strategy,
                                      lle_history_dedup_scope_t scope) {
    if (!dedup || !history_core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_history_dedup_engine_t *engine =
        lle_pool_alloc(sizeof(lle_history_dedup_engine_t));
    if (!engine) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize engine */
    engine->strategy = strategy;
    engine->scope = scope;
    engine->history_core = history_core;
    engine->duplicates_detected = 0;
    engine->duplicates_merged = 0;
    engine->duplicates_ignored = 0;
    engine->perf_monitor = NULL;

    /* Default configuration */
    engine->case_sensitive = true;
    engine->trim_whitespace = true;
    engine->merge_forensics = true;
    engine->unicode_normalize =
        true; /* Use Unicode NFC normalization by default */

    *dedup = engine;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy deduplication engine
 *
 * Frees all resources associated with the deduplication engine.
 *
 * @param dedup Engine to destroy (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL
 */
lle_result_t lle_history_dedup_destroy(lle_history_dedup_engine_t *dedup) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_pool_free(dedup);
    return LLE_SUCCESS;
}

/**
 * @brief Check if entry is duplicate of existing entry
 *
 * Scans history entries for a matching command, respecting the configured
 * dedup scope from the dedup engine's configuration.
 * Note: Must be called from within add_entry which holds the write lock.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param new_entry Entry to check for duplicates (must not be NULL)
 * @param duplicate_entry Output for found duplicate entry (may be NULL)
 * @return LLE_SUCCESS if duplicate found, LLE_ERROR_NOT_FOUND if not a
 * duplicate, LLE_ERROR_INVALID_PARAMETER if dedup or new_entry is NULL,
 *         LLE_ERROR_INVALID_STATE if history_core is not set
 */
lle_result_t lle_history_dedup_check(lle_history_dedup_engine_t *dedup,
                                     const lle_history_entry_t *new_entry,
                                     lle_history_entry_t **duplicate_entry) {
    if (!dedup || !new_entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (duplicate_entry) {
        *duplicate_entry = NULL;
    }

    /* Check dedup scope - if NONE, skip deduplication entirely */
    if (dedup->scope == LLE_HISTORY_DEDUP_SCOPE_NONE) {
        return LLE_ERROR_NOT_FOUND; /* No dedup = no duplicates found */
    }

    /* Get history entries (we need to scan backwards from most recent) */
    lle_history_core_t *core = dedup->history_core;
    if (!core) {
        return LLE_ERROR_INVALID_STATE;
    }

    /* CRITICAL: Dedup is called from within add_entry which already holds a
     * write lock. We MUST NOT call functions that acquire locks (deadlock!).
     * Instead, directly access core->entries and core->entry_count.
     * This is safe because the caller (add_entry) holds the write lock.
     */
    size_t entry_count = core->entry_count;

    /* Determine how many entries to check based on dedup scope (issue #41)
     *
     * - LLE_HISTORY_DEDUP_SCOPE_NONE:    No deduplication (handled above)
     * - LLE_HISTORY_DEDUP_SCOPE_SESSION: Check entries from current session
     *                                    (approximated by last 1000 entries)
     * - LLE_HISTORY_DEDUP_SCOPE_RECENT:  Check last N entries (default 100)
     * - LLE_HISTORY_DEDUP_SCOPE_GLOBAL:  Check entire history
     */
    size_t check_limit;
    switch (dedup->scope) {
    case LLE_HISTORY_DEDUP_SCOPE_SESSION:
        /* Session scope: check a reasonable number of recent entries
         * that would typically represent a session's worth of commands */
        check_limit = (entry_count > 1000) ? 1000 : entry_count;
        break;
    case LLE_HISTORY_DEDUP_SCOPE_RECENT:
        /* Recent scope: check last 100 entries (the original behavior) */
        check_limit = (entry_count > 100) ? 100 : entry_count;
        break;
    case LLE_HISTORY_DEDUP_SCOPE_GLOBAL:
        /* Global scope: check entire history */
        check_limit = entry_count;
        break;
    default:
        /* Fallback to recent behavior */
        check_limit = (entry_count > 100) ? 100 : entry_count;
        break;
    }

    for (size_t i = entry_count; i > entry_count - check_limit && i > 0; i--) {
        /* Direct array access - safe because caller holds lock */
        size_t index = i - 1;
        if (index >= entry_count) {
            continue; /* Safety check */
        }

        lle_history_entry_t *entry = core->entries[index];
        if (!entry) {
            continue;
        }

        /* Skip deleted/archived entries */
        if (entry->state != LLE_HISTORY_STATE_ACTIVE) {
            continue;
        }

        /* Compare commands */
        if (commands_equal(dedup, new_entry->command, entry->command)) {
            dedup->duplicates_detected++;

            if (duplicate_entry) {
                *duplicate_entry = entry;
            }

            return LLE_SUCCESS; /* Duplicate found */
        }
    }

    return LLE_ERROR_NOT_FOUND; /* Not a duplicate */
}

/**
 * @brief Merge duplicate entry with existing entry
 *
 * Merges forensic metadata (if enabled) and marks the discard entry as deleted.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param keep_entry Entry to keep (must not be NULL)
 * @param discard_entry Entry to discard (must not be NULL, will be marked
 * deleted)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if any parameter
 * is NULL or entries are not duplicates
 */
lle_result_t lle_history_dedup_merge(lle_history_dedup_engine_t *dedup,
                                     lle_history_entry_t *keep_entry,
                                     lle_history_entry_t *discard_entry) {
    if (!dedup || !keep_entry || !discard_entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Verify they're actually duplicates */
    if (!commands_equal(dedup, keep_entry->command, discard_entry->command)) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Merge forensic metadata if enabled */
    if (dedup->merge_forensics) {
        lle_result_t result =
            merge_forensic_metadata(keep_entry, discard_entry);
        if (result != LLE_SUCCESS) {
            return result;
        }
    }

    /* Mark discard entry as deleted */
    discard_entry->state = LLE_HISTORY_STATE_DELETED;

    dedup->duplicates_merged++;
    return LLE_SUCCESS;
}

/**
 * @brief Apply deduplication strategy to new entry
 *
 * Checks for duplicates and applies the configured strategy (IGNORE,
 * KEEP_RECENT, KEEP_FREQUENT, MERGE_METADATA, or KEEP_ALL).
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param new_entry New entry to check and possibly reject (must not be NULL)
 * @param entry_rejected Output indicating if entry was rejected (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup or
 * new_entry is NULL
 */
lle_result_t lle_history_dedup_apply(lle_history_dedup_engine_t *dedup,
                                     lle_history_entry_t *new_entry,
                                     bool *entry_rejected) {
    if (!dedup || !new_entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (entry_rejected) {
        *entry_rejected = false;
    }

    /* KEEP_ALL strategy: no deduplication */
    if (dedup->strategy == LLE_DEDUP_KEEP_ALL) {
        return LLE_SUCCESS;
    }

    /* Check for duplicates */
    lle_history_entry_t *duplicate = NULL;
    lle_result_t result = lle_history_dedup_check(dedup, new_entry, &duplicate);

    if (result == LLE_ERROR_NOT_FOUND) {
        /* Not a duplicate - accept entry */
        return LLE_SUCCESS;
    }

    if (result != LLE_SUCCESS) {
        /* Error during check */
        return result;
    }

    /* Found duplicate - apply strategy */
    switch (dedup->strategy) {
    case LLE_DEDUP_IGNORE:
        /* Reject new entry, keep old one */
        dedup->duplicates_ignored++;
        if (entry_rejected) {
            *entry_rejected = true;
        }
        return LLE_SUCCESS;

    case LLE_DEDUP_KEEP_RECENT:
        /* Keep new entry (more recent), mark old as deleted */
        if (duplicate) {
            result = lle_history_dedup_merge(dedup, new_entry, duplicate);
            if (result != LLE_SUCCESS) {
                return result;
            }
        }
        return LLE_SUCCESS;

    case LLE_DEDUP_KEEP_FREQUENT:
        /* Keep entry with higher usage count */
        if (duplicate && duplicate->usage_count >= new_entry->usage_count) {
            /* Old entry is more frequent - reject new one */
            if (dedup->merge_forensics) {
                merge_forensic_metadata(duplicate, new_entry);
            }
            dedup->duplicates_ignored++;
            if (entry_rejected) {
                *entry_rejected = true;
            }
        } else if (duplicate) {
            /* New entry is more frequent - keep it, discard old */
            result = lle_history_dedup_merge(dedup, new_entry, duplicate);
            if (result != LLE_SUCCESS) {
                return result;
            }
        }
        return LLE_SUCCESS;

    case LLE_DEDUP_MERGE_METADATA:
        /* Merge metadata into existing entry, reject new entry */
        if (duplicate) {
            if (dedup->merge_forensics) {
                merge_forensic_metadata(duplicate, new_entry);
            }
            dedup->duplicates_ignored++;
            if (entry_rejected) {
                *entry_rejected = true;
            }
        }
        return LLE_SUCCESS;

    case LLE_DEDUP_KEEP_ALL:
        /* Already handled above */
        return LLE_SUCCESS;
    }

    return LLE_ERROR_INVALID_PARAMETER; /* Invalid strategy */
}

/**
 * @brief Cleanup old duplicate entries
 *
 * Scans history for entries marked as deleted and counts them.
 * Note: Full implementation would physically remove deleted entries.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param entries_removed Output for count of removed entries (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL,
 *         LLE_ERROR_INVALID_STATE if history_core is not set
 */
lle_result_t lle_history_dedup_cleanup(lle_history_dedup_engine_t *dedup,
                                       size_t *entries_removed) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (entries_removed) {
        *entries_removed = 0;
    }

    lle_history_core_t *core = dedup->history_core;
    if (!core) {
        return LLE_ERROR_INVALID_STATE;
    }

    /* Get all entries and remove those marked as deleted */
    size_t entry_count = 0;
    lle_result_t result = lle_history_get_entry_count(core, &entry_count);
    if (result != LLE_SUCCESS) {
        return result;
    }

    size_t removed = 0;

    /* Scan all entries and physically remove deleted ones
     * In production, this would be more efficient with a cleanup list */
    for (size_t i = 0; i < entry_count; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(core, i, &entry);

        if (result != LLE_SUCCESS || !entry) {
            continue;
        }

        if (entry->state == LLE_HISTORY_STATE_DELETED) {
            /* This entry should be removed - for now just count it
             * Full implementation would call lle_history_remove_entry() */
            removed++;
        }
    }

    if (entries_removed) {
        *entries_removed = removed;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Set deduplication strategy
 *
 * Changes the active deduplication strategy.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param strategy New strategy to use (must be valid)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL
 * or strategy invalid
 */
lle_result_t
lle_history_dedup_set_strategy(lle_history_dedup_engine_t *dedup,
                               lle_history_dedup_strategy_t strategy) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Validate strategy */
    if (strategy < LLE_DEDUP_IGNORE || strategy > LLE_DEDUP_KEEP_ALL) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    dedup->strategy = strategy;
    return LLE_SUCCESS;
}

/**
 * @brief Get deduplication statistics
 *
 * Returns counts of detected, merged, and ignored duplicates, plus current
 * strategy.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param stats Output structure for statistics (must not be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if either
 * parameter is NULL
 */
lle_result_t
lle_history_dedup_get_stats(const lle_history_dedup_engine_t *dedup,
                            lle_history_dedup_stats_t *stats) {
    if (!dedup || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    stats->duplicates_detected = dedup->duplicates_detected;
    stats->duplicates_merged = dedup->duplicates_merged;
    stats->duplicates_ignored = dedup->duplicates_ignored;
    stats->current_strategy = dedup->strategy;

    return LLE_SUCCESS;
}

/**
 * @brief Set deduplication configuration options
 *
 * Configures how commands are compared and how duplicates are handled.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param case_sensitive Whether comparison is case-sensitive
 * @param trim_whitespace Whether to trim whitespace before comparison
 * @param merge_forensics Whether to merge forensic metadata on dedup
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL
 */
lle_result_t lle_history_dedup_configure(lle_history_dedup_engine_t *dedup,
                                         bool case_sensitive,
                                         bool trim_whitespace,
                                         bool merge_forensics) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    dedup->case_sensitive = case_sensitive;
    dedup->trim_whitespace = trim_whitespace;
    dedup->merge_forensics = merge_forensics;

    return LLE_SUCCESS;
}

/**
 * @brief Set Unicode normalization for deduplication
 *
 * Enables or disables Unicode NFC normalization during command comparison.
 * When enabled, visually identical Unicode strings will match.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param unicode_normalize Whether to use Unicode NFC normalization
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL
 */
lle_result_t
lle_history_dedup_set_unicode_normalize(lle_history_dedup_engine_t *dedup,
                                        bool unicode_normalize) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    dedup->unicode_normalize = unicode_normalize;

    return LLE_SUCCESS;
}

/**
 * @brief Perform full deduplication scan of entire history
 *
 * Scans through all history entries and marks duplicates as DELETED based on
 * the current deduplication strategy. Unlike incremental deduplication (which
 * only checks when adding new entries), this function retroactively cleans up
 * all existing duplicates in the history.
 *
 * For each unique command, keeps either the most recent (KEEP_RECENT) or most
 * frequently used (KEEP_FREQUENT) entry and marks all others as DELETED.
 *
 * @param dedup Dedup engine (must not be NULL)
 * @param duplicates_removed Output for count of duplicates marked as deleted
 * (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if dedup is NULL,
 *         LLE_ERROR_INVALID_STATE if history_core is not set
 */
lle_result_t lle_history_dedup_full_scan(lle_history_dedup_engine_t *dedup,
                                         size_t *duplicates_removed) {
    if (!dedup) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (duplicates_removed) {
        *duplicates_removed = 0;
    }

    /* KEEP_ALL strategy means no deduplication */
    if (dedup->strategy == LLE_DEDUP_KEEP_ALL) {
        return LLE_SUCCESS;
    }

    lle_history_core_t *core = dedup->history_core;
    if (!core) {
        return LLE_ERROR_INVALID_STATE;
    }

    size_t entry_count = core->entry_count;
    if (entry_count < 2) {
        return LLE_SUCCESS; /* Nothing to deduplicate */
    }

    size_t removed = 0;

    /* Scan from oldest to newest, marking duplicates
     * For KEEP_RECENT: when we find a duplicate, mark the older one as deleted
     * For KEEP_FREQUENT: compare usage counts and mark the less frequent one
     */
    for (size_t i = 0; i < entry_count; i++) {
        lle_history_entry_t *entry_i = core->entries[i];
        if (!entry_i || entry_i->state != LLE_HISTORY_STATE_ACTIVE) {
            continue;
        }

        /* Check against all later entries for duplicates */
        for (size_t j = i + 1; j < entry_count; j++) {
            lle_history_entry_t *entry_j = core->entries[j];
            if (!entry_j || entry_j->state != LLE_HISTORY_STATE_ACTIVE) {
                continue;
            }

            /* Check if commands are equal */
            if (commands_equal(dedup, entry_i->command, entry_j->command)) {
                /* Found a duplicate - decide which to keep based on strategy */
                lle_history_entry_t *keep = NULL;
                lle_history_entry_t *discard = NULL;

                switch (dedup->strategy) {
                case LLE_DEDUP_IGNORE:
                case LLE_DEDUP_KEEP_RECENT:
                case LLE_DEDUP_MERGE_METADATA:
                    /* Keep the more recent entry (higher index = more recent)
                     */
                    keep = entry_j;
                    discard = entry_i;
                    break;

                case LLE_DEDUP_KEEP_FREQUENT:
                    /* Keep entry with higher usage count */
                    if (entry_i->usage_count > entry_j->usage_count) {
                        keep = entry_i;
                        discard = entry_j;
                    } else {
                        keep = entry_j;
                        discard = entry_i;
                    }
                    break;

                case LLE_DEDUP_KEEP_ALL:
                    /* Should not reach here - handled above */
                    continue;
                }

                /* Merge forensic metadata if enabled */
                if (dedup->merge_forensics && keep && discard) {
                    merge_forensic_metadata(keep, discard);
                }

                /* Mark the discard entry as deleted */
                if (discard) {
                    discard->state = LLE_HISTORY_STATE_DELETED;
                    removed++;
                    dedup->duplicates_merged++;

                    /* If we discarded entry_i, break inner loop and move to
                     * next i */
                    if (discard == entry_i) {
                        break;
                    }
                }
            }
        }
    }

    if (duplicates_removed) {
        *duplicates_removed = removed;
    }

    return LLE_SUCCESS;
}
