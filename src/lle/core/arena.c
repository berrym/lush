/**
 * @file arena.c
 * @brief Hierarchical Arena Memory Allocator Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implementation of the arena-based memory allocator for LLE.
 * See include/lle/arena.h for API documentation.
 *
 * Integration:
 * - Arenas allocate chunks from lush_pool_alloc()
 * - Chunks are returned to lush_pool_free() on destroy
 * - No changes needed to the underlying pool system
 */

#include "lle/arena.h"
#include "lush_memory_pool.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/**
 * Align a size up to the specified alignment
 */
static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/**
 * Check if alignment is valid (power of 2)
 */
static inline bool is_power_of_two(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/**
 * Allocate a new chunk from lush pool
 *
 * @param min_data_size Minimum usable size needed in the chunk
 * @return New chunk, or NULL on failure
 */
static lle_arena_chunk_t *arena_alloc_chunk(size_t min_data_size) {
    /// Calculate total size including header
    size_t total_size = sizeof(lle_arena_chunk_t) + min_data_size;

    /// Round up to at least minimum chunk size
    if (total_size < LLE_ARENA_MIN_CHUNK_SIZE) {
        total_size = LLE_ARENA_MIN_CHUNK_SIZE;
    }

    /// Allocate from lush pool
    lle_arena_chunk_t *chunk = lush_pool_alloc(total_size);
    if (!chunk) {
        return NULL;
    }

    /// Initialize chunk
    chunk->next = NULL;
    chunk->size = total_size - sizeof(lle_arena_chunk_t);
    chunk->used = 0;

    return chunk;
}

/**
 * Free a chunk back to lush pool
 */
static void arena_free_chunk(lle_arena_chunk_t *chunk) {
    if (chunk) {
        lush_pool_free(chunk);
    }
}

/**
 * Free all chunks in a chain
 */
static void arena_free_all_chunks(lle_arena_chunk_t *first) {
    lle_arena_chunk_t *chunk = first;
    while (chunk) {
        lle_arena_chunk_t *next = chunk->next;
        arena_free_chunk(chunk);
        chunk = next;
    }
}

/**
 * Unlink arena from parent's child list
 */
static void arena_unlink_from_parent(lle_arena_t *arena) {
    if (!arena || !arena->parent) {
        return;
    }

    lle_arena_t *parent = arena->parent;

    /// Is this the first child?
    if (parent->first_child == arena) {
        parent->first_child = arena->next_sibling;
    } else {
        /// Find previous sibling
        lle_arena_t *prev = parent->first_child;
        while (prev && prev->next_sibling != arena) {
            prev = prev->next_sibling;
        }
        if (prev) {
            prev->next_sibling = arena->next_sibling;
        }
    }

    arena->parent = NULL;
    arena->next_sibling = NULL;
}

/**
 * Link arena as child of parent
 */
static void arena_link_to_parent(lle_arena_t *arena, lle_arena_t *parent) {
    if (!arena || !parent) {
        return;
    }

    arena->parent = parent;
    arena->next_sibling = parent->first_child;
    parent->first_child = arena;
}

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

lle_arena_t *lle_arena_create(lle_arena_t *parent, const char *name,
                              size_t initial_size) {
    return lle_arena_create_with_flags(parent, name, initial_size,
                                       LLE_ARENA_FLAG_NONE);
}

lle_arena_t *lle_arena_create_with_flags(lle_arena_t *parent, const char *name,
                                         size_t initial_size, uint32_t flags) {
    /// Determine chunk size
    if (initial_size == 0) {
        initial_size = LLE_ARENA_DEFAULT_CHUNK_SIZE;
    }

    /* Allocate initial chunk (includes space for arena struct if we
     * want to be clever, but for simplicity we allocate separately) */
    lle_arena_chunk_t *chunk = arena_alloc_chunk(initial_size);
    if (!chunk) {
        return NULL;
    }

    /// Allocate arena structure from the chunk itself for locality
    if (chunk->size < sizeof(lle_arena_t)) {
        /// Chunk too small for arena struct, allocate separately
        arena_free_chunk(chunk);
        return NULL;
    }

    /// Place arena struct at start of chunk data
    lle_arena_t *arena = (lle_arena_t *)chunk->data;
    chunk->used = align_up(sizeof(lle_arena_t), LLE_ARENA_DEFAULT_ALIGNMENT);

    /// Initialize arena
    arena->name = name;
    arena->parent = NULL;
    arena->first_child = NULL;
    arena->next_sibling = NULL;
    arena->first_chunk = chunk;
    arena->current_chunk = chunk;
    arena->default_chunk_size = initial_size;
    arena->alignment = LLE_ARENA_DEFAULT_ALIGNMENT;
    arena->flags = flags;

#if LLE_ARENA_STATS
    arena->total_allocated = sizeof(lle_arena_t);
    arena->allocation_count = 1; /// Count arena struct itself
    arena->chunk_count = 1;
    arena->peak_usage = chunk->used;
#endif

    /// Link to parent
    if (parent) {
        arena_link_to_parent(arena, parent);
    }

    return arena;
}

void lle_arena_destroy(lle_arena_t *arena) {
    if (!arena) {
        return;
    }

    /// Recursively destroy all children first (depth-first)
    while (arena->first_child) {
        lle_arena_destroy(arena->first_child);
    }

    /// Unlink from parent
    arena_unlink_from_parent(arena);

    /// Save first_chunk before freeing (arena struct is in first chunk)
    lle_arena_chunk_t *first_chunk = arena->first_chunk;

    /// Clear arena to help catch use-after-free in debug builds
#ifndef NDEBUG
    arena->name = NULL;
    arena->first_chunk = NULL;
    arena->current_chunk = NULL;
#endif

    /// Free all chunks (including the one containing the arena struct)
    arena_free_all_chunks(first_chunk);
}

void lle_arena_reset(lle_arena_t *arena) {
    if (!arena) {
        return;
    }

    /// Reset all chunks to empty
    lle_arena_chunk_t *chunk = arena->first_chunk;
    bool first = true;
    while (chunk) {
        if (first) {
            /// First chunk contains arena struct, keep that
            chunk->used =
                align_up(sizeof(lle_arena_t), LLE_ARENA_DEFAULT_ALIGNMENT);
            first = false;
        } else {
            chunk->used = 0;
        }
        chunk = chunk->next;
    }

    /// Reset to first chunk
    arena->current_chunk = arena->first_chunk;

#if LLE_ARENA_STATS
    arena->total_allocated = sizeof(lle_arena_t);
    arena->allocation_count = 1;
    /// Keep chunk_count - chunks are still allocated
#endif
}

void lle_arena_destroy_children(lle_arena_t *arena) {
    if (!arena) {
        return;
    }

    while (arena->first_child) {
        lle_arena_destroy(arena->first_child);
    }
}

/* ============================================================================
 * ALLOCATION FUNCTIONS
 * ============================================================================
 */

void *lle_arena_alloc(lle_arena_t *arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }

    /// Align size
    size_t aligned_size = align_up(size, arena->alignment);

    /// Try to allocate from current chunk
    lle_arena_chunk_t *chunk = arena->current_chunk;
    if (chunk->used + aligned_size <= chunk->size) {
        void *ptr = chunk->data + chunk->used;
        chunk->used += aligned_size;

#if LLE_ARENA_STATS
        arena->total_allocated += size;
        arena->allocation_count++;
        if (chunk->used > arena->peak_usage) {
            arena->peak_usage = chunk->used;
        }
#endif

        return ptr;
    }

    /// Need a new chunk
    if (arena->flags & LLE_ARENA_FLAG_NO_GROW) {
        return NULL; /// Not allowed to grow
    }

    /// Allocate new chunk - at least default size or requested size
    size_t new_chunk_size = aligned_size > arena->default_chunk_size
                                ? aligned_size
                                : arena->default_chunk_size;

    lle_arena_chunk_t *new_chunk = arena_alloc_chunk(new_chunk_size);
    if (!new_chunk) {
        return NULL;
    }

    /// Link new chunk at the front (so current_chunk is always newest)
    new_chunk->next = arena->first_chunk;
    arena->first_chunk = new_chunk;
    arena->current_chunk = new_chunk;

#if LLE_ARENA_STATS
    arena->chunk_count++;
#endif

    /// Allocate from new chunk
    void *ptr = new_chunk->data;
    new_chunk->used = aligned_size;

#if LLE_ARENA_STATS
    arena->total_allocated += size;
    arena->allocation_count++;
#endif

    return ptr;
}

void *lle_arena_calloc(lle_arena_t *arena, size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    /// Check for overflow
    size_t total = count * size;
    if (total / count != size) {
        return NULL; /// Overflow
    }

    void *ptr = lle_arena_alloc(arena, total);
    if (ptr) {
        memset(ptr, 0, total);
    }

    return ptr;
}

void *lle_arena_alloc_aligned(lle_arena_t *arena, size_t size,
                              size_t alignment) {
    if (!arena || size == 0 || !is_power_of_two(alignment)) {
        return NULL;
    }

    /* If requested alignment is less than or equal to default, use normal
     * alloc */
    if (alignment <= arena->alignment) {
        return lle_arena_alloc(arena, size);
    }

    /// Need extra space for alignment padding
    size_t padding = alignment - 1;
    size_t total_size = size + padding;

    /// Allocate with extra space
    void *raw_ptr = lle_arena_alloc(arena, total_size);
    if (!raw_ptr) {
        return NULL;
    }

    /// Align the pointer
    uintptr_t addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);

    return (void *)aligned_addr;
}

char *lle_arena_strdup(lle_arena_t *arena, const char *str) {
    if (!arena || !str) {
        return NULL;
    }

    size_t len = strlen(str);
    char *dup = lle_arena_alloc(arena, len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }

    return dup;
}

char *lle_arena_strndup(lle_arena_t *arena, const char *str, size_t max_len) {
    if (!arena || !str) {
        return NULL;
    }

    /// Find actual length (up to max_len)
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }

    char *dup = lle_arena_alloc(arena, len + 1);
    if (dup) {
        memcpy(dup, str, len);
        dup[len] = '\0';
    }

    return dup;
}

void *lle_arena_memdup(lle_arena_t *arena, const void *src, size_t size) {
    if (!arena || !src || size == 0) {
        return NULL;
    }

    void *dup = lle_arena_alloc(arena, size);
    if (dup) {
        memcpy(dup, src, size);
    }

    return dup;
}

char *lle_arena_sprintf(lle_arena_t *arena, const char *format, ...) {
    if (!arena || !format) {
        return NULL;
    }

    va_list args1, args2;
    va_start(args1, format);
    va_copy(args2, args1);

    /// Determine required size
    int len = vsnprintf(NULL, 0, format, args1);
    va_end(args1);

    if (len < 0) {
        va_end(args2);
        return NULL;
    }

    /// Allocate and format
    char *str = lle_arena_alloc(arena, (size_t)len + 1);
    if (str) {
        vsnprintf(str, (size_t)len + 1, format, args2);
    }

    va_end(args2);
    return str;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

size_t lle_arena_get_allocated(lle_arena_t *arena, bool include_children) {
    if (!arena) {
        return 0;
    }

#if LLE_ARENA_STATS
    size_t total = arena->total_allocated;

    if (include_children) {
        lle_arena_t *child = arena->first_child;
        while (child) {
            total += lle_arena_get_allocated(child, true);
            child = child->next_sibling;
        }
    }

    return total;
#else
    (void)include_children;
    return 0; /// Stats disabled
#endif
}

size_t lle_arena_get_total_size(lle_arena_t *arena, bool include_children) {
    if (!arena) {
        return 0;
    }

    size_t total = 0;

    /// Sum up chunk sizes
    lle_arena_chunk_t *chunk = arena->first_chunk;
    while (chunk) {
        total += sizeof(lle_arena_chunk_t) + chunk->size;
        chunk = chunk->next;
    }

    if (include_children) {
        lle_arena_t *child = arena->first_child;
        while (child) {
            total += lle_arena_get_total_size(child, true);
            child = child->next_sibling;
        }
    }

    return total;
}

size_t lle_arena_get_chunk_count(lle_arena_t *arena) {
    if (!arena) {
        return 0;
    }

#if LLE_ARENA_STATS
    return arena->chunk_count;
#else
    size_t count = 0;
    lle_arena_chunk_t *chunk = arena->first_chunk;
    while (chunk) {
        count++;
        chunk = chunk->next;
    }
    return count;
#endif
}

size_t lle_arena_get_child_count(lle_arena_t *arena, bool recursive) {
    if (!arena) {
        return 0;
    }

    size_t count = 0;
    lle_arena_t *child = arena->first_child;
    while (child) {
        count++;
        if (recursive) {
            count += lle_arena_get_child_count(child, true);
        }
        child = child->next_sibling;
    }

    return count;
}

bool lle_arena_has_space(lle_arena_t *arena, size_t size) {
    if (!arena) {
        return false;
    }

    size_t aligned_size = align_up(size, arena->alignment);
    return arena->current_chunk->used + aligned_size <=
           arena->current_chunk->size;
}

void lle_arena_print_stats(lle_arena_t *arena, int indent) {
    if (!arena) {
        return;
    }

    /// Print indentation
    for (int i = 0; i < indent; i++) {
        fprintf(stderr, "  ");
    }

#if LLE_ARENA_STATS
    fprintf(stderr, "Arena '%s': %zu bytes allocated, %zu allocs, %zu chunks\n",
            arena->name ? arena->name : "(unnamed)", arena->total_allocated,
            arena->allocation_count, arena->chunk_count);
#else
    fprintf(stderr, "Arena '%s': (stats disabled)\n",
            arena->name ? arena->name : "(unnamed)");
#endif

    /// Print children
    lle_arena_t *child = arena->first_child;
    while (child) {
        lle_arena_print_stats(child, indent + 1);
        child = child->next_sibling;
    }
}

void lle_arena_print_tree(lle_arena_t *root) {
    if (!root) {
        fprintf(stderr, "Arena tree: (null)\n");
        return;
    }

    fprintf(stderr, "=== Arena Tree ===\n");
    lle_arena_print_stats(root, 0);

    size_t total_size = lle_arena_get_total_size(root, true);
    size_t total_allocated = lle_arena_get_allocated(root, true);
    size_t child_count = lle_arena_get_child_count(root, true);

    fprintf(stderr, "==================\n");
    fprintf(stderr,
            "Total: %zu bytes used, %zu bytes allocated, %zu children\n",
            total_size, total_allocated, child_count);
}

/* ============================================================================
 * SCRATCH / TEMPORARY ALLOCATION
 * ============================================================================
 */

lle_arena_scratch_t lle_arena_scratch_begin(lle_arena_t *arena) {
    lle_arena_scratch_t scratch = {0};

    if (arena) {
        scratch.arena = arena;
        scratch.chunk = arena->current_chunk;
        scratch.chunk_used = arena->current_chunk->used;
    }

    return scratch;
}

void lle_arena_scratch_end(lle_arena_scratch_t *scratch) {
    if (!scratch || !scratch->arena) {
        return;
    }

    lle_arena_t *arena = scratch->arena;

    /// Free any chunks allocated after scratch began
    while (arena->first_chunk != scratch->chunk) {
        lle_arena_chunk_t *chunk = arena->first_chunk;
        arena->first_chunk = chunk->next;
        arena_free_chunk(chunk);
#if LLE_ARENA_STATS
        arena->chunk_count--;
#endif
    }

    /// Reset current chunk to scratch state
    arena->current_chunk = scratch->chunk;
    if (scratch->chunk) {
        scratch->chunk->used = scratch->chunk_used;
    }

    /// Clear scratch marker
    scratch->arena = NULL;
    scratch->chunk = NULL;
    scratch->chunk_used = 0;
}
