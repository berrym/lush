/**
 * @file async_worker.h
 * @brief LLE Async Worker Thread Pool
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides a pthread-based worker thread for async operations like git status.
 * This enables non-blocking prompt generation where expensive operations run
 * in the background.
 *
 * Specification: docs/lle_specification/25_prompt_theme_system_complete.md
 * Section: 7 - Async Operations
 *
 * Design Principles:
 * - Single worker thread (sufficient for git operations)
 * - Request queue with mutex/condition variable synchronization
 * - Completion callbacks for async responses
 * - Graceful shutdown with pending request draining
 *
 * Example Usage:
 *
 *     /// Callback for completed requests
 *     void on_git_status(const lle_async_response_t *resp, void *user_data) {
 *         if (resp->result == LLE_SUCCESS) {
 *             printf("Branch: %s\n", resp->data.git_status.branch);
 *         }
 *     }
 *
 *     /// Initialize and start worker
 *     lle_async_worker_t *worker;
 *     lle_async_worker_init(&worker, on_git_status, NULL);
 *     lle_async_worker_start(worker);
 *
 *     /// Submit async request
 *     lle_async_request_t *req =
 * lle_async_request_create(LLE_ASYNC_GIT_STATUS); strncpy(req->cwd,
 * "/path/to/repo", sizeof(req->cwd) - 1); lle_async_worker_submit(worker, req);
 *
 *     /// Later: shutdown
 *     lle_async_worker_shutdown(worker);
 *     lle_async_worker_destroy(worker);
 */

#ifndef LLE_ASYNC_WORKER_H
#define LLE_ASYNC_WORKER_H

#include "lle/error_handling.h"

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** Maximum length of branch name */
#define LLE_ASYNC_BRANCH_MAX 256

/** Maximum length of commit hash */
#define LLE_ASYNC_COMMIT_MAX 64

/** Default request timeout in milliseconds */
#define LLE_ASYNC_DEFAULT_TIMEOUT_MS 5000

/** Maximum queue size before rejecting new requests */
#define LLE_ASYNC_MAX_QUEUE_SIZE 16

/* ============================================================================
 * TYPES AND STRUCTURES
 * ============================================================================
 */

/**
 * Async request types
 */
typedef enum lle_async_request_type {
    LLE_ASYNC_GIT_STATUS, ///< Get git repository status
    LLE_ASYNC_CUSTOM      ///< Custom request with user-provided handler
} lle_async_request_type_t;

/**
 * Git status data returned from async worker
 */
typedef struct lle_git_status_data {
    bool is_git_repo;                  ///< Is this a git repository?
    char branch[LLE_ASYNC_BRANCH_MAX]; ///< Current branch name
    char commit[LLE_ASYNC_COMMIT_MAX]; ///< Short commit hash
    int staged_count;                  ///< Number of staged files
    int unstaged_count;                ///< Number of unstaged files
    int untracked_count;               ///< Number of untracked files
    int ahead;                         ///< Commits ahead of upstream
    int behind;                        ///< Commits behind upstream
    bool is_detached;                  ///< HEAD is detached
    bool is_merging;                   ///< Merge in progress
    bool is_rebasing;                  ///< Rebase in progress
} lle_git_status_data_t;

/**
 * Forward declarations
 */
typedef struct lle_async_request lle_async_request_t;
typedef struct lle_async_response lle_async_response_t;
typedef struct lle_async_worker lle_async_worker_t;

/**
 * Async response structure
 */
typedef struct lle_async_response {
    uint64_t id;         ///< Matching request ID
    lle_result_t result; ///< Success or error code

    union {
        lle_git_status_data_t git_status; ///< Git status data
        void *custom_data;                ///< Custom response data
    } data;
} lle_async_response_t;

/**
 * Completion callback type
 *
 * Called when an async request completes. This is called from the worker
 * thread, so the callback must be thread-safe or queue work for the main
 * thread.
 *
 * @param response Response data (valid only during callback)
 * @param user_data User-provided context
 */
typedef void (*lle_async_completion_fn)(const lle_async_response_t *response,
                                        void *user_data);

/**
 * Async request structure
 */
typedef struct lle_async_request {
    uint64_t id;                   ///< Unique request ID (assigned by worker)
    lle_async_request_type_t type; ///< Request type
    char cwd[PATH_MAX];            ///< Working directory for the request
    uint32_t timeout_ms;           ///< Timeout in milliseconds
    void *user_data;               ///< Custom data for custom requests

    struct lle_async_request *next; ///< Queue linkage (internal use)
} lle_async_request_t;

/**
 * Async worker thread structure
 */
typedef struct lle_async_worker {
    pthread_t thread;            ///< Worker thread
    pthread_mutex_t queue_mutex; ///< Queue mutex
    pthread_cond_t queue_cond;   ///< Queue condition variable

    /// Request queue
    lle_async_request_t *queue_head; ///< Queue head
    lle_async_request_t *queue_tail; ///< Queue tail
    size_t queue_size;               ///< Current queue size

    /// State
    bool running;            ///< Worker is running
    bool shutdown_requested; ///< Shutdown has been requested

    /// Completion callback
    lle_async_completion_fn on_complete; ///< Completion callback
    void *callback_user_data;            ///< Callback user data

    /// Statistics
    uint64_t total_requests;  ///< Total requests submitted
    uint64_t total_completed; ///< Total requests completed
    uint64_t total_timeouts;  ///< Total requests timed out
    uint64_t next_request_id; ///< Next request ID to assign
} lle_async_worker_t;

/* ============================================================================
 * WORKER LIFECYCLE
 * ============================================================================
 */

/**
 * Initialize async worker
 *
 * Creates the worker structure but does not start the thread. Call
 * lle_async_worker_start() to begin processing requests.
 *
 * @param worker Output pointer for created worker (must not be NULL)
 * @param on_complete Callback for completed requests (may be NULL)
 * @param user_data User data passed to callback
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker is NULL
 * @return LLE_ERROR_OUT_OF_MEMORY if allocation fails
 */
lle_result_t lle_async_worker_init(lle_async_worker_t **worker,
                                   lle_async_completion_fn on_complete,
                                   void *user_data);

/**
 * Start async worker thread
 *
 * Starts the worker thread. The worker will begin processing queued requests.
 *
 * @param worker Worker to start (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker is NULL or already running
 * @return LLE_ERROR_INTERNAL if thread creation fails
 */
lle_result_t lle_async_worker_start(lle_async_worker_t *worker);

/**
 * Request worker shutdown
 *
 * Signals the worker to stop accepting new requests and finish processing
 * pending requests. This is non-blocking; use lle_async_worker_wait() to
 * wait for completion.
 *
 * @param worker Worker to shutdown (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker is NULL
 */
lle_result_t lle_async_worker_shutdown(lle_async_worker_t *worker);

/**
 * Wait for worker to complete
 *
 * Blocks until the worker thread exits. Should be called after shutdown.
 *
 * @param worker Worker to wait for (must not be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker is NULL
 */
lle_result_t lle_async_worker_wait(lle_async_worker_t *worker);

/**
 * Destroy async worker
 *
 * Frees all resources. Must call lle_async_worker_shutdown() and
 * lle_async_worker_wait() first if the worker is running.
 *
 * @param worker Worker to destroy (may be NULL)
 * @return LLE_SUCCESS on success
 */
lle_result_t lle_async_worker_destroy(lle_async_worker_t *worker);

/* ============================================================================
 * REQUEST MANAGEMENT
 * ============================================================================
 */

/**
 * Create async request
 *
 * Allocates and initializes a new request. The caller must free the request
 * or submit it to the worker (which takes ownership).
 *
 * @param type Request type
 * @return New request, or NULL on allocation failure
 */
lle_async_request_t *lle_async_request_create(lle_async_request_type_t type);

/**
 * Free async request
 *
 * Frees a request that was not submitted to the worker.
 *
 * @param request Request to free (may be NULL)
 */
void lle_async_request_free(lle_async_request_t *request);

/**
 * Submit async request
 *
 * Submits a request to the worker queue. The worker takes ownership of
 * the request and will free it after processing.
 *
 * @param worker Worker to submit to (must not be NULL)
 * @param request Request to submit (must not be NULL, ownership transferred)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker or request is NULL
 * @return LLE_ERROR_INVALID_STATE if worker is not running or shutdown
 * requested
 * @return LLE_ERROR_RESOURCE_EXHAUSTED if queue is full
 */
lle_result_t lle_async_worker_submit(lle_async_worker_t *worker,
                                     lle_async_request_t *request);

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * Check if worker is running
 *
 * @param worker Worker to check (must not be NULL)
 * @return true if running, false otherwise
 */
bool lle_async_worker_is_running(const lle_async_worker_t *worker);

/**
 * Get pending request count
 *
 * @param worker Worker to query (must not be NULL)
 * @return Number of pending requests, or 0 if worker is NULL
 */
size_t lle_async_worker_pending_count(const lle_async_worker_t *worker);

/**
 * Get worker statistics
 *
 * @param worker Worker to query (must not be NULL)
 * @param total_requests Output for total requests (may be NULL)
 * @param total_completed Output for completed requests (may be NULL)
 * @param total_timeouts Output for timed out requests (may be NULL)
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if worker is NULL
 */
lle_result_t lle_async_worker_get_stats(const lle_async_worker_t *worker,
                                        uint64_t *total_requests,
                                        uint64_t *total_completed,
                                        uint64_t *total_timeouts);

#ifdef __cplusplus
}
#endif

#endif /// LLE_ASYNC_WORKER_H
