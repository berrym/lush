/**
 * Mock Memory Pool Header for LLE Testing
 *
 * Declares mock memory pool functions for standalone unit tests.
 *
 * IMPORTANT: This must be included BEFORE any LLE headers to avoid
 * type conflicts with the real memory management system.
 */

#ifndef TEST_MEMORY_MOCK_H
#define TEST_MEMORY_MOCK_H

// Prevent the real memory_management.h from being included
#define LLE_MEMORY_MANAGEMENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock memory pool type - simple stub
typedef struct lush_memory_pool_system_t {
    int dummy;
} lush_memory_pool_system_t;

typedef lush_memory_pool_system_t lush_memory_pool_t;
typedef lush_memory_pool_system_t lle_memory_pool_t;

// Mock optimizer type to satisfy performance.h dependencies
typedef struct lle_memory_optimizer {
    int dummy;
} lle_memory_optimizer_t;

// Global memory pool instance
extern lush_memory_pool_t *global_memory_pool;

// Mock memory pool functions
void *lle_pool_alloc(size_t size);
void lle_pool_free(void *ptr);
void *lush_pool_alloc(lush_memory_pool_t *pool, size_t size);
void lush_pool_free(lush_memory_pool_t *pool, void *ptr);

// LLE memory pool API
lle_memory_pool_t *lle_pool_create(void);
void lle_pool_destroy(lle_memory_pool_t *pool);
int lle_pool_init(lle_memory_pool_t **pool, size_t size);

#ifdef __cplusplus
}
#endif

#endif // TEST_MEMORY_MOCK_H
