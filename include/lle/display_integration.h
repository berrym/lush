/**
 * @file display_integration.h
 * @brief LLE Display Integration - Type Definitions (Layer 0)
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * This header defines the complete type system for Spec 08: Display
 * Integration. It provides the integration layer between LLE's internal
 * buffer/cursor system and Lush's existing layered display architecture.
 *
 * SPECIFICATION: docs/lle_specification/08_display_integration_complete.md
 * IMPLEMENTATION PLAN: docs/lle_implementation/SPEC_08_IMPLEMENTATION_PLAN.md
 *
 * LAYER 0: Type Definitions Only
 * - All structure definitions
 * - All enum definitions
 * - All typedef declarations
 * - Function declarations (implementations in Layer 1)
 *
 * ZERO-TOLERANCE COMPLIANCE:
 * - No stubs, no TODOs, no deferred work
 * - 100% spec-compliant type definitions
 * - Complete documentation for all types
 */

#ifndef LLE_DISPLAY_INTEGRATION_H
#define LLE_DISPLAY_INTEGRATION_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* LLE Core Dependencies */
#include "lle/arena.h"
#include "lle/buffer_management.h"
#include "lle/error_handling.h"
#include "lle/memory_management.h"
#include "lle/performance.h"

/* Lush Display System Dependencies */
#include "display/display_controller.h"

/* libhashtable Integration (Spec 05) - Use LLE wrapper */
#include "lle/hashtable.h"

/* LLE Event System - Use existing event types */
#include "lle/event_system.h"

/* LLE Terminal Abstraction - Use existing terminal types */
#include "lle/terminal_abstraction.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                         FORWARD DECLARATIONS                               */
/* ========================================================================== */

/* Core integration structures */
typedef struct lle_display_integration_t lle_display_integration_t;
typedef struct lle_display_bridge_t lle_display_bridge_t;
typedef struct lle_render_controller_t lle_render_controller_t;
typedef struct lle_event_coordinator_t lle_event_coordinator_t;

/* Rendering structures */
typedef struct lle_render_pipeline_t lle_render_pipeline_t;
typedef struct lle_buffer_renderer_t lle_buffer_renderer_t;
typedef struct lle_cursor_renderer_t lle_cursor_renderer_t;
typedef struct lle_render_output_t lle_render_output_t;
typedef struct lle_render_context_t lle_render_context_t;

/* Cache structures */
typedef struct lle_display_cache_t lle_display_cache_t;
typedef struct lle_render_cache_t lle_render_cache_t;
typedef struct lle_cached_entry_t lle_cached_entry_t;
typedef struct lle_cache_policy_t lle_display_cache_policy_t;

/* Event structures */
typedef struct lle_event_translator_t lle_event_translator_t;
typedef struct lle_event_router_t lle_event_router_t;
/* NOTE: lle_event_filter_t is defined in event_system.h */
typedef struct lle_coord_queue_t
    lle_coord_queue_t; /* Coordinator queue (not core event system) */

/* Memory structures */
typedef struct lle_display_memory_manager_t lle_display_memory_manager_t;
typedef struct lle_memory_usage_tracker_t lle_memory_usage_tracker_t;

/* Theme structures */
typedef struct lle_syntax_color_table_t lle_syntax_color_table_t;
typedef struct lle_cursor_colors_t lle_cursor_colors_t;

/* Terminal structures */
typedef struct lle_terminal_adapter_t lle_terminal_adapter_t;
/* NOTE: lle_terminal_capabilities_t is defined in terminal_abstraction.h */
typedef struct lle_compatibility_matrix_t lle_compatibility_matrix_t;

/* ========================================================================== */
/*                              ENUMERATIONS                                  */
/* ========================================================================== */

/**
 * @brief Display synchronization states
 *
 * Tracks the synchronization state between LLE buffer and display system.
 */
typedef enum {
    LLE_DISPLAY_SYNC_IDLE,        /**< No synchronization in progress */
    LLE_DISPLAY_SYNC_PENDING,     /**< Synchronization requested */
    LLE_DISPLAY_SYNC_IN_PROGRESS, /**< Synchronization executing */
    LLE_DISPLAY_SYNC_COMPLETE,    /**< Synchronization complete */
    LLE_DISPLAY_SYNC_FAILED       /**< Synchronization failed */
} lle_display_sync_state_t;

/**
 * @brief Rendering pipeline stage types
 *
 * Defines the different stages in the multi-stage rendering pipeline.
 */
typedef enum {
    LLE_RENDER_STAGE_PREPROCESSING, /**< Preprocessing stage */
    LLE_RENDER_STAGE_SYNTAX,        /**< Syntax highlighting stage */
    LLE_RENDER_STAGE_FORMATTING,    /**< Formatting stage */
    LLE_RENDER_STAGE_COMPOSITION    /**< Final composition stage */
} lle_render_stage_type_t;

/**
 * @brief Display event types
 *
 * Defines the types of display-specific events that flow through the
 * integration layer. Uses lle_display_event_type_t to avoid conflict with
 * memory_management.h event types.
 */
typedef enum {
    LLE_DISPLAY_EVENT_BUFFER_CHANGE,   /**< Buffer content changed */
    LLE_DISPLAY_EVENT_CURSOR_MOVE,     /**< Cursor position changed */
    LLE_DISPLAY_EVENT_THEME_CHANGE,    /**< Theme changed */
    LLE_DISPLAY_EVENT_TERMINAL_RESIZE, /**< Terminal size changed */
    LLE_DISPLAY_EVENT_DISPLAY_UPDATE,  /**< Display update requested */
    LLE_DISPLAY_EVENT_CACHE_INVALIDATE /**< Cache invalidation requested */
} lle_display_event_type_t;

/* NOTE: lle_terminal_type_t is defined in terminal_abstraction.h - using that
 * definition */

/* ========================================================================== */
/*                      DISPLAY BRIDGE STRUCTURES                             */
/* ========================================================================== */

/**
 * @brief Render request queue entry
 *
 * Represents a pending render request in the queue.
 */
typedef struct {
    uint64_t request_id;          /**< Unique request identifier */
    uint64_t timestamp;           /**< Request timestamp (microseconds) */
    bool force_full_render;       /**< Force complete redraw flag */
    lle_buffer_t *buffer;         /**< Buffer to render */
    lle_cursor_position_t cursor; /**< Cursor position */
} lle_render_request_t;

/**
 * @brief Render request queue
 *
 * Queue for managing pending render requests.
 */
struct lle_coord_queue_t {
    lle_render_request_t *requests; /**< Array of requests */
    size_t capacity;                /**< Queue capacity */
    size_t count;                   /**< Current request count */
    size_t head;                    /**< Queue head index */
    size_t tail;                    /**< Queue tail index */
    pthread_mutex_t lock;           /**< Thread safety lock */
};

/**
 * @brief Display difference tracker
 *
 * Tracks changes between display states for efficient updates.
 */
typedef struct {
    uint64_t last_buffer_hash; /**< Last buffer state hash */
    uint64_t last_cursor_hash; /**< Last cursor state hash */
    size_t dirty_start;        /**< Start of dirty region */
    size_t dirty_end;          /**< End of dirty region */
    bool full_redraw_needed;   /**< Full redraw required flag */
} lle_display_diff_t;

/**
 * @brief Display bridge structure
 *
 * Bridges LLE buffer system with Lush display layers.
 * Coordinates event flow and state synchronization.
 */
struct lle_display_bridge_t {
    /* LLE system connections */
    lle_buffer_t *active_buffer;       /**< Currently active editing buffer */
    lle_cursor_position_t *cursor_pos; /**< Current cursor position */
    void *lle_event_manager;           /**< LLE event system (opaque) */

    /* Lush display connections */
    void *command_layer; /**< Lush command display layer (opaque) */
    composition_engine_t *composition_engine; /**< Lush composition engine */
    layer_event_system_t *layer_events;       /**< Lush layer event system */
    display_controller_t
        *display_controller; /**< Display controller for terminal info */

    /* Bridge state management */
    lle_display_sync_state_t sync_state; /**< Synchronization state tracking */
    lle_coord_queue_t *render_queue;     /**< Pending render requests */
    lle_display_diff_t *diff_tracker;    /**< Display change tracking */

    /* Performance optimization */
    struct timespec last_render_time; /**< Last successful render timestamp */
    uint32_t render_skip_count;       /**< Consecutive skipped renders */
    bool force_full_render;           /**< Force complete redraw flag */

    /* Error handling */
    lle_error_context_t *error_context; /**< Bridge error context */
    uint32_t consecutive_errors;        /**< Error tracking for fallback */
};

/* ========================================================================== */
/*                      RENDERING SYSTEM STRUCTURES                           */
/* ========================================================================== */

/**
 * @brief Format attributes for rendered characters
 *
 * Visual formatting attributes applied during rendering.
 */
typedef struct {
    uint32_t foreground_color; /**< Foreground RGB color */
    uint32_t background_color; /**< Background RGB color */
    bool bold;                 /**< Bold text flag */
    bool italic;               /**< Italic text flag */
    bool underline;            /**< Underline flag */
    bool reverse;              /**< Reverse video flag */
} lle_format_attributes_t;

/**
 * @brief Render output structure
 *
 * Contains the rendered display content ready for display system.
 *
 * CRITICAL: Per MODERN_EDITOR_WRAPPING_RESEARCH.md, this structure now includes
 * the cursor screen coordinates calculated during rendering using incremental
 * tracking (like Replxx, Fish, ZLE). The display system should use these
 * coordinates directly, NOT recalculate them.
 */
struct lle_render_output_t {
    char *content;                       /**< Rendered content string */
    size_t content_length;               /**< Content length in bytes */
    size_t content_capacity;             /**< Allocated capacity */
    lle_format_attributes_t *attributes; /**< Format attributes per character */
    size_t attribute_count;              /**< Number of attributes */
    uint64_t render_hash;                /**< Hash of this render output */
    uint64_t timestamp;                  /**< Render timestamp (microseconds) */

    /* Cursor screen position (calculated during rendering) */
    size_t cursor_screen_row;    /**< Cursor row (0-based) on screen */
    size_t cursor_screen_column; /**< Cursor column (0-based) on screen */
    bool cursor_position_valid;  /**< True if cursor position was calculated */
};

/**
 * @brief Render context
 *
 * Context information passed through rendering pipeline stages.
 */
struct lle_render_context_t {
    lle_buffer_t *buffer;                   /**< Buffer being rendered */
    lle_cursor_position_t *cursor;          /**< Cursor position */
    lle_terminal_capabilities_t *term_caps; /**< Terminal capabilities */
    lle_syntax_color_table_t *color_table;  /**< Syntax color table */
    size_t terminal_width;                  /**< Terminal width in columns */
    size_t terminal_height;                 /**< Terminal height in rows */
    bool force_full_render;                 /**< Force full render flag */
    lle_memory_pool_t *memory_pool;         /**< Memory pool for allocations */
};

/**
 * @brief Rendering pipeline stage
 *
 * Individual stage in the multi-stage rendering pipeline.
 */
typedef struct {
    lle_render_stage_type_t type; /**< Stage type */
    const char *name;             /**< Stage name for debugging */
    lle_result_t (*execute)(
        struct lle_render_context_t *context,
        lle_render_output_t **output); /**< Execution function */
    bool enabled;                      /**< Stage enabled flag */
    uint64_t execution_count;          /**< Times executed */
    uint64_t total_execution_time_ns;  /**< Total execution time */
} lle_render_stage_t;

/**
 * @brief Rendering pipeline
 *
 * Multi-stage rendering pipeline for display content generation.
 */
struct lle_render_pipeline_t {
    lle_render_stage_t *stages;      /**< Array of pipeline stages */
    size_t stage_count;              /**< Number of stages */
    size_t stage_capacity;           /**< Stage array capacity */
    bool parallel_execution_enabled; /**< Parallel execution flag */
    pthread_mutex_t pipeline_lock;   /**< Thread safety lock */
    lle_memory_pool_t *memory_pool;  /**< Memory pool for pipeline */
};

/**
 * @brief Buffer renderer
 *
 * Handles rendering of buffer content to display format.
 */
struct lle_buffer_renderer_t {
    lle_render_pipeline_t *pipeline;       /**< Rendering pipeline */
    lle_syntax_color_table_t *color_table; /**< Syntax colors */
    size_t max_render_size;                /**< Maximum render size */
    lle_memory_pool_t *memory_pool;        /**< Memory pool */
};

/**
 * @brief Cursor renderer
 *
 * Handles rendering of cursor position and appearance.
 */
struct lle_cursor_renderer_t {
    lle_cursor_colors_t *colors;    /**< Cursor colors */
    bool cursor_visible;            /**< Cursor visibility */
    uint32_t cursor_style;          /**< Cursor style (block/line/underline) */
    lle_memory_pool_t *memory_pool; /**< Memory pool */
};

/**
 * @brief Frame scheduler
 *
 * Schedules and throttles rendering frames to prevent overload.
 */
typedef struct {
    uint64_t target_frame_time_us; /**< Target frame time (microseconds) */
    uint64_t last_frame_time;      /**< Last frame timestamp */
    uint32_t frames_rendered;      /**< Frames rendered counter */
    uint32_t frames_skipped;       /**< Frames skipped counter */
    bool throttling_enabled;       /**< Throttling enabled flag */
} lle_frame_scheduler_t;

/**
 * @brief Render metrics
 *
 * Performance metrics for rendering operations.
 */
typedef struct {
    uint64_t total_renders;              /**< Total renders performed */
    uint64_t partial_renders;            /**< Partial renders performed */
    uint64_t full_renders;               /**< Full renders performed */
    uint64_t cache_hits;                 /**< Cache hits */
    uint64_t cache_misses;               /**< Cache misses */
    uint64_t avg_render_time_ns;         /**< Average render time */
    uint64_t max_render_time_ns;         /**< Maximum render time */
    uint64_t min_render_time_ns;         /**< Minimum render time */
    uint64_t avg_partial_render_time_ns; /**< Average partial render time */
    uint64_t avg_full_render_time_ns;    /**< Average full render time */
} lle_render_metrics_t;

/**
 * @brief Render configuration
 *
 * Configuration settings for rendering system.
 */
typedef struct {
    bool syntax_highlighting_enabled; /**< Enable syntax highlighting */
    bool caching_enabled;             /**< Enable render caching */
    bool dirty_tracking_enabled;      /**< Enable dirty tracking */
    size_t max_cache_entries;         /**< Maximum cache entries */
    uint32_t cache_ttl_ms;            /**< Cache time-to-live */
} lle_render_config_t;

/**
 * @brief Render controller
 *
 * Coordinates all rendering operations with performance optimization.
 */
struct lle_render_controller_t {
    lle_display_bridge_t *bridge;             /**< Display bridge reference */
    lle_render_pipeline_t *pipeline;          /**< Rendering pipeline */
    lle_buffer_renderer_t *buffer_renderer;   /**< Buffer renderer */
    lle_cursor_renderer_t *cursor_renderer;   /**< Cursor renderer */
    lle_frame_scheduler_t *scheduler;         /**< Frame scheduler */
    lle_render_cache_t *cache;                /**< Render cache */
    lle_render_metrics_t *metrics;            /**< Rendering metrics */
    lle_render_config_t *config;              /**< Rendering configuration */
    lle_syntax_color_table_t *theme_colors;   /**< Theme syntax colors */
    lle_cursor_colors_t *cursor_theme_colors; /**< Theme cursor colors */
    lle_memory_pool_t *memory_pool;           /**< Memory pool for rendering */

    /* Frame arena for per-frame allocations.
     * Reset at start of each render to reclaim temporary buffers. */
    lle_arena_t *frame_arena;
};

/* ========================================================================== */
/*                        CACHE SYSTEM STRUCTURES                             */
/* ========================================================================== */

/**
 * @brief Cached entry
 *
 * Individual entry in the display cache.
 */
struct lle_cached_entry_t {
    char *cache_key;                 /**< Cache key string */
    void *data;                      /**< Cached data */
    size_t data_size;                /**< Data size in bytes */
    uint64_t timestamp;              /**< Entry creation timestamp */
    uint64_t last_access;            /**< Last access timestamp */
    uint32_t access_count;           /**< Access frequency counter */
    bool valid;                      /**< Entry validity flag */
    struct lle_cached_entry_t *next; /**< Next entry (for chaining) */
};

/**
 * @brief Cache policy (LRU)
 *
 * Least Recently Used cache eviction policy.
 */
struct lle_cache_policy_t {
    lle_cached_entry_t *lru_head; /**< LRU list head */
    lle_cached_entry_t *lru_tail; /**< LRU list tail */
    size_t max_entries;           /**< Maximum cache entries */
    uint32_t eviction_count;      /**< Eviction counter */
};

/**
 * @brief Cache metrics
 *
 * Performance metrics for cache system.
 */
typedef struct {
    uint64_t cache_hits;   /**< Cache hit count */
    uint64_t cache_misses; /**< Cache miss count */
    uint64_t evictions;    /**< Eviction count */
    double hit_rate;       /**< Cache hit rate percentage */
    size_t memory_usage;   /**< Cache memory usage bytes */
} lle_cache_metrics_t;

/**
 * @brief Display cache
 *
 * Main display caching system with LRU policy using libhashtable.
 */
struct lle_display_cache_t {
    ht_strblob_t *cache_table; /**< Binary-safe libhashtable for cache storage.
                                  Stores length-prefixed blobs so cached entries
                                  containing embedded NUL bytes (ANSI sequences,
                                  terminal control bytes) round-trip intact. */
    lle_display_cache_policy_t *policy; /**< Cache policy (LRU) */
    lle_cache_metrics_t *metrics;       /**< Cache metrics */
    pthread_rwlock_t cache_lock;        /**< Thread safety lock */
    lush_memory_pool_t *memory_pool;    /**< Lush memory pool for cache */
};

/**
 * @brief Render cache
 *
 * Specialized cache for rendered output.
 */
struct lle_render_cache_t {
    lle_display_cache_t *base_cache; /**< Base cache implementation */
    size_t max_render_size;          /**< Maximum cached render size */
    uint32_t cache_ttl_ms;           /**< Cache time-to-live */
};

/* ========================================================================== */
/*                      EVENT COORDINATION STRUCTURES                         */
/* ========================================================================== */

/* NOTE: lle_event_t is defined in event_system.h - using that definition */

/**
 * @brief Buffer change event
 *
 * Event triggered when buffer content changes.
 */
typedef struct {
    lle_display_event_type_t type; /**< Event type (BUFFER_CHANGE) */
    uint64_t timestamp;            /**< Event timestamp */
    lle_buffer_t *buffer;          /**< Buffer that changed */
    size_t change_offset;          /**< Change offset in buffer */
    size_t change_length;          /**< Length of change */
    bool insertion;                /**< True if insertion, false if deletion */
} lle_buffer_change_event_t;

/**
 * @brief Cursor move event
 *
 * Event triggered when cursor position changes.
 */
typedef struct {
    lle_display_event_type_t type;      /**< Event type (CURSOR_MOVE) */
    uint64_t timestamp;                 /**< Event timestamp */
    lle_cursor_position_t old_position; /**< Old cursor position */
    lle_cursor_position_t new_position; /**< New cursor position */
} lle_cursor_move_event_t;

/* NOTE: lle_event_handler_fn is defined in event_system.h - using that
 * definition */

/**
 * @brief Event route
 *
 * Maps event types to handlers.
 */
typedef struct {
    lle_display_event_type_t event_type; /**< Event type to route */
    lle_event_handler_fn handler;        /**< Handler function */
    void *user_data;                     /**< User data for handler */
} lle_event_route_t;

/**
 * @brief Event router
 *
 * Routes events to appropriate handlers.
 */
struct lle_event_router_t {
    lle_event_route_t *routes;   /**< Array of routes */
    size_t route_count;          /**< Number of routes */
    size_t route_capacity;       /**< Route array capacity */
    pthread_mutex_t router_lock; /**< Thread safety lock */
};

/**
 * @brief Event translator
 *
 * Translates between LLE and Lush event formats.
 */
struct lle_event_translator_t {
    void *lle_event_system; /**< LLE event system reference (opaque) */
    layer_event_system_t *lush_events; /**< Lush event system reference */
    lle_memory_pool_t *memory_pool;    /**< Memory pool for translations */
};

/**
 * @brief Event filter
 *
 * Filters events based on criteria.
 */
struct lle_event_filter_t {
    bool (*should_process)(lle_event_t *event); /**< Filter function */
    uint32_t events_filtered;                   /**< Filtered event count */
};

/**
 * @brief Event metrics
 *
 * Performance metrics for event system.
 */
typedef struct {
    uint64_t events_processed;       /**< Events processed */
    uint64_t events_filtered;        /**< Events filtered */
    uint64_t avg_processing_time_ns; /**< Average processing time */
} lle_event_metrics_t;

/**
 * @brief Event coordinator
 *
 * Coordinates event flow between LLE and Lush display systems.
 */
struct lle_event_coordinator_t {
    lle_event_translator_t *translator; /**< Event translator */
    lle_event_router_t *router;         /**< Event router */
    lle_event_filter_t *filter;         /**< Event filter */
    lle_coord_queue_t *queue;           /**< Coordinator queue */
    lle_event_metrics_t *metrics;       /**< Event metrics */
    pthread_mutex_t coordinator_lock;   /**< Thread safety lock */
    lle_memory_pool_t *memory_pool;     /**< Memory pool */
};

/* ========================================================================== */
/*                     MEMORY MANAGEMENT STRUCTURES                           */
/* ========================================================================== */

/**
 * @brief Memory usage tracker
 *
 * Tracks memory usage for display operations.
 */
struct lle_memory_usage_tracker_t {
    size_t current_usage;      /**< Current memory usage */
    size_t peak_usage;         /**< Peak memory usage */
    size_t allocation_count;   /**< Allocation count */
    size_t deallocation_count; /**< Deallocation count */
};

/**
 * @brief Allocation tracker
 *
 * Tracks individual memory allocations.
 */
typedef struct {
    void **allocations;         /**< Array of allocation pointers */
    size_t *allocation_sizes;   /**< Array of allocation sizes */
    size_t allocation_count;    /**< Number of tracked allocations */
    size_t allocation_capacity; /**< Allocation array capacity */
} lle_allocation_tracker_t;

/**
 * @brief Memory metrics
 *
 * Performance metrics for memory operations.
 */
typedef struct {
    uint64_t total_allocations;          /**< Total allocations */
    uint64_t total_deallocations;        /**< Total deallocations */
    size_t current_memory_usage;         /**< Current usage */
    size_t peak_memory_usage;            /**< Peak usage */
    uint32_t pool_defragmentation_count; /**< Defragmentation count */
} lle_memory_metrics_t;

/**
 * @brief Pool manager
 *
 * Manages multiple memory pools for display operations.
 */
typedef struct {
    lle_memory_pool_t **pools; /**< Array of memory pools */
    size_t pool_count;         /**< Number of pools */
    size_t active_pool_index;  /**< Currently active pool index */
} lle_pool_manager_t;

/**
 * @brief Memory policy
 *
 * Defines memory allocation policies.
 */
typedef struct {
    size_t max_allocation_size;    /**< Maximum single allocation */
    bool allow_external_alloc;     /**< Allow non-pool allocations */
    bool auto_defragment;          /**< Auto-defragment pools */
    uint32_t defragment_threshold; /**< Defragmentation threshold */
} lle_memory_policy_t;

/**
 * @brief Display memory manager
 *
 * Manages all memory operations for display integration.
 */
struct lle_display_memory_manager_t {
    lle_memory_pool_t *base_pool;              /**< Base memory pool */
    lle_pool_manager_t *pool_manager;          /**< Pool manager */
    lle_memory_usage_tracker_t *usage_tracker; /**< Usage tracker */
    lle_allocation_tracker_t *alloc_tracker;   /**< Allocation tracker */
    lle_memory_metrics_t *metrics;             /**< Memory metrics */
    lle_memory_policy_t *policy;               /**< Memory policy */
    pthread_mutex_t manager_lock;              /**< Thread safety lock */
};

/* ========================================================================== */
/*                       THEME INTEGRATION STRUCTURES                         */
/* ========================================================================== */

/**
 * @brief Syntax color table
 *
 * Color table for syntax highlighting.
 */
struct lle_syntax_color_table_t {
    uint32_t keyword_color;  /**< Keyword color */
    uint32_t string_color;   /**< String literal color */
    uint32_t comment_color;  /**< Comment color */
    uint32_t number_color;   /**< Number literal color */
    uint32_t operator_color; /**< Operator color */
    uint32_t variable_color; /**< Variable color */
    uint32_t function_color; /**< Function color */
    uint32_t type_color;     /**< Type color */
    uint32_t default_color;  /**< Default text color */
    void *theme_context;     /**< Theme context reference (opaque) */
};

/**
 * @brief Cursor colors
 *
 * Color configuration for cursor rendering.
 */
struct lle_cursor_colors_t {
    uint32_t cursor_color;            /**< Cursor color */
    uint32_t cursor_text_color;       /**< Text under cursor color */
    uint32_t cursor_background_color; /**< Cursor background color */
};

/* ========================================================================== */
/*                    TERMINAL COMPATIBILITY STRUCTURES                       */
/* ========================================================================== */

/**
 * @brief Terminal capabilities
 *
 * Detected terminal capabilities for compatibility.
 */
struct lle_terminal_capabilities_t {
    lle_terminal_type_t terminal_type; /**< Terminal type */
    bool supports_colors;              /**< Color support */
    bool supports_256_colors;          /**< 256-color support */
    bool supports_truecolor;           /**< True color (24-bit) support */
    bool supports_utf8;                /**< UTF-8 support */
    bool supports_cursor_movement;     /**< Cursor movement support */
    size_t terminal_width;             /**< Terminal width in columns */
    size_t terminal_height;            /**< Terminal height in rows */
};

/**
 * @brief Compatibility matrix
 *
 * Matrix of feature support across terminal types.
 */
struct lle_compatibility_matrix_t {
    bool feature_support[9][16]; /**< Feature support matrix [terminal][feature]
                                    - 9 terminal types */
    const char *feature_names[16]; /**< Feature name strings */
};

/**
 * @brief Terminal adapter
 *
 * Adapts display output for different terminal types.
 */
struct lle_terminal_adapter_t {
    lle_terminal_capabilities_t *capabilities; /**< Terminal capabilities */
    lle_compatibility_matrix_t *compat_matrix; /**< Compatibility matrix */
    display_controller_t
        *display_controller;        /**< Display controller reference */
    lle_memory_pool_t *memory_pool; /**< Memory pool */
};

/* ========================================================================== */
/*                      MAIN INTEGRATION STRUCTURE                            */
/* ========================================================================== */

/**
 * @brief Display configuration
 *
 * Configuration for display integration system.
 */
typedef struct {
    bool enable_syntax_highlighting;    /**< Enable syntax highlighting */
    bool enable_caching;                /**< Enable display caching */
    bool enable_performance_monitoring; /**< Enable performance monitoring */
    size_t max_cache_size;              /**< Maximum cache size */
    uint32_t render_throttle_ms;        /**< Render throttle milliseconds */
} lle_display_config_t;

/**
 * @brief Display state
 *
 * Current state of display integration.
 */
typedef struct {
    uint64_t last_update_time; /**< Last update timestamp */
    uint64_t update_count;     /**< Update counter */
    bool needs_full_redraw;    /**< Full redraw needed flag */
} lle_display_state_t;

/**
 * @brief Display metrics
 *
 * Performance metrics for display integration.
 */
typedef struct {
    uint64_t total_updates;      /**< Total display updates */
    uint64_t avg_update_time_us; /**< Average update time (microseconds) */
    uint64_t max_update_time_us; /**< Maximum update time */
    double cache_hit_rate;       /**< Cache hit rate percentage */
} lle_display_metrics_t;

/**
 * @brief Composition manager
 *
 * Manages display layer composition.
 */
typedef struct {
    display_controller_t *display_controller; /**< Display controller */
    composition_engine_t *compositor;         /**< Composition engine */
    lle_memory_pool_t *memory_pool;           /**< Memory pool */
} lle_composition_manager_t;

/**
 * @brief LLE Display Integration (Main Structure)
 *
 * Master coordinator for LLE display integration with Lush.
 * This is the top-level structure that manages all display integration
 * components and coordinates between LLE and Lush display systems.
 */
struct lle_display_integration_t {
    /* Core integration components */
    lle_display_bridge_t *display_bridge; /**< Bridge between LLE and display */
    lle_render_controller_t *render_controller; /**< Rendering coordinator */
    lle_display_cache_t *display_cache;         /**< Display caching system */
    lle_composition_manager_t *comp_manager;    /**< Composition manager */

    /* Lush system integration */
    display_controller_t *lush_display; /**< Existing Lush display controller */
    void *theme_system;                 /**< Existing theme system (opaque) */
    lle_memory_pool_t *memory_pool;     /**< Lush memory pool */

    /* Performance and coordination */
    lle_display_metrics_t *perf_metrics; /**< Display performance monitoring */
    lle_event_coordinator_t
        *event_coordinator;                   /**< Event system coordination */
    lle_terminal_adapter_t *terminal_adapter; /**< Terminal compatibility */

    /* Configuration and state */
    lle_display_config_t *config; /**< Display integration configuration */
    lle_display_state_t *current_state; /**< Current display state */
    void *render_cache_hashtable; /**< Render result caching (hash table) */

    /* Synchronization and safety */
    pthread_rwlock_t integration_lock; /**< Thread-safe access control */
    bool integration_active;           /**< Integration system status */
    uint64_t frame_counter;            /**< Display frame tracking */
    uint32_t api_version;              /**< Integration API version */
};

/* ========================================================================== */
/*                       FUNCTION DECLARATIONS                                */
/* ========================================================================== */

/* NOTE: Function implementations will be in Layer 1
 * (src/lle/display_integration.c) This header only declares the function
 * signatures per Layer 0 requirements.
 */

/* Core Integration Functions */
lle_display_integration_t *lle_display_integration_get_global(void);
lle_result_t
lle_display_integration_init(lle_display_integration_t **integration,
                             void *editor, display_controller_t *lush_display,
                             lle_memory_pool_t *memory_pool);
lle_result_t
lle_display_integration_cleanup(lle_display_integration_t *integration);

/* Display Bridge Functions */
lle_result_t lle_display_bridge_init(lle_display_bridge_t **bridge,
                                     void *editor,
                                     display_controller_t *display,
                                     lle_memory_pool_t *memory_pool);
lle_result_t lle_display_bridge_cleanup(lle_display_bridge_t *bridge);
lle_result_t lle_display_bridge_send_output(lle_display_bridge_t *bridge,
                                            lle_render_output_t *render_output,
                                            lle_cursor_position_t *cursor);
lle_result_t lle_display_create_bridge(lle_display_bridge_t **bridge,
                                       void *editor,
                                       display_controller_t *display,
                                       lle_memory_pool_t *pool);

/* Event Coordinator Functions */
lle_result_t lle_event_coordinator_init(lle_event_coordinator_t **coordinator,
                                        void *editor,
                                        display_controller_t *display,
                                        lle_memory_pool_t *memory_pool);
lle_result_t
lle_event_coordinator_cleanup(lle_event_coordinator_t *coordinator);
lle_result_t
lle_event_coordinator_process_event(lle_event_coordinator_t *coordinator,
                                    lle_event_t *event);

/* Render Controller Functions */
lle_result_t lle_render_controller_init(lle_render_controller_t **controller,
                                        lle_display_bridge_t *bridge,
                                        lle_memory_pool_t *memory_pool);
lle_result_t lle_render_controller_cleanup(lle_render_controller_t *controller);

/* Rendering Functions */
lle_result_t lle_render_buffer_content(lle_render_controller_t *controller,
                                       lle_buffer_t *buffer,
                                       lle_cursor_position_t *cursor,
                                       lle_render_output_t **output);
lle_result_t lle_render_cursor_position(lle_render_controller_t *controller,
                                        lle_cursor_position_t *cursor,
                                        char *output, size_t output_size,
                                        size_t *bytes_written);
lle_result_t lle_render_output_free(lle_render_output_t *output);

/* Pipeline Functions */
lle_result_t lle_render_pipeline_init(lle_render_pipeline_t **pipeline,
                                      lle_memory_pool_t *memory_pool);
lle_result_t lle_render_pipeline_cleanup(lle_render_pipeline_t *pipeline);
lle_result_t lle_render_pipeline_execute(lle_render_pipeline_t *pipeline,
                                         lle_render_context_t *context,
                                         lle_render_output_t **output);

/* Event Sub-Component Functions */
lle_result_t lle_event_translator_init(lle_event_translator_t **translator,
                                       lle_memory_pool_t *memory_pool);
lle_result_t lle_event_router_init(lle_event_router_t **router,
                                   lle_memory_pool_t *memory_pool);
lle_result_t lle_event_filter_init(lle_event_filter_t **filter,
                                   lle_memory_pool_t *memory_pool);
lle_result_t lle_coord_queue_init(lle_coord_queue_t **queue,
                                  lle_memory_pool_t *memory_pool);
lle_result_t lle_event_metrics_init(lle_event_metrics_t **metrics,
                                    lle_memory_pool_t *memory_pool);

/* Event Routing Functions */
lle_result_t lle_event_router_add_route(lle_event_router_t *router,
                                        lle_display_event_type_t event_type,
                                        lle_event_handler_fn handler);
bool lle_event_filter_should_process(lle_event_filter_t *filter,
                                     lle_event_t *event);

/* Event Handlers */
lle_result_t
lle_display_on_buffer_change(lle_display_integration_t *integration,
                             lle_buffer_change_event_t *event);
lle_result_t lle_display_on_cursor_move(lle_display_integration_t *integration,
                                        lle_cursor_move_event_t *event);

/* Cache Functions (libhashtable integration per Spec 05) */
lle_result_t lle_display_cache_init(lle_display_cache_t **cache,
                                    lle_memory_pool_t *memory_pool);
lle_result_t lle_display_cache_cleanup(lle_display_cache_t *cache);
lle_result_t lle_display_cache_store(lle_display_cache_t *cache, uint64_t key,
                                     const void *data, size_t data_size);
lle_result_t lle_display_cache_lookup(lle_display_cache_t *cache, uint64_t key,
                                      void **data, size_t *data_size);
lle_result_t lle_display_cache_invalidate(lle_display_cache_t *cache,
                                          uint64_t key);
lle_result_t lle_display_cache_invalidate_all(lle_display_cache_t *cache);
lle_result_t lle_render_cache_init(lle_render_cache_t **cache,
                                   lle_memory_pool_t *memory_pool);
lle_result_t lle_render_cache_cleanup(lle_render_cache_t *cache);
uint64_t lle_compute_cache_key(lle_buffer_t *buffer,
                               lle_cursor_position_t *cursor);

/* Terminal Adapter Functions */
lle_result_t lle_terminal_adapter_init(lle_terminal_adapter_t **adapter,
                                       display_controller_t *display_controller,
                                       lle_memory_pool_t *memory_pool);
lle_result_t lle_terminal_adapter_cleanup(lle_terminal_adapter_t *adapter);
const lle_terminal_capabilities_t *
lle_terminal_adapter_get_capabilities(const lle_terminal_adapter_t *adapter);
bool lle_terminal_adapter_supports_feature(
    const lle_terminal_adapter_t *adapter, const char *feature_name);

/* Additional function declarations will be added as implementation progresses
 */

#ifdef __cplusplus
}
#endif

#endif /* LLE_DISPLAY_INTEGRATION_H */
