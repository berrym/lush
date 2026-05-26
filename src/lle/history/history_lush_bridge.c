/**
 * @file history_lush_bridge.c
 * @brief LLE History System - Lush Integration Bridge
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 09 - History System (Phase 2 Day 5)
 * Version: 1.0.0
 * Status: Phase 2 Day 5 - Lush Integration Bridge
 *
 * Provides bidirectional synchronization between:
 * - LLE history core engine
 * - GNU Readline history API
 * - POSIX history manager
 * - Lush history builtin
 *
 * This bridge ensures seamless integration with existing Lush history
 * systems while maintaining backward compatibility and zero data loss.
 */

#include "lle/error_handling.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include "posix_history.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/// GNU Readline headers - conditionally included
/// HAVE_READLINE is defined by meson build system (-DHAVE_READLINE=0/1)
#if HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

/* ============================================================================
 * BRIDGE STATE AND CONFIGURATION
 * ============================================================================
 */

/**
 * Bridge state structure
 */
typedef struct lle_history_bridge {
    lle_history_core_t *lle_core;           ///< LLE history core
    posix_history_manager_t *posix_manager; ///< POSIX history manager
    lle_memory_pool_t *memory_pool;         ///< Memory pool

    /// Synchronization state
    bool readline_sync_enabled;     ///< Sync with GNU Readline
    bool posix_sync_enabled;        ///< Sync with POSIX manager
    uint64_t last_readline_sync_id; ///< Last synced readline entry
    uint64_t last_posix_sync_id;    ///< Last synced POSIX entry

    /// Configuration
    bool auto_sync;          ///< Automatic sync on add
    bool bidirectional_sync; ///< Bidirectional sync
    bool import_on_init;     ///< Import existing history

    /// Statistics
    size_t readline_imports; ///< Readline imports count
    size_t readline_exports; ///< Readline exports count
    size_t posix_imports;    ///< POSIX imports count
    size_t posix_exports;    ///< POSIX exports count
    size_t sync_errors;      ///< Synchronization errors

    bool initialized; ///< Initialization flag
} lle_history_bridge_t;

/// Global bridge instance
static lle_history_bridge_t *g_bridge = NULL;

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * @brief Allocate bridge structure
 *
 * @param bridge Output pointer for the allocated bridge
 * @param memory_pool Memory pool for allocation
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if parameters are
 * invalid, or LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
static lle_result_t bridge_alloc(lle_history_bridge_t **bridge,
                                 lle_memory_pool_t *memory_pool) {
    if (!bridge || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *bridge =
        (lle_history_bridge_t *)lle_pool_alloc(sizeof(lle_history_bridge_t));
    if (!*bridge) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(*bridge, 0, sizeof(lle_history_bridge_t));
    (*bridge)->memory_pool = memory_pool;

    return LLE_SUCCESS;
}

/**
 * @brief Free bridge structure
 *
 * @param bridge Bridge structure to free (may be NULL)
 */
static void bridge_free(lle_history_bridge_t *bridge) {
    if (bridge) {
        lle_pool_free(bridge);
    }
}

/**
 * @brief Check if command should be ignored (whitespace, empty, etc.)
 *
 * @param command Command string to check
 * @return true if command should be ignored, false otherwise
 */
static bool should_ignore_command(const char *command) {
    if (!command || !*command) {
        return true;
    }

    /// Check for whitespace-only
    const char *p = command;
    while (*p) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            return false;
        }
        p++;
    }

    return true;
}

/* ============================================================================
 * BRIDGE LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Initialize Lush history bridge
 *
 * Creates and initializes the bridge between LLE history and other history
 * systems. Optionally imports existing history from Readline and POSIX
 * managers.
 *
 * @param lle_core LLE history core engine
 * @param posix_manager POSIX history manager (may be NULL)
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS on success, LLE_ERROR_INVALID_PARAMETER if required
 * parameters are invalid, LLE_ERROR_ALREADY_INITIALIZED if already initialized,
 * or other error codes
 */
lle_result_t lle_history_bridge_init(lle_history_core_t *lle_core,
                                     posix_history_manager_t *posix_manager,
                                     lle_memory_pool_t *memory_pool) {
    if (!lle_core || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Check if already initialized
    if (g_bridge && g_bridge->initialized) {
        return LLE_ERROR_ALREADY_INITIALIZED;
    }

    /// Allocate bridge
    lle_result_t result = bridge_alloc(&g_bridge, memory_pool);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Initialize bridge state
    g_bridge->lle_core = lle_core;
    g_bridge->posix_manager = posix_manager;
    g_bridge->memory_pool = memory_pool;

    /// Default configuration
    g_bridge->readline_sync_enabled = true;
    g_bridge->posix_sync_enabled = (posix_manager != NULL);
    g_bridge->auto_sync = true;
    g_bridge->bidirectional_sync = true;
    g_bridge->import_on_init = true;

    /// Initialize sync IDs
    g_bridge->last_readline_sync_id = 0;
    g_bridge->last_posix_sync_id = 0;

    /// Reset statistics
    g_bridge->readline_imports = 0;
    g_bridge->readline_exports = 0;
    g_bridge->posix_imports = 0;
    g_bridge->posix_exports = 0;
    g_bridge->sync_errors = 0;

    g_bridge->initialized = true;

    /// Import existing history if configured
    if (g_bridge->import_on_init) {
        /// Import from GNU Readline
        if (g_bridge->readline_sync_enabled) {
            lle_history_bridge_import_from_readline();
        }

        /// Import from POSIX
        if (g_bridge->posix_sync_enabled && posix_manager) {
            lle_history_bridge_import_from_posix();
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Shutdown and cleanup bridge
 *
 * Performs final synchronization and frees all bridge resources.
 *
 * @return LLE_SUCCESS on success
 */
lle_result_t lle_history_bridge_shutdown(void) {
    if (!g_bridge) {
        return LLE_SUCCESS;
    }

    /// Final sync before shutdown
    if (g_bridge->initialized) {
        if (g_bridge->readline_sync_enabled) {
            lle_history_bridge_export_to_readline();
        }

        if (g_bridge->posix_sync_enabled && g_bridge->posix_manager) {
            lle_history_bridge_export_to_posix();
        }
    }

    /// Free bridge
    bridge_free(g_bridge);
    g_bridge = NULL;

    return LLE_SUCCESS;
}

/**
 * @brief Check if bridge is initialized
 *
 * @return true if bridge is initialized and ready, false otherwise
 */
bool lle_history_bridge_is_initialized(void) {
    return (g_bridge && g_bridge->initialized);
}

/* ============================================================================
 * GNU READLINE SYNCHRONIZATION
 * ============================================================================
 */

/**
 * @brief Import history from GNU Readline
 *
 * Imports all entries from the GNU Readline history list into LLE history.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_import_from_readline(void) {
#if HAVE_READLINE
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->readline_sync_enabled) {
        return LLE_SUCCESS; /// Not an error, just disabled
    }

    /// Get readline history list
    HIST_ENTRY **hist_list = history_list();
    if (!hist_list) {
        return LLE_SUCCESS; /// No history to import
    }

    /// Import each entry
    size_t imported = 0;
    for (int i = 0; hist_list[i] != NULL; i++) {
        const char *command = hist_list[i]->line;

        if (should_ignore_command(command)) {
            continue;
        }

        /// Add to LLE core
        uint64_t entry_id = 0;
        lle_result_t result = lle_history_add_entry(g_bridge->lle_core, command,
                                                    -1, /// Unknown exit code
                                                    &entry_id);

        if (result == LLE_SUCCESS) {
            imported++;
            g_bridge->last_readline_sync_id = entry_id;
        } else {
            g_bridge->sync_errors++;
        }
    }

    g_bridge->readline_imports += imported;

    return LLE_SUCCESS;
#else
    /// Readline not available
    (void)g_bridge;
    return LLE_SUCCESS;
#endif
}

/**
 * @brief Export history to GNU Readline
 *
 * Exports all LLE history entries to the GNU Readline history list.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_export_to_readline(void) {
#if HAVE_READLINE
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->readline_sync_enabled) {
        return LLE_SUCCESS;
    }

    /// Get entry count from LLE core
    size_t entry_count = 0;
    lle_result_t result =
        lle_history_get_entry_count(g_bridge->lle_core, &entry_count);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Export each entry to readline
    size_t exported = 0;
    for (size_t i = 0; i < entry_count; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(g_bridge->lle_core, i, &entry);

        if (result != LLE_SUCCESS || !entry) {
            continue;
        }

        /// Check if already in readline (avoid duplicates)
        HIST_ENTRY *last = history_get(history_length);
        if (last && strcmp(last->line, entry->command) == 0) {
            continue;
        }

        /// Add to readline history
        add_history(entry->command);
        exported++;
    }

    g_bridge->readline_exports += exported;

    return LLE_SUCCESS;
#else
    /// Readline not available
    return LLE_SUCCESS;
#endif
}

/**
 * @brief Sync single entry to readline
 *
 * Adds a single history entry to the GNU Readline history list.
 *
 * @param entry History entry to sync
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t
lle_history_bridge_sync_entry_to_readline(const lle_history_entry_t *entry) {
#if HAVE_READLINE
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->readline_sync_enabled) {
        return LLE_SUCCESS;
    }

    if (!entry || should_ignore_command(entry->command)) {
        return LLE_SUCCESS;
    }

    /// Add to readline
    add_history(entry->command);
    g_bridge->readline_exports++;

    return LLE_SUCCESS;
#else
    (void)entry;
    return LLE_SUCCESS;
#endif
}

/**
 * @brief Clear readline history
 *
 * Clears all entries from the GNU Readline history list.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_clear_readline(void) {
#if HAVE_READLINE
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->readline_sync_enabled) {
        return LLE_SUCCESS;
    }

    clear_history();

    return LLE_SUCCESS;
#else
    return LLE_SUCCESS;
#endif
}

/* ============================================================================
 * POSIX HISTORY SYNCHRONIZATION
 * ============================================================================
 */

/**
 * @brief Import history from POSIX manager
 *
 * Imports all entries from the POSIX history manager into LLE history.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_import_from_posix(void) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->posix_sync_enabled || !g_bridge->posix_manager) {
        return LLE_SUCCESS;
    }

    posix_history_manager_t *mgr = g_bridge->posix_manager;

    /// Import each entry from POSIX manager
    size_t imported = 0;
    for (size_t i = 0; i < mgr->count; i++) {
        const char *command = mgr->entries[i].command;

        if (should_ignore_command(command)) {
            continue;
        }

        /// Add to LLE core
        uint64_t entry_id = 0;
        lle_result_t result = lle_history_add_entry(g_bridge->lle_core, command,
                                                    -1, /// Unknown exit code
                                                    &entry_id);

        if (result == LLE_SUCCESS) {
            imported++;
            g_bridge->last_posix_sync_id = entry_id;
        } else {
            g_bridge->sync_errors++;
        }
    }

    g_bridge->posix_imports += imported;

    return LLE_SUCCESS;
}

/**
 * @brief Export history to POSIX manager
 *
 * Exports all LLE history entries to the POSIX history manager.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_export_to_posix(void) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->posix_sync_enabled || !g_bridge->posix_manager) {
        return LLE_SUCCESS;
    }

    /// Get entry count from LLE core
    size_t entry_count = 0;
    lle_result_t result =
        lle_history_get_entry_count(g_bridge->lle_core, &entry_count);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Export each entry to POSIX manager
    size_t exported = 0;
    for (size_t i = 0; i < entry_count; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(g_bridge->lle_core, i, &entry);

        if (result != LLE_SUCCESS || !entry) {
            continue;
        }

        /// Add to POSIX manager (posix_history_add handles duplicates)
        int posix_number =
            posix_history_add(g_bridge->posix_manager, entry->command);
        if (posix_number > 0) {
            exported++;
        }
    }

    g_bridge->posix_exports += exported;

    return LLE_SUCCESS;
}

/**
 * @brief Sync single entry to POSIX manager
 *
 * Adds a single history entry to the POSIX history manager.
 *
 * @param entry History entry to sync
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t
lle_history_bridge_sync_entry_to_posix(const lle_history_entry_t *entry) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (!g_bridge->posix_sync_enabled || !g_bridge->posix_manager) {
        return LLE_SUCCESS;
    }

    if (!entry || should_ignore_command(entry->command)) {
        return LLE_SUCCESS;
    }

    /// Add to POSIX manager
    posix_history_add(g_bridge->posix_manager, entry->command);
    g_bridge->posix_exports++;

    return LLE_SUCCESS;
}

/* ============================================================================
 * BIDIRECTIONAL SYNCHRONIZATION
 * ============================================================================
 */

/**
 * @brief Add entry to LLE and sync to all systems
 *
 * Adds a command to LLE history and automatically syncs to Readline and POSIX
 * if auto-sync is enabled.
 *
 * @param command Command string to add
 * @param exit_code Exit code of the command
 * @param entry_id Output pointer for the assigned entry ID (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_add_entry(const char *command, int exit_code,
                                          uint64_t *entry_id) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (should_ignore_command(command)) {
        return LLE_SUCCESS;
    }

    /// Add to LLE core
    uint64_t id = 0;
    lle_result_t result =
        lle_history_add_entry(g_bridge->lle_core, command, exit_code, &id);

    if (result != LLE_SUCCESS) {
        return result;
    }

    if (entry_id) {
        *entry_id = id;
    }

    /// Auto-sync if enabled
    if (g_bridge->auto_sync) {
        /// Get the entry we just added
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_id(g_bridge->lle_core, id, &entry);

        if (result == LLE_SUCCESS && entry) {
            /// Sync to readline
            if (g_bridge->readline_sync_enabled) {
                lle_history_bridge_sync_entry_to_readline(entry);
            }

            /// Sync to POSIX
            if (g_bridge->posix_sync_enabled && g_bridge->posix_manager) {
                lle_history_bridge_sync_entry_to_posix(entry);
            }
        }
    }

    return LLE_SUCCESS;
}

/**
 * @brief Synchronize all systems (full bidirectional sync)
 *
 * Exports LLE history to all connected systems (Readline, POSIX).
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_sync_all(void) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    lle_result_t result;

    /// Export LLE to other systems
    if (g_bridge->readline_sync_enabled) {
        result = lle_history_bridge_export_to_readline();
        if (result != LLE_SUCCESS) {
            g_bridge->sync_errors++;
        }
    }

    if (g_bridge->posix_sync_enabled && g_bridge->posix_manager) {
        result = lle_history_bridge_export_to_posix();
        if (result != LLE_SUCCESS) {
            g_bridge->sync_errors++;
        }
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * HISTORY BUILTIN COMPATIBILITY
 * ============================================================================
 */

/**
 * @brief Handle history builtin command
 *
 * Provides compatibility with existing 'history' command behavior
 * while using LLE as the backend.
 *
 * @param argc Argument count (currently unused)
 * @param argv Argument values (currently unused)
 * @param output Output pointer for formatted history text (caller must free if
 * non-NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized, or LLE_ERROR_OUT_OF_MEMORY on allocation failure
 */
lle_result_t lle_history_bridge_handle_builtin(int argc, char **argv,
                                               char **output) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    (void)argc;
    (void)argv;

    /// Get entry count
    size_t entry_count = 0;
    lle_result_t result =
        lle_history_get_entry_count(g_bridge->lle_core, &entry_count);
    if (result != LLE_SUCCESS) {
        return result;
    }

    /// Allocate output buffer (estimate 100 bytes per entry)
    size_t buffer_size = entry_count * 100 + 1024;
    char *buffer = (char *)lle_pool_alloc(buffer_size);
    if (!buffer) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    buffer[0] = '\0';
    size_t buffer_used = 0;

    /// Format each entry: "  ID  command"
    for (size_t i = 0; i < entry_count; i++) {
        lle_history_entry_t *entry = NULL;
        result = lle_history_get_entry_by_index(g_bridge->lle_core, i, &entry);

        if (result != LLE_SUCCESS || !entry) {
            continue;
        }

        /// Skip deleted entries (removed by deduplication)
        if (entry->state == LLE_HISTORY_STATE_DELETED) {
            continue;
        }

        /// Format entry
        int written = snprintf(
            buffer + buffer_used, buffer_size - buffer_used, "%5llu  %s\n",
            (unsigned long long)entry->entry_id, entry->command);

        if (written > 0 && (size_t)written < buffer_size - buffer_used) {
            buffer_used += written;
        } else {
            /// Buffer full
            break;
        }
    }

    if (output) {
        *output = buffer;
    } else {
        /// Print to stdout
        printf("%s", buffer);
        lle_pool_free(buffer);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Get entry by history number (for history expansion like !123)
 *
 * @param number History entry number to look up
 * @param entry Output pointer for the found entry
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized, or LLE_ERROR_NOT_FOUND if entry not found
 */
lle_result_t lle_history_bridge_get_by_number(uint64_t number,
                                              lle_history_entry_t **entry) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /// In LLE, entry_id is the number
    return lle_history_get_entry_by_id(g_bridge->lle_core, number, entry);
}

/**
 * @brief Get entry by reverse index (0 = most recent, for !! and !-N)
 *
 * @param reverse_index Reverse index (0 = most recent, 1 = second most recent,
 * etc.)
 * @param entry Output pointer for the found entry
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized, or LLE_ERROR_NOT_FOUND if entry not found
 */
lle_result_t
lle_history_bridge_get_by_reverse_index(size_t reverse_index,
                                        lle_history_entry_t **entry) {
    if (!g_bridge || !g_bridge->initialized) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    return lle_history_get_entry_by_reverse_index(g_bridge->lle_core,
                                                  reverse_index, entry);
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================
 */

/**
 * @brief Enable/disable readline synchronization
 *
 * @param enabled true to enable, false to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_set_readline_sync(bool enabled) {
    if (!g_bridge) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    g_bridge->readline_sync_enabled = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Enable/disable POSIX synchronization
 *
 * @param enabled true to enable, false to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_set_posix_sync(bool enabled) {
    if (!g_bridge) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    g_bridge->posix_sync_enabled = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Enable/disable automatic synchronization
 *
 * @param enabled true to enable, false to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_set_auto_sync(bool enabled) {
    if (!g_bridge) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    g_bridge->auto_sync = enabled;
    return LLE_SUCCESS;
}

/**
 * @brief Enable/disable bidirectional synchronization
 *
 * @param enabled true to enable, false to disable
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_set_bidirectional_sync(bool enabled) {
    if (!g_bridge) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    g_bridge->bidirectional_sync = enabled;
    return LLE_SUCCESS;
}

/* ============================================================================
 * STATISTICS AND DIAGNOSTICS
 * ============================================================================
 */

/**
 * @brief Get bridge statistics
 *
 * @param readline_imports Output for readline imports count (may be NULL)
 * @param readline_exports Output for readline exports count (may be NULL)
 * @param posix_imports Output for POSIX imports count (may be NULL)
 * @param posix_exports Output for POSIX exports count (may be NULL)
 * @param sync_errors Output for synchronization errors count (may be NULL)
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_get_stats(size_t *readline_imports,
                                          size_t *readline_exports,
                                          size_t *posix_imports,
                                          size_t *posix_exports,
                                          size_t *sync_errors) {
    if (!g_bridge) {
        return LLE_ERROR_NOT_INITIALIZED;
    }

    if (readline_imports)
        *readline_imports = g_bridge->readline_imports;
    if (readline_exports)
        *readline_exports = g_bridge->readline_exports;
    if (posix_imports)
        *posix_imports = g_bridge->posix_imports;
    if (posix_exports)
        *posix_exports = g_bridge->posix_exports;
    if (sync_errors)
        *sync_errors = g_bridge->sync_errors;

    return LLE_SUCCESS;
}

/**
 * @brief Print bridge diagnostics
 *
 * Prints diagnostic information about the bridge state to stdout.
 *
 * @return LLE_SUCCESS on success, LLE_ERROR_NOT_INITIALIZED if bridge not
 * initialized
 */
lle_result_t lle_history_bridge_print_diagnostics(void) {
    if (!g_bridge) {
        printf("Bridge: Not initialized\n");
        return LLE_ERROR_NOT_INITIALIZED;
    }

    printf("=== LLE History Bridge Diagnostics ===\n");
    printf("Initialized: %s\n", g_bridge->initialized ? "Yes" : "No");
    printf("Readline sync: %s\n",
           g_bridge->readline_sync_enabled ? "Enabled" : "Disabled");
    printf("POSIX sync: %s\n",
           g_bridge->posix_sync_enabled ? "Enabled" : "Disabled");
    printf("Auto sync: %s\n", g_bridge->auto_sync ? "Enabled" : "Disabled");
    printf("Bidirectional: %s\n",
           g_bridge->bidirectional_sync ? "Enabled" : "Disabled");
    printf("\n");
    printf("Statistics:\n");
    printf("  Readline imports: %zu\n", g_bridge->readline_imports);
    printf("  Readline exports: %zu\n", g_bridge->readline_exports);
    printf("  POSIX imports: %zu\n", g_bridge->posix_imports);
    printf("  POSIX exports: %zu\n", g_bridge->posix_exports);
    printf("  Sync errors: %zu\n", g_bridge->sync_errors);

    return LLE_SUCCESS;
}
