/**
 * @file buffer_management.h
 * @brief LLE Buffer Management System - Type Definitions and Function
 * Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 03 - Buffer Management Complete Specification
 * Version: 1.0.0
 *
 * This header contains ALL type definitions and function declarations for the
 * LLE buffer management system. NO implementations are included here.
 *
 * Implementation Status: PHASE 1 - Core Buffer Structure
 * - Phase 1: Core buffer structure and lifecycle (THIS PHASE)
 * - Phase 2: UTF-8 processing subsystem (FUTURE)
 * - Phase 3: Line structure management (FUTURE)
 * - Phase 4: Cursor management (FUTURE)
 * - Phase 5: Change tracking and undo/redo (FUTURE)
 * - Phase 6: Buffer operations (insert, delete, replace) (FUTURE)
 * - Phase 7: Multiline support (FUTURE)
 *
 * Layer 0: Type Definitions Only
 * Layer 1: Implementations in src/lle/buffer_management.c (separate file)
 */

#ifndef LLE_BUFFER_MANAGEMENT_H
#define LLE_BUFFER_MANAGEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Include LLE dependencies */
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/* Buffer Size and Capacity Limits */
#define LLE_BUFFER_NAME_MAX 256               /* Maximum buffer name length */
#define LLE_BUFFER_DEFAULT_CAPACITY 4096      /* Default 4KB buffer */
#define LLE_BUFFER_MIN_CAPACITY 256           /* Minimum 256 bytes */
#define LLE_BUFFER_MAX_CAPACITY (1024 * 1024) /* Maximum 1MB per buffer */
#define LLE_BUFFER_GROWTH_FACTOR 2            /* Capacity growth factor */

/* Memory Alignment */
#define LLE_BUFFER_MEMORY_ALIGNMENT 16 /* 16-byte alignment */

/* Line Structure Limits */
#define LLE_BUFFER_MAX_LINES 10000           /* Maximum lines per buffer */
#define LLE_BUFFER_DEFAULT_LINE_CAPACITY 100 /* Default line array capacity */

/* UTF-8 Index Configuration */
#define LLE_UTF8_INDEX_GRANULARITY 64   /* Index every 64 bytes */
#define LLE_UTF8_INDEX_MAX_ENTRIES 1024 /* Maximum index entries */

/* Change Tracking Limits */
#define LLE_BUFFER_MAX_UNDO_LEVELS 1000      /* Maximum undo history */
#define LLE_BUFFER_MAX_REDO_LEVELS 1000      /* Maximum redo history */
#define LLE_BUFFER_CHANGE_SEQUENCE_LIMIT 100 /* Max operations per sequence */

/* Cache Configuration */
#define LLE_BUFFER_CACHE_SIZE 256      /* Cache entry count */
#define LLE_BUFFER_CACHE_LINE_TTL 1000 /* Cache entry TTL (ms) */

/* Performance Targets (nanoseconds) */
#define LLE_BUFFER_PERF_INSERT_MAX_NS 500000ULL /* 0.5ms insert target */
#define LLE_BUFFER_PERF_DELETE_MAX_NS 500000ULL /* 0.5ms delete target */
#define LLE_BUFFER_PERF_UTF8_CALC_MAX_NS                                       \
    100000ULL /* 0.1ms UTF-8 calc target                                       \
               */

/* Buffer Flags */
#define LLE_BUFFER_FLAG_READONLY 0x0001    /* Buffer is read-only */
#define LLE_BUFFER_FLAG_MODIFIED 0x0002    /* Buffer has been modified */
#define LLE_BUFFER_FLAG_MULTILINE 0x0004   /* Multiline mode active */
#define LLE_BUFFER_FLAG_UTF8_DIRTY 0x0008  /* UTF-8 index needs rebuild */
#define LLE_BUFFER_FLAG_LINE_DIRTY 0x0010  /* Line structure needs rebuild */
#define LLE_BUFFER_FLAG_CACHE_DIRTY 0x0020 /* Cache needs refresh */
#define LLE_BUFFER_FLAG_VALIDATION_FAILED 0x0040 /* Validation failed */

/* Line Flags */
#define LLE_LINE_FLAG_CONTINUATION 0x01       /* Line is a continuation */
#define LLE_LINE_FLAG_NEEDS_REVALIDATION 0x02 /* Line needs revalidation */
#define LLE_LINE_FLAG_CACHED 0x04             /* Line render is cached */

/* Cache Flags */
#define LLE_CACHE_LINE_STRUCTURE 0x01 /* Cache line structure */
#define LLE_CACHE_RENDER 0x02         /* Cache render data */
#define LLE_CACHE_LAYOUT 0x04         /* Cache layout data */

/* ============================================================================
 * ENUMERATIONS
 * ============================================================================
 */

/**
 * @brief Buffer change operation types
 * Spec Reference: Line 1014-1023
 */
typedef enum {
    LLE_CHANGE_TYPE_INSERT = 1,  /* Insert text operation */
    LLE_CHANGE_TYPE_DELETE,      /* Delete text operation */
    LLE_CHANGE_TYPE_REPLACE,     /* Replace text operation */
    LLE_CHANGE_TYPE_CURSOR_MOVE, /* Cursor movement */
    LLE_CHANGE_TYPE_SELECTION,   /* Selection change */
    LLE_CHANGE_TYPE_COMPOSITE    /* Composite operation */
} lle_change_type_t;

/**
 * @brief Line types for multiline context
 */
typedef enum {
    LLE_LINE_TYPE_COMMAND,      /* Regular command line */
    LLE_LINE_TYPE_CONTINUATION, /* Continuation line */
    LLE_LINE_TYPE_HEREDOC,      /* Here-document content */
    LLE_LINE_TYPE_QUOTED,       /* Inside quoted string */
    LLE_LINE_TYPE_COMMENT       /* Comment line */
} lle_line_type_t;

/**
 * @brief Multiline parser states
 */
typedef enum {
    LLE_MULTILINE_STATE_NONE,           /* No multiline context */
    LLE_MULTILINE_STATE_QUOTE_SINGLE,   /* Inside single quote */
    LLE_MULTILINE_STATE_QUOTE_DOUBLE,   /* Inside double quote */
    LLE_MULTILINE_STATE_QUOTE_BACKTICK, /* Inside backtick */
    LLE_MULTILINE_STATE_HEREDOC,        /* Inside heredoc */
    LLE_MULTILINE_STATE_PAREN,          /* Inside parentheses */
    LLE_MULTILINE_STATE_BRACE,          /* Inside braces */
    LLE_MULTILINE_STATE_BRACKET,        /* Inside brackets */
    LLE_MULTILINE_STATE_PIPE,           /* After pipe */
    LLE_MULTILINE_STATE_BACKSLASH       /* After backslash */
} lle_multiline_state_t;

/* ============================================================================
 * TYPE DEFINITIONS - FORWARD DECLARATIONS
 * ============================================================================
 */

/* Forward declarations for circular dependencies */
typedef struct lle_buffer_t lle_buffer_t;
typedef struct lle_buffer_system_t lle_buffer_system_t;
typedef struct lle_buffer_pool_t lle_buffer_pool_t;
typedef struct lle_line_info_t lle_line_info_t;
typedef struct lle_utf8_processor lle_utf8_processor_t;
typedef struct lle_utf8_index_t lle_utf8_index_t;
typedef struct lle_multiline_context_t lle_multiline_context_t;
typedef struct lle_multiline_manager_t lle_multiline_manager_t;
typedef struct lle_cursor_position_t lle_cursor_position_t;
typedef struct lle_cursor_manager_t lle_cursor_manager_t;
typedef struct lle_change_operation_t lle_change_operation_t;
typedef struct lle_change_sequence_t lle_change_sequence_t;
typedef struct lle_change_tracker_t lle_change_tracker_t;
typedef struct lle_buffer_validator_t lle_buffer_validator_t;
typedef struct lle_buffer_cache_t lle_buffer_cache_t;
typedef struct lle_selection_range_t lle_selection_range_t;
typedef struct lle_buffer_performance_metrics_t
    lle_buffer_performance_metrics_t;
typedef struct lle_cursor_cache_t lle_cursor_cache_t;

/* Type aliases for flag bitfields */
typedef uint16_t lle_buffer_flags_t;
typedef uint8_t lle_line_flags_t;
typedef uint8_t lle_cache_flags_t;

/* ============================================================================
 * STRUCTURE DEFINITIONS - PHASE 1: CORE BUFFER
 * ============================================================================
 */

/**
 * @brief Cursor position structure
 * Spec Reference: Spec 03, Line 867-891
 *
 * Complete cursor position structure with ALL fields from specification.
 */
struct lle_cursor_position_t {
    /* Byte-based position (primary) */
    size_t byte_offset; /* Byte offset in buffer */

    /* UTF-8 based positions */
    size_t codepoint_index; /* Unicode codepoint index */
    size_t grapheme_index;  /* Grapheme cluster index */

    /* Line-based position */
    size_t line_number;      /* Line number (0-based) */
    size_t column_offset;    /* Column offset in line (bytes) */
    size_t column_codepoint; /* Column position (codepoints) */
    size_t column_grapheme;  /* Column position (graphemes) */

    /* Visual position */
    size_t visual_line;   /* Visual line (with wrapping) */
    size_t visual_column; /* Visual column position */

    /* Position validity */
    bool position_valid;     /* Position validity flag */
    uint32_t buffer_version; /* Associated buffer version */
};

/**
 * @brief Selection range structure
 *
 * Represents a selected range of text in the buffer.
 */
struct lle_selection_range_t {
    lle_cursor_position_t start; /* Selection start position */
    lle_cursor_position_t end;   /* Selection end position */
    bool active;                 /* Selection is active */
    bool visual_mode;            /* Visual selection mode */
};

/**
 * @brief UTF-8 index structure
 * Spec Reference: Spec 03 Section 4.1
 *
 * Fast position mapping for UTF-8 buffers providing O(1) lookups.
 * Maps between byte offsets, codepoint indices, and grapheme cluster indices.
 */
struct lle_utf8_index_t {
    /* Fast position mapping arrays */
    size_t *byte_to_codepoint;     /* Byte offset to codepoint index */
    size_t *codepoint_to_byte;     /* Codepoint index to byte offset */
    size_t *grapheme_to_codepoint; /* Grapheme cluster to codepoint index */
    size_t *codepoint_to_grapheme; /* Codepoint to grapheme cluster index */
    size_t *grapheme_to_display;   /* Grapheme cluster to display column */
    size_t *display_to_grapheme;   /* Display column to grapheme cluster */

    /* Index metadata */
    size_t byte_count;      /* Total bytes indexed */
    size_t codepoint_count; /* Total codepoints indexed */
    size_t grapheme_count;  /* Total grapheme clusters indexed */
    size_t display_width;   /* Total display columns */

    /* Index validity and versioning */
    bool index_valid;          /* Index validity flag */
    uint32_t buffer_version;   /* Associated buffer version */
    uint64_t last_update_time; /* Last index update time */

    /* Performance tracking */
    size_t rebuild_count; /* Number of index rebuilds */
    uint64_t
        total_rebuild_time_ns; /* Total time spent rebuilding (nanoseconds) */
};

/**
 * @brief Buffer validator structure
 * Spec Reference: Spec 03 Section 8
 *
 * Validates buffer integrity including UTF-8 encoding, line structure,
 * cursor positions, and content checksums.
 */
struct lle_buffer_validator_t {
    /* Validation configuration */
    bool utf8_validation_enabled;   /* UTF-8 validation flag */
    bool line_structure_validation; /* Line structure validation flag */
    bool cursor_validation_enabled; /* Cursor position validation flag */
    bool bounds_checking_enabled;   /* Bounds checking flag */

    /* Validation statistics */
    uint32_t validation_count;      /* Total validations performed */
    uint32_t validation_failures;   /* Number of validation failures */
    uint32_t corruption_detections; /* Buffer corruption detections */
    uint32_t bounds_violations;     /* Bounds check violations */

    /* UTF-8 processor reference */
    lle_utf8_processor_t *utf8_processor; /* UTF-8 processor (optional) */

    /* Last validation results */
    lle_result_t last_validation_result; /* Result of last validation */
    uint64_t last_validation_time;       /* Timestamp of last validation */
};

/**
 * @brief Cursor manager structure
 * Spec Reference: Spec 03, Line 891-909
 *
 * Manages cursor position and movement operations.
 */
struct lle_cursor_manager_t {
    /* Current cursor state */
    lle_cursor_position_t position; /* Current cursor position */
    lle_cursor_position_t target;   /* Target cursor position */

    /* Movement preferences */
    size_t preferred_visual_column; /* Preferred visual column */
    bool sticky_column;             /* Sticky column mode */

    /* UTF-8 processor reference */
    lle_utf8_processor_t *utf8_processor; /* UTF-8 processor (optional) */

    /* Buffer reference */
    lle_buffer_t *buffer; /* Associated buffer */

    /* Performance optimization */
    lle_cursor_cache_t *position_cache; /* Position calculation cache */
};

/**
 * @brief Core buffer structure
 * Spec Reference: Line 212-269
 *
 * Complete buffer structure with ALL fields from specification.
 * Phase 1: Only basic lifecycle functions implemented.
 * Future phases will implement subsystem-specific operations.
 */
struct lle_buffer_t {
    /* Buffer metadata */
    uint32_t buffer_id;             /* Unique buffer identifier */
    char name[LLE_BUFFER_NAME_MAX]; /* Buffer name/description */
    uint64_t creation_time;         /* Buffer creation timestamp */
    uint64_t last_modified_time;    /* Last modification timestamp */
    uint32_t modification_count;    /* Total modifications counter */

    /* Buffer content storage */
    char *data;      /* UTF-8 encoded buffer data */
    size_t capacity; /* Allocated buffer capacity */
    size_t length;   /* Current buffer length (bytes) */
    size_t used;     /* Actually used buffer space */

    /* UTF-8 and Unicode metadata */
    size_t codepoint_count;       /* Number of Unicode codepoints */
    size_t grapheme_count;        /* Number of grapheme clusters */
    lle_utf8_index_t *utf8_index; /* Fast UTF-8 position index */
    bool utf8_index_valid;        /* UTF-8 index validity flag */

    /* Line structure information */
    lle_line_info_t *lines;                 /* Line structure array */
    size_t line_count;                      /* Number of logical lines */
    size_t line_capacity;                   /* Allocated line array capacity */
    bool multiline_active;                  /* Multiline mode status */
    lle_multiline_context_t *multiline_ctx; /* Multiline parsing context */

    /* Cursor and selection */
    lle_cursor_position_t cursor;     /* Current cursor position */
    lle_selection_range_t *selection; /* Current selection range */
    bool selection_active;            /* Selection status flag */

    /* Change tracking integration */
    lle_change_sequence_t *current_sequence; /* Active change sequence */
    uint32_t sequence_number;                /* Current sequence number */
    bool change_tracking_enabled;            /* Change tracking status */

    /* Performance optimization */
    lle_buffer_cache_t *cache; /* Rendering and operation cache */
    uint32_t cache_version;    /* Cache version counter */
    bool cache_dirty;          /* Cache dirty flag */

    /* Validation and integrity */
    uint32_t checksum;        /* Buffer content checksum */
    bool integrity_valid;     /* Integrity validation status */
    lle_buffer_flags_t flags; /* Buffer status flags */

    /* Memory management */
    lle_buffer_pool_t *pool;         /* Associated buffer pool */
    lush_memory_pool_t *memory_pool; /* Lush memory pool reference */

    /* Security - Minimal Secure Mode (Spec 15 Phase 1) */
    bool secure_mode_enabled; /* Secure mode active flag */
    bool memory_locked;       /* Memory mlock status */
};

/**
 * @brief Line information structure
 * Spec Reference: Line 269-294
 *
 * Complete line structure with ALL fields from specification.
 * Phase 3 will implement line structure management.
 */
struct lle_line_info_t {
    size_t start_offset;    /* Line start byte offset */
    size_t end_offset;      /* Line end byte offset */
    size_t length;          /* Line length in bytes */
    size_t codepoint_count; /* Number of codepoints in line */
    size_t grapheme_count;  /* Number of grapheme clusters */
    size_t visual_width;    /* Visual display width */

    /* Line type and characteristics */
    lle_line_type_t type;   /* Line type (command, continuation, etc.) */
    lle_line_flags_t flags; /* Line status flags */
    uint8_t indent_level;   /* Indentation level */

    /* Multiline context */
    lle_multiline_state_t ml_state; /* Multiline parser state */
    char *ml_context;               /* Multiline context string */

    /* Performance optimization */
    uint32_t render_cache_key; /* Render cache key */
    bool needs_revalidation;   /* Revalidation required flag */
};

/**
 * @brief Change operation structure
 * Spec Reference: Line 1025-1051
 *
 * Represents a single atomic change to the buffer.
 * Contains all information needed for undo/redo.
 */
struct lle_change_operation_t {
    /* Operation metadata */
    uint32_t operation_id;  /* Unique operation ID */
    lle_change_type_t type; /* Operation type */
    uint64_t timestamp;     /* Operation timestamp */

    /* Position information */
    size_t start_position;  /* Start byte offset */
    size_t end_position;    /* End byte offset */
    size_t affected_length; /* Length of affected text */

    /* Operation data for undo/redo */
    char *inserted_text;    /* Text that was inserted */
    size_t inserted_length; /* Length of inserted text */
    char *deleted_text;     /* Text that was deleted */
    size_t deleted_length;  /* Length of deleted text */

    /* Cursor state preservation */
    lle_cursor_position_t cursor_before; /* Cursor before operation */
    lle_cursor_position_t cursor_after;  /* Cursor after operation */

    /* Operation linking */
    struct lle_change_operation_t *next; /* Next in sequence */
    struct lle_change_operation_t *prev; /* Previous in sequence */
};

/**
 * @brief Change sequence structure
 * Spec Reference: Line 1053-1069
 *
 * Groups related operations into a single undo/redo unit.
 */
struct lle_change_sequence_t {
    /* Sequence metadata */
    uint32_t sequence_id; /* Unique sequence ID */
    char description[64]; /* Human-readable description */
    uint64_t start_time;  /* Sequence start time */
    uint64_t end_time;    /* Sequence end time */

    /* Operation chain */
    lle_change_operation_t *first_op; /* First operation */
    lle_change_operation_t *last_op;  /* Last operation */
    size_t operation_count;           /* Number of operations */

    /* Sequence state */
    bool sequence_complete; /* Sequence is complete */
    bool can_undo;          /* Can be undone */
    bool can_redo;          /* Can be redone */

    /* Sequence linking */
    struct lle_change_sequence_t *next; /* Next in history */
    struct lle_change_sequence_t *prev; /* Previous in history */
};

/**
 * @brief Change tracker structure
 *
 * Manages undo/redo history for a buffer.
 */
struct lle_change_tracker_t {
    /* Sequence history */
    lle_change_sequence_t *first_sequence;   /* First in history */
    lle_change_sequence_t *last_sequence;    /* Last in history */
    lle_change_sequence_t *current_position; /* Current position */
    size_t sequence_count;                   /* Total sequences */

    /* Active sequence tracking */
    lle_change_sequence_t *active_sequence; /* Building sequence */
    bool sequence_in_progress;              /* Sequence in progress */

    /* Undo/redo limits */
    size_t max_undo_levels; /* Maximum undo levels */
    size_t max_redo_levels; /* Maximum redo levels */

    /* Statistics */
    uint32_t undo_count;      /* Undo operations */
    uint32_t redo_count;      /* Redo operations */
    uint32_t operation_count; /* Total operations */

    /* ID generation */
    uint32_t next_sequence_id;  /* Next sequence ID */
    uint32_t next_operation_id; /* Next operation ID */

    /* Memory management */
    lush_memory_pool_t *memory_pool; /* Memory pool */
    size_t memory_used;              /* Memory used */
};

/**
 * @brief Multiline context for shell construct tracking
 *
 * Wraps the shared input_continuation.c parser to provide LLE-specific
 * multiline state tracking. This allows buffer management to understand
 * shell constructs (quotes, brackets, control structures) and determine
 * when input is complete or needs continuation.
 */
struct lle_multiline_context_t {
    /* Core parser state (delegates to input_continuation.c) */
    void *core_state; /* continuation_state_t wrapper */

    /* LLE-specific tracking */
    char *current_construct;       /* Current construct name */
    size_t construct_start_line;   /* Starting line number */
    size_t construct_start_offset; /* Starting byte offset */
    uint8_t nesting_level;         /* Nesting depth */

    /* Completion state */
    bool construct_complete;   /* Construct is complete */
    bool needs_continuation;   /* Needs continuation line */
    char *expected_terminator; /* Expected terminator string */

    /* Performance optimization */
    uint32_t cache_key; /* Cache key for results */
    bool cache_valid;   /* Cache validity flag */

    /* Memory management */
    lush_memory_pool_t *memory_pool; /* Memory pool */
};

/**
 * @brief Multiline manager for buffer-wide multiline analysis
 *
 * Manages multiline state for an entire buffer, analyzing lines to
 * determine shell construct boundaries and continuation requirements.
 */
struct lle_multiline_manager_t {
    /* Configuration */
    lush_memory_pool_t *memory_pool; /* Memory pool */

    /* Statistics */
    uint64_t analysis_count; /* Analysis operations */
    uint64_t line_updates;   /* Line updates */

    /* Performance monitoring */
    lle_performance_monitor_t *perf_monitor; /* Performance monitor */
};

/* ============================================================================
 * FUNCTION DECLARATIONS - PHASE 1: CORE BUFFER LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Create a new buffer
 *
 * Allocates and initializes a new buffer with the specified initial capacity.
 * All buffer fields are initialized to safe default values.
 *
 * @param buffer Pointer to receive created buffer
 * @param memory_pool Lush memory pool for allocations
 * @param initial_capacity Initial buffer capacity (0 = use default)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_create(lle_buffer_t **buffer,
                               lush_memory_pool_t *memory_pool,
                               size_t initial_capacity);

/**
 * @brief Destroy a buffer
 *
 * Frees all resources associated with the buffer including data array,
 * line structures, and any cached data.
 *
 * @param buffer Buffer to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_destroy(lle_buffer_t *buffer);

/**
 * @brief Initialize buffer to empty state
 *
 * Resets buffer to empty state while preserving allocated capacity.
 * Clears all content and resets all counters.
 *
 * @param buffer Buffer to clear
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_clear(lle_buffer_t *buffer);

/* ============================================================================
 * SECURE MODE FUNCTIONS (Spec 15 Minimal Secure Mode)
 * ============================================================================
 */

/**
 * @brief Enable secure mode for sensitive data
 *
 * Activates secure mode which:
 * - Locks buffer memory with mlock() to prevent swapping
 * - Marks buffer for secure wiping on destroy
 *
 * Use for buffers containing passwords or other sensitive data.
 *
 * @param buffer Buffer to protect
 * @return LLE_SUCCESS or error code
 * @note mlock may fail due to privileges - buffer remains usable
 */
lle_result_t lle_buffer_enable_secure_mode(lle_buffer_t *buffer);

/**
 * @brief Securely clear buffer contents
 *
 * Wipes buffer data using secure memory wipe that cannot be
 * optimized away by the compiler. Uses explicit_bzero if available,
 * falls back to volatile pointer trick.
 *
 * @param buffer Buffer to wipe
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_secure_clear(lle_buffer_t *buffer);

/**
 * @brief Disable secure mode
 *
 * Deactivates secure mode:
 * - Unlocks buffer memory with munlock()
 * - Clears secure mode flag
 *
 * @param buffer Buffer to unprotect
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_disable_secure_mode(lle_buffer_t *buffer);

/**
 * @brief Validate buffer integrity
 *
 * Performs complete buffer validation checking:
 * - Memory bounds
 * - UTF-8 validity
 * - Line structure consistency
 * - Checksum verification
 *
 * @param buffer Buffer to validate
 * @return LLE_SUCCESS if valid, error code if validation fails
 */
lle_result_t lle_buffer_validate(lle_buffer_t *buffer);

/* ============================================================================
 * FUNCTION DECLARATIONS - CHANGE TRACKING AND UNDO/REDO
 * ============================================================================
 */

/**
 * @brief Initialize change tracker
 *
 * @param tracker Pointer to receive initialized tracker
 * @param memory_pool Memory pool for allocations
 * @param max_undo_levels Maximum undo history (0 = use default)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_init(lle_change_tracker_t **tracker,
                                     lush_memory_pool_t *memory_pool,
                                     size_t max_undo_levels);

/**
 * @brief Destroy change tracker
 *
 * @param tracker Change tracker to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_destroy(lle_change_tracker_t *tracker);

/**
 * @brief Clear all change history
 *
 * @param tracker Change tracker to clear
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_clear(lle_change_tracker_t *tracker);

/**
 * @brief Begin a new change sequence
 *
 * @param tracker Change tracker
 * @param description Human-readable description
 * @param sequence Pointer to receive new sequence
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_change_tracker_begin_sequence(lle_change_tracker_t *tracker,
                                  const char *description,
                                  lle_change_sequence_t **sequence);

/**
 * @brief Complete current change sequence
 *
 * @param tracker Change tracker
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_change_tracker_complete_sequence(lle_change_tracker_t *tracker);

/**
 * @brief Begin tracking a buffer operation
 *
 * @param sequence Active sequence (NULL = create new sequence)
 * @param type Operation type
 * @param start_position Operation start position
 * @param length Operation length
 * @param operation Pointer to receive new operation
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_begin_operation(
    lle_change_sequence_t *sequence, lle_change_type_t type,
    size_t start_position, size_t length, lle_change_operation_t **operation);

/**
 * @brief Complete a buffer operation
 *
 * @param operation Operation to complete
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_change_tracker_complete_operation(lle_change_operation_t *operation);

/**
 * @brief Save deleted text for undo
 *
 * @param operation Operation to save text for
 * @param deleted_text Text that was deleted
 * @param deleted_length Length of deleted text
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_change_tracker_save_deleted_text(lle_change_operation_t *operation,
                                     const char *deleted_text,
                                     size_t deleted_length);

/**
 * @brief Save inserted text for undo
 *
 * @param operation Operation to save text for
 * @param inserted_text Text that was inserted
 * @param inserted_length Length of inserted text
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_change_tracker_save_inserted_text(lle_change_operation_t *operation,
                                      const char *inserted_text,
                                      size_t inserted_length);

/**
 * @brief Undo last operation sequence
 * Spec Reference: Spec 03, Line 1073-1151
 *
 * @param tracker Change tracker
 * @param buffer Buffer to apply undo to
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_undo(lle_change_tracker_t *tracker,
                                     lle_buffer_t *buffer);

/**
 * @brief Redo previously undone sequence
 *
 * @param tracker Change tracker
 * @param buffer Buffer to apply redo to
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_change_tracker_redo(lle_change_tracker_t *tracker,
                                     lle_buffer_t *buffer);

/**
 * @brief Check if undo is available
 *
 * @param tracker Change tracker
 * @return true if undo available, false otherwise
 */
bool lle_change_tracker_can_undo(const lle_change_tracker_t *tracker);

/**
 * @brief Check if redo is available
 *
 * @param tracker Change tracker
 * @return true if redo available, false otherwise
 */
bool lle_change_tracker_can_redo(const lle_change_tracker_t *tracker);

/**
 * @brief Get undo history depth
 *
 * @param tracker Change tracker
 * @return Number of undoable sequences
 */
size_t lle_change_tracker_undo_depth(const lle_change_tracker_t *tracker);

/**
 * @brief Get redo history depth
 *
 * @param tracker Change tracker
 * @return Number of redoable sequences
 */
size_t lle_change_tracker_redo_depth(const lle_change_tracker_t *tracker);

/**
 * @brief Get memory usage
 *
 * @param tracker Change tracker
 * @return Memory used in bytes
 */
size_t lle_change_tracker_memory_usage(const lle_change_tracker_t *tracker);

/* ============================================================================
 * FUNCTION DECLARATIONS - BUFFER OPERATIONS (ATOMIC)
 * ============================================================================
 */

/**
 * @brief Insert text into buffer (atomic operation)
 * Spec Reference: Spec 03, Line 382-487
 *
 * All buffer modifications are atomic and tracked for undo/redo.
 *
 * @param buffer Buffer to insert into
 * @param position Byte offset position
 * @param text Text to insert
 * @param text_length Length of text
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_insert_text(lle_buffer_t *buffer, size_t position,
                                    const char *text, size_t text_length);

/**
 * @brief Delete text from buffer (atomic operation)
 * Spec Reference: Spec 03, Line 489-594
 *
 * @param buffer Buffer to delete from
 * @param start_position Start byte offset
 * @param delete_length Number of bytes to delete
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_delete_text(lle_buffer_t *buffer, size_t start_position,
                                    size_t delete_length);

/**
 * @brief Replace text in buffer (atomic operation)
 *
 * @param buffer Buffer to modify
 * @param start_position Start byte offset
 * @param delete_length Number of bytes to delete
 * @param insert_text Text to insert
 * @param insert_length Length of text to insert
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_replace_text(lle_buffer_t *buffer,
                                     size_t start_position,
                                     size_t delete_length,
                                     const char *insert_text,
                                     size_t insert_length);

/* ============================================================================
 * FUNCTION DECLARATIONS - UTF-8 INDEX
 * ============================================================================
 */

/**
 * @brief Initialize UTF-8 index structure
 * Spec Reference: Spec 03, Section 4.2
 *
 * @param index Pointer to index structure to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_init(lle_utf8_index_t *index);

/**
 * @brief Clean up UTF-8 index structure
 *
 * @param index Pointer to index structure to clean up
 */
void lle_utf8_index_cleanup(lle_utf8_index_t *index);

/**
 * @brief Rebuild UTF-8 index from text
 * Spec Reference: Spec 03, Section 4.2
 *
 * Rebuilds all position mapping arrays for fast O(1) lookups.
 *
 * @param index UTF-8 index to rebuild
 * @param text Text to index
 * @param text_length Length of text in bytes
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_rebuild(lle_utf8_index_t *index, const char *text,
                                    size_t text_length);

/**
 * @brief Get codepoint index from byte offset
 *
 * O(1) lookup using index if available, O(n) scan if not.
 *
 * @param index UTF-8 index
 * @param byte_offset Byte offset in text
 * @param codepoint_index Pointer to receive codepoint index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_byte_to_codepoint(const lle_utf8_index_t *index,
                                              size_t byte_offset,
                                              size_t *codepoint_index);

/**
 * @brief Get byte offset from codepoint index
 *
 * O(1) lookup using index.
 *
 * @param index UTF-8 index
 * @param codepoint_index Codepoint index
 * @param byte_offset Pointer to receive byte offset
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_codepoint_to_byte(const lle_utf8_index_t *index,
                                              size_t codepoint_index,
                                              size_t *byte_offset);

/**
 * @brief Get grapheme index from codepoint index
 *
 * O(1) lookup using index.
 *
 * @param index UTF-8 index
 * @param codepoint_index Codepoint index
 * @param grapheme_index Pointer to receive grapheme index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_codepoint_to_grapheme(const lle_utf8_index_t *index,
                                                  size_t codepoint_index,
                                                  size_t *grapheme_index);

/**
 * @brief Get codepoint index from grapheme index
 *
 * O(1) lookup using index.
 *
 * @param index UTF-8 index
 * @param grapheme_index Grapheme cluster index
 * @param codepoint_index Pointer to receive codepoint index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_grapheme_to_codepoint(const lle_utf8_index_t *index,
                                                  size_t grapheme_index,
                                                  size_t *codepoint_index);

/**
 * @brief Convert grapheme cluster index to display column
 *
 * @param index UTF-8 index
 * @param grapheme_index Grapheme cluster index
 * @param display_column Output: display column
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_grapheme_to_display(const lle_utf8_index_t *index,
                                                size_t grapheme_index,
                                                size_t *display_column);

/**
 * @brief Convert display column to grapheme cluster index
 *
 * @param index UTF-8 index
 * @param display_column Display column
 * @param grapheme_index Output: grapheme cluster index
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_utf8_index_display_to_grapheme(const lle_utf8_index_t *index,
                                                size_t display_column,
                                                size_t *grapheme_index);

/**
 * @brief Invalidate index (mark as needing rebuild)
 *
 * @param index Index to invalidate
 */
void lle_utf8_index_invalidate(lle_utf8_index_t *index);

/**
 * @brief Check if index is valid
 *
 * @param index Index to check
 * @return true if valid, false if needs rebuild
 */
bool lle_utf8_index_is_valid(const lle_utf8_index_t *index);

/* ============================================================================
 * FUNCTION DECLARATIONS - BUFFER VALIDATOR
 * ============================================================================
 */

/**
 * @brief Initialize buffer validator
 * Spec Reference: Spec 03, Section 8
 *
 * @param validator Pointer to receive initialized validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validator_init(lle_buffer_validator_t **validator);

/**
 * @brief Destroy buffer validator
 *
 * @param validator Buffer validator to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validator_destroy(lle_buffer_validator_t *validator);

/**
 * @brief Validate complete buffer
 * Spec Reference: Spec 03, Section 8.1
 *
 * Performs comprehensive buffer validation including UTF-8 encoding,
 * line structure, cursor positions, and bounds checking.
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code indicating validation failure
 */
lle_result_t lle_buffer_validate_complete(lle_buffer_t *buffer,
                                          lle_buffer_validator_t *validator);

/**
 * @brief Validate UTF-8 encoding
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validate_utf8(lle_buffer_t *buffer,
                                      lle_buffer_validator_t *validator);

/**
 * @brief Validate line structure
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_buffer_validate_line_structure(lle_buffer_t *buffer,
                                   lle_buffer_validator_t *validator);

/**
 * @brief Validate cursor position
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_buffer_validate_cursor_position(lle_buffer_t *buffer,
                                    lle_buffer_validator_t *validator);

/**
 * @brief Validate buffer bounds
 *
 * Checks that all offsets and sizes are within valid ranges.
 *
 * @param buffer Buffer to validate
 * @param validator Buffer validator
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_buffer_validate_bounds(lle_buffer_t *buffer,
                                        lle_buffer_validator_t *validator);

/* ============================================================================
 * FUNCTION DECLARATIONS - CURSOR MANAGER
 * ============================================================================
 */

/**
 * @brief Initialize cursor manager
 * Spec Reference: Spec 03, Section 6
 *
 * @param manager Pointer to receive initialized cursor manager
 * @param buffer Associated buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_cursor_manager_init(lle_cursor_manager_t **manager,
                                     lle_buffer_t *buffer);

/**
 * @brief Destroy cursor manager
 *
 * @param manager Cursor manager to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_cursor_manager_destroy(lle_cursor_manager_t *manager);

/**
 * @brief Move cursor to specific byte offset
 * Spec Reference: Spec 03, Line 915-976
 *
 * Updates all cursor position fields (codepoint, grapheme, line, column,
 * visual).
 *
 * @param manager Cursor manager
 * @param byte_offset Target byte offset
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_cursor_manager_move_to_byte_offset(lle_cursor_manager_t *manager,
                                       size_t byte_offset);

/**
 * @brief Move cursor by grapheme clusters
 * Spec Reference: Spec 03, Line 975-1006
 *
 * @param manager Cursor manager
 * @param grapheme_delta Number of graphemes to move (positive or negative)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_cursor_manager_move_by_graphemes(lle_cursor_manager_t *manager,
                                                  int grapheme_delta);

/**
 * @brief Move cursor by codepoints
 *
 * @param manager Cursor manager
 * @param codepoint_delta Number of codepoints to move (positive or negative)
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_cursor_manager_move_by_codepoints(lle_cursor_manager_t *manager,
                                      int codepoint_delta);

/**
 * @brief Move cursor to start of line
 *
 * @param manager Cursor manager
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_cursor_manager_move_to_line_start(lle_cursor_manager_t *manager);

/**
 * @brief Move cursor to end of line
 *
 * @param manager Cursor manager
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_cursor_manager_move_to_line_end(lle_cursor_manager_t *manager);

/**
 * @brief Move cursor up/down by lines
 *
 * @param manager Cursor manager
 * @param line_delta Number of lines to move (positive or negative)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_cursor_manager_move_by_lines(lle_cursor_manager_t *manager,
                                              int line_delta);

/**
 * @brief Validate and correct cursor position
 *
 * Ensures cursor is at a valid position in the buffer.
 *
 * @param manager Cursor manager
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_cursor_manager_validate_and_correct(lle_cursor_manager_t *manager);

/**
 * @brief Get current cursor position
 *
 * @param manager Cursor manager
 * @param position Pointer to receive cursor position
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_cursor_manager_get_position(const lle_cursor_manager_t *manager,
                                lle_cursor_position_t *position);

/* ============================================================================
 * FUNCTION DECLARATIONS - MULTILINE MANAGER
 * ============================================================================
 */

/**
 * @brief Initialize multiline context
 *
 * Creates and initializes a multiline context for tracking shell constructs.
 * Allocates the core continuation_state_t parser and initializes LLE-specific
 * tracking fields.
 *
 * @param ctx Pointer to receive created context
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_context_init(lle_multiline_context_t **ctx,
                                        lush_memory_pool_t *memory_pool);

/**
 * @brief Destroy multiline context
 *
 * Frees all resources associated with a multiline context, including
 * the wrapped continuation_state_t and any allocated strings.
 *
 * @param ctx Context to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_context_destroy(lle_multiline_context_t *ctx);

/**
 * @brief Reset multiline context to initial state
 *
 * Clears all parsing state while preserving allocated structures.
 * Used when starting analysis of a new buffer or after completing
 * a multiline construct.
 *
 * @param ctx Context to reset
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_context_reset(lle_multiline_context_t *ctx);

/**
 * @brief Analyze a line for shell constructs
 *
 * Parses a line and updates the multiline context state. Delegates core
 * parsing to continuation_analyze_line() from input_continuation.c, then
 * extracts and caches LLE-specific state.
 *
 * @param ctx Multiline context
 * @param line Line content to analyze
 * @param length Line length in bytes
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_analyze_line(lle_multiline_context_t *ctx,
                                        const char *line, size_t length);

/**
 * @brief Check if current construct is complete
 *
 * Determines if all shell constructs are closed and input is complete.
 * Delegates to continuation_is_complete() from input_continuation.c.
 *
 * @param ctx Multiline context
 * @return true if complete, false if continuation needed
 */
bool lle_multiline_is_complete(const lle_multiline_context_t *ctx);

/**
 * @brief Check if continuation is needed
 *
 * Inverse of lle_multiline_is_complete() for convenience.
 *
 * @param ctx Multiline context
 * @return true if continuation needed, false if complete
 */
bool lle_multiline_needs_continuation(const lle_multiline_context_t *ctx);

/**
 * @brief Get continuation prompt string
 *
 * Returns appropriate prompt based on current parsing state.
 * Delegates to continuation_get_prompt() from input_continuation.c.
 *
 * @param ctx Multiline context
 * @return Prompt string (e.g., "> ", "quote> ", "if> ")
 */
const char *lle_multiline_get_prompt(const lle_multiline_context_t *ctx);

/**
 * @brief Get current construct name
 *
 * Returns a string describing the current multiline construct
 * (e.g., "if statement", "double quote", "function definition").
 *
 * @param ctx Multiline context
 * @return Construct name or NULL if none
 */
const char *lle_multiline_get_construct(const lle_multiline_context_t *ctx);

/**
 * @brief Initialize multiline manager
 *
 * Creates and initializes a multiline manager for buffer-wide analysis.
 *
 * @param manager Pointer to receive created manager
 * @param memory_pool Memory pool for allocations
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_manager_init(lle_multiline_manager_t **manager,
                                        lush_memory_pool_t *memory_pool);

/**
 * @brief Destroy multiline manager
 *
 * Frees all resources associated with a multiline manager.
 *
 * @param manager Manager to destroy
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_manager_destroy(lle_multiline_manager_t *manager);

/**
 * @brief Analyze entire buffer for multiline constructs
 *
 * Performs line-by-line analysis of the buffer, updating each line's
 * multiline state and determining overall buffer completion status.
 * Updates buffer->multiline_active and line flags.
 *
 * @param manager Multiline manager
 * @param buffer Buffer to analyze
 * @return LLE_SUCCESS or error code
 */
lle_result_t
lle_multiline_manager_analyze_buffer(lle_multiline_manager_t *manager,
                                     lle_buffer_t *buffer);

/**
 * @brief Update multiline state for a specific line
 *
 * Re-analyzes a single line and updates its multiline state.
 * Used after line edits to maintain accurate state.
 *
 * @param manager Multiline manager
 * @param buffer Buffer containing the line
 * @param line_index Index of line to update
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_multiline_manager_update_line_state(
    lle_multiline_manager_t *manager, lle_buffer_t *buffer, size_t line_index);

#endif /* LLE_BUFFER_MANAGEMENT_H */
