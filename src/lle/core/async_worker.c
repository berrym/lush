/**
 * @file async_worker.c
 * @brief LLE Async Worker Thread Pool Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements a pthread-based worker thread for async operations.
 *
 * Specification: docs/lle_specification/25_prompt_theme_system_complete.md
 * Section: 7 - Async Operations
 */

#include "lle/async_worker.h"
#include "lle/git_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * INTERNAL DECLARATIONS
 * ============================================================================
 */

/**
 * @brief Worker thread main function
 * @param arg Worker context (lle_async_worker_t *)
 * @return Always NULL
 */
static void *lle_async_worker_thread(void *arg);

/**
 * @brief Get git repository status
 * @param cwd Working directory
 * @param timeout_ms Timeout in milliseconds
 * @param status Output status structure
 * @return Result code
 */
static lle_result_t lle_async_get_git_status(const char *cwd,
                                             uint32_t timeout_ms,
                                             lle_git_status_data_t *status);

/* ============================================================================
 * WORKER LIFECYCLE
 * ============================================================================
 */

lle_result_t lle_async_worker_init(lle_async_worker_t **worker,
                                   lle_async_completion_fn on_complete,
                                   void *user_data) {
    if (!worker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_async_worker_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&w->queue_mutex, NULL) != 0) {
        free(w);
        return LLE_ERROR_SYSTEM_CALL;
    }

    if (pthread_cond_init(&w->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&w->queue_mutex);
        free(w);
        return LLE_ERROR_SYSTEM_CALL;
    }

    w->on_complete = on_complete;
    w->callback_user_data = user_data;
    w->running = false;
    w->shutdown_requested = false;
    w->next_request_id = 1;
    w->queue_head = NULL;
    w->queue_tail = NULL;
    w->queue_size = 0;
    w->total_requests = 0;
    w->total_completed = 0;
    w->total_timeouts = 0;

    *worker = w;
    return LLE_SUCCESS;
}

lle_result_t lle_async_worker_start(lle_async_worker_t *worker) {
    if (!worker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&worker->queue_mutex);
    if (worker->running) {
        pthread_mutex_unlock(&worker->queue_mutex);
        return LLE_ERROR_INVALID_PARAMETER;
    }

    worker->running = true;
    worker->shutdown_requested = false;
    pthread_mutex_unlock(&worker->queue_mutex);

    if (pthread_create(&worker->thread, NULL, lle_async_worker_thread,
                       worker) != 0) {
        pthread_mutex_lock(&worker->queue_mutex);
        worker->running = false;
        pthread_mutex_unlock(&worker->queue_mutex);
        return LLE_ERROR_SYSTEM_CALL;
    }

    return LLE_SUCCESS;
}

lle_result_t lle_async_worker_shutdown(lle_async_worker_t *worker) {
    if (!worker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&worker->queue_mutex);
    worker->shutdown_requested = true;
    pthread_cond_signal(&worker->queue_cond);
    pthread_mutex_unlock(&worker->queue_mutex);

    return LLE_SUCCESS;
}

lle_result_t lle_async_worker_wait(lle_async_worker_t *worker) {
    if (!worker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&worker->queue_mutex);
    bool was_running = worker->running;
    pthread_mutex_unlock(&worker->queue_mutex);

    if (was_running) {
        pthread_join(worker->thread, NULL);

        pthread_mutex_lock(&worker->queue_mutex);
        worker->running = false;
        pthread_mutex_unlock(&worker->queue_mutex);
    }

    return LLE_SUCCESS;
}

lle_result_t lle_async_worker_destroy(lle_async_worker_t *worker) {
    if (!worker) {
        return LLE_SUCCESS;
    }

    /* Free any pending requests */
    pthread_mutex_lock(&worker->queue_mutex);
    lle_async_request_t *req = worker->queue_head;
    while (req) {
        lle_async_request_t *next = req->next;
        free(req);
        req = next;
    }
    worker->queue_head = NULL;
    worker->queue_tail = NULL;
    worker->queue_size = 0;
    pthread_mutex_unlock(&worker->queue_mutex);

    pthread_mutex_destroy(&worker->queue_mutex);
    pthread_cond_destroy(&worker->queue_cond);

    free(worker);
    return LLE_SUCCESS;
}

/* ============================================================================
 * REQUEST MANAGEMENT
 * ============================================================================
 */

lle_async_request_t *lle_async_request_create(lle_async_request_type_t type) {
    lle_async_request_t *req = calloc(1, sizeof(*req));
    if (!req) {
        return NULL;
    }

    req->type = type;
    req->timeout_ms = LLE_ASYNC_DEFAULT_TIMEOUT_MS;
    req->id = 0; /* Assigned by worker on submit */
    req->next = NULL;
    req->user_data = NULL;
    req->cwd[0] = '\0';

    return req;
}

void lle_async_request_free(lle_async_request_t *request) { free(request); }

lle_result_t lle_async_worker_submit(lle_async_worker_t *worker,
                                     lle_async_request_t *request) {
    if (!worker || !request) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&worker->queue_mutex);

    if (!worker->running || worker->shutdown_requested) {
        pthread_mutex_unlock(&worker->queue_mutex);
        return LLE_ERROR_INVALID_STATE;
    }

    if (worker->queue_size >= LLE_ASYNC_MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&worker->queue_mutex);
        return LLE_ERROR_RESOURCE_EXHAUSTED;
    }

    /* Assign request ID */
    request->id = worker->next_request_id++;
    request->next = NULL;

    /* Enqueue */
    if (worker->queue_tail) {
        worker->queue_tail->next = request;
    } else {
        worker->queue_head = request;
    }
    worker->queue_tail = request;
    worker->queue_size++;
    worker->total_requests++;

    pthread_cond_signal(&worker->queue_cond);
    pthread_mutex_unlock(&worker->queue_mutex);

    return LLE_SUCCESS;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

bool lle_async_worker_is_running(const lle_async_worker_t *worker) {
    if (!worker) {
        return false;
    }

    /* Note: This is a simple check without locking for performance.
     * The running flag is set atomically by the worker lifecycle functions. */
    return worker->running && !worker->shutdown_requested;
}

size_t lle_async_worker_pending_count(const lle_async_worker_t *worker) {
    if (!worker) {
        return 0;
    }

    /* Note: queue_size is updated under mutex, but this read is safe
     * for informational purposes. */
    return worker->queue_size;
}

lle_result_t lle_async_worker_get_stats(const lle_async_worker_t *worker,
                                        uint64_t *total_requests,
                                        uint64_t *total_completed,
                                        uint64_t *total_timeouts) {
    if (!worker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (total_requests) {
        *total_requests = worker->total_requests;
    }
    if (total_completed) {
        *total_completed = worker->total_completed;
    }
    if (total_timeouts) {
        *total_timeouts = worker->total_timeouts;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * WORKER THREAD
 * ============================================================================
 */

/**
 * @brief Worker thread main function
 *
 * Main loop for the async worker thread. Waits for requests on the queue,
 * processes them, and invokes the completion callback with results.
 *
 * @param arg Worker context pointer (lle_async_worker_t *)
 * @return Always returns NULL
 */
static void *lle_async_worker_thread(void *arg) {
    lle_async_worker_t *worker = arg;

    while (1) {
        lle_async_request_t *request = NULL;

        /* Wait for work */
        pthread_mutex_lock(&worker->queue_mutex);
        while (worker->queue_head == NULL && !worker->shutdown_requested) {
            pthread_cond_wait(&worker->queue_cond, &worker->queue_mutex);
        }

        if (worker->shutdown_requested && worker->queue_head == NULL) {
            pthread_mutex_unlock(&worker->queue_mutex);
            break;
        }

        /* Dequeue request */
        request = worker->queue_head;
        if (request) {
            worker->queue_head = request->next;
            if (worker->queue_head == NULL) {
                worker->queue_tail = NULL;
            }
            worker->queue_size--;
        }
        pthread_mutex_unlock(&worker->queue_mutex);

        if (!request) {
            continue;
        }

        /* Process request */
        lle_async_response_t response;
        memset(&response, 0, sizeof(response));
        response.id = request->id;

        switch (request->type) {
        case LLE_ASYNC_GIT_STATUS:
            response.result = lle_async_get_git_status(
                request->cwd, request->timeout_ms, &response.data.git_status);
            break;

        case LLE_ASYNC_CUSTOM:
            /* Custom requests not yet implemented */
            response.result = LLE_ERROR_FEATURE_NOT_AVAILABLE;
            break;

        default:
            response.result = LLE_ERROR_INVALID_PARAMETER;
            break;
        }

        /* Update stats before callback so they're visible when callback signals */
        pthread_mutex_lock(&worker->queue_mutex);
        worker->total_completed++;
        pthread_mutex_unlock(&worker->queue_mutex);

        /* Notify completion */
        if (worker->on_complete) {
            worker->on_complete(&response, worker->callback_user_data);
        }

        free(request);
    }

    return NULL;
}

/* ============================================================================
 * GIT STATUS PROVIDER
 * ============================================================================
 */

/**
 * @brief Run a git command in a directory with timeout and capture output
 *
 * Uses git -C <dir> to avoid process-wide chdir() which is unsafe from
 * worker threads. All commands have a wall-clock timeout to prevent hangs.
 *
 * @param cwd         Working directory for git
 * @param args        Git arguments (e.g., "rev-parse --git-dir")
 * @param output      Buffer for captured output (may be NULL)
 * @param output_size Size of output buffer
 * @param timeout_ms  Timeout in milliseconds
 * @return true if command succeeded (exit status 0), false otherwise
 */
static bool run_git_in_dir(const char *cwd, const char *args, char *output,
                           size_t output_size, uint32_t timeout_ms) {
    git_cmd_result_t r =
        git_command_in_dir(cwd, args, output, output_size, timeout_ms);
    return !r.timed_out && r.exit_status == 0;
}

/**
 * @brief Get git repository status
 *
 * Gathers comprehensive git status information for a repository.
 * Uses git -C <cwd> for all commands (no process-wide chdir).
 * All commands respect the timeout_ms parameter to prevent hangs.
 *
 * @param cwd Working directory of the repository
 * @param timeout_ms Timeout in milliseconds per git command
 * @param status Output structure for git status data
 * @return LLE_SUCCESS on success
 * @return LLE_ERROR_INVALID_PARAMETER if cwd or status is NULL
 */
static lle_result_t lle_async_get_git_status(const char *cwd,
                                             uint32_t timeout_ms,
                                             lle_git_status_data_t *status) {
    if (!cwd || !status) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (timeout_ms == 0) {
        timeout_ms = GIT_CMD_ASYNC_TIMEOUT_MS;
    }

    memset(status, 0, sizeof(*status));

    /* Check if in git repo */
    if (!run_git_in_dir(cwd, "rev-parse --git-dir", NULL, 0, timeout_ms)) {
        status->is_git_repo = false;
        return LLE_SUCCESS;
    }
    status->is_git_repo = true;

    /* Get branch name */
    if (run_git_in_dir(cwd, "branch --show-current", status->branch,
                       sizeof(status->branch), timeout_ms)) {
        /* branch is set */
    } else {
        /* Might be detached HEAD */
        status->is_detached = true;
        run_git_in_dir(cwd, "rev-parse --short HEAD", status->commit,
                       sizeof(status->commit), timeout_ms);
    }

    /* Check for detached HEAD explicitly */
    char head_ref[256] = {0};
    if (run_git_in_dir(cwd, "symbolic-ref HEAD", head_ref, sizeof(head_ref),
                       timeout_ms)) {
        status->is_detached = false;
    } else {
        status->is_detached = true;
    }

    /* Get short commit hash */
    if (status->commit[0] == '\0') {
        run_git_in_dir(cwd, "rev-parse --short HEAD", status->commit,
                       sizeof(status->commit), timeout_ms);
    }

    /* Get status counts using git status --porcelain */
    status->staged_count = 0;
    status->unstaged_count = 0;
    status->untracked_count = 0;

    char porcelain[8192] = {0};
    if (run_git_in_dir(cwd, "status --porcelain", porcelain,
                       sizeof(porcelain), timeout_ms)) {
        /* Parse porcelain output line by line */
        char *line = porcelain;
        while (*line) {
            if (line[0] == '?') {
                status->untracked_count++;
            } else if (line[0] != '\0' && line[1] != '\0') {
                if (line[0] != ' ' && line[0] != '?') {
                    status->staged_count++;
                }
                if (line[1] != ' ' && line[1] != '?') {
                    status->unstaged_count++;
                }
            }
            /* Advance to next line */
            char *nl = strchr(line, '\n');
            if (nl) {
                line = nl + 1;
            } else {
                break;
            }
        }
    }

    /* Check ahead/behind counts */
    char ahead_behind[64] = {0};
    if (run_git_in_dir(cwd,
                       "rev-list --left-right --count HEAD...@{upstream}",
                       ahead_behind, sizeof(ahead_behind), timeout_ms)) {
        sscanf(ahead_behind, "%d %d", &status->ahead, &status->behind);
    }

    /* Check for merge in progress using git rev-parse */
    char git_dir[512] = {0};
    if (run_git_in_dir(cwd, "rev-parse --git-dir", git_dir, sizeof(git_dir),
                       timeout_ms)) {
        /* Build absolute path if git_dir is relative */
        char merge_path[1024];
        char rebase_merge_path[1024];
        char rebase_apply_path[1024];

        if (git_dir[0] == '/') {
            snprintf(merge_path, sizeof(merge_path), "%s/MERGE_HEAD",
                     git_dir);
            snprintf(rebase_merge_path, sizeof(rebase_merge_path),
                     "%s/rebase-merge", git_dir);
            snprintf(rebase_apply_path, sizeof(rebase_apply_path),
                     "%s/rebase-apply", git_dir);
        } else {
            snprintf(merge_path, sizeof(merge_path), "%s/%s/MERGE_HEAD", cwd,
                     git_dir);
            snprintf(rebase_merge_path, sizeof(rebase_merge_path),
                     "%s/%s/rebase-merge", cwd, git_dir);
            snprintf(rebase_apply_path, sizeof(rebase_apply_path),
                     "%s/%s/rebase-apply", cwd, git_dir);
        }

        /* Use access() for non-blocking file existence check */
        if (access(merge_path, F_OK) == 0) {
            status->is_merging = true;
        }
        if (access(rebase_merge_path, F_OK) == 0 ||
            access(rebase_apply_path, F_OK) == 0) {
            status->is_rebasing = true;
        }
    }

    return LLE_SUCCESS;
}
