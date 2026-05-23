/**
 * @file kill_ring.h
 * @brief Kill ring system for GNU Readline compatible cut/paste operations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements the kill ring (yank/kill buffer) system for cut/paste operations
 * compatible with GNU Readline behavior in bash and other shells.
 *
 * The kill ring is a circular buffer that stores killed (cut) text. Users can
 * yank (paste) the most recently killed text with Ctrl-Y, then cycle through
 * previous kills with Meta-Y (yank-pop).
 *
 * Key Features:
 * - Circular buffer with configurable size (default 32 entries)
 * - Successive kills append to same entry (like GNU Readline)
 * - Yank-pop cycles through ring after yank operation
 * - Thread-safe operations
 * - Memory pool integration
 *
 * GNU Readline Compatibility:
 * - C-k (kill-line) adds text to ring
 * - C-y (yank) pastes most recent kill
 * - M-y (yank-pop) cycles to previous kills
 * - Successive C-k commands append to same ring entry
 *
 * Specification:
 * docs/lle_specification/critical_gaps/25_default_keybindings_complete.md
 */

#ifndef LLE_KILL_RING_H
#define LLE_KILL_RING_H

#include "lle/error_handling.h"
#include "lle/memory_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/**
 * @brief Default maximum number of entries in the kill ring
 *
 * GNU Readline default is typically 32 entries. This provides good balance
 * between memory usage and utility.
 */
#define LLE_KILL_RING_DEFAULT_SIZE 32

/**
 * @brief Maximum allowed kill ring size
 *
 * Arbitrary limit to prevent excessive memory usage. Can be configured
 * smaller but not larger than this.
 */
#define LLE_KILL_RING_MAX_SIZE 256

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

/**
 * @brief Opaque kill ring structure
 */
typedef struct lle_kill_ring lle_kill_ring_t;

/* ============================================================================
 * LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Create a new kill ring
 * @param ring Output pointer for created kill ring
 * @param max_entries Maximum number of entries (0 = use default size)
 * @param pool Memory pool for allocations (NULL = use global pool)
 * @return LLE_SUCCESS or error code
 *
 * @note If max_entries is 0, LLE_KILL_RING_DEFAULT_SIZE (32) is used
 * @note If max_entries > LLE_KILL_RING_MAX_SIZE, it is clamped
 * @note All killed text strings are allocated from the memory pool
 */
lle_result_t lle_kill_ring_create(lle_kill_ring_t **ring, size_t max_entries,
                                  lle_memory_pool_t *pool);

/**
 * @brief Destroy a kill ring and free all resources
 * @param ring Kill ring to destroy
 * @return LLE_SUCCESS or error code
 *
 * @note All killed text entries are freed
 * @note After this call, ring pointer is invalid
 */
lle_result_t lle_kill_ring_destroy(lle_kill_ring_t *ring);

/* ============================================================================
 * KILL OPERATIONS (Adding text to ring)
 * ============================================================================
 */

/**
 * @brief Add killed text to the ring
 * @param ring Kill ring
 * @param text Text to add (must be non-NULL, non-empty)
 * @param append If true, append to last entry; if false, create new entry
 * @return LLE_SUCCESS or error code
 *
 * GNU Readline Behavior:
 * - Successive kill commands (C-k, M-d, etc.) set append=true
 * - This accumulates multiple kills into single entry
 * - Any non-kill command resets append mode
 * - When ring is full, oldest entry is overwritten (circular)
 *
 * @note Text is copied into memory pool storage
 * @note If append=true but ring is empty, creates new entry
 * @note If ring is full, oldest entry is freed and replaced
 */
lle_result_t lle_kill_ring_add(lle_kill_ring_t *ring, const char *text,
                               bool append);

/**
 * @brief Add killed text to front of last entry (prepend mode)
 * @param ring Kill ring
 * @param text Text to prepend to last entry
 * @return LLE_SUCCESS or error code
 *
 * Use Case: C-u (backward-kill-line) prepends to ring entry
 * because text is killed backwards but should paste forwards.
 *
 * @note If ring is empty, creates new entry
 * @note Text is copied and prepended to existing last entry
 */
lle_result_t lle_kill_ring_prepend(lle_kill_ring_t *ring, const char *text);

/* ============================================================================
 * YANK OPERATIONS (Retrieving text from ring)
 * ============================================================================
 */

/**
 * @brief Get the current yank text (most recent kill)
 * @param ring Kill ring
 * @param text_out Output pointer for text (do not free)
 * @return LLE_SUCCESS, LLE_ERROR_EMPTY, or other error code
 *
 * GNU Readline C-y (yank) behavior:
 * - Returns most recently killed text
 * - Prepares ring for yank-pop (M-y) operation
 * - Marks last operation as yank
 *
 * @note Returns pointer to internal storage (do not modify or free)
 * @note Returns LLE_ERROR_EMPTY if ring has no entries
 * @note Sets internal state for subsequent yank-pop
 */
lle_result_t lle_kill_ring_get_current(lle_kill_ring_t *ring,
                                       const char **text_out);

/**
 * @brief Cycle to previous kill in ring (yank-pop operation)
 * @param ring Kill ring
 * @param text_out Output pointer for text (do not free)
 * @return LLE_SUCCESS, LLE_ERROR_NOT_AFTER_YANK, or other error code
 *
 * GNU Readline M-y (yank-pop) behavior:
 * - MUST be called immediately after yank or yank-pop
 * - Cycles backwards through kill ring
 * - Wraps around to newest when reaching oldest
 * - Returns error if last operation was not yank
 *
 * @note Returns pointer to internal storage (do not modify or free)
 * @note Returns LLE_ERROR_NOT_AFTER_YANK if last op was not yank
 * @note Maintains yank state so successive M-y calls cycle through ring
 */
lle_result_t lle_kill_ring_yank_pop(lle_kill_ring_t *ring,
                                    const char **text_out);

/* ============================================================================
 * STATE MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Clear all entries from the kill ring
 * @param ring Kill ring
 * @return LLE_SUCCESS or error code
 *
 * @note Frees all killed text entries
 * @note Resets ring to empty state
 * @note Resets yank state
 */
lle_result_t lle_kill_ring_clear(lle_kill_ring_t *ring);

/**
 * @brief Reset yank state (call after any non-yank operation)
 * @param ring Kill ring
 * @return LLE_SUCCESS or error code
 *
 * GNU Readline Behavior:
 * - Any non-yank command breaks the yank chain
 * - Next M-y will error until another C-y is performed
 *
 * @note This should be called after any editor operation that isn't
 * yank/yank-pop
 * @note Does not clear kill ring contents, only yank state
 */
lle_result_t lle_kill_ring_reset_yank_state(lle_kill_ring_t *ring);

/**
 * @brief Mark that last operation was a kill (for append mode)
 * @param ring Kill ring
 * @param was_kill True if last operation was a kill command
 * @return LLE_SUCCESS or error code
 *
 * GNU Readline Behavior:
 * - Successive kill commands append to same ring entry
 * - Any non-kill command breaks the append chain
 *
 * Usage Pattern:
 * - Call with true after successful kill operation
 * - Call with false after any non-kill operation
 * - Next kill with append=true will only append if this is true
 */
lle_result_t lle_kill_ring_set_last_was_kill(lle_kill_ring_t *ring,
                                             bool was_kill);

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get number of entries currently in the ring
 * @param ring Kill ring
 * @param count_out Output pointer for count
 * @return LLE_SUCCESS or error code
 *
 * @note Returns 0 if ring is empty
 * @note Maximum return value is max_entries from create
 */
lle_result_t lle_kill_ring_get_count(lle_kill_ring_t *ring, size_t *count_out);

/**
 * @brief Check if ring is empty
 * @param ring Kill ring
 * @param empty_out Output pointer for empty status
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_ring_is_empty(lle_kill_ring_t *ring, bool *empty_out);

/**
 * @brief Check if last operation was a yank (for yank-pop validation)
 * @param ring Kill ring
 * @param was_yank_out Output pointer for yank status
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_ring_was_last_yank(lle_kill_ring_t *ring,
                                         bool *was_yank_out);

/**
 * @brief Get maximum capacity of the ring
 * @param ring Kill ring
 * @param capacity_out Output pointer for capacity
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_ring_get_capacity(lle_kill_ring_t *ring,
                                        size_t *capacity_out);

/* ============================================================================
 * DEBUGGING/INTROSPECTION (Development only)
 * ============================================================================
 */

#ifdef LLE_DEBUG

/**
 * @brief Get entry at specific index (for testing/debugging)
 * @param ring Kill ring
 * @param index Index (0 = newest, count-1 = oldest)
 * @param text_out Output pointer for text
 * @return LLE_SUCCESS, LLE_ERROR_OUT_OF_BOUNDS, or other error code
 *
 * @note Only available in debug builds
 * @note Returns pointer to internal storage (do not modify or free)
 */
lle_result_t lle_kill_ring_get_entry_at_index(lle_kill_ring_t *ring,
                                              size_t index,
                                              const char **text_out);

/**
 * @brief Dump kill ring contents to stdout (debugging)
 * @param ring Kill ring
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_kill_ring_dump(lle_kill_ring_t *ring);

#endif // LLE_DEBUG

#endif // LLE_KILL_RING_H
