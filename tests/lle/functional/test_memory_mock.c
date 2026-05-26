/**
 * Mock Memory Pool for LLE Testing
 *
 * Provides simple malloc/free based implementations of memory pool
 * functions so buffer tests can run standalone without full lush.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/// Mock memory pool type
typedef struct lush_memory_pool_system_t {
    int dummy;
} lush_memory_pool_system_t;

typedef lush_memory_pool_system_t lush_memory_pool_t;

/// Global memory pool (unused in mock)
static lush_memory_pool_system_t global_pool_instance = {0};
lush_memory_pool_t *global_memory_pool = &global_pool_instance;

/// Mock memory pool functions - just use system malloc/free
void *lle_pool_alloc(size_t size) { return malloc(size); }

void lle_pool_free(void *ptr) { free(ptr); }

void *lush_pool_alloc(lush_memory_pool_t *pool, size_t size) {
    (void)pool; /// Unused
    return malloc(size);
}

void lush_pool_free(lush_memory_pool_t *pool, void *ptr) {
    (void)pool; /// Unused
    free(ptr);
}

/// Mock memory pool config
typedef struct lush_memory_pool_config_t {
    size_t initial_size;
} lush_memory_pool_config_t;

lush_memory_pool_config_t *lush_pool_get_default_config(void) {
    static lush_memory_pool_config_t config = {4096};
    return &config;
}

int lush_pool_init(lush_memory_pool_t *pool,
                   lush_memory_pool_config_t *config) {
    (void)pool;
    (void)config;
    return 0;
}

/// LLE memory pool API
typedef lush_memory_pool_t lle_memory_pool_t;

lle_memory_pool_t *lle_pool_create(void) { return &global_pool_instance; }

void lle_pool_destroy(lle_memory_pool_t *pool) { (void)pool; }

int lle_pool_init(lle_memory_pool_t **pool, size_t size) {
    (void)size;
    *pool = &global_pool_instance;
    return 0;
}
