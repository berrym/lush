/**
 * @file test_async_worker.c
 * @brief Unit tests for LLE Async Worker Thread Pool
 *
 * Tests cover:
 * - Worker lifecycle (init/start/shutdown/destroy)
 * - Request creation and submission
 * - Git status provider
 * - Completion callbacks
 * - Error handling
 * - Statistics tracking
 *
 * SPECIFICATION: docs/lle_specification/25_prompt_theme_system_complete.md
 * SECTION: 7 - Async Operations
 */

#include "lle/async_worker.h"
#include "lle/error_handling.h"
#include "test_framework.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Callback state tracking — shared by completion-callback tests */
static pthread_mutex_t callback_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
static int callback_count = 0;
static lle_async_response_t last_response;
static bool response_received = false;

/* ========================================================================== */
/*                          TEST HELPER FUNCTIONS                             */
/* ========================================================================== */

static void reset_callback_state(void) {
    pthread_mutex_lock(&callback_mutex);
    callback_count = 0;
    response_received = false;
    memset(&last_response, 0, sizeof(last_response));
    pthread_mutex_unlock(&callback_mutex);
}

static void test_completion_callback(const lle_async_response_t *response,
                                     void *user_data) {
    (void)user_data;

    pthread_mutex_lock(&callback_mutex);
    callback_count++;
    memcpy(&last_response, response, sizeof(last_response));
    response_received = true;
    pthread_cond_signal(&callback_cond);
    pthread_mutex_unlock(&callback_mutex);
}

static bool wait_for_response(int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&callback_mutex);
    while (!response_received) {
        int rc = pthread_cond_timedwait(&callback_cond, &callback_mutex, &ts);
        if (rc != 0) {
            pthread_mutex_unlock(&callback_mutex);
            return false;
        }
    }
    pthread_mutex_unlock(&callback_mutex);
    return true;
}

/* ========================================================================== */
/*                              UNIT TESTS                                    */
/* ========================================================================== */

/* -------------------------------------------------------------------------- */
/*                          LIFECYCLE TESTS                                   */
/* -------------------------------------------------------------------------- */

TEST(worker_init_null_output) {
    lle_result_t result = lle_async_worker_init(NULL, NULL, NULL);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER,
              "Init with NULL output should fail");
}

TEST(worker_init_success) {
    lle_async_worker_t *worker = NULL;
    lle_result_t result = lle_async_worker_init(&worker, NULL, NULL);

    ASSERT_EQ(result, LLE_SUCCESS, "Init should succeed");
    ASSERT_NOT_NULL(worker, "Worker should be created");
    ASSERT_FALSE(lle_async_worker_is_running(worker),
                 "Worker should not be running after init");

    lle_async_worker_destroy(worker);
}

TEST(worker_start_success) {
    lle_async_worker_t *worker = NULL;
    lle_result_t result = lle_async_worker_init(&worker, NULL, NULL);
    ASSERT_EQ(result, LLE_SUCCESS, "Init should succeed");

    result = lle_async_worker_start(worker);
    ASSERT_EQ(result, LLE_SUCCESS, "Start should succeed");
    ASSERT_TRUE(lle_async_worker_is_running(worker),
                "Worker should be running after start");

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

TEST(worker_double_start_fails) {
    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, NULL, NULL);
    lle_async_worker_start(worker);

    lle_result_t result = lle_async_worker_start(worker);
    ASSERT_EQ(result, LLE_ERROR_INVALID_PARAMETER, "Double start should fail");

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

TEST(worker_shutdown_and_wait) {
    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, NULL, NULL);
    lle_async_worker_start(worker);

    lle_result_t result = lle_async_worker_shutdown(worker);
    ASSERT_EQ(result, LLE_SUCCESS, "Shutdown should succeed");

    result = lle_async_worker_wait(worker);
    ASSERT_EQ(result, LLE_SUCCESS, "Wait should succeed");

    ASSERT_FALSE(lle_async_worker_is_running(worker),
                 "Worker should not be running after wait");

    lle_async_worker_destroy(worker);
}

TEST(worker_destroy_null_safe) {
    lle_result_t result = lle_async_worker_destroy(NULL);
    ASSERT_EQ(result, LLE_SUCCESS, "Destroy NULL should succeed");
}

/* -------------------------------------------------------------------------- */
/*                          REQUEST TESTS                                     */
/* -------------------------------------------------------------------------- */

TEST(request_create_git_status) {
    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);

    ASSERT_NOT_NULL(req, "Request should be created");
    ASSERT_EQ(req->type, LLE_ASYNC_GIT_STATUS, "Type should be git status");
    ASSERT_EQ(req->timeout_ms, LLE_ASYNC_DEFAULT_TIMEOUT_MS,
              "Timeout should be default");

    lle_async_request_free(req);
}

TEST(request_free_null_safe) {
    lle_async_request_free(NULL); /* Should not crash */
}

TEST(submit_to_stopped_worker_fails) {
    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, NULL, NULL);
    /* Not started */

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    lle_result_t result = lle_async_worker_submit(worker, req);

    ASSERT_EQ(result, LLE_ERROR_INVALID_STATE,
              "Submit to stopped worker should fail");

    lle_async_request_free(req);
    lle_async_worker_destroy(worker);
}

TEST(submit_after_shutdown_fails) {
    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, NULL, NULL);
    lle_async_worker_start(worker);
    lle_async_worker_shutdown(worker);

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    lle_result_t result = lle_async_worker_submit(worker, req);

    ASSERT_EQ(result, LLE_ERROR_INVALID_STATE,
              "Submit after shutdown should fail");

    lle_async_request_free(req);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

/* -------------------------------------------------------------------------- */
/*                        CALLBACK TESTS                                      */
/* -------------------------------------------------------------------------- */

TEST(callback_invoked_on_completion) {
    reset_callback_state();

    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, test_completion_callback, NULL);
    lle_async_worker_start(worker);

    /* Get current directory for the request */
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    snprintf(req->cwd, sizeof(req->cwd), "%s", cwd);

    lle_result_t result = lle_async_worker_submit(worker, req);
    ASSERT_EQ(result, LLE_SUCCESS, "Submit should succeed");

    /* Wait for callback */
    bool received = wait_for_response(5000);
    ASSERT_TRUE(received, "Should receive response within timeout");

    pthread_mutex_lock(&callback_mutex);
    ASSERT_EQ(callback_count, 1, "Callback should be called once");
    ASSERT_EQ(last_response.result, LLE_SUCCESS, "Result should be success");
    pthread_mutex_unlock(&callback_mutex);

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

TEST(git_status_detects_repo) {
    reset_callback_state();

    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, test_completion_callback, NULL);
    lle_async_worker_start(worker);

    /* Use parent of build directory (should be the git repo) */
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    /* If we're in builddir, go up one level to the repo root */
    char *builddir = strstr(cwd, "/build");
    if (builddir) {
        *builddir = '\0';
    }

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    snprintf(req->cwd, sizeof(req->cwd), "%s", cwd);

    lle_async_worker_submit(worker, req);
    wait_for_response(5000);

    pthread_mutex_lock(&callback_mutex);
    /* Assuming tests run from lush git repo */
    ASSERT_TRUE(last_response.data.git_status.is_git_repo,
                "Should detect git repo");
    ASSERT_TRUE(strlen(last_response.data.git_status.branch) > 0 ||
                    last_response.data.git_status.is_detached,
                "Should have branch or be detached");
    pthread_mutex_unlock(&callback_mutex);

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

TEST(git_status_non_repo) {
    reset_callback_state();

    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, test_completion_callback, NULL);
    lle_async_worker_start(worker);

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    snprintf(req->cwd, sizeof(req->cwd), "/tmp");

    lle_async_worker_submit(worker, req);
    bool received = wait_for_response(10000);

    if (!received) {
        /* Timeout - force shutdown and skip assertions */
        lle_async_worker_shutdown(worker);
        lle_async_worker_wait(worker);
        lle_async_worker_destroy(worker);
        ASSERT_TRUE(false, "Timed out waiting for response");
        return;
    }

    pthread_mutex_lock(&callback_mutex);
    ASSERT_EQ(last_response.result, LLE_SUCCESS, "Result should be success");
    ASSERT_FALSE(last_response.data.git_status.is_git_repo,
                 "/tmp should not be a git repo");
    pthread_mutex_unlock(&callback_mutex);

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

/* -------------------------------------------------------------------------- */
/*                        STATISTICS TESTS                                    */
/* -------------------------------------------------------------------------- */

TEST(statistics_tracking) {
    reset_callback_state();

    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, test_completion_callback, NULL);
    lle_async_worker_start(worker);

    uint64_t total_req, total_comp, total_timeout;
    lle_async_worker_get_stats(worker, &total_req, &total_comp, &total_timeout);
    ASSERT_EQ(total_req, 0, "Initial requests should be 0");
    ASSERT_EQ(total_comp, 0, "Initial completed should be 0");

    /* Submit a request */
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    lle_async_request_t *req = lle_async_request_create(LLE_ASYNC_GIT_STATUS);
    snprintf(req->cwd, sizeof(req->cwd), "%s", cwd);
    lle_async_worker_submit(worker, req);

    lle_async_worker_get_stats(worker, &total_req, NULL, NULL);
    ASSERT_EQ(total_req, 1, "Should have 1 request after submit");

    wait_for_response(5000);

    lle_async_worker_get_stats(worker, NULL, &total_comp, NULL);
    ASSERT_EQ(total_comp, 1, "Should have 1 completed after response");

    lle_async_worker_shutdown(worker);
    lle_async_worker_wait(worker);
    lle_async_worker_destroy(worker);
}

TEST(pending_count) {
    lle_async_worker_t *worker = NULL;
    lle_async_worker_init(&worker, NULL, NULL);

    size_t pending = lle_async_worker_pending_count(worker);
    ASSERT_EQ(pending, 0, "Initial pending should be 0");

    lle_async_worker_destroy(worker);
}

/* ========================================================================== */
/*                              TEST RUNNER                                   */
/* ========================================================================== */

int main(void) {
    printf("=== LLE Async Worker Unit Tests ===\n\n");

    printf("--- Lifecycle ---\n");
    RUN_TEST(worker_init_null_output);
    RUN_TEST(worker_init_success);
    RUN_TEST(worker_start_success);
    RUN_TEST(worker_double_start_fails);
    RUN_TEST(worker_shutdown_and_wait);
    RUN_TEST(worker_destroy_null_safe);

    printf("\n--- Request ---\n");
    RUN_TEST(request_create_git_status);
    RUN_TEST(request_free_null_safe);
    RUN_TEST(submit_to_stopped_worker_fails);
    RUN_TEST(submit_after_shutdown_fails);

    printf("\n--- Callbacks (real task submission + result delivery) ---\n");
    RUN_TEST(callback_invoked_on_completion);
    RUN_TEST(git_status_detects_repo);
    RUN_TEST(git_status_non_repo);

    printf("\n--- Statistics ---\n");
    RUN_TEST(statistics_tracking);
    RUN_TEST(pending_count);

    return TEST_RESULT();
}
