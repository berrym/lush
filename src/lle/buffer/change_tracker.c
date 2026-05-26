/**
 * @file change_tracker.c
 * @brief LLE Change Tracking and Undo/Redo System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 03 - Buffer Management, Section 7
 *
 * Complete implementation of atomic operation tracking with undo/redo support.
 * All buffer modifications are tracked and can be reversed.
 *
 * Key Features:
 * - Atomic operation tracking
 * - Complete undo/redo with cursor restoration
 * - Operation sequences for grouping
 * - Memory-efficient storage
 * - Branching timeline support (redo cleared on new edits)
 */

#include "lle/buffer_management.h"
#include "lle/utf8_support.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

/// @brief Get current timestamp in microseconds
static uint64_t get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/// @brief Find last undoable sequence
static lle_change_sequence_t *
find_last_undoable_sequence(lle_change_tracker_t *tracker) {
    if (!tracker || !tracker->current_position) {
        return NULL;
    }

    /// Start from current position and search backwards
    lle_change_sequence_t *seq = tracker->current_position;
    while (seq) {
        if (seq->can_undo && seq->sequence_complete) {
            return seq;
        }
        seq = seq->prev;
    }

    return NULL;
}

/// @brief Find last redoable sequence
static lle_change_sequence_t *
find_last_redoable_sequence(lle_change_tracker_t *tracker) {
    if (!tracker) {
        return NULL;
    }

    /// Start from current position and search forwards
    /// If current_position is NULL, start from first_sequence
    lle_change_sequence_t *seq = tracker->current_position
                                     ? tracker->current_position->next
                                     : tracker->first_sequence;

    while (seq) {
        if (seq->can_redo && seq->sequence_complete) {
            return seq;
        }
        seq = seq->next;
    }

    return NULL;
}

/// @brief Free operation and its associated data
static void free_operation(lle_change_operation_t *op,
                           lush_memory_pool_t *pool) {
    (void)pool; /// Reserved for future pool-based deallocation
    if (!op) {
        return;
    }

    if (op->inserted_text) {
        lle_pool_free(op->inserted_text);
    }
    if (op->deleted_text) {
        lle_pool_free(op->deleted_text);
    }

    lle_pool_free(op);
}

/// @brief Free sequence and all its operations
static void free_sequence(lle_change_sequence_t *seq,
                          lush_memory_pool_t *pool) {
    if (!seq) {
        return;
    }

    /// Free all operations in sequence
    lle_change_operation_t *op = seq->first_op;
    while (op) {
        lle_change_operation_t *next = op->next;
        free_operation(op, pool);
        op = next;
    }

    lle_pool_free(seq);
}

/* ============================================================================
 * CHANGE TRACKER LIFECYCLE
 * ============================================================================
 */

lle_result_t lle_change_tracker_init(lle_change_tracker_t **tracker,
                                     lush_memory_pool_t *memory_pool,
                                     size_t max_undo_levels) {
    if (!tracker || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate tracker structure
    lle_change_tracker_t *t =
        (lle_change_tracker_t *)lle_pool_alloc(sizeof(lle_change_tracker_t));
    if (!t) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize all fields
    memset(t, 0, sizeof(lle_change_tracker_t));

    t->memory_pool = memory_pool;
    t->max_undo_levels =
        (max_undo_levels > 0) ? max_undo_levels : LLE_BUFFER_MAX_UNDO_LEVELS;
    t->max_redo_levels = LLE_BUFFER_MAX_REDO_LEVELS;
    t->next_sequence_id = 1;
    t->next_operation_id = 1;
    t->memory_used = sizeof(lle_change_tracker_t);

    *tracker = t;
    return LLE_SUCCESS;
}

lle_result_t lle_change_tracker_destroy(lle_change_tracker_t *tracker) {
    if (!tracker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free all sequences
    lle_change_sequence_t *seq = tracker->first_sequence;
    while (seq) {
        lle_change_sequence_t *next = seq->next;
        free_sequence(seq, tracker->memory_pool);
        seq = next;
    }

    /// Free tracker itself
    lle_pool_free(tracker);

    return LLE_SUCCESS;
}

lle_result_t lle_change_tracker_clear(lle_change_tracker_t *tracker) {
    if (!tracker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Free all sequences
    lle_change_sequence_t *seq = tracker->first_sequence;
    while (seq) {
        lle_change_sequence_t *next = seq->next;
        free_sequence(seq, tracker->memory_pool);
        seq = next;
    }

    /// Reset tracker state
    tracker->first_sequence = NULL;
    tracker->last_sequence = NULL;
    tracker->current_position = NULL;
    tracker->active_sequence = NULL;
    tracker->sequence_in_progress = false;
    tracker->sequence_count = 0;
    tracker->undo_count = 0;
    tracker->redo_count = 0;
    tracker->operation_count = 0;
    tracker->memory_used = sizeof(lle_change_tracker_t);

    return LLE_SUCCESS;
}

/* ============================================================================
 * OPERATION TRACKING
 * ============================================================================
 */

lle_result_t
lle_change_tracker_begin_sequence(lle_change_tracker_t *tracker,
                                  const char *description,
                                  lle_change_sequence_t **sequence) {
    if (!tracker || !sequence) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Check if sequence already in progress
    if (tracker->sequence_in_progress) {
        return LLE_ERROR_OPERATION_IN_PROGRESS;
    }

    /// Allocate new sequence
    lle_change_sequence_t *seq =
        (lle_change_sequence_t *)lle_pool_alloc(sizeof(lle_change_sequence_t));
    if (!seq) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize sequence
    memset(seq, 0, sizeof(lle_change_sequence_t));

    seq->sequence_id = tracker->next_sequence_id++;
    seq->start_time = get_timestamp_us();
    seq->can_undo = true;
    seq->can_redo = false;
    seq->sequence_complete = false;

    if (description) {
        strncpy(seq->description, description, sizeof(seq->description) - 1);
        seq->description[sizeof(seq->description) - 1] = '\0';
    } else {
        strcpy(seq->description, "Untitled operation");
    }

    /// Clear any redo history (branching timeline)
    if (tracker->current_position && tracker->current_position->next) {
        lle_change_sequence_t *redo_seq = tracker->current_position->next;
        while (redo_seq) {
            lle_change_sequence_t *next = redo_seq->next;
            free_sequence(redo_seq, tracker->memory_pool);
            redo_seq = next;
        }
        tracker->current_position->next = NULL;
        tracker->last_sequence = tracker->current_position;
    }

    /// Add to sequence chain
    if (!tracker->first_sequence) {
        tracker->first_sequence = seq;
        tracker->last_sequence = seq;
        tracker->current_position = seq;
    } else {
        seq->prev = tracker->last_sequence;
        tracker->last_sequence->next = seq;
        tracker->last_sequence = seq;
        tracker->current_position = seq;
    }

    tracker->active_sequence = seq;
    tracker->sequence_in_progress = true;
    tracker->sequence_count++;
    tracker->memory_used += sizeof(lle_change_sequence_t);

    /// Enforce undo limit
    while (tracker->sequence_count > tracker->max_undo_levels) {
        lle_change_sequence_t *old = tracker->first_sequence;
        if (old == tracker->current_position) {
            break; /// Don't remove current position
        }

        tracker->first_sequence = old->next;
        if (tracker->first_sequence) {
            tracker->first_sequence->prev = NULL;
        }

        free_sequence(old, tracker->memory_pool);
        tracker->sequence_count--;
        tracker->memory_used -= sizeof(lle_change_sequence_t);
    }

    *sequence = seq;
    return LLE_SUCCESS;
}

lle_result_t
lle_change_tracker_complete_sequence(lle_change_tracker_t *tracker) {
    if (!tracker) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!tracker->sequence_in_progress || !tracker->active_sequence) {
        return LLE_ERROR_NO_OPERATION_IN_PROGRESS;
    }

    /// Finalize sequence
    tracker->active_sequence->end_time = get_timestamp_us();
    tracker->active_sequence->sequence_complete = true;

    /// Clear tracking state
    tracker->active_sequence = NULL;
    tracker->sequence_in_progress = false;

    return LLE_SUCCESS;
}

lle_result_t lle_change_tracker_begin_operation(
    lle_change_sequence_t *sequence, lle_change_type_t type,
    size_t start_position, size_t length, lle_change_operation_t **operation) {
    if (!sequence || !operation) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate new operation
    lle_change_operation_t *op = (lle_change_operation_t *)lle_pool_alloc(
        sizeof(lle_change_operation_t));
    if (!op) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /// Initialize operation
    memset(op, 0, sizeof(lle_change_operation_t));

    op->type = type;
    op->start_position = start_position;
    op->end_position = start_position + length;
    op->affected_length = length;
    op->timestamp = get_timestamp_us();

    /// Add to sequence
    if (!sequence->first_op) {
        sequence->first_op = op;
        sequence->last_op = op;
    } else {
        op->prev = sequence->last_op;
        sequence->last_op->next = op;
        sequence->last_op = op;
    }

    sequence->operation_count++;

    *operation = op;
    return LLE_SUCCESS;
}

lle_result_t
lle_change_tracker_complete_operation(lle_change_operation_t *operation) {
    if (!operation) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Operation is complete - nothing special needed
    return LLE_SUCCESS;
}

lle_result_t
lle_change_tracker_save_deleted_text(lle_change_operation_t *operation,
                                     const char *deleted_text,
                                     size_t deleted_length) {
    if (!operation || !deleted_text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate and copy deleted text
    operation->deleted_text = (char *)lle_pool_alloc(deleted_length + 1);
    if (!operation->deleted_text) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memcpy(operation->deleted_text, deleted_text, deleted_length);
    operation->deleted_text[deleted_length] = '\0';
    operation->deleted_length = deleted_length;

    return LLE_SUCCESS;
}

lle_result_t
lle_change_tracker_save_inserted_text(lle_change_operation_t *operation,
                                      const char *inserted_text,
                                      size_t inserted_length) {
    if (!operation || !inserted_text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Allocate and copy inserted text
    operation->inserted_text = (char *)lle_pool_alloc(inserted_length + 1);
    if (!operation->inserted_text) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memcpy(operation->inserted_text, inserted_text, inserted_length);
    operation->inserted_text[inserted_length] = '\0';
    operation->inserted_length = inserted_length;

    return LLE_SUCCESS;
}

/* ============================================================================
 * UNDO/REDO OPERATIONS
 * ============================================================================
 */

lle_result_t lle_change_tracker_undo(lle_change_tracker_t *tracker,
                                     lle_buffer_t *buffer) {
    if (!tracker || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Find the most recent sequence that can be undone
    lle_change_sequence_t *sequence = find_last_undoable_sequence(tracker);
    if (!sequence) {
        return LLE_ERROR_NO_UNDO_AVAILABLE;
    }

    lle_result_t result = LLE_SUCCESS;

    /// Step 1: Disable change tracking during undo
    bool tracking_was_enabled = buffer->change_tracking_enabled;
    buffer->change_tracking_enabled = false;

    /// Step 2: Process operations in reverse order
    lle_change_operation_t *op = sequence->last_op;
    while (op) {
        switch (op->type) {
        case LLE_CHANGE_TYPE_INSERT:
            /// Undo insert by deleting the inserted text
            result = lle_buffer_delete_text(buffer, op->start_position,
                                            op->inserted_length);
            break;

        case LLE_CHANGE_TYPE_DELETE:
            /// Undo delete by reinserting the deleted text
            result =
                lle_buffer_insert_text(buffer, op->start_position,
                                       op->deleted_text, op->deleted_length);
            break;

        case LLE_CHANGE_TYPE_REPLACE:
            /// Undo replace by deleting new text and inserting old text
            result = lle_buffer_delete_text(buffer, op->start_position,
                                            op->inserted_length);
            if (result == LLE_SUCCESS) {
                result = lle_buffer_insert_text(buffer, op->start_position,
                                                op->deleted_text,
                                                op->deleted_length);
            }
            break;

        default:
            /// Other operation types don't modify buffer content
            break;
        }

        if (result != LLE_SUCCESS) {
            break;
        }

        op = op->prev;
    }

    /// Step 3: Restore cursor position
    if (result == LLE_SUCCESS && sequence->first_op) {
        buffer->cursor = sequence->first_op->cursor_before;
    }

    /// Step 4: Mark sequence as undone
    if (result == LLE_SUCCESS) {
        sequence->can_undo = false;
        sequence->can_redo = true;
        tracker->undo_count++;

        /// Move current position back
        if (tracker->current_position == sequence) {
            tracker->current_position =
                sequence->prev; /// Can be NULL for first sequence
        }
    }

    /// Step 5: Re-enable change tracking
    buffer->change_tracking_enabled = tracking_was_enabled;

    return result;
}

lle_result_t lle_change_tracker_redo(lle_change_tracker_t *tracker,
                                     lle_buffer_t *buffer) {
    if (!tracker || !buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Find the most recent sequence that can be redone
    lle_change_sequence_t *sequence = find_last_redoable_sequence(tracker);
    if (!sequence) {
        return LLE_ERROR_NO_REDO_AVAILABLE;
    }

    lle_result_t result = LLE_SUCCESS;

    /// Step 1: Disable change tracking during redo
    bool tracking_was_enabled = buffer->change_tracking_enabled;
    buffer->change_tracking_enabled = false;

    /// Step 2: Process operations in forward order
    lle_change_operation_t *op = sequence->first_op;
    while (op) {
        switch (op->type) {
        case LLE_CHANGE_TYPE_INSERT:
            /// Redo insert
            result =
                lle_buffer_insert_text(buffer, op->start_position,
                                       op->inserted_text, op->inserted_length);
            break;

        case LLE_CHANGE_TYPE_DELETE:
            /// Redo delete
            result = lle_buffer_delete_text(buffer, op->start_position,
                                            op->deleted_length);
            break;

        case LLE_CHANGE_TYPE_REPLACE:
            /// Redo replace
            result = lle_buffer_delete_text(buffer, op->start_position,
                                            op->deleted_length);
            if (result == LLE_SUCCESS) {
                result = lle_buffer_insert_text(buffer, op->start_position,
                                                op->inserted_text,
                                                op->inserted_length);
            }
            break;

        default:
            /// Other operation types don't modify buffer content
            break;
        }

        if (result != LLE_SUCCESS) {
            break;
        }

        op = op->next;
    }

    /// Step 3: Restore cursor position
    if (result == LLE_SUCCESS && sequence->last_op) {
        buffer->cursor = sequence->last_op->cursor_after;
    }

    /// Step 4: Mark sequence as redone
    if (result == LLE_SUCCESS) {
        sequence->can_undo = true;
        sequence->can_redo = false;
        tracker->redo_count++;

        /// Move current position forward
        tracker->current_position = sequence;
    }

    /// Step 5: Re-enable change tracking
    buffer->change_tracking_enabled = tracking_was_enabled;

    return result;
}

bool lle_change_tracker_can_undo(const lle_change_tracker_t *tracker) {
    if (!tracker) {
        return false;
    }

    return find_last_undoable_sequence((lle_change_tracker_t *)tracker) != NULL;
}

bool lle_change_tracker_can_redo(const lle_change_tracker_t *tracker) {
    if (!tracker) {
        return false;
    }

    return find_last_redoable_sequence((lle_change_tracker_t *)tracker) != NULL;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

size_t lle_change_tracker_undo_depth(const lle_change_tracker_t *tracker) {
    if (!tracker) {
        return 0;
    }

    size_t depth = 0;
    lle_change_sequence_t *seq = tracker->current_position;

    while (seq) {
        if (seq->can_undo && seq->sequence_complete) {
            depth++;
        }
        seq = seq->prev;
    }

    return depth;
}

size_t lle_change_tracker_redo_depth(const lle_change_tracker_t *tracker) {
    if (!tracker) {
        return 0;
    }

    size_t depth = 0;
    lle_change_sequence_t *seq =
        tracker->current_position ? tracker->current_position->next : NULL;

    while (seq) {
        if (seq->can_redo && seq->sequence_complete) {
            depth++;
        }
        seq = seq->next;
    }

    return depth;
}

size_t lle_change_tracker_memory_usage(const lle_change_tracker_t *tracker) {
    if (!tracker) {
        return 0;
    }

    return tracker->memory_used;
}
