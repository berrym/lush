/**
 * @file arena.h
 * @brief Hierarchical Arena Memory Allocator for LLE
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This file implements a hierarchical arena (region-based) memory allocator
 * that provides automatic lifetime-scoped cleanup. Arenas match natural
 * program scopes (session -> edit -> event -> frame), eliminating the need
 * for individual frees.
 *
 * Key Properties:
 * - O(1) allocation (bump pointer)
 * - O(1) destruction (free all chunks)
 * - No individual frees needed
 * - Child arenas freed when parent freed
 * - Integrates with existing lush_pool_alloc() for chunks
 *
 * Arena Hierarchy for LLE:
 *
 *   Session Arena (shell lifetime)
 *   +-- Edit Arena (per lle_readline call)
 *       +-- Event Arena (per input event)
 *       +-- Frame Arena (per render)
 *
 * Usage:
 *
 *   // Create arena (child of parent, or NULL for root)
 *   lle_arena_t *arena = lle_arena_create(parent, "name", 4096);
 *
 *   // Allocate from arena - no free needed
 *   void *ptr = lle_arena_alloc(arena, size);
 *   char *str = lle_arena_strdup(arena, "hello");
 *
 *   // Destroy arena - frees ALL allocations and child arenas
 *   lle_arena_destroy(arena);
 *
 * See docs/development/ARENA_MEMORY_MANAGEMENT_PLAN.md for full design.
 */

#ifndef LLE_ARENA_H
#define LLE_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================
 */

/** Enable arena statistics tracking (set to 0 to disable for performance) */
#ifndef LLE_ARENA_STATS
#define LLE_ARENA_STATS 1
#endif

/** Default chunk size for new arenas (4KB) */
#define LLE_ARENA_DEFAULT_CHUNK_SIZE 4096

/** Default memory alignment (16 bytes for SSE compatibility) */
#define LLE_ARENA_DEFAULT_ALIGNMENT 16

/** Minimum chunk size */
#define LLE_ARENA_MIN_CHUNK_SIZE 256

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================
 */

/**
 * Arena chunk - contiguous memory region allocated from lush pool
 *
 * Forms a linked list for arenas that grow beyond initial size.
 * The chunk header is followed by usable memory in the flexible array.
 */
typedef struct lle_arena_chunk_t {
    struct lle_arena_chunk_t *next; /**< Next chunk in chain (NULL if last) */
    size_t size;                    /**< Usable size of this chunk (bytes) */
    size_t used;                    /**< Bytes allocated from this chunk */
    char data[];                    /**< Flexible array - actual memory */
} lle_arena_chunk_t;

/**
 * Arena allocator - bump-pointer allocator with hierarchical lifetime
 *
 * Arenas form a tree structure where destroying a parent automatically
 * destroys all children. This matches natural program scopes.
 */
typedef struct lle_arena_t {
    /* Arena identity and hierarchy */
    const char *name;                /**< Debug name (e.g., "edit_session") */
    struct lle_arena_t *parent;      /**< Parent arena (NULL for root) */
    struct lle_arena_t *first_child; /**< First child arena */
    struct lle_arena_t
        *next_sibling; /**< Next sibling in parent's child list */

    /* Memory management */
    lle_arena_chunk_t *first_chunk;   /**< First chunk (always present) */
    lle_arena_chunk_t *current_chunk; /**< Current allocation chunk */
    size_t default_chunk_size;        /**< Size for new chunks */
    size_t alignment;                 /**< Default alignment requirement */

#if LLE_ARENA_STATS
    /* Statistics (optional, can be compiled out) */
    size_t total_allocated;  /**< Total bytes allocated from this arena */
    size_t allocation_count; /**< Number of allocations made */
    size_t chunk_count;      /**< Number of chunks allocated */
    size_t peak_usage;       /**< High water mark for usage */
#endif

    /* Flags */
    uint32_t flags; /**< Arena configuration flags */
} lle_arena_t;

/**
 * Arena flags for configuration
 */
typedef enum lle_arena_flags_t {
    LLE_ARENA_FLAG_NONE = 0,
    LLE_ARENA_FLAG_ZERO_ALLOC = 0x0001, /**< Zero-initialize all allocations */
    LLE_ARENA_FLAG_NO_GROW = 0x0002, /**< Don't allocate new chunks if full */
} lle_arena_flags_t;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * Create a new arena
 *
 * @param parent       Parent arena (NULL for root arena)
 * @param name         Debug name for the arena (stored, not copied)
 * @param initial_size Initial chunk size in bytes (0 = default 4KB)
 * @return New arena, or NULL on allocation failure
 *
 * Performance: O(1) - single lush_pool_alloc call
 *
 * The arena is automatically linked into parent's child list.
 * When the parent is destroyed, this arena will also be destroyed.
 *
 * Example:
 *   lle_arena_t *edit = lle_arena_create(session, "edit", 8192);
 *   // ... use edit arena ...
 *   lle_arena_destroy(edit);  // Or let parent destruction handle it
 */
lle_arena_t *lle_arena_create(lle_arena_t *parent, const char *name,
                              size_t initial_size);

/**
 * Create arena with custom flags
 *
 * @param parent       Parent arena (NULL for root)
 * @param name         Debug name
 * @param initial_size Initial chunk size (0 = default)
 * @param flags        Configuration flags (LLE_ARENA_FLAG_*)
 * @return New arena, or NULL on failure
 */
lle_arena_t *lle_arena_create_with_flags(lle_arena_t *parent, const char *name,
                                         size_t initial_size, uint32_t flags);

/**
 * Destroy arena and all children
 *
 * @param arena Arena to destroy (NULL is safe no-op)
 *
 * Performance: O(n) where n = number of chunks + child arenas
 *
 * This function:
 * 1. Recursively destroys all child arenas (depth-first)
 * 2. Unlinks from parent's child list
 * 3. Frees all chunks back to lush_pool
 * 4. Frees the arena structure itself
 *
 * WARNING: After this call, ALL pointers allocated from this arena
 * (and its children) are INVALID. Using them is undefined behavior.
 */
void lle_arena_destroy(lle_arena_t *arena);

/**
 * Reset arena to empty state (keep chunks allocated)
 *
 * @param arena Arena to reset
 *
 * Performance: O(c) where c = number of chunks
 *
 * This is faster than destroy+create when you want to reuse an arena
 * for a new operation of similar size. Chunks are kept but marked empty.
 *
 * NOTE: Does NOT destroy child arenas. Call lle_arena_destroy_children()
 * first if you want to reset children too.
 */
void lle_arena_reset(lle_arena_t *arena);

/**
 * Destroy all child arenas without destroying the parent
 *
 * @param arena Parent arena whose children should be destroyed
 *
 * Useful for resetting a scope without destroying the parent arena.
 */
void lle_arena_destroy_children(lle_arena_t *arena);

/* ============================================================================
 * ALLOCATION FUNCTIONS
 * ============================================================================
 */

/**
 * Allocate memory from arena
 *
 * @param arena Arena to allocate from
 * @param size  Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 *
 * Performance: O(1) in common case (bump pointer)
 *              O(1) when new chunk needed (lush_pool_alloc)
 *
 * Memory is NOT zeroed by default (unless LLE_ARENA_FLAG_ZERO_ALLOC set).
 * Use lle_arena_calloc() for zeroed memory.
 *
 * Memory is aligned to arena's default alignment (16 bytes by default).
 */
void *lle_arena_alloc(lle_arena_t *arena, size_t size);

/**
 * Allocate zeroed memory from arena
 *
 * @param arena Arena to allocate from
 * @param count Number of elements
 * @param size  Size of each element
 * @return Pointer to zeroed memory, or NULL on failure
 *
 * Performance: O(count * size) for memset
 *
 * Equivalent to lle_arena_alloc() followed by memset to zero.
 */
void *lle_arena_calloc(lle_arena_t *arena, size_t count, size_t size);

/**
 * Allocate memory with specific alignment
 *
 * @param arena     Arena to allocate from
 * @param size      Number of bytes to allocate
 * @param alignment Required alignment (must be power of 2)
 * @return Pointer to aligned memory, or NULL on failure
 *
 * Performance: O(1) with up to (alignment-1) bytes of padding
 */
void *lle_arena_alloc_aligned(lle_arena_t *arena, size_t size,
                              size_t alignment);

/**
 * Duplicate string in arena
 *
 * @param arena Arena to allocate from
 * @param str   String to duplicate (must be NULL-terminated)
 * @return Pointer to duplicated string, or NULL on failure
 *
 * Performance: O(n) where n = strlen(str)
 */
char *lle_arena_strdup(lle_arena_t *arena, const char *str);

/**
 * Duplicate string with length limit
 *
 * @param arena   Arena to allocate from
 * @param str     String to duplicate
 * @param max_len Maximum length to copy (not including null terminator)
 * @return Pointer to duplicated string, or NULL on failure
 *
 * The result is always null-terminated.
 */
char *lle_arena_strndup(lle_arena_t *arena, const char *str, size_t max_len);

/**
 * Duplicate memory block in arena
 *
 * @param arena Arena to allocate from
 * @param src   Source memory to copy
 * @param size  Number of bytes to copy
 * @return Pointer to duplicated memory, or NULL on failure
 */
void *lle_arena_memdup(lle_arena_t *arena, const void *src, size_t size);

/**
 * Formatted string allocation (printf-style)
 *
 * @param arena  Arena to allocate from
 * @param format Printf format string
 * @param ...    Format arguments
 * @return Pointer to formatted string, or NULL on failure
 */
char *lle_arena_sprintf(lle_arena_t *arena, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * Get total bytes allocated from arena
 *
 * @param arena            Arena to query
 * @param include_children Include child arena allocations in total
 * @return Total bytes allocated
 */
size_t lle_arena_get_allocated(lle_arena_t *arena, bool include_children);

/**
 * Get total bytes used (including chunk overhead)
 *
 * @param arena            Arena to query
 * @param include_children Include child arenas
 * @return Total bytes used including chunk headers
 */
size_t lle_arena_get_total_size(lle_arena_t *arena, bool include_children);

/**
 * Get number of chunks allocated
 *
 * @param arena Arena to query
 * @return Number of chunks
 */
size_t lle_arena_get_chunk_count(lle_arena_t *arena);

/**
 * Get number of child arenas
 *
 * @param arena         Arena to query
 * @param recursive     Count all descendants, not just direct children
 * @return Number of child arenas
 */
size_t lle_arena_get_child_count(lle_arena_t *arena, bool recursive);

/**
 * Check if arena has enough space for allocation without growing
 *
 * @param arena Arena to check
 * @param size  Size of desired allocation
 * @return true if allocation would fit in current chunk
 */
bool lle_arena_has_space(lle_arena_t *arena, size_t size);

/**
 * Print arena statistics (debug)
 *
 * @param arena  Arena to print (and optionally children)
 * @param indent Indentation level for hierarchical display
 *
 * Prints to stderr. Useful for debugging memory usage.
 */
void lle_arena_print_stats(lle_arena_t *arena, int indent);

/**
 * Print full arena tree (debug)
 *
 * @param root Root arena to print
 *
 * Prints entire arena hierarchy with statistics.
 */
void lle_arena_print_tree(lle_arena_t *root);

/* ============================================================================
 * SCRATCH / TEMPORARY ALLOCATION
 * ============================================================================
 */

/**
 * Temporary allocation marker for scratch allocations
 *
 * Use with lle_arena_scratch_begin() and lle_arena_scratch_end() to
 * allocate temporary memory that is freed at end of scope without
 * destroying the arena.
 */
typedef struct lle_arena_scratch_t {
    lle_arena_t *arena;
    lle_arena_chunk_t *chunk;
    size_t chunk_used;
} lle_arena_scratch_t;

/**
 * Begin a scratch allocation scope
 *
 * @param arena Arena to use for scratch allocations
 * @return Scratch marker (pass to lle_arena_scratch_end)
 *
 * Example:
 *   lle_arena_scratch_t scratch = lle_arena_scratch_begin(arena);
 *   char *temp = lle_arena_alloc(arena, 1024);
 *   // ... use temp ...
 *   lle_arena_scratch_end(&scratch);  // temp is now invalid
 */
lle_arena_scratch_t lle_arena_scratch_begin(lle_arena_t *arena);

/**
 * End a scratch allocation scope
 *
 * @param scratch Scratch marker from lle_arena_scratch_begin
 *
 * Resets arena to state it was in before scratch_begin.
 * All allocations made since scratch_begin are invalidated.
 *
 * NOTE: This only works if no child arenas were created during the
 * scratch scope. Child arenas are NOT destroyed.
 */
void lle_arena_scratch_end(lle_arena_scratch_t *scratch);

#ifdef __cplusplus
}
#endif

#endif /* LLE_ARENA_H */
