/**
 * @file spec_15_memory_management_compliance.c
 * @brief Spec 15 memory-management behavioral compliance test
 *
 * Exercises the LLE pool allocator (lle_pool_alloc / lle_pool_free) and
 * asserts its behavior. This is the LLE-side pool API; the Lush-side pool
 * (lush_pool_*) is covered separately by tests/unit/test_memory_pool.c.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/memory_management.h"
#include <stdio.h>
#include <stdlib.h>

static int assertions_passed = 0;

#define COMPLIANCE_ASSERT(condition, message)                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "COMPLIANCE VIOLATION: %s\n", message);            \
            fprintf(stderr, "   at %s:%d\n", __FILE__, __LINE__);              \
            exit(1);                                                           \
        }                                                                      \
        assertions_passed++;                                                   \
    } while (0)

/// A zero-size request returns NULL; a real request returns usable memory.
static void test_alloc_basic(void) {
    COMPLIANCE_ASSERT(lle_pool_alloc(0) == NULL,
                      "a zero-size allocation returns NULL");

    void *ptr = lle_pool_alloc(1024);
    COMPLIANCE_ASSERT(ptr != NULL, "a 1 KiB allocation succeeds");
    lle_pool_free(ptr);
}

/// free tolerates NULL and releases a real allocation.
static void test_free_basic(void) {
    lle_pool_free(NULL); /// must not crash

    void *ptr = lle_pool_alloc(512);
    COMPLIANCE_ASSERT(ptr != NULL, "a 512 B allocation succeeds");
    lle_pool_free(ptr);
}

/// Concurrent allocations return distinct, non-overlapping pointers.
static void test_alloc_multiple(void) {
    void *a = lle_pool_alloc(256);
    void *b = lle_pool_alloc(512);
    void *c = lle_pool_alloc(1024);
    COMPLIANCE_ASSERT(a != NULL && b != NULL && c != NULL,
                      "three allocations all succeed");
    COMPLIANCE_ASSERT(a != b && b != c && a != c,
                      "distinct allocations return distinct pointers");
    lle_pool_free(a);
    lle_pool_free(b);
    lle_pool_free(c);
}

/// The allocator serves a range of sizes.
static void test_alloc_sizes(void) {
    size_t sizes[] = {1, 4096, 65536};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        void *ptr = lle_pool_alloc(sizes[i]);
        COMPLIANCE_ASSERT(ptr != NULL, "allocation of a valid size succeeds");
        lle_pool_free(ptr);
    }
}

int main(void) {
    printf("Spec 15 Memory Management Behavioral Compliance\n");
    printf("===============================================\n\n");

    test_alloc_basic();
    test_free_basic();
    test_alloc_multiple();
    test_alloc_sizes();

    printf("Spec 15 behavioral compliance: %d assertions passed\n",
           assertions_passed);
    return 0;
}
