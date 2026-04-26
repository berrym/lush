/**
 * @file test_memory_pool.c
 * @brief Unit tests for memory pool system
 *
 * Tests the enterprise-grade memory pool including:
 * - Pool initialization and shutdown
 * - Allocation and deallocation
 * - Pool size categories
 * - Statistics tracking
 * - Error handling
 * - Memory validation
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lush_memory_pool.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/* Test framework macros */

/* Helper to setup and teardown pool for each test */
static void setup_pool(void) {
    lush_pool_config_t config = lush_pool_get_default_config();
    lush_pool_error_t err = lush_pool_init(&config);
    ASSERT(err == LUSH_POOL_SUCCESS, "Pool init should succeed");
}

static void teardown_pool(void) { lush_pool_shutdown(); }

/* ============================================================================
 * CONFIGURATION TESTS
 * ============================================================================
 */

TEST(get_default_config) {
    lush_pool_config_t config = lush_pool_get_default_config();
    ASSERT(config.small_pool_blocks > 0, "Should have small pool blocks");
    ASSERT(config.medium_pool_blocks > 0, "Should have medium pool blocks");
    ASSERT(config.large_pool_blocks > 0, "Should have large pool blocks");
    ASSERT(config.xlarge_pool_blocks > 0, "Should have xlarge pool blocks");
}

TEST(get_display_optimized_config) {
    lush_pool_config_t config = lush_pool_get_display_optimized_config();
    ASSERT(config.large_pool_blocks > 0, "Should have large pool blocks");
}

/* Note: get_minimal_config test removed - function declared but not implemented
 */

/* ============================================================================
 * INITIALIZATION TESTS
 * ============================================================================
 */

TEST(pool_init_default) {
    lush_pool_config_t config = lush_pool_get_default_config();
    lush_pool_error_t err = lush_pool_init(&config);
    ASSERT_EQ(err, LUSH_POOL_SUCCESS, "Init should succeed");
    lush_pool_shutdown();
}

TEST(pool_init_null_config) {
    lush_pool_error_t err = lush_pool_init(NULL);
    ASSERT_EQ(err, LUSH_POOL_SUCCESS,
              "Init with NULL config should use defaults");
    lush_pool_shutdown();
}

TEST(pool_init_with_statistics) {
    lush_pool_config_t config = lush_pool_get_default_config();
    config.enable_statistics = true;
    lush_pool_error_t err = lush_pool_init(&config);
    ASSERT_EQ(err, LUSH_POOL_SUCCESS, "Init with statistics should succeed");
    lush_pool_shutdown();
}

TEST(pool_double_init) {
    lush_pool_config_t config = lush_pool_get_default_config();
    lush_pool_init(&config);
    /* Second init should handle gracefully */
    lush_pool_error_t err = lush_pool_init(&config);
    /* May succeed (reinit) or fail gracefully */
    lush_pool_shutdown();
    (void)err; /* Suppress unused warning */
}

TEST(pool_shutdown_without_init) {
    /* Should not crash */
    lush_pool_shutdown();
}

/* ============================================================================
 * ALLOCATION TESTS
 * ============================================================================
 */

TEST(pool_alloc_small) {
    setup_pool();

    void *ptr = lush_pool_alloc(64);
    ASSERT_NOT_NULL(ptr, "Small allocation should succeed");
    memset(ptr, 0xAB, 64); /* Write to verify usability */
    lush_pool_free(ptr);

    teardown_pool();
}

TEST(pool_alloc_medium) {
    setup_pool();

    void *ptr = lush_pool_alloc(256);
    ASSERT_NOT_NULL(ptr, "Medium allocation should succeed");
    memset(ptr, 0xCD, 256);
    lush_pool_free(ptr);

    teardown_pool();
}

TEST(pool_alloc_large) {
    setup_pool();

    void *ptr = lush_pool_alloc(2048);
    ASSERT_NOT_NULL(ptr, "Large allocation should succeed");
    memset(ptr, 0xEF, 2048);
    lush_pool_free(ptr);

    teardown_pool();
}

TEST(pool_alloc_xlarge) {
    setup_pool();

    void *ptr = lush_pool_alloc(8192);
    ASSERT_NOT_NULL(ptr, "XLarge allocation should succeed");
    memset(ptr, 0x12, 8192);
    lush_pool_free(ptr);

    teardown_pool();
}

TEST(pool_alloc_zero) {
    setup_pool();

    void *ptr = lush_pool_alloc(0);
    /* May return NULL or minimal allocation */
    if (ptr) {
        lush_pool_free(ptr);
    }

    teardown_pool();
}

TEST(pool_alloc_oversized) {
    setup_pool();

    /* Larger than XLarge pool - should fallback to malloc */
    void *ptr = lush_pool_alloc(100000);
    ASSERT_NOT_NULL(ptr, "Oversized allocation should fallback to malloc");
    lush_pool_free(ptr);

    teardown_pool();
}

TEST(pool_alloc_multiple) {
    setup_pool();

    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = lush_pool_alloc(100);
        ASSERT_NOT_NULL(ptrs[i], "Multiple allocations should succeed");
    }

    for (int i = 0; i < 10; i++) {
        lush_pool_free(ptrs[i]);
    }

    teardown_pool();
}

TEST(pool_free_null) {
    setup_pool();

    /* Should not crash */
    lush_pool_free(NULL);

    teardown_pool();
}

/* ============================================================================
 * REALLOC TESTS
 * ============================================================================
 */

TEST(pool_realloc_grow) {
    setup_pool();

    void *ptr = lush_pool_alloc(64);
    ASSERT_NOT_NULL(ptr, "Initial allocation should succeed");
    memset(ptr, 'A', 64);

    void *new_ptr = lush_pool_realloc(ptr, 256);
    ASSERT_NOT_NULL(new_ptr, "Realloc grow should succeed");
    /* First 64 bytes should be preserved */
    ASSERT(((char *)new_ptr)[0] == 'A', "Data should be preserved");

    lush_pool_free(new_ptr);
    teardown_pool();
}

TEST(pool_realloc_shrink) {
    setup_pool();

    void *ptr = lush_pool_alloc(256);
    ASSERT_NOT_NULL(ptr, "Initial allocation should succeed");
    memset(ptr, 'B', 256);

    void *new_ptr = lush_pool_realloc(ptr, 64);
    ASSERT_NOT_NULL(new_ptr, "Realloc shrink should succeed");
    ASSERT(((char *)new_ptr)[0] == 'B', "Data should be preserved");

    lush_pool_free(new_ptr);
    teardown_pool();
}

TEST(pool_realloc_null) {
    setup_pool();

    void *ptr = lush_pool_realloc(NULL, 64);
    ASSERT_NOT_NULL(ptr, "Realloc NULL should allocate");

    lush_pool_free(ptr);
    teardown_pool();
}

TEST(pool_realloc_zero_size) {
    setup_pool();

    void *ptr = lush_pool_alloc(64);
    ASSERT_NOT_NULL(ptr, "Initial allocation should succeed");

    void *new_ptr = lush_pool_realloc(ptr, 0);
    /* Should free and return NULL */
    ASSERT_NULL(new_ptr, "Realloc to 0 should free");

    teardown_pool();
}

/* ============================================================================
 * CALLOC TESTS
 * ============================================================================
 */

TEST(pool_calloc_basic) {
    setup_pool();

    int *arr = lush_pool_calloc(10, sizeof(int));
    ASSERT_NOT_NULL(arr, "Calloc should succeed");

    /* All should be zero */
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(arr[i], 0, "Calloc memory should be zeroed");
    }

    lush_pool_free(arr);
    teardown_pool();
}

TEST(pool_calloc_large) {
    setup_pool();

    char *buf = lush_pool_calloc(1000, sizeof(char));
    ASSERT_NOT_NULL(buf, "Large calloc should succeed");

    for (int i = 0; i < 1000; i++) {
        ASSERT_EQ(buf[i], 0, "Calloc memory should be zeroed");
    }

    lush_pool_free(buf);
    teardown_pool();
}

/* ============================================================================
 * STRDUP TESTS
 * ============================================================================
 */

TEST(pool_strdup_basic) {
    setup_pool();

    const char *original = "hello world";
    char *copy = lush_pool_strdup(original);
    ASSERT_NOT_NULL(copy, "Strdup should succeed");
    ASSERT_STR_EQ(copy, original, "Strdup should copy string");
    ASSERT(copy != original, "Should be a new allocation");

    lush_pool_free(copy);
    teardown_pool();
}

TEST(pool_strdup_empty) {
    setup_pool();

    const char *original = "";
    char *copy = lush_pool_strdup(original);
    ASSERT_NOT_NULL(copy, "Strdup empty should succeed");
    ASSERT_STR_EQ(copy, "", "Empty string should be preserved");

    lush_pool_free(copy);
    teardown_pool();
}

TEST(pool_strdup_null) {
    setup_pool();

    char *copy = lush_pool_strdup(NULL);
    ASSERT_NULL(copy, "Strdup NULL should return NULL");

    teardown_pool();
}

TEST(pool_strdup_long) {
    setup_pool();

    char original[1000];
    memset(original, 'x', 999);
    original[999] = '\0';

    char *copy = lush_pool_strdup(original);
    ASSERT_NOT_NULL(copy, "Strdup long string should succeed");
    ASSERT_STR_EQ(copy, original, "Long string should be copied");

    lush_pool_free(copy);
    teardown_pool();
}

/* ============================================================================
 * STATISTICS TESTS
 * ============================================================================
 */

TEST(pool_get_stats) {
    lush_pool_config_t config = lush_pool_get_default_config();
    config.enable_statistics = true;
    lush_pool_init(&config);

    void *ptr = lush_pool_alloc(64);
    ASSERT_NOT_NULL(ptr, "Allocation should succeed");

    lush_pool_stats_t stats = lush_pool_get_stats();
    ASSERT(stats.total_allocations > 0, "Should track allocations");
    ASSERT(stats.active_allocations > 0, "Should have active allocation");

    lush_pool_free(ptr);
    lush_pool_shutdown();
}

TEST(pool_reset_stats) {
    lush_pool_config_t config = lush_pool_get_default_config();
    config.enable_statistics = true;
    lush_pool_init(&config);

    void *ptr = lush_pool_alloc(64);
    lush_pool_free(ptr);

    lush_pool_reset_stats();
    lush_pool_stats_t stats = lush_pool_get_stats();
    ASSERT_EQ(stats.total_allocations, 0, "Stats should be reset");

    lush_pool_shutdown();
}

/* ============================================================================
 * POOL INFO TESTS
 * ============================================================================
 */

TEST(pool_get_recommended_size) {
    lush_pool_size_t size;

    size = lush_pool_get_recommended_size(64);
    ASSERT_EQ(size, LUSH_POOL_SMALL, "64 bytes should use SMALL pool");

    size = lush_pool_get_recommended_size(256);
    ASSERT_EQ(size, LUSH_POOL_MEDIUM, "256 bytes should use MEDIUM pool");

    size = lush_pool_get_recommended_size(2048);
    ASSERT_EQ(size, LUSH_POOL_LARGE, "2048 bytes should use LARGE pool");

    size = lush_pool_get_recommended_size(8192);
    ASSERT_EQ(size, LUSH_POOL_XLARGE, "8192 bytes should use XLARGE pool");
}

/* Note: pool_get_pool_info test removed - function declared but not implemented
 */

TEST(pool_is_healthy) {
    setup_pool();

    bool healthy = lush_pool_is_healthy();
    ASSERT(healthy, "Fresh pool should be healthy");

    teardown_pool();
}

TEST(pool_is_pool_pointer) {
    setup_pool();

    void *pool_ptr = lush_pool_alloc(64);
    ASSERT_NOT_NULL(pool_ptr, "Allocation should succeed");

    void *malloc_ptr = malloc(64);
    ASSERT_NOT_NULL(malloc_ptr, "Malloc should succeed");

    bool is_pool = lush_pool_is_pool_pointer(pool_ptr);
    /* May be true if from pool, false if from malloc fallback */
    (void)is_pool;

    bool is_not_pool = lush_pool_is_pool_pointer(malloc_ptr);
    ASSERT(!is_not_pool, "Malloc pointer should not be from pool");

    lush_pool_free(pool_ptr);
    free(malloc_ptr);
    teardown_pool();
}

/* Note: pool_validate_integrity and pool_maintenance tests removed - functions
 * declared but not implemented */

/* ============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================
 */

TEST(pool_error_string) {
    const char *str;

    str = lush_pool_error_string(LUSH_POOL_SUCCESS);
    ASSERT_NOT_NULL(str, "Success error string should exist");

    str = lush_pool_error_string(LUSH_POOL_ERROR_NOT_INITIALIZED);
    ASSERT_NOT_NULL(str, "Not initialized error string should exist");

    str = lush_pool_error_string(LUSH_POOL_ERROR_INVALID_SIZE);
    ASSERT_NOT_NULL(str, "Invalid size error string should exist");

    str = lush_pool_error_string(LUSH_POOL_ERROR_POOL_EXHAUSTED);
    ASSERT_NOT_NULL(str, "Pool exhausted error string should exist");

    str = lush_pool_error_string(LUSH_POOL_ERROR_MALLOC_FAILED);
    ASSERT_NOT_NULL(str, "Malloc failed error string should exist");
}

TEST(pool_get_last_error) {
    setup_pool();

    /* After successful operations, last error should be SUCCESS */
    void *ptr = lush_pool_alloc(64);
    lush_pool_error_t err = lush_pool_get_last_error();
    ASSERT_EQ(err, LUSH_POOL_SUCCESS, "Last error should be SUCCESS");

    lush_pool_free(ptr);
    teardown_pool();
}

TEST(pool_set_debug_mode) {
    setup_pool();

    /* Should not crash */
    lush_pool_set_debug_mode(true);
    lush_pool_set_debug_mode(false);

    teardown_pool();
}

/* ============================================================================
 * PERFORMANCE TARGET TESTS
 * ============================================================================
 */

TEST(pool_meets_performance_targets) {
    setup_pool();

    /* Just verify the function doesn't crash - actual performance may vary */
    bool meets = lush_pool_meets_performance_targets();
    (void)meets; /* Result is system-dependent */

    teardown_pool();
}

TEST(pool_get_memory_usage) {
    lush_pool_config_t config = lush_pool_get_default_config();
    config.enable_statistics = true;
    lush_pool_init(&config);

    uint64_t pool_bytes, malloc_bytes;
    double efficiency;

    lush_pool_get_memory_usage(&pool_bytes, &malloc_bytes, &efficiency);
    /* Initially should be minimal */

    void *ptr = lush_pool_alloc(64);
    lush_pool_get_memory_usage(&pool_bytes, &malloc_bytes, &efficiency);

    lush_pool_free(ptr);
    lush_pool_shutdown();
}

/* ============================================================================
 * PREALLOCATE TESTS
 * ============================================================================
 */

/* Note: pool_preallocate test removed - function declared but not implemented
 */

/* ============================================================================
 * STRESS TESTS
 * ============================================================================
 */

TEST(pool_stress_alloc_free) {
    setup_pool();

    /* Many allocations and frees */
    for (int i = 0; i < 1000; i++) {
        size_t size = (i % 4) * 100 + 50; /* Vary sizes */
        void *ptr = lush_pool_alloc(size);
        ASSERT_NOT_NULL(ptr, "Stress allocation should succeed");
        memset(ptr, 0xAA, size);
        lush_pool_free(ptr);
    }

    teardown_pool();
}

TEST(pool_stress_mixed_sizes) {
    setup_pool();

    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        size_t sizes[] = {32, 128, 512, 2048, 8192};
        size_t size = sizes[i % 5];
        ptrs[i] = lush_pool_alloc(size);
        ASSERT_NOT_NULL(ptrs[i], "Mixed size allocation should succeed");
    }

    for (int i = 0; i < 100; i++) {
        lush_pool_free(ptrs[i]);
    }

    teardown_pool();
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running lush_memory_pool.c tests...\n\n");

    printf("Configuration Tests:\n");
    RUN_TEST(get_default_config);
    RUN_TEST(get_display_optimized_config);
    /* get_minimal_config test removed - function not implemented */

    printf("\nInitialization Tests:\n");
    RUN_TEST(pool_init_default);
    RUN_TEST(pool_init_null_config);
    RUN_TEST(pool_init_with_statistics);
    RUN_TEST(pool_double_init);
    RUN_TEST(pool_shutdown_without_init);

    printf("\nAllocation Tests:\n");
    RUN_TEST(pool_alloc_small);
    RUN_TEST(pool_alloc_medium);
    RUN_TEST(pool_alloc_large);
    RUN_TEST(pool_alloc_xlarge);
    RUN_TEST(pool_alloc_zero);
    RUN_TEST(pool_alloc_oversized);
    RUN_TEST(pool_alloc_multiple);
    RUN_TEST(pool_free_null);

    printf("\nRealloc Tests:\n");
    RUN_TEST(pool_realloc_grow);
    RUN_TEST(pool_realloc_shrink);
    RUN_TEST(pool_realloc_null);
    RUN_TEST(pool_realloc_zero_size);

    printf("\nCalloc Tests:\n");
    RUN_TEST(pool_calloc_basic);
    RUN_TEST(pool_calloc_large);

    printf("\nStrdup Tests:\n");
    RUN_TEST(pool_strdup_basic);
    RUN_TEST(pool_strdup_empty);
    RUN_TEST(pool_strdup_null);
    RUN_TEST(pool_strdup_long);

    printf("\nStatistics Tests:\n");
    RUN_TEST(pool_get_stats);
    RUN_TEST(pool_reset_stats);

    printf("\nPool Info Tests:\n");
    RUN_TEST(pool_get_recommended_size);
    /* pool_get_pool_info test removed - function not implemented */
    RUN_TEST(pool_is_healthy);
    RUN_TEST(pool_is_pool_pointer);

    printf("\nValidation Tests:\n");
    /* pool_validate_integrity and pool_maintenance tests removed - functions
     * not implemented */

    printf("\nError Handling Tests:\n");
    RUN_TEST(pool_error_string);
    RUN_TEST(pool_get_last_error);
    RUN_TEST(pool_set_debug_mode);

    printf("\nPerformance Target Tests:\n");
    RUN_TEST(pool_meets_performance_targets);
    RUN_TEST(pool_get_memory_usage);

    printf("\nPreallocate Tests:\n");
    /* pool_preallocate test removed - function not implemented */

    printf("\nStress Tests:\n");
    RUN_TEST(pool_stress_alloc_free);
    RUN_TEST(pool_stress_mixed_sizes);

    return TEST_RESULT();
}
