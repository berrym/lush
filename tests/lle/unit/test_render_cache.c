/**
 * @file test_render_cache.c
 * @brief Unit tests for LLE render cache (display + render cache layers)
 *
 * Migrated to the shared test framework and strengthened:
 *   - The original tests for cache_lookup_hit only asserted "data is
 *     non-NULL and size > 0", which proves the lookup returned
 *     SOMETHING but not that it returned the RIGHT thing. Replaced
 *     with memcmp-based round-trip assertions.
 *   - Added overwrite-same-key, invalidate-then-restore, and
 *     invalidate-all-then-restore behavioral tests that the original
 *     suite did not cover.
 *   - Mock-pool stubs preserved (the cache routes through the LLE
 *     pool layer, which the test stubs back to malloc/free).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/display_integration.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "test_framework.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Mock memory pool and stubs
 *
 * The cache calls into LLE pool helpers which in turn defer to lush_pool_*.
 * These stubs route allocation to libc so the cache works against a
 * pointer-only "pool" that exists purely to satisfy the API contract.
 * ============================================================================
 */

static int mock_pool_dummy = 42;
static lle_memory_pool_t *mock_pool = (lle_memory_pool_t *)&mock_pool_dummy;

lush_memory_pool_system_t *global_memory_pool = NULL;

void *lush_pool_alloc(size_t size) { return malloc(size); }
void lush_pool_free(void *ptr) { free(ptr); }

lush_pool_config_t lush_pool_get_default_config(void) {
    lush_pool_config_t config = {0};
    return config;
}

lush_pool_error_t lush_pool_init(const lush_pool_config_t *config) {
    (void)config;
    return 0;
}

/* ============================================================================
 * lle_display_cache: lifecycle
 * ============================================================================
 */

TEST(display_cache_init_success) {
    lle_display_cache_t *cache = NULL;
    ASSERT_EQ(lle_display_cache_init(&cache, mock_pool), LLE_SUCCESS,
              "init returns SUCCESS");
    ASSERT_NOT_NULL(cache, "cache out-pointer populated");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_init_rejects_null_out) {
    ASSERT_EQ(lle_display_cache_init(NULL, mock_pool),
              LLE_ERROR_INVALID_PARAMETER, "NULL out -> INVALID_PARAMETER");
}

TEST(display_cache_init_rejects_null_pool) {
    lle_display_cache_t *cache = NULL;
    ASSERT_EQ(lle_display_cache_init(&cache, NULL), LLE_ERROR_INVALID_PARAMETER,
              "NULL pool -> INVALID_PARAMETER");
    ASSERT_NULL(cache, "cache stays NULL after rejected init");
}

TEST(display_cache_cleanup_rejects_null) {
    ASSERT_EQ(lle_display_cache_cleanup(NULL), LLE_ERROR_INVALID_PARAMETER,
              "cleanup(NULL) -> INVALID_PARAMETER");
}

TEST(display_cache_cleanup_succeeds) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    ASSERT_EQ(lle_display_cache_cleanup(cache), LLE_SUCCESS,
              "cleanup of init'd cache returns SUCCESS");
}

/* ============================================================================
 * lle_display_cache: store
 * ============================================================================
 */

TEST(display_cache_store_rejects_null_cache) {
    const char *data = "x";
    ASSERT_EQ(lle_display_cache_store(NULL, 1, data, 1),
              LLE_ERROR_INVALID_PARAMETER, "NULL cache");
}

TEST(display_cache_store_rejects_null_data) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    ASSERT_EQ(lle_display_cache_store(cache, 1, NULL, 5),
              LLE_ERROR_INVALID_PARAMETER, "NULL data");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_store_rejects_zero_size) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    const char *data = "x";
    ASSERT_EQ(lle_display_cache_store(cache, 1, data, 0),
              LLE_ERROR_INVALID_PARAMETER, "zero size");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_store_succeeds) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    const char *data = "stored";
    ASSERT_EQ(lle_display_cache_store(cache, 42, data, strlen(data)),
              LLE_SUCCESS, "store of valid input");
    lle_display_cache_cleanup(cache);
}

/* ============================================================================
 * lle_display_cache: lookup — including ROUND-TRIP DATA verification
 *
 * The original tests only asserted data != NULL and size > 0 for hits.
 * That proves the lookup returned something but does not prove it
 * returned the right thing. Real tests must verify the bytes.
 * ============================================================================
 */

TEST(display_cache_lookup_rejects_null_cache) {
    void *data = NULL;
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(NULL, 1, &data, &size),
              LLE_ERROR_INVALID_PARAMETER, "NULL cache");
}

TEST(display_cache_lookup_rejects_null_data_out) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 1, NULL, &size),
              LLE_ERROR_INVALID_PARAMETER, "NULL data out");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_lookup_rejects_null_size_out) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    void *data = NULL;
    ASSERT_EQ(lle_display_cache_lookup(cache, 1, &data, NULL),
              LLE_ERROR_INVALID_PARAMETER, "NULL size out");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_lookup_miss_returns_cache_miss) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    void *data = NULL;
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 999, &data, &size),
              LLE_ERROR_CACHE_MISS,
              "lookup of unstored key returns CACHE_MISS");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_store_then_lookup_returns_exact_data) {
    /* Store a known byte pattern, look it up, verify byte-for-byte equality.
     * This is the test the original suite was missing — proving the cache
     * actually caches the data, not just that it returns "something". */
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);

    const char *expected = "the quick brown fox";
    size_t expected_len = strlen(expected);
    ASSERT_EQ(lle_display_cache_store(cache, 100, expected, expected_len),
              LLE_SUCCESS, "store");

    void *retrieved = NULL;
    size_t retrieved_len = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 100, &retrieved, &retrieved_len),
              LLE_SUCCESS, "lookup hit");
    ASSERT_NOT_NULL(retrieved, "data pointer populated");
    ASSERT_EQ(retrieved_len, expected_len, "size matches what was stored");
    ASSERT_EQ(memcmp(retrieved, expected, expected_len), 0,
              "bytes match what was stored");

    lle_display_cache_cleanup(cache);
}

TEST(display_cache_store_then_lookup_with_binary_data) {
    /* Store a binary blob containing embedded NUL bytes — proves the
     * cache treats data as opaque bytes, not C strings. */
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);

    const unsigned char expected[] = {0x00, 0xFF, 0x42, 0x00, 0x7F, 0x80};
    size_t expected_len = sizeof(expected);
    ASSERT_EQ(lle_display_cache_store(cache, 200, expected, expected_len),
              LLE_SUCCESS, "store");

    void *retrieved = NULL;
    size_t retrieved_len = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 200, &retrieved, &retrieved_len),
              LLE_SUCCESS, "lookup hit");
    ASSERT_EQ(retrieved_len, expected_len, "size of binary blob matches");
    ASSERT_EQ(memcmp(retrieved, expected, expected_len), 0,
              "binary bytes round-trip exactly (including embedded NULs)");

    lle_display_cache_cleanup(cache);
}

TEST(display_cache_overwrite_same_key_returns_new_value) {
    /* Store, then store again under the same key with different bytes.
     * Lookup must return the NEW bytes, not the old ones. */
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);

    const char *first = "first";
    const char *second = "second-value";

    lle_display_cache_store(cache, 7, first, strlen(first));
    lle_display_cache_store(cache, 7, second, strlen(second));

    void *retrieved = NULL;
    size_t retrieved_len = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 7, &retrieved, &retrieved_len),
              LLE_SUCCESS, "lookup after overwrite");
    ASSERT_EQ(retrieved_len, strlen(second), "size is the new size");
    ASSERT_EQ(memcmp(retrieved, second, strlen(second)), 0,
              "retrieved bytes are the new value, not the old");

    lle_display_cache_cleanup(cache);
}

TEST(display_cache_independent_keys_round_trip_independently) {
    /// Storing under several distinct keys must not cross-contaminate.
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);

    const char *vals[] = {"alpha", "bravo", "charlie", "delta", "echo"};
    for (uint64_t i = 0; i < 5; i++) {
        lle_display_cache_store(cache, i, vals[i], strlen(vals[i]));
    }
    for (uint64_t i = 0; i < 5; i++) {
        void *retrieved = NULL;
        size_t retrieved_len = 0;
        ASSERT_EQ(
            lle_display_cache_lookup(cache, i, &retrieved, &retrieved_len),
            LLE_SUCCESS, "each key looks up");
        ASSERT_EQ(retrieved_len, strlen(vals[i]), "each size matches");
        ASSERT_EQ(memcmp(retrieved, vals[i], strlen(vals[i])), 0,
                  "each value matches its key");
    }
    lle_display_cache_cleanup(cache);
}

/* ============================================================================
 * lle_display_cache: invalidate / invalidate_all
 * ============================================================================
 */

TEST(display_cache_invalidate_rejects_null) {
    ASSERT_EQ(lle_display_cache_invalidate(NULL, 1),
              LLE_ERROR_INVALID_PARAMETER, "NULL cache");
}

TEST(display_cache_invalidate_removes_entry) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    const char *data = "to-be-removed";
    lle_display_cache_store(cache, 50, data, strlen(data));
    ASSERT_EQ(lle_display_cache_invalidate(cache, 50), LLE_SUCCESS,
              "invalidate of stored key returns SUCCESS");

    void *out = NULL;
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 50, &out, &size),
              LLE_ERROR_CACHE_MISS, "lookup after invalidate -> CACHE_MISS");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_invalidate_then_restore_works) {
    /* After invalidating a key, storing it again should make subsequent
     * lookups hit with the new value. Verifies invalidation does not put
     * the cache into a broken state. */
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    const char *first = "old";
    const char *second = "new";

    lle_display_cache_store(cache, 99, first, strlen(first));
    lle_display_cache_invalidate(cache, 99);
    lle_display_cache_store(cache, 99, second, strlen(second));

    void *out = NULL;
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 99, &out, &size), LLE_SUCCESS,
              "restored entry looks up");
    ASSERT_EQ(size, strlen(second), "size matches the restored value");
    ASSERT_EQ(memcmp(out, second, strlen(second)), 0,
              "bytes are the restored value");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_invalidate_all_rejects_null) {
    ASSERT_EQ(lle_display_cache_invalidate_all(NULL),
              LLE_ERROR_INVALID_PARAMETER, "NULL cache");
}

TEST(display_cache_invalidate_all_removes_every_entry) {
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    for (uint64_t i = 0; i < 5; i++) {
        char data[16];
        snprintf(data, sizeof(data), "v%" PRIu64, i);
        lle_display_cache_store(cache, i, data, strlen(data));
    }
    ASSERT_EQ(lle_display_cache_invalidate_all(cache), LLE_SUCCESS,
              "invalidate_all returns SUCCESS");
    for (uint64_t i = 0; i < 5; i++) {
        void *out = NULL;
        size_t size = 0;
        ASSERT_EQ(lle_display_cache_lookup(cache, i, &out, &size),
                  LLE_ERROR_CACHE_MISS,
                  "every previously-stored entry now misses");
    }
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_invalidate_all_then_restore_works) {
    /* invalidate_all on a populated cache, then re-store and look up,
     * must function identically to a fresh cache. */
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    lle_display_cache_store(cache, 1, "old1", 4);
    lle_display_cache_store(cache, 2, "old2", 4);
    lle_display_cache_invalidate_all(cache);

    lle_display_cache_store(cache, 3, "new3", 4);
    void *out = NULL;
    size_t size = 0;
    ASSERT_EQ(lle_display_cache_lookup(cache, 3, &out, &size), LLE_SUCCESS,
              "post-invalidate-all store looks up");
    ASSERT_EQ(memcmp(out, "new3", 4), 0, "post-invalidate-all data is correct");
    lle_display_cache_cleanup(cache);
}

TEST(display_cache_invalidate_all_on_empty_is_safe) {
    /// No assertions on state — just that the call does not crash.
    lle_display_cache_t *cache = NULL;
    lle_display_cache_init(&cache, mock_pool);
    ASSERT_EQ(lle_display_cache_invalidate_all(cache), LLE_SUCCESS,
              "invalidate_all on empty cache");
    lle_display_cache_cleanup(cache);
}

/* ============================================================================
 * lle_render_cache: lifecycle
 *
 * The render-level cache wraps the display-level one. The original suite
 * exercised only init/cleanup; we add the same here. Behavioral tests
 * for the render cache's higher-level API are out of scope here because
 * its semantics depend on render-state types which are themselves not
 * covered by this file's stubs.
 * ============================================================================
 */

TEST(render_cache_init_success) {
    lle_render_cache_t *cache = NULL;
    ASSERT_EQ(lle_render_cache_init(&cache, mock_pool), LLE_SUCCESS,
              "render cache init");
    ASSERT_NOT_NULL(cache, "cache pointer populated");
    lle_render_cache_cleanup(cache);
}

TEST(render_cache_init_rejects_null_out) {
    ASSERT_EQ(lle_render_cache_init(NULL, mock_pool),
              LLE_ERROR_INVALID_PARAMETER, "NULL out");
}

TEST(render_cache_cleanup_rejects_null) {
    ASSERT_EQ(lle_render_cache_cleanup(NULL), LLE_ERROR_INVALID_PARAMETER,
              "NULL cleanup");
}

/* ============================================================================
 * Main runner
 * ============================================================================
 */

int main(void) {
    printf("=== LLE Render Cache Tests ===\n\n");

    printf("--- display_cache lifecycle ---\n");
    RUN_TEST(display_cache_init_success);
    RUN_TEST(display_cache_init_rejects_null_out);
    RUN_TEST(display_cache_init_rejects_null_pool);
    RUN_TEST(display_cache_cleanup_rejects_null);
    RUN_TEST(display_cache_cleanup_succeeds);

    printf("\n--- display_cache store ---\n");
    RUN_TEST(display_cache_store_rejects_null_cache);
    RUN_TEST(display_cache_store_rejects_null_data);
    RUN_TEST(display_cache_store_rejects_zero_size);
    RUN_TEST(display_cache_store_succeeds);

    printf(
        "\n--- display_cache lookup (with data round-trip verification) ---\n");
    RUN_TEST(display_cache_lookup_rejects_null_cache);
    RUN_TEST(display_cache_lookup_rejects_null_data_out);
    RUN_TEST(display_cache_lookup_rejects_null_size_out);
    RUN_TEST(display_cache_lookup_miss_returns_cache_miss);
    RUN_TEST(display_cache_store_then_lookup_returns_exact_data);
    RUN_TEST(display_cache_store_then_lookup_with_binary_data);
    RUN_TEST(display_cache_overwrite_same_key_returns_new_value);
    RUN_TEST(display_cache_independent_keys_round_trip_independently);

    printf("\n--- display_cache invalidate ---\n");
    RUN_TEST(display_cache_invalidate_rejects_null);
    RUN_TEST(display_cache_invalidate_removes_entry);
    RUN_TEST(display_cache_invalidate_then_restore_works);
    RUN_TEST(display_cache_invalidate_all_rejects_null);
    RUN_TEST(display_cache_invalidate_all_removes_every_entry);
    RUN_TEST(display_cache_invalidate_all_then_restore_works);
    RUN_TEST(display_cache_invalidate_all_on_empty_is_safe);

    printf("\n--- render_cache lifecycle ---\n");
    RUN_TEST(render_cache_init_success);
    RUN_TEST(render_cache_init_rejects_null_out);
    RUN_TEST(render_cache_cleanup_rejects_null);

    return TEST_RESULT();
}
