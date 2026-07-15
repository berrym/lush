/**
 * @file history_core.c
 * @brief LLE History System - Core Engine Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 09 - History System
 * Core Structures and Lifecycle
 *
 * Implements the central history management engine with basic entry
 * management, storage, and retrieval functionality.
 */

#include "ht.h"
#include "lle/history.h"
#include "lle/unicode_compare.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* Note: lle_pool_alloc() and lle_pool_free() are provided by
 * memory_management.h They wrap the Lush global pool. The memory_pool
 * parameter in our API is for future per-pool allocation support, but currently
 * uses the global pool.
 */

/* ============================================================================
 * CONFIGURATION MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create default history configuration
 * @param config Output pointer for the created configuration
 * @param memory_pool Memory pool for allocation (currently uses global pool)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_config_create_default(lle_history_config_t **config,
                                               lle_memory_pool_t *memory_pool) {
    (void)memory_pool; /// Unused - we use global pool via lle_pool_alloc

    if (!config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate configuration
    lle_history_config_t *cfg = lle_pool_alloc(sizeof(lle_history_config_t));
    if (!cfg) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }

    /// Set defaults
    memset(cfg, 0, sizeof(lle_history_config_t));
    cfg->max_entries = LLE_HISTORY_DEFAULT_CAPACITY;
    cfg->max_command_length = LLE_HISTORY_MAX_COMMAND_LENGTH;
    cfg->initial_capacity = LLE_HISTORY_INITIAL_CAPACITY;

    /// File settings
    const char *home = getenv("HOME");
    if (home) {
        size_t path_len = strlen(home) + strlen(LLE_HISTORY_DEFAULT_FILE) + 2;
        cfg->history_file_path = lle_pool_alloc(path_len);
        if (cfg->history_file_path) {
            snprintf(cfg->history_file_path, path_len, "%s/%s", home,
                     LLE_HISTORY_DEFAULT_FILE);
        }
    }

    cfg->auto_save = false;    /// disable auto-save for now
    cfg->load_on_init = false; /// disable auto-load for now

    /// Behavior settings
    cfg->ignore_duplicates = false;                     /// deduplication
    cfg->dedup_strategy = LLE_DEDUP_KEEP_RECENT;        /// Default strategy
    cfg->dedup_scope = LLE_HISTORY_DEDUP_SCOPE_SESSION; /// Default scope
    cfg->expire_dups_first = false; /// FIFO trim unless opted in
    cfg->ignore_space_prefix =
        false; /// Off by default (bash/zsh); the lle.hist_ignore_space cell and
               /// the two live populate paths drive it
    cfg->save_timestamps = true;
    cfg->save_working_dir = true;
    cfg->save_exit_codes = true;
    cfg->use_indexing = true; /// hashtable indexing

    *config = cfg;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy history configuration and free resources
 * @param config Configuration to destroy
 * @param memory_pool Memory pool (currently uses global pool)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_config_destroy(lle_history_config_t *config,
                                        lle_memory_pool_t *memory_pool) {
    (void)memory_pool; /// Unused - we use global pool via lle_pool_free

    if (!config) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free history file path if allocated
    if (config->history_file_path) {
        lle_pool_free(config->history_file_path);
    }

    /// Free configuration structure
    lle_pool_free(config);

    return LLE_SUCCESS;
}

/* ============================================================================
 * ENTRY MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create a new history entry from a command string
 * @param entry Output pointer for the created entry
 * @param command Command string to store in the entry
 * @param memory_pool Memory pool for allocation (currently uses global pool)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_entry_create(lle_history_entry_t **entry,
                                      const char *command,
                                      lle_memory_pool_t *memory_pool) {
    (void)memory_pool; /// Unused - we use global pool via lle_pool_alloc

    if (!entry || !command) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Validate command length
    size_t cmd_len = strlen(command);
    if (cmd_len == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    if (cmd_len > LLE_HISTORY_MAX_COMMAND_LENGTH) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Allocate entry
    lle_history_entry_t *e = lle_pool_alloc(sizeof(lle_history_entry_t));
    if (!e) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }

    /// Initialize entry
    memset(e, 0, sizeof(lle_history_entry_t));

    /// Copy command
    e->command = lle_pool_alloc(cmd_len + 1);
    if (!e->command) {
        lle_pool_free(e);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }
    memcpy(e->command, command, cmd_len + 1);
    e->command_length = cmd_len;

    /// Get current timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    e->timestamp = (uint64_t)tv.tv_sec;

    /// Get current working directory
    char cwd_buffer[LLE_HISTORY_MAX_PATH_LENGTH];
    if (lle_history_get_cwd(cwd_buffer, sizeof(cwd_buffer)) == LLE_SUCCESS) {
        size_t cwd_len = strlen(cwd_buffer);
        e->working_directory = lle_pool_alloc(cwd_len + 1);
        if (e->working_directory) {
            memcpy(e->working_directory, cwd_buffer, cwd_len + 1);
        }
    }

    /// Initialize state
    e->state = LLE_HISTORY_STATE_ACTIVE;
    e->exit_code = -1; /// Unknown

    /// Extended fields - initialize to defaults
    e->is_multiline = false;
    e->original_multiline = NULL;
    e->duration_ms = 0;
    e->edit_count = 0;

    /// Forensic fields - initialize to defaults
    e->process_id = 0;
    e->session_id = 0;
    e->user_id = 0;
    e->group_id = 0;
    e->terminal_name = NULL;
    e->start_time_ns = 0;
    e->end_time_ns = 0;
    /// Frecency signal: a freshly recorded command has been used once, now.
    /// The dedup merge accumulates usage_count and refreshes last_access_time
    /// on re-run, so frecency ranking (usage x recency bucket) is meaningful.
    e->usage_count = 1;
    e->last_access_time = e->timestamp;

    /// List pointers
    e->next = NULL;
    e->prev = NULL;

    *entry = e;
    return LLE_SUCCESS;
}

/**
 * @brief Destroy a history entry and free all associated resources
 * @param entry Entry to destroy
 * @param memory_pool Memory pool (currently uses global pool)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_entry_destroy(lle_history_entry_t *entry,
                                       lle_memory_pool_t *memory_pool) {
    (void)memory_pool; /// Unused - we use global pool via lle_pool_free

    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free command
    if (entry->command) {
        lle_pool_free(entry->command);
        entry->command = NULL;
    }

    /// Free working directory
    if (entry->working_directory) {
        lle_pool_free(entry->working_directory);
        entry->working_directory = NULL;
    }

    /// Free multiline data if present
    if (entry->original_multiline) {
        lle_pool_free(entry->original_multiline);
        entry->original_multiline = NULL;
    }

    /// Free forensic data if present
    if (entry->terminal_name) {
        lle_pool_free(entry->terminal_name);
        entry->terminal_name = NULL;
    }

    /// Free entry structure itself last
    lle_pool_free(entry);

    return LLE_SUCCESS;
}

/**
 * @brief Validate a history entry for consistency
 * @param entry Entry to validate
 * @return LLE_SUCCESS if valid, or error code describing the problem
 */
lle_result_t lle_history_validate_entry(const lle_history_entry_t *entry) {
    if (!entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Validate command
    if (!entry->command || entry->command_length == 0) {
        return LLE_FAULT(LLE_ERROR_STATE_CORRUPTION, "history",
                         "history entry has no command");
    }

    /// Validate command length matches
    if (strlen(entry->command) != entry->command_length) {
        return LLE_FAULT(LLE_ERROR_STATE_CORRUPTION, "history",
                         "history entry command length mismatch");
    }

    /// Validate state
    if (entry->state > LLE_HISTORY_STATE_CORRUPTED) {
        return LLE_FAULT(LLE_ERROR_STATE_CORRUPTION, "history",
                         "history entry state invalid");
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * CORE ENGINE LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create and initialize the history core engine
 * @param core Output pointer for the created history core
 * @param memory_pool Memory pool for allocation
 * @param config Configuration settings (NULL for defaults)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_core_create(lle_history_core_t **core,
                                     lle_memory_pool_t *memory_pool,
                                     const lle_history_config_t *config) {
    if (!core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_result_t result = LLE_SUCCESS;

    /// Allocate core structure
    lle_history_core_t *c = lle_pool_alloc(sizeof(lle_history_core_t));
    if (!c) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }
    memset(c, 0, sizeof(lle_history_core_t));

    /// Store memory pool reference
    c->memory_pool = memory_pool;

    /// Create or copy configuration
    if (config) {
        /// Copy provided configuration
        c->config = lle_pool_alloc(sizeof(lle_history_config_t));
        if (!c->config) {
            lle_pool_free(c);
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                             "history core allocation failed");
        }
        memcpy(c->config, config, sizeof(lle_history_config_t));

        /// Deep copy history_file_path if present
        if (config->history_file_path) {
            size_t path_len = strlen(config->history_file_path) + 1;
            c->config->history_file_path = lle_pool_alloc(path_len);
            if (c->config->history_file_path) {
                memcpy(c->config->history_file_path, config->history_file_path,
                       path_len);
            }
        }
    } else {
        /// Create default configuration
        result = lle_history_config_create_default(&c->config, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_pool_free(c);
            return result;
        }
    }

    /// Allocate initial entry array
    size_t initial_cap = c->config->initial_capacity;
    c->entries = lle_pool_alloc(sizeof(lle_history_entry_t *) * initial_cap);
    if (!c->entries) {
        lle_history_config_destroy(c->config, memory_pool);
        lle_pool_free(c);
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }
    memset(c->entries, 0, sizeof(lle_history_entry_t *) * initial_cap);

    c->entry_capacity = initial_cap;
    c->entry_count = 0;
    c->next_entry_id = 1; /// Start IDs at 1

    /// Initialize linked list pointers
    c->first_entry = NULL;
    c->last_entry = NULL;

    /// Create hashtable index if enabled
    if (c->config->use_indexing) {
        result = lle_history_index_create(&c->entry_lookup, initial_cap);
        if (result != LLE_SUCCESS) {
            lle_pool_free(c->entries);
            lle_history_config_destroy(c->config, memory_pool);
            lle_pool_free(c);
            return result;
        }
    } else {
        c->entry_lookup = NULL;
    }

    /// Create deduplication engine if configured
    if (c->config->ignore_duplicates) {
        /// Use configured strategy and scope (defaults: KEEP_RECENT, SESSION)
        result = lle_history_dedup_create(&c->dedup_engine, c,
                                          c->config->dedup_strategy,
                                          c->config->dedup_scope);
        if (result != LLE_SUCCESS) {
            if (c->entry_lookup) {
                lle_history_index_destroy(c->entry_lookup);
            }
            lle_pool_free(c->entries);
            lle_history_config_destroy(c->config, memory_pool);
            lle_pool_free(c);
            return result;
        }
        /// Configure Unicode normalization for dedup comparison
        lle_history_dedup_set_unicode_normalize(c->dedup_engine,
                                                c->config->unicode_normalize);
    } else {
        c->dedup_engine = NULL;
    }

    /// Initialize statistics
    memset(&c->stats, 0, sizeof(lle_history_stats_t));

    /// Initialize thread safety
    if (pthread_rwlock_init(&c->lock, NULL) != 0) {
        lle_pool_free(c->entries);
        lle_history_config_destroy(c->config, memory_pool);
        lle_pool_free(c);
        return LLE_FAULT(LLE_ERROR_INITIALIZATION_FAILED, "history",
                         "history core lock init failed");
    }

    c->initialized = true;

    *core = c;
    return LLE_SUCCESS;
}

/**
 * @brief Enable or disable duplicate-command suppression at runtime.
 *
 * Enabling lazily creates the dedup engine the same way the creation path does
 * (configured strategy/scope, Unicode normalization); disabling destroys it so
 * the add path stops consulting it. Keys on `enable` alone -- a scope==NONE
 * engine is a harmless no-op (the check returns not-found), so the extra
 * scope condition the creation gate applies is unnecessary here. Idempotent --
 * a no-op when already in the requested state. Backs `setopt hist_ignore_dups`.
 *
 * @param core   History core (may be NULL -> no-op success)
 * @param enable Desired duplicate-suppression state
 * @return LLE_SUCCESS, or the dedup-create error (flag left off on failure)
 */
lle_result_t lle_history_set_ignore_duplicates(lle_history_core_t *core,
                                               bool enable) {
    if (!core || !core->config) {
        return LLE_SUCCESS;
    }
    pthread_rwlock_wrlock(&core->lock);
    lle_result_t result = LLE_SUCCESS;
    if (core->config->ignore_duplicates != enable) {
        core->config->ignore_duplicates = enable;
        if (enable && !core->dedup_engine) {
            result = lle_history_dedup_create(&core->dedup_engine, core,
                                              core->config->dedup_strategy,
                                              core->config->dedup_scope);
            if (result == LLE_SUCCESS) {
                lle_history_dedup_set_unicode_normalize(
                    core->dedup_engine, core->config->unicode_normalize);
            } else {
                core->config->ignore_duplicates = false;
                core->dedup_engine = NULL;
            }
        } else if (!enable && core->dedup_engine) {
            lle_history_dedup_destroy(core->dedup_engine);
            core->dedup_engine = NULL;
        }
    }
    pthread_rwlock_unlock(&core->lock);
    return result;
}

/// Apply a live change to the "ignore space-prefixed commands" setting. The add
/// path consults core->config->ignore_space_prefix on every insert, so this
/// takes effect immediately -- the target of the lle.hist_ignore_space
/// subscriber that `setopt hist_ignore_space` writes.
lle_result_t lle_history_set_ignore_space_prefix(lle_history_core_t *core,
                                                 bool enable) {
    if (!core || !core->config) {
        return LLE_SUCCESS;
    }
    pthread_rwlock_wrlock(&core->lock);
    core->config->ignore_space_prefix = enable;
    pthread_rwlock_unlock(&core->lock);
    return LLE_SUCCESS;
}

/**
 * @brief Destroy history core and free all resources
 * @param core History core to destroy
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_core_destroy(lle_history_core_t *core) {
    if (!core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Acquire write lock
    pthread_rwlock_wrlock(&core->lock);

    /// Mark as not initialized
    core->initialized = false;

    /// Free all entries
    for (size_t i = 0; i < core->entry_count; i++) {
        if (core->entries[i]) {
            lle_history_entry_destroy(core->entries[i], core->memory_pool);
            core->entries[i] = NULL;
        }
    }

    /// Free entries array
    if (core->entries) {
        lle_pool_free(core->entries);
    }

    /// Destroy hashtable index if present
    if (core->entry_lookup) {
        lle_history_index_destroy(core->entry_lookup);
        core->entry_lookup = NULL;
    }

    /// Destroy deduplication engine if present
    if (core->dedup_engine) {
        lle_history_dedup_destroy(core->dedup_engine);
        core->dedup_engine = NULL;
    }

    /// Destroy configuration
    if (core->config) {
        lle_history_config_destroy(core->config, core->memory_pool);
    }

    /// Release lock and destroy
    pthread_rwlock_unlock(&core->lock);
    pthread_rwlock_destroy(&core->lock);

    /// Free core structure
    lle_pool_free(core);

    return LLE_SUCCESS;
}

/* ============================================================================
 * ENTRY OPERATIONS
 * ============================================================================
 */

/**
 * @brief Expand the entry array capacity when full
 * @param core History core whose capacity to expand
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_expand_capacity(lle_history_core_t *core) {
    if (!core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Calculate new capacity (double it, or use initial minimum if currently
    /// 0)
    size_t new_capacity;
    if (core->entry_capacity == 0) {
        /// Handle case where initial_capacity was set to 0 - use a reasonable
        /// minimum
        new_capacity = 100;
    } else {
        new_capacity = core->entry_capacity * 2;
    }

    /// Check maximum capacity
    if (new_capacity > core->config->max_entries) {
        new_capacity = core->config->max_entries;
    }

    /// Check if already at max
    if (core->entry_capacity >= core->config->max_entries) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /// Allocate new array
    lle_history_entry_t **new_entries =
        lle_pool_alloc(sizeof(lle_history_entry_t *) * new_capacity);
    if (!new_entries) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "history",
                         "history core allocation failed");
    }

    /// Copy existing entries
    memcpy(new_entries, core->entries,
           sizeof(lle_history_entry_t *) * core->entry_count);

    /// Zero new space
    memset(new_entries + core->entry_count, 0,
           sizeof(lle_history_entry_t *) * (new_capacity - core->entry_count));

    /// Free old array
    lle_pool_free(core->entries);

    /// Update core
    core->entries = new_entries;
    core->entry_capacity = new_capacity;

    return LLE_SUCCESS;
}

/**
 * @brief Physically remove entries[idx] and free it.
 *
 * Keeps the entry array (compacted oldest -> newest), the doubly-linked
 * list, the id -> entry index, and the statistics consistent. The caller
 * must hold the write lock and guarantee 0 <= idx < entry_count.
 */
static void lle_history_evict_entry_at(lle_history_core_t *core, size_t idx) {
    lle_history_entry_t *victim = core->entries[idx];
    if (!victim) {
        return;
    }

    /// Unlink from the doubly-linked list.
    if (victim->prev) {
        victim->prev->next = victim->next;
    } else {
        core->first_entry = victim->next;
    }
    if (victim->next) {
        victim->next->prev = victim->prev;
    } else {
        core->last_entry = victim->prev;
    }

    /// Drop from the id -> entry index.
    if (core->entry_lookup) {
        lle_history_index_remove(core->entry_lookup, victim->entry_id);
    }

    /// Compact the array so entries stay contiguous.
    if (idx + 1 < core->entry_count) {
        memmove(&core->entries[idx], &core->entries[idx + 1],
                (core->entry_count - idx - 1) * sizeof(lle_history_entry_t *));
    }
    core->entry_count--;
    core->entries[core->entry_count] = NULL;

    /// Statistics.
    if (victim->state == LLE_HISTORY_STATE_ACTIVE) {
        if (core->stats.active_entries > 0) {
            core->stats.active_entries--;
        }
    } else if (victim->state == LLE_HISTORY_STATE_DELETED) {
        if (core->stats.deleted_entries > 0) {
            core->stats.deleted_entries--;
        }
    }

    lle_history_entry_destroy(victim, core->memory_pool);
}

/**
 * @brief Index of the oldest live entry whose command is duplicated by
 *        another live entry, or 0 (oldest) when there is no duplicate.
 *
 * One O(n) pass records each NFC-normalized command in a "seen" set and,
 * on the second sighting, in a "duplicated" set; a second pass returns the
 * first (oldest) entry whose command is duplicated. Falls back to the
 * oldest entry (FIFO) on any allocation failure. NFC normalization matches
 * the dedup engine's default notion of equality.
 */
static size_t lle_history_oldest_duplicate(lle_history_core_t *core) {
    size_t victim = 0;

    ht_strset_t *seen = ht_strset_create(NULL);
    ht_strset_t *duplicated = ht_strset_create(NULL);
    char **norm = calloc(core->entry_count, sizeof(char *));
    if (!seen || !duplicated || !norm) {
        free(norm);
        ht_strset_destroy(seen);
        ht_strset_destroy(duplicated);
        return victim;
    }

    for (size_t i = 0; i < core->entry_count; i++) {
        if (!core->entries[i]->command) {
            continue;
        }
        norm[i] = lle_unicode_normalize_nfc_alloc(core->entries[i]->command);
        if (!norm[i]) {
            continue;
        }
        /// ht_strset_add returns false when the key is already present, i.e.
        /// this is a second (or later) sighting of the command.
        if (!ht_strset_add(seen, norm[i])) {
            ht_strset_add(duplicated, norm[i]);
        }
    }

    for (size_t i = 0; i < core->entry_count; i++) {
        if (norm[i] && ht_strset_contains(duplicated, norm[i])) {
            victim = i;
            break;
        }
    }

    for (size_t i = 0; i < core->entry_count; i++) {
        free(norm[i]);
    }
    free(norm);
    ht_strset_destroy(seen);
    ht_strset_destroy(duplicated);
    return victim;
}

/**
 * @brief Evict one entry to reclaim a slot when the history is at its cap.
 *
 * A soft-deleted tombstone (dedup's superseded duplicate, still occupying a
 * slot) is reclaimed first, so making room never loses live history when
 * deduplication is active. Otherwise the default policy drops the oldest
 * entry (FIFO), keeping the most recent commands -- matching bash and zsh,
 * which discard old history rather than the incoming command. With
 * hist_expire_dups_first (config->expire_dups_first) the oldest live entry
 * that is duplicated by another live entry is dropped before any unique
 * entry, via a single O(n) normalized-command pass. The caller must hold
 * the write lock.
 */
void lle_history_trim_one(lle_history_core_t *core) {
    if (!core || core->entry_count == 0) {
        return;
    }

    /// Reclaim a soft-deleted tombstone first -- frees a slot with no live
    /// loss, and is the dups-first outcome while dedup is active.
    for (size_t i = 0; i < core->entry_count; i++) {
        if (core->entries[i]->state == LLE_HISTORY_STATE_DELETED) {
            lle_history_evict_entry_at(core, i);
            return;
        }
    }

    size_t victim = 0; /// oldest live entry by default (FIFO)

    if (core->config && core->config->expire_dups_first &&
        core->entry_count > 1) {
        victim = lle_history_oldest_duplicate(core);
    }

    lle_history_evict_entry_at(core, victim);
}

/**
 * @brief Add a new command entry to history
 * @param core History core engine
 * @param command Command string to add
 * @param exit_code Exit code of the command (-1 if unknown)
 * @param entry_id Output pointer for the assigned entry ID (may be NULL)
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_add_entry(lle_history_core_t *core,
                                   const char *command, int exit_code,
                                   uint64_t *entry_id) {
    if (!core || !command) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire write lock
    pthread_rwlock_wrlock(&core->lock);

    /// Skip commands that begin with a space when configured. The field is
    /// runtime-mutable via lle_history_set_ignore_space_prefix (which writes it
    /// under this same lock), so the read is held under the lock too.
    if (core->config->ignore_space_prefix && command[0] == ' ') {
        pthread_rwlock_unlock(&core->lock);
        return LLE_SUCCESS; /// Silently ignore
    }

    lle_result_t result = LLE_SUCCESS;

    /// Start performance measurement
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    /// Create entry
    lle_history_entry_t *entry = NULL;
    result = lle_history_entry_create(&entry, command, core->memory_pool);
    if (result != LLE_SUCCESS) {
        pthread_rwlock_unlock(&core->lock);
        return result;
    }

    /// Assign entry ID
    entry->entry_id = core->next_entry_id++;
    entry->exit_code = exit_code;

    /// Capture forensic context
    lle_forensic_context_t forensic_ctx;
    if (lle_forensic_capture_context(&forensic_ctx) == LLE_SUCCESS) {
        lle_forensic_apply_to_entry(entry, &forensic_ctx);
        lle_forensic_free_context(&forensic_ctx);
    }

    /// Check for duplicates if dedup engine is enabled
    if (core->dedup_engine) {
        bool entry_rejected = false;
        result =
            lle_history_dedup_apply(core->dedup_engine, entry, &entry_rejected);

        if (result != LLE_SUCCESS) {
            lle_history_entry_destroy(entry, core->memory_pool);
            pthread_rwlock_unlock(&core->lock);
            return result;
        }

        if (entry_rejected) {
            /// Duplicate was rejected - clean up and return success
            lle_history_entry_destroy(entry, core->memory_pool);
            pthread_rwlock_unlock(&core->lock);

            /// Optionally return the ID of the existing entry if requested
            if (entry_id) {
                *entry_id = 0; /// Indicate entry was not added
            }

            return LLE_SUCCESS;
        }
    }

    /// Make room for the accepted entry -- only now, after dedup could have
    /// rejected a duplicate, so a rejected or failed add never costs an
    /// existing entry. At the max_entries cap bash and zsh keep the newest
    /// command and discard old history; lle_history_trim_one reclaims a
    /// soft-deleted tombstone first, then (with hist_expire_dups_first) an
    /// old duplicate, else the oldest entry.
    if (core->entry_count >= core->entry_capacity) {
        result = lle_history_expand_capacity(core);
        if (result == LLE_ERROR_BUFFER_OVERFLOW) {
            lle_history_trim_one(core);
            result = LLE_SUCCESS;
        }
        if (result != LLE_SUCCESS) {
            lle_history_entry_destroy(entry, core->memory_pool);
            pthread_rwlock_unlock(&core->lock);
            return result;
        }
    }

    /// Add to array
    core->entries[core->entry_count] = entry;

    /// Update linked list
    if (core->last_entry) {
        core->last_entry->next = entry;
        entry->prev = core->last_entry;
    } else {
        core->first_entry = entry;
    }
    core->last_entry = entry;

    core->entry_count++;

    /// Add to hashtable index if enabled
    if (core->entry_lookup) {
        result = lle_history_index_insert(core->entry_lookup, entry->entry_id,
                                          entry);
        if (result != LLE_SUCCESS) {
            /// Rollback: remove from array and linked list
            core->entry_count--;
            core->entries[core->entry_count] = NULL;
            if (entry->prev) {
                entry->prev->next = NULL;
                core->last_entry = entry->prev;
            } else {
                core->first_entry = NULL;
                core->last_entry = NULL;
            }
            lle_history_entry_destroy(entry, core->memory_pool);
            pthread_rwlock_unlock(&core->lock);
            return result;
        }
    }

    /// Update statistics
    core->stats.total_entries++;
    core->stats.active_entries++;
    core->stats.add_count++;

    /// End performance measurement
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    uint64_t elapsed_us = (end_time.tv_sec - start_time.tv_sec) * 1000000 +
                          (end_time.tv_usec - start_time.tv_usec);
    core->stats.total_add_time_us += elapsed_us;

    /// Return entry ID if requested
    if (entry_id) {
        *entry_id = entry->entry_id;
    }

    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Internal lock-free version of get_entry_by_index
 *
 * CRITICAL: Caller MUST hold at least a read lock on core->lock.
 * Used by dedup engine to avoid deadlock when called from add_entry.
 *
 * @param core History core engine
 * @param index Index of entry to retrieve
 * @param entry Output pointer for the entry
 * @return LLE_SUCCESS on success, or error code on failure
 */
LLE_MAYBE_UNUSED
static inline lle_result_t
get_entry_by_index_unlocked(lle_history_core_t *core, size_t index,
                            lle_history_entry_t **entry) {
    if (!core || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Check bounds
    if (index >= core->entry_count) {
        return LLE_ERROR_NOT_FOUND;
    }

    /// Return entry directly - no locking
    *entry = core->entries[index];

    /// Update statistics
    core->stats.retrieve_count++;

    return LLE_SUCCESS;
}

/**
 * @brief Get a history entry by its array index
 * @param core History core engine
 * @param index Array index of the entry (0 = oldest)
 * @param entry Output pointer for the entry
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if out of bounds
 */
lle_result_t lle_history_get_entry_by_index(lle_history_core_t *core,
                                            size_t index,
                                            lle_history_entry_t **entry) {
    if (!core || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire read lock
    pthread_rwlock_rdlock(&core->lock);

    /// Check bounds
    if (index >= core->entry_count) {
        pthread_rwlock_unlock(&core->lock);
        return LLE_ERROR_NOT_FOUND;
    }

    /// Start performance measurement
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    /// Return entry
    *entry = core->entries[index];

    /// Update statistics
    core->stats.retrieve_count++;

    /// End performance measurement
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    uint64_t elapsed_us = (end_time.tv_sec - start_time.tv_sec) * 1000000 +
                          (end_time.tv_usec - start_time.tv_usec);
    core->stats.total_retrieve_time_us += elapsed_us;

    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Get a history entry by its unique ID
 *
 * Uses hashtable lookup if indexing is enabled, otherwise linear search.
 *
 * @param core History core engine
 * @param entry_id Unique entry ID to look up
 * @param entry Output pointer for the entry
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_FOUND if not found
 */
lle_result_t lle_history_get_entry_by_id(lle_history_core_t *core,
                                         uint64_t entry_id,
                                         lle_history_entry_t **entry) {
    if (!core || !entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire read lock
    pthread_rwlock_rdlock(&core->lock);

    /// Start performance measurement
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    /// Use hashtable lookup if available, otherwise linear
    /// search
    lle_history_entry_t *found = NULL;
    lle_result_t lookup_result;

    if (core->entry_lookup) {
        /// O(1) hashtable lookup
        lookup_result =
            lle_history_index_lookup(core->entry_lookup, entry_id, &found);
        (void)lookup_result; /* Lookup returns success even if not found (found
                                will be NULL) */
    } else {
        /// O(n) linear search fallback
        for (size_t i = 0; i < core->entry_count; i++) {
            if (core->entries[i]->entry_id == entry_id) {
                found = core->entries[i];
                break;
            }
        }
    }

    /// Update statistics
    core->stats.retrieve_count++;

    /// End performance measurement
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    uint64_t elapsed_us = (end_time.tv_sec - start_time.tv_sec) * 1000000 +
                          (end_time.tv_usec - start_time.tv_usec);
    core->stats.total_retrieve_time_us += elapsed_us;

    pthread_rwlock_unlock(&core->lock);

    if (!found) {
        return LLE_ERROR_NOT_FOUND;
    }

    *entry = found;
    return LLE_SUCCESS;
}

/**
 * @brief Internal lock-free version of get_entry_count
 *
 * CRITICAL: Caller MUST hold at least a read lock on core->lock.
 *
 * @param core History core engine
 * @param count Output pointer for the entry count
 * @return LLE_SUCCESS on success, or error code on failure
 */
LLE_MAYBE_UNUSED
static inline lle_result_t get_entry_count_unlocked(lle_history_core_t *core,
                                                    size_t *count) {
    if (!core || !count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    *count = core->entry_count;
    return LLE_SUCCESS;
}

/**
 * @brief Get the number of entries in history
 * @param core History core engine
 * @param count Output pointer for the entry count
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_get_entry_count(lle_history_core_t *core,
                                         size_t *count) {
    if (!core || !count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire read lock
    pthread_rwlock_rdlock(&core->lock);
    *count = core->entry_count;
    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Clear all entries from history
 * @param core History core engine
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_clear(lle_history_core_t *core) {
    if (!core) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire write lock
    pthread_rwlock_wrlock(&core->lock);

    /// Destroy all entries
    for (size_t i = 0; i < core->entry_count; i++) {
        if (core->entries[i]) {
            lle_history_entry_destroy(core->entries[i], core->memory_pool);
            core->entries[i] = NULL;
        }
    }

    /// Reset counts
    core->entry_count = 0;
    core->first_entry = NULL;
    core->last_entry = NULL;

    /// Clear hashtable index if present
    if (core->entry_lookup) {
        lle_history_index_clear(core->entry_lookup);
    }

    /// Update statistics
    core->stats.active_entries = 0;

    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/**
 * @brief Get history statistics
 * @param core History core engine
 * @param stats Output pointer for the statistics structure
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_get_stats(lle_history_core_t *core,
                                   const lle_history_stats_t **stats) {
    if (!core || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!core->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// Acquire read lock
    pthread_rwlock_rdlock(&core->lock);
    *stats = &core->stats;
    pthread_rwlock_unlock(&core->lock);

    return LLE_SUCCESS;
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get the current working directory
 * @param buffer Output buffer for the path
 * @param size Size of the buffer
 * @return LLE_SUCCESS on success, or error code on failure
 */
lle_result_t lle_history_get_cwd(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (getcwd(buffer, size) == NULL) {
        return LLE_FAULT(LLE_ERROR_ASSERTION_FAILED, "history",
                         "history working directory unavailable");
    }

    return LLE_SUCCESS;
}
