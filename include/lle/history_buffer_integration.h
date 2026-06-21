/**
 * @file history_buffer_integration.h
 * @brief History-Buffer integration system for command editing
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements the critical missing functionality from LLE_DESIGN_DOCUMENT.md:
 * seamless integration between history system and buffer management for
 * interactive editing of historical commands with complete multiline structure
 * preservation and restoration.
 *
 * Critical Gap Spec: 22_history_buffer_integration_complete.md
 * Integration Target: Spec 09 (History System)
 */

#ifndef LLE_HISTORY_BUFFER_INTEGRATION_H
#define LLE_HISTORY_BUFFER_INTEGRATION_H

#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/event_system.h"
#include "lle/history.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

typedef struct lle_history_buffer_integration lle_history_buffer_integration_t;
typedef struct lle_edit_session lle_edit_session_t;
typedef struct lle_command_structure lle_command_structure_t;
typedef struct lle_reconstruction_engine lle_reconstruction_engine_t;
typedef struct lle_edit_session_manager lle_edit_session_manager_t;
typedef struct lle_multiline_parser lle_multiline_parser_t;
typedef struct lle_structure_analyzer lle_structure_analyzer_t;
typedef struct lle_formatting_engine lle_formatting_engine_t;
typedef struct lle_history_edit_callbacks lle_history_edit_callbacks_t;
typedef struct lle_callback_registry lle_callback_registry_t;
typedef struct lle_edit_cache lle_edit_cache_t;
typedef struct lle_integration_config lle_integration_config_t;
typedef struct lle_integration_state lle_integration_state_t;
typedef struct lle_indentation_info lle_indentation_info_t;
typedef struct lle_line_mapping lle_line_mapping_t;
typedef struct lle_multiline_info lle_multiline_info_t;
typedef struct lle_edit_change lle_edit_change_t;

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/// Edit session state enum is defined in edit_session_manager.h
/// Include that header to get the enum definition
#include "lle/edit_session_manager.h"

/**
 * Command structure type (shell constructs)
 */
typedef enum lle_command_type {
    LLE_CMD_TYPE_SIMPLE = 0,     ///< Simple command
    LLE_CMD_TYPE_PIPELINE,       ///< Pipeline (|)
    LLE_CMD_TYPE_COMPOUND,       ///< Compound command (&&, ||, ;)
    LLE_CMD_TYPE_FOR_LOOP,       ///< for loop
    LLE_CMD_TYPE_WHILE_LOOP,     ///< while loop
    LLE_CMD_TYPE_UNTIL_LOOP,     ///< until loop
    LLE_CMD_TYPE_IF_STATEMENT,   ///< if statement
    LLE_CMD_TYPE_CASE_STATEMENT, ///< case statement
    LLE_CMD_TYPE_FUNCTION,       ///< Function definition
    LLE_CMD_TYPE_SUBSHELL,       ///< Subshell ( )
    LLE_CMD_TYPE_COMMAND_GROUP,  ///< Command group { }
    LLE_CMD_TYPE_UNKNOWN         /// Unknown/unparseable
} lle_command_type_t;

/**
 * Integration state tracking
 */
typedef enum lle_integration_state_type {
    LLE_INTEGRATION_UNINITIALIZED = 0, ///< System not initialized
    LLE_INTEGRATION_READY,             ///< Ready for operations
    LLE_INTEGRATION_BUSY,              ///< Operation in progress
    LLE_INTEGRATION_ERROR,             ///< Error state
    LLE_INTEGRATION_SHUTDOWN           /// Shutting down
} lle_integration_state_type_t;

/* ============================================================================
 * CALLBACK TYPES
 * ============================================================================
 */

/**
 * Callback for edit session start
 * @param entry History entry being edited
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_edit_start_callback_t)(lle_history_entry_t *entry,
                                                  void *user_data);

/**
 * Callback for edit session completion
 * @param entry History entry after editing
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_edit_complete_callback_t)(lle_history_entry_t *entry,
                                                     void *user_data);

/**
 * Callback for edit session cancellation
 * @param entry Original history entry (unmodified)
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_edit_cancel_callback_t)(lle_history_entry_t *entry,
                                                   void *user_data);

/**
 * Callback for buffer loaded with reconstructed content
 * @param buffer Buffer containing reconstructed command
 * @param entry History entry that was loaded
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_buffer_loaded_callback_t)(lle_buffer_t *buffer,
                                                     lle_history_entry_t *entry,
                                                     void *user_data);

/**
 * Callback for structure reconstruction completion
 * @param multiline Multiline structure information
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_structure_reconstructed_callback_t)(
    lle_multiline_info_t *multiline, void *user_data);

/**
 * Callback for buffer modification during editing
 * @param buffer Buffer being edited
 * @param change Description of the change
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_edit_modified_callback_t)(lle_buffer_t *buffer,
                                                     lle_edit_change_t *change,
                                                     void *user_data);

/**
 * Callback for save request
 * @param buffer Buffer with edited content
 * @param entry History entry being saved
 * @param user_data User-provided context
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_save_requested_callback_t)(
    lle_buffer_t *buffer, lle_history_entry_t *entry, void *user_data);

/* ============================================================================
 * CORE STRUCTURES
 * ============================================================================
 */

/**
 * Callback framework structure
 */
struct lle_history_edit_callbacks {
    lle_edit_start_callback_t on_edit_start;
    lle_edit_complete_callback_t on_edit_complete;
    lle_edit_cancel_callback_t on_edit_cancel;
    lle_buffer_loaded_callback_t on_buffer_loaded;
    lle_structure_reconstructed_callback_t on_structure_reconstructed;
    lle_edit_modified_callback_t on_edit_modified;
    lle_save_requested_callback_t on_save_requested;
    void *user_data;
};

/**
 * Integration configuration
 */
struct lle_integration_config {
    bool enable_multiline_reconstruction; ///< Enable multiline reconstruction
    bool preserve_original_formatting;    ///< Preserve original indentation
    bool enable_structure_analysis;       ///< Enable shell construct analysis
    bool enable_edit_caching;             ///< Enable reconstruction caching
    uint32_t max_cache_entries;           ///< Maximum cache entries
    uint32_t max_reconstruction_depth;    ///< Maximum nesting depth
    uint32_t reconstruction_timeout_ms;   ///< Reconstruction timeout
};

/**
 * Integration state
 */
struct lle_integration_state {
    lle_integration_state_type_t state;  ///< Current state
    uint64_t active_sessions;            ///< Number of active edit sessions
    uint64_t total_edits;                ///< Total edits performed
    uint64_t successful_reconstructions; ///< Successful reconstructions
    uint64_t failed_reconstructions;     ///< Failed reconstructions
    uint64_t cache_hits;                 ///< Cache hits
    uint64_t cache_misses;               ///< Cache misses
    uint64_t last_operation_time_us;     ///< Last operation time (microseconds)
};

/**
 * Main history-buffer integration system
 */
struct lle_history_buffer_integration {
    /// Core components
    lle_history_core_t *history_core; ///< History core reference
    lle_buffer_t *editing_buffer;     ///< Buffer system reference
    lle_reconstruction_engine_t
        *reconstruction; ///< Command reconstruction engine
    lle_edit_session_manager_t *session_manager; ///< Edit session management

    /// Multiline support
    lle_multiline_parser_t *multiline_parser; ///< Multiline structure parser
    lle_structure_analyzer_t *structure_analyzer; ///< Shell construct analyzer
    lle_formatting_engine_t *formatter; ///< Intelligent formatting engine

    /// Callback system
    lle_history_edit_callbacks_t *edit_callbacks; ///< Edit event callbacks
    lle_callback_registry_t *callback_registry;   ///< Callback management

    /// Performance optimization
    lle_edit_cache_t *edit_cache;            ///< Edit operation caching
    lle_memory_pool_t *memory_pool;          ///< Memory pool integration
    lle_performance_monitor_t *perf_monitor; ///< Performance monitoring

    /// Configuration and state
    lle_integration_config_t *config;       ///< Integration configuration
    lle_integration_state_t *current_state; ///< Current integration state

    /// Event system integration
    lle_event_system_t *event_system; ///< Event system reference

    /// Synchronization
    pthread_rwlock_t integration_lock; ///< Thread-safe access
    bool system_active;                ///< Integration system status
    uint64_t session_counter;          ///< Edit session counter
};

/* ============================================================================
 * SYSTEM LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * Create and initialize history-buffer integration system
 *
 * @param integration Output pointer for created system
 * @param history_core History system to integrate with
 * @param memory_pool Memory pool for allocations
 * @param event_system Event system for notifications (optional)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_create(
    lle_history_buffer_integration_t **integration,
    lle_history_core_t *history_core, lle_memory_pool_t *memory_pool,
    lle_event_system_t *event_system);

/**
 * Destroy history-buffer integration system
 *
 * @param integration Integration system to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_destroy(
    lle_history_buffer_integration_t *integration);

/**
 * Set integration configuration
 *
 * @param integration Integration system
 * @param config Configuration to apply
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_set_config(
    lle_history_buffer_integration_t *integration,
    const lle_integration_config_t *config);

/**
 * Get current integration configuration
 *
 * @param integration Integration system
 * @param config Output pointer for configuration
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_get_config(
    lle_history_buffer_integration_t *integration,
    lle_integration_config_t *config);

/**
 * Get current integration state
 *
 * @param integration Integration system
 * @param state Output pointer for state
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_get_state(
    lle_history_buffer_integration_t *integration,
    lle_integration_state_t *state);

/* ============================================================================
 * CALLBACK REGISTRATION
 * ============================================================================
 */

/**
 * Register edit callbacks
 *
 * @param integration Integration system
 * @param callbacks Callback structure (NULL to clear)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_register_callbacks(
    lle_history_buffer_integration_t *integration,
    const lle_history_edit_callbacks_t *callbacks);

/**
 * Unregister all callbacks
 *
 * @param integration Integration system
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_unregister_callbacks(
    lle_history_buffer_integration_t *integration);

/* ============================================================================
 * INTERACTIVE EDITING FUNCTIONS
 * ============================================================================
 */

/**
 * Start interactive editing of a history entry
 *
 * Loads the specified history entry into a buffer with multiline
 * reconstruction applied. Creates an edit session to track modifications.
 *
 * @param integration Integration system
 * @param entry_index History entry index to edit
 * @param buffer Buffer to load entry into
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_history_edit_entry(lle_history_buffer_integration_t *integration,
                       size_t entry_index, lle_buffer_t *buffer);

/**
 * Complete an edit session and save changes
 *
 * Saves the buffer contents back to history, replacing the original entry.
 * Triggers on_edit_complete callback if registered.
 *
 * @param integration Integration system
 * @param buffer Buffer with edited content
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_history_session_complete(lle_history_buffer_integration_t *integration,
                             lle_buffer_t *buffer);

/**
 * Cancel an edit session without saving
 *
 * Discards buffer modifications and closes the edit session.
 * Triggers on_edit_cancel callback if registered.
 *
 * @param integration Integration system
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_history_session_cancel(lle_history_buffer_integration_t *integration);

/* ============================================================================
 * PERFORMANCE MONITORING FUNCTIONS
 * ============================================================================
 */

/// Forward declaration for cache stats
typedef struct lle_edit_cache_stats lle_edit_cache_stats_t;

/// Include edit_cache.h to get stats structure definition
#include "lle/edit_cache.h"

/**
 * Get cache performance statistics
 *
 * Retrieves hit/miss ratios, entry counts, evictions, and other
 * cache performance metrics.
 *
 * @param integration Integration system
 * @param stats Output parameter for cache statistics
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_get_cache_stats(
    lle_history_buffer_integration_t *integration,
    lle_edit_cache_stats_t *stats);

/**
 * Clear all cache entries
 *
 * Removes all cached reconstruction results. Useful for testing
 * or when memory needs to be reclaimed.
 *
 * @param integration Integration system
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_clear_cache(
    lle_history_buffer_integration_t *integration);

/**
 * Perform cache maintenance
 *
 * Evicts expired cache entries based on TTL settings.
 * Should be called periodically to prevent unbounded growth.
 *
 * @param integration Integration system
 * @param expired_count Output parameter for number of entries evicted
 * (optional)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_history_buffer_integration_maintain_cache(
    lle_history_buffer_integration_t *integration, size_t *expired_count);

#endif /// LLE_HISTORY_BUFFER_INTEGRATION_H
