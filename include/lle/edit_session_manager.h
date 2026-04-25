/**
 * @file edit_session_manager.h
 * @brief Edit session lifecycle and state tracking
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * LLE Specification 22: History-Buffer Integration - Phase 3
 * Manages edit sessions for history entry editing with operation tracking.
 */

#ifndef LLE_EDIT_SESSION_MANAGER_H
#define LLE_EDIT_SESSION_MANAGER_H

#include "lle/command_structure.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct lle_edit_session_manager lle_edit_session_manager_t;
typedef struct lle_edit_session lle_edit_session_t;
typedef struct lle_history_core lle_history_core_t;

/**
 * @brief Edit session state enumeration
 */
typedef enum lle_edit_session_state {
    LLE_EDIT_SESSION_INACTIVE = 0, /**< No active session */
    LLE_EDIT_SESSION_ACTIVE,       /**< Session active, editing in progress */
    LLE_EDIT_SESSION_MODIFIED,     /**< Session has unsaved modifications */
    LLE_EDIT_SESSION_COMPLETED,    /**< Session being completed */
    LLE_EDIT_SESSION_CANCELING,    /**< Session being canceled */
    LLE_EDIT_SESSION_ERROR         /**< Session encountered error */
} lle_edit_session_state_t;

/**
 * @brief Edit operation type enumeration
 */
typedef enum lle_edit_operation_type {
    LLE_EDIT_OP_INSERT = 0, /**< Insert text */
    LLE_EDIT_OP_DELETE,     /**< Delete text */
    LLE_EDIT_OP_REPLACE,    /**< Replace text */
    LLE_EDIT_OP_INDENT,     /**< Change indentation */
    LLE_EDIT_OP_FORMAT      /**< Apply formatting */
} lle_edit_operation_type_t;

/**
 * @brief Edit operation record structure
 */
typedef struct lle_edit_operation {
    lle_edit_operation_type_t type;  /**< Operation type */
    size_t offset;                   /**< Position in buffer */
    size_t length;                   /**< Length of affected text */
    char *text;                      /**< Operation data */
    size_t text_length;              /**< Length of operation data */
    struct timespec timestamp;       /**< Operation timestamp */
    struct lle_edit_operation *next; /**< Next operation in list */
} lle_edit_operation_t;

/**
 * @brief Edit session information structure
 */
typedef struct lle_edit_session {
    uint64_t session_id;                   /**< Session identification */
    lle_edit_session_state_t state;        /**< Current session state */
    size_t entry_index;                    /**< History entry being edited */
    char *original_text;                   /**< Original text before editing */
    size_t original_length;                /**< Length of original text */
    char *current_text;                    /**< Current edited content */
    size_t current_length;                 /**< Length of current content */
    lle_command_structure_t *structure;    /**< Command structure */
    lle_edit_operation_t *first_operation; /**< First edit operation */
    lle_edit_operation_t *last_operation;  /**< Last edit operation */
    size_t operation_count;                /**< Number of operations */
    struct timespec start_time;            /**< Session start time */
    struct timespec last_modified;         /**< Last modification time */
    bool has_modifications; /**< Whether session has modifications */
    bool multiline_mode;    /**< Whether in multiline mode */
    void *reserved[2];      /**< Reserved for future use */
} lle_edit_session_t;

/**
 * @brief Session manager configuration structure
 */
typedef struct lle_session_manager_config {
    size_t max_sessions;         /**< Maximum concurrent sessions */
    uint32_t session_timeout_ms; /**< Session timeout (milliseconds, 0 = no
                                    timeout) */
    bool track_operations;       /**< Track edit operations */
    size_t max_operations;       /**< Maximum operations per session */
    void *reserved[4];           /**< Reserved for future use */
} lle_session_manager_config_t;

/**
 * @brief Create an edit session manager
 * @param manager Output parameter for created manager
 * @param memory_pool Memory pool for allocations
 * @param history_core History core for entry access
 * @param config Configuration (NULL for defaults)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_create(lle_edit_session_manager_t **manager,
                                lle_memory_pool_t *memory_pool,
                                lle_history_core_t *history_core,
                                const lle_session_manager_config_t *config);

/**
 * @brief Destroy an edit session manager
 * @param manager Manager to destroy
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_destroy(lle_edit_session_manager_t *manager);

/**
 * @brief Start a new edit session for a history entry
 * @param manager Session manager
 * @param entry_index History entry index to edit
 * @param session Output parameter for created session
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_start_session(lle_edit_session_manager_t *manager,
                                       size_t entry_index,
                                       lle_edit_session_t **session);

/**
 * @brief Get the current active session
 * @param manager Session manager
 * @param session Output parameter for current session (NULL if none)
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_session_manager_get_current_session(
    lle_edit_session_manager_t *manager, lle_edit_session_t **session);

/**
 * @brief Record an edit operation in the current session
 * @param manager Session manager
 * @param session Session to record operation in
 * @param operation Operation to record
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_session_manager_record_operation(
    lle_edit_session_manager_t *manager, lle_edit_session_t *session,
    const lle_edit_operation_t *operation);

/**
 * @brief Update the current text in a session
 * @param manager Session manager
 * @param session Session to update
 * @param new_text New text content
 * @param new_length Length of new text
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_update_text(lle_edit_session_manager_t *manager,
                                     lle_edit_session_t *session,
                                     const char *new_text, size_t new_length);

/**
 * @brief Complete an edit session (save changes)
 * @param manager Session manager
 * @param session Session to complete
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_complete_session(lle_edit_session_manager_t *manager,
                                          lle_edit_session_t *session);

/**
 * @brief Cancel an edit session (discard changes)
 * @param manager Session manager
 * @param session Session to cancel
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_cancel_session(lle_edit_session_manager_t *manager,
                                        lle_edit_session_t *session);

/**
 * @brief Check if a session has timed out
 * @param manager Session manager
 * @param session Session to check
 * @param timed_out Output parameter for timeout status
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t
lle_edit_session_manager_check_timeout(lle_edit_session_manager_t *manager,
                                       lle_edit_session_t *session,
                                       bool *timed_out);

/**
 * @brief Get default session manager configuration
 * @param config Configuration structure to fill with defaults
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_edit_session_manager_get_default_config(
    lle_session_manager_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* LLE_EDIT_SESSION_MANAGER_H */
