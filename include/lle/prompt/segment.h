/**
 * @file segment.h
 * @brief LLE Prompt Segment System - Type Definitions and Function Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 25 Section 5 - Segment Architecture
 * Version: 1.0.0
 *
 * Segments are the building blocks of prompts - modular, reusable components
 * that render specific pieces of information (directory, git status, etc.).
 *
 * Segments can be:
 * - Synchronous: Render immediately (directory, user, host)
 * - Asynchronous: Fetch data in background (git status)
 * - Cached: Reuse previous output until invalidated
 * - Conditional: Only visible when certain conditions are met
 */

#ifndef LLE_PROMPT_SEGMENT_H
#define LLE_PROMPT_SEGMENT_H

#include "lle/error_handling.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/** @brief Maximum segment name length */
#define LLE_SEGMENT_NAME_MAX 32

/** @brief Maximum segment description length */
#define LLE_SEGMENT_DESC_MAX 256

/** @brief Maximum segment output length */
#define LLE_SEGMENT_OUTPUT_MAX 512

/** @brief Maximum property name length */
#define LLE_SEGMENT_PROPERTY_MAX 32

/** @brief Maximum number of properties per segment */
#define LLE_SEGMENT_MAX_PROPERTIES 16

/** @brief Maximum number of segments in registry */
#define LLE_SEGMENT_REGISTRY_MAX 64

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Segment capability flags
 */
typedef enum lle_segment_capability {
    LLE_SEG_CAP_NONE = 0,
    LLE_SEG_CAP_ASYNC = (1 << 0),       /**< Requires async data fetch */
    LLE_SEG_CAP_CACHEABLE = (1 << 1),   /**< Output can be cached */
    LLE_SEG_CAP_EXPENSIVE = (1 << 2),   /**< May be slow, respect timeouts */
    LLE_SEG_CAP_THEME_AWARE = (1 << 3), /**< Uses theme colors */
    LLE_SEG_CAP_DYNAMIC = (1 << 4),     /**< Content changes frequently */
    LLE_SEG_CAP_OPTIONAL = (1 << 5),    /**< Can be hidden if no data */
    LLE_SEG_CAP_PROPERTIES = (1 << 6)   /**< Exposes sub-properties */
} lle_segment_capability_t;

/**
 * @brief Segment render result
 */
typedef struct lle_segment_output {
    char content[LLE_SEGMENT_OUTPUT_MAX]; /**< Rendered content with ANSI */
    size_t content_len;                   /**< Length in bytes */
    size_t visual_width;                  /**< Display width in columns */
    bool is_empty;                        /**< No content to display */
    bool needs_separator;                 /**< Should have separator after */
} lle_segment_output_t;

/**
 * @brief Prompt context passed to segments during rendering
 *
 * Contains all environmental information needed to render segments.
 */
typedef struct lle_prompt_context {
    /* Shell state */
    int last_exit_code;            /**< Exit code of last command */
    uint64_t last_cmd_duration_ms; /**< Duration of last command */
    int background_job_count;      /**< Number of background jobs */

    /* User information */
    char username[64];  /**< Current username */
    char hostname[256]; /**< Hostname */
    uid_t uid;          /**< User ID */
    bool is_root;       /**< Running as root */

    /* Directory information */
    char cwd[PATH_MAX];         /**< Current working directory */
    char cwd_display[PATH_MAX]; /**< Display version (~ for home) */
    char home_dir[PATH_MAX];    /**< Home directory path */
    bool cwd_is_home;           /**< CWD is home directory */
    bool cwd_is_root;           /**< CWD is root directory / */
    bool cwd_is_writable;       /**< CWD is writable */
    bool cwd_is_git_repo;       /**< CWD is in a git repo */

    /* Terminal information */
    int terminal_width;  /**< Terminal width in columns */
    int terminal_height; /**< Terminal height in rows */
    bool has_true_color; /**< Terminal supports true color */
    bool has_256_color;  /**< Terminal supports 256 colors */
    bool has_unicode;    /**< Terminal supports Unicode */

    /* Time information */
    time_t current_time;  /**< Current time */
    struct tm current_tm; /**< Current time broken down */

    /* Shell-specific */
    int shlvl;           /**< Shell nesting level */
    bool is_ssh_session; /**< Running in SSH session */

    /* Keymap state (for vi mode indicator) */
    char keymap[32]; /**< "viins", "vicmd", "emacs" */
} lle_prompt_context_t;

/* Forward declarations */
struct lle_prompt_segment;
struct lle_theme;

/**
 * @brief Segment lifecycle callback: initialize segment state
 *
 * @param self  Segment to initialize
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_segment_init_fn)(struct lle_prompt_segment *self);

/**
 * @brief Segment lifecycle callback: cleanup segment state
 *
 * @param self  Segment to cleanup
 */
typedef void (*lle_segment_cleanup_fn)(struct lle_prompt_segment *self);

/**
 * @brief Segment visibility check: is this segment enabled?
 *
 * @param self  Segment to check
 * @return true if enabled, false otherwise
 */
typedef bool (*lle_segment_is_enabled_fn)(
    const struct lle_prompt_segment *self);

/**
 * @brief Segment visibility check: should this segment be shown?
 *
 * @param self  Segment to check
 * @param ctx   Prompt context
 * @return true if visible, false otherwise
 */
typedef bool (*lle_segment_is_visible_fn)(const struct lle_prompt_segment *self,
                                          const lle_prompt_context_t *ctx);

/**
 * @brief Segment render callback: generate output
 *
 * @param self    Segment to render
 * @param ctx     Prompt context
 * @param theme   Theme for symbols/colors (can be NULL for defaults)
 * @param output  Output structure to fill
 * @return LLE_SUCCESS or error code
 */
typedef lle_result_t (*lle_segment_render_fn)(
    const struct lle_prompt_segment *self, const lle_prompt_context_t *ctx,
    const struct lle_theme *theme, lle_segment_output_t *output);

/**
 * @brief Segment property access: get property value
 *
 * @param self           Segment to query
 * @param property_name  Property to get
 * @return Property value or NULL if not found
 */
typedef const char *(*lle_segment_get_property_fn)(
    const struct lle_prompt_segment *self, const char *property_name);

/**
 * @brief Segment cache control: is cache still valid?
 *
 * @param self  Segment to check
 * @return true if cache is valid, false otherwise
 */
typedef bool (*lle_segment_cache_valid_fn)(
    const struct lle_prompt_segment *self);

/**
 * @brief Segment cache control: invalidate cache
 *
 * @param self  Segment to invalidate
 */
typedef void (*lle_segment_invalidate_fn)(struct lle_prompt_segment *self);

/**
 * @brief Complete segment definition
 */
typedef struct lle_prompt_segment {
    /* Identity */
    char name[LLE_SEGMENT_NAME_MAX];        /**< Segment name */
    char description[LLE_SEGMENT_DESC_MAX]; /**< Segment description */

    /* Capabilities */
    uint32_t capabilities; /**< lle_segment_capability_t flags */

    /* Lifecycle functions */
    lle_segment_init_fn init;       /**< Initialize segment */
    lle_segment_cleanup_fn cleanup; /**< Cleanup segment */

    /* Visibility */
    lle_segment_is_enabled_fn is_enabled; /**< Check if enabled */
    lle_segment_is_visible_fn is_visible; /**< Check if visible */

    /* Rendering */
    lle_segment_render_fn render; /**< Render segment */

    /* Property access */
    lle_segment_get_property_fn get_property; /**< Get property value */

    /* Cache control */
    lle_segment_cache_valid_fn is_cache_valid;  /**< Check cache validity */
    lle_segment_invalidate_fn invalidate_cache; /**< Invalidate cache */

    /* Segment-private state */
    void *state; /**< Private segment state */

    /* Statistics */
    uint64_t total_render_time_ns; /**< Total render time */
    uint64_t render_count;         /**< Number of renders */
    uint64_t cache_hit_count;      /**< Cache hit count */
} lle_prompt_segment_t;

/**
 * @brief Segment registry for managing registered segments
 */
typedef struct lle_segment_registry {
    lle_prompt_segment_t *segments[LLE_SEGMENT_REGISTRY_MAX]; /**< Segments */
    size_t count;     /**< Number of registered segments */
    bool initialized; /**< Registry is initialized */
} lle_segment_registry_t;

/* ============================================================================
 * SEGMENT REGISTRY API
 * ============================================================================
 */

/**
 * @brief Initialize the segment registry
 *
 * @param registry  Registry to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_segment_registry_init(lle_segment_registry_t *registry);

/**
 * @brief Cleanup the segment registry and all registered segments
 *
 * @param registry  Registry to cleanup
 */
void lle_segment_registry_cleanup(lle_segment_registry_t *registry);

/**
 * @brief Register a segment with the registry
 *
 * @param registry  Target registry
 * @param segment   Segment to register (ownership transferred)
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_segment_registry_register(lle_segment_registry_t *registry,
                                           lle_prompt_segment_t *segment);

/**
 * @brief Find a segment by name
 *
 * @param registry  Registry to search
 * @param name      Segment name
 * @return Pointer to segment or NULL if not found
 */
lle_prompt_segment_t *
lle_segment_registry_find(const lle_segment_registry_t *registry,
                          const char *name);

/**
 * @brief Get all registered segment names
 *
 * @param registry   Registry to query
 * @param names      Output array for segment names (caller provides)
 * @param max_names  Maximum number of names to return
 * @return Number of segments in registry
 */
size_t lle_segment_registry_list(const lle_segment_registry_t *registry,
                                 const char **names, size_t max_names);

/**
 * @brief Invalidate all segment caches
 *
 * Call this on directory change or other events that invalidate cached data.
 *
 * @param registry  Registry containing segments
 */
void lle_segment_registry_invalidate_all(lle_segment_registry_t *registry);

/* ============================================================================
 * PROMPT CONTEXT API
 * ============================================================================
 */

/**
 * @brief Initialize prompt context with current environment
 *
 * @param ctx  Context to initialize
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_prompt_context_init(lle_prompt_context_t *ctx);

/**
 * @brief Update prompt context after command execution
 *
 * @param ctx          Context to update
 * @param exit_code    Exit code of last command
 * @param duration_ms  Duration of last command in milliseconds
 */
void lle_prompt_context_update(lle_prompt_context_t *ctx, int exit_code,
                               uint64_t duration_ms);

/**
 * @brief Refresh directory information in context
 *
 * @param ctx  Context to refresh
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_prompt_context_refresh_directory(lle_prompt_context_t *ctx);

/**
 * @brief Set the background job count in the context
 *
 * @param ctx        Context to update
 * @param job_count  Number of active background jobs
 */
void lle_prompt_context_set_job_count(lle_prompt_context_t *ctx, int job_count);

/* ============================================================================
 * SEGMENT CREATION HELPERS
 * ============================================================================
 */

/**
 * @brief Create a new segment with the given name
 *
 * @param name         Segment name
 * @param description  Segment description
 * @param capabilities Capability flags
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create(const char *name,
                                         const char *description,
                                         uint32_t capabilities);

/**
 * @brief Free a segment and its state
 *
 * @param segment  Segment to free
 */
void lle_segment_free(lle_prompt_segment_t *segment);

/* ============================================================================
 * BUILT-IN SEGMENTS
 * ============================================================================
 */

/**
 * @brief Register all built-in segments with a registry
 *
 * @param registry  Target registry
 * @return Number of segments registered
 */
size_t lle_segment_register_builtins(lle_segment_registry_t *registry);

/**
 * @brief Create directory segment
 *
 * Shows current working directory with ~ substitution for home.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_directory(void);

/**
 * @brief Create git segment
 *
 * Shows git branch and status (staged, unstaged, ahead/behind).
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_git(void);

/**
 * @brief Create user segment
 *
 * Shows current username.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_user(void);

/**
 * @brief Create host segment
 *
 * Shows hostname.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_host(void);

/**
 * @brief Create time segment
 *
 * Shows current time in HH:MM:SS format.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_time(void);

/**
 * @brief Create status segment
 *
 * Shows exit code of last command if non-zero.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_status(void);

/**
 * @brief Create jobs segment
 *
 * Shows number of background jobs if any.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_jobs(void);

/**
 * @brief Create symbol segment
 *
 * Shows prompt symbol ($ for user, # for root).
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_symbol(void);

/**
 * @brief Create shell level segment
 *
 * Shows nesting depth when SHLVL > 1.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_shlvl(void);

/**
 * @brief Create SSH indicator segment
 *
 * Shows indicator when running in an SSH session.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_ssh(void);

/**
 * @brief Create command duration segment
 *
 * Shows last command's execution time when above threshold.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_cmd_duration(void);

/**
 * @brief Create virtual environment segment
 *
 * Shows Python virtual environment name from $VIRTUAL_ENV or $CONDA_DEFAULT_ENV.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_virtualenv(void);

/**
 * @brief Create container segment
 *
 * Shows container runtime when running inside Docker/Podman/LXC.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_container(void);

/**
 * @brief Create AWS profile segment
 *
 * Shows $AWS_PROFILE when set.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_aws(void);

/**
 * @brief Create Kubernetes context segment
 *
 * Shows current kubectl context from kubeconfig.
 *
 * @return New segment or NULL on error
 */
lle_prompt_segment_t *lle_segment_create_kubernetes(void);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_SEGMENT_H */
